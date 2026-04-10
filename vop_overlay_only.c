#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include "rkfb_ioctl.h"

#define GRF_PA    0xff320000UL
#define VIOGRF_PA 0xff770000UL
#define VOP_PA    0xff900000UL

int
main(void)
{
        int memfd, fbfd;
        struct rkfb_info info;
        struct rkfb_fill fill;
        volatile uint32_t *grf, *vio, *vop;

        memfd = open("/dev/mem", O_RDWR);
        if (memfd < 0)
                err(1, "open /dev/mem");
        fbfd = open("/dev/rkfb0", O_RDWR);
        if (fbfd < 0)
                err(1, "open /dev/rkfb0");

        memset(&info, 0, sizeof(info));
        if (ioctl(fbfd, RKFB_GETINFO, &info) < 0)
                err(1, "RKFB_GETINFO");

        fill.pixel = 0xffff0000; /* red */
        if (ioctl(fbfd, RKFB_CLEAR, &fill) < 0)
                err(1, "RKFB_CLEAR");

        grf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, GRF_PA);
        vio = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, VIOGRF_PA);
        vop = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, VOP_PA);
        if (grf == MAP_FAILED || vio == MAP_FAILED || vop == MAP_FAILED)
                err(1, "mmap");

        /* HDMI HPD/SCL/SDA pinmux and VOPB -> HDMI route */
        grf[0x010c / 4] = (0xfc00u << 16) | 0x5400u;
        vio[0x0250 / 4] = (0x00ffu << 16) | 0x0000u;

        /* 720p60 timing + framebuffer scanout */
        vop[0x000c / 4] = 0x0003a000;
        vop[0x0010 / 4] = 0x00000000;
        vop[0x0014 / 4] = 0x0000e400;
        vop[0x0018 / 4] = 0x00000000;
        vop[0x01a0 / 4] = 0x06710027;
        vop[0x01a4 / 4] = 0x01040604;
        vop[0x01a8 / 4] = 0x02ed0004;
        vop[0x01ac / 4] = 0x001902e9;
        vop[0x003c / 4] = 0x05000500;
        vop[0x0040 / 4] = (uint32_t)(info.fb_pa & 0xffffffffu);
        vop[0x0048 / 4] = 0x02cf04ff;
        vop[0x004c / 4] = 0x02cf04ff;
        vop[0x0050 / 4] = 0x00000000;
        vop[0x0030 / 4] = 0x00000011;
        vop[0x0008 / 4] = 0x20821800;
        vop[0x0000 / 4] = 0x00000001;
        usleep(50000);

        printf("SOC20=0x%08x SYS=0x%08x WIN0=0x%08x FB=0x%08x\n",
            vio[0x0250 / 4], vop[0x0008 / 4], vop[0x0030 / 4], vop[0x0040 / 4]);
        return (0);
}
