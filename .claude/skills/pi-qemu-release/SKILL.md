---
name: pi-qemu-release
description: Build TriMixxx's SD card images and releases with pi-qemu - a deck's dev card from stock Raspberry Pi OS (`pi-qemu image build`), the A/B release card and its RAUC update bundle from HEAD's pi/vX.Y.Z tag (`release build`), each deck's own card with its identity (`release card`), broken updates for the fault tests (`release faults`), an update shipped onto a deck (`deck ship`), and a real deck readied for its first release card (`deck prepare`, bootloader and boot watchdog included). Use when asked to make an image, a release, a deck's card, to ship or rehearse an update, or to prepare a deck; and to try a release on emulated decks before any real deck gets it.
---

# pi-qemu: images, releases, cards

The deck's software reaches a card two ways, both through `pi-qemu`:

- **A dev card** (`pi-qemu image build DECK`): stock Raspberry Pi OS, then
  every deploy step (`pi-qemu/deploy/NNN_*`), as against any deck. What the
  emulated decks run, and what a release is made from.
- **A release** (`pi-qemu release build`): that system sealed read-only into
  an A/B card every deck can run, and its RAUC bundle for updates over the
  air; then each deck's card, with its identity on p7 (`release card DECK`).

`pi-qemu/PLAN.md` Part 1 is the design (its invariants are numbered; the
code cites them), `pi-qemu/rauc-pi-4-setup.md` the how-to, and PLAN.md §7
the runbook for putting real decks on release cards.

## Rules

1. **Real decks only when asked.** `--host trimixxx-pi`, `trimixxx-pi-2`,
   `trimixxx2`, ... are real decks: ship to one, or prepare one, only when the
   person asked for that deck. Try everything on emulated decks first.
2. **The person flashes cards.** Build the image and give them the exact
   path; never run `diskutil`, `dd` or anything else on a card or disk.
3. **Releases are tags.** A release's version is HEAD's `pi/vX.Y.Z` tag, and
   `release build` refuses a dirty tree. Tags are signed, so they need a
   message: `git tag -m "TriMixxx X.Y.Z" pi/vX.Y.Z`. The repo's other parts
   tag `midi-s3/v…` and `prolinks-compat/v…`. Tagging is the person's call.
4. **The golden pair is the person's call.** `image build trimixxx0` ends by
   remaking it (`deck golden`), and every new instance starts from it: build
   trimixxx0 only when asked. Try image builds on a scratch DECK name, and
   delete its outputs afterwards (below).
5. **The bootloader is the person's call.** `deck prepare --eeprom` flashes a
   real deck's EEPROM and reboots it.
6. **Docker's disk:** builds need ~6 GB free; pi-qemu checks and stops.
   Freeing space is the person's call: say so, never prune. Keep the build
   caches (`pi-qemu/.cache/base`, Docker's build trees).

## Dev cards

```sh
pi-qemu image build onetest           # -> pi-qemu/.cache/build/onetest.img, its log onetest.log
pi-qemu image build onetest --no-window --rebuild-base
```

- It builds in an instance of its own, `build-DECK`: `pi-qemu deck ssh
  build-DECK` looks at a build in progress. A failed build leaves its machine
  off and its card for a look (`deck up build-DECK`, then `deck rm build-DECK`).
- The first boot and the base step are cached in `pi-qemu/.cache/base/` while
  their inputs are unchanged (the stock card, the deck's name and panel, the
  key, the password, the seed, `000_base.pi.sh`). The rest takes minutes,
  most of it Docker builds that are cached too.
- The stock card is pinned in `pi-qemu/image/stock.env`; the first-boot seed
  is `image/user-data.in` and `network-config`; the console password and the
  home Wi-Fi are in `image/secrets.env` (gitignored, never printed or
  committed).
- A build window follows the log unless `--no-window`; its steps come from
  the build's own `==> plan:` line.
- A scratch build's outputs to delete afterwards: `pi-qemu/.cache/build/DECK.img`,
  `DECK.log`, and `pi-qemu/.cache/base/DECK-*.img`.

## Releases

```sh
git tag -m "TriMixxx 1.2.0" pi/v1.2.0      # the person's call
pi-qemu release build                      # -> pi-qemu/release/out/1.2.0/
pi-qemu release card trimixxx2             # -> out/1.2.0/trimixxx2-1.2.0.img, to flash
pi-qemu deck ship --host trimixxx-pi-2     # the update, over the air (only when asked)
pi-qemu release faults                     # out/1.2.0-nonet, -nossh, -noinitramfs, -panic, -hang
```

- `release build` builds the system (`image build trimixxx-release`), then
  seals it in the release container (`release/Dockerfile` running
  `release/container.sh`): `trimixxx-X.Y.Z.img` (every deck's card),
  `trimixxx-X.Y.Z.raucb` (the update), `rootfs.squashfs`, `boot.vfat`,
  `manifest.txt`. `--keep-system` seals the system card built last time;
  `--rehearsal X.Y.Z` lets an untagged or dirty tree through, and says so in
  the manifest.
- Releases always land in the main checkout's `pi-qemu/release/out/`, also
  from a worktree. `--version X.Y.Z` names another release than HEAD's for
  `release card`, `release faults` and `deck ship`.
- A deck's identity (its name, ssh host keys kept across reflashes, its
  hotspot, the home Wi-Fi) is in `pi-qemu/.cache/decks/DECK/`, made the first
  time `release card` needs it.
- `deck ship` streams the bundle into RAUC's state partition, installs it
  into the other slot and reboots into it as a trial; the deck's health check
  (network up and ssh answering) keeps it or rolls it back.

## Trying a release on emulated decks

```sh
pi-qemu release build --rehearsal 0.0.9 --keep-system --no-window
pi-qemu release card trimixxx0 --version 0.0.1            # the emulated deck's card of a release
pi-qemu deck up abtest --from pi-qemu/release/out/0.0.1/trimixxx0-0.0.1.img
pi-qemu deck ship abtest --version 0.0.9                  # a trial of B, then its health check
pi-qemu deck ssh abtest 'tail -3 /var/lib/rauc/trimixxx-health.log; rauc status'
```

Afterwards, delete what the rehearsal made: `deck rm abtest`, and
`pi-qemu/release/out/0.0.9*` and `out/0.0.1/trimixxx0-0.0.1.img`. Keep the
tagged releases and their decks' cards: an untagged release is never left in
`out/`.

## Readying a real deck (only when asked)

```sh
pi-qemu deck prepare --host trimixxx-pi-2 trimixxx2            # reads it only
pi-qemu deck prepare --host trimixxx-pi-2 trimixxx2 --eeprom   # bootloader + boot watchdog: reboots it
```

It writes the board's serial into `mixxx_config/units/DECK.json` (commit it
before the release is built: the release's config.txt gives each board its
own section), keeps the deck's ssh host keys for its identity, checks the
bootloader (2022-12-01 or newer knows `tryboot_a_b`), and captures its boot
files and profiles in `pi-qemu/.cache/captures/` (private: Wi-Fi passwords).
An emulated deck can stand in to try it (`deck prepare NAME DECK`); it has no
bootloader, so `--eeprom` stops there.
