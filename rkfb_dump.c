#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(void)
{
	int fd;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_DUMPREGS) < 0)
		err(1, "ioctl(RKFB_DUMPREGS)");

	close(fd);
	return (0);
}
