#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "rkfb_ioctl.h"

int main(void) {
    int fd = open("/dev/rkfb0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    struct rkfb_regop ro;
    ro.block = 3;
    ro.off   = 0x4001;   /* MC_CLKDIS */
    ro.val   = 0x00;

    printf("Before write: MC_CLKDIS = ");
    if (ioctl(fd, RKFB_REG_READ, &ro) == 0)
        printf("0x%02x\n", ro.val);

    printf("Writing 0x00 to MC_CLKDIS [0x4001]...\n");
    ro.val = 0x00;
    if (ioctl(fd, RKFB_HDMI_REG_WRITE, &ro) < 0)
        perror("ioctl HDMI_WRITE");
    else
        printf("Write returned OK\n");

    printf("After write: MC_CLKDIS = ");
    if (ioctl(fd, RKFB_REG_READ, &ro) == 0)
        printf("0x%02x\n", ro.val);

    close(fd);
    printf("Done.\n");
    return 0;
}
