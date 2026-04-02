#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <err.h>

#include "rkfb_ioctl.h"

int main(void)
{
	int fd;
	struct rkfb_rect r;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	/* Draw a red rectangle */
	r.x = 100;
	r.y = 100;
	r.w = 200;
	r.h = 150;
	r.pixel = 0x00ff0000;

	if (ioctl(fd, RKFB_FILLRECT, &r) < 0)
		err(1, "ioctl");

	close(fd);
	return 0;
}
