#!/bin/sh
set -eu
cd /home/claude/rkfb

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

# Best-effort HDMI/VOP restore for the current base.
# 1. Ensure rkfb is loaded so /dev/rkfb0 exists.
# 2. Run the existing controller/frame-composer path.
# 3. Re-run the corrected direct PHY-lock path.
# 4. Reapply the VOP/framebuffer route.
# 5. Print a direct register snapshot.

if ! kldstat -n rkfb.ko >/dev/null 2>&1; then
    kldload ./rkfb.ko || true
    sleep 1
fi

if [ -c /dev/rkfb0 ]; then
    ./rkfb_init || true
else
    echo "rkfb device missing after kldload; skipping rkfb_init" >&2
fi

./phy_lock || true

if [ -c /dev/rkfb0 ]; then
    ./vop_overlay_only || true
else
    echo "rkfb device missing; skipping vop_overlay_only" >&2
fi

./snap_regs || true
