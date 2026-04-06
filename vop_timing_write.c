/*
 * vop_timing_write.c - Test write to VOP timing registers via rkfb ioctl
 * Bypasses the whitelist issue by also trying direct /dev/mem write.
 * Build: cc -o vop_timing_write vop_timing_write.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "rkfb_ioctl.h"

#define VOP_PA 0xff900000UL

int main(void)
{
    int fd, memfd;
    struct rkfb_regop ro;

    /* Test via ioctl */
    fd = open("/dev/rkfb0", O_RDWR);
    if (fd < 0) { perror("open rkfb0"); return 1; }

    printf("=== VOP timing register write test ===\n\n");

    /* Read current value */
    ro.block = RKFB_BLOCK_VOP; ro.off = 0x0188; ro.val = 0;
    if (ioctl(fd, RKFB_REG_READ, &ro) < 0)
        printf("READ  0x0188 via ioctl: FAILED (%s)\n", strerror(errno));
    else
        printf("READ  0x0188 via ioctl: 0x%08x\n", ro.val);

    /* Try write via ioctl */
    ro.block = RKFB_BLOCK_VOP; ro.off = 0x0188; ro.val = 0x06710027;
    if (ioctl(fd, RKFB_REG_WRITE, &ro) < 0)
        printf("WRITE 0x0188 via ioctl: FAILED (%s) <-- whitelist blocking\n", strerror(errno));
    else
        printf("WRITE 0x0188 via ioctl: OK\n");

    /* Read back */
    ro.block = RKFB_BLOCK_VOP; ro.off = 0x0188; ro.val = 0;
    ioctl(fd, RKFB_REG_READ, &ro);
    printf("READ  0x0188 after write: 0x%08x\n\n", ro.val);

    /* Also write via direct /dev/mem as fallback */
    printf("Trying direct /dev/mem write to VOP 0x0188...\n");
    memfd = open("/dev/mem", O_RDWR);
    if (memfd >= 0) {
        volatile uint32_t *vop = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
            MAP_SHARED, memfd, VOP_PA);
        if (vop != MAP_FAILED) {
            printf("  0x0188 before: 0x%08x\n", vop[0x0188/4]);
            vop[0x0188/4] = 0x06710027;
            vop[0x018c/4] = 0x01040604;
            vop[0x0190/4] = 0x02ed0004;
            vop[0x0194/4] = 0x001902e9;
            /* Also set SYS_CTRL dclk_en and clear standby */
            uint32_t sc = vop[0x0008/4];
            sc &= ~(1u<<11); sc |= (1u<<1);
            vop[0x0008/4] = sc;
            /* WIN0 */
            vop[0x003c/4] = 0x05000500;
            vop[0x0030/4] = 0x00000001;
            /* REG_CFG_DONE */
            vop[0x0000/4] = 0x01;
            usleep(40000);
            printf("  0x0188 after:  0x%08x  (expect 0x06710027)\n", vop[0x0188/4]);
            printf("  0x0190 after:  0x%08x  (expect 0x02ed0004)\n", vop[0x0190/4]);
            printf("  WIN0_CTRL0:    0x%08x\n", vop[0x0030/4]);
            printf("  SYS_CTRL:      0x%08x\n", vop[0x0008/4]);
        } else { perror("  mmap VOP"); }
        close(memfd);
    }

    close(fd);
    return 0;
}
