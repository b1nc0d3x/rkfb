/*
 * hdmi_pinmux_fix2.c - Configure correct HDMI HPD pinmux on RK3399
 *
 * HDMI HPD is on GPIO4_C7, NOT GPIO2_B7.
 * GPIO2_B7 is HDMI_CEC (consumer electronics control).
 *
 * RK3399 pin assignments:
 *   GPIO4_C7 = HDMI_HPD  (GRF_GPIO4C_IOMUX bits[15:14], function 1)
 *   GPIO4_C5 = HDMI_SCL  (GRF_GPIO4C_IOMUX bits[11:10], function 1)
 *   GPIO4_C6 = HDMI_SDA  (GRF_GPIO4C_IOMUX bits[13:12], function 1)
 *   GPIO2_B7 = HDMI_CEC  (GRF_GPIO2B_IOMUX bits[15:14], function 1)
 *
 * GPIO4_C7 = GPIO4 bank C pin 7 = bit 23 of GPIO4_DR
 * GPIO4 base = 0xff790000
 * GRF_GPIO4C_IOMUX = GRF(0xff320000) + 0x010c
 *
 * Build: cc -o hdmi_pinmux_fix2 hdmi_pinmux_fix2.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    volatile uint32_t *grf  = mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0xff320000);
    volatile uint32_t *gpio4= mmap(NULL,0x100, PROT_READ,            MAP_SHARED,fd,0xff790000);
    volatile uint32_t *gpio2= mmap(NULL,0x100, PROT_READ,            MAP_SHARED,fd,0xff780000);

    printf("=== HDMI HPD Pinmux Fix ===\n\n");

    printf("Before:\n");
    printf("  GRF_GPIO4C_IOMUX [0x010c] = 0x%08x\n", grf[0x010c/4]);
    printf("  GRF_GPIO2B_IOMUX [0x00e8] = 0x%08x\n", grf[0x00e8/4]);
    printf("  GPIO4_DR  bit23 (GPIO4_C7/HPD) = %d\n", (gpio4[0]>>23)&1);
    printf("  GPIO4_DR  bit21 (GPIO4_C5/SCL) = %d\n", (gpio4[0]>>21)&1);
    printf("  GPIO4_DR  bit22 (GPIO4_C6/SDA) = %d\n", (gpio4[0]>>22)&1);
    printf("  GPIO2_DR  bit15 (GPIO2_B7/CEC) = %d\n", (gpio2[0]>>15)&1);
    printf("  GPIO4_DR  full  = 0x%08x\n\n", gpio4[0]);

    /*
     * GRF_GPIO4C_IOMUX [0x010c]:
     *   bits[15:14] = GPIO4_C7: 00=gpio, 01=hdmi_hpd
     *   bits[13:12] = GPIO4_C6: 00=gpio, 01=hdmi_sda
     *   bits[11:10] = GPIO4_C5: 00=gpio, 01=hdmi_scl
     *
     * Set all three: HPD + I2C (needed for EDID reads later)
     * mask = bits[15:10] = 0xfc00, val = 0b01_01_01_xx_xx_xx = 0x5400
     */
    printf("Setting GPIO4_C7=HDMI_HPD, GPIO4_C6=HDMI_SDA, GPIO4_C5=HDMI_SCL...\n");
    grf[0x010c/4] = (0xfc00u << 16) | 0x5400u;

    usleep(1000);
    printf("\nAfter:\n");
    printf("  GRF_GPIO4C_IOMUX [0x010c] = 0x%08x\n", grf[0x010c/4]);
    printf("  GPIO4_C7 mux bits[15:14] = %d  %s\n",
        (grf[0x010c/4]>>14)&3,
        ((grf[0x010c/4]>>14)&3)==1 ? "(HDMI_HPD - correct)" : "(wrong!)");

    printf("\nPolling GPIO4_C7 (HPD) for 10 seconds -- plug/unplug cable now...\n");
    printf("  Also watching GRF_SOC_STATUS1 bit14 (HDMI HPD status)\n\n");

    uint32_t last_gpio4 = 0xffffffff;
    uint32_t last_grf   = 0xffffffff;
    for (int i = 0; i < 100; i++) {
        uint32_t g4  = gpio4[0];
        uint32_t gst = grf[0x04e4/4];
        int hpd_gpio = (g4 >> 23) & 1;
        int hpd_grf  = (gst >> 14) & 1;

        if (g4 != last_gpio4 || gst != last_grf || i == 0) {
            printf("  [%3ds] GPIO4_DR=0x%08x HPD_gpio4_C7=%d | GRF_STATUS1=0x%08x HPD_bit14=%d",
                i/10, g4, hpd_gpio, gst, hpd_grf);
            if (hpd_gpio) printf("  << HPD HIGH - monitor connected!");
            printf("\n");
            last_gpio4 = g4;
            last_grf   = gst;
        }
        usleep(100000);
    }

    printf("\nFinal state:\n");
    printf("  GPIO4_C7 (HPD) = %d\n", (gpio4[0]>>23)&1);
    printf("  GRF_STATUS1    = 0x%08x\n", grf[0x04e4/4]);


    close(fd);
    return 0;
}
