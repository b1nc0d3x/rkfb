/*
 * pmu_check.c - Check RK3399 PMU power domains for VOP/VIO
 *
 * PMU at 0xFF310000:
 *   PMU_PWRDN_CON  [0x008C]: power down control (1=power down)
 *   PMU_PWRDN_ST   [0x0098]: power domain status (1=powered down)
 *
 * RK3399 power domains (bits in PWRDN):
 *   bit0:  PD_CPUB (B cluster)
 *   bit1:  PD_GPU
 *   bit2:  PD_DDR
 *   bit3:  PD_VIO  ← VIO power domain (VOP, ISP, etc.)
 *   bit4:  PD_VOPB ← VOPB specifically? (or is it PD_VIO only?)
 *   bit5:  PD_VOPL?
 *   bit6:  PD_ISP0?
 *   ...
 *   bit20: PD_HDCP?
 *   bit21: PD_HDMI?
 *
 * RK3399 TRM: VIO power domain covers VOPB, VOPL, ISP, EDP, HDMI
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>

#define PMU_PA  0xFF310000UL

static volatile uint32_t *g_pmu;
#define PR(o) g_pmu[(o)/4]
#define PW(o,v) (g_pmu[(o)/4] = (v))

int main(void)
{
    int fd, i;
    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) err(1, "open /dev/mem");
    g_pmu = mmap(NULL, 0x200, PROT_READ|PROT_WRITE, MAP_SHARED, fd, PMU_PA);
    if (g_pmu == MAP_FAILED) err(1, "mmap PMU");

    printf("=== RK3399 PMU Power Domain Check ===\n\n");
    printf("PMU_PWRDN_CON  [0x008C]: 0x%08x  (1=powered down)\n", PR(0x008C));
    printf("PMU_PWRDN_ST   [0x0098]: 0x%08x  (1=idle/powered down)\n", PR(0x0098));
    printf("PMU_PWRGATE_ST [0x009C]: 0x%08x\n", PR(0x009C));
    printf("\nAll PMU registers 0x0080-0x00C0:\n");
    for (i = 0x0080; i < 0x00C4; i += 4)
        printf("  [0x%04x]: 0x%08x\n", i, PR(i));

    uint32_t st = PR(0x0098);
    printf("\nPD_VIO status (bit3):  %d  (%s)\n", (st>>3)&1, ((st>>3)&1) ? "POWERED DOWN" : "POWERED UP");
    printf("PD_VOPB status(bit4):  %d  (%s)\n", (st>>4)&1, ((st>>4)&1) ? "POWERED DOWN" : "POWERED UP");
    printf("PD_VOPL status(bit5):  %d  (%s)\n", (st>>5)&1, ((st>>5)&1) ? "POWERED DOWN" : "POWERED UP");

    /* If VIO/VOPB is powered down, try to power it up */
    uint32_t con = PR(0x008C);
    if ((con >> 3) & 1) {
        printf("\nPD_VIO is powered down! Attempting power-up (bit3=0)...\n");
        PW(0x008C, con & ~(1u<<3));
        usleep(5000);
        printf("PMU_PWRDN_CON after: 0x%08x\n", PR(0x008C));
        printf("PMU_PWRDN_ST  after: 0x%08x\n", PR(0x0098));
    } else if ((con >> 4) & 1) {
        printf("\nPD_VOPB is powered down! Attempting power-up (bit4=0)...\n");
        PW(0x008C, con & ~(1u<<4));
        usleep(5000);
        printf("PMU_PWRDN_CON after: 0x%08x\n", PR(0x008C));
        printf("PMU_PWRDN_ST  after: 0x%08x\n", PR(0x0098));
    } else {
        printf("\nAll VIO/VOP domains appear powered up.\n");
    }

    close(fd);
    return 0;
}
