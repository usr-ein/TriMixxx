# 02 — Device Discovery, Announcement, Keep-Alive & Device-Number Claiming (UDP 50000)

> **⚠ Pre-hardware document.** Written from published reverse-engineering
> literature before any capture from real CDJs existed. Much of it has since
> been confirmed, and a good deal corrected, by testing against two
> CDJ-2000NXS. **`docs/PROTOCOL.md` is the current specification; where this
> document disagrees with it, this document is wrong.** `docs/FINDINGS.md`
> records each correction with its evidence.

Scope: how a Pioneer Pro DJ Link / "ProLink" / "DJ Link" device joins the network, announces
itself, claims a player/device number (defending it against collisions), and then maintains
presence with a periodic keep-alive. All of this happens on **UDP port 50000**, broadcast to the
local subnet broadcast address. This is the protocol surface the project must implement to
impersonate a CDJ-2000nexus.

Sources cross-referenced:
- `dysentery/doc/.../startup.adoc` — primary, full handshake (cited `startup.adoc`)
- `dysentery/doc/.../vcdj.adoc` — virtual CDJ / device-number tradeoffs (cited `vcdj.adoc`)
- `libcdj/doc/autoip.md`, `libcdj/doc/id-use-reply.md` (cited `autoip.md`, `id-use-reply.md`)
- `python-prodj-link/prodj/network/packets.py`, `core/vcdj.py` (cited `packets.py`, `vcdj.py`)
- `prolink-connect/src/virtualcdj/index.ts`, `devices/index.ts`, `devices/utils.ts`,
  `constants.ts`, `utils/index.ts` (cited `prolink-connect`)

Legend: **(confirmed)** = stated in docs or implemented identically across ≥2 reference codebases;
**(inferred)** = derived from one source or my reading of the structures.

> Numbers in `code font` are **hexadecimal** (byte offsets and byte values). Plain prose numbers
> (300 ms, 1.5 s, ports) are decimal. This mirrors dysentery's convention.

---

## 0. Constants & wire facts

| Fact | Value | Source |
|---|---|---|
| Magic header (10 bytes ASCII) | `Qspt1WmJOL` = `51 73 70 74 31 57 6d 4a 4f 4c` | `packets.py` `UdpMagic`, `prolink-connect` `PROLINK_HEADER`, `startup_shared.edn` (confirmed) |
| Discovery/announce/keepalive port | **UDP 50000** | all sources (confirmed) |
| Beat port (not covered here) | UDP 50001 | `packets.py`, `prolink-connect` |
| Status port (vCDJ binds here to receive) | UDP 50002 | `vcdj.adoc`, `prolink-connect` |
| Destination during discovery/keepalive | subnet **broadcast** address (e.g. `192.168.1.255` / `169.254.255.255`) | `startup.adoc`, `vcdj.py`, `id-use-reply.md` (confirmed) |
| Some handshake replies are **unicast** to port 50000 of the target | see §1.3, §1.4 | `startup.adoc`, `id-use-reply.md` (confirmed) |
| Source port | sender's own 50000 (you bind 50000 and send from it; needed to *receive* conflict/assignment unicasts) | `id-use-reply.md` ("software players should bind and listen to their local IP port 50000") (confirmed) |
| Keep-alive cadence | ~ every **1.5 s** (1500 ms) | `startup.adoc`, `vcdj.py` `packet_interval=1.5`, `prolink-connect` `ANNOUNCE_INTERVAL=1500` (confirmed) |
> **Corrected on hardware — C12.** A CDJ-2000NXS sends every **2.0026 s**, a tight hardware timer. All four cited sources are either the interval a reference *tool* chose or loose prose; nobody had measured hardware. The 10 s timeout is therefore 5 missed keep-alives, not 6-7.
| Discovery-phase cadence | ~ every **300 ms** | `startup.adoc` (confirmed) |
| IP autoconfig when no DHCP | RFC 3927 link-local `169.254/16` (CDJs also accept DHCP) | `autoip.md` (confirmed) |

### Common header (every UDP-50000 packet)

The first 13 bytes are identical in structure across all discovery/keepalive packet kinds:

| Offset | Len | Field | Notes |
|---|---|---|---|
| `00`–`09` | 10 | Magic | `51 73 70 74 31 57 6d 4a 4f 4c` ("Qspt1WmJOL") |
| `0a` | 1 | **Packet type** (`kind`) | The discriminator. Bold byte in dysentery diagrams. See §0.1 |
| `0b` | 1 | Subtype / structure byte | Normally `00`. `01` marks a *directed reply* during mixer-assigned handshake (§1.4). Maps to dysentery `subtype`. |
| `0c`–`1f` | 20 | **Device name** | ASCII, NUL-padded. (4-byte box + 16-byte box in EDN = 20 total.) |
| `20` | 1 | Constant `01` | "always 1 in every kind of packet" (`startup.adoc`); `packets.py` `u1 = Const(1)` |
| `21` | 1 | **Proto/device-kind byte** | varies; see §0.2. (`packets.py` `device_type`.) |
| `22` | 1 | Padding `00` | (`packets.py` `Padding(1)`) |
| `23` | 1 | Subtype value (`stype`) | pairs with type; see §0.1 |
| `24`+ | … | type-specific payload | per §1 |

> Note on two different offset conventions:
> dysentery's `draw-packet-header` draws magic(10) + type(`0a`) + subtype(`0b`), then the 20-byte
> name, so in dysentery diagrams the name occupies `0c`–`1f`, the `01` constant is at `20`, and the
> *second* structure byte (the `02`/`stype`) is at `21`. `packets.py` models the same bytes:
> `type`@`0a`, Padding@`0b`, name@`0c`–`1f`, `u1=01`@`20`, `device_type`@`21`, Padding@`22`,
> `subtype`@`23`. **The offsets in §1 below use the dysentery byte numbering** (which is what the
> `(startup.adoc)` citations reference). Where prolink-connect's hand-rolled offsets differ
> (it places fields one byte earlier in a couple of spots), I note it.

### 0.1 Packet TYPE byte (`0a`) — the discriminator (confirmed)

From `packets.py` `KeepAlivePacketType` and `startup.adoc`. Each type pairs with a fixed subtype
byte and a fixed total length:

| TYPE `0a` | Name (packets.py) | Purpose | Subtype `stype` | Pkt len |
|---|---|---|---|---|
| `0a` | `type_hello` | Initial "I'm here" announcement | `25` | `25` (CDJ-3000 variant `26`, see §1.6) |
| `00` | `type_mac` | Stage-1 number claim: announce MAC, iter 1→3 | `2c` | `2c` |
| `02` | `type_ip` | Stage-2 number claim: announce IP+MAC, propose number _D_ | `32` | `32` |
| `04` | `type_number` | Stage-3 / final number claim: assert _D_, iter 1→3 | `26` | `2a` |
| `06` | `type_status` | **Keep-alive** (steady state) | `36` | `36` |
| `08` | `type_change` | **Channel conflict** ("that number is mine") | `29` | `29` |
| `01` | (mixer→player) | Mixer "I will assign you a number" intention | `2f`* | `2f` |
| `03` | (mixer→player) | Mixer "use device number _D_" assignment | `27`* | `27` |
| `05` | (mixer→player) | Mixer "assignment finished" | `26`* | `26` |

\* The `01`/`03`/`05` mixer-side types are only in `startup.adoc` (channel-specific port flow);
`packets.py` does not model them. The subtype/len values for them are inferred from the stated
packet sizes in `startup.adoc`.

> Mnemonic for the auto-assign chain: **hello(`0a`) → mac(`00`) → ip(`02`) → number(`04`) →
> keepalive(`06`)**, with **conflict(`08`)** as the interrupt. Note the type byte does **not**
> monotonically increase; `00` and `02` come after `0a`.

### 0.2 The `21` proto/device byte (confirmed, important for impersonation)

This single byte at offset `21` (dysentery numbering; = `device_type` in `packets.py`) distinguishes
device families and, critically, **CDJ-3000-compatibility variants**:

| Value `21` | Meaning | Source |
|---|---|---|
| `01` | DJM / mixer | `packets.py` `DeviceType.djm=1`; mixer hello ends in `02` payload |
| `02` | CDJ (classic / nexus) | `packets.py` `DeviceType.cdj=2`; CDJ hello ends in `01` payload |
| `03` | rekordbox **and CDJ-3000** | `packets.py` comment "also used by cdj-3000" |

For startup.adoc's CDJ-3000-compatible packets, the bolded byte at `21` changes (e.g. `04` in the
hello, `03` in the claim packets) — see §1.6. **To impersonate a CDJ-2000NXS use `02`.**

---

## 1. Full startup negotiation on UDP 50000

Two device roles initiate: **mixer** and **CDJ**. They are byte-for-byte nearly identical; the
differences are (a) the `21` device byte and (b) a trailing payload byte (`02` for mixer, `01` for
CDJ). A CDJ's path also forks on whether it is plugged into a *channel-specific* mixer port. (`startup.adoc`)

### 1.0 Timeline overview (auto-assign on a generic port) (confirmed)

```
t=0      ──►  3× type 0x0a  "hello"            broadcast, ~300ms apart
t≈0.9s   ──►  3× type 0x00  stage-1 (MAC)      broadcast, N=01,02,03
t≈1.8s   ──►  3× type 0x02  stage-2 (IP+propose D)   broadcast, N=01,02,03, a=auto/manual
t≈2.7s   ──►  3× type 0x04  stage-3 (assert D)  broadcast, N=01,02,03
              (if D already taken, owner unicasts type 0x08 conflict → pick new D, restart claim)
t≈3.6s   ──►  type 0x06 keep-alive             broadcast every ~1.5s forever
```

When set to a **specific** (manual) number, the device sends only **one** stage-3 packet (N=01)
then proceeds to keep-alive. (`startup.adoc`)

### 1.1 Initial announcement — TYPE `0a` "hello" (confirmed)

Broadcast to 50000, ~300 ms apart, sent ~3 times. Data length `25` bytes.

| Offset | Len | Field | Value |
|---|---|---|---|
| `00`–`09` | 10 | Magic | `Qspt1WmJOL` |
| `0a` | 1 | TYPE | `0a` |
| `0b` | 1 | subtype | `00` |
| `0c`–`1f` | 20 | Device name | ASCII NUL-padded |
| `20` | 1 | const | `01` |
| `21` | 1 | device byte | `02` (CDJ) / `01` (mixer) |
| `22` | 1 | pad | `00` |
| `23` | 1 | stype | `25` |
| `24` | 1 | payload | **`01` for CDJ, `02` for mixer** (`u2`; djm900nxs sends `03`) |

`packets.py`: `type_hello` payload is one byte `u2` (default `01`). The only diff between mixer and
CDJ hello is this last byte. (`startup.adoc` lines 137, 35; `packets.py` lines 70-72)

### 1.2 Stage-1 number claim — TYPE `00` (announce MAC) (confirmed)

Broadcast, ~300 ms apart, 3 times. Length `2c`. Not yet claiming a number — just publishes MAC and
iterates a counter _N_. (Possibly used by mixer to tell port-wired CDJs their channel — `startup.adoc`.)

| Offset | Len | Field | Value |
|---|---|---|---|
| `00`–`23` | — | common header | TYPE=`00`, stype=`2c` |
| `24` | 1 | **N** (iteration) | `01`, `02`, `03` across the three sends |
| `25` | 1 | flags / device | `01` for CDJ, `02` for mixer (`packets.py` `flags`, default `1`) |
| `26`–`2b` | 6 | **MAC address** | sender's NIC MAC |

`packets.py` `type_mac`: `iteration` @ `24`, `flags` @ `25`, `mac_addr` @ `26`–`2b`. (`startup.adoc` 161; `packets.py` 76-81)

### 1.3 Stage-2 number claim — TYPE `02` (propose number + publish IP) (confirmed)

Broadcast, ~300 ms apart, 3 times. Length `32`. This is what libcdj/Pioneer call an
**IdUseRequest** (`id-use-reply.md`). The device now proposes a device number _D_.

| Offset | Len | Field | Value |
|---|---|---|---|
| `00`–`23` | — | common header | TYPE=`02`, stype=`32` |
| `24`–`27` | 4 | **IP address** | sender's IP |
| `28`–`2d` | 6 | **MAC address** | sender's MAC |
| `2e` | 1 | **D** (proposed device number) | e.g. `02`,`03`; `00` when asking the mixer to assign (§1.4) |
| `2f` | 1 | **N** (iteration) | `01`,`02`,`03` |
| `30` | 1 | const | `01` |
| `31` | 1 | **a** (assignment mode) | `01` = auto-assign, `02` = claiming a specific number |

`packets.py` `type_ip`: `ip_addr`@`24`–`27`, `mac_addr`@`28`–`2d`, `player_number`(D)@`2e`,
`iteration`(N)@`2f`, `flags`@`30`, `player_number_assignment`(a)@`31` (`auto=1`,`manual=2`).
(`startup.adoc` 180-198; `packets.py` 82-89; `id-use-reply.md`)

**Defending against this packet (collision detection):** if another device already owns _D_, it
**unicasts** a TYPE `03` IdUseReply back on port 50000 (see §1.4 / §1.5 for the conflict variant).
`id-use-reply.md` shows a real `XDJ-1000` capture: it sends the type-`02` request with
`player_id=5`; an existing player 04 replies and the booter picks a different ID on its next
broadcast.

### 1.4 Stage-3 / final number claim — TYPE `04` (assert D) (confirmed)

Broadcast, ~300 ms apart. Length `2a`. Auto-assign → 3 packets (N=01,02,03); manual → 1 packet (N=01).

| Offset | Len | Field | Value |
|---|---|---|---|
| `00`–`23` | — | common header | TYPE=`04`, stype=`26` |
| `24` | 1 | **D** (claimed device number) | final number |
| `25` | 1 | **N** (iteration) | `01`(`02`,`03`) |

`packets.py` `type_number`: `proposed_player_number`(D)@`24`, `iteration`(N)@`25`. After the last
of these, the device transitions to keep-alive. (`startup.adoc` 200-216; `packets.py` 72-75)

### 1.5 Channel conflict — TYPE `08` (defend an owned number) (confirmed)

When a *new* device tries to claim a number already in use, the **existing owner** sends a `29`-byte
packet directly (unicast) to port 50000 of the newcomer. On receipt, the newcomer abandons _D_ and
picks another. (`startup.adoc` 420-438)

| Offset | Len | Field | Value |
|---|---|---|---|
| `00`–`23` | — | common header | TYPE=`08`, stype=`29` |
| `24` | 1 | **D** | the contested device number the owner is defending |
| `25`–`28` | 4 | **IP address** | owner's IP |

`packets.py` `type_change`: `old_player_number`@`24`, `ip_addr`@`25`–`28`. (Despite the name,
this is the conflict/defense packet.)

> Implementation duty: to be a well-behaved peer, **you must both (a) listen for type `08` while
> claiming and back off, and (b) emit type `08` to defend your own number** if another device's
> type `02`/`04` proposes a number you hold. `id-use-reply.md`: "software players should bind and
> listen to their local IP port 50000" precisely to receive these unicast replies.

> **XDJ-XZ / Opus-Quad caveat (confirmed, `startup.adoc` 440-456):** when acting as mixer they do
> **not** send conflict packets to defend their own numbers (1/2 for decks, 33/`21` for mixer), and
> on the laptop port they will tell you to use *any* number including 0/1/2. So "watch the network
> before claiming" is mandatory; you cannot rely on conflict packets alone.

### 1.6 CDJ-3000-compatible variants (confirmed, `startup.adoc` 331-418)

To start up / coexist where a CDJ-3000 uses device number 5 or 6, the packets differ **only** at a
few bytes. Same overall flow:

- **Hello (`0a`)**: one byte longer (`26` instead of `25`); byte `21` = `04`; payload `[01 64]`.
- **Stage-1 (`00`)**: byte `21` = `03`.
- **Stage-2 (`02`)**: byte `21` = `03`.
- **Stage-3 (`04`)**: byte `21` = `03`.
- **Keep-alive (`06`)**: byte `35` final value = `64` instead of `01` (see §3). "Having the wrong
  value there can cause CDJ-3000s set to player 5/6 to repeatedly kick themselves off the network."

`packets.py` reflects this in the keep-alive default `u4 = 0x64` ("0x64 for cdj-3000") and
`device_count` default `2` "for cdj-3000 compatibility".

### 1.7 Channel-specific (mixer-assigned) port flow (confirmed, `startup.adoc` 245-329)

If the CDJ is plugged into a mixer Ethernet port bound to a specific channel, the mixer overrides
self-assignment:

1. CDJ sends its 3 hellos (`0a`) then **one** stage-1 (`00`) packet.
2. Mixer **unicasts** TYPE `01` "assignment intention" (`2f` bytes) to the CDJ's port 50000. Layout:
   header(TYPE=`01`) + IP(`24`–`27`) + MAC(`28`–`2d`) + const `01`. (addresses are the *mixer's*.)
3. CDJ **unicasts** back a TYPE `02` variant (`32` bytes) — same as §1.3 but with **`0b`=`01`**
   (directed-reply marker) and **D=`00`** at `2e`.
4. Mixer **unicasts** TYPE `03` "assignment" (`27` bytes): header(`0b`=`01`) + **D**(`24`) +
   **N**(`25`) + `00`. _D_ is the channel the CDJ is wired to.
5. CDJ accepts _D_, broadcasts a single stage-3 (`04`) claim with N=`01`.
6. Mixer unicasts TYPE `05` "assignment finished" (`26` bytes): header + **D**=mixer's own number
   (`21`) + const `01`. CDJ then jumps straight to keep-alive.

(XDJ-XZ omits the type `05` packet — `startup.adoc` 444.)

---

## 2. Steady-state KEEP-ALIVE — TYPE `06` (confirmed)

After startup, the device broadcasts this to 50000 every **~1.5 s** (1500 ms). Length `36` bytes.
This is the packet a Virtual CDJ must emit to be seen and to receive directed status traffic. (`startup.adoc` 220-243; `vcdj.adoc` 11-15; `vcdj.py`; `prolink-connect`)

| Offset | Len | Field | Value / notes |
|---|---|---|---|
| `00`–`09` | 10 | Magic | `Qspt1WmJOL` |
| `0a` | 1 | TYPE | `06` |
| `0b` | 1 | subtype | `00` |
| `0c`–`1f` | 20 | Device name | ASCII NUL-padded |
| `20` | 1 | const | `01` |
| `21` | 1 | device byte | `02` CDJ / `01` mixer / `03` CDJ-3000 |
| `22` | 1 | pad | `00` |
| `23` | 1 | stype | `36` |
| `24` | 1 | **D** (device/player number) | your claimed number |
| `25` | 1 | const | `01` CDJ / `02` mixer (`packets.py` `u2`, "sometimes other player's id") |
| `26`–`2b` | 6 | **MAC address** | your NIC MAC |
| `2c`–`2f` | 4 | **IP address** | your NIC IP |
| `30` | 1 | **p** (peer count) | number of devices on net incl. self; starts `01`, ++ as devices appear, −− ~10 s after one leaves (`startup.adoc` 123-126) |
| `31`–`33` | 3 | `00 00 00` | padding |
| `34` | 1 | flags | `01` CDJ / `02` mixer (`packets.py` `flags`, default `1`) |
| `35` | 1 | trailing | `00` classic, **`01`** typical CDJ keep-alive, **`64`** for CDJ-3000 compat (`packets.py` `u4`) |

`packets.py` `type_status`: `player_number`(D)@`24`, `u2`@`25`, `mac_addr`@`26`–`2b`,
`ip_addr`@`2c`–`2f`, `device_count`(p)@`30` default `2`, Padding(3)@`31`–`33`, `flags`@`34`,
`u4`@`35` default `0x64`.

**Cross-check — prolink-connect `makeAnnouncePacket`** builds the *same* TYPE `06` keep-alive but
its hand-rolled offsets sit one byte earlier than dysentery's (it puts type at `0a`, name at `0c`,
length `00 36` at `22`–`23`, D at `24`, type at `25`, MAC at `26`–`2b`, IP at `2c`–`2f`, then
`01 00 00 00`, type, `00`). It treats bytes `20`–`21` as "2 byte unknown" `01 02`. Net wire bytes
match the dysentery layout; just a different field-grouping in comments. (confirmed via `prolink-connect/src/virtualcdj/index.ts` 84-124)

**Cadence confirmation:** `vcdj.py` `packet_interval = 1.5`; `prolink-connect`
`ANNOUNCE_INTERVAL = 1500`; `startup.adoc` "roughly every second and a half"; `vcdj.adoc` "roughly
every 1.5 seconds". **Confirmed 1.5 s.** Receivers treat a device as gone after a timeout
(prolink-connect default `deviceTimeout = 10000` ms = ~6–7 missed keep-alives). (confirmed)

> Minimal vCDJ keep-alive (what `vcdj.py.send_keepalive_packet` actually fills): type=`type_status`,
> subtype=`stype_status`, model=name, content={player_number, ip_addr, mac_addr}; everything else
> takes the struct defaults above. Sent to `(broadcast_addr, 50000)`.

---

## 3. Choosing a device number safely

### 3.1 Valid ranges (confirmed)

- **1–4**: real player slots (CDJ deck numbers as displayed on the unit).
- **5–6**: additionally valid on **CDJ-3000** networks.
- **`21` (=33 dec)**: mixer / DJM. rekordbox uses `11` (17 dec); rekordbox-mobile uses `29`. (`vcdj.adoc` 740-742)
- **7+**: "safe" for a passive virtual CDJ that does not need to act as a real player.

### 3.2 The core tradeoff (confirmed, `vcdj.adoc` 14-18, 258-265)

```
Higher number (e.g. 0x07)         |   Number in 1–6 (ideally 1–4)
----------------------------------|-----------------------------------------
+ Won't collide with real players |  − Must contend / may collide & be kicked
+ Simple, "just works" to observe |  + Required to issue dbserver metadata queries
− CANNOT do dbserver metadata     |  + Required to query streaming-track metadata
  queries (and streaming metadata |    (vcdj.adoc: "device number must be 6 or lower")
  fails for D > 6)                |  − If 4 real players present, no free slot in 1–4
```

`vcdj.adoc`: "use of a non-standard player number (outside 1–4, or 1–6 for CDJ-3000s) will
interfere with your ability to perform metadata requests using `dbserver` queries." And for
streaming tracks: "the virtual CDJ device number must be 6 or lower … greater than 6 will not
receive responses."

When all four 1–4 slots are occupied, there is no clean number for metadata; the docs point to
alternate strategies ("Reading Data with Four Players").

### 3.3 What the reference vCDJs default to

| Project | Default vCDJ number | Note |
|---|---|---|
| `prolink-connect` | `DEFAULT_VCDJ_ID = 0x07` | comment: "out of 1-6 range, thus will not be able to request metadata via remotedb" |
| `python-prodj-link` | `self.player_number = 5` (`vcdj.py`) | inside 1–6 → metadata-capable on CDJ-3000 nets |
| `vcdj.adoc` suggestion | `07` | "so as not to conflict with any actual players" |

### 3.4 Safe-claim algorithm (recommended for this project) (inferred from §1 + §3)

1. Bind UDP 50000 (and 50002) on the chosen interface; passively listen ≥ ~2 s, recording every
   `D` seen in keep-alive (`06`) and claim (`02`/`04`) packets. (Mandatory because XDJ-XZ won't
   defend its numbers — §1.5.)
2. To merely **observe/serve status**: pick `0x07` (or any free 7–15). Done — go to keep-alive.
3. To do **metadata/dbserver** queries: pick the lowest free number in 1–4 (1–6 on CDJ-3000 nets).
   Run the full §1.2–§1.4 claim handshake with `a`=auto (`31`=`01`).
4. If a TYPE `08` conflict arrives for your `D`, drop it, mark it taken, pick the next free number,
   restart the claim. Also be prepared to *send* TYPE `08` to defend your number afterward.

---

## 4. Impersonating a CDJ-2000NXS (device name & fields)

### 4.1 Device name string (confirmed / inferred)

- A real CDJ-2000nexus reports the name **`CDJ-2000nexus`** (ASCII). dysentery/vcdj.adoc refer to a
  "DJM-2000 nexus" mixer and CDJ status uses model names like `XDJ-1000` (see `id-use-reply.md`
  raw dump showing `XDJ-1000` and `VDJ-1000`/`VDJ-1000` in the 20-byte name field). The exact
  `CDJ-2000nexus` spelling is the well-known Pioneer model string. **(inferred for exact casing;
  confirmed that the field is the model/name string)**
- The name field is **20 bytes**, ASCII, **NUL-padded** (`prolink-connect/buildName`:
  `new Uint8Array(20)` then `set(ascii)`; `packets.py` `Padded(20, CString)`). Truncate/pad to 20.
- Reference vCDJs use non-CDJ names on purpose so they're filtered out
  (`prolink-connect` `VIRTUAL_CDJ_NAME='prolink-typescript'`; `python` `'Virtual CDJ'`). **To
  impersonate, set this to `CDJ-2000nexus`.** Note `prolink-connect`'s DeviceManager *ignores* any
  announce whose name equals its own `VIRTUAL_CDJ_NAME` — so a name collision with another tool
  could make it invisible to that tool.

### 4.2 Fields a real CDJ-2000NXS populates (confirmed where cited)

In its **keep-alive** (TYPE `06`): name=`CDJ-2000nexus`, `21`=`02` (CDJ), `D`=1–4, real MAC@`26`,
real IP@`2c`, `25`=`01`, `34`=`01`, `35`=`01`, `p`=peer count. In **status** packets (port 50002,
out of scope here but relevant to impersonation): nexus players send `d4`-byte status packets with
subtype `03` at byte `20`, firmware ASCII at `7c`–`7f`, `nx`(byte `cc`)=`0f` for nexus.
(`vcdj.adoc` 84-88, 404, 576). prolink-connect's `makeStatusPacket` uses firmware string `1.43`
and a fixed `d4`-ish template with device id written at `0x21` and `0x24`.

### 4.3 Use the real interface MAC & IP (confirmed)

`vcdj.adoc`: "use the actual MAC and IP addresses of the network interface on which you are
receiving DJ-Link traffic, so the devices can see how to reach you." Spoofing fake addresses breaks
the directed-traffic return path.

---

## 5. Broadcast addresses & source ports (confirmed)

| Aspect | Value |
|---|---|
| Discovery/claim/keepalive dest | subnet broadcast (`vcdj.py` derives via `IPv4Network(ip/netmask).broadcast_address`). On link-local nets: `169.254.255.255`; on `192.168.1.0/24`: `192.168.1.255` (`id-use-reply.md`). |
| Dest port | always **50000** |
| Conflict (`08`), mixer assign (`01`/`03`/`05`), IdUseReply (`03`) | **unicast** to the target device's port 50000 |
| Source port | bind and send from **50000** — required to receive the unicast conflict/assignment replies (`id-use-reply.md`) |
| vCDJ also binds | **50002** to receive status packets from real devices once announced (`vcdj.adoc`) |

---

## 6. Quick reference: type-byte hex discriminators

```
0x0a  hello / initial announcement      (also reused as CDJ status type on 50002)
0x00  stage-1 claim (MAC, N=1..3)
0x02  stage-2 claim / IdUseRequest (IP + propose D, a=auto/manual)
0x04  stage-3/final claim (assert D, N)
0x06  KEEP-ALIVE (steady state, every 1.5s)
> **Corrected on hardware — C12.** 2.0 s, not 1.5.
0x08  channel conflict / defense (give up D)
0x01  mixer→player: assignment intention   (channel-specific port only)
0x03  mixer→player: assignment (use D) / IdUseReply
0x05  mixer→player: assignment finished
```

All carry magic `51 73 70 74 31 57 6d 4a 4f 4c` at offset `00`, type at `0a`, name at `0c`–`1f`.

---

### Summary

A ProLink device joins the network on UDP 50000 by broadcasting (every ~300 ms) three "hello"
packets (type `0x0a`), then a three-stage device-number claim — type `0x00` (announce MAC), type
`0x02` (publish IP and propose number _D_, the "IdUseRequest"), and type `0x04` (assert _D_) — each
iterating a counter N=1..3, with byte `0x31` flagging auto (`01`) vs manual (`02`) assignment;
existing owners defend a number by unicasting a type `0x08` conflict packet back to port 50000, on
receipt of which the newcomer picks another number, so an implementation must bind/send from 50000
to both hear and send these. Once settled, the device broadcasts a 0x36-byte keep-alive (type
`0x06`) every ~1.5 s carrying name, _D_, MAC, IP and a peer count. All packets share the
`Qspt1WmJOL` magic, a type byte at `0x0a`, a 20-byte NUL-padded name at `0x0c`–`0x1f`, and a device
byte at `0x21` (`02`=CDJ, `01`=mixer, `03`=rekordbox/CDJ-3000). For a virtual CDJ, `0x07` is safe
but cannot do dbserver/metadata queries (which need 1–4, or ≤6 for CDJ-3000/streaming); to
impersonate a CDJ-2000NXS use name `CDJ-2000nexus`, device byte `02`, a number in 1–4, and the real
NIC MAC/IP. CDJ-3000 coexistence requires the variant bytes (byte `21` and keep-alive byte `35`=`64`).

**Gaps / uncertainties:**
- The exact ASCII casing of `CDJ-2000nexus` is the documented Pioneer model string but is *inferred*
  here — none of the supplied source files contain a literal CDJ-2000 name dump (the only raw name
  captures are `XDJ-1000`/`VDJ-1000` in `id-use-reply.md`). Verify against a real capture before relying on exact bytes.
- Subtype/length values for the mixer-side assignment types `01`/`03`/`05` are inferred from the
  stated packet sizes in `startup.adoc`; `packets.py` does not model them, so per-byte field offsets
  there are my reading of the bytefield diagrams.
- The precise meaning of keep-alive bytes `25`, `34`, `35` (the `01`/`02`/`64` family) is only
  partially documented; values given are the confirmed observed ones, not an exhaustive spec.
- dysentery vs prolink-connect use slightly different field groupings/offsets in comments; the on-
  wire bytes agree, but if you copy prolink-connect's offset comments verbatim you'll be off by one
  in a couple of spots versus the `(startup.adoc)` numbering used throughout this doc.
