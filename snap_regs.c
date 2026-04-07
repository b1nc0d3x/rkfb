#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
int main(void){
    int fd=open("/dev/mem",0);
    volatile uint8_t  *hdmi  =mmap(NULL,0x20000,1,1,fd,0xff940000);
    volatile uint32_t *cru   =mmap(NULL,0x1000, 1,1,fd,0xff760000);
    volatile uint32_t *viogrf=mmap(NULL,0x1000, 1,1,fd,0xff770000);
    volatile uint32_t *grf   =mmap(NULL,0x1000, 1,1,fd,0xff320000);
    volatile uint32_t *vop   =mmap(NULL,0x2000, 1,1,fd,0xff900000);
    volatile uint32_t *gpio2 =mmap(NULL,0x100,  1,1,fd,0xff780000);
    volatile uint32_t *gpio4 =mmap(NULL,0x100,  1,1,fd,0xff790000);

    printf("=== WORKING STATE SNAPSHOT ===\n\n");

    printf("-- HDMI --\n");
    printf("MC_CLKDIS    [0x4001] = 0x%02x\n", hdmi[0x4001]);
    printf("MC_SWRSTZREQ [0x4002] = 0x%02x\n", hdmi[0x4002]);
    printf("MC_PHYRSTZ   [0x4005] = 0x%02x\n", hdmi[0x4005]);
    printf("MC_LOCKONCLOCK[0x4006]= 0x%02x\n", hdmi[0x4006]);
    printf("PHY_CONF0    [0x3000] = 0x%02x\n", hdmi[0x3000]);
    printf("PHY_STAT0    [0x3004] = 0x%02x\n", hdmi[0x3004]);
    printf("IH_PHY_STAT0 [0x0104] = 0x%02x\n", hdmi[0x0104]);
    printf("FC_INVIDCONF [0x1000] = 0x%02x\n", hdmi[0x1000]);
    printf("VP_CONF      [0x0804] = 0x%02x\n", hdmi[0x0804]);

    printf("\n-- CRU --\n");
    printf("VPLL CON0-3: %08x %08x %08x %08x\n",
        cru[0x20/4],cru[0x24/4],cru[0x28/4],cru[0x2c/4]);
    printf("GPLL CON0-3: %08x %08x %08x %08x\n",
        cru[0x80/4],cru[0x84/4],cru[0x88/4],cru[0x8c/4]);
    printf("CLKSEL49    [0x00c4] = 0x%08x\n", cru[0x00c4/4]);
    printf("CLKGATE16   [0x0240] = 0x%08x\n", cru[0x0240/4]);
    printf("CLKGATE20   [0x0250] = 0x%08x\n", cru[0x0250/4]);
    printf("SOFTRST_CON5[0x0414] = 0x%08x\n", cru[0x0414/4]);

    printf("\n-- VIO GRF --\n");
    printf("SOC_CON20   [0x0250] = 0x%08x\n", viogrf[0x0250/4]);

    printf("\n-- GRF --\n");
    printf("GPIO4C_IOMUX[0x010c] = 0x%08x\n", grf[0x010c/4]);
    printf("SOC_STATUS1 [0x0484] = 0x%08x  HPD=%d\n",
        grf[0x0484/4], (grf[0x0484/4]>>14)&1);

    printf("\n-- VOP --\n");
    printf("SYS_CTRL    [0x0008] = 0x%08x\n", vop[0x0008/4]);
    printf("WIN0_CTRL0  [0x0030] = 0x%08x\n", vop[0x0030/4]);
    printf("HTOTAL_HS   [0x0188] = 0x%08x\n", vop[0x0188/4]);
    printf("VTOTAL_VS   [0x0190] = 0x%08x\n", vop[0x0190/4]);
    printf("HACT_ST_END [0x018c] = 0x%08x\n", vop[0x018c/4]);
    printf("VACT_ST_END [0x0194] = 0x%08x\n", vop[0x0194/4]);

    printf("\n-- GPIO --\n");
    printf("GPIO2_DR/DDR= 0x%08x / 0x%08x  A5=%d\n",
        gpio2[0],gpio2[1],(gpio2[0]>>5)&1);
    printf("GPIO4_DR/DDR= 0x%08x / 0x%08x  C7_HPD=%d\n",
        gpio4[0],gpio4[1],(gpio4[0]>>23)&1);

    printf("\n-- RK808 LDO_EN --\n");
    /* Can't read I2C from here, but note GPIO2_A5 state */
    printf("GPIO2_A5 (avdd_en) = %d\n", (gpio2[0]>>5)&1);
    close(fd);
    return 0;
}
