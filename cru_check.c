/*
 * cru_check.c - Check VOP reset state in CRU and try to release it
 *
 * RK3399 SOFTRESET_CON(17) at Linux offset 0x444:
 *   Our offset = 0x444 - 0x100 = 0x344
 *   bit0: SRST_A_VOPB (aclk reset)
 *   bit1: SRST_P_VOPB (pclk reset)
 *   bit2: SRST_H_VOPB (hclk reset)
 *   bit3: SRST_D_VOPB (dclk reset)
 *   bit4: SRST_A_VOPL
 *   bit5: SRST_P_VOPL
 *   bit6: SRST_H_VOPL
 *   bit7: SRST_D_VOPL
 *
 * Write 1 = assert reset, write 0 = release reset (HIWORD_UPDATE)
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define CRU_PA  0xff760000UL
#define VOP_PA  0xff900000UL
#define HDMI_PA 0xff940000UL

static volatile uint32_t *g_cru;
static volatile uint32_t *g_vop;
static volatile uint8_t  *g_hdmi;

#define CR(o) g_cru[(o)/4]
#define CW(o,v) (g_cru[(o)/4] = (v))
#define VR(o) g_vop[(o)/4]
#define HR(o) g_hdmi[(o)*4]

static void cru_hiword(uint32_t off, uint32_t mask, uint32_t val)
{
    CW(off, (mask << 16) | (val & mask));
}

int main(void)
{
    int fd, i;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_cru  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
    g_vop  = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (!g_cru || !g_vop || !g_hdmi) err(1, "mmap");

    printf("=== CRU + VOP Reset Diagnostic ===\n\n");

    /* Check SOFTRESET registers for VOPB */
    printf("CRU SOFTRESET_CON(17) [0x0344]: 0x%08x  (bits[3:0]=VOPB reset, 1=held in reset)\n",
        CR(0x0344));
    printf("CRU SOFTRESET_CON(16) [0x0340]: 0x%08x\n", CR(0x0340));
    printf("CRU SOFTRESET_CON(18) [0x0348]: 0x%08x\n", CR(0x0348));

    /* Check clock gates */
    printf("\nCRU CLKGATE_CON(10) [0x0228]: 0x%08x  (bit12=DCLK_VOPB gate)\n", CR(0x0228));
    printf("CRU CLKGATE_CON(28) [0x0270]: 0x%08x  (bit4=ACLK_VOPB, bit6=HCLK_VOPB)\n", CR(0x0270));
    printf("CRU CLKSEL_CON(49)  [0x00c4]: 0x%08x  (bits[9:8]=src, bits[7:0]=div)\n", CR(0x00c4));

    printf("\nVOP state before reset release:\n");
    printf("  SYS_CTRL:    0x%08x\n", VR(0x0008));
    printf("  SCAN_LINE:   0x%08x\n", VR(0x02A0));
    printf("  INTR_STATUS: 0x%08x\n", VR(0x0284));

    /* Release VOPB from reset (HIWORD: mask=bits[3:0], val=0 to release) */
    printf("\nReleasing VOPB from reset (SOFTRESET_CON17 bits[3:0] = 0)...\n");
    cru_hiword(0x0344, 0xf, 0);
    usleep(5000);
    printf("CRU SOFTRESET_CON(17) [0x0344] after: 0x%08x\n", CR(0x0344));

    printf("\nVOP state after reset release:\n");
    printf("  SYS_CTRL:    0x%08x\n", VR(0x0008));
    printf("  SCAN_LINE:   0x%08x\n", VR(0x02A0));
    printf("  INTR_STATUS: 0x%08x\n", VR(0x0284));

    /* Trigger VOP commit */
    g_vop[0] = 0x01;
    usleep(50000);

    printf("\nSCAN_LINE_NUM over 100ms after reset release + commit:\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  INTR=0x%08x  IH_FC=0x%02x\n",
            i, VR(0x02A0), VR(0x0284), HR(0x0100));
        usleep(10000);
    }

    close(fd);
    return 0;
}
