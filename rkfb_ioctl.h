#ifndef _RKFB_IOCTL_H_
#define _RKFB_IOCTL_H_

#include <sys/ioccom.h>
#include <sys/types.h>

#define RKFB_IOCTL_BASE   'R'

struct rkfb_info {
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t stride;
	uint64_t fb_size;
};

struct rkfb_rect {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
	uint32_t pixel;
};


struct rkfb_fill {
	uint32_t pixel;
};

struct rkfb_regdump {
	uint32_t base;
	uint32_t count;
};

#define RKFB_GETINFO _IOR(RKFB_IOCTL_BASE, 0, struct rkfb_info)
#define RKFB_CLEAR _IOW(RKFB_IOCTL_BASE, 1, struct rkfb_fill)
#define RKFB_FILLRECT _IOW(RKFB_IOCTL_BASE, 2, struct rkfb_rect)
#define RKFB_DUMPREGS _IO(RKFB_IOCTL_BASE, 3)
#define RKFB_VOP_DUMP_RANGE _IOW(RKFB_IOCTL_BASE, 4, struct rkfb_regdump)
#define RKFB_HDMI_DUMP_RANGE _IOW(RKFB_IOCTL_BASE, 5, struct rkfb_regdump)


#endif
