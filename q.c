#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>


int main(void){
    int fd=open("/dev/mem",0);
    volatile uint32_t *grf=mmap(0,0x1000,1,1,fd,0xff320000);
    volatile uint32_t *g4=mmap(0,0x100,1,1,fd,0xff790000);
    printf("GPIO4C_IOMUX=0x%08x GPIO4_DR=0x%08x HPD_bit23=%d\n",
        grf[0x10c/4], g4[0], (g4[0]>>23)&1);

    close(fd);

    return 0;

}
