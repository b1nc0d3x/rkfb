/*
 * vop_clk_test.c - RK3399 VOP clock enable + register read test
 *
 * Pokes the CRU directly via /dev/mem to ungate VOP clocks,
 * then re-reads VOP registers to confirm hardware responds.
 *
 * RK3399 CRU base:  0xff760000
 * CLKGATE_CON[n] at CRU_BASE + 0x300 + (n * 4)
 * Writes use HIWORD_UPDATE: upper 16 bits = mask, lower 16 = value
 * Writing 0 to a gate bit = clock ENABLED
 *
 * VOP big (vop0):
 *   aclk_vop0: CLKGATE_CON28 bit 3
 *   hclk_vop0: CLKGATE_CON28 bit 4
 *   dclk_vop0: CLKGATE_CON27 bit 1
 *
 * VOP little (vop1):
 *   aclk_vop1: CLKGATE_CON28 bit 7
 *   hclk_vop1: CLKGATE_CON28 bit 8
 *   dclk_vop1: CLKGATE_CON27 bit 5
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define CRU_BASE        0xff760000
#define CRU_MAP_SIZE    0x1000

#define CLKGATE_CON(n)  (0x300 + (n) * 4)

/* HIWORD_UPDATE: set bits in mask to value */
#define HIWORD_UPDATE(val, mask)  (((mask) << 16) | ((val) & (mask)))

/* VOP base addresses */
#define VOP_BIG_BASE    0xff900000
#define VOP_LIT_BASE    0xff8f0000
#define VOP_MAP_SIZE    0x2000

/* VOP registers */
#define VOP_VERSION     0x0000
#define VOP_SYS_CTRL    0x0008
#define VOP_DSP_CTRL0   0x0010
#define VOP_WIN0_CTRL0  0x0030

static volatile uint32_t *cru;

static void cru_write(uint32_t offset, uint32_t val)
{
    cru[offset / 4] = val;
    /* read back to confirm */
    (void)cru[offset / 4];
}

static uint32_t cru_read(uint32_t offset)
{
    return cru[offset / 4];
}

static void enable_vop_clocks(void)
{
    uint32_t val;

    printf("CRU CLKGATE_CON27 before: 0x%08x\n",
           cru_read(CLKGATE_CON(27)));
    printf("CRU CLKGATE_CON28 before: 0x%08x\n",
           cru_read(CLKGATE_CON(28)));

    /*
     * Enable VOP big clocks:
     *   CLKGATE_CON27 bit1 (dclk_vop0) = 0 (ungate)
     *   CLKGATE_CON28 bit3 (aclk_vop0) = 0
     *   CLKGATE_CON28 bit4 (hclk_vop0) = 0
     */
    cru_write(CLKGATE_CON(27), HIWORD_UPDATE(0, (1 << 1)));
    cru_write(CLKGATE_CON(28), HIWORD_UPDATE(0, (1 << 3) | (1 << 4)));

    /*
     * Enable VOP little clocks:
     *   CLKGATE_CON27 bit5 (dclk_vop1) = 0
     *   CLKGATE_CON28 bit7 (aclk_vop1) = 0
     *   CLKGATE_CON28 bit8 (hclk_vop1) = 0
     */
    cru_write(CLKGATE_CON(27), HIWORD_UPDATE(0, (1 << 5)));
    cru_write(CLKGATE_CON(28), HIWORD_UPDATE(0, (1 << 7) | (1 << 8)));

    printf("CRU CLKGATE_CON27 after:  0x%08x\n",
           cru_read(CLKGATE_CON(27)));
    printf("CRU CLKGATE_CON28 after:  0x%08x\n",
           cru_read(CLKGATE_CON(28)));
}

static int test_vop(int fd, const char *name, off_t base)
{
    void *map;
    volatile uint32_t *regs;
    uint32_t version, sys_ctrl, dsp_ctrl0, win0_ctrl0;

    printf("\n--- %s @ 0x%08lx ---\n", name, base);

    map = mmap(NULL, VOP_MAP_SIZE, PROT_READ, MAP_SHARED, fd, base);
    if (map == MAP_FAILED) {
        printf("  mmap FAILED: %s\n", strerror(errno));
        return -1;
    }

    regs = (volatile uint32_t *)map;

    version   = regs[VOP_VERSION  / 4];
    sys_ctrl  = regs[VOP_SYS_CTRL / 4];
    dsp_ctrl0 = regs[VOP_DSP_CTRL0 / 4];
    win0_ctrl0= regs[VOP_WIN0_CTRL0 / 4];

    printf("  VOP_VERSION   (0x%04x): 0x%08x\n", VOP_VERSION,   version);
    printf("  VOP_SYS_CTRL  (0x%04x): 0x%08x\n", VOP_SYS_CTRL,  sys_ctrl);
    printf("  VOP_DSP_CTRL0 (0x%04x): 0x%08x\n", VOP_DSP_CTRL0, dsp_ctrl0);
    printf("  VOP_WIN0_CTRL0(0x%04x): 0x%08x\n", VOP_WIN0_CTRL0,win0_ctrl0);

    if (version == 0xffffffff)
        printf("  >> 0xffffffff - bus error, TZPC or power domain blocking\n");
    else if (version == 0x00000000)
        printf("  >> VERSION=0 - clocks may still be gated or power domain off\n");
    else
        printf("  >> VOP live - version reg valid\n");

    munmap(map, VOP_MAP_SIZE);
    return 0;
}

int main(void)
{
    int memfd;
    void *cru_map;

    printf("RK3399 VOP clock enable + register test\n");
    printf("=========================================\n\n");

    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0) {
        fprintf(stderr, "open(/dev/mem) O_RDWR failed: %s\n", strerror(errno));
        fprintf(stderr, "Run as root.\n");
        return 1;
    }

    /* Map CRU for clock gating control */
    cru_map = mmap(NULL, CRU_MAP_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, memfd, CRU_BASE);
    if (cru_map == MAP_FAILED) {
        fprintf(stderr, "CRU mmap failed: %s\n", strerror(errno));
        close(memfd);
        return 1;
    }
    cru = (volatile uint32_t *)cru_map;

    printf("=== Enabling VOP clocks via CRU ===\n");
    enable_vop_clocks();

    /* small delay for clocks to stabilize */
    usleep(1000);

    printf("\n=== Reading VOP registers ===\n");
    test_vop(memfd, "VOP big   (rk3399-vop-big)", VOP_BIG_BASE);
    test_vop(memfd, "VOP little (rk3399-vop-lit)", VOP_LIT_BASE);

    munmap(cru_map, CRU_MAP_SIZE);
    close(memfd);

    printf("\n=========================================\n");
    printf("Done. If VERSION is still 0, power domain\n");
    printf("(PD_VIO) may need to be enabled first.\n");

    return 0;
}
