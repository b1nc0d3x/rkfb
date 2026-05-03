# Dependencies and build/test guide for rkfb!

`rkfb!` is the minimal, single-file HDMI framebuffer driver for RK3399.
It exposes the framebuffer to FreeBSD's `vt(4)` console at a hardcoded
1920×1080 32-bpp mode.  No DRM/KMS, no EDID-driven mode setting, no
USB-C DP, no kernel rebuild required.

## 1. Hardware

- Rockchip RK3399 board.  Verified on RockPro64.  Other RK3399 boards
  may need DTB or pinmux tweaks since the driver assumes RockPro64's
  HDMI/I²C wiring.
- microSD or eMMC for the FreeBSD boot media (≥ 4 GB).
- USB-to-serial 3.3 V adapter on the RockPro64 console UART (1.5 Mbaud).
  Strongly recommended — kernel iteration may DDB-trap and the serial
  console is the only reliable way out.
- HDMI cable + a display that accepts **1920×1080 @ 60 Hz** (the driver
  programs that mode unconditionally).
- Optional: USB keyboard for `vt(4)` console testing.

## 2. Prerequisite: a working FreeBSD/arm64 boot

You need a RockPro64 already booting **FreeBSD-CURRENT or 15.x** from
its boot media.  This branch does NOT include a base FreeBSD image; it
only supplies the rkfb kmod.

If you don't yet have a working FreeBSD on the board, follow the
official FreeBSD/arm64 RockPro64 getting-started instructions first,
then come back here.

## 3. Software (build host)

You can build the kmod on either:

- **The RockPro64 itself** (native arm64 build), once it's running
  FreeBSD-15+ — easiest.
- **A FreeBSD/amd64 build host** that cross-builds to arm64.

Required tools (already part of the FreeBSD base):

- `cc` (clang), `lld`, `make` from the same FreeBSD release.
- `git` to fetch this branch.
- A FreeBSD source tree on the build host at `/usr/src` (or any path
  you'll pass via `SRCTOP=`).  The kmod build pulls headers and
  `bsd.kmod.mk` from there.

## 4. Get the source

On the build host:

```sh
git clone --branch 'rkfb!' <URL-of-this-repo> ~/rkfb
cd ~/rkfb
```

(`'rkfb!'` is quoted because the `!` would otherwise expand in some
shells.)

If you don't already have a FreeBSD source tree:

```sh
sudo git clone -b releng/15.0 https://git.freebsd.org/src.git /usr/src
```

## 5. Build

### 5a. Native build on the RockPro64

```sh
cd ~/rkfb
make depend all
sudo make install
```

This produces `rkfb.ko`, installs it to `/boot/modules/rkfb.ko`, and
runs `kldxref`.

### 5b. Cross-build from FreeBSD/amd64

```sh
cd ~/rkfb
env MAKEOBJDIRPREFIX=$HOME/obj \
    SRCTOP=/usr/src \
    make MACHINE=arm64 MACHINE_ARCH=aarch64 depend all
```

Resulting `rkfb.ko` lives under `$HOME/obj/$PWD/`.  Copy it to the
target's `/boot/modules/`:

```sh
scp ~/obj/path/to/rkfb.ko admin@<rockpro64>:/tmp/
ssh admin@<rockpro64> "sudo cp /tmp/rkfb.ko /boot/modules/ && sudo kldxref /boot/modules"
```

## 6. Load on the target

Either on demand (preferred while iterating):

```sh
sudo kldload rkfb
sudo kldstat | grep rkfb
```

Or autoload at boot — append to `/boot/loader.conf.local`:

```
rkfb_load="YES"
```

## 7. Verify

After loading:

```sh
sysctl dev.rkfb               # driver instance + state
dmesg | grep -i rkfb          # attach + mode messages
vidcontrol -i mode             # vt(4) sees the new framebuffer
```

Expected:

- HDMI display shows the FreeBSD `vt(4)` boot/login console at
  1920×1080.
- `dmesg` reports the VOP/HDMI bring-up and the attached mode.
- `kldstat` lists `rkfb`.
- `sysctl dev.rkfb.0.%desc` returns the driver description.

## 8. Use

- The framebuffer becomes the `vt(4)` console automatically.  Login
  prompt + scrollback all appear on the HDMI display.
- Run `vidcontrol -m on` to enable the mouse cursor when a USB mouse
  is plugged in.
- The driver does not implement page flips, mode switching, EDID
  parsing, or any DRM/KMS functionality.  For those, see the
  separate `rk_drm!` branch.

## 9. Unload

```sh
sudo kldunload rkfb
```

The console will return to whatever it was using before (typically the
serial console only).

## 10. Limitations

- Hardcoded mode 1920×1080 @ 60 Hz, 32-bpp.  Connecting a display that
  doesn't accept this mode will produce a blank screen even though the
  driver attaches successfully — the failure mode is in the monitor,
  not the kernel.
- HDMI only — no USB-C DP, no DisplayPort connector.
- RockPro64 wiring is assumed (HDMI on the standard connector, I²C3
  for the EDID-less PHY init).  Other RK3399 boards may attach but
  produce no image.
- No hot-plug detection.  If the cable is unplugged after `kldload`,
  the driver does not notice.

## 11. Reporting issues

Please include:

- `uname -a` and `kern.version`
- `kldstat`
- `sysctl dev.rkfb`
- `dmesg | grep -E 'rkfb|VOP|HDMI'`
- The exact display + cable in use, and whether the display reports a
  signal at all.
