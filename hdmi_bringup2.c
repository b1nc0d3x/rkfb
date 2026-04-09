/*
 * hdmi_bringup.c - RK3399 HDMI complete bringup, single pass
 *
 * Maps all needed regions upfront. Holds all kernel-contested registers
 * (CLKSEL49, VIO GRF mux, GPIO4C pinmux, GPIO2_A5 avdd) on every poll
 * iteration. Uses /dev/mem directly for VOP timing.
 *
 * Build: cc -o hdmi_bringup hdmi_bringup.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA    0xff760000UL
#define GRF_PA    0xff320000UL
#define VIOGRF_PA 0xff770000UL
#define VOP_PA    0xff900000UL
#define HDMI_PA   0xff940000UL
#define GPIO2_PA  0xff780000UL

static volatile uint32_t *g_cru;
static volatile uint32_t *g_grf;
static volatile uint32_t *g_viogrf;
static volatile uint32_t *g_vop;
static volatile uint8_t  *g_hdmi;
static volatile uint32_t *g_gpio2;
static volatile uint32_t *g_gpio4;

static inline uint8_t  hr(uint32_t o)              { return g_hdmi[(o)*4]; }
static inline void     hw(uint32_t o, uint8_t v)    { g_hdmi[(o)*4] = v; }
static inline uint32_t cru_r(uint32_t o)            { return g_cru[o/4]; }
static inline void     cru_w(uint32_t o, uint32_t v){ g_cru[o/4] = v; }
static inline uint32_t vop_r(uint32_t o)            { return g_vop[o/4]; }
static inline void     vop_w(uint32_t o, uint32_t v){ g_vop[o/4] = v; }

static inline void cru_hw(uint32_t o, uint32_t mask, uint32_t val)
{ cru_w(o, (mask<<16)|(val&mask)); }

/* Re-assert all kernel-contested registers */
static void hold_regs(void)
{
    /* CLKSEL49: GPLL/8 = 74.25 MHz */
    cru_hw(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
    /* VIO GRF: VOPB->HDMI */
    g_viogrf[0x0250/4] = (0x00ffu<<16) | 0x0040u;
    /* GPIO4C pinmux: C7=HPD, C6=SDA, C5=SCL */
    g_grf[0x010c/4] = (0xfc00u<<16) | 0x5400u;
    /* GPIO2_A5 = avdd_1v8_hdmi enable */
    g_gpio2[1] |= (1u<<5);
    g_gpio2[0] |= (1u<<5);
    /* MC_PHYRSTZ: keep PHY reset released */
    g_hdmi[(0x4005)*4] = 0x01;
    /* MC clocks/reset */
    g_hdmi[(0x4001)*4] = 0x00;
    g_hdmi[(0x4002)*4] = 0xff;
}

struct mpll_reg { uint8_t addr; uint16_t val; };
static const struct mpll_reg mpll_74250[] = {
    {0x06,0x0008},{0x0d,0x0000},{0x0e,0x0260},{0x10,0x8009},
    {0x19,0x0000},{0x1e,0x0000},{0x1f,0x0000},{0x20,0x0000},
    {0x21,0x0000},{0x22,0x0000},{0x23,0x0000},{0x24,0x0000},
    {0x25,0x0272},{0x26,0x0000},
};
static const struct mpll_reg phy_drive[] = {
    {0x19,0x0004},{0x1e,0x8009},{0x25,0x0272},
};

static int phy_i2c_write(uint8_t reg, uint16_t val)
{
    int i; uint8_t stat;
    hw(0x3020,0x69); hw(0x3021,reg);
    hw(0x3022,(val>>8)&0xff); hw(0x3023,val&0xff);
    hw(0x3026,0x10);
    for (i=0; i<300; i++) {  /* 30 seconds */
        usleep(500); stat=hr(0x3027);
        if (stat&0x02) { hw(0x3027,0x02); return 0; }
        if (stat&0x08) { hw(0x3027,0x08); return -1; }
    }
    return -1;
}

int main(void)
{
    int fd, i;
    uint32_t sc;

    printf("hdmi_bringup - RK3399 HDMI single-pass init\n");
    printf("============================================\n\n");

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");

    g_cru    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
    g_grf    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,GRF_PA);
    g_viogrf = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
    g_vop    = mmap(NULL,0x2000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
    g_hdmi   = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
    g_gpio2  = mmap(NULL,0x100,  PROT_READ|PROT_WRITE,MAP_SHARED,fd,GPIO2_PA);
    g_gpio4  = mmap(NULL,0x100,  PROT_READ,            MAP_SHARED,fd,0xff790000);
    if (g_cru==MAP_FAILED||g_grf==MAP_FAILED||g_viogrf==MAP_FAILED||
        g_vop==MAP_FAILED||g_hdmi==MAP_FAILED||g_gpio2==MAP_FAILED||
        g_gpio4==MAP_FAILED)
        err(1,"mmap");

    /* === 1: Pixel clock === */
    printf("[1] GPLL/8 = 74.25 MHz pixel clock\n");
    cru_hw(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
    printf("    CLKSEL49 = 0x%08x\n", cru_r(0x00c4));

    /* === 2: avdd_1v8_hdmi (GPIO2_A5) === */
    printf("\n[2] avdd_1v8_hdmi: GPIO2_A5 high\n");
    g_gpio2[1] |= (1u<<5);
    g_gpio2[0] |= (1u<<5);
    printf("    GPIO2_DR bit5 = %d\n", (g_gpio2[0]>>5)&1);

    /* === 3: VOP 720p60 timing === */
    printf("\n[3] VOP 720p60 timing (direct MMIO)\n");
    sc = vop_r(0x0008);
    sc &= ~(1u<<11); sc |= (1u<<1);
    vop_w(0x0008, sc);
    vop_w(0x0188, 0x06710027);
    vop_w(0x018c, 0x01040604);
    vop_w(0x0190, 0x02ed0004);
    vop_w(0x0194, 0x001902e9);
    vop_w(0x003c, 0x05000500);
    vop_w(0x0048, (719u<<16)|1279u);
    vop_w(0x004c, (719u<<16)|1279u);
    vop_w(0x0050, (25u<<16)|260u);
    vop_w(0x0030, 0x00000001);
    vop_w(0x0000, 0x01);   /* REG_CFG_DONE */
    usleep(40000);
    printf("    DSP_HTOTAL = 0x%08x (want 0x06710027)\n", vop_r(0x0188));
    printf("    SYS_CTRL   = 0x%08x (bit1=1 wanted)\n",  vop_r(0x0008));

    /* === 4: Pinmux + GRF mux === */
    printf("\n[4] Pinmux + VIO GRF VOPB->HDMI\n");
    g_grf[0x010c/4] = (0xfc00u<<16) | 0x5400u;   /* GPIO4C: HPD+SDA+SCL */
    g_viogrf[0x0250/4] = (0x00ffu<<16) | 0x0040u; /* VOPB->HDMI */
    printf("    GPIO4C_IOMUX = 0x%08x (HPD bit[15:14]=%d)\n",
        g_grf[0x010c/4], (g_grf[0x010c/4]>>14)&3);
    printf("    SOC_CON20    = 0x%08x (bit6=%d)\n",
        g_viogrf[0x0250/4], (g_viogrf[0x0250/4]>>6)&1);

    /* === 5: HDMI MC === */
    printf("\n[5] HDMI MC clocks + reset\n");
    hw(0x4001,0x00); hw(0x4002,0xff); usleep(2000);

    /* === 6: PHY init (Linux sequence) === */
    printf("\n[6] PHY init\n");
    hw(0x3029,0x17); hw(0x3027,0xff); hw(0x3028,0xff);
    phy_i2c_write(0x02, 0x0000);   /* clear PDATAEN */

    hw(0x3000,0xe2);   /* PDZ|ENTMDS|SPARECTRL|SELDATAENPOL */
    usleep(5000);
    printf("    step1: PHY_CONF0=0x%02x\n", hr(0x3000));

    for (i=0; i<(int)(sizeof(mpll_74250)/sizeof(mpll_74250[0])); i++)
        phy_i2c_write(mpll_74250[i].addr, mpll_74250[i].val);
    for (i=0; i<(int)(sizeof(phy_drive)/sizeof(phy_drive[0])); i++)
        phy_i2c_write(phy_drive[i].addr, phy_drive[i].val);

    hw(0x3000,0xf2);   /* add GEN2_TXPWRON */
    usleep(2000);
    printf("    step2: PHY_CONF0=0x%02x\n", hr(0x3000));

    hw(0x4005,0x00); usleep(100);
    hw(0x4005,0x01); usleep(5000);
    printf("    MC_PHYRSTZ=%02x PHY_STAT0=%02x\n", hr(0x4005), hr(0x3004));

    /* === 7: Poll with register hold === */
    printf("\n[7] Polling (holding all regs)...\n");
    for (i=0; i<3000; i++) {
        uint8_t stat = hr(0x3004);
        uint8_t ih   = hr(0x0104);
        uint32_t gpio4 = g_gpio4[0];

        if ((i%50)==0||(stat&0x12))
            printf("    [%3dms] STAT=%02x IH=%02x CON20=%08x HPD=%d MC_PHYRSTZ=%02x%s%s\n",
                i*10, stat, ih, g_viogrf[0x0250/4],
                (gpio4>>23)&1, g_hdmi[(0x4005)*4],
                (stat&0x10)?" LOCKED":"",
                (stat&0x02)?" HPD_PHY":"");
        if (stat&0x10) break;
        hold_regs();
        usleep(10000);
    }

    /* Final state */
    uint8_t s = hr(0x3004);
    printf("\n[Result]\n");
    printf("    PHY_STAT0    = 0x%02x\n", s);
    printf("    PHY_CONF0    = 0x%02x\n", hr(0x3000));
    printf("    MC_PHYRSTZ   = 0x%02x\n", hr(0x4005));
    printf("    CLKSEL49     = 0x%08x\n", cru_r(0x00c4));
    printf("    SOC_CON20    = 0x%08x\n", g_viogrf[0x0250/4]);
    printf("    GPIO4C_IOMUX = 0x%08x\n", g_grf[0x010c/4]);
    printf("    DSP_HTOTAL   = 0x%08x\n", vop_r(0x0188));
    printf("    GPIO2_A5     = %d\n", (g_gpio2[0]>>5)&1);

    if ((s&0x12)==0x12) printf("\n>> PHY locked + HPD\n");
    else if (s&0x10)    printf("\n>> PHY locked, no HPD\n");
    else if (s&0x02)    printf("\n>> HPD present, no lock\n");
    else                printf("\n>> No lock, no HPD\n");

    close(fd);
    return 0;
}
