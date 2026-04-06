#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint32_t *gpio2 = mmap(NULL,0x100,PROT_READ,MAP_SHARED,fd,0xff780000);
    volatile uint32_t *gpio4 = mmap(NULL,0x100,PROT_READ,MAP_SHARED,fd,0xff790000);
    printf("GPIO2_DR    [0x00] = 0x%08x\n", gpio2[0]);
    printf("GPIO2_DDR   [0x04] = 0x%08x\n", gpio2[1]);
    printf("GPIO2 bit15 (B7/HPD) = %d\n", (gpio2[0]>>15)&1);
    printf("GPIO4_DR    [0x00] = 0x%08x\n", gpio4[0]);
    close(fd);

    return 0;
}
