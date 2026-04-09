/*
 * pmu_vio.c - Power up VIO power domain on RK3399
 *
 * From Linux drivers/soc/rockchip/pm_domains.c for RK3399:
 *   pwr_offset    = 0x14c  (PMU_PWRDN_CON within PMU at 0xFF310000)
 *   status_offset = 0x158  (PMU_PWRDN_ST)
 *   req_offset    = 0x60   (bus idle request)
 *   idle_offset   = 0x64   (bus idle status)
 *   ack_offset    = 0x60
 *
 * From include/dt-bindings/power/rk3399-power.h:
 *   RK3399_PD_VIO = 8, corresponding to BIT(10) in power registers
 *   (bits 2-10 in pwr/status registers, with PD_TCPD0=BIT(2), PD_VIO=BIT(10))
 *
 * Sequence to power up VIO (from Linux pm_domains.c):
 *   1. Write pwr_offset: clear VIO bit (BIT(10)) to request power-up
 *   2. Poll status_offset: wait for VIO bit to clear (powered up)
 *   3. Write req_offset: clear idle request for VIO bus
 *   4. Poll idle_offset: wait for VIO bus to become active
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define PMU_PA  0xFF310000UL
#define VOP_PA  0xFF900000UL

static volatile uint32_t *g_pmu;
static volatile uint32_t *g_vop;

#define PR(o) g_pmu[(o)/4]
#define PW(o,v) (g_pmu[(o)/4] = (v))
#define VR(o) g_vop[(o)/4]

int main(void)
{
    int fd, i;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_pmu = mmap(NULL, 0x200, PROT_READ|PROT_WRITE, MAP_SHARED, fd, PMU_PA);
    g_vop = mmap(NULL, 0x400, PROT_READ|PROT_WRITE, MAP_SHARED, fd, VOP_PA);
    if (g_pmu == MAP_FAILED || g_vop == MAP_FAILED) err(1, "mmap");

    printf("=== PMU VIO Power-Up ===\n\n");

    /* Show original offsets I tried */
    printf("Original PMU offsets (probably wrong):\n");
    printf("  [0x008C]: 0x%08x (thought this was PWRDN_CON)\n", PR(0x008C));
    printf("  [0x0098]: 0x%08x (thought this was PWRDN_ST)\n", PR(0x0098));

    /* Show Linux-documented offsets */
    printf("\nLinux pm_domains.c offsets:\n");
    printf("  [0x014C] pwr_offset:    0x%08x  (bit10=VIO power down ctrl)\n", PR(0x014C));
    printf("  [0x0158] status_offset: 0x%08x  (bit10=VIO powered down status)\n", PR(0x0158));
    printf("  [0x0060] req_offset:    0x%08x  (bus idle request)\n", PR(0x0060));
    printf("  [0x0064] idle_offset:   0x%08x  (bus idle status)\n", PR(0x0064));

    uint32_t pwr = PR(0x014C);
    uint32_t st  = PR(0x0158);
    printf("\nVIO (bit10): pwr=%d  st=%d\n", (pwr>>10)&1, (st>>10)&1);

    if ((pwr >> 10) & 1) {
        printf("VIO is powered down! Attempting power-up...\n");

        /* Step 1: Clear bit10 of pwr_offset to request power-up */
        PW(0x014C, pwr & ~(1u << 10));
        printf("Wrote pwr_offset=0x%08x\n", pwr & ~(1u << 10));

        /* Step 2: Poll for power-up */
        for (i = 0; i < 100; i++) {
            usleep(1000);
            if (!((PR(0x0158) >> 10) & 1)) {
                printf("VIO powered up after %d ms\n", i+1);
                break;
            }
        }
        printf("  pwr_offset after:    0x%08x\n", PR(0x014C));
        printf("  status_offset after: 0x%08x\n", PR(0x0158));

        /* Step 3: Release bus idle for VIO */
        uint32_t req = PR(0x0060);
        printf("\nBus idle req before: 0x%08x\n", req);
        /* VIO bus idle bit - try bit10 as well */
        if ((req >> 10) & 1) {
            PW(0x0060, req & ~(1u << 10));
            usleep(5000);
            printf("Bus idle req after: 0x%08x\n", PR(0x0060));
            printf("Bus idle st  after: 0x%08x\n", PR(0x0064));
        }
    } else {
        printf("VIO appears powered up in pwr register.\n");
        printf("Checking if VOP is scanning now:\n");
    }

    /* Check VOP state */
    printf("\nVOP state after PMU check:\n");
    printf("  SYS_CTRL:    0x%08x\n", VR(0x0008));
    printf("  SCAN_LINE:   0x%08x\n", VR(0x02A0));
    printf("  INTR_STATUS: 0x%08x\n", VR(0x0284));
    for (i = 0; i < 5; i++) {
        usleep(10000);
        printf("  [%d] scan=0x%08x\n", i, VR(0x02A0));
    }

    close(fd);
    return 0;
}
