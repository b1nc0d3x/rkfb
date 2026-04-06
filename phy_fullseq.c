/*
 * phy_fullseq.c - RK3399 HDMI full clock+PHY bring-up, 74.25 MHz
 *
 * Self-contained: sets up the full clock path then runs PHY init.
 *
 * RK3399 VPLL register layout (RK3036-style PLL):
 *   CON0 [0x20]: bit[15]=BYPASS, bits[14:12]=POSTDIV1, bits[11:0]=FBDIV
 *   CON1 [0x24]: bit[15]=DSMPD(1=int), bits[14:12]=POSTDIV2, bits[5:0]=REFDIV
 *   CON2 [0x28]: bits[23:0]=FRACDIV (0 for integer), bit[31]=LOCK (read-only)
 *   CON3 [0x2c]: bit[0]=PWRDOWN
 *   All CON registers use hiword-update (upper 16 bits = write mask).
 *
 * Target: FBDIV=99, REFDIV=2, PD1=4, PD2=4
 *   VCO  = 24 * 99 / 2       = 1188 MHz  (spec: 800-1600 MHz)
 *   Fout = 1188 / (4 * 4)    = 74.25 MHz
 *   CON0 = 0x7fff4063  (mask=0x7fff, POSTDIV1=4, FBDIV=99)
 *   CON1 = 0xffffc002  (mask=0xffff, DSMPD=1, POSTDIV2=4, REFDIV=2)
 *
 * MPLL table from Linux phy-rockchip-inno-hdmi.c (GPLv2, Rockchip).
 * Build: cc -o phy_fullseq phy_fullseq.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define HDMI_PA     0xff940000UL
#define CRU_PA      0xff760000UL
#define VIOGRF_PA   0xff770000UL
#define VOP_PA      0xff900000UL

static volatile uint8_t  *g_hdmi;
static volatile uint32_t *g_cru;
static volatile uint32_t *g_viogrf;
static volatile uint32_t *g_vop;

static inline uint8_t  hr(uint32_t o)              { return g_hdmi[o]; }
static inline void     hw(uint32_t o, uint8_t v)    { g_hdmi[o] = v; }
static inline uint32_t cru_r(uint32_t o)            { return g_cru[o/4]; }
static inline void     cru_w(uint32_t o, uint32_t v){ g_cru[o/4] = v; }
static inline uint32_t vop_r(uint32_t o)            { return g_vop[o/4]; }
static inline void     vop_w(uint32_t o, uint32_t v){ g_vop[o/4] = v; }

/* All CRU registers use hiword-update: upper 16 bits = write mask */
static inline void cru_hiword(uint32_t o, uint32_t mask, uint32_t val)
{ cru_w(o, (mask<<16)|(val&mask)); }

static inline void viogrf_hiword(uint32_t o, uint32_t mask, uint32_t val)
{ g_viogrf[o/4] = (mask<<16)|(val&mask); }

struct mpll_reg { uint8_t addr; uint16_t val; };

/* MPLL table for 74.25 MHz from Linux phy-rockchip-inno-hdmi.c */
static const struct mpll_reg mpll_74250[] = {
	{ 0x06, 0x0008 }, { 0x0d, 0x0000 }, { 0x0e, 0x0260 },
	{ 0x10, 0x8009 }, { 0x19, 0x0000 }, { 0x1e, 0x0000 },
	{ 0x1f, 0x0000 }, { 0x20, 0x0000 }, { 0x21, 0x0000 },
	{ 0x22, 0x0000 }, { 0x23, 0x0000 }, { 0x24, 0x0000 },
	{ 0x25, 0x0272 }, { 0x26, 0x0000 },
};
#define MPLL_N  (int)(sizeof(mpll_74250)/sizeof(mpll_74250[0]))

static const struct mpll_reg phy_drive[] = {
	{ 0x19, 0x0004 }, { 0x1e, 0x8009 }, { 0x25, 0x0272 },
};
#define DRIVE_N (int)(sizeof(phy_drive)/sizeof(phy_drive[0]))

static int
phy_i2c_write(uint8_t reg, uint16_t val)
{
	int i; uint8_t stat;
	hw(0x3020, 0x69); hw(0x3021, reg);
	hw(0x3022, (val>>8)&0xff); hw(0x3023, val&0xff);
	hw(0x3026, 0x10);
	for (i = 0; i < 100; i++) {
		usleep(500); stat = hr(0x3027);
		if (stat & 0x02) { hw(0x3027, 0x02); return 0; }
		if (stat & 0x08) { hw(0x3027, 0x08);
			fprintf(stderr, "I2C ERROR reg=0x%02x\n", reg); return -1; }
	}
	fprintf(stderr, "I2C TIMEOUT reg=0x%02x\n", reg); return -1;
}

int
main(void)
{
	int fd, i, rc; uint32_t sc;

	printf("phy_fullseq - RK3399 HDMI full clock+PHY bring-up\n");
	printf("===================================================\n\n");

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) err(1, "open /dev/mem");
	g_hdmi   = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
	g_cru    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
	g_viogrf = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
	g_vop    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
	if (g_hdmi==MAP_FAILED)   err(1,"mmap HDMI");
	if (g_cru==MAP_FAILED)    err(1,"mmap CRU");
	if (g_viogrf==MAP_FAILED) err(1,"mmap VIO GRF");
	if (g_vop==MAP_FAILED)    err(1,"mmap VOP");

	/* ---- 0a: CLKSEL49 = GPLL/8 = 74.25 MHz ----------------------- */
	/*
	 * GPLL = 594 MHz, kernel-stable, never reprogrammed.
	 * 594 / 8 = 74.25 MHz exactly. No PLL config needed.
	 * CLKSEL49: bits[9:8]=01(GPLL), bits[7:0]=0x07(div=8) -> 0x107
	 */
	printf("[0a] CLKSEL49: GPLL/8 = 74.25 MHz...\n");
	printf("     GPLL CON2 = 0x%08x (locked=%d)\n",
	    cru_r(0x0088), (cru_r(0x0088)>>31)&1);
	printf("     CLKSEL49 before: 0x%08x\n", cru_r(0x00c4));
	cru_hiword(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
	printf("     CLKSEL49 after:  0x%08x  (want bits[9:0]=0x107)\n", cru_r(0x00c4));


	/* ---- 0c: VOP dclk enable, clear standby ----------------------- */
	printf("\n[0c] VOP dclk enable, clear standby...\n");
	printf("     SYS_CTRL before: 0x%08x\n", vop_r(0x0008));
	sc = vop_r(0x0008);
	sc &= ~(1u<<11);
	sc |=  (1u<<1);
	vop_w(0x0008, sc);
	vop_w(0x0000, 0x01);   /* REG_CFG_DONE */
	usleep(20000);
	printf("     SYS_CTRL after:  0x%08x  (want bit11=0, bit1=1)\n",
	    vop_r(0x0008));

	/* ---- 0d: VIO GRF VOPB->HDMI ----------------------------------- */
	printf("\n[0d] VIO GRF VOPB->HDMI mux...\n");
	printf("     SOC_CON20 before: 0x%08x\n", g_viogrf[0x250/4]);
	viogrf_hiword(0x0250, (1u<<6), (1u<<6));
	usleep(1000);
	printf("     SOC_CON20 after:  0x%08x  (want bit6=1)\n",
	    g_viogrf[0x250/4]);

	/* ---- 1: MC clocks/reset --------------------------------------- */
	printf("\n[1] MC clocks on, soft reset released...\n");
	hw(0x4001,0x00); hw(0x4002,0xff); usleep(5000);
	printf("    MC_CLKDIS=%02x  MC_SWRSTZREQ=%02x\n",hr(0x4001),hr(0x4002));

	/* ---- 2: PHY reset + power down -------------------------------- */
	printf("\n[2] PHY reset + power-down...\n");
	hw(0x4005,0x00); hw(0x3000,0x00); usleep(2000);
	printf("    MC_PHYRSTZ=%02x  PHY_CONF0=%02x\n",hr(0x4005),hr(0x3000));

	/* ---- 3: I2C divider ------------------------------------------- */
	printf("\n[3] I2C divider...\n");
	hw(0x3029,0x17); hw(0x3027,0xff); hw(0x3028,0xff);
	printf("    I2CM_DIV=%02x\n",hr(0x3029));

	/* ---- 4: MPLL -------------------------------------------------- */
	printf("\n[4] MPLL registers (%d)...\n",MPLL_N);
	for (i=0; i<MPLL_N; i++) {
		rc = phy_i2c_write(mpll_74250[i].addr, mpll_74250[i].val);
		printf("    [0x%02x]=0x%04x  %s\n",
		    mpll_74250[i].addr,mpll_74250[i].val,rc?"FAIL":"OK");
	}

	/* ---- 5: Drive ------------------------------------------------- */
	printf("\n[5] Drive config (%d)...\n",DRIVE_N);
	for (i=0; i<DRIVE_N; i++) {
		rc = phy_i2c_write(phy_drive[i].addr, phy_drive[i].val);
		printf("    [0x%02x]=0x%04x  %s\n",
		    phy_drive[i].addr,phy_drive[i].val,rc?"FAIL":"OK");
	}

	/* ---- 6: PHY power up ------------------------------------------ */
	/* ---- 6: PHY power up (two-step) -------------------------------- */
	/* Step1: PDZ|ENTMDS|SPARECTRL|SELDATAENPOL=0xE2 (SPARECTRL was missing!) */
	/* Step2: add GEN2_TXPWRON=0xF2. Two-step matches Linux inno PHY driver. */
	printf("\n[6] PHY power-up sequence...\n");
	hw(0x3000, 0xe2);   /* step1: PDZ|ENTMDS|SPARECTRL|SELDATAENPOL */
	usleep(5000);
	printf("    Step1 PHY_CONF0=0x%02x (want 0xe2)\n", hr(0x3000));
	hw(0x3000, 0xf2);   /* step2: add GEN2_TXPWRON */
	usleep(2000);
	printf("    Step2 PHY_CONF0=0x%02x (want 0xf2)\n", hr(0x3000));

	/* ---- 7: Release PHY reset ------------------------------------- */
	printf("\n[7] Release PHY reset...\n");
	hw(0x4005,0x01); usleep(5000);
	printf("    MC_PHYRSTZ=%02x  PHY_STAT0=%02x\n",hr(0x4005),hr(0x3004));

	/* ---- 8: Poll -------------------------------------------------- */
	printf("\n[8] Polling PHY_STAT0...\n");
	for (i=0; i<50; i++) {
		uint8_t stat=hr(0x3004), ih=hr(0x0104);
		if ((i%5)==0||(stat&0x12))
			printf("    [%3d ms] PHY_STAT0=%02x  IH_PHY=%02x%s%s\n",
			    i*10,stat,ih,
			    (stat&0x10)?" LOCKED":"",(stat&0x02)?" HPD":"");
		if (stat&0x10) break;
		usleep(10000);
	}

	/* ---- Result --------------------------------------------------- */
	{
		uint8_t s=hr(0x3004);
		printf("\n[Result]\n");
		printf("    PHY_STAT0  = 0x%02x\n",s);
		printf("    IH_PHY     = 0x%02x\n",hr(0x0104));
		printf("    PHY_CONF0  = 0x%02x\n",hr(0x3000));
		printf("    MC_PHYRSTZ = 0x%02x\n",hr(0x4005));
		printf("    GPLL CON2  = 0x%08x  (locked=%d)\n", cru_r(0x0088), (cru_r(0x0088)>>31)&1);
		printf("    CLKSEL49   = 0x%08x\n",cru_r(0x00c4));
		printf("    SYS_CTRL   = 0x%08x\n",vop_r(0x0008));
		printf("    SOC_CON20  = 0x%08x\n",g_viogrf[0x250/4]);
		if ((s&0x12)==0x12)
			printf("\n  >> PHY locked + HPD -- display should be active\n");
		else if (s&0x10)
			printf("\n  >> PHY locked, no HPD -- check cable/monitor\n");
		else if (s&0x02)
			printf("\n  >> HPD present, no lock -- check MPLL/clock\n");
		else
			printf("\n  >> No lock, no HPD\n");
	}
	close(fd);
	return 0;
}
