#include <sys/ioccom.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define DRM_IOCTL_BASE 'd'
#define DRM_IOWR(nr, type) _IOWR(DRM_IOCTL_BASE, nr, type)

#define DRM_CAP_DUMB_BUFFER 0x1

struct drm_get_cap {
	uint64_t capability;
	uint64_t value;
};

struct drm_mode_create_dumb {
	uint32_t height;
	uint32_t width;
	uint32_t bpp;
	uint32_t flags;
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
};

#define DRM_IOCTL_GET_CAP DRM_IOWR(0x0c, struct drm_get_cap)
#define DRM_IOCTL_MODE_CREATE_DUMB DRM_IOWR(0xB2, struct drm_mode_create_dumb)

int
main(void)
{
	struct drm_get_cap cap;
	struct drm_mode_create_dumb create;
	int fd, ret, saved_errno;

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("open(/dev/dri/card0)");
		return (1);
	}

	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_DUMB_BUFFER;
	ret = ioctl(fd, DRM_IOCTL_GET_CAP, &cap);
	saved_errno = errno;
	printf("GET_CAP ret=%d errno=%d value=%" PRIu64 "\n",
	    ret, ret < 0 ? saved_errno : 0, cap.value);

	memset(&create, 0, sizeof(create));
	create.width = 1920;
	create.height = 1080;
	create.bpp = 32;
	ret = ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	saved_errno = errno;
	printf("CREATE_DUMB ret=%d errno=%d handle=%u pitch=%u size=%" PRIu64 "\n",
	    ret, ret < 0 ? saved_errno : 0, create.handle, create.pitch,
	    create.size);

	close(fd);
	return (0);
}
