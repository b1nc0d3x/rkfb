#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

#include "rkfb_ioctl.h"

int
main(void)
{
	int fd;
	struct rkfb_rect r;
	struct rkfb_info info;
	uint32_t pixel;
	off_t off;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_GETINFO, &info) < 0)
		err(1, "ioctl(GETINFO)");

	r.x = 100;
	r.y = 100;
	r.w = 200;
	r.h = 150;
	r.pixel = 0x00ff0000;

	if (ioctl(fd, RKFB_FILLRECT, &r) < 0)
		err(1, "ioctl(FILLRECT)");

	off = ((off_t)100 * info.stride) + ((off_t)100 * 4);

	if (lseek(fd, off, SEEK_SET) < 0)
		err(1, "lseek");

	if (read(fd, &pixel, sizeof(pixel)) != sizeof(pixel))
		err(1, "read");

	printf("pixel at (100,100) = 0x%08x\n", pixel);

	close(fd);
	return (0);
}
