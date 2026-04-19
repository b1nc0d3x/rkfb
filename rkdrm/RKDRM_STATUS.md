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
- EDID is now read through the native FreeBSD DDC / `iicbus` path
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

## EDID Status

The connector is no longer purely synthetic.

It now:

- looks up the HDMI DDC bus through the device tree
- reads EDID through FreeBSD's native `device_t` DDC path
- feeds EDID into DRM's standard property and mode parsing helpers

Current limitation:

- `mode_valid` still intentionally restricts the hardware path to
  `1920x1080`
- so EDID-backed probing works, but the driver still only accepts
  `1920x1080` modes for actual use

That is why Xorg now reports:

- `EDID for output HDMI-1`
- probed `1920x1080` variants from the monitor
- `Output HDMI-1 connected`
- `Output HDMI-1 using initial mode 1920x1080 +0+0`

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
