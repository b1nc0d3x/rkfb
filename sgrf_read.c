#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint32_t *sgrf = mmap(NULL, 0x10000, PROT_READ,
        MAP_SHARED, fd, 0xff330000);

    printf("SGRF_SOC_CON(5) [0xe014] = 0x%08x\n", sgrf[0xe014/4]);
    printf("SGRF_SOC_CON(6) [0xe018] = 0x%08x\n", sgrf[0xe018/4]);
    printf("SGRF_SOC_CON(7) [0xe01c] = 0x%08x\n", sgrf[0xe01c/4]);
    printf("SGRF_SLV_CON0   [0xe3c0] = 0x%08x\n", sgrf[0xe3c0/4]);
    printf("SGRF_SLV_CON1   [0xe3c4] = 0x%08x\n", sgrf[0xe3c4/4]);
    printf("SGRF_SLV_CON2   [0xe3c8] = 0x%08x\n", sgrf[0xe3c8/4]);
    printf("SGRF_SLV_CON3   [0xe3cc] = 0x%08x\n", sgrf[0xe3cc/4]);
    printf("SGRF_SLV_CON4   [0xe3d0] = 0x%08x\n", sgrf[0xe3d0/4]);
    return 0;
}
