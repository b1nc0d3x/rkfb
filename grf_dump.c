/*
 * grf_dump.c - Dump GRF and VIO GRF registers relevant to HDMI
 *
 * Build: cc -o grf_dump grf_dump.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void)
{
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint32_t *grf    = mmap(NULL,0x8000,PROT_READ,MAP_SHARED,fd,0xff320000);
    volatile uint32_t *viogrf = mmap(NULL,0x1000,PROT_READ,MAP_SHARED,fd,0xff770000);

    printf("=== GRF registers (0xff320000) ===\n");
    /* SOC_CON0-9 at 0x000-0x024 */
    for (int i = 0; i <= 9; i++)
        printf("  GRF_SOC_CON%-2d [0x%03x] = 0x%08x\n",
            i, i*4, grf[i]);

    printf("\n  GRF_SOC_CON20 [0x050] = 0x%08x\n", grf[0x050/4]);
    printf("  GRF_SOC_CON21 [0x054] = 0x%08x\n", grf[0x054/4]);
    printf("  GRF_SOC_CON25 [0x064] = 0x%08x\n", grf[0x064/4]);

    printf("\n  GRF_SOC_STATUS0 [0x480] = 0x%08x\n", grf[0x480/4]);
    printf("  GRF_SOC_STATUS1 [0x484] = 0x%08x  HPD bit14=%d\n",
        grf[0x484/4], (grf[0x484/4]>>14)&1);
    printf("  GRF_SOC_STATUS5 [0x4e8] = 0x%08x\n", grf[0x4e8/4]);

    printf("\n=== VIO GRF registers (0xff770000) ===\n");
    /* SOC_CON0-27 */
    for (int i = 0; i <= 27; i++) {
        uint32_t v = viogrf[i];
        if (v != 0)
            printf("  VIO_GRF_SOC_CON%-2d [0x%03x] = 0x%08x\n",
                i, i*4, v);
    }
    printf("  VIO_GRF_SOC_CON20 [0x250] = 0x%08x\n", viogrf[0x250/4]);
    printf("    bit6 HDMI_LCDC_SEL = %d  (1=VOPB)\n", (viogrf[0x250/4]>>6)&1);
    printf("    bit5 = %d\n", (viogrf[0x250/4]>>5)&1);
    printf("    bit4 = %d\n", (viogrf[0x250/4]>>4)&1);

    /* VIO status */
    printf("\n  VIO_GRF_SOC_STATUS [0x280] = 0x%08x\n", viogrf[0x280/4]);

    close(fd);
    return 0;
}
