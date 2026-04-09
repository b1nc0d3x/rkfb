/*
 * vop_diag.c - Read VOP status to see if it's scanning
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_PA 0xff900000UL

static volatile uint32_t *g_vop;
#define VR(o) g_vop[(o)/4]

int main(void)
{
    int fd, i;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_vop = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    if (g_vop == MAP_FAILED) err(1, "mmap VOP");

    printf("VOP diagnostic\n");
    printf("  SYS_CTRL     [0x0008]: 0x%08x\n", VR(0x0008));
    printf("  DSP_CTRL0    [0x0004]: 0x%08x  bits[3:0]=out_fmt\n", VR(0x0004));
    printf("  WIN0_CTRL0   [0x0030]: 0x%08x\n", VR(0x0030));
    printf("  WIN0_YRGB_MST[0x0040]: 0x%08x\n", VR(0x0040));
    printf("  DSP_HTOTAL   [0x0188]: 0x%08x\n", VR(0x0188));
    printf("  DSP_VTOTAL   [0x0190]: 0x%08x\n", VR(0x0190));
    printf("  INTR_STATUS0 [0x0284]: 0x%08x\n", VR(0x0284));
    printf("  INTR_EN0     [0x0280]: 0x%08x\n", VR(0x0280));
    printf("\nReading SCAN_LINE_NUM [0x02A0] 5 times:\n");
    for (i = 0; i < 5; i++) {
        printf("  [%d] 0x%08x\n", i, VR(0x02A0));
        usleep(4000);  /* ~1/4 of 720p60 vsync period */
    }
    usleep(50000);
    printf("\nINTR_STATUS0 after 50ms: 0x%08x\n", VR(0x0284));
    /* Try raw DSP_CTRL0 read and write via /dev/mem */
    printf("\nDSP_CTRL0 raw: 0x%08x\n", VR(0x0004));
    /* Clear bits[3:0] to set RGB888 */
    uint32_t dsp = VR(0x0004);
    printf("Writing DSP_CTRL0 with bits[3:0]=0...\n");
    g_vop[0x0004/4] = dsp & ~0xfu;
    /* REG_CFG_DONE */
    g_vop[0] = 0x01;
    usleep(50000);
    printf("DSP_CTRL0 after: 0x%08x\n", VR(0x0004));
    printf("INTR_STATUS0 after DSP_CTRL0 fix + commit: 0x%08x\n", VR(0x0284));
    close(fd);
    return 0;
}
