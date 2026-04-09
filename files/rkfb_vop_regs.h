/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 B1nc0d3x
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
 * against Linux drivers/gpu/drm/rockchip/rockchip_vop_reg.h (GPL-2.0).
 * Hardware register addresses are factual descriptions of the RK3399 SoC
 * and are not subject to copyright protection.
 *
 * RK3399 VOP (Video Output Processor) register map.
 * The RK3399 has two VOPs:
 *   VOPB (big):  base address 0xff900000, supports up to 4K output
 *   VOPL (lit):  base address 0xff8f0000, supports up to 2K output
 * Gamma LUT for VOPB is at 0xff902000 (0x1000 bytes).
 * Gamma LUT for VOPL is at 0xff8f2000 (0x400 bytes).
 */

#ifndef _RKFB_VOP_REGS_H_
#define _RKFB_VOP_REGS_H_

/*
 * Physical base addresses
 */
#define RK3399_VOPB_BASE		0xff900000
#define RK3399_VOPB_SIZE		0x2000
#define RK3399_VOPB_GAMMA_BASE		0xff902000
#define RK3399_VOPB_GAMMA_SIZE		0x1000

#define RK3399_VOPL_BASE		0xff8f0000
#define RK3399_VOPL_SIZE		0x2000
#define RK3399_VOPL_GAMMA_BASE		0xff8f2000
#define RK3399_VOPL_GAMMA_SIZE		0x400

/*
 * VOP GRF (General Register Files) — selects which VOP drives HDMI.
 * GRF base: 0xff770000
 */
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		(1 << 6)  /* 0=VOPB, 1=VOPL */

/* Convenience: hiword-update write (sets mask bits in upper 16, value in lower) */
#define RK3399_HIWORD_UPDATE(val, mask)	((val) | ((mask) << 16))

/*
 * -----------------------------------------------------------------------
 * RK3399 VOP register offsets (relative to VOP base)
 * -----------------------------------------------------------------------
 */

/* --- Global / system control ------------------------------------------- */
#define RK3399_REG_CFG_DONE		0x0000	/* Config done (write 1 to latch shadow regs) */
#define RK3399_VERSION_INFO		0x0004	/* (RO) VOP version */
#define RK3399_SYS_CTRL			0x0008	/* System control */
#define RK3399_SYS_CTRL1		0x000c	/* System control 1 */
#define RK3399_DSP_CTRL0		0x0010	/* Display control 0 */
#define RK3399_DSP_CTRL1		0x0014	/* Display control 1 */
#define RK3399_DSP_BG			0x0018	/* Display background color */
#define RK3399_MCU_CTRL			0x001c	/* MCU control */

/* --- Writeback ---------------------------------------------------------- */
#define RK3399_WB_CTRL0			0x0020
#define RK3399_WB_CTRL1			0x0024
#define RK3399_WB_YRGB_MST		0x0028	/* Writeback YRGB base address */
#define RK3399_WB_CBR_MST		0x002c	/* Writeback CbCr base address */

/* --- Window 0 (YRGB / full-featured, supports scaling) ----------------- */
#define RK3399_WIN0_CTRL0		0x0030	/* Win0 control 0 (enable, format, swap) */
#define RK3399_WIN0_CTRL1		0x0034	/* Win0 control 1 (FIFO thresholds) */
#define RK3399_WIN0_COLOR_KEY		0x0038	/* Win0 color key */
#define RK3399_WIN0_VIR			0x003c	/* Win0 virtual stride (YRGB [15:0], UV [31:16]) */
#define RK3399_WIN0_YRGB_MST		0x0040	/* Win0 YRGB frame buffer start address */
#define RK3399_WIN0_CBR_MST		0x0044	/* Win0 CbCr frame buffer start address */
#define RK3399_WIN0_ACT_INFO		0x0048	/* Win0 active region (width-1 [28:16], height-1 [12:0]) */
#define RK3399_WIN0_DSP_INFO		0x004c	/* Win0 display size (width-1 [27:16], height-1 [11:0]) */
#define RK3399_WIN0_DSP_ST		0x0050	/* Win0 display start (x [28:16], y [12:0]) */
#define RK3399_WIN0_SCL_FACTOR_YRGB	0x0054	/* Win0 YRGB scale factor */
#define RK3399_WIN0_SCL_FACTOR_CBR	0x0058	/* Win0 CbCr scale factor */
#define RK3399_WIN0_SCL_OFFSET		0x005c	/* Win0 scale phase offset */
#define RK3399_WIN0_SRC_ALPHA_CTRL	0x0060	/* Win0 source alpha control */
#define RK3399_WIN0_DST_ALPHA_CTRL	0x0064	/* Win0 destination alpha control */
#define RK3399_WIN0_FADING_CTRL		0x0068	/* Win0 fading control */
#define RK3399_WIN0_CTRL2		0x006c	/* Win0 control 2 (channel select) */

/* --- Window 1 ---------------------------------------------------------- */
#define RK3399_WIN1_CTRL0		0x0070
#define RK3399_WIN1_CTRL1		0x0074
#define RK3399_WIN1_COLOR_KEY		0x0078
#define RK3399_WIN1_VIR			0x007c
#define RK3399_WIN1_YRGB_MST		0x0080
#define RK3399_WIN1_CBR_MST		0x0084
#define RK3399_WIN1_ACT_INFO		0x0088
#define RK3399_WIN1_DSP_INFO		0x008c
#define RK3399_WIN1_DSP_ST		0x0090
#define RK3399_WIN1_SCL_FACTOR_YRGB	0x0094
#define RK3399_WIN1_SCL_FACTOR_CBR	0x0098
#define RK3399_WIN1_SCL_OFFSET		0x009c
#define RK3399_WIN1_SRC_ALPHA_CTRL	0x00a0
#define RK3399_WIN1_DST_ALPHA_CTRL	0x00a4
#define RK3399_WIN1_FADING_CTRL		0x00a8

/* --- Window 2 (4-layer, tile-based, no scaler) ------------------------- */
#define RK3399_WIN2_CTRL0		0x00b0
#define RK3399_WIN2_CTRL1		0x00b4
#define RK3399_WIN2_VIR0_1		0x00b8
#define RK3399_WIN2_VIR2_3		0x00bc
#define RK3399_WIN2_MST0		0x00c0
#define RK3399_WIN2_DSP_INFO0		0x00c4
#define RK3399_WIN2_DSP_ST0		0x00c8
#define RK3399_WIN2_COLOR_KEY		0x00cc
#define RK3399_WIN2_MST1		0x00d0
#define RK3399_WIN2_DSP_INFO1		0x00d4
#define RK3399_WIN2_DSP_ST1		0x00d8
#define RK3399_WIN2_SRC_ALPHA_CTRL	0x00dc
#define RK3399_WIN2_MST2		0x00e0
#define RK3399_WIN2_DSP_INFO2		0x00e4
#define RK3399_WIN2_DSP_ST2		0x00e8
#define RK3399_WIN2_DST_ALPHA_CTRL	0x00ec
#define RK3399_WIN2_MST3		0x00f0
#define RK3399_WIN2_DSP_INFO3		0x00f4
#define RK3399_WIN2_DSP_ST3		0x00f8
#define RK3399_WIN2_FADING_CTRL		0x00fc

/* --- Window 3 (same layout as WIN2) ------------------------------------ */
#define RK3399_WIN3_CTRL0		0x0100
#define RK3399_WIN3_CTRL1		0x0104
#define RK3399_WIN3_VIR0_1		0x0108
#define RK3399_WIN3_VIR2_3		0x010c
#define RK3399_WIN3_MST0		0x0110
#define RK3399_WIN3_DSP_INFO0		0x0114
#define RK3399_WIN3_DSP_ST0		0x0118
#define RK3399_WIN3_COLOR_KEY		0x011c
#define RK3399_WIN3_MST1		0x0120
#define RK3399_WIN3_DSP_INFO1		0x0124
#define RK3399_WIN3_DSP_ST1		0x0128
#define RK3399_WIN3_SRC_ALPHA_CTRL	0x012c
#define RK3399_WIN3_MST2		0x0130
#define RK3399_WIN3_DSP_INFO2		0x0134
#define RK3399_WIN3_DSP_ST2		0x0138
#define RK3399_WIN3_DST_ALPHA_CTRL	0x013c
#define RK3399_WIN3_MST3		0x0140
#define RK3399_WIN3_DSP_INFO3		0x0144
#define RK3399_WIN3_DSP_ST3		0x0148
#define RK3399_WIN3_FADING_CTRL		0x014c

/* --- Hardware cursor (HWC) --------------------------------------------- */
#define RK3399_HWC_CTRL0		0x0150
#define RK3399_HWC_CTRL1		0x0154
#define RK3399_HWC_MST			0x0158
#define RK3399_HWC_DSP_ST		0x015c
#define RK3399_HWC_SRC_ALPHA_CTRL	0x0160
#define RK3399_HWC_DST_ALPHA_CTRL	0x0164
#define RK3399_HWC_FADING_CTRL		0x0168

/* --- Interrupt / status ------------------------------------------------- */
#define RK3399_INTR_EN0			0x0180	/* Interrupt enable 0 */
#define RK3399_INTR_CLEAR0		0x0184	/* Interrupt clear 0 */
#define RK3399_INTR_STATUS0		0x0188	/* Interrupt status 0 (RO) */
#define RK3399_INTR_RAW_STATUS0		0x018c	/* Raw interrupt status 0 (RO) */
#define RK3399_INTR_EN1			0x0190
#define RK3399_INTR_CLEAR1		0x0194
#define RK3399_INTR_STATUS1		0x0198
#define RK3399_INTR_RAW_STATUS1		0x019c

/* --- Display horizontal/vertical timing --------------------------------- */
#define RK3399_DSP_HTOTAL_HS_END	0x01a0	/* H total & hsync end pixel */
#define RK3399_DSP_HACT_ST_END		0x01a4	/* H active start & end pixel */
#define RK3399_DSP_VTOTAL_VS_END	0x01a8	/* V total & vsync end line */
#define RK3399_DSP_VACT_ST_END		0x01ac	/* V active start & end line */
#define RK3399_DSP_VS_ST_END_F1		0x01b0	/* Vsync start/end (field 1, interlaced) */
#define RK3399_DSP_VACT_ST_END_F1	0x01b4	/* Vact start/end (field 1, interlaced) */

/* --- Post-processing / BCSH -------------------------------------------- */
#define RK3399_POST_DSP_HACT_INFO	0x01b8
#define RK3399_POST_DSP_VACT_INFO	0x01bc
#define RK3399_POST_SCL_FACTOR_YRGB	0x01c0
#define RK3399_POST_SCL_CTRL		0x01c4
#define RK3399_POST_DSP_VACT_INFO_F1	0x01c8
#define RK3399_DSP_BG_COLOR0		0x01cc	/* (VOPB) background color layer 0 */
#define RK3399_DSP_BG_COLOR1		0x01d0	/* (VOPB) background color layer 1 */
#define RK3399_BCSH_CTRL		0x01d8	/* BCSH (brightness/contrast/saturation/hue) ctrl */
#define RK3399_BCSH_COL_BAR		0x01dc
#define RK3399_BCSH_BCS			0x01e0
#define RK3399_BCSH_H			0x01e4

/* --- MMU / AXI ---------------------------------------------------------- */
#define RK3399_FRC_LOWER01_0		0x01f0
#define RK3399_FRC_LOWER01_1		0x01f4
#define RK3399_FRC_LOWER10_0		0x01f8
#define RK3399_FRC_LOWER10_1		0x01fc
#define RK3399_FRC_UPPER_0		0x0200
#define RK3399_FRC_UPPER_1		0x0204

/* --- Debug / performance counters -------------------------------------- */
#define RK3399_DBG_PERF_LATENCY_CTRL0		0x0230
#define RK3399_DBG_PERF_RD_MAX_LATENCY_NUM0	0x0234
#define RK3399_DBG_PERF_RD_LATENCY_THR_NUM0	0x0238
#define RK3399_DBG_PERF_RD_LATENCY_SAMP_NUM0	0x023c
#define RK3399_DBG_PERF_LATENCY_CTRL1		0x0240
#define RK3399_DBG_PERF_RD_MAX_LATENCY_NUM1	0x0244
#define RK3399_DBG_PERF_RD_LATENCY_THR_NUM1	0x0248
#define RK3399_DBG_PERF_RD_LATENCY_SAMP_NUM1	0x024c

/* --- Line flag / status ------------------------------------------------- */
#define RK3399_LINE_FLAG		0x0280
#define RK3399_VOP_STATUS		0x02a4	/* (RO) VOP status */
#define RK3399_BLANKING_VALUE		0x02a8
#define RK3399_MCU_BYPASS_PORT		0x02ac
#define RK3399_WIN0_DSP_BG		0x02b0
#define RK3399_WIN1_DSP_BG		0x02b4
#define RK3399_WIN2_DSP_BG		0x02b8
#define RK3399_WIN3_DSP_BG		0x02bc
#define RK3399_YUV2YUV_WIN		0x02c0
#define RK3399_YUV2YUV_POST		0x02c4
#define RK3399_AUTO_GATING_EN		0x02cc

/* --- Extended debug ----------------------------------------------------- */
#define RK3399_DBG_POST_REG1		0x036c

/* --- CSC coefficient tables -------------------------------------------- */
#define RK3399_WIN0_CSC_COE		0x03a0	/* 8 x 32-bit coefficients */
#define RK3399_WIN1_CSC_COE		0x03c0
#define RK3399_WIN2_CSC_COE		0x03e0
#define RK3399_WIN3_CSC_COE		0x0400
#define RK3399_HWC_CSC_COE		0x0420
#define RK3399_BCSH_R2Y_CSC_COE		0x0440
#define RK3399_BCSH_Y2R_CSC_COE		0x0460
#define RK3399_POST_YUV2YUV_Y2R_COE	0x0480
#define RK3399_POST_YUV2YUV_3X3_COE	0x04a0
#define RK3399_POST_YUV2YUV_R2Y_COE	0x04c0

/* --- YUV2YUV per-window conversion tables (WIN0/WIN1 only) ------------- */
#define RK3399_WIN0_YUV2YUV_Y2R	0x02e0	/* 32 bytes (12 x 16-bit + 3 x 32-bit offsets) */
#define RK3399_WIN1_YUV2YUV_Y2R	0x0300

/*
 * -----------------------------------------------------------------------
 * REG_CFG_DONE bit definitions
 * -----------------------------------------------------------------------
 */
#define VOP_CFG_DONE_IMD		(1 << 1)  /* Immediate register update */
#define VOP_CFG_DONE_WORK		(1 << 0)  /* Latch config to working regs on next vsync */

/*
 * -----------------------------------------------------------------------
 * SYS_CTRL bit definitions
 * -----------------------------------------------------------------------
 */
#define VOP_SYSCTRL_STANDBY		(1 << 22)
#define VOP_SYSCTRL_MMU_EN		(1 << 20)
#define VOP_SYSCTRL_EDPI_WMS_MODE	(1 << 7)
#define VOP_SYSCTRL_EDPI_EN		(1 << 6)
#define VOP_SYSCTRL_P2I_EN		(1 << 5)
#define VOP_SYSCTRL_DSP_FIELD_POL	(1 << 4)
#define VOP_SYSCTRL_DSP_INTERLACE	(1 << 3)
#define VOP_SYSCTRL_AUTO_GATE_EN	(1 << 2)
#define VOP_SYSCTRL_VOP_EN		(1 << 1)
#define VOP_SYSCTRL_RST_EN		(1 << 0)

/*
 * -----------------------------------------------------------------------
 * DSP_CTRL0 bit definitions
 * -----------------------------------------------------------------------
 */
#define VOP_DSPCTRL0_OUT_MODE_MASK	(0xf << 0)   /* Output interface mode */
#define VOP_DSPCTRL0_OUT_RGB888		(0x0 << 0)
#define VOP_DSPCTRL0_OUT_BT1120		(0x1 << 0)
#define VOP_DSPCTRL0_OUT_BT656		(0x2 << 0)
#define VOP_DSPCTRL0_OUT_RGB666		(0x3 << 0)
#define VOP_DSPCTRL0_OUT_RGB565		(0x4 << 0)
#define VOP_DSPCTRL0_OUT_YUV420		(0x5 << 0)
#define VOP_DSPCTRL0_DITHER_UP		(1 << 6)
#define VOP_DSPCTRL0_DITHER_DOWN_EN	(1 << 8)
#define VOP_DSPCTRL0_DSP_LUT_EN		(1 << 12)  /* Enable gamma LUT */
#define VOP_DSPCTRL0_UPDATE_GAMMA_LUT	(1 << 13)  /* Trigger gamma LUT update (RK3399 VOPB) */

/*
 * -----------------------------------------------------------------------
 * WIN0_CTRL0 pixel format field (bits [3:1])
 * -----------------------------------------------------------------------
 */
#define VOP_WIN_FMT_ARGB8888		0x0
#define VOP_WIN_FMT_RGB888		0x1
#define VOP_WIN_FMT_RGB565		0x2
#define VOP_WIN_FMT_XRGB8888		0x4
#define VOP_WIN_FMT_YUV420SP		0x5
#define VOP_WIN_FMT_YUV422SP		0x6
#define VOP_WIN_FMT_YUV444SP		0x7

/*
 * -----------------------------------------------------------------------
 * Interrupt bits (INTR_EN0 / INTR_STATUS0 / INTR_CLEAR0)
 * -----------------------------------------------------------------------
 */
#define VOP_INTR_FS			(1 << 0)   /* Frame start (vsync leading edge) */
#define VOP_INTR_FS_FIELD		(1 << 1)   /* Frame start field */
#define VOP_INTR_LINE_FLAG0		(1 << 2)   /* Line flag 0 */
#define VOP_INTR_LINE_FLAG1		(1 << 3)   /* Line flag 1 */
#define VOP_INTR_BUS_ERROR		(1 << 4)   /* AXI bus error */
#define VOP_INTR_WIN0_EMPTY		(1 << 5)   /* Win0 FIFO underflow */
#define VOP_INTR_WIN1_EMPTY		(1 << 6)
#define VOP_INTR_WIN2_EMPTY		(1 << 7)
#define VOP_INTR_WIN3_EMPTY		(1 << 8)
#define VOP_INTR_HWC_EMPTY		(1 << 9)
#define VOP_INTR_POST_BUF_EMPTY		(1 << 10)
#define VOP_INTR_PWM_GEN		(1 << 11)
#define VOP_INTR_DSP_HOLD_VALID		(1 << 12)

#endif /* _RKFB_VOP_REGS_H_ */
