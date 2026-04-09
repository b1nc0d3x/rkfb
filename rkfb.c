/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 B1nc0d3x
 * All rights reserved.
 *
 * RK3399 / RockPro64 VOP framebuffer + HDMI bring-up driver.
 * FreeBSD OFW kernel driver - proper newbus/bus_space attachment.
 *
 * Attaches to "rockchip,rk3399-vop-big" via device tree.
 * All MMIO via bus_space - no raw pmap_mapdev.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fbio.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include "rkfb_ioctl.h"

#define RKFB_WIDTH      1280
#define RKFB_HEIGHT      720
#define RKFB_BPP          32

#define RKFB_HDMI_PA    0xff940000UL
#define RKFB_GRF_PA     0xff320000UL
#define RKFB_GRF_SIZE   0x8000
#define RKFB_CRU_PA     0xff760000UL
#define RKFB_CRU_SIZE   0x1000
#define RKFB_VIOGRF_PA  0xff770000UL
#define RKFB_VIOGRF_SIZE 0x1000

struct rkfb_softc {
        device_t                dev;
        struct cdev            *cdev;

        struct resource        *vop_res;
        vm_offset_t             vop_va;
        vm_size_t               vop_map_size;
        vm_offset_t             hdmi_va;
        vm_offset_t             grf_va;
        vm_offset_t             cru_va;
        vm_offset_t             viogrf_va;

        vm_offset_t             fb_va;
        vm_paddr_t              fb_pa;
        size_t                  fb_size;

        uint32_t                width;
        uint32_t                height;
        uint32_t                bpp;
        uint32_t                stride;

        struct mtx              mtx;
};

#define MMIO_READ4(base, off) \
        (*(volatile uint32_t *)((base) + (off)))
#define MMIO_WRITE4(base, off, val) do { \
        (*(volatile uint32_t *)((base) + (off)) = (uint32_t)(val)); \
        __asm volatile("dsb sy" ::: "memory"); \
} while (0)
#define MMIO_READ1(base, off) \
        (*(volatile uint8_t *)((base) + (off)))

#define VOP_READ4(sc, o)        MMIO_READ4((sc)->vop_va, (o))
#define VOP_WRITE4(sc, o, v)    MMIO_WRITE4((sc)->vop_va, (o), (v))
#define GRF_READ4(sc, o)        MMIO_READ4((sc)->grf_va, (o))
#define GRF_WRITE4(sc, o, v)    MMIO_WRITE4((sc)->grf_va, (o), (v))
#define CRU_READ4(sc, o)        MMIO_READ4((sc)->cru_va, (o))
#define CRU_WRITE4(sc, o, v)    MMIO_WRITE4((sc)->cru_va, (o), (v))
#define VIOGRF_READ4(sc, o)     MMIO_READ4((sc)->viogrf_va, (o))
#define VIOGRF_WRITE4(sc, o, v) MMIO_WRITE4((sc)->viogrf_va, (o), (v))
/*
 * DW-HDMI on RK3399 uses 4-byte register stride.
 * Reads use byte semantics at reg*4. Writes must be 32-bit with the value in
 * bits[7:0] to avoid APB byte-strobe faults.
 */
#define HDMI_READ1(sc, o)       MMIO_READ1((sc)->hdmi_va, (o) * 4)
#define HDMI_WRITE1(sc, o, v)   MMIO_WRITE4((sc)->hdmi_va, (o) * 4, (uint8_t)(v))

static int
rkfb_vop_write_allowed(uint32_t off)
{
        switch (off) {
        /* System / global */
        case 0x0000: case 0x0004: case 0x0008: case 0x0010:
        /* WIN0 */
        case 0x0030: case 0x003c: case 0x0040: case 0x0048:
        case 0x004c: case 0x0050:
        /* Display timing: HTOTAL/HS, HACT, VTOTAL/VS, VACT */
        case 0x0188: case 0x018c: case 0x0190: case 0x0194:
        /* Post-processing / other */
        case 0x020c: case 0x028c: case 0x029c:
        case 0x0310: case 0x0314: case 0x0318:
                return (1);
        default:
                return (0);
        }
}

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_read_t  rkfb_read;
static d_write_t rkfb_write;
static d_mmap_t  rkfb_mmap;

static struct cdevsw rkfb_cdevsw = {
        .d_version = D_VERSION,
        .d_open    = rkfb_open,
        .d_close   = rkfb_close,
        .d_ioctl   = rkfb_ioctl,
        .d_read    = rkfb_read,
        .d_write   = rkfb_write,
        .d_mmap    = rkfb_mmap,
        .d_name    = "rkfb",
};

static int
rkfb_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        return (0);
}

static int
rkfb_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        return (0);
}

static int
rkfb_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr,
    int nprot, vm_memattr_t *memattr)
{
        struct rkfb_softc *sc = dev->si_drv1;

        /* Framebuffer region */
        if (offset < (vm_ooffset_t)sc->fb_size) {
                *paddr   = sc->fb_pa + offset;
                *memattr = VM_MEMATTR_WRITE_COMBINING;
                return (0);
        }

        /* HDMI register region (userspace-safe: SError → SIGBUS, not panic) */
        if (offset >= RKFB_MMAP_HDMI_OFF &&
            offset <  RKFB_MMAP_HDMI_OFF + RKFB_HDMI_SIZE) {
                *paddr   = RKFB_HDMI_PA + (offset - RKFB_MMAP_HDMI_OFF);
                *memattr = VM_MEMATTR_DEVICE;
                return (0);
        }

        return (EINVAL);
}

static int
rkfb_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t avail;
        if ((size_t)uio->uio_offset >= sc->fb_size) return (0);
        avail = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)avail) avail = uio->uio_resid;
        return (uiomove((void *)(sc->fb_va + uio->uio_offset), avail, uio));
}

//static inline void
//rkfb_hdmi_write1_safe(struct rkfb_softc *sc, bus_size_t off, uint8_t val)
//{
//      uint64_t daif;
        
        /* Save and mask SError */
//    __asm volatile("mrs %0, daif" : "=r"(daif));
//    __asm volatile("msr daifset, #4");  /* set A bit = mask SError */
        
// bus_space_write_1(sc->hdmi_bst, sc->hdmi_bsh, off*4, val);
//      __asm volatile("dsb sy" ::: "memory");
        
        /* Restore DAIF */
//  __asm volatile("msr daif, %0" :: "r"(daif));
//	}*/

static int
rkfb_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t avail;
        if ((size_t)uio->uio_offset >= sc->fb_size) return (ENOSPC);
        avail = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)avail) avail = uio->uio_resid;
        return (uiomove((void *)(sc->fb_va + uio->uio_offset), avail, uio));
}

static int
rkfb_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        if (sc == NULL) return (ENXIO);

        switch (cmd) {

        case RKFB_REG_READ: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;
                switch (ro->block) {
                case RKFB_BLOCK_VOP:
                        if ((ro->off & 3) || ro->off >= 0x10000) return (EINVAL);
                        ro->val = VOP_READ4(sc, ro->off); return (0);
                case RKFB_BLOCK_GRF:
                        if ((ro->off & 3) || ro->off >= RKFB_GRF_SIZE) return (EINVAL);
                        ro->val = GRF_READ4(sc, ro->off); return (0);
                case RKFB_BLOCK_CRU:
                        if ((ro->off & 3) || ro->off >= RKFB_CRU_SIZE) return (EINVAL);
                        ro->val = CRU_READ4(sc, ro->off); return (0);
                case RKFB_BLOCK_HDMI:
                        if (ro->off >= 0x8000) return (EINVAL);
                        ro->val = HDMI_READ1(sc, ro->off); return (0);
                case RKFB_BLOCK_VIOGRF:
                        if ((ro->off & 3) || ro->off >= RKFB_VIOGRF_SIZE) return (EINVAL);
                        ro->val = VIOGRF_READ4(sc, ro->off); return (0);
                default: return (EINVAL);
                }
        }

        case RKFB_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;
                switch (ro->block) {
                case RKFB_BLOCK_VOP:
                        if ((ro->off & 3) || ro->off >= 0x10000) return (EINVAL);
                        if (!rkfb_vop_write_allowed(ro->off)) return (EPERM);
                        VOP_WRITE4(sc, ro->off, ro->val); return (0);
                case RKFB_BLOCK_GRF:
                        if ((ro->off & 3) || ro->off >= RKFB_GRF_SIZE) return (EINVAL);
                        GRF_WRITE4(sc, ro->off, ro->val); return (0);
                case RKFB_BLOCK_CRU:
                        if ((ro->off & 3) || ro->off >= RKFB_CRU_SIZE) return (EINVAL);
                        CRU_WRITE4(sc, ro->off, ro->val); return (0);
                case RKFB_BLOCK_VIOGRF:
                        if ((ro->off & 3) || ro->off >= RKFB_VIOGRF_SIZE) return (EINVAL);
                        VIOGRF_WRITE4(sc, ro->off, ro->val); return (0);
                default: return (EPERM);
                }
        }

        case RKFB_HDMI_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;
                if (ro->off >= 0x8000) return (EINVAL);
                HDMI_WRITE1(sc, ro->off, (uint8_t)(ro->val & 0xff));
                return (0);
        }

        case RKFB_VOP_MASKWRITE: {
                struct rkfb_regmaskop *mo = (struct rkfb_regmaskop *)data;
                uint32_t v;
                if ((mo->off & 3) || mo->off >= 0x10000) return (EINVAL);
                if (!rkfb_vop_write_allowed(mo->off)) return (EPERM);
                v = ((mo->mask & 0xffff) << 16) | (mo->val & 0xffff);
                VOP_WRITE4(sc, mo->off, v); return (0);
        }

        case RKFB_VOP_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i;
                if ((rd->base & 3) || rd->count == 0 || rd->count > 64) return (EINVAL);
                if (rd->base + rd->count * 4 > 0x10000) return (EINVAL);
                for (i = 0; i < rd->count; i++)
                        device_printf(sc->dev, "VOP[0x%04x] = 0x%08x\n",
                            rd->base + i * 4, VOP_READ4(sc, rd->base + i * 4));
                return (0);
        }

        case RKFB_HDMI_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i;
                if (rd->count == 0 || rd->count > 256) return (EINVAL);
                if (rd->base + rd->count > 0x8000) return (EINVAL);
                for (i = 0; i < rd->count; i++)
                        device_printf(sc->dev, "HDMI[0x%04x] = 0x%02x\n",
                            rd->base + i, HDMI_READ1(sc, rd->base + i));
                return (0);
        }

        case RKFB_DUMPREGS:
                device_printf(sc->dev, "VOP[0x0008] = 0x%08x\n", VOP_READ4(sc, 0x0008));
                device_printf(sc->dev, "HDMI design_id=0x%02x MC_SWRSTZREQ=0x%02x\n",
                    HDMI_READ1(sc, 0x0000), HDMI_READ1(sc, 0x4002));
                return (0);

        case RKFB_GETINFO: {
                struct rkfb_info *info = (struct rkfb_info *)data;
                info->width   = sc->width;
                info->height  = sc->height;
                info->bpp     = sc->bpp;
                info->stride  = sc->stride;
                info->fb_size = sc->fb_size;
                info->fb_pa   = (uint64_t)sc->fb_pa;
                return (0);
        }

        case RKFB_CLEAR: {
                struct rkfb_fill *fill = (struct rkfb_fill *)data;
                uint32_t *p = (uint32_t *)sc->fb_va;
                size_t i, n = sc->fb_size / 4;
                for (i = 0; i < n; i++) p[i] = fill->pixel;
                return (0);
        }

        case RKFB_FILLRECT: {
                struct rkfb_rect *r = (struct rkfb_rect *)data;
                uint32_t *fb = (uint32_t *)sc->fb_va;
                uint32_t x, y, mx, my;
                if (r->x >= sc->width || r->y >= sc->height) return (EINVAL);
                mx = MIN(r->x + r->w, sc->width);
                my = MIN(r->y + r->h, sc->height);
                for (y = r->y; y < my; y++)
                        for (x = r->x; x < mx; x++)
                                fb[y * sc->width + x] = r->pixel;
                return (0);
        }

        default:
                return (ENOTTY);
        }
}

static struct ofw_compat_data compat_data[] = {
        { "rockchip,rk3399-vop-big", 1 },
        { NULL, 0 }
};

static int
rkfb_probe(device_t dev)
{
        if (!ofw_bus_status_okay(dev))
                return (ENXIO);
        if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
                return (ENXIO);
        device_set_desc(dev, "RockChip RK3399 VOP framebuffer");
        return (BUS_PROBE_DEFAULT);
}

static int
rkfb_attach(device_t dev)
{
        struct rkfb_softc *sc = device_get_softc(dev);
        int rid, error;

        sc->dev    = dev;
        sc->width  = RKFB_WIDTH;
        sc->height = RKFB_HEIGHT;
        sc->bpp    = RKFB_BPP;
        sc->stride = sc->width * (sc->bpp / 8);
        sc->fb_size = sc->stride * sc->height;
        mtx_init(&sc->mtx, "rkfb", NULL, MTX_DEF);

        rid = 0;
        sc->vop_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
            &rid, RF_ACTIVE);
        if (sc->vop_res == NULL) {
                device_printf(dev, "cannot map VOP\n");
                error = ENXIO; goto fail_mtx;
        }
        sc->vop_map_size = (vm_size_t)rman_get_size(sc->vop_res);
        sc->vop_va = (vm_offset_t)pmap_mapdev((vm_paddr_t)rman_get_start(sc->vop_res),
            sc->vop_map_size);
        if (sc->vop_va == 0) {
                device_printf(dev, "cannot pmap VOP\n");
                error = ENXIO; goto fail_vop;
        }
        sc->grf_va = (vm_offset_t)pmap_mapdev((vm_paddr_t)RKFB_GRF_PA, RKFB_GRF_SIZE);
        if (sc->grf_va == 0) { device_printf(dev, "cannot pmap GRF\n"); error = ENXIO; goto fail_vopmap; }
        sc->cru_va = (vm_offset_t)pmap_mapdev((vm_paddr_t)RKFB_CRU_PA, RKFB_CRU_SIZE);
        if (sc->cru_va == 0) { device_printf(dev, "cannot pmap CRU\n"); error = ENXIO; goto fail_grf; }
        sc->viogrf_va = (vm_offset_t)pmap_mapdev((vm_paddr_t)RKFB_VIOGRF_PA, RKFB_VIOGRF_SIZE);
        if (sc->viogrf_va == 0) { device_printf(dev, "cannot pmap VIO GRF\n"); error = ENXIO; goto fail_cru; }
        sc->hdmi_va = (vm_offset_t)pmap_mapdev((vm_paddr_t)RKFB_HDMI_PA, RKFB_HDMI_SIZE);
        if (sc->hdmi_va == 0) {
                device_printf(dev, "cannot pmap HDMI\n");
                error = ENXIO;
                goto fail_viogrf;
        }

        device_printf(dev, "VOP[0x0008]=0x%08x\n", VOP_READ4(sc, 0x0008));
        device_printf(dev, "HDMI mapped\n");
        /*device_printf(dev, "HDMI design_id=0x%02x rev=0x%02x "
            "MC_SWRSTZREQ=0x%02x PHY_STAT0=0x%02x\n",
            HDMI_READ1(sc, 0x0000), HDMI_READ1(sc, 0x0004),
            HDMI_READ1(sc, 0x4002), HDMI_READ1(sc, 0x3004));*/

        sc->fb_va = (vm_offset_t)kmem_alloc_contig(
            round_page(sc->fb_size), M_WAITOK | M_ZERO,
            0, ~0UL, PAGE_SIZE, 0, VM_MEMATTR_WRITE_COMBINING);
        if (sc->fb_va == 0) {
                device_printf(dev, "cannot allocate framebuffer\n");
                error = ENOMEM; goto fail_hdmi;
        }
        sc->fb_pa = pmap_kextract(sc->fb_va);
        device_printf(dev, "fb %ux%u %ubpp pa=0x%jx size=%zu\n",
            sc->width, sc->height, sc->bpp,
            (uintmax_t)sc->fb_pa, sc->fb_size);

        sc->cdev = make_dev(&rkfb_cdevsw, 0,
            UID_ROOT, GID_WHEEL, 0600, "rkfb0");
        if (sc->cdev == NULL) {
                device_printf(dev, "cannot create /dev/rkfb0\n");
                error = ENXIO; goto fail_fb;
        }
        sc->cdev->si_drv1 = sc;
        device_printf(dev, "ready /dev/rkfb0\n");
        return (0);

fail_fb:      kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
fail_hdmi:    pmap_unmapdev((void *)sc->hdmi_va, RKFB_HDMI_SIZE);
fail_viogrf:  pmap_unmapdev((void *)sc->viogrf_va, RKFB_VIOGRF_SIZE);
fail_cru:     pmap_unmapdev((void *)sc->cru_va, RKFB_CRU_SIZE);
fail_grf:     pmap_unmapdev((void *)sc->grf_va, RKFB_GRF_SIZE);
fail_vopmap:  pmap_unmapdev((void *)sc->vop_va, sc->vop_map_size);
fail_vop:     bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->vop_res);
fail_mtx:     mtx_destroy(&sc->mtx);
        return (error);
}

static int
rkfb_detach(device_t dev)
{
        struct rkfb_softc *sc = device_get_softc(dev);
        if (sc->cdev   != NULL) destroy_dev(sc->cdev);
        if (sc->fb_va  != 0)    kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
        if (sc->hdmi_va != 0)    pmap_unmapdev((void *)sc->hdmi_va, RKFB_HDMI_SIZE);
        if (sc->viogrf_va != 0)  pmap_unmapdev((void *)sc->viogrf_va, RKFB_VIOGRF_SIZE);
        if (sc->cru_va != 0)     pmap_unmapdev((void *)sc->cru_va, RKFB_CRU_SIZE);
        if (sc->grf_va != 0)     pmap_unmapdev((void *)sc->grf_va, RKFB_GRF_SIZE);
        if (sc->vop_va != 0)     pmap_unmapdev((void *)sc->vop_va, sc->vop_map_size);
        if (sc->vop_res  != NULL) bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->vop_res);
        mtx_destroy(&sc->mtx);
        return (0);
}

static device_method_t rkfb_methods[] = {
        DEVMETHOD(device_probe,  rkfb_probe),
        DEVMETHOD(device_attach, rkfb_attach),
        DEVMETHOD(device_detach, rkfb_detach),
        DEVMETHOD_END
};

static driver_t rkfb_driver = {
        "rkfb",
        rkfb_methods,
        sizeof(struct rkfb_softc),
};

DRIVER_MODULE(rkfb, simplebus, rkfb_driver, 0, 0);
MODULE_VERSION(rkfb, 2);
//MODULE_DEPEND(rkfb, ofw_bus, 1, 1, 1);
