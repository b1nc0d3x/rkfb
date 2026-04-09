/*
 * vop_iommu.c - Check VOP IOMMU state and try to disable it
 *
 * VOPB IOMMU at 0xFF903000 (within VOP MMIO range)
 * RK IOMMU registers:
 *   0x000: DTE_ADDR (page table base)
 *   0x004: STATUS   (bit0=active, bit1=enabled)
 *   0x008: INT_RAWSTA  (raw interrupt status)
 *   0x00C: INT_STA     (masked interrupt status)
 *   0x010: INT_MASK    (interrupt mask)
 *   0x014: INT_CLR     (interrupt clear)
 *   0x018: FAULT_ADDR  (faulting address)
 *   0x01C: FAULT_STATUS
 *   0x020: AUTO_DISABLE (auto-disable on fault)
 *   0x024: COMMAND      (write to reset/enable/disable)
 *
 * If IOMMU is enabled (STATUS bit1=1) and has fault (INT_RAWSTA != 0),
 * the VOP bus transactions are being rejected -> VOP stalls.
 *
 * Fix: disable IOMMU (COMMAND=0x1) to use physical addresses directly.
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define VOP_PA   0xff900000UL   /* VOPB base */
#define IOMMU_PA 0xff903000UL   /* VOPB IOMMU (3000 bytes from VOP base) */
#define HDMI_PA  0xff940000UL

static volatile uint32_t *g_iommu;
static volatile uint32_t *g_vop;
static volatile uint8_t  *g_hdmi;

#define IR(o) g_iommu[(o)/4]
#define IW(o,v) (g_iommu[(o)/4] = (v))
#define VR(o) g_vop[(o)/4]
#define HR(o) g_hdmi[(o)*4]

int main(void)
{
    int fd, i;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_iommu = mmap(NULL, 0x100, PROT_READ|PROT_WRITE, MAP_SHARED, fd, IOMMU_PA);
    g_vop   = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_hdmi  = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, HDMI_PA);
    if (g_iommu == MAP_FAILED || g_vop == MAP_FAILED || g_hdmi == MAP_FAILED)
        err(1, "mmap");

    printf("=== VOPB IOMMU Check ===\n\n");
    printf("IOMMU registers at 0xFF903000:\n");
    for (i = 0; i <= 0x28; i += 4)
        printf("  [0x%03x]: 0x%08x\n", i, IR(i));

    uint32_t status = IR(0x004);
    printf("\nIOMMU STATUS [0x004]: 0x%08x\n", status);
    printf("  bit0 (active):  %d\n", (status>>0)&1);
    printf("  bit1 (enabled): %d  ← if 1, IOMMU is intercepting DMA\n", (status>>1)&1);

    uint32_t fault = IR(0x008);
    printf("INT_RAWSTA [0x008]: 0x%08x  ← non-zero = IOMMU fault/error\n", fault);
    printf("FAULT_ADDR [0x018]: 0x%08x\n", IR(0x018));
    printf("FAULT_ST   [0x01C]: 0x%08x\n", IR(0x01C));

    if ((status >> 1) & 1) {
        printf("\nIOMMU IS ENABLED. Disabling IOMMU (COMMAND=0x1 = reset)...\n");
        IW(0x024, 0x1);  /* COMMAND: disable/reset IOMMU */
        usleep(5000);
        printf("STATUS after disable: 0x%08x\n", IR(0x004));

        /* Also try ZEROIZE page table */
        IW(0x000, 0x00000000);  /* DTE_ADDR = 0 */
        usleep(1000);
        printf("DTE_ADDR after: 0x%08x\n", IR(0x000));
    } else {
        printf("\nIOMMU appears disabled.\n");
        /* Check for faults anyway */
        if (fault) {
            printf("But INT_RAWSTA=0x%08x (fault pending), clearing...\n", fault);
            IW(0x014, fault);  /* INT_CLR */
            usleep(1000);
        }
    }

    /* Try to start VOP after IOMMU fix */
    printf("\nAttempting VOP start with SYS_CTRL clean...\n");
    /* Set just the minimum: hdmi_out_en, not in standby, bit1=1 */
    VR(0x0008);  /* dummy read to flush */
    g_vop[0x0008/4] = 0x00002002;  /* bit13=HDMI_OUT, bit1 */
    g_vop[0] = 0x01;  /* REG_CFG_DONE */
    usleep(50000);

    printf("\nSCAN_LINE_NUM over 100ms:\n");
    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x  INTR=0x%08x  IH_FC=0x%02x\n",
            i, VR(0x02A0), VR(0x0284), HR(0x0100));
        usleep(10000);
    }
    printf("\nVOP SYS_CTRL: 0x%08x\n", VR(0x0008));
    printf("WIN0_YRGB:    0x%08x\n", VR(0x0040));

    close(fd);
    return 0;
}
