#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(void)
{
	int fd;
	struct rkfb_info info;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_GETINFO, &info) < 0)
		err(1, "ioctl");

	printf("width=%u height=%u bpp=%u stride=%u size=%llu\n",
	    info.width, info.height, info.bpp, info.stride,
	    (unsigned long long)info.fb_size);

	close(fd);
	return (0);
}
