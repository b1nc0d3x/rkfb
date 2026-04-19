
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Kyle T. Crenshaw
 * All rights reserved.
 *
 * RK3399 / RockPro64 framebuffer + display bring-up driver.
 * FreeBSD kernel module.
 *
 * Mapped blocks:
 *   VOP B   0xff900000  0x10000   32-bit registers
 *   GRF     0xff770000  0x10000   32-bit registers
 *   PMU     0xff310000  0x1000    32-bit registers
 *   PMUCRU  0xff750000  0x1000    32-bit registers
 *   CRU     0xff760000  0x1000    32-bit registers
 *   HDMI    0xff940000  0x20000   DW-HDMI logical byte registers on
 *                                 32-bit-spaced MMIO
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/ioccom.h>
#include <sys/errno.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/cpufunc.h>
#include <arm/include/fdt.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/openfirm.h>
#include <sys/fbio.h>
#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include "rkfb_ioctl.h"

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define RKFB_BPP          32

#ifndef RKFB_VERBOSE
#define RKFB_VERBOSE      0
#endif

#define RKFB_VPRINTF(...) do {             \
        if (RKFB_VERBOSE)                  \
                printf(__VA_ARGS__);       \
} while (0)

#define HDMI_PHY_I2C_ADDR  0x69

#define RKFB_MODE_WIDTH         1920
#define RKFB_MODE_HEIGHT        1080
#define RKFB_MODE_HFP             88
#define RKFB_MODE_HSYNC           44
#define RKFB_MODE_HBP            148
#define RKFB_MODE_VFP              4
#define RKFB_MODE_VSYNC            5
#define RKFB_MODE_VBP             36
#define RKFB_MODE_VIC             16

#define RKFB_SYS_GRF_GPIO4C_IOMUX 0x0e028
#define RKFB_SYS_GRF_SOC_CON20    0x6250
#define RKFB_VIO_GRF_SOC_CON20    0x0250
#define RKFB_GRF_HDMI_LCDC_SEL    (1u << 6)
/*
 * GRF writes use hiword-update semantics. RK3399 TRM Part 1 defines
 * GPIO4C[1:0] = 2'b01 for i2c3hdmi_{scl,sda}, while GPIO4C[2] should remain
 * GPIO (2'b00). Mask GPIO4C[2:0] so stale PWM mux state on GPIO4C[2] is
 * cleared instead of inherited from a previous owner.
 */
#define RKFB_GRF_GPIO4C_I2C3HDMI  0x003f0005u

#define RKFB_VOP_DSP_HTOTAL_HS_END 0x0188
#define RKFB_VOP_DSP_HACT_ST_END   0x018c
#define RKFB_VOP_DSP_VTOTAL_VS_END 0x0190
#define RKFB_VOP_DSP_VACT_ST_END   0x0194
#define RKFB_VOP_POST_DSP_HACT_INFO 0x0170
#define RKFB_VOP_POST_DSP_VACT_INFO 0x0174
#define RKFB_VOP_SYS_CTRL_STANDBY    (1u << 22)
#define RKFB_VOP_SYS_CTRL_GATE_EN    (1u << 23)
#define RKFB_VOP_SYS_CTRL_MMU_EN     (1u << 20)
#define RKFB_VOP_SYS_CTRL_ENABLE     (1u << 11)
#define RKFB_VOP_SYS_CTRL_RGB_EN     (1u << 12)
#define RKFB_VOP_SYS_CTRL_HDMI_EN    (1u << 13)
#define RKFB_VOP_SYS_CTRL_EDP_EN     (1u << 14)
#define RKFB_VOP_SYS_CTRL_MIPI_EN    (1u << 15)
#define RKFB_VOP_SYS_CTRL_MIPI_DUAL  (1u << 3)
#define RKFB_VOP_DSP_OUT_MODE_MASK 0x0000000fu
#define RKFB_VOP_DSP_OUT_MODE_P888  0x00000000u
#define RKFB_VOP_DSP_OUT_MODE_AAAA  0x0000000fu
#define RKFB_VOP_DSP_CTRL1_HDMI_PIN_POL_MASK  (0x7u << 20)
#define RKFB_VOP_DSP_CTRL1_HDMI_PIN_POL_POS   (0x3u << 20)
#define RKFB_VOP_DSP_CTRL1_HDMI_DCLK_POL      (1u << 23)
#define RKFB_VOP_DSP_CTRL1_PRE_DITHER_DOWN    (1u << 1)
#define RKFB_VOP_DSP_CTRL1_DITHER_DOWN_SEL    (1u << 4)
#define RKFB_VOP_DSP_CTRL1_DITHER_DOWN_MODE   (1u << 3)
#define RKFB_VOP_DSP_CTRL1_DITHER_DOWN_EN     (1u << 2)
#define RKFB_VOP_DSP_CTRL1_DITHER_UP          (1u << 6)
#define RKFB_VOP_BG_WHITE           0x00ffffffu
#define RKFB_VOP_BG_RED             0x00ff0000u
#define RKFB_VOP_WIN0_CTRL0_DISABLE 0x00000000u
#define RKFB_VOP_WIN0_LB_MODE_RGB_1920X5 (4u << 5)
/*
 * BSD RK3399 VOP reference uses data-format value 0 for 32-bit RGB on WIN0.
 * The earlier value 4 kept reproducing VOP BUS_ERROR on this board.
 */
#define RKFB_VOP_WIN0_DATA_FMT_XRGB8888 0x00000000u
#define RKFB_VOP_WIN0_CTRL0_ENABLE  (RKFB_VOP_WIN0_LB_MODE_RGB_1920X5 | \
    RKFB_VOP_WIN0_DATA_FMT_XRGB8888 | 0x00000001u)
#define RKFB_VOP_WIN0_CTRL2_PRIMARY 0x00000021u
#define RKFB_FB_DMA_LOWADDR_TEST    0x0fffffffu

#define RKFB_CRU_VPLL_CON0         0x00c0
#define RKFB_CRU_VPLL_CON1         0x00c4
#define RKFB_CRU_VPLL_CON2         0x00c8
#define RKFB_CRU_VPLL_CON3         0x00cc
#define RKFB_CRU_CLKGATE_CON10     0x0328
#define RKFB_CRU_CLKGATE_CON28     0x0370
#define RKFB_CRU_SOFTRST_CON17     0x0444
#define RKFB_CRU_DRESETN_VOP0_REQ  (1u << 8)
#define RKFB_CRU_VPLL_CON2_LOCK    (1u << 31)
#define RKFB_CRU_PLL_MODE_SLOW     (0u << 8)
#define RKFB_CRU_PLL_MODE_NORMAL   (1u << 8)
#define RKFB_CRU_PLL_DSMPD         (1u << 3)
#define RKFB_CRU_PLL_BYPASS        (1u << 1)
#define RKFB_CRU_PLL_POWER_DOWN    (1u << 0)
#define RKFB_VPLL_148500_FBDIV     99u
#define RKFB_VPLL_148500_REFDIV    4u
#define RKFB_VPLL_148500_POSTDIV1  4u
#define RKFB_VPLL_148500_POSTDIV2  1u
#define RKFB_CRU_CLKGATE_VOP0_MASK \
    ((1u << 12) | (1u << 9) | (1u << 8))
#define RKFB_CRU_CLKGATE_VOPB_MASK \
    ((1u << 7) | (1u << 6) | (1u << 5) | (1u << 4) | \
    (1u << 3) | (1u << 2) | (1u << 1) | (1u << 0))

#define RKFB_PMU_PWRDN_CON         0x0014
#define RKFB_PMU_PWRDN_ST          0x0018
#define RKFB_PMU_BUS_IDLE_REQ      0x0060
#define RKFB_PMU_BUS_IDLE_ST       0x0064
#define RKFB_PMU_BUS_IDLE_ACK      0x0068
#define RKFB_PMU_PD_VO             (1u << 20)
#define RKFB_PMU_IDLE_VOPL         (1u << 8)
#define RKFB_PMU_IDLE_VOPB         (1u << 7)

#define RKFB_PMUCRU_GATEDIS_CON0   0x0130
#define RKFB_PMUCRU_GATEDIS_VOPB   (1u << 19)

#define RKFB_HDMI_TX_INVID0        0x0200
#define RKFB_HDMI_IH_I2CM_STAT0    0x0105
#define RKFB_HDMI_IH_I2CMPHY_STAT0 0x0108
#define RKFB_HDMI_IH_MUTE_I2CM_STAT0 0x0185
#define RKFB_HDMI_VP_PR_CD         0x0801
#define RKFB_HDMI_VP_STUFF         0x0802
#define RKFB_HDMI_VP_REMAP         0x0803
#define RKFB_HDMI_VP_CONF          0x0804
#define RKFB_HDMI_FC_INVIDCONF     0x1000
#define RKFB_HDMI_FC_VSYNCINWIDTH  0x100d
#define RKFB_HDMI_FC_CTRLDUR       0x1011
#define RKFB_HDMI_FC_EXCTRLDUR     0x1012
#define RKFB_HDMI_FC_EXCTRLSPAC    0x1013
#define RKFB_HDMI_FC_CH0PREAM      0x1014
#define RKFB_HDMI_FC_CH1PREAM      0x1015
#define RKFB_HDMI_FC_CH2PREAM      0x1016
#define RKFB_HDMI_FC_AVICONF3      0x1017
#define RKFB_HDMI_FC_GCP           0x1018
#define RKFB_HDMI_FC_AVICONF0      0x1019
#define RKFB_HDMI_FC_AVICONF1      0x101a
#define RKFB_HDMI_FC_AVICONF2      0x101b
#define RKFB_HDMI_FC_AVIVID        0x101c
#define RKFB_HDMI_FC_PACKET_TX_EN  0x10e3
#define RKFB_HDMI_PHY_CONF0        0x3000
#define RKFB_HDMI_PHY_STAT0        0x3004
#define RKFB_HDMI_PHY_I2CM_SLAVE   0x3020
#define RKFB_HDMI_PHY_I2CM_ADDRESS 0x3021
#define RKFB_HDMI_PHY_I2CM_DATAO_1 0x3022
#define RKFB_HDMI_PHY_I2CM_DATAO_0 0x3023
#define RKFB_HDMI_PHY_I2CM_OPERATION 0x3026
#define RKFB_HDMI_PHY_I2CM_INT     0x3027
#define RKFB_HDMI_PHY_I2CM_CTLINT  0x3028
#define RKFB_HDMI_PHY_I2CM_DIV     0x3029
#define RKFB_HDMI_PHY_I2CM_SOFTRSTZ 0x302a
#define RKFB_HDMI_PHY_I2CM_SS_HCNT1 0x302b
#define RKFB_HDMI_PHY_I2CM_SS_HCNT0 0x302c
#define RKFB_HDMI_PHY_I2CM_SS_LCNT1 0x302d
#define RKFB_HDMI_PHY_I2CM_SS_LCNT0 0x302e
#define RKFB_HDMI_PHY_I2CM_FS_HCNT1 0x302f
#define RKFB_HDMI_PHY_I2CM_FS_HCNT0 0x3030
#define RKFB_HDMI_PHY_I2CM_FS_LCNT1 0x3031
#define RKFB_HDMI_PHY_I2CM_FS_LCNT0 0x3032
#define RKFB_HDMI_PHY_I2CM_SDA_HOLD 0x3033
#define RKFB_HDMI_PHY_JTAG_CFG     0x3034
#define RKFB_HDMI_MC_CLKDIS        0x4001
#define RKFB_HDMI_MC_SWRSTZREQ     0x4002
#define RKFB_HDMI_MC_FLOWCTRL      0x4004
#define RKFB_HDMI_MC_PHYRSTZ       0x4005
#define RKFB_HDMI_MC_LOCKONCLOCK   0x4006
#define RKFB_HDMI_MC_HEACPHY_RST   0x4007
#define RKFB_HDMI_BASE_SFRDIVLOW   0x4018
#define RKFB_HDMI_BASE_SFRDIVHIGH  0x4019
#define RKFB_HDMI_A_HDCPCFG0       0x5000
#define RKFB_HDMI_A_HDCPCFG1       0x5001
#define RKFB_HDMI_A_VIDPOLCFG      0x5009
#define RKFB_HDMI_PKT_SEND_CTL     0x0640
#define RKFB_HDMI_I2CM_SLAVE       0x7e00
#define RKFB_HDMI_I2CM_ADDRESS     0x7e01
#define RKFB_HDMI_I2CM_DATAO       0x7e02
#define RKFB_HDMI_I2CM_DATAI       0x7e03
#define RKFB_HDMI_I2CM_OPERATION   0x7e04
#define RKFB_HDMI_I2CM_INT         0x7e05
#define RKFB_HDMI_I2CM_CTLINT      0x7e06
#define RKFB_HDMI_I2CM_DIV         0x7e07
#define RKFB_HDMI_I2CM_SEGADDR     0x7e08
#define RKFB_HDMI_I2CM_SOFTRSTZ    0x7e09
#define RKFB_HDMI_I2CM_SEGPTR      0x7e0a
#define RKFB_HDMI_I2CM_SS_SCL_HCNT1 0x7e0b
#define RKFB_HDMI_I2CM_SS_SCL_HCNT0 0x7e0c
#define RKFB_HDMI_I2CM_SS_SCL_LCNT1 0x7e0d
#define RKFB_HDMI_I2CM_SS_SCL_LCNT0 0x7e0e
#define RKFB_HDMI_I2CM_FS_SCL_HCNT1 0x7e0f
#define RKFB_HDMI_I2CM_FS_SCL_HCNT0 0x7e10
#define RKFB_HDMI_I2CM_FS_SCL_LCNT1 0x7e11
#define RKFB_HDMI_I2CM_FS_SCL_LCNT0 0x7e12
#define RKFB_HDMI_I2CM_SDA_HOLD    0x7e13
#define RKFB_HDMI_I2CM_READ_BUFF0  0x7e20

#define RKFB_HDMI_PHY_CONF0_PDZ          (1u << 7)
#define RKFB_HDMI_PHY_CONF0_ENTMDS       (1u << 6)
#define RKFB_HDMI_PHY_CONF0_SVSRET       (1u << 5)
#define RKFB_HDMI_PHY_CONF0_PDDQ         (1u << 4)
#define RKFB_HDMI_PHY_CONF0_TXPWRON      (1u << 3)
#define RKFB_HDMI_PHY_CONF0_ENHPDRXSENSE (1u << 2)
#define RKFB_HDMI_PHY_CONF0_SELDATAENPOL (1u << 1)
#define RKFB_HDMI_PHY_CONF0_SELDIPIF     (1u << 0)
#define RKFB_HDMI_PHY_CONF0_BASE \
    (RKFB_HDMI_PHY_CONF0_ENHPDRXSENSE | RKFB_HDMI_PHY_CONF0_SELDATAENPOL)
#define RKFB_HDMI_PHY_I2CM_DIV_DEFAULT      0x0b
#define RKFB_HDMI_PHY_I2CM_SS_HCNT0_DEFAULT 0x6c
#define RKFB_HDMI_PHY_I2CM_SS_LCNT0_DEFAULT 0x7f
#define RKFB_HDMI_PHY_I2CM_FS_HCNT0_DEFAULT 0x11
#define RKFB_HDMI_PHY_I2CM_FS_LCNT0_DEFAULT 0x24
#define RKFB_HDMI_PHY_I2CM_SDA_HOLD_DEFAULT 0x09
#define RKFB_HDMI_PHY_JTAG_CFG_I2C 0x11
#define RKFB_HDMI_BASE_SFRDIVLOW_DEFAULT  0x93
#define RKFB_HDMI_BASE_SFRDIVHIGH_DEFAULT 0x69
#define RKFB_HDMI_MC_SWRST_TMDS    (1u << 1)
#define RKFB_HDMI_MC_SWRST_PIXEL   (1u << 0)
#define RKFB_HDMI_MC_CLKDIS_HDCPCLK_DISABLE (1u << 6)
#define RKFB_HDMI_MC_CLKDIS_CECCLK_DISABLE  (1u << 5)
#define RKFB_HDMI_MC_CLKDIS_CSCCLK_DISABLE  (1u << 4)
#define RKFB_HDMI_MC_CLKDIS_AUDCLK_DISABLE  (1u << 3)
#define RKFB_HDMI_MC_CLKDIS_PREPCLK_DISABLE (1u << 2)
#define RKFB_HDMI_MC_CLKDIS_TMDSCLK_DISABLE (1u << 1)
#define RKFB_HDMI_MC_CLKDIS_PIXELCLK_DISABLE (1u << 0)
#define RKFB_HDMI_MC_LOCK_PCLK     (1u << 6)
#define RKFB_HDMI_MC_LOCK_TCLK     (1u << 5)
#define RKFB_HDMI_MC_LOCK_PREPCLK  (1u << 4)
#define RKFB_HDMI_FC_INVIDCONF_DVI_1080P60 0x70
#define RKFB_HDMI_FC_INVIDCONF_HDMI_1080P60 0x78
#define RKFB_HDMI_FC_AVICONF1_PICTURE_ASPECT_16_9 (2u << 4)
#define RKFB_HDMI_FC_PACKET_TX_EN_AVI (1u << 2)
#define RKFB_HDMI_FC_PACKET_TX_EN_GCP (1u << 1)
#define RKFB_HDMI_A_HDCPCFG0_HDMIDVI (1u << 0)
#define RKFB_HDMI_A_HDCPCFG1_SWRESETN          (1u << 0)
#define RKFB_HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE (1u << 1)
/*
 * TRM: ph2upshftenc must always be 1 for all PHYs.
 */
#define RKFB_HDMI_A_HDCPCFG1_PH2UPSHFTENC      (1u << 2)
#define RKFB_HDMI_A_HDCPCFG1_DEFAULT \
    (RKFB_HDMI_A_HDCPCFG1_SWRESETN | \
    RKFB_HDMI_A_HDCPCFG1_ENCRYPTIONDISABLE | \
    RKFB_HDMI_A_HDCPCFG1_PH2UPSHFTENC)
#define RKFB_HDMI_A_VIDPOLCFG_DATAENPOL (1u << 4)
#define RKFB_HDMI_A_VIDPOLCFG_VSYNCPOL  (1u << 3)
#define RKFB_HDMI_A_VIDPOLCFG_HSYNCPOL  (1u << 1)
#define RKFB_HDMI_PKT_SEND_CTL_AVI_INFO_UP (1u << 6)
#define RKFB_HDMI_PKT_SEND_CTL_AVI_INFO_EN (1u << 2)
#define RKFB_HDMI_I2CM_STAT_SCDC_READREQ (1u << 2)
#define RKFB_HDMI_I2CM_STAT_DONE    (1u << 1)
#define RKFB_HDMI_I2CM_STAT_ERROR   (1u << 0)
#define RKFB_HDMI_I2CM_OPERATION_BUSCLEAR (1u << 5)
#define RKFB_HDMI_I2CM_OPERATION_RD8 (1u << 2)
#define RKFB_HDMI_EDID_SLAVE_ADDR   0x50
#define RKFB_EXT_DDC_ADDR          (0x50 << 1)
#define RKFB_EXT_DDC_SEGADDR       (0x30 << 1)
#define RKFB_EXT_EDID_LENGTH       0x80
#define RKFB_CEA_EXT_TAG           0x02
#define RKFB_CEA_DB_VENDOR         0x03
#define RKFB_HDMI_IEEE_OUI         0x000c03

/*
 * PHY programming for 148.5 MHz / 24-bit RGB. The TRM only describes the
 * sequencing; the actual GEN2 PHY values below are cross-checked against the
 * BSD-licensed Synopsys DW-HDMI PHY implementation used on RK3399.
 */
#define RKFB_HDMI_PHY_I2C_CKCALCTRL        0x05
#define RKFB_HDMI_PHY_I2C_CPCE_CTRL        0x06
#define RKFB_HDMI_PHY_I2C_CKSYMTXCTRL      0x09
#define RKFB_HDMI_PHY_I2C_VLEVCTRL         0x0e
#define RKFB_HDMI_PHY_I2C_CURRCTRL         0x10
#define RKFB_HDMI_PHY_I2C_PLLPHBYCTRL      0x13
#define RKFB_HDMI_PHY_I2C_GMPCTRL          0x15
#define RKFB_HDMI_PHY_I2C_MSM_CTRL         0x17
#define RKFB_HDMI_PHY_I2C_TXTERM           0x19
#define RKFB_HDMI_PHY_I2C_CKCALCTRL_OVERRIDE 0x8000
#define RKFB_HDMI_PHY_148500_CPCE_CTRL     0x0051
#define RKFB_HDMI_PHY_148500_GMPCTRL       0x0003
#define RKFB_HDMI_PHY_148500_CURRCTRL      0x0000
#define RKFB_HDMI_PHY_148500_MSM_CTRL      0x0006
/*
 * DW-HDMI PHY tables are ordered as { pixelclk, symbol, term, vlev }.
 * CKSYMTXCTRL takes the symbol/pre-emphasis value, while TXTERM takes
 * the termination value.
 */
#define RKFB_HDMI_PHY_148500_TXTERM        0x0004
#define RKFB_HDMI_PHY_148500_CKSYMTXCTRL   0x802b
#define RKFB_HDMI_PHY_148500_VLEVCTRL      0x028d

/* -------------------------------------------------------------------------
 * Softc
 * ---------------------------------------------------------------------- */

struct rkfb_softc {
        struct cdev    *cdev;
        struct fb_info  fb_info;

        /* Framebuffer */
        bus_dma_tag_t   fb_dma_tag;
        bus_dmamap_t    fb_dma_map;
        vm_offset_t     fb_va;
        vm_paddr_t      fb_pa;
        size_t          fb_size;

        /* VOP B */
        bus_space_handle_t vop_bsh;
        vm_offset_t     vop_va;
        vm_paddr_t      vop_pa;
        size_t          vop_size;

        /* HDMI (DW-HDMI, byte-wide registers) */
        vm_offset_t     hdmi_va;
        vm_paddr_t      hdmi_pa;
        size_t          hdmi_size;

        /* GRF */
        bus_space_handle_t grf_bsh;
        vm_offset_t     grf_va;
        vm_paddr_t      grf_pa;
        size_t          grf_size;

        /* PMU */
        bus_space_handle_t pmu_bsh;
        vm_offset_t     pmu_va;
        vm_paddr_t      pmu_pa;
        size_t          pmu_size;

        /* PMUCRU */
        bus_space_handle_t pmucru_bsh;
        vm_offset_t     pmucru_va;
        vm_paddr_t      pmucru_pa;
        size_t          pmucru_size;

        /* Legacy aux map (currently unused for HDMI routing) */
        bus_space_handle_t viogrf_bsh;
        vm_offset_t     viogrf_va;
        vm_paddr_t      viogrf_pa;
        size_t          viogrf_size;

        /* CRU */
        bus_space_handle_t cru_bsh;
        vm_offset_t     cru_va;
        vm_paddr_t      cru_pa;
        size_t          cru_size;

        uint32_t        width;
        uint32_t        height;
        uint32_t        bpp;
        uint32_t        stride;

        struct mtx      mtx;
        int             mtx_initialized;
        int             fb_registered;
};




static struct rkfb_softc g_rkfb_sc;

/* -------------------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------------- */

static void rkfb_hdmi_phy_init(struct rkfb_softc *sc);
static int  rkfb_hdmi_phy_i2c_reset(struct rkfb_softc *sc);
static int  rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc,
    uint8_t reg, uint16_t val);
static int  rkfb_hdmi_ddc_read_block(struct rkfb_softc *sc, int block,
    uint8_t *edid);
static int  rkfb_hdmi_edid_is_hdmi_monitor(const uint8_t *ext);
static uint8_t rkfb_hdmi_read1(struct rkfb_softc *sc, size_t off);
static void rkfb_hdmi_enable_hdmi_mode(struct rkfb_softc *sc);
static void rkfb_hdmi_enable_dvi_mode(struct rkfb_softc *sc);
static void rkfb_hdmi_sink_probe(struct rkfb_softc *sc);
static void rkfb_fb_dma_cb(void *arg, bus_dma_segment_t *segs, int nseg,
    int error);
static int  rkfb_fb_alloc(struct rkfb_softc *sc);
static void rkfb_fb_free(struct rkfb_softc *sc);
static void rkfb_fb_sync(struct rkfb_softc *sc);
static void rkfb_hdmi_toggle_main_reset(struct rkfb_softc *sc, uint8_t mask);
static void rkfb_hdmi_clear_overflow(struct rkfb_softc *sc);
static void rkfb_route_vop_to_hdmi(struct rkfb_softc *sc);
static void rkfb_display_domain_sanity(struct rkfb_softc *sc);
static int  rkfb_program_vpll_148500khz(struct rkfb_softc *sc);
static void rkfb_vop_pulse_dclk_reset(struct rkfb_softc *sc);
static void rkfb_vop_init_1080p60(struct rkfb_softc *sc);
static void rkfb_dw_hdmi_init_1080p60(struct rkfb_softc *sc);
static void rkfb_dw_hdmi_finish_1080p60(struct rkfb_softc *sc);
static void rkfb_cleanup(struct rkfb_softc *sc);
static int  rkfb_attach(struct rkfb_softc *sc);
static void rkfb_detach(struct rkfb_softc *sc);
static struct cdevsw rkfb_cdevsw;
static void rkfb_fb_info_init(struct rkfb_softc *sc);

/* -------------------------------------------------------------------------
 * VOP accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_vop_read4(struct rkfb_softc *sc, size_t off)
{
        return (bus_space_read_4(fdtbus_bs_tag, sc->vop_bsh, off));
}

static inline void
rkfb_vop_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        bus_space_write_4(fdtbus_bs_tag, sc->vop_bsh, off, val);
        bus_space_barrier(fdtbus_bs_tag, sc->vop_bsh, off, 4,
            BUS_SPACE_BARRIER_WRITE);
}

static void
rkfb_fb_dma_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
        bus_addr_t *fb_busaddr = arg;

        if (error != 0 || nseg != 1)
                return;

        *fb_busaddr = segs[0].ds_addr;
}

static int
rkfb_fb_alloc(struct rkfb_softc *sc)
{
        const size_t alloc_size = round_page(sc->fb_size);
        bus_addr_t fb_busaddr;
        void *fb_kva;
        int error;

        error = bus_dma_tag_create(NULL, PAGE_SIZE, 0,
            RKFB_FB_DMA_LOWADDR_TEST, BUS_SPACE_MAXADDR,
            NULL, NULL, alloc_size, 1, alloc_size, 0,
            NULL, NULL, &sc->fb_dma_tag);
        if (error != 0) {
                printf("rkfb: bus_dma_tag_create failed: %d\n", error);
                return (error);
        }

        error = bus_dmamem_alloc(sc->fb_dma_tag, &fb_kva,
            BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
            &sc->fb_dma_map);
        if (error != 0) {
                printf("rkfb: bus_dmamem_alloc failed: %d\n", error);
                bus_dma_tag_destroy(sc->fb_dma_tag);
                sc->fb_dma_tag = NULL;
                return (error);
        }

        fb_busaddr = 0;
        error = bus_dmamap_load(sc->fb_dma_tag, sc->fb_dma_map, fb_kva,
            alloc_size, rkfb_fb_dma_cb, &fb_busaddr, BUS_DMA_WAITOK);
        if (error != 0 || fb_busaddr == 0) {
                printf("rkfb: bus_dmamap_load failed: %d addr=0x%jx\n",
                    error, (uintmax_t)fb_busaddr);
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
rkfb_fb_free(struct rkfb_softc *sc)
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
rkfb_fb_info_init(struct rkfb_softc *sc)
{
        struct fb_info *info;

        info = &sc->fb_info;
        bzero(info, sizeof(*info));
        info->fb_type = FBTYPE_PCIMISC;
        info->fb_height = sc->height;
        info->fb_width = sc->width;
        info->fb_depth = sc->bpp;
        info->fb_cmsize = 0;
        info->fb_size = sc->fb_size;
        info->fb_vbase = sc->fb_va;
        info->fb_pbase = 0;
        info->fb_priv = sc;
        info->fb_name = "rkfb";
        info->fb_stride = sc->stride;
        info->fb_bpp = sc->bpp;
}

static int
rkfb_vop_write_allowed(uint32_t off)
{
  switch (off) {
        case 0x0000:  /* REG_CFG_DONE */
        case 0x0008:  /* SYS_CTRL     */
        case 0x0030:  /* WIN0_CTRL0   */
        case 0x003c:  /* WIN0_VIR     */
        case 0x0040:  /* WIN0_YRGB_MST */
        case 0x0048:  /* WIN0_ACT_INFO */
        case 0x004c:  /* WIN0_DSP_INFO */
        case 0x0050:  /* WIN0_DSP_ST   */
        case RKFB_VOP_DSP_HTOTAL_HS_END:
        case RKFB_VOP_DSP_HACT_ST_END:
        case RKFB_VOP_DSP_VTOTAL_VS_END:
        case RKFB_VOP_DSP_VACT_ST_END:
        case 0x020c:
        case 0x028c:
        case 0x029c:
        case 0x0310:
        case 0x0314:
        case 0x0318:
                return (1);
        default:
                return (0);
        }
}

static void
rkfb_cleanup(struct rkfb_softc *sc)
{
        if (sc->fb_registered) {
                fbd_unregister(&sc->fb_info);
                sc->fb_registered = 0;
        }
        if (sc->cdev != NULL) {
                destroy_dev(sc->cdev);
                sc->cdev = NULL;
        }
        if (sc->hdmi_va != 0)
                pmap_unmapdev((void *)sc->hdmi_va, sc->hdmi_size);
        if (sc->cru_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->cru_bsh, sc->cru_size);
        if (sc->viogrf_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->viogrf_bsh,
                    sc->viogrf_size);
        if (sc->pmucru_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->pmucru_bsh,
                    sc->pmucru_size);
        if (sc->pmu_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->pmu_bsh, sc->pmu_size);
        if (sc->grf_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->grf_bsh, sc->grf_size);
        if (sc->vop_va != 0)
                bus_space_unmap(fdtbus_bs_tag, sc->vop_bsh, sc->vop_size);
        rkfb_fb_free(sc);
        if (sc->mtx_initialized) {
                mtx_destroy(&sc->mtx);
                sc->mtx_initialized = 0;
        }
        bzero(sc, sizeof(*sc));
}

static int
rkfb_attach(struct rkfb_softc *sc)
{
        int error;

        bzero(sc, sizeof(*sc));

        /*
         * Keep framebuffer geometry tied to the programmed video mode
         * so scanout size and exported /dev/rkfb0 size cannot drift.
         */
        sc->width  = RKFB_MODE_WIDTH;
        sc->height = RKFB_MODE_HEIGHT;
        sc->bpp    = RKFB_BPP;
        sc->stride = sc->width * (sc->bpp / 8);
        sc->fb_size = sc->stride * sc->height;

        mtx_init(&sc->mtx, "rkfb", NULL, MTX_DEF);
        sc->mtx_initialized = 1;

        error = rkfb_fb_alloc(sc);
        if (error != 0) {
                printf("rkfb: failed to allocate DMA framebuffer\n");
                goto fail;
        }
        rkfb_fb_sync(sc);

        /* Map VOP B */
        sc->vop_pa   = 0xff900000;
        sc->vop_size = 0x10000;
        if (bus_space_map(fdtbus_bs_tag, sc->vop_pa, sc->vop_size, 0,
            &sc->vop_bsh) != 0) {
                printf("rkfb: failed to map VOP\n");
                error = ENXIO;
                goto fail;
        }
        sc->vop_va = (vm_offset_t)sc->vop_bsh;
        RKFB_VPRINTF("rkfb: VOP mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->vop_pa, (uintmax_t)sc->vop_va,
            sc->vop_size);

        /*
         * Map the main GRF block.
         *
         * RP64-DOCS TRM Part 1 places GRF_SOC_CON20 and GPIO4C_IOMUX
         * in the GRF chapter, while the live RockPro64 DT binds
         * rockchip,grf to syscon@ff770000. The old 0xff320000 base is
         * PMUGRF, not the HDMI route/IOMUX block.
         */
        sc->grf_pa   = 0xff770000;
        sc->grf_size = 0x10000;
        if (bus_space_map(fdtbus_bs_tag, sc->grf_pa, sc->grf_size, 0,
            &sc->grf_bsh) != 0) {
                printf("rkfb: failed to map GRF\n");
                error = ENXIO;
                goto fail;
        }
        sc->grf_va = (vm_offset_t)sc->grf_bsh;

        /* Map PMU */
        sc->pmu_pa   = 0xff310000;
        sc->pmu_size = 0x1000;
        if (bus_space_map(fdtbus_bs_tag, sc->pmu_pa, sc->pmu_size, 0,
            &sc->pmu_bsh) != 0) {
                printf("rkfb: failed to map PMU\n");
                error = ENXIO;
                goto fail;
        }
        sc->pmu_va = (vm_offset_t)sc->pmu_bsh;

        /* Map PMUCRU */
        sc->pmucru_pa   = 0xff750000;
        sc->pmucru_size = 0x1000;
        if (bus_space_map(fdtbus_bs_tag, sc->pmucru_pa,
            sc->pmucru_size, 0, &sc->pmucru_bsh) != 0) {
                printf("rkfb: failed to map PMUCRU\n");
                error = ENXIO;
                goto fail;
        }
        sc->pmucru_va = (vm_offset_t)sc->pmucru_bsh;

        /* Legacy aux map; not used for the documented HDMI mux path. */
        sc->viogrf_pa   = 0xff320000;
        sc->viogrf_size = 0x1000;
        if (bus_space_map(fdtbus_bs_tag, sc->viogrf_pa,
            sc->viogrf_size, 0, &sc->viogrf_bsh) != 0) {
                printf("rkfb: failed to map VIO GRF\n");
                error = ENXIO;
                goto fail;
        }
        sc->viogrf_va = (vm_offset_t)sc->viogrf_bsh;

        /* Map CRU */
        sc->cru_pa   = 0xff760000;
        sc->cru_size = 0x1000;
        if (bus_space_map(fdtbus_bs_tag, sc->cru_pa, sc->cru_size, 0,
            &sc->cru_bsh) != 0) {
                printf("rkfb: failed to map CRU\n");
                error = ENXIO;
                goto fail;
        }
        sc->cru_va = (vm_offset_t)sc->cru_bsh;

        RKFB_VPRINTF("rkfb: GRF mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->grf_pa, (uintmax_t)sc->grf_va,
            sc->grf_size);
        RKFB_VPRINTF("rkfb: PMU mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->pmu_pa, (uintmax_t)sc->pmu_va,
            sc->pmu_size);
        RKFB_VPRINTF("rkfb: PMUCRU mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->pmucru_pa, (uintmax_t)sc->pmucru_va,
            sc->pmucru_size);
        RKFB_VPRINTF("rkfb: VIO GRF mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->viogrf_pa, (uintmax_t)sc->viogrf_va,
            sc->viogrf_size);
        RKFB_VPRINTF("rkfb: CRU mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->cru_pa, (uintmax_t)sc->cru_va,
            sc->cru_size);

        /* Map HDMI */
        sc->hdmi_pa   = 0xff940000;
        sc->hdmi_size = 0x20000;
        sc->hdmi_va   = (vm_offset_t)pmap_mapdev(sc->hdmi_pa,
            sc->hdmi_size);
        if (sc->hdmi_va == 0) {
                printf("rkfb: failed to map HDMI\n");
                error = ENXIO;
                goto fail;
        }

        RKFB_VPRINTF("rkfb: HDMI mapped pa=0x%jx va=0x%jx size=0x%zx\n",
            (uintmax_t)sc->hdmi_pa, (uintmax_t)sc->hdmi_va,
            sc->hdmi_size);

        /* Keep load-time HDMI probing minimal to avoid risky paths. */
        RKFB_VPRINTF("rkfb: HDMI design_id=0x%02x rev=0x%02x "
            "prod0=0x%02x\n",
            rkfb_hdmi_read1(sc, 0x0000),
            rkfb_hdmi_read1(sc, 0x0001),
            rkfb_hdmi_read1(sc, 0x0002));

        rkfb_display_domain_sanity(sc);
        rkfb_route_vop_to_hdmi(sc);
        rkfb_vop_init_1080p60(sc);
        rkfb_dw_hdmi_init_1080p60(sc);
        rkfb_hdmi_phy_init(sc);
        rkfb_dw_hdmi_finish_1080p60(sc);
        rkfb_hdmi_sink_probe(sc);

        /* Create /dev/rkfb0 */
        sc->cdev = make_dev(&rkfb_cdevsw, 0,
            UID_ROOT, GID_WHEEL, 0600, "rkfb0");
        if (sc->cdev == NULL) {
                printf("rkfb: failed to create /dev/rkfb0\n");
                error = ENXIO;
                goto fail;
        }
        sc->cdev->si_drv1 = sc;

        rkfb_fb_info_init(sc);
        error = fbd_register(&sc->fb_info);
        if (error == 0) {
                sc->fb_registered = 1;
                printf("rkfb: registered /dev/fb0 via fbd/vt\n");
        } else {
                printf("rkfb: fbd_register failed: %d\n", error);
        }

        printf("rkfb: loaded /dev/rkfb0 %ux%u %u-bpp "
            "stride=%u size=%zu pa=0x%jx\n",
            sc->width, sc->height, sc->bpp,
            sc->stride, sc->fb_size,
            (uintmax_t)sc->fb_pa);

        return (0);

fail:
        rkfb_cleanup(sc);
        return (error);
}

static void
rkfb_detach(struct rkfb_softc *sc)
{
        rkfb_cleanup(sc);
        printf("rkfb: unloaded\n");
}

/* -------------------------------------------------------------------------
 * GRF accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_grf_read4(struct rkfb_softc *sc, size_t off)
{
        return (bus_space_read_4(fdtbus_bs_tag, sc->grf_bsh, off));
}

static inline void
rkfb_grf_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        bus_space_write_4(fdtbus_bs_tag, sc->grf_bsh, off, val);
        bus_space_barrier(fdtbus_bs_tag, sc->grf_bsh, off, 4,
            BUS_SPACE_BARRIER_WRITE);
}

/* -------------------------------------------------------------------------
 * PMU / PMUCRU accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_pmu_read4(struct rkfb_softc *sc, size_t off)
{
        return (bus_space_read_4(fdtbus_bs_tag, sc->pmu_bsh, off));
}

static inline void
rkfb_pmu_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        bus_space_write_4(fdtbus_bs_tag, sc->pmu_bsh, off, val);
        bus_space_barrier(fdtbus_bs_tag, sc->pmu_bsh, off, 4,
            BUS_SPACE_BARRIER_WRITE);
}

static inline uint32_t
rkfb_pmucru_read4(struct rkfb_softc *sc, size_t off)
{
        return (bus_space_read_4(fdtbus_bs_tag, sc->pmucru_bsh, off));
}

static inline void
rkfb_pmucru_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        bus_space_write_4(fdtbus_bs_tag, sc->pmucru_bsh, off, val);
        bus_space_barrier(fdtbus_bs_tag, sc->pmucru_bsh, off, 4,
            BUS_SPACE_BARRIER_WRITE);
}

/* -------------------------------------------------------------------------
 * CRU accessors (32-bit)
 * ---------------------------------------------------------------------- */

static inline uint32_t
rkfb_cru_read4(struct rkfb_softc *sc, size_t off)
{
        return (bus_space_read_4(fdtbus_bs_tag, sc->cru_bsh, off));
}

static inline void
rkfb_cru_write4(struct rkfb_softc *sc, size_t off, uint32_t val)
{
        bus_space_write_4(fdtbus_bs_tag, sc->cru_bsh, off, val);
        bus_space_barrier(fdtbus_bs_tag, sc->cru_bsh, off, 4,
            BUS_SPACE_BARRIER_WRITE);
}

/* -------------------------------------------------------------------------
 * HDMI accessors (byte-wide — DW-HDMI is a byte-register IP)
 * ---------------------------------------------------------------------- */

static inline uint8_t
rkfb_hdmi_read1(struct rkfb_softc *sc, size_t off)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->hdmi_va + (off << 2));
        return ((uint8_t)(*reg & 0xff));
}

static inline void
rkfb_hdmi_write1(struct rkfb_softc *sc, size_t off, uint8_t val)
{
        volatile uint32_t *reg;
        reg = (volatile uint32_t *)(sc->hdmi_va + (off << 2));
        *reg = val;
        __asm volatile("dsb sy" ::: "memory");
        __asm volatile("isb" ::: "memory");
}

static inline void
rkfb_hdmi_write1_safe(struct rkfb_softc *sc, size_t off, uint8_t val)
{
        uint64_t daif;

        __asm volatile("mrs %0, daif" : "=r"(daif));
        __asm volatile("msr daifset, #4");
        rkfb_hdmi_write1(sc, off, val);
        __asm volatile("msr daif, %0" :: "r"(daif));
}

static void
rkfb_hdmi_toggle_main_reset(struct rkfb_softc *sc, uint8_t mask)
{
        uint8_t reg;

        reg = rkfb_hdmi_read1(sc, RKFB_HDMI_MC_SWRSTZREQ);
        rkfb_hdmi_write1(sc, RKFB_HDMI_MC_SWRSTZREQ, reg & ~mask);
        DELAY(10);
        rkfb_hdmi_write1(sc, RKFB_HDMI_MC_SWRSTZREQ, reg | mask);
        DELAY(10);
}

static void
rkfb_hdmi_clear_overflow(struct rkfb_softc *sc)
{
        uint8_t swrstz;
        uint8_t invidconf;
        int i;

        /*
         * Match the existing FreeBSD DW-HDMI workaround:
         * request a TMDS soft reset, then rewrite FC_INVIDCONF several times
         * to flush latent overflow state in the controller path.
         */
        swrstz = rkfb_hdmi_read1(sc, RKFB_HDMI_MC_SWRSTZREQ);
        rkfb_hdmi_write1(sc, RKFB_HDMI_MC_SWRSTZREQ,
            swrstz & ~RKFB_HDMI_MC_SWRST_TMDS);
        invidconf = rkfb_hdmi_read1(sc, RKFB_HDMI_FC_INVIDCONF);
        for (i = 0; i < 4; i++)
                rkfb_hdmi_write1(sc, RKFB_HDMI_FC_INVIDCONF, invidconf);
        rkfb_hdmi_write1(sc, RKFB_HDMI_MC_SWRSTZREQ,
            swrstz | RKFB_HDMI_MC_SWRST_TMDS);
}

static void
rkfb_hdmi_log_clock_present(struct rkfb_softc *sc, const char *tag)
{
        uint8_t reg;

        reg = rkfb_hdmi_read1(sc, RKFB_HDMI_MC_LOCKONCLOCK);
        RKFB_VPRINTF("rkfb: %s MC_LOCKONCLOCK=0x%02x pclk=%d tclk=%d"
            " prepclk=%d\n",
            tag, reg,
            (reg & RKFB_HDMI_MC_LOCK_PCLK) != 0,
            (reg & RKFB_HDMI_MC_LOCK_TCLK) != 0,
            (reg & RKFB_HDMI_MC_LOCK_PREPCLK) != 0);
}


/* -------------------------------------------------------------------------
 * PHY I2C master — write one 16-bit value to Innosilicon PHY register
 * ---------------------------------------------------------------------- */

static int
rkfb_hdmi_phy_i2c_reset(struct rkfb_softc *sc)
{
        int timeout;

        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SOFTRSTZ, 0x00);
        for (timeout = 100; timeout > 0; timeout--) {
                if ((rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_SOFTRSTZ) &
                    0x01) != 0)
                        return (0);
                DELAY(10);
        }

        printf("rkfb: PHY I2C reset timeout rstz=0x%02x jtag=0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_SOFTRSTZ),
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_JTAG_CFG));
        return (ETIMEDOUT);
}

static int
rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc, uint8_t reg, uint16_t val)
{
        uint8_t stat;
        uint8_t err;
        uint8_t sticky;
        int rv;
        int timeout;

        rv = rkfb_hdmi_phy_i2c_reset(sc);
        if (rv != 0)
                return (rv);
        rkfb_hdmi_write1(sc, RKFB_HDMI_IH_I2CMPHY_STAT0, 0x03);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SLAVE, HDMI_PHY_I2C_ADDR);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_ADDRESS, reg);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_DATAO_1, (val >> 8) & 0xff);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_DATAO_0, val & 0xff);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_OPERATION, 0x10);

        for (timeout = 200; timeout > 0; timeout--) {
                DELAY(1000);
                err = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_CTLINT);
                sticky = rkfb_hdmi_read1(sc, RKFB_HDMI_IH_I2CMPHY_STAT0);
                if ((sticky & 0x01) != 0 || (err & 0x10) != 0 ||
                    (err & 0x01) != 0) {
                        printf("rkfb: PHY I2C write failed reg=0x%02x val=0x%04x"
                            " int=0x%02x ctl=0x%02x ih=0x%02x rstz=0x%02x"
                            " jtag=0x%02x\n",
                            reg, val,
                            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_INT), err,
                            sticky,
                            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_SOFTRSTZ),
                            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_JTAG_CFG));
                        rkfb_hdmi_write1(sc, RKFB_HDMI_IH_I2CMPHY_STAT0,
                            sticky);
                        (void)rkfb_hdmi_phy_i2c_reset(sc);
                        return (EIO);
                }
                stat = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_INT);
                if ((sticky & 0x02) != 0 || (stat & 0x01) != 0) {
                        rkfb_hdmi_write1(sc, RKFB_HDMI_IH_I2CMPHY_STAT0,
                            0x02);
                        return (0);
                }
        }
        printf("rkfb: PHY I2C write timeout reg=0x%02x val=0x%04x"
            " int=0x%02x ctl=0x%02x ih=0x%02x rstz=0x%02x jtag=0x%02x\n",
            reg, val,
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_INT),
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_CTLINT),
            rkfb_hdmi_read1(sc, RKFB_HDMI_IH_I2CMPHY_STAT0),
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_I2CM_SOFTRSTZ),
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_JTAG_CFG));
        (void)rkfb_hdmi_phy_i2c_reset(sc);
        return (ETIMEDOUT);
}

static int
rkfb_hdmi_ddc_read_block_once(int block, uint8_t *edid)
{
        device_t i2c_dev;
        uint8_t addr;
        uint8_t segment;
        unsigned char xfers;
        struct iic_msg msg[] = {
                { RKFB_EXT_DDC_SEGADDR, IIC_M_WR, 1, &segment },
                { RKFB_EXT_DDC_ADDR, IIC_M_WR, 1, &addr },
                { RKFB_EXT_DDC_ADDR, IIC_M_RD, RKFB_EXT_EDID_LENGTH, edid },
        };
        phandle_t node;
        pcell_t xref;
        int rv;

        node = OF_finddevice("/hdmi@ff940000");
        if (node == -1)
                return (ENXIO);
        if (OF_getencprop(node, "ddc-i2c-bus", &xref, sizeof(xref)) == -1 &&
            OF_getencprop(node, "ddc", &xref, sizeof(xref)) == -1)
                return (ENXIO);

        i2c_dev = OF_device_from_xref(xref);
        if (i2c_dev == NULL)
                return (ENXIO);

        addr = (block & 1) ? RKFB_EXT_EDID_LENGTH : 0;
        segment = block >> 1;
        xfers = segment ? 3 : 2;

        rv = iicbus_request_bus(i2c_dev, i2c_dev, IIC_INTRWAIT);
        if (rv != 0)
                return (rv);
        rv = iicbus_transfer(i2c_dev, &msg[3 - xfers], xfers);
        iicbus_release_bus(i2c_dev, i2c_dev);
        return (rv);
}

static int
rkfb_hdmi_ddc_read_block(struct rkfb_softc *sc, int block, uint8_t *edid)
{
        uint8_t phy_stat;
        int attempt;
        int rv;

        rv = ENXIO;
        for (attempt = 0; attempt < 20; attempt++) {
                if (sc->hdmi_va != 0) {
                        phy_stat = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_STAT0);
                        if ((phy_stat & 0x02) == 0) {
                                rv = ENXIO;
                                goto retry;
                        }
                } else {
                        phy_stat = 0;
                }

                rv = rkfb_hdmi_ddc_read_block_once(block, edid);
                if (rv == 0) {
                        if (attempt > 0) {
                                RKFB_VPRINTF("rkfb: external EDID block %d ready"
                                    " after %d retries PHY_STAT0=0x%02x\n",
                                    block, attempt, phy_stat);
                        }
                        return (0);
                }

retry:
                pause("rkfbddc", MAX(1, hz / 20));
        }

        return (rv);
}

static void
rkfb_hdmi_enable_hdmi_mode(struct rkfb_softc *sc)
{
        uint8_t hdcpcfg0;
        uint8_t pkt_en;

        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_INVIDCONF,
            RKFB_HDMI_FC_INVIDCONF_HDMI_1080P60);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_AVICONF3, 0x00);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_AVICONF0, 0x00);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_AVICONF1,
            RKFB_HDMI_FC_AVICONF1_PICTURE_ASPECT_16_9);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_AVICONF2, 0x00);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_AVIVID, RKFB_MODE_VIC);

        pkt_en = rkfb_hdmi_read1(sc, RKFB_HDMI_FC_PACKET_TX_EN);
        pkt_en |= RKFB_HDMI_FC_PACKET_TX_EN_AVI |
            RKFB_HDMI_FC_PACKET_TX_EN_GCP;
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_PACKET_TX_EN, pkt_en);
        /*
         * TRM 5.8.2 requires the AVI content update pulse before the
         * persistent send-enable bit. AVI_INFO_UP is self-clearing.
         */
        rkfb_hdmi_write1(sc, RKFB_HDMI_PKT_SEND_CTL,
            RKFB_HDMI_PKT_SEND_CTL_AVI_INFO_UP);
        DELAY(10);
        rkfb_hdmi_write1(sc, RKFB_HDMI_PKT_SEND_CTL,
            RKFB_HDMI_PKT_SEND_CTL_AVI_INFO_EN);

        hdcpcfg0 = rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG0);
        hdcpcfg0 |= RKFB_HDMI_A_HDCPCFG0_HDMIDVI;
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_HDCPCFG0, hdcpcfg0);
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_HDCPCFG1,
            RKFB_HDMI_A_HDCPCFG1_DEFAULT);
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_VIDPOLCFG,
            RKFB_HDMI_A_VIDPOLCFG_DATAENPOL);
        rkfb_hdmi_clear_overflow(sc);

        RKFB_VPRINTF("rkfb: HDMI mode enabled FC_INVIDCONF=0x%02x"
            " FC_PACKET_TX_EN=0x%02x PKT_SEND_CTL=0x%02x"
            " A_HDCPCFG0=0x%02x A_VIDPOLCFG=0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_FC_INVIDCONF),
            rkfb_hdmi_read1(sc, RKFB_HDMI_FC_PACKET_TX_EN),
            rkfb_hdmi_read1(sc, RKFB_HDMI_PKT_SEND_CTL),
            rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG0),
            rkfb_hdmi_read1(sc, RKFB_HDMI_A_VIDPOLCFG));
}

static void
rkfb_hdmi_enable_dvi_mode(struct rkfb_softc *sc)
{
        uint8_t hdcpcfg0;

        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_INVIDCONF,
            RKFB_HDMI_FC_INVIDCONF_DVI_1080P60);
        hdcpcfg0 = rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG0);
        hdcpcfg0 &= ~RKFB_HDMI_A_HDCPCFG0_HDMIDVI;
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_HDCPCFG0, hdcpcfg0);
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_HDCPCFG1,
            RKFB_HDMI_A_HDCPCFG1_DEFAULT);
        rkfb_hdmi_write1(sc, RKFB_HDMI_A_VIDPOLCFG,
            RKFB_HDMI_A_VIDPOLCFG_DATAENPOL);
}

static int
rkfb_hdmi_edid_is_hdmi_monitor(const uint8_t *ext)
{
        unsigned oui;
        int end;
        int i;
        int len;

        if (ext[0] != RKFB_CEA_EXT_TAG)
                return (0);

        end = ext[2];
        if (end == 0)
                end = 127;
        if (end < 4 || end > 127)
                return (0);

        for (i = 4; i < end; i += len + 1) {
                len = ext[i] & 0x1f;
                if (i + 1 + len > end)
                        break;
                if ((ext[i] >> 5) != RKFB_CEA_DB_VENDOR || len < 5)
                        continue;
                oui = ext[i + 1] | ((unsigned)ext[i + 2] << 8) |
                    ((unsigned)ext[i + 3] << 16);
                if (oui == RKFB_HDMI_IEEE_OUI)
                        return (1);
        }

        return (0);
}

static void
rkfb_hdmi_sink_probe(struct rkfb_softc *sc)
{
        static const uint8_t edid_magic[8] =
            { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
        uint8_t edid[RKFB_EXT_EDID_LENGTH];
        uint8_t ext[RKFB_EXT_EDID_LENGTH];
        int hdmi_monitor;
        int i;
        int valid;
        int rv;

        RKFB_VPRINTF("rkfb: sink ctrl FC_INVIDCONF=0x%02x A_HDCPCFG0=0x%02x"
            " A_HDCPCFG1=0x%02x A_VIDPOLCFG=0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_FC_INVIDCONF),
            rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG0),
            rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG1),
            rkfb_hdmi_read1(sc, RKFB_HDMI_A_VIDPOLCFG));

        rv = rkfb_hdmi_ddc_read_block(sc, 0, edid);
        if (rv != 0) {
                printf("rkfb: external EDID read failed rv=%d; preserving"
                    " current HDMI/DVI mode\n", rv);
                return;
        }

        RKFB_VPRINTF("rkfb: EDID[0:7]=%02x %02x %02x %02x %02x %02x %02x %02x\n",
            edid[0], edid[1], edid[2], edid[3],
            edid[4], edid[5], edid[6], edid[7]);
        RKFB_VPRINTF("rkfb: EDID extensions=%u preferred_dclk=%u kHz\n",
            edid[126],
            ((unsigned)edid[55] << 8 | edid[54]) * 10);

        valid = 1;
        for (i = 0; i < 8; i++) {
                if (edid[i] != edid_magic[i]) {
                        valid = 0;
                        break;
                }
        }
        RKFB_VPRINTF("rkfb: EDID header %s\n", valid ? "valid" : "invalid");

        hdmi_monitor = 0;
        if (valid && edid[126] > 0) {
                rv = rkfb_hdmi_ddc_read_block(sc, 1, ext);
                if (rv != 0) {
                        printf("rkfb: CTA extension read failed rv=%d\n", rv);
                } else {
                        hdmi_monitor = rkfb_hdmi_edid_is_hdmi_monitor(ext);
                        RKFB_VPRINTF("rkfb: CTA ext tag=0x%02x rev=%u dtd_offset=%u"
                            " hdmi_vsdb=%d\n",
                            ext[0], ext[1], ext[2], hdmi_monitor);
                }
        }

        if (hdmi_monitor != 0)
                rkfb_hdmi_enable_hdmi_mode(sc);
        else {
                printf("rkfb: sink not identified as HDMI, switching to DVI"
                    " mode\n");
                rkfb_hdmi_enable_dvi_mode(sc);
        }
}

static void
rkfb_fb_sync(struct rkfb_softc *sc)
{
        uint8_t phy_stat;
        uint8_t hdcpcfg0;
        uint8_t invidconf;

        /*
         * The VOP reads the framebuffer via DMA. Push CPU-written pixels to
         * the point of coherency so scanout sees the latest contents.
         */
        if (sc->hdmi_va != 0) {
                phy_stat = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_STAT0);
                hdcpcfg0 = rkfb_hdmi_read1(sc, RKFB_HDMI_A_HDCPCFG0);
                invidconf = rkfb_hdmi_read1(sc, RKFB_HDMI_FC_INVIDCONF);
                if ((phy_stat & 0x02) != 0 &&
                    (((hdcpcfg0 & RKFB_HDMI_A_HDCPCFG0_HDMIDVI) == 0) ||
                    invidconf != RKFB_HDMI_FC_INVIDCONF_HDMI_1080P60)) {
                        RKFB_VPRINTF("rkfb: fb_sync re-probing sink PHY_STAT0=0x%02x"
                            " FC_INVIDCONF=0x%02x A_HDCPCFG0=0x%02x\n",
                            phy_stat, invidconf, hdcpcfg0);
                        rkfb_hdmi_sink_probe(sc);
                }
        }

        if (sc->fb_dma_tag != NULL && sc->fb_dma_map != NULL)
                bus_dmamap_sync(sc->fb_dma_tag, sc->fb_dma_map,
                    BUS_DMASYNC_PREWRITE);
	else
		cpu_dcache_wb_range((void *)sc->fb_va,
		    round_page(sc->fb_size));
}

static void
rkfb_route_vop_to_hdmi(struct rkfb_softc *sc)
{
        rkfb_grf_write4(sc, RKFB_SYS_GRF_SOC_CON20,
            (RKFB_GRF_HDMI_LCDC_SEL << 16));
        rkfb_grf_write4(sc, RKFB_SYS_GRF_GPIO4C_IOMUX,
            RKFB_GRF_GPIO4C_I2C3HDMI);
        RKFB_VPRINTF("rkfb: GRF_SOC_CON20=0x%08x GPIO4C_IOMUX=0x%08x\n",
            rkfb_grf_read4(sc, RKFB_SYS_GRF_SOC_CON20),
            rkfb_grf_read4(sc, RKFB_SYS_GRF_GPIO4C_IOMUX));
}

static void
rkfb_display_domain_sanity(struct rkfb_softc *sc)
{
        uint32_t pwrdn_con, pwrdn_st, idle_req, idle_st, idle_ack;
        uint32_t gatedis0, clkgate10, clkgate28;
        int i;

        pwrdn_con = rkfb_pmu_read4(sc, RKFB_PMU_PWRDN_CON);
        pwrdn_st = rkfb_pmu_read4(sc, RKFB_PMU_PWRDN_ST);
        idle_req = rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_REQ);
        idle_st = rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_ST);
        idle_ack = rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_ACK);
        gatedis0 = rkfb_pmucru_read4(sc, RKFB_PMUCRU_GATEDIS_CON0);
        clkgate10 = rkfb_cru_read4(sc, RKFB_CRU_CLKGATE_CON10);
        clkgate28 = rkfb_cru_read4(sc, RKFB_CRU_CLKGATE_CON28);

        RKFB_VPRINTF("rkfb: display-domain before PWRDN_CON=0x%08x PWRDN_ST=0x%08x"
            " IDLE_REQ=0x%08x IDLE_ST=0x%08x IDLE_ACK=0x%08x"
            " PMUCRU_GATEDIS0=0x%08x CLKGATE10=0x%08x CLKGATE28=0x%08x\n",
            pwrdn_con, pwrdn_st, idle_req, idle_st, idle_ack,
            gatedis0, clkgate10, clkgate28);

        if ((pwrdn_st & RKFB_PMU_PD_VO) != 0) {
                rkfb_pmu_write4(sc, RKFB_PMU_PWRDN_CON,
                    pwrdn_con & ~RKFB_PMU_PD_VO);
                for (i = 0; i < 1000; i++) {
                        pwrdn_st = rkfb_pmu_read4(sc, RKFB_PMU_PWRDN_ST);
                        if ((pwrdn_st & RKFB_PMU_PD_VO) == 0)
                                break;
                        DELAY(10);
                }
                if ((pwrdn_st & RKFB_PMU_PD_VO) != 0)
                        printf("rkfb: pd_vo power-up timeout PWRDN_ST=0x%08x\n",
                            pwrdn_st);
        }

        if ((idle_req & (RKFB_PMU_IDLE_VOPB | RKFB_PMU_IDLE_VOPL)) != 0) {
                rkfb_pmu_write4(sc, RKFB_PMU_BUS_IDLE_REQ,
                    idle_req & ~(RKFB_PMU_IDLE_VOPB | RKFB_PMU_IDLE_VOPL));
                DELAY(10);
        }

        if ((gatedis0 & RKFB_PMUCRU_GATEDIS_VOPB) == 0) {
                rkfb_pmucru_write4(sc, RKFB_PMUCRU_GATEDIS_CON0,
                    gatedis0 | RKFB_PMUCRU_GATEDIS_VOPB);
                DELAY(10);
        }

        rkfb_cru_write4(sc, RKFB_CRU_CLKGATE_CON10,
            (RKFB_CRU_CLKGATE_VOP0_MASK << 16));
        rkfb_cru_write4(sc, RKFB_CRU_CLKGATE_CON28,
            (RKFB_CRU_CLKGATE_VOPB_MASK << 16));

        RKFB_VPRINTF("rkfb: display-domain after PWRDN_CON=0x%08x PWRDN_ST=0x%08x"
            " IDLE_REQ=0x%08x IDLE_ST=0x%08x IDLE_ACK=0x%08x"
            " PMUCRU_GATEDIS0=0x%08x CLKGATE10=0x%08x CLKGATE28=0x%08x\n",
            rkfb_pmu_read4(sc, RKFB_PMU_PWRDN_CON),
            rkfb_pmu_read4(sc, RKFB_PMU_PWRDN_ST),
            rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_REQ),
            rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_ST),
            rkfb_pmu_read4(sc, RKFB_PMU_BUS_IDLE_ACK),
            rkfb_pmucru_read4(sc, RKFB_PMUCRU_GATEDIS_CON0),
            rkfb_cru_read4(sc, RKFB_CRU_CLKGATE_CON10),
            rkfb_cru_read4(sc, RKFB_CRU_CLKGATE_CON28));
}

static int
rkfb_program_vpll_148500khz(struct rkfb_softc *sc)
{
        const uint32_t con3_mask = (0x3u << 8) | RKFB_CRU_PLL_DSMPD |
            RKFB_CRU_PLL_BYPASS | RKFB_CRU_PLL_POWER_DOWN;
        const uint32_t con1_mask = (0x7u << 12) | (0x7u << 8) | 0x3fu;
        uint32_t con3;
        int i;

        /* TRM Part 1 PLL formula: 24 / 4 * 99 / 4 / 1 = 148.5 MHz. */
        con3 = RKFB_CRU_PLL_MODE_SLOW | RKFB_CRU_PLL_DSMPD |
            RKFB_CRU_PLL_POWER_DOWN;
        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON3, (con3_mask << 16) | con3);
        DELAY(2);

        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON0,
            (0x0fffu << 16) | RKFB_VPLL_148500_FBDIV);
        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON1,
            (con1_mask << 16) |
            (RKFB_VPLL_148500_POSTDIV2 << 12) |
            (RKFB_VPLL_148500_POSTDIV1 << 8) |
            RKFB_VPLL_148500_REFDIV);
        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON2, 0x00000000);

        con3 = RKFB_CRU_PLL_MODE_SLOW | RKFB_CRU_PLL_DSMPD;
        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON3, (con3_mask << 16) | con3);

        for (i = 0; i < 5000; i++) {
                if ((rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON2) &
                    RKFB_CRU_VPLL_CON2_LOCK) != 0)
                        break;
                DELAY(10);
        }
        if (i == 5000) {
            printf("rkfb: VPLL lock timeout CON0=0x%08x CON1=0x%08x"
                " CON2=0x%08x CON3=0x%08x\n",
                rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON0),
                rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON1),
                rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON2),
                rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON3));
            return (ETIMEDOUT);
        }

        con3 = RKFB_CRU_PLL_MODE_NORMAL | RKFB_CRU_PLL_DSMPD;
        rkfb_cru_write4(sc, RKFB_CRU_VPLL_CON3, (con3_mask << 16) | con3);
        RKFB_VPRINTF("rkfb: VPLL 148.5MHz CON0=0x%08x CON1=0x%08x CON2=0x%08x"
            " CON3=0x%08x\n",
            rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON0),
            rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON1),
            rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON2),
            rkfb_cru_read4(sc, RKFB_CRU_VPLL_CON3));
        return (0);
}

static void
rkfb_vop_pulse_dclk_reset(struct rkfb_softc *sc)
{
        rkfb_cru_write4(sc, RKFB_CRU_SOFTRST_CON17,
            (RKFB_CRU_DRESETN_VOP0_REQ << 16) | RKFB_CRU_DRESETN_VOP0_REQ);
        DELAY(1000);
        rkfb_cru_write4(sc, RKFB_CRU_SOFTRST_CON17,
            (RKFB_CRU_DRESETN_VOP0_REQ << 16));
        DELAY(1000);
}

static void
rkfb_vop_init_1080p60(struct rkfb_softc *sc)
{
        uint32_t sys_ctrl, dsp_ctrl0, dsp_ctrl1;

        /*
         * RK3399 TRM Part 1:
         * - CRU_CLKSEL_CON47 selects aclk_vop0_pre / hclk_vop0_pre.
         * - CRU_CLKSEL_CON49 selects dclk_vop0.
         *
         * Match the Linux-proven clock path:
         *   VPLL = 148.5 MHz
         *   dclk_vop0 = VPLL / 1
         */
        if (rkfb_program_vpll_148500khz(sc) != 0)
                printf("rkfb: VPLL setup failed, continuing with current"
                    " clock tree\n");
        /*
         * Match the Linux-proven VOP bus-clock path on RK3399:
         *   aclk_vop0_pre = CPLL / (1 + 1)
         *   hclk_vop0_pre = aclk_vop0_pre / (3 + 1)
         *
         * Keep dclk_vop0 on the Linux-proven direct VPLL path below.
         */
        rkfb_cru_write4(sc, 0x01bc,
            ((((0x1fu << 8) | (0x3u << 6) | 0x1fu) << 16) |
            ((3u << 8) | (1u << 6) | 1u)));
        rkfb_cru_write4(sc, 0x01c4,
            ((((1u << 11) | (0x3u << 8) | 0xffu) << 16) |
            0x0000u));

        RKFB_VPRINTF("rkfb: VOP clocks CLKSEL47=0x%08x CLKSEL49=0x%08x\n",
            rkfb_cru_read4(sc, 0x01bc), rkfb_cru_read4(sc, 0x01c4));
        sys_ctrl = rkfb_vop_read4(sc, 0x0008);
        dsp_ctrl0 = rkfb_vop_read4(sc, 0x0010);
        dsp_ctrl1 = rkfb_vop_read4(sc, 0x0014);
        RKFB_VPRINTF("rkfb: VOP SYS_CTRL before=0x%08x DSP_CTRL0 before=0x%08x"
            " DSP_CTRL1 before=0x%08x WIN0_CTRL0 before=0x%08x\n",
            sys_ctrl,
            dsp_ctrl0,
            dsp_ctrl1,
            rkfb_vop_read4(sc, 0x0030));

        /*
         * Follow the RK3399 VOP model used by the BSD RK3399 VOP driver:
         * bit 11 enables the block, while the output-enable bits select the
         * active sink. Keep scanout on the direct physical-address path for
         * this standalone driver by clearing MMU_EN.
         */
        sys_ctrl &= ~(RKFB_VOP_SYS_CTRL_STANDBY |
            RKFB_VOP_SYS_CTRL_MMU_EN |
            RKFB_VOP_SYS_CTRL_EDP_EN |
            RKFB_VOP_SYS_CTRL_MIPI_EN |
            RKFB_VOP_SYS_CTRL_MIPI_DUAL);
        sys_ctrl |= RKFB_VOP_SYS_CTRL_ENABLE |
            RKFB_VOP_SYS_CTRL_RGB_EN |
            RKFB_VOP_SYS_CTRL_HDMI_EN;
        rkfb_vop_write4(sc, 0x0008, sys_ctrl);
        dsp_ctrl0 &= ~RKFB_VOP_DSP_OUT_MODE_MASK;
        dsp_ctrl0 |= RKFB_VOP_DSP_OUT_MODE_AAAA;
        rkfb_vop_write4(sc, 0x0010, dsp_ctrl0);
        dsp_ctrl1 &= ~(RKFB_VOP_DSP_CTRL1_HDMI_PIN_POL_MASK |
            RKFB_VOP_DSP_CTRL1_HDMI_DCLK_POL);
        dsp_ctrl1 |= RKFB_VOP_DSP_CTRL1_HDMI_PIN_POL_POS |
            RKFB_VOP_DSP_CTRL1_HDMI_DCLK_POL;
        rkfb_vop_write4(sc, 0x0014, dsp_ctrl1);
        rkfb_vop_write4(sc, 0x0018, RKFB_VOP_BG_RED);
        rkfb_vop_write4(sc, 0x0038, 0x00000000);
        rkfb_vop_write4(sc, 0x003c, sc->stride / 4);
        rkfb_vop_write4(sc, 0x0040, (uint32_t)sc->fb_pa);
        rkfb_vop_write4(sc, 0x0048,
            ((RKFB_MODE_HEIGHT - 1) << 16) | (RKFB_MODE_WIDTH - 1));
        rkfb_vop_write4(sc, 0x004c,
            ((RKFB_MODE_HEIGHT - 1) << 16) | (RKFB_MODE_WIDTH - 1));
        rkfb_vop_write4(sc, 0x0050,
            ((RKFB_MODE_VSYNC + RKFB_MODE_VBP) << 16) |
            (RKFB_MODE_HSYNC + RKFB_MODE_HBP));
        rkfb_vop_write4(sc, 0x006c, RKFB_VOP_WIN0_CTRL2_PRIMARY);
        rkfb_vop_write4(sc, 0x01cc, RKFB_VOP_BG_RED);
        rkfb_vop_write4(sc, 0x01d0, RKFB_VOP_BG_RED);
        rkfb_vop_write4(sc, RKFB_VOP_POST_DSP_HACT_INFO,
            ((RKFB_MODE_HSYNC + RKFB_MODE_HBP) << 16) |
            (RKFB_MODE_HSYNC + RKFB_MODE_HBP + RKFB_MODE_WIDTH));
        rkfb_vop_write4(sc, RKFB_VOP_POST_DSP_VACT_INFO,
            ((RKFB_MODE_VSYNC + RKFB_MODE_VBP) << 16) |
            (RKFB_MODE_VSYNC + RKFB_MODE_VBP + RKFB_MODE_HEIGHT));
        rkfb_vop_write4(sc, 0x0030, RKFB_VOP_WIN0_CTRL0_ENABLE);
        rkfb_vop_write4(sc, RKFB_VOP_DSP_HTOTAL_HS_END,
            (2200 << 16) | RKFB_MODE_HSYNC);
        rkfb_vop_write4(sc, RKFB_VOP_DSP_HACT_ST_END,
            ((RKFB_MODE_HSYNC + RKFB_MODE_HBP) << 16) |
            (RKFB_MODE_HSYNC + RKFB_MODE_HBP + RKFB_MODE_WIDTH));
        rkfb_vop_write4(sc, RKFB_VOP_DSP_VTOTAL_VS_END,
            (1125 << 16) | RKFB_MODE_VSYNC);
        rkfb_vop_write4(sc, RKFB_VOP_DSP_VACT_ST_END,
            ((RKFB_MODE_VSYNC + RKFB_MODE_VBP) << 16) |
            (RKFB_MODE_VSYNC + RKFB_MODE_VBP + RKFB_MODE_HEIGHT));
        rkfb_vop_write4(sc, 0x0000, 0x00000001);
        rkfb_vop_pulse_dclk_reset(sc);
        DELAY(40000);

        RKFB_VPRINTF("rkfb: VOP 1080p60 timing programmed"
            " SYS_CTRL=0x%08x DSP_CTRL0=0x%08x WIN0_CTRL0=0x%08x"
            " WIN0_CTRL2=0x%08x"
            " VIR=0x%08x YRGB_MST=0x%08x"
            " HTOTAL=0x%08x VTOTAL=0x%08x\n",
            rkfb_vop_read4(sc, 0x0008),
            rkfb_vop_read4(sc, 0x0010),
            rkfb_vop_read4(sc, 0x0030),
            rkfb_vop_read4(sc, 0x006c),
            rkfb_vop_read4(sc, 0x003c),
            rkfb_vop_read4(sc, 0x0040),
            rkfb_vop_read4(sc, RKFB_VOP_DSP_HTOTAL_HS_END),
            rkfb_vop_read4(sc, RKFB_VOP_DSP_VTOTAL_VS_END));
}

static void
rkfb_dw_hdmi_init_1080p60(struct rkfb_softc *sc)
{
        /*
         * Follow the documented Step B/D split and the existing FreeBSD
         * generic DWC-HDMI sequence:
         *   1. Program the frame composer first.
         *   2. Configure/power the PHY.
         *   3. Only then enable the video path, packetizer, and sampler.
         *
         * Leave the controller in DVI-compatible mode until the external
         * DDC probe finishes; the later sink probe can switch to HDMI mode.
         */
        RKFB_VPRINTF("rkfb: DW-HDMI init: FC\n");
        rkfb_hdmi_enable_dvi_mode(sc);
        rkfb_hdmi_write1_safe(sc, 0x1001, 0x80);
        rkfb_hdmi_write1_safe(sc, 0x1002, 0x07);
        rkfb_hdmi_write1_safe(sc, 0x1003, 0x18);
        rkfb_hdmi_write1_safe(sc, 0x1004, 0x01);
        rkfb_hdmi_write1_safe(sc, 0x1005, 0x38);
        rkfb_hdmi_write1_safe(sc, 0x1006, 0x04);
        rkfb_hdmi_write1_safe(sc, 0x1007, 0x2d);
        rkfb_hdmi_write1_safe(sc, 0x1008, 0x58);
        rkfb_hdmi_write1_safe(sc, 0x1009, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x100a, 0x2c);
        rkfb_hdmi_write1_safe(sc, 0x100b, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x100c, 0x04);
        rkfb_hdmi_write1_safe(sc, 0x100d, 0x05);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CTRLDUR, 12);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_EXCTRLDUR, 32);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_EXCTRLSPAC, 1);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH0PREAM, 0x0b);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH1PREAM, 0x16);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH2PREAM, 0x21);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_AVICONF3, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_GCP, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_AVICONF0, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_AVICONF1, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_AVICONF2, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_AVIVID, RKFB_MODE_VIC);
        rkfb_hdmi_write1_safe(sc, 0x01ff, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0184, 0xfe);
        RKFB_VPRINTF("rkfb: DW-HDMI pre-PHY composer programmed FC_INVIDCONF=0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_FC_INVIDCONF));
}

static void
rkfb_dw_hdmi_finish_1080p60(struct rkfb_softc *sc)
{
        uint8_t clkdis;
        uint8_t val;

        RKFB_VPRINTF("rkfb: DW-HDMI init: video path\n");
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_MC_FLOWCTRL, 0x00);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CTRLDUR, 12);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_EXCTRLDUR, 32);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_EXCTRLSPAC, 1);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH0PREAM, 0x0b);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH1PREAM, 0x16);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_CH2PREAM, 0x21);

        clkdis = rkfb_hdmi_read1(sc, RKFB_HDMI_MC_CLKDIS) &
            RKFB_HDMI_MC_CLKDIS_CECCLK_DISABLE;
        clkdis |= (uint8_t)~RKFB_HDMI_MC_CLKDIS_CECCLK_DISABLE;
        clkdis &= ~RKFB_HDMI_MC_CLKDIS_PIXELCLK_DISABLE;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_MC_CLKDIS, clkdis);
        clkdis &= ~RKFB_HDMI_MC_CLKDIS_TMDSCLK_DISABLE;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_MC_CLKDIS, clkdis);
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_FC_VSYNCINWIDTH, RKFB_MODE_VSYNC);

        RKFB_VPRINTF("rkfb: DW-HDMI init: VP\n");
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_PR_CD, 0x40);

        val = rkfb_hdmi_read1(sc, RKFB_HDMI_VP_STUFF);
        val &= ~0x01;
        val |= 0x01;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_STUFF, val);

        val = rkfb_hdmi_read1(sc, RKFB_HDMI_VP_CONF);
        val &= ~(0x10 | 0x04);
        val |= 0x04;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_CONF, val);

        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_REMAP, 0x00);

        val = rkfb_hdmi_read1(sc, RKFB_HDMI_VP_CONF);
        val &= ~(0x40 | 0x20 | 0x08);
        val |= 0x40;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_CONF, val);

        val = rkfb_hdmi_read1(sc, RKFB_HDMI_VP_STUFF);
        val &= ~(0x02 | 0x04);
        val |= 0x02 | 0x04;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_STUFF, val);

        val = rkfb_hdmi_read1(sc, RKFB_HDMI_VP_CONF);
        val &= ~0x03;
        val |= 0x03;
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_VP_CONF, val);

        RKFB_VPRINTF("rkfb: DW-HDMI init: TX\n");
        rkfb_hdmi_write1_safe(sc, RKFB_HDMI_TX_INVID0, 0x01);
        rkfb_hdmi_write1_safe(sc, 0x0201, 0x07);
        rkfb_hdmi_write1_safe(sc, 0x0202, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0203, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0204, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0205, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0206, 0x00);
        rkfb_hdmi_write1_safe(sc, 0x0207, 0x00);

        rkfb_hdmi_clear_overflow(sc);
        rkfb_hdmi_log_clock_present(sc, "after DW-HDMI post-PHY init");
        RKFB_VPRINTF("rkfb: DW-HDMI post-PHY video path programmed"
            " VP_CONF=0x%02x TX_INVID0=0x%02x MC_CLKDIS=0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_VP_CONF),
            rkfb_hdmi_read1(sc, RKFB_HDMI_TX_INVID0),
            rkfb_hdmi_read1(sc, RKFB_HDMI_MC_CLKDIS));
}

/* -------------------------------------------------------------------------
 * HDMI PHY init sequence
 *
 * Based on RK3399 TRM Part 2:
 * - section 5.4 PHY register descriptions
 * - section 5.6 programming order
 * - table 5-6 for the 148.5 MHz MPLL values
 * ---------------------------------------------------------------------- */

static void
rkfb_hdmi_phy_init(struct rkfb_softc *sc)
{
        uint8_t phy_conf0;
        uint8_t stat;
        int iter;
        int timeout;

        RKFB_VPRINTF("rkfb: starting HDMI PHY init\n");

        /*
         * Enable HDCP/HDMI clocks at CRU.
         * CRU uses hiword-update format: bits[31:16] = mask, bits[15:0] = value.
         * Writing 0 to a gate bit enables the clock.
         */
        rkfb_cru_write4(sc, 0x0240,
            (1u << 25) | (1u << 26) | (0 << 9) | (0 << 10));
        rkfb_cru_write4(sc, 0x0244,
            (1u << 18) | (0 << 2));
        rkfb_cru_write4(sc, 0x0250,
            (1u << 28) | (0 << 12));
        rkfb_cru_write4(sc, 0x0254,
            (1u << 24) | (0 << 8));
        DELAY(10000);

        /*
         * NetBSD's DW-HDMI PHY path performs the GEN2 configuration twice.
         * Keep the same pre/post PHY_CONF0 state transitions around each pass.
         */
        for (iter = 0; iter < 2; iter++) {
                rkfb_hdmi_write1(sc, RKFB_HDMI_MC_FLOWCTRL, 0x00);
                rkfb_hdmi_write1(sc, RKFB_HDMI_MC_PHYRSTZ, 0x01);
                rkfb_hdmi_write1(sc, RKFB_HDMI_VP_PR_CD, 0x40);
                DELAY(5000);
                rkfb_hdmi_write1(sc, RKFB_HDMI_MC_PHYRSTZ, 0x00);
                DELAY(5000);
                rkfb_hdmi_write1(sc, RKFB_HDMI_MC_HEACPHY_RST, 0x01);

                rkfb_hdmi_write1(sc, RKFB_HDMI_BASE_SFRDIVLOW,
                    RKFB_HDMI_BASE_SFRDIVLOW_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_BASE_SFRDIVHIGH,
                    RKFB_HDMI_BASE_SFRDIVHIGH_DEFAULT);

                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_JTAG_CFG,
                    RKFB_HDMI_PHY_JTAG_CFG_I2C);
                if (rkfb_hdmi_phy_i2c_reset(sc) != 0)
                        return;
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SLAVE,
                    HDMI_PHY_I2C_ADDR);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_DIV,
                    RKFB_HDMI_PHY_I2CM_DIV_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SS_HCNT1, 0x00);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SS_HCNT0,
                    RKFB_HDMI_PHY_I2CM_SS_HCNT0_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SS_LCNT1, 0x00);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SS_LCNT0,
                    RKFB_HDMI_PHY_I2CM_SS_LCNT0_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_FS_HCNT1, 0x00);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_FS_HCNT0,
                    RKFB_HDMI_PHY_I2CM_FS_HCNT0_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_FS_LCNT1, 0x00);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_FS_LCNT0,
                    RKFB_HDMI_PHY_I2CM_FS_LCNT0_DEFAULT);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_I2CM_SDA_HOLD,
                    RKFB_HDMI_PHY_I2CM_SDA_HOLD_DEFAULT);

                phy_conf0 = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_CONF0);
                phy_conf0 |= RKFB_HDMI_PHY_CONF0_SELDATAENPOL |
                    RKFB_HDMI_PHY_CONF0_PDDQ;
                phy_conf0 &= ~(RKFB_HDMI_PHY_CONF0_SELDIPIF |
                    RKFB_HDMI_PHY_CONF0_ENTMDS |
                    RKFB_HDMI_PHY_CONF0_PDZ |
                    RKFB_HDMI_PHY_CONF0_TXPWRON |
                    RKFB_HDMI_PHY_CONF0_SVSRET);
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_CONF0, phy_conf0);
                DELAY(1000);

                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_CPCE_CTRL,
                    RKFB_HDMI_PHY_148500_CPCE_CTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_GMPCTRL,
                    RKFB_HDMI_PHY_148500_GMPCTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_CURRCTRL,
                    RKFB_HDMI_PHY_148500_CURRCTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_PLLPHBYCTRL,
                    0x0000) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_MSM_CTRL,
                    RKFB_HDMI_PHY_148500_MSM_CTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_TXTERM,
                    RKFB_HDMI_PHY_148500_TXTERM) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_CKSYMTXCTRL,
                    RKFB_HDMI_PHY_148500_CKSYMTXCTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_VLEVCTRL,
                    RKFB_HDMI_PHY_148500_VLEVCTRL) != 0)
                        return;
                if (rkfb_hdmi_phy_i2c_write(sc, RKFB_HDMI_PHY_I2C_CKCALCTRL,
                    RKFB_HDMI_PHY_I2C_CKCALCTRL_OVERRIDE) != 0)
                        return;

                RKFB_VPRINTF("rkfb: PHY I2C configured for 148.5 MHz pass %d\n",
                    iter + 1);

                phy_conf0 = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_CONF0);
                phy_conf0 |= RKFB_HDMI_PHY_CONF0_PDZ;
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_CONF0, phy_conf0);
                DELAY(1000);

                phy_conf0 &= ~RKFB_HDMI_PHY_CONF0_ENTMDS;
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_CONF0, phy_conf0);
                DELAY(1000);
                phy_conf0 |= RKFB_HDMI_PHY_CONF0_ENTMDS;
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_CONF0, phy_conf0);
                DELAY(1000);

                phy_conf0 |= RKFB_HDMI_PHY_CONF0_TXPWRON |
                    RKFB_HDMI_PHY_CONF0_SVSRET;
                phy_conf0 &= ~RKFB_HDMI_PHY_CONF0_PDDQ;
                rkfb_hdmi_write1(sc, RKFB_HDMI_PHY_CONF0, phy_conf0);
                DELAY(5000);
        }

        /*
         * TRM 5.6 step B(4e/f):
         * enable the pixel/TMDS data paths after PHY configuration and
         * re-write VSYNC width.
         */
        rkfb_hdmi_write1(sc, RKFB_HDMI_MC_CLKDIS, 0x00);
        rkfb_hdmi_write1(sc, RKFB_HDMI_FC_VSYNCINWIDTH, RKFB_MODE_VSYNC);
        rkfb_hdmi_toggle_main_reset(sc,
            RKFB_HDMI_MC_SWRST_TMDS | RKFB_HDMI_MC_SWRST_PIXEL);
        rkfb_hdmi_log_clock_present(sc, "before PHY lock poll");

        RKFB_VPRINTF("rkfb: PHY powered up, waiting for PLL lock\n");

        /* PHY_STAT0 bit 0 is TX_PHY_LOCK; bit 1 is HPD. */
        for (timeout = 20; timeout > 0; timeout--) {
                DELAY(5000);
                stat = rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_STAT0);
                if ((stat & 0x01) != 0) {
                        RKFB_VPRINTF("rkfb: PHY locked! PHY_STAT0=0x%02x HPD=%d\n",
                            stat, (stat >> 1) & 1);
                        break;
                }
        }
        if (timeout == 0)
                printf("rkfb: PHY lock timeout PHY_STAT0=0x%02x\n",
                    rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_STAT0));

        rkfb_hdmi_write1(sc, 0x01ff, 0x00);
        rkfb_hdmi_write1(sc, 0x0184, 0xfe);

        RKFB_VPRINTF("rkfb: HDMI PHY init complete\n");
        RKFB_VPRINTF("rkfb: PHY_CONF0 [0x3000] = 0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_CONF0));
        RKFB_VPRINTF("rkfb: PHY_STAT0 [0x3004] = 0x%02x\n",
            rkfb_hdmi_read1(sc, RKFB_HDMI_PHY_STAT0));
        RKFB_VPRINTF("rkfb: IH_PHY    [0x0104] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x0104));
        rkfb_hdmi_log_clock_present(sc, "after PHY init");
}

/* -------------------------------------------------------------------------
 * cdev operations
 * ---------------------------------------------------------------------- */

static d_open_t  rkfb_open;
static d_close_t rkfb_close;
static d_ioctl_t rkfb_ioctl;
static d_read_t  rkfb_read;
static d_write_t rkfb_write;

static struct cdevsw rkfb_cdevsw = {
        .d_version = D_VERSION,
        .d_open    = rkfb_open,
        .d_close   = rkfb_close,
        .d_ioctl   = rkfb_ioctl,
        .d_read    = rkfb_read,
        .d_write   = rkfb_write,
        .d_name    = "rkfb",
};

static int
rkfb_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        if (sc == NULL)
                return (ENXIO);
        return (0);
}

static int
rkfb_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        if (sc == NULL)
                return (ENXIO);
        return (0);
}

static int
rkfb_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
        struct rkfb_softc *sc = dev->si_drv1;
        struct rkfb_info  *info;

        if (sc == NULL)
                return (ENXIO);

        switch (cmd) {

        /* ---- register read (multi-block) -------------------------------- */
        case RKFB_REG_READ: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;

                switch (ro->block) {
                case 0: /* VOP — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->vop_size)
                                return (EINVAL);
                        ro->val = rkfb_vop_read4(sc, ro->off);
                        return (0);
                case 1: /* GRF — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->grf_size)
                                return (EINVAL);
                        ro->val = rkfb_grf_read4(sc, ro->off);
                        return (0);
                case 2: /* CRU — 32-bit aligned */
                        if ((ro->off & 0x3) != 0)
                                return (EINVAL);
                        if (ro->off >= sc->cru_size)
                                return (EINVAL);
                        ro->val = rkfb_cru_read4(sc, ro->off);
                        return (0);
                case 3: /* HDMI — 8-bit regs on 32-bit-spaced bus */
                        if ((ro->off << 2) >= sc->hdmi_size)
                                return (EINVAL);
                        ro->val = rkfb_hdmi_read1(sc, ro->off);
                        return (0);
                default:
                        return (EINVAL);
                }
        }

        /* ---- HDMI byte write -------------------------------------------- */
        case RKFB_HDMI_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;
                if ((ro->off << 2) >= sc->hdmi_size)
                        return (EINVAL);
                printf("rkfb: HDMI_WRITE[0x%04x] <= 0x%02x\n",
                    ro->off, ro->val & 0xff);
                rkfb_hdmi_write1(sc, ro->off, (uint8_t)(ro->val & 0xff));
                return (0);
        }

        /* ---- VOP masked write ------------------------------------------- */
        case RKFB_VOP_MASKWRITE: {
                struct rkfb_regmaskop *mo = (struct rkfb_regmaskop *)data;
                uint32_t writeval;

                if ((mo->off & 0x3) != 0)
                        return (EINVAL);
                if (mo->off >= sc->vop_size)
                        return (EINVAL);
                if (!rkfb_vop_write_allowed(mo->off))
                        return (EPERM);

                writeval = ((mo->mask & 0xffff) << 16) | (mo->val & 0xffff);
                printf("rkfb: MASKWRITE VOP[0x%04x] mask=0x%04x "
                    "val=0x%04x raw=0x%08x\n",
                    mo->off, mo->mask & 0xffff, mo->val & 0xffff, writeval);
                rkfb_vop_write4(sc, mo->off, writeval);
                return (0);
        }

        /* ---- raw register write (multi-block) ----------------------------- */
        case RKFB_REG_WRITE: {
                struct rkfb_regop *ro = (struct rkfb_regop *)data;

                if ((ro->off & 0x3) != 0)
                        return (EINVAL);

                switch (ro->block) {
                case 0: /* VOP — allowlist enforced */
                        if (ro->off >= sc->vop_size)
                                return (EINVAL);
                        if (!rkfb_vop_write_allowed(ro->off))
                                return (EPERM);
                        printf("rkfb: REG_WRITE VOP[0x%04x] <= 0x%08x\n",
                            ro->off, ro->val);
                        rkfb_vop_write4(sc, ro->off, ro->val);
                        return (0);
                case 1: /* GRF */
                        if (ro->off >= sc->grf_size)
                                return (EINVAL);
                        printf("rkfb: REG_WRITE GRF[0x%04x] <= 0x%08x\n",
                            ro->off, ro->val);
                        rkfb_grf_write4(sc, ro->off, ro->val);
                        return (0);
                case 2: /* CRU */
                        if (ro->off >= sc->cru_size)
                                return (EINVAL);
                        printf("rkfb: REG_WRITE CRU[0x%04x] <= 0x%08x\n",
                            ro->off, ro->val);
                        rkfb_cru_write4(sc, ro->off, ro->val);
                        return (0);
                default:
                        return (EPERM);
                }
        }

        /* ---- VOP range dump --------------------------------------------- */
        case RKFB_VOP_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i, off;

                if ((rd->base & 0x3) != 0)
                        return (EINVAL);
                if (rd->count == 0 || rd->count > 64)
                        return (EINVAL);
                if (rd->base + rd->count * 4 > sc->vop_size)
                        return (EINVAL);

                printf("rkfb: ---- VOP dump base=0x%08x count=%u ----\n",
                    rd->base, rd->count);
                for (i = 0; i < rd->count; i++) {
                        off = rd->base + i * 4;
                        printf("rkfb: VOP[0x%04x] = 0x%08x\n",
                            off, rkfb_vop_read4(sc, off));
                }
                printf("rkfb: ------------------------------------------\n");
                return (0);
        }

        /* ---- HDMI range dump -------------------------------------------- */
        case RKFB_HDMI_DUMP_RANGE: {
                struct rkfb_regdump *rd = (struct rkfb_regdump *)data;
                uint32_t i, off;

                if (rd->count == 0 || rd->count > 256)
                        return (EINVAL);
                if (((rd->base + rd->count - 1) << 2) >= sc->hdmi_size)
                        return (EINVAL);

                printf("rkfb: ---- HDMI dump base=0x%04x count=%u ----\n",
                    rd->base, rd->count);
                for (i = 0; i < rd->count; i++) {
                        off = rd->base + i;
                        printf("rkfb: HDMI[0x%04x] = 0x%02x\n",
                            off, rkfb_hdmi_read1(sc, off));
                }
                printf("rkfb: -----------------------------------------\n");
                return (0);
        }

        /* ---- misc -------------------------------------------------------- */
        case RKFB_DUMPREGS:
                printf("rkfb: ---- register dump ----\n");
                printf("rkfb: VOP[0x0000] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0000));
                printf("rkfb: VOP[0x0004] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0004));
                printf("rkfb: VOP[0x0008] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0008));
                printf("rkfb: VOP[0x0010] = 0x%08x\n",
                    rkfb_vop_read4(sc, 0x0010));
                printf("rkfb: GRF[0x0000] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0000));
                printf("rkfb: GRF[0x0004] = 0x%08x\n",
                    rkfb_grf_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0000] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0000));
                printf("rkfb: CRU[0x0004] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0004));
                printf("rkfb: CRU[0x0008] = 0x%08x\n",
                    rkfb_cru_read4(sc, 0x0008));
                printf("rkfb: -----------------------\n");
                return (0);

        case RKFB_GETINFO:
                info = (struct rkfb_info *)data;
                info->width   = sc->width;
                info->height  = sc->height;
                info->bpp     = sc->bpp;
                info->stride  = sc->stride;
                info->fb_size = sc->fb_size;
		info->fb_pa   = (uint64_t)sc->fb_pa;
                return (0);

        case RKFB_CLEAR: {
                struct rkfb_fill *fill = (struct rkfb_fill *)data;
                uint32_t *p = (uint32_t *)sc->fb_va;
                size_t count = sc->fb_size / sizeof(uint32_t);
                size_t i;
                for (i = 0; i < count; i++)
                        p[i] = fill->pixel | 0xff000000u;
                rkfb_fb_sync(sc);
                return (0);
        }

        case RKFB_FILLRECT: {
                struct rkfb_rect *r = (struct rkfb_rect *)data;
                uint32_t *fb = (uint32_t *)sc->fb_va;
                uint32_t x, y, max_x, max_y;

                if (r->x >= sc->width || r->y >= sc->height)
                        return (EINVAL);

                max_x = r->x + r->w;
                max_y = r->y + r->h;
                if (max_x > sc->width)  max_x = sc->width;
                if (max_y > sc->height) max_y = sc->height;

                RKFB_VPRINTF("rkfb: fillrect x=%u y=%u w=%u h=%u pixel=0x%08x\n",
                    r->x, r->y, r->w, r->h, r->pixel);

                for (y = r->y; y < max_y; y++)
                        for (x = r->x; x < max_x; x++)
                                fb[y * sc->width + x] =
                                    r->pixel | 0xff000000u;

                rkfb_fb_sync(sc);
                return (0);
        }

        default:
                return (ENOTTY);
        }
}

static int
rkfb_read(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t available;

        if (sc == NULL)
                return (ENXIO);
        if ((size_t)uio->uio_offset >= sc->fb_size)
                return (0);

        available = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)available)
                available = uio->uio_resid;

        return (uiomove((void *)(sc->fb_va + uio->uio_offset),
            available, uio));
}

static int
rkfb_write(struct cdev *dev, struct uio *uio, int ioflag)
{
        struct rkfb_softc *sc = dev->si_drv1;
        size_t available;

        if (sc == NULL)
                return (ENXIO);
        if ((size_t)uio->uio_offset >= sc->fb_size)
                return (ENOSPC);

        available = sc->fb_size - (size_t)uio->uio_offset;
        if (uio->uio_resid < (ssize_t)available)
                available = uio->uio_resid;

        {
                int error;

                error = uiomove((void *)(sc->fb_va + uio->uio_offset),
                    available, uio);
                if (error == 0)
                        rkfb_fb_sync(sc);

                return (error);
        }
}

/* -------------------------------------------------------------------------
 * Module load / unload
 * ---------------------------------------------------------------------- */

static int
rkfb_modevent(module_t mod, int type, void *data)
{
        struct rkfb_softc *sc = &g_rkfb_sc;
        int error = 0;

        switch (type) {
        case MOD_LOAD:
                error = rkfb_attach(sc);
                break;

        case MOD_UNLOAD:
                rkfb_detach(sc);
                break;

        default:
                error = EOPNOTSUPP;
                break;
        }

        return (error);
}

DEV_MODULE(rkfb, rkfb_modevent, NULL);
MODULE_VERSION(rkfb, 1);
