/*
 * hdmi_probe.c
 *
 * Userspace probe tool for rkfb driver.
 * Reads HDMI, VOP, GRF, and CRU registers via /dev/rkfb0 ioctl.
 * No writes — read-only, safe to run at any time.
 *
 * Build: cc -o hdmi_probe hdmi_probe.c
 * Run:   ./hdmi_probe
 */

#include <stdio.h>
#include <fcntl.h>
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


static void
vop_write(uint32_t off, uint32_t val)
{
        struct rkfb_regop ro;
        ro.block = 0;
        ro.off   = off;
        ro.val   = val;
        if (ioctl(g_fd, RKFB_REG_WRITE, &ro) < 0)
                perror("ioctl VOP_REG_WRITE");
}
/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int
main(void)
{
        g_fd = open("/dev/rkfb0", O_RDWR);
        if (g_fd < 0) {
                perror("open /dev/rkfb0");
                return (1);
        }

        /* --- HDMI identification --------------------------------------- */
        printf("--- HDMI ID ---\n");
        printf("design_id  [0x0000] = 0x%02x\n", hdmi_read(0x0000));
        printf("revision   [0x0004] = 0x%02x\n", hdmi_read(0x0004));
        printf("product0   [0x0008] = 0x%02x\n", hdmi_read(0x0008));
        printf("product1   [0x000c] = 0x%02x\n", hdmi_read(0x000c));
        printf("config0    [0x0010] = 0x%02x\n", hdmi_read(0x0010));
        printf("config1    [0x0014] = 0x%02x\n", hdmi_read(0x0014));
        printf("config2    [0x0018] = 0x%02x\n", hdmi_read(0x0018));

        /* --- HDMI interrupt status ------------------------------------- */
        printf("\n--- HDMI Interrupts ---\n");
        printf("IH_PHY_STAT0    [0x0104] = 0x%02x\n", hdmi_read(0x0104));
        printf("IH_MUTE_PHY     [0x0184] = 0x%02x\n", hdmi_read(0x0184));
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
        printf("FC_AVICONF0     [0x1017] = 0x%02x\n", hdmi_read(0x1017));
        printf("FC_AVICONF1     [0x1018] = 0x%02x\n", hdmi_read(0x1018));
        printf("FC_AVICONF2     [0x1019] = 0x%02x\n", hdmi_read(0x1019));
        printf("FC_AVIVID       [0x101b] = 0x%02x\n", hdmi_read(0x101b));

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

        /* --- VOP WIN0 -------------------------------------------------- */
        printf("\n--- VOP WIN0 ---\n");
        printf("SYS_CTRL        [0x0008] = 0x%08x\n", vop_read(0x0008));
        printf("DSP_CTRL0       [0x0010] = 0x%08x\n", vop_read(0x0010));
        printf("WIN0_CTRL0      [0x0030] = 0x%08x\n", vop_read(0x0030));
        printf("WIN0_VIR        [0x003c] = 0x%08x\n", vop_read(0x003c));
        printf("WIN0_YRGB_MST   [0x0040] = 0x%08x\n", vop_read(0x0040));
        printf("WIN0_CBR_MST    [0x0044] = 0x%08x\n", vop_read(0x0044));
        printf("WIN0_ACT_INFO   [0x0048] = 0x%08x\n", vop_read(0x0048));
        printf("WIN0_DSP_INFO   [0x004c] = 0x%08x\n", vop_read(0x004c));
        printf("WIN0_DSP_ST     [0x0050] = 0x%08x\n", vop_read(0x0050));

        /* --- CRU ------------------------------------------------------- */
        printf("\n--- CRU ---\n");
        printf("CLKSEL49        [0x00c4] = 0x%08x\n", cru_read(0x00c4));
        printf("CLKGATE16       [0x0240] = 0x%08x\n", cru_read(0x0240));
        printf("CLKGATE17       [0x0244] = 0x%08x\n", cru_read(0x0244));
        printf("CLKGATE18       [0x0248] = 0x%08x\n", cru_read(0x0248));
        printf("CLKGATE19       [0x024c] = 0x%08x\n", cru_read(0x024c));
        printf("CLKGATE20       [0x0250] = 0x%08x\n", cru_read(0x0250));
        printf("CLKGATE21       [0x0254] = 0x%08x\n", cru_read(0x0254));
        printf("CLKGATE22       [0x0258] = 0x%08x\n", cru_read(0x0258));

        /* --- GRF ------------------------------------------------------- */
        printf("\n--- GRF ---\n");
        printf("SOC_STATUS5     [0x04e8] = 0x%08x\n", grf_read(0x04e8));

        printf("\n--- VOP write test ---\n");
	printf("WIN0_YRGB_MST before = 0x%08x\n", vop_read(0x0040));
	vop_write(0x0040, 0x12345678);
	printf("WIN0_YRGB_MST after  = 0x%08x\n", vop_read(0x0040));
	vop_write(0x0040, 0x00000000);  /* restore */



	printf("\n--- VOP WIN0 scanout setup ---\n");

	struct rkfb_info info;
memset(&info, 0, sizeof(info));
if (ioctl(g_fd, RKFB_GETINFO, &info) < 0) {
        perror("RKFB_GETINFO");
        close(g_fd);
        return (1);
}
printf("\n--- FB Info ---\n");
printf("fb_pa  = 0x%016llx\n", (unsigned long long)info.fb_pa);
printf("width  = %u\n", info.width);
printf("height = %u\n", info.height);
printf("stride = %u\n", info.stride);

printf("\n--- VOP WIN0 scanout setup ---\n");
uint32_t fb_pa32 = (uint32_t)(info.fb_pa & 0xffffffff);

/* Fill FB with a solid blue so we can see it */
struct rkfb_fill fill;
fill.pixel = 0xff0000ff;   /* ARGB blue */
if (ioctl(g_fd, RKFB_CLEAR, &fill) < 0)
        perror("RKFB_CLEAR");

/* stride in 32-bit words = width = 1024 = 0x400 */
uint32_t vir = (info.stride / 4) | ((info.stride / 4) << 16);
uint32_t act = ((info.height - 1) << 16) | (info.width - 1);



	
/* Our framebuffer physical address from rkfb load message */
/* pa=0xc8400000 from dmesg */
//uint32_t fb_pa = 0xc8400000;

/* 1080p timing values */
/* Active: 1920x1080, format ARGB8888 */
/* VIR: stride in 32-bit words = 1920 = 0x780 */

vop_write(0x0040, fb_pa32);              /* WIN0_YRGB_MST — FB address    */
vop_write(0x003c, 0x07800780);        /* WIN0_VIR — stride 1920 words  */
vop_write(0x0048, 0x0437077f);        /* WIN0_ACT_INFO — 1080h x 1920w */
vop_write(0x004c, 0x0437077f);        /* WIN0_DSP_INFO — same          */
vop_write(0x0050, 0x00000000);        /* WIN0_DSP_ST — start at 0,0    */
vop_write(0x0030, 0x00000011);        /* WIN0_CTRL0 — enable, ARGB8888 */
vop_write(0x0000, 0x00000001);        /* REG_CFG_DONE — latch all      */

printf("Scanout programmed. Check display.\n");
printf("WIN0_CTRL0    = 0x%08x\n", vop_read(0x0030));
printf("WIN0_YRGB_MST = 0x%08x\n", vop_read(0x0040));
        close(g_fd);
        return (0);
}
