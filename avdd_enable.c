/*
 * avdd_enable.c - Enable HDMI PHY analog power rail (avdd)
 *
 * On RockPro64, avdd_1v8_hdmi is a fixed regulator enabled by GPIO2_A5.
 * FreeBSD's regulator framework shut it down at boot because the HDMI
 * node had no driver attached. This restores it.
 *
 * GPIO2_A5 = bit 5 of GPIO2_DR (0xff780000 + 0x00)
 * GPIO2_DDR = bit 5 must be 1 (output) then GPIO2_DR bit5 = 1 (high)
 *
 * Build: cc -o avdd_enable avdd_enable.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    volatile uint32_t *gpio2 = mmap(NULL,0x100,PROT_READ|PROT_WRITE,
        MAP_SHARED, fd, 0xff780000);

    printf("=== avdd_1v8_hdmi Enable ===\n\n");
    printf("Before:\n");
    printf("  GPIO2_DR  [0x00] = 0x%08x  (bit5=%d)\n", gpio2[0], (gpio2[0]>>5)&1);
    printf("  GPIO2_DDR [0x04] = 0x%08x  (bit5=%d, 0=input 1=output)\n",
        gpio2[1], (gpio2[1]>>5)&1);

    /* Set GPIO2_A5 as output */
    gpio2[1] |= (1u << 5);   /* DDR bit5 = 1 (output) */
    /* Drive it high (enable regulator) */
    gpio2[0] |= (1u << 5);   /* DR bit5 = 1 */
    usleep(10000);   /* 10ms for rail to come up */

    printf("\nAfter:\n");
    printf("  GPIO2_DR  [0x00] = 0x%08x  (bit5=%d, want 1)\n",
        gpio2[0], (gpio2[0]>>5)&1);
    printf("  GPIO2_DDR [0x04] = 0x%08x  (bit5=%d, want 1)\n",
        gpio2[1], (gpio2[1]>>5)&1);

    if ((gpio2[0]>>5)&1)
        printf("\navdd_1v8_hdmi: ENABLED (GPIO2_A5 high)\n");
    else
        printf("\nWARNING: GPIO2_A5 still low -- check GPIO2_A5 pin assignment\n");

    printf("\nWait 50ms for rail to stabilize...\n");
    usleep(50000);
    printf("Ready. Run ./phy_i2c_readback to verify MPLL config latches.\n");

    close(fd);
    return 0;
}
