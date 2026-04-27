/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Minimal native FreeBSD FUSB302 Type-C controller driver.
 *
 * This first pass intentionally stops at:
 * - native iicbus/OFW attachment
 * - optional VBUS regulator control
 * - source-side CC toggle setup for RockPro64 debugging
 * - IRQ/status plumbing for live CC/VBUS visibility
 *
 * It does not implement USB-PD policy or DisplayPort Alt Mode by itself.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/intr.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <dev/regulator/regulator.h>

#include "iicbus_if.h"
#include "fusb302_var.h"

#define	FUSB_REG_DEVICE_ID		0x01
#define	FUSB_REG_CONTROL0		0x06
#define	 FUSB_CONTROL0_HOST_CUR_DEF	0x04
#define	FUSB_REG_CONTROL2		0x08
#define	 FUSB_CONTROL2_TOG_RD_ONLY	0x20
#define	 FUSB_CONTROL2_MODE_DFP		0x06
#define	 FUSB_CONTROL2_TOGGLE		0x01
#define	FUSB_REG_MASK1			0x0A
#define	FUSB_REG_POWER			0x0B
#define	 FUSB_POWER_ALL			0x07
#define	FUSB_REG_RESET			0x0C
#define	 FUSB_RESET_PD_RESET		0x02
#define	 FUSB_RESET_SW_RES		0x01
#define	FUSB_REG_MASKA			0x0E
#define	FUSB_REG_MASKB			0x0F
#define	FUSB_REG_STATUS0A		0x3C
#define	FUSB_REG_STATUS1A		0x3D
#define	FUSB_REG_INTERRUPTA		0x3E
#define	FUSB_REG_INTERRUPTB		0x3F
#define	FUSB_REG_STATUS0		0x40
#define	 FUSB_STATUS0_VBUSOK		0x80
#define	 FUSB_STATUS0_BC_LVL_MASK	0x03
#define	FUSB_REG_STATUS1		0x41
#define	FUSB_REG_INTERRUPT		0x42

#define	FUSB_MASK1_TOGGLE_DEBUG		0x7e
#define	FUSB_MASKA_TOGGLE_DEBUG		0xbf
#define	FUSB_MASKB_TOGGLE_DEBUG		0x01

#define	FUSB_STATUS1A_TOGSS_SHIFT	3
#define	FUSB_STATUS1A_TOGSS_MASK	0x38

struct fusb302_softc {
	device_t		dev;
	struct mtx		mtx;
	uint8_t			addr;
	int			irq_rid;
	struct resource		*irq_res;
	void			*irq_cookie;
	struct task		irq_task;
	regulator_t		vbus_supply;
	bool			vbus_enabled;
	bool			initialized;
	bool			state_valid;
	uint8_t			device_id;
	uint8_t			power;
	uint8_t			control2;
	uint8_t			status0;
	uint8_t			status1;
	uint8_t			status0a;
	uint8_t			status1a;
	enum fusb302_typec_orientation	orientation;
	enum fusb302_typec_role	role;
	bool			attached;
};

static struct ofw_compat_data compat_data[] = {
	{ "fcs,fusb302",	1 },
	{ NULL,			0 }
};
IICBUS_FDT_PNP_INFO(compat_data);

static int	fusb302_probe(device_t dev);
static int	fusb302_attach(device_t dev);
static int	fusb302_detach(device_t dev);

static int	fusb302_read_reg(struct fusb302_softc *sc, uint8_t reg,
    uint8_t *val);
static int	fusb302_write_reg(struct fusb302_softc *sc, uint8_t reg,
    uint8_t val);
static int	fusb302_refresh_state_locked(struct fusb302_softc *sc,
    bool *changedp);
static int	fusb302_clear_irqs_locked(struct fusb302_softc *sc,
    uint8_t *intr, uint8_t *intra, uint8_t *intrb);
static int	fusb302_init_locked(struct fusb302_softc *sc);
static void	fusb302_log_state(struct fusb302_softc *sc, const char *reason,
    uint8_t intr, uint8_t intra, uint8_t intrb);
static int	fusb302_try_ofw_irq(struct fusb302_softc *sc);
static void	fusb302_irq_task(void *context, int pending);
static void	fusb302_intr(void *context);
static int	fusb302_sysctl_reg(SYSCTL_HANDLER_ARGS);
static void	fusb302_add_sysctls(struct fusb302_softc *sc);

static const char *
fusb302_togss_name(uint8_t status1a)
{
	switch ((status1a & FUSB_STATUS1A_TOGSS_MASK) >>
	    FUSB_STATUS1A_TOGSS_SHIFT) {
	case 0:
		return ("running");
	case 1:
		return ("src-cc1");
	case 2:
		return ("src-cc2");
	case 5:
		return ("snk-cc1");
	case 6:
		return ("snk-cc2");
	case 7:
		return ("audio");
	default:
		return ("unknown");
	}
}

static uint8_t
fusb302_togss_raw(uint8_t status1a)
{
	return ((status1a & FUSB_STATUS1A_TOGSS_MASK) >>
	    FUSB_STATUS1A_TOGSS_SHIFT);
}

static enum fusb302_typec_orientation
fusb302_orientation(uint8_t status1a)
{
	switch (fusb302_togss_raw(status1a)) {
	case 1:
	case 5:
		return (FUSB302_TYPEC_ORIENT_CC1);
	case 2:
	case 6:
		return (FUSB302_TYPEC_ORIENT_CC2);
	case 7:
		return (FUSB302_TYPEC_ORIENT_UNKNOWN);
	case 0:
	default:
		return (FUSB302_TYPEC_ORIENT_NONE);
	}
}

static enum fusb302_typec_role
fusb302_role(uint8_t status1a)
{
	switch (fusb302_togss_raw(status1a)) {
	case 1:
	case 2:
		return (FUSB302_TYPEC_ROLE_SOURCE);
	case 5:
	case 6:
		return (FUSB302_TYPEC_ROLE_SINK);
	case 7:
		return (FUSB302_TYPEC_ROLE_ACCESSORY);
	case 0:
	default:
		return (FUSB302_TYPEC_ROLE_NONE);
	}
}

static int
fusb302_read_reg(struct fusb302_softc *sc, uint8_t reg, uint8_t *val)
{
	int error;

	error = iicdev_readfrom(sc->dev, reg, val, 1, IIC_WAIT);
	return (iic2errno(error));
}

static int
fusb302_write_reg(struct fusb302_softc *sc, uint8_t reg, uint8_t val)
{
	int error;

	error = iicdev_writeto(sc->dev, reg, &val, 1, IIC_WAIT);
	return (iic2errno(error));
}

static int
fusb302_clear_irqs_locked(struct fusb302_softc *sc, uint8_t *intr,
    uint8_t *intra, uint8_t *intrb)
{
	int error;

	error = fusb302_read_reg(sc, FUSB_REG_INTERRUPT, intr);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_INTERRUPTA, intra);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_INTERRUPTB, intrb);
	if (error != 0)
		return (error);

	return (0);
}

static int
fusb302_refresh_state_locked(struct fusb302_softc *sc, bool *changedp)
{
	uint8_t power, control2, status0, status1, status0a, status1a;
	int error;

	error = fusb302_read_reg(sc, FUSB_REG_POWER, &power);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_CONTROL2, &control2);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_STATUS0, &status0);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_STATUS1, &status1);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_STATUS0A, &status0a);
	if (error != 0)
		return (error);
	error = fusb302_read_reg(sc, FUSB_REG_STATUS1A, &status1a);
	if (error != 0)
		return (error);

	*changedp = !sc->state_valid ||
	    sc->power != power ||
	    sc->control2 != control2 ||
	    sc->status0 != status0 ||
	    sc->status1 != status1 ||
	    sc->status0a != status0a ||
	    sc->status1a != status1a;

	sc->power = power;
	sc->control2 = control2;
	sc->status0 = status0;
	sc->status1 = status1;
	sc->status0a = status0a;
	sc->status1a = status1a;
	sc->orientation = fusb302_orientation(status1a);
	sc->role = fusb302_role(status1a);
	sc->attached = (sc->role != FUSB302_TYPEC_ROLE_NONE);
	sc->state_valid = true;

	return (0);
}

static void
fusb302_log_state(struct fusb302_softc *sc, const char *reason, uint8_t intr,
    uint8_t intra, uint8_t intrb)
{
	uint8_t bc_lvl;
	int vbusok;

	bc_lvl = sc->status0 & FUSB_STATUS0_BC_LVL_MASK;
	vbusok = (sc->status0 & FUSB_STATUS0_VBUSOK) != 0;

	device_printf(sc->dev,
	    "%s: togss=%s attached=%d role=%d orient=%d vbusok=%d bc_lvl=%u "
	    "power=0x%02x c2=0x%02x "
	    "st0=0x%02x st1=0x%02x st0a=0x%02x st1a=0x%02x "
	    "irq=0x%02x irqa=0x%02x irqb=0x%02x\n",
	    reason, fusb302_togss_name(sc->status1a), sc->attached,
	    sc->role, sc->orientation, vbusok, bc_lvl,
	    sc->power, sc->control2, sc->status0, sc->status1,
	    sc->status0a, sc->status1a, intr, intra, intrb);
}

int
fusb302_get_typec_status(device_t dev, struct fusb302_typec_status *status)
{
	struct fusb302_softc *sc;

	if (dev == NULL || status == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
	status->attached = sc->attached;
	status->vbusok = ((sc->status0 & FUSB_STATUS0_VBUSOK) != 0);
	status->has_irq = (sc->irq_res != NULL);
	status->state_valid = sc->state_valid;
	status->togss_raw = fusb302_togss_raw(sc->status1a);
	status->orientation = sc->orientation;
	status->role = sc->role;
	mtx_unlock(&sc->mtx);

	return (status->state_valid ? 0 : ENXIO);
}

static int
fusb302_try_ofw_irq(struct fusb302_softc *sc)
{
	pcell_t *cells;
	phandle_t node, producer;
	rman_res_t count, start;
	int error, irq, ncells;

	/*
	 * Recover the IRQ directly from the FDT node when the ofw_iicbus
	 * resource list reaches us empty. This keeps the driver on the real
	 * interrupt path even when the bus failed to materialize it earlier.
	 */
	if (bus_get_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid, &start,
	    &count) == 0)
		return (EEXIST);

	node = ofw_bus_get_node(sc->dev);
	if (node <= 0)
		return (ENOENT);

	cells = NULL;
	error = ofw_bus_intr_by_rid(sc->dev, node, sc->irq_rid, &producer,
	    &ncells, &cells);
	if (error != 0)
		return (error);

	irq = ofw_bus_map_intr(sc->dev, producer, ncells, cells);
	free(cells, M_OFWPROP);
	if (irq <= 0)
		return (ENOENT);

	error = bus_set_resource(sc->dev, SYS_RES_IRQ, sc->irq_rid, irq, 1);
	if (error != 0)
		return (error);

	device_printf(sc->dev, "recovered irq resource %d from OFW\n", irq);
	return (0);
}

static int
fusb302_init_locked(struct fusb302_softc *sc)
{
	uint8_t intr, intra, intrb;
	bool changed;
	int error;

	error = fusb302_write_reg(sc, FUSB_REG_RESET, FUSB_RESET_SW_RES);
	if (error != 0)
		return (error);
	DELAY(1000);

	if (sc->vbus_supply != NULL && !sc->vbus_enabled) {
		error = regulator_enable(sc->vbus_supply);
		if (error != 0)
			return (error);
		sc->vbus_enabled = true;
	}

	error = fusb302_write_reg(sc, FUSB_REG_POWER, FUSB_POWER_ALL);
	if (error != 0)
		return (error);
	error = fusb302_write_reg(sc, FUSB_REG_CONTROL0,
	    FUSB_CONTROL0_HOST_CUR_DEF);
	if (error != 0)
		return (error);
	error = fusb302_write_reg(sc, FUSB_REG_MASK1, FUSB_MASK1_TOGGLE_DEBUG);
	if (error != 0)
		return (error);
	error = fusb302_write_reg(sc, FUSB_REG_MASKA, FUSB_MASKA_TOGGLE_DEBUG);
	if (error != 0)
		return (error);
	error = fusb302_write_reg(sc, FUSB_REG_MASKB, FUSB_MASKB_TOGGLE_DEBUG);
	if (error != 0)
		return (error);
	error = fusb302_clear_irqs_locked(sc, &intr, &intra, &intrb);
	if (error != 0)
		return (error);
	error = fusb302_write_reg(sc, FUSB_REG_CONTROL2,
	    FUSB_CONTROL2_TOG_RD_ONLY |
	    FUSB_CONTROL2_MODE_DFP |
	    FUSB_CONTROL2_TOGGLE);
	if (error != 0)
		return (error);
	DELAY(1000);

	error = fusb302_refresh_state_locked(sc, &changed);
	if (error != 0)
		return (error);

	sc->initialized = true;
	fusb302_log_state(sc, "initialized", intr, intra, intrb);
	return (0);
}

static void
fusb302_irq_task(void *context, int pending __unused)
{
	struct fusb302_softc *sc;
	uint8_t intr, intra, intrb;
	bool changed;
	int error;

	sc = context;

	mtx_lock(&sc->mtx);
	if (!sc->initialized) {
		mtx_unlock(&sc->mtx);
		return;
	}

	error = fusb302_clear_irqs_locked(sc, &intr, &intra, &intrb);
	if (error == 0)
		error = fusb302_refresh_state_locked(sc, &changed);
	if (error != 0) {
		device_printf(sc->dev, "irq refresh failed: %d\n", error);
		mtx_unlock(&sc->mtx);
		return;
	}
	if (intr != 0 || intra != 0 || intrb != 0 || changed)
		fusb302_log_state(sc, "event", intr, intra, intrb);
	mtx_unlock(&sc->mtx);
}

static void
fusb302_intr(void *context)
{
	struct fusb302_softc *sc;

	sc = context;
	taskqueue_enqueue(taskqueue_thread, &sc->irq_task);
}

static int
fusb302_sysctl_reg(SYSCTL_HANDLER_ARGS)
{
	struct fusb302_softc *sc;
	uint8_t reg, val;
	int ival, error;

	sc = arg1;
	reg = (uint8_t)arg2;

	mtx_lock(&sc->mtx);
	error = fusb302_read_reg(sc, reg, &val);
	mtx_unlock(&sc->mtx);
	if (error != 0)
		return (error);

	ival = val;
	return (sysctl_handle_int(oidp, &ival, 0, req));
}

static void
fusb302_add_sysctls(struct fusb302_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->dev);
	tree = device_get_sysctl_tree(sc->dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "device_id",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_DEVICE_ID, fusb302_sysctl_reg, "I",
	    "FUSB302 device ID register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "power",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_POWER, fusb302_sysctl_reg, "I",
	    "FUSB302 power register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "control2",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_CONTROL2, fusb302_sysctl_reg, "I",
	    "FUSB302 control2 register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "status0",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_STATUS0, fusb302_sysctl_reg, "I",
	    "FUSB302 status0 register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "status1",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_STATUS1, fusb302_sysctl_reg, "I",
	    "FUSB302 status1 register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "status0a",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_STATUS0A, fusb302_sysctl_reg, "I",
	    "FUSB302 status0a register");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "status1a",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc,
	    FUSB_REG_STATUS1A, fusb302_sysctl_reg, "I",
	    "FUSB302 status1a register");
}

static int
fusb302_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Fairchild FUSB302 Type-C controller");
	return (BUS_PROBE_DEFAULT);
}

static int
fusb302_attach(device_t dev)
{
	struct fusb302_softc *sc;
	rman_res_t irq_count, irq_start;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->addr = iicbus_get_addr(dev);
	sc->irq_rid = 0;
	mtx_init(&sc->mtx, device_get_nameunit(dev), NULL, MTX_DEF);
	TASK_INIT(&sc->irq_task, 0, fusb302_irq_task, sc);

	error = regulator_get_by_ofw_property(dev, 0, "vbus-supply",
	    &sc->vbus_supply);
	if (error != 0 && error != ENOENT) {
		device_printf(dev, "cannot get vbus-supply regulator: %d\n",
		    error);
		goto fail;
	}

	error = fusb302_read_reg(sc, FUSB_REG_DEVICE_ID, &sc->device_id);
	if (error != 0) {
		device_printf(dev, "cannot read device ID: %d\n", error);
		goto fail;
	}
	device_printf(dev, "device id 0x%02x at addr %#x\n",
	    sc->device_id, sc->addr);

	mtx_lock(&sc->mtx);
	error = fusb302_init_locked(sc);
	mtx_unlock(&sc->mtx);
	if (error != 0) {
		device_printf(dev, "cannot initialize controller: %d\n", error);
		goto fail;
	}

	if (bus_get_resource(dev, SYS_RES_IRQ, sc->irq_rid, &irq_start,
	    &irq_count) == 0) {
		device_printf(dev, "irq metadata start=%ju count=%ju rid=%d\n",
		    (uintmax_t)irq_start, (uintmax_t)irq_count, sc->irq_rid);
	}

	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		error = fusb302_try_ofw_irq(sc);
		if (error == 0) {
			sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
			    &sc->irq_rid, RF_ACTIVE);
		} else if (error != EEXIST && error != ENOENT) {
			device_printf(dev, "cannot recover OFW irq: %d\n", error);
		}
	}
	if (sc->irq_res == NULL &&
	    bus_get_resource(dev, SYS_RES_IRQ, sc->irq_rid, &irq_start,
	    &irq_count) == 0) {
		sc->irq_res = bus_alloc_resource(dev, SYS_RES_IRQ, &sc->irq_rid,
		    irq_start, irq_start, 1, RF_ACTIVE);
		if (sc->irq_res == NULL) {
			device_printf(dev, "explicit irq alloc failed for %ju\n",
			    (uintmax_t)irq_start);
			sc->irq_res = bus_alloc_resource(dev, SYS_RES_IRQ,
			    &sc->irq_rid, irq_start, irq_start, 1,
			    RF_ACTIVE | RF_SHAREABLE);
			if (sc->irq_res == NULL) {
				device_printf(dev,
				    "shareable irq alloc failed for %ju\n",
				    (uintmax_t)irq_start);
			}
		}
		if (sc->irq_res == NULL) {
			u_int clone_irq;

			clone_irq = intr_map_clone_irq((u_int)irq_start);
			device_printf(dev, "cloned irq %ju -> %u\n",
			    (uintmax_t)irq_start, clone_irq);
			error = bus_set_resource(dev, SYS_RES_IRQ, sc->irq_rid,
			    clone_irq, 1);
			if (error == 0) {
				sc->irq_res = bus_alloc_resource(dev,
				    SYS_RES_IRQ, &sc->irq_rid, clone_irq,
				    clone_irq, 1, RF_ACTIVE);
				if (sc->irq_res == NULL) {
					device_printf(dev,
					    "cloned irq alloc failed for %u\n",
					    clone_irq);
				}
			} else {
				device_printf(dev,
				    "cannot set cloned irq resource %u: %d\n",
				    clone_irq, error);
			}
		}
	}
	if (sc->irq_res != NULL) {
		error = bus_setup_intr(dev, sc->irq_res,
		    INTR_TYPE_MISC | INTR_MPSAFE, NULL, fusb302_intr, sc,
		    &sc->irq_cookie);
		if (error != 0) {
			device_printf(dev, "cannot setup irq: %d\n", error);
			bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid,
			    sc->irq_res);
			sc->irq_res = NULL;
			sc->irq_cookie = NULL;
		}
	} else {
		device_printf(dev, "no irq resource, using manual/sysctl reads\n");
	}

	fusb302_add_sysctls(sc);
	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), dev);
	return (0);

fail:
	if (sc->vbus_enabled) {
		regulator_disable(sc->vbus_supply);
		sc->vbus_enabled = false;
	}
	if (sc->vbus_supply != NULL)
		regulator_release(sc->vbus_supply);
	mtx_destroy(&sc->mtx);
	return (ENXIO);
}

static int
fusb302_detach(device_t dev)
{
	struct fusb302_softc *sc;

	sc = device_get_softc(dev);

	if (sc->irq_cookie != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_cookie);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, sc->irq_res);
	taskqueue_drain(taskqueue_thread, &sc->irq_task);
	if (sc->vbus_enabled && sc->vbus_supply != NULL)
		regulator_disable(sc->vbus_supply);
	if (sc->vbus_supply != NULL)
		regulator_release(sc->vbus_supply);
	OF_device_register_xref(OF_xref_from_node(ofw_bus_get_node(dev)), NULL);
	mtx_destroy(&sc->mtx);

	return (0);
}

static device_method_t fusb302_methods[] = {
	DEVMETHOD(device_probe,		fusb302_probe),
	DEVMETHOD(device_attach,	fusb302_attach),
	DEVMETHOD(device_detach,	fusb302_detach),

	DEVMETHOD_END
};

static driver_t fusb302_driver = {
	"fusb302",
	fusb302_methods,
	sizeof(struct fusb302_softc),
};

DRIVER_MODULE(fusb302, iicbus, fusb302_driver, 0, 0);
MODULE_DEPEND(fusb302, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(fusb302, 1);
