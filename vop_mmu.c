/*
 * vop_mmu.c - Test if VOP bit17 (mmu_en) is blocking DMA
 *
 * SYS_CTRL = 0x20822000:
 *   bit29=1: unknown (write-back? scaler?)
 *   bit23=1: might be dp_out_en (DisplayPort output)
 *   bit17=1: might be mmu_en (IOMMU for VOP DMA)
 *   bit13=1: hdmi_out_en
 *
 * If bit17=mmu_en=1 and IOMMU has no mapping, VOP DMA hangs.
 * Test: clear bit17 and see if VOP starts.
 *
 * Also clear bit23 (might cause DP conflict) and bit29.
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_PA  0xff900000UL
#define HDMI_PA 0xff940000UL

static volatile uint32_t *g_vop;
static volatile uint8_t  *g_hdmi;
#define VR(o) g_vop[(o)/4]
#define VW(o,v) (g_vop[(o)/4] = (v))
#define HR(o) g_hdmi[(o)*4]

static void print_state(const char *label) {
    printf("%s:\n", label);
    printf("  SYS_CTRL:    0x%08x\n", VR(0x0008));
    printf("  DSP_CTRL0:   0x%08x  bits[3:0]=%u\n", VR(0x0004), VR(0x0004)&0xf);
    printf("  WIN0_CTRL0:  0x%08x\n", VR(0x0030));
    printf("  WIN0_YRGB:   0x%08x\n", VR(0x0040));
    printf("  SCAN_LINE:   0x%08x\n", VR(0x02A0));
    printf("  INTR_STATUS: 0x%08x\n", VR(0x0284));
    printf("  IH_FC_STAT0: 0x%02x\n", HR(0x0100));
}

int main(void)
{
    int fd, i;
    uint32_t sc;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_vop  = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (g_vop == MAP_FAILED || g_hdmi == MAP_FAILED) err(1, "mmap");

    print_state("Before");

    /* Write SYS_CTRL with ONLY hdmi_out_en and bit1 set, clear bits 17/23/29 */
    sc = VR(0x0008);
    printf("\nOriginal SYS_CTRL: 0x%08x\n", sc);
    printf("  bit29=%d (write-back?)\n", (sc>>29)&1);
    printf("  bit23=%d (dp_out_en?)\n", (sc>>23)&1);
    printf("  bit17=%d (mmu_en?)\n", (sc>>17)&1);
    printf("  bit13=%d (hdmi_out_en)\n", (sc>>13)&1);
    printf("  bit11=%d (standby)\n", (sc>>11)&1);
    printf("  bit1=%d  (unknown)\n", (sc>>1)&1);

    printf("\nTest 1: Clear bit17 (mmu_en) only...\n");
    sc = VR(0x0008);
    sc &= ~(1u << 17);   /* clear mmu_en */
    sc |=  (1u << 1);    /* set bit1 (needed by phy_fullseq) */
    printf("Writing SYS_CTRL = 0x%08x\n", sc);
    VW(0x0008, sc);
    VW(0, 0x01);  /* REG_CFG_DONE */
    usleep(50000);

    printf("\nSCAN_LINE_NUM over 100ms:\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  INTR=0x%08x  IH_FC=0x%02x\n",
            i, VR(0x02A0), VR(0x0284), HR(0x0100));
        usleep(10000);
    }
    print_state("After test1 (clear bit17)");

    printf("\n\nTest 2: Clear bits 17+23+29...\n");
    sc = VR(0x0008);
    sc &= ~((1u<<17)|(1u<<23)|(1u<<29));  /* clear mystery bits */
    sc |=  (1u << 1);    /* bit1 */
    sc |=  (1u << 13);   /* hdmi_out */
    printf("Writing SYS_CTRL = 0x%08x\n", sc);
    VW(0x0008, sc);
    VW(0, 0x01);  /* REG_CFG_DONE */
    usleep(50000);

    printf("\nSCAN_LINE_NUM over 100ms:\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  INTR=0x%08x  IH_FC=0x%02x\n",
            i, VR(0x02A0), VR(0x0284), HR(0x0100));
        usleep(10000);
    }
    print_state("After test2 (clear bits 17+23+29)");

    close(fd);
    return 0;
}
