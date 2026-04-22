#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>

#include "rkfb_ioctl.h"

int
main(int argc, char **argv)
{
	int fd;
	char *end;
	struct rkfb_fill fill;

	fd = open("/dev/rkfb0", O_RDWR);
	if (fd < 0)
		err(1, "open");

	fill.pixel = 0x00ff0000;   /* red by default */
	if (argc > 2) {
		fprintf(stderr, "usage: %s [RRGGBB|0xRRGGBB]\n", argv[0]);
		close(fd);
		return (1);
	}
	if (argc == 2) {
		fill.pixel = (uint32_t)strtoul(argv[1], &end, 16);
		if (*argv[1] == '\0' || *end != '\0') {
			fprintf(stderr, "invalid color: %s\n", argv[1]);
			close(fd);
			return (1);
		}
		fill.pixel &= 0x00ffffffu;
	}

	if (ioctl(fd, RKFB_CLEAR, &fill) < 0)
		err(1, "ioctl(RKFB_CLEAR)");

	close(fd);
	return (0);
}
