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
	printf("[0] Enabling HDMI/HDCP + VOP clocks via CRU...\n");
	printf("    CLKGATE_HDMI(0x0250): 0x%08x\n", cru_r(0x0250));
	printf("    CLKGATE_DCLK(0x0228): 0x%08x  (bit12=DCLK_VOPB gate)\n", cru_r(0x0228));
	printf("    CLKGATE_AXI (0x0270): 0x%08x  (bit4=ACLK_VOPB bit6=HCLK_VOPB)\n", cru_r(0x0270));
	printf("    CLKGATE_NOC (0x0274): 0x%08x  (bit0=ACLK_NOC bit4=HCLK_NOC)\n", cru_r(0x0274));

	cru_hiword(0x0240, (1<<9)|(1<<10), 0);   /* aclk_hdcp, hclk_hdcp */
	cru_hiword(0x0244, (1<<2), 0);            /* pclk_hdcp */
	cru_hiword(0x0250, (1<<12), 0);           /* pclk_hdmi_ctrl */
	cru_hiword(0x0254, (1<<8), 0);            /* hdmi_cec_clk */

	/*
	 * VOPB (VOP0) clock enables. Linux CLKGATE_CON(n) = 0x300 + n*4;
	 * our offsets are Linux_offset - 0x100.
	 *   DCLK_VOP0:    Linux CLKGATE_CON(10) bit12 → our 0x0228 bit12
	 *   ACLK_VOP0:    Linux CLKGATE_CON(28) bit4  → our 0x0270 bit4
	 *   HCLK_VOP0:    Linux CLKGATE_CON(28) bit6  → our 0x0270 bit6
	 *   ACLK_VOP0_NOC: Linux CLKGATE_CON(29) bit0 → our 0x0274 bit0
	 *   HCLK_VOP0_NOC: Linux CLKGATE_CON(29) bit4 → our 0x0274 bit4
	 */
	cru_hiword(0x0228, (1<<12), 0);           /* ungate DCLK_VOPB */
	cru_hiword(0x0270, (1<<4)|(1<<6), 0);     /* ungate ACLK_VOPB, HCLK_VOPB */
	cru_hiword(0x0274, (1<<0)|(1<<4), 0);     /* ungate ACLK_VOPB_NOC, HCLK_VOPB_NOC */
	usleep(10000);

	printf("    CLKGATE_HDMI(0x0250): 0x%08x  (after)\n", cru_r(0x0250));
	printf("    CLKGATE_DCLK(0x0228): 0x%08x  (after, bit12 want 0)\n", cru_r(0x0228));
	printf("    CLKGATE_AXI (0x0270): 0x%08x  (after, bits 4,6 want 0)\n", cru_r(0x0270));
}

/* -------------------------------------------------------------------------
 * Step 0b: Set HDMI pixel clock to 74.25 MHz via GPLL/8
 *
 * GPLL = 594 MHz. 594 / 8 = 74.25 MHz.
 * CLKSEL49 [0x00c4]: bits[9:8]=src mux, bits[7:0]=div-1
 *   Mux: 00=VPLL, 01=CPLL, 10=GPLL
 *   CPLL on RK3399 is typically 1000 MHz → CPLL/8 = 125 MHz (WRONG for 720p60)
 *   GPLL = 594 MHz → GPLL/8 = 74.25 MHz (CORRECT)
 *   Use src=10 (GPLL), div=7 → 594/8 = 74.25 MHz
 * ---------------------------------------------------------------------- */

static void
step0b_pixclk(void)
{
	uint32_t before, after, cpll_con0, cpll_con1, cpll_con2;
	uint32_t fbdiv, refdiv, postdiv1, postdiv2, cpll_mhz;

	printf("\n[0b] Setting pixel clock: GPLL/8 = 74.25 MHz...\n");
	before = cru_r(0x00c4);
	printf("     CLKSEL49 before: 0x%08x  bits[9:8]=0x%x (Linux: 0=VPLL,1=CPLL,2=GPLL,3=PPLL)\n",
	    before, (before >> 8) & 3);

	/*
	 * Read all PLLs to determine actual frequencies.
	 * RK3399 PLL CON1 layout: bits[5:0]=REFDIV, bits[10:8]=POSTDIV1, bits[14:12]=POSTDIV2
	 * RK3399 PLL CON0 layout: bits[11:0]=FBDIV
	 * FOUT = 24MHz * FBDIV / (REFDIV * POSTDIV1 * POSTDIV2)
	 */
	cpll_con0 = cru_r(0x0060); cpll_con1 = cru_r(0x0064); cpll_con2 = cru_r(0x0068);
	fbdiv = cpll_con0 & 0xfff;
	refdiv = cpll_con1 & 0x3f; if (refdiv == 0) refdiv = 1;
	postdiv1 = (cpll_con1 >> 8) & 0x7; if (postdiv1 == 0) postdiv1 = 1;
	postdiv2 = (cpll_con1 >> 12) & 0x7; if (postdiv2 == 0) postdiv2 = 1;
	cpll_mhz = 24u * fbdiv / (refdiv * postdiv1 * postdiv2);
	printf("     CPLL  CON0=0x%08x CON1=0x%08x lock=%d → %u MHz (/8=%u MHz)\n",
	    cpll_con0, cpll_con1, (cpll_con2 >> 31) & 1, cpll_mhz, cpll_mhz/8);

	/* GPLL at 0x0080 */
	{
		uint32_t g0=cru_r(0x0080), g1=cru_r(0x0084), g2=cru_r(0x0088);
		uint32_t gf=g0&0xfff, gr=g1&0x3f; if(gr==0)gr=1;
		uint32_t gp1=(g1>>8)&0x7; if(gp1==0)gp1=1;
		uint32_t gp2=(g1>>12)&0x7; if(gp2==0)gp2=1;
		uint32_t gmhz=24u*gf/(gr*gp1*gp2);
		printf("     GPLL  CON0=0x%08x CON1=0x%08x lock=%d → %u MHz (/8=%u MHz)\n",
		    g0, g1, (g2>>31)&1, gmhz, gmhz/8);
	}
	/* VPLL at 0x00C0 (may not exist on all RK3399) */
	{
		uint32_t v0=cru_r(0x00c0), v1=cru_r(0x00c4-4), v2=cru_r(0x00c8);
		printf("     VPLL? CON0=0x%08x CON1=0x%08x lock=%d\n",
		    v0, v1, (v2>>31)&1);
	}

	/*
	 * DCLK_VOPB mux (Linux naming): bits[9:8]=0→VPLL,1→CPLL,2→GPLL,3→PPLL
	 * Bootloader used bits[9:8]=01 (CPLL/8=100MHz). We need GPLL/8=74.25MHz.
	 * HIWORD_UPDATE: mask=(3<<8)|0xff, val=(2<<8)|0x07 → mux=GPLL, div=8
	 */
	cru_hiword(0x00c4, (3u << 8) | 0xffu, (2u << 8) | 0x07u);
	usleep(5000);
	after = cru_r(0x00c4);
	printf("     CLKSEL49 after:  0x%08x  bits[9:8]=0x%x (want 0x2=GPLL) div=%u (want 7)\n",
	    after, (after >> 8) & 3, after & 0xff);
}

/* -------------------------------------------------------------------------
 * Step 1: Release HDMI soft reset
 * ---------------------------------------------------------------------- */

static void
step1_reset_release(void)
{
	printf("\n[1] DW-HDMI full reset + clock enable...\n");
	printf("    MC_SWRSTZREQ before: 0x%02x\n", hdmi_r(0x4002));

	/*
	 * Full soft reset: assert all module resets first, then release.
	 * This clears any stale FC/VP state from previous runs.
	 * Bit5 (IPAHB) is known to stick at 0 on this silicon — ignore.
	 */
	hdmi_w(0x4002, 0x00);   /* assert all resets */
	usleep(5000);
	hdmi_w(0x4001, 0x70);   /* MC_CLKDIS: enable pixel+TMDS+PREP clocks, gate others */
	hdmi_w(0x4002, 0xff);   /* release all resets */
	usleep(10000);

	printf("    MC_SWRSTZREQ after:  0x%02x  (want 0xff, bit5 may stick=0)\n", hdmi_r(0x4002));
	printf("    MC_CLKDIS    after:  0x%02x\n", hdmi_r(0x4001));
}

/* -------------------------------------------------------------------------
 * Step 2: GRF mux - VOPB -> HDMI (VIO GRF SOC_CON20 bit6)
 * ---------------------------------------------------------------------- */

static void
step2_grf_mux(void)
{
	printf("\n[2] GRF mux: VOPB -> HDMI...\n");

	printf("    GRF_SOC_CON4 before:  0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x0410));
	printf("    GRF_SOC_CON20 before: 0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x6250));

	/*
	 * GRF_SOC_CON20 [0x6250]: clear bit6 to feed HDMI from the BSD primary display block.
	 * This board misbehaves if that selector is left high.
	 *
	 * NOTE: the 0x6250 selector lives in the expanded GRF view in this driver.
	 *
	reg_hiword(RKFB_BLOCK_GRF, 0x6250, (1u << 6), 0);
	usleep(1000);

	printf("    GRF_SOC_CON4 after:   0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x0410));
	printf("    GRF_SOC_CON20 after:  0x%08x\n", reg_r(RKFB_BLOCK_GRF, 0x6250));
}

/* -------------------------------------------------------------------------
 * Step 3: VOP WIN0 scanout + display timing (720p60)
 * ---------------------------------------------------------------------- */

static void
step3_vop(uint32_t fb_pa)
{
	uint32_t sys_ctrl;

	printf("\n[3] VOP WIN0 + 720p60 timing...\n");
	printf("    SYS_CTRL before:           0x%08x\n", vop_r(0x0008));
	printf("    BOOT DSP_HTOTAL_HS_END:    0x%08x  (htotal-1 in [30:16])\n", vop_r(0x0188));
	printf("    BOOT DSP_VTOTAL_VS_END:    0x%08x  (vtotal-1 in [30:16])\n", vop_r(0x0190));

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
	vop_w(0x0030, 0x01000001);               /* WIN0_CTRL0: enable, ARGB8888, AXI_GATHER_EN(bit24) */

	/*
	 * SYS_CTRL on the working path requires hdmi_dclk_en (bit1).
	 * The newer bit13 path leaves the live register unchanged on this board,
	 * so preserve the older logic: clear standby bits and set bit1.
	 */
	sys_ctrl = vop_r(0x0008);
	sys_ctrl &= ~((3u << 22));   /* clear standby bits */
	sys_ctrl |=  (1u << 1);      /* hdmi_dclk_en */
	vop_w(0x0008, sys_ctrl);

	/* Commit shadow → active */
	printf("    Committing shadow regs...\n");
	vop_w(0x0000, 0x01);   /* REG_CFG_DONE */

	/* Wait for VOP to start and shadow commit to take effect */
	usleep(40000);   /* 2+ frames at 60Hz */

	printf("    SYS_CTRL:          0x%08x  (want bit1=1, standby cleared)\n", vop_r(0x0008));
	printf("    DSP_CTRL0:         0x%08x  (want bits[3:0]=0=RGB888)\n", vop_r(0x0004));
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

	/*
	 * FC_INVIDCONF: 0x70 = DVI mode (bit3=0), VSYNC_H(bit6), HSYNC_H(bit5), DE_H(bit4)
	 * Testing DVI first: no InfoFrames or data islands required.
	 * Change to 0x78 (HDMI mode, bit3=1) once display is confirmed working.
	 */
	hdmi_w(0x1000, 0x70);   /* FC_INVIDCONF: DVI mode test */
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

	/*
	 * AVI InfoFrame content registers (Data Bytes 1-4):
	 *   FC_AVICONF0 (DB1): Y[1:0]=00 (RGB), A0=0, B=0, S=0
	 *   FC_AVICONF1 (DB2): C[1:0]=0, M[1:0]=10 (16:9 picture AR), R=0
	 *     bits[5:4]=M → 10b=0x20 for 16:9.  0x08 was wrong (that's R field).
	 *   FC_AVICONF2 (DB3): EC=0, Q=0, SC=0
	 *   FC_AVIVID   (DB4): VIC 4 = 720p60
	 */
	hdmi_w(0x100E, 0x00);   /* FC_PRCONF: no pixel repetition */
	hdmi_w(0x1017, 0x00);   /* FC_AVICONF0: RGB colorspace, no active format */
	hdmi_w(0x1018, 0x20);   /* FC_AVICONF1: M[5:4]=10 = 16:9 picture AR */
	hdmi_w(0x1019, 0x00);   /* FC_AVICONF2: default colorimetry/range */
	hdmi_w(0x101b, 0x04);   /* FC_AVIVID: VIC 4 = 720p60 */
	/*
	 * Enable automatic AVI InfoFrame transmission every frame.
	 * Without this the FC_AVICONF registers are programmed but the
	 * InfoFrame packet is never actually inserted into the HDMI stream.
	 * FC_DATAUTO3 (0x10B3) bit4 = AVI_AUTO enable.
	 */
	hdmi_w(0x10B3, 0x10);   /* FC_DATAUTO3: AVI InfoFrame auto-send */

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
	printf("    VP_CONF:      0x%02x  (expect 0x43)\n", hdmi_r(0x0804));
	printf("    FC_DATAUTO3:  0x%02x  (expect 0x10)\n", hdmi_r(0x10B3));
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
	{ 0x06, 0x0008 }, { 0x0d, 0x0000 }, { 0x0e, 0x0260 },
	{ 0x10, 0x8009 }, { 0x19, 0x0000 }, { 0x1e, 0x0000 },
	{ 0x1f, 0x0000 }, { 0x20, 0x0000 }, { 0x21, 0x0000 },
	{ 0x22, 0x0000 }, { 0x23, 0x0000 }, { 0x24, 0x0000 },
	{ 0x25, 0x0272 }, { 0x26, 0x0000 },
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

/*
 * Innosilicon PHY direct register access.
 *
 * The Innosilicon HDMI PHY (RK3399) is memory-mapped within the DW-HDMI
 * address space at 0xFF940000.  Its internal registers are accessed via
 * 32-bit reads/writes with a specific encoding:
 *
 *   Write PHY reg R with value V:
 *     bus_space_write_4(base, R*4,  (R << 16) | (V << 8))
 *
 *   Read PHY reg R:
 *     bits[15:8] of bus_space_read_4(base, R*4)
 *
 * This is NOT the DW-HDMI PHY_I2CM interface (0x3020-0x3029); that
 * interface is not connected to the Innosilicon PHY on RK3399.
 */
static void
phy_d_w(uint8_t reg, uint8_t val)
{
	/* Use full 32-bit word write: (reg<<16)|(val<<8) */
	uint32_t word = ((uint32_t)reg << 16) | ((uint32_t)val << 8);
	struct rkfb_regop ro = { .block = RKFB_BLOCK_HDMI, .off = reg, .val = word };
	if (ioctl(g_rkfb, RKFB_REG_WRITE, &ro) < 0)
		err(1, "phy_d_w reg=0x%02x", reg);
}

static uint8_t
phy_d_r(uint8_t reg)
{
	return (reg_r(RKFB_BLOCK_HDMI, reg) >> 8) & 0xff;
}

static void
step5_phy(void)
{
	int i;
	uint8_t stat;

	printf("\n[5] Innosilicon PHY check...\n");
	stat = hdmi_r(0x3004);
	printf("    PHY_STAT0: 0x%02x  PHY_CONF0: 0x%02x  MC_PHYRSTZ: 0x%02x\n",
	    stat, hdmi_r(0x3000), hdmi_r(0x4005));

	if (stat & 0x10) {
		/*
		 * PHY is already locked. The Innosilicon PHY on RK3399 auto-locks
		 * to the pixel clock when powered and out of reset. Resetting it
		 * (MC_PHYRSTZ cycle) only disrupts the lock and may cause it to
		 * fail to relock within a short poll window. Skip reset.
		 */
		printf("    PHY already locked (bit4=1), HPD=%d. Skipping reset.\n",
		    (stat >> 1) & 1);
		return;
	}

	/*
	 * PHY not locked. Run the full Innosilicon PHY init sequence:
	 * 1. Assert MC_PHYRSTZ + power down PHY_CONF0
	 * 2. Write MPLL config via PHY I2CM (slave 0x69) while in reset
	 * 3. Two-step power-up (0xe2 then 0xee)
	 * 4. Release MC_PHYRSTZ
	 * 5. Poll for lock
	 *
	 * Key: I2CM writes work while PHY is in reset + powered down.
	 * Key: MPLL auto-configures from pixel clock; writes tune the lock.
	 */
	printf("    PHY not locked. Running full init...\n");
	hdmi_w(0x4005, 0x00);   /* MC_PHYRSTZ: assert reset */
	hdmi_w(0x3000, 0x00);   /* PHY_CONF0: power down */
	usleep(5000);

	/* Configure PHY I2CM for MPLL programming */
	hdmi_w(0x3020, 0x69);   /* PHY_I2CM_SLAVE */
	hdmi_w(0x3029, 0x17);   /* PHY_I2CM_DIV */
	hdmi_w(0x3027, 0xff);   /* clear I2CM interrupts */
	hdmi_w(0x3028, 0xff);

	/* Write MPLL config for 74.25 MHz via I2CM */
	printf("    Writing MPLL config (%d regs)...\n", PHY_MPLL_N);
	for (i = 0; i < PHY_MPLL_N; i++)
		phy_i2c_wr(phy_74250_mpll[i].addr, phy_74250_mpll[i].val);

	printf("    Writing drive config (%d regs)...\n", PHY_DRIVE_N);
	for (i = 0; i < PHY_DRIVE_N; i++)
		phy_i2c_wr(phy_74250_drive[i].addr, phy_74250_drive[i].val);

	/* Two-step PHY power-up (matches EDK2/Linux sequence for Innosilicon V2) */
	hdmi_w(0x3000, 0xe2);   /* step 1: PDZ | ENTMDS | SPARECTRL | SELDATAENPOL */
	usleep(5000);
	hdmi_w(0x3000, 0xee);   /* step 2: add GEN2_TXPWRON */
	usleep(5000);
	printf("    PHY_CONF0: 0x%02x  (want 0xee)\n", hdmi_r(0x3000));

	hdmi_w(0x4005, 0x01);   /* MC_PHYRSTZ: release → MPLL relocks */
	usleep(10000);

	/* Poll for TX_PHY_LOCK (bit4) up to 1 second */
	printf("    Polling for TX_PHY_LOCK...\n");
	for (i = 0; i < 100; i++) {
		uint8_t ih;
		stat = hdmi_r(0x3004);
		ih   = hdmi_r(0x0104);
		if ((i % 10) == 0 || (stat & 0x12))
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
	printf("    PHY lock TIMEOUT after 1s. PHY_STAT0=0x%02x\n", hdmi_r(0x3004));
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

	/* FC interrupt status - reveals if frame composer is generating frames */
	printf("    IH_FC_STAT0 [0x0100]: 0x%02x  (bit5=frameComposerDone)\n", hdmi_r(0x0100));
	printf("    IH_FC_STAT1 [0x0101]: 0x%02x\n", hdmi_r(0x0101));
	printf("    IH_FC_STAT2 [0x0102]: 0x%02x\n", hdmi_r(0x0102));
	printf("    FC_INVIDCONF[0x1000]: 0x%02x  (0x70=DVI, 0x78=HDMI)\n", hdmi_r(0x1000));
	printf("    VOP DSP_CTRL0:        0x%08x  (bits[3:0]=out_fmt)\n", vop_r(0x0004));
	printf("    VOP INTR_STATUS0:     0x%08x\n", vop_r(0x0284));

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

	/* Fill framebuffer with opaque blue (ARGB8888: A=0xFF, R=0, G=0, B=0xFF) */
	fill.pixel = 0xFF0000FF;
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
