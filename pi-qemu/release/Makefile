# The deck's locked card and its updates over the air: pi-qemu/PLAN.md Part 1.
#
#   make -C pi-qemu/release phase1   phase 1's test card for trimixxx3:
#                                      out/phase1/trimixxx3-phase1.img
#
# A target builds the release container (Dockerfile), then runs again inside
# it, without privileges: the tools are Debian trixie's, the decks' own. The
# inputs come from the main checkout, also from a git worktree: Pi OS's image
# (pi-qemu/.cache, fetched by image/build.sh) and the console password
# (pi-qemu/image/secrets.env). SSH_KEY is the key the card trusts:
# ~/.ssh/no_pass/rsa_sam by default, as for image/build.sh; on MBP-NJ,
# SSH_KEY=~/.ssh/with_pass/rsa_sam.

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c
.ONESHELL:
.PHONY: phase1

# The card's MBR disk signature, the same on every card for good (PLAN.md
# invariant 7): partitions are PARTUUID=5d0bc1ec-0N, so the command lines never
# change. Any fixed value works; this is the first 32 bits of sha256("trimixxx").
export DISK_ID := 0x5d0bc1ec
ID := $(DISK_ID:0x%=%)
PIOS := 2026-09-15-raspios-trixie-arm64-lite.img

ifndef IN_CONTAINER # ---- on the Mac ------------------------------------------------

MAIN := $(shell dirname "$$(git -C '$(CURDIR)' rev-parse --path-format=absolute --git-common-dir)")
SSH_KEY ?= $(HOME)/.ssh/no_pass/rsa_sam
SECRETS := $(MAIN)/pi-qemu/image/secrets.env

phase1:
	[ -f '$(MAIN)/pi-qemu/.cache/$(PIOS)' ] || { echo "no Pi OS image: pi-qemu/image/build.sh fetches it" >&2; exit 1; }
	[ -f '$(SSH_KEY).pub' ] || { echo "no $(SSH_KEY).pub: set SSH_KEY" >&2; exit 1; }
	docker build -q -t trimixxx-release - < Dockerfile > /dev/null
	docker run --rm -e IN_CONTAINER=1 $(if $(wildcard $(SECRETS)),--env-file '$(SECRETS)') \
	    --mount type=bind,src='$(CURDIR)',dst=/release -w /release \
	    --mount type=bind,src='$(MAIN)/pi-qemu/.cache',dst=/cache,readonly \
	    --mount type=bind,src='$(SSH_KEY).pub',dst=/key.pub,readonly \
	    trimixxx-release make $@

else # ---- in the container ------------------------------------------------------------

export WORK := /work
export MTOOLS_SKIP_CHECK := 1

# Phase 1's kernel command lines: the slot they name (rauc.slot=) and the boot
# partition they mount at /boot/firmware. Never `resize`, which would grow the
# root and rewrite the disk signature (PLAN.md invariant 6).
cmdline = console=serial0,115200 console=tty1 root=PARTUUID=$(ID)-05 rootfstype=ext4 fsck.repair=yes rootwait rauc.slot=$(1) systemd.mount-extra=PARTUUID=$(ID)-0$(2):/boot/firmware:vfat

phase1:
	: "$${SAM1902_PASSWORD:?no SAM1902_PASSWORD in pi-qemu/image/secrets.env (see secrets.example.env)}"
	rm -rf $(WORK) out/phase1
	mkdir -p $(WORK)/pios $(WORK)/root $(WORK)/in out/phase1
	pios=/cache/$(PIOS)
	read -r boot_start _ < <(partx -g -o START,SECTORS -n 1 $$pios)
	read -r root_start root_size < <(partx -g -o START,SECTORS -n 2 $$pios)

	# The boot files: Pi OS's, with config.txt's additions and our user-data
	# (cloud-init reads its seed from /boot/firmware), without its command line.
	mcopy -snm -i "$$pios@@$$((boot_start * 512))" '::*' $(WORK)/pios/
	rm $(WORK)/pios/{cmdline.txt,user-data}
	cat phase1/config.txt >> $(WORK)/pios/config.txt
	sed -e "s|@PASSWD@|$$(openssl passwd -6 "$$SAM1902_PASSWORD")|" \
	    -e "s|@SSH_KEY@|$$(cat /key.pub)|" phase1/user-data.in > $(WORK)/pios/user-data
	for d in bootsel boot-a boot-b; do cp -a $(WORK)/pios $(WORK)/$$d; done
	# p1 also boots on its own, as slot R, should the bootloader ignore
	# autoboot.txt. The nofilter command lines are read if [boot_partition=N] is.
	cp boot/autoboot.txt $(WORK)/bootsel/
	echo '$(call cmdline,R,1)' > $(WORK)/bootsel/cmdline-r.txt
	echo '$(call cmdline,R-nofilter,1)' > $(WORK)/bootsel/cmdline.txt
	for d in boot-a boot-b; do
	    echo '$(call cmdline,A,2)' > $(WORK)/$$d/cmdline-a.txt
	    echo '$(call cmdline,B,3)' > $(WORK)/$$d/cmdline-b.txt
	done
	echo '$(call cmdline,A-nofilter,2)' > $(WORK)/boot-a/cmdline.txt
	echo '$(call cmdline,B-nofilter,3)' > $(WORK)/boot-b/cmdline.txt

	# The root: Pi OS's, grown to a slot's 4 GiB. Its fstab mounts only the
	# root (the command line mounts /boot/firmware); NetworkManager gets the
	# card's profiles (phase1/*.nmconnection); the start-up EEPROM update is
	# masked: changing the bootloader is a step of its own (PLAN.md 1.2).
	root=$(WORK)/in/root.ext4
	dd if=$$pios of=$$root bs=1M iflag=skip_bytes,count_bytes conv=sparse status=none \
	    skip=$$((root_start * 512)) count=$$((root_size * 512))
	e2fsck -fy $$root > /dev/null || [ $$? -le 1 ]
	truncate -s 4G $$root
	resize2fs $$root
	printf '%s\n' 'proc /proc proc defaults 0 0' 'PARTUUID=$(ID)-05 / ext4 defaults,noatime 0 1' > $(WORK)/fstab
	nm=/etc/NetworkManager/system-connections
	{
	    echo 'rm /etc/fstab'
	    echo 'write $(WORK)/fstab /etc/fstab'
	    for f in phase1/*.nmconnection; do
	        install -m 600 $$f $(WORK)/
	        echo "write $(WORK)/$${f##*/} $$nm/$${f##*/}"
	    done
	    echo 'symlink /etc/systemd/system/rpi-eeprom-update.service /dev/null'
	} | debugfs -w -f - $$root
	# debugfs exits 0 whatever happened: check. NetworkManager ignores a
	# keyfile that isn't root's alone.
	debugfs -R 'cat /etc/fstab' $$root | grep -q '^PARTUUID=$(ID)-05 / '
	for f in phase1/*.nmconnection; do debugfs -R "stat $$nm/$${f##*/}" $$root | grep -q 'Mode:  0600'; done
	debugfs -R 'stat /etc/systemd/system/rpi-eeprom-update.service' $$root | grep -q 'Type: symlink'

	genimage --config genimage-phase1.cfg --rootpath $(WORK)/root --inputpath $(WORK)/in \
	    --tmppath $(WORK)/tmp --outputpath out/phase1
	sfdisk -l out/phase1/trimixxx3-phase1.img

endif
