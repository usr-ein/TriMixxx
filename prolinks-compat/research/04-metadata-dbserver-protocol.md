# 04 — Metadata / dbserver (remotedb) TCP Protocol

How one player queries another player's library over TCP: track metadata, browse menus,
album art, waveforms, beat grids, cue points, playlists. This is the "remotedb" /
"dbserver" protocol. It underpins project objective #1 (read other CDJs' libraries) and #2
(serve our library to other CDJs).

**Sources (cited inline):**
- `dysentery/doc/modules/ROOT/pages/track_metadata.adoc` — primary spec (~1700 lines). Cited as `[TM:line]`.
- `dysentery/doc/modules/ROOT/pages/menus.adoc` — menu request catalogue. Cited as `[MENU:line]`.
- `prolink-connect/src/remotedb/*` — TypeScript reference implementation (client only). Cited as `[PC:file]`.
- `python-prodj-link/prodj/data/dbclient.py`, `prodj/network/packets.py` — Python reference client. Cited as `[PY:dbclient]` / `[PY:packets]`.

Confidence is marked **(confirmed)** = corroborated by ≥2 independent implementations or documented packet captures, **(inferred)** = derived from one source / stated as guess in the source.

---

## 0. Big picture

```
                 UDP discovery (see doc 01)         we already know the target's IP + player number
                          │
                          ▼
   ┌─────────────────────────────────────────────────────────────┐
   │ Step 1: TCP connect to port 12523 ("RemoteDBServer" query)   │  → learn dynamic dbserver port (usually 1051)
   ├─────────────────────────────────────────────────────────────┤
   │ Step 2: TCP connect to that dbserver port                    │
   │   2a. send 4-byte "1" preamble, server echoes 4-byte "1"     │
   │   2b. send Introduce (type 0x0000, TxID 0xfffffffe, our #)   │  → server replies 0x4000 w/ its own player #
   ├─────────────────────────────────────────────────────────────┤
   │ Step 3: per-query: setup request → 0x4000 (count avail)      │
   │         then render menu (0x3000) → header/items/footer      │
   │         OR direct binary request (artwork/waveform/cue/grid) │
   └─────────────────────────────────────────────────────────────┘
```

The protocol is **stateful per client**: you set up a menu, then page through it. The server
keeps a small per-client state table, which is the root of the "player number" limitations
(§2.3) `[TM:193]`.

---

## 1. Port discovery (DBServerQuery, TCP port 12523)

Open a TCP connection to **port 12523** (`REMOTEDB_SERVER_QUERY_PORT`) `[PC:constants]` `[PY:packets:456]`
on the target device and send this fixed query packet `[TM:12-21]`:

```
00 00 00 0f  52 65 6d 6f 74 65 44 42 53 65 72 76 65 72  00
└─ UInt32 ─┘  └──────── "RemoteDBServer" (ASCII) ───────┘ └ NUL
   = 0x0f
```

- A big-endian 4-byte integer with value `0x0000000f` (=15, the length of the following string), then the 14 ASCII bytes `RemoteDBServer`, then a single trailing `00` (NUL). Total **19 bytes**. **(confirmed)**
  - `prolink-connect`: `Buffer.from([0x00,0x00,0x00,0x0f, ...Buffer.from('RemoteDBServer','ascii'), 0x00])` `[PC:index L91-95]`
  - `python-prodj-link`: `DBServerQuery = Struct("magic"/Const(0x0f, Int32ub), "query"/Const("RemoteDBServer", CString))` `[PY:packets:457-460]`

**Response:** exactly **2 bytes**, a big-endian UInt16 = the dbserver TCP port. `[TM:23-24]` `[PC:index L105-109]` `[PY:packets:461]`.
On real CDJs the port has always been **1051**, but you must query it (it is documented as dynamic / future-proof). `[TM:24]` **(confirmed)**

> The same query also works against **rekordbox** running on a laptop — it answers with its own
> dbserver port, which you can then query identically. `[TM:25]` (confirmed)

After getting the port, **close** this socket and open a fresh TCP connection to the dbserver port `[PY:dbclient:400-408]`.

---

## 2. Connection setup / handshake on the dbserver port

Three sub-steps, in order. All occur on the freshly-opened TCP connection to the dynamic port.

### 2.1 Preamble (`0x01` setup)

Send a single **number field** (UInt32) with value **1** — i.e. the 5 bytes `11 00 00 00 01`
(`0x11` = UInt32 field tag, then big-endian `00000001`). The server echoes the **same 5 bytes** back. `[TM:30-44]` **(confirmed)**

- `prolink-connect`: writes `new UInt32(0x01).buffer`, reads a UInt32 field, asserts value `0x01` `[PC:index L228-237]`.
- `python-prodj-link`: `DBFieldFixed("int32").build(1)` then reads & parses reply `[PY:dbclient:410-418]`.

This is just the smallest example of the field type system (§3.1). Note the field tag byte `0x11`
precedes the 4 value bytes — it is NOT a bare `00000001`.

### 2.2 "Menu setup" / Introduce message (the RMST/context message)

Now send the **query-context setup message** (prolink-connect calls it `Introduce`, python calls
it `setup`, dysentery calls it the "query context setup message"). It is a full dbserver message
(§3.2) with: `[TM:177-191]` **(confirmed)**

| Field | Value |
|---|---|
| magic | `0x872349ae` |
| TxID  | `0xfffffffe` (special, reused for Disconnect) |
| type  | `0x0000` (Introduce / setup) |
| arg count | 1 |
| arg-type list | `06 00 00 00 00 00 00 00 00 00 00 00` (one UInt32 arg) |
| arg 1 | UInt32 = **D_ours** (our device/player number) |

`prolink-connect`: `new Message({transactionId: 0xfffffffe, type: Introduce, args:[new UInt32(hostDevice.id)]})` `[PC:index L240-247]`.
`python-prodj-link`: `{transaction_id:0xfffffffe, type:"setup", args:[{int32: own_player_number}]}` `[PY:dbclient:420-426]`.

**Response:** a `0x4000` ("Success" / requested-data-available) message with **2 numeric args**:
arg1 = echoes the request type (`0x0000`), arg2 = **the server's own player number** (`D_theirs`),
not an item count. `[TM:196-206]` **(confirmed)**

### 2.3 Player-number negotiation rules `[TM:188-193]` (confirmed)

`D_ours` must be a valid player number **1–4** AND:
- that player must actually be present on the network,
- must not be the player you are contacting,
- must not belong to a different player that has Link-connected to the target and loaded a track from it.

Safest value: your own virtual CDJ's number — only possible if fewer than 4 real CDJs are on the network.

> `python-prodj-link` notes an alternative: using **player number 0** "seems to work if less than
> 4 players are on the network", but it "messes up rendering on the players sometimes" (e.g. when
> the target has its browser open). `[PY:dbclient:114-117]` **(inferred / situational)**

The first byte of the per-request first argument (the `D` in `r:m:s:t`, §4.1) carries this device number.

---

## 3. Message wire format

### 3.1 Field type system `[TM:46-117]` (confirmed)

Every value on the wire is a **type-tagged field**: 1 type byte, then the value. Fixed-size for
numbers; length-prefixed for variable types.

| Field tag | Type | Value encoding |
|---:|---|---|
| `0f` | UInt8  | 1 byte, big-endian |
| `10` | UInt16 | 2 bytes, big-endian |
| `11` | UInt32 | 4 bytes, big-endian |
| `14` | Binary (blob) | 4-byte BE length, then that many raw bytes |
| `26` | String | 4-byte BE length **in UTF-16 chars**, then 2×length bytes of UTF-16 **big-endian**, last char always NUL (`0000`) |

Source enums agree exactly: `FieldType {UInt8=0x0f, UInt16=0x10, UInt32=0x11, Binary=0x14, String=0x26}` `[PC:fields L8-14]`;
`DBFieldType {int8=0x0f, int16=0x10, int32=0x11, binary=0x14, string=0x26}` `[PY:packets:463-469]`.

String notes:
- Length prefix counts **UTF-16 characters including the trailing NUL**, not bytes. So bytes on wire = 2 × length. `[TM:106-108]`
- prolink-connect builds it as `utf16le` then `.swap16()` to get big-endian, length header = `data.length/2` `[PC:fields L157-172]`.
- The "Label N byte size" numeric arg in menu items (§4.4) reports the **byte** size of the following string, which is 2× the char count. (e.g. a 23-char title shows byte size `0x50`=80 → 40 chars-with-NUL × 2; observed in dysentery example `[TM:1673]`).

### 3.2 Message header `[TM:119-158]` (confirmed)

A message = header + argument fields. Header layout (each piece is itself a field):

```
11 872349ae      UInt32 magic            (always 0x872349ae)            [TM:122]
11 <TxID>        UInt32 transaction id   (1,2,3,…; 0xfffffffe special)  [TM:123]
10 <type>        UInt16 message type     (see §3.4)                     [TM:124]
0f <n>           UInt8  argument count    (number of args present)       [TM:124]
14 0000000c <t1..t12>   Binary blob, ALWAYS 12 bytes: argument-type tags [TM:124-126]
<arg field 1> … <arg field n>            the actual argument fields
```

- **TxID**: starts at 1, incremented per query; all responses echo the request's TxID. `[TM:123]` Setup & Disconnect use the magic `0xfffffffe`. `[TM:178,1609]`
- **Argument-type blob** is always 12 bytes (max 12 args), zero-padded past the real count. `[TM:125-126]`
- prolink-connect serializes exactly this order `[PC:message/index L164-173]`; python `DBMessage` struct matches `[PY:packets:591-598]`.

### 3.3 Argument-type tag values `[TM:163-175]` (confirmed)

These are a **second, different** numbering from the field tags (§3.1) — redundant but mandatory.

| Arg tag | Meaning |
|---:|---|
| `02` | UTF-16BE string (trailing NUL) |
| `03` | Binary blob |
| `06` | UInt32 (4-byte BE int) |
| `04` | UInt8 (1-byte int) — **inferred**, never observed `[TM:174]` |
| `05` | UInt16 (2-byte int) — **inferred**, never observed `[TM:174]` |

Confirmed by `ArgumentType {String=0x02, Binary=0x03, UInt32=0x06}` `[PC:message/index L19-23]` and
`DBMessageFieldType {int8=0x04, int16=0x05, int32=0x06, binary=0x03, string=0x02}` `[PY:packets:496-502]`.

**Empty-blob quirk (important for both reading and writing):** there is always a UInt32 length
field immediately before a Binary arg. If that length is 0, the Binary field is **omitted
entirely** from the wire — you must not try to read it; the next field follows directly. This is
also how clients *send* a "this blob is empty" placeholder (declare it in the arg-type list but
don't emit it). `[TM:563,701-708]` `[PC:message/index L97-106, L149-162]` **(confirmed)**

### 3.4 Message type IDs

Request types (client → server):

| Type | Name | Notes |
|---:|---|---|
| `0000` | Introduce / setup | context setup, TxID `0xfffffffe` `[TM:178]` |
| `0100` | Disconnect | header only, TxID `0xfffffffe` `[TM:1599-1609]` |
| `3000` | Render menu | paginate a set-up menu `[TM:259]` |
| `1000` | Root menu | args: `r:m:s:t`, sort, `0x00ffffff` `[MENU:27]` |
| `1001` | Genre menu | `[MENU:28]` |
| `1002` | Artist menu | `[MENU:29]` |
| `1003` | Album menu | `[MENU:30]` |
| `1004` | Track (all tracks) menu | args: `r:m:s:t`, sort `[TM:1417-1430]` |
| `1006` | BPM menu | |
| `1007` | Rating menu | |
| `1008` | Year/Century menu | |
| `100a` | Label menu | |
| `100d` | Color menu | |
| `1010` | Time/Duration menu | |
| `1011` | Bitrate menu | |
| `1012` | History menu | |
| `1013` | Filename menu | |
| `1014` | Key menu | |
| `1101` | Artists for Genre | + genre id `[MENU:45]` |
| `1102` | Albums for Artist | + artist id |
| `1103` | Tracks for Album | + album id |
| `1105` | Playlist menu | args: `r:m:s:t`, sort, id, folder? `[TM:1528-1543]` |
| `1107` | Tracks for Rating | |
| `1108` | Years for Decade | |
| `110a` | Artists for Label | |
| `110d` | Tracks for Color | |
| `1110` | Tracks for Time | |
| `1112` | Tracks for History | |
| `1114` | Distances for Key | |
| `1201` | Albums for Genre+Artist | use `-1`/`0xffffffff` for "all" `[MENU:58]` |
| `1202` | Tracks for Artist+Album | |
| `1206` | Tracks for BPM ±% | + bpm id, distance 0–6 |
| `1208` | Tracks for Decade+Year | |
| `120a` | Albums for Label+Artist | |
| `1214` | Tracks near Key | + key id, distance |
| `1301` | Tracks for Genre+Artist+Album | |
| `1300` | Search (substring) | uppercase UTF-16 string arg `[MENU:71,89-94]` |
| `1302` | Original Artist menu | |
| `1402` | Albums for Original Artist | |
| `1602` | Remixer menu | |
| `1702` | Albums for Remixer | |
| `2001` | Hot cue bank request | `[PY:packets:561]` (inferred) |
| `2002` | Get track metadata (rekordbox) | `[TM:213-221]` |
| `2003` | Get artwork | `[TM:540-552]` |
| `2004` | Get waveform preview | `[TM:679-692]` |
| `2006` | Folder menu | args: `r:m:s:t`, sort?, folder id, 0 `[MENU:72]` |
| `2102` | Get track info / mount info (path) | `[PY:packets:568]`, `[PC:types L65]` |
| `2104` | Get cue points & loops (nexus) | `[TM:1141-1148]` |
| `2202` | Get generic metadata (non-rekordbox) | variant of 2002 `[TM:532]` |
| `2204` | Get beat grid | `[TM:606-615]` |
| `2904` | Get waveform detail | `[TM:755-765]` |
| `2b04` | Get extended (nxs2) cue points & loops | `[TM:1217-1225]` |
| `2c04` | Get analysis tag (nxs2/3000 waveforms, song structure, vocal) | generic ANLZ-tag request `[TM:1363-1391]` |

Response types (server → client):

| Type | Name | Notes |
|---:|---|---|
| `4000` | Success / data available | arg1 = echoed request type, arg2 = item count (or `0xffffffff` = not found, or player# for setup) `[TM:195,241-256]` |
| `4001` | Menu header | precedes items `[TM:320-328]` |
| `4002` | Artwork | blob `[TM:559-575]` |
| `4003` | Error / invalid request | `[PC:types L79]`, `[PY:packets:579]` (inferred) |
| `4101` | Menu item | 12 args (§4.4) `[TM:333-369]` |
| `4201` | Menu footer | header only, no args `[TM:343-349,429-431]` |
| `4402` | Waveform preview | blob `[TM:712-729]` |
| `4502` | (unknown1) | reply to `2504`, seen during track load `[PY:packets:583]` (inferred) |
| `4602` | Beat grid | blob `[TM:625-641]` |
| `4702` | Cue & loop (nexus) | 9 args, blob `[TM:1158-1168]` |
| `4a02` | Waveform detail | blob `[TM:776-793]` |
| `4e02` | Extended cue & loop (nxs2) | blob + entry count `[TM:1234-1251]` |
| `4f02` | Analysis tag (nxs2/3000) | reply to `2c04` `[TM:843-862]` |

Canonical enums: `[PC:types]` (ControlRequest/MenuRequest/DataRequest/Response) and `[PY:packets:518-589]` (`DBRequestType`).

---

## 4. Menu request patterns

### 4.1 The `r:m:s:t` / DMST first argument `[TM:225-234]` `[MENU:75-87]` (confirmed)

The first arg of nearly every track/menu request is a single UInt32 packed as 4 bytes
`D : M : Sr : Tr` (Beat Link names it `requester:menu:slot:type`):

- **D** (byte 3, high) = our device number (= `D_ours` from setup).
- **M** (byte 2) = menu/display location:
  - `01` = main menu (left half) — used for metadata, track lists, playlists, and (oddly) waveform detail & nxs2 tag requests. `[TM:229,772,840]`
  - `02` = sub-menu (right-half info popup) `[TM:279]`
  - `03` = metadata preview of a selected track `[MENU:82]`
  - `08` = loading non-text/binary data: artwork, beat grid, preview waveform, cues `[TM:282,557,623,699,1155]`
- **Sr** (byte 1) = media slot (same values as CDJ status packets): `0` empty, `1` cd, `2` sd, `3` usb, `4` rekordbox. `[PY:packets:201-207]`
- **Tr** (byte 0, low) = track type: `1` rekordbox-analyzed, `2` unanalyzed file, `5` CD audio, `6` streaming (Beatport LINK). `[TM:232,532,674]` `[PY:packets:209-214]`

Both implementations build it as `D<<24 | M<<16 | Sr<<8 | Tr`:
`python` `self.own_player_number<<24 | 1<<16 | slot_id<<8 | 1` `[PY:dbclient:269]`;
`prolink-connect` builds it in `fieldFromDescriptor(lookupDescriptor)` (utils).

### 4.2 Metadata-by-id flow (the canonical example) `[TM:208-311]` (confirmed)

To fetch one track's full metadata, given rekordbox id + slot:

1. **Setup request** type `0x2002`, 2 args: `[r:m:s:t with M=01]`, `UInt32 rekordbox_id`. `[TM:213-221]`
   prolink: `new Message({type: GetMetadata, args:[descriptor, new UInt32(trackId)]})` `[PC:queries L43-46]`.
2. Server replies `0x4000`: arg1=`0x2002`, arg2=count (e.g. `0x0b`=11 items). `0xffffffff` ⇒ no such track. `[TM:241-256]`
3. **Render menu** request type `0x3000`, 6 args: `[r:m:s:t]`, `offset`, `limit`, `0`, `total`, `0`. `[TM:261-309]`
4. Server streams: one `0x4001` menu header, then `count` × `0x4101` menu items, then one `0x4201` footer. `[TM:311]`

Generic (non-rekordbox) metadata is identical but request type `0x2202` and `Tr`=`02` (file) or `05` (CD). `[TM:527-538]`

Track **file path** uses type `0x2102` (`GetTrackInfo`), then render; the path is the `Path`
item (item type `0x0000`). `[PC:queries L333-366]` `[PY:dbclient:568]`

### 4.3 The render request in detail `[TM:261-294]` (confirmed)

6 UInt32 args:

| Arg | Name | Value |
|---:|---|---|
| 1 | `r:m:s:t` | same D/M/Sr/Tr as the setup request |
| 2 | offset | first item index to return (0-based) |
| 3 | limit | how many items to return |
| 4 | unknown | send `0` |
| 5 | total | usually the count from the `0x4000`; sending a copy of `limit` also works |
| 6 | unknown | send `0` |

**Pagination:** large lists must be fetched in batches. **64 items per render is documented safe**
on Nexus 2; thousands fail. Loop: increment `offset` by 64, set `limit`/`total` = min(64,
remaining). `[TM:288-290,1445-1447]` python's client found "hundreds at once" works on XDJ-1000
and doesn't fragment `[PY:dbclient:306-307]` — so the safe batch size is hardware-dependent;
**use ≤64 to be safe.**

### 4.4 Menu item (`0x4101`) argument layout `[TM:351-369]` (confirmed)

Always 12 args, types `06 06 06 02 06 02 06 06 06 06 06 06` (10 numbers + 2 strings):

| Arg | Type | Meaning |
|---:|---|---|
| 1 | num | Parent ID (e.g. artist id for a track) |
| 2 | num | Main ID (e.g. rekordbox id) |
| 3 | num | byte length of Label 1 |
| 4 | str | Label 1 (primary text: title / name) |
| 5 | num | byte length of Label 2 |
| 6 | str | Label 2 (secondary text) |
| 7 | num | **item type** (see §4.5) |
| 8 | num | flag/column-config field (unclear; e.g. `0x01000000`) `[TM:1677]` |
| 9 | num | artwork id (when item is a Track Title) |
| 10 | num | playlist position (when listing a playlist) |
| 11 | num | unknown |
| 12 | num | unknown |

Parsed identically by `makeItemData` (uses args 0,1,3,5,6,8) `[PC:item L102-109]` and
`parse_metadata_payload` `[PY:dbclient:121-188]`.

For a single track's metadata, the items returned (item type in arg 7) are: `04` Title (arg9 =
artwork id, arg1 = artist id), `07` Artist, `02` Album, `0b` Duration (seconds in arg2), `0d`
Tempo (BPM×100 in arg2), `23` Comment, `0f` Key, `0a` Rating (0–5 in arg2), `13`–`1b` Color, `06`
Genre, `2e` Date Added. `[TM:371-427]`

### 4.5 Item type values (arg 7) `[TM:433-525]` (confirmed)

| Type | Meaning | | Type | Meaning |
|---:|---|---|---:|---|
| `0000` | mount path / Path | | `0098` | Hot cue bank menu |
| `0001` | Folder | | `00a0` | All |
| `0002` | Album title | | `0013` | Color: none |
| `0003` | Disc | | `0014`–`001b` | Color: pink/red/orange/yellow/green/aqua/blue/purple |
| `0004` | Track title | | `0080` | Genre menu |
| `0006` | Genre | | `0081` | Artist menu |
| `0007` | Artist | | `0082` | Album menu |
| `0008` | Playlist | | `0083` | Track menu |
| `000a` | Rating | | `0084` | Playlist menu |
| `000b` | Duration (sec) | | `0085`–`0095` | BPM/Rating/Year/Remixer/Label/OrigArtist/Key/DateAdded/Color/Folder/Search/Time/Bitrate/Filename/History menus |
| `000d` | Tempo (BPM×100)| | `0204` | Title + Album |
| `000e` | Label | | `0604` | Title + Genre |
| `000f` | Key | | `0704` | Title + Artist |
| `0010` | Bit rate | | `0a04` | Title + Rating |
| `0011` | Year | | `0b04` | Title + Time |
| `0023` | Comment | | `0d04` | Title + BPM |
| `0024` | History playlist | | `0e04` | Title + Label |
| `0028` | Original artist | | `0f04` | Title + Key |
| `0029` | Remixer | | `1004` | Title + Bit rate |
| `002e` | Date added | | `1a04` | Title + Color |
| | | | `2304` | Title + Comment |
| | | | `2804` | Title + Original Artist |
| | | | `2904` | Title + Remixer |
| | | | `2a04` | Title + DJ Play Count |
| | | | `2e04` | Title + Date Added |

Full enum: `[PC:item L8-78]`, `[PY:dbclient:11-78]`.

> **CDJ-3000 caveat:** CDJ-3000s put extra info in the two high bytes of arg 7; mask with
> `0xffff` to recover the real item type. `[TM:523-525]` **(confirmed)**

### 4.6 Track list & sort orders `[TM:1417-1503]` (confirmed)

`0x1004` lists all tracks in a slot; `sort` arg (arg 2) controls both order and the second column
(arg 6 / Label 2) returned. Sort values: `01` Title, `02` Artist, `03` Album, `04` BPM, `05`
Rating, `06` Genre, `07` Comment, `08` Time, `09` Remixer, `0a` Label, `0b` Original Artist, `0c`
Key, `0d` Bit rate, `10` Play count, `11` Date added; `0` = default (rekordbox-configured second
column). `[TM:1483-1503]` `[PY:dbclient:81-98]`. In all sorts arg2 = track id, arg4 = title.

### 4.7 Playlists & folders `[TM:1520-1595]` (confirmed)

`0x1105`, 4 args after magic header: `[r:m:s:t]`, `sort`, `id`, `folder?`.
- root of playlist tree: `id=0, folder?=1`. `[TM:1543-1544]`
- folder listing returns items of type `0001` (folder) / `0008` (playlist), name in Label 1, arg10 = position. `[TM:1550-1567]`
- a playlist listing (`folder?=0`) returns track-list entries exactly like §4.6, honoring `sort`. `[TM:1569-1573]`

python builds it: arg order `[descriptor, sort_id, id, folder_flag]` where folder_flag = `1` for
folder else `0` `[PY:dbclient:278-281]`; prolink-connect: `[descriptor, sort, id, isFolder]`,
`isFolder = isFolderRequest ? 1 : 0` `[PC:queries L391-398]`.

> Note: `prolink-connect` and `python` pass `isFolder` as `0/1` in the **4th** position; the dysentery
> doc field order matches. The `id` is the folder/playlist id. (confirmed across all 3.)

### 4.8 Root menu & nested navigation

`0x1000` root menu: args `[r:m:s:t]`, `sort`, `0x00ffffff` `[MENU:27]` `[PY:dbclient:273-275]`.
The response items have item types `0x80`–`0x95` telling you which sub-menus the media exposes
(the DJ chooses which indices exist). `[MENU:18-20]` Then drill down: Genre→`1001`,
Artists-for-Genre→`1101`(+genre id), Albums-for-Artist→`1102`(+artist id),
Tracks-for-Album→`1103`(+album id), etc. (full table §3.4). Use `0xffffffff` (`-1`) as an id to
mean "all" in the multi-level requests. `[MENU:58]` `[PY:dbclient:285-286]`

---

## 5. Binary responses

All binary requests use `M=08` in `r:m:s:t` (except waveform detail and the `2c04` analysis-tag
family which oddly use `M=01` `[TM:772,840]`). Every binary response carries a UInt32 length arg
right before the blob; **if length=0 the blob is omitted** (§3.3) — never blindly read it. `[TM:563,629,716]`

### 5.1 Album art — request `0x2003`, response `0x4002` `[TM:540-595]` (confirmed)
- Request args: `[r:m:s:t M=08]`, `UInt32 artwork_id` (the artwork id from the Track Title item, arg 9).
- Optional: append a UInt32 `1` ⇒ 240×240 hi-res; `2` ⇒ art for a non-rekordbox track. Default = 80×80. `[TM:588-595]`
- Response 4 args: `2003`, `0`, `length`, `blob` (image bytes — JPEG/PNG). `[TM:559-575]`
- prolink: `args:[descriptor, new UInt32(artworkId)]`, returns `art.data` = `args[3].value` `[PC:queries L196-209]`, `[PC:response L36]`.

### 5.2 Waveform preview — request `0x2004`, response `0x4402` `[TM:679-750]` (confirmed)
- Request declares **5 args** but sends only 4: `[r:m:s:t M=08]`, `UInt32 4` (unknown; 3 or 4 seen), `UInt32 rekordbox_id`, `UInt32 0` (blob length=0 → blob omitted). `[TM:687-710]`
  prolink sends `[descriptor, UInt32(0), UInt32(trackId), UInt32(0), Binary(empty)]` `[PC:queries L236-245]`;
  python inserts a `4` then appends a `0` `[PY:dbclient:367-369]`.
- Response: `4402`,`0`,`length`,`blob`. Blob = **900 bytes**: 400 columns × 2 bytes (byte0 = height 0–31, byte1 = whiteness 0–7), then a trailing 100-byte tiny preview for pre-Nexus CDJ-900s. `[TM:731-750]` decode in `[PC:response L57-67]`.

### 5.3 Waveform detail — request `0x2904`, response `0x4a02` `[TM:752-800]` (confirmed)
- Request args: `[r:m:s:t M=01]`, `UInt32 rekordbox_id`, `UInt32 0`. `[TM:757-765]` `[PC:queries L256-263]`.
- Response blob: 1 byte per segment, 150 segments/sec; high 3 bits = color/whiteness, low 5 bits = height 0–31. `[TM:795-800]` `[PC:response L72-89]`.

### 5.4 Nxs2 / CDJ-3000 waveforms & analysis tags — request `0x2c04`, response `0x4f02` `[TM:812-1414]` (confirmed)
Generic "fetch an ANLZ analysis-file tag" mechanism. Args: `[r:m:s:t M=01]`, `UInt32 rekordbox_id`,
`UInt32 tag` (4-char code as LE-packed int), `UInt32 extension` (e.g. `EXT\0`). `[TM:1371-1391]`
- Tags: `PWV4` nxs2 color preview, `PWV5` nxs2 color detail, `PWV6`/`PWV7` CDJ-3000 3-band preview/detail (ext `2EX`), `PWVC` vocal config (`2EX`), `PSSI` song structure/phrases (`EXT`). `[TM:823,887,956,1019,1093,1318]`
- prolink builds tag/ext via `Buffer.from('PWV5').readUInt32LE()` / `Buffer.from('EXT\0').readUInt32LE()` `[PC:queries L278-286]`;
  python uses `Nxs2RequestIds {"4VWP":0x34565750, "5VWP":0x35565750, "TXE":0x00545845}` `[PY:packets:618-622]`.
- Response 5 args: `2c04`,`0`,`length`,`blob`,`0`. Tag body starts at **byte 0x34** of the blob. `[TM:843-864,1414]`

### 5.5 Beat grid — request `0x2204`, response `0x4602` `[TM:597-664]` (confirmed)
- Request args: `[r:m:s:t M=08]`, `UInt32 rekordbox_id`. `[TM:608-615]` `[PC:queries L218-224]`.
- Response 4 args: `2204`,`0`,`length`,`blob`. Beat entries start at **byte 0x14**, 16 bytes each, **little-endian**:
  - bytes 0–1: beat-in-bar (1–4); bytes 2–3: tempo (BPM×100); bytes 4–7: time ms at 100% speed; bytes 8–15 unknown. `[TM:643-664]`
  - decode `[PC:response L41-52]` (`offset = readUInt32LE(+4)`, `bpm = readUInt16LE(+2)/100`, `count = data[0]`); python `Beatgrid` struct `[PY:packets:602-615]`.

### 5.6 Cue points & loops `[TM:1133-1313]` (confirmed)

**Nexus (basic)** — request `0x2104`, response `0x4702`:
- Request: `[r:m:s:t M=08]`, `UInt32 rekordbox_id`. `[TM:1141-1148]`
- Response has 9 args; arg4 = blob of **0x24 (36)-byte** entries `[TM:1158-1176]`:
  - byte0 `Fl` (1=loop), byte1 `Fc` (1=cue), byte2 `H` (0=memory; 1/2/3 = hot cue A/B/C), bytes `0x0c`–`0f` `cue` (LE, 1/150 s), bytes `0x10`–`13` `loop` end (LE). `[TM:1180-1203]`
  - decode `[PC:response L108-130]` (offset/length in 1/150-s frames → ms).

**Nxs2 (extended)** — request `0x2b04`, response `0x4e02`:
- Request: `[r:m:s:t M=08]`, `UInt32 rekordbox_id`, `UInt32 0`. `[TM:1217-1225]` `[PC:queries L319-321]`.
- Response 5 args; arg4 = blob of **variable-length** entries (each starts with a 4-byte LE `length`), arg5 = entry count. `[TM:1234-1253]`
  - byte3 `H` (1–8 = hot cues A–H, 0=memory), byte6 `Fl` (1=memory,2=loop), `cue`@`0x0c` / `loop`@`0x10` (LE frames), `c_id`@`0x22` color-table row, `len_c`@`0x48` (LE) comment byte length, UTF-16 comment follows, then 4 bytes hot-cue color code + RGB. `[TM:1257-1313]`
  - decode `[PC:response L136-186]`. Tip: even old players were firmware-updated to answer `0x2b04`; fall back to `0x2104` if it fails. `[TM:1211-1213]`

> All cue/beat-grid times are **little-endian** and in 1/150-second frame units — the only LE
> numbers in an otherwise big-endian protocol. `[TM:656 fn,1201]`

### 5.7 Disconnect `[TM:1597-1609]` (confirmed)
Send type `0x0100` with TxID `0xfffffffe` and no args; the player closes its side.
prolink: `new Message({transactionId:0xfffffffe, type:Disconnect, args:[]})` `[PC:index L269-273]`.

---

## 6. SERVING this protocol (act as the dbserver so other CDJs read OUR library)

Objective #2 means **we run the server side**. Both reference repos are **clients only** — neither
implements a dbserver. So serving is a from-scratch effort; the wire format above is fully
specified, but several behaviors are observed only from the client's perspective. What it takes:

### 6.1 Required server surface
1. **Listen on TCP 12523** and answer the `RemoteDBServer` query with a 2-byte BE port (e.g. announce 1051 or any port we bind). `[TM:23-24]` Simple and fully specified.
2. **Listen on the announced dbserver port.** Per connection:
   - echo the 4-byte `0x01` preamble. `[TM:30]`
   - on `Introduce` (`0x0000`, TxID `0xfffffffe`), reply `0x4000` with arg1=`0x0000`, arg2 = **our** player number. `[TM:196-206]`
   - maintain **per-connection menu state** (the set-up menu + its item count) so a following `0x3000` render can page it. This statefulness is mandatory — the client sends setup then render as separate messages. `[TM:193]`
3. **Implement setup→render for menus** (`0x4000` count, then `0x4001`/`0x4101`×n/`0x4201` on render) and **direct binary responders** for artwork/waveforms/grid/cues. We map our own library (rekordbox `.PDB` + ANLZ files, see doc 03) into these item/blob shapes.
4. Honor the empty-blob omission rule and the 12-slot arg-type list exactly (§3.2–3.3), and echo TxID.

### 6.2 Known asymmetries & difficulties
- **No reference server exists** in either repo — all field semantics are inferred from the *client* direction. Several arg slots are "unknown but send 0"; as a *server* we must decide what to emit, and clients (real CDJs) may be picky. **(inferred risk)**
- **Client = real CDJ vs client = rekordbox differ.** dysentery warns rekordbox packs **multiple
  messages into one TCP segment**, and a single message may (defensively) span segments — our
  server must frame by message length, not by recv() boundaries. `[TM:314-318]` Conversely, when
  *we serve*, a CDJ client likely tolerates one-message-per-write but we should test both.
- **Player-number gating** (§2.3) is enforced by *real player* servers. If we serve, we should be
  lenient about which `D_ours` we accept (real CDJs reject some), but we still must report a
  sensible player number in the setup reply.
- **CDJ-3000 high-byte item type bits** (§4.5) — if we serve CDJ-3000 clients we may need to set
  those bits; their meaning is undocumented. `[TM:523-525]` **(gap)**
- **Analysis-tag passthrough (`0x2c04`)** is the cleanest path for new waveforms — a server that
  can serve raw ANLZ tags (`PWV4/5/6/7`, `PSSI`, `PWVC`) by reading our exported `.EXT`/`.2EX`
  files covers nxs2 + CDJ-3000 features without per-tag protocol work. `[TM:1363-1369]`
- **String length semantics** (char-count prefix, ×2 bytes, big-endian, trailing NUL) and the
  redundant pre-blob length must be produced exactly or clients mis-parse. `[TM:106-108]`
- **Alternative to serving live:** Deep Symmetry's "Crate Digger" downloads & parses the whole
  USB database instead of using dbserver. Not relevant to *serving*, but confirms the data we'd
  need to expose lives in the rekordbox export (PDB + ANLZ). `[TM:9]` (see doc 03).

---

## Summary & gaps

**Summary.** The dbserver/remotedb protocol is a stateful, big-endian, type-tagged TCP message
protocol. A client (1) TCP-queries port 12523 with the 19-byte `00 00 00 0f "RemoteDBServer" 00`
magic to learn the dynamic dbserver port (always 1051 on CDJs); (2) connects there, exchanges a
4-byte `0x01` preamble, then sends an `Introduce` message (type `0x0000`, TxID `0xfffffffe`,
arg = its own player number 1–4) and gets back a `0x4000` carrying the server's player number; (3)
issues queries built from a common header (`magic 0x872349ae`, TxID, UInt16 type, UInt8 arg-count,
12-byte arg-type blob, args). Library browsing is a two-step setup→render dance: a menu/metadata
request (`0x1xxx`/`0x2002`) returns `0x4000` with an item count, then `0x3000` render
(offset/limit, paginate ≤64) streams `0x4001` header + N×`0x4101` items + `0x4201` footer; each
item is 12 args keyed by an item-type byte. Binary objects (artwork `0x2003`/`0x4002`, waveform
preview `0x2004`/`0x4402`, detail `0x2904`/`0x4a02`, nxs2/3000 analysis tags `0x2c04`/`0x4f02`,
beat grid `0x2204`/`0x4602`, cues `0x2104`/`0x4702`, extended cues `0x2b04`/`0x4e02`) are fetched
directly, each returning a length+blob with the empty-blob-omission rule. The whole format is
fully specified for the **client**; serving it requires re-implementing the server side, including
per-connection menu state, message-boundary framing, and mapping our rekordbox export into these
shapes.

**Gaps / open questions for implementation:**
1. **No reference server** — every "send 0 / unknown" arg slot is a server-side decision we must
   validate against real CDJ clients (arg 8 flag/column-config in items; render args 4 & 6; the
   `0x4f02` 5th arg).
2. **CDJ-3000 item-type high bytes** (§4.5) — meaning unknown; may be required when serving 3000s.
3. **Exact safe pagination limit** is hardware-dependent (64 documented safe on Nexus2, "hundreds"
   on XDJ-1000); for serving, we must decide our own max-per-render and how to signal overflow.
4. **Beat-grid trailing 8 bytes** and the cue-response final binary arg are undocumented `[TM:663,1174]`.
5. **rekordbox-vs-CDJ client framing differences** — we must frame by message length and test
   against both client types; behavior of real CDJs as *clients to us* is entirely untested in
   these repos.
6. **Streaming track types (`Tr=06`, Beatport LINK)** and CD audio (`Tr=05`) metadata paths are
   noted but only lightly documented; serving them is out of scope for a local library but worth
   noting for `Tr`/`Sr` validation.
