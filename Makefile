KMOD=	rkfb
SRCS=	rkfb.c device_if.h bus_if.h ofw_bus_if.h

TOOLS=	cru_check force_route hdmi_bring_up hdmi_bringup hdmi_bringup2 hdmi_bringup4 hdmi_clk_fix hdmi_hpd_watch hdmi_write_test orchard_clk orchard_clksel orchard_east orchard_profile phy_conf_test phy_fullseq phy_i2c_matrix phy_i2c_rdbck phy_i2c_test phy_i2c_test2 phy_lock phy_lock_matrix phy_mmio_probe phy_rdbck phy_readback phy_restore_direct phy_rstz_test phy_test pmu_check pmu_orchard pmu_vio poll_route replay_vop_working snap_regs vop_diag vop_dump vop_iommu vop_mmu vop_overlay_only vop_scan_find vop_start vop_timing
TOOL_CFLAGS?=	-O2 -Wall

all: tools

tools:
	@set -e; for t in ${TOOLS}; do \
		${CC} ${TOOL_CFLAGS} -o $$t $$t.c; \
	done

clean-tools:
	rm -f ${TOOLS}

clean: clean-tools

.include <bsd.kmod.mk>
