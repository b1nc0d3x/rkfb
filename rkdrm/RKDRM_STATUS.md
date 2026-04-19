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
- EDID is now read through the native FreeBSD DDC / `iicbus` path
- bounded dynamic modeset is implemented from the selected DRM mode down into:
  - RK3399 VPLL programming
  - VOP timing registers
  - DW-HDMI frame-composer timing registers
  - HDMI PHY parameter selection
- native HPD polling is implemented through a driver-local timeout task plus
  `drm_helper_hpd_irq_event()`
- connector DPMS and CRTC prepare/commit/disable hooks now drive real hardware
  blank/unblank instead of remaining no-ops
- `fbd` / `vt` can attach through the DRM path
- dumb-buffer capability is reported and working
- Xorg `modesetting` can use the driver through `/dev/dri/card0`

## Direct KMS Proof

The userland dumb-buffer probe under `tools/drm_dumb_probe.c` verified:

- `DRM_CAP_DUMB_BUFFER = 1`
- `DRM_IOCTL_MODE_CREATE_DUMB` succeeds

That was the concrete proof needed to get past the earlier Xorg failure:

- old failure:
  - `KMS doesn't support dumb interface`
- current result:
  - Xorg `modesetting` reaches the fixed HDMI output path

The next direct proof is now also in place:

- the board booted the rebuilt `RP64KERN_RKDRM` kernel
- `slim` and Xorg started on that kernel
- `xrandr` reported multiple EDID-backed modes on `HDMI-1`
- a live switch to `1024x768` succeeded
- a live switch back to `1920x1080` succeeded
- after widening the bounded PLL set, `xrandr` now reports:
  - `640x480`
  - `800x600`
  - `1024x768`
  - `1152x864`
  - `1280x1024`
  - `1600x900`
  - `1920x1080`
- additional live switches to `800x600` and `1600x900` both succeeded

## EDID And Modeset Status

The connector is no longer purely synthetic.

It now:

- looks up the HDMI DDC bus through the device tree
- reads EDID through FreeBSD's native `device_t` DDC path
- feeds EDID into DRM's standard property and mode parsing helpers

Current limitation:

- `mode_valid` now accepts only the subset of EDID modes whose clocks match the
  currently implemented RK3399 VPLL table and whose dimensions fit within the
  bounded `1920x1080` scanout policy
- the current supported clocks are:
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
- standard DMT clocks that are slightly off those integer-mode values are
  accepted through a narrow `250 kHz` tolerance window, which is what lets:
  - `25.175 MHz` map to `25.200 MHz`
  - `81.62 MHz` map to `81.600 MHz`
- interlaced and doublescan modes are still rejected
- the driver still boots in the known-good `1920x1080` mode before KMS picks a
  runtime mode

That means EDID is now doing real work for both discovery and mode selection,
but only inside the bounded hardware policy above.

## Immediate Validation State

The new mode path has now passed all of the following on the RockPro64 board:

- in-tree `arm64` `rk_drm` module build
- full `RP64KERN_RKDRM` kernel build
- kernel install and reboot
- DRM attach and `/dev/dri/card0` creation on the new kernel
- Xorg `modesetting` startup on the new kernel
- automatic `slim` / Xorg startup on the new kernel
- EDID-backed mode exposure through `xrandr`
- actual runtime switches to non-`1080p` modes:
  - `1024x768`
  - `800x600`
  - `1600x900`

That moves this branch from "enumerates modes" to "performs real bounded
dynamic modeset on hardware."

## Still In Progress

This branch is not a complete desktop-grade DRM stack yet.

Missing or incomplete pieces still include:

- a physical unplug/replug validation pass for the new HPD path
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
