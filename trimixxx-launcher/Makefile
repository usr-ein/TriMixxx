BINS = trimixxx-launchd trimixxx-deckkeys
DIST ?= dist

# Cross-build (Docker): produce fully static ARM binaries from any host.
ARM_PLATFORM ?= linux/arm64

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

# Onto a deck: pi-qemu deck deploy TARGET launcher (pi-qemu/deploy/003_launcher.sh),
# which builds docker-arm and installs what it made.

clean:
	rm -f $(BINS)
	rm -rf $(DIST)

.PHONY: all test chart docker-arm clean
