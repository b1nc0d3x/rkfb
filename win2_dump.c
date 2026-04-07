/*
 * win2_dump.c - Dump VOP WIN2/WIN3 registers to find active EFI framebuffer layer
 * Build: cc -o win2_dump win2_dump.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint32_t *vop = mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff900000);

    printf("=== VOP Active Layer Dump ===\n\n");

    /* WIN0 */
    printf("WIN0_CTRL0  [0x0030] = 0x%08x  en=%d\n", vop[0x30/4], vop[0x30/4]&1);
    printf("WIN0_YRGB   [0x0040] = 0x%08x\n", vop[0x40/4]);

    /* WIN1 at 0x00b0 */
    printf("WIN1_CTRL0  [0x00b0] = 0x%08x  en=%d\n", vop[0xb0/4], vop[0xb0/4]&1);
    printf("WIN1_YRGB   [0x00c0] = 0x%08x\n", vop[0xc0/4]);

    /* WIN2 at 0x0170 */
    printf("WIN2_CTRL0  [0x0170] = 0x%08x  en=%d\n", vop[0x170/4], vop[0x170/4]&1);
    printf("WIN2_CTRL1  [0x0174] = 0x%08x\n", vop[0x174/4]);
    printf("WIN2_MST0   [0x0178] = 0x%08x\n", vop[0x178/4]);
    printf("WIN2_DSP_INFO0[0x0180]= 0x%08x\n", vop[0x180/4]);
    printf("WIN2_DSP_ST0[0x0184] = 0x%08x\n", vop[0x184/4]);

    /* WIN3 at 0x01d0 */
    printf("WIN3_CTRL0  [0x01d0] = 0x%08x  en=%d\n", vop[0x1d0/4], vop[0x1d0/4]&1);
    printf("WIN3_MST0   [0x01d8] = 0x%08x\n", vop[0x1d8/4]);

    /* VOP SYS regs */
    printf("\nSYS_CTRL    [0x0008] = 0x%08x\n", vop[0x08/4]);
    printf("SYS_CTRL1   [0x000c] = 0x%08x\n", vop[0x0c/4]);
    printf("DSP_CTRL0   [0x0010] = 0x%08x\n", vop[0x10/4]);
    printf("DSP_CTRL1   [0x001c] = 0x%08x\n", vop[0x1c/4]);
    printf("DSP_BG      [0x0020] = 0x%08x\n", vop[0x20/4]);

    /* Timing */
    printf("\nHTOTAL_HS   [0x0188] = 0x%08x  htotal=%d hsync=%d\n",
        vop[0x188/4], (vop[0x188/4]>>16)+1, (vop[0x188/4]&0xffff)+1);
    printf("HACT_ST_END [0x018c] = 0x%08x\n", vop[0x18c/4]);
    printf("VTOTAL_VS   [0x0190] = 0x%08x  vtotal=%d vsync=%d\n",
        vop[0x190/4], (vop[0x190/4]>>16)+1, (vop[0x190/4]&0xffff)+1);
    printf("VACT_ST_END [0x0194] = 0x%08x\n", vop[0x194/4]);

    /* Dump full 0x170-0x1a0 range */
    printf("\nWIN2 area [0x0170-0x01c0]:\n");
    for(int i=0x170; i<0x1c0; i+=4)
        printf("  [0x%04x]=0x%08x\n", i, vop[i/4]);
    close(fd);
    return 0;
}
