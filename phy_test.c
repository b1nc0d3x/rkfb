#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>


int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);
    printf("PHY_CONF0    [0x3000] = 0x%02x\n", hdmi[(0x3000)*4]);
    printf("PHY_STAT0    [0x3004] = 0x%02x\n", hdmi[(0x3004)*4]);
    printf("PHY_I2CM_DIV [0x3029] = 0x%02x\n", hdmi[(0x3029)*4]);
    hdmi[(0x3029)*4] = 0x17;
    printf("PHY_I2CM_DIV after write: 0x%02x\n", hdmi[(0x3029)*4]);
    printf("PHY_I2CM_INT [0x3027] = 0x%02x\n", hdmi[(0x3027)*4]);
close(fd);    
return 0;
}
