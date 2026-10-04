# TriMixxx

A DJ deck: a Raspberry Pi 4 runs a fork of Mixxx full screen on a 10" panel,
and an ESP32-S3 board (the S3) reads the deck's controls and talks MIDI to the
Pi over a UART.

| Path | What it is |
|---|---|
| `mixxx/` | the Mixxx fork the deck runs: a submodule, its own repo (`usr-ein/mixxx`); `mixxx/lib/prolink` is a third repo inside it |
| `mixxx_config/` | the deck's Mixxx setup: mapping and scripts, skin, `mixxx.cfg`, each deck's wiring (`units/<deck>.json`); `ttymidi/` (a submodule) bridges the UART to ALSA MIDI |
| `pi_config/` | the Pi's system: units, session, splash, networking (`upload.sh`), and `fresh-install.md` |
| `trimixxx-launcher/` | boot modes and the deck's own keys on the Pi |
| `firmwares/trimixxx-midi/` | the S3's firmware, and the MIDI contract (`lib/PiLink/MidiMap.hpp`); see its CLAUDE.md |
| `pi-qemu/` | an emulated deck (trimixxx0) on the Mac, and `deploy.sh`, which sets up any deck |

## Testing on a deck

Test deck software on an emulated deck of your own: **use the `trimixxx0`
skill** (`.claude/skills/trimixxx0/SKILL.md`). It starts in seconds and is
headless and silent. Several run side by side, one per agent. `trimixxx-pi`,
`trimixxx2` and other ssh aliases on this Mac are **real decks**: never deploy
to one unless asked. The repo's upload scripts default to `HOST=trimixxx-pi`,
which is why they are only run through `pi-qemu/instance.sh`. For people, the
guide is `pi-qemu/README.md`.

## Working in a git worktree

Agents work side by side, each in its own git worktree of this repo (the
harness's, or `git worktree add .claude/worktrees/NAME -b NAME`). Working
from a worktree:

1. **`pi-qemu/worktree.sh prepare`** before anything else. A new worktree's
   submodules are empty directories. `prepare` makes `mixxx/`, its
   `lib/prolink` and `mixxx_config/ttymidi` linked worktrees of the main
   checkout's repositories, detached at the commits this branch records. It
   takes seconds and downloads nothing. Not `git submodule update`: that clones
   private copies, and commits made in them die with the worktree.
2. **Changing Mixxx:** `git -C mixxx switch -c NAME` before the first commit
   there. Its commits and branches are then in the main checkout's `mixxx/` at
   once. A commit in a submodule changes the parent repo too: commit the bump
   (`git add mixxx`) on your branch as well.
3. **Building and trying it:** `pi-qemu/instance.sh up NAME`, then
   `pi-qemu/instance.sh deploy NAME mixxx` (or `config`, `system`, ...). It
   deploys *this worktree's* code. Use the worktree's own `pi-qemu/instance.sh`.
   - Each worktree builds Mixxx in its own Docker build tree (`mixxx/checkout-id.sh`).
     The first build copies the main checkout's tree and rebuilds only what
     differs, then each build after that only what changed, both by content.
   - A Mixxx deploy takes about 75 s the first time in a worktree, then about
     45 s.
   - Builds from several worktrees queue on the shared compiler cache, so a
     deploy may wait for another agent's build first. They never mix code
     from two checkouts.
4. **When done:** commit, `pi-qemu/instance.sh rm NAME`, then
   **`pi-qemu/worktree.sh release`**. That gives back the worktree's ~4 GB
   Docker build tree and its submodule checkouts. It refuses if they hold
   uncommitted work or commits no branch has. git will not remove a worktree
   with checked-out submodules until this is done (short of `--force`, which
   leaves the Docker build tree behind).

`pi-qemu/worktree.sh status` shows what a worktree has checked out. Branches
are merged in the main checkout: the fork's in `mixxx/` first, then this repo's,
with the submodule bump on top.

## Commits

Commit as `Samuel Prevost <usr_ein@pm.me>`
(`git -c user.name="Samuel Prevost" -c user.email=usr_ein@pm.me commit ...`),
in this repo, the fork and `lib/prolink` alike, and pass the same `-c` flags to
`git rebase`. A change inside a submodule is committed there first, then bumped
here.
