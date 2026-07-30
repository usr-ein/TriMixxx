# Hardware runbook

For the session with the two CDJ-2000NXS. Ordered so the riskiest unknown is
answered first: if step 3 comes back empty, everything after it is moot and the
project pivots to the dbserver path.

Record everything. Each command writes a JSONL journal under `captures/<utc>/`
unless you pass `--no-record`; add `--notes "unit A, USB in, playing"` and it
lands in the journal too. A capture without the hardware state written down is
close to worthless a month later.

## Setup

```bash
cd prolinks-compat
uv venv && uv pip install -e '.[dev]'
.venv/bin/python -m pytest tests/ -q          # 150 should pass
```

Wiring: Mac → USB-Ethernet dongle → unmanaged switch → both CDJs. The players
have no built-in switch. Leave DHCP off and let everything self-assign
`169.254/16`; it takes ~10 s after link-up.

```bash
prolinks interfaces      # confirm the dongle appears with a link-local address
```

Everything below is passive except `announce`. Run a `tcpdump` alongside for an
independent record:

```bash
sudo tcpdump -i en5 -w captures/session.pcapng
```

## 1. Discovery — are we on the same network?

```bash
prolinks devices --watch
```

Expect both players with the numbers, IPs and MACs shown on their
`UTILITY → LINK` screens. This also prints the literal 20-byte name fields.

*If nothing appears:* wrong interface (`--iface en5`), no link-local address
yet, or the macOS firewall is dropping inbound UDP 50000.

## 2. Baseline capture

```bash
prolinks sniff --duration 60 --decode --notes "both units idle, USB in slot"
```

Then power-cycle one player while sniffing, to catch the full claim handshake —
we have only four `ClaimIp` packets in total from the reference captures.

## 3. The go/no-go gate — does an NXS serve NFS?  *(experiment E4)*

The one thing that decides whether the chosen transport works on this hardware.
`research/06`'s "confirmed" NFS evidence is an **XDJ** capture, and the players
in `LinkInfo.pcapng` are not pinned down either. Run the matrix:

```bash
for ip in <IP-A> <IP-B>; do
  prolinks rpcinfo $ip --notes "USB in, idle"
done
```

Repeat for: USB only / SD only / both / neither, and idle / track loaded /
playing. Twelve probes per unit; each takes seconds.

- **portmapper answers, mountd + nfsd registered** → continue to step 4.
- **portmapper answers, NFS programs absent** → retry with media inserted; they
  may register only on mount. If never, stop and pivot to dbserver.
- **nothing on UDP/111 under any condition, on both units** → the NFS transport
  is dead for this hardware. Stop, write it up in `FINDINGS.md`, and re-plan
  around `research/04`.

## 4. Exports  *(experiment E3)*

```bash
prolinks exports <IP>
```

Prints the raw UTF-16LE bytes as well as the decode. We already know from the
captures that the path is **not always `/C/`** — one device serves `/C/EXPORT`
(FINDINGS C6) — so record verbatim what these units say. The client matches by
prefix, so both spellings work.

## 5. Mount, and the `NFSERR_ACCES` question  *(experiment E2)*

```bash
prolinks mount <IP> --slot usb
```

If it fails with status 13, that is what libcdj hit. Work the hypotheses:

```bash
prolinks mount <IP> --slot usb --auth null              # H1: is AUTH_UNIX required?
prolinks mount <IP> --slot usb --stamp 0xdeadbeef       # H1: does the stamp matter?
sudo .venv/bin/prolinks mount <IP> --slot usb --source-port 1023   # H3: reserved port?
prolinks mount <IP> --slot sd                           # H2: empty slot?
```

The stamp is now known to be a per-call nonce (FINDINGS C8), so H1's stamp half
is already all but ruled out — the flavour is what to test.

## 6. The anchor test — byte-exact file transfer  *(milestone M4)*

The strongest verification available:

```bash
prolinks pull-db <IP> --slot usb --assert-passive -o /tmp/export-nfs.pdb
```

Then eject the stick, plug it into the Mac, and compare:

```bash
shasum -a 256 /Volumes/<STICK>/PIONEER/rekordbox/export.pdb /tmp/export-nfs.pdb
```

Byte-identical or the transport is not trustworthy. `--assert-passive` makes
the run fail if a single datagram goes out on a DJ-Link port, which is what
turns experiment E1 — "does NFS work without announcing ourselves?" — into
evidence rather than an assumption.

*If it fails with NOENT:* the media may be HFS-formatted, putting the tree
under `.PIONEER`. `prolinks nfsprobe <IP> --slot usb` tries both and also
tabulates which NFSv2 procedures the player implements (experiment E5).

## 7. Listing the library  *(milestone M5)*

The pdb parser is validated against a synthetic database only — tonight is its
first contact with a real one, so expect this to be where bugs surface.

```bash
prolinks pdb-dump  --file /tmp/export-nfs.pdb      # structure and row counts
prolinks tracks    --file /tmp/export-nfs.pdb      # the track list
prolinks playlists --file /tmp/export-nfs.pdb      # the playlist tree
```

Cross-check track count, titles, BPMs and the playlist tree against the CDJ's
own browse screen. Then do it in one step, straight off the player:

```bash
prolinks tracks <IP> --slot usb
```

Also point it at the stick mounted locally, which is the same code path serve
mode will use:

```bash
prolinks tracks --volume /Volumes/<STICK>
```

## 8. Fetching audio  *(milestone M7)*

Take a `path` from `prolinks tracks --json` and pull it:

```bash
prolinks fetch <IP> --slot usb --path "Contents/..." -o /tmp/track.mp3
afplay /tmp/track.mp3
```

Then measure, for experiment E7:

```bash
for chunk in 1024 1280 2048 4096 8192; do
  for window in 1 2 4 8; do
    prolinks fetch <IP> --slot usb --path "Contents/..." \
      --chunk $chunk --window $window --json -o /tmp/t.bin | \
      jq -r '"chunk=\(.chunk) window=\(.window) \(.throughput_kib_s) KiB/s"'
  done
done
```

Watch the `tcpdump` for IP fragmentation above 1280, and watch whether the
player's own UI stutters — being a rude NFS client to a deck mid-set is not
acceptable behaviour for the eventual Mixxx feature.

## 8b. Browsing over dbserver — what the LINK button actually drives

NFS gets us the files; dbserver is what a CDJ uses to *browse*. Needs a device
number in 1-4 that is present on the network and is not the player being
queried, so pick one belonging to the other CDJ:

```bash
prolinks db-browse <IP-A> --as 2 --what root
prolinks db-browse <IP-A> --as 2 --what tracks
prolinks db-browse <IP-A> --as 2 --what playlists
prolinks db-browse <IP-A> --as 2 --what metadata --id <track-id>
```

Cross-check the track list against `prolinks tracks` from the same slot — the
two paths should agree, and any disagreement is interesting.

**Also capture the undocumented messages.** A real player sends `0x3e03`
immediately after `Introduce` (see FINDINGS C11). Sniff a genuine CDJ-to-CDJ
LINK browse with `tcpdump -w`, then run `prolinks pcap` over it — knowing what
`0x3e03` expects may be what makes step 10 work at all.

## 9. Announcing  *(milestone M9 — the first time we transmit)*

Check what we would send before sending it:

```bash
prolinks announce --dry-run
```

Our keep-alive is already byte-identical to a real CDJ-2000nexus one except for
device number, MAC, IP, peer count, and byte `0x25` (meaning still open,
FINDINGS C4). Then go live at a number that cannot collide:

```bash
prolinks announce --number 7 --duration 60
```

Both CDJs should list us. Only then consider `--claim`, which takes a real deck
slot (4 → 3 → 2 → 1, backing off on conflict) and is the only mode that can
disturb a live rig.

**Re-run step 6 after announcing.** If the passive fetch failed but this one
works, the finding is "NFS requires prior announcement" — which would make the
announcer a hard dependency of the Mixxx feature rather than an optional extra,
and is worth knowing before any C++ is written.

## 10. Sharing the Mac's stick with the CDJs

The reverse direction. Plug a rekordbox USB into the Mac and serve it:

```bash
# dbserver only, no privileges needed -- start here
prolinks serve --volume /Volumes/<STICK> --no-nfs --number 5

# the full thing; NFS's portmapper must bind UDP/111, so this needs root
sudo .venv/bin/prolinks serve --volume /Volumes/<STICK> --number 5
```

Then on a CDJ: press **LINK** and look for us in the device list. Expect this
to be the least-finished part of the evening — the server is validated only
against our own client, so a real player is its first genuine test. Useful
things to note when it does not work:

- does the CDJ list us at all? (announcing works, or does not)
- does it connect to the dbserver? (`serve` logs every request it receives)
- what does it ask for first, and do we answer it? (see FINDINGS C11)

Run it alongside `tcpdump` — a capture of a real player rejecting us is worth
more than any amount of guessing.

## 11. Testing the Mixxx feature on the deck  *(phase B)*

The first increment of the C++ port: **players appear in Mixxx's library
sidebar**. Nothing can be browsed or loaded yet — that is the next increment —
so what is under test here is discovery, the thread boundary, the offline/removal
timing, and clean shutdown.

Entirely passive. Mixxx transmits **nothing** on any Pro DJ Link port in this
build, so it cannot contend for a device number or disturb a live rig.

### Deploy

```sh
cd ../mixxx        && ./upload.sh      # build arm64 in Docker, swap /usr/bin/mixxx
cd ../mixxx_config && ./upload.sh      # ~/.mixxx, incl. ShowProLinkLibrary 1
```

`mixxx/upload.sh` reads the Debian release off the deck and builds against it, so
the binary links to the libraries already there. First build is ~3 min; after
that the ccache and build-tree cache mounts make it ~1 min.

`pi_config/upload.sh` **is** needed, once: it runs `prolink-eth0.sh`, which puts
eth0 on an IPv4 link-local address. Without that nothing is discovered at all,
and the way it fails is misleading — Mixxx binds UDP 50000 happily, eth0's RX
counter climbs, and the sidebar says "no players found". CDJs broadcast to
`169.254.255.255`, a *directed subnet broadcast*, so a host with no address in
that subnet receives the frames at the NIC and discards them at the IP layer.
(The UDP/111 sysctl in the same script only matters for serving.)

Confirm before testing anything else:

```sh
ssh trimixxx-pi 'ip -4 -br addr show eth0'      # want 169.254.x.y/16
```

### What to expect

| Action | Expected |
|---|---|
| Open the **Pro DJ Link** item in the sidebar | Status page: "Listening on UDP port 50000", and a table of players |
| Power on a CDJ | It appears as `1 · CDJ-2000nexus` within a few seconds. **Allow ~10 s**: a CDJ tries DHCP about three times before self-assigning and says nothing until then (F8) |
| Pull its Ethernet cable | Goes grey and gains ` (offline)` after **10 s** — five missed keep-alives. The row stays |
| Plug it back in | Label returns to normal, no flicker |
| Leave it unplugged | Row disappears at **60 s** |
| Set `[ProLink],refresh` to 1 | Offline rows go immediately, without waiting out the 60 s |
| Quit Mixxx with a CDJ on the network | Clean exit, no crash. The shutdown-ordering test (R5); it can only fail with a device actually present |

> **Quit from inside Mixxx** — `Ctrl+Q` or File → Exit. `systemctl stop
> getty@tty1.service` does **not** test this: it SIGTERMs the session scope, and
> Mixxx installs no SIGTERM handler, so the process dies with no destructors at
> all. `~ProLinkFeature` never runs and the test silently passes for the wrong
> reason. Tell the two apart by whether the shutdown sequence is in the log:
>
> ```sh
> ssh trimixxx-pi 'grep -c "deleting Library" ~/.mixxx/mixxx.log.1'
> ```
>
> `1` means Qt's teardown ran and the test was real. `0` means it did not.

**Check the Interface column on the status page**: on the deck it must say
`eth0`, not `wlan0`. A device attributed to the wrong interface is the
multi-homing failure (R5), and it is otherwise silent right up until something
opens a socket towards it and every RPC times out with nothing to point at.

The status page is the primary test surface — it re-renders on every change, so
no log reading is needed for any of the table above.

### Logs

**Mixxx does not log to journald.** It is launched from `~/.xinitrc` on the deck,
not as a systemd unit, so `journalctl` shows nothing useful. It writes its own
file:

```
~/.mixxx/mixxx.log          current run
~/.mixxx/mixxx.log.1        previous run  (rotated on every start, up to .10)
```

The rotation matters: after a restart, the run you just did is `mixxx.log.1`.

**The default log level hides everything this feature emits.** Mixxx defaults to
`Warning` (`kLogLevelDefault`, `src/util/logging.h:24`), and discovery logs at
`info` — "listening on UDP 50000", "found 1 · CDJ-2000nexus", "went offline",
"removing". Note that `--developer` does *not* raise the level, despite what its
name suggests. So on the deck, add the flag to the Mixxx line in `~/.xinitrc`:

```sh
ssh trimixxx-pi 'grep mixxx ~/.xinitrc'          # see what is there now
# ...append --log-level info to that line...
```

Then:

```sh
# follow live while power-cycling a CDJ
ssh trimixxx-pi 'tail -f ~/.mixxx/mixxx.log | grep -i prolink'

# pull the whole thing back for a proper look
scp trimixxx-pi:.mixxx/mixxx.log /tmp/deck-mixxx.log

# previous run, e.g. after testing a clean quit
scp trimixxx-pi:.mixxx/mixxx.log.1 /tmp/deck-mixxx-prev.log
```

Expect lines like:

```
ProLinkDiscovery - listening on UDP 50000
ProLinkDiscovery - found 1 · CDJ-2000nexus at 169.254.202.84 on eth0
ProLinkDiscovery - 1 · CDJ-2000nexus went offline
ProLinkDiscovery - removing 1 · CDJ-2000nexus
```

### Corroborating the timeouts with the kernel

Do not take our own timestamps as evidence of our own timing. The Pi's NIC driver
logs link transitions independently, so the two can be diffed:

```sh
ssh trimixxx-pi 'sudo dmesg -T | grep -i "eth0: Link"'
```

A confirmed run looked like this — kernel on the left, Mixxx on the right:

| | | |
|---|---|---|
| `14:36:21 Link is Down` | `14:36:30.949 went offline` | 9.95 s → `kDeviceTimeoutMs` 10 s |
| | `14:37:20.949 removing` | exactly 50.000 s later → 60 s total from the last keep-alive |
| `14:37:30 Link is Up` | `14:37:41.544 found` | 11.5 s — the CDJ's own ~10 s self-assign delay (F8), not ours |

`--log-level debug` additionally reports datagrams that failed to decode, which
is what to reach for if a device on the network never appears — a mixer or a
CDJ-3000 emitting a packet type our schema does not cover would show up there.
Take the flag back off afterwards: debug logging is not free on a Pi mid-set.

### If no players appear

- Is the sidebar item even there? If not, `ShowProLinkLibrary` did not take —
  check `~/.mixxx/mixxx.cfg` on the deck.
- Does the status page say the port could not be bound? Something else holds
  50000.
- Cross-check with the PoC from the Mac on the same switch:
  `prolinks devices --watch`. If that sees the decks and Mixxx does not, the bug
  is ours; if neither does, it is the network.

## 12. The anchor test — pull a database off a real CDJ  *(phase B, step 3)*

The strongest check that exists on the RPC/NFS stack, and it needs no interface:
fetch `export.pdb` over NFS and prove it byte-identical to the same file read off
the physically ejected stick. If this passes, XDR, ONC RPC, portmap, mountd,
NFSv2 `LOOKUP`/`GETATTR`/`READ`, the windowed transfer and the reassembly are all
correct together.

Deploy as in §11, then with a CDJ on the network and a rekordbox USB in it:

1. In Mixxx, open **Developer Tools** (already enabled — `~/.xinitrc` passes
   `--developer`).
2. Find `[ProLink]` → `pull_db` and set it to **1**.
3. Watch the log:

```sh
ssh trimixxx-pi 'tail -f ~/.mixxx/mixxx.log | grep -i "pull_db\|ProLinkNfs"'
```

Expect:

```
pull_db: mounting /C/ on 2 · CDJ-2000nexus 169.254.202.84
pull_db: mounted, mountd 48276 nfsd 2049
pull_db: export.pdb is 1077248 bytes
pull_db OK: 1077248 bytes in 842 reads, 0 short, ... ms, ... KiB/s
pull_db sha1: <digest>
```

Then eject the stick, put it in the Mac, and compare:

```sh
shasum -a 1 /Volumes/<STICK>/PIONEER/rekordbox/export.pdb
```

**The digests must match.** One legitimate exception: a player rewrites its own
bookkeeping into the pdb header as it operates — a play count, a history entry,
landing in the sequence counter at `0x14` (F13). If the two files differ *only*
in bytes `0x10`–`0x18`, that is the deck writing to its own database, not a
transfer error. Anything else is a real bug. Confirm with:

```sh
cmp -l /Volumes/<STICK>/PIONEER/rekordbox/export.pdb /tmp/pulled.pdb | head
```

### If it fails

The log names the stage, which is the point of resolving both ports up front:

| Message | Meaning |
|---|---|
| `no mountd: the portmapper did not answer` | Nothing on UDP/111 at the player, or we are talking to the wrong address |
| `no nfsd: program 100003 is not registered` | Portmapper answered but NFS is not running — try with media inserted |
| `MNT /C/: NFSERR_ACCES` | The export list did not include us. Firmware-dependent; try after announcing |
| `LOOKUP PIONEER: NFSERR_NOENT` | HFS-formatted media puts the tree under `.PIONEER` |
| `timed out` on everything | Almost always the wrong source interface — check the Interface column in §11 says `eth0` |

## 13. Cover art over dbserver  *(phase B — the NFS artwork path replaced)*

Covers used to be fetched over NFS alongside the audio, and it never worked
properly: about one in ten arrived and the rest came back `NFSERR_STALE`,
deterministically the same ones each time. F49 explains why — a real CDJ **never
asks NFS for an image**, and walking `PIONEER/Artwork/000NN` per cover mints four
filehandles a time until the player's table churns and it starts refusing handles
a millisecond old. Artwork now goes over dbserver, by id, where no handle exists
to go stale.

### Power both decks on

This is new and it is load-bearing. Every dbserver request carries a device
number in its descriptor, and the player validates it: 1–4, **belonging to a
device actually on the network**, and not the deck being asked. We do not
announce, so we have no number of our own and have to borrow the *other* deck's.
With one deck on the network there is nobody to borrow from; Mixxx falls back to
the lowest number in range that is not the target's and tries anyway, which is
untested — see the experiment below.

### The test

```sh
ssh trimixxx-pi 'sudo tcpdump -i eth0 -w /tmp/dbserver.pcap "tcp port 1051 or tcp port 12523"' &
```

In Mixxx: **ProLink → the deck → USB → All tracks**, then scroll the whole list.
Expect the Cover column to fill in progressively, over a few seconds, for every
track that has art — not the ~10% it used to.

Stop the capture, then read the log:

```sh
ssh trimixxx-pi 'grep -E "ProLinkDbServer|cover images" ~/.mixxx/mixxx.log'
```

| Line | Means |
|---|---|
| `queued N cover images for <mac>|3 over dbserver` | The prefetch started. N is distinct images, not tracks |
| `connected to <ip> port 1051 as device 2; peer reports device 1` | Handshake done. The two numbers must differ |
| `Introduce rejected (reply 0x4003)` | The borrowed number was refused — see the experiment |
| `no other player to borrow a device number from; claiming N` | Only one deck is up |
| `gave up after 3 dbserver failures` | Three connection attempts failed; nothing further is tried this session |

### The experiment worth running while you are there

Power the second deck **off** and reload the medium (`[ProLink],refresh`, then
re-expand the slot). Mixxx will claim a number that belongs to nothing.

- **Covers still load** → the "must be present on the network" half of the rule
  is wrong, or at least not enforced by 1.44 firmware, and single-deck operation
  is fine. Record it as a finding and simplify `pickRequesterNumber`.
- **`Introduce rejected`** → the rule holds, artwork genuinely needs a second
  player until we announce, and that is an argument for bringing the virtual CDJ
  forward in the plan.

Either answer is worth having; right now we are guessing.

### Reading the capture

```sh
uv run prolinks pcap /tmp/dbserver.pcap
```

Every `0x2003 GetArtwork` should draw exactly one `0x4002 Artwork`, and the blob
sizes should look like JPEGs (tens of kilobytes). A `0x4003` is a refusal; a
request with no reply means the connection died mid-conversation and the log will
say so.

## What to bring back

- `captures/` from the whole session, with `--notes` filled in
- the `export.pdb` and a couple of ANLZ `.DAT`/`.EXT` pairs, for `fixtures/`
- verdicts for E1–E8 appended to `FINDINGS.md`
- a capture of a real CDJ browsing another CDJ over dbserver, so `0x3e03`
  and `0x3100` can be worked out (FINDINGS C11 / O4)
- a capture of a real CDJ trying to browse *us*, whether or not it succeeds
- firmware versions from each unit's `UTILITY` screen
