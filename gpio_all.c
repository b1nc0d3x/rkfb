/*
 * gpio_all.c - Dump all GPIO banks: direction and output state
 * Shows every pin configured as output, which one might be avdd enable
 * Build: cc -o gpio_all gpio_all.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    uint32_t bases[] = {0xff720000,0xff730000,0xff780000,0xff788000,0xff790000};
    const char *names[] = {"GPIO0","GPIO1","GPIO2","GPIO3","GPIO4"};
    for (int i = 0; i < 5; i++) {
        volatile uint32_t *g = mmap(NULL,0x100,PROT_READ,MAP_SHARED,fd,bases[i]);
        uint32_t dr = g[0], ddr = g[1];
        printf("%s: DR=0x%08x DDR=0x%08x\n", names[i], dr, ddr);
        /* Print pins configured as outputs */
        for (int b = 0; b < 32; b++) {
            if ((ddr>>b)&1)
                printf("  bit%2d (%s_%c%d) = OUTPUT, value=%d\n",
                    b, names[i],
                    'A'+b/8, b%8,
                    (dr>>b)&1);
        }
    }

    close(fd);
    return 0;
}
