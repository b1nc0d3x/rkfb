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

  //   HDMI
  
       vm_offset_t hdmi_va;
       vm_paddr_t hdmi_pa;
       size_t hdmi_size;

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


/* -----------------------------------------------------------------------
 * Innosilicon HDMI PHY I2C register read/write via DW-HDMI I2C master
 * ----------------------------------------------------------------------- */

#define HDMI_PHY_I2C_ADDR       0x69

static int
rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc, uint8_t reg, uint16_t val)
{
        int timeout;
        uint8_t stat;

        /* Set slave address */
        rkfb_hdmi_write1(sc, 0x3020, HDMI_PHY_I2C_ADDR);
        /* Set register address */
        rkfb_hdmi_write1(sc, 0x3021, reg);
        /* Write data MSB then LSB */
        rkfb_hdmi_write1(sc, 0x3022, (val >> 8) & 0xff);
        rkfb_hdmi_write1(sc, 0x3023, val & 0xff);
        /* Trigger write operation */
        rkfb_hdmi_write1(sc, 0x3026, 0x10);  /* OPERATION: write */

        /* Poll for done or error — timeout ~10ms at 1000hz */
        for (timeout = 10; timeout > 0; timeout--) {
                DELAY(1000);
                stat = rkfb_hdmi_read1(sc, 0x3027);  /* PHY_I2CM_INT */
                if (stat & 0x02) {  /* done */
                        rkfb_hdmi_write1(sc, 0x3027, 0x02);  /* clear */
                        return (0);
                }
                if (stat & 0x08) {  /* error */
                        rkfb_hdmi_write1(sc, 0x3027, 0x08);  /* clear */
                        printf("rkfb: PHY I2C write error reg=0x%02x\n", reg);
                        return (EIO);
                }
        }
        printf("rkfb: PHY I2C write timeout reg=0x%02x\n", reg);
        return (ETIMEDOUT);
}

static void
rkfb_hdmi_phy_init(struct rkfb_softc *sc)
{
        uint8_t stat;
        int timeout;

        printf("rkfb: starting HDMI PHY init\n");

        /*
         * Step 1 — configure PHY I2C master clock.
         * Reference clock = 4.8 MHz (from CRU_CLKSEL49).
         * Target SCL = 100 kHz standard mode.
         * DIV = (4800000 / (2 * 100000)) - 1 = 23 = 0x17
         * HCNT = LCNT = 24 = 0x18
         */
        rkfb_hdmi_write1(sc, 0x3029, 0x17);  /* PHY_I2CM_DIV */
        rkfb_hdmi_write1(sc, 0x302b, 0x00);  /* SS_SCL_HCNT_1 */
        rkfb_hdmi_write1(sc, 0x302c, 0x18);  /* SS_SCL_HCNT_0 */
        rkfb_hdmi_write1(sc, 0x302d, 0x00);  /* SS_SCL_LCNT_1 */
        rkfb_hdmi_write1(sc, 0x302e, 0x18);  /* SS_SCL_LCNT_0 */

        printf("rkfb: PHY I2C master clock configured\n");

        /*
         * Step 2 — enable PHY interface in DW-HDMI.
         * PHY_CONF0: ENTMDS=1 (bit6), SELDATAENPOL=1 (bit1)
         * Keep PDZ=0 (powered down) until PHY is configured.
         */
        rkfb_hdmi_write1(sc, 0x3000, 0x42);
        DELAY(5000);

        printf("rkfb: PHY_CONF0 set, interface enabled\n");

        /*
         * Step 3 — configure Innosilicon PHY via I2C for 1080p60.
         * Pixel clock = 148.5 MHz.
         * Values from RK3399 TRM / Linux dw_hdmi-rockchip.c phy tables.
         */

        /* mpll_config: pixel clock = 148.5 MHz → mpixelclk_mult=2, mpll_n=1 */
        rkfb_hdmi_phy_i2c_write(sc, 0x06, 0x0008);  /* CPCE_CTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x15, 0x0000);  /* GMPCTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x10, 0x01b5);  /* TXTERM */
        rkfb_hdmi_phy_i2c_write(sc, 0x09, 0x0091);  /* CKSYMTXCTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x0e, 0x0000);  /* VLEVCTRL */

        /* current control */
        rkfb_hdmi_phy_i2c_write(sc, 0x19, 0x0000);  /* CKCALCTRL */

        printf("rkfb: PHY I2C config written\n");

        /*
         * Step 4 — power up the PHY.
         * PHY_CONF0: PDZ=1(bit7) | ENTMDS=1(bit6) |
         *            GEN2_TXPWRON=1(bit3) | SELDATAENPOL=1(bit1)
         * = 0x80 | 0x40 | 0x08 | 0x02 = 0xCA
         */
        rkfb_hdmi_write1(sc, 0x3000, 0xca);
        DELAY(5000);

        printf("rkfb: PHY powered up, waiting for lock\n");

        /*
         * Step 5 — wait for PHY PLL lock.
         * PHY_STAT0 bit4 = TX_PHY_LOCK
         * PHY_STAT0 bit1 = HPD
         */
        for (timeout = 20; timeout > 0; timeout--) {
                DELAY(5000);
                stat = rkfb_hdmi_read1(sc, 0x3004);
                if (stat & 0x10) {  /* TX_PHY_LOCK */
                        printf("rkfb: PHY locked! "
                            "PHY_STAT0=0x%02x (HPD=%d)\n",
                            stat, (stat >> 1) & 1);
                        break;
                }
        }
        if (timeout == 0)
                printf("rkfb: PHY lock timeout PHY_STAT0=0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x3004));

        /*
         * Step 6 — unmute global interrupts and enable HPD interrupt.
         * IH_MUTE [0x01ff]: write 0x00 to unmute all
         * IH_MUTE_PHY_STAT0 [0x0184]: bit1=HPD, write 0 to unmask
         */
        rkfb_hdmi_write1(sc, 0x01ff, 0x00);  /* IH_MUTE: unmute all */
        rkfb_hdmi_write1(sc, 0x0184, 0xfe);  /* unmask HPD only (bit1=0) */

        printf("rkfb: HDMI PHY init complete\n");
        printf("rkfb: PHY_CONF0  [0x3000] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3000));
        printf("rkfb: PHY_STAT0  [0x3004] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3004));
        printf("rkfb: IH_PHY     [0x0104] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x0104));
}

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
rkfb_vop_read4(struct rkfb_softc *sc, size_t off)
{
  volatile uint32_t *reg;
  reg = (volatile uint32_t *)(sc->vop_va + off);
  return (*reg);
}

static int
rkfb_vop_write_allowed(uint32_t off)
{
	switch (off) {
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

static inline uint32_t
rkfb_hdmi_read4(struct rkfb_softc *sc, size_t off)
{
	volatile uint32_t *reg;

	reg = (volatile uint32_t *)(sc->hdmi_va + off);
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

	case RKFB_VOP_MASKWRITE: {
		struct rkfb_regmaskop *mo;
		uint32_t writeval;

		mo = (struct rkfb_regmaskop *)data;

	if ((mo->off & 0x3) != 0)
		return (EINVAL);
	if (mo->off >= sc->vop_size)
		return (EINVAL);
	if (!rkfb_vop_write_allowed(mo->off))
		return (EPERM);

	writeval = ((mo->mask & 0xffff) << 16) | (mo->val& 0xffff);

	printf("rkfb: MASKWRITE VOP[0x%04x] mask=0x%04x value=0x%04x raw=0x%08x\n",
	    mo->off, mo->mask & 0xffff, mo->val & 0xffff, writeval);

	rkfb_vop_write(sc, mo->off, writeval);
	return (0);
}


	  case RKFB_REG_WRITE: {
		struct rkfb_regop *ro;

		ro = (struct rkfb_regop *)data;

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

		rkfb_vop_write(sc, ro->off, ro->val);

		return (0);
	}

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


		case RKFB_REG_READ: {
		struct rkfb_regop *ro;

		ro = (struct rkfb_regop *)data;

		switch (ro->block) {
		case 0:
		  if((ro->off & 0x3) != 0)
		    return (EINVAL);
			if (ro->off >= sc->vop_size)
				return (EINVAL);
			ro->val = rkfb_vop_read4(sc, ro->off);
			return (0);
		case 1:
		  if ((ro->off & 0x3) != 0)
            return (EINVAL);
			if (ro->off >= sc->grf_size)
				return (EINVAL);
			ro->val = rkfb_grf_read4(sc, ro->off);
			return (0);
		case 2:
		  if ((ro->off & 0x3) != 0)
            return (EINVAL);
			if (ro->off >= sc->cru_size)
				return (EINVAL);
			ro->val = rkfb_cru_read4(sc, ro->off);
			return (0);
		case 3:
			if (ro->off >= sc->hdmi_size)
				return (EINVAL);
			ro->val = rkfb_hdmi_read1(sc, ro->off);
			return (0);
		default:
			return (EINVAL);
		}
	}

		  case RKFB_HDMI_DUMP_RANGE: {
	struct rkfb_regdump *rd;
	uint32_t off, i;

	rd = (struct rkfb_regdump *)data;

	if ((rd->base & 0x3) != 0)
		return (EINVAL);
	if (rd->count == 0 || rd->count > 64)
		return (EINVAL);
	if (rd->base + rd->count * 4 > sc->hdmi_size)
		return (EINVAL);

	printf("rkfb: ---- HDMI range dump base=0x%08x count=%u ----\n",
	    rd->base, rd->count);

	for (i = 0; i < rd->count; i++) {
		off = rd->base + i * 4;
		printf("rkfb: HDMI[0x%04x] = 0x%08x\n",
		    off, rkfb_hdmi_read4(sc, off));
	}

	printf("rkfb: --------------------------------------------\n");
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

	  case RKFB_HDMI_REG_WRITE: {
    struct rkfb_regop *ro;
    ro = (struct rkfb_regop *)data;
    if (ro->off >= sc->hdmi_size)
        return (EINVAL);
    printf("rkfb: HDMI_WRITE[0x%04x] <= 0x%02x\n",
        ro->off, ro->val & 0xff);
    rkfb_hdmi_write1(sc, ro->off, (uint8_t)(ro->val & 0xff));
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


                  sc->hdmi_pa = 0xff940000;
sc->hdmi_size = 0x10000;
sc->hdmi_va = (vm_offset_t)pmap_mapdev(sc->hdmi_pa, sc->hdmi_size);

if (sc->hdmi_va == 0) {
	printf("rkfb: failed to map HDMI at %#jx\n",
	    (uintmax_t)sc->hdmi_pa);
	pmap_unmapdev((void *)sc->cru_va, sc->cru_size);
	pmap_unmapdev((void *)sc->grf_va, sc->grf_size);
	pmap_unmapdev((void *)sc->vop_va, sc->vop_size);
	kmem_free((void *)sc->fb_va, round_page(sc->fb_size));
	mtx_destroy(&sc->mtx);
	return (ENXIO);
}

 printf("rkfb: HDMI mapped pa=%#jx va=%#jx size=%#zx\n",
    (uintmax_t)sc->hdmi_pa, (uintmax_t)sc->hdmi_va, sc->hdmi_size);

printf("rkfb: HDMI[0x0000] = 0x%08x\n", rkfb_hdmi_read4(sc, 0x0000));
printf("rkfb: HDMI[0x0004] = 0x%08x\n", rkfb_hdmi_read4(sc, 0x0004));
printf("rkfb: HDMI[0x0008] = 0x%08x\n", rkfb_hdmi_read4(sc, 0x0008));
printf("rkfb: HDMI[0x0010] = 0x%08x\n", rkfb_hdmi_read4(sc, 0x0010));

                rkfb_hdmi_phy_init(sc);
 
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
		printf("rkfb: HDMI design_id=0x%02x rev=0x%02x prod0=0x%02x\n",
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

 if (sc->hdmi_va != 0)
	pmap_unmapdev((void *)sc->hdmi_va, sc->hdmi_size);
		
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
