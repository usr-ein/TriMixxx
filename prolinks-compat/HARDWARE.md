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
.venv/bin/python -m pytest tests/ -q          # 116 should pass
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

## What to bring back

- `captures/` from the whole session, with `--notes` filled in
- the `export.pdb` and a couple of ANLZ `.DAT`/`.EXT` pairs, for `fixtures/`
- verdicts for E1–E8 appended to `FINDINGS.md`
- a capture of a real CDJ browsing another CDJ over dbserver, so `0x3e03`
  and `0x3100` can be worked out (FINDINGS C11 / O4)
- a capture of a real CDJ trying to browse *us*, whether or not it succeeds
- firmware versions from each unit's `UTILITY` screen
