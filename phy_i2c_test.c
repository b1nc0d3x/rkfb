#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

int main(void) {
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *hdmi = mmap(NULL, 0x20000, PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff940000);

    /* Release reset first */
    hdmi[0x4001] = 0x00;
    hdmi[0x4002] = 0xff;
    usleep(10000);

    printf("After reset release:\n");
    printf("  MC_SWRSTZREQ [0x4002] = 0x%02x\n", hdmi[0x4002]);

    /* Enable PHY transmitter */
    hdmi[0x3000] = 0xc2;
    usleep(5000);
    printf("  PHY_CONF0    [0x3000] = 0x%02x\n", hdmi[0x3000]);
    printf("  PHY_STAT0    [0x3004] = 0x%02x\n", hdmi[0x3004]);

    /* Configure I2C master clock */
    hdmi[0x3029] = 0x17;
    hdmi[0x302b] = 0x00;
    hdmi[0x302c] = 0x18;
    hdmi[0x302d] = 0x00;
    hdmi[0x302e] = 0x18;

    printf("  PHY_I2CM_DIV [0x3029] = 0x%02x\n", hdmi[0x3029]);

    /* Set slave address */
    hdmi[0x3020] = 0x69;
    printf("  PHY_I2CM_SLAVE [0x3020] = 0x%02x\n", hdmi[0x3020]);

    /* Write reg 0x06 = 0x0008 */
    hdmi[0x3021] = 0x06;   /* address */
    hdmi[0x3022] = 0x00;   /* MSB */
    hdmi[0x3023] = 0x08;   /* LSB */
    hdmi[0x3026] = 0x10;   /* trigger write */

    printf("  Triggered I2C write, polling INT...\n");
    int i;
    for (i = 0; i < 50; i++) {
        usleep(2000);
        uint8_t st = hdmi[0x3027];
        printf("  [%d] PHY_I2CM_INT [0x3027] = 0x%02x\n", i, st);
        if (st & 0x02) { printf("  DONE!\n"); hdmi[0x3027]=0x02; break; }
        if (st & 0x08) { printf("  ERROR!\n"); hdmi[0x3027]=0x08; break; }
    }
	close(fd);
    return 0;
}
