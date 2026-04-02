#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include "rkfb_ioctl.h"

#define RKFB_WIDTH   1024
#define RKFB_HEIGHT   768
#define RKFB_BPP       32

struct rkfb_softc {
	device_t dev;
	struct cdev *cdev;

	vm_offset_t fb_va;
	vm_paddr_t fb_pa;
	size_t fb_size;

	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t stride;

	struct mtx mtx;
};

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_mmap_t  rkfb_mmap;

static struct cdevsw rkfb_cdevsw = {
	.d_version = D_VERSION,
	.d_open = rkfb_open,
	.d_close = rkfb_close,
	.d_ioctl = rkfb_ioctl,
	.d_mmap = rkfb_mmap,
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
	case RKFB_GETINFO:
		info = (struct rkfb_info *)data;
		info->width = sc->width;
		info->height = sc->height;
		info->bpp = sc->bpp;
		info->stride = sc->stride;
		info->fb_size = sc->fb_size;
		return (0);
	default:
		return (ENOTTY);
	}
}

static int
rkfb_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr, int nprot,
    vm_memattr_t *memattr)
{
	struct rkfb_softc *sc = dev->si_drv1;

	if (sc == NULL)
		return (ENXIO);

	if (offset >= sc->fb_size)
		return (EINVAL);

	*paddr = sc->fb_pa + offset;
	*memattr = VM_MEMATTR_WRITE_COMBINING;
	return (0);
}

static int
rkfb_probe(device_t dev)
{
	device_set_desc(dev, "RK3399 framebuffer stub");
	return (BUS_PROBE_DEFAULT);
}

static int
rkfb_attach(device_t dev)
{
	struct rkfb_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->width = RKFB_WIDTH;
	sc->height = RKFB_HEIGHT;
	sc->bpp = RKFB_BPP;
	sc->stride = sc->width * (sc->bpp / 8);
	sc->fb_size = sc->stride * sc->height;

	mtx_init(&sc->mtx, "rkfb", NULL, MTX_DEF);

	/*
	 * Allocate contiguous memory for the framebuffer.
	 * This is a first-pass stub. We only need RAM we can mmap to userspace.
	 */
	sc->fb_va = (vm_offset_t)kmem_alloc_contig(kernel_arena,
	    round_page(sc->fb_size),
	    M_NOWAIT | M_ZERO,
	    0, ~0UL,
	    PAGE_SIZE, 0,
	    VM_MEMATTR_WRITE_COMBINING);

	if (sc->fb_va == 0) {
		device_printf(dev, "failed to allocate framebuffer memory\n");
		mtx_destroy(&sc->mtx);
		return (ENOMEM);
	}

	sc->fb_pa = pmap_kextract(sc->fb_va);

	sc->cdev = make_dev(&rkfb_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "rkfb0");
	if (sc->cdev == NULL) {
		device_printf(dev, "failed to create /dev/rkfb0\n");
		kmem_free(kernel_arena, sc->fb_va, round_page(sc->fb_size));
		mtx_destroy(&sc->mtx);
		return (ENXIO);
	}
	sc->cdev->si_drv1 = sc;

	device_printf(dev,
	    "attached: %ux%u %u-bpp stride=%u fb_size=%zu pa=%#jx\n",
	    sc->width, sc->height, sc->bpp, sc->stride, sc->fb_size,
	    (uintmax_t)sc->fb_pa);

	return (0);
}

static int
rkfb_detach(device_t dev)
{
	struct rkfb_softc *sc;

	sc = device_get_softc(dev);

	if (sc->cdev != NULL)
		destroy_dev(sc->cdev);

	if (sc->fb_va != 0)
		kmem_free(kernel_arena, sc->fb_va, round_page(sc->fb_size));

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

static devclass_t rkfb_devclass;
DRIVER_MODULE(rkfb, simplebus, rkfb_driver, rkfb_devclass, 0, 0);
MODULE_VERSION(rkfb, 1);
MODULE_DEPEND(rkfb, kernel, 1, 1, 1);
