#!/usr/bin/env bash
# A git worktree of this repo, ready to build and deploy the deck's code --
# for agents (and people) working side by side, one worktree each.
#
#   pi-qemu/worktree.sh prepare     check out the submodules the deck is built from
#   pi-qemu/worktree.sh status      what this worktree has: submodules, branches, build trees
#   pi-qemu/worktree.sh release     before removing the worktree: its submodule
#                                     checkouts and its Docker build trees, gone
#
# It acts on the checkout it is in (pi-qemu/worktree.sh of that worktree),
# wherever it is run from.
#
# A new worktree has empty submodule directories. `prepare` makes mixxx/, its
# lib/prolink and mixxx_config/ttymidi linked worktrees of the MAIN checkout's
# submodule repositories, detached at the commits this worktree records:
# seconds, nothing downloaded, and -- unlike `git submodule update`, which
# clones a private copy that dies with the worktree -- a commit or branch made
# in them lands in the main checkout's repositories at once. So work on the
# Mixxx fork in a worktree: `git -C mixxx switch -c BRANCH` first, commit there,
# and the main checkout sees BRANCH (merge it in mixxx/, then bump mixxx in the
# parent repo).
#
# Each checkout builds Mixxx in its own Docker build tree (mixxx/checkout-id.sh);
# a worktree's first one starts as a copy of the main checkout's. That is ~4 GB
# of Docker's disk per worktree, which `release` gives back. git will not remove
# a worktree whose submodules are checked out (without --force), which `release`
# also undoes; it refuses itself if they hold uncommitted changes. A worktree
# removed with --force anyway keeps its Mixxx branches -- they are the main
# checkout's -- but not its build trees, nor detached commits.
set -euo pipefail

die() { echo "worktree.sh: $*" >&2; exit 1; }
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "$HERE" rev-parse --show-toplevel)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
SUBS=(mixxx mixxx/lib/prolink mixxx_config/ttymidi)

is_main() { [ "$ROOT" = "$MAIN" ]; }
checked_out() { [ -e "$ROOT/$1/.git" ]; }

# $1: a submodule path. The commit the repository around it records for it:
# this worktree's for mixxx and ttymidi, mixxx's own for its lib/prolink.
recorded() {
    case "$1" in
        mixxx/lib/prolink) git -C "$ROOT/mixxx" rev-parse "HEAD:lib/prolink" ;;
        *) git -C "$ROOT" rev-parse "HEAD:$1" ;;
    esac
}

# $1: a submodule path. A linked worktree of the main checkout's repository for
# it, at the commit recorded for it.
add_sub() {
    local path=$1 sha
    checked_out "$path" && return 0
    sha="$(recorded "$path")"
    [ -e "$MAIN/$path/.git" ] || die "the main checkout has no $path to borrow from: git -C $MAIN submodule update --init --recursive"
    rmdir "$ROOT/$path" 2>/dev/null || true
    # entries left by a worktree removed with --force, before this one is added
    git -C "$MAIN/$path" worktree prune
    git -C "$MAIN/$path" worktree add --quiet --detach "$ROOT/$path" "$sha"
    echo "$path: at ${sha:0:10}, a worktree of $MAIN/$path"
}

# $1: a submodule path. Its linked worktree removed -- not if it has changes.
remove_sub() {
    local path=$1
    checked_out "$path" || return 0
    if [ -n "$(git -C "$ROOT/$path" status --porcelain --ignore-submodules=all)" ]; then
        die "$path has uncommitted changes: commit them (on a branch) or discard them first"
    fi
    local branch sha
    branch="$(git -C "$ROOT/$path" symbolic-ref --quiet --short HEAD || true)"
    sha="$(git -C "$ROOT/$path" rev-parse HEAD)"
    # Detached at a commit nothing else holds: work that would only survive in
    # a reflog. A branch keeps it.
    if [ -z "$branch" ] && [ "$sha" != "$(recorded "$path")" ] &&
        [ -z "$(git -C "$MAIN/$path" branch --all --contains "$sha" 2>/dev/null)" ]; then
        die "$path is detached at commits no branch holds: git -C $path switch -c BRANCH first"
    fi
    git -C "$MAIN/$path" worktree remove "$ROOT/$path"
    mkdir -p "$ROOT/$path"
    echo "$path: released${branch:+ (branch $branch stays in $MAIN/$path)}"
}

# This checkout's Mixxx build trees in Docker's cache, emptied: a throwaway
# build mounts each by its id and deletes what is in it.
drop_builds() {
    local id; id="$(CHECKOUT_ID= "$ROOT/mixxx/checkout-id.sh" 2>/dev/null || true)"
    [ -n "$id" ] || { echo "no Docker build trees to drop (main checkout, or mixxx not checked out)"; return 0; }
    command -v docker >/dev/null && docker info >/dev/null 2>&1 || { echo "Docker is not running: build trees left in place"; return 0; }
    local tmp; tmp="$(mktemp -d)"
    cat > "$tmp/Dockerfile" <<'EOF'
FROM debian:trixie
ARG BUILD_ID TEST_ID
RUN --mount=type=cache,target=/b,sharing=locked,id=${BUILD_ID} \
    --mount=type=cache,target=/t,sharing=locked,id=${TEST_ID} \
    find /b /t -mindepth 1 -delete
EOF
    docker buildx build --quiet --pull=false --no-cache --platform linux/arm64 \
        --build-arg BUILD_ID="mixxx-build-debian:trixie$id" \
        --build-arg TEST_ID="mixxx-build-test-debian:trixie$id" \
        --output type=cacheonly "$tmp" >/dev/null
    rm -rf "$tmp"
    echo "Docker build trees for $id: emptied"
}

case "${1:-}" in
prepare)
    is_main && { echo "this is the main checkout: its submodules are its own"; exit 0; }
    add_sub mixxx
    add_sub mixxx/lib/prolink
    add_sub mixxx_config/ttymidi
    ;;
status)
    echo "checkout: $ROOT$(is_main && echo ' (the main one)')"
    for p in "${SUBS[@]}"; do
        if checked_out "$p"; then
            printf '  %-22s %s %s\n' "$p" "$(git -C "$ROOT/$p" rev-parse --short HEAD)" \
                "$(git -C "$ROOT/$p" symbolic-ref --quiet --short HEAD || echo '(detached)')"
        else
            printf '  %-22s not checked out\n' "$p"
        fi
    done
    checked_out mixxx && echo "  Mixxx build tree: mixxx-build-debian:trixie$("$ROOT/mixxx/checkout-id.sh")"
    ;;
release)
    is_main && die "this is the main checkout: nothing to release"
    checked_out mixxx && drop_builds
    for p in mixxx/lib/prolink mixxx mixxx_config/ttymidi; do remove_sub "$p"; done
    echo "released: git worktree remove $ROOT"
    ;;
*)
    sed -n '/^#   pi-qemu\/worktree.sh/s/^# *//p' "$0" >&2
    exit 2
    ;;
esac
