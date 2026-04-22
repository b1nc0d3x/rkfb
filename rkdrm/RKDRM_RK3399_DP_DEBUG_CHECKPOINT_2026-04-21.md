# RK3399 DP Debug Checkpoint 2026-04-21

## Scope

This checkpoint captures the current known-good boot path and the isolated
runtime test results for the RockPro64 / RK3399 DisplayPort-related drivers
that were implicated in board crashes:

- `rk_cdn_dp`
- `rk3399_power`
- `fusb302`

The immediate goal was to keep the board bootable while narrowing the failing
piece before folding anything back into `RP64KERN_RKDRM`.

## Known-Good Boot State

A reduced `RP64KERN_RKDRM` kernel was built and staged with these pieces kept
out of the kernel image:

- `rk_cdn_dp`
- `rk3399_power`
- `fusb302`

That kernel boots successfully on the board from `/boot/kernel` and is the
current recovery baseline.

The local kernel config used for that work is:

- `kernelconf/RP64KERN_RKDRM`

That config now also includes debugger support so crashes stop in-kernel
instead of disappearing into a silent reset path:

- `options DDB`
- `options KDB`
- `options KDB_TRACE`
- `options DDB_CTF`

## Board Debug Setup

Reference docs on the board:

- `/home/admin/rkfb/RP64-DOCS/Rockchip RK3399 TRM V1.3 Part2.pdf`
- `/home/admin/rkfb/RP64-DOCS/Rockchip_RK3399TRM_V1.4_Part1-20170408.pdf`
- `/home/admin/rkfb/RP64-DOCS/RK3399_Design_Guide_V1.0_20170420.pdf`

Crash-debugging was made persistent on the board:

- `debug.debugger_on_panic="1"` in `/boot/loader.conf.local`
- `dumpdev="AUTO"` in `/etc/rc.conf`
- `dumpon -l` reports `gpt/swap`
- `kern.shutdown.dumpdevname` reports `gpt/swap`

## Runtime Module Isolation Results

### `rk3399_power`

A modular test build of `rk3399_power` loads and attaches successfully at
runtime.

Observed result:

- `rk3399_power0` attaches cleanly
- loading it by itself does not reproduce the crash

Notes:

- the module copy used for testing removed hard module dependency metadata
  during bring-up and was rebuilt with exported symbols for follow-on tests
- because it is normally an `EARLY_DRIVER_MODULE`, a runtime `kldload` does
  not perfectly reproduce boot-time ordering, but it is still useful for
  isolating immediate attach faults

### `fusb302`

A standalone module wrapper was added for `fusb302` and the driver also loads
and attaches successfully at runtime.

Observed result:

- `fusb3020` attaches on `iicbus3`
- the controller identifies itself and initializes
- loading it by itself does not reproduce the crash

This removed `fusb302` from the short list of immediate single-driver panic
suspects.

### `rk_cdn_dp`

The original modular `rk_cdn_dp` path first failed on unresolved linkage to
`rk3399_power_enable_domain`. To keep isolating the problem, a diagnostic test
copy of `rk_cdn_dp` was used with the hard power-domain call bypassed during
modular testing.

With the optional AUX / MMIO probe still deferred, that diagnostic module
loads and reaches the main attach milestones successfully.

Observed diagnostic sequence:

- resources allocated
- clocks acquired: `core-clk`, `pclk`, `spdif`, `grf`
- resets acquired: `spdif`, `dptx`, `apb`, `core`
- PHY count discovered
- clocks, resets, and PHY enable sequence completes
- Cadence DP scaffold attaches

That means the basic `rk_cdn_dp` attach path is not the crash by itself.

## Reproduced Panic Condition

To push past the scaffold-only attach, the debug probe gate was enabled:

- `hw.rk_cdn_dp_attach_debug_probe=1`

Reloading the diagnostic `rk_cdn_dp` module with that gate enabled reproduced
an actual crash and dropped the board into `ddb` on serial.

Observed panic signature:

- `panic: Unhandled System Error`
- backtrace includes `getenv_quad()` / `getenv_int()` and `rk_cdn_dp_attach()`
- the board stops at `db>` with `debug.debugger_on_panic="1"`

Important interpretation:

This looks like an ARM `SError`, so the backtrace location may only be where
an earlier hardware fault became visible. In other words, the fault is likely
associated with the debug-probe path entered from `rk_cdn_dp_attach()`, but it
may have been armed by an earlier MMIO, clock, reset, PHY, or AUX-side access
rather than by the environment-variable read itself.

## Reintegration Step In Progress

After the module isolation pass, the next integration decision was to put
`rk3399_power` and `fusb302` back into the kernel while still keeping
`rk_cdn_dp` out.

Current state of that work:

- local `kernelconf/RP64KERN_RKDRM` now includes:
  - `device rk3399_power`
  - `device fusb302`
- repo overlay `rkdrm/freebsd-overlay/sys/arm64/conf/RP64KERN_RKDRM` was
  updated to match
- the updated config was copied to the board as
  `/home/admin/RP64KERN_RKDRM.codex`
- `/usr/src/sys/arm64/conf/RP64KERN_RKDRM` on the board was replaced with that
  config
- `make -j4 buildkernel KERNCONF=RP64KERN_RKDRM` completed successfully on the
  board
- `make installkernel KERNCONF=RP64KERN_RKDRM` was started next

This is the current checkpoint before any further `rk_cdn_dp` reintegration.

## 2026-04-22 USB-C IRQ Fix Follow-Up

After reintegrating `fusb302` into the kernel and rebuilding the board kernel,
the original USB-C interrupt-resource blocker was driven to a working fix.

What is now verified live:

- the board boots the rebuilt `RP64KERN_RKDRM` kernel with in-kernel
  `rk3399_power`, `rk_typec_phy`, `rk_drm`, and `fusb302`
- the `fusb302` child attaches and reports:
  - `fusb3020 ... irq 82 on iicbus3`
- descendant child IRQ allocation succeeds through the bus chain:
  - `rk_i2c3`
  - `ofwbus0`
  - `nexus0`
- descendant child IRQ activation succeeds
- descendant child `bus_setup_intr()` succeeds
- the critical live trace lines are:
  - `nexus0: irq alloc fusb3020 ... -> ok`
  - `ofwbus0: passthrough irq alloc fusb3020 ... -> ok`
  - `rk_i2c3: child irq alloc fusb3020 ... -> ok`
  - `rk_i2c3: child irq setup fusb3020 ... -> 0`
  - `bus_generic_setup_intr: -> 0`
- the board continues booting well past `fusb302` into later device attach
  stages such as `pwm`, `i2s`, `pcm0`, and `armv8crypto0`

Interpretation:

- this is no longer a DT/OFW interrupt-discovery problem
- this is no longer an IRQ reservation problem
- this is no longer an IRQ activation problem
- the earlier FreeBSD base-system bug was in descendant I2C child IRQ
  propagation through the Rockchip `rk_i2c` bus path

Local fix shape:

- add explicit descendant child IRQ bridging in the Rockchip `rk_i2c` driver
  for:
  - `BUS_ALLOC_RESOURCE()`
  - `BUS_ACTIVATE_RESOURCE()`
  - `BUS_SETUP_INTR()`

Current implication:

- if USB-C display is still not working after this fix, the remaining blocker
  has moved above the raw `fusb302` IRQ path into:
  - Type-C state handling
  - altmode / extcon negotiation
  - `rk_cdn_dp`
  - DRM connector bring-up

## 2026-04-22 `rp64dbg` Module-First `rk_cdn_dp` Result

The recovered `rp64dbg` board is now the active safe place for module-first DP
testing.  On this board the DT handoff is known-good and exposes the real
Cadence DP node in the live tree:

- `dp@fec00000 compat=rockchip,rk3399-cdn-dp`
- `typec-portc@22 compat=fcs,fusb302`
- `rk_typec_phy0`
- `rk_typec_phy1`

The original modular `rk_cdn_dp` blocker on `rp64dbg` was no longer DT
discovery.  It was module linkage:

- `rk3399_power_enable_domain` existed in `rk3399_power.ko` but was not
  exported to consumers
- `rk_cdn_dp.ko` also needed an explicit runtime dependency on
  `rk3399_power`

For the module-first path, the working setup became:

- rebuild `rk3399_power.ko` with `EXPORT_SYMS=rk3399_power_enable_domain`
- rebuild `rk_cdn_dp.ko` with:
  - `DRIVER_MODULE(..., ofwbus, ...)`
  - `MODULE_DEPEND(rk_cdn_dp, rk3399_power, 1, 1, 1)`
  - no modular `clk` / `hwreset` / `phy` dependency metadata in the test copy
- load in this order:
  - `clk`
  - `syscon`
  - `hwreset`
  - `phy`
  - `rk3399_power`
  - `rk_cdn_dp`

With that setup in place, serial capture on `rp64dbg` shows the real attach
path succeeding:

- `rk_cdn_dp0: probe: compat=rockchip,rk3399-cdn-dp status_okay=1`
- `rk_cdn_dp0: probe: compatible match accepted`
- `rk_cdn_dp0: <Rockchip RK3399 Cadence DisplayPort scaffold> ... on ofwbus0`
- `rk_cdn_dp0: attach-step: resources ok`
- `rk_cdn_dp0: attach-step: power-domain lookup ok provider=present id=21`
- `rk_cdn_dp0: attach-step: power-domain enable ok id=21`
- `rk_cdn_dp0: attach-step: clocks ok`
- `rk_cdn_dp0: attach-step: resets ok`
- `rk_cdn_dp0: attach-step: phys ok count=1`
- all four CDN-DP clocks enable successfully
- all four resets deassert successfully
- `phy_set_mode(0)` succeeds
- `phy_enable(0)` succeeds
- `rk_cdn_dp0: attach-step: enable sequence ok`
- `rk_cdn_dp0: Cadence DP scaffold attached: phys=1 extcon=yes irq=present`
- `rk_cdn_dp0: DP MMIO/AUX probe deferred; set hw.rk_cdn_dp_attach_debug_probe=1 for debug`

This is the new important boundary:

- the remaining blocker on `rp64dbg` is no longer module linkage
- it is no longer early attach sequencing through power / clocks / resets /
  PHY
- the next narrowing pass starts after scaffold attach, around:
  - AUX / DPCD reads
  - extcon / Type-C state use
  - link training
  - DRM connector progression

## Current Conclusions

What is known to be good enough so far:

- the reduced `RP64KERN_RKDRM` boot path is stable
- `rk3399_power` alone does not reproduce the crash
- `fusb302` alone does not reproduce the crash
- `rk_cdn_dp` scaffold attach succeeds through resource, clock, reset, and PHY
  setup when the debug probe is deferred
- `rk3399_power` and `fusb302` are the first candidates being folded back into
  the kernel image
- the original `fusb302` descendant IRQ allocation / activation / setup blocker
  is fixed locally in the Rockchip `rk_i2c` bus path

What remains suspect:

- the `rk_cdn_dp` debug/AUX/MMIO bring-up path after scaffold attach
- Type-C altmode / extcon state progression after `fusb302` interrupt setup
- possible ordering or readiness problems around DP controller register access
- possible asynchronous hardware fault surfacing after an earlier access

## Next Debug Steps

The next narrowing pass should focus only on the post-scaffold `rk_cdn_dp`
path, using the RK3399 TRM and design guide for register sequencing checks.

Recommended sequence:

1. Finish the current kernel install and reboot into the reintegrated image.
2. Verify the board remains stable with in-kernel `rk3399_power`,
   `rk_typec_phy`, and interrupt-driven `fusb302`.
3. Keep HDMI unplugged and continue USB-C-only bring-up so Type-C / DP state
   can be isolated without mixed display paths.
4. Inspect live logs for the next post-IRQ blocker in:
   - `fusb302`
   - Type-C role / altmode state
   - `rk_typec_phy`
   - `rk_cdn_dp`
   - `rk_drm`
5. Keep `rk_cdn_dp` out until the failing AUX / MMIO step is isolated.
6. Capture fuller `ddb` state from the next panic involving `rk_cdn_dp`:
   - `bt`
   - `show registers`
   - `show pcpu`
   - `show panic`
7. Add tighter diagnostics around the debug-probe path in the test copy of
   `rk_cdn_dp`, especially before and after:
   - HPD status reads
   - AUX initialization
   - DPCD capability reads
   - any forced Type-C / HPD path
8. Re-run with the smallest possible register-touch surface until the exact
   operation that arms the `SError` is identified.

## Local Test Artifacts

The current ad hoc test source used to instrument the modular DP path exists
locally as:

- `/home/b1nc0d3x/rk_cdn_dp_test.c`

It is a diagnostic copy derived from the FreeBSD `rk_cdn_dp` source and should
be treated as a temporary test harness rather than a final upstreamable patch.
