BINS = trimixxx-launchd trimixxx-deckkeys
DIST ?= dist

# Cross-build (Docker): produce fully static ARM binaries from any host.
ARM_PLATFORM ?= linux/arm64

# Host to deploy to (an ssh alias, same one upload.sh uses).
HOST ?= trimixxx-pi

# Native build, for developing on this machine. Dynamic: needs cgo + ALSA
# headers (libasound2-dev on Debian). Not what ships to the Pi -- see docker-arm.
# deckkeys compiles everywhere but only does anything on Linux (uinput is a
# kernel facility); off the Pi, run it with --dry-run.
all:
	go build -o trimixxx-launchd ./cmd/launchd
	go build -o trimixxx-deckkeys ./cmd/deckkeys

test:
	go test ./...

# The control charts, straight out of the mapping tables. Handy for the docs and
# for checking a mapping change without a deck in front of you.
chart: all
	./trimixxx-deckkeys --map doom --print-map
	./trimixxx-deckkeys --map console --print-map

# The real build. Fully static (musl + ALSA + libstdc++) via Alpine, so the
# binaries run on Raspberry Pi OS with no runtime deps. Land in $(DIST)/.
docker-arm:
	docker build --platform $(ARM_PLATFORM) --target export \
		--output type=local,dest=$(DIST) .
	@file $(DIST)/trimixxx-launchd $(DIST)/trimixxx-deckkeys

# Ship it: both binaries + the unit onto the Pi, then (re)start the service.
#
# Restarting the launcher does NOT disturb the running session -- it sees that
# the mode file already exists and skips the boot gesture. It does drop and
# recreate the virtual MIDI port, though, and Mixxx enumerates MIDI devices once
# at startup: a Mixxx that was already running loses the pi-midi-daemon
# controller until it is restarted. Nothing new, but worth knowing mid-set.
install-remote: docker-arm
	scp $(DIST)/trimixxx-launchd $(DIST)/trimixxx-deckkeys $(HOST):/tmp/
	scp trimixxx-launchd.service $(HOST):/tmp/
	ssh $(HOST) 'set -eu; \
		sudo install -m 0755 /tmp/trimixxx-launchd /tmp/trimixxx-deckkeys /usr/local/bin/ ; \
		sudo install -m 0644 /tmp/trimixxx-launchd.service /etc/systemd/system/ ; \
		rm -f /tmp/trimixxx-launchd /tmp/trimixxx-deckkeys /tmp/trimixxx-launchd.service ; \
		if systemctl list-unit-files | grep -q "^pi-midi-daemon.service"; then \
			echo "removing the old pi-midi-daemon.service -- this daemon replaces it" ; \
			sudo systemctl disable --now pi-midi-daemon.service || true ; \
			sudo rm -f /etc/systemd/system/pi-midi-daemon.service /usr/local/bin/pi-midi-daemon ; \
		fi ; \
		sudo systemctl daemon-reload ; \
		sudo systemctl enable trimixxx-launchd.service ; \
		sudo systemctl restart trimixxx-launchd.service'
	ssh $(HOST) 'systemctl --no-pager status trimixxx-launchd.service | head -8; cat /run/trimixxx/mode'

clean:
	rm -f $(BINS)
	rm -rf $(DIST)

.PHONY: all test chart docker-arm install-remote clean
