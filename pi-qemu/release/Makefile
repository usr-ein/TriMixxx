# The deck's locked card and its updates over the air: pi-qemu/PLAN.md Part 1.
#
#   make -C pi-qemu/release phase1   phase 1's test card for trimixxx3:
#                                      out/phase1/trimixxx3-phase1.img
#   make -C pi-qemu/release release VERSION=1.0.0
#                                    the release: out/1.0.0/trimixxx-1.0.0.img (the
#                                    card, every deck's), trimixxx-1.0.0.raucb (the
#                                    update), manifest.txt
#   make -C pi-qemu/release card DECK=trimixxx2 VERSION=1.0.0
#                                    that card with the deck's identity on p7:
#                                    out/1.0.0/trimixxx2-1.0.0.img, to flash
#   make -C pi-qemu/release ship DECK=trimixxx2 VERSION=1.0.0
#                                    the update onto a running deck, over ssh; for an
#                                    emulated one: pi-qemu/instance.sh run NAME -- make ...
#   make -C pi-qemu/release faults VERSION=1.0.0
#                                    broken updates made from that release, for the fault
#                                    tests (PLAN.md §5.4): out/1.0.0-nonet, -nossh,
#                                    -noinitramfs, -panic and -hang, each to `make ship`
#
# A target builds the release container (Dockerfile), then runs again inside
# it: the tools are Debian trixie's, the decks' own. Only `release` runs it
# privileged, to mount the built card read-only. The inputs come from the main
# checkout, also from a git worktree: Pi OS's image and the built system card
# (pi-qemu/.cache), and the console password (pi-qemu/image/secrets.env).
# SSH_KEY is the key pair the cards trust: ~/.ssh/no_pass/rsa_sam by default,
# as for image/build.sh; on MBP-NJ, SSH_KEY=~/.ssh/with_pass/rsa_sam. Only its
# .pub reaches a card (authorized_keys); the private half stays on the Mac,
# where the build logs into the emulated Pi it builds on.
#
# `release` builds the system with image/build.sh trimixxx-release (SKIP_BUILD=1
# seals the card already built), from a clean tree whose commit is tagged
# v$(VERSION). REHEARSAL=1 lets an untagged or dirty tree through, for trying
# releases on emulated decks; its manifest says so.

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c
.ONESHELL:
.PHONY: phase1 release card ship faults

# The card's MBR disk signature, the same on every card for good (PLAN.md
# invariant 7): partitions are PARTUUID=5d0bc1ec-0N, so the command lines never
# change. Any fixed value works; this is the first 32 bits of sha256("trimixxx").
export DISK_ID := 0x5d0bc1ec
ID := $(DISK_ID:0x%=%)
PIOS := 2026-09-15-raspios-trixie-arm64-lite.img
SYSTEM := trimixxx-release
# The decks' screens, as their framebuffers report them (WxHxBPPxSTRIDE):
# trimixxx1's panel, and trimixxx2's (the emulated trimixxx0's too). The splash
# carries a labelled image for each (pi_config/trimixxx-splash.sh).
SPLASH_SCREENS := 1024x600x16x2048 1280x800x16x2560

ifndef IN_CONTAINER # ---- on the Mac ------------------------------------------------

MAIN := $(shell dirname "$$(git -C '$(CURDIR)' rev-parse --path-format=absolute --git-common-dir)")
REPO := $(abspath $(CURDIR)/../..)
SSH_KEY ?= $(HOME)/.ssh/no_pass/rsa_sam
SECRETS := $(MAIN)/pi-qemu/image/secrets.env
CACHE := $(MAIN)/pi-qemu/.cache
DECKS := $(CACHE)/decks
# The Mac's UTC offset, as a POSIX TZ that the container's date reads with no
# zone files (+0200 is LOC-02:00): its step lines then carry the Mac's time,
# as image/build.sh's do, which the build window counts the time from.
TZ_LOCAL := $(shell date +%z | sed -e 's/^+/m/' -e 's/^-/+/' -e 's/^m/-/' -e 's/^\(.\)\(..\)\(..\)$$/LOC\1\2:\3/')
CONTAINER := docker build -q -t trimixxx-release - < Dockerfile > /dev/null && docker run --rm -e IN_CONTAINER=1 -e TZ='$(TZ_LOCAL)' \
    --mount type=bind,src='$(CURDIR)',dst=/release -w /release \
    --mount type=bind,src='$(REPO)',dst=/repo,readonly

phase1:
	@set -euo pipefail; \
	[ -f '$(CACHE)/$(PIOS)' ] || { echo "no Pi OS image: pi-qemu/image/build.sh fetches it" >&2; exit 1; }; \
	[ -f '$(SSH_KEY).pub' ] || { echo "no $(SSH_KEY).pub: set SSH_KEY" >&2; exit 1; }; \
	$(CONTAINER) $(if $(wildcard $(SECRETS)),--env-file '$(SECRETS)') \
	    --mount type=bind,src='$(CACHE)',dst=/cache,readonly \
	    --mount type=bind,src='$(SSH_KEY).pub',dst=/key.pub,readonly \
	    trimixxx-release make $@

# macOS's make is 3.81: no .ONESHELL or .SHELLFLAGS. So each recipe on this
# side is one shell command, `set -euo pipefail` first.
#
# `release` shows itself in pi-qemu's build window (`pi-qemu build-log`), unless
# BUILD_WINDOW=0: the system's build and then the release's own steps, all in
# pi-qemu/.cache/build/build.log, which ends with BUILD_EXIT <status>.
release:
	@set -euo pipefail; \
	[ -n '$(VERSION)' ] || { echo "make release VERSION=1.0.0" >&2; exit 1; }; \
	commit="$$(git -C '$(REPO)' describe --tags --match 'v[0-9]*' --always --dirty)"; \
	if [ -z '$(REHEARSAL)' ]; then \
	    [ -z "$$(git -C '$(REPO)' status --porcelain)" ] || { echo "the tree has changes: commit them (or REHEARSAL=1)" >&2; exit 1; }; \
	    [ "$$(git -C '$(REPO)' tag --points-at HEAD)" = 'v$(VERSION)' ] || \
	        { echo "HEAD is not tagged v$(VERSION): git tag v$(VERSION) (or REHEARSAL=1)" >&2; exit 1; }; \
	else commit="$$commit (rehearsal)"; fi; \
	log='$(CACHE)/build/build.log'; mkdir -p '$(CACHE)/build'; : > "$$log"; \
	if [ '$(BUILD_WINDOW)' != 0 ] && ! pgrep -f "build-log $$log" > /dev/null; then \
	    ( trap '' INT; exec '$(MAIN)/pi-qemu/app/build/pi-qemu' build-log "$$log" ) > /dev/null 2>&1 & \
	fi; \
	set +e; ( set -e; \
	    [ -n '$(SKIP_BUILD)' ] || BUILD_LOGGED=1 SSH_KEY='$(SSH_KEY)' '$(REPO)/pi-qemu/image/build.sh' $(SYSTEM); \
	    [ -f '$(CACHE)/build/$(SYSTEM).img' ] || { echo "no $(SYSTEM) card in pi-qemu/.cache/build" >&2; exit 1; }; \
	    $(CONTAINER) --privileged -e VERSION='$(VERSION)' -e COMMIT="$$commit" \
	        --mount type=bind,src='$(CACHE)/build',dst=/build,readonly \
	        trimixxx-release make $@ \
	) 2>&1 | tee -a "$$log"; status=$${PIPESTATUS[0]}; \
	echo "BUILD_EXIT $$status" >> "$$log"; exit $$status

# The deck's identity, kept in pi-qemu/.cache/decks/<deck>/ (gitignored) and
# made the first time: its name, its ssh host keys (kept, so known_hosts
# survives a reflash), its hotspot, the home Wi-Fi from image/secrets.env, and
# for an emulated deck the profile of pi-qemu's management NIC. Any other
# network goes there by hand, as a NetworkManager keyfile
# (NetworkManager/*.nmconnection).
card:
	@set -euo pipefail; \
	[ -n '$(DECK)' ] && [ -n '$(VERSION)' ] || { echo "make card DECK=trimixxx2 VERSION=1.0.0" >&2; exit 1; }; \
	[ -f 'out/$(VERSION)/trimixxx-$(VERSION).img' ] || { echo "no release $(VERSION): make release first" >&2; exit 1; }; \
	d='$(DECKS)/$(DECK)'; \
	mkdir -p "$$d/ssh" "$$d/NetworkManager"; \
	[ -f "$$d/trimixxx.conf" ] || echo '$(DECK)' > "$$d/trimixxx.conf"; \
	if ! ls "$$d"/ssh/ssh_host_*_key >/dev/null 2>&1; then \
	    t="$$(mktemp -d)"; mkdir -p "$$t/etc/ssh"; ssh-keygen -A -f "$$t" >/dev/null; \
	    cp "$$t"/etc/ssh/ssh_host_* "$$d/ssh/"; rm -rf "$$t"; \
	fi; \
	channel="$$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get("hotspotChannel", 6))' \
	    '$(REPO)/mixxx_config/units/$(DECK).json' 2>/dev/null || echo 6)"; \
	psk="$$(sed -n "s/^PASSWORD='\(.*\)'$$/\1/p" '$(REPO)/pi_config/wifi-fallback/install.sh')"; \
	sed -e 's/@DECK@/$(DECK)/' -e "s/@CHANNEL@/$$channel/" -e "s/@PSK@/$$psk/" hotspot.nmconnection.in \
	    > "$$d/NetworkManager/trimixxx-hotspot.nmconnection"; \
	HOME_WIFI_SSID= HOME_WIFI_PSK=; [ ! -f '$(SECRETS)' ] || . '$(SECRETS)'; \
	if [ -n "$$HOME_WIFI_SSID" ]; then \
	    HOME_WIFI_SSID="$$HOME_WIFI_SSID" HOME_WIFI_PSK="$$HOME_WIFI_PSK" python3 -c 'import os, sys, uuid; \
	        s, k = os.environ["HOME_WIFI_SSID"], os.environ["HOME_WIFI_PSK"]; \
	        u = str(uuid.uuid5(uuid.NAMESPACE_DNS, "home-wifi." + s)); \
	        sys.stdout.write(open(sys.argv[1]).read().replace("@SSID@", s).replace("@PSK@", k).replace("@UUID@", u))' \
	        home-wifi.nmconnection.in > "$$d/NetworkManager/home-wifi.nmconnection"; \
	    chmod 600 "$$d/NetworkManager/home-wifi.nmconnection"; \
	fi; \
	[ '$(DECK)' != trimixxx0 ] || cp phase1/pi-qemu-home.nmconnection "$$d/NetworkManager/"; \
	rm -f 'out/$(VERSION)/$(DECK)-$(VERSION).img'; \
	cp -c 'out/$(VERSION)/trimixxx-$(VERSION).img' 'out/$(VERSION)/$(DECK)-$(VERSION).img'; \
	$(CONTAINER) -e DECK='$(DECK)' -e VERSION='$(VERSION)' \
	    --mount type=bind,src="$$d",dst=/deck,readonly \
	    trimixxx-release make $@

# The update onto a running deck: the bundle into RAUC's state partition
# (streamed there: /tmp is RAM, and a bundle is over a gigabyte), the install
# into the other slot, a reboot into it as a trial, then what the deck says:
# its version, its slot, its health check, RAUC's status.
ship:
	@set -euo pipefail; \
	[ -n '$(DECK)' ] && [ -n '$(VERSION)' ] || { echo "make ship DECK=trimixxx2 VERSION=1.0.0" >&2; exit 1; }; \
	b='out/$(VERSION)/trimixxx-$(VERSION).raucb'; \
	[ -f "$$b" ] || { echo "no $$b: make release first" >&2; exit 1; }; \
	ssh '$(DECK)' 'sudo sh -c "cat > /var/lib/rauc/trimixxx-$(VERSION).raucb"' < "$$b"; \
	ssh '$(DECK)' 'sudo rauc install /var/lib/rauc/trimixxx-$(VERSION).raucb; r=$$?; sudo rm -f /var/lib/rauc/trimixxx-$(VERSION).raucb; exit $$r'; \
	old="$$(ssh '$(DECK)' cat /proc/sys/kernel/random/boot_id)"; \
	ssh '$(DECK)' 'sudo systemd-run --quiet --on-active=1 systemctl reboot'; \
	echo "$(DECK): installed $(VERSION); rebooting into it as a trial"; \
	for _ in $$(seq 1 120); do \
	    sleep 2; \
	    now="$$(ssh -o ConnectTimeout=2 '$(DECK)' cat /proc/sys/kernel/random/boot_id 2>/dev/null)" || continue; \
	    [ -n "$$now" ] && [ "$$now" != "$$old" ] && break; \
	done; \
	ssh '$(DECK)' 'cat /etc/trimixxx-release; /usr/lib/rauc/rpi-tryboot booted; sudo systemctl is-active trimixxx-health || true; rauc status'

faults:
	@set -euo pipefail; \
	[ -n '$(VERSION)' ] || { echo "make faults VERSION=1.0.0" >&2; exit 1; }; \
	[ -f 'out/$(VERSION)/rootfs.squashfs' ] || { echo "no release $(VERSION): make release first" >&2; exit 1; }; \
	$(CONTAINER) -e VERSION='$(VERSION)' trimixxx-release make $@

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

# The release's kernel command line, for slot $(1): its root and boot partitions.
slot_cmdline = sed -e 's/@ID@/$(ID)/g' -e 's/@SLOT@/$(1)/' -e 's/@ROOT@/$(2)/' -e 's/@BOOT@/$(3)/' boot/cmdline.txt.in

# A step of `release`, for the build window: a line as image/build.sh's say
# prints them.
say = echo; echo "==> [$$(date +%H:%M:%S)] release: $(1)"

release:
	o=out/$(VERSION) card=/build/$(SYSTEM).img r=/merged p=/repo/pi_config
	rm -rf $(WORK) $$o
	mkdir -p $(WORK)/boot $(WORK)/bootsel $(WORK)/root $(WORK)/bundle $(WORK)/img $$o
	read -r boot_start _ < <(partx -g -o START,SECTORS -n 1 $$card)
	read -r root_start root_size < <(partx -g -o START,SECTORS -n 2 $$card)

	# ---- the system: the built card's root, sealed, as SquashFS --------------------
	$(call say,the system sealed into SquashFS)
	# Read-only underneath, the seal's changes in RAM on top: the built card is
	# never written.
	mkdir -p /lower /up $$r
	mount -t tmpfs tmpfs /up && mkdir -p /up/u /up/w
	mount -o loop,ro,offset=$$((root_start * 512)),sizelimit=$$((root_size * 512)) -t ext4 $$card /lower
	mount -t overlay overlay -o lowerdir=/lower,upperdir=/up/u,workdir=/up/w $$r
	trap 'umount $$r /lower /up 2>/dev/null || true' EXIT
	# The build's identity and leftovers (exclude.txt). Each deck's comes from
	# /data at every start (trimixxx-identity).
	shopt -s nullglob dotglob
	while read -r g; do for f in $$r/$$g; do rm -rf "$$f"; done; done \
	    < <(sed -e 's/#.*//' -e '/^[[:space:]]*$$/d' exclude.txt)
	find $$r/var/log -type f -delete
	: > $$r/etc/machine-id
	echo trimixxx > $$r/etc/hostname
	sed -i 's/^127\.0\.1\.1.*/127.0.1.1\ttrimixxx/' $$r/etc/hosts
	# The release's own files, from the repo whatever the build deployed: its
	# mounts, RAUC's configuration and backend, the identity and health units,
	# eth0's profile.
	install -m 0644 rootfs/etc/fstab $$r/etc/fstab
	install -D -m 0644 $$p/rauc/system.conf $$r/etc/rauc/system.conf
	install -D -m 0644 $$p/rauc/keyring.pem $$r/etc/rauc/keyring.pem
	install -D -m 0755 $$p/rauc/rpi-tryboot $$r/usr/lib/rauc/rpi-tryboot
	install -D -m 0755 $$p/trimixxx-identity $$r/usr/local/lib/trimixxx/identity
	install -D -m 0755 $$p/trimixxx-health $$r/usr/local/lib/trimixxx/health
	install -m 0644 $$p/trimixxx-identity.service $$p/trimixxx-health.service $$r/etc/systemd/system/
	install -D -m 0600 $$p/eth0-link-local.nmconnection $$r/etc/NetworkManager/system-connections/eth0-link-local.nmconnection
	install -m 0755 $$p/trimixxx-splash.sh $$r/usr/local/bin/trimixxx-splash
	mkdir -p $$r/data $$r/var/lib/rauc
	# The splash says which slot started: an image per screen and per start,
	# labelled with the slot, the version, and the trial.
	for g in $(SPLASH_SCREENS); do
	    IFS=x read -r w h bpp stride <<< "$$g"
	    dst=$$r/usr/local/share/trimixxx/splash.d/$$g
	    mkdir -p $$dst
	    for s in A B; do
	        python3 $$p/splash-render.py $$p/trimixxx_logo_crt.svg -o $$dst/$$s.raw \
	            --width $$w --height $$h --bpp $$bpp --stride $$stride --label "$$s  ·  v$(VERSION)" > /dev/null
	        python3 $$p/splash-render.py $$p/trimixxx_logo_crt.svg -o $$dst/$$s-trial.raw \
	            --width $$w --height $$h --bpp $$bpp --stride $$stride --label "$$s  ·  trial  ·  v$(VERSION)" --trial > /dev/null
	    done
	done
	# Started at every start: the deck's identity, and the health check. Masked:
	# the first-start jobs a read-only root would run at every start (systemd
	# counts each as a first start, /etc/machine-id being empty): new ssh host
	# keys over the deck's own, and growing the root.
	mkdir -p $$r/etc/systemd/system/sysinit.target.wants $$r/etc/systemd/system/multi-user.target.wants
	ln -sf /etc/systemd/system/trimixxx-identity.service $$r/etc/systemd/system/sysinit.target.wants/
	ln -sf /etc/systemd/system/trimixxx-health.service $$r/etc/systemd/system/multi-user.target.wants/
	for u in regenerate_ssh_host_keys.service rpi-resize.service; do ln -sf /dev/null $$r/etc/systemd/system/$$u; done
	printf 'VERSION=%s\nCOMMIT=%s\n' '$(VERSION)' "$$COMMIT" > $$r/etc/trimixxx-release
	dpkg-query --admindir=$$r/var/lib/dpkg -W -f='$${Package} $${Version}\n' > $(WORK)/packages.txt
	# POSIX ACLs (the journal's directory) have no place in SquashFS: left out.
	mksquashfs $$r $(WORK)/img/rootfs.squashfs -comp zstd -xattrs -xattrs-exclude '^system\.posix_acl' \
	    -noappend -quiet -no-progress
	umount $$r /lower /up; trap - EXIT

	# ---- the boot files: the built card's, with both slots' command lines ----------
	$(call say,the card: boot files and genimage)
	mcopy -snm -i "$$card@@$$((boot_start * 512))" '::*' $(WORK)/boot/
	rm -f $(WORK)/boot/{cmdline.txt,user-data,network-config,meta-data,config.txt.pre-trimixxx,cmdline.txt.pre-trimixxx}
	cat boot/config-release.txt >> $(WORK)/boot/config.txt
	$(call slot_cmdline,A,05,02) > $(WORK)/boot/cmdline-a.txt
	$(call slot_cmdline,B,06,03) > $(WORK)/boot/cmdline-b.txt
	cp boot/autoboot.txt $(WORK)/bootsel/

	# ---- the card ---------------------------------------------------------------------
	# All of it made in the container's own filesystem, then copied out. Read
	# through Docker's share of the Mac's disk, a file just written can claim to
	# be one big hole, and `cp` then copies nothing but zeros.
	genimage --config genimage.cfg --rootpath $(WORK)/root --inputpath $(WORK)/img \
	    --tmppath $(WORK)/tmp --outputpath $(WORK)/img
	# The release's p1 holds no firmware: a damaged autoboot.txt then starts p2
	# (PLAN.md invariant 13).
	! mdir -b -i "$(WORK)/img/trimixxx.img@@$$((16384 * 512))" ::/ | grep -i '\.elf$$'

	# ---- the update: a RAUC bundle of both slot images -----------------------------
	$(call say,the update: a RAUC bundle)
	# Signed with signing-key.pem, public on purpose (Sam, 2026-10-08): TriMixxx
	# is open hardware, and anyone may sign a release for it. The signature,
	# which RAUC requires, guards against a damaged bundle, not a hostile one.
	sed -e 's|@VERSION@|$(VERSION)|' -e "s|@BUILD@|$$COMMIT|" manifest.raucm.in > $(WORK)/bundle/manifest.raucm
	ln $(WORK)/img/rootfs.squashfs $(WORK)/img/boot.vfat $(WORK)/bundle/
	# Made and checked in the container's own filesystem: RAUC won't read a
	# plain bundle off a FUSE mount such as Docker's share of the Mac's disk.
	rauc bundle --cert=$$p/rauc/keyring.pem --key=signing-key.pem $(WORK)/bundle $(WORK)/trimixxx-$(VERSION).raucb
	rauc info --keyring=$$p/rauc/keyring.pem $(WORK)/trimixxx-$(VERSION).raucb | grep -E "Compatible|Version|Build"
	cp --sparse=always $(WORK)/img/trimixxx.img $$o/trimixxx-$(VERSION).img
	cp $(WORK)/img/rootfs.squashfs $(WORK)/img/boot.vfat $(WORK)/trimixxx-$(VERSION).raucb $$o/

	# ---- what went in -----------------------------------------------------------------
	{
	    echo "TriMixxx $(VERSION), from $$COMMIT, built $$(date -u +%Y-%m-%dT%H:%MZ)"
	    echo
	    (cd $$o && sha256sum trimixxx-$(VERSION).img trimixxx-$(VERSION).raucb rootfs.squashfs boot.vfat)
	    echo
	    cat $(WORK)/packages.txt
	} > $$o/manifest.txt
	ls -l $$o

# Broken releases from release VERSION's images, each signed like a real one:
# out/VERSION-NAME/trimixxx-VERSION-NAME.raucb.
faults:
	o=out/$(VERSION) p=/repo/pi_config
	rm -rf $(WORK) && mkdir -p $(WORK)
	# Plain reads off Docker's share of the Mac's disk, never cp (see release).
	dd if=$$o/rootfs.squashfs of=$(WORK)/rootfs.squashfs bs=4M status=none
	dd if=$$o/boot.vfat of=$(WORK)/boot.vfat bs=4M status=none
	bundle() { # NAME ROOTFS BOOT
	    local v=$(VERSION)-$$1 d=$(WORK)/b-$$1
	    mkdir -p $$d out/$$v
	    sed -e "s|@VERSION@|$$v|" -e 's|@BUILD@|a fault test, from $(VERSION)|' manifest.raucm.in > $$d/manifest.raucm
	    ln $$2 $$d/rootfs.squashfs && ln $$3 $$d/boot.vfat
	    rauc bundle --cert=$$p/rauc/keyring.pem --key=signing-key.pem --keyring=$$p/rauc/keyring.pem $$d $(WORK)/$$v.raucb > /dev/null
	    cp $(WORK)/$$v.raucb out/$$v/trimixxx-$$v.raucb
	    echo "out/$$v/trimixxx-$$v.raucb"
	}
	# F4a, nonet: NetworkManager masked, so the deck joins no network. F4b,
	# nossh: sshd masked. Either way it can't take the next release, which is
	# all the health check asks.
	unsquashfs -q -no-progress -d $(WORK)/root $(WORK)/rootfs.squashfs
	for f in nonet:NetworkManager.service nossh:ssh.service; do
	    n=$${f%%:*} u=$${f#*:}
	    ln -s /dev/null $(WORK)/root/etc/systemd/system/$$u
	    sed -i "s/^VERSION=.*/VERSION=$(VERSION)-$$n/" $(WORK)/root/etc/trimixxx-release
	    mksquashfs $(WORK)/root $(WORK)/rootfs-$$n.squashfs -comp zstd -xattrs -noappend -quiet -no-progress
	    rm $(WORK)/root/etc/systemd/system/$$u
	    bundle $$n $(WORK)/rootfs-$$n.squashfs $(WORK)/boot.vfat
	done
	rm -rf $(WORK)/root
	# F5, noinitramfs: the kernel can't mount its SquashFS root, panics, and
	# panic=10 reboots it.
	cp $(WORK)/boot.vfat $(WORK)/boot-noinitramfs.vfat
	mdel -i $(WORK)/boot-noinitramfs.vfat ::/initramfs8
	bundle noinitramfs $(WORK)/rootfs.squashfs $(WORK)/boot-noinitramfs.vfat
	# F6a, panic: the initramfs gives up (break=premount, as for a root it
	# can't find), and with panic=10 initramfs-tools reboots after 10 s.
	# F6b, hang: the same without panic=, so a shell nobody answers, which only
	# the watchdog ends.
	for f in panic hang; do
	    cp $(WORK)/boot.vfat $(WORK)/boot-$$f.vfat
	    for c in cmdline-a.txt cmdline-b.txt; do
	        mtype -i $(WORK)/boot-$$f.vfat ::/$$c | sed -e '1 s/$$/ break=premount/' > $(WORK)/$$c
	        [ $$f = panic ] || sed -i 's/ panic=10 / /' $(WORK)/$$c
	        mcopy -o -i $(WORK)/boot-$$f.vfat $(WORK)/$$c ::/$$c
	    done
	    bundle $$f $(WORK)/rootfs.squashfs $(WORK)/boot-$$f.vfat
	done

# The deck's identity onto p7 of its copy of the release card (an APFS clone
# made on the Mac).
card:
	img=out/$(VERSION)/$(DECK)-$(VERSION).img
	read -r start size < <(partx -g -o START,SECTORS -n 7 $$img)
	rm -rf $(WORK) && mkdir -p $(WORK)
	mke2fs -q -t ext4 -L trimixxx-data -d /deck $(WORK)/data.ext4 $$((size / 2))k
	dd if=$(WORK)/data.ext4 of=$$img bs=1M seek=$$((start * 512)) oflag=seek_bytes conv=notrunc,sparse status=none
	debugfs -R 'ls -l /' $(WORK)/data.ext4 2>/dev/null | awk 'NR > 2 {print $$NF}' | tr '\n' ' '; echo
	ls -l $$img

endif
