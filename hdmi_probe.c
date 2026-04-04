#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "rkfb_ioctl.h"
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Innosilicon HDMI PHY I2C register read/write via DW-HDMI I2C master
 * ----------------------------------------------------------------------- */

#define HDMI_PHY_I2C_ADDR       0x69

static int
rkfb_hdmi_phy_i2c_write(struct rkfb_softc *sc, uint8_t reg, uint16_t val)
{
        int timeout;
        uint8_t stat;

        /* Set slave address */
        rkfb_hdmi_write1(sc, 0x3020, HDMI_PHY_I2C_ADDR);
        /* Set register address */
        rkfb_hdmi_write1(sc, 0x3021, reg);
        /* Write data MSB then LSB */
        rkfb_hdmi_write1(sc, 0x3022, (val >> 8) & 0xff);
        rkfb_hdmi_write1(sc, 0x3023, val & 0xff);
        /* Trigger write operation */
        rkfb_hdmi_write1(sc, 0x3026, 0x10);  /* OPERATION: write */

        /* Poll for done or error — timeout ~10ms at 1000hz */
        for (timeout = 10; timeout > 0; timeout--) {
                DELAY(1000);
                stat = rkfb_hdmi_read1(sc, 0x3027);  /* PHY_I2CM_INT */
                if (stat & 0x02) {  /* done */
                        rkfb_hdmi_write1(sc, 0x3027, 0x02);  /* clear */
                        return (0);
                }
                if (stat & 0x08) {  /* error */
                        rkfb_hdmi_write1(sc, 0x3027, 0x08);  /* clear */
                        printf("rkfb: PHY I2C write error reg=0x%02x\n", reg);
                        return (EIO);
                }
        }
        printf("rkfb: PHY I2C write timeout reg=0x%02x\n", reg);
        return (ETIMEDOUT);
}

static void
rkfb_hdmi_phy_init(struct rkfb_softc *sc)
{
        uint8_t stat;
        int timeout;

        printf("rkfb: starting HDMI PHY init\n");

        /*
         * Step 1 — configure PHY I2C master clock.
         * Reference clock = 4.8 MHz (from CRU_CLKSEL49).
         * Target SCL = 100 kHz standard mode.
         * DIV = (4800000 / (2 * 100000)) - 1 = 23 = 0x17
         * HCNT = LCNT = 24 = 0x18
         */
        rkfb_hdmi_write1(sc, 0x3029, 0x17);  /* PHY_I2CM_DIV */
        rkfb_hdmi_write1(sc, 0x302b, 0x00);  /* SS_SCL_HCNT_1 */
        rkfb_hdmi_write1(sc, 0x302c, 0x18);  /* SS_SCL_HCNT_0 */
        rkfb_hdmi_write1(sc, 0x302d, 0x00);  /* SS_SCL_LCNT_1 */
        rkfb_hdmi_write1(sc, 0x302e, 0x18);  /* SS_SCL_LCNT_0 */

        printf("rkfb: PHY I2C master clock configured\n");

        /*
         * Step 2 — enable PHY interface in DW-HDMI.
         * PHY_CONF0: ENTMDS=1 (bit6), SELDATAENPOL=1 (bit1)
         * Keep PDZ=0 (powered down) until PHY is configured.
         */
        rkfb_hdmi_write1(sc, 0x3000, 0x42);
        DELAY(5000);

        printf("rkfb: PHY_CONF0 set, interface enabled\n");

        /*
         * Step 3 — configure Innosilicon PHY via I2C for 1080p60.
         * Pixel clock = 148.5 MHz.
         * Values from RK3399 TRM / Linux dw_hdmi-rockchip.c phy tables.
         */

        /* mpll_config: pixel clock = 148.5 MHz → mpixelclk_mult=2, mpll_n=1 */
        rkfb_hdmi_phy_i2c_write(sc, 0x06, 0x0008);  /* CPCE_CTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x15, 0x0000);  /* GMPCTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x10, 0x01b5);  /* TXTERM */
        rkfb_hdmi_phy_i2c_write(sc, 0x09, 0x0091);  /* CKSYMTXCTRL */
        rkfb_hdmi_phy_i2c_write(sc, 0x0e, 0x0000);  /* VLEVCTRL */

        /* current control */
        rkfb_hdmi_phy_i2c_write(sc, 0x19, 0x0000);  /* CKCALCTRL */

        printf("rkfb: PHY I2C config written\n");

        /*
         * Step 4 — power up the PHY.
         * PHY_CONF0: PDZ=1(bit7) | ENTMDS=1(bit6) |
         *            GEN2_TXPWRON=1(bit3) | SELDATAENPOL=1(bit1)
         * = 0x80 | 0x40 | 0x08 | 0x02 = 0xCA
         */
        rkfb_hdmi_write1(sc, 0x3000, 0xca);
        DELAY(5000);

        printf("rkfb: PHY powered up, waiting for lock\n");

        /*
         * Step 5 — wait for PHY PLL lock.
         * PHY_STAT0 bit4 = TX_PHY_LOCK
         * PHY_STAT0 bit1 = HPD
         */
        for (timeout = 20; timeout > 0; timeout--) {
                DELAY(5000);
                stat = rkfb_hdmi_read1(sc, 0x3004);
                if (stat & 0x10) {  /* TX_PHY_LOCK */
                        printf("rkfb: PHY locked! "
                            "PHY_STAT0=0x%02x (HPD=%d)\n",
                            stat, (stat >> 1) & 1);
                        break;
                }
        }
        if (timeout == 0)
                printf("rkfb: PHY lock timeout PHY_STAT0=0x%02x\n",
                    rkfb_hdmi_read1(sc, 0x3004));

        /*
         * Step 6 — unmute global interrupts and enable HPD interrupt.
         * IH_MUTE [0x01ff]: write 0x00 to unmute all
         * IH_MUTE_PHY_STAT0 [0x0184]: bit1=HPD, write 0 to unmask
         */
        rkfb_hdmi_write1(sc, 0x01ff, 0x00);  /* IH_MUTE: unmute all */
        rkfb_hdmi_write1(sc, 0x0184, 0xfe);  /* unmask HPD only (bit1=0) */

        printf("rkfb: HDMI PHY init complete\n");
        printf("rkfb: PHY_CONF0  [0x3000] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3000));
        printf("rkfb: PHY_STAT0  [0x3004] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x3004));
        printf("rkfb: IH_PHY     [0x0104] = 0x%02x\n",
            rkfb_hdmi_read1(sc, 0x0104));
}


static uint32_t
hdmi_read(int fd, uint32_t off)
{
    struct rkfb_regop ro;
    ro.block = 3;   /* HDMI */
    ro.off   = off;
    ro.val   = 0;
    if (ioctl(fd, RKFB_REG_READ, &ro) < 0) {
        perror("ioctl RKFB_REG_READ");
        return (0xdeadbeef);
    }
    return (ro.val);
}

static uint32_t
cru_read(int fd, uint32_t off)
{
    struct rkfb_regop ro;
    ro.block = 2;   /* CRU */
    ro.off   = off;
    ro.val   = 0;
    if (ioctl(fd, RKFB_REG_READ, &ro) < 0) {
        perror("ioctl CRU_REG_READ");
        return (0xdeadbeef);
    }
    return (ro.val);
}

int
main(void)
{
    int fd;

    fd = open("/dev/rkfb0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    printf("PHY_I2CM_INT    [0x3027] = 0x%02x\n", hdmi_read(fd, 0x3027));
    printf("PHY_I2CM_CTLINT [0x3028] = 0x%02x\n", hdmi_read(fd, 0x3028));
    printf("PHY_I2CM_DIV    [0x3029] = 0x%02x\n", hdmi_read(fd, 0x3029));
    printf("MC_OPCTRL       [0x4003] = 0x%02x\n", hdmi_read(fd, 0x4003));
    printf("MC_FLOWCTRL     [0x4004] = 0x%02x\n", hdmi_read(fd, 0x4004));
    printf("FC_INVIDCONF    [0x1000] = 0x%02x\n", hdmi_read(fd, 0x1000));
    printf("\n--- CRU ---\n");
    printf("CRU_CLKSEL49  [0x00c4] = 0x%08x\n", cru_read(fd, 0x00c4));
    printf("CRU_CLKGATE20 [0x0250] = 0x%08x\n", cru_read(fd, 0x0250));
    printf("CRU_CLKGATE21 [0x0254] = 0x%08x\n", cru_read(fd, 0x0254));
    close(fd);
    return (0);
}
