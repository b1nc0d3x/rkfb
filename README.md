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
- additional live runtime switches to `800x600` and `1600x900` have also been
  verified under X

Current bounded-mode policy:

- the driver still boots in the known-good `1920x1080` mode
- runtime KMS mode selection is accepted only for progressive modes within
  `1920x1080` whose pixel clocks match the currently implemented RK3399 VPLL
  table
- native HPD polling is now wired through a driver-local timeout task and
  `drm_helper_hpd_irq_event()`
- software vblank is now wired through a driver-local timeout task, with
  `drm_vblank_init()` / `drm_handle_vblank()` / `drm_send_vblank_event()`
  backing single-CRTC page flips
- hardware/task state is now serialized through a dedicated driver mutex so
  HPD polling, software vblank, page flips, and KMS blank/unblank paths do
  not race each other on the same VOP/HDMI state
- `lastclose` now restores fbdev/vt mode cleanly after DRM clients exit
- CRTC prepare/commit/disable/DPMS hooks now perform real hardware blanking
  and re-enable instead of staying stubbed out
- the initial supported clocks are:
  - `25.200 MHz`
  - `27.000 MHz`
  - `40.000 MHz`
  - `54.000 MHz`
  - `65.000 MHz`
  - `74.250 MHz`
  - `81.600 MHz`
  - `96.000 MHz`
  - `106.500 MHz`
  - `108.000 MHz`
  - `119.000 MHz`
  - `148.500 MHz`
- on the current `W156F1` monitor, the verified exposed EDID modes are:
  - `640x480`
  - `800x600`
  - `1024x768`
  - `1152x864`
  - `1280x1024`
  - `1600x900`
  - `1920x1080`

Still missing:

- hardware acceleration
- deeper long-run stress-testing around repeated flips/modesets

## Intended Use

Use this branch as the DRM/KMS work branch. Keep `rkfb!` as the recovery and
comparison baseline until `rk_drm` fully replaces it.
