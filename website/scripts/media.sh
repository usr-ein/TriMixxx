#!/bin/sh
# Fetches the deck's photographs: the trimixxx/ folder of usr-ein/static-resources,
# as a shallow, sparse clone into ./media (ignored by this repository's git).
# Run it again to update. Photos are added with scripts/add-photos.sh, then
# committed and pushed from ./media like any repository.
#
# On a laptop with no network the site still builds, without photos. In CI a
# failed clone fails the build, so a deploy never quietly loses its pictures.
set -eu

repo=${MEDIA_REPO:-https://github.com/usr-ein/static-resources.git}
branch=${MEDIA_BRANCH:-master}
site=$(cd "$(dirname "$0")/.." && pwd)
dir="$site/media"

if [ -d "$dir/.git" ]; then
  git -C "$dir" pull --ff-only --quiet
elif GIT_TERMINAL_PROMPT=0 git clone --quiet --depth 1 --filter=blob:none --sparse \
  --branch "$branch" "$repo" "$dir"; then
  git -C "$dir" sparse-checkout set trimixxx
else
  echo "media: could not clone $repo" >&2
  if [ -n "${CI:-}" ]; then exit 1; fi
  echo "media: building without photos" >&2
fi

if [ -d "$dir/trimixxx/photos" ]; then
  count=$(find "$dir/trimixxx/photos" -name '*.yaml' | wc -l | tr -d ' ')
  echo "media: $count photos"
fi
