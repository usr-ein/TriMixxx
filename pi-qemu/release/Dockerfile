# The release toolbox (PLAN.md Part 1): what builds a TriMixxx card and its
# updates, run without privileges. Debian trixie, the decks' own release, so
# the RAUC that signs bundles here is the RAUC that installs them there.
FROM debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        genimage rauc squashfs-tools dosfstools mtools e2fsprogs fdisk \
        zstd xz-utils openssl make \
 && rm -rf /var/lib/apt/lists/*
