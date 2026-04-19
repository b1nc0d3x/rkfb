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
- HDMI EDID is read through native FreeBSD DDC / `iicbus`
- runtime modeset is now EDID-driven for the bounded RK3399-safe clock set
  implemented in `rk_drm_hw.c`
- dumb buffers work
- Xorg `modesetting` gets past the old
  `KMS doesn't support dumb interface` failure
- Xorg now exposes multiple EDID-backed modes on `HDMI-1`
- a live runtime switch to `1024x768` and back to `1920x1080` has been
  verified under X

Current bounded-mode policy:

- the driver still boots in the known-good `1920x1080` mode
- runtime KMS mode selection is accepted only for progressive modes within
  `1920x1080` whose pixel clocks match the currently implemented RK3399 VPLL
  table
- the initial supported clocks are:
  - `27.000 MHz`
  - `54.000 MHz`
  - `65.000 MHz`
  - `74.250 MHz`
  - `96.000 MHz`
- `106.500 MHz`
- `148.500 MHz`
- on the current `W156F1` monitor, the verified runtime EDID modes include:
  - `1920x1080`
  - `1024x768`

Still missing:

- hotplug policy
- hardware acceleration
- broader KMS polish like page flips / vblank work

## Intended Use

Use this branch as the DRM/KMS work branch. Keep `rkfb!` as the recovery and
comparison baseline until `rk_drm` fully replaces it.
