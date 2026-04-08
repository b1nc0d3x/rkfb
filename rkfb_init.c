/*
 * rkfb_init.c - RK3399 / RockPro64 display bring-up tool
 *
 * All hardware access goes through /dev/rkfb0 ioctls.
 * No /dev/mem required - works without root.
 *
 * Run once at boot: /usr/local/sbin/rkfb_init
 *
 * Sequence:
 *   0.  Enable HDMI clocks via CRU
 *   0b. Set pixel clock: GPLL/8 = 74.25 MHz
 *   1.  Release HDMI soft reset
 *   2.  Configure GRF mux: VOPB -> HDMI (VIO GRF)
 *   3.  Configure VOP WIN0 scanout + timing (720p60)
 *   4.  Program HDMI frame composer (720p60)
 *   5.  Init Innosilicon PHY at 74.25 MHz
 *   6.  Commit VOP REG_CFG_DONE
 *   7.  Report final state
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

#include "rkfb_ioctl.h"

static int g_rkfb = -1;

/* -------------------------------------------------------------------------
 * Register accessors via /dev/rkfb0 ioctls
 * ---------------------------------------------------------------------- */

static uint32_t
reg_r(uint32_t block, uint32_t off)
{
	struct rkfb_regop ro = { .block = block, .off = off };
	if (ioctl(g_rkfb, RKFB_REG_READ, &ro) < 0)
		err(1, "reg_r block=%u off=0x%04x", block, off);
	return ro.val;
}

static void
reg_w(uint32_t block, uint32_t off, uint32_t val)
{
	struct rkfb_regop ro = { .block = block, .off = off, .val = val };
	if (ioctl(g_rkfb, RKFB_REG_WRITE, &ro) < 0)
		err(1, "reg_w block=%u off=0x%04x val=0x%08x", block, off, val);
}

/* HIWORD_UPDATE write: mask selects which bits to change */
static void
reg_hiword(uint32_t block, uint32_t off, uint32_t mask, uint32_t val)
{
	reg_w(block, off, (mask << 16) | (val & mask));
}

/* Convenience wrappers */
static inline uint32_t cru_r(uint32_t off)    { return reg_r(RKFB_BLOCK_CRU,    off); }
static inline void      cru_w(uint32_t off, uint32_t v) { reg_w(RKFB_BLOCK_CRU, off, v); }
static inline void      cru_hiword(uint32_t off, uint32_t mask, uint32_t val)
                                               { reg_hiword(RKFB_BLOCK_CRU, off, mask, val); }

static inline uint32_t viogrf_r(uint32_t off) { return reg_r(RKFB_BLOCK_VIOGRF, off); }
static inline void      viogrf_hiword(uint32_t off, uint32_t mask, uint32_t val)
                                               { reg_hiword(RKFB_BLOCK_VIOGRF, off, mask, val); }

static inline uint32_t vop_r(uint32_t off)    { return reg_r(RKFB_BLOCK_VOP,    off); }
static inline void      vop_w(uint32_t off, uint32_t v) { reg_w(RKFB_BLOCK_VOP, off, v); }

/*
 * HDMI: 8-bit registers, accessed via /dev/rkfb0 ioctls.
 * Writes use RKFB_HDMI_REG_WRITE → kernel bus_space_write_4 (32-bit word,
 * value in low byte) to avoid byte-write PSLVERR on RK3399 APB bridge.
 */
static uint8_t
hdmi_r(uint32_t off)
{
	struct rkfb_regop ro = { .block = RKFB_BLOCK_HDMI, .off = off };
	if (ioctl(g_rkfb, RKFB_REG_READ, &ro) < 0)
		err(1, "hdmi_r off=0x%04x", off);
	return (uint8_t)(ro.val & 0xff);
}

static void
hdmi_w(uint32_t off, uint8_t val)
{
	struct rkfb_regop ro = { .block = RKFB_BLOCK_HDMI, .off = off, .val = val };
	if (ioctl(g_rkfb, RKFB_HDMI_REG_WRITE, &ro) < 0)
		err(1, "hdmi_w off=0x%04x val=0x%02x", off, val);
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
}

/* -------------------------------------------------------------------------
 * Step 0b: Set HDMI pixel clock to 74.25 MHz via GPLL/8
 *
 * GPLL = 594 MHz. 594 / 8 = 74.25 MHz.
 * CLKSEL49 [0x00c4]: bits[9:8]=src, bits[7:0]=div-1
 *   src=01 (GPLL), div=7 -> 594/8 = 74.25 MHz
 * ---------------------------------------------------------------------- */

static void
step0b_pixclk(void)
{
	uint32_t before, after;

	printf("\n[0b] Setting pixel clock: GPLL/8 = 74.25 MHz...\n");
	before = cru_r(0x00c4);
	printf("     CLKSEL49 before: 0x%08x\n", before);
	printf("     GPLL CON2: 0x%08x  (bit31=lock: %d)\n",
	    cru_r(0x0088), (cru_r(0x0088) >> 31) & 1);

	cru_hiword(0x00c4, (3u<<8) | 0xffu, (1u<<8) | 0x07u);

	after = cru_r(0x00c4);
	printf("     CLKSEL49 after:  0x%08x  (want bits[9:0]=0x107)\n", after);
	if ((after & 0x3ff) == 0x107)
		printf("     OK: GPLL(594 MHz)/8 = 74.25 MHz\n");
	else
		printf("     WARNING: bits[9:0]=0x%03x (expect 0x107)\n", after & 0x3ff);
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

	hdmi_w(0x4001, 0x74);   /* MC_CLKDIS: pixel+TMDS clk on, others gated */
	hdmi_w(0x4002, 0xff);   /* MC_SWRSTZREQ: release ALL soft resets */
	usleep(5000);

	printf("    MC_SWRSTZREQ after:  0x%02x  (want 0xff)\n", hdmi_r(0x4002));
	printf("    MC_CLKDIS    after:  0x%02x  (want 0x74)\n", hdmi_r(0x4001));
}

/* -------------------------------------------------------------------------
 * Step 2: GRF mux - VOPB -> HDMI (VIO GRF SOC_CON20 bit6)
 * ---------------------------------------------------------------------- */

static void
step2_grf_mux(void)
{
	printf("\n[2] GRF mux: VOPB -> HDMI (RK3399_HDMI_LCDC_SEL bit6=1)...\n");

	/*
	 * RK3399_HDMI_LCDC_SEL bit6: 1=VOPB, 0=VOPL
	 * The exact register location varies by source:
	 * - Some say VIO GRF offset 0x0050 (SOC_CON20 = word 20 * 4)
	 * - Some say VIO GRF offset 0x0250
	 * - Linux uses sys GRF with offset RK3399_GRF_SOC_CON20
	 * Try both VIO GRF offsets; sys GRF is also tried via RKFB_BLOCK_GRF.
	 */
	/*
	 * Try all known locations for HDMI_LCDC_SEL (VOPB=1 / VOPL=0):
	 *   - VIO GRF (0xFF770000) offset 0x0050  (VIO_GRF_SOC_CON20 = 20*4)
	 *   - SYS GRF (0xFF320000) offset 0x6250  (RK3399_GRF_SOC_CON20 from Linux)
	 * Linux dw_hdmi-rockchip.c uses the SYS GRF via rockchip,grf phandle.
	 */
	printf("    VIO GRF[0x0050] before: 0x%08x\n", viogrf_r(0x0050));
	printf("    SYS GRF[0x6250] before: 0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x6250));

	viogrf_hiword(0x0050, (1<<6), (1<<6));
	reg_hiword(RKFB_BLOCK_GRF, 0x6250, (1<<6), (1<<6));
	usleep(1000);

	printf("    VIO GRF[0x0050] after:  0x%08x\n", viogrf_r(0x0050));
	printf("    SYS GRF[0x6250] after:  0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x6250));
}

/* -------------------------------------------------------------------------
 * Step 3: VOP WIN0 scanout + display timing (720p60)
 * ---------------------------------------------------------------------- */

static void
step3_vop(uint32_t fb_pa)
{
	uint32_t sys_ctrl;

	printf("\n[3] VOP WIN0 + 720p60 timing...\n");
	printf("    SYS_CTRL before: 0x%08x\n", vop_r(0x0008));

	/*
	 * Write timing and WIN0 to shadow registers WHILE VOP IS IN STANDBY.
	 * Commit (REG_CFG_DONE), then de-standby. The VOP uses shadow values
	 * on its first frame after leaving standby.
	 *
	 * 720p60 timing (74.25 MHz pixel clock):
	 * H: active=1280, fp=110, sync=40, bp=220, total=1650
	 * V: active=720,  fp=5,   sync=5,  bp=20,  total=750
	 */
	vop_w(0x0188, 0x06710027);   /* DSP_HTOTAL_HS_END */
	vop_w(0x018c, 0x01040604);   /* DSP_HACT_ST_END   */
	vop_w(0x0190, 0x02ed0004);   /* DSP_VTOTAL_VS_END */
	vop_w(0x0194, 0x001902e9);   /* DSP_VACT_ST_END   */

	/* WIN0: ARGB8888, 1280x720 */
	vop_w(0x003c, 0x05000500);               /* WIN0_VIR */
	vop_w(0x0040, fb_pa);                    /* WIN0_YRGB_MST */
	vop_w(0x0048, ((720-1)<<16)|(1280-1));   /* WIN0_ACT_INFO */
	vop_w(0x004c, ((720-1)<<16)|(1280-1));   /* WIN0_DSP_INFO */
	vop_w(0x0050, (25<<16)|260);             /* WIN0_DSP_ST */
	vop_w(0x0030, 0x00000001);               /* WIN0_CTRL0: enable, ARGB8888 */

	/*
	 * SYS_CTRL (0x0008) output enable bits (NOT shadow — take effect now):
	 *   bit11: standby_en  (0=running)
	 *   bit12: rgb_out_en  (clear — not using RGB parallel)
	 *   bit13: hdmi_out_en (1=VOP feeds HDMI encoder)  ← THE KEY BIT
	 *   bit14: edp_out_en  (clear)
	 *
	 * Must set bit13 so the VOP actually drives the DW-HDMI controller.
	 * Without it the VOP runs but its output goes nowhere useful.
	 */
	sys_ctrl  = vop_r(0x0008);
	sys_ctrl &= ~(1u << 11);   /* clear standby */
	sys_ctrl &= ~(1u << 12);   /* clear rgb_out_en */
	sys_ctrl &= ~(1u << 14);   /* clear edp_out_en */
	sys_ctrl |=  (1u << 13);   /* SET hdmi_out_en */
	vop_w(0x0008, sys_ctrl);

	/* Commit shadow → active */
	printf("    Committing shadow regs...\n");
	vop_w(0x0000, 0x01);   /* REG_CFG_DONE */

	/* Wait for VOP to start and shadow commit to take effect */
	usleep(40000);   /* 2+ frames at 60Hz */

	printf("    SYS_CTRL:          0x%08x  (want bit13=1 bit11=0)\n", vop_r(0x0008));
	printf("    WIN0_CTRL0:        0x%08x  (expect 0x00000001)\n", vop_r(0x0030));
	printf("    WIN0_YRGB_MST:     0x%08x  (expect 0x%08x)\n", vop_r(0x0040), fb_pa);
	printf("    DSP_HTOTAL_HS_END: 0x%08x  (expect 0x06710027)\n", vop_r(0x0188));
	printf("    DSP_VTOTAL_VS_END: 0x%08x  (expect 0x02ed0004)\n", vop_r(0x0190));
}

/* -------------------------------------------------------------------------
 * Step 4: HDMI frame composer 720p60
 * ---------------------------------------------------------------------- */

static void
step4_fc_720p60(void)
{
	printf("\n[4] HDMI frame composer (720p60)...\n");

	hdmi_w(0x1000, 0x78);   /* FC_INVIDCONF: HDMI, VSYNC_H, HSYNC_H, DE_H */
	hdmi_w(0x1001, 0x00);   /* FC_INHACTV0: hactive=1280 low */
	hdmi_w(0x1002, 0x05);   /* FC_INHACTV1: hactive=1280 high */
	hdmi_w(0x1003, 0x72);   /* FC_INHBLANK0: hblank=370 low */
	hdmi_w(0x1004, 0x01);   /* FC_INHBLANK1: hblank=370 high */
	hdmi_w(0x1005, 0xd0);   /* FC_INVACTV0: vactive=720 low */
	hdmi_w(0x1006, 0x02);   /* FC_INVACTV1: vactive=720 high */
	hdmi_w(0x1007, 0x1e);   /* FC_INVBLANK: vblank=30 */
	hdmi_w(0x1008, 0x6e);   /* FC_HSYNCINDELAY0: hfp=110 low */
	hdmi_w(0x1009, 0x00);   /* FC_HSYNCINDELAY1 */
	hdmi_w(0x100a, 0x28);   /* FC_HSYNCINWIDTH0: hsync=40 low */
	hdmi_w(0x100b, 0x00);   /* FC_HSYNCINWIDTH1 */
	hdmi_w(0x100c, 0x05);   /* FC_VSYNCINDELAY: vfp=5 */
	hdmi_w(0x100d, 0x05);   /* FC_VSYNCINWIDTH: vsync=5 */

	/*
	 * Control period duration registers — required for HDMI (not DVI).
	 * Without these, TMDS control periods between data islands are malformed
	 * and many monitors will not sync.
	 */
	hdmi_w(0x1011, 12);     /* FC_CTRLDUR: control period min duration */
	hdmi_w(0x1012, 32);     /* FC_EXCTRLDUR: extended control period duration */
	hdmi_w(0x1013, 1);      /* FC_EXCTRLSPAC: extended ctrl period spacing */
	hdmi_w(0x1014, 0x0b);   /* FC_CH0PREAM: channel 0 preamble */
	hdmi_w(0x1015, 0x16);   /* FC_CH1PREAM: channel 1 preamble */
	hdmi_w(0x1016, 0x21);   /* FC_CH2PREAM: channel 2 preamble */

	hdmi_w(0x1017, 0x00);   /* FC_AVICONF0: RGB colorspace */
	hdmi_w(0x1018, 0x08);   /* FC_AVICONF1: 16:9 aspect ratio */
	hdmi_w(0x1019, 0x00);   /* FC_AVICONF2 */
	hdmi_w(0x101b, 0x04);   /* FC_AVIVID: VIC 4 = 720p60 */

	/*
	 * TX input video mapping — tell the DW-HDMI encoder what format the
	 * VOP pixel bus carries. Without this the encoder doesn't know the
	 * input is RGB888 and produces garbage or no output.
	 *
	 * TX_INVID0 (0x0200): bits[6:1] = video_mapping, bit7 = internal DE gen
	 *   RGB888 mapping code = 1, placed at bits[6:1] → byte value = 1<<1 = 0x02
	 *   bit7 = 0 (use external DE from VOP, not internal generator)
	 * TX_INSTUFFING (0x0201): enable stuffing for all 3 data channels (0x07)
	 * TX_GYDATA/RCRDATA/BCBDATA: stuffing values (0 for black)
	 */
	hdmi_w(0x0200, 0x02);   /* TX_INVID0: RGB888 mapping (code=1 at bits[6:1] = 1<<1) */
	hdmi_w(0x0201, 0x07);   /* TX_INSTUFFING: enable GY/RCR/BCB stuffing */
	hdmi_w(0x0202, 0x00);   /* TX_GYDATA0  */
	hdmi_w(0x0203, 0x00);   /* TX_GYDATA1  */
	hdmi_w(0x0204, 0x00);   /* TX_RCRDATA0 */
	hdmi_w(0x0205, 0x00);   /* TX_RCRDATA1 */
	hdmi_w(0x0206, 0x00);   /* TX_BCBDATA0 */
	hdmi_w(0x0207, 0x00);   /* TX_BCBDATA1 */

	/*
	 * Video Packetizer: 8-bit RGB, bypass pixel packer.
	 * bypass_en is bit6 (0x40). output_selector bits[1:0]=11 (0x03) = bypass.
	 * 0x43 = bypass_en(bit6) | output_selector(bits[1:0]=11)
	 * NB: 0x47 is wrong — bit2 selects pixel_repeater bypass instead of
	 *     vid_packetizer bypass, preventing proper video path.
	 */
	hdmi_w(0x0801, 0x00);   /* VP_PR_CD: no pixel repeat, 24-bit */
	hdmi_w(0x0802, 0x07);   /* VP_STUFF: stuffing enable */
	hdmi_w(0x0803, 0x00);   /* VP_REMAP: no remap */
	hdmi_w(0x0804, 0x43);   /* VP_CONF: bypass_en=bit6, output=bypass(bits[1:0]=11) */

	/* CSC: bypass (RGB in, RGB out) */
	hdmi_w(0x4004, 0x01);   /* MC_FLOWCTRL: CSC bypass mode */

	hdmi_w(0x01ff, 0x00);   /* IH_MUTE: unmute global interrupts */
	hdmi_w(0x0184, 0xfe);   /* IH_MUTE_PHY: unmask HPD interrupt */

	printf("    FC_INVIDCONF: 0x%02x  (expect 0x78)\n", hdmi_r(0x1000));
	printf("    FC_CTRLDUR:   0x%02x  (expect 0x0c)\n", hdmi_r(0x1011));
	printf("    FC_INHACTV:   0x%02x%02x  (expect 0x0500)\n",
	    hdmi_r(0x1002), hdmi_r(0x1001));
	printf("    FC_INVACTV:   0x%02x%02x  (expect 0x02d0)\n",
	    hdmi_r(0x1006), hdmi_r(0x1005));
	printf("    VP_CONF:      0x%02x  (expect 0x47)\n", hdmi_r(0x0804));
	printf("    MC_FLOWCTRL:  0x%02x  (expect 0x01)\n", hdmi_r(0x4004));
}

/* -------------------------------------------------------------------------
 * Step 5: Innosilicon PHY init for 74.25 MHz
 * ---------------------------------------------------------------------- */

struct phy_reg { uint8_t addr; uint16_t val; };

/*
 * MPLL + drive config for 74.25 MHz (720p60).
 * Source: Linux phy-rockchip-inno-hdmi.c rockchip_mpll_cfg[] /
 *         rockchip_phy_config[], GPLv2, Rockchip.
 */
static const struct phy_reg phy_74250_mpll[] = {
	{ 0x06, 0x0008 },
	{ 0x0d, 0x0000 },
	{ 0x0e, 0x0260 },
	{ 0x10, 0x8009 },
	{ 0x19, 0x0000 },
	{ 0x1e, 0x0000 },
	{ 0x25, 0x0272 },
};
#define PHY_MPLL_N (int)(sizeof(phy_74250_mpll)/sizeof(phy_74250_mpll[0]))

static const struct phy_reg phy_74250_drive[] = {
	{ 0x19, 0x0004 },
	{ 0x1e, 0x8009 },
	{ 0x25, 0x0272 },
};
#define PHY_DRIVE_N (int)(sizeof(phy_74250_drive)/sizeof(phy_74250_drive[0]))

static int
phy_i2c_wr(uint8_t reg, uint16_t val)
{
	int i;
	uint8_t st;

	hdmi_w(0x3020, 0x69);              /* PHY_I2CM_SLAVE */
	hdmi_w(0x3021, reg);               /* PHY_I2CM_ADDR  */
	hdmi_w(0x3022, (val >> 8) & 0xff); /* PHY_I2CM_DATAO_1 (MSB) */
	hdmi_w(0x3023, val & 0xff);        /* PHY_I2CM_DATAO_0 (LSB) */
	hdmi_w(0x3026, 0x10);              /* PHY_I2CM_OPERATION: write */

	for (i = 0; i < 100; i++) {
		usleep(500);
		st = hdmi_r(0x3027);
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

static void
step5_phy(void)
{
	int i, rc;

	printf("\n[5] Innosilicon PHY init (74.25 MHz)...\n");

	/* Setup I2C divider, clear pending interrupts */
	hdmi_w(0x3029, 0x0b);   /* PHY_I2CM_DIV */
	hdmi_w(0x3027, 0xff);
	hdmi_w(0x3028, 0xff);
	phy_i2c_wr(0x02, 0x0000);   /* clear PDATAEN */

	printf("    Writing MPLL table (%d regs)...\n", PHY_MPLL_N);
	for (i = 0; i < PHY_MPLL_N; i++) {
		rc = phy_i2c_wr(phy_74250_mpll[i].addr, phy_74250_mpll[i].val);
		printf("      [0x%02x]=0x%04x %s\n",
		    phy_74250_mpll[i].addr, phy_74250_mpll[i].val,
		    rc == 0 ? "OK" : "FAIL");
	}
	printf("    Writing drive config (%d regs)...\n", PHY_DRIVE_N);
	for (i = 0; i < PHY_DRIVE_N; i++) {
		rc = phy_i2c_wr(phy_74250_drive[i].addr, phy_74250_drive[i].val);
		printf("      [0x%02x]=0x%04x %s\n",
		    phy_74250_drive[i].addr, phy_74250_drive[i].val,
		    rc == 0 ? "OK" : "FAIL");
	}

	/* Power up PHY (PHY_CONF0 = 0xee from working state) */
	hdmi_w(0x3000, 0xee);
	usleep(5000);
	printf("    PHY_CONF0: 0x%02x  (want 0xee)\n", hdmi_r(0x3000));

	/* Release PHY reset */
	hdmi_w(0x4005, 0x01);   /* MC_PHYRSTZ */
	usleep(5000);

	/* Poll for TX_PHY_LOCK (bit4) */
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
 * Step 6: Final VOP commit
 * ---------------------------------------------------------------------- */

static void
step6_commit(void)
{
	printf("\n[6] VOP final commit...\n");
	vop_w(0x0000, 0x01);
	usleep(40000);
	printf("    WIN0_CTRL0:    0x%08x  (expect 0x00000001)\n", vop_r(0x0030));
	printf("    WIN0_YRGB_MST: 0x%08x\n", vop_r(0x0040));
	printf("    SYS_CTRL:      0x%08x\n", vop_r(0x0008));
}

/* -------------------------------------------------------------------------
 * Step 7: Final report
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
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
	struct rkfb_info info;
	struct rkfb_fill fill;

	printf("rkfb_init - RK3399 display bring-up (720p60)\n");
	printf("=============================================\n\n");

	g_rkfb = open("/dev/rkfb0", O_RDWR);
	if (g_rkfb < 0)
		err(1, "open /dev/rkfb0");

	if (ioctl(g_rkfb, RKFB_GETINFO, &info) < 0)
		err(1, "RKFB_GETINFO");

	printf("Framebuffer: %ux%u %ubpp stride=%u pa=0x%016llx\n",
	    info.width, info.height, info.bpp, info.stride,
	    (unsigned long long)info.fb_pa);
	printf("HDMI design_id=0x%02x rev=0x%02x\n\n",
	    hdmi_r(0x0000), hdmi_r(0x0004));

	/* Fill framebuffer with blue test pattern */
	fill.pixel = 0x000000ff;
	if (ioctl(g_rkfb, RKFB_CLEAR, &fill) < 0)
		err(1, "RKFB_CLEAR");
	printf("Framebuffer filled with blue.\n");

	step0_clocks();
	step0b_pixclk();
	step1_reset_release();
	step2_grf_mux();
	step3_vop((uint32_t)(info.fb_pa & 0xffffffff));
	step4_fc_720p60();
	step5_phy();
	step6_commit();
	step7_report();

	close(g_rkfb);
	return (0);
}
