/*
 * vop_start.c - Try different SYS_CTRL bits to start VOP scanning
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

static void vop_commit(void) { VW(0, 0x01); usleep(50000); }

static void print_vop(void) {
    printf("  SYS_CTRL:     0x%08x\n", VR(0x0008));
    printf("  DSP_CTRL0:    0x%08x  bits[3:0]=%u\n", VR(0x0004), VR(0x0004)&0xf);
    printf("  WIN0_CTRL0:   0x%08x\n", VR(0x0030));
    printf("  SCAN_LINE_NUM:0x%08x\n", VR(0x02A0));
    printf("  INTR_STATUS0: 0x%08x\n", VR(0x0284));
    printf("  IH_FC_STAT0:  0x%02x\n", HR(0x0100));
}

int main(void)
{
    int fd, i;
    uint32_t sc;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_vop  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (g_vop == MAP_FAILED || g_hdmi == MAP_FAILED) err(1, "mmap");

    printf("=== VOP start test ===\n\nBefore:\n");
    print_vop();

    /* Step 1: Fix DSP_CTRL0 bits[3:0]=0 (RGB888) */
    {
        uint32_t d = VR(0x0004) & ~0xfu;
        VW(0x0004, d);
    }

    /* Step 2: Set SYS_CTRL - try setting bit1 AND bit11=0 */
    sc = VR(0x0008);
    printf("\nSYS_CTRL original: 0x%08x\n", sc);
    sc &= ~(1u << 11);  /* clear standby (bit11) */
    sc |=  (1u << 1);   /* set bit1 (phy_fullseq needed this) */
    sc |=  (1u << 13);  /* hdmi_out_en */
    printf("SYS_CTRL writing:  0x%08x\n", sc);
    VW(0x0008, sc);

    vop_commit();

    printf("\nAfter bit1+commit:\n");
    print_vop();

    printf("\nSCAN_LINE_NUM over 100ms:\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  IH_FC=0x%02x  INTR=0x%08x\n",
            i, VR(0x02A0), HR(0x0100), VR(0x0284));
        usleep(10000);
    }

    printf("\nFinal state:\n");
    print_vop();

    close(fd);
    return 0;
}
