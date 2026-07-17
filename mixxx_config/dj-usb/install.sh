#!/usr/bin/env bash
# One-shot install of the DJ USB auto-mount onto the deck's Pi. Unlike
# ../upload.sh (which only touches ~/.mixxx), this writes system files and needs
# sudo on the far side. Re-running it is safe.
set -eux

HOST="${HOST:-trimixxx-pi}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

scp "$HERE/dj-usb" "$HERE/99-dj-usb.rules" "$HERE/dj-usb@.service" "$HOST":/tmp/

ssh "$HOST" '
    set -eux
    sudo install -m 0755 /tmp/dj-usb            /usr/local/bin/dj-usb
    sudo install -m 0644 /tmp/99-dj-usb.rules   /etc/udev/rules.d/99-dj-usb.rules
    sudo install -m 0644 /tmp/dj-usb@.service   /etc/systemd/system/dj-usb@.service
    rm -f /tmp/dj-usb /tmp/99-dj-usb.rules /tmp/dj-usb@.service
    sudo systemctl daemon-reload
    sudo udevadm control --reload-rules
    # Picks up any stick already plugged in, so this works without a replug.
    sudo udevadm trigger --subsystem-match=block --action=add
'
echo "dj-usb installed. Plug a stick in and check: ssh $HOST 'findmnt /media/DJ_USB_1'"
