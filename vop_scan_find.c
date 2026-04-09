/*
 * vop_scan_find.c - Poll all VOP registers to find any that change (scan counter)
 * Also check if VOP needs a specific enable bit
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#include <string.h>

#define VOP_PA   0xFF900000UL
#define IOMMU_PA 0xFF903F00UL   /* VOPB IOMMU - actual RK3399 address */
#define CRU_PA   0xFF760000UL

static volatile uint32_t *g_vop;
static volatile uint32_t *g_iommu;
static volatile uint32_t *g_cru;

#define VR(o)    g_vop[(o)/4]
#define VW(o,v)  (g_vop[(o)/4] = (v))
#define IR(o)    g_iommu[(o)/4]
#define IW(o,v)  (g_iommu[(o)/4] = (v))
#define CR(o)    g_cru[(o)/4]

int main(void)
{
    int fd, i, o;
    uint32_t before[0x400/4], after[0x400/4];

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_vop   = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    g_iommu = mmap(NULL, 0x100, PROT_READ|PROT_WRITE, MAP_SHARED, fd, IOMMU_PA);
    g_cru   = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, CRU_PA);
    if (g_vop == MAP_FAILED || g_iommu == MAP_FAILED || g_cru == MAP_FAILED)
        err(1, "mmap");

    /* Check actual IOMMU at 0xFF903F00 */
    printf("=== VOPB IOMMU at 0xFF903F00 ===\n");
    for (i = 0; i <= 0x28; i += 4)
        printf("  [0x%03x]: 0x%08x\n", i, IR(i));
    printf("  STATUS[0x004] bit1(enabled)=%d  bit0(active)=%d\n",
        (IR(0x004)>>1)&1, (IR(0x004)>>0)&1);
    printf("  INT_RAWSTA[0x008]: 0x%08x\n", IR(0x008));
    if ((IR(0x004) >> 1) & 1) {
        printf("  IOMMU IS ENABLED - disabling (COMMAND=0x1)...\n");
        IW(0x024, 0x1);
        usleep(5000);
        printf("  STATUS after: 0x%08x\n", IR(0x004));
    } else {
        printf("  IOMMU disabled or not present at this address\n");
    }

    /* Snapshot VOP registers */
    printf("\n=== Polling VOP regs for changing values ===\n");
    for (i = 0; i < 0x400/4; i++)
        before[i] = g_vop[i];
    usleep(100000);  /* 100ms = 6 frames @ 60Hz */
    for (i = 0; i < 0x400/4; i++)
        after[i] = g_vop[i];

    int found = 0;
    for (i = 0; i < 0x400/4; i++) {
        if (before[i] != after[i]) {
            printf("  CHANGING: [0x%04x]: 0x%08x -> 0x%08x\n",
                i*4, before[i], after[i]);
            found++;
        }
    }
    if (!found) printf("  No VOP registers changed in 100ms.\n");

    /* CRU soft reset register for VOP B */
    /* SOFTRST_CON(n) at linux 0x400+n*4, our offset 0x300+n*4 */
    printf("\n=== CRU Soft Reset Registers ===\n");
    for (i = 0; i <= 0x54; i += 4)
        printf("  SOFTRST[%2d][0x%04x]: 0x%08x\n", i/4, 0x300+i, CR(0x300+i));

    printf("\n=== SYS_CTRL analysis ===\n");
    uint32_t sc = VR(0x0008);
    printf("SYS_CTRL = 0x%08x\n", sc);
    for (i = 0; i < 32; i++)
        if ((sc >> i) & 1)
            printf("  bit%-2d = 1\n", i);

    /* Try: set standby then clear it (force VOP restart) */
    printf("\n=== Try VOP standby toggle ===\n");
    printf("Before: SYS_CTRL=0x%08x SCAN=0x%08x\n", VR(0x0008), VR(0x02A0));
    /* Set standby at bit22 (per Linux driver) */
    sc = VR(0x0008);
    sc |= (1u << 22);
    VW(0x0008, sc);
    VW(0x0000, 0x01);  /* cfg_done */
    usleep(20000);
    printf("Standby set: SYS_CTRL=0x%08x SCAN=0x%08x\n", VR(0x0008), VR(0x02A0));

    /* Clear standby */
    sc = VR(0x0008);
    sc &= ~(1u << 22);
    VW(0x0008, sc);
    VW(0x0000, 0x01);  /* cfg_done */
    usleep(50000);
    printf("Standby clear: SYS_CTRL=0x%08x SCAN=0x%08x\n", VR(0x0008), VR(0x02A0));

    for (i = 0; i < 10; i++) {
        printf("  [%2d] scan=0x%08x\n", i, VR(0x02A0));
        usleep(20000);
    }

    close(fd);
    return 0;
}
