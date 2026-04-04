/*
 * vop_test.c - RK3399 VOP register accessibility test
 * Tests whether TZPC is blocking VOP MMIO access
 *
 * VOP big:   0xff900000
 * VOP little: 0xff8f0000
 *
 * VOP_VERSION register is at offset 0x0000 on both VOPs
 * Expected value on RK3399: 0x20061200 (big) / 0x20071100 (little)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define VOP_BIG_BASE    0xff900000
#define VOP_LIT_BASE    0xff8f0000
#define VOP_MAP_SIZE    0x2000

#define VOP_VERSION     0x0000
#define VOP_SYS_CTRL    0x0008
#define VOP_DSP_CTRL0   0x0010

static int test_vop(int fd, const char *name, off_t base)
{
    void *map;
    volatile uint32_t *regs;
    uint32_t version, sys_ctrl, dsp_ctrl0;

    printf("\n--- %s @ 0x%08lx ---\n", name, base);

    map = mmap(NULL, VOP_MAP_SIZE, PROT_READ, MAP_SHARED, fd, base);
    if (map == MAP_FAILED) {
        printf("  mmap FAILED: %s\n", strerror(errno));
        printf("  >> TZPC likely still blocking access\n");
        return -1;
    }

    regs = (volatile uint32_t *)map;

    version  = regs[VOP_VERSION  / 4];
    sys_ctrl = regs[VOP_SYS_CTRL / 4];
    dsp_ctrl0= regs[VOP_DSP_CTRL0/ 4];

    printf("  VOP_VERSION  (0x%04x): 0x%08x\n", VOP_VERSION,  version);
    printf("  VOP_SYS_CTRL (0x%04x): 0x%08x\n", VOP_SYS_CTRL, sys_ctrl);
    printf("  VOP_DSP_CTRL0(0x%04x): 0x%08x\n", VOP_DSP_CTRL0,dsp_ctrl0);

    if (version == 0xffffffff) {
        printf("  >> Read returned 0xffffffff - bus error / TZPC blocking\n");
        munmap(map, VOP_MAP_SIZE);
        return -1;
    } else if (version == 0x00000000) {
        printf("  >> Read returned 0x00000000 - clocks may be gated\n");
    } else {
        printf("  >> Register access OK - TZPC not blocking\n");
    }

    munmap(map, VOP_MAP_SIZE);
    return 0;
}

int main(void)
{
    int fd;
    int ret = 0;

    printf("RK3399 VOP MMIO access test\n");
    printf("============================\n");

    fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open(/dev/mem) failed: %s\n", strerror(errno));
        fprintf(stderr, "Run as root.\n");
        return 1;
    }

    ret |= test_vop(fd, "VOP big  (rk3399-vop-big)", VOP_BIG_BASE);
    ret |= test_vop(fd, "VOP little (rk3399-vop-lit)", VOP_LIT_BASE);

    close(fd);

    printf("\n============================\n");
    if (ret == 0)
        printf("Result: VOP registers accessible - TZPC cleared OK\n");
    else
        printf("Result: One or more VOPs inaccessible - check TZPC/clocks\n");

    return ret;
}
