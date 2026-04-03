#ifndef _RKFB_VOP_REGS_CLEAN_H_
#define _RKFB_VOP_REGS_CLEAN_H_

/*
 * rkfb_vop_regs_clean.h
 *
 * Clean-room RK3399 VOP register offsets for rkfb experiments.
 *
 * This header is intentionally limited to:
 *   - offsets directly observed from live MMIO dumps on the user's hardware
 *   - neutral grouping based on observed behavior and repetition
 *
 * It intentionally avoids:
 *   - copying Linux GPL register names/macros/tables
 *   - claiming authoritative semantic meaning where not yet proven
 *
 * Naming convention:
 *   RKFB_VOP_REG_xxxx       exact 32-bit register offset
 *   RKFB_VOP_GRP_*          observed regions/groups
 *
 * Notes:
 *   - All offsets are byte offsets from VOP_BIG base 0xFF900000.
 *   - "Stable" means repeat reads looked unchanged during testing.
 *   - "Dynamic" means repeat reads changed during testing.
 *   - "Resisted write" means plain and/or masked writes did not change
 *     the observed read-back value in current experiments.
 */

#include <sys/types.h>
#include <stdint.h>

/* Base address used in current experiments */
#define RKFB_VOP_BIG_BASE             0xFF900000u

/* Observed group boundaries */
#define RKFB_VOP_GRP_LOW_START        0x0000u
#define RKFB_VOP_GRP_LOW_END          0x013Cu

#define RKFB_VOP_GRP_CTRL_START       0x0200u
#define RKFB_VOP_GRP_CTRL_END         0x020Cu

#define RKFB_VOP_GRP_MID_START        0x0280u
#define RKFB_VOP_GRP_MID_END          0x031Cu

/***********************
 * Low repeated region *
 ***********************/
#define RKFB_VOP_REG_0000             0x0000u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0004             0x0004u /* stable: 0x03058896 */
#define RKFB_VOP_REG_0008             0x0008u /* stable: 0x20801800 */
#define RKFB_VOP_REG_000C             0x000Cu /* stable: 0x0003A000 */
#define RKFB_VOP_REG_0010             0x0010u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0014             0x0014u /* stable: 0x0000E400 */
#define RKFB_VOP_REG_0018             0x0018u /* stable: 0x00000000 */
#define RKFB_VOP_REG_001C             0x001Cu /* stable: 0x00711C08 */
#define RKFB_VOP_REG_0020             0x0020u /* stable: 0xED000000 */
#define RKFB_VOP_REG_0024             0x0024u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0028             0x0028u /* stable: 0x00000000 */
#define RKFB_VOP_REG_002C             0x002Cu /* stable: 0x00000000 */
#define RKFB_VOP_REG_0030             0x0030u /* stable: 0x3A000040 */
#define RKFB_VOP_REG_0034             0x0034u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0038             0x0038u /* stable: 0x00000000 */
#define RKFB_VOP_REG_003C             0x003Cu /* stable: 0x01400140 */

#define RKFB_VOP_REG_0040             0x0040u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0044             0x0044u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0048             0x0048u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_004C             0x004Cu /* stable: 0x00EF013F */
#define RKFB_VOP_REG_0050             0x0050u /* stable: 0x000A000A */
#define RKFB_VOP_REG_0054             0x0054u /* stable: 0x10001000 */
#define RKFB_VOP_REG_0058             0x0058u /* stable: 0x10001000 */
#define RKFB_VOP_REG_005C             0x005Cu /* stable: 0x00000000 */
#define RKFB_VOP_REG_0060             0x0060u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0064             0x0064u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0068             0x0068u /* stable: 0x00000000 */
#define RKFB_VOP_REG_006C             0x006Cu /* stable: 0x00000021 */
#define RKFB_VOP_REG_0070             0x0070u /* stable: 0x3A000040 */
#define RKFB_VOP_REG_0074             0x0074u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0078             0x0078u /* stable: 0x00000000 */
#define RKFB_VOP_REG_007C             0x007Cu /* stable: 0x01400140 */

#define RKFB_VOP_REG_0080             0x0080u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0084             0x0084u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0088             0x0088u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_008C             0x008Cu /* stable: 0x00EF013F */
#define RKFB_VOP_REG_0090             0x0090u /* stable: 0x000A000A */
#define RKFB_VOP_REG_0094             0x0094u /* stable: 0x10001000 */
#define RKFB_VOP_REG_0098             0x0098u /* stable: 0x10001000 */
#define RKFB_VOP_REG_009C             0x009Cu /* stable: 0x00000000 */
#define RKFB_VOP_REG_00A0             0x00A0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00A4             0x00A4u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00A8             0x00A8u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00AC             0x00ACu /* stable: 0x00000043 */
#define RKFB_VOP_REG_00B0             0x00B0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00B4             0x00B4u /* stable: 0x00501D00 */
#define RKFB_VOP_REG_00B8             0x00B8u /* stable: 0x01400140 */
#define RKFB_VOP_REG_00BC             0x00BCu /* stable: 0x01400140 */

#define RKFB_VOP_REG_00C0             0x00C0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00C4             0x00C4u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_00C8             0x00C8u /* stable: 0x000A000A */
#define RKFB_VOP_REG_00CC             0x00CCu /* stable: 0x00000000 */
#define RKFB_VOP_REG_00D0             0x00D0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00D4             0x00D4u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_00D8             0x00D8u /* stable: 0x000A000A */
#define RKFB_VOP_REG_00DC             0x00DCu /* stable: 0x00000000 */
#define RKFB_VOP_REG_00E0             0x00E0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00E4             0x00E4u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_00E8             0x00E8u /* stable: 0x000A000A */
#define RKFB_VOP_REG_00EC             0x00ECu /* stable: 0x00000000 */
#define RKFB_VOP_REG_00F0             0x00F0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_00F4             0x00F4u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_00F8             0x00F8u /* stable: 0x000A000A */
#define RKFB_VOP_REG_00FC             0x00FCu /* stable: 0x00000000 */

#define RKFB_VOP_REG_0100             0x0100u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0104             0x0104u /* stable: 0x00601D00 */
#define RKFB_VOP_REG_0108             0x0108u /* stable: 0x01400140 */
#define RKFB_VOP_REG_010C             0x010Cu /* stable: 0x01400140 */
#define RKFB_VOP_REG_0110             0x0110u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0114             0x0114u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_0118             0x0118u /* stable: 0x000A000A */
#define RKFB_VOP_REG_011C             0x011Cu /* stable: 0x00000000 */
#define RKFB_VOP_REG_0120             0x0120u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0124             0x0124u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_0128             0x0128u /* stable: 0x000A000A */
#define RKFB_VOP_REG_012C             0x012Cu /* stable: 0x00000000 */
#define RKFB_VOP_REG_0130             0x0130u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0134             0x0134u /* stable: 0x00EF013F */
#define RKFB_VOP_REG_0138             0x0138u /* stable: 0x000A000A */
#define RKFB_VOP_REG_013C             0x013Cu /* stable: 0x00000000 */

/********************************
 * Small control/status region  *
 ********************************/
#define RKFB_VOP_REG_0200             0x0200u /* stable: 0x000081D8 */
#define RKFB_VOP_REG_0204             0x0204u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0208             0x0208u /* stable: 0x00000000 */
#define RKFB_VOP_REG_020C             0x020Cu /* stable: 0x00000001; resisted plain+masked writes */

/********************************
 * Mid active config/state area *
 ********************************/
#define RKFB_VOP_REG_0280             0x0280u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0284             0x0284u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0288             0x0288u /* stable: 0x00000000 */
#define RKFB_VOP_REG_028C             0x028Cu /* stable: 0x0000901B */
#define RKFB_VOP_REG_0290             0x0290u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0294             0x0294u /* stable: 0x00000000 */
#define RKFB_VOP_REG_0298             0x0298u /* stable: 0x00000000 */
#define RKFB_VOP_REG_029C             0x029Cu /* stable: 0x00008000; resisted plain+masked writes */
#define RKFB_VOP_REG_02A0             0x02A0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_02A4             0x02A4u /* dynamic: observed 0x0003001C, 0x000300C0, 0x000300E9, 0x000300F3 */
#define RKFB_VOP_REG_02A8             0x02A8u /* stable: 0x00000000 */
#define RKFB_VOP_REG_02AC             0x02ACu /* stable: 0x00000000 */
#define RKFB_VOP_REG_02B0             0x02B0u /* stable: 0x00000000 */
#define RKFB_VOP_REG_02B4             0x02B4u /* stable: 0x00000000 */
#define RKFB_VOP_REG_02B8             0x02B8u /* stable: 0x00000000 */
#define RKFB_VOP_REG_02BC             0x02BCu /* stable: 0x00000000 */

#define RKFB_VOP_REG_0310             0x0310u /* stable: 0x000000FF */
#define RKFB_VOP_REG_0314             0x0314u /* stable: 0x01000000 */
#define RKFB_VOP_REG_0318             0x0318u /* stable in observed reads: 0x100B867F */
#define RKFB_VOP_REG_031C             0x031Cu /* stable: 0x00000000 */

/*
 * Simple helper for code that wants a raw offset array.
 * Keep this intentionally minimal and observation-based.
 */
static const uint32_t rkfb_vop_key_regs[] = {
	RKFB_VOP_REG_0004,
	RKFB_VOP_REG_0008,
	RKFB_VOP_REG_000C,
	RKFB_VOP_REG_0014,
	RKFB_VOP_REG_001C,
	RKFB_VOP_REG_0020,
	RKFB_VOP_REG_0030,
	RKFB_VOP_REG_003C,
	RKFB_VOP_REG_0048,
	RKFB_VOP_REG_004C,
	RKFB_VOP_REG_0050,
	RKFB_VOP_REG_0054,
	RKFB_VOP_REG_0058,
	RKFB_VOP_REG_006C,
	RKFB_VOP_REG_0070,
	RKFB_VOP_REG_007C,
	RKFB_VOP_REG_0088,
	RKFB_VOP_REG_008C,
	RKFB_VOP_REG_0090,
	RKFB_VOP_REG_0094,
	RKFB_VOP_REG_0098,
	RKFB_VOP_REG_00AC,
	RKFB_VOP_REG_00B4,
	RKFB_VOP_REG_00B8,
	RKFB_VOP_REG_00BC,
	RKFB_VOP_REG_0104,
	RKFB_VOP_REG_0108,
	RKFB_VOP_REG_010C,
	RKFB_VOP_REG_0200,
	RKFB_VOP_REG_020C,
	RKFB_VOP_REG_028C,
	RKFB_VOP_REG_029C,
	RKFB_VOP_REG_02A4,
	RKFB_VOP_REG_0310,
	RKFB_VOP_REG_0314,
	RKFB_VOP_REG_0318
};

#define RKFB_VOP_KEY_REG_COUNT \
	(sizeof(rkfb_vop_key_regs) / sizeof(rkfb_vop_key_regs[0]))

#endif /* _RKFB_VOP_REGS_CLEAN_H_ */
