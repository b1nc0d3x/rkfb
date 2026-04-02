#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#include "rkfb_ioctl.h"

#define RKFB_WIDTH   1024
#define RKFB_HEIGHT   768
#define RKFB_BPP       32

struct rkfb_softc {
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

static struct rkfb_softc g_rkfb_sc;

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_mmap_single_t rkfb_mmap_single;

static struct cdevsw rkfb_cdevsw = {
	.d_version = D_VERSION,
	.d_open = rkfb_open,
	.d_close = rkfb_close,
	.d_ioctl = rkfb_ioctl,
	.d_mmap_single = rkfb_mmap_single,
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
rkfb_mmap_single(struct cdev *dev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **object, int nprot)
{
	struct rkfb_softc *sc = dev->si_drv1;

	if (sc == NULL)
		return (ENXIO);

	if (*offset >= sc->fb_size)
		return (EINVAL);

	if (size > sc->fb_size - *offset)
		return (EINVAL);

	*object = vm_pager_allocate(OBJT_DEVICE, sc, round_page(sc->fb_size),
	    nprot, *offset, curthread->td_ucred);

	if (*object == NULL)
		return (EINVAL);

	return (0);
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

		sc->fb_va = (vm_offset_t)kmem_alloc_contig(kernel_arena,
		    round_page(sc->fb_size),
		    M_WAITOK | M_ZERO,
		    0, ~0UL,
		    PAGE_SIZE, 0,
		    VM_MEMATTR_WRITE_COMBINING);

		if (sc->fb_va == 0) {
			printf("rkfb: failed to allocate framebuffer memory\n");
			mtx_destroy(&sc->mtx);
			return (ENOMEM);
		}

		sc->fb_pa = pmap_kextract(sc->fb_va);

		sc->cdev = make_dev(&rkfb_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
		    "rkfb0");
		if (sc->cdev == NULL) {
			printf("rkfb: failed to create /dev/rkfb0\n");
			kmem_free(kernel_arena, sc->fb_va, round_page(sc->fb_size));
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
			kmem_free(kernel_arena, sc->fb_va, round_page(sc->fb_size));

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
