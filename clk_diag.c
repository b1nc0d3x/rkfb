/*
 * clk_diag.c — RK3399 HDMI clock path diagnostic
 *
 * Checks every stage of the clock path from VPLL to PHY:
 *
 *   VPLL (CRU 0xff760000)
 *     -> CLKSEL49 mux/divider (CRU 0x00c4)
 *     -> VOP SYS_CTRL dclk enable (VOP 0xff900000 + 0x0008)
 *     -> GRF HDMI_LCDC_SEL mux  (VIO GRF 0xff770000 + 0x0250)
 *     -> PHY clock input
 *
 * Also dumps BPLL/CPLL/GPLL for reference since FreeBSD CRU driver
 * may have configured them differently than U-Boot left them.
 *
 * Build: cc -o clk_diag clk_diag.c
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA      0xff760000UL
#define GRF_PA      0xff320000UL
#define VIOGRF_PA   0xff770000UL
#define VOP_PA      0xff900000UL   /* VOPB */

static volatile uint32_t *g_cru;
static volatile uint32_t *g_grf;
static volatile uint32_t *g_viogrf;
static volatile uint32_t *g_vop;

static inline uint32_t cru_r(uint32_t o)    { return g_cru[o/4]; }
static inline uint32_t grf_r(uint32_t o)    { return g_grf[o/4]; }
static inline uint32_t viogrf_r(uint32_t o) { return g_viogrf[o/4]; }
static inline uint32_t vop_r(uint32_t o)    { return g_vop[o/4]; }

/* Decode RK3399 integer PLL: Fout = Fin*FBDIV / (REFDIV*POSTDIV1*POSTDIV2) */
static void
decode_pll(const char *name, uint32_t con0, uint32_t con1, uint32_t con2, uint32_t con3)
{
	uint32_t fbdiv    = con0 & 0xfff;
	uint32_t postdiv1 = (con1 >> 12) & 0x7;
	uint32_t postdiv2 = (con1 >> 8)  & 0x7;
	uint32_t refdiv   = con1 & 0x3f;
	uint32_t dsmpd    = (con2 >> 24) & 1;   /* 1=integer mode */
	uint32_t lock     = (con2 >> 31) & 1;
	uint32_t pwrdown  = (con3 >> 0)  & 1;

	double fout = 0.0;
	if (refdiv && postdiv1 && postdiv2)
		fout = (24.0 * fbdiv) / ((double)refdiv * postdiv1 * postdiv2);

	printf("  %-8s  CON0=0x%08x  CON1=0x%08x  CON2=0x%08x  CON3=0x%08x\n",
	    name, con0, con1, con2, con3);
	printf("           FBDIV=%u REFDIV=%u PD1=%u PD2=%u  -> %.4f MHz  %s%s%s\n",
	    fbdiv, refdiv, postdiv1, postdiv2, fout,
	    lock     ? "[LOCKED]"   : "[NOT LOCKED]",
	    pwrdown  ? " [PWRDN]"   : "",
	    dsmpd    ? " [INT]"     : " [FRAC]");
}

int
main(void)
{
	int fd;
	uint32_t v;

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) err(1, "open /dev/mem");

	g_cru    = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
	g_grf    = mmap(NULL, 0x8000, PROT_READ,            MAP_SHARED, fd, GRF_PA);
	g_viogrf = mmap(NULL, 0x1000, PROT_READ,            MAP_SHARED, fd, VIOGRF_PA);
	g_vop    = mmap(NULL, 0x1000, PROT_READ,            MAP_SHARED, fd, VOP_PA);

	if (g_cru == MAP_FAILED)    err(1, "mmap CRU");
	if (g_grf == MAP_FAILED)    err(1, "mmap GRF");
	if (g_viogrf == MAP_FAILED) err(1, "mmap VIO GRF");
	if (g_vop == MAP_FAILED)    err(1, "mmap VOP");

	printf("=== RK3399 HDMI Clock Path Diagnostic ===\n\n");

	/* ---- PLLs -------------------------------------------------------- */
	printf("[1] PLL states:\n");
	decode_pll("BPLL",
	    cru_r(0x0000), cru_r(0x0004), cru_r(0x0008), cru_r(0x000c));
	decode_pll("DPLL",
	    cru_r(0x0010), cru_r(0x0014), cru_r(0x0018), cru_r(0x001c));
	decode_pll("VPLL",
	    cru_r(0x0020), cru_r(0x0024), cru_r(0x0028), cru_r(0x002c));
	decode_pll("CPLL",
	    cru_r(0x0060), cru_r(0x0064), cru_r(0x0068), cru_r(0x006c));
	decode_pll("GPLL",
	    cru_r(0x0080), cru_r(0x0084), cru_r(0x0088), cru_r(0x008c));
	decode_pll("NPLL",
	    cru_r(0x00a0), cru_r(0x00a4), cru_r(0x00a8), cru_r(0x00ac));

	/* ---- CLKSEL49: HDMI pixel clock mux ------------------------------ */
	printf("\n[2] HDMI pixel clock selector (CLKSEL49):\n");
	v = cru_r(0x00c4);
	{
		uint32_t src = (v >> 8) & 0x3;
		uint32_t div = (v & 0xff) + 1;
		const char *src_names[] = { "CPLL", "GPLL", "VPLL", "NPLL" };
		printf("  CLKSEL49 [0x00c4] = 0x%08x\n", v);
		printf("    source  = %s (bits[9:8]=%u)\n", src_names[src], src);
		printf("    divider = %u (bits[7:0]=%u, pixel_clk = src/%u)\n",
		    div, div-1, div);

		/* Estimate pixel clock */
		double pll_mhz = 0;
		if (src == 2) {  /* VPLL */
			uint32_t fbdiv    = cru_r(0x0020) & 0xfff;
			uint32_t postdiv1 = (cru_r(0x0024) >> 12) & 0x7;
			uint32_t postdiv2 = (cru_r(0x0024) >> 8)  & 0x7;
			uint32_t refdiv   = cru_r(0x0024) & 0x3f;
			if (refdiv && postdiv1 && postdiv2)
				pll_mhz = (24.0 * fbdiv) / ((double)refdiv * postdiv1 * postdiv2);
		}
		if (pll_mhz > 0)
			printf("    pixel clock = %.4f MHz / %u = %.4f MHz  %s\n",
			    pll_mhz, div, pll_mhz / div,
			    (pll_mhz / div > 74.0 && pll_mhz / div < 74.5) ?
			    "[CORRECT for 720p60]" : "[WRONG — need 74.25 MHz]");
		else
			printf("    (can't compute — non-VPLL source or PLL not decoded)\n");
	}

	/* ---- CRU clock gates --------------------------------------------- */
	printf("\n[3] CRU clock gates (0 = running, 1 = gated):\n");
	v = cru_r(0x0240);
	printf("  CLKGATE16 [0x0240] = 0x%08x", v);
	if (v & (1<<9))  printf("  [aclk_hdcp GATED]");
	if (v & (1<<10)) printf("  [hclk_hdcp GATED]");
	printf("\n");
	v = cru_r(0x0244);
	printf("  CLKGATE17 [0x0244] = 0x%08x", v);
	if (v & (1<<2))  printf("  [pclk_hdcp GATED]");
	printf("\n");
	v = cru_r(0x0250);
	printf("  CLKGATE20 [0x0250] = 0x%08x", v);
	if (v & (1<<12)) printf("  [pclk_hdmi_ctrl GATED]");
	printf("\n");
	v = cru_r(0x0254);
	printf("  CLKGATE21 [0x0254] = 0x%08x", v);
	if (v & (1<<8))  printf("  [hdmi_cec_clk GATED]");
	printf("\n");

	/* ---- VOP SYS_CTRL ------------------------------------------------- */
	printf("\n[4] VOP VOPB SYS_CTRL:\n");
	v = vop_r(0x0008);
	printf("  SYS_CTRL  [0x0008] = 0x%08x\n", v);
	printf("    bit[1]  hdmi_dclk_en = %u  %s\n",
	    (v>>1)&1, ((v>>1)&1) ? "[ENABLED]" : "[DISABLED <<<]");
	printf("    bit[11] vop_standby  = %u  %s\n",
	    (v>>11)&1, ((v>>11)&1) ? "[STANDBY <<<]" : "[ACTIVE]");
	printf("    VOP DSP_CTRL0 [0x0010] = 0x%08x\n", vop_r(0x0010));
	printf("    VOP INTR_STATUS[0x0284]= 0x%08x\n", vop_r(0x0284));

	/* ---- VIO GRF HDMI mux --------------------------------------------- */
	printf("\n[5] VIO GRF HDMI mux (LCDC select):\n");
	v = viogrf_r(0x0250);
	printf("  VIO_GRF_SOC_CON20 [0x0250] = 0x%08x\n", v);
	printf("    bit[6] RK3399_HDMI_LCDC_SEL = %u  (%s -> HDMI)\n",
	    (v>>6)&1, ((v>>6)&1) ? "VOPB" : "VOPL");

	/* ---- GRF SOC_STATUS ----------------------------------------------- */
	printf("\n[6] GRF status:\n");
	printf("  SOC_STATUS5 [0x04e8] = 0x%08x\n", grf_r(0x04e8));

	/* ---- Summary ------------------------------------------------------ */
	printf("\n=== Summary ===\n");

	uint32_t vpll_con2  = cru_r(0x0028);
	uint32_t clksel49   = cru_r(0x00c4);
	uint32_t sys_ctrl   = vop_r(0x0008);
	uint32_t viomux     = viogrf_r(0x0250);

	int vpll_locked   = (vpll_con2 >> 31) & 1;
	int vpll_pwrdn    = (cru_r(0x002c)) & 1;
	int clk_src_vpll  = ((clksel49 >> 8) & 3) == 2;
	int clk_div_ok    = (clksel49 & 0xff) == 0;
	int dclk_en       = (sys_ctrl >> 1) & 1;
	int not_standby   = !((sys_ctrl >> 11) & 1);
	int mux_vopb      = (viomux >> 6) & 1;

	printf("  VPLL locked:        %s\n", vpll_locked  ? "YES" : "NO  <<<");
	printf("  VPLL powered:       %s\n", !vpll_pwrdn  ? "YES" : "NO  <<<");
	printf("  CLKSEL49 src=VPLL:  %s\n", clk_src_vpll ? "YES" : "NO  <<<");
	printf("  CLKSEL49 div=1:     %s (div=%u)\n",
	    clk_div_ok ? "YES" : "NO  <<<", (clksel49 & 0xff) + 1);
	printf("  VOP hdmi_dclk_en:   %s\n", dclk_en    ? "YES" : "NO  <<<");
	printf("  VOP not standby:    %s\n", not_standby ? "YES" : "NO  <<<");
	printf("  VIO GRF mux=VOPB:   %s\n", mux_vopb   ? "YES" : "NO  <<<");

	if (vpll_locked && !vpll_pwrdn && clk_src_vpll && clk_div_ok && dclk_en && not_standby && mux_vopb)
		printf("\n  >> Clock path looks GOOD — PHY should get 74.25 MHz\n");
	else
		printf("\n  >> Clock path has issues — fix items marked <<< before PHY init\n");

	close(fd);
	return (0);
}
