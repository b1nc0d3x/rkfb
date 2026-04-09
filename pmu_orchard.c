#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <err.h>

#define PMU_PA 0xFF310000UL
#define PMU_LOGIC_PDN 0x0014
#define PMU_LOGIC_ST  0x0018
#define PMU_IDLE_REQ  0x0060
#define PMU_IDLE_ST   0x0064
#define PMU_IDLE_ACK  0x0068

#define ORCHARD_FABRIC   (1u << 14)
#define ORCHARD_WEST     (1u << 17)
#define ORCHARD_EAST     (1u << 18)

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    volatile uint32_t *pmu;
    if (fd < 0) err(1, "open /dev/mem");
    pmu = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, PMU_PA);
    if (pmu == MAP_FAILED) err(1, "mmap");

    printf("BSD display orchard PMU view\n");
    printf("logic_pdn[0x14] = 0x%08x\n", pmu[PMU_LOGIC_PDN/4]);
    printf("logic_st [0x18] = 0x%08x\n", pmu[PMU_LOGIC_ST/4]);
    printf("idle_req [0x60] = 0x%08x\n", pmu[PMU_IDLE_REQ/4]);
    printf("idle_st  [0x64] = 0x%08x\n", pmu[PMU_IDLE_ST/4]);
    printf("idle_ack [0x68] = 0x%08x\n", pmu[PMU_IDLE_ACK/4]);
    printf("fabric(bit14): pdn=%d st=%d\n", (pmu[PMU_LOGIC_PDN/4] >> 14) & 1, (pmu[PMU_LOGIC_ST/4] >> 14) & 1);
    printf("west  (bit17): idle_req=%d idle_st=%d idle_ack=%d\n", (pmu[PMU_IDLE_REQ/4] >> 17) & 1, (pmu[PMU_IDLE_ST/4] >> 17) & 1, (pmu[PMU_IDLE_ACK/4] >> 17) & 1);
    printf("east  (bit18): idle_req=%d idle_st=%d idle_ack=%d\n", (pmu[PMU_IDLE_REQ/4] >> 18) & 1, (pmu[PMU_IDLE_ST/4] >> 18) & 1, (pmu[PMU_IDLE_ACK/4] >> 18) & 1);
    close(fd);
    return 0;
}
