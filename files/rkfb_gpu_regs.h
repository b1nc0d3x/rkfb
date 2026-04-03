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
 * against Linux drivers/gpu/drm/panfrost/panfrost_regs.h (GPL-2.0),
 * which notes: "Register definitions based on mali_midg_regmap.h,
 * (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved."
 * Hardware register addresses are factual descriptions of the Mali T860
 * Midgard GPU as integrated in the RK3399 SoC; they are not subject to
 * copyright protection.
 *
 * Mali T860 MP4 (Midgard) GPU — RK3399 integration:
 *   Base address:    0xff9a0000  (size 0x10000 / 64 KiB)
 *   Interrupt lines: job (SPI 17), mmu (SPI 18), gpu (SPI 16)
 *
 * Register map layout:
 *   0x0000 - 0x00ff  GPU Control
 *   0x1000 - 0x1fff  Job Control (per-slot, 0x80 bytes each)
 *   0x2000 - 0x2fff  MMU Control (per-AS, 0x40 bytes each)
 *
 * The T860 has 3 job slots (JS0=Fragment, JS1=Vertex/Tiler, JS2=Compute)
 * and 8 address spaces (AS0..AS7).
 */

#ifndef _RKFB_GPU_REGS_H_
#define _RKFB_GPU_REGS_H_

/*
 * Physical base address
 */
#define RK3399_GPU_BASE			0xff9a0000
#define RK3399_GPU_SIZE			0x10000

/*
 * -----------------------------------------------------------------------
 * GPU Control registers  [0x0000 - 0x00ff]
 * -----------------------------------------------------------------------
 */

/* --- Identification (read-only) --------------------------------------- */
#define GPU_ID				0x0000	/* GPU ID: T860 = 0x08600002 */
#define GPU_L2_FEATURES			0x0004	/* L2 cache features */
#define GPU_CORE_FEATURES		0x0008	/* Shader core features */
#define GPU_TILER_FEATURES		0x000c	/* Tiler features */
#define GPU_MEM_FEATURES		0x0010	/* Memory system features */
#define   GPU_MEM_GROUPS_L2_COHERENT	(1 << 0)  /* Core groups are L2-coherent */
#define GPU_MMU_FEATURES		0x0014	/* MMU features */
#define GPU_AS_PRESENT			0x0018	/* (RO) Address space bitmask present */
#define GPU_JS_PRESENT			0x001c	/* (RO) Job slot bitmask present */

/* --- GPU interrupt ----------------------------------------------------- */
#define GPU_INT_RAWSTAT			0x0020	/* Raw (unmasked) interrupt status */
#define GPU_INT_CLEAR			0x0024	/* Write 1 to clear interrupt bit */
#define GPU_INT_MASK			0x0028	/* Interrupt enable mask (1=enabled) */
#define GPU_INT_STAT			0x002c	/* Masked interrupt status (RO) */

/* GPU interrupt bits (used in RAWSTAT, CLEAR, MASK, STAT) */
#define GPU_IRQ_FAULT			(1 << 0)   /* GPU fault (see GPU_FAULTSTATUS) */
#define GPU_IRQ_MULTIPLE_FAULT		(1 << 7)   /* Multiple faults latched */
#define GPU_IRQ_RESET_COMPLETED		(1 << 8)   /* Soft/hard reset complete */
#define GPU_IRQ_POWER_CHANGED		(1 << 9)   /* Power domain change complete */
#define GPU_IRQ_POWER_CHANGED_ALL	(1 << 10)  /* All power changes done */
#define GPU_IRQ_PERFCNT_SAMPLE_COMPLETED (1 << 16) /* Performance counter sample done */
#define GPU_IRQ_CLEAN_CACHES_COMPLETED	(1 << 17)  /* Cache clean/flush complete */

#define GPU_IRQ_MASK_ALL		0x00031f87  /* All defined bits */
#define GPU_IRQ_MASK_ERROR		(GPU_IRQ_FAULT | GPU_IRQ_MULTIPLE_FAULT)

/* --- GPU command ------------------------------------------------------- */
#define GPU_COMMAND			0x0030	/* Write command to issue */
#define   GPU_CMD_SOFT_RESET		0x01   /* Soft reset (clears job slots) */
#define   GPU_CMD_HARD_RESET		0x02   /* Hard reset */
#define   GPU_CMD_PRFCNT_CLEAR		0x03   /* Clear performance counters */
#define   GPU_CMD_PRFCNT_SAMPLE		0x04   /* Sample performance counters */
#define   GPU_CMD_CYCLE_COUNT_START	0x05   /* Start cycle counter */
#define   GPU_CMD_CYCLE_COUNT_STOP	0x06   /* Stop cycle counter */
#define   GPU_CMD_CLEAN_CACHES		0x07   /* Clean L2 / shader caches */
#define   GPU_CMD_CLEAN_INV_CACHES	0x08   /* Clean + invalidate caches */
#define   GPU_CMD_SET_PROTECTED_MODE	0x09   /* Enter protected mode */

/* --- GPU status (read-only) ------------------------------------------- */
#define GPU_STATUS			0x0034
#define   GPU_STATUS_PRFCNT_ACTIVE	(1 << 2)
#define   GPU_STATUS_CYCLE_COUNT_ACTIVE	(1 << 6)
#define   GPU_STATUS_SHADER_ACTIVE	(1 << 4)
#define   GPU_STATUS_TILER_ACTIVE	(1 << 7)
#define   GPU_STATUS_GPU_ACTIVE		(1 << 1)

/* --- Cycle counters ---------------------------------------------------- */
#define GPU_CYCLE_COUNT_LO		0x0038	/* GPU cycle counter [31:0] */
#define GPU_CYCLE_COUNT_HI		0x003c	/* GPU cycle counter [63:32] */
#define GPU_TIMESTAMP_LO		0x0040	/* System timestamp [31:0] */
#define GPU_TIMESTAMP_HI		0x0044	/* System timestamp [63:32] */

/* --- Fault status ------------------------------------------------------ */
#define GPU_FAULTSTATUS			0x003c	/* GPU fault status (see TRM) */
#define GPU_FAULTADDRESS_LO		0x0040	/* Faulting address [31:0] */
#define GPU_FAULTADDRESS_HI		0x0044	/* Faulting address [63:32] */

/* --- Power management -------------------------------------------------- */
#define SHADER_READY_LO			0x0140	/* (RO) Shader cores powered and ready [31:0] */
#define SHADER_READY_HI			0x0144
#define TILER_READY_LO			0x0148	/* (RO) Tiler powered and ready */
#define TILER_READY_HI			0x014c
#define L2_READY_LO			0x0150	/* (RO) L2 cache powered and ready */
#define L2_READY_HI			0x0154

#define SHADER_PWRON_LO			0x0180	/* Power on shader cores (write bitmask) */
#define SHADER_PWRON_HI			0x0184
#define TILER_PWRON_LO			0x0188
#define TILER_PWRON_HI			0x018c
#define L2_PWRON_LO			0x0190
#define L2_PWRON_HI			0x0194

#define SHADER_PWROFF_LO		0x01c0	/* Power off shader cores (write bitmask) */
#define SHADER_PWROFF_HI		0x01c4
#define TILER_PWROFF_LO			0x01c8
#define TILER_PWROFF_HI			0x01cc
#define L2_PWROFF_LO			0x01d0
#define L2_PWROFF_HI			0x01d4

#define SHADER_PWRTRANS_LO		0x0200	/* (RO) Shader cores in power transition */
#define SHADER_PWRTRANS_HI		0x0204
#define TILER_PWRTRANS_LO		0x0208
#define TILER_PWRTRANS_HI		0x020c
#define L2_PWRTRANS_LO			0x0210
#define L2_PWRTRANS_HI			0x0214

#define SHADER_PWRACTIVE_LO		0x0240	/* (RO) Shader cores with active jobs */
#define SHADER_PWRACTIVE_HI		0x0244
#define TILER_PWRACTIVE_LO		0x0248
#define TILER_PWRACTIVE_HI		0x024c
#define L2_PWRACTIVE_LO			0x0250
#define L2_PWRACTIVE_HI			0x0254

/* --- Present bitmasks -------------------------------------------------- */
#define SHADER_PRESENT_LO		0x0100	/* (RO) Shader core presence [31:0] */
#define SHADER_PRESENT_HI		0x0104	/*      T860 MP4: 0x0000000f */
#define TILER_PRESENT_LO		0x0110	/* (RO) Tiler presence */
#define TILER_PRESENT_HI		0x0114
#define L2_PRESENT_LO			0x0120	/* (RO) L2 cache slice presence */
#define L2_PRESENT_HI			0x0124

/* --- Coherency --------------------------------------------------------- */
#define COHERENCY_ENABLE		0x0300
#define COHERENCY_FEATURES		0x030c	/* (RO) */

/* --- Performance counters (base) --------------------------------------- */
#define GPU_PRFCNT_BASE_LO		0x0060	/* 64-bit DMA address for perf counter dump */
#define GPU_PRFCNT_BASE_HI		0x0064
#define GPU_PRFCNT_CONFIG		0x0068	/* Counter enable config */
#define GPU_PRFCNT_JM_EN		0x006c
#define GPU_PRFCNT_SHADER_EN		0x0070
#define GPU_PRFCNT_TILER_EN		0x0074
#define GPU_PRFCNT_MMU_L2_EN		0x007c

/*
 * -----------------------------------------------------------------------
 * Job Control registers  [0x1000 - 0x17ff]
 * Each of 3 job slots occupies 0x80 bytes: JS_BASE(n) = 0x1000 + n*0x80
 * -----------------------------------------------------------------------
 */
#define JS_BASE(n)			(0x1000 + (n) * 0x80)

/* Interrupt registers (global, cover all slots) */
#define JOB_INT_RAWSTAT			0x1000	/* Raw job interrupt status */
#define JOB_INT_CLEAR			0x1004	/* Write 1 to clear */
#define JOB_INT_MASK			0x1008	/* Interrupt enable mask */
#define JOB_INT_STAT			0x100c	/* Masked job interrupt status (RO) */

/*
 * JOB_INT bit layout:
 *   bits [2:0]  = slot N done (N=0,1,2)
 *   bits [18:16] = slot N fault (N=0,1,2)
 */
#define JOB_INT_DONE(n)			(1 << (n))
#define JOB_INT_FAULT(n)		(1 << (16 + (n)))

/* Per-slot registers: use JS_REG(slot, offset) */
#define JS_REG(n, r)			(JS_BASE(n) + (r))

#define JS_HEAD_LO			0x00	/* Current job chain address [31:0] (RO) */
#define JS_HEAD_HI			0x04	/* Current job chain address [63:32] (RO) */
#define JS_TAIL_LO			0x08	/* Tail job chain address [31:0] (RO) */
#define JS_TAIL_HI			0x0c
#define JS_AFFINITY_NEXT_LO		0x10	/* Next job affinity [31:0] */
#define JS_AFFINITY_NEXT_HI		0x14
#define JS_CONFIG_NEXT			0x18	/* Next job config */
#define   JS_CONFIG_START_FLUSH_CLEAN	(1 << 8)
#define   JS_CONFIG_START_FLUSH_CLEAN_INV (3 << 8)
#define   JS_CONFIG_START_MMU		(1 << 10)
#define   JS_CONFIG_JOB_CHAIN_FLAG	(1 << 13)
#define   JS_CONFIG_END_FLUSH_CLEAN	(1 << 16)
#define   JS_CONFIG_END_FLUSH_CLEAN_INV	(3 << 16)
#define   JS_CONFIG_ENABLE_FLUSH_REDUCTION (1 << 24)
#define   JS_CONFIG_DISABLE_DESCRIPTOR_WR_BK (1 << 4)
#define JS_XAFFINITY_NEXT		0x1c
#define JS_COMMAND_NEXT			0x20	/* Next job command */
#define   JS_COMMAND_NOP		0x00
#define   JS_COMMAND_START		0x01   /* Start queued next job */
#define   JS_COMMAND_SOFT_STOP		0x02   /* Soft stop current job */
#define   JS_COMMAND_HARD_STOP		0x03   /* Hard stop current job */
#define   JS_COMMAND_SOFT_STOP_0	0x04
#define   JS_COMMAND_HARD_STOP_0	0x05
#define   JS_COMMAND_FLUSH_CACHES	0x06
#define JS_FLUSH_ID_NEXT		0x24
#define JS_AFFINITY_LO			0x40	/* Current job affinity [31:0] (RO) */
#define JS_AFFINITY_HI			0x44
#define JS_CONFIG			0x48	/* Current job config (RO) */
#define JS_XAFFINITY			0x4c
#define JS_COMMAND			0x50	/* Command for current job */
#define JS_FLUSH_ID			0x54
#define JS_STATUS			0x58	/* (RO) Job slot status */
#define   JS_STATUS_IDLE		0x00
#define   JS_STATUS_QUEUE_EMPTY		0x01
#define   JS_STATUS_DONE		0x03
#define JS_HEAD_NEXT_LO			0x60	/* Next job chain pointer [31:0] */
#define JS_HEAD_NEXT_HI			0x64

/*
 * -----------------------------------------------------------------------
 * MMU Control registers  [0x2000 - 0x27ff]
 * Each address space occupies 0x40 bytes: AS_BASE(n) = 0x2000 + n*0x40
 * The T860 exposes AS0..AS7 (GPU_AS_PRESENT bitmask).
 * -----------------------------------------------------------------------
 */
#define AS_BASE(n)			(0x2000 + (n) * 0x40)
#define AS_REG(n, r)			(AS_BASE(n) + (r))

/* MMU global interrupt registers */
#define MMU_INT_RAWSTAT			0x2000
#define MMU_INT_CLEAR			0x2004
#define MMU_INT_MASK			0x2008
#define MMU_INT_STAT			0x200c	/* (RO) */

/* Per-address-space registers */
#define AS_TRANSTAB_LO			0x00	/* Translation table base [31:0] */
#define AS_TRANSTAB_HI			0x04	/* Translation table base [63:32] */
#define AS_MEMATTR_LO			0x08	/* Memory attributes [31:0] */
#define AS_MEMATTR_HI			0x0c
#define AS_LOCKADDR_LO			0x10	/* Lock region base address [31:0] */
#define AS_LOCKADDR_HI			0x14
#define AS_COMMAND			0x18	/* MMU command */
#define   AS_COMMAND_NOP		0x00
#define   AS_COMMAND_UPDATE		0x01   /* Flush TLB for this AS */
#define   AS_COMMAND_LOCK		0x02   /* Lock region */
#define   AS_COMMAND_UNLOCK		0x03
#define   AS_COMMAND_FLUSH_PT		0x04   /* Flush page table walk cache */
#define   AS_COMMAND_FLUSH_MEM		0x05   /* Flush L2 and PT walk cache */
#define AS_FAULTSTATUS			0x1c	/* (RO) MMU fault status for this AS */
#define   AS_FAULTSTATUS_EXCEPTION_TYPE_MASK	0xff
#define   AS_FAULTSTATUS_ACCESS_TYPE_MASK	(0x3 << 8)
#define   AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC	(0x0 << 8)
#define   AS_FAULTSTATUS_ACCESS_TYPE_EX		(0x1 << 8)
#define   AS_FAULTSTATUS_ACCESS_TYPE_READ	(0x2 << 8)
#define   AS_FAULTSTATUS_ACCESS_TYPE_WRITE	(0x3 << 8)
#define   AS_FAULTSTATUS_SOURCE_ID_MASK		(0xffff << 16)
#define AS_FAULTADDRESS_LO		0x20	/* (RO) Faulting VA [31:0] */
#define AS_FAULTADDRESS_HI		0x24	/* (RO) Faulting VA [63:32] */
#define AS_STATUS			0x28	/* (RO) AS status */
#define   AS_STATUS_AS_ACTIVE		(1 << 0)
#define AS_TRANSCFG_LO			0x30	/* Translation config [31:0] */
#define AS_TRANSCFG_HI			0x34
#define AS_FAULTEXTRA_LO		0x38	/* Extra fault info [31:0] */
#define AS_FAULTEXTRA_HI		0x3c

/*
 * -----------------------------------------------------------------------
 * Mali Midgard page table entry (PTE) format
 * 64-bit PTEs; this is *not* standard ARM LPAE.
 * -----------------------------------------------------------------------
 */
#define MIDGARD_PTE_VALID		(1ULL << 0)
#define MIDGARD_PTE_LEAF		(1ULL << 1)   /* 0=table, 1=4K page */
#define MIDGARD_PTE_WRITE		(1ULL << 7)
#define MIDGARD_PTE_READ		(1ULL << 6)
#define MIDGARD_PTE_EXECUTE		(0ULL << 4)   /* No NX bit; executable by default */
#define MIDGARD_PTE_ATTR_MASK		(0xfULL << 2) /* Memory attribute index */

/*
 * -----------------------------------------------------------------------
 * Mali T860 GPU ID register value
 * -----------------------------------------------------------------------
 */
#define MALI_T860_GPU_ID		0x08600002
#define MALI_GPU_ID_PRODUCT_MASK	0xffff0000
#define MALI_GPU_ID_MAJOR_MASK		0x0000f000
#define MALI_GPU_ID_MINOR_MASK		0x00000f00
#define MALI_GPU_ID_STATUS_MASK		0x000000ff

#define MALI_GPU_ID_PRODUCT(id)		(((id) & MALI_GPU_ID_PRODUCT_MASK) >> 16)
#define MALI_GPU_ID_MAJOR(id)		(((id) & MALI_GPU_ID_MAJOR_MASK) >> 12)
#define MALI_GPU_ID_MINOR(id)		(((id) & MALI_GPU_ID_MINOR_MASK) >> 8)

#endif /* _RKFB_GPU_REGS_H_ */
