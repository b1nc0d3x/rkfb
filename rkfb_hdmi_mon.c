/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 B1nc0d3x
 * All rights reserved.
 *
 * RK3399 HDMI register change monitor — FreeBSD kernel module.
 *
 * The Synopsys DesignWare HDMI TX is a byte-register IP: each logical
 * register is 8 bits wide.  On RK3399 these are accessed as byte reads
 * over AXI; a 32-bit read to an unaligned offset will fault on aarch64.
 * This module therefore uses uint8_t throughout for all MMIO access.
 *
 * Tunables (set via loader.conf before kldload, RDTUN = read-once at load):
 *   hw.rkfb_hdmi_mon.base      — physical base  (default 0xFF940000)
 *   hw.rkfb_hdmi_mon.offset    — start offset   (default 0x0000)
 *   hw.rkfb_hdmi_mon.range     — byte range     (default 0x0200)
 *                                capped at 0x8000
 *   hw.rkfb_hdmi_mon.poll_hz   — polls/sec      (default 4, max 100)
 *
 * Runtime sysctl:
 *   hw.rkfb_hdmi_mon.dump=1    — print live values of key registers
 *
 * Usage:
 *   kldload ./rkfb_hdmi_mon.ko
 *   sysctl hw.rkfb_hdmi_mon.dump=1 && dmesg | tail -20
 *   kldunload rkfb_hdmi_mon
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
    "Start byte offset within base to monitor");
SYSCTL_UINT(_hw_rkfb_hdmi_mon, OID_AUTO, range,
    CTLFLAG_RDTUN, &mon_range, 0,
    "Byte range to monitor (max 0x8000)");
SYSCTL_INT(_hw_rkfb_hdmi_mon, OID_AUTO, poll_hz,
    CTLFLAG_RDTUN, &mon_poll_hz, 0,
    "Poll frequency in Hz (1-100)");

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */

MALLOC_DEFINE(M_RKFBMON, "rkfb_mon", "rkfb HDMI monitor shadow buffer");

static struct callout	 mon_callout;
static struct mtx	 mon_mtx;
static volatile vm_offset_t mon_va;	/* KVA of mapped MMIO window */
static uint8_t		*mon_shadow;	/* previous-value snapshot, one byte/reg */
static unsigned int	 mon_nbytes;	/* number of bytes watched = range */

/* -------------------------------------------------------------------------
 * MMIO accessor — byte read only.
 *
 * DW-HDMI is a byte-register IP.  Each logical register is 8 bits.
 * On aarch64 an unaligned 32-bit MMIO read panics with "Misaligned access
 * from kernel space" (ESR 0x96000021).  Always use uint8_t here.
 * ---------------------------------------------------------------------- */

static inline uint8_t
mon_read8(unsigned int byte_off)
{
	return (*(volatile uint8_t *)(mon_va + byte_off));
}

/* -------------------------------------------------------------------------
 * Callout — fires at mon_poll_hz, steps one byte at a time.
 * ---------------------------------------------------------------------- */

static void
mon_tick(void *arg __unused)
{
	unsigned int i;
	uint8_t      cur;

	for (i = 0; i < mon_nbytes; i++) {
		cur = mon_read8(mon_offset + i);
		if (cur != mon_shadow[i]) {
			printf("rkfb_hdmi_mon: +0x%04x  "
			    "0x%02x -> 0x%02x\n",
			    mon_offset + i,
			    (unsigned)mon_shadow[i],
			    (unsigned)cur);
			mon_shadow[i] = cur;
		}
	}

	callout_reset(&mon_callout, hz / mon_poll_hz, mon_tick, NULL);
}

/* -------------------------------------------------------------------------
 * On-demand dump sysctl — write 1 to trigger.
 * Prints the key registers live without waiting for a poll cycle.
 * ---------------------------------------------------------------------- */

static int
sysctl_hdmi_dump(SYSCTL_HANDLER_ARGS)
{
	int error, val;
	int i;

	val = 0;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL || val == 0)
		return (error);

	if (mon_va == 0) {
		printf("rkfb_hdmi_mon: not mapped\n");
		return (ENXIO);
	}

	printf("rkfb_hdmi_mon: --- live key register dump ---\n");
	for (i = 0; i < rkfb_hdmi_key_reg_count; i++) {
		uint32_t off = rkfb_hdmi_key_regs[i];
		/*
		 * Guard: only read offsets that fall within our mapped window.
		 * Key regs like 0x3004, 0x4000 are outside the default 0x200
		 * range but inside the 0x8000 max — safe if mon_range=0x8000.
		 */
		if (off >= mon_offset + mon_nbytes) {
			printf("  [0x%04x] -- outside mapped range --\n", off);
			continue;
		}
		printf("  [0x%04x] = 0x%02x\n",
		    off, (unsigned)mon_read8(off));
	}
	printf("rkfb_hdmi_mon: --- end dump ---\n");

	return (0);
}

SYSCTL_PROC(_hw_rkfb_hdmi_mon, OID_AUTO, dump,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_hdmi_dump, "I",
    "Write 1 to dump live values of key HDMI registers");

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
	if (range > MON_MAX_RANGE) {
		printf("rkfb_hdmi_mon: range 0x%x > max 0x%x, capping\n",
		    range, MON_MAX_RANGE);
		range = MON_MAX_RANGE;
	}

	/* Sanitise poll rate */
	if (mon_poll_hz < 1)   mon_poll_hz = 1;
	if (mon_poll_hz > 100) mon_poll_hz = 100;

	mon_nbytes = range;

	/*
	 * Map physical MMIO into KVA.
	 * We map from base+0 to base+(offset+range) so that
	 * mon_va+offset is the first byte we watch.
	 */
	mon_va = (vm_offset_t)pmap_mapdev(
	    (vm_paddr_t)mon_base,
	    (vm_size_t)(mon_offset + range));

	if (mon_va == 0) {
		printf("rkfb_hdmi_mon: pmap_mapdev(0x%lx, 0x%x) failed\n",
		    mon_base, mon_offset + range);
		return (ENOMEM);
	}

	/* Shadow buffer — one byte per watched offset */
	mon_shadow = malloc(mon_nbytes, M_RKFBMON, M_WAITOK | M_ZERO);

	/* Snapshot current state */
	for (i = 0; i < mon_nbytes; i++)
		mon_shadow[i] = mon_read8(mon_offset + i);

	mtx_init(&mon_mtx, "rkfb_hdmi_mon", NULL, MTX_DEF);
	callout_init_mtx(&mon_callout, &mon_mtx, 0);

	printf("rkfb_hdmi_mon: monitoring 0x%lx  "
	    "offset=0x%x  range=0x%x  (%u bytes)  @ %d Hz\n",
	    mon_base, mon_offset, range, mon_nbytes, mon_poll_hz);

	callout_reset(&mon_callout, hz / mon_poll_hz, mon_tick, NULL);
	return (0);
}

static int
rkfb_hdmi_mon_unload(void)
{
	callout_drain(&mon_callout);
	mtx_destroy(&mon_mtx);

	if (mon_shadow != NULL) {
		free(mon_shadow, M_RKFBMON);
		mon_shadow = NULL;
	}
	if (mon_va != 0) {
		pmap_unmapdev((void *)mon_va,
		    (vm_size_t)(mon_offset + mon_nbytes));
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

static moduledata_t rkfb_hdmi_mon_data = {
	"rkfb_hdmi_mon",
	rkfb_hdmi_mon_event,
	NULL,
};

DECLARE_MODULE(rkfb_hdmi_mon, rkfb_hdmi_mon_data,
    SI_SUB_DRIVERS, SI_ORDER_ANY);
MODULE_VERSION(rkfb_hdmi_mon, 2);
