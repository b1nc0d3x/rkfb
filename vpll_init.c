#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint32_t *cru = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff760000);

    printf("VPLL before:\n");
    printf("  CON0=0x%08x CON1=0x%08x CON2=0x%08x CON3=0x%08x\n",
        cru[0x20/4], cru[0x24/4], cru[0x28/4], cru[0x2c/4]);

    /* Power down VPLL first */
    cru[0x2c/4] = (1<<20) | (1<<3);  /* HIWORD: mask bit3, set bit3=1 (pwrdn) */
    usleep(1000);

    /* CON0: FBDIV=99=0x63, bits[11:0] */
    cru[0x20/4] = (0xfff << 16) | 0x0063;

    /* CON1: POSTDIV2=8(bits[14:12]), POSTDIV1=4(bits[10:8]), REFDIV=1(bits[5:0]) */
    cru[0x24/4] = (0xffff << 16) | (8 << 12) | (4 << 8) | 1;

    /* CON2: FRACDIV=0 */
    cru[0x28/4] = 0x00000000;

    /* CON3: power up, clear reset */
    cru[0x2c/4] = (0xff << 16) | 0x00;  /* clear pwrdn, reset bits */
    usleep(1000);

    /* Wait for PLL lock — CON2 bit 31 = lock */
    int i;
    for (i = 0; i < 100; i++) {
        usleep(1000);
        if (cru[0x28/4] & (1u << 31)) {
            printf("VPLL locked after %d ms\n", i+1);
            break;
        }
    }
    if (i == 100) printf("VPLL lock TIMEOUT\n");

    printf("VPLL after:\n");
    printf("  CON0=0x%08x CON1=0x%08x CON2=0x%08x CON3=0x%08x\n",
        cru[0x20/4], cru[0x24/4], cru[0x28/4], cru[0x2c/4]);

    return 0;
}
