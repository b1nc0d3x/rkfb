#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void){
    int fd=open("/dev/mem",0);
    volatile uint32_t *grf=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff320000);
    printf("GPIO4C_IOMUX [0x10c] = 0x%08x\n", grf[0x10c/4]);
    printf("  C7 bits[15:14] = %d (1=HDMI_HPD)\n", (grf[0x10c/4]>>14)&3);
    printf("  C6 bits[13:12] = %d (1=HDMI_SDA)\n", (grf[0x10c/4]>>12)&3);
    printf("  C5 bits[11:10] = %d (1=HDMI_SCL)\n", (grf[0x10c/4]>>10)&3);
    printf("GPIO4D_IOMUX [0x110] = 0x%08x\n", grf[0x110/4]);
    printf("  D3 bits[7:6]   = %d\n", (grf[0x110/4]>>6)&3);
    printf("  D2 bits[5:4]   = %d\n", (grf[0x110/4]>>4)&3);

    close(fd);

    return 0;

      }
