/*
 * deep_snapshot.c - Complete HDMI+VOP+PHY register dump while display is working
 * Run this BEFORE touching anything else.
 * Build: cc -o deep_snapshot deep_snapshot.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
    int fd = open("/dev/mem", O_RDONLY);
    volatile uint8_t  *hdmi  = mmap(NULL,0x20000,PROT_READ,MAP_SHARED,fd,0xff940000);
    volatile uint32_t *cru   = mmap(NULL,0x1000, PROT_READ,MAP_SHARED,fd,0xff760000);
    volatile uint32_t *viogrf= mmap(NULL,0x1000, PROT_READ,MAP_SHARED,fd,0xff770000);
    volatile uint32_t *grf   = mmap(NULL,0x1000, PROT_READ,MAP_SHARED,fd,0xff320000);
    volatile uint32_t *vop   = mmap(NULL,0x2000, PROT_READ,MAP_SHARED,fd,0xff900000);
    volatile uint32_t *gpio2 = mmap(NULL,0x100,  PROT_READ,MAP_SHARED,fd,0xff780000);
    volatile uint32_t *gpio4 = mmap(NULL,0x100,  PROT_READ,MAP_SHARED,fd,0xff790000);
    volatile uint32_t *pmu   = mmap(NULL,0x1000, PROT_READ,MAP_SHARED,fd,0xff310000);

    printf("=== DEEP SNAPSHOT (display working) ===\n\n");

    printf("-- HDMI ALL KEY REGS --\n");
    /* Identification */
    printf("design_id=%02x rev=%02x prod=%02x%02x cfg=%02x%02x%02x\n",
        hdmi[(0)*4],hdmi[(4)*4],hdmi[(8)*4],hdmi[(0xc)*4],hdmi[(0x10)*4],hdmi[(0x14)*4],hdmi[(0x18)*4]);
    /* Main controller */
    printf("MC: CLKDIS=%02x SWRSTZ=%02x OPCTRL=%02x FLOWCTRL=%02x PHYRSTZ=%02x LOCKONCLOCK=%02x\n",
        hdmi[(0x4001)*4],hdmi[(0x4002)*4],hdmi[(0x4003)*4],hdmi[(0x4004)*4],hdmi[(0x4005)*4],hdmi[(0x4006)*4]);
    /* PHY */
    printf("PHY: CONF0=%02x STAT0=%02x TST0=%02x\n",
        hdmi[(0x3000)*4],hdmi[(0x3004)*4],hdmi[(0x3001)*4]);
    printf("PHY_I2CM: SLAVE=%02x ADDR=%02x OP=%02x INT=%02x DIV=%02x\n",
        hdmi[(0x3020)*4],hdmi[(0x3021)*4],hdmi[(0x3026)*4],hdmi[(0x3027)*4],hdmi[(0x3029)*4]);
    /* Frame composer */
    printf("FC: INVIDCONF=%02x HACT=%02x%02x VACT=%02x%02x\n",
        hdmi[(0x1000)*4],hdmi[(0x1002)*4],hdmi[(0x1001)*4],hdmi[(0x1006)*4],hdmi[(0x1005)*4]);
    printf("FC: HBLANK=%02x%02x VBLANK=%02x\n",
        hdmi[(0x1004)*4],hdmi[(0x1003)*4],hdmi[(0x1007)*4]);
    printf("FC: HSYNC_DLY=%02x%02x HSYNC_WID=%02x%02x\n",
        hdmi[(0x1009)*4],hdmi[(0x1008)*4],hdmi[(0x100b)*4],hdmi[(0x100a)*4]);
    printf("FC: VSYNC_DLY=%02x VSYNC_WID=%02x\n",hdmi[(0x100c)*4],hdmi[(0x100d)*4]);
    printf("FC: AVICONF=%02x%02x%02x VID=%02x\n",
        hdmi[(0x1017)*4],hdmi[(0x1018)*4],hdmi[(0x1019)*4],hdmi[(0x101b)*4]);
    /* Video packetizer */
    printf("VP: STATUS=%02x PR_CD=%02x CONF=%02x\n",
        hdmi[(0x0800)*4],hdmi[(0x0801)*4],hdmi[(0x0804)*4]);
    /* Interrupts */
    printf("IH: PHY=%02x MUTE_PHY=%02x MUTE=%02x\n",
        hdmi[(0x0104)*4],hdmi[(0x0184)*4],hdmi[(0x01ff)*4]);
    /* Dump 0x3000-0x302f (PHY interface) */
    printf("PHY_IF[0x3000-0x302f]:");
    for(int i=0;i<0x30;i++) printf(" %02x",hdmi[(0x3000+i)*4]);
    printf("\n");
    /* Dump 0x4000-0x400f (MC) */
    printf("MC[0x4000-0x400f]:");
    for(int i=0;i<0x10;i++) printf(" %02x",hdmi[(0x4000+i)*4]);
    printf("\n");

    printf("\n-- CRU --\n");
    printf("VPLL: %08x %08x %08x %08x\n",
        cru[0x20/4],cru[0x24/4],cru[0x28/4],cru[0x2c/4]);
    printf("GPLL: %08x %08x %08x %08x\n",
        cru[0x80/4],cru[0x84/4],cru[0x88/4],cru[0x8c/4]);
    printf("CLKSEL49[0x00c4]=%08x  CLKSEL50[0x00c8]=%08x\n",
        cru[0x00c4/4],cru[0x00c8/4]);
    for(int i=0;i<8;i++)
        printf("CLKGATE[%02d][0x%04x]=%08x\n",
            i+16,0x0240+i*4,cru[(0x0240+i*4)/4]);
    printf("SOFTRST_CON5[0x0414]=%08x\n",cru[0x0414/4]);

    printf("\n-- VIO GRF --\n");
    printf("SOC_CON20[0x0250]=%08x\n",viogrf[0x0250/4]);
    /* Dump all non-zero VIO GRF regs */
    for(int i=0;i<0x100;i+=4){
        uint32_t v=viogrf[i/4];
        if(v) printf("  VIO_GRF[0x%04x]=%08x\n",i,v);
    }

    printf("\n-- GRF --\n");
    printf("GPIO4C_IOMUX[0x010c]=%08x\n",grf[0x010c/4]);
    printf("GPIO4D_IOMUX[0x0110]=%08x\n",grf[0x0110/4]);
    printf("GPIO2B_IOMUX[0x00e8]=%08x\n",grf[0x00e8/4]);
    printf("SOC_STATUS1 [0x0484]=%08x HPD=%d\n",
        grf[0x0484/4],(grf[0x0484/4]>>14)&1);
    /* Dump all non-zero GRF SOC_CON regs */
    for(int i=0;i<0x80;i+=4){
        uint32_t v=grf[i/4];
        if(v) printf("  GRF_CON[0x%04x]=%08x\n",i,v);
    }

    printf("\n-- VOP --\n");
    /* Dump first 0x60 and timing area */
    for(int i=0;i<0x60;i+=4)
        printf("  VOP[0x%04x]=%08x\n",i,vop[i/4]);
    printf("  ...\n");
    for(int i=0x188;i<=0x198;i+=4)
        printf("  VOP[0x%04x]=%08x\n",i,vop[i/4]);

    printf("\n-- GPIO --\n");
    printf("GPIO2: DR=%08x DDR=%08x  A5=%d\n",
        gpio2[0],gpio2[1],(gpio2[0]>>5)&1);
    printf("GPIO4: DR=%08x DDR=%08x  C7=%d D2=%d D3=%d\n",
        gpio4[0],gpio4[1],
        (gpio4[0]>>23)&1,(gpio4[0]>>26)&1,(gpio4[0]>>27)&1);

    printf("\n-- PMU --\n");
    printf("PMU_PWRDN_ST[0x0098]=%08x  VIO=%d\n",
        pmu[0x0098/4],(pmu[0x0098/4]>>7)&1);

    close(fd);
    return 0;
}
