#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "rkfb_ioctl.h"

int main(void) {
    int fd = open("/dev/rkfb0", O_RDWR);
    struct rkfb_regop ro = { .block = 3, .off = 0x4001, .val = 0x00 };
    ioctl(fd, RKFB_HDMI_REG_WRITE, &ro);
    close(fd);
    return 0;
}
