# RKDRM FreeBSD In-Tree Path

## Goal

Move the Rockchip DRM/KMS work out of the private out-of-tree KLD model and
into the FreeBSD kernel tree, because the current kernel does not expose
`drm2` in a way that lets a standalone `rkdrm.ko` link and load cleanly.

## Phase 1 Layout

The scaffold is now staged in the local FreeBSD source tree as:

- `sys/arm64/rockchip/rk_drm.c`
- `sys/arm64/rockchip/rk_drm.h`
- `sys/modules/rockchip/rk_drm/Makefile`
- `sys/conf/files.arm64`
- `sys/modules/rockchip/Makefile`

## What Phase 1 Did

It is intentionally small:

- matches `rockchip,display-subsystem`
- creates one DRM device on the old `drm2` stack
- creates one fixed HDMI-A connector
- creates one TMDS encoder
- creates one CRTC
- advertised one fixed mode: `1920x1080p60`

That first step did not yet:

- program VOP or DW-HDMI registers
- allocate dumb buffers
- expose fbdev emulation
- do EDID or hotplug
- support page flips or vblank

## Current State

The work has moved past the original scaffold.

Current local milestone:

- builds in-tree on FreeBSD `arm64`
- boots as `RP64KERN_RKDRM`
- `rk_drm0` attaches on `ofwbus0`
- exposes `/dev/dri/card0`
- drives a fixed `1920x1080` mode
- supports dumb buffers
- gets Xorg `modesetting` past the old
  `KMS doesn't support dumb interface` failure

It is still a fixed-mode bring-up path, not a complete DRM stack.

## Why This Path

The out-of-tree prototype already proved the source itself can compile, but it
failed at load time because the running kernel did not provide the needed
`drm2` symbol linkage for an external KLD.

The in-tree path fixes the class of problem by making `rk_drm` build inside
the same kernel/module world as `drm2`.

## Build Shape

This is the intended next-step build model:

1. keep `rkfb` as the known-good baseline
2. build `rk_drm` from the FreeBSD source tree
3. prove probe/attach under the kernel-tree build
4. only then start moving fixed-mode VOP/HDMI bring-up knowledge from `rkfb`
   into `rk_drm`

## Next Technical Step

After the in-tree scaffold exists, the next meaningful step is not more API
theory. It is one of:

- build the module from the FreeBSD source tree against a kernel with `drm2`
- or add `device drm2` and `device rk_drm` to an experimental kernel config
  and test it as a kernel-built driver

Phase 2 and beyond are now in progress:

- fixed-mode hardware enable using the known-good `rkfb` sequence
- then dumb buffers
- then real mode handling

## arm64 AGP Patch Note

The first full `RP64KERN_RKDRM` kernel build exposed a FreeBSD `drm2` problem
that is unrelated to Rockchip display logic:

- `drm2` was still enabling legacy `AGP` support on `__aarch64__`
- the build then pulled in AGP-only objects such as:
  - `drm_agpsupport.c`
  - `drm_memory.c`
  - `ttm_agp_backend.c`
- final kernel link failed on missing legacy AGP symbols:
  - `agp_find_device`
  - `agp_bind_memory`
  - `agp_unbind_memory`
  - `agp_bind_pages`
  - `agp_unbind_pages`
  - and related AGP entry points

That is legacy DRM baggage, not a RockPro64 hardware requirement. RockPro64
does not use AGP.

The local arm64 cleanup path is:

1. In `sys/dev/drm2/drm_os_freebsd.h`, restrict `CONFIG_AGP` and
   `CONFIG_MTRR` to legacy x86/ia64-style platforms instead of enabling them
   for every architecture except `__arm__`.
2. In `sys/conf/files`, gate AGP-only DRM files on the `agp` kernel option:
   - `dev/drm2/drm_agpsupport.c`
   - `dev/drm2/drm_memory.c`
   - `dev/drm2/ttm/ttm_agp_backend.c`
3. Keep the arm64/Rockchip DRM path focused on platform/non-AGP code.

This patch is intentionally documented because it is part of making old
FreeBSD `drm2` behave sanely on arm64, and it is easy to misread as a
Rockchip-specific issue when it is not.
