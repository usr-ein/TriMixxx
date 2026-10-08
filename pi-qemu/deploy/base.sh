#!/usr/bin/env bash
# pi_config/fresh-install.md sections 1-2, as code: what was done to trimixxx2
# by hand between a freshly flashed card and the first deploy script. Runs on
# the Pi as root -- ../deploy.sh pipes it into `sudo bash -s`.
#
# Idempotent, and safe on a deck that is already set up: every change checks
# first, nothing is removed that a deck may rely on, and nothing reboots. If
# config.txt or cmdline.txt changed, the last line says REBOOT NEEDED.
#
#   PANEL_OVERLAY   the deck's display overlay line, from its unit file
#                   ("panelOverlay"); empty leaves the panel config alone.
#
# Not here, because they are per board rather than per card: the bootloader
# EEPROM (1.2) and the UCA222's level (3.6).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
PANEL_OVERLAY="${PANEL_OVERLAY:-}"
reboot_needed=0

# ---- 1.3 boot flags: config.txt, cmdline.txt -----------------------------------
cd /boot/firmware
[ -f config.txt.pre-trimixxx ] || cp config.txt config.txt.pre-trimixxx
[ -f cmdline.txt.pre-trimixxx ] || cp cmdline.txt cmdline.txt.pre-trimixxx
before="$(md5sum config.txt cmdline.txt)"

# Stock lines changed in place (no-ops when already changed).
sed -i -e 's/^dtparam=audio=on$/dtparam=audio=off/' \
       -e 's/^display_auto_detect=1$/display_auto_detect=0/' \
       -e 's/^dtoverlay=vc4-kms-v3d$/dtoverlay=vc4-kms-v3d,noaudio/' \
       -e 's/^camera_auto_detect=1$/camera_auto_detect=0/' config.txt
# Lines the deck adds under [all], each only if it is not there already.
want=(enable_uart=1 dtoverlay=disable-bt disable_splash=1)
[ -n "$PANEL_OVERLAY" ] && want+=("dtoverlay=$PANEL_OVERLAY")
missing=()
for line in "${want[@]}"; do grep -qxF "$line" config.txt || missing+=("$line"); done
if [ "${#missing[@]}" -gt 0 ]; then
    {
        printf '\n[all]\n# TriMixxx (pi_config/fresh-install.md 1.3, added by pi-qemu/deploy.sh)\n'
        printf '%s\n' "${missing[@]}"
    } >> config.txt
fi
# The serial console would fight ttymidi for the port. Kept one line.
sed -i 's/console=serial0,115200 //' cmdline.txt
[ "$before" = "$(md5sum config.txt cmdline.txt)" ] || reboot_needed=1

# ---- 1.1 ssh: keys only -----------------------------------------------------------
# sshd keeps the first value it reads, files in name order: "05-" comes before
# cloud-init's "50-" and before a hand-made "100-custom-sshd.conf" saying yes.
# Applies from sshd's next start; the session running this uses a key anyway.
if [ ! -f /etc/ssh/sshd_config.d/05-trimixxx.conf ]; then
    echo 'PasswordAuthentication no' > /etc/ssh/sshd_config.d/05-trimixxx.conf
    sshd -t
fi

# ---- 1.4 background load ---------------------------------------------------------
# Masked rather than disabled: a first boot's systemd presets cannot bring
# them back.
systemctl mask --quiet bluetooth.service apt-daily.timer apt-daily-upgrade.timer man-db.timer
mkdir -p /etc/NetworkManager/conf.d
printf '[connection]\nwifi.powersave = 2\n' > /etc/NetworkManager/conf.d/wifi-powersave-off.conf

# ---- 1.5 card wear + 2 display, touch, X ---------------------------------------
# As trimixxx2 got them, recommends included; installed ones are left as they
# are. sqlite3 is for the library-directory step, alsa-utils for aplay.
pkgs=(log2ram xserver-xorg xinit x11-xserver-utils xinput libgl1-mesa-dri mesa-utils
      unclutter picom matchbox-window-manager scrot xdotool evtest libinput-tools
      mixxx sqlite3 alsa-utils)
missing_pkgs=()
for p in "${pkgs[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q '^install ok installed$' || missing_pkgs+=("$p")
done
if [ "${#missing_pkgs[@]}" -eq 0 ]; then
    echo "packages: all there"
else
    apt-get update -q
    apt-get install -y -q "${missing_pkgs[@]}"
fi

# ---- a release card's read-only root and A/B updates (pi-qemu/PLAN.md Part 1) ----
# overlayroot puts RAM over a read-only root when the kernel command line asks
# for it (overlayroot=tmpfs:...), which only a release card's does. RAUC installs
# updates into the other slot; nothing starts it without /etc/rauc/system.conf,
# which only a release card carries. The initramfs gets what mounts a release's
# root: squashfs, and overlay.
ab_pkgs=(overlayroot rauc rauc-service)
missing_pkgs=()
for p in "${ab_pkgs[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q '^install ok installed$' || missing_pkgs+=("$p")
done
if [ "${#missing_pkgs[@]}" -gt 0 ]; then
    apt-get update -q
    apt-get install -y -q "${missing_pkgs[@]}"
fi
modules_changed=0
for m in squashfs overlay; do
    grep -qx "$m" /etc/initramfs-tools/modules && continue
    [ "$modules_changed" = 1 ] || echo "# TriMixxx: a release card's root (pi-qemu/PLAN.md Part 1)" >> /etc/initramfs-tools/modules
    echo "$m" >> /etc/initramfs-tools/modules
    modules_changed=1
done
[ "$modules_changed" = 0 ] || update-initramfs -u -k "$(uname -r)"

# en_US.UTF-8 alongside the image's own en_GB: a Mac's ssh forwards its
# LC_ALL, and without the locale every login warns "cannot change locale".
if ! locale -a 2>/dev/null | grep -qx 'en_US.utf8'; then
    sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
    grep -qx 'en_US.UTF-8 UTF-8' /etc/locale.gen || echo 'en_US.UTF-8 UTF-8' >> /etc/locale.gen
    locale-gen
fi

# Console autologin on tty1 -- the session that becomes Mixxx.
raspi-config nonint do_boot_behaviour B2

if [ "$reboot_needed" = 1 ]; then echo "base: done, REBOOT NEEDED (boot flags changed)"; else echo "base: done"; fi
