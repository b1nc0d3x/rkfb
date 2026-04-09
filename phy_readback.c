/*
 * phy_readback.c - Read Innosilicon PHY registers via DW-HDMI PHY I2CM
 * Reads all regs without changing anything.
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define HDMI_PA 0xff940000UL

static volatile uint8_t *g_hdmi;
#define HR(o) g_hdmi[(o)*4]
#define HW(o,v) (g_hdmi[(o)*4]=(uint8_t)(v))

static int
phy_i2c_read(uint8_t reg, uint16_t *out)
{
    int i; uint8_t st;
    HW(0x3020, 0x69);
    HW(0x3021, reg);
    HW(0x3026, 0x01); /* read */
    for (i = 0; i < 200; i++) {
        usleep(500); st = HR(0x3027);
        if (st & 0x02) {
            HW(0x3027, 0x02);
            *out = ((uint16_t)HR(0x3024) << 8) | HR(0x3025);
            return 0;
        }
        if (st & 0x08) { HW(0x3027, 0x08); return -1; }
    }
    return -2; /* timeout */
}

int
main(void)
{
    int fd, rc, i;
    uint16_t val;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (g_hdmi == MAP_FAILED) err(1, "mmap");

    printf("PHY register readback (via I2CM slave=0x69)\n");
    printf("MC_PHYRSTZ=0x%02x PHY_CONF0=0x%02x PHY_STAT0=0x%02x\n",
        HR(0x4005), HR(0x3000), HR(0x3004));
    printf("I2CM_DIV=0x%02x\n\n", HR(0x3029));

    /* Set I2C divider for stable reads */
    HW(0x3020, 0x69);
    HW(0x3029, 0x17);
    HW(0x3027, 0xff);
    HW(0x3028, 0xff);

    printf("MPLL registers (0x00-0x30):\n");
    for (i = 0x00; i <= 0x30; i++) {
        rc = phy_i2c_read(i, &val);
        if (rc == 0)
            printf("  [0x%02x] = 0x%04x\n", i, val);
        else
            printf("  [0x%02x] = ERR(%d)\n", i, rc);
        usleep(1000);
    }

    close(fd);
    return 0;
}
