#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);

    hdmi[0x4001] = 0x00;   /* MC_CLKDIS: all clocks on */
    hdmi[0x4002] = 0xff;   /* MC_SWRSTZREQ: release soft reset */
    usleep(5000);
    hdmi[0x4005] = 0x01;   /* MC_PHYRSTZ: release PHY reset */
    usleep(5000);

    printf("MC_PHYRSTZ [0x4005] = 0x%02x\n", hdmi[0x4005]);
    printf("PHY_CONF0  [0x3000] = 0x%02x\n", hdmi[0x3000]);

    hdmi[0x3000] = 0xc2;
    usleep(5000);
    printf("PHY_CONF0 after: 0x%02x\n", hdmi[0x3000]);
    printf("PHY_STAT0:       0x%02x\n", hdmi[0x3004]);

    hdmi[0x3029] = 0x17;
    hdmi[0x302b] = 0x00;
    hdmi[0x302c] = 0x18;
    hdmi[0x302d] = 0x00;
    hdmi[0x302e] = 0x18;
    hdmi[0x3020] = 0x69;
    hdmi[0x3021] = 0x06;
    hdmi[0x3022] = 0x00;
    hdmi[0x3023] = 0x08;

    printf("Triggering I2C write...\n");
    hdmi[0x3026] = 0x10;

    int i;
    for (i = 0; i < 30; i++) {
        usleep(2000);
        uint8_t st = hdmi[0x3027];
        if (st) printf("  [%d] INT=0x%02x\n", i, st);
        if (st & 0x02) { printf("  DONE!\n"); hdmi[0x3027]=0x02; break; }
        if (st & 0x08) { printf("  ERROR!\n"); hdmi[0x3027]=0x08; break; }
    }
    printf("Final PHY_STAT0: 0x%02x\n", hdmi[0x3004]);
	close(fd); 
   return 0;
}
