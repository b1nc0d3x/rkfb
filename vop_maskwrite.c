
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(int argc, char **argv)
{
	int fd;
	struct rkfb_regmaskop mo;

	if (argc != 4)
		errx(1, "usage: %s <offset> <mask> <value>", argv[0]);

	mo.off = strtoul(argv[1], NULL, 0);
	mo.mask = strtoul(argv[2], NULL, 0);
	mo.val = strtoul(argv[3], NULL, 0);

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_VOP_MASKWRITE, &mo) < 0)
		err(1, "ioctl(RKFB_VOP_MASKWRITE)");

	close(fd);
	return (0);
}