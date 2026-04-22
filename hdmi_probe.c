/*
 * hdmi_probe.c
 *
 * Userspace probe tool for rkfb driver.
 * Reads HDMI, VOP, GRF, and CRU registers via /dev/rkfb0 ioctl.
 * Read-only probe: does not attempt PHY, VOP, or clock writes.
 *
 * Build: cc -o hdmi_probe hdmi_probe.c
 * Run:   sudo ./hdmi_probe
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "rkfb_ioctl.h"
#include <string.h>

static int g_fd;

/* -------------------------------------------------------------------------
 * Register read helpers
 * ---------------------------------------------------------------------- */

static uint32_t
hdmi_read(uint32_t off)
{
        struct rkfb_regop ro;
        ro.block = 3;
        ro.off   = off;
        ro.val   = 0;
        if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0) {
                perror("ioctl HDMI_REG_READ");
                return (0xdeadbeef);
        }
        return (ro.val);
}

static uint32_t
vop_read(uint32_t off)
{
        struct rkfb_regop ro;
        ro.block = 0;
        ro.off   = off;
        ro.val   = 0;
        if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0) {
                perror("ioctl VOP_REG_READ");
                return (0xdeadbeef);
        }
        return (ro.val);
}

static uint32_t
grf_read(uint32_t off)
{
        struct rkfb_regop ro;
        ro.block = 1;
        ro.off   = off;
        ro.val   = 0;
        if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0) {
                perror("ioctl GRF_REG_READ");
                return (0xdeadbeef);
        }
        return (ro.val);
}

static uint32_t
cru_read(uint32_t off)
{
        struct rkfb_regop ro;
        ro.block = 2;
        ro.off   = off;
        ro.val   = 0;
        if (ioctl(g_fd, RKFB_REG_READ, &ro) < 0) {
                perror("ioctl CRU_REG_READ");
                return (0xdeadbeef);
        }
        return (ro.val);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
        struct rkfb_info info;

        if (geteuid() != 0) {
                fprintf(stderr, "hdmi_probe must run as root to open /dev/rkfb0\n");
                return (1);
        }

        g_fd = open("/dev/rkfb0", O_RDONLY);
        if (g_fd < 0) {
                fprintf(stderr, "open /dev/rkfb0 failed: %s\n", strerror(errno));
                return (1);
        }

        /* --- HDMI identification --------------------------------------- */
        printf("--- HDMI ID ---\n");
        printf("design_id  [0x0000] = 0x%02x\n", hdmi_read(0x0000));
        printf("revision   [0x0001] = 0x%02x\n", hdmi_read(0x0001));
        printf("product0   [0x0002] = 0x%02x\n", hdmi_read(0x0002));
        printf("product1   [0x0003] = 0x%02x\n", hdmi_read(0x0003));
        printf("config0    [0x0004] = 0x%02x\n", hdmi_read(0x0004));
        printf("config1    [0x0005] = 0x%02x\n", hdmi_read(0x0005));
        printf("config2    [0x0006] = 0x%02x\n", hdmi_read(0x0006));

        /* --- HDMI interrupt status ------------------------------------- */
        printf("\n--- HDMI Interrupts ---\n");
        printf("IH_PHY_STAT0    [0x0104] = 0x%02x\n", hdmi_read(0x0104));
        printf("IH_I2CM_STAT0   [0x0105] = 0x%02x\n", hdmi_read(0x0105));
        printf("IH_I2CMPHY_STAT0[0x0108] = 0x%02x\n", hdmi_read(0x0108));
        printf("IH_MUTE_PHY     [0x0184] = 0x%02x\n", hdmi_read(0x0184));
        printf("IH_MUTE_I2CM    [0x0185] = 0x%02x\n", hdmi_read(0x0185));
        printf("IH_MUTE         [0x01ff] = 0x%02x\n", hdmi_read(0x01ff));

        /* --- PHY ------------------------------------------------------- */
        printf("\n--- HDMI PHY ---\n");
        printf("PHY_CONF0       [0x3000] = 0x%02x\n", hdmi_read(0x3000));
        printf("PHY_TST0        [0x3001] = 0x%02x\n", hdmi_read(0x3001));
        printf("PHY_STAT0       [0x3004] = 0x%02x\n", hdmi_read(0x3004));
        printf("PHY_I2CM_SLAVE  [0x3020] = 0x%02x\n", hdmi_read(0x3020));
        printf("PHY_I2CM_ADDR   [0x3021] = 0x%02x\n", hdmi_read(0x3021));
        printf("PHY_I2CM_OP     [0x3026] = 0x%02x\n", hdmi_read(0x3026));
        printf("PHY_I2CM_INT    [0x3027] = 0x%02x\n", hdmi_read(0x3027));
        printf("PHY_I2CM_CTLINT [0x3028] = 0x%02x\n", hdmi_read(0x3028));
        printf("PHY_I2CM_DIV    [0x3029] = 0x%02x\n", hdmi_read(0x3029));
        printf("PHY_I2CM_RSTZ   [0x302a] = 0x%02x\n", hdmi_read(0x302a));
        printf("PHY_JTAG_CFG    [0x3034] = 0x%02x\n", hdmi_read(0x3034));

        /* --- Frame composer -------------------------------------------- */
        printf("\n--- Frame Composer ---\n");
        printf("FC_INVIDCONF    [0x1000] = 0x%02x\n", hdmi_read(0x1000));
        printf("FC_INHACTV0     [0x1001] = 0x%02x\n", hdmi_read(0x1001));
        printf("FC_INHACTV1     [0x1002] = 0x%02x\n", hdmi_read(0x1002));
        printf("FC_INHBLANK0    [0x1003] = 0x%02x\n", hdmi_read(0x1003));
        printf("FC_INHBLANK1    [0x1004] = 0x%02x\n", hdmi_read(0x1004));
        printf("FC_INVACTV0     [0x1005] = 0x%02x\n", hdmi_read(0x1005));
        printf("FC_INVACTV1     [0x1006] = 0x%02x\n", hdmi_read(0x1006));
        printf("FC_INVBLANK     [0x1007] = 0x%02x\n", hdmi_read(0x1007));
        printf("FC_HSYNCINDELAY0[0x1008] = 0x%02x\n", hdmi_read(0x1008));
        printf("FC_HSYNCINDELAY1[0x1009] = 0x%02x\n", hdmi_read(0x1009));
        printf("FC_HSYNCINWIDTH0[0x100a] = 0x%02x\n", hdmi_read(0x100a));
        printf("FC_HSYNCINWIDTH1[0x100b] = 0x%02x\n", hdmi_read(0x100b));
        printf("FC_VSYNCINDELAY [0x100c] = 0x%02x\n", hdmi_read(0x100c));
        printf("FC_VSYNCINWIDTH [0x100d] = 0x%02x\n", hdmi_read(0x100d));
        printf("FC_INFREQ0      [0x100e] = 0x%02x\n", hdmi_read(0x100e));
        printf("FC_INFREQ1      [0x100f] = 0x%02x\n", hdmi_read(0x100f));
        printf("FC_INFREQ2      [0x1010] = 0x%02x\n", hdmi_read(0x1010));
        printf("FC_AVICONF3     [0x1017] = 0x%02x\n", hdmi_read(0x1017));
        printf("FC_GCP          [0x1018] = 0x%02x\n", hdmi_read(0x1018));
        printf("FC_AVICONF0     [0x1019] = 0x%02x\n", hdmi_read(0x1019));
        printf("FC_AVICONF1     [0x101a] = 0x%02x\n", hdmi_read(0x101a));
        printf("FC_AVICONF2     [0x101b] = 0x%02x\n", hdmi_read(0x101b));
        printf("FC_AVIVID       [0x101c] = 0x%02x\n", hdmi_read(0x101c));
        printf("FC_PACKET_TX_EN [0x10e3] = 0x%02x\n", hdmi_read(0x10e3));

        /* --- Video packetizer ------------------------------------------ */
        printf("\n--- Video Packetizer ---\n");
        printf("VP_STATUS       [0x0800] = 0x%02x\n", hdmi_read(0x0800));
        printf("VP_PR_CD        [0x0801] = 0x%02x\n", hdmi_read(0x0801));
        printf("VP_STUFF        [0x0802] = 0x%02x\n", hdmi_read(0x0802));
        printf("VP_REMAP        [0x0803] = 0x%02x\n", hdmi_read(0x0803));
        printf("VP_CONF         [0x0804] = 0x%02x\n", hdmi_read(0x0804));

        /* --- Main controller ------------------------------------------- */
        printf("\n--- Main Controller ---\n");
        printf("MC_CLKDIS       [0x4001] = 0x%02x\n", hdmi_read(0x4001));
        printf("MC_SWRSTZREQ    [0x4002] = 0x%02x\n", hdmi_read(0x4002));
        printf("MC_OPCTRL       [0x4003] = 0x%02x\n", hdmi_read(0x4003));
        printf("MC_FLOWCTRL     [0x4004] = 0x%02x\n", hdmi_read(0x4004));
        printf("MC_PHYRSTZ      [0x4005] = 0x%02x\n", hdmi_read(0x4005));
        printf("MC_LOCKONCLOCK  [0x4006] = 0x%02x\n", hdmi_read(0x4006));
        printf("BASE_SFRDIVLOW  [0x4018] = 0x%02x\n", hdmi_read(0x4018));
        printf("BASE_SFRDIVHIGH [0x4019] = 0x%02x\n", hdmi_read(0x4019));

        /* --- Sink / DDC ------------------------------------------------ */
        printf("\n--- Sink / DDC ---\n");
        printf("A_HDCPCFG0      [0x5000] = 0x%02x\n", hdmi_read(0x5000));
        printf("A_HDCPCFG1      [0x5001] = 0x%02x\n", hdmi_read(0x5001));
        printf("A_VIDPOLCFG     [0x5009] = 0x%02x\n", hdmi_read(0x5009));
        printf("PKT_SEND_CTL    [0x0640] = 0x%02x\n", hdmi_read(0x0640));
        printf("I2CM_SLAVE      [0x7e00] = 0x%02x\n", hdmi_read(0x7e00));
        printf("I2CM_ADDRESS    [0x7e01] = 0x%02x\n", hdmi_read(0x7e01));
        printf("I2CM_DATAI      [0x7e03] = 0x%02x\n", hdmi_read(0x7e03));
        printf("I2CM_OPERATION  [0x7e04] = 0x%02x\n", hdmi_read(0x7e04));
        printf("I2CM_INT        [0x7e05] = 0x%02x\n", hdmi_read(0x7e05));
        printf("I2CM_CTLINT     [0x7e06] = 0x%02x\n", hdmi_read(0x7e06));
        printf("I2CM_DIV        [0x7e07] = 0x%02x\n", hdmi_read(0x7e07));
        printf("I2CM_SOFTRSTZ   [0x7e09] = 0x%02x\n", hdmi_read(0x7e09));
        printf("I2CM_RDBUF0..7  [0x7e20] = %02x %02x %02x %02x %02x %02x %02x %02x\n",
            hdmi_read(0x7e20), hdmi_read(0x7e21), hdmi_read(0x7e22),
            hdmi_read(0x7e23), hdmi_read(0x7e24), hdmi_read(0x7e25),
            hdmi_read(0x7e26), hdmi_read(0x7e27));

        /* --- VOP WIN0 -------------------------------------------------- */
        printf("\n--- VOP WIN0 ---\n");
        printf("REG_CFG_DONE    [0x0000] = 0x%08x\n", vop_read(0x0000));
        printf("SYS_CTRL        [0x0008] = 0x%08x\n", vop_read(0x0008));
        printf("SYS_CTRL1       [0x000c] = 0x%08x\n", vop_read(0x000c));
        printf("DSP_CTRL0       [0x0010] = 0x%08x\n", vop_read(0x0010));
        printf("DSP_CTRL1       [0x0014] = 0x%08x\n", vop_read(0x0014));
        printf("DSP_BG          [0x0018] = 0x%08x\n", vop_read(0x0018));
        printf("WIN0_CTRL0      [0x0030] = 0x%08x\n", vop_read(0x0030));
        printf("WIN0_CTRL1      [0x0034] = 0x%08x\n", vop_read(0x0034));
        printf("WIN0_COLOR_KEY  [0x0038] = 0x%08x\n", vop_read(0x0038));
        printf("WIN0_VIR        [0x003c] = 0x%08x\n", vop_read(0x003c));
        printf("WIN0_YRGB_MST   [0x0040] = 0x%08x\n", vop_read(0x0040));
        printf("WIN0_CBR_MST    [0x0044] = 0x%08x\n", vop_read(0x0044));
        printf("WIN0_ACT_INFO   [0x0048] = 0x%08x\n", vop_read(0x0048));
        printf("WIN0_DSP_INFO   [0x004c] = 0x%08x\n", vop_read(0x004c));
        printf("WIN0_DSP_ST     [0x0050] = 0x%08x\n", vop_read(0x0050));
        printf("WIN0_CTRL2      [0x006c] = 0x%08x\n", vop_read(0x006c));
        printf("WIN0_SRC_ALPHA  [0x0060] = 0x%08x\n", vop_read(0x0060));
        printf("WIN0_DST_ALPHA  [0x0064] = 0x%08x\n", vop_read(0x0064));
        printf("DSP_BG_COLOR0   [0x01cc] = 0x%08x\n", vop_read(0x01cc));
        printf("DSP_BG_COLOR1   [0x01d0] = 0x%08x\n", vop_read(0x01d0));
        printf("WIN0_DSP_BG     [0x02b0] = 0x%08x\n", vop_read(0x02b0));

        /* --- CRU ------------------------------------------------------- */
        printf("\n--- CRU ---\n");
        printf("CLKSEL42        [0x01a8] = 0x%08x\n", cru_read(0x01a8));
        printf("CLKSEL43        [0x01ac] = 0x%08x\n", cru_read(0x01ac));
        printf("CLKSEL47        [0x01bc] = 0x%08x\n", cru_read(0x01bc));
        printf("CLKSEL48        [0x01c0] = 0x%08x\n", cru_read(0x01c0));
        printf("CLKSEL49        [0x01c4] = 0x%08x\n", cru_read(0x01c4));
        printf("CLKSEL50        [0x01c8] = 0x%08x\n", cru_read(0x01c8));
        printf("CLKGATE10       [0x0328] = 0x%08x\n", cru_read(0x0328));
        printf("CLKGATE11       [0x032c] = 0x%08x\n", cru_read(0x032c));
        printf("CLKGATE28       [0x0370] = 0x%08x\n", cru_read(0x0370));
        printf("CLKGATE29       [0x0374] = 0x%08x\n", cru_read(0x0374));

        /* --- GRF ------------------------------------------------------- */
        printf("\n--- GRF ---\n");
        printf("GPIO4C_IOMUX    [0xE028] = 0x%08x\n", grf_read(0xE028));
        printf("SOC_CON20       [0x6250] = 0x%08x\n", grf_read(0x6250));
        printf("SOC_STATUS5     [0x04e8] = 0x%08x\n", grf_read(0x04e8));
        printf("\n--- FB Info ---\n");
        memset(&info, 0, sizeof(info));
        if (ioctl(g_fd, RKFB_GETINFO, &info) < 0) {
                perror("RKFB_GETINFO");
                close(g_fd);
                return (1);
        }
        printf("fb_pa  = 0x%016llx\n", (unsigned long long)info.fb_pa);
        printf("width  = %u\n", info.width);
        printf("height = %u\n", info.height);
        printf("stride = %u\n", info.stride);
        printf("fb_size= %llu\n", (unsigned long long)info.fb_size);

        close(g_fd);
        return (0);
}
