# The release toolbox (PLAN.md Part 1): what builds a TriMixxx card and its
# updates. Debian trixie, the decks' own release, so the RAUC that signs
# bundles here is the RAUC that installs them there. Run without privileges,
# but for `make release`, which mounts the built card read-only.
FROM debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        genimage rauc squashfs-tools dosfstools mtools e2fsprogs fdisk \
        zstd xz-utils openssl make attr \
        python3 python3-pil librsvg2-bin \
 && rm -rf /var/lib/apt/lists/*
