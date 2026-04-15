#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void){
    int fd=open("/dev/mem",0);
    volatile uint32_t *vop=mmap(NULL,0x2000,PROT_READ,MAP_SHARED,fd,0xff900000);
    volatile uint32_t *viogrf=mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff770000);
    printf("SYS_CTRL    = 0x%08x  bit1(dclk_en)=%d bit11(standby)=%d\n",
        vop[0x08/4],(vop[0x08/4]>>1)&1,(vop[0x08/4]>>11)&1);
    printf("SYS_CTRL1   = 0x%08x\n", vop[0x0c/4]);
    printf("DSP_CTRL1   = 0x%08x\n", vop[0x1c/4]);
    printf("WIN0_CTRL0  = 0x%08x  en=%d\n", vop[0x30/4], vop[0x30/4]&1);
    printf("WIN0_YRGB   = 0x%08x\n", vop[0x40/4]);
    printf("DSP_HTOTAL  = 0x%08x  htotal=%d\n",
        vop[0x188/4],(vop[0x188/4]>>16)+1);
    printf("SOC_CON20   = 0x%08x  VOPB=%d\n",
        viogrf[0x250/4],(viogrf[0x250/4]>>6)&1);
    /* Check if VOP is generating pixel clock */
    printf("INTR_STATUS = 0x%08x\n", vop[0x284/4]);
    printf("LINE_COUNT  = 0x%08x\n", vop[0x290/4]);

    close(fd);

    return 0;

}
