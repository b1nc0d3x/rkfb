/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Kyle T. Crenshaw
 * All rights reserved.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/errno.h>

#include <arm/include/fdt.h>

#include <machine/bus.h>
#include <machine/cpufunc.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc.h>

#include "rk_drm.h"

#define HDMI_PHY_I2C_ADDR  0x69

#define RK_DRM_MODE_HFP             88
#define RK_DRM_MODE_HSYNC           44
#define RK_DRM_MODE_HBP            148
#define RK_DRM_MODE_VFP              4
#define RK_DRM_MODE_VSYNC            5
#define RK_DRM_MODE_VBP             36
#define RK_DRM_MODE_VIC             16

#define RK_DRM_SYS_GRF_GPIO4C_IOMUX 0x0e028
#define RK_DRM_SYS_GRF_SOC_CON20    0x6250
#define RK_DRM_GRF_HDMI_LCDC_SEL    (1u << 6)
#define RK_DRM_GRF_GPIO4C_I2C3HDMI  0x003f0005u

#define RK_DRM_VOP_DSP_HTOTAL_HS_END 0x0188
#define RK_DRM_VOP_DSP_HACT_ST_END   0x018c
#define RK_DRM_VOP_DSP_VTOTAL_VS_END 0x0190
#define RK_DRM_VOP_DSP_VACT_ST_END   0x0194
#define RK_DRM_VOP_POST_DSP_HACT_INFO 0x0170
#define RK_DRM_VOP_POST_DSP_VACT_INFO 0x0174
#define RK_DRM_VOP_SYS_CTRL_STANDBY    (1u << 22)
#define RK_DRM_VOP_SYS_CTRL_MMU_EN     (1u << 20)
#define RK_DRM_VOP_SYS_CTRL_ENABLE     (1u << 11)
#define RK_DRM_VOP_SYS_CTRL_RGB_EN     (1u << 12)
#define RK_DRM_VOP_SYS_CTRL_HDMI_EN    (1u << 13)
#define RK_DRM_VOP_SYS_CTRL_EDP_EN     (1u << 14)
#define RK_DRM_VOP_SYS_CTRL_MIPI_EN    (1u << 15)
#define RK_DRM_VOP_SYS_CTRL_MIPI_DUAL  (1u << 3)
#define RK_DRM_VOP_DSP_OUT_MODE_MASK 0x0000000fu
#define RK_DRM_VOP_DSP_OUT_MODE_AAAA 0x0000000fu
#define RK_DRM_VOP_DSP_CTRL1_HDMI_PIN_POL_MASK  (0x7u << 20)
#define RK_DRM_VOP_DSP_CTRL1_HDMI_PIN_POL_POS   (0x3u << 20)
#define RK_DRM_VOP_DSP_CTRL1_HDMI_DCLK_POL      (1u << 23)
#define RK_DRM_VOP_WIN0_LB_MODE_RGB_1920X5 (4u << 5)
#define RK_DRM_VOP_WIN0_DATA_FMT_XRGB8888 0x00000000u
#define RK_DRM_VOP_WIN0_CTRL0_ENABLE  (RK_DRM_VOP_WIN0_LB_MODE_RGB_1920X5 | \
    RK_DRM_VOP_WIN0_DATA_FMT_XRGB8888 | 0x00000001u)
#define RK_DRM_VOP_WIN0_CTRL2_PRIMARY 0x00000021u

#define RK_DRM_FB_DMA_LOWADDR_TEST    0x0fffffffu

#define RK_DRM_CRU_VPLL_CON0         0x00c0
#define RK_DRM_CRU_VPLL_CON1         0x00c4
#define RK_DRM_CRU_VPLL_CON2         0x00c8
#define RK_DRM_CRU_VPLL_CON3         0x00cc
#define RK_DRM_CRU_CLKGATE_CON10     0x0328
#define RK_DRM_CRU_CLKGATE_CON28     0x0370
#define RK_DRM_CRU_SOFTRST_CON17     0x0444
#define RK_DRM_CRU_DRESETN_VOP0_REQ  (1u << 8)
#define RK_DRM_CRU_VPLL_CON2_LOCK    (1u << 31)
#define RK_DRM_CRU_PLL_MODE_SLOW     (0u << 8)
#define RK_DRM_CRU_PLL_MODE_NORMAL   (1u << 8)
#define RK_DRM_CRU_PLL_DSMPD         (1u << 3)
#define RK_DRM_CRU_PLL_BYPASS        (1u << 1)
#define RK_DRM_CRU_PLL_POWER_DOWN    (1u << 0)
#define RK_DRM_VPLL_148500_FBDIV     99u
#define RK_DRM_VPLL_148500_REFDIV    4u
#define RK_DRM_VPLL_148500_POSTDIV1  4u
#define RK_DRM_VPLL_148500_POSTDIV2  1u
#define RK_DRM_CRU_CLKGATE_VOP0_MASK \
	((1u << 12) | (1u << 9) | (1u << 8))
#define RK_DRM_CRU_CLKGATE_VOPB_MASK \
	((1u << 7) | (1u << 6) | (1u << 5) | (1u << 4) | \
	 (1u << 3) | (1u << 2) | (1u << 1) | (1u << 0))

#define RK_DRM_PMU_PWRDN_CON         0x0014
#define RK_DRM_PMU_PWRDN_ST          0x0018
#define RK_DRM_PMU_BUS_IDLE_REQ      0x0060
#define RK_DRM_PMU_PD_VO             (1u << 20)
#define RK_DRM_PMU_IDLE_VOPL         (1u << 8)
#define RK_DRM_PMU_IDLE_VOPB         (1u << 7)

#define RK_DRM_PMUCRU_GATEDIS_CON0   0x0130
#define RK_DRM_PMUCRU_GATEDIS_VOPB   (1u << 19)

#define RK_DRM_HDMI_TX_INVID0        0x0200
#define RK_DRM_HDMI_VP_PR_CD         0x0801
#define RK_DRM_HDMI_VP_STUFF         0x0802
#define RK_DRM_HDMI_VP_REMAP         0x0803
#define RK_DRM_HDMI_VP_CONF          0x0804
#define RK_DRM_HDMI_FC_INVIDCONF     0x1000
#define RK_DRM_HDMI_FC_VSYNCINWIDTH  0x100d
#define RK_DRM_HDMI_FC_CTRLDUR       0x1011
#define RK_DRM_HDMI_FC_EXCTRLDUR     0x1012
#define RK_DRM_HDMI_FC_EXCTRLSPAC    0x1013
#define RK_DRM_HDMI_FC_CH0PREAM      0x1014
#define RK_DRM_HDMI_FC_CH1PREAM      0x1015
#define RK_DRM_HDMI_FC_CH2PREAM      0x1016
#define RK_DRM_HDMI_FC_AVICONF3      0x1017
#define RK_DRM_HDMI_FC_GCP           0x1018
#define RK_DRM_HDMI_FC_AVICONF0      0x1019
#define RK_DRM_HDMI_FC_AVICONF1      0x101a
#define RK_DRM_HDMI_FC_AVICONF2      0x101b
#define RK_DRM_HDMI_FC_AVIVID        0x101c
#define RK_DRM_HDMI_FC_PACKET_TX_EN  0x10e3
#define RK_DRM_HDMI_IH_I2CMPHY_STAT0 0x0108
#define RK_DRM_HDMI_PHY_CONF0        0x3000
#define RK_DRM_HDMI_PHY_STAT0        0x3004
#define RK_DRM_HDMI_PHY_I2CM_SLAVE   0x3020
#define RK_DRM_HDMI_PHY_I2CM_ADDRESS 0x3021
#define RK_DRM_HDMI_PHY_I2CM_DATAO_1 0x3022
#define RK_DRM_HDMI_PHY_I2CM_DATAO_0 0x3023
#define RK_DRM_HDMI_PHY_I2CM_OPERATION 0x3026
#define RK_DRM_HDMI_PHY_I2CM_INT     0x3027
#define RK_DRM_HDMI_PHY_I2CM_CTLINT  0x3028
#define RK_DRM_HDMI_PHY_I2CM_DIV     0x3029
#define RK_DRM_HDMI_PHY_I2CM_SOFTRSTZ 0x302a
#define RK_DRM_HDMI_PHY_I2CM_SS_HCNT1 0x302b
#define RK_DRM_HDMI_PHY_I2CM_SS_HCNT0 0x302c
#define RK_DRM_HDMI_PHY_I2CM_SS_LCNT1 0x302d
#define RK_DRM_HDMI_PHY_I2CM_SS_LCNT0 0x302e
#define RK_DRM_HDMI_PHY_I2CM_FS_HCNT1 0x302f
#define RK_DRM_HDMI_PHY_I2CM_FS_HCNT0 0x3030
#define RK_DRM_HDMI_PHY_I2CM_FS_LCNT1 0x3031
#define RK_DRM_HDMI_PHY_I2CM_FS_LCNT0 0x3032
#define RK_DRM_HDMI_PHY_I2CM_SDA_HOLD 0x3033
#define RK_DRM_HDMI_PHY_JTAG_CFG     0x3034
#define RK_DRM_HDMI_MC_CLKDIS        0x4001
#define RK_DRM_HDMI_MC_SWRSTZREQ     0x4002
#define RK_DRM_HDMI_MC_FLOWCTRL      0x4004
#define RK_DRM_HDMI_MC_PHYRSTZ       0x4005
#define RK_DRM_HDMI_MC_LOCKONCLOCK   0x4006
#define RK_DRM_HDMI_MC_HEACPHY_RST   0x4007
#define RK_DRM_HDMI_BASE_SFRDIVLOW   0x4018
#define RK_DRM_HDMI_BASE_SFRDIVHIGH  0x4019
#define RK_DRM_HDMI_A_HDCPCFG0       0x5000
#define RK_DRM_HDMI_A_HDCPCFG1       0x5001
#define RK_DRM_HDMI_A_VIDPOLCFG      0x5009
#define RK_DRM_HDMI_PKT_SEND_CTL     0x0640

#define RK_DRM_HDMI_PHY_CONF0_PDZ          (1u << 7)
#define RK_DRM_HDMI_PHY_CONF0_ENTMDS       (1u << 6)
#define RK_DRM_HDMI_PHY_CONF0_SVSRET       (1u << 5)
#define RK_DRM_HDMI_PHY_CONF0_PDDQ         (1u << 4)
#define RK_DRM_HDMI_PHY_CONF0_TXPWRON      (1u << 3)
#define RK_DRM_HDMI_PHY_CONF0_ENHPDRXSENSE (1u << 2)
#define RK_DRM_HDMI_PHY_CONF0_SELDATAENPOL (1u << 1)
#define RK_DRM_HDMI_PHY_CONF0_SELDIPIF     (1u << 0)
#define RK_DRM_HDMI_PHY_I2CM_DIV_DEFAULT      0x0b
#define RK_DRM_HDMI_PHY_I2CM_SS_HCNT0_DEFAULT 0x6c
#define RK_DRM_HDMI_PHY_I2CM_SS_LCNT0_DEFAULT 0x7f
#define RK_DRM_HDMI_PHY_I2CM_FS_HCNT0_DEFAULT 0x11
#define RK_DRM_HDMI_PHY_I2CM_FS_LCNT0_DEFAULT 0x24
#define RK_DRM_HDMI_PHY_I2CM_SDA_HOLD_DEFAULT 0x09
#define RK_DRM_HDMI_PHY_JTAG_CFG_I2C 0x11
#define RK_DRM_HDMI_BASE_SFRDIVLOW_DEFAULT  0x93
#define RK_DRM_HDMI_BASE_SFRDIVHIGH_DEFAULT 0x69
#define RK_DRM_HDMI_MC_SWRST_TMDS    (1u << 1)
#define RK_DRM_HDMI_MC_SWRST_PIXEL   (1u << 0)
#define RK_DRM_HDMI_MC_CLKDIS_CECCLK_DISABLE  (1u << 5)
#define RK_DRM_HDMI_FC_INVIDCONF_DVI_1080P60 0x70
#define RK_DRM_HDMI_FC_INVIDCONF_HDMI_1080P60 0x78
#define RK_DRM_HDMI_FC_AVICONF1_PICTURE_ASPECT_16_9 (2u << 4)
#define RK_DRM_HDMI_FC_PACKET_TX_EN_AVI (1u << 2)
#define RK_DRM_HDMI_FC_PACKET_TX_EN_GCP (1u << 1)
#define RK_DRM_HDMI_A_HDCPCFG0_HDMIDVI (1u << 0)
#define RK_DRM_HDMI_A_HDCPCFG1_SWRESETN          (1u << 0)
#define RK_DRM_HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE (1u << 1)
#define RK_DRM_HDMI_A_HDCPCFG1_PH2UPSHFTENC      (1u << 2)
#define RK_DRM_HDMI_A_HDCPCFG1_DEFAULT \
	(RK_DRM_HDMI_A_HDCPCFG1_SWRESETN | \
	 RK_DRM_HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE | \
	 RK_DRM_HDMI_A_HDCPCFG1_PH2UPSHFTENC)
#define RK_DRM_HDMI_A_VIDPOLCFG_DATAENPOL (1u << 4)
#define RK_DRM_HDMI_PKT_SEND_CTL_AVI_INFO_UP (1u << 6)
#define RK_DRM_HDMI_PKT_SEND_CTL_AVI_INFO_EN (1u << 2)

#define RK_DRM_HDMI_PHY_I2C_CKCALCTRL        0x05
#define RK_DRM_HDMI_PHY_I2C_CPCE_CTRL        0x06
#define RK_DRM_HDMI_PHY_I2C_CKSYMTXCTRL      0x09
#define RK_DRM_HDMI_PHY_I2C_VLEVCTRL         0x0e
#define RK_DRM_HDMI_PHY_I2C_CURRCTRL         0x10
#define RK_DRM_HDMI_PHY_I2C_PLLPHBYCTRL      0x13
#define RK_DRM_HDMI_PHY_I2C_GMPCTRL          0x15
#define RK_DRM_HDMI_PHY_I2C_MSM_CTRL         0x17
#define RK_DRM_HDMI_PHY_I2C_TXTERM           0x19
#define RK_DRM_HDMI_PHY_I2C_CKCALCTRL_OVERRIDE 0x8000
#define RK_DRM_HDMI_PHY_148500_CPCE_CTRL     0x0051
#define RK_DRM_HDMI_PHY_148500_GMPCTRL       0x0003
#define RK_DRM_HDMI_PHY_148500_CURRCTRL      0x0000
#define RK_DRM_HDMI_PHY_148500_MSM_CTRL      0x0006
#define RK_DRM_HDMI_PHY_148500_TXTERM        0x0004
#define RK_DRM_HDMI_PHY_148500_CKSYMTXCTRL   0x802b
#define RK_DRM_HDMI_PHY_148500_VLEVCTRL      0x028d

static inline uint32_t
rk_drm_vop_read4(struct rk_drm_softc *sc, size_t off)
{
	return (bus_space_read_4(fdtbus_bs_tag, sc->vop_bsh, off));
}

static inline void
rk_drm_vop_write4(struct rk_drm_softc *sc, size_t off, uint32_t val)
{
	bus_space_write_4(fdtbus_bs_tag, sc->vop_bsh, off, val);
	bus_space_barrier(fdtbus_bs_tag, sc->vop_bsh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
rk_drm_grf_read4(struct rk_drm_softc *sc, size_t off)
{
	return (bus_space_read_4(fdtbus_bs_tag, sc->grf_bsh, off));
}

static inline void
rk_drm_grf_write4(struct rk_drm_softc *sc, size_t off, uint32_t val)
{
	bus_space_write_4(fdtbus_bs_tag, sc->grf_bsh, off, val);
	bus_space_barrier(fdtbus_bs_tag, sc->grf_bsh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
rk_drm_pmu_read4(struct rk_drm_softc *sc, size_t off)
{
	return (bus_space_read_4(fdtbus_bs_tag, sc->pmu_bsh, off));
}

static inline void
rk_drm_pmu_write4(struct rk_drm_softc *sc, size_t off, uint32_t val)
{
	bus_space_write_4(fdtbus_bs_tag, sc->pmu_bsh, off, val);
	bus_space_barrier(fdtbus_bs_tag, sc->pmu_bsh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
rk_drm_pmucru_read4(struct rk_drm_softc *sc, size_t off)
{
	return (bus_space_read_4(fdtbus_bs_tag, sc->pmucru_bsh, off));
}

static inline void
rk_drm_pmucru_write4(struct rk_drm_softc *sc, size_t off, uint32_t val)
{
	bus_space_write_4(fdtbus_bs_tag, sc->pmucru_bsh, off, val);
	bus_space_barrier(fdtbus_bs_tag, sc->pmucru_bsh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
rk_drm_cru_read4(struct rk_drm_softc *sc, size_t off)
{
	return (bus_space_read_4(fdtbus_bs_tag, sc->cru_bsh, off));
}

static inline void
rk_drm_cru_write4(struct rk_drm_softc *sc, size_t off, uint32_t val)
{
	bus_space_write_4(fdtbus_bs_tag, sc->cru_bsh, off, val);
	bus_space_barrier(fdtbus_bs_tag, sc->cru_bsh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline uint8_t
rk_drm_hdmi_read1(struct rk_drm_softc *sc, size_t off)
{
	volatile uint32_t *reg;

	reg = (volatile uint32_t *)(sc->hdmi_va + (off << 2));
	return ((uint8_t)(*reg & 0xff));
}

static inline void
rk_drm_hdmi_write1(struct rk_drm_softc *sc, size_t off, uint8_t val)
{
	volatile uint32_t *reg;

	reg = (volatile uint32_t *)(sc->hdmi_va + (off << 2));
	*reg = val;
	__asm volatile("dsb sy" ::: "memory");
	__asm volatile("isb" ::: "memory");
}

static inline void
rk_drm_hdmi_write1_safe(struct rk_drm_softc *sc, size_t off, uint8_t val)
{
	uint64_t daif;

	__asm volatile("mrs %0, daif" : "=r"(daif));
	__asm volatile("msr daifset, #4");
	rk_drm_hdmi_write1(sc, off, val);
	__asm volatile("msr daif, %0" :: "r"(daif));
}

static void
rk_drm_fb_dma_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *fb_busaddr;

	fb_busaddr = arg;
	if (error != 0 || nseg != 1)
		return;
	*fb_busaddr = segs[0].ds_addr;
}

static int
rk_drm_fb_alloc(struct rk_drm_softc *sc)
{
	const size_t alloc_size = round_page(sc->fb_size);
	bus_addr_t fb_busaddr;
	void *fb_kva;
	int error;

	error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
	    RK_DRM_FB_DMA_LOWADDR_TEST, BUS_SPACE_MAXADDR,
	    NULL, NULL, alloc_size, 1, alloc_size, 0,
	    NULL, NULL, &sc->fb_dma_tag);
	if (error != 0)
		return (error);

	error = bus_dmamem_alloc(sc->fb_dma_tag, &fb_kva,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->fb_dma_map);
	if (error != 0) {
		bus_dma_tag_destroy(sc->fb_dma_tag);
		sc->fb_dma_tag = NULL;
		return (error);
	}

	fb_busaddr = 0;
	error = bus_dmamap_load(sc->fb_dma_tag, sc->fb_dma_map, fb_kva,
	    alloc_size, rk_drm_fb_dma_cb, &fb_busaddr, BUS_DMA_WAITOK);
	if (error != 0 || fb_busaddr == 0) {
		bus_dmamem_free(sc->fb_dma_tag, fb_kva, sc->fb_dma_map);
		bus_dma_tag_destroy(sc->fb_dma_tag);
		sc->fb_dma_map = NULL;
		sc->fb_dma_tag = NULL;
		return (error != 0 ? error : ENXIO);
	}

	sc->fb_va = (vm_offset_t)fb_kva;
	sc->fb_pa = (vm_paddr_t)fb_busaddr;
	return (0);
}

static void
rk_drm_fb_free(struct rk_drm_softc *sc)
{
	if (sc->fb_dma_tag != NULL) {
		if (sc->fb_dma_map != NULL && sc->fb_va != 0)
			bus_dmamap_unload(sc->fb_dma_tag, sc->fb_dma_map);
		if (sc->fb_va != 0)
			bus_dmamem_free(sc->fb_dma_tag, (void *)sc->fb_va,
			    sc->fb_dma_map);
		bus_dma_tag_destroy(sc->fb_dma_tag);
	}
	sc->fb_dma_map = NULL;
	sc->fb_dma_tag = NULL;
	sc->fb_va = 0;
	sc->fb_pa = 0;
}

static void
rk_drm_fb_fill(struct rk_drm_softc *sc, uint32_t color)
{
	uint32_t *fb32;
	size_t i, words;

	fb32 = (uint32_t *)sc->fb_va;
	words = sc->fb_size / sizeof(uint32_t);
	for (i = 0; i < words; i++)
		fb32[i] = color;

	if (sc->fb_dma_tag != NULL && sc->fb_dma_map != NULL)
		bus_dmamap_sync(sc->fb_dma_tag, sc->fb_dma_map,
		    BUS_DMASYNC_PREWRITE);
	else
		cpu_dcache_wb_range((void *)sc->fb_va, round_page(sc->fb_size));
}

static void
rk_drm_display_domain_sanity(struct rk_drm_softc *sc)
{
	uint32_t pwrdn_con, pwrdn_st, idle_req, gatedis0;
	int i;

	pwrdn_con = rk_drm_pmu_read4(sc, RK_DRM_PMU_PWRDN_CON);
	pwrdn_st = rk_drm_pmu_read4(sc, RK_DRM_PMU_PWRDN_ST);
	idle_req = rk_drm_pmu_read4(sc, RK_DRM_PMU_BUS_IDLE_REQ);
	gatedis0 = rk_drm_pmucru_read4(sc, RK_DRM_PMUCRU_GATEDIS_CON0);

	if ((pwrdn_st & RK_DRM_PMU_PD_VO) != 0) {
		rk_drm_pmu_write4(sc, RK_DRM_PMU_PWRDN_CON,
		    pwrdn_con & ~RK_DRM_PMU_PD_VO);
		for (i = 0; i < 1000; i++) {
			pwrdn_st = rk_drm_pmu_read4(sc, RK_DRM_PMU_PWRDN_ST);
			if ((pwrdn_st & RK_DRM_PMU_PD_VO) == 0)
				break;
			DELAY(10);
		}
	}

	if ((idle_req & (RK_DRM_PMU_IDLE_VOPB | RK_DRM_PMU_IDLE_VOPL)) != 0)
		rk_drm_pmu_write4(sc, RK_DRM_PMU_BUS_IDLE_REQ,
		    idle_req & ~(RK_DRM_PMU_IDLE_VOPB | RK_DRM_PMU_IDLE_VOPL));

	if ((gatedis0 & RK_DRM_PMUCRU_GATEDIS_VOPB) == 0)
		rk_drm_pmucru_write4(sc, RK_DRM_PMUCRU_GATEDIS_CON0,
		    gatedis0 | RK_DRM_PMUCRU_GATEDIS_VOPB);

	rk_drm_cru_write4(sc, RK_DRM_CRU_CLKGATE_CON10,
	    (RK_DRM_CRU_CLKGATE_VOP0_MASK << 16));
	rk_drm_cru_write4(sc, RK_DRM_CRU_CLKGATE_CON28,
	    (RK_DRM_CRU_CLKGATE_VOPB_MASK << 16));
}

static void
rk_drm_route_vop_to_hdmi(struct rk_drm_softc *sc)
{
	rk_drm_grf_write4(sc, RK_DRM_SYS_GRF_SOC_CON20,
	    (RK_DRM_GRF_HDMI_LCDC_SEL << 16));
	rk_drm_grf_write4(sc, RK_DRM_SYS_GRF_GPIO4C_IOMUX,
	    RK_DRM_GRF_GPIO4C_I2C3HDMI);
}

static int
rk_drm_program_vpll_148500khz(struct rk_drm_softc *sc)
{
	const uint32_t con3_mask = (0x3u << 8) | RK_DRM_CRU_PLL_DSMPD |
	    RK_DRM_CRU_PLL_BYPASS | RK_DRM_CRU_PLL_POWER_DOWN;
	const uint32_t con1_mask = (0x7u << 12) | (0x7u << 8) | 0x3fu;
	uint32_t con3;
	int i;

	con3 = RK_DRM_CRU_PLL_MODE_SLOW | RK_DRM_CRU_PLL_DSMPD |
	    RK_DRM_CRU_PLL_POWER_DOWN;
	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON3, (con3_mask << 16) | con3);
	DELAY(2);

	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON0,
	    (0x0fffu << 16) | RK_DRM_VPLL_148500_FBDIV);
	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON1,
	    (con1_mask << 16) |
	    (RK_DRM_VPLL_148500_POSTDIV2 << 12) |
	    (RK_DRM_VPLL_148500_POSTDIV1 << 8) |
	    RK_DRM_VPLL_148500_REFDIV);
	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON2, 0x00000000);

	con3 = RK_DRM_CRU_PLL_MODE_SLOW | RK_DRM_CRU_PLL_DSMPD;
	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON3, (con3_mask << 16) | con3);

	for (i = 0; i < 5000; i++) {
		if ((rk_drm_cru_read4(sc, RK_DRM_CRU_VPLL_CON2) &
		    RK_DRM_CRU_VPLL_CON2_LOCK) != 0)
			break;
		DELAY(10);
	}
	if (i == 5000)
		return (ETIMEDOUT);

	con3 = RK_DRM_CRU_PLL_MODE_NORMAL | RK_DRM_CRU_PLL_DSMPD;
	rk_drm_cru_write4(sc, RK_DRM_CRU_VPLL_CON3, (con3_mask << 16) | con3);
	return (0);
}

static void
rk_drm_vop_pulse_dclk_reset(struct rk_drm_softc *sc)
{
	rk_drm_cru_write4(sc, RK_DRM_CRU_SOFTRST_CON17,
	    (RK_DRM_CRU_DRESETN_VOP0_REQ << 16) | RK_DRM_CRU_DRESETN_VOP0_REQ);
	DELAY(1000);
	rk_drm_cru_write4(sc, RK_DRM_CRU_SOFTRST_CON17,
	    (RK_DRM_CRU_DRESETN_VOP0_REQ << 16));
	DELAY(1000);
}

static void
rk_drm_vop_init_1080p60(struct rk_drm_softc *sc)
{
	uint32_t sys_ctrl, dsp_ctrl0, dsp_ctrl1;

	if (rk_drm_program_vpll_148500khz(sc) != 0)
		device_printf(sc->dev, "VPLL setup failed, continuing\n");

	rk_drm_cru_write4(sc, 0x01bc,
	    ((((0x1fu << 8) | (0x3u << 6) | 0x1fu) << 16) |
	    ((3u << 8) | (1u << 6) | 1u)));
	rk_drm_cru_write4(sc, 0x01c4,
	    ((((1u << 11) | (0x3u << 8) | 0xffu) << 16) | 0x0000u));

	sys_ctrl = rk_drm_vop_read4(sc, 0x0008);
	dsp_ctrl0 = rk_drm_vop_read4(sc, 0x0010);
	dsp_ctrl1 = rk_drm_vop_read4(sc, 0x0014);

	sys_ctrl &= ~(RK_DRM_VOP_SYS_CTRL_STANDBY |
	    RK_DRM_VOP_SYS_CTRL_MMU_EN |
	    RK_DRM_VOP_SYS_CTRL_EDP_EN |
	    RK_DRM_VOP_SYS_CTRL_MIPI_EN |
	    RK_DRM_VOP_SYS_CTRL_MIPI_DUAL);
	sys_ctrl |= RK_DRM_VOP_SYS_CTRL_ENABLE |
	    RK_DRM_VOP_SYS_CTRL_RGB_EN |
	    RK_DRM_VOP_SYS_CTRL_HDMI_EN;
	rk_drm_vop_write4(sc, 0x0008, sys_ctrl);

	dsp_ctrl0 &= ~RK_DRM_VOP_DSP_OUT_MODE_MASK;
	dsp_ctrl0 |= RK_DRM_VOP_DSP_OUT_MODE_AAAA;
	rk_drm_vop_write4(sc, 0x0010, dsp_ctrl0);

	dsp_ctrl1 &= ~(RK_DRM_VOP_DSP_CTRL1_HDMI_PIN_POL_MASK |
	    RK_DRM_VOP_DSP_CTRL1_HDMI_DCLK_POL);
	dsp_ctrl1 |= RK_DRM_VOP_DSP_CTRL1_HDMI_PIN_POL_POS |
	    RK_DRM_VOP_DSP_CTRL1_HDMI_DCLK_POL;
	rk_drm_vop_write4(sc, 0x0014, dsp_ctrl1);

	rk_drm_vop_write4(sc, 0x0038, 0x00000000);
	rk_drm_vop_write4(sc, 0x003c, sc->stride / 4);
	rk_drm_vop_write4(sc, 0x0040, (uint32_t)sc->fb_pa);
	rk_drm_vop_write4(sc, 0x0048,
	    ((RK_DRM_MODE_HEIGHT - 1) << 16) | (RK_DRM_MODE_WIDTH - 1));
	rk_drm_vop_write4(sc, 0x004c,
	    ((RK_DRM_MODE_HEIGHT - 1) << 16) | (RK_DRM_MODE_WIDTH - 1));
	rk_drm_vop_write4(sc, 0x0050,
	    ((RK_DRM_MODE_VSYNC + RK_DRM_MODE_VBP) << 16) |
	    (RK_DRM_MODE_HSYNC + RK_DRM_MODE_HBP));
	rk_drm_vop_write4(sc, 0x006c, RK_DRM_VOP_WIN0_CTRL2_PRIMARY);
	rk_drm_vop_write4(sc, RK_DRM_VOP_POST_DSP_HACT_INFO,
	    ((RK_DRM_MODE_HSYNC + RK_DRM_MODE_HBP) << 16) |
	    (RK_DRM_MODE_HSYNC + RK_DRM_MODE_HBP + RK_DRM_MODE_WIDTH));
	rk_drm_vop_write4(sc, RK_DRM_VOP_POST_DSP_VACT_INFO,
	    ((RK_DRM_MODE_VSYNC + RK_DRM_MODE_VBP) << 16) |
	    (RK_DRM_MODE_VSYNC + RK_DRM_MODE_VBP + RK_DRM_MODE_HEIGHT));
	rk_drm_vop_write4(sc, 0x0030, RK_DRM_VOP_WIN0_CTRL0_ENABLE);
	rk_drm_vop_write4(sc, RK_DRM_VOP_DSP_HTOTAL_HS_END,
	    (2200 << 16) | RK_DRM_MODE_HSYNC);
	rk_drm_vop_write4(sc, RK_DRM_VOP_DSP_HACT_ST_END,
	    ((RK_DRM_MODE_HSYNC + RK_DRM_MODE_HBP) << 16) |
	    (RK_DRM_MODE_HSYNC + RK_DRM_MODE_HBP + RK_DRM_MODE_WIDTH));
	rk_drm_vop_write4(sc, RK_DRM_VOP_DSP_VTOTAL_VS_END,
	    (1125 << 16) | RK_DRM_MODE_VSYNC);
	rk_drm_vop_write4(sc, RK_DRM_VOP_DSP_VACT_ST_END,
	    ((RK_DRM_MODE_VSYNC + RK_DRM_MODE_VBP) << 16) |
	    (RK_DRM_MODE_VSYNC + RK_DRM_MODE_VBP + RK_DRM_MODE_HEIGHT));
	rk_drm_vop_write4(sc, 0x0000, 0x00000001);
	rk_drm_vop_pulse_dclk_reset(sc);
	DELAY(40000);
}

int
rk_drm_hw_set_scanout(struct rk_drm_softc *sc, vm_paddr_t paddr, uint32_t stride)
{
	if (!sc->hw_attached)
		return (ENXIO);
	if (paddr == 0 || stride == 0)
		return (EINVAL);

	rk_drm_vop_write4(sc, 0x003c, stride / 4);
	rk_drm_vop_write4(sc, 0x0040, (uint32_t)paddr);
	rk_drm_vop_write4(sc, 0x0000, 0x00000001);
	return (0);
}

static void
rk_drm_hdmi_toggle_main_reset(struct rk_drm_softc *sc, uint8_t mask)
{
	uint8_t reg;

	reg = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_MC_SWRSTZREQ);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_SWRSTZREQ, reg & ~mask);
	DELAY(10);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_SWRSTZREQ, reg | mask);
	DELAY(10);
}

static void
rk_drm_hdmi_clear_overflow(struct rk_drm_softc *sc)
{
	uint8_t swrstz;
	uint8_t invidconf;
	int i;

	swrstz = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_MC_SWRSTZREQ);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_SWRSTZREQ,
	    swrstz & ~RK_DRM_HDMI_MC_SWRST_TMDS);
	invidconf = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_FC_INVIDCONF);
	for (i = 0; i < 4; i++)
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_INVIDCONF, invidconf);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_SWRSTZREQ,
	    swrstz | RK_DRM_HDMI_MC_SWRST_TMDS);
}

static void
rk_drm_hdmi_enable_dvi_mode(struct rk_drm_softc *sc)
{
	uint8_t hdcpcfg0;

	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_INVIDCONF,
	    RK_DRM_HDMI_FC_INVIDCONF_DVI_1080P60);
	hdcpcfg0 = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_A_HDCPCFG0);
	hdcpcfg0 &= ~RK_DRM_HDMI_A_HDCPCFG0_HDMIDVI;
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_HDCPCFG0, hdcpcfg0);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_HDCPCFG1,
	    RK_DRM_HDMI_A_HDCPCFG1_DEFAULT);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_VIDPOLCFG,
	    RK_DRM_HDMI_A_VIDPOLCFG_DATAENPOL);
}

static void
rk_drm_hdmi_enable_hdmi_mode(struct rk_drm_softc *sc)
{
	uint8_t hdcpcfg0;
	uint8_t pkt_en;

	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_INVIDCONF,
	    RK_DRM_HDMI_FC_INVIDCONF_HDMI_1080P60);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_AVICONF3, 0x00);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_AVICONF0, 0x00);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_AVICONF1,
	    RK_DRM_HDMI_FC_AVICONF1_PICTURE_ASPECT_16_9);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_AVICONF2, 0x00);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_AVIVID, RK_DRM_MODE_VIC);

	pkt_en = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_FC_PACKET_TX_EN);
	pkt_en |= RK_DRM_HDMI_FC_PACKET_TX_EN_AVI |
	    RK_DRM_HDMI_FC_PACKET_TX_EN_GCP;
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_PACKET_TX_EN, pkt_en);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PKT_SEND_CTL,
	    RK_DRM_HDMI_PKT_SEND_CTL_AVI_INFO_UP);
	DELAY(10);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PKT_SEND_CTL,
	    RK_DRM_HDMI_PKT_SEND_CTL_AVI_INFO_EN);

	hdcpcfg0 = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_A_HDCPCFG0);
	hdcpcfg0 |= RK_DRM_HDMI_A_HDCPCFG0_HDMIDVI;
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_HDCPCFG0, hdcpcfg0);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_HDCPCFG1,
	    RK_DRM_HDMI_A_HDCPCFG1_DEFAULT);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_A_VIDPOLCFG,
	    RK_DRM_HDMI_A_VIDPOLCFG_DATAENPOL);
	rk_drm_hdmi_clear_overflow(sc);
}

static void
rk_drm_dw_hdmi_init_1080p60(struct rk_drm_softc *sc)
{
	rk_drm_hdmi_enable_dvi_mode(sc);
	rk_drm_hdmi_write1_safe(sc, 0x1001, 0x80);
	rk_drm_hdmi_write1_safe(sc, 0x1002, 0x07);
	rk_drm_hdmi_write1_safe(sc, 0x1003, 0x18);
	rk_drm_hdmi_write1_safe(sc, 0x1004, 0x01);
	rk_drm_hdmi_write1_safe(sc, 0x1005, 0x38);
	rk_drm_hdmi_write1_safe(sc, 0x1006, 0x04);
	rk_drm_hdmi_write1_safe(sc, 0x1007, 0x2d);
	rk_drm_hdmi_write1_safe(sc, 0x1008, 0x58);
	rk_drm_hdmi_write1_safe(sc, 0x1009, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x100a, 0x2c);
	rk_drm_hdmi_write1_safe(sc, 0x100b, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x100c, 0x04);
	rk_drm_hdmi_write1_safe(sc, 0x100d, 0x05);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CTRLDUR, 12);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_EXCTRLDUR, 32);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_EXCTRLSPAC, 1);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH0PREAM, 0x0b);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH1PREAM, 0x16);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH2PREAM, 0x21);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_AVICONF3, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_GCP, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_AVICONF0, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_AVICONF1, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_AVICONF2, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_AVIVID, RK_DRM_MODE_VIC);
	rk_drm_hdmi_write1_safe(sc, 0x01ff, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0184, 0xfe);
}

static void
rk_drm_dw_hdmi_finish_1080p60(struct rk_drm_softc *sc)
{
	uint8_t clkdis;
	uint8_t val;

	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_MC_FLOWCTRL, 0x00);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CTRLDUR, 12);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_EXCTRLDUR, 32);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_EXCTRLSPAC, 1);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH0PREAM, 0x0b);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH1PREAM, 0x16);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_CH2PREAM, 0x21);

	clkdis = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_MC_CLKDIS) &
	    RK_DRM_HDMI_MC_CLKDIS_CECCLK_DISABLE;
	clkdis |= (uint8_t)~RK_DRM_HDMI_MC_CLKDIS_CECCLK_DISABLE;
	clkdis &= ~0x01;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_MC_CLKDIS, clkdis);
	clkdis &= ~0x02;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_MC_CLKDIS, clkdis);
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_FC_VSYNCINWIDTH,
	    RK_DRM_MODE_VSYNC);

	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_PR_CD, 0x40);

	val = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_VP_STUFF);
	val &= ~0x01;
	val |= 0x01;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_STUFF, val);

	val = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_VP_CONF);
	val &= ~(0x10 | 0x04);
	val |= 0x04;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_CONF, val);

	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_REMAP, 0x00);

	val = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_VP_CONF);
	val &= ~(0x40 | 0x20 | 0x08);
	val |= 0x40;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_CONF, val);

	val = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_VP_STUFF);
	val &= ~(0x02 | 0x04);
	val |= 0x02 | 0x04;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_STUFF, val);

	val = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_VP_CONF);
	val &= ~0x03;
	val |= 0x03;
	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_VP_CONF, val);

	rk_drm_hdmi_write1_safe(sc, RK_DRM_HDMI_TX_INVID0, 0x01);
	rk_drm_hdmi_write1_safe(sc, 0x0201, 0x07);
	rk_drm_hdmi_write1_safe(sc, 0x0202, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0203, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0204, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0205, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0206, 0x00);
	rk_drm_hdmi_write1_safe(sc, 0x0207, 0x00);

	rk_drm_hdmi_clear_overflow(sc);
}

static int
rk_drm_hdmi_phy_i2c_reset(struct rk_drm_softc *sc)
{
	int timeout;

	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SOFTRSTZ, 0x00);
	for (timeout = 100; timeout > 0; timeout--) {
		if ((rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_SOFTRSTZ) &
		    0x01) != 0)
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

static int
rk_drm_hdmi_phy_i2c_write(struct rk_drm_softc *sc, uint8_t reg, uint16_t val)
{
	uint8_t stat;
	uint8_t err;
	uint8_t sticky;
	int timeout;

	if (rk_drm_hdmi_phy_i2c_reset(sc) != 0)
		return (ETIMEDOUT);

	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_IH_I2CMPHY_STAT0, 0x03);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SLAVE, HDMI_PHY_I2C_ADDR);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_ADDRESS, reg);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_DATAO_1, (val >> 8) & 0xff);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_DATAO_0, val & 0xff);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_OPERATION, 0x10);

	for (timeout = 200; timeout > 0; timeout--) {
		DELAY(1000);
		err = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_CTLINT);
		sticky = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_IH_I2CMPHY_STAT0);
		if ((sticky & 0x01) != 0 || (err & 0x10) != 0 ||
		    (err & 0x01) != 0) {
			device_printf(sc->dev,
			    "phy i2c write reg=0x%02x val=0x%04x ctlint=0x%02x "
			    "int=0x%02x ih=0x%02x stat0=0x%02x conf0=0x%02x\n",
			    reg, val, err,
			    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_INT),
			    sticky,
			    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0),
			    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0));
			rk_drm_hdmi_write1(sc, RK_DRM_HDMI_IH_I2CMPHY_STAT0,
			    sticky);
			(void)rk_drm_hdmi_phy_i2c_reset(sc);
			return (EIO);
		}
		stat = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_INT);
		if ((sticky & 0x02) != 0 || (stat & 0x01) != 0) {
			rk_drm_hdmi_write1(sc, RK_DRM_HDMI_IH_I2CMPHY_STAT0, 0x02);
			return (0);
		}
	}
	device_printf(sc->dev,
	    "phy i2c write timeout reg=0x%02x val=0x%04x ctlint=0x%02x "
	    "int=0x%02x ih=0x%02x stat0=0x%02x conf0=0x%02x\n",
	    reg, val, rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_CTLINT),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_I2CM_INT),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_IH_I2CMPHY_STAT0),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0));
	(void)rk_drm_hdmi_phy_i2c_reset(sc);
	return (ETIMEDOUT);
}

static int
rk_drm_hdmi_phy_init(struct rk_drm_softc *sc)
{
	uint8_t phy_conf0;
	int iter;
	int timeout;

	rk_drm_cru_write4(sc, 0x0240,
	    (1u << 25) | (1u << 26) | (0 << 9) | (0 << 10));
	rk_drm_cru_write4(sc, 0x0244, (1u << 18) | (0 << 2));
	rk_drm_cru_write4(sc, 0x0250, (1u << 28) | (0 << 12));
	rk_drm_cru_write4(sc, 0x0254, (1u << 24) | (0 << 8));
	DELAY(10000);

	for (iter = 0; iter < 2; iter++) {
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_FLOWCTRL, 0x00);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_PHYRSTZ, 0x01);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_VP_PR_CD, 0x40);
		DELAY(5000);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_PHYRSTZ, 0x00);
		DELAY(5000);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_HEACPHY_RST, 0x01);

		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_BASE_SFRDIVLOW,
		    RK_DRM_HDMI_BASE_SFRDIVLOW_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_BASE_SFRDIVHIGH,
		    RK_DRM_HDMI_BASE_SFRDIVHIGH_DEFAULT);

		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_JTAG_CFG,
		    RK_DRM_HDMI_PHY_JTAG_CFG_I2C);
		if (rk_drm_hdmi_phy_i2c_reset(sc) != 0) {
			device_printf(sc->dev,
			    "phy i2c reset timed out stat0=0x%02x conf0=0x%02x\n",
			    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0),
			    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0));
			return (ETIMEDOUT);
		}
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SLAVE,
		    HDMI_PHY_I2C_ADDR);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_DIV,
		    RK_DRM_HDMI_PHY_I2CM_DIV_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SS_HCNT1, 0x00);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SS_HCNT0,
		    RK_DRM_HDMI_PHY_I2CM_SS_HCNT0_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SS_LCNT1, 0x00);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SS_LCNT0,
		    RK_DRM_HDMI_PHY_I2CM_SS_LCNT0_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_FS_HCNT1, 0x00);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_FS_HCNT0,
		    RK_DRM_HDMI_PHY_I2CM_FS_HCNT0_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_FS_LCNT1, 0x00);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_FS_LCNT0,
		    RK_DRM_HDMI_PHY_I2CM_FS_LCNT0_DEFAULT);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_I2CM_SDA_HOLD,
		    RK_DRM_HDMI_PHY_I2CM_SDA_HOLD_DEFAULT);

		phy_conf0 = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0);
		phy_conf0 |= RK_DRM_HDMI_PHY_CONF0_SELDATAENPOL |
		    RK_DRM_HDMI_PHY_CONF0_PDDQ;
		phy_conf0 &= ~(RK_DRM_HDMI_PHY_CONF0_SELDIPIF |
		    RK_DRM_HDMI_PHY_CONF0_ENTMDS |
		    RK_DRM_HDMI_PHY_CONF0_PDZ |
		    RK_DRM_HDMI_PHY_CONF0_TXPWRON |
		    RK_DRM_HDMI_PHY_CONF0_SVSRET);
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_CONF0, phy_conf0);
		DELAY(1000);

		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_CPCE_CTRL,
		    RK_DRM_HDMI_PHY_148500_CPCE_CTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_GMPCTRL,
		    RK_DRM_HDMI_PHY_148500_GMPCTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_CURRCTRL,
		    RK_DRM_HDMI_PHY_148500_CURRCTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_PLLPHBYCTRL,
		    0x0000) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_MSM_CTRL,
		    RK_DRM_HDMI_PHY_148500_MSM_CTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_TXTERM,
		    RK_DRM_HDMI_PHY_148500_TXTERM) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_CKSYMTXCTRL,
		    RK_DRM_HDMI_PHY_148500_CKSYMTXCTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_VLEVCTRL,
		    RK_DRM_HDMI_PHY_148500_VLEVCTRL) != 0)
			return (EIO);
		if (rk_drm_hdmi_phy_i2c_write(sc, RK_DRM_HDMI_PHY_I2C_CKCALCTRL,
		    RK_DRM_HDMI_PHY_I2C_CKCALCTRL_OVERRIDE) != 0)
			return (EIO);

		phy_conf0 = rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0);
		phy_conf0 |= RK_DRM_HDMI_PHY_CONF0_PDZ;
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_CONF0, phy_conf0);
		DELAY(1000);

		phy_conf0 &= ~RK_DRM_HDMI_PHY_CONF0_ENTMDS;
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_CONF0, phy_conf0);
		DELAY(1000);
		phy_conf0 |= RK_DRM_HDMI_PHY_CONF0_ENTMDS;
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_CONF0, phy_conf0);
		DELAY(1000);

		phy_conf0 |= RK_DRM_HDMI_PHY_CONF0_TXPWRON |
		    RK_DRM_HDMI_PHY_CONF0_SVSRET;
		phy_conf0 &= ~RK_DRM_HDMI_PHY_CONF0_PDDQ;
		rk_drm_hdmi_write1(sc, RK_DRM_HDMI_PHY_CONF0, phy_conf0);
		DELAY(5000);
	}

	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_MC_CLKDIS, 0x00);
	rk_drm_hdmi_write1(sc, RK_DRM_HDMI_FC_VSYNCINWIDTH, RK_DRM_MODE_VSYNC);
	rk_drm_hdmi_toggle_main_reset(sc,
	    RK_DRM_HDMI_MC_SWRST_TMDS | RK_DRM_HDMI_MC_SWRST_PIXEL);

	for (timeout = 20; timeout > 0; timeout--) {
		DELAY(5000);
		if ((rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0) & 0x01) != 0)
			return (0);
	}
	device_printf(sc->dev,
	    "phy pll lock timeout stat0=0x%02x lock=0x%02x conf0=0x%02x\n",
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_MC_LOCKONCLOCK),
	    rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_CONF0));
	return (ETIMEDOUT);
}

int
rk_drm_hw_modeset(struct rk_drm_softc *sc)
{
	int error;

	if (!sc->hw_attached)
		return (ENXIO);

	rk_drm_fb_fill(sc, RK_DRM_FB_BOOT_COLOR);
	rk_drm_display_domain_sanity(sc);
	rk_drm_route_vop_to_hdmi(sc);
	rk_drm_vop_init_1080p60(sc);
	rk_drm_dw_hdmi_init_1080p60(sc);
	error = rk_drm_hdmi_phy_init(sc);
	if (error != 0) {
		device_printf(sc->dev, "HDMI PHY init failed: %d\n", error);
		return (error);
	}
	rk_drm_dw_hdmi_finish_1080p60(sc);
	rk_drm_hdmi_enable_hdmi_mode(sc);
	return (0);
}

bool
rk_drm_hw_hpd(struct rk_drm_softc *sc)
{
	if (sc->hdmi_va == 0)
		return (false);
	return ((rk_drm_hdmi_read1(sc, RK_DRM_HDMI_PHY_STAT0) & 0x02) != 0);
}

static void
rk_drm_hw_unmap(struct rk_drm_softc *sc)
{
	if (sc->hdmi_va != 0)
		pmap_unmapdev((void *)sc->hdmi_va, sc->hdmi_size);
	if (sc->cru_va != 0)
		bus_space_unmap(fdtbus_bs_tag, sc->cru_bsh, sc->cru_size);
	if (sc->pmucru_va != 0)
		bus_space_unmap(fdtbus_bs_tag, sc->pmucru_bsh, sc->pmucru_size);
	if (sc->pmu_va != 0)
		bus_space_unmap(fdtbus_bs_tag, sc->pmu_bsh, sc->pmu_size);
	if (sc->grf_va != 0)
		bus_space_unmap(fdtbus_bs_tag, sc->grf_bsh, sc->grf_size);
	if (sc->vop_va != 0)
		bus_space_unmap(fdtbus_bs_tag, sc->vop_bsh, sc->vop_size);

	sc->hdmi_va = 0;
	sc->cru_va = 0;
	sc->pmucru_va = 0;
	sc->pmu_va = 0;
	sc->grf_va = 0;
	sc->vop_va = 0;
}

int
rk_drm_hw_attach(struct rk_drm_softc *sc)
{
	int error;

	sc->stride = RK_DRM_MODE_WIDTH * (RK_DRM_BPP / 8);
	sc->fb_size = sc->stride * RK_DRM_MODE_HEIGHT;

	error = rk_drm_fb_alloc(sc);
	if (error != 0) {
		device_printf(sc->dev, "framebuffer alloc failed: %d\n", error);
		return (error);
	}

	sc->vop_pa = 0xff900000;
	sc->vop_size = 0x10000;
	if (bus_space_map(fdtbus_bs_tag, sc->vop_pa, sc->vop_size, 0,
	    &sc->vop_bsh) != 0) {
		error = ENXIO;
		goto fail;
	}
	sc->vop_va = (vm_offset_t)sc->vop_bsh;

	sc->grf_pa = 0xff770000;
	sc->grf_size = 0x10000;
	if (bus_space_map(fdtbus_bs_tag, sc->grf_pa, sc->grf_size, 0,
	    &sc->grf_bsh) != 0) {
		error = ENXIO;
		goto fail;
	}
	sc->grf_va = (vm_offset_t)sc->grf_bsh;

	sc->pmu_pa = 0xff310000;
	sc->pmu_size = 0x1000;
	if (bus_space_map(fdtbus_bs_tag, sc->pmu_pa, sc->pmu_size, 0,
	    &sc->pmu_bsh) != 0) {
		error = ENXIO;
		goto fail;
	}
	sc->pmu_va = (vm_offset_t)sc->pmu_bsh;

	sc->pmucru_pa = 0xff750000;
	sc->pmucru_size = 0x1000;
	if (bus_space_map(fdtbus_bs_tag, sc->pmucru_pa, sc->pmucru_size, 0,
	    &sc->pmucru_bsh) != 0) {
		error = ENXIO;
		goto fail;
	}
	sc->pmucru_va = (vm_offset_t)sc->pmucru_bsh;

	sc->cru_pa = 0xff760000;
	sc->cru_size = 0x1000;
	if (bus_space_map(fdtbus_bs_tag, sc->cru_pa, sc->cru_size, 0,
	    &sc->cru_bsh) != 0) {
		error = ENXIO;
		goto fail;
	}
	sc->cru_va = (vm_offset_t)sc->cru_bsh;

	sc->hdmi_pa = 0xff940000;
	sc->hdmi_size = 0x20000;
	sc->hdmi_va = (vm_offset_t)pmap_mapdev(sc->hdmi_pa, sc->hdmi_size);
	if (sc->hdmi_va == 0) {
		error = ENXIO;
		goto fail;
	}

	sc->hw_attached = true;
	error = rk_drm_hw_modeset(sc);
	if (error != 0)
		goto fail;

	device_printf(sc->dev,
	    "fixed scanout ready %dx%d stride=%u fb_pa=0x%jx hpd=%d\n",
	    RK_DRM_MODE_WIDTH, RK_DRM_MODE_HEIGHT, sc->stride,
	    (uintmax_t)sc->fb_pa, rk_drm_hw_hpd(sc));
	return (0);

fail:
	rk_drm_hw_detach(sc);
	return (error);
}

void
rk_drm_hw_detach(struct rk_drm_softc *sc)
{
	rk_drm_hw_unmap(sc);
	rk_drm_fb_free(sc);
	sc->hw_attached = false;
}
