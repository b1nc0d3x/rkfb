#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);

    hdmi[(0x4001)*4] = 0x00;
    hdmi[(0x4002)*4] = 0xff;
    usleep(10000);

    /* Check interrupt mute registers */
    printf("IH_MUTE         [0x01ff] = 0x%02x\n", hdmi[(0x01ff)*4]);
    printf("IH_MUTE_PHY     [0x0184] = 0x%02x\n", hdmi[(0x0184)*4]);
    printf("IH_MUTE_I2CM    [0x0185] = 0x%02x\n", hdmi[(0x0185)*4]);
    printf("PHY_I2CM_CTLINT [0x3028] = 0x%02x\n", hdmi[(0x3028)*4]);

    /* Check MC_CLKDIS more carefully */
    printf("MC_CLKDIS       [0x4001] = 0x%02x\n", hdmi[(0x4001)*4]);
    printf("MC_OPCTRL       [0x4003] = 0x%02x\n", hdmi[(0x4003)*4]);
    printf("MC_FLOWCTRL     [0x4004] = 0x%02x\n", hdmi[(0x4004)*4]);
    printf("MC_PHYRSTZ      [0x4005] = 0x%02x\n", hdmi[(0x4005)*4]);

    /* Unmute I2C master interrupt */
    hdmi[(0x01ff)*4] = 0x00;
    hdmi[(0x0184)*4] = 0x00;
    hdmi[(0x3028)*4] = 0x00;   /* PHY_I2CM_CTLINT: unmute all */

    /* Try I2C write */
    hdmi[(0x3000)*4] = 0xc2;
    usleep(5000);
    hdmi[(0x3029)*4] = 0x17;
    hdmi[(0x3020)*4] = 0x69;
    hdmi[(0x3021)*4] = 0x06;
    hdmi[(0x3022)*4] = 0x00;
    hdmi[(0x3023)*4] = 0x08;

    printf("\nTriggering write...\n");
    hdmi[(0x3026)*4] = 0x10;

    int i;
    for (i = 0; i < 20; i++) {
        usleep(5000);
        printf("  INT=0x%02x CTLINT=0x%02x\n",
            hdmi[(0x3027)*4], hdmi[(0x3028)*4]);
        if (hdmi[(0x3027)*4] & 0x0a) break;
    }
close(fd);
    return 0;
}
