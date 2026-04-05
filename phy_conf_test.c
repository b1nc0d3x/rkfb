#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);
    printf("MC_SWRSTZREQ before: 0x%02x\n", hdmi[0x4002]);
    hdmi[0x4002] = 0xff;
    printf("MC_SWRSTZREQ after:  0x%02x\n", hdmi[0x4002]);
    usleep(5000);
    printf("PHY_CONF0 before: 0x%02x\n", hdmi[0x3000]);
    hdmi[0x3000] = 0x42;
    usleep(5000);
    printf("PHY_CONF0 after:  0x%02x\n", hdmi[0x3000]);
    printf("PHY_STAT0:        0x%02x\n", hdmi[0x3004]);
	close(fd); 
   return 0;
}
