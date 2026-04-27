## FreeBSD USB-C DP implementation history

This directory mirrors the actual FreeBSD-side work from `/home/b1nc0d3x/fbsd`
so `rkfb` carries more than notes.

Contents:

- `patches/0001-rockpro64-usbc-dp-tracked.patch`
  - tracked-tree diff for the main in-tree changes:
    - `sys/arm64/rockchip/rk_cdn_dp.c`
    - `sys/modules/rockchip/Makefile`
    - `sys/contrib/device-tree/src/arm64/rockchip/rk3399-rockpro64.dtsi`
- `sources/rockchip/`
  - current source snapshots for the DP bring-up helpers and related files:
    - `rk_cdn_dp.c`
    - `rk3399_tcphy_helper.c`
    - `rk3399_fusb302_helper.c`
    - `rk3399_fdt_fixup.c`
    - `rk3399_typec_altmode_helper.c`
    - `rk3399_typec_altmode_var.h`
- `sources/modules/`
  - module `Makefile` snapshots for the helper/module path
- `sources/iicbus-usb/`
  - `fusb302.c` / `fusb302_var.h` snapshots from the experimental built-in
    provider path
- `USB_C_DP_CHECKPOINT.md`
  - the running bring-up log from the FreeBSD tree

Important current state:

- The helper-fed Alt Mode bridge is consumed by `rk_cdn_dp`.
- The DTB fix for `tcphy0` board properties is required.
- PMA becomes ready:
  - `PMA_CMN_CTRL1 = 0x833`
- The PHY still does not reach A0 ready:
  - `DP_MODE_CTL = 0xc181`
- `uphy-pipe` deassert was added to the post-boot helper and succeeds, but the
  first real `READ_DPCD` still stalls with:
  - `mbox_last_send_written=0`
  - `mbox_last_full=1`
  - `mbox_last_empty=1`

Why both patch and source snapshots are here:

- The patch preserves tracked-tree history in a form that can be reviewed or
  replayed.
- The source snapshots preserve untracked helper/module work that would not
  appear in a normal `git diff`.
