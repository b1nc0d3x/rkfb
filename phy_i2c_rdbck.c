/*
 * phy_i2c_readback.c - Read back PHY internal MPLL registers
 *
 * PHY_I2CM_OPERATION [0x3026]:
 *   0x10 = write
 *   0x08 = read  (NOT 0x40 as previously used)
 *
 * Build: cc -o phy_i2c_readback phy_i2c_readback.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static volatile uint8_t *hdmi;

static int phy_read(uint8_t reg, uint16_t *val)
{
    int i; uint8_t st;
    hdmi[(0x3020)*4] = 0x69;
    hdmi[(0x3021)*4] = reg;
    hdmi[(0x3026)*4] = 0x08;   /* RD operation - bit3, NOT bit6 */
    for (i=0; i<200; i++) {
        usleep(500);
        st = hdmi[(0x3027)*4];
        if (st & 0x02) {
            hdmi[(0x3027)*4] = 0x02;
            *val = ((uint16_t)hdmi[(0x3024)*4] << 8) | hdmi[(0x3025)*4];
            return 0;
        }
        if (st & 0x08) { hdmi[(0x3027)*4]=0x08; return -1; }
    }
    return -2; /* timeout */
}

static int phy_write(uint8_t reg, uint16_t val)
{
    int i; uint8_t st;
    hdmi[(0x3020)*4] = 0x69;
    hdmi[(0x3021)*4] = reg;
    hdmi[(0x3022)*4] = (val>>8)&0xff;
    hdmi[(0x3023)*4] = val&0xff;
    hdmi[(0x3026)*4] = 0x10;
    for (i=0; i<200; i++) {
        usleep(500);
        st = hdmi[(0x3027)*4];
        if (st & 0x02) { hdmi[(0x3027)*4]=0x02; return 0; }
        if (st & 0x08) { hdmi[(0x3027)*4]=0x08; return -1; }
    }
    return -2;
}

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    hdmi = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0xff940000);

    /* Minimal setup */
    hdmi[(0x4001)*4] = 0x00;
    hdmi[(0x4002)*4] = 0xff;
    hdmi[(0x3029)*4] = 0x17;
    hdmi[(0x3027)*4] = 0xff;
    hdmi[(0x3028)*4] = 0xff;

    printf("=== PHY I2C Readback Test ===\n\n");
    printf("PHY_CONF0 before power: 0x%02x\n\n", hdmi[(0x3000)*4]);

    /* Power up PHY so MPLL is active */
    hdmi[(0x3000)*4] = 0xe2;
    usleep(5000);
    printf("PHY_CONF0 after 0xe2: 0x%02x\n\n", hdmi[(0x3000)*4]);

    /* Write known values */
    printf("Writing MPLL regs...\n");
    phy_write(0x06, 0x0008);
    phy_write(0x0e, 0x0260);
    phy_write(0x10, 0x8009);
    phy_write(0x25, 0x0272);
    printf("Done.\n\n");

    /* Read back immediately */
    printf("Reading back:\n");
    uint16_t val;
    int rc;

    struct { uint8_t reg; uint16_t expect; } regs[] = {
        {0x06, 0x0008}, {0x0e, 0x0260},
        {0x10, 0x8009}, {0x25, 0x0272},
    };

    for (int i=0; i<4; i++) {
        rc = phy_read(regs[i].reg, &val);
        if (rc == 0)
            printf("  reg[0x%02x] = 0x%04x (expect 0x%04x) %s\n",
                regs[i].reg, val, regs[i].expect,
                val==regs[i].expect ? "OK" : "MISMATCH");
        else
            printf("  reg[0x%02x] = READ FAILED (rc=%d)\n", regs[i].reg, rc);
    }

    /* Try reading with PHY fully powered */
    printf("\nPowering up fully...\n");
    hdmi[(0x3000)*4] = 0xf2;
    usleep(2000);
    hdmi[(0x4005)*4] = 0x00; usleep(100);
    hdmi[(0x4005)*4] = 0x01; usleep(5000);

    printf("PHY_STAT0 = 0x%02x\n", hdmi[(0x3004)*4]);
    printf("MC_LOCKONCLOCK = 0x%02x\n", hdmi[(0x4006)*4]);

    printf("\nReading back after full power-up:\n");
    for (int i=0; i<4; i++) {
        rc = phy_read(regs[i].reg, &val);
        if (rc == 0)
            printf("  reg[0x%02x] = 0x%04x (expect 0x%04x) %s\n",
                regs[i].reg, val, regs[i].expect,
                val==regs[i].expect ? "OK" : "MISMATCH");
        else
            printf("  reg[0x%02x] = READ FAILED (rc=%d)\n", regs[i].reg, rc);
    }

    close(fd);
    return 0;
}
