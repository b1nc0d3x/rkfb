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
- a physical HDMI unplug/replug validation pass succeeded on the RockPro64
- software vblank and single-CRTC page flips are implemented through a
  driver-local timeout task plus FreeBSD `drm2` vblank helpers:
  - `drm_vblank_init()`
  - `drm_handle_vblank()`
  - `drm_crtc_send_vblank_event()`
- framebuffer-backed scanout works through GEM CMA dumb buffers plus driver
  buffer objects:
  - `DUMB_GET_HARGS`
  - `DUMB_GET_VARS`
  - `DUMB_SET_VARS`
  - `DUMB_PAN_DISPLAY`
- the simple Xorg path works on top of the DRM node via the `modesetting`
  driver, with real monitor EDID instead of a fixed fake mode list
- bounded dynamic modeset was validated end-to-end through:
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

## 2026-04-21 DP Debug Checkpoint

DisplayPort-related crash isolation is now far enough along that it has a
stable recovery baseline and a reproducible panic path.

What is verified:

- a reduced `RP64KERN_RKDRM` kernel that excludes `rk_cdn_dp`,
  `rk3399_power`, and `fusb302` boots reliably and is the current safe kernel
- `DDB`, `KDB`, and persistent crash dumps to swap are configured on the board
- `rk3399_power` loads and attaches successfully as a standalone runtime test
- `fusb302` loads and attaches successfully as a standalone runtime test
- a diagnostic modular `rk_cdn_dp` test copy reaches resource, clock, reset,
  PHY, and scaffold attach milestones when its optional debug probe is deferred
- enabling the debug/AUX probe path reproduces a real `panic: Unhandled System
  Error` and drops the board into `ddb`

The current working hypothesis is that the remaining fault is in the
post-scaffold `rk_cdn_dp` bring-up path, likely around AUX / MMIO access or a
closely related asynchronous hardware fault.

See the detailed checkpoint note here:

- `rkdrm/RKDRM_RK3399_DP_DEBUG_CHECKPOINT_2026-04-21.md`

## 2026-04-22 USB-C IRQ Fix Checkpoint

USB-C bring-up on RK3399 is past the original `fusb302` interrupt-resource
blocker.

What is now verified:

- the `fusb302` DT node carries a valid interrupt description
- the running kernel resolves that child interrupt to mapped IRQ `82`
- descendant child IRQ allocation now succeeds through:
  - `rk_i2c`
  - `ofwbus`
  - `nexus`
- descendant child IRQ activation now succeeds
- descendant child `bus_setup_intr()` now succeeds
- the live boot log shows:
  - `rk_i2c3: child irq setup fusb3020 ... -> 0`
  - `bus_generic_setup_intr: -> 0`
- the board continues booting well past `fusb302` attach into later device
  attach stages

What was fixed in FreeBSD locally:

- the Rockchip `rk_i2c` bus path now bridges descendant child IRQ allocation,
  activation, and interrupt setup correctly for OFW/I2C children such as
  `fusb3020`

Why this matters:

- this confirms the earlier blocker was a FreeBSD base-system bus propagation
  bug, not a board DT description failure
- `fusb302` is no longer limited to the polling/manual fallback because of a
  missing interrupt resource

Current next blocker:

- if USB-C display still does not light up, the remaining fault is now above
  raw `fusb302` IRQ setup, most likely in:
  - Type-C state handling
  - altmode / extcon negotiation
  - `rk_cdn_dp` bring-up
  - DRM connector handling

## 2026-04-22 rp64dbg Module-First DP Checkpoint

Module-first `rk_cdn_dp` bring-up on `rp64dbg` is now past the earlier
linker and dependency failures.

What is now verified:

- the live RockPro64 DT on `rp64dbg` exposes the real DP node:
  - `dp@fec00000 compat=rockchip,rk3399-cdn-dp`
- `rk3399_power.ko` can be rebuilt to export
  `rk3399_power_enable_domain`
- `rk_cdn_dp.ko` can be rebuilt with an explicit
  `MODULE_DEPEND(rk_cdn_dp, rk3399_power, 1, 1, 1)` for the module-first path
- with the load order:
  - `clk`
  - `syscon`
  - `hwreset`
  - `phy`
  - `rk3399_power`
  - `rk_cdn_dp`
  the module now probes and attaches on the real node
- serial-confirmed attach milestones now succeed through:
  - power-domain enable
  - clock enables
  - reset deassert
  - PHY mode switch
  - PHY enable
- the board reaches:
  - `Cadence DP scaffold attached: phys=1 extcon=yes irq=present`

Why this matters:

- the remaining DP blocker on `rp64dbg` is no longer module linkage or early
  bring-up sequencing
- `rk_cdn_dp` can now be narrowed safely as a module on the real board DT
  instead of being baked into the kernel first

Current next blocker:

- the next work item is above scaffold attach:
  - AUX / DPCD probing
  - extcon / Type-C state consumption
  - link-training and DRM connector progression

## 2026-05-12 USB-C DisplayPort End-To-End

End-to-end USB-C DisplayPort scanout is working on the RockPro64 target.
On boot, the rp64dbg lights an attached XYM W156F1 portable monitor and
renders the FreeBSD `vt(4)` console plus SLIM/Xorg login at native
1920x1080 over a single USB-C cable.

What landed end-to-end:

- `fusb302` PD / VDM altmode bring-up with stable HPD signalling
- `rk_typec_phy` DP-altmode PHY init (CC-orientation aware, 2-lane HBR)
- `rk_cdn_dp` Cadence MHDP firmware load + mailbox protocol +
  link-training driver (CR + EQ at HBR 2-lane, software-trained)
- DRM-driven modeset that pushes the Cadence framer MSA + VOP_BIG
  scanout in lockstep
- HPD-IRQ-driven retrain loop (`rk_drm_hpd_task` observes the
  sink-side LINK_STATUS_UPDATED via the partner's VDM ATTENTION
  dp_status and reruns CR + EQ without disturbing the framer)
- shared header `sys/arm64/rockchip/rk_dp_forced_mode.h` as the single
  source of truth for the forced DP mode parameters; both `rk_cdn_dp`
  and `rk_drm` consume it
- non-destructive diagnostic sysctls on `dev.rk_cdn_dp.0`:
  - `probe_warm` — enable cdn-dp clocks without resetting the µCPU
  - `framer_dump_now` — dump Cadence framer regs via mailbox
  - `retrain_now`, `display_power`, `backlight_power`
  - `dpcd_write_now` / `dpcd_read_now` — generic AUX poker

Panel-specific hard rule discovered during bring-up: the XYM W156F1
(and likely many HDMI-to-USB-C converter panels) gates its backlight
on a hidden stream-validity check that **rejects CEA-861 standard
1080p variants** (PHSYNC + narrow hsync 44).  Only DMT-style timing
(NHSYNC + wide hsync 144) makes the backlight come on, even though
the EDID falsely advertises CEA VIC 16 / VIC 31 as native modes.
DPCD-based eDP backlight control (0x720 / 0x721) is silently ignored
on this panel — AUX writes ACK and the sink retains the values, but
no observable backlight change.

Two changes pin the forced mode end-to-end:

- `rk_drm_connector_get_modes` INJECTS the DMT-style 1920x1080 mode
  into `connector->probed_modes` as PREFERRED.  Userspace (Xorg,
  SLIM, fb_helper) sees the working timing and allocates a 1920-wide
  GEM framebuffer with matching stride.
- `rk_drm_crtc_mode_set` force-overrides DRM's chosen mode with the
  forced-mode header values on the USB-C DP path.  Even if DRM picks
  a CEA variant from the EDID-derived list, the framer + VOP both
  end up driving the custom timing.

`fusb302` and `rk_cdn_dp` are now compiled into `RP64KERN_RKDRM`
(previously modules in `/etc/rc.conf kld_list`) so DP bring-up
happens during normal device probe rather than userland init time,
moving panel bring-up several seconds earlier in boot.

## Still In Progress

This branch is not a complete desktop-grade DRM stack yet.

Missing or incomplete pieces still include:

- hardware-accelerated rendering
- deeper long-run stress-testing around repeated flips / modesets
- final root-cause isolation and reintegration of the RK3399 DisplayPort path

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
