#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/mman.h>

static jmp_buf jb;
static void sigbus(int s) { longjmp(jb, 1); }

int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    signal(SIGBUS, sigbus);

    uint32_t addrs[] = {
        0xff940000,  /* DW-HDMI controller */
        0xff9e0000,  /* Innosilicon PHY (rk3399.dtsi claim) */
        0xff950000,  /* try nearby */
        0xff960000,
        0xff970000,
        0xff980000,
        0xff990000,
        0xffa00000,
    };

    for (int i = 0; i < 8; i++) {
        volatile uint8_t *p = mmap(NULL, 0x100, PROT_READ,
            MAP_SHARED, fd, addrs[i]);
        if (p == MAP_FAILED) {
            printf("0x%08x: mmap failed\n", addrs[i]);
            continue;
        }
        if (setjmp(jb)) {
            printf("0x%08x: BUS ERROR\n", addrs[i]);
        } else {
            printf("0x%08x: [0x00]=0x%02x [0x01]=0x%02x [0x02]=0x%02x [0x03]=0x%02x\n",
                addrs[i], p[0], p[1], p[2], p[3]);
        }
        munmap((void*)p, 0x100);
    }
    close(fd);
    return 0;
}
