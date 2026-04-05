#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x10000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);
    printf("MC_CLKDIS before: 0x%02x\n", hdmi[0x4001]);
    printf("MC_SWRSTZREQ before: 0x%02x\n", hdmi[0x4002]);
    hdmi[0x4001] = 0x00;
    printf("Write completed\n");
    hdmi[0x4002] = 0xff;
    printf("MC_SWRSTZREQ after: 0x%02x\n", hdmi[0x4002]);
    
close(fd);
return 0;
}
