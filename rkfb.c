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
/*#include <sys/bus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <machine/bus.h>
#include <sys/rman.h>*/
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include "rkfb_ioctl.h"

#define RKFB_WIDTH   1024
#define RKFB_HEIGHT   768
#define RKFB_BPP       32

struct rkfb_softc {
	struct cdev *cdev;

	vm_offset_t fb_va;
	vm_paddr_t fb_pa;
	size_t fb_size;

  //    VISUAL OUTPUT PROCESSOR
  
        vm_offset_t vop_va;
        vm_paddr_t vop_pa;
        size_t vop_size;

  //    GENERAL REGISTER FILE
  
        vm_offset_t grf_va;
        vm_paddr_t grf_pa;
        size_t grf_size;


  //    CLOCK RESET UNIT
        vm_offset_t cru_va;
        vm_paddr_t cru_pa;
        size_t cru_size;

	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t stride;

	struct mtx mtx;
};

static inline uint32_t
rkfb_vop_read4(struct rkfb_softc *sc, size_t off)
{
  volatile uint32_t *reg;
  reg = (volatile uint32_t *)(sc->vop_va + off);
  return (*reg);
}

static inline void
rkfb_vop_write(struct rkfb_softc *sc, size_t off, uint32_t val)
{
  volatile uint32_t *reg;
  reg = (volatile uint32_t *)(sc->vop_va + off);
  *reg = val;
}


static inline uint32_t
rkfb_grf_read4(struct rkfb_softc *sc, size_t off)
{
	volatile uint32_t *reg;

	reg = (volatile uint32_t *)(sc->grf_va + off);
	return (*reg);
}

static inline uint32_t
rkfb_cru_read4(struct rkfb_softc *sc, size_t off)
{
	volatile uint32_t *reg;

	reg = (volatile uint32_t *)(sc->cru_va + off);
	return (*reg);
}


static struct rkfb_softc g_rkfb_sc;

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_read_t  rkfb_read;
static d_write_t rkfb_write;

static struct cdevsw rkfb_cdevsw = {
	.d_version = D_VERSION,
	.d_open = rkfb_open,
	.d_close = rkfb_close,
	.d_ioctl = rkfb_ioctl,
	.d_read = rkfb_read,
	.d_write = rkfb_write,
	.d_name = "rkfb",
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
rkfb_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	struct rkfb_softc *sc = dev->si_drv1;
	struct rkfb_info *info;

	if (sc == NULL)
		return (ENXIO);
	
	

	switch (cmd) {

	  	case RKFB_VOP_DUMP_RANGE: {
		struct rkfb_regdump *rd;
		uint32_t off, i;

		rd = (struct rkfb_regdump *)data;

		if ((rd->base & 0x3) != 0)
			return (EINVAL);
		if (rd->count == 0 || rd->count > 64)
			return (EINVAL);
		if (rd->base + rd->count * 4 > sc->vop_size)
			return (EINVAL);

		printf("rkfb: ---- VOP range dump base=0x%08x count=%u ----\n",
		    rd->base, rd->count);

		for (i = 0; i < rd->count; i++) {
			off = rd->base + i * 4;
			printf("rkfb: VOP[0x%04x] = 0x%08x\n",
			    off, rkfb_vop_read4(sc, off));
		}

		printf("rkfb: -------------------------------------------\n");
		return (0);
	}

	case RKFB_DUMPREGS:
		printf("rkfb: ---- register dump ----\n");
		printf("rkfb: VOP[0x0000] = 0x%08x\n", rkfb_vop_read4(sc, 0x0000));
		printf("rkfb: VOP[0x0004] = 0x%08x\n", rkfb_vop_read4(sc, 0x0004));
		printf("rkfb: VOP[0x0008] = 0x%08x\n", rkfb_vop_read4(sc, 0x0008));
		printf("rkfb: VOP[0x0010] = 0x%08x\n", rkfb_vop_read4(sc, 0x0010));

		printf("rkfb: GRF[0x0000] = 0x%08x\n", rkfb_grf_read4(sc, 0x0000));
		printf("rkfb: GRF[0x0004] = 0x%08x\n", rkfb_grf_read4(sc, 0x0004));

		printf("rkfb: CRU[0x0000] = 0x%08x\n", rkfb_cru_read4(sc, 0x0000));
		printf("rkfb: CRU[0x0004] = 0x%08x\n", rkfb_cru_read4(sc, 0x0004));
		printf("rkfb: CRU[0x0008] = 0x%08x\n", rkfb_cru_read4(sc, 0x0008));
		printf("rkfb: -----------------------\n");
		return (0);  
	case RKFB_GETINFO:
		info = (struct rkfb_info *)data;
		info->width = sc->width;
		info->height = sc->height;
		info->bpp = sc->bpp;
		info->stride = sc->stride;
		info->fb_size = sc->fb_size;
		return (0);
        case RKFB_CLEAR: {
		struct rkfb_fill *fill;
		uint32_t *p;
		size_t count, i;

		fill = (struct rkfb_fill *)data;
		p = (uint32_t *)sc->fb_va;
		count = sc->fb_size / sizeof(uint32_t);

		for (i = 0; i < count; i++)
			p[i] = fill->pixel;

		return (0);
	}
	

	case RKFB_FILLRECT:{ 
	       struct rkfb_rect *r;
	       uint32_t *fb;
	       uint32_t x, y;

	       r = (struct rkfb_rect *)data;
	       fb = (uint32_t *)sc->fb_va;


	       printf("rkfb: fillrect x=%u y=%u w=%u h=%u pixel=0x%08x\n",
		      r->x, r->y, r->w, r->h, r->pixel);
	/* Clamp to screen bounds */
	       if (r->x >= sc->width || r->y >= sc->height)
		return (EINVAL);

	       uint32_t max_x = r->x + r->w;
	       uint32_t max_y = r->y + r->h;

	       if (max_x > sc->width)
		max_x = sc->width;
	       if (max_y > sc->height)
		max_y = sc->height;

	       for (y = r->y; y < max_y; y++) {
		for (x = r->x; x < max_x; x++) {
			fb[y * sc->width + x] = r->pixel;
		}
	}

	       printf("rkfb: clamped to max_x=%u max_y=%u\n", max_x, max_y);

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
	int error;

	if (sc == NULL)
		return (ENXIO);

	if ((size_t)uio->uio_offset >= sc->fb_size)
		return (0);

	available = sc->fb_size - (size_t)uio->uio_offset;
	if (uio->uio_resid < available)
		available = uio->uio_resid;

	error = uiomove((void *)(sc->fb_va + uio->uio_offset), available, uio);
	return (error);
}

static int
rkfb_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct rkfb_softc *sc = dev->si_drv1;
	size_t available;
	int error;

	if (sc == NULL)
		return (ENXIO);

	if ((size_t)uio->uio_offset >= sc->fb_size)
		return (ENOSPC);

	available = sc->fb_size - (size_t)uio->uio_offset;
	if (uio->uio_resid < available)
		available = uio->uio_resid;

	error = uiomove((void *)(sc->fb_va + uio->uio_offset), available, uio);
	return (error);
}

static int
rkfb_modevent(module_t mod, int type, void *data)
{
	struct rkfb_softc *sc;
	int error;

	sc = &g_rkfb_sc;
	error = 0;

	switch (type) {
	case MOD_LOAD:
		bzero(sc, sizeof(*sc));

		sc->width = RKFB_WIDTH;
		sc->height = RKFB_HEIGHT;
		sc->bpp = RKFB_BPP;
		sc->stride = sc->width * (sc->bpp / 8);
		sc->fb_size = sc->stride * sc->height;

		mtx_init(&sc->mtx, "rkfb", NULL, MTX_DEF);

		sc->fb_va = (vm_offset_t)kmem_alloc_contig(
		    round_page(sc->fb_size),
		    M_WAITOK | M_ZERO,
		    0,
		    ~0UL,
		    PAGE_SIZE,
		    0,
		    VM_MEMATTR_WRITE_COMBINING);

		if (sc->fb_va == 0) {
			printf("rkfb: failed to allocate framebuffer memory\n");
			mtx_destroy(&sc->mtx);
			return (ENOMEM);
		}

		sc->fb_pa = pmap_kextract(sc->fb_va);

		sc->vop_pa = 0xff900000;
		sc->vop_size = 0x10000;
		sc->vop_va = (vm_offset_t)pmap_mapdev(sc->vop_pa, sc->vop_size);

		if (sc->vop_va == 0) {
			printf("rkfb: failed to map VOP at %#jx\n",
			    (uintmax_t)sc->vop_pa);
			if (sc->cdev != NULL)
				destroy_dev(sc->cdev);
			kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
			mtx_destroy(&sc->mtx);
			return (ENXIO);
		}

		printf("rkfb: VOP mapped pa=%#jx va=%#jx size=%#zx\n",
		    (uintmax_t)sc->vop_pa, (uintmax_t)sc->vop_va, sc->vop_size);

		printf("rkfb: VOP[0x0000] = 0x%08x\n",
		    rkfb_vop_read4(sc, 0x0000));
		printf("rkfb: VOP[0x0004] = 0x%08x\n",
		    rkfb_vop_read4(sc, 0x0004));
		printf("rkfb: VOP[0x0008] = 0x%08x\n",
		    rkfb_vop_read4(sc, 0x0008));
		printf("rkfb: VOP[0x0010] = 0x%08x\n",
		    rkfb_vop_read4(sc, 0x0010));

		sc->grf_pa = 0xff320000;
sc->grf_size = 0x1000;
sc->grf_va = (vm_offset_t)pmap_mapdev(sc->grf_pa, sc->grf_size);
if (sc->grf_va == 0) {
	printf("rkfb: failed to map GRF at %#jx\n", (uintmax_t)sc->grf_pa);
	pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
	kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
	mtx_destroy(&sc->mtx);
	return (ENXIO);
}

sc->cru_pa = 0xff760000;
sc->cru_size = 0x1000;
sc->cru_va = (vm_offset_t)pmap_mapdev(sc->cru_pa, sc->cru_size);
if (sc->cru_va == 0) {
	printf("rkfb: failed to map CRU at %#jx\n", (uintmax_t)sc->cru_pa);
	pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
	pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
	kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
	mtx_destroy(&sc->mtx);
	return (ENXIO);
 }

 printf("rkfb: GRF mapped pa=%#jx va=%#jx size=%#zx\n",
    (uintmax_t)sc->grf_pa, (uintmax_t)sc->grf_va, sc->grf_size);
printf("rkfb: CRU mapped pa=%#jx va=%#jx size=%#zx\n",
    (uintmax_t)sc->cru_pa, (uintmax_t)sc->cru_va, sc->cru_size);

printf("rkfb: GRF[0x0000] = 0x%08x\n", rkfb_grf_read4(sc, 0x0000));
printf("rkfb: GRF[0x0004] = 0x%08x\n", rkfb_grf_read4(sc, 0x0004));

printf("rkfb: CRU[0x0000] = 0x%08x\n", rkfb_cru_read4(sc, 0x0000));
printf("rkfb: CRU[0x0004] = 0x%08x\n", rkfb_cru_read4(sc, 0x0004));
printf("rkfb: CRU[0x0008] = 0x%08x\n", rkfb_cru_read4(sc, 0x0008));

		sc->cdev = make_dev(&rkfb_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
		    "rkfb0");
		if (sc->cdev == NULL) {
			printf("rkfb: failed to create /dev/rkfb0\n");
			kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
			mtx_destroy(&sc->mtx);
			return (ENXIO);
		}
		sc->cdev->si_drv1 = sc;

		printf("rkfb: loaded /dev/rkfb0 %ux%u %u-bpp stride=%u size=%zu pa=%#jx\n",
		    sc->width, sc->height, sc->bpp, sc->stride, sc->fb_size,
		    (uintmax_t)sc->fb_pa);
		break;

	case MOD_UNLOAD:
		if (sc->cdev != NULL)
			destroy_dev(sc->cdev);

		if (sc->fb_va != 0)
			kmem_free((void *)sc->fb_va, round_page(sc->fb_size));

                  if (sc->cru_va != 0)
	pmap_unmapdev((void *)sc->cru_va, sc->cru_size);

if (sc->grf_va != 0)
	pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
		
		if (sc->vop_va != 0)
		  pmap_unmapdev((void*)sc->vop_va, sc->vop_size);
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
