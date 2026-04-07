/*
 * phy_mmio_probe.c - Probe Innosilicon PHY MMIO at 0xff9e0000
 *
 * The RK3399 Innosilicon HDMI PHY registers are at a SEPARATE
 * MMIO region from the DW-HDMI controller.
 *
 * From rk3399.dtsi:
 *   hdmi@ff940000 { reg = <0xff940000 0x20000>,  <- DW-HDMI
 *                          <0xff9e0000 0x20000>; } <- PHY MMIO
 *
 * The Linux phy-rockchip-inno-hdmi.c driver uses direct byte
 * MMIO writes to 0xff9e0000+offset, NOT the DW-HDMI I2C master.
 *
 * Build: cc -o phy_mmio_probe phy_mmio_probe.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define PHY_PA  0xff9e0000UL
#define HDMI_PA 0xff940000UL

int main(void)
{
    int fd = open("/dev/mem", O_RDWR);
    volatile uint8_t *phy  = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,
        MAP_SHARED,fd,PHY_PA);
    volatile uint8_t *hdmi = mmap(NULL,0x20000,PROT_READ|PROT_WRITE,
        MAP_SHARED,fd,HDMI_PA);

    if (phy  == MAP_FAILED) { perror("mmap PHY");  return 1; }
    if (hdmi == MAP_FAILED) { perror("mmap HDMI"); return 1; }

    printf("=== Innosilicon PHY MMIO Probe (0xff9e0000) ===\n\n");

    /* Dump first 0x40 bytes of PHY MMIO */
    printf("PHY MMIO [0xff9e0000..0xff9e003f]:\n");
    for (int i = 0; i < 0x40; i++) {
        if (i % 16 == 0) printf("  [0x%02x]", i);
        printf(" %02x", phy[i]);
        if (i % 16 == 15) printf("\n");
    }
    printf("\n");

    /* Try writing known value and reading back */
    printf("Write/readback test on PHY[0x06]:\n");
    printf("  Before: PHY[0x06] = 0x%02x\n", phy[0x06]);
    phy[0x06] = 0xAA;
    usleep(100);
    printf("  After write 0xAA: PHY[0x06] = 0x%02x\n", phy[0x06]);
    phy[0x06] = 0x55;
    usleep(100);
    printf("  After write 0x55: PHY[0x06] = 0x%02x\n", phy[0x06]);
    phy[0x06] = 0x00;   /* restore */

    printf("\nDW-HDMI PHY_CONF0 [0x3000] = 0x%02x\n", hdmi[0x3000]);
    printf("DW-HDMI PHY_STAT0 [0x3004] = 0x%02x\n", hdmi[0x3004]);

    /* Release HDMI soft reset so PHY MMIO is accessible */
    printf("\nReleasing HDMI soft reset...\n");
    hdmi[0x4001] = 0x00;
    hdmi[0x4002] = 0xff;
    usleep(5000);

    printf("PHY MMIO after reset release [0xff9e0000..0x0f]:\n  ");
    for (int i = 0; i < 0x10; i++) printf("%02x ", phy[i]);
    printf("\n");

    /* Write/readback again with reset released */
    printf("\nWrite/readback after reset release:\n");
    printf("  PHY[0x06] before: 0x%02x\n", phy[0x06]);
    phy[0x06] = 0x08;   /* MPLL_CNTRL value for 74.25 MHz */
    usleep(100);
    printf("  PHY[0x06] after write 0x08: 0x%02x  %s\n",
        phy[0x06], phy[0x06]==0x08 ? "LATCHED" : "did not latch");

    phy[0x25] = 0x72;   /* VLEVCTRL low byte */
    usleep(100);
    printf("  PHY[0x25] after write 0x72: 0x%02x  %s\n",
        phy[0x25], phy[0x25]==0x72 ? "LATCHED" : "did not latch");

    close(fd);
    return 0;
}
