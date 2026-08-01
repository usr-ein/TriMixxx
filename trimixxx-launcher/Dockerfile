# Builds fully static TriMixxx launcher binaries for ARM Linux (e.g. Raspberry
# Pi OS): trimixxx-launchd and trimixxx-deckkeys.
#
# Same trick as the ttymidi fork's Dockerfile: Alpine/musl gives a tiny
# toolchain, but the binaries are linked fully static (musl + ALSA + libstdc++),
# so they run on ANY ARM Linux of the target architecture -- including
# glibc-based Raspberry Pi OS -- with nothing to install on the device.
#
# Two things differ from ttymidi's C build:
#   - cgo must be told to link statically (-linkmode external -extldflags -static).
#   - rtmidi (vendored inside gomidi) is C++, hence g++ and a static libstdc++.
#
# ALSA ships no static archive on Alpine, so libasound.a is built from source --
# exactly as ttymidi does. uinput needs nothing: it is raw ioctls, no library.
#
# Build with: make docker-arm   (see Makefile / README).

FROM golang:1.26-alpine AS build

RUN apk add --no-cache g++ musl-dev linux-headers make wget bzip2

# Static libasound.a from source (Alpine only packages the shared lib). Kept as
# its own layer so it stays cached across source changes.
ARG ALSA_VERSION=1.2.11
RUN wget -q "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2" \
    && tar xf "alsa-lib-${ALSA_VERSION}.tar.bz2" \
    && cd "alsa-lib-${ALSA_VERSION}" \
    && ./configure --enable-static --disable-shared --disable-python --without-debug \
    && make -j"$(nproc)" \
    && make install \
    && cd / && rm -rf "/alsa-lib-${ALSA_VERSION}" "/alsa-lib-${ALSA_VERSION}.tar.bz2"

WORKDIR /build
# Dependencies first, so the module cache survives source-only changes.
COPY go.mod go.sum ./
RUN go mod download
COPY internal/ ./internal/
COPY cmd/ ./cmd/

ENV CGO_ENABLED=1
ENV GOFLAGS=-trimpath
ENV LDFLAGS='-s -w -linkmode external -extldflags "-static"'

# The `! readelf -d | grep NEEDED` on each: fail the build rather than ship
# something that needs a runtime loader on the device.
RUN go build -ldflags "$LDFLAGS" -o /trimixxx-launchd ./cmd/launchd \
    && ! readelf -d /trimixxx-launchd | grep -q NEEDED

RUN go build -ldflags "$LDFLAGS" -o /trimixxx-deckkeys ./cmd/deckkeys \
    && ! readelf -d /trimixxx-deckkeys | grep -q NEEDED

# Export stage: `--output` copies just the binaries out to the host.
FROM scratch AS export
COPY --from=build /trimixxx-launchd /trimixxx-launchd
COPY --from=build /trimixxx-deckkeys /trimixxx-deckkeys
