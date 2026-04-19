# RKDRM Architecture Notes

## What DRM/KMS Is Supposed To Do

A display DRM/KMS driver gives userspace a standard kernel object model for
display hardware instead of a private framebuffer device.

The important objects are:

- `connector`
  - represents the sink-facing output, e.g. HDMI-A
  - answers: is a display connected, and what modes are available?

- `encoder`
  - bridges a scanout pipeline to the connector signal format
  - for RK3399 HDMI, this is conceptually the VOP-to-DW-HDMI path

- `crtc`
  - the scanout/timing engine
  - owns the active mode timings and page-flip/vblank behavior

- `framebuffer`
  - pixel storage userspace wants scanned out

- optional planes
  - primary plane, cursor plane, overlays

The normal KMS flow is:

1. userspace opens `/dev/dri/cardN`
2. discovers connector/encoder/crtc objects
3. probes modes
4. allocates a framebuffer
5. asks the kernel to bind framebuffer + mode + connector to a CRTC
6. does page flips / vsync-driven updates

## Why This Is Different From `rkfb`

`rkfb` is a working private framebuffer driver:
- custom `/dev/rkfb0`
- custom ioctls
- direct HDMI/VOP bring-up

`rkdrm` is meant to become the standard display path:
- `/dev/dri/*`
- standard Xorg/Wayland/userspace interfaces
- standard mode objects instead of a private fixed framebuffer ABI

## Current Prototype Scope

The first `rkdrm` step was intentionally small:

- platform DRM driver attach path
- one fixed HDMI-A connector
- one TMDS encoder
- one CRTC
- one fixed mode: `1920x1080p60`

The current branch has already moved beyond that first scaffold:

- fixed-mode hardware modeset is in place
- dumb buffers work
- Xorg `modesetting` is now using the DRM path at fixed `1920x1080`

Still not implemented or not finished yet:

- hotplug / EDID
- page flips / vblank
- Rockchip VOP and DW-HDMI sub-drivers

## Intended Growth Path

1. Stage 0
   - compileable/platform-attaching DRM skeleton
   - fixed mode

2. Stage 1
   - split Rockchip pieces:
     - core `rkdrm`
     - `rkvop`
     - `rkdwhdmi`

3. Stage 2
   - real modeset path using the working `rkfb` hardware knowledge
   - real connector detection and mode enumeration

4. Stage 3
   - GEM/dumb buffers
   - fbdev/scfb bridge if useful
   - page flips and vblank

5. Stage 4
   - dynamic modes and hotplug
   - eventually retire the private `rkfb` path or keep it as a debug tool
