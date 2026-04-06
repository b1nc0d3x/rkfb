/*
 * rk808_hdmi_pwr.c - Enable RK808 HDMI power rails on RockPro64
 *
 * RK808 on iicbus2 at address 0x1b (7-bit: 0x1b, 8-bit write: 0x36)
 *
 * RockPro64 HDMI power rails:
 *   SWITCH_REG1 (reg 0x23 bit6? No - SWITCH regs are separate)
 *
 * RK808 register map:
 *   0x23 = LDO_EN     bits[7:0] = LDO8..LDO1 enable
 *   0x24 = LDO1_ON_VSEL ... LDO8 at 0x2b
 *   0x2f = DCDC_EN    bits: BUCK4..BUCK1, SW2, SW1
 *
 * On RockPro64 per schematic:
 *   LDO_REG7 -> VCC_1V8_S3 (system 1.8V, always on in S3)
 *   SWITCH_REG1 -> VCC3V3_S3 or similar
 *
 * Actually the HDMI rails on RockPro64:
 *   avdd_0v9_hdmi: from vdd_0v9 (LDO or fixed)
 *   avdd_1v8_hdmi: fixed 1.8V regulator (regfix)
 *
 * Let's dump all RK808 registers to understand the actual state.
 *
 * Build: cc -o rk808_hdmi_pwr rk808_hdmi_pwr.c
 */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <dev/iicbus/iic.h>
#include <sys/ioctl.h>

#define RK808_ADDR  0x1b   /* 7-bit */
#define IIC_DEV     "/dev/iic2"

static int fd;

static uint8_t rk808_read(uint8_t reg)
{
    struct iic_msg msgs[2];
    struct iic_rdwr_data rd;
    uint8_t buf[1] = {reg};
    uint8_t val[1] = {0};

    msgs[0].slave = RK808_ADDR << 1;
    msgs[0].flags = IIC_M_WR;
    msgs[0].len   = 1;
    msgs[0].buf   = buf;

    msgs[1].slave = RK808_ADDR << 1;
    msgs[1].flags = IIC_M_RD;
    msgs[1].len   = 1;
    msgs[1].buf   = val;

    rd.msgs = msgs;
    rd.nmsgs = 2;

    if (ioctl(fd, I2CRDWR, &rd) < 0) {
        perror("I2CRDWR read");
        return 0xff;
    }
    return val[0];
}

static void rk808_write(uint8_t reg, uint8_t val)
{
    struct iic_msg msg;
    struct iic_rdwr_data rd;
    uint8_t buf[2] = {reg, val};

    msg.slave = RK808_ADDR << 1;
    msg.flags = IIC_M_WR;
    msg.len   = 2;
    msg.buf   = buf;

    rd.msgs  = &msg;
    rd.nmsgs = 1;

    if (ioctl(fd, I2CRDWR, &rd) < 0)
        perror("I2CRDWR write");
}

int main(void)
{
    int i;

    fd = open(IIC_DEV, O_RDWR);
    if (fd < 0) err(1, "open %s", IIC_DEV);

    printf("=== RK808 PMIC Register Dump ===\n\n");

    /* Dump key registers */
    printf("Power enable registers:\n");
    printf("  0x23 LDO_EN     = 0x%02x\n", rk808_read(0x23));
    printf("  0x2f DCDC_EN    = 0x%02x\n", rk808_read(0x2f));
    printf("  0x30 SLEEP_SET_OFF1 = 0x%02x\n", rk808_read(0x30));

    printf("\nLDO voltage settings (ON_VSEL):\n");
    for (i = 1; i <= 8; i++) {
        uint8_t reg = 0x23 + i;   /* LDO1_ON=0x24, ..., LDO8_ON=0x2b */
        printf("  LDO%d [0x%02x] = 0x%02x", i, reg, rk808_read(reg));
        /* RK808 LDO voltage: 0.8V + n*0.1V for LDO1-4, 1.8V + n*0.1V for others */
        uint8_t v = rk808_read(reg) & 0x1f;
        float mv;
        if (i <= 4) mv = 800.0f + v * 100.0f;
        else        mv = 1800.0f + v * 100.0f;
        printf("  -> ~%.0f mV", mv);
        printf("\n");
    }

    printf("\nDCDC voltage settings:\n");
    printf("  DCDC1 [0x02] = 0x%02x\n", rk808_read(0x02));
    printf("  DCDC2 [0x04] = 0x%02x\n", rk808_read(0x04));
    printf("  DCDC3 [0x06] = 0x%02x\n", rk808_read(0x06));
    printf("  DCDC4 [0x08] = 0x%02x\n", rk808_read(0x08));

    printf("\nSWITCH state:\n");
    printf("  0x2f DCDC_EN bits[6:5] = SW2,SW1 = %d,%d\n",
        (rk808_read(0x2f)>>6)&1, (rk808_read(0x2f)>>5)&1);

    /* Decode LDO_EN */
    uint8_t ldo_en = rk808_read(0x23);
    printf("\nLDO_EN = 0x%02x:\n", ldo_en);
    for (i = 1; i <= 8; i++)
        printf("  LDO%d: %s\n", i, (ldo_en>>(i-1))&1 ? "ON" : "OFF");

    printf("\nDCDC_EN = 0x%02x:\n", rk808_read(0x2f));
    printf("  BUCK1:%d BUCK2:%d BUCK3:%d BUCK4:%d SW1:%d SW2:%d\n",
        (rk808_read(0x2f)>>0)&1, (rk808_read(0x2f)>>1)&1,
        (rk808_read(0x2f)>>2)&1, (rk808_read(0x2f)>>3)&1,
        (rk808_read(0x2f)>>5)&1, (rk808_read(0x2f)>>6)&1);

    /* Try enabling LDO7 if it's off */
    if (!((ldo_en >> 6) & 1)) {
        printf("\nLDO7 is OFF -- enabling...\n");
        /* First set voltage: LDO7_ON_VSEL at 0x2a, 1.8V = (1800-1800)/100 = 0 */
        rk808_write(0x2a, 0x00);   /* 1.8V */
        usleep(1000);
        /* Enable LDO7: set bit6 */
        rk808_write(0x23, ldo_en | (1<<6));
        usleep(10000);
        printf("LDO7 EN after: 0x%02x  LDO7_VSEL: 0x%02x\n",
            rk808_read(0x23), rk808_read(0x2a));
    } else {
        printf("\nLDO7 already ON\n");
    }

    /* Also try enabling LDO5 if off (may be avdd_0v9) */
    ldo_en = rk808_read(0x23);
    if (!((ldo_en >> 4) & 1)) {
        printf("LDO5 is OFF -- enabling at 0.9V...\n");
        rk808_write(0x28, 0x01);   /* LDO5_ON_VSEL: 0.9V = (900-800)/100=1 */
        usleep(1000);
        rk808_write(0x23, ldo_en | (1<<4));
        usleep(10000);
        printf("LDO5 EN after: 0x%02x\n", rk808_read(0x23));
    }

    close(fd);
    return 0;
}
