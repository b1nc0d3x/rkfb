# Linux / BSD / MIT Provenance Notes

This note records the Linux-, BSD-, and MIT-licensed sources that were
actually used during `rkfb` bring-up, along with the scope of that use and
the upstream authors/copyright holders that should be credited.

It is intentionally narrow:
- it covers the `rkfb` display/input work in this repo
- it distinguishes between retained implementation choices and debug-only use
- it does not treat hardware register addresses as copyrightable expression
- it does not claim that ordinary use of FreeBSD kernel APIs creates a
  third-party code dependency by itself

## Primary Non-Linux Source Of Truth

The primary authority for register meanings and bit definitions was the vendor
documentation in `RP64-DOCS`, especially the RK3399 TRM.

Linux and BSD-family sources were used as secondary references, runtime
comparisons, and implementation sanity checks.

## FreeBSD Policy Fit

The local FreeBSD source tree is not "BSD-only" in the absolute sense. It
contains mixed-license material, but the tree also makes that separation very
clear:

- `COPYRIGHT`: the project compilation is distributed under a BSD-style
  license
- `README.md`: GPL/LGPL/CDDL and other third-party material live in explicitly
  identified parts of the tree such as `gnu/`, `cddl/`, and `contrib/`
- `CONTRIBUTING.md`: contributors are expected to have rights to contribute
  under the current license of the file being changed

The practical rule this repo should follow for FreeBSD-facing driver work is:

- keep native driver code permissively licensed where possible
- keep provenance explicit when outside references are used
- avoid importing nontrivial copyleft implementation text into new native
  driver files unless there is a deliberate licensing decision to do so

`rkfb` follows that rule with one important boundary:

- `rkfb.c` and related repo-owned driver files are `BSD-2-Clause`
- RP64-DOCS remained the primary source of truth
- Linux was used here only for documented runtime observations, factual
  register-map cross-checks, and non-verbatim behavioral comparison
- if `rkfb` were ever proposed as an upstream FreeBSD-native driver path, that
  boundary should stay intact: documented hardware facts are fine, but copied
  GPL implementation text should not enter the FreeBSD-facing driver code

## RP64-DOCS Used

The following vendor materials were actually used during this bring-up:

- `Rockchip_RK3399TRM_V1.4_Part1-20170408.pdf`
  - used for:
    - CRU / PLL / clock tree field definitions
    - GRF addressing and HDMI/VOP routing registers
    - reset and gate control semantics
    - VOP and clocking register layout

- `Rockchip RK3399 TRM V1.3 Part2.pdf`
  - used for:
    - DW-HDMI register meanings
    - HDMI PHY programming order
    - frame composer / packetizer / TX-side register semantics
    - EDID/DDC / I2C / `FC_INVIDCONF` / `A_VIDPOLCFG` / `TX_INVID0`

- `RK3399TRM_Part1.txt`
- `RK3399TRM_Part2.txt`
  - plain-text extractions used for fast grep/search while debugging and
    documenting tests

- `part1_gs.txt`
  - used as a searchable clock-tree / reset / gate summary during clock-path
    work

- `RK3399_Design_Guide_V1.0_20170420.pdf`
- `design_guide.txt`
  - consulted for pad/function naming, especially around HDMI-related I/O
    naming and pinmux sanity checks

These RP64-DOCS materials were the primary basis for deciding what each field
meant. Linux and BSD-family sources were then used to compare working software
behavior against the documented hardware.

## Linux / GPL Inputs Used

### 1. Public register-map cross-checks

The following repo files explicitly record that their register offsets were
cross-referenced against public Linux DRM headers/sources:

- `rkfb_hdmi_regs.h`
  - cross-referenced against:
    - `drivers/gpu/drm/rockchip/dw_hdmi-rockchip.c`
    - `drivers/gpu/drm/bridge/synopsys/dw-hdmi.h`
  - upstream attribution:
    - `dw_hdmi-rockchip.c`: Rockchip Electronics Co., Ltd.
    - `dw-hdmi.h`: Freescale Semiconductor, Inc.
  - use made here:
    - factual DW-HDMI / Rockchip register offsets and region grouping
    - no Linux code logic was copied into the working driver from these files

- `rkfb_vop_regs.h`
  - cross-referenced against:
    - `drivers/gpu/drm/rockchip/rockchip_vop_reg.h`
  - upstream attribution:
    - Rockchip Electronics Co., Ltd.
    - Author: Mark Yao `<mark.yao@rock-chips.com>`
  - use made here:
    - factual RK3399 VOP register offsets
    - no Linux tables/macros were intentionally copied as Linux identifiers

- `files/rkfb_hdmi_regs.h`
  - records the same DW-HDMI / Rockchip register-map provenance more
    explicitly in its header comment

- `files/rkfb_vop_regs.h`
  - records the same VOP register-map provenance more explicitly in its
    header comment

- `files/rkfb_gpu_regs.h`
  - cross-referenced against Linux Panfrost register headers
  - upstream attribution:
    - Marty E. Plummer `<hanetzer@startmail.com>`
    - Linaro, Ltd.
    - Rob Herring `<robh@kernel.org>`
    - with register definitions noted there as being based on ARM Limited
      material
  - status:
    - present in the repo
    - not part of the current working `rkfb` display baseline

### 2. Linux runtime observations retained in the working display path

The following Linux-observed values were intentionally carried into the working
FreeBSD `rkfb` path after being checked against RK3399 documentation and board
behavior:

- The working clock tree:
  - `VPLL = 148.5 MHz`
  - `CRU_CLKSEL_CON47 = 0x00000341`
  - `CRU_CLKSEL_CON49 = 0x00000000`
  - implemented in `rkfb.c` in the `rkfb_vop_init_1080p60()` path

- The working fixed display mode:
  - `1920x1080 @ 148.5 MHz`
  - confirmed from Linux/Armbian runtime and used as the fixed baseline mode

- `WIN0_CTRL2 = 0x00000021`
  - retained as the working primary-plane channel/routing value
  - present in `rkfb.c` as `RKFB_VOP_WIN0_CTRL2_PRIMARY`

### 3. Linux runtime observations used only for debugging, not retained as
final code requirements

Linux/Armbian was also used as a working-state comparison target during
bring-up. This is documented heavily in `TEST_LOG.md`.

That debug use included comparing or testing values for:
- `GRF_SOC_CON20`
- `PHY_CONF0`
- `A_VIDPOLCFG`
- `DSP_CTRL1`
- `WIN0_YRGB_MST`
- other VOP / DW-HDMI live register values

Important limitation:
- not every Linux-observed register value was retained in the final driver
- some early Linux reads were later found to come from the wrong physical
  addresses and are explicitly marked invalid in `TEST_LOG.md`

### 4. What was not copied from Linux

The current intent and in-tree comments are explicit about the following:

- no GPL Linux source file was copied verbatim into `rkfb.c`
- no Linux implementation blocks were intentionally pasted into the working
  driver
- factual register addresses and observed runtime values were treated as
  hardware facts, then validated against vendor docs and board behavior

## BSD-Licensed Inputs Used

### 1. FreeBSD DW-HDMI reference

Referenced files:
- `/home/b1nc0d3x/fbsd/sys/dev/hdmi/dwc_hdmi.c`
- `/home/b1nc0d3x/fbsd/sys/dev/hdmi/dwc_hdmi_fdt.c`

License family:
- BSD-2-Clause / FreeBSD style

Upstream attribution:
- Oleksandr Tymoshenko `<gonzo@freebsd.org>`
- Jared McNeill `<jmcneill@invisible.ca>` for `dwc_hdmi_fdt.c`

What was used from them:
- DW-HDMI sequencing as a BSD-side reference
- EDID/DDC probe structure and expected behavior
- ordering around frame composer / PHY / video-path bring-up
- the overflow-clear workaround noted in the existing FreeBSD path

How this shows up in the repo:
- `rkfb.c` comments explicitly reference:
  - “existing FreeBSD DW-HDMI workaround”
  - “existing FreeBSD generic DWC-HDMI sequence”

### 2. NetBSD RK3399 VOP reference

Referenced file:
- `/home/b1nc0d3x/srcs/nbsd/sys/arch/arm/rockchip/rk_vop.c`

License family:
- BSD-2-Clause / NetBSD style

Upstream attribution:
- Jared D. McNeill `<jmcneill@invisible.ca>`

What was used from it:
- the `WIN0` 32-bit RGB data-format value `0`
- the `RGBaaa` / `AAAA` output-mode interpretation for RK3399 VOP big
- the low-field stride encoding for `WIN0_VIR`
- VOP enable/output semantics used as a BSD-side reference
- interrupt bit naming during VOP fault debugging

How this shows up in the repo:
- `rkfb.c` explicitly notes:
  - “BSD RK3399 VOP reference uses data-format value 0 for 32-bit RGB”
  - “Follow the RK3399 VOP model used by the BSD RK3399 VOP driver”
- `TEST_LOG.md` records multiple debug steps against this BSD reference

### 3. NetBSD RK3399 DW-HDMI reference

Referenced file:
- `/home/b1nc0d3x/srcs/nbsd/sys/arch/arm/rockchip/rk_dwhdmi.c`

License family:
- BSD-2-Clause / NetBSD style

Upstream attribution:
- Jared D. McNeill `<jmcneill@invisible.ca>`

What was used from it:
- RK3399 DW-HDMI PHY/MPLL table values as a BSD-licensed reference
- especially the `148.5 MHz` PHY settings used to sanity-check the PHY
  programming path
- the “configure GEN2 PHY twice” behavior that was carried into the FreeBSD
  `rkfb` PHY init sequence

How this shows up in the repo:
- `rkfb.c` explicitly notes:
  - “NetBSD's DW-HDMI PHY path performs the GEN2 configuration twice”
  - the PHY table block is described as cross-checked against a BSD-licensed
    Synopsys DW-HDMI PHY implementation used on RK3399

### 4. FreeBSD framebuffer / vt integration references

Referenced files:
- `/home/b1nc0d3x/fbsd/sys/dev/fb/fbd.c`
- `/home/b1nc0d3x/fbsd/sys/dev/fb/fbreg.h`
- `/home/b1nc0d3x/fbsd/sys/dev/vt/hw/fb/vt_fb.c`

License family:
- BSD-2-Clause / FreeBSD style

Upstream attribution:
- `fbd.c` / `vt_fb.c`:
  - Aleksandr Rybalko
  - The FreeBSD Foundation
- `fbreg.h`:
  - Kazutaka YOKOTA `<yokota@zodiac.mech.utsunomiya-u.ac.jp>`

What was used from them:
- the integration model for exposing a framebuffer to FreeBSD `vt`
- `struct fb_info` usage
- `fbd_register()` / `fbd_unregister()` expectations
- confirming `/dev/fb0` behavior and the `vt` framebuffer path

How this shows up in the repo:
- `rkfb.c` includes `vt_fb.h`
- `rkfb` now populates `struct fb_info`
- `rkfb` registers itself through `fbd_register()`
- `TEST_LOG.md` explicitly documents the `vt/fbd` integration step

### 5. FreeBSD USB mouse baseline work

Referenced files / subsystems:
- FreeBSD `ums` module source under `/usr/src/sys/modules/usb/ums`
- FreeBSD `sysmouse` / `moused`

License family:
- FreeBSD BSD-style base system licensing

What was used from them:
- to fix pointer input on the working baseline
- not part of the HDMI/display driver logic itself

How this shows up in the repo:
- `FREEBSD_BASELINE.md`
- `deploy-freebsd-board.sh`
- `TEST_LOG.md`

## MIT-Licensed Inputs Used

No MIT-licensed source file has been intentionally copied or adapted into the
current `rkfb` driver implementation during this bring-up work.

That statement is narrow:
- it applies to the `rkfb` implementation and the tracked helper artifacts in
  this repo
- it does not attempt to classify every downstream package used at runtime
  (for example Xorg/Xfce package licensing)

## Acknowledgements

Thank you to the upstream authors and maintainers whose published work was
used as reference, comparison material, or BSD-licensed implementation
guidance during this bring-up:

- Rockchip Electronics Co., Ltd.
- Freescale Semiconductor, Inc.
- Mark Yao
- Marty E. Plummer
- Rob Herring
- Linaro, Ltd.
- ARM Limited, for the underlying register material referenced by Panfrost
- Oleksandr Tymoshenko
- Jared McNeill
- Jared D. McNeill
- Aleksandr Rybalko
- Kazutaka YOKOTA
- The FreeBSD Foundation

This acknowledgement section is intentionally broader than the retained-code
list above: some of these inputs were used only as cross-checks or runtime
comparisons, while others materially influenced the final working FreeBSD
baseline.

## Short Practical Summary

The implemented `rkfb` baseline was built from:
- vendor RK3399 documentation as the primary authority
- Linux runtime observations for working board values
- BSD-family source references for VOP, DW-HDMI, PHY, and FreeBSD framebuffer
  integration behavior

The key Linux-derived items intentionally retained in the working code are:
- `1920x1080 @ 148.5 MHz`
- `VPLL = 148.5 MHz`
- `CLKSEL47 = 0x00000341`
- `CLKSEL49 = 0x00000000`
- `WIN0_CTRL2 = 0x00000021`

The key BSD-derived implementation guidance intentionally retained in the
working code is:
- VOP `WIN0` format / stride / output-mode semantics
- DW-HDMI sequencing and overflow handling
