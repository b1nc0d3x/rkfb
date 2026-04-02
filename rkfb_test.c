#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(void)
{
	int fd;
	struct rkfb_info info;
	uint32_t *fb;
	size_t pixels, i;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_GETINFO, &info) < 0)
		err(1, "ioctl(RKFB_GETINFO)");

	printf("width=%u height=%u bpp=%u stride=%u size=%llu\n",
	    info.width, info.height, info.bpp, info.stride,
	    (unsigned long long)info.fb_size);

	fb = mmap(NULL, info.fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED)
		err(1, "mmap");

	pixels = info.fb_size / 4;

	for (i = 0; i < pixels; i++)
		fb[i] = 0x00ff0000;   /* red in XRGB8888-ish layout */

	sleep(1);

	for (i = 0; i < pixels; i++)
		fb[i] = 0x0000ff00;   /* green */

	sleep(1);

	for (i = 0; i < pixels; i++)
		fb[i] = 0x000000ff;   /* blue */

	sleep(1);

	munmap(fb, info.fb_size);
	close(fd);
	return (0);
}
