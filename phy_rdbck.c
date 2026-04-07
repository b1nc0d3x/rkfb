#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,
        MAP_SHARED,fd,0xff940000);

    /* Setup I2C to read back PHY internal registers */
    hdmi[0x4001] = 0x00;
    hdmi[0x4002] = 0xff;
    hdmi[0x3029] = 0x17;  /* I2C divider */
    hdmi[0x3000] = 0xe2;  /* PHY powered up so MPLL is active */
    usleep(5000);

    printf("PHY_CONF0    = 0x%02x\n", hdmi[0x3000]);
    printf("PHY_STAT0    = 0x%02x\n", hdmi[0x3004]);
    printf("MC_PHYRSTZ   = 0x%02x\n", hdmi[0x4005]);
    printf("MC_LOCKONCLOCK = 0x%02x\n", hdmi[0x4006]);

    /* Read PHY internal reg 0x06 (MPLL_CNTRL) via I2C read */
    /* PHY I2C read operation */
    hdmi[0x3020] = 0x69;  /* slave */
    hdmi[0x3021] = 0x06;  /* reg addr */
    hdmi[0x3026] = 0x40;  /* trigger READ (bit6=1) */
    for (int i=0; i<50; i++) {
        usleep(500);
        uint8_t st = hdmi[0x3027];
        if (st & 0x02) {
            hdmi[0x3027] = 0x02;
            printf("PHY reg 0x06 readback: MSB=0x%02x LSB=0x%02x (want 0x0008)\n",
                hdmi[0x3024], hdmi[0x3025]);
            break;
        }
        if (st & 0x08) {
            hdmi[0x3027] = 0x08;
            printf("PHY I2C read error\n");
            break;
        }
    }

    /* Try reading reg 0x25 (VLEVCTRL) */
    hdmi[0x3021] = 0x25;
    hdmi[0x3026] = 0x40;
    for (int i=0; i<50; i++) {
        usleep(500);
        uint8_t st = hdmi[0x3027];
        if (st & 0x02) {
            hdmi[0x3027] = 0x02;
            printf("PHY reg 0x25 readback: MSB=0x%02x LSB=0x%02x (want 0x0272)\n",
                hdmi[0x3024], hdmi[0x3025]);
            break;
        }
        if (st & 0x08) { hdmi[0x3027]=0x08; break; }
    }

    /* Now power up fully and release reset, check lock */
    hdmi[0x3000] = 0xf2;
    usleep(2000);
    hdmi[0x4005] = 0x00;
    usleep(100);
    hdmi[0x4005] = 0x01;
    usleep(100000);  /* 100ms */

    printf("\nAfter full power-up + reset release:\n");
    printf("PHY_STAT0      = 0x%02x\n", hdmi[0x3004]);
    printf("MC_LOCKONCLOCK = 0x%02x\n", hdmi[0x4006]);
    printf("IH_PHY_STAT0   = 0x%02x\n", hdmi[0x0104]);

    close(fd);
    return 0;
}
