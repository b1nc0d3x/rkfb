/*
 * hdmi_clk_fix.c - Dump and fix all clock gates/resets needed for HDMI
 *
 * Checks and unblocks:
 *   - PMU VIO power domain
 *   - CRU clock gates: pclk_vio, dclk_vop0, and all HDMI gates
 *   - CRU hardware soft resets for HDMI
 *
 * Run this BEFORE phy_fullseq or rkfb_init.
 *
 * Build: cc -o hdmi_clk_fix hdmi_clk_fix.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA   0xff760000UL
#define PMU_PA   0xff310000UL
#define HDMI_PA  0xff940000UL

static volatile uint32_t *cru;
static volatile uint32_t *pmu;
static volatile uint8_t  *hdmi;

/* CRU hiword write: upper 16 = mask, lower 16 = value */
static void cru_hw(uint32_t off, uint32_t mask, uint32_t val)
{ cru[off/4] = (mask<<16)|(val&mask); }

int main(void)
{
	int fd;
	uint32_t v;

	fd = open("/dev/mem", O_RDWR);
	if (fd < 0) err(1, "open /dev/mem");

	cru  = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
	pmu  = mmap(NULL,0x1000, PROT_READ,            MAP_SHARED,fd,PMU_PA);
	hdmi = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
	if (cru==MAP_FAILED)  err(1,"mmap CRU");
	if (pmu==MAP_FAILED)  err(1,"mmap PMU");
	if (hdmi==MAP_FAILED) err(1,"mmap HDMI");

	printf("=== HDMI Clock/Reset Fix ===\n\n");

	/* ---- 1: PMU power domain ------------------------------------ */
	printf("[1] PMU power domain status:\n");
	v = pmu[0x0098/4];
	printf("    PMU_PWRDN_ST [0x0098] = 0x%08x\n", v);
	printf("    bit7  VIO  power domain = %d  (%s)\n",
	    (v>>7)&1, ((v>>7)&1) ? "POWERED OFF ***" : "on");
	printf("    bit8  VCODEC            = %d\n", (v>>8)&1);
	printf("    bit9  VDU               = %d\n", (v>>9)&1);
	printf("    bit15 IEP               = %d\n",(v>>15)&1);
	if ((v>>7)&1)
		printf("    *** VIO power domain is OFF - cannot fix from userspace ***\n");

	/* ---- 2: CRU clock gates ------------------------------------- */
	printf("\n[2] CRU clock gates (0=running, 1=gated):\n");

#define SHOW_GATE(reg, bit, name) do { \
	uint32_t _v = cru[(reg)/4] & 0xffff; \
	printf("    " #reg " bit%2d %-20s = %d  %s\n", \
	    (bit), (name), (_v>>(bit))&1, \
	    ((_v>>(bit))&1) ? "<< GATED" : ""); \
} while(0)

	SHOW_GATE(0x0218,  1, "dclk_vop0");       /* VOPB pixel clock */
	SHOW_GATE(0x0220,  4, "hclk_vop0");       /* VOPB AHB */
	SHOW_GATE(0x0220,  8, "pclk_vio");        /* VIO APB *** */
	SHOW_GATE(0x0224,  4, "hclk_vio");        /* VIO AHB */
	SHOW_GATE(0x0240,  9, "aclk_hdcp");
	SHOW_GATE(0x0240, 10, "hclk_hdcp");
	SHOW_GATE(0x0244,  2, "pclk_hdcp");
	SHOW_GATE(0x0250, 12, "pclk_hdmi_ctrl");
	SHOW_GATE(0x0254,  8, "hdmi_cec");

	/* ---- 3: CRU hard resets ------------------------------------ */
	printf("\n[3] CRU hard resets (0=running, 1=held in reset):\n");

#define SHOW_RST(reg, bit, name) do { \
	uint32_t _v = cru[(reg)/4] & 0xffff; \
	printf("    " #reg " bit%2d %-20s = %d  %s\n", \
	    (bit), (name), (_v>>(bit))&1, \
	    ((_v>>(bit))&1) ? "<< IN RESET" : ""); \
} while(0)

	SHOW_RST(0x0414,  9, "srst_p_hdmi_ctrl");
	SHOW_RST(0x0414, 10, "srst_hdmi_ctrl");
	SHOW_RST(0x0414,  6, "srst_h_hdcp");
	SHOW_RST(0x0414,  7, "srst_a_hdcp");
	SHOW_RST(0x0414,  8, "srst_hdcp");
	SHOW_RST(0x0418,  2, "srst_vop0_axi");
	SHOW_RST(0x0418,  3, "srst_vop0_ahb");
	SHOW_RST(0x0418,  4, "srst_vop0_dclk");

	/* ---- 4: Apply fixes ---------------------------------------- */
	printf("\n[4] Applying fixes...\n");

	/* Ungate all needed clocks */
	cru_hw(0x0218, (1<<1),  0);             /* dclk_vop0: ungate */
	cru_hw(0x0220, (1<<4)|(1<<8), 0);      /* hclk_vop0, pclk_vio: ungate */
	cru_hw(0x0224, (1<<4), 0);             /* hclk_vio: ungate */
	cru_hw(0x0240, (1<<9)|(1<<10), 0);     /* aclk_hdcp, hclk_hdcp */
	cru_hw(0x0244, (1<<2), 0);             /* pclk_hdcp */
	cru_hw(0x0250, (1<<12), 0);            /* pclk_hdmi_ctrl */
	cru_hw(0x0254, (1<<8), 0);             /* hdmi_cec */
	usleep(5000);

	/* Release hard resets (write 0 to deassert) */
	cru_hw(0x0414, (1<<9)|(1<<10), 0);     /* HDMI APB + core reset release */
	cru_hw(0x0414, (1<<6)|(1<<7)|(1<<8), 0); /* HDCP resets */
	cru_hw(0x0418, (1<<2)|(1<<3)|(1<<4), 0); /* VOP0 resets */
	usleep(5000);

	/* Release HDMI soft resets */
	hdmi[(0x4001)*4] = 0x00;   /* MC_CLKDIS: all on */
	hdmi[(0x4002)*4] = 0xff;   /* MC_SWRSTZREQ: release */
	usleep(10000);

	/* ---- 5: Verify -------------------------------------------- */
	printf("\n[5] After fix:\n");
	SHOW_GATE(0x0218,  1, "dclk_vop0");
	SHOW_GATE(0x0220,  8, "pclk_vio");
	SHOW_RST(0x0414,   9, "srst_p_hdmi_ctrl");
	SHOW_RST(0x0414,  10, "srst_hdmi_ctrl");

	printf("\n    MC_LOCKONCLOCK [0x4006] = 0x%02x\n", hdmi[(0x4006)*4]);
	{
		uint8_t loc = hdmi[(0x4006)*4];
		printf("      bit5 sfrclk   = %d\n", (loc>>5)&1);
		printf("      bit2 pixelclk = %d  %s\n", (loc>>2)&1,
		    (loc>>2)&1 ? "(pixel clock present!)" : "(still absent)");
		printf("      bit0 pclk     = %d  %s\n", (loc>>0)&1,
		    (loc>>0)&1 ? "(APB clock present!)" : "(still absent)");
	}

	printf("    MC_SWRSTZREQ   [0x4002] = 0x%02x\n", hdmi[(0x4002)*4]);
	printf("    PHY_STAT0      [0x3004] = 0x%02x\n", hdmi[(0x3004)*4]);

	close(fd);
	return 0;
}
