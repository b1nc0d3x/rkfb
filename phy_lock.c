#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA    0xff760000UL
#define GRF_PA    0xff320000UL
#define VIOGRF_PA 0xff770000UL
#define HDMI_PA   0xff940000UL
#define GPIO2_PA  0xff780000UL

static volatile uint32_t *cru, *grf, *vio, *gpio2;
static volatile uint8_t *hdmi;

static inline void dsb(void) { __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t CR(uint32_t o){ return cru[o/4]; }
static inline void CW(uint32_t o, uint32_t v){ cru[o/4]=v; dsb(); }
static inline void CRHW(uint32_t o, uint32_t mask, uint32_t val){ CW(o, (mask<<16) | (val & mask)); }
static inline void GW(uint32_t o, uint32_t v){ grf[o/4]=v; dsb(); }
static inline void VIOW(uint32_t o, uint32_t v){ vio[o/4]=v; dsb(); }
static inline uint8_t HR(uint32_t o){ return hdmi[o*4]; }
static inline void HW(uint32_t o, uint8_t v){ hdmi[o*4]=v; dsb(); }

static int
phy_i2c_wr(uint8_t reg, uint16_t val)
{
        int i;
        uint8_t st;

        HW(0x3020, 0x69);
        HW(0x3021, reg);
        HW(0x3022, (val >> 8) & 0xff);
        HW(0x3023, val & 0xff);
        HW(0x3026, 0x10);
        for (i = 0; i < 100; i++) {
                usleep(500);
                st = HR(0x3027);
                if (st & 0x02) {
                        HW(0x3027, 0x02);
                        return (0);
                }
                if (st & 0x08) {
                        HW(0x3027, 0x08);
                        return (-1);
                }
        }
        return (-1);
}

int
main(void)
{
        int fd, i, try, rc;
        static const struct { uint8_t a; uint16_t v; } mpll[] = {
                {0x06,0x0008},{0x0d,0x0000},{0x0e,0x0260},{0x10,0x8009},
                {0x19,0x0000},{0x1e,0x0000},{0x1f,0x0000},{0x20,0x0000},
                {0x21,0x0000},{0x22,0x0000},{0x23,0x0000},{0x24,0x0000},
                {0x25,0x0272},{0x26,0x0000},
        };
        static const struct { uint8_t a; uint16_t v; } drv[] = {
                {0x19,0x0004},{0x1e,0x8009},{0x25,0x0272},
        };

        fd = open("/dev/mem", O_RDWR);
        if (fd < 0)
                err(1, "open /dev/mem");

        cru = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
        grf = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GRF_PA);
        vio = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VIOGRF_PA);
        hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
        gpio2 = mmap(NULL, 0x100, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO2_PA);
        if (cru == MAP_FAILED || grf == MAP_FAILED || vio == MAP_FAILED ||
            hdmi == MAP_FAILED || gpio2 == MAP_FAILED)
                err(1, "mmap");

        printf("phy_lock - direct RK3399 HDMI PHY lock helper\n");
        printf("before: CLKSEL49=0x%08x PHY_STAT0=0x%02x IH=0x%02x MC_CLKDIS=0x%02x PHY_CONF0=0x%02x SOC20=0x%08x\n",
            CR(0x00c4), HR(0x3004), HR(0x0104), HR(0x4001), HR(0x3000), vio[0x0250/4]);

        /* clocks + pin/power */
        CRHW(0x0240, (1<<9)|(1<<10), 0);
        CRHW(0x0244, (1<<2), 0);
        CRHW(0x0250, (1<<12), 0);
        CRHW(0x0254, (1<<8), 0);
        CRHW(0x0228, (1<<12), 0);
        CRHW(0x0270, (1<<4)|(1<<6), 0);
        CRHW(0x0274, (1<<0)|(1<<4), 0);
        CW(0x00c4, 0x00001207);
        gpio2[1] |= (1u<<5);
        gpio2[0] |= (1u<<5);
        GW(0x010c, (0xfc00u<<16) | 0x5400u);
        VIOW(0x0250, (0x00ffu<<16) | 0x0000u);

        for (try = 0; try < 3; try++) {
                printf("try %d\n", try + 1);

                /* controller/phy reset */
                HW(0x4002, 0x00);
                usleep(2000);
                HW(0x4001, 0x70);
                HW(0x4002, 0xff);
                usleep(5000);
                HW(0x4005, 0x00);
                HW(0x3000, 0x00);
                usleep(5000);

                /* i2c side */
                HW(0x3029, 0x17);
                HW(0x3027, 0xff);
                HW(0x3028, 0xff);

                rc = 0;
                for (i = 0; i < (int)(sizeof(mpll)/sizeof(mpll[0])); i++)
                        rc |= phy_i2c_wr(mpll[i].a, mpll[i].v);
                for (i = 0; i < (int)(sizeof(drv)/sizeof(drv[0])); i++)
                        rc |= phy_i2c_wr(drv[i].a, drv[i].v);
                printf("  i2c rc=%d PHY_I2CM_INT=0x%02x\n", rc, HR(0x3027));

                HW(0x3000, 0xe2);
                usleep(5000);
                HW(0x3000, 0xee);
                usleep(5000);
                HW(0x4005, 0x01);
                usleep(20000);

                for (i = 0; i < 50; i++) {
                        CW(0x00c4, 0x00001207);
                        GW(0x010c, (0xfc00u<<16) | 0x5400u);
                        VIOW(0x0250, (0x00ffu<<16) | 0x0000u);
                        uint8_t stat = HR(0x3004);
                        uint8_t ih = HR(0x0104);
                        if ((i % 10) == 0 || (stat & 0x12))
                                printf("  [%3d ms] PHY_STAT0=0x%02x IH=0x%02x MC_CLKDIS=0x%02x PHY_CONF0=0x%02x%s%s\n",
                                    i * 10, stat, ih, HR(0x4001), HR(0x3000),
                                    (stat & 0x10) ? " LOCK" : "",
                                    (stat & 0x02) ? " HPD" : "");
                        if (stat & 0x10)
                                break;
                        usleep(10000);
                }

                if (HR(0x3004) & 0x10)
                        break;
        }

        printf("after: CLKSEL49=0x%08x PHY_STAT0=0x%02x IH=0x%02x MC_CLKDIS=0x%02x PHY_CONF0=0x%02x SOC20=0x%08x\n",
            CR(0x00c4), HR(0x3004), HR(0x0104), HR(0x4001), HR(0x3000), vio[0x0250/4]);
        return (0);
}
