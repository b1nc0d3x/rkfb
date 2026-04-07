/*
 * hdmi_stride4_probe.c - Read HDMI registers with correct 4-byte stride
 *
 * DW-HDMI on RK3399 uses 4-byte register stride:
 * physical_offset = logical_register_address * 4
 *
 * Build: cc -o hdmi_stride4_probe hdmi_stride4_probe.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define HDMI_PA 0xff940000UL

/* Read logical HDMI register (stride=4) */
#define HR(base, logical) ((base)[(logical)*4])
#define HW(base, logical, val) ((base)[(logical)*4] = (val))

int main(void)
{
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint8_t *h = mmap(NULL,0x20000,PROT_READ,MAP_SHARED,fd,HDMI_PA);

    printf("=== HDMI registers with 4-byte stride ===\n\n");

    printf("Identification:\n");
    printf("  design_id  [log 0x0000] = 0x%02x  (phys 0x%05x)\n",
        HR(h,0x0000), 0x0000*4);
    printf("  revision   [log 0x0004] = 0x%02x\n", HR(h,0x0004));

    printf("\nMain Controller:\n");
    printf("  MC_SFRDIV    [log 0x4000] = 0x%02x  (phys 0x%05x)\n",
        HR(h,0x4000), 0x4000*4);
    printf("  MC_CLKDIS    [log 0x4001] = 0x%02x\n", HR(h,0x4001));
    printf("  MC_SWRSTZREQ [log 0x4002] = 0x%02x\n", HR(h,0x4002));
    printf("  MC_PHYRSTZ   [log 0x4005] = 0x%02x\n", HR(h,0x4005));
    printf("  MC_LOCKONCLOCK[log 0x4006]= 0x%02x\n", HR(h,0x4006));

    printf("\nPHY Interface:\n");
    printf("  PHY_CONF0    [log 0x3000] = 0x%02x  (phys 0x%05x)\n",
        HR(h,0x3000), 0x3000*4);
    printf("  PHY_STAT0    [log 0x3004] = 0x%02x\n", HR(h,0x3004));
    printf("  PHY_I2CM_DIV [log 0x3029] = 0x%02x\n", HR(h,0x3029));

    printf("\nFrame Composer:\n");
    printf("  FC_INVIDCONF [log 0x1000] = 0x%02x\n", HR(h,0x1000));
    uint16_t hact = ((uint16_t)HR(h,0x1002)<<8)|HR(h,0x1001);
    uint16_t vact = ((uint16_t)HR(h,0x1006)<<8)|HR(h,0x1005);
    printf("  FC_INHACTV   [log 0x1001/2] = %d (0x%03x)\n", hact, hact);
    printf("  FC_INVACTV   [log 0x1005/6] = %d (0x%03x)\n", vact, vact);
    printf("  FC_HSYNCINDELAY [log 0x1008] = %d\n", HR(h,0x1008));
    printf("  FC_HSYNCINWIDTH [log 0x100a] = %d\n", HR(h,0x100a));
    printf("  FC_VSYNCINDELAY [log 0x100c] = %d\n", HR(h,0x100c));
    printf("  FC_VSYNCINWIDTH [log 0x100d] = %d\n", HR(h,0x100d));

    printf("\nVideo Packetizer:\n");
    printf("  VP_STATUS    [log 0x0800] = 0x%02x\n", HR(h,0x0800));
    printf("  VP_PR_CD     [log 0x0801] = 0x%02x\n", HR(h,0x0801));
    printf("  VP_CONF      [log 0x0804] = 0x%02x\n", HR(h,0x0804));

    printf("\nInterrupts:\n");
    printf("  IH_PHY_STAT0 [log 0x0104] = 0x%02x\n", HR(h,0x0104));
    printf("  IH_MUTE      [log 0x01ff] = 0x%02x\n", HR(h,0x01ff));

    printf("\nPHY_STAT0 bit interpretation:\n");
    uint8_t phy_stat = HR(h,0x3004);
    printf("  TX_PHY_LOCK (bit4) = %d\n", (phy_stat>>4)&1);
    printf("  HPD         (bit1) = %d\n", (phy_stat>>1)&1);

    close(fd);
    return 0;
}
