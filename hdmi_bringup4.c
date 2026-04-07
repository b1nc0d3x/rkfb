/*
 * hdmi_bringup.c - RK3399 HDMI bringup with correct 4-byte register stride
 *
 * CRITICAL: DW-HDMI on RK3399 uses 4-byte register stride.
 * Physical byte offset = logical register address * 4
 * e.g. PHY_CONF0 logical 0x3000 -> physical 0xC000 from HDMI base
 *
 * Working register values captured from EDK2-initialized state:
 *   PHY_CONF0    = 0xee  (PDZ|ENTMDS|SPARECTRL|GEN2_PDDQ|bit2|SELDATAENPOL)
 *   MC_CLKDIS    = 0x74  (only pixel clock enabled)
 *   MC_SWRSTZREQ = 0xdf  (all released except TMDS)
 *   PHY_I2CM_DIV = 0x0b
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

static volatile uint8_t  *g_hdmi;
static volatile uint32_t *g_cru;
static volatile uint32_t *g_grf;
static volatile uint32_t *g_viogrf;
static volatile uint32_t *g_vop;
static volatile uint32_t *g_gpio2;
static volatile uint32_t *g_gpio4;

/* HDMI register access with 4-byte stride */
#define HR(log)       g_hdmi[(log)*4]
#define HW(log, val)  (g_hdmi[(log)*4] = (uint8_t)(val))

static inline uint32_t cru_r(uint32_t o)            { return g_cru[o/4]; }
static inline void     cru_hw(uint32_t o, uint32_t mask, uint32_t val)
{ g_cru[o/4] = (mask<<16)|(val&mask); }
static inline uint32_t vop_r(uint32_t o)            { return g_vop[o/4]; }
static inline void     vop_w(uint32_t o, uint32_t v){ g_vop[o/4] = v; }

/* MPLL table for 74.25 MHz from Linux phy-rockchip-inno-hdmi.c (GPLv2) */
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
    HW(0x3020, 0x69);               /* PHY_I2CM_SLAVE */
    HW(0x3021, reg);                /* PHY_I2CM_ADDR */
    HW(0x3022, (val>>8)&0xff);      /* PHY_I2CM_DATAO_1 */
    HW(0x3023, val&0xff);           /* PHY_I2CM_DATAO_0 */
    HW(0x3026, 0x10);               /* PHY_I2CM_OPERATION: write */
    for (i=0; i<200; i++) {
        usleep(500);
        stat = HR(0x3027);          /* PHY_I2CM_INT */
        if (stat&0x02) { HW(0x3027,0x02); return 0; }
        if (stat&0x08) { HW(0x3027,0x08); return -1; }
    }
    return -1;
}

static void hold_regs(void)
{
    cru_hw(0x00c4,(3u<<8)|0xffu,(1u<<8)|0x07u); /* GPLL/8=74.25MHz */
    g_viogrf[0x0250/4] = (0x00ffu<<16)|0x0040u; /* VOPB->HDMI */
    g_grf[0x010c/4] = (0xfc00u<<16)|0x5400u;    /* GPIO4C HPD+SDA+SCL */
    g_gpio2[1] |= (1u<<5); g_gpio2[0] |= (1u<<5); /* avdd enable */
    HW(0x4005, 0x01);    /* MC_PHYRSTZ: released */
    HW(0x4001, 0x74);    /* MC_CLKDIS: pixel clock only */
    HW(0x4002, 0xdf);    /* MC_SWRSTZREQ: all except TMDS */
}

int main(void)
{
    int fd, i;

    printf("hdmi_bringup - RK3399 HDMI (4-byte stride corrected)\n");
    printf("=====================================================\n\n");

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1,"open /dev/mem");

    g_hdmi   = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,HDMI_PA);
    g_cru    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,CRU_PA);
    g_grf    = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,GRF_PA);
    g_viogrf = mmap(NULL,0x1000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VIOGRF_PA);
    g_vop    = mmap(NULL,0x2000, PROT_READ|PROT_WRITE,MAP_SHARED,fd,VOP_PA);
    g_gpio2  = mmap(NULL,0x100,  PROT_READ|PROT_WRITE,MAP_SHARED,fd,GPIO2_PA);
    g_gpio4  = mmap(NULL,0x100,  PROT_READ,            MAP_SHARED,fd,0xff790000);
    if (!g_hdmi||!g_cru||!g_grf||!g_viogrf||!g_vop||!g_gpio2||!g_gpio4)
        err(1,"mmap");

    /* 1: Pixel clock */
    printf("[1] GPLL/8 = 74.25 MHz\n");
    cru_hw(0x00c4,(3u<<8)|0xffu,(1u<<8)|0x07u);
    printf("    CLKSEL49 = 0x%08x (want 0x????0107)\n", cru_r(0x00c4));

    /* 2: avdd */
    printf("\n[2] avdd_1v8_hdmi enable (GPIO2_A5)\n");
    g_gpio2[1] |= (1u<<5); g_gpio2[0] |= (1u<<5);
    printf("    GPIO2_A5 = %d\n", (g_gpio2[0]>>5)&1);

    /* 3: VOP 720p60 */
    printf("\n[3] VOP 720p60 timing\n");
    { uint32_t sc=vop_r(0x0008); sc&=~(1u<<11); sc|=(1u<<1); vop_w(0x0008,sc); }
    vop_w(0x0188,0x06710027); vop_w(0x018c,0x01040604);
    vop_w(0x0190,0x02ed0004); vop_w(0x0194,0x001902e9);
    vop_w(0x003c,0x05000500); vop_w(0x0048,(719u<<16)|1279u);
    vop_w(0x004c,(719u<<16)|1279u); vop_w(0x0050,(25u<<16)|260u);
    vop_w(0x0030,0x00000001); vop_w(0x0000,0x01);
    usleep(40000);
    printf("    DSP_HTOTAL = 0x%08x (want 0x06710027)\n", vop_r(0x0188));

    /* 4: Pinmux + mux */
    printf("\n[4] Pinmux + VIO GRF\n");
    g_grf[0x010c/4]   = (0xfc00u<<16)|0x5400u;
    g_viogrf[0x0250/4]= (0x00ffu<<16)|0x0040u;
    printf("    GPIO4C_IOMUX = 0x%08x HPD=%d\n",
        g_grf[0x010c/4], (g_grf[0x010c/4]>>14)&3);
    printf("    SOC_CON20    = 0x%08x bit6=%d\n",
        g_viogrf[0x0250/4], (g_viogrf[0x0250/4]>>6)&1);

    /* 5: HDMI MC init - matching working state */
    printf("\n[5] HDMI MC init\n");
    HW(0x4001, 0x74);    /* MC_CLKDIS: pixel clock on, others gated */
    HW(0x4002, 0xdf);    /* MC_SWRSTZREQ: release all except TMDS */
    usleep(2000);
    printf("    MC_CLKDIS    = 0x%02x (want 0x74)\n", HR(0x4001));
    printf("    MC_SWRSTZREQ = 0x%02x (want 0xdf)\n", HR(0x4002));
    printf("    MC_LOCKONCLOCK = 0x%02x\n", HR(0x4006));

    /* 6: PHY init */
    printf("\n[6] PHY init\n");

    /* Pre-init: clear PDATAEN (PHY reg 0x02) */
    HW(0x3029, 0x0b);    /* PHY_I2CM_DIV = 0x0b (from working state) */
    HW(0x3027, 0xff);    /* clear INT flags */
    HW(0x3028, 0xff);
    phy_i2c_write(0x02, 0x0000);
    printf("    pre-init: PDATAEN cleared\n");

    /* Power up step 1: PHY_CONF0 = 0xee (from working state) */
    HW(0x3000, 0xee);
    usleep(5000);
    printf("    PHY_CONF0 = 0x%02x (want 0xee)\n", HR(0x3000));

    /* Write MPLL with PHY powered */
    for (i=0; i<(int)(sizeof(mpll_74250)/sizeof(mpll_74250[0])); i++)
        phy_i2c_write(mpll_74250[i].addr, mpll_74250[i].val);
    for (i=0; i<(int)(sizeof(phy_drive)/sizeof(phy_drive[0])); i++)
        phy_i2c_write(phy_drive[i].addr, phy_drive[i].val);
    printf("    MPLL written\n");

    /* Release PHY reset */
    HW(0x4005, 0x00); usleep(100);
    HW(0x4005, 0x01); usleep(5000);
    printf("    MC_PHYRSTZ = 0x%02x\n", HR(0x4005));
    printf("    PHY_STAT0  = 0x%02x\n", HR(0x3004));

    /* 7: FC 720p60 */
    printf("\n[7] Frame Composer 720p60\n");
    HW(0x1000, 0x78);   /* FC_INVIDCONF: HDMI, VSYNC_H, HSYNC_H */
    HW(0x1001, 0x00);   /* FC_INHACTV0: 1280 low */
    HW(0x1002, 0x05);   /* FC_INHACTV1: 1280 high */
    HW(0x1003, 0x72);   /* FC_INHBLANK0: 370 */
    HW(0x1004, 0x01);   /* FC_INHBLANK1 */
    HW(0x1005, 0xd0);   /* FC_INVACTV0: 720 low */
    HW(0x1006, 0x02);   /* FC_INVACTV1: 720 high */
    HW(0x1007, 0x1e);   /* FC_INVBLANK: 30 */
    HW(0x1008, 0x6e);   /* FC_HSYNCINDELAY0: 110 */
    HW(0x1009, 0x00);
    HW(0x100a, 0x28);   /* FC_HSYNCINWIDTH0: 40 */
    HW(0x100b, 0x00);
    HW(0x100c, 0x05);   /* FC_VSYNCINDELAY */
    HW(0x100d, 0x05);   /* FC_VSYNCINWIDTH */
    HW(0x1017, 0x00);   /* FC_AVICONF0: RGB */
    HW(0x1018, 0x08);   /* FC_AVICONF1: 16:9 */
    HW(0x1019, 0x00);
    HW(0x101b, 0x04);   /* FC_AVIVID: VIC 4 = 720p60 */
    HW(0x0801, 0x00);   /* VP_PR_CD */
    HW(0x0804, 0x47);   /* VP_CONF: from working state */
    printf("    FC_INVIDCONF = 0x%02x (want 0x78)\n", HR(0x1000));
    printf("    FC hactive   = %d\n",
        (HR(0x1002)<<8)|HR(0x1001));

    /* 8: Poll */
    printf("\n[8] Polling PHY_STAT0 (30s)...\n");
    for (i=0; i<3000; i++) {
        uint8_t stat = HR(0x3004);
        uint8_t ih   = HR(0x0104);
        uint32_t hpd = (g_gpio4[0]>>23)&1;
        if ((i%500)==0||(stat&0x12))
            printf("    [%4dms] PHY_STAT0=%02x IH=%02x HPD_gpio=%d "
                   "LOCKONCLOCK=%02x%s%s\n",
                i*10, stat, ih, hpd, HR(0x4006),
                (stat&0x10)?" LOCKED":"",
                (stat&0x02)?" HPD_PHY":"");
        if ((stat&0x10)) {
            printf("    LOCKED at %dms -- holding registers\n", i*10);
            break;
        }
        hold_regs();
        usleep(10000);
    }

    /* Hold registers forever so display stays up */
    if (HR(0x3004) & 0x10) {
        printf("Holding display active. Press Ctrl+C to exit.\n");
        for (;;) {
            hold_regs();
            /* Also re-assert FC in case it gets reset */
            HW(0x4002, 0xdf);
            HW(0x4001, 0x74);
            HW(0x4005, 0x01);
            usleep(100000);  /* 100ms hold loop */
        }
    }

    uint8_t s = HR(0x3004);
    printf("\n[Result]\n");
    printf("    PHY_STAT0    = 0x%02x  TX_LOCK=%d HPD=%d\n",
        s,(s>>4)&1,(s>>1)&1);
    printf("    PHY_CONF0    = 0x%02x\n", HR(0x3000));
    printf("    MC_LOCKONCLOCK=0x%02x\n", HR(0x4006));
    printf("    MC_PHYRSTZ   = 0x%02x\n", HR(0x4005));
    printf("    FC_INVIDCONF = 0x%02x\n", HR(0x1000));
    printf("    CLKSEL49     = 0x%08x\n", cru_r(0x00c4));
    printf("    DSP_HTOTAL   = 0x%08x\n", vop_r(0x0188));

    if ((s&0x12)==0x12) printf("\n>> PHY locked + HPD -- display active!\n");
    else if (s&0x10)    printf("\n>> PHY locked, no HPD\n");
    else if (s&0x02)    printf("\n>> HPD present, no lock\n");
    else                printf("\n>> No lock, no HPD\n");

    close(fd);
    return 0;
}
