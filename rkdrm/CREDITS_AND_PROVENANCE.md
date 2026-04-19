# Credits And Provenance

This branch exists to carry the RockPro64 / RK3399 FreeBSD DRM/KMS work.

It also needs an explicit credit record, because the working `rk_drm` path was
not developed in a vacuum. It stands on:

- Rockchip public vendor documentation
- FreeBSD `drm2`, `fbd`, and DW-HDMI code
- BSD Rockchip display references from NetBSD
- earlier `rkfb` hardware bring-up work

This note separates:

- primary source-of-truth documentation
- BSD / MIT reference inputs
- runtime observations
- things that were *not* copied verbatim

It is intentionally branch-specific. Linux-specific provenance from the older
`rkfb` bring-up is not repeated here.

## Primary Hardware Documentation

The primary authority for register meanings and field semantics was the vendor
documentation under `RP64-DOCS`.

Materials actually used:

- `Rockchip_RK3399TRM_V1.4_Part1-20170408.pdf`
  - CRU / PLL / clock tree fields
  - GRF routing and pinmux fields
  - VOP register layout and semantics
- `Rockchip RK3399 TRM V1.3 Part2.pdf`
  - DW-HDMI controller and PHY programming
  - DDC / EDID / I2C register semantics
  - frame composer / packetizer / TX-side controls
- `RK3399TRM_Part1.txt`
- `RK3399TRM_Part2.txt`
  - searchable text extracts used during debugging
- `part1_gs.txt`
  - searchable clock / reset / gate summary
- `RK3399_Design_Guide_V1.0_20170420.pdf`
- `design_guide.txt`
  - HDMI-related I/O naming and board-level sanity checks

These RP64-DOCS files were the primary source of truth for what the hardware
means. Other sources were used as comparisons, cross-checks, or reference
implementations.

## FreeBSD Sources Used

### Generic framebuffer / vt bridge

Referenced files:

- `sys/dev/fb/fbd.c`
- `sys/dev/fb/fbreg.h`
- `sys/dev/vt/hw/fb/vt_fb.c`

Credits:

- Aleksandr Rybalko
- The FreeBSD Foundation
- Kazutaka YOKOTA `<yokota@zodiac.mech.utsunomiya-u.ac.jp>`

Why they matter here:

- `rkfb` first used the normal `fbd` / `vt` bridge instead of inventing a new
  FreeBSD console path
- `rk_drm` later reused the same system integration model on the DRM side
- the `ttydev` memattr fix was made in our driver export path so that the
  original FreeBSD `fbd` / `vt` code could remain untouched

### FreeBSD DW-HDMI reference

Referenced files:

- `sys/dev/hdmi/dwc_hdmi.c`
- `sys/dev/hdmi/dwc_hdmi_fdt.c`

Credits:

- Oleksandr Tymoshenko `<gonzo@freebsd.org>`
- Jared McNeill `<jmcneill@invisible.ca>`

Why they matter here:

- BSD-side ordering reference for DW-HDMI setup
- EDID / DDC probe expectations
- PHY / frame composer / video-path sequencing sanity checks
- carried forward first into `rkfb`, then into the `rk_drm` hardware path

### FreeBSD drm2 / TTM / arm64 work

Referenced and patched files:

- `sys/dev/drm2/drm_os_freebsd.h`
- `sys/dev/drm2/ttm/ttm_bo_util.c`
- `sys/conf/files`
- `sys/conf/files.arm64`

Credits explicitly visible in those areas include:

- Thomas Hellstrom `<thellstrom-at-vmware-dot-com>`
- VMware, Inc.
- The broader FreeBSD DRM / `drm2` maintainers

Why they matter here:

- old FreeBSD `drm2` needed arm64 cleanup before `rk_drm` could build and boot
- TTM memory-attribute handling needed an `__aarch64__` path
- AGP had to be kept out of the arm64 build
- `drm_ioc32.c` had to be brought into the kernel file list for
  `compat_freebsd32`

These are documented in:

- `rkdrm/patches/0001-freebsd-drm2-arm64-prereqs.patch`
- `rkdrm/patches/0002-freebsd-rk_drm-integration.patch`

## NetBSD BSD-Licensed References Used

### RK3399 VOP

Referenced file:

- `sys/arch/arm/rockchip/rk_vop.c`

Credits:

- Jared D. McNeill `<jmcneill@invisible.ca>`

Why it mattered:

- 32-bit RGB format value / output interpretation
- `WIN0_VIR` stride encoding reference
- VOP enable / output semantics
- interrupt naming used during early fault debugging

### RK3399 DW-HDMI

Referenced file:

- `sys/arch/arm/rockchip/rk_dwhdmi.c`

Credits:

- Jared D. McNeill `<jmcneill@invisible.ca>`

Why it mattered:

- RK3399 DW-HDMI PHY/MPLL table reference
- `148.5 MHz` PHY sanity checks
- the “configure GEN2 PHY twice” behavior carried into the working FreeBSD
  hardware path

## Relationship To rkfb

`rk_drm` did not discover the hardware path from zero.

The working hardware sequence first came together in `rkfb`, which solved the
original W156F1 / RockPro64 FreeBSD display problem. `rk_drm` then reused the
same board-proven knowledge in a standards-based DRM/KMS framework.

That means the `rk_drm` branch inherits some of the same external credit line
as the original `rkfb` work:

- RP64-DOCS as primary documentation
- FreeBSD DW-HDMI and framebuffer integration references
- NetBSD RK3399 VOP / DW-HDMI references

## What Was Not Copied Verbatim

The claim here is intentionally narrow:

- no third-party driver file was copied verbatim into this branch
- register addresses and runtime values were treated as hardware facts and
  checked against vendor documentation
- the DRM/KMS integration work itself was built around FreeBSD `drm2` and
  our own Rockchip driver code

## Thanks

The following people and projects materially helped make this branch possible,
either through public code, public documentation, or the existing FreeBSD
kernel infrastructure it builds on:

- Aleksandr Rybalko
- The FreeBSD Foundation
- Kazutaka YOKOTA
- Oleksandr Tymoshenko
- Jared McNeill
- Thomas Hellstrom
- VMware, Inc.

And, most importantly for the hardware itself:

- the Rockchip RK3399 public documentation set in `RP64-DOCS`
