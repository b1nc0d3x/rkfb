#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>

#define HDMI_PA 0xff940000UL
static volatile uint8_t *h;
static inline void dsb(void) { __asm volatile("dsb sy" ::: "memory"); }
static inline uint8_t HR(uint32_t o){ return h[o*4]; }
static inline void HW(uint32_t o, uint8_t v){ h[o*4]=v; dsb(); }

static int phy_write(uint8_t slave, uint8_t div, uint8_t reg, uint16_t val)
{
    int i; uint8_t st;
    HW(0x302a, 0x01);
    HW(0x3029, div);
    HW(0x302b, 0x00);
    HW(0x302c, 0x18);
    HW(0x302d, 0x00);
    HW(0x302e, 0x18);
    HW(0x3027, 0xff);
    HW(0x3028, 0xff);
    HW(0x3020, slave);
    HW(0x3021, reg);
    HW(0x3022, (val >> 8) & 0xff);
    HW(0x3023, val & 0xff);
    HW(0x3026, 0x10);
    for (i = 0; i < 200; i++) {
        usleep(500);
        st = HR(0x3027);
        if (st & 0x02) { HW(0x3027, 0x02); return 0; }
        if (st & 0x08) { HW(0x3027, 0x08); return -1; }
    }
    return -2;
}

static int phy_read(uint8_t slave, uint8_t div, uint8_t reg, uint8_t op, uint16_t *val)
{
    int i; uint8_t st;
    HW(0x302a, 0x01);
    HW(0x3029, div);
    HW(0x302b, 0x00);
    HW(0x302c, 0x18);
    HW(0x302d, 0x00);
    HW(0x302e, 0x18);
    HW(0x3027, 0xff);
    HW(0x3028, 0xff);
    HW(0x3020, slave);
    HW(0x3021, reg);
    HW(0x3026, op);
    for (i = 0; i < 200; i++) {
        usleep(500);
        st = HR(0x3027);
        if (st & 0x02) {
            HW(0x3027, 0x02);
            *val = ((uint16_t)HR(0x3024) << 8) | HR(0x3025);
            return 0;
        }
        if (st & 0x08) { HW(0x3027, 0x08); return -1; }
    }
    return -2;
}

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    uint8_t slaves[] = { 0x69, 0x49, 0x39, 0x59 };
    uint8_t divs[] = { 0x17, 0x0b };
    uint8_t read_ops[] = { 0x01, 0x08, 0x40 };
    int i, j, k;
    uint16_t val;

    if (fd < 0) err(1, "open /dev/mem");
    h = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (h == MAP_FAILED) err(1, "mmap");

    HW(0x4001, 0x74);
    HW(0x4002, 0xdf);
    HW(0x4005, 0x00);
    HW(0x3000, 0xee);
    usleep(5000);
    HW(0x4005, 0x01);
    usleep(5000);

    printf("before: PHY_CONF0=0x%02x PHY_STAT0=0x%02x IH=0x%02x\n",
        HR(0x3000), HR(0x3004), HR(0x0104));

    for (i = 0; i < (int)(sizeof(slaves)/sizeof(slaves[0])); i++) {
        for (j = 0; j < (int)(sizeof(divs)/sizeof(divs[0])); j++) {
            int wrc = phy_write(slaves[i], divs[j], 0x06, 0x0008);
            printf("slave=0x%02x div=0x%02x write_rc=%d INT=0x%02x\n",
                slaves[i], divs[j], wrc, HR(0x3027));
            for (k = 0; k < (int)(sizeof(read_ops)/sizeof(read_ops[0])); k++) {
                int rrc = phy_read(slaves[i], divs[j], 0x06, read_ops[k], &val);
                printf("  read_op=0x%02x rc=%d val=0x%04x\n",
                    read_ops[k], rrc, rrc ? 0xffff : val);
            }
        }
    }
    return 0;
}
