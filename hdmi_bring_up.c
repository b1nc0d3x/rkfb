/*
 * hdmi_bringup.c - RK3399 HDMI complete bringup in one tight sequence
 *
 * Races against the kernel clock framework by doing everything fast.
 * Uses /dev/mem directly for VOP (bypassing ioctl whitelist issue).
 * Holds all registers, then immediately runs PHY sequence.
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
#define VIOGRF_PA 0xff770000UL
#define VOP_PA    0xff900000UL
#define HDMI_PA   0xff940000UL

static volatile uint32_t *g_cru;
static volatile uint32_t *g_viogrf;
static volatile uint32_t *g_vop;
static volatile uint8_t  *g_hdmi;

static inline uint8_t  hr(uint32_t o)              { return g_hdmi[o]; }
static inline void     hw(uint32_t o, uint8_t v)    { g_hdmi[o] = v; }
static inline uint32_t cru_r(uint32_t o)            { return g_cru[o/4]; }
static inline void     cru_hw(uint32_t o, uint32_t mask, uint32_t val)
{ g_cru[o/4] = (mask<<16)|(val&mask); }
static inline uint32_t vop_r(uint32_t o)            { return g_vop[o/4]; }
static inline void     vop_w(uint32_t o, uint32_t v){ g_vop[o/4] = v; }

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
    for (i=0; i<100; i++) {
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

    printf("hdmi_bringup - single-pass RK3399 HDMI init\n");
    printf("============================================\n\n");

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_cru    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
    g_viogrf = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
    g_vop    = mmap(NULL,0x2000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
    g_hdmi   = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
    if (!g_cru||!g_viogrf||!g_vop||!g_hdmi) err(1,"mmap");

    /* === PHASE 1: Set pixel clock === */
    printf("[1] CLKSEL49: GPLL/8 = 74.25 MHz\n");
    cru_hw(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
    printf("    CLKSEL49 = 0x%08x\n", cru_r(0x00c4));

    /* === PHASE 2: VOP timing (direct /dev/mem, no ioctl) === */
    printf("\n[2] VOP 720p60 timing + dclk enable\n");
    sc = vop_r(0x0008);
    sc &= ~(1u<<11); sc |= (1u<<1);
    vop_w(0x0008, sc);
    vop_w(0x0188, 0x06710027);   /* DSP_HTOTAL_HS_END */
    vop_w(0x018c, 0x01040604);   /* DSP_HACT_ST_END */
    vop_w(0x0190, 0x02ed0004);   /* DSP_VTOTAL_VS_END */
    vop_w(0x0194, 0x001902e9);   /* DSP_VACT_ST_END */
    vop_w(0x003c, 0x05000500);   /* WIN0_VIR */
    vop_w(0x0048, (719u<<16)|1279u); /* WIN0_ACT_INFO */
    vop_w(0x004c, (719u<<16)|1279u); /* WIN0_DSP_INFO */
    vop_w(0x0050, (25u<<16)|260u);   /* WIN0_DSP_ST */
    vop_w(0x0030, 0x00000001);       /* WIN0_CTRL0: enable ARGB8888 */
    vop_w(0x0000, 0x01);             /* REG_CFG_DONE */
    usleep(40000);   /* wait 2+ frames for shadow->active latch */
    printf("    DSP_HTOTAL_HS_END = 0x%08x (want 0x06710027)\n", vop_r(0x0188));
    printf("    DSP_VTOTAL_VS_END = 0x%08x (want 0x02ed0004)\n", vop_r(0x0190));
    printf("    SYS_CTRL          = 0x%08x (bit1=1 bit11=0 wanted)\n", vop_r(0x0008));

    /* === PHASE 3: VIO GRF mux === */
    printf("\n[3] VIO GRF: VOPB->HDMI\n");
    g_viogrf[0x0250/4] = (1u<<22)|(1u<<6);
    printf("    SOC_CON20 = 0x%08x (bit6=1 wanted)\n", g_viogrf[0x0250/4]);

    /* === PHASE 4: HDMI MC init === */
    printf("\n[4] HDMI MC clocks + reset\n");
    hw(0x4001, 0x00);
    hw(0x4002, 0xff);
    usleep(2000);
    printf("    MC_CLKDIS=%02x MC_SWRSTZREQ=%02x\n", hr(0x4001), hr(0x4002));

    /* === PHASE 5: PHY sequence (no delays except where required) === */
    printf("\n[5] PHY init\n");
    hw(0x4005, 0x00);   /* assert PHY reset */
    hw(0x3000, 0x00);   /* power down */
    usleep(1000);

    hw(0x3029, 0x17);   /* I2C divider */
    hw(0x3027, 0xff);
    hw(0x3028, 0xff);

    /* MPLL */
    for (i=0; i<(int)(sizeof(mpll_74250)/sizeof(mpll_74250[0])); i++)
        phy_i2c_write(mpll_74250[i].addr, mpll_74250[i].val);
    /* Drive */
    for (i=0; i<(int)(sizeof(phy_drive)/sizeof(phy_drive[0])); i++)
        phy_i2c_write(phy_drive[i].addr, phy_drive[i].val);

    /* Power up: step1 then step2 */
    hw(0x3000, 0xe2);
    usleep(5000);
    hw(0x3000, 0xf2);
    usleep(2000);

    /* Release PHY reset */
    hw(0x4005, 0x01);
    usleep(3000);
    printf("    PHY_CONF0=%02x MC_PHYRSTZ=%02x PHY_STAT0=%02x\n",
        hr(0x3000), hr(0x4005), hr(0x3004));

    /* Re-assert CLKSEL49 and VOP dclk in case kernel cleared them */
    cru_hw(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
    sc = vop_r(0x0008); sc &= ~(1u<<11); sc |= (1u<<1);
    vop_w(0x0008, sc);
    g_viogrf[0x0250/4] = (1u<<22)|(1u<<6);

    /* === PHASE 6: Poll === */
    printf("\n[6] Polling PHY_STAT0...\n");
    for (i=0; i<100; i++) {
        uint8_t stat=hr(0x3004), ih=hr(0x0104);
        if ((i%10)==0 || (stat&0x12))
            printf("    [%3d ms] PHY_STAT0=%02x IH_PHY=%02x CLKSEL49=%08x SOC_CON20=%08x%s%s\n",
                i*10, stat, ih, cru_r(0x00c4), g_viogrf[0x0250/4],
                (stat&0x10)?" LOCKED":"", (stat&0x02)?" HPD":"");
        if (stat&0x10) break;
        /* Keep re-asserting registers the kernel might clear */
        cru_hw(0x00c4, (3u<<8)|0xffu, (1u<<8)|0x07u);
        g_viogrf[0x0250/4] = (1u<<22)|(1u<<6);
        usleep(10000);
    }

    uint8_t s=hr(0x3004);
    printf("\n[Result] PHY_STAT0=%02x PHY_CONF0=%02x MC_PHYRSTZ=%02x\n",
        s, hr(0x3000), hr(0x4005));
    printf("         CLKSEL49=0x%08x SOC_CON20=0x%08x\n",
        cru_r(0x00c4), g_viogrf[0x0250/4]);
    printf("         DSP_HTOTAL=0x%08x SYS_CTRL=0x%08x\n",
        vop_r(0x0188), vop_r(0x0008));

    if ((s&0x12)==0x12) printf("\n>> PHY locked + HPD\n");
    else if (s&0x10)    printf("\n>> PHY locked, no HPD\n");
    else if (s&0x02)    printf("\n>> HPD present, no lock\n");
    else                printf("\n>> No lock, no HPD\n");

    close(fd);
    return 0;
}
