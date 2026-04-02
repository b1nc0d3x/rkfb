#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>

#include "rkfb_ioctl.h"

int
main(int argc, char **argv)
{
	int fd;
	struct rkfb_regdump rd;

	if (argc != 3)
		errx(1, "usage: %s <base_hex> <count>", argv[0]);

	rd.base = strtoul(argv[1], NULL, 0);
	rd.count = strtoul(argv[2], NULL, 0);

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_VOP_DUMP_RANGE, &rd) < 0)
		err(1, "ioctl(RKFB_VOP_DUMP_RANGE)");

	close(fd);
	return (0);
}
