#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0xff940000);
    volatile uint32_t *grf = mmap(NULL,0x8000,PROT_READ,MAP_SHARED,fd,0xff320000);

    /* Enable PHY power and release resets minimally */
    hdmi[(0x4001)*4]=0x00; hdmi[(0x4002)*4]=0xff;
    hdmi[(0x3000)*4]=0xe2; /* PDZ+ENTMDS+SPARECTRL */
    usleep(5000);
    hdmi[(0x4005)*4]=0x01; /* release PHY reset */
    usleep(5000);

    printf("Watching HPD for 30 seconds -- plug/unplug HDMI cable now\n");
    printf("PHY_STAT0[0x3004] GRF_STATUS1[0x04e4] GRF_STATUS5[0x04e8]\n");

    uint8_t last=0xff; uint32_t lgrf=0xffffffff;
    for(int i=0;i<300;i++) {
        uint8_t s=hdmi[(0x3004)*4];
        uint32_t g=grf[0x04e4/4];
        if(s!=last||g!=lgrf) {
            printf("[%3ds] PHY_STAT0=%02x IH_PHY=%02x GRF=0x%08x HPD_bit14=%d PHY_HPD=%d\n",
                i/10,s,hdmi[(0x0104)*4],g,(g>>14)&1,(s>>1)&1);
            last=s; lgrf=g;
        }
        usleep(100000);
    }
    printf("Done. Final: PHY_STAT0=%02x GRF_STATUS1=0x%08x\n",
        hdmi[(0x3004)*4], grf[0x04e4/4]);
    return 0;
}
