# RKDRM Status

## Verified Milestones

The current in-tree `rk_drm` work has crossed the minimum useful DRM/KMS
milestones on RockPro64 / RK3399:

- `rk_drm` builds as part of the FreeBSD kernel tree
- the experimental `RP64KERN_RKDRM` kernel boots
- `rk_drm0` attaches on `ofwbus0`
- DRM device nodes are created:
  - `/dev/dri/card0`
  - `/dev/dri/controlD64`
- a fixed `1920x1080` mode is programmed
- `fbd` / `vt` can attach through the DRM path
- dumb-buffer capability is reported and working
- Xorg `modesetting` can use the driver at fixed `1920x1080`

## Direct KMS Proof

The userland dumb-buffer probe under `tools/drm_dumb_probe.c` verified:

- `DRM_CAP_DUMB_BUFFER = 1`
- `DRM_IOCTL_MODE_CREATE_DUMB` succeeds

That was the concrete proof needed to get past the earlier Xorg failure:

- old failure:
  - `KMS doesn't support dumb interface`
- current result:
  - Xorg `modesetting` reaches the fixed HDMI output path

## Still In Progress

This branch is not a complete desktop-grade DRM stack yet.

Missing or incomplete pieces still include:

- EDID-backed mode enumeration
- dynamic modes and `xrandr` mode lists
- hotplug handling beyond the current fixed-mode path
- hardware-accelerated rendering
- broader KMS cleanup around page flips / vblank / polish

## Relationship To rkfb

`rkfb` solved the original hardware problem first. It remains the known-good
fallback.

`rk_drm` is the standards-based path:

- `/dev/dri/*`
- DRM/KMS objects
- Xorg `modesetting`
- eventual modern display stack path

Development should continue with `rkfb` kept available as the fallback path,
not as a live handoff owner.
