#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA    0xff760000UL
#define VIOGRF_PA 0xff770000UL
#define HDMI_PA   0xff940000UL
#define GPIO2_PA  0xff780000UL

static volatile uint32_t *cru, *vio, *gpio2;
static volatile uint8_t *hdmi;
static inline void dsb(void) { __asm volatile("dsb sy" ::: "memory"); }
static inline uint32_t CR(uint32_t o){ return cru[o/4]; }
static inline void CW(uint32_t o, uint32_t v){ cru[o/4]=v; dsb(); }
static inline void CRHW(uint32_t o, uint32_t mask, uint32_t val){ CW(o, (mask<<16) | (val & mask)); }
static inline uint8_t HR(uint32_t o){ return hdmi[o*4]; }
static inline void HW(uint32_t o, uint8_t v){ hdmi[o*4]=v; dsb(); }

static void phy_mask(uint8_t mask, int enable)
{
        uint8_t v = HR(0x3000);
        if (enable) v |= mask; else v &= ~mask;
        HW(0x3000, v);
}

static int phy_i2c_wr(uint8_t reg, uint16_t val)
{
        int i;
        uint8_t st;

        HW(0x0108, 0xff);
        HW(0x3021, reg);
        HW(0x3022, (val >> 8) & 0xff);
        HW(0x3023, val & 0xff);
        HW(0x3026, 0x10);
        for (i = 0; i < 1000; i++) {
                usleep(1000);
                st = HR(0x0108) & 0x3;
                if (st) {
                        HW(0x0108, st);
                        return (st & 0x1) ? -1 : 0;
                }
        }
        return (-1);
}

int main(void)
{
        int fd, i;
        struct { uint8_t reg; uint16_t val; } seq[] = {
                {0x06, 0x0072},
                {0x15, 0x0001},
                {0x10, 0x0000},
                {0x13, 0x0000},
                {0x17, 0x0006},
                {0x19, 0x0004},
                {0x09, 0x8009},
                {0x0e, 0x0272},
                {0x05, 0x8000},
        };

        fd = open("/dev/mem", O_RDWR);
        if (fd < 0) err(1, "open /dev/mem");
        cru = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
        vio = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VIOGRF_PA);
        hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
        gpio2 = mmap(NULL, 0x100, PROT_READ|PROT_WRITE, MAP_SHARED, fd, GPIO2_PA);
        if (cru == MAP_FAILED || vio == MAP_FAILED || hdmi == MAP_FAILED || gpio2 == MAP_FAILED)
                err(1, "mmap");

        printf("phy_lock - dw-hdmi gen2 style sequence\n");
        printf("before: CLKSEL49=0x%08x PHY_STAT0=0x%02x IH_I2CMPHY=0x%02x MC_CLKDIS=0x%02x PHY_CONF0=0x%02x\n",
            CR(0x00c4), HR(0x3004), HR(0x0108), HR(0x4001), HR(0x3000));

        CRHW(0x0240, (1<<9)|(1<<10), 0);
        CRHW(0x0244, (1<<2), 0);
        CRHW(0x0250, (1<<12), 0);
        CRHW(0x0254, (1<<8), 0);
        CRHW(0x0228, (1<<12), 0);
        CRHW(0x0270, (1<<4)|(1<<6), 0);
        CRHW(0x0274, (1<<0)|(1<<4), 0);
        gpio2[1] |= (1u<<5);
        gpio2[0] |= (1u<<5);

        /* Power off PHY first. */
        phy_mask(0x08, 0); /* TXPWRON=0 */
        for (i = 0; i < 5; i++) {
                if (!(HR(0x3004) & 0x01))
                        break;
                usleep(2000);
        }
        phy_mask(0x10, 1); /* PDDQ=1 */
        phy_mask(0x20, 1); /* SVSRET=1 */

        /* Gen2 reset and HEACPHY reset. */
        HW(0x4005, 0x01);
        HW(0x4005, 0x00);
        HW(0x4007, 0x01);

        /* Set PHY I2C slave address using the DW-HDMI test-clear handshake. */
        HW(0x3001, HR(0x3001) | 0x20);
        HW(0x3020, 0x69);
        HW(0x3001, HR(0x3001) & ~0x20);
        HW(0x302a, 0x00);
        HW(0x0188, 0xff);
        HW(0x0108, 0xff);

        for (i = 0; i < (int)(sizeof(seq)/sizeof(seq[0])); i++) {
                int rc = phy_i2c_wr(seq[i].reg, seq[i].val);
                printf("  i2c reg=0x%02x val=0x%04x rc=%d stat=0x%02x\n",
                    seq[i].reg, seq[i].val, rc, HR(0x0108));
        }

        phy_mask(0x08, 1); /* TXPWRON=1 */
        phy_mask(0x10, 0); /* PDDQ=0 */

        for (i = 0; i < 20; i++) {
                uint8_t stat = HR(0x3004);
                printf("  [%3d ms] PHY_STAT0=0x%02x%s%s CONF0=0x%02x\n",
                    i * 2, stat,
                    (stat & 0x01) ? " LOCK" : "",
                    (stat & 0x02) ? " HPD" : "",
                    HR(0x3000));
                if (stat & 0x01)
                        break;
                usleep(2000);
        }

        printf("after: CLKSEL49=0x%08x PHY_STAT0=0x%02x IH_I2CMPHY=0x%02x MC_CLKDIS=0x%02x PHY_CONF0=0x%02x\n",
            CR(0x00c4), HR(0x3004), HR(0x0108), HR(0x4001), HR(0x3000));
        return 0;
}
