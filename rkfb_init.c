/*
 * rkfb_init.c — RK3399 / RockPro64 display bring-up tool
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
 * Step 2: GRF mux — VOPB -> HDMI
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

        printf("\n[3] Configuring VOP WIN0 for 720p60...\n");
        printf("    SYS_CTRL before:  0x%08x\n", vop_r(0x0008));
        printf("    WIN0_CTRL0 before: 0x%08x\n", vop_r(0x0030));

        sys_ctrl = vop_r(0x0008);
        sys_ctrl &= ~(3 << 22);   /* clear standby */
        sys_ctrl |=  (1 << 1);    /* hdmi_dclk_en */
        vop_w(0x0008, sys_ctrl);

        vop_w(0x003c, 0x05000500);                      /* WIN0_VIR: 1280 words */
        vop_w(0x0040, fb_pa);                           /* WIN0_YRGB_MST */
        vop_w(0x0048, ((720-1) << 16) | (1280-1));     /* WIN0_ACT_INFO */
        vop_w(0x004c, ((720-1) << 16) | (1280-1));     /* WIN0_DSP_INFO */
        vop_w(0x0050, (20 << 16) | 220);               /* WIN0_DSP_ST */
        vop_w(0x0030, 0x00000011);                      /* WIN0_CTRL0: enable ARGB8888 */

        printf("    SYS_CTRL after:   0x%08x\n", vop_r(0x0008));
        printf("    WIN0_CTRL0 after: 0x%08x\n", vop_r(0x0030));
        printf("    WIN0_YRGB_MST:    0x%08x\n", vop_r(0x0040));
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
 * Step 5: Innosilicon PHY init for 74.25 MHz
 * ---------------------------------------------------------------------- */

static void
step5_phy(void)
{
  printf("\n[5] PHY init for 74.25 MHz (720p60)...\n");

  /* Release PHY reset */
  hdmi_w(0x4005, 0x00);   /* MC_PHYRSTZ: assert reset */
  usleep(1000);
  hdmi_w(0x4005, 0x01);   /* MC_PHYRSTZ: release reset */
  usleep(5000);

  /* MPLL config for 74.25 MHz from rockchip_mpll_cfg */
  /* opmode=0, fbdiv2=x, fbdiv1=x */
  hdmi_w(0x3000, 0x00);   /* PHY_CONF0: clear */
  hdmi_w(0x3001, 0x00);   /* PHY_TST0 */

  /* Write PHY config via Gen2 PHY interface */
  /* PHY_CKSYMTXCTRL = 0x8009 */
  hdmi_w(0x3006, 0x09);   /* low byte */
  hdmi_w(0x3007, 0x80);   /* high byte */

  /* PHY_TXTERM = 0x0004 */
  hdmi_w(0x3004, 0x04);
  hdmi_w(0x3005, 0x00);

  /* PHY_VLEVCTRL = 0x0272 */
  hdmi_w(0x3008, 0x72);
  hdmi_w(0x3009, 0x02);

  /* Enable PHY: PDZ|ENTMDS|GEN2_TXPWRON|SELDATAENPOL */
  hdmi_w(0x3000, 0xca);
  usleep(10000);

  printf("    PHY_CONF0: 0x%02x\n", hdmi_r(0x3000));
  printf("    PHY_STAT0: 0x%02x\n", hdmi_r(0x3004));

  /* Wait for lock */
  int i;
  for (i = 0; i < 30; i++) {
    usleep(5000);
    uint8_t stat = hdmi_r(0x3004);
    if (stat & 0x10) {
      printf("    PHY locked! PHY_STAT0=0x%02x HPD=%d\n",
	     stat, (stat >> 1) & 1);
      return;
    }
  }
  printf("    PHY lock TIMEOUT. PHY_STAT0=0x%02x\n", hdmi_r(0x3004));
}


/* -------------------------------------------------------------------------
 * Step 6: VOP commit
 * ---------------------------------------------------------------------- */

static void
step6_commit(void)
{
        printf("\n[6] Committing VOP config...\n");
        vop_w(0x0000, 0x01);    /* REG_CFG_DONE */
        usleep(20000);
        printf("    WIN0_CTRL0:    0x%08x\n", vop_r(0x0030));
        printf("    WIN0_YRGB_MST: 0x%08x\n", vop_r(0x0040));
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
                printf("  >> PHY locked + HPD — display should be active\n");
        else if (phy_stat & 0x10)
                printf("  >> PHY locked, no HPD — check cable/monitor\n");
        else if (phy_stat & 0x02)
                printf("  >> HPD present, PHY not locked — check PHY config\n");
        else
                printf("  >> No lock, no HPD — check cable and init sequence\n");
}
//##############################   VPLL   ###############################

static void
step0b_vpll()
{
        printf("\n[0b] Configuring VPLL for 74.25 MHz...\n");
        printf("     VPLL_CON3 before: 0x%08x\n", cru_r(0x002c));

        /* Power down */
        cru_w(0x002c, (1<<20) | (1<<3));
        usleep(1000);

        /* FBDIV=99 */
        cru_w(0x0020, (0xfff << 16) | 0x0063);
        /* POSTDIV2=8, POSTDIV1=4, REFDIV=1 */
        cru_w(0x0024, (0xffff << 16) | (8<<12) | (4<<8) | 1);
        /* FRACDIV=0 */
        cru_w(0x0028, 0x00000000);
        /* Power up */
        cru_w(0x002c, (0xff << 16) | 0x00);
        usleep(1000);

        /* Wait for lock — CON2 bit31 */
        int i;
        for (i = 0; i < 100; i++) {
                usleep(1000);
                if (cru_r(0x0028) & (1u << 31)) {
                        printf("     VPLL locked after %d ms\n", i+1);
                        break;
                }
        }
        if (i == 100) printf("     VPLL lock TIMEOUT\n");

        /* Route VPLL to HDMI pixel clock via CLKSEL49 */
        /* CLKSEL49 [0x00c4]: bits[9:8]=clock source, 10=VPLL */
        cru_hiword(0x00c4, (3<<8), (2<<8));
        printf("     CLKSEL49 after: 0x%08x\n", cru_r(0x00c4));
}
/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
        struct rkfb_info info;
        struct rkfb_fill fill;

        printf("rkfb_init — RK3399 display bring-up (720p60)\n");
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
