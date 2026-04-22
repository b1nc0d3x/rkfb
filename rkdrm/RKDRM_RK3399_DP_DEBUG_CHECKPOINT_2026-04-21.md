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

Board under test:

- host: `the dedicated debug board`
- ssh port: `the configured board SSH port`
- serial console: `the dedicated serial console at 1500000`

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

## Current Conclusions

What is known to be good enough so far:

- the reduced `RP64KERN_RKDRM` boot path is stable
- `rk3399_power` alone does not reproduce the crash
- `fusb302` alone does not reproduce the crash
- `rk_cdn_dp` scaffold attach succeeds through resource, clock, reset, and PHY
  setup when the debug probe is deferred
- `rk3399_power` and `fusb302` are the first candidates being folded back into
  the kernel image

What remains suspect:

- the `rk_cdn_dp` debug/AUX/MMIO bring-up path after scaffold attach
- possible ordering or readiness problems around DP controller register access
- possible asynchronous hardware fault surfacing after an earlier access

## Next Debug Steps

The next narrowing pass should focus only on the post-scaffold `rk_cdn_dp`
path, using the RK3399 TRM and design guide for register sequencing checks.

Recommended sequence:

1. Finish the current kernel install and reboot into the reintegrated image.
2. Verify the board remains stable with in-kernel `rk3399_power` and
   `fusb302`.
3. Keep `rk_cdn_dp` out until the failing AUX / MMIO step is isolated.
4. Capture fuller `ddb` state from the next panic involving `rk_cdn_dp`:
   - `bt`
   - `show registers`
   - `show pcpu`
   - `show panic`
5. Add tighter diagnostics around the debug-probe path in the test copy of
   `rk_cdn_dp`, especially before and after:
   - HPD status reads
   - AUX initialization
   - DPCD capability reads
   - any forced Type-C / HPD path
6. Re-run with the smallest possible register-touch surface until the exact
   operation that arms the `SError` is identified.

## Local Test Artifacts

The current ad hoc test source used to instrument the modular DP path exists
locally as:

- `/home/b1nc0d3x/rk_cdn_dp_test.c`

It is a diagnostic copy derived from the FreeBSD `rk_cdn_dp` source and should
be treated as a temporary test harness rather than a final upstreamable patch.
