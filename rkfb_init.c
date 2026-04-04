/*
 * rkfb_init.c — RK3399 / RockPro64 HDMI + VOP bring-up sequencer
 *
 * Targets: 720p60 (1280x720 @ 74.25 MHz) via VOP big -> HDMI
 *
 * Requires rkfb.ko loaded (/dev/rkfb0 present).
 * Uses rkfb ioctls for register access — no raw /dev/mem in this tool.
 *
 * Sequence:
 *   1. Release HDMI soft reset
 *   2. Configure GRF mux: VOPB -> HDMI
 *   3. Configure VOP big for HDMI output, point WIN0 at framebuffer
 *   4. Configure HDMI frame composer (720p60 timing)
 *   5. Init Innosilicon PHY via I2C master (74.25 MHz)
 *   6. Enable VOP output, commit REG_CFG_DONE
 *   7. Verify and report
 *
 * Build: cc -o rkfb_init rkfb_init.c
 * Run:   ./rkfb_init
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

/* -------------------------------------------------------------------------
 * Register access helpers via rkfb ioctls
 * ---------------------------------------------------------------------- */

static int g_fd;

static uint32_t
vop_r(uint32_t off)
{
	struct rkfb_regop ro = { .block = 0, .off = off, .val = 0 };
	if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0)
		err(1, "vop_r(0x%04x)", off);
	return ro.val;
}

static void
vop_w(uint32_t off, uint32_t val)
{
	struct rkfb_regop ro = { .block = 0, .off = off, .val = val };
	if (ioctl(g_fd, RKFB_REG_WRITE, &ro) < 0)
		err(1, "vop_w(0x%04x, 0x%08x)", off, val);
}

static uint32_t
grf_r(uint32_t off)
{
	struct rkfb_regop ro = { .block = 1, .off = off, .val = 0 };
	if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0)
		err(1, "grf_r(0x%04x)", off);
	return ro.val;
}

static void
grf_w(uint32_t off, uint32_t val)
{
	struct rkfb_regop ro = { .block = 1, .off = off, .val = val };
	if (ioctl(g_fd, RKFB_REG_WRITE, &ro) < 0)
		err(1, "grf_w(0x%04x)", off);
}

static uint32_t
cru_r(uint32_t off)
{
	struct rkfb_regop ro = { .block = 2, .off = off, .val = 0 };
	if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0)
		err(1, "cru_r(0x%04x)", off);
	return ro.val;
}

static void
cru_w(uint32_t off, uint32_t val)
{
	struct rkfb_regop ro = { .block = 2, .off = off, .val = val };
	if (ioctl(g_fd, RKFB_REG_WRITE, &ro) < 0)
		err(1, "cru_w(0x%04x)", off);
}

static uint8_t
hdmi_r(uint32_t off)
{
	struct rkfb_regop ro = { .block = 3, .off = off, .val = 0 };
	if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0)
		err(1, "hdmi_r(0x%04x)", off);
	return (uint8_t)ro.val;
}

static void
hdmi_w(uint32_t off, uint8_t val)
{
	struct rkfb_regop ro = { .block = 3, .off = off, .val = val };
	if (ioctl(g_fd, RKFB_HDMI_REG_WRITE, &ro) < 0)
		err(1, "hdmi_w(0x%04x)", off);
}

/*
 * GRF uses HIWORD_UPDATE: bits[31:16]=mask, bits[15:0]=value.
 * Only the bits covered by the mask are modified.
 */
static void
grf_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
	grf_w(off, (mask << 16) | (val & mask));
}

/*
 * CRU also uses HIWORD_UPDATE for clock gate registers.
 */
static void
cru_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
	cru_w(off, (mask << 16) | (val & mask));
}

/* -------------------------------------------------------------------------
 * PHY I2C master — write one 16-bit value to Innosilicon PHY register
 * ---------------------------------------------------------------------- */

static void
phy_i2c_write(uint8_t reg, uint16_t val)
{
	uint8_t stat;
	int timeout;

	hdmi_w(0x3020, 0x69);          /* PHY_I2CM_SLAVE  — Innosilicon addr */
	hdmi_w(0x3021, reg);           /* PHY_I2CM_ADDRESS */
	hdmi_w(0x3022, (val >> 8) & 0xff);  /* data MSB */
	hdmi_w(0x3023, val & 0xff);         /* data LSB */
	hdmi_w(0x3026, 0x10);          /* PHY_I2CM_OPERATION — write */

	for (timeout = 20; timeout > 0; timeout--) {
		usleep(1000);
		stat = hdmi_r(0x3027);     /* PHY_I2CM_INT */
		if (stat & 0x02) {
			hdmi_w(0x3027, 0x02);  /* clear done */
			return;
		}
		if (stat & 0x08) {
			hdmi_w(0x3027, 0x08);  /* clear error */
			fprintf(stderr, "PHY I2C error reg=0x%02x\n", reg);
			return;
		}
	}
	fprintf(stderr, "PHY I2C timeout reg=0x%02x\n", reg);
}

/* -------------------------------------------------------------------------
 * Step 1: Release HDMI soft reset
 *
 * MC_SWRSTZREQ [0x4002]: writing 0xff releases all blocks from reset.
 * MC_CLKDIS    [0x4001]: 0x00 = all clocks enabled (already set).
 * ---------------------------------------------------------------------- */

static void
step1_hdmi_reset_release(void)
{
	printf("\n[1] Releasing HDMI soft reset...\n");
	printf("    MC_SWRSTZREQ before: 0x%02x\n", hdmi_r(0x4002));

	hdmi_w(0x4001, 0x00);   /* MC_CLKDIS: all clocks on */
	hdmi_w(0x4002, 0xff);   /* MC_SWRSTZREQ: release all resets */
	usleep(5000);

	printf("    MC_SWRSTZREQ after:  0x%02x\n", hdmi_r(0x4002));
	printf("    MC_CLKDIS    after:  0x%02x\n", hdmi_r(0x4001));
}

/* -------------------------------------------------------------------------
 * Step 2: GRF mux — route VOPB output to HDMI
 *
 * GRF_SOC_CON20 at GRF+0x6250 (VIO GRF, which is in the expanded GRF map):
 *   bit 6 = hdmi_dclk_vop_sel: 0=VOPL, 1=VOPB
 *
 * GRF_SOC_CON4  at GRF+0x0410:
 *   bits[9:8] = hdmi_lcdc_sel (output mux):
 *               0b01 = VOPB feeds HDMI
 *
 * Note: the GRF block in rkfb is mapped from 0xff320000 with size 0x8000,
 * which covers both the main GRF (0xff320000) and reaches into the VIO GRF
 * area. GRF_SOC_CON20 is at 0xff770000 + 0x250 = VIO GRF, a separate block.
 * We use the main GRF SOC_CON4 to set the mux.
 * ---------------------------------------------------------------------- */

static void
step2_grf_mux(void)
{
	printf("\n[2] Configuring GRF mux: VOPB -> HDMI...\n");
	printf("    GRF_SOC_CON4  before: 0x%08x\n", grf_r(0x0410));
	printf("    GRF_SOC_CON20 before: 0x%08x\n", grf_r(0x6250));

	/*
	 * GRF_SOC_CON4 [0x0410]:
	 * bits[9:8]: hdmi lcdc select — 01 = VOPB
	 * Use HIWORD_UPDATE so we only touch bits 9:8.
	 */
	grf_hiword(0x0410, (3 << 8), (1 << 8));

	/*
	 * GRF_SOC_CON20 [0x6250] in VIO GRF (0xff770000 + 0x250).
	 * The rkfb GRF map starts at 0xff320000 and is 0x8000 bytes,
	 * which does NOT reach 0xff770000 — this is a separate peripheral.
	 * We skip this write for now; the SOC_CON4 mux above should suffice
	 * for the clock path. Revisit if DCLK polarity is wrong.
	 */

	usleep(1000);
	printf("    GRF_SOC_CON4  after:  0x%08x\n", grf_r(0x0410));
}

/* -------------------------------------------------------------------------
 * Step 3: VOP big — configure for HDMI output + WIN0 scanout
 *
 * SYS_CTRL [0x0008]:
 *   bits[23:22] = standby_en, vop_standby_en
 *   bits[21:20] = out_mode: 00=RGB, 01=YUV, 10=YUV420, 11=reserved
 *   bits[1:0]   = mipi_dclk_en, hdmi_dclk_en — set bit 1 for HDMI
 *
 * WIN0_CTRL0 [0x0030]:
 *   bit 0     = win0_en
 *   bits[4:1] = win0_data_fmt: 0000=ARGB8888
 *   bit 5     = win0_rb_swap
 *
 * DSP_CTRL0 [0x0010]: display timing source select
 * ---------------------------------------------------------------------- */

static void
step3_vop_configure(uint32_t fb_pa)
{
	uint32_t sys_ctrl;

	printf("\n[3] Configuring VOP big for HDMI + WIN0 scanout...\n");
	printf("    SYS_CTRL before:   0x%08x\n", vop_r(0x0008));
	printf("    WIN0_CTRL0 before: 0x%08x\n", vop_r(0x0030));

	/*
	 * SYS_CTRL: clear standby, select HDMI output (bit 1).
	 * Current value 0x20801800:
	 *   bit 29 = 1 (auto_gate_en)
	 *   bit 23 = 1 (mipi_dclk_en or standby related)
	 *   bit 12 = 1 (core_dclk_en)
	 * We preserve existing bits and set bit 1 (hdmi_dclk_en).
	 * Clear bits 23:22 (standby).
	 */
	sys_ctrl = vop_r(0x0008);
	sys_ctrl &= ~((3 << 22));   /* clear standby bits */
	sys_ctrl |=  (1 << 1);      /* hdmi_dclk_en */
	vop_w(0x0008, sys_ctrl);

	/* 720p: 1280x720, stride = 1280 * 4 = 5120 bytes = 1280 words */
	/* WIN0_VIR [0x003c]: stride in 32-bit words, both h and v = 0x500 */
	vop_w(0x003c, 0x05000500);

	/* WIN0_YRGB_MST [0x0040]: framebuffer physical address */
	vop_w(0x0040, fb_pa);

	/* WIN0_ACT_INFO [0x0048]: (height-1)<<16 | (width-1) */
	vop_w(0x0048, ((720 - 1) << 16) | (1280 - 1));

	/* WIN0_DSP_INFO [0x004c]: display size = same as active */
	vop_w(0x004c, ((720 - 1) << 16) | (1280 - 1));

	/* WIN0_DSP_ST [0x0050]: start position (HBP, VBP) for 720p60 */
	/* hbp=220, vbp=20 — subtract hsync+hfp to get display start */
	vop_w(0x0050, (20 << 16) | 220);

	/*
	 * WIN0_CTRL0 [0x0030]:
	 *   bit 0 = enable
	 *   bits[4:1] = fmt: 0000 = ARGB8888
	 * Write 0x00000011: enable + ARGB8888
	 * Note: VOP does NOT use HIWORD_UPDATE — plain write.
	 */
	vop_w(0x0030, 0x00000011);

	printf("    SYS_CTRL after:    0x%08x\n", vop_r(0x0008));
	printf("    WIN0_CTRL0 after:  0x%08x\n", vop_r(0x0030));
	printf("    WIN0_YRGB_MST:     0x%08x\n", vop_r(0x0040));
	printf("    WIN0_ACT_INFO:     0x%08x\n", vop_r(0x0048));
}

/* -------------------------------------------------------------------------
 * Step 4: HDMI frame composer — 720p60 timing
 *
 * CEA-861 VIC 4: 1280x720 @ 60Hz, pixel clock 74.25 MHz
 *   hactive=1280, hfp=110, hsync=40, hbp=220  -> hblank=370
 *   vactive=720,  vfp=5,   vsync=5,  vbp=20   -> vblank=30
 *
 * FC_INVIDCONF [0x1000]:
 *   bit 7 = hdmi_dvi_mode: 1=HDMI
 *   bit 6 = vsync_in_polarity: 1=active high
 *   bit 5 = hsync_in_polarity: 1=active high
 *   bit 4 = de_in_polarity: 1=active high
 *   bit 1 = r_v_blank_in_osync
 *   bit 0 = in_I_P: 0=progressive
 *   720p60: 0xe0 (HDMI|VSYNC_H|HSYNC_H|DE_H)
 *
 * FC_INHACTV: hactive = 1280 = 0x500
 *   FC_INHACTV0 [0x1001] = 0x00 (low byte)
 *   FC_INHACTV1 [0x1002] = 0x05 (high nibble bits[3:0])
 *
 * FC_INHBLANK: hblank = 370 = 0x172
 *   FC_INHBLANK0 [0x1003] = 0x72
 *   FC_INHBLANK1 [0x1004] = 0x01
 *
 * FC_INVACTV: vactive = 720 = 0x2d0
 *   FC_INVACTV0 [0x1005] = 0xd0
 *   FC_INVACTV1 [0x1006] = 0x02
 *
 * FC_INVBLANK: vblank = 30 = 0x1e
 *   FC_INVBLANK [0x1007] = 0x1e
 *
 * FC_HSYNCINDELAY: hfp = 110 = 0x6e
 *   FC_HSYNCINDELAY0 [0x1008] = 0x6e
 *   FC_HSYNCINDELAY1 [0x1009] = 0x00
 *
 * FC_HSYNCINWIDTH: hsync = 40 = 0x28
 *   FC_HSYNCINWIDTH0 [0x100a] = 0x28
 *   FC_HSYNCINWIDTH1 [0x100b] = 0x00
 *
 * FC_VSYNCINDELAY: vfp = 5
 *   FC_VSYNCINDELAY [0x100c] = 0x05
 *
 * FC_VSYNCINWIDTH: vsync = 5
 *   FC_VSYNCINWIDTH [0x100d] = 0x05
 *
 * FC_AVIVID [0x101b] = 4 (CEA VIC 4 = 720p60)
 * ---------------------------------------------------------------------- */

static void
step4_hdmi_fc_720p60(void)
{
	printf("\n[4] Programming HDMI frame composer (720p60)...\n");

	/* Video input configuration */
	hdmi_w(0x1000, 0xe0);   /* FC_INVIDCONF: HDMI, VSYNC_H, HSYNC_H, DE_H, progressive */

	/* Horizontal active: 1280 */
	hdmi_w(0x1001, 0x00);   /* FC_INHACTV0: low byte */
	hdmi_w(0x1002, 0x05);   /* FC_INHACTV1: high nibble */

	/* Horizontal blank: 370 */
	hdmi_w(0x1003, 0x72);   /* FC_INHBLANK0 */
	hdmi_w(0x1004, 0x01);   /* FC_INHBLANK1 */

	/* Vertical active: 720 */
	hdmi_w(0x1005, 0xd0);   /* FC_INVACTV0 */
	hdmi_w(0x1006, 0x02);   /* FC_INVACTV1 */

	/* Vertical blank: 30 */
	hdmi_w(0x1007, 0x1e);   /* FC_INVBLANK */

	/* Horizontal front porch: 110 */
	hdmi_w(0x1008, 0x6e);   /* FC_HSYNCINDELAY0 */
	hdmi_w(0x1009, 0x00);   /* FC_HSYNCINDELAY1 */

	/* Horizontal sync width: 40 */
	hdmi_w(0x100a, 0x28);   /* FC_HSYNCINWIDTH0 */
	hdmi_w(0x100b, 0x00);   /* FC_HSYNCINWIDTH1 */

	/* Vertical front porch: 5 */
	hdmi_w(0x100c, 0x05);   /* FC_VSYNCINDELAY */

	/* Vertical sync width: 5 */
	hdmi_w(0x100d, 0x05);   /* FC_VSYNCINWIDTH */

	/* AVI InfoFrame: RGB, no repeat, VIC=4 */
	hdmi_w(0x1017, 0x00);   /* FC_AVICONF0: RGB */
	hdmi_w(0x1018, 0x08);   /* FC_AVICONF1: picture aspect 16:9 */
	hdmi_w(0x1019, 0x00);   /* FC_AVICONF2 */
	hdmi_w(0x101b, 0x04);   /* FC_AVIVID: VIC 4 = 720p60 */

	/* Video packetizer: bypass (no pixel repeat, no deep color) */
	hdmi_w(0x0801, 0x00);   /* VP_PR_CD: no pixel repeat, 24bpp */
	hdmi_w(0x0802, 0x00);   /* VP_STUFF */
	hdmi_w(0x0804, 0x48);   /* VP_CONF: bypass=1, output_selector=bypass */

	/* Unmute interrupts */
	hdmi_w(0x01ff, 0x00);   /* IH_MUTE: unmute all */
	hdmi_w(0x0184, 0xfe);   /* IH_MUTE_PHY: unmask HPD (bit0 = HPD) */

	printf("    FC_INVIDCONF: 0x%02x\n", hdmi_r(0x1000));
	printf("    FC_INHACTV:   0x%02x%02x\n", hdmi_r(0x1002), hdmi_r(0x1001));
	printf("    FC_INVACTV:   0x%02x%02x\n", hdmi_r(0x1006), hdmi_r(0x1005));
}

/* -------------------------------------------------------------------------
 * Step 5: Innosilicon PHY init for 74.25 MHz (720p60)
 *
 * PHY I2C DIV: ref=4.8MHz (CLKSEL49=0x1202 confirms this),
 *   SCL=100kHz, DIV=(4800000/(2*100000))-1=23=0x17
 *
 * Innosilicon PHY register values for 74.25 MHz from RK3399 reference.
 * These are the same values U-Boot and Linux use for this frequency.
 * ---------------------------------------------------------------------- */

static void
step5_phy_init_720p60(void)
{
	uint8_t stat;
	int timeout;

	printf("\n[5] Initialising Innosilicon PHY for 74.25 MHz (720p60)...\n");

	/* Configure PHY I2C master clock: 4.8MHz ref, 100kHz SCL */
	hdmi_w(0x3029, 0x17);   /* PHY_I2CM_DIV */
	hdmi_w(0x302b, 0x00);   /* SS_SCL_HCNT_1 */
	hdmi_w(0x302c, 0x18);   /* SS_SCL_HCNT_0 */
	hdmi_w(0x302d, 0x00);   /* SS_SCL_LCNT_1 */
	hdmi_w(0x302e, 0x18);   /* SS_SCL_LCNT_0 */

	/*
	 * PHY_CONF0 [0x3000]:
	 *   bit 6 = ENTMDS (enable transmitter)
	 *   bit 1 = SELDATAENPOL
	 * Set 0x42 to enable transmitter, keep PDZ=0 (power down) during config.
	 */
	hdmi_w(0x3000, 0x42);
	usleep(5000);

	printf("    PHY_CONF0 (pre-power): 0x%02x\n", hdmi_r(0x3000));

	/*
	 * Innosilicon PHY register writes for 74.25 MHz.
	 * Register addresses are PHY-internal (via I2C master).
	 */
	phy_i2c_write(0x06, 0x0008);  /* CPCE_CTRL: charge pump */
	phy_i2c_write(0x15, 0x0000);  /* GMPCTRL: GMP control */
	phy_i2c_write(0x10, 0x01b5);  /* TXTERM: termination */
	phy_i2c_write(0x09, 0x0091);  /* CKSYMTXCTRL: clock symbol */
	phy_i2c_write(0x0e, 0x0000);  /* VLEVCTRL: voltage level */
	phy_i2c_write(0x19, 0x0000);  /* CKCALCTRL: clock cal */

	printf("    PHY I2C config written\n");

	/*
	 * Power up PHY:
	 * PHY_CONF0: PDZ(7)|ENTMDS(6)|GEN2_TXPWRON(3)|SELDATAENPOL(1) = 0xCA
	 */
	hdmi_w(0x3000, 0xca);
	usleep(5000);

	printf("    PHY_CONF0 (post-power): 0x%02x\n", hdmi_r(0x3000));
	printf("    Waiting for PHY PLL lock...\n");

	/* Poll PHY_STAT0 [0x3004] bit 4 = TX_PHY_LOCK */
	for (timeout = 30; timeout > 0; timeout--) {
		usleep(5000);
		stat = hdmi_r(0x3004);
		if (stat & 0x10) {
			printf("    PHY locked! PHY_STAT0=0x%02x  HPD=%d\n",
			    stat, (stat >> 1) & 1);
			break;
		}
	}
	if (timeout == 0)
		printf("    PHY lock TIMEOUT. PHY_STAT0=0x%02x\n",
		    hdmi_r(0x3004));
}

/* -------------------------------------------------------------------------
 * Step 6: VOP — enable output and commit REG_CFG_DONE
 *
 * DSP_CTRL0 [0x0010]: set output interface to HDMI
 *   bits[3:0] = dsp_out_mode:
 *     0x0 = RGB888
 *     0x8 = HDMI (on RK3399 VOP big, bit 3 selects HDMI path)
 *
 * REG_CFG_DONE [0x0000]: write 1 to latch all pending VOP config.
 * ---------------------------------------------------------------------- */

static void
step6_vop_enable(void)
{
	printf("\n[6] Enabling VOP output and committing config...\n");

	/*
	 * DSP_CTRL0 [0x0010]:
	 * Set output mode to HDMI. On RK3399 VOP big, bits[3:0]=0x0 is RGB;
	 * the HDMI path is selected via the GRF mux in step 2.
	 * Leave DSP_CTRL0 as-is for now and just commit.
	 */

	/* Commit all pending register writes */
	vop_w(0x0000, 0x01);   /* REG_CFG_DONE */
	usleep(20000);         /* wait one frame at 60Hz (~16ms) */

	printf("    REG_CFG_DONE written\n");
	printf("    VOP SYS_CTRL:    0x%08x\n", vop_r(0x0008));
	printf("    VOP DSP_CTRL0:   0x%08x\n", vop_r(0x0010));
	printf("    WIN0_CTRL0:      0x%08x\n", vop_r(0x0030));
	printf("    WIN0_YRGB_MST:   0x%08x\n", vop_r(0x0040));
}

/* -------------------------------------------------------------------------
 * Step 7: Verify and report final state
 * ---------------------------------------------------------------------- */

static void
step7_report(void)
{
	uint8_t phy_stat, ih_phy, mc_sw;

	printf("\n[7] Final state report...\n");

	phy_stat = hdmi_r(0x3004);
	ih_phy   = hdmi_r(0x0104);
	mc_sw    = hdmi_r(0x4002);

	printf("    HDMI PHY_STAT0   [0x3004]: 0x%02x", phy_stat);
	if (phy_stat & 0x10) printf(" [TX_PHY_LOCK]");
	if (phy_stat & 0x02) printf(" [HPD]");
	printf("\n");

	printf("    HDMI IH_PHY_STAT [0x0104]: 0x%02x", ih_phy);
	if (ih_phy & 0x02) printf(" [HPD event]");
	printf("\n");

	printf("    HDMI MC_SWRSTZREQ[0x4002]: 0x%02x\n", mc_sw);
	printf("    HDMI FC_INVIDCONF[0x1000]: 0x%02x\n", hdmi_r(0x1000));
	printf("    HDMI VP_STATUS   [0x0800]: 0x%02x\n", hdmi_r(0x0800));

	printf("    VOP  WIN0_CTRL0:           0x%08x\n", vop_r(0x0030));
	printf("    VOP  WIN0_YRGB_MST:        0x%08x\n", vop_r(0x0040));

	printf("\n");
	if ((phy_stat & 0x12) == 0x12)
		printf("  >> PHY locked + HPD detected — display should be active\n");
	else if (phy_stat & 0x10)
		printf("  >> PHY locked but no HPD — check cable/monitor\n");
	else if (phy_stat & 0x02)
		printf("  >> HPD present but PHY not locked — check PHY config\n");
	else
		printf("  >> No lock, no HPD — check cable and init sequence\n");
}

/* -------------------------------------------------------------------------
 * Step 0: Enable HDMI clocks via CRU
 *
 * CRU uses HIWORD_UPDATE: bits[31:16] = write mask, bits[15:0] = value.
 * Writing 0 to a gate bit = clock ENABLED.
 * Formula: write (mask << 16) | (value & mask)
 * To ungate bit N: write (1<<(N+16)) | 0  i.e. set mask bit, clear value bit.
 *
 * CLKGATE16 [0x0240]: bit 9  = aclk_hdcp,      bit 10 = hclk_hdcp
 * CLKGATE17 [0x0244]: bit 2  = pclk_hdcp
 * CLKGATE20 [0x0250]: bit 12 = pclk_hdmi_ctrl
 * CLKGATE21 [0x0254]: bit 8  = hdmi_cec_clk
 * ---------------------------------------------------------------------- */

static void
step0_hdmi_clocks(void)
{
	printf("[0] Enabling HDMI/HDCP clocks via CRU...\n");

	printf("    CLKGATE16 before: 0x%08x\n", cru_r(0x0240));
	printf("    CLKGATE17 before: 0x%08x\n", cru_r(0x0244));
	printf("    CLKGATE20 before: 0x%08x\n", cru_r(0x0250));
	printf("    CLKGATE21 before: 0x%08x\n", cru_r(0x0254));

	/* Ungate aclk_hdcp (bit9) and hclk_hdcp (bit10) */
	cru_w(0x0240, ((1 << 9) | (1 << 10)) << 16);

	/* Ungate pclk_hdcp (bit2) */
	cru_w(0x0244, (1 << 2) << 16);

	/* Ungate pclk_hdmi_ctrl (bit12) */
	cru_w(0x0250, (1 << 12) << 16);

	/* Ungate hdmi_cec_clk (bit8) */
	cru_w(0x0254, (1 << 8) << 16);

	usleep(10000);   /* let clocks stabilise */

	printf("    CLKGATE16 after:  0x%08x\n", cru_r(0x0240));
	printf("    CLKGATE17 after:  0x%08x\n", cru_r(0x0244));
	printf("    CLKGATE20 after:  0x%08x\n", cru_r(0x0250));
	printf("    CLKGATE21 after:  0x%08x\n", cru_r(0x0254));
	printf("    Done.\n");
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
	struct rkfb_info info;

	printf("rkfb_init — RK3399 HDMI + VOP bring-up sequencer (720p60)\n");
	printf("============================================================\n");

	g_fd = open("/dev/rkfb0", O_RDWR);
	if (g_fd < 0)
		err(1, "open /dev/rkfb0");

	if (ioctl(g_fd, RKFB_GETINFO, &info) < 0)
		err(1, "RKFB_GETINFO");

	printf("Framebuffer: %ux%u %ubpp stride=%u pa=0x%016llx\n\n",
	    info.width, info.height, info.bpp, info.stride,
	    (unsigned long long)info.fb_pa);

	/* Fill framebuffer with a test pattern: blue */
	{
		struct rkfb_fill fill;
		fill.pixel = 0x000000ff;   /* blue in XRGB */
		if (ioctl(g_fd, RKFB_CLEAR, &fill) < 0)
			err(1, "RKFB_CLEAR");
		printf("Framebuffer filled with blue test pattern.\n\n");
	}

	step0_hdmi_clocks();
	step1_hdmi_reset_release();
	step2_grf_mux();
	step3_vop_configure((uint32_t)(info.fb_pa & 0xffffffff));
	step4_hdmi_fc_720p60();
	step5_phy_init_720p60();
	step6_vop_enable();
	step7_report();

	close(g_fd);
	return (0);
}
