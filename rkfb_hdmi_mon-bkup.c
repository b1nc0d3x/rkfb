/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 B1nc0d3x
 * All rights reserved.
 *
 * RK3399 HDMI register change monitor — FreeBSD kernel module.
 *
 * Polls a configurable base+offset+range window at a fixed interval and
 * prints any changed registers to the kernel log.  Designed for bring-up
 * work: plug/unplug HDMI and watch which registers move.
 *
 * Tunables (set via loader.conf or sysctl before/after load):
 *   hw.rkfb_hdmi_mon.poll_hz   — polls per second (default 4)
 *   hw.rkfb_hdmi_mon.base      — MMIO physical base  (default 0xFF940000)
 *   hw.rkfb_hdmi_mon.offset    — start offset within base (default 0)
 *   hw.rkfb_hdmi_mon.range     — byte range to watch (default 0x200)
 *                                must be a multiple of 4; capped at 0x8000
 *
 * Usage:
 *   kldload ./rkfb_hdmi_mon.ko           # load with defaults
 *   kldunload rkfb_hdmi_mon              # unload
 *
 *   # Override via loader.conf (before boot):
 *   hw.rkfb_hdmi_mon.range=0x8000
 *   hw.rkfb_hdmi_mon.poll_hz=10
 *
 *   # Watch dmesg:
 *   dmesg | grep rkfb_hdmi_mon
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/callout.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/bus.h>

#include "rkfb_hdmi_regs.h"

/* -------------------------------------------------------------------------
 * Key register table — owned here, extern-declared in the header.
 * ---------------------------------------------------------------------- */

const uint32_t rkfb_hdmi_key_regs[] = {
	RKFB_HDMI_REG_0000,
	RKFB_HDMI_REG_0004,
	RKFB_HDMI_REG_0008,
	RKFB_HDMI_REG_000C,
	RKFB_HDMI_REG_0010,
	RKFB_HDMI_REG_0014,
	RKFB_HDMI_REG_0018,
	RKFB_HDMI_REG_0100,
	RKFB_HDMI_REG_0104,
	RKFB_HDMI_REG_0108,
	RKFB_HDMI_REG_0180,
	RKFB_HDMI_REG_0184,
	RKFB_HDMI_REG_0188,
	RKFB_HDMI_REG_0200,
	RKFB_HDMI_REG_0800,
	RKFB_HDMI_REG_0804,
	RKFB_HDMI_REG_1000,
	RKFB_HDMI_REG_1008,
	RKFB_HDMI_REG_100A,
	RKFB_HDMI_REG_100E,
	RKFB_HDMI_REG_3000,
	RKFB_HDMI_REG_3004,
	RKFB_HDMI_REG_3026,
	RKFB_HDMI_REG_4000,
	RKFB_HDMI_REG_4001,
	RKFB_HDMI_REG_4002,
	RKFB_HDMI_REG_4005,
};

const int rkfb_hdmi_key_reg_count =
    (int)(sizeof(rkfb_hdmi_key_regs) / sizeof(rkfb_hdmi_key_regs[0]));

/* -------------------------------------------------------------------------
 * Tunables / sysctls
 * ---------------------------------------------------------------------- */

#define MON_DEFAULT_HZ		4
#define MON_DEFAULT_BASE	RKFB_HDMI_BASE
#define MON_DEFAULT_OFFSET	0x0000u
#define MON_DEFAULT_RANGE	0x0200u
#define MON_MAX_RANGE		0x8000u

static unsigned long	mon_base    = MON_DEFAULT_BASE;
static unsigned int	mon_offset  = MON_DEFAULT_OFFSET;
static unsigned int	mon_range   = MON_DEFAULT_RANGE;
static int		mon_poll_hz = MON_DEFAULT_HZ;

SYSCTL_NODE(_hw, OID_AUTO, rkfb_hdmi_mon, CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, "rkfb HDMI register monitor");

SYSCTL_ULONG(_hw_rkfb_hdmi_mon, OID_AUTO, base,
    CTLFLAG_RDTUN, &mon_base, 0,
    "HDMI MMIO physical base address");
SYSCTL_UINT(_hw_rkfb_hdmi_mon, OID_AUTO, offset,
    CTLFLAG_RDTUN, &mon_offset, 0,
    "Start offset within base to monitor");
SYSCTL_UINT(_hw_rkfb_hdmi_mon, OID_AUTO, range,
    CTLFLAG_RDTUN, &mon_range, 0,
    "Byte range to monitor (multiple of 4, max 0x8000)");
SYSCTL_INT(_hw_rkfb_hdmi_mon, OID_AUTO, poll_hz,
    CTLFLAG_RDTUN, &mon_poll_hz, 0,
    "Poll frequency in Hz");

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

MALLOC_DEFINE(M_RKFBMON, "rkfb_mon", "rkfb HDMI monitor shadow buffer");

static struct callout	 mon_callout;
static struct mtx	 mon_mtx;
static volatile vm_offset_t mon_va;	/* KVA of mapped MMIO window */
static uint32_t		*mon_shadow;	/* previous-value snapshot */
static unsigned int	 mon_nregs;	/* number of 32-bit words watched */

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/*
 * mon_read — read one 32-bit slot at byte offset `byte_off' from mon_va.
 *
 * The DW-HDMI on RK3399 is byte-addressed (each logical register is 8 bits
 * wide) but the AXI bus delivers 32-bit reads.  Only bits [7:0] carry HDMI
 * data; bits [31:8] read as zero.  Keeping the full 32-bit word in the
 * shadow buffer is fine — any unexpected non-zero upper bits will show up
 * as a diff and are interesting on their own during bring-up.
 */
static inline uint32_t
mon_read(unsigned int byte_off)
{
	return (*(volatile uint32_t *)(mon_va + byte_off));
}

/* -------------------------------------------------------------------------
 * Callout — fires at mon_poll_hz
 * ---------------------------------------------------------------------- */

static void
mon_tick(void *arg __unused)
{
	unsigned int i;
	uint32_t     cur;

	for (i = 0; i < mon_nregs; i++) {
		cur = mon_read(mon_offset + i * (unsigned int)sizeof(uint32_t));
		if (cur != mon_shadow[i]) {
			printf("rkfb_hdmi_mon: +0x%04x  "
			    "0x%08x -> 0x%08x\n",
			    mon_offset + i * (unsigned int)sizeof(uint32_t),
			    mon_shadow[i], cur);
			mon_shadow[i] = cur;
		}
	}

	callout_reset(&mon_callout, hz / mon_poll_hz, mon_tick, NULL);
}

/* -------------------------------------------------------------------------
 * Module load / unload
 * ---------------------------------------------------------------------- */

static int
rkfb_hdmi_mon_load(void)
{
	unsigned int range;
	unsigned int i;

	/* Sanitise range */
	range = mon_range;
	if (range == 0)
		range = MON_DEFAULT_RANGE;
	if (range & 3u) {
		printf("rkfb_hdmi_mon: range 0x%x not 4-byte aligned, "
		    "rounding up\n", range);
		range = (range + 3u) & ~3u;
	}
	if (range > MON_MAX_RANGE) {
		printf("rkfb_hdmi_mon: range 0x%x > max 0x%x, capping\n",
		    range, MON_MAX_RANGE);
		range = MON_MAX_RANGE;
	}

	/* Sanitise poll rate */
	if (mon_poll_hz < 1)   mon_poll_hz = 1;
	if (mon_poll_hz > 100) mon_poll_hz = 100;

	mon_nregs = range / (unsigned int)sizeof(uint32_t);

	/*
	 * Map the physical MMIO window into kernel virtual address space.
	 * We map from base 0 to (offset + range) so that mon_va + offset
	 * points at the first register we care about, and the KVA is stable
	 * for the lifetime of the module.
	 */
	mon_va = (vm_offset_t)pmap_mapdev(
	    (vm_paddr_t)mon_base,
	    (vm_size_t)(mon_offset + range));

	if (mon_va == 0) {
		printf("rkfb_hdmi_mon: pmap_mapdev(0x%lx, 0x%x) failed\n",
		    mon_base, mon_offset + range);
		return (ENOMEM);
	}

	/* Shadow buffer — one uint32_t per watched slot */
	mon_shadow = malloc(mon_nregs * sizeof(uint32_t),
	    M_RKFBMON, M_WAITOK | M_ZERO);

	/* Snapshot current state so the first tick only reports real changes */
	for (i = 0; i < mon_nregs; i++)
		mon_shadow[i] = mon_read(
		    mon_offset + i * (unsigned int)sizeof(uint32_t));

	mtx_init(&mon_mtx, "rkfb_hdmi_mon", NULL, MTX_DEF);
	callout_init_mtx(&mon_callout, &mon_mtx, 0);

	printf("rkfb_hdmi_mon: monitoring 0x%lx  "
	    "offset=0x%x  range=0x%x  (%u regs)  @ %d Hz\n",
	    mon_base, mon_offset, range, mon_nregs, mon_poll_hz);

	callout_reset(&mon_callout, hz / mon_poll_hz, mon_tick, NULL);
	return (0);
}

static int
rkfb_hdmi_mon_unload(void)
{
	/* callout_drain sleeps until any in-progress tick finishes */
	callout_drain(&mon_callout);
	mtx_destroy(&mon_mtx);

	if (mon_shadow != NULL) {
		free(mon_shadow, M_RKFBMON);
		mon_shadow = NULL;
	}
	if (mon_va != 0) {
		pmap_unmapdev((void *)mon_va,
		    (vm_size_t)(mon_offset +
		    mon_nregs * (unsigned int)sizeof(uint32_t)));
		mon_va = 0;
	}

	printf("rkfb_hdmi_mon: unloaded\n");
	return (0);
}

static int
rkfb_hdmi_mon_event(module_t mod __unused, int event, void *arg __unused)
{
	switch (event) {
	case MOD_LOAD:
		return (rkfb_hdmi_mon_load());
	case MOD_UNLOAD:
		return (rkfb_hdmi_mon_unload());
	default:
		return (EOPNOTSUPP);
	}
}


static int
sysctl_hdmi_dump(SYSCTL_HANDLER_ARGS)
{
  int error, val = 0;

  error = sysctl_handle_int(oidp, &val, 0, req);
  if (error || !req->newptr || val == 0)
    return (error);

  if (mon_va == 0) {
    printf("rkfb_hdmi_mon: not mapped\n");
    return (ENXIO);
  }

  printf("rkfb_hdmi_mon: --- live dump ---\n");
  printf("  IH_PHY_STAT0 [0x0104] = 0x%08x\n", mon_read(0x0104));
  printf("  IH_MUTE      [0x01ff] = 0x%08x\n", mon_read(0x01ff));
  printf("  PHY_STAT0    [0x3004] = 0x%08x\n", mon_read(0x3004));
  printf("  PHY_CONF0    [0x3000] = 0x%08x\n", mon_read(0x3000));
  printf("rkfb_hdmi_mon: --- end dump ---\n");

  return (0);
}

SYSCTL_PROC(_hw_rkfb_hdmi_mon, OID_AUTO, dump,
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, 0,
	    sysctl_hdmi_dump, "I",
	    "Write 1 to dump live HDMI register values");


static moduledata_t rkfb_hdmi_mon_data = {
	"rkfb_hdmi_mon",
	rkfb_hdmi_mon_event,
	NULL,
};

DECLARE_MODULE(rkfb_hdmi_mon, rkfb_hdmi_mon_data,
    SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(rkfb_hdmi_mon, 1);
