
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Kyle T. Crenshaw
 * All rights reserved.
 *
 * RK3399 / RockPro64 framebuffer + display bring-up driver.
 * FreeBSD kernel module.
 *
 * Mapped blocks:
 *   VOP B   0xff900000  0x10000   32-bit registers
 *   GRF     0xff320000  0x8000    32-bit registers (expanded from 0x1000)
 *   CRU     0xff760000  0x1000    32-bit registers
 *   HDMI    0xff940000  0x10000   byte-wide registers (DW-HDMI)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>
#include <sys/errno.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <sys/fbio.h>
#include "rkfb_ioctl.h"

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define RKFB_WIDTH      1280
#define RKFB_HEIGHT      720
#define RKFB_BPP          32

#define HDMI_PHY_I2C_ADDR  0x69

/* -------------------------------------------------------------------------
 * Softc
 * ---------------------------------------------------------------------- */

struct rkfb_softc {
        struct cdev    *cdev;

        /* Framebuffer */
        vm_offset_t     fb_va;
        vm_paddr_t      fb_pa;
        size_t          fb_size;

        /* VOP B */
        vm_offset_t     vop_va;
        vm_paddr_t      vop_pa;
        size_t          vop_size;

        /* HDMI (DW-HDMI, byte-wide registers) */
        vm_offset_t     hdmi_va;
        vm_paddr_t      hdmi_pa;
        size_t          hdmi_size;

        /* GRF */
        vm_offset_t     grf_va;
        vm_paddr_t      grf_pa;
        size_t          grf_size;

        /* CRU */
        vm_offset_t     cru_va;
        vm_paddr_t      cru_pa;
        size_t          cru_size;

        uint32_t        width;
        uint32_t        height;
        uint32_t        bpp;
        uint32_t        stride;

        /* PMU */
       vm_offset_t     pmu_va;
       vm_paddr_t      pmu_pa;
       size_t          pmu_size;
  
        struct mtx      mtx;
};

/* Minimal vt_device declaration — avoids pulling in vt.h and its deps */
struct vt_device {
  const void      *vd_driver;     /* struct vt_driver * */
  void            *vd_softc;      /* fb_info * for efifb */
  /* we only need vd_softc, rest is opaque */
  /* pad to avoid touching beyond vd_softc */
};




static struct rkfb_softc g_rkfb_sc;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static void rkfb_hdmi_phy_init(struct rkfb_softc *sc);
static int  rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc,
                uint8_t reg, uint16_t val);

/* -------------------------------------------------------------------------
 * VOP accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_vop_read4(struct rkfb_softc *sc, size_t off)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->vop_va + off);
        return (*reg);
}

static inline void
rkfb_vop_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->vop_va + off);
        *reg = val;
}

static int
rkfb_vop_write_allowed(uint32_t off)
{
  switch (off) {
        case 0x0000:  /* REG_CFG_DONE */
        case 0x0008:  /* SYS_CTRL     */
        case 0x0030:  /* WIN0_CTRL0   */
        case 0x003c:  /* WIN0_VIR     */
        case 0x0040:  /* WIN0_YRGB_MST */
        case 0x0048:  /* WIN0_ACT_INFO */
        case 0x004c:  /* WIN0_DSP_INFO */
        case 0x0050:  /* WIN0_DSP_ST   */
        case 0x020c:
        case 0x028c:
        case 0x029c:
        case 0x0310:
        case 0x0314:
        case 0x0318:
                return (1);
        default:
                return (0);
        }
}

/* -------------------------------------------------------------------------
 * GRF accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_grf_read4(struct rkfb_softc *sc, size_t off)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->grf_va + off);
        return (*reg);
}

static inline void
rkfb_grf_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->grf_va + off);
        *reg = val;
}

/* -------------------------------------------------------------------------
 * CRU accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_cru_read4(struct rkfb_softc *sc, size_t off)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->cru_va + off);
        return (*reg);
}

static inline void
rkfb_cru_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->cru_va + off);
        *reg = val;
}

/* -------------------------------------------------------------------------
 * HDMI accessors (byte-wide — DW-HDMI is a byte-register IP)
 * ---------------------------------------------------------------------- */

static inline uint8_t
rkfb_hdmi_read1(struct rkfb_softc *sc, size_t off)
{
        volatile uint8_t *reg;
        reg = (volatile uint8_t *)(sc->hdmi_va + off);
        return (*reg);
}

static inline void
rkfb_hdmi_write1(struct rkfb_softc *sc, size_t off, uint8_t val)
{
        volatile uint8_t *reg;
        reg = (volatile uint8_t *)(sc->hdmi_va + off);
        *reg = val;
}


static inline uint32_t
rkfb_pmu_read4(struct rkfb_softc *sc, size_t off)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->pmu_va + off);
        return (*reg);
}

/* -------------------------------------------------------------------------
 * PHY I2C master — write one 16-bit value to Innosilicon PHY register
 * ---------------------------------------------------------------------- */

static int
rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc, uint8_t reg, uint16_t val)
{
        uint8_t stat;
        int timeout;

        rkfb_hdmi_write1(sc, 0x3020, HDMI_PHY_I2C_ADDR);
        rkfb_hdmi_write1(sc, 0x3021, reg);
        rkfb_hdmi_write1(sc, 0x3022, (val >> 8) & 0xff);  /* MSB */
        rkfb_hdmi_write1(sc, 0x3023, val & 0xff);          /* LSB */
        rkfb_hdmi_write1(sc, 0x3026, 0x10);                /* trigger write */

        for (timeout = 10; timeout > 0; timeout--) {
                DELAY(1000);
                stat = rkfb_hdmi_read1(sc, 0x3027);  /* PHY_I2CM_INT */
                if (stat & 0x02) {
                        rkfb_hdmi_write1(sc, 0x3027, 0x02);  /* clear done */
                        return (0);
                }
                if (stat & 0x08) {
                        rkfb_hdmi_write1(sc, 0x3027, 0x08);  /* clear error */
                        printf("rkfb: PHY I2C write error reg=0x%02x\n", reg);
                        return (EIO);
                }
        }
        printf("rkfb: PHY I2C write timeout reg=0x%02x\n", reg);
        return (ETIMEDOUT);
}

/* -------------------------------------------------------------------------
 * HDMI PHY init sequence
 *
 * Currently COMMENTED OUT at the call site — the PHY block at 0x3000+
 * is causing SError (AXI bus abort) on access, likely because the HDCP
 * power domain or a required clock is not enabled.  The function is kept
 * here for reference as we work toward enabling the right clocks first.
 * ---------------------------------------------------------------------- */

static void
rkfb_hdmi_phy_init(struct rkfb_softc *sc)
{
        uint8_t stat;
        int timeout;

        printf("rkfb: starting HDMI PHY init\n");

        /*
         * Enable HDCP/HDMI clocks at CRU.
         * CRU uses hiword-update format: bits[31:16] = mask, bits[15:0] = value.
         * Writing 0 to a gate bit enables the clock.
         *
         * CRU_CLKGATE20 [0x0250]:
         *   bit 12 = pclk_hdmi_ctrl
         * CRU_CLKGATE21 [0x0254]:
         *   bit  8 = hdmi_cec_clk
         * CRU_CLKGATE16 [0x0240]:
         *   bit  9 = aclk_hdcp
         *   bit 10 = hclk_hdcp
         * CRU_CLKGATE17 [0x0244]:
         *   bit  2 = pclk_hdcp
         */
        rkfb_cru_write4(sc, 0x0240,
            (1u << 25) | (1u << 26) | (0 << 9) | (0 << 10));
        rkfb_cru_write4(sc, 0x0244,
            (1u << 18) | (0 << 2));
        rkfb_cru_write4(sc, 0x0250,
            (1u << 28) | (0 << 12));
        rkfb_cru_write4(sc, 0x0254,
            (1u << 24) | (0 <<  8));
        DELAY(10000);

        printf("rkfb: HDMI/HDCP clocks enabled\n");

        /*
         * Configure PHY I2C master clock.
         * Reference = 4.8 MHz (from CRU_CLKSEL49 = 0x00001202).
         * Target SCL = 100 kHz.
         * DIV  = (4800000 / (2 * 100000)) - 1 = 23 = 0x17
         * HCNT = LCNT = 24 = 0x18
         */
        rkfb_hdmi_write1(sc, 0x3029, 0x17);  /* PHY_I2CM_DIV       */
        rkfb_hdmi_write1(sc, 0x302b, 0x00);  /* SS_SCL_HCNT_1      */
        rkfb_hdmi_write1(sc, 0x302c, 0x18);  /* SS_SCL_HCNT_0      */
        rkfb_hdmi_write1(sc, 0x302d, 0x00);  /* SS_SCL_LCNT_1      */
        rkfb_hdmi_write1(sc, 0x302e, 0x18);  /* SS_SCL_LCNT_0      */

        printf("rkfb: PHY I2C master clock configured\n");

        /*
         * Enable PHY interface — ENTMDS=1(bit6), SELDATAENPOL=1(bit1).
         * Keep PDZ=0 (powered down) until PHY registers are configured.
         */
        rkfb_hdmi_write1(sc, 0x3000, 0x42);
        DELAY(5000);

        printf("rkfb: PHY_CONF0 set\n");

        /*
         * Configure Innosilicon PHY via I2C for 1080p60 (148.5 MHz).
         * Values from RK3399 TRM / Linux dw_hdmi-rockchip phy tables.
         */
        rkfb_hdmi_phy_i2c_write(sc, 0x06, 0x0008);  /* CPCE_CTRL  */
        rkfb_hdmi_phy_i2c_write(sc, 0x15, 0x0000);  /* GMPCTRL    */
        rkfb_hdmi_phy_i2c_write(sc, 0x10, 0x01b5);  /* TXTERM     */
        rkfb_hdmi_phy_i2c_write(sc, 0x09, 0x0091);  /* CKSYMTXCTRL*/
        rkfb_hdmi_phy_i2c_write(sc, 0x0e, 0x0000);  /* VLEVCTRL   */
        rkfb_hdmi_phy_i2c_write(sc, 0x19, 0x0000);  /* CKCALCTRL  */

        printf("rkfb: PHY I2C config written\n");

        /*
         * Power up PHY.
         * PHY_CONF0: PDZ=1(7) | ENTMDS=1(6) | GEN2_TXPWRON=1(3) |
         *            SELDATAENPOL=1(1) = 0xCA
         */
        rkfb_hdmi_write1(sc, 0x3000, 0xca);
        DELAY(5000);

        printf("rkfb: PHY powered up, waiting for PLL lock\n");

        /* Wait for TX_PHY_LOCK (PHY_STAT0 bit 4) */
        for (timeout = 20; timeout > 0; timeout--) {
                DELAY(5000);
                stat = rkfb_hdmi_read1(sc, 0x3004);
                if (stat & 0x10) {
                        printf("rkfb: PHY locked! "
                            "PHY_STAT0=0x%02x HPD=%d\n",
                            stat, (stat >> 1) & 1);
                        break;
                }
        }
        if (timeout == 0)
                printf("rkfb: PHY lock timeout PHY_STAT0=0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x3004));

        /* Unmute global interrupts, enable HPD only */
        rkfb_hdmi_write1(sc, 0x01ff, 0x00);  /* IH_MUTE: unmute all  */
        rkfb_hdmi_write1(sc, 0x0184, 0xfe);  /* unmask HPD (bit1=0)  */

        printf("rkfb: HDMI PHY init complete\n");
        printf("rkfb: PHY_CONF0 [0x3000] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3000));
        printf("rkfb: PHY_STAT0 [0x3004] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3004));
        printf("rkfb: IH_PHY    [0x0104] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x0104));
}

/* -------------------------------------------------------------------------
 * cdev operations
 * ---------------------------------------------------------------------- */

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_read_t  rkfb_read;
static d_write_t rkfb_write;

static struct cdevsw rkfb_cdevsw = {
        .d_version = D_VERSION,
        .d_open    = rkfb_open,
        .d_close   = rkfb_close,
        .d_ioctl   = rkfb_ioctl,
        .d_read    = rkfb_read,
        .d_write   = rkfb_write,
        .d_name    = "rkfb",
};

static int
rkfb_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        if (sc == NULL)
                return (ENXIO);
        return (0);
}

static int
rkfb_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        if (sc == NULL)
                return (ENXIO);
        return (0);
}

static int
rkfb_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        struct rkfb_info  *info;

        if (sc == NULL)
                return (ENXIO);

        switch (cmd) {

        /* ---- register read (multi-block) -------------------------------- */
        case RKFB_REG_READ: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;

                switch (ro->block) {
                case 0: /* VOP — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->vop_size)
                                return (EINVAL);
                        ro->val = rkfb_vop_read4(sc, ro->off);
                        return (0);
                case 1: /* GRF — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->grf_size)
                                return (EINVAL);
                        ro->val = rkfb_grf_read4(sc, ro->off);
                        return (0);
                case 2: /* CRU — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->cru_size)
                                return (EINVAL);
                        ro->val = rkfb_cru_read4(sc, ro->off);
                        return (0);
                case 3: /* HDMI — byte-wide, no alignment requirement */
                        if (ro->off >= sc->hdmi_size)
                                return (EINVAL);
                        ro->val = rkfb_hdmi_read1(sc, ro->off);
                        return (0);
                default:
                        return (EINVAL);
                }
        }

        /* ---- HDMI byte write -------------------------------------------- */
        case RKFB_HDMI_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;
                if (ro->off >= sc->hdmi_size)
                        return (EINVAL);
                printf("rkfb: HDMI_WRITE[0x%04x] <= 0x%02x\n",
                    ro->off, ro->val & 0xff);
                rkfb_hdmi_write1(sc, ro->off, (uint8_t)(ro->val & 0xff));
                return (0);
        }

        /* ---- VOP masked write ------------------------------------------- */
        case RKFB_VOP_MASKWRITE: {
                struct rkfb_regmaskop *mo = (struct rkfb_regmaskop *)data;
                uint32_t writeval;

                if ((mo->off & 0x3) != 0)
                        return (EINVAL);
                if (mo->off >= sc->vop_size)
                        return (EINVAL);
                if (!rkfb_vop_write_allowed(mo->off))
                        return (EPERM);

                writeval = ((mo->mask & 0xffff) << 16) | (mo->val & 0xffff);
                printf("rkfb: MASKWRITE VOP[0x%04x] mask=0x%04x "
                    "val=0x%04x raw=0x%08x\n",
                    mo->off, mo->mask & 0xffff, mo->val & 0xffff, writeval);
                rkfb_vop_write4(sc, mo->off, writeval);
                return (0);
        }

        /* ---- VOP raw write ---------------------------------------------- */
        case RKFB_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;

                if (ro->block != 0)
                        return (EPERM);
                if ((ro->off & 0x3) != 0)
                        return (EINVAL);
                if (ro->off >= sc->vop_size)
                        return (EINVAL);
                if (!rkfb_vop_write_allowed(ro->off))
                        return (EPERM);

                printf("rkfb: REG_WRITE VOP[0x%04x] <= 0x%08x\n",
                    ro->off, ro->val);
                rkfb_vop_write4(sc, ro->off, ro->val);
                return (0);
        }

        /* ---- VOP range dump --------------------------------------------- */
        case RKFB_VOP_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i, off;

                if ((rd->base & 0x3) != 0)
                        return (EINVAL);
                if (rd->count == 0 || rd->count > 64)
                        return (EINVAL);
                if (rd->base + rd->count * 4 > sc->vop_size)
                        return (EINVAL);

                printf("rkfb: ---- VOP dump base=0x%08x count=%u ----\n",
                    rd->base, rd->count);
                for (i = 0; i < rd->count; i++) {
                        off = rd->base + i * 4;
                        printf("rkfb: VOP[0x%04x] = 0x%08x\n",
                            off, rkfb_vop_read4(sc, off));
                }
                printf("rkfb: ------------------------------------------\n");
                return (0);
        }

        /* ---- HDMI range dump -------------------------------------------- */
        case RKFB_HDMI_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i, off;

                if (rd->count == 0 || rd->count > 256)
                        return (EINVAL);
                if (rd->base + rd->count > sc->hdmi_size)
                        return (EINVAL);

                printf("rkfb: ---- HDMI dump base=0x%04x count=%u ----\n",
                    rd->base, rd->count);
                for (i = 0; i < rd->count; i++) {
                        off = rd->base + i;
                        printf("rkfb: HDMI[0x%04x] = 0x%02x\n",
                            off, rkfb_hdmi_read1(sc, off));
                }
                printf("rkfb: -----------------------------------------\n");
                return (0);
        }

        /* ---- misc -------------------------------------------------------- */
        case RKFB_DUMPREGS:
                printf("rkfb: ---- register dump ----\n");
                printf("rkfb: VOP[0x0000] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0000));
                printf("rkfb: VOP[0x0004] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0004));
                printf("rkfb: VOP[0x0008] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0008));
                printf("rkfb: VOP[0x0010] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0010));
                printf("rkfb: GRF[0x0000] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0000));
                printf("rkfb: GRF[0x0004] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0000] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0000));
                printf("rkfb: CRU[0x0004] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0008] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0008));
                printf("rkfb: -----------------------\n");
                return (0);

        case RKFB_GETINFO:
                info = (struct rkfb_info *)data;
                info->width   = sc->width;
                info->height  = sc->height;
                info->bpp     = sc->bpp;
                info->stride  = sc->stride;
                info->fb_size = sc->fb_size;
		info->fb_pa   = (uint64_t)sc->fb_pa;
                return (0);

        case RKFB_CLEAR: {
                struct rkfb_fill *fill = (struct rkfb_fill *)data;
                uint32_t *p = (uint32_t *)sc->fb_va;
                size_t count = sc->fb_size / sizeof(uint32_t);
                size_t i;
                for (i = 0; i < count; i++)
                        p[i] = fill->pixel;
                return (0);
        }

        case RKFB_FILLRECT: {
                struct rkfb_rect *r = (struct rkfb_rect *)data;
                uint32_t *fb = (uint32_t *)sc->fb_va;
                uint32_t x, y, max_x, max_y;

                if (r->x >= sc->width || r->y >= sc->height)
                        return (EINVAL);

                max_x = r->x + r->w;
                max_y = r->y + r->h;
                if (max_x > sc->width)  max_x = sc->width;
                if (max_y > sc->height) max_y = sc->height;

                printf("rkfb: fillrect x=%u y=%u w=%u h=%u pixel=0x%08x\n",
                    r->x, r->y, r->w, r->h, r->pixel);

                for (y = r->y; y < max_y; y++)
                        for (x = r->x; x < max_x; x++)
                                fb[y * sc->width + x] = r->pixel;

                return (0);
        }

        default:
                return (ENOTTY);
        }
}

static int
rkfb_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t available;

        if (sc == NULL)
                return (ENXIO);
        if ((size_t)uio->uio_offset >= sc->fb_size)
                return (0);

        available = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)available)
                available = uio->uio_resid;

        return (uiomove((void *)(sc->fb_va + uio->uio_offset),
            available, uio));
}

static int
rkfb_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t available;

        if (sc == NULL)
                return (ENXIO);
        if ((size_t)uio->uio_offset >= sc->fb_size)
                return (ENOSPC);

        available = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)available)
                available = uio->uio_resid;

        return (uiomove((void *)(sc->fb_va + uio->uio_offset),
            available, uio));
}

/* -------------------------------------------------------------------------
 * Module load / unload
 * ---------------------------------------------------------------------- */

static int
rkfb_modevent(module_t mod, int type, void *data)
{
        struct rkfb_softc *sc = &g_rkfb_sc;
        int error = 0;

        switch (type) {
        case MOD_LOAD:
                bzero(sc, sizeof(*sc));

                sc->width  = RKFB_WIDTH;
                sc->height = RKFB_HEIGHT;
                sc->bpp    = RKFB_BPP;
                sc->stride = sc->width * (sc->bpp / 8);
                sc->fb_size = sc->stride * sc->height;

                mtx_init(&sc->mtx, "rkfb", NULL, MTX_DEF);

                /* Allocate write-combining framebuffer memory */
                sc->fb_va = (vm_offset_t)kmem_alloc_contig(
                    round_page(sc->fb_size),
                    M_WAITOK | M_ZERO,
                    0, ~0UL,
                    PAGE_SIZE, 0,
                    VM_MEMATTR_WRITE_COMBINING);

                if (sc->fb_va == 0) {
                        printf("rkfb: failed to allocate framebuffer\n");
                        mtx_destroy(&sc->mtx);
                        return (ENOMEM);
                }
                sc->fb_pa = pmap_kextract(sc->fb_va);

                /* Map VOP B */
                sc->vop_pa   = 0xff900000;
                sc->vop_size = 0x10000;
                sc->vop_va   = (vm_offset_t)pmap_mapdev(sc->vop_pa,
                    sc->vop_size);
                if (sc->vop_va == 0) {
                        printf("rkfb: failed to map VOP\n");
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                        mtx_destroy(&sc->mtx);
                        return (ENXIO);
                }
                printf("rkfb: VOP mapped pa=0x%jx va=0x%jx size=0x%zx\n",
                    (uintmax_t)sc->vop_pa, (uintmax_t)sc->vop_va,
                    sc->vop_size);
                printf("rkfb: VOP[0x0000] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0000));
                printf("rkfb: VOP[0x0004] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0004));
                printf("rkfb: VOP[0x0008] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0008));
                printf("rkfb: VOP[0x0010] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0010));

                /* Map GRF (expanded to 0x8000 to reach VIO GRF regs) */
                sc->grf_pa   = 0xff320000;
                sc->grf_size = 0x8000;
                sc->grf_va   = (vm_offset_t)pmap_mapdev(sc->grf_pa,
                    sc->grf_size);
                if (sc->grf_va == 0) {
                        printf("rkfb: failed to map GRF\n");
                        pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                        mtx_destroy(&sc->mtx);
                        return (ENXIO);
                }

                /* Map CRU */
                sc->cru_pa   = 0xff760000;
                sc->cru_size = 0x1000;
                sc->cru_va   = (vm_offset_t)pmap_mapdev(sc->cru_pa,
                    sc->cru_size);
                if (sc->cru_va == 0) {
                        printf("rkfb: failed to map CRU\n");
                        pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
                        pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                        mtx_destroy(&sc->mtx);
                        return (ENXIO);
                }

                printf("rkfb: GRF mapped pa=0x%jx va=0x%jx size=0x%zx\n",
                    (uintmax_t)sc->grf_pa, (uintmax_t)sc->grf_va,
                    sc->grf_size);
                printf("rkfb: CRU mapped pa=0x%jx va=0x%jx size=0x%zx\n",
                    (uintmax_t)sc->cru_pa, (uintmax_t)sc->cru_va,
                    sc->cru_size);
                printf("rkfb: GRF[0x0000] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0000));
                printf("rkfb: GRF[0x0004] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0000] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0000));
                printf("rkfb: CRU[0x0004] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0008] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0008));

                /* Map HDMI */
                sc->hdmi_pa   = 0xff940000;
                sc->hdmi_size = 0x10000;
                sc->hdmi_va   = (vm_offset_t)pmap_mapdev(sc->hdmi_pa,
                    sc->hdmi_size);
                if (sc->hdmi_va == 0) {
                        printf("rkfb: failed to map HDMI\n");
                        pmap_unmapdev((void *)sc->cru_va, sc->cru_size);
                        pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
                        pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                        mtx_destroy(&sc->mtx);
                        return (ENXIO);
                }

                printf("rkfb: HDMI mapped pa=0x%jx va=0x%jx size=0x%zx\n",
                    (uintmax_t)sc->hdmi_pa, (uintmax_t)sc->hdmi_va,
                    sc->hdmi_size);

                /* Boot-time HDMI status dump using correct byte reads */
                printf("rkfb: HDMI design_id=0x%02x rev=0x%02x "
                    "prod0=0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x0000),
                    rkfb_hdmi_read1(sc, 0x0004),
                    rkfb_hdmi_read1(sc, 0x0008));
                printf("rkfb: HDMI PHY_CONF0  [0x3000] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x3000));
                printf("rkfb: HDMI PHY_STAT0  [0x3004] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x3004));
                printf("rkfb: HDMI IH_PHY     [0x0104] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x0104));
                printf("rkfb: HDMI IH_MUTE    [0x01ff] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x01ff));
                printf("rkfb: HDMI VP_STATUS  [0x0800] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x0800));
                printf("rkfb: HDMI MC_CLKDIS  [0x4001] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x4001));
                printf("rkfb: HDMI MC_SWRSTZREQ [0x4002] = 0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x4002));

                /*
                 * PHY init — DISABLED pending resolution of SError on
                 * access to the PHY I2C master block (0x3000+).
                 * The HDCP power domain or an AXI path clock is likely
                 * not enabled.  Uncomment once the correct CRU/PMU
                 * sequence is confirmed safe.
                 */
                /* rkfb_hdmi_phy_init(sc); */

                /* Create /dev/rkfb0 */
                sc->cdev = make_dev(&rkfb_cdevsw, 0,
                    UID_ROOT, GID_WHEEL, 0600, "rkfb0");
                if (sc->cdev == NULL) {
                        printf("rkfb: failed to create /dev/rkfb0\n");
                        pmap_unmapdev((void *)sc->hdmi_va, sc->hdmi_size);
                        pmap_unmapdev((void *)sc->cru_va,  sc->cru_size);
                        pmap_unmapdev((void *)sc->grf_va,  sc->grf_size);
                        pmap_unmapdev((void *)sc->vop_va,  sc->vop_size);
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                        mtx_destroy(&sc->mtx);
                        return (ENXIO);
                }
                sc->cdev->si_drv1 = sc;

                printf("rkfb: loaded /dev/rkfb0 %ux%u %u-bpp "
                    "stride=%u size=%zu pa=0x%jx\n",
                    sc->width, sc->height, sc->bpp,
                    sc->stride, sc->fb_size,
                    (uintmax_t)sc->fb_pa);

                /*      PMU       */
		sc->pmu_pa   = 0xff310000;
		sc->pmu_size = 0x1000;
		sc->pmu_va   = (vm_offset_t)pmap_mapdev(sc->pmu_pa, sc->pmu_size);
		if (sc->pmu_va == 0) {
			printf("rkfb: failed to map PMU\n");
			/* cleanup... */
			return (ENXIO);
		}
		printf("rkfb: PMU mapped pa=0x%jx\n", (uintmax_t)sc->pmu_pa);

		/* PMU power domain status */
		printf("rkfb: PMU_PWRDN_ST  [0x0098] = 0x%08x\n",
		    rkfb_pmu_read4(sc, 0x0098));
                
		break;

        case MOD_UNLOAD:
                if (sc->cdev != NULL)
                        destroy_dev(sc->cdev);
                if (sc->fb_va != 0)
                        kmem_free((void *)sc->fb_va,
                            round_page(sc->fb_size));
                if (sc->hdmi_va != 0)
                        pmap_unmapdev((void *)sc->hdmi_va, sc->hdmi_size);
                if (sc->cru_va != 0)
                        pmap_unmapdev((void *)sc->cru_va, sc->cru_size);
                if (sc->grf_va != 0)
                        pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
                if (sc->vop_va != 0)
                        pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
                mtx_destroy(&sc->mtx);
                printf("rkfb: unloaded\n");
                break;

        default:
                error = EOPNOTSUPP;
                break;
        }

        return (error);
}

DEV_MODULE(rkfb, rkfb_modevent, NULL);
MODULE_VERSION(rkfb, 1);
