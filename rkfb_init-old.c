/*
 * rkfb_init.c - RK3399 / RockPro64 display bring-up tool
 *
 * HDMI init via /dev/mem (works from EL0, avoids EL1 SError).
 * VOP scanout programmed via /dev/rkfb0 ioctls.
 *
 * Run once at boot: /usr/local/sbin/rkfb_init
 *
 * Sequence:
 *   0. Enable HDMI clocks via /dev/mem CRU
 *   1. Release HDMI soft reset
 *   2. Configure GRF mux: VOPB -> HDMI
 *   3. Configure VOP WIN0 scanout via rkfb ioctls
 *   4. Program HDMI frame composer (720p60)
 *   5. Init Innosilicon PHY at 74.25 MHz
 *   6. Commit VOP REG_CFG_DONE
 *   7. Report final state
 *
 * Build: cc -o rkfb_init rkfb_init.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "rkfb_ioctl.h"

/* -------------------------------------------------------------------------
 * Physical addresses
 * ---------------------------------------------------------------------- */

#define HDMI_PA         0xff940000UL
#define HDMI_SIZE       0x20000
#define CRU_PA          0xff760000UL
#define CRU_SIZE        0x1000
#define GRF_PA          0xff320000UL
#define GRF_SIZE        0x8000
#define VIOGRF_PA   0xff770000UL
#define VIOGRF_SIZE 0x1000
/* -------------------------------------------------------------------------
 * /dev/mem mapped regions
 * ---------------------------------------------------------------------- */

static volatile uint8_t  *g_hdmi;
static volatile uint32_t *g_cru;
static volatile uint32_t *g_grf;
static int g_memfd;
static int g_rkfb;

/* -------------------------------------------------------------------------
 * Accessors
 * ---------------------------------------------------------------------- */

static inline uint8_t  hdmi_r(uint32_t off) { return g_hdmi[off]; }
static inline void     hdmi_w(uint32_t off, uint8_t v) { g_hdmi[off] = v; }
static inline uint32_t cru_r(uint32_t off) { return g_cru[off/4]; }
static inline void     cru_w(uint32_t off, uint32_t v) { g_cru[off/4] = v; }
static inline uint32_t grf_r(uint32_t off) { return g_grf[off/4]; }
static inline void     grf_w(uint32_t off, uint32_t v) { g_grf[off/4] = v; }

/* HIWORD_UPDATE: set mask bits to val */
static inline void
grf_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
        grf_w(off, (mask << 16) | (val & mask));
}

static inline void
cru_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
        cru_w(off, (mask << 16) | (val & mask));
}

/* -------------------------------------------------------------------------
 * VOP ioctls via /dev/rkfb0
 * ---------------------------------------------------------------------- */

static uint32_t
vop_r(uint32_t off)
{
        struct rkfb_regop ro = { .block = RKFB_BLOCK_VOP, .off = off };
        if (ioctl(g_rkfb, RKFB_REG_READ, &ro) < 0)
                err(1, "vop_r(0x%04x)", off);
        return ro.val;
}

static void
vop_w(uint32_t off, uint32_t val)
{
        struct rkfb_regop ro = { .block = RKFB_BLOCK_VOP, .off = off, .val = val };
        if (ioctl(g_rkfb, RKFB_REG_WRITE, &ro) < 0)
                err(1, "vop_w(0x%04x)", off);
}

/* -------------------------------------------------------------------------
 * PHY I2C master
 * ---------------------------------------------------------------------- */

static void
phy_i2c_write(uint8_t reg, uint16_t val)
{
        uint8_t stat;
        int timeout;

        hdmi_w(0x3020, 0x69);
        hdmi_w(0x3021, reg);
        hdmi_w(0x3022, (val >> 8) & 0xff);
        hdmi_w(0x3023, val & 0xff);
        hdmi_w(0x3026, 0x10);

        for (timeout = 30; timeout > 0; timeout--) {
                usleep(1000);
                stat = hdmi_r(0x3027);
                if (stat & 0x02) { hdmi_w(0x3027, 0x02); return; }
                if (stat & 0x08) {
                        hdmi_w(0x3027, 0x08);
                        fprintf(stderr, "PHY I2C error reg=0x%02x\n", reg);
                        return;
                }
        }
        fprintf(stderr, "PHY I2C timeout reg=0x%02x\n", reg);
}

/* -------------------------------------------------------------------------
 * Step 0: Enable HDMI clocks
 * ---------------------------------------------------------------------- */

static void
step0_clocks(void)
{
        printf("[0] Enabling HDMI/HDCP clocks via CRU...\n");
        printf("    CLKGATE16 before: 0x%08x\n", cru_r(0x0240));
        printf("    CLKGATE20 before: 0x%08x\n", cru_r(0x0250));

        cru_hiword(0x0240, (1<<9)|(1<<10), 0);   /* aclk_hdcp, hclk_hdcp */
        cru_hiword(0x0244, (1<<2), 0);            /* pclk_hdcp */
        cru_hiword(0x0250, (1<<12), 0);           /* pclk_hdmi_ctrl */
        cru_hiword(0x0254, (1<<8), 0);            /* hdmi_cec_clk */
        usleep(10000);

        printf("    CLKGATE16 after:  0x%08x\n", cru_r(0x0240));
        printf("    CLKGATE20 after:  0x%08x\n", cru_r(0x0250));
        printf("    Done.\n");
}

/* -------------------------------------------------------------------------
 * Step 1: Release HDMI soft reset
 * ---------------------------------------------------------------------- */

static void
step1_reset_release(void)
{
        printf("\n[1] Releasing HDMI soft reset...\n");
        printf("    MC_SWRSTZREQ before: 0x%02x\n", hdmi_r(0x4002));
        printf("    MC_CLKDIS    before: 0x%02x\n", hdmi_r(0x4001));

        hdmi_w(0x4001, 0x00);   /* MC_CLKDIS: all clocks on */
        hdmi_w(0x4002, 0xff);   /* MC_SWRSTZREQ: release all resets */
        usleep(5000);

        printf("    MC_SWRSTZREQ after:  0x%02x\n", hdmi_r(0x4002));
        printf("    MC_CLKDIS    after:  0x%02x\n", hdmi_r(0x4001));
}

/* -------------------------------------------------------------------------
 * Step 2: GRF mux - VOPB -> HDMI
 * ---------------------------------------------------------------------- */

static volatile uint32_t *g_viogrf;

static inline uint32_t viogrf_r(uint32_t off) { return g_viogrf[off/4]; }
static inline void viogrf_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
        g_viogrf[off/4] = (mask << 16) | (val & mask);
}

static void
step2_grf_mux(void)
{
        printf("\n[2] GRF mux: VOPB -> HDMI (VIO GRF)...\n");
        printf("    VIO GRF SOC_CON20 before: 0x%08x\n", viogrf_r(0x0250));

        /* RK3399_HDMI_LCDC_SEL bit6: 1=VOPB, 0=VOPL */
        viogrf_hiword(0x0250, (1<<6), (1<<6));
        usleep(1000);

        printf("    VIO GRF SOC_CON20 after:  0x%08x\n", viogrf_r(0x0250));
}

/* -------------------------------------------------------------------------
 * Step 3: VOP WIN0 scanout
 * ---------------------------------------------------------------------- */

static void
step3_vop(uint32_t fb_pa)
{
        uint32_t sys_ctrl;

        printf("\n[3] Configuring VOP WIN0 + display timing for 720p60...\n");
        printf("    SYS_CTRL before:   0x%08x\n", vop_r(0x0008));

        /* SYS_CTRL: clear standby (bit11), set hdmi_dclk_en (bit1) */
        sys_ctrl  = vop_r(0x0008);
        sys_ctrl &= ~(1u << 11);
        sys_ctrl |=  (1u << 1);
        vop_w(0x0008, sys_ctrl);

        /*
         * Display timing for 720p60 (74.25 MHz pixel clock):
         * H: active=1280, fp=110, sync=40, bp=220, total=1650
         * V: active=720,  fp=5,   sync=5,  bp=20,  total=750
         *
         * DSP_HTOTAL_HS_END [0x0188]: [31:16]=htotal-1, [15:0]=hsync_end-1
         * DSP_HACT_ST_END   [0x018c]: [31:16]=hact_start, [15:0]=hact_end
         * DSP_VTOTAL_VS_END [0x0190]: [31:16]=vtotal-1, [15:0]=vsync_end-1
         * DSP_VACT_ST_END   [0x0194]: [31:16]=vact_start, [15:0]=vact_end
         */
        vop_w(0x0188, 0x06710027);   /* htotal=1649, hsync_end=39 */
        vop_w(0x018c, 0x01040604);   /* hact_st=260, hact_end=1540 */
        vop_w(0x0190, 0x02ed0004);   /* vtotal=749, vsync_end=4 */
        vop_w(0x0194, 0x001902e9);   /* vact_st=25, vact_end=745 */

        /* WIN0 scanout: ARGB8888, 1280x720, start at (row=25, col=260) */
        vop_w(0x003c, 0x05000500);
        vop_w(0x0040, fb_pa);
        vop_w(0x0048, ((720-1)<<16)|(1280-1));
        vop_w(0x004c, ((720-1)<<16)|(1280-1));
        vop_w(0x0050, (25<<16)|260);
        vop_w(0x0030, 0x00000001);   /* WIN0_CTRL0: enable, ARGB8888 */

        /*
         * Commit VOP timing NOW, before the PHY sequence.
         * The PHY MPLL needs a running pixel clock to lock against.
         * Without the display timing committed, the VOP outputs no clock
         * even if hdmi_dclk_en=1. Wait 2+ frames (40ms at 60Hz).
         */
        printf("    Pre-PHY VOP commit (REG_CFG_DONE)...\n");
        vop_w(0x0000, 0x01);
        usleep(40000);

        printf("    SYS_CTRL after:    0x%08x  (bit11=0 bit1=1 wanted)\n", vop_r(0x0008));
        printf("    WIN0_CTRL0:        0x%08x\n", vop_r(0x0030));
        printf("    DSP_HTOTAL_HS_END: 0x%08x  (expect 0x06710027)\n", vop_r(0x0188));
        printf("    DSP_VTOTAL_VS_END: 0x%08x  (expect 0x02ed0004)\n", vop_r(0x0190));
        printf("    fb_pa:             0x%08x\n", fb_pa);
}

/* -------------------------------------------------------------------------
 * Step 4: HDMI frame composer 720p60
 * ---------------------------------------------------------------------- */

static void
step4_fc_720p60(void)
{
        printf("\n[4] Programming HDMI frame composer (720p60)...\n");

        hdmi_w(0x1000, 0xe0);   /* FC_INVIDCONF: HDMI, VSYNC_H, HSYNC_H, DE_H */
        hdmi_w(0x1001, 0x00);   /* FC_INHACTV0: hactive=1280 low */
        hdmi_w(0x1002, 0x05);   /* FC_INHACTV1: hactive=1280 high */
        hdmi_w(0x1003, 0x72);   /* FC_INHBLANK0: hblank=370 */
        hdmi_w(0x1004, 0x01);   /* FC_INHBLANK1 */
        hdmi_w(0x1005, 0xd0);   /* FC_INVACTV0: vactive=720 */
        hdmi_w(0x1006, 0x02);   /* FC_INVACTV1 */
        hdmi_w(0x1007, 0x1e);   /* FC_INVBLANK: vblank=30 */
        hdmi_w(0x1008, 0x6e);   /* FC_HSYNCINDELAY0: hfp=110 */
        hdmi_w(0x1009, 0x00);   /* FC_HSYNCINDELAY1 */
        hdmi_w(0x100a, 0x28);   /* FC_HSYNCINWIDTH0: hsync=40 */
        hdmi_w(0x100b, 0x00);   /* FC_HSYNCINWIDTH1 */
        hdmi_w(0x100c, 0x05);   /* FC_VSYNCINDELAY: vfp=5 */
        hdmi_w(0x100d, 0x05);   /* FC_VSYNCINWIDTH: vsync=5 */
        hdmi_w(0x1017, 0x00);   /* FC_AVICONF0: RGB */
        hdmi_w(0x1018, 0x08);   /* FC_AVICONF1: 16:9 */
        hdmi_w(0x1019, 0x00);   /* FC_AVICONF2 */
        hdmi_w(0x101b, 0x04);   /* FC_AVIVID: VIC 4 = 720p60 */
        hdmi_w(0x0801, 0x00);   /* VP_PR_CD: no pixel repeat */
        hdmi_w(0x0804, 0x48);   /* VP_CONF: bypass */
        hdmi_w(0x01ff, 0x00);   /* IH_MUTE: unmute all */
        hdmi_w(0x0184, 0xfe);   /* IH_MUTE_PHY: unmask HPD */

        printf("    FC_INVIDCONF: 0x%02x\n", hdmi_r(0x1000));
        printf("    FC_INHACTV:   0x%02x%02x\n", hdmi_r(0x1002), hdmi_r(0x1001));
        printf("    FC_INVACTV:   0x%02x%02x\n", hdmi_r(0x1006), hdmi_r(0x1005));
}

/* -------------------------------------------------------------------------
 * PHY I2C master write helper (used only by step5)
 *
 * Writes one 16-bit value to a PHY-internal register via the DW-HDMI
 * embedded I2C master (slave address 0x69, Innosilicon PHY).
 * ---------------------------------------------------------------------- */

struct phy_reg { uint8_t addr; uint16_t val; };

/*
 * MPLL + drive config for 74.25 MHz (720p60).
 * Source: Linux phy-rockchip-inno-hdmi.c rockchip_mpll_cfg[] /
 *         rockchip_phy_config[], GPLv2, Rockchip.
 * Hardware register values are facts, not creative expression.
 */
static const struct phy_reg phy_74250_mpll[] = {
	{ 0x06, 0x0008 },   /* MPLL_CNTRL     */
	{ 0x0d, 0x0000 },   /* CURR_CNTRL     */
	{ 0x0e, 0x0260 },   /* MPLL_GMP_CNTRL */
	{ 0x10, 0x8009 },   /* MPLL_MISC_CNTRL */
	{ 0x19, 0x0000 },   /* TXTERM (pre-cfg) */
	{ 0x1e, 0x0000 },   /* CKSYMTXCTRL    */
	{ 0x25, 0x0272 },   /* VLEVCTRL       */
};
#define PHY_74250_MPLL_N  (int)(sizeof(phy_74250_mpll)/sizeof(phy_74250_mpll[0]))

static const struct phy_reg phy_74250_drive[] = {
	{ 0x19, 0x0004 },   /* TXTERM: 50 ohm */
	{ 0x1e, 0x8009 },   /* CKSYMTXCTRL    */
	{ 0x25, 0x0272 },   /* VLEVCTRL       */
};
#define PHY_74250_DRIVE_N (int)(sizeof(phy_74250_drive)/sizeof(phy_74250_drive[0]))

static int
phy_i2c_wr(uint8_t reg, uint16_t val)
{
	int i;
	uint8_t st;

	hdmi_w(0x3020, 0x69);              /* PHY_I2CM_SLAVE  */
	hdmi_w(0x3021, reg);               /* PHY_I2CM_ADDR   */
	hdmi_w(0x3022, (val >> 8) & 0xff); /* PHY_I2CM_DATAO_1 (MSB) */
	hdmi_w(0x3023, val & 0xff);        /* PHY_I2CM_DATAO_0 (LSB) */
	hdmi_w(0x3026, 0x10);              /* PHY_I2CM_OPERATION: write */

	for (i = 0; i < 100; i++) {
		usleep(500);
		st = hdmi_r(0x3027);   /* PHY_I2CM_INT */
		if (st & 0x02) { hdmi_w(0x3027, 0x02); return (0); }
		if (st & 0x08) {
			hdmi_w(0x3027, 0x08);
			fprintf(stderr, "    PHY I2C ERROR reg=0x%02x\n", reg);
			return (-1);
		}
	}
	fprintf(stderr, "    PHY I2C TIMEOUT reg=0x%02x\n", reg);
	return (-1);
}

/* -------------------------------------------------------------------------
 * Step 5: Innosilicon PHY init for 74.25 MHz
 *
 * Sequence mirrors Linux inno_hdmi_phy_power_on() / rockchip_hdmi_phy_init():
 *   1. Assert PHY reset, power down
 *   2. Setup I2C master divider
 *   3. Write MPLL table via I2C
 *   4. Write drive config via I2C
 *   5. Power up PHY: two-step sequence
 *   6. Release PHY reset
 *   7. Poll for lock
 */
static void
step5_phy(void)
{
	int i, rc;

	/* 1. Assert PHY reset, power down */
	hdmi_w(0x4005, 0x00);
	hdmi_w(0x3000, 0x00);
	usleep(2000);

	/* 2. I2C master divider */
	hdmi_w(0x3029, 0x17);
	hdmi_w(0x3027, 0xff);
	hdmi_w(0x3028, 0xff);

	/* 3+4. Write MPLL + drive tables (done inline below) */
	printf("    Writing MPLL table (%d regs)...\n", PHY_74250_MPLL_N);
	for (i = 0; i < PHY_74250_MPLL_N; i++) {
		rc = phy_i2c_wr(phy_74250_mpll[i].addr, phy_74250_mpll[i].val);
		printf("      [0x%02x]=0x%04x %s\n",
		    phy_74250_mpll[i].addr, phy_74250_mpll[i].val,
		    rc == 0 ? "OK" : "FAIL");
	}
	printf("    Writing drive config (%d regs)...\n", PHY_74250_DRIVE_N);
	for (i = 0; i < PHY_74250_DRIVE_N; i++) {
		rc = phy_i2c_wr(phy_74250_drive[i].addr, phy_74250_drive[i].val);
		printf("      [0x%02x]=0x%04x %s\n",
		    phy_74250_drive[i].addr, phy_74250_drive[i].val,
		    rc == 0 ? "OK" : "FAIL");
	}

	/* 5. Power up PHY: two-step sequence */
	/*    Step1: PDZ|ENTMDS|SPARECTRL|SELDATAENPOL = 0xE2 (SPARECTRL was missing!) */
	/*    Step2: add GEN2_TXPWRON = 0xF2 */
	hdmi_w(0x3000, 0xe2);
	usleep(5000);
	printf("    PHY_CONF0 step1: 0x%02x (want 0xe2)\n", hdmi_r(0x3000));
	hdmi_w(0x3000, 0xf2);
	usleep(2000);
	printf("    PHY_CONF0 step2: 0x%02x (want 0xf2)\n", hdmi_r(0x3000));
	/* 6. Release PHY reset */
	hdmi_w(0x4005, 0x01);
	usleep(5000);

	/* 7. Poll for lock */
	printf("    Polling for TX_PHY_LOCK...\n");
	for (i = 0; i < 50; i++) {
		uint8_t stat = hdmi_r(0x3004);
		uint8_t ih   = hdmi_r(0x0104);
		if ((i % 5) == 0 || (stat & 0x12))
			printf("      [%3d ms] PHY_STAT0=0x%02x IH_PHY=0x%02x%s%s\n",
			    i * 10, stat, ih,
			    (stat & 0x10) ? " LOCKED" : "",
			    (stat & 0x02) ? " HPD" : "");
		if (stat & 0x10) {
			printf("    PHY locked! PHY_STAT0=0x%02x HPD=%d\n",
			    stat, (stat >> 1) & 1);
			return;
		}
		usleep(10000);
	}
	printf("    PHY lock TIMEOUT. PHY_STAT0=0x%02x\n", hdmi_r(0x3004));
}



/* -------------------------------------------------------------------------
 * Step 6: VOP commit
 * ---------------------------------------------------------------------- */

static void
step6_commit(void)
{
        printf("\n[6] VOP final commit (REG_CFG_DONE)...\n");
        /*
         * VOP was already committed in step3 to give the PHY a pixel clock.
         * This second commit picks up any remaining shadow register changes.
         */
        vop_w(0x0000, 0x01);
        usleep(40000);
        printf("    WIN0_CTRL0:    0x%08x  (expect 0x00000001)\n", vop_r(0x0030));
        printf("    WIN0_YRGB_MST: 0x%08x  (expect fb_pa)\n",     vop_r(0x0040));
        printf("    SYS_CTRL:      0x%08x  (bit11=0 bit1=1 wanted)\n", vop_r(0x0008));
}

/* -------------------------------------------------------------------------
 * Step 7: Report
 * ---------------------------------------------------------------------- */

static void
step7_report(void)
{
        uint8_t phy_stat, ih_phy;

        printf("\n[7] Final state...\n");
        phy_stat = hdmi_r(0x3004);
        ih_phy   = hdmi_r(0x0104);

        printf("    PHY_STAT0   [0x3004]: 0x%02x", phy_stat);
        if (phy_stat & 0x10) printf(" [TX_PHY_LOCK]");
        if (phy_stat & 0x02) printf(" [HPD]");
        printf("\n");
        printf("    IH_PHY_STAT [0x0104]: 0x%02x\n", ih_phy);
        printf("    MC_SWRSTZREQ[0x4002]: 0x%02x\n", hdmi_r(0x4002));
        printf("    FC_INVIDCONF[0x1000]: 0x%02x\n", hdmi_r(0x1000));
        printf("    VOP WIN0_CTRL0:       0x%08x\n", vop_r(0x0030));
        printf("    VOP WIN0_YRGB_MST:    0x%08x\n", vop_r(0x0040));

        printf("\n");
        if ((phy_stat & 0x12) == 0x12)
                printf("  >> PHY locked + HPD - display should be active\n");
        else if (phy_stat & 0x10)
                printf("  >> PHY locked, no HPD - check cable/monitor\n");
        else if (phy_stat & 0x02)
                printf("  >> HPD present, PHY not locked - check PHY config\n");
        else
                printf("  >> No lock, no HPD - check cable and init sequence\n");
}
/* -------------------------------------------------------------------------
 * Step 0b: Program VPLL to 74.25 MHz
 *
 * RK3399 VPLL target: 74.25 MHz (720p60 pixel clock)
 *
 * Valid integer config (VCO must be 800-1600 MHz per RK3399 TRM):
 *   FBDIV=99  REFDIV=2  POSTDIV1=4  POSTDIV2=4
 *   VCO  = 24 * 99 / 2          = 1188 MHz  (in range)
 *   Fout = 1188 / (4 * 4)       = 74.25 MHz
 *
 * Previous (broken) config: FBDIV=99, REFDIV=1, PD1=4, PD2=8
 *   VCO = 24*99/1 = 2376 MHz - out of spec, PLL unreliable.
 *
 * CON0 [0x0020]: bits[11:0] = FBDIV
 * CON1 [0x0024]: bits[12:10]=POSTDIV2, bits[8:6]=POSTDIV1 ... wait, RK3399 TRM:
 *   CON1: [14:12]=POSTDIV2, [10:8]=POSTDIV1 -- NO.
 *   Actual from Linux clk-rk3399.c RK3036_PLLCON1():
 *     bits[12:10] = POSTDIV2
 *     bits[9:8]   = (unused/reserved in RK3036 layout but RK3399 uses)
 *   Use observed working values from Linux driver directly:
 *     CON1 for PD2=4,PD1=4,REFDIV=2: (4<<12)|(4<<8)|2 = 0x4402
 *
 * CON3 [0x002c]: bit[3]=DSMPD (1=integer mode), bit[0]=PWRDOWN
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Step 0b: Set HDMI pixel clock to 74.25 MHz via GPLL/8
 *
 * GPLL = 594 MHz (stable, kernel-managed for peripherals, never reprogrammed).
 * 594 / 8 = 74.25 MHz exactly.
 *
 * CLKSEL49 [0x00c4]:
 *   bits[9:8] = clock source: 00=CPLL, 01=GPLL, 10=VPLL, 11=NPLL
 *   bits[7:0] = divider: pixel_clk = src / (div+1)
 *
 * For GPLL/8: src=GPLL(01), div=7(0x07)
 * No PLL reprogramming needed -- GPLL is already locked at 594 MHz.
 * ---------------------------------------------------------------------- */

static void
step0b_vpll(void)
{
        uint32_t before, after;

        printf("\n[0b] Setting HDMI pixel clock: GPLL/8 = 74.25 MHz...\n");

        before = cru_r(0x00c4);
        printf("     CLKSEL49 before: 0x%08x\n", before);
        printf("     GPLL CON2: 0x%08x  (bit31=lock: %d)\n",
            cru_r(0x0088), (cru_r(0x0088)>>31)&1);

        /* CLKSEL49: src=GPLL(bits[9:8]=01), div=7(bits[7:0]=0x07) -> 594/8=74.25 MHz */
        cru_hiword(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);

        after = cru_r(0x00c4);
        printf("     CLKSEL49 after:  0x%08x  (want bits[9:0]=0x107)\n", after);

        if ((after & 0x3ff) == 0x107)
                printf("     Pixel clock: GPLL(594 MHz)/8 = 74.25 MHz  OK\n");
        else
                printf("     WARNING: CLKSEL49 bits[9:0]=0x%03x (expect 0x107)\n",
                    after & 0x3ff);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
        struct rkfb_info info;
        struct rkfb_fill fill;

        printf("rkfb_init - RK3399 display bring-up (720p60)\n");
        printf("=============================================\n\n");

        /* Open /dev/mem for HDMI/CRU/GRF access */
        g_memfd = open("/dev/mem", O_RDWR);
        if (g_memfd < 0)
                err(1, "open /dev/mem");

        g_hdmi = mmap(NULL, HDMI_SIZE, PROT_READ|PROT_WRITE,
            MAP_SHARED, g_memfd, HDMI_PA);
        if (g_hdmi == MAP_FAILED)
                err(1, "mmap HDMI");

        g_cru = mmap(NULL, CRU_SIZE, PROT_READ|PROT_WRITE,
            MAP_SHARED, g_memfd, CRU_PA);
        if (g_cru == MAP_FAILED)
                err(1, "mmap CRU");

        g_grf = mmap(NULL, GRF_SIZE, PROT_READ|PROT_WRITE,
            MAP_SHARED, g_memfd, GRF_PA);
        if (g_grf == MAP_FAILED)
                err(1, "mmap GRF");

	g_viogrf = mmap(NULL, VIOGRF_SIZE, PROT_READ|PROT_WRITE,
    MAP_SHARED, g_memfd, VIOGRF_PA);
if (g_viogrf == MAP_FAILED)
    err(1, "mmap VIO GRF");

        /* Open /dev/rkfb0 for VOP access */
        g_rkfb = open("/dev/rkfb0", O_RDWR);
        if (g_rkfb < 0)
                err(1, "open /dev/rkfb0");

        if (ioctl(g_rkfb, RKFB_GETINFO, &info) < 0)
                err(1, "RKFB_GETINFO");

        printf("Framebuffer: %ux%u %ubpp stride=%u pa=0x%016llx\n\n",
            info.width, info.height, info.bpp, info.stride,
            (unsigned long long)info.fb_pa);

        /* Fill framebuffer with blue test pattern */
        fill.pixel = 0x000000ff;
        if (ioctl(g_rkfb, RKFB_CLEAR, &fill) < 0)
                err(1, "RKFB_CLEAR");
        printf("Framebuffer filled with blue.\n");

        step0_clocks();
        step0b_vpll();
	step1_reset_release();
        step2_grf_mux();
        step3_vop((uint32_t)(info.fb_pa & 0xffffffff));
        step4_fc_720p60();
        step5_phy();
        step6_commit();
        step7_report();

        munmap((void *)g_hdmi, HDMI_SIZE);
        munmap((void *)g_cru,  CRU_SIZE);
        munmap((void *)g_grf,  GRF_SIZE);
        close(g_memfd);
        close(g_rkfb);
        return (0);
}
