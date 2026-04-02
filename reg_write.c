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
	struct rkfb_regop ro;

	if (argc != 4)
		errx(1, "usage: %s <block> <offset> <value>", argv[0]);

	ro.block = strtoul(argv[1], NULL, 0);
	ro.off = strtoul(argv[2], NULL, 0);
	ro.val = strtoul(argv[3], NULL, 0);

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_REG_WRITE, &ro) < 0)
		err(1, "ioctl(RKFB_REG_WRITE)");

	close(fd);
	return (0);
}
