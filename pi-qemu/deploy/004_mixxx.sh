#!/usr/bin/env bash
# Deploy step 004 (lib.sh has the contract): the Mixxx fork (mixxx/, a
# submodule), built for the deck in Docker, over the binary apt installed.
# Only the binary: /usr/share/mixxx stays as apt left it, and the skin and
# config live in ~/.mixxx (005_config).
#
# The build is in this checkout's own Docker build tree (mixxx/checkout-id.sh),
# against the Debian release the deck runs: trixie, pinned here rather than
# asked of the deck, so a build never quietly becomes another release. An
# older one fails to link (__cxa_call_terminate), a newer one links and then
# won't start on the deck's older glibc. The deck is checked against it.
#
# The same binary is left alone; a new one restarts the session (exit 12).
# needs: docker
. "$(dirname "$0")/lib.sh"

CODENAME=trixie
deck_codename="$(ssh deck '. /etc/os-release && printf %s "$VERSION_CODENAME"')"
if [ "$deck_codename" != "$CODENAME" ]; then
    echo "the deck runs $deck_codename, the fork builds for $CODENAME: fix the pin here" >&2
    exit 1
fi

# shellcheck disable=SC2046 # the build args are words, on purpose
(cd mixxx && docker buildx build --platform linux/arm64 --target export \
    --build-arg BASE="debian:${CODENAME}" \
    $(./checkout-id.sh --build-args) \
    --output type=local,dest=dist .)

# Where apt put it, rather than assuming /usr/bin/mixxx. Staged under a name
# nothing else uses: /tmp/mixxx is the deck's log directory, and scp into a
# directory quietly drops the file inside it.
bin="$(ssh deck 'command -v mixxx')"
staged=/tmp/mixxx.upload
scp mixxx/dist/mixxx deck:$staged
if ssh deck "cmp -s $staged $bin"; then
    ssh deck "rm -f $staged"
    echo "==> deck:$bin: the same binary, left alone"
    exit $DONE
fi
# Every library the new binary asks for must be on the deck before it
# replaces a working Mixxx. An ldd that cannot read it stops this too.
libs="$(ssh deck "ldd $staged")" || { echo "ldd could not read $staged on the deck: not installing" >&2; exit 1; }
if grep -F 'not found' <<< "$libs"; then
    ssh deck "rm -f $staged"
    echo "the libraries above are missing on the deck: not installing" >&2
    exit 1
fi
# apt's binary kept aside the first time, so a bad build is undone with a
# copy: sudo cp -a /usr/bin/mixxx.apt /usr/bin/mixxx
ssh deck "test -e $bin.apt || sudo cp -a $bin $bin.apt"
ssh deck "sudo install -m 0755 $staged $bin && rm -f $staged"
# Mixxx is started by the tty1 autologin.
ssh deck 'sudo systemctl restart getty@tty1.service'
echo "==> deck:$bin"
exit $SESSION_RESTARTED
