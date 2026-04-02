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

	if (argc != 3)
		errx(1, "usage: %s <block> <offset>", argv[0]);

	ro.block = strtoul(argv[1], NULL, 0);
	ro.off = strtoul(argv[2], NULL, 0);
	ro.val = 0;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	if (ioctl(fd, RKFB_REG_READ, &ro) < 0)
		err(1, "ioctl");

	printf("block=%u off=0x%08x val=0x%08x\n", ro.block, ro.off, ro.val);

	close(fd);
	return (0);
}
