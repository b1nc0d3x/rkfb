#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(int argc, char **argv)
{
	int fd;
	struct rkfb_fill fill;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	fill.pixel = 0x00ff0000;   /* red */

	if (ioctl(fd, RKFB_CLEAR, &fill) < 0)
		err(1, "ioctl(RKFB_CLEAR)");

	close(fd);
	return (0);
}
