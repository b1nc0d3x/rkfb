# Session Checkpoint

## 2026-04-25 current USB-C DP checkpoint

Branch: `rk_drm!`
Host source tree: `/home/b1nc0d3x/fbsd`
Board source tree: `/usr/src`
Board: `rp64dbg`
Safe kernel: `FreeBSD 15.0-RELEASE-p5 #29`

Current goal:
- Keep the safe kernel stable.
- Use module-only `rk_cdn_dp` iterations.
- Push USB-C DP bring-up as far as possible without more built-in kernel risk.

Current safe posture:
- Do not boot `kernel.test` for `rk3399_power` experiments. Even reduced
  provider-only test kernels still failed to complete boot.
- Keep `rk3399_power` in-kernel only on the known-good safe kernel.
- Keep `rk_cdn_dp` as the only live test surface.

What is now proven on the safe kernel:
- `rk_cdn_dp.good.ko` loads and binds as `rk_cdn_dp0`.
- A userspace `/dev/mem` probe proved the raw HDCP/CDN-DP PMU state is already
  open on the safe kernel:
  - `PWRDN_ST[21] == 0`
  - `BUS_IDLE_REQ[11] == 0`
  - `BUS_IDLE_ST[11] == 0`
  - `BUS_IDLE_ACK[11] == 0`
- So the old theory "stage 1 freezes because PMU bus idle is stuck" is no
  longer the active blocker.

Current module-side workaround:
- `rk_cdn_dp` stage 1 now checks PMU readiness directly through the PMU syscon.
- If domain 21 is already on and `BUS_IDLE_ST[11]` / `BUS_IDLE_ACK[11]` are
  already clear, stage 1 skips the freezing `rk3399_power_enable_domain()`
  provider call entirely.

Current live stage results:
- stage 1 (`power-domain`): clean
- stage 2 (`handles`): clean
- stage 3 (`clocks`): clean
- stage 4 (`resets`): clean
- stage 5 (`phys`): clean
- stage 6 (`fw-get`): clean
- stage 7 (`fw-prep`): clean
- stage 8 (`fw-load`): clean
- stage 9 (`fw-active`): clean
- stage 10 (`hpd-sel`): clean
- stage 11 (`hpd-state`): clean, `hpd_status=1`
- stage 12 (`host-cap`): clean
- stage 13 (`dpcd-read`): current blocker

Current blocker:
- `dev.rk_cdn_dp.0.stage=13` still fails in the Cadence firmware/mailbox path.
- The earlier raw AUX crash path is no longer the active blocker.
- Current common failure signature is:
  - `mailbox DPCD reply header failed (60)`
  - or later mailbox send timeouts after repeated retries

Cadence firmware note:
- `dptx.bin` is now part of the live bring-up path.
- Board firmware path:
  - `/boot/firmware/rockchip/dptx.bin`
- FreeBSD driver path:
  - `firmware_get("rockchip/dptx.bin")`
- What it is supposed to do:
  - initialize the Cadence DP controller's embedded firmware
  - enable the mailbox command interface
  - handle HPD, DPCD, EDID, and link-training transactions through mailbox ops
- What it is not:
  - not FUSB302 firmware
  - not USB-PD firmware
  - not Type-C PHY firmware

What this means:
- PMU/power-domain state is no longer the active gate.
- Raw AUX register pokes are no longer the active gate either.
- The active gate is now the first mailbox DPCD capability transaction after:
  - firmware load
  - HPD select
  - HPD state
  - host-cap
- Future live work should stay module-only and focus on:
  - mailbox DPCD readiness
  - Type-C polarity / lane mapping inputs
  - FUSB302-derived orientation instead of guessed `flip`

Latest milestone:
- Rockchip HPD selection through `GRF_SOC_CON26` works on FreeBSD.
- Mailbox `HPD_STATE` now reports `1`, so HPD routing is no longer the blocker.
- Host-cap overrides were added for faster testing:
  - `dev.rk_cdn_dp.0.hostcap_lanes`
  - `dev.rk_cdn_dp.0.hostcap_flip`
- A `DP_SINK_COUNT` wait was also added before the first DPCD caps
  read.
- FreeBSD-native FUSB302 polarity fallback was added locally via:
  - `fusb302_get_typec_status(device_t, struct fusb302_typec_status *)`
- The board can still hard-wedge during the longer mailbox retry path, so risky
  stage-13 tests must keep serial recovery ready.

## 2026-04-20 22:50 America/New_York

Branch: `rk_drm!`
Board repo/docs: `/home/admin/rkfb`
Board source of truth: the dedicated debug board
Board: `rp64dbg`
Console: the dedicated serial console at `1500000`
Target kernel: `RP64KERN_RKDRM`

Current goal:
- Keep `rkfb` as fallback.
- Make `rk_drm!` boot stably.
- Get USB-C video output working.

Confirmed board-side code/doc state:
- Vendor docs are on the board in `/home/admin/rkfb/RP64-DOCS`.
- Active in-tree work is in `/usr/src`, not just the local branch overlay.
- `/usr/src/sys/arm64/conf/RP64KERN_RKDRM` includes:
  - `device rk_cdn_dp`
  - `device rk_drm`
  - `device rk3399_power`
  - `device fusb302`

Confirmed runtime DT / loader facts:
- `/boot/loader.conf` has `hw.rk3399_typec_dp_force="1"`.
- `/boot/loader.conf.local` has `hw.rk3399_typec_dp_force="0"`.
- `kenv hw.rk3399_typec_dp_force` was seen as `0`.
- Despite that, the live OFW tree already looked DP-oriented:
  - DP node enabled
  - `extcon` present
  - `phys` present
  - USB3 OTG node disabled
  - `fusb302` present and `okay`

Kernel rebuild/install performed on the board:
- Built on board:
  - `cd /usr/src && make -j4 buildkernel KERNCONF=RP64KERN_RKDRM`
- Installed on board:
  - `cd /usr/src && make installkernel KERNCONF=RP64KERN_RKDRM`
- `background_fsck` was set persistently to `NO` before reboot.
- Running rebuilt kernel banner seen on serial:
  - `FreeBSD 15.0-RELEASE-p5 #27 ... /usr/obj/usr/src/arm64.aarch64/sys/RP64KERN_RKDRM`

Important loader finding:
- A later "boot it now" attempt still loaded:
  - `Booting [/boot/kernel/kernel]...`
- So the explicit `kernel.RP64KERN_RKDRM` selection did not actually take effect on that run.
- If selecting the alternate kernel manually, verify with:
  - `show kernel`
  - it must print `kernel.RP64KERN_RKDRM` before `boot -v`

Current confirmed attach sequence on the rebuilt kernel:
- `rk_cdn_dp0: <Rockchip RK3399 Cadence DisplayPort scaffold>`
- `rk_cdn_dp0: Cadence DP scaffold attached: phys=1 extcon=yes irq=present`
- `rk_cdn_dp0: DP MMIO/AUX probe deferred; set hw.rk_cdn_dp_attach_debug_probe=1 for debug`
- `fusb3020: <Fairchild FUSB302 Type-C controller> ...`
- `fusb3020: initialized ...`
- `fusb3020: no irq resource, using manual/sysctl reads`

Current confirmed stall point:
- Boot proceeds through device discovery.
- USB buses enumerate:
  - `usbus0: 480Mbps High Speed USB v2.0`
  - `usbus1: 12Mbps Full Speed USB v1.0`
  - `usbus2: 480Mbps High Speed USB v2.0`
  - `usbus3: 12Mbps Full Speed USB v1.0`
  - `usbus4: 5.0Gbps Super Speed USB v3.0`
- Then boot stalls with no further progress.

Current diagnosis:
- The old KLD mismatch is no longer the blocker.
- The live regression is now reproducible in-kernel.
- Most likely problem area is USB / Type-C interaction after `rk_cdn_dp` and `fusb302` attach, before multiuser.
- This matches the earlier bug note that boot can stall during USB attach after the newer DP integration.

Earlier filesystem panic status:
- Prior panic was:
  - `panic: ffs_freefile: freeing free inode`
  - via `sysctl_ffs_fsck`
- That was tied to deferred background fsck on dirty root.
- `background_fsck=NO` is now set, so the immediate blocker has shifted from fsck panic to USB/DP boot stall.

Next steps:
1. At the loader prompt, explicitly select `kernel.RP64KERN_RKDRM` and verify with `show kernel`.
2. If the same stall reproduces, make the kernel safer by disabling automatic `rk_cdn_dp` attach for the next rebuild.
3. Keep `rk_drm` usable while removing the DP/USB-C stall from early boot.
4. Reintroduce DP pieces incrementally after stable multiuser boot is restored.

Later board-doc findings from `RP64-DOCS`:
- RockPro64 USB-C video is the `TYPEC0` path on `USB3.0 PHY0`.
- `TYPEC1` / `USB3.0 PHY1` is wired as the fixed USB 3.0 A-path, not as an
  alternate USB-C DP port.
- `FUSB302B` on `I2C4` is the external CC/orientation controller.
- `VBUS_TYPEC` is switched through `VCC5V0_TYPEC0_EN`.
- `CC2` therefore means flipped orientation on the same `TYPEC0` port, not
  "use port 1".
- RK3399 exposes explicit AUX polarity-reversal pins for this:
  - `TYPEC0_AUXP_PD_PU`
  - `TYPEC0_AUXM_PU_PD`
- Driver work must keep `active_port=0` on RockPro64 and use orientation only
  for `flip`/lane mapping.

Newest stable mailbox result:
- Keep RockPro64 on:
  - `active_port=0`
  - `hostcap_flip=1` when FUSB302 reports flipped orientation
- Stable live query after the corrected `TYPEC0` path shows:
  - `dev.rk_cdn_dp.0.stage=12`
  - `dev.rk_cdn_dp.0.last_error=0`
  - `dev.rk_cdn_dp.0.hpd_status=1`
  - `dev.rk_cdn_dp.0.mbox_last_send_written=0`
  - `dev.rk_cdn_dp.0.mbox_last_empty=1`
  - `dev.rk_cdn_dp.0.mbox_last_empty_after_send=1`
  - `dev.rk_cdn_dp.0.mbox_last_header=0`
  - `dev.rk_cdn_dp.0.mbox_last_keep_alive=43`
- Meaning:
  - Cadence firmware is alive
  - no mailbox reply header is produced
  - `MAILBOX_EMPTY` stays asserted even immediately after the attempted first
    real `READ_DPCD` path
  - the remaining work is to find the last missing `READ_DPCD` precondition,
    not to revisit the old RockPro64 port-selection mistake

Newest kernel-safety correction:
- The DP PHY extras belong in `rk_typec_phy`, not `rk_cdn_dp`.
- First direct in-kernel attempt made `RP64KERN_RKDRM` non-booting.
- The new plan is boot-safe gating:
  - `hw.rk_typec_phy_dp_extras=0` by default
  - explicitly enable later only for live DP testing
- Gated extras include:
  - FUSB302 flip/orientation read
  - `typec_conn_dir`
  - external PSM
  - `uphy_dp_sel`
  - DP A2 -> A0 transition

Safer replacement path:
- leave the installed kernel on the known-good baseline
- stop changing `rk_typec_phy`
- use late-load helpers for risky DP state bridging instead of more built-in
  Type-C changes

## 2026-04-26 late-load helper and boot isolation

What was ruled out:

- Reverting only the built-in `fusb302` Alt Mode export/sysctl additions did
  not restore clean boot on the newer kernel image.
- That A/B kernel still reached deep device attach and then stalled later.
- So the built-in `fusb302` export path is not the primary FreeBSD boot
  blocker.

What was added safely instead:

- New late-load module:
  - `rk3399_fusb302_helper.ko`
- Local source:
  - `/home/b1nc0d3x/fbsd/sys/arm64/rockchip/rk3399_fusb302_helper.c`
- Module path:
  - `/home/b1nc0d3x/fbsd/sys/modules/rockchip/rk3399_fusb302_helper`
- The helper exports:
  - `fusb302_get_dp_altmode_state(device_t,
    struct rk3399_typec_dp_altmode_status *)`
- The helper exposes sysctls:
  - `hw.rk3399_fusb302_helper.valid`
  - `hw.rk3399_fusb302_helper.dp_ready`
  - `hw.rk3399_fusb302_helper.usb_ss`
  - `hw.rk3399_fusb302_helper.pin_assignment`
  - `hw.rk3399_fusb302_helper.dp_status`
  - `hw.rk3399_fusb302_helper.get_count`

Stable-kernel result with helper:

- Booted stable `kernel.old`:
  - `FreeBSD 15.0-RELEASE-p5 #29`
  - `kern.bootfile=/boot/kernel.old/kernel`
- Built and loaded `rk3399_fusb302_helper.ko` post-boot.
- Loaded `rk_cdn_dp.ko` manually post-boot.
- Seeded helper with legacy-good values:
  - `valid=1`
  - `dp_ready=1`
  - `usb_ss=0`
  - `pin_assignment=8`
  - `dp_status=154`

What that proved:

- `stage=12` completes and consumes the helper:
  - `hw.rk3399_fusb302_helper.get_count=1`
  - `dev.rk_cdn_dp.0.dp_altmode_valid=1`
  - `dev.rk_cdn_dp.0.dp_altmode_ready=1`
  - `dev.rk_cdn_dp.0.dp_altmode_usb_ss=0`
  - `dev.rk_cdn_dp.0.dp_altmode_pin_assignment=8`
  - `dev.rk_cdn_dp.0.dp_altmode_status=154`
  - `dev.rk_cdn_dp.0.hpd_status=1`
  - `dev.rk_cdn_dp.0.last_error=0`
  - `dev.rk_cdn_dp.0.stage=12`
- `stage=13` consumes the helper again:
  - `hw.rk3399_fusb302_helper.get_count=2`
- But the first real mailbox DPCD send still stalls at the same boundary:
  - `mbox_last_send_header=0x03010005`
  - `mbox_last_send_size=9`
  - `mbox_last_send_written=0`
  - `mbox_last_full=1`
  - `mbox_last_empty=1`
  - `mbox_last_empty_after_send=1`
  - `mbox_last_write_full_first=1`
  - `mbox_last_write_full_last=1`
  - `mbox_last_write_full_polls=1676`
  - `mbox_last_header=0`
  - `dev.rk_cdn_dp.0.stage` falls back to `12`

Current interpretation:

- The helper-vs-built-in bridge question is answered.
- `rk_cdn_dp` can consume late-load Alt Mode state just fine.
- The remaining blocker is still the deeper Cadence-side precondition behind
  the first real `READ_DPCD` mailbox send.

Useful external note:

- The public FUSB300C datasheet confirms the Type-C controller class is a thin
  client driven by host software through I2C status and interrupts such as:
  - `WAKE`
  - `VBUSOK`
  - `BC_LVL`
  - `COMP`
- That supports the current diagnosis that real Type-C / Alt Mode behavior is
  event-driven, not just a final set of state values.
- use the new post-boot module `rk3399_tcphy_helper.ko`
- the helper applies the TC-PHY GRF fields through syscon only when explicitly
  requested after boot, so DP precondition experiments no longer perturb early
  attach order or boot.

Native DTB correction that moved the frontier:
- Added the working TC-PHY properties from the legacy RockPro64 stack to the
  FreeBSD RockPro64 DTB under `&tcphy0`:
  - `rockchip,typec-conn-dir`
  - `rockchip,external-psm`
  - `rockchip,uphy-dp-sel`
- After rebooting on that DTB, the old stuck-TX result changed natively:
  - `mbox_last_send_written=9`
  - `mbox_last_full=0`
- So those properties are required board data, not just debug overrides.

Current frontier after the DTB fix:
- `READ_DPCD` transmit can succeed, but no reply header appears:
  - `mbox_last_empty=1`
  - `mbox_last_empty_after_send=1`
  - `mbox_last_header=0`

Post-boot helper experiments after that DTB fix:
- Extended `rk3399_tcphy_helper.ko` to try more RK3399 PHY-side writes safely:
  - `PMA_LANE_CFG = PIN_ASSIGN_C_E`
  - `PMA_LANE_CFG = PIN_ASSIGN_D_F`
  - `DP_MODE_CTL = DP_MODE_ENTER_A2`
  - `DP_MODE_CTL = DP_MODE_ENTER_A2` then `DP_MODE_ENTER_A0`
- All helper writes applied and reported success:
  - `last_applied_mask=31`
  - `last_dp_mode_ctl=0xc184` after A2
  - `last_dp_mode_ctl=0xc181` after A2->A0
- None of them changed the live mailbox boundary:
  - `dev.rk_cdn_dp.0.stage=12`
  - `dev.rk_cdn_dp.0.mbox_last_send_written=0`
  - `dev.rk_cdn_dp.0.mbox_last_full=1`
  - `dev.rk_cdn_dp.0.mbox_last_empty=1`

Meaning now:
- the missing piece is no longer the basic TC-PHY field set or these direct
  post-boot PHY mode writes
- the remaining likely gap is higher-level Type-C / DP Alt Mode state, or a
  deeper PHY sequence such as the Linux AUX calibration path rather than the
  simple register toggles above

Crash note:
- A later panic with:
  - `Fatal data abort`
  - `elr: rk_cdn_dp_mailbox_write + 0xac`
  - `far: 0x8`
  happened after serial logged `detached`
- That identifies a detach/rebind race in `rk_cdn_dp`, not a new USB-C DP
  recipe issue
- Immediate source fix:
  - disable the async `rk_cdn_dp_rebind_task` path during staged bring-up
  - add a `detached` guard so mailbox MMIO returns `ENXIO` once teardown starts
