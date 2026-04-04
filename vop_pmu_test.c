/*
 * vop_pmu_test.c - RK3399 PD_VIO power domain enable + VOP register test
 *
 * RK3399 PMU base:  0xff310000
 * PMU_PWRDN_CON:    PMU_BASE + 0x0014  (write 0 to bit = power ON)
 * PMU_PWRDN_ST:     PMU_BASE + 0x0018  (bit=0 means domain is powered)
 *
 * PD_VIO is bit 12 in both registers.
 *
 * After powering PD_VIO, re-read VOP_VERSION to confirm hardware responds.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define PMU_BASE            0xff310000
#define PMU_MAP_SIZE        0x1000
#define PMU_PWRDN_CON       0x0014
#define PMU_PWRDN_ST        0x0018

#define PD_VIO_BIT          (1 << 12)

#define VOP_BIG_BASE        0xff900000
#define VOP_LIT_BASE        0xff8f0000
#define VOP_MAP_SIZE        0x2000

#define VOP_VERSION         0x0000
#define VOP_SYS_CTRL        0x0008
#define VOP_DSP_CTRL0       0x0010
#define VOP_WIN0_CTRL0      0x0030

static volatile uint32_t *pmu;

static uint32_t pmu_read(uint32_t offset)
{
    return pmu[offset / 4];
}

static void pmu_write(uint32_t offset, uint32_t val)
{
    pmu[offset / 4] = val;
}

static int enable_pd_vio(void)
{
    uint32_t con, st;
    int timeout;

    con = pmu_read(PMU_PWRDN_CON);
    st  = pmu_read(PMU_PWRDN_ST);

    printf("PMU_PWRDN_CON before: 0x%08x\n", con);
    printf("PMU_PWRDN_ST  before: 0x%08x\n", st);

    if (!(st & PD_VIO_BIT)) {
        printf("PD_VIO is already powered on.\n");
        return 0;
    }

    printf("PD_VIO is OFF (bit 12 set in PWRDN_ST). Powering on...\n");

    /* Clear bit 12 in PWRDN_CON to request power-on */
    con &= ~PD_VIO_BIT;
    pmu_write(PMU_PWRDN_CON, con);

    /* Poll PWRDN_ST until bit 12 clears (domain powered) */
    timeout = 1000;
    while (timeout--) {
        st = pmu_read(PMU_PWRDN_ST);
        if (!(st & PD_VIO_BIT))
            break;
        usleep(100);
    }

    con = pmu_read(PMU_PWRDN_CON);
    st  = pmu_read(PMU_PWRDN_ST);

    printf("PMU_PWRDN_CON after:  0x%08x\n", con);
    printf("PMU_PWRDN_ST  after:  0x%08x\n", st);

    if (st & PD_VIO_BIT) {
        printf("ERROR: PD_VIO failed to power on (timed out)\n");
        return -1;
    }

    printf("PD_VIO powered on OK.\n");
    return 0;
}

static void test_vop(int fd, const char *name, off_t base)
{
    void *map;
    volatile uint32_t *regs;
    uint32_t version, sys_ctrl, dsp_ctrl0, win0_ctrl0;

    printf("\n--- %s @ 0x%08lx ---\n", name, base);

    map = mmap(NULL, VOP_MAP_SIZE, PROT_READ, MAP_SHARED, fd, base);
    if (map == MAP_FAILED) {
        printf("  mmap FAILED: %s\n", strerror(errno));
        return;
    }

    regs = (volatile uint32_t *)map;

    version    = regs[VOP_VERSION   / 4];
    sys_ctrl   = regs[VOP_SYS_CTRL  / 4];
    dsp_ctrl0  = regs[VOP_DSP_CTRL0 / 4];
    win0_ctrl0 = regs[VOP_WIN0_CTRL0/ 4];

    printf("  VOP_VERSION   (0x%04x): 0x%08x\n", VOP_VERSION,    version);
    printf("  VOP_SYS_CTRL  (0x%04x): 0x%08x\n", VOP_SYS_CTRL,   sys_ctrl);
    printf("  VOP_DSP_CTRL0 (0x%04x): 0x%08x\n", VOP_DSP_CTRL0,  dsp_ctrl0);
    printf("  VOP_WIN0_CTRL0(0x%04x): 0x%08x\n", VOP_WIN0_CTRL0, win0_ctrl0);

    if (version == 0xffffffff)
        printf("  >> 0xffffffff - bus error, something still blocking\n");
    else if (version == 0x00000000)
        printf("  >> VERSION=0 - power domain or IOMMU may still be blocking\n");
    else
        printf("  >> VOP live - version register valid!\n");

    munmap(map, VOP_MAP_SIZE);
}

int main(void)
{
    int memfd;
    void *pmu_map;

    printf("RK3399 PD_VIO power domain enable + VOP register test\n");
    printf("=======================================================\n\n");

    memfd = open("/dev/mem", O_RDWR);
    if (memfd < 0) {
        fprintf(stderr, "open(/dev/mem) O_RDWR failed: %s\n", strerror(errno));
        fprintf(stderr, "Run as root.\n");
        return 1;
    }

    pmu_map = mmap(NULL, PMU_MAP_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, memfd, PMU_BASE);
    if (pmu_map == MAP_FAILED) {
        fprintf(stderr, "PMU mmap failed: %s\n", strerror(errno));
        close(memfd);
        return 1;
    }
    pmu = (volatile uint32_t *)pmu_map;

    printf("=== PMU PD_VIO power domain ===\n");
    if (enable_pd_vio() != 0) {
        munmap(pmu_map, PMU_MAP_SIZE);
        close(memfd);
        return 1;
    }

    usleep(5000);   /* let domain settle */

    printf("\n=== VOP register read after PD_VIO enable ===\n");
    test_vop(memfd, "VOP big   (rk3399-vop-big)", VOP_BIG_BASE);
    test_vop(memfd, "VOP little (rk3399-vop-lit)", VOP_LIT_BASE);

    munmap(pmu_map, PMU_MAP_SIZE);
    close(memfd);

    printf("\n=======================================================\n");
    printf("If VERSION still 0, next step is IOMMU bypass or reset.\n");

    return 0;
}
