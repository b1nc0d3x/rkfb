# rk_drm!

This branch is the RockPro64 / RK3399 DRM/KMS branch.

It is intentionally separate from the working `rkfb!` framebuffer branch.
`rkfb` remains the known-good fallback path. `rk_drm` is the in-tree FreeBSD
DRM/KMS effort.

## What Is Here

- `rkdrm/freebsd-overlay/`
  Tree-relative FreeBSD source files for the in-tree `rk_drm` driver and the
  `RP64KERN_RKDRM` kernel config.
- `rkdrm/patches/`
  FreeBSD `drm2` / arm64 prerequisite patches that had to be applied before
  the Rockchip DRM driver could build and boot sanely.
- `rkdrm/tools/`
  Small userland probes used to validate the KMS path.
- `rkdrm/RKDRM_*.md`
  Architecture, integration, and status notes.
- `rkdrm/CREDITS_AND_PROVENANCE.md`
  Credits and provenance for the external work, docs, and upstream authors
  this branch relied on.

## Current Status

Current milestone from the local FreeBSD tree:

- `rk_drm` builds in-tree on `arm64`
- `RP64KERN_RKDRM` boots successfully
- `rk_drm0` probes on `ofwbus0`
- `/dev/dri/card0` and `/dev/dri/controlD64` exist
- fixed `1920x1080` scanout is active
- dumb buffers work
- Xorg `modesetting` gets past the old
  `KMS doesn't support dumb interface` failure

Still missing:

- dynamic mode enumeration
- EDID-driven mode selection
- hotplug policy
- hardware acceleration
- broader KMS polish like page flips / vblank work

## Intended Use

Use this branch as the DRM/KMS work branch. Keep `rkfb!` as the recovery and
comparison baseline until `rk_drm` fully replaces it.
