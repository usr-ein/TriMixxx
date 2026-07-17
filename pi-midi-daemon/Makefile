BIN  = pi-midi-daemon
DIST ?= dist

# Cross-build (Docker): produce a fully static ARM binary from any host.
ARM_PLATFORM ?= linux/arm64

# Host to deploy to (an ssh alias, same one upload.sh uses).
HOST ?= trimixxx-pi

# Native build, for developing on this machine. Dynamic: needs cgo + ALSA
# headers (libasound2-dev on Debian). Not what ships to the Pi -- see docker-arm.
all:
	go build -o $(BIN) .

test:
	go test ./...

# The real build. Fully static (musl + ALSA + libstdc++) via Alpine, so the
# binary runs on Raspberry Pi OS with no runtime deps. Lands in $(DIST)/$(BIN).
docker-arm:
	docker build --platform $(ARM_PLATFORM) --target export \
		--output type=local,dest=$(DIST) .
	@file $(DIST)/$(BIN)

# Ship it: static binary + unit onto the Pi, then (re)start the service.
install-remote: docker-arm
	scp $(DIST)/$(BIN) $(HOST):/tmp/$(BIN)
	scp $(BIN).service $(HOST):/tmp/$(BIN).service
	ssh $(HOST) 'sudo install -m 0755 /tmp/$(BIN) /usr/local/bin/ \
		&& sudo install -m 0644 /tmp/$(BIN).service /etc/systemd/system/ \
		&& sudo systemctl daemon-reload \
		&& sudo systemctl enable $(BIN).service \
		&& sudo systemctl restart $(BIN).service \
		&& rm -f /tmp/$(BIN) /tmp/$(BIN).service'
	ssh $(HOST) 'systemctl --no-pager status $(BIN).service | head -5'

clean:
	rm -f $(BIN)
	rm -rf $(DIST)

.PHONY: all test docker-arm install-remote clean
