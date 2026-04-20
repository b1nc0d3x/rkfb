# rkfb!

This branch is the minimal working RockPro64 / RK3399 framebuffer branch.

Its job is simple:

- build the standalone `rkfb` FreeBSD display driver
- keep the branch small and stable
- preserve the known-good fallback path while `rk_drm!` evolves separately

## What Is Here

- `rkfb.c`
  The working framebuffer driver.
- `rkfb_ioctl.h`
  Userland ioctl definitions for the driver interface.
- `Makefile`
  FreeBSD kmod build file.
- `opt_*.h`
  Minimal build options required for the module build.
- `PROVENANCE_LINUX_BSD_MIT.md`
  Explicit provenance, licensing boundary, and credit record for this branch.

## What Is Not Here

- DRM/KMS development work
- in-tree FreeBSD kernel overlays
- generic branch clutter, editor backups, or build junk

That work belongs on `rk_drm!`, not here.

## Maintenance Rule

If a change is needed to keep the known-good framebuffer path working, it can
belong here.

If a change is about:

- `/dev/dri/card0`
- KMS
- EDID-driven dynamic modes
- Xorg `modesetting`
- hotplug or page flips in the DRM path

it belongs on `rk_drm!`.
