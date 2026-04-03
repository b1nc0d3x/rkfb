/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Kyle T. Crenshaw
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Register offsets derived from hardware documentation and cross-referenced
 * against Linux drivers/gpu/drm/rockchip/dw_hdmi-rockchip.c (GPL-2.0) and
 * drivers/gpu/drm/bridge/synopsys/dw-hdmi.h (GPL-2.0).
 * Hardware register addresses are factual descriptions of the RK3399 SoC
 * and the embedded Synopsys DesignWare HDMI controller; they are not subject
 * to copyright protection.
 *
 * RK3399 HDMI controller:
 *   Base address: 0xff940000  (size 0x20000)
 *   IRQ: SPI 23
 *   Clocks: hdmiphy (VPLL), grf (pclk_vio_grf)
 *
 * The RK3399 uses a Synopsys DesignWare HDMI 2.0 TX IP (dw-hdmi).
 * Registers below 0x200 are DW-HDMI standard; offsets above are
 * Rockchip-specific GRF (General Register Files) glue registers.
 *
 * The HDMI controller output is routed via GRF_SOC_CON20 to either
 * VOPB or VOPL.  VOPB (big VOP) supports 4K; VOPL supports up to 2K.
 */

#ifndef _RKFB_HDMI_REGS_H_
#define _RKFB_HDMI_REGS_H_

/*
 * Physical base address
 */
#define RK3399_HDMI_BASE		0xff940000
#define RK3399_HDMI_SIZE		0x20000

/*
 * GRF VOP->HDMI routing (within GRF, base 0xff770000)
 * Write via hiword-update: upper 16 bits = mask, lower 16 = value.
 */
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		(1 << 6)  /* 0 = VOPB (4K), 1 = VOPL (2K) */

#define RK3399_HIWORD_UPDATE(val, mask)	((val) | ((mask) << 16))

/* Select VOPB (big, supports 4K) as HDMI source */
#define RK3399_HDMI_SEL_VOPB		RK3399_HIWORD_UPDATE(0,                    RK3399_HDMI_LCDC_SEL)
/* Select VOPL (lit, 2K max) as HDMI source */
#define RK3399_HDMI_SEL_VOPL		RK3399_HIWORD_UPDATE(RK3399_HDMI_LCDC_SEL, RK3399_HDMI_LCDC_SEL)

/*
 * -----------------------------------------------------------------------
 * Synopsys DesignWare HDMI TX register offsets
 * All offsets are byte addresses relative to RK3399_HDMI_BASE.
 * The DW-HDMI core uses an 8-bit register bus internally; each register
 * occupies one byte in the register map (accessed as 32-bit MMIO on RK3399
 * with byte-enables or byte-wide reads).
 * -----------------------------------------------------------------------
 */

/* --- Identification / version ------------------------------------------ */
#define HDMI_DESIGN_ID			0x0000	/* (RO) Design ID (0x00) */
#define HDMI_REVISION_ID		0x0001	/* (RO) Revision ID */
#define HDMI_PRODUCT_ID0		0x0002	/* (RO) Product ID byte 0 (0xa0 = HDMI TX) */
#define HDMI_PRODUCT_ID1		0x0003	/* (RO) Product ID byte 1 */
#define HDMI_CONFIG0_ID			0x0004	/* (RO) Config 0 capabilities */
#define HDMI_CONFIG1_ID			0x0005	/* (RO) Config 1 capabilities */
#define HDMI_CONFIG2_ID			0x0006	/* (RO) Config 2 capabilities */
#define HDMI_CONFIG3_ID			0x0007	/* (RO) Config 3 capabilities */

/* --- Interrupt controller ---------------------------------------------- */
#define HDMI_IH_FC_STAT0		0x0100	/* Frame composer intr status 0 */
#define HDMI_IH_FC_STAT1		0x0101
#define HDMI_IH_FC_STAT2		0x0102
#define HDMI_IH_AS_STAT0		0x0103	/* Audio sampler intr status */
#define HDMI_IH_PHY_STAT0		0x0104	/* PHY interrupt status */
#define   HDMI_IH_PHY_STAT0_HPD		(1 << 0)  /* Hot plug detect */
#define   HDMI_IH_PHY_STAT0_TX_PHY_LOCK (1 << 4)
#define HDMI_IH_I2CM_STAT0		0x0105	/* I2C master intr status */
#define   HDMI_IH_I2CM_STAT0_DONE	(1 << 1)
#define   HDMI_IH_I2CM_STAT0_ERROR	(1 << 0)
#define HDMI_IH_CEC_STAT0		0x0106
#define HDMI_IH_VP_STAT0		0x0107	/* Video packing intr status */
#define HDMI_IH_I2CMPHY_STAT0		0x0108	/* PHY I2C master intr status */
#define HDMI_IH_AHBDMAAUD_STAT0		0x0109	/* AHB DMA audio intr status */

/* Interrupt mute (write 1 to mask, 0 to unmask) */
#define HDMI_IH_MUTE_FC_STAT0		0x0180
#define HDMI_IH_MUTE_FC_STAT1		0x0181
#define HDMI_IH_MUTE_FC_STAT2		0x0182
#define HDMI_IH_MUTE_AS_STAT0		0x0183
#define HDMI_IH_MUTE_PHY_STAT0		0x0184
#define HDMI_IH_MUTE_I2CM_STAT0	0x0185
#define HDMI_IH_MUTE_CEC_STAT0		0x0186
#define HDMI_IH_MUTE_VP_STAT0		0x0187
#define HDMI_IH_MUTE_I2CMPHY_STAT0	0x0188
#define HDMI_IH_MUTE_AHBDMAAUD_STAT0	0x0189
#define HDMI_IH_MUTE			0x01ff  /* Global mute (bit 1=all) */

/* --- TX / video configuration ------------------------------------------ */
#define HDMI_TX_INVID0			0x0200  /* Video input mapping */
#define   HDMI_TX_INVID0_INTERNAL_DE_GEN_EN (1 << 7)
#define   HDMI_TX_INVID0_VIDEO_MAPPING_MASK 0x1f
#define HDMI_TX_INSTUFFING		0x0201
#define HDMI_TX_GYDATA0			0x0202
#define HDMI_TX_GYDATA1			0x0203
#define HDMI_TX_RCRDATA0		0x0204
#define HDMI_TX_RCRDATA1		0x0205
#define HDMI_TX_BCBDATA0		0x0206
#define HDMI_TX_BCBDATA1		0x0207

/* --- Video packing ------------------------------------------------------- */
#define HDMI_VP_STATUS			0x0800
#define HDMI_VP_PR_CD			0x0801  /* Pixel repetition and color depth */
#define   HDMI_VP_PR_CD_COLOR_DEPTH_24	(0x4 << 4)
#define   HDMI_VP_PR_CD_COLOR_DEPTH_30	(0x5 << 4)
#define   HDMI_VP_PR_CD_COLOR_DEPTH_36	(0x6 << 4)
#define   HDMI_VP_PR_CD_DESIRED_PR_FACTOR_MASK 0xf
#define HDMI_VP_STUFF			0x0802
#define HDMI_VP_REMAP			0x0803
#define HDMI_VP_CONF			0x0804
#define   HDMI_VP_CONF_BYPASS_SELECT	(1 << 2)
#define   HDMI_VP_CONF_BYPASS_EN	(1 << 6)
#define   HDMI_VP_CONF_PP_EN		(1 << 5)   /* Pixel packing enable */
#define   HDMI_VP_CONF_YCC422_EN	(1 << 3)
#define HDMI_VP_MASK			0x0807

/* --- Frame composer (FC) ------------------------------------------------ */
#define HDMI_FC_INVIDCONF		0x1000  /* Input video config */
#define   HDMI_FC_INVIDCONF_HDCP_KEEPOUT (1 << 7)
#define   HDMI_FC_INVIDCONF_VSYNC_IN_POLARITY_ACTIVE_HIGH (1 << 6)
#define   HDMI_FC_INVIDCONF_HSYNC_IN_POLARITY_ACTIVE_HIGH (1 << 5)
#define   HDMI_FC_INVIDCONF_DE_IN_POLARITY_ACTIVE_HIGH (1 << 4)
#define   HDMI_FC_INVIDCONF_DVI_MODE	(0 << 3)
#define   HDMI_FC_INVIDCONF_HDMI_MODE	(1 << 3)
#define   HDMI_FC_INVIDCONF_R_V_BLANK_IN_OSC (1 << 1)
#define   HDMI_FC_INVIDCONF_IN_I_P_PROGRESSIVE (1 << 0)
#define HDMI_FC_INHACTV0		0x1001  /* H active pixels [7:0] */
#define HDMI_FC_INHACTV1		0x1002  /* H active pixels [12:8] */
#define HDMI_FC_INHBLANK0		0x1003  /* H blanking [7:0] */
#define HDMI_FC_INHBLANK1		0x1004  /* H blanking [9:8] */
#define HDMI_FC_INVACTV0		0x1005  /* V active lines [7:0] */
#define HDMI_FC_INVACTV1		0x1006  /* V active lines [12:8] */
#define HDMI_FC_INVBLANK		0x1007  /* V blanking */
#define HDMI_FC_HSYNCINDELAY0		0x1008
#define HDMI_FC_HSYNCINDELAY1		0x1009
#define HDMI_FC_HSYNCINWIDTH0		0x100a
#define HDMI_FC_HSYNCINWIDTH1		0x100b
#define HDMI_FC_VSYNCINDELAY		0x100c
#define HDMI_FC_VSYNCINWIDTH		0x100d
#define HDMI_FC_INFREQ0			0x100e  /* Input pixel clock [7:0] (kHz) */
#define HDMI_FC_INFREQ1			0x100f
#define HDMI_FC_INFREQ2			0x1010
#define HDMI_FC_CTRLDUR			0x1011  /* Control period minimum duration */
#define HDMI_FC_EXCTRLDUR		0x1012
#define HDMI_FC_EXCTRLSPAC		0x1013
#define HDMI_FC_CH0PREAM		0x1014  /* Preamble / channel 0 */
#define HDMI_FC_CH1PREAM		0x1015
#define HDMI_FC_CH2PREAM		0x1016
#define HDMI_FC_AVICONF0		0x1017  /* AVI infoframe config 0 */
#define HDMI_FC_AVICONF1		0x1018
#define HDMI_FC_AVICONF2		0x1019
#define HDMI_FC_AVIVID			0x101b  /* AVI infoframe VIC (CEA-861 Video ID Code) */
#define HDMI_FC_AUDSCONF		0x1063  /* Audio sample config */
#define HDMI_FC_AUDSCHNL0		0x1067
#define HDMI_FC_MASK0			0x10d2  /* FC interrupt mask 0 */
#define HDMI_FC_MASK1			0x10d6
#define HDMI_FC_MASK2			0x10da

/* --- PHY ---------------------------------------------------------------- */
#define HDMI_PHY_CONF0			0x3000  /* PHY control 0 */
#define   HDMI_PHY_CONF0_PDZ		(1 << 7)  /* Power-down polarity */
#define   HDMI_PHY_CONF0_ENTMDS		(1 << 6)  /* Enable TMDS clock */
#define   HDMI_PHY_CONF0_SPARECTRL	(1 << 5)
#define   HDMI_PHY_CONF0_GEN2_TXPWRON	(1 << 3)  /* Gen2 TX power on */
#define   HDMI_PHY_CONF0_GEN2_PDDQ	(1 << 2)  /* Gen2 power-down TX */
#define   HDMI_PHY_CONF0_SELDATAENPOL	(1 << 1)
#define   HDMI_PHY_CONF0_SELDIPIF	(1 << 0)

#define HDMI_PHY_TST0			0x3001
#define   HDMI_PHY_TST0_TSTCLRGLBL	(1 << 7)
#define   HDMI_PHY_TST0_TSTEN		(1 << 2)
#define   HDMI_PHY_TST0_TSTCLK		(1 << 1)
#define HDMI_PHY_TST1			0x3002
#define HDMI_PHY_TST2			0x3003
#define HDMI_PHY_STAT0			0x3004  /* (RO) PHY status 0 */
#define   HDMI_PHY_STAT0_TX_PHY_LOCK	(1 << 4)  /* PLL locked */
#define   HDMI_PHY_STAT0_HPD		(1 << 1)  /* Hot-plug detect level */
#define HDMI_PHY_INT0			0x3005
#define HDMI_PHY_MASK0			0x3006
#define HDMI_PHY_POL0			0x3007
#define   HDMI_PHY_POL0_HPD		(1 << 1)

/* --- PHY I2C master (for external/internal PHY config) ----------------- */
#define HDMI_PHY_I2CM_SLAVE_ADDR	0x3020
#define   HDMI_PHY_I2CM_SLAVE_ADDR_PHY_GEN2 0x69
#define HDMI_PHY_I2CM_ADDRESS_ADDR	0x3021
#define HDMI_PHY_I2CM_DATAO_1_ADDR	0x3022  /* Write data MSB */
#define HDMI_PHY_I2CM_DATAO_0_ADDR	0x3023  /* Write data LSB */
#define HDMI_PHY_I2CM_DATAI_1_ADDR	0x3024  /* Read data MSB (RO) */
#define HDMI_PHY_I2CM_DATAI_0_ADDR	0x3025  /* Read data LSB (RO) */
#define HDMI_PHY_I2CM_OPERATION_ADDR	0x3026  /* Initiate read/write */
#define   HDMI_PHY_I2CM_OPERATION_ADDR_WRITE (1 << 4)
#define   HDMI_PHY_I2CM_OPERATION_ADDR_READ  (1 << 0)
#define HDMI_PHY_I2CM_INT_ADDR		0x3027
#define HDMI_PHY_I2CM_CTLINT_ADDR	0x3028
#define HDMI_PHY_I2CM_DIV_ADDR		0x3029
#define   HDMI_PHY_I2CM_DIV_FAST_STD_MODE (0 << 3) /* Standard mode */
#define HDMI_PHY_I2CM_SOFTRSTZ_ADDR	0x302a
#define HDMI_PHY_I2CM_SS_SCL_HCNT_1_ADDR 0x302b
#define HDMI_PHY_I2CM_SS_SCL_HCNT_0_ADDR 0x302c
#define HDMI_PHY_I2CM_SS_SCL_LCNT_1_ADDR 0x302d
#define HDMI_PHY_I2CM_SS_SCL_LCNT_0_ADDR 0x302e
#define HDMI_PHY_I2CM_FS_SCL_HCNT_1_ADDR 0x302f
#define HDMI_PHY_I2CM_FS_SCL_HCNT_0_ADDR 0x3030
#define HDMI_PHY_I2CM_FS_SCL_LCNT_1_ADDR 0x3031
#define HDMI_PHY_I2CM_FS_SCL_LCNT_0_ADDR 0x3032

/* --- I2C master (DDC / EDID) ------------------------------------------- */
#define HDMI_I2CM_SLAVE			0x7e00
#define   HDMI_I2CM_SLAVE_DDC_ADDR	0x50     /* EDID DDC I2C address */
#define HDMI_I2CM_ADDRESS		0x7e01
#define HDMI_I2CM_DATAO			0x7e02
#define HDMI_I2CM_DATAI			0x7e03  /* (RO) */
#define HDMI_I2CM_OPERATION		0x7e04
#define   HDMI_I2CM_OPERATION_WRITE	(1 << 4)
#define   HDMI_I2CM_OPERATION_READ	(1 << 0)
#define   HDMI_I2CM_OPERATION_READ_EXT	(1 << 1)  /* Read EDID block ext */
#define HDMI_I2CM_INT			0x7e05
#define HDMI_I2CM_CTLINT		0x7e06
#define   HDMI_I2CM_CTLINT_ARB_NOKICK	(1 << 7)
#define   HDMI_I2CM_CTLINT_ARB_MASK	(1 << 3)
#define   HDMI_I2CM_CTLINT_NAC_NOKICK	(1 << 6)
#define   HDMI_I2CM_CTLINT_NAC_MASK	(1 << 2)
#define HDMI_I2CM_DIV			0x7e07
#define   HDMI_I2CM_DIV_FAST_STD_MODE	(0 << 3)
#define HDMI_I2CM_SEGADDR		0x7e08
#define   HDMI_I2CM_SEGADDR_DDC		0x30
#define HDMI_I2CM_SOFTRSTZ		0x7e09
#define   HDMI_I2CM_SOFTRSTZ_RESET	(0 << 0)
#define HDMI_I2CM_SEGPTR		0x7e0a
#define HDMI_I2CM_SS_SCL_HCNT_1_ADDR	0x7e0b
#define HDMI_I2CM_SS_SCL_HCNT_0_ADDR	0x7e0c
#define HDMI_I2CM_SS_SCL_LCNT_1_ADDR	0x7e0d
#define HDMI_I2CM_SS_SCL_LCNT_0_ADDR	0x7e0e
#define HDMI_I2CM_FS_SCL_HCNT_1_ADDR	0x7e0f
#define HDMI_I2CM_FS_SCL_HCNT_0_ADDR	0x7e10
#define HDMI_I2CM_FS_SCL_LCNT_1_ADDR	0x7e11
#define HDMI_I2CM_FS_SCL_LCNT_0_ADDR	0x7e12
#define HDMI_I2CM_BUF0			0x7e20  /* DDC read buffer byte 0 */
/* 128-byte buffer: HDMI_I2CM_BUF0 .. HDMI_I2CM_BUF0+127 */

/*
 * -----------------------------------------------------------------------
 * Rockchip PHY MPLL/OPLL configuration table for dw-hdmi on RK3399
 * (pixel clock frequency ranges, from dw_hdmi-rockchip.c)
 * -----------------------------------------------------------------------
 * These are not registers; they are configuration values written via
 * the PHY I2C master to the internal PHY registers.
 * Pixel clock    mpixelclk_mult  mpll_n  tmds_div   clk_div
 *  45.25 MHz        4              1       1          1
 * 148.35 MHz        2              1       2          2
 * 340.00 MHz        1              1       2          2
 * 594.00 MHz        1              1       4          4   (HDMI 2.0, TMDS 4.0)
 */

#endif /* _RKFB_HDMI_REGS_H_ */
