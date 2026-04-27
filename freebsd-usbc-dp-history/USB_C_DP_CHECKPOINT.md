# USB-C DP Checkpoint

Current safe posture on the RockPro64 boards:

- Boot the known-good `RP64KERN_RKDRM` kernel only.
- Keep `rk_cdn_dp` out of the kernel image.
- Keep `ofwbus` and other core bus changes out of the live boot path.
- Keep panic handling enabled so failures land in DDB instead of forcing blind resets.
- Treat `fusb302` as the current stable Type-C base.
- Use the safe kernel plus module-only `rk_cdn_dp` iterations as the only live USB-C DP test path.

What was confirmed:

- The board boots reliably again from the restored safe SD card kernel.
- `sysctl debug.kdb.enter=1` reaches `db>`, so DDB is usable on the live kernel.
- `dumpon -l` reports the swap-backed dump device.
- `dp@fec00000` exists in the OFW tree but remains unbound when `rk_cdn_dp.ko` is loaded after boot.
- `rk_cdn_dp.ko` now builds as a safer staged scaffold with inert attach semantics and explicit `allow_phys` / `allow_aux` gates in source.
- A userspace `/dev/mem` probe showed the raw RK3399 PMU state for the HDCP/CDN-DP path is already open on the safe kernel:
  - `PWRDN_ST[21] == 0`
  - `BUS_IDLE_REQ[11] == 0`
  - `BUS_IDLE_ST[11] == 0`
  - `BUS_IDLE_ACK[11] == 0`
- The live blocker is no longer PMU state itself. It is now in the staged driver path after stage 5.

What was attempted and rolled back:

- A late-binding `ofwbus` rescan and `bus_driver_added` experiment was added to let post-boot `rk_cdn_dp.ko` bind the existing DP node.
- That kernel still hung during boot at the same late attach point, so the `ofwbus` patch is not part of the safe path.
- A narrower `ofwbus` `bus_driver_added()` hook that only cleared stale direct-child `unknown` devclasses was also built and tested, and that test kernel crashed as well.
- Building `rk_cdn_dp` into the kernel is also not part of the safe path until a lower-risk attach strategy is proven.

Current blocker:

- The current `rk_cdn_dp` module path now binds safely and advances through:
  - stage 1: power-domain
  - stage 2: handles
  - stage 3: clocks
  - stage 4: resets
  - stage 5: phys
  - stage 6: firmware get
  - stage 7: firmware prep
  - stage 8: firmware load
  - stage 9: firmware active
  - stage 10: Rockchip HPD selection
  - stage 11: mailbox HPD state
  - stage 12: mailbox host capabilities
- The new live blocker is stage 13 (`dpcd-read`).
- That stage no longer crashes immediately. It now fails in the Cadence
  firmware/mailbox path while trying to read DPCD capability bytes.

Cadence firmware milestone:

- The driver no longer uses the old raw AUX register path as the primary probe.
- `rk_cdn_dp` now uses the Cadence firmware/mailbox direction.
- The firmware file is loaded from:
  - `/boot/firmware/rockchip/dptx.bin`
- FreeBSD loads it with:
  - `firmware_get("rockchip/dptx.bin")`

What `dptx.bin` is supposed to do:

- `dptx.bin` is the Cadence DisplayPort transmitter firmware blob for the
  RK3399 CDN-DP block.
- It is not USB-PD firmware and not FUSB302 firmware.
- It is loaded into the Cadence DP controller's internal memory and brings up
  the controller's embedded microcontroller.
- After that, the driver is supposed to use the Cadence mailbox protocol for:
  - HPD state
  - DPCD reads and writes
  - EDID fetch
  - link training
  - later video/audio configuration

What is now proven:

- `dptx.bin` is present and loadable from the board firmware path.
- Firmware load and activation complete without the earlier raw-AUX crashes.
- Rockchip HPD routing through `GRF_SOC_CON26` works.
- Mailbox HPD query works and reports `hpd_status=1`.
- Mailbox host-cap stage works.
- First mailbox DPCD read is the remaining blocker.

Latest mailbox-side findings:

- The original mailbox DPCD read failed with:
  - `mailbox DPCD reply header failed (60)`
- Host-cap was then split and tested with explicit overrides:
  - `lanes=4 flip=1`
  - `lanes=2 flip=1`
- Neither override cleared the DPCD timeout.
- A `DP_SINK_COUNT` wait was added before the first DPCD caps read.
- That changed behavior:
  - the failure path no longer returned immediately
  - repeated mailbox retries eventually left the board wedged hard enough that
    serial `reset` produced no response

Important FreeBSD-native finding:

- FreeBSD already exports useful Type-C cable state from the FUSB302 driver via:
  - `fusb302_get_typec_status(device_t, struct fusb302_typec_status *)`
- That status includes:
  - `attached`
  - `role`
  - `orientation`
  - `togss_raw`
  - `vbusok`
- `rk_cdn_dp` has been updated locally to use that as the polarity fallback
  when the extcon handle path is missing.

Rule going forward:

- Risky USB-C/DP changes must survive a non-boot-critical test path before they are integrated into the kernel image.
- Keep the default `/boot/kernel` on the last known-good image.
- Treat `rk_cdn_dp` as module-first and stage-driven only.
- Advance one risky subsystem at a time: binding, then power, then handles, then clocks, then resets, then PHY, then AUX.
- Keep all future live iterations module-only unless a built-in kernel change is unavoidable.
- Preserve every failed test kernel as a backup and repair the SD card back to the known-good kernel before continuing.

New FreeBSD issue findings:

- arm64 standalone KMOD metadata loss
  Self-built arm64 modules produced by `make -C sys/modules/...` were loading
  into memory without registering full module metadata. The final `.ko` lacked
  the linker-set boundary symbols that working base modules carry:
  `__start_set_modmetadata_set`, `__stop_set_modmetadata_set`,
  `__start_set_sysctl_set`, `__stop_set_sysctl_set`,
  `__start_set_sysinit_set`, and `__stop_set_sysinit_set`.

  Observable symptoms:
  - `kldload rk_cdn_dp.ko` succeeded
  - no module event logs ran
  - no `hw.rk_cdn_dp.*` sysctls appeared
  - no single-child reprobe logic ran

  Local fix now under test:
  - added `sys/conf/ldscript.kmod.arm64`
  - rebuilt `rk_cdn_dp.ko`
  - verified the rebuilt module gained the missing linker-set symbols
  - verified module event logging now runs
  - verified the existing `dp@fec00000` node now binds as `rk_cdn_dp0`
  - verified staged per-device sysctls now appear as `dev.rk_cdn_dp.0.*`

- late OFW/FDT rebinding through core bus code is still unsafe
  Both broad and narrow `ofwbus` late-binding experiments destabilized test
  kernels, so they remain off the safe path even though the module-local
  single-child reprobe path now works once arm64 KMOD metadata is fixed.

Latest safe-kernel DP findings:

- A module-only stage-1 bypass now checks PMU readiness directly from `rk_cdn_dp`.
  If domain 21 is already on and `BUS_IDLE_ST[11]` / `BUS_IDLE_ACK[11]` are
  already clear, stage 1 skips the freezing `rk3399_power_enable_domain()`
  provider call entirely.
- With that bypass in place, the live safe-kernel module now reaches:
  - `dev.rk_cdn_dp.0.stage=1` cleanly
  - `dev.rk_cdn_dp.0.stage=2` cleanly
  - `dev.rk_cdn_dp.0.stage=3` cleanly
  - `dev.rk_cdn_dp.0.stage=4` cleanly
  - `dev.rk_cdn_dp.0.stage=5` cleanly
- Stage 6 is now explicitly `aux-probe` and requires:
  - `dev.rk_cdn_dp.0.allow_aux=1`
- After opening that gate, `dev.rk_cdn_dp.0.stage=6` drops to DDB with:
  - `external_abort()`
  - `generic_bs_r_4()`
- So the next target is not PMU or provider bring-up. It is the exact Cadence
  DP MMIO read performed first in AUX/HPD probe.

What not to do next:

- Do not keep retrying `kernel.test` built-in `rk3399_power` experiments. Even
  reduced provider-only test kernels still failed to complete boot.
- Do not go back to raw AUX register pokes as the main path. They were useful
  for narrowing the old crash frontier, but the real bring-up path is now the
  Cadence firmware/mailbox path.
- Do not assume `nphys` alone gives the correct USB-C lane/polarity state.
  FreeBSD now needs that information from `fusb302`/extcon before mailbox DPCD
  reads are expected to work.

Board-doc corrections from `/home/b1nc0d3x/RP64-DOCS`:

- RockPro64 USB-C video is fixed to `TYPEC0` / `USB3.0 PHY0`.
- `TYPEC1` / `USB3.0 PHY1` is wired out as the fixed USB 3.0 A-path on this
  board, not as an alternate USB-C DP connector.
- `CC2` therefore means "flipped orientation on TYPEC0", not "use port 1".
- `FUSB302B` is the external CC/Type-C controller on `I2C4`.
- `VBUS_TYPEC` is switched by `SY6280AAC` through `VCC5V0_TYPEC0_EN`.
- The SoC exposes explicit AUX polarity reversal pins:
  - `TYPEC0_AUXP_PD_PU`
  - `TYPEC0_AUXM_PU_PD`
  These confirm that orientation is supposed to be handled as AUX/lane flip on
  one port, not by switching DP controllers/PHYs.
- RK3399 has only one built-in DisplayPort controller shared by two Type-C
  PHYs. On RockPro64, the only board-valid DP target is `TYPEC0`.

Driver direction updated from those docs:

- `rk_cdn_dp` should default to `active_port=0` on RockPro64.
- FUSB302 orientation should feed `hostcap_flip`, not `active_port`.
- Any path that maps `CC2 -> active_port=1` on RockPro64 is wrong.

Latest stable `READ_DPCD` boundary result:

- With the corrected RockPro64 recipe:
  - `active_port=0`
  - `hostcap_flip=1`
  - `hpd_status=1`
  - `stage=12` passes cleanly
- The first real mailbox `READ_DPCD` command is now reached on the correct
  `TYPEC0` path.
- The current stable live discriminator is:
  - `mbox_last_send_written=0`
  - `mbox_last_empty=1`
  - `mbox_last_empty_after_send=1`
  - `mbox_last_header=0`
  - `mbox_last_keep_alive=43`
- Interpretation:
  - the Cadence firmware uCPU is still alive
  - no reply header is queued
  - `MAILBOX_EMPTY` remains asserted even immediately after the attempted send
  - the remaining blocker is still the first real `READ_DPCD` transaction
    boundary, now narrowed to the last missing precondition before a mailbox
    reply is produced

Boot-safety correction for `rk_typec_phy`:

- The first attempt to add the fuller RK3399 DP PHY behavior directly into the
  built-in `rk_typec_phy` boot path produced a non-booting kernel.
- That behavior is now being moved behind a boot-safe gate:
  - `hw.rk_typec_phy_dp_extras`
- Default must remain `0` so the system boots with the old safe PHY path.
- When explicitly enabled later, the gated extras apply:
  - FUSB302-based flip/orientation read
  - `typec_conn_dir`
  - external PSM select
  - `uphy_dp_sel`
  - DP A2 -> A0 transition
- This keeps the kernel boot-safe while preserving the missing Type-C PHY
  recipe needed for the next `READ_DPCD` test.

Late-load `fusb302` bridge result:

- A late-load helper module was added instead of putting more DP Alt Mode state
  into built-in `fusb302`:
  - `rk3399_fusb302_helper.ko`
- The helper exports the same getter name that `rk_cdn_dp` probes for:
  - `fusb302_get_dp_altmode_state(device_t,
    struct rk3399_typec_dp_altmode_status *)`
- The helper exposes post-boot sysctls:
  - `hw.rk3399_fusb302_helper.valid`
  - `hw.rk3399_fusb302_helper.dp_ready`
  - `hw.rk3399_fusb302_helper.usb_ss`
  - `hw.rk3399_fusb302_helper.pin_assignment`
  - `hw.rk3399_fusb302_helper.dp_status`
  - `hw.rk3399_fusb302_helper.get_count`
- On the stable `kernel.old` system, this module builds, loads, and is consumed
  by `rk_cdn_dp`.

What the helper path proved:

- With helper-fed legacy-good values:
  - `valid=1`
  - `dp_ready=1`
  - `usb_ss=0`
  - `pin_assignment=0x8`
  - `dp_status=0x9a`
- `rk_cdn_dp` consumes them successfully at `stage=12`:
  - `hw.rk3399_fusb302_helper.get_count=1`
  - `dev.rk_cdn_dp.0.dp_altmode_valid=1`
  - `dev.rk_cdn_dp.0.dp_altmode_ready=1`
  - `dev.rk_cdn_dp.0.dp_altmode_usb_ss=0`
  - `dev.rk_cdn_dp.0.dp_altmode_pin_assignment=8`
  - `dev.rk_cdn_dp.0.dp_altmode_status=154`
- `stage=12` still completes cleanly and preserves:
  - `hpd_status=1`
  - `last_error=0`

What `stage=13` still does with the helper:

- The helper is consumed again:
  - `hw.rk3399_fusb302_helper.get_count=2`
- But the first real `READ_DPCD` mailbox send still stalls at the same
  boundary:
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
  - `stage` falls back to `12`

Conclusion from that helper result:

- The remaining blocker is not:
  - built-in `fusb302` changes
  - helper-vs-built-in DP Alt Mode state plumbing
  - missing consumption of legacy-good `pin_assignment=0x8` /
    `dp_status=0x9a`
- The remaining blocker is still the deeper Cadence-side precondition behind
  the first real `READ_DPCD` mailbox send.

Eliminated FreeBSD boot theory:

- A dedicated A/B kernel test reverted only the built-in `fusb302` Alt Mode
  export/sysctl additions.
- That reverted kernel still hung during late boot after deep device attach.
- So the built-in `fusb302` export path is not the primary FreeBSD boot
  blocker.

Useful FUSB300C datasheet note:

- Public document:
  - `https://docs.rs-online.com/2c77/0900766b8145d60e.pdf`
  - `FUSB300C Programmable USB Type-C Controller`
- It confirms the FUSB300-class device is a thin client driven by host
  software over I2C and interrupts:
  - `WAKE`
  - `VBUSOK`
  - `BC_LVL`
  - `COMP`
- This supports the current diagnosis that real Type-C / Alt Mode behavior is
  event-driven, not just a set of final state values.
- It does not provide:
  - USB-PD Alt Mode details
  - Cadence mailbox details
  - RK3399 DP integration details

Post-boot helper plan:

- Do not continue modifying the early `rk_typec_phy` boot path.
- Use a loadable helper module instead:
  - `rk3399_tcphy_helper`
- The helper resolves `/phy@ff7c0000` and its `rockchip,grf` syscon after boot
  and exposes explicit sysctls to apply the RockPro64 TC-PHY GRF fields on
  demand:
  - `rockchip,typec-conn-dir`
  - `rockchip,external-psm`
  - `rockchip,uphy-dp-sel`
- This allows one-field-at-a-time testing before `stage=13` without changing
  the early driver dependency graph.

Native DTB result:

- The RockPro64 DTB was patched under `&tcphy0` to add:
  - `rockchip,typec-conn-dir`
  - `rockchip,external-psm`
  - `rockchip,uphy-dp-sel`
- After rebooting on that DTB, the old transmit-side blocker disappeared
  without the helper loaded:
  - `mbox_last_send_written=9`
  - `mbox_last_full=0`
- So those TC-PHY properties are a required native board precondition.

Remaining blocker after the DTB fix:

- The mailbox reply still never appears:
  - `mbox_last_empty=1`
  - `mbox_last_empty_after_send=1`
  - `mbox_last_header=0`
- That moved the frontier from "cannot send READ_DPCD" to
  "successful READ_DPCD send with no reply."

Later helper experiments that did not move the new frontier:

- `rk3399_tcphy_helper` was extended post-boot to test additional RK3399 PHY
  writes without touching the kernel:
  - `PMA_LANE_CFG = PIN_ASSIGN_C_E`
  - `PMA_LANE_CFG = PIN_ASSIGN_D_F`
  - `DP_MODE_CTL = DP_MODE_ENTER_A2`
  - `DP_MODE_CTL = DP_MODE_ENTER_A2` then `DP_MODE_ENTER_A0`
- All of those writes applied successfully and read back expected values, but
  none changed the live `stage=13` mailbox boundary:
  - `dev.rk_cdn_dp.0.stage=12`
  - `dev.rk_cdn_dp.0.mbox_last_send_written=0`
  - `dev.rk_cdn_dp.0.mbox_last_full=1`
  - `dev.rk_cdn_dp.0.mbox_last_empty=1`
- So the next missing ingredient is no longer basic TC-PHY field programming;
  it is higher-level Type-C / DP Alt Mode state or another PHY-side sequence
  beyond these direct register writes.

Detach/rebind crash note:

- A later crash reported:
  - `Fatal data abort`
  - `elr: rk_cdn_dp_mailbox_write + 0xac`
  - `far: 0x8`
  immediately after a `detached` message on serial.
- That points to a detach/rebind race in `rk_cdn_dp` where mailbox MMIO was
  touched after teardown began.
- Source-side mitigation applied:
  - disable the async module-load rebind task during staged bring-up
  - reject mailbox MMIO once a new `detached` flag is set in release/detach
