/*
 * vop_timing.c - Read VOP timing registers directly via /dev/mem
 * Also check CRU clock gates for VOPB dclk/aclk/hclk
 * CRU at 0xFF760000, our offset = linux_offset - 0x100
 * CLKGATE_CON(n) at linux offset 0x300+n*4, our offset 0x200+n*4
 * CLKSEL_CON(49) at linux offset 0x1C4, our offset 0x0C4
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_PA  0xFF900000UL
#define CRU_PA  0xFF760000UL

static volatile uint32_t *g_vop;
static volatile uint32_t *g_cru;

#define VR(o)    g_vop[(o)/4]
#define VW(o,v)  (g_vop[(o)/4] = (v))
#define CR(o)    g_cru[(o)/4]

int main(void)
{
    int fd, i;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");

    g_vop = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_cru = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
    if (g_vop == MAP_FAILED || g_cru == MAP_FAILED) err(1, "mmap");

    printf("=== VOP Timing Registers (direct /dev/mem) ===\n\n");
    printf("SYS_CTRL     [0x0008]: 0x%08x\n", VR(0x0008));
    printf("DSP_CTRL0    [0x0004]: 0x%08x\n", VR(0x0004));
    printf("WIN0_CTRL0   [0x0030]: 0x%08x  (bit0=win_en)\n", VR(0x0030));
    printf("WIN0_YRGB    [0x0040]: 0x%08x  (framebuf addr)\n", VR(0x0040));
    printf("\nDisplay Timing (shadow committed values):\n");
    printf("HTOTAL_HS    [0x0188]: 0x%08x  (want 0x06710027)\n", VR(0x0188));
    printf("HACT_ST_END  [0x018C]: 0x%08x  (want 0x01040604)\n", VR(0x018c));
    printf("VTOTAL_VS    [0x0190]: 0x%08x  (want 0x02ed0004)\n", VR(0x0190));
    printf("VACT_ST_END  [0x0194]: 0x%08x  (want 0x001902e9)\n", VR(0x0194));

    printf("\nCRU clock registers:\n");
    printf("CLKSEL49     [0x00C4]: 0x%08x  (want bits[9:0]=0x207 = GPLL/8)\n", CR(0x00c4));
    /* CLKGATE_CON(28) at our offset 0x200+28*4 = 0x270 */
    printf("CLKGATE(28)  [0x0270]: 0x%08x  (bit2=dclk_vopb_gate, bit8=aclk_vopb_gate, bit9=hclk_vopb_gate)\n", CR(0x0270));
    printf("CLKGATE(27)  [0x026C]: 0x%08x\n", CR(0x026c));
    printf("CLKGATE(29)  [0x0274]: 0x%08x\n", CR(0x0274));

    printf("\nScanning status:\n");
    printf("SCAN_LINE    [0x02A0]: 0x%08x\n", VR(0x02A0));
    printf("INTR_STATUS  [0x0284]: 0x%08x\n", VR(0x0284));

    /* Try force-commit via /dev/mem directly */
    printf("\nForce commit REG_CFG_DONE=0x01...\n");
    VW(0x0000, 0x01);
    usleep(50000);  /* wait >1 frame @ 60Hz */
    printf("After commit + 50ms:\n");
    printf("SCAN_LINE    [0x02A0]: 0x%08x\n", VR(0x02A0));
    printf("WIN0_YRGB    [0x0040]: 0x%08x  (should update if committed)\n", VR(0x0040));
    printf("HTOTAL_HS    [0x0188]: 0x%08x\n", VR(0x0188));

    /* Poll scan for 500ms */
    printf("\nScan poll (500ms):\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  INTR=0x%08x\n", i, VR(0x02A0), VR(0x0284));
        usleep(50000);
    }

    close(fd);
    return 0;
}
