# PCAP capture plan

A session plan for the two CDJ-2000NXS, each on its own NIC of the Mac.

The point of this session is not "get some traffic" — it is to answer specific
open questions that currently block the implementation. Each scenario below
names the question it settles. If time runs short, **S5 and S6 are the two that
matter most**; everything else can be re-run another evening.

---

## 0. Prerequisite: bridge the two NICs

With one CDJ per NIC and no bridge, the two players are on separate L2
segments and **cannot see each other at all** — there is no CDJ↔CDJ traffic to
capture, and the LINK button will find nothing. Bridging the interfaces puts
them on one segment and makes every frame between them transit the Mac.

This is a passive L2 bridge, not a router and not an ARP spoof: the CDJs are
unaware of it, nothing is forged, and no packet is delayed by a forwarding
decision at L3.

On this machine the two NICs are:

| Interface | Hardware | MAC | Deck |
|---|---|---|---|
| `en12` | Dell Universal Dock D6000 | `0c:37:96:38:32:09` | **deck A** |
| `en9` | USB 10/100/1000 LAN | `a0:ce:c8:e2:26:de` | **deck B** |

### Capture on the **members**, not on `bridge1`

`bridge1` is the wrong tap, and the reason is easy to miss. A BSD bridge
**floods** broadcast frames — so keep-alives, hellos and claim packets all
appear on the bridge interface, and a capture there looks perfectly healthy.
But once the bridge has learned the members' MAC addresses it forwards
**unicast** frames member-to-member directly, and those never reach the bridge
interface's BPF tap.

Everything interesting is unicast: TCP 12523 and 1051 (dbserver), and the whole
NFS conversation. So a `bridge1` capture of a LINK browse records the two decks'
keep-alives and *nothing of the browse itself*.

This was learned the expensive way: an S5 capture on `bridge1` came back with
210 keep-alives and no TCP at all, during a browse that demonstrably worked on
the deck's own display.

> **The trap:** verifying the tap by checking that both decks are visible only
> proves that **broadcast** reaches you. It says nothing about unicast. In every
> capture taken on `bridge1`, every unicast frame recorded had the Mac itself as
> an endpoint — those are delivered locally rather than bridge-forwarded, which
> is why the NFS transfers were captured fine and gave false confidence.

**Preferred:** capture both members at once, which macOS `tcpdump` supports via
the `pktap` pseudo-device:

```bash
./tools/capture.sh S05-link-browse pktap,en12,en9 "..."
```

**Fallback**, if `pktap` is unavailable: capture on the member belonging to the
deck that *initiates* the activity — it sees everything to and from that deck,
in both directions.

```bash
./tools/capture.sh S05-link-browse en12 "deck A browses deck B"
```

**Verify with unicast, not broadcast.** The check that matters is that a LINK
browse produces TCP on 12523 and 1051. If a capture of a browse contains only
UDP 50000, the tap is wrong no matter how many decks are visible.

**Use `bridge1`, not `bridge0`.** `bridge0` already exists on macOS as the
Thunderbolt bridge (members en1/en2/en3) and must be left alone.

Each NIC also has a network service in System Settings. Leave those enabled and
`configd` will keep re-applying DHCP to the bridge members, fighting the
bridge — so turn them off first.

```bash
# 1. Stop configd from managing the members
sudo networksetup -setnetworkserviceenabled "USB 10/100/1000 LAN 2" off
sudo networksetup -setnetworkserviceenabled "Dell Universal Dock D6000" off
sudo ipconfig set en9 NONE
sudo ipconfig set en12 NONE

# 2. Build the bridge
sudo ifconfig bridge1 create
sudo ifconfig bridge1 addm en9 addm en12
sudo ifconfig bridge1 up

# 3. Give the Mac an address on the segment, so prolinks can participate too.
#    169.254/16 is the link-local range the CDJs self-assign into when there is
#    no DHCP server. A static address is instant and deterministic; `DHCP`
#    also works but spends ~20 s timing out before falling back to IPv4LL.
sudo ipconfig set bridge1 MANUAL 169.254.99.100 255.255.0.0
```

> `ipconfig set` takes only `BOOTP, MANUAL, DHCP, INFORM, NONE` for IPv4 —
> `AUTOMATIC-V4` is a `networksetup` method name and is rejected here.

**With both decks off, the address will not appear yet, and that is correct.**
`ifconfig bridge1` will show the members but no `inet` line, because
IPConfiguration stores the configuration and refuses to apply it while the
interface has no carrier — both members are down until a powered CDJ is
plugged in. `ipconfig getsummary bridge1` shows exactly that:

```
Active : FALSE
LastFailureStatus : media inactive
ManualAddress : 169.254.99.100
```

It goes active on its own once a deck powers on. **This does not block S1**:
`tcpdump` captures at layer 2 and needs no address, so the cold-boot capture
can start with everything still off. The address only matters for running
`prolinks`, which happens after the decks are up regardless.

**Use `ipconfig set`, not `ifconfig inet`.** They are not interchangeable for
link-local addresses, and the difference is invisible until nothing works:

```bash
sudo ipconfig set en9 MANUAL 169.254.99.100 255.255.0.0     # works
sudo ifconfig en9 inet 169.254.99.100 netmask 255.255.0.0   # looks identical, does not work
```

`ifconfig` writes the address straight into the kernel; `ipconfig` goes through
IPConfiguration, which performs the RFC 3927 link-local ARP probe and
announcement that makes a 169.254 address actually usable. After `ifconfig`,
`ifconfig en9 | grep inet` shows exactly what you asked for, the route looks
right — and every ARP for a peer stays `(incomplete)`, so every unicast fails
with "Host is down" while broadcast reception keeps working perfectly. That
combination is deeply misleading: you can *see* the decks and cannot *reach*
them.

Diagnose with `arp -an | grep 169.254`. An `(incomplete)` entry for a deck
means the address is not properly announced, not that the deck is absent.

Note also that **a CDJ does not answer ICMP**, so `ping` is useless as a
reachability test. Use `prolinks rpcinfo <ip>` instead -- it is one RPC round
trip and it exercises the path that matters.

Once the decks are up, confirm neither of them picked `169.254.99.100` — the
address is chosen statically here rather than ARP-probed, so a collision is
possible if unlikely. `prolinks devices` shows their addresses; pick another
host part if it clashes.

Confirm the bridge came up with both members:

```bash
ifconfig bridge1 | grep -E 'member|status'
```

**Teardown, at the end of the night:**

```bash
sudo ifconfig bridge1 destroy
sudo networksetup -setnetworkserviceenabled "USB 10/100/1000 LAN 2" on
sudo networksetup -setnetworkserviceenabled "Dell Universal Dock D6000" on
```

### Verify the tap before trusting any capture

The single most expensive failure mode tonight is capturing for two hours and
discovering afterwards that only one deck was visible. So check first, with
both players on:

```bash
sudo tcpdump -i bridge1 -n -c 20 udp port 50000
```

You must see keep-alives sourced from **both** CDJ IPs. Do not proceed until
two distinct sources show up.

If `bridge1` yields nothing at all, some BSD bridge implementations do not feed
BPF on the bridge interface itself. Fall back to a member — either one sees the
whole conversation, since every frame between the decks is forwarded across the
bridge:

```bash
sudo tcpdump -i en12 -n -c 20 udp port 50000
```

If a member shows only *one* deck, the likely culprit is the **D6000 dock**:
bridging needs the member NIC to support promiscuous mode, and DisplayLink dock
NICs are the more temperamental of the two here. Diagnose by swapping which
deck is on which port — if the visible deck follows `en9`, the dock is the
problem, and the fallback is a second plain USB dongle or a switch with port
mirroring.

```bash
prolinks devices      # should list both players
```

---

## 1. Capture mechanics

Use `tools/capture.sh` (below) — it sets the flags that matter and creates the
notes skeleton, so nothing is forgotten at 1 a.m.

The flags that actually matter:

- `-s 0` — **full packets, no truncation.** A snaplen that clips payloads
  silently destroys exactly the dbserver and NFS content we are after.
- `-n` — no DNS lookups, which would otherwise inject traffic of their own.
- **No BPF filter.** Capture everything. A filter that looks obviously correct
  is how you discover afterwards that the interesting packet was on a port
  nobody thought to include. These captures are a few MB; disk is not the
  constraint.

One directory per scenario. A capture without its notes is close to worthless
in a month — which IP was which physical unit, and what was pressed when, are
not recoverable from the bytes.

---

## 2. Physical setup before powering anything on

- Two USB sticks, both prepared by rekordbox, **with different content** so it
  is unambiguous which library came from which deck. Label them A and B.
- Deck A's `PLAYER No.` is set **manually to 1** (confirmed in S1, and the
  basis of FINDINGS C13). Note deck B's setting before you start.
- Note each unit's **firmware version** from its UTILITY screen — captures are
  only comparable against other captures of the same firmware.
- The deck↔NIC mapping is fixed above (**deck A on `en12`, deck B on `en9`**).
  Record each deck's **IP** in `NOTES.md` as soon as it boots: everything
  downstream is IP-based, and mapping IP→deck after the fact is guesswork.

---

## 3. Scenarios

Ordered so that each builds on the last, and so the highest-value captures
happen while you and the gear are still fresh.

### S1 — Cold boot, one deck alone
**Answers:** the full announcement and device-number claim chain with no
contention. We have only 4 `ClaimIp` packets in total from the reference
captures, and the claim sequence is what our announcer has to imitate.

Start the capture **before** applying power, so the very first packet is caught.

1. Both decks off. Start capture.
2. Power on **deck A only**. Wait until it has fully booted and settled (~60 s).
3. Stop capture.

### S2 — Second deck joins
**Answers:** does the incumbent defend its number? What does a real contended
claim look like?

1. Deck A already up and settled. Start capture.
2. Power on **deck B**. Wait ~60 s.
3. Stop capture.

### S2b — Deliberate device-number collision  *(high value)*
**Answers:** the type-`0x08` conflict packet, which **has never appeared in any
capture we have** — our only reference for it is a hand-written libcdj fixture,
so our encoder for it is effectively unverified.

Deck A is already set manually to player **1**, so this is a one-setting change.

1. Power both decks off.
2. In UTILITY on **deck B**, set `PLAYER No.` manually to **1** as well.
3. Start capture. Power on deck A, let it settle, then power on deck B.
4. Watch deck B's display — it should complain or renumber. Stop capture.
5. **Put deck B back to its own number afterwards.**

### S1b / S2c — isolate the stage-3 repeat count  *(new, cheap, do while booting anyway)*
**Answers:** FINDINGS C13. Deck A (manual, booting alone) sent **three** stage-3
packets; deck B (manual, joining an occupied network) sent **one**. Two
variables are confounded: peer presence, and the number being claimed (1 vs 2).

- **S1b** — power everything off, then boot **deck B alone**. Three packets ⇒
  peer presence is the variable. One ⇒ the number is.
- **S2c** — with deck B up and settled, boot **deck A** into it. The mirror
  image, and it settles the remaining ambiguity either way.

Two extra power cycles, no reconfiguration, and it converts a "depends on
something" into a rule we can implement.

### S3 — Idle steady state, 3 minutes
**Answers:** keep-alive cadence over time, and the still-unexplained variation
in keep-alive byte `0x25` (FINDINGS C4 / O3) — we have seen a single deck send
both `01` and `02` and have no idea what selects it. Three minutes of undisturbed
keep-alives is enough to correlate it against anything else in the session.

Both decks up, no media, nobody touching anything. Capture 180 s.

### S4 — Media insert and eject
**Answers:** whether the NFS programs register with the portmapper only when
media is mounted. This is a branch in experiment E4's decision tree: if NFS
only appears with a stick inserted, the Mixxx feature must gate its probing on
media state rather than probing on discovery.

1. Both decks up, no media. Start capture.
2. Insert stick A into deck A. Wait 20 s.
3. Insert stick B into deck B. Wait 20 s.
4. Eject stick A. Wait 20 s. Re-insert.
5. Stop capture.

Immediately after, with media still in, run the probe from the Mac:

```bash
prolinks rpcinfo <IP-A> --notes "S4: USB inserted, idle"
```

### S4b — media insert/eject, **on the correct tap**  *(re-run of S04)*
**Answers:** whether a player advertises media changes, and where. S04 was
captured on `bridge1` and so could not see the unicast 50002 status traffic that
S05/S06 later showed exists in quantity (1440 and 2020 packets). Its apparent
"media events are invisible" result was an artefact and has been retracted.

Same steps as S4, but `pktap,en12,en9`, and with a specific question: does
anything appear on 50002 when a stick goes in or out, and does its content
change? This decides whether the Mixxx feature can learn media presence by
listening or must poll MOUNT `EXPORT`.

Worth doing *before* trusting any other negative result from the S04 window.

### S5 — LINK browse, deck A → deck B  *(the most important capture)*
**Answers:** FINDINGS C11 / O4 — the three undocumented message types
(`0x3e03`, `0x4b02`, `0x3100`) that appear in an ordinary browse. **`0x3e03` is
the first thing a player sends after `Introduce`**, and our server currently
answers it with an error, which is the most likely reason a real CDJ would
refuse to browse us. Nothing else on the list unblocks as much.

Also answers: does a browsing player touch NFS, or dbserver only?

Go slowly and deliberately, pausing between actions so they are separable in
the timeline. Note the wall-clock time of each step.

1. Both decks up, both sticks inserted, both libraries loaded. Start capture.
2. On deck A press **LINK**. Wait 10 s.
3. Select deck B's USB. Wait 10 s.
4. Browse into a **playlist**. Wait 10 s.
5. Scroll through the track list, far enough to force a second page (>64
   entries if the library allows). Wait 10 s.
6. Select a track so its **artwork and waveform** load. Wait 10 s.
7. Back out to the root menu. Wait 10 s.
8. Stop capture.

### S6 — Load and play a track off the other deck  *(the other important one)*
**Answers:** open question **O1** — how the audio itself travels when a player
loads a track from another player's USB. dbserver serves metadata, not audio;
NFS is the only plausible carrier but no published source states it. This gates
whether TriMiXxX can be *played from* at all, not merely browsed.

1. Continuing from S5. Start a fresh capture.
2. On deck A, **load** a track from deck B's USB. Wait for it to finish loading.
3. **Play** it for ~30 s.
4. Cue, scratch, jump to a hot cue.
5. Stop capture.

Expect this to be a large file if audio really does cross the wire — which is
itself the answer.

### S7 — Playback state and beat traffic
**Answers:** the UDP 50002 status packet layout and 50001 beat packets, for the
decode-only parts of the PoC.

1. Both decks playing their own local media. Start capture.
2. Play / pause / cue on deck A. Adjust the tempo fader through its range.
3. Set deck A as sync master; sync deck B to it.
4. Stop capture after ~60 s.

### S8 — Yank the media mid-browse
**Answers:** experiment E8 — what a stale NFS filehandle looks like from the
wire, and how a player reports a medium disappearing underneath it. Our client
assumes `NFSERR_STALE`; this checks that.

1. Deck A browsing deck B's USB (as in S5). Start capture.
2. **Eject the stick from deck B** while deck A is mid-browse.
3. Watch what deck A displays. Wait 20 s.
4. Re-insert. Wait 20 s. Stop capture.

### S9 — Our tools, passive then announcing
**Answers:** experiment **E1** — does a CDJ serve us files over NFS when we
have never announced ourselves? And E4/E2/E3 in passing.

Run each with `--notes`, and keep the JSONL journals alongside the pcap.

```bash
# Passive: --assert-passive fails the run if a single DJ-Link datagram escapes
prolinks rpcinfo  <IP-A> --assert-passive --notes "S9 passive"
prolinks exports  <IP-A> --assert-passive --notes "S9 passive"
prolinks pull-db  <IP-A> --slot usb --assert-passive -o /tmp/A-passive.pdb

# The anchor test: same file, read off the stick directly
shasum -a 256 /tmp/A-passive.pdb /Volumes/<STICK-A>/PIONEER/rekordbox/export.pdb

# Now announce, and repeat. If the passive attempt failed and this one works,
# that is a first-class finding: the announcer becomes a hard dependency.
prolinks announce --number 7 --duration 120 &
sleep 10
prolinks rpcinfo <IP-A> --notes "S9 after announcing"
```

### S10 — A real CDJ trying to browse *us*
**Answers:** whether the dbserver we wrote is remotely acceptable to real
hardware. It has only ever been driven by our own client, so this is its first
genuine test, and a capture of it *failing* is worth as much as one of it
working — the useful datum is which request it stalls on.

1. Plug stick A into the **Mac**. Start capture.
2. `sudo .venv/bin/prolinks serve --volume /Volumes/<STICK-A> --number 5`
   (root is needed only for the NFS portmapper on UDP/111; `--no-nfs` drops
   that requirement if the bind fails.)
3. On deck A, press **LINK** and look for us in the device list.
4. Try to select us. Try to browse. Note exactly what the display says.
5. Stop capture. Keep `serve`'s own request log — it prints what it received.

### S24 — Does a deck need our portmapper?  *(experiment E9)*

**Answers:** whether serving requires binding the privileged UDP/111. A deck
does call portmap `GETPORT` for mountd and nfsd (F24) — but a real player
answers **48276** and **2049** on every device we have seen (F6), stable enough
to be compiled-in defaults it may fall back to when `GETPORT` goes unanswered.

If it falls back, Mixxx never needs a privileged port, on any platform, and the
whole privileged-helper design in `research/10` §B5 can be deleted. That makes
this the cheapest high-value capture left.

**Setup is the simplest of any scenario here: one deck, one cable, no bridge.**
The bridge exists to watch two decks talk to each other; here the Mac is one of
the two endpoints, so everything of interest crosses the dongle.

```
Mac ── USB-Ethernet dongle ── (cable, direct or via the switch) ── deck B
```

Two runs, differing in **one** variable. Run the control first; without it a
failure in run B is unattributable.

**S24a — control.** The known-good configuration, exactly as S17/S23:

```bash
tools/capture.sh S24a-e9-control en9 "control: portmap on 111, as sudo"
sudo .venv/bin/prolinks -v serve --volume /Volumes/<STICK> --iface en9 --number 3
```

**`--number` must be 1–4** (F45). At 5 the deck accepts us and trades status
with us all session, then never sends a media query, so it never lists us —
which reads exactly like an announce bug and is not one. `serve` now defaults
to 3 and warns if you go outside the range.

**S24b — the experiment.** Same stick, same deck, same everything — but
portmap is moved off 111 so nothing answers there, mountd and nfsd sit on the
numbers a real player uses, and **there is no `sudo`**:

```bash
tools/capture.sh S24b-e9-noportmap en9 "E9: portmap off 111, mountd 48276, nfsd 2049, no root"
.venv/bin/prolinks -v serve --volume /Volumes/<STICK> --iface en9 --number 3 \
    --portmap-port 11111 --mountd-port 48276 --nfsd-port 2049
```

In **both** runs, on the deck: press **LINK**, select us, browse — and then
**load a track and play it**.

> **Browsing proves nothing.** dbserver runs over TCP and never touches
> portmap, so the deck will list us and open every menu identically in both
> runs. Only a track load exercises NFS. The verdict is playback, not the menu.

**Reading the result.** `serve` prints an RPC tally at exit:

| S24b tally | Verdict |
|---|---|
| `mountd:MNT`, `nfsd:LOOKUP`, `nfsd:READ` present, track plays | **Pass** — portmap is optional; delete §B5 |
| `portmap:*` absent and no `mountd:MNT` | **Fail** — the deck gave up when `GETPORT` went unanswered |

The two failure modes look identical on the deck's screen ("LOADING…" then an
error), so read the tally, not the display. If S24a also fails, the rig is at
fault rather than the hypothesis — fix that before drawing any conclusion.

---

## 4. Analysis pass, same evening if possible

```bash
for d in captures/S*/; do
  echo "=== $d"
  prolinks pcap "$d"/run.pcap
done
```

A capture that shows `round-trip: N/N byte-exact` is one the codecs already
understand. Any mismatch is a finding — that is precisely how the corrections
in `FINDINGS.md` were produced.

Then append verdicts for E1–E8 to `FINDINGS.md` while the session is fresh.

---

## 5. What to bring back

- `captures/` in full, with every `NOTES.md` filled in
- the `export.pdb` from each stick, plus 3–5 ANLZ `.DAT`/`.EXT` pairs, for
  `fixtures/`
- firmware versions from both units
- the IP↔deck mapping
- for S10: what the CDJ's screen actually said

---

## Appendix: `tools/capture.sh`

```bash
#!/usr/bin/env bash
# Usage: tools/capture.sh S05-link-browse bridge1 "deck A browses deck B's USB"
set -euo pipefail
name="${1:?scenario name}"; iface="${2:?interface}"; shift 2
dir="captures/$name"; mkdir -p "$dir"

{
  echo "# $name"
  echo
  echo "- started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "- capture interface: $iface"
  echo "- description: $*"
  echo
  echo "## Hardware state"
  echo "- deck A: ip=?  firmware=?  slot=?  media=?"
  echo "- deck B: ip=?  firmware=?  slot=?  media=?"
  echo
  echo "## Timeline"
  echo "- 0:00 capture started"
} > "$dir/NOTES.md"

echo "tcpdump -i $iface -s 0 -n -w $dir/run.pcap" > "$dir/cmd.txt"
echo "capturing to $dir/run.pcap -- Ctrl-C to stop"
sudo tcpdump -i "$iface" -s 0 -n -w "$dir/run.pcap"
```
