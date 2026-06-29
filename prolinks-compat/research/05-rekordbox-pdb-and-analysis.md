# 05 — Rekordbox Export Database (export.pdb / DeviceSQL) and Track Analysis Files (ANLZ)

Research for **prolinks-compat** (CDJ ProLink compatibility). This document covers the
on-media data structures that hold a CDJ's library when read from USB/SD: the paged
DeviceSQL database `export.pdb`, and the tagged analysis files `ANLZxxxx.DAT` / `.EXT`.
These files underpin both the dbserver metadata responses and the NFS file-access paths
used by ProLink clients.

Markings: **(confirmed)** = directly read from a parser source file in `ref-repos/`;
**(inferred)** = deduced from field names/comments or cross-reference, not byte-validated here.

Primary sources read:
- `python-prodj-link/prodj/pdblib/*.py` (construct-based PDB + ANLZ parser)
- `python-prodj-link/prodj/data/pdbprovider.py`, `pdblib/pdbdatabase.py`
- `prolink-connect/src/localdb/{rekordbox.ts,schema.ts,orm.ts,index.ts}` (uses `rekordbox-parser`, a Kaitai port of Crate Digger)
- `dysentery/doc/Analysis.tex`, `dysentery/doc/modules/ROOT/pages/missing.adoc`

The database engine is **DeviceSQL**, a now-defunct embedded DB from Encirq (acquired by
Ubiquitous Corp, 2008). (confirmed: `dysentery/doc/Analysis.tex:4595`,
`missing.adoc:35`). Pioneer ships pre-built `.pdb` files; no live SQL engine is on the player.

---

## 1. On-media directory layout

Rekordbox writes its export under a top-level `PIONEER/` (FAT32/exFAT) **or** `.PIONEER/`
(HFS+, leading dot) directory. prolink-connect tries both because there is no way to know
the media filesystem type from the network. (confirmed: `prolink-connect/src/localdb/index.ts:194-203`;
python-prodj-link falls back from `/PIONEER/rekordbox/export.pdb` to
`/.PIONEER/rekordbox/export.pdb`, `pdbprovider.py:56-59`).

```
<media root>/
├── PIONEER/                         (or .PIONEER/ on HFS+ media)
│   ├── rekordbox/
│   │   ├── export.pdb               main DeviceSQL database (confirmed path)
│   │   ├── exportExt.pdb            extended DB (newer rekordbox; tags/myTags/cue comments) (inferred)
│   │   └── share.db                 (inferred; not referenced by either parser here)
│   └── USBANLZ/                     analysis files tree (inferred dir name; see note)
│       └── <hashed subdirs>/
│           └── <track>/
│               ├── ANLZ0000.DAT     beat grid, cues, preview + whole waveform (confirmed tags)
│               ├── ANLZ0000.EXT     color/HD waveforms, nxs2 cues, song structure (confirmed tags)
│               └── ANLZ0000.2EX     (newer; "EX2" form noted but unparsed) (inferred)
└── Contents/                        actual audio files (artist/album tree), e.g. .mp3/.flac
    └── ...                          + cover art .jpg files alongside (inferred)
```

Notes / caveats:
- The **exact per-track ANLZ directory path is not hardcoded** in either parser. It is read
  from the track row's `analyze_path` field (an absolute path string), and the player serves
  whatever path that string contains over NFS. (confirmed: `pdbprovider.py:105` uses
  `track.analyze_path`; `track.py:54`). The `USBANLZ` name is the conventional rekordbox layout
  but is not validated by the code here — **trust `analyze_path`, not a fixed dir**. (inferred)
- The companion `.EXT` is obtained by string-replacing `DAT`→`EXT` in `analyze_path`
  (python-prodj-link, `pdbprovider.py:92`) or by trimming the extension and re-appending
  `.DAT`/`.EXT` (prolink-connect, `rekordbox.ts:131`, which trims 4 chars at `:299`).
- Artwork image paths are likewise **data-driven**: the `block_artwork` table maps
  `artwork_id → path` (a PioString), and that path is fetched over NFS directly
  (confirmed: `artwork.py`, `pdbprovider.py:143-153`). Locations are typically `.jpg` thumbnails
  under the rekordbox/PIONEER tree (inferred).
- `exportExt.pdb` / `share.db` are **not parsed by either reference repo** — listed here as
  known-to-exist but out of scope for these parsers. (inferred)

---

## 2. export.pdb / DeviceSQL file format

The file is a fixed-page database. **Page size = 4096 bytes (0x1000)**, asserted as a
constant. (confirmed: `pdbfile.py:14`, `page.py:90`).

### 2.1 File header (page 0)
(confirmed: `pdbfile.py:5-25`)

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0x00 | 4 | (padding) | always 0 |
| 0x04 | 4 | `page_size` | const **4096** |
| 0x08 | 4 | `page_entries` (num table pointers) | usually **20** |
| 0x0C | 4 | `next_unused_page` | points "out of file"; even unreferenced |
| 0x10 | 4 | `unknown1` | seen (5,4,4,1,1,1…) |
| 0x14 | 4 | `sequence` | global seq, incremented by 1 (sometimes 2–3) per write |
| 0x18 | 4 | (padding) | always 0 |
| 0x1C | 16×`page_entries` | `entries[]` | array of `FileHeaderEntry` (16 B each) |
| … | — | (padding) | zero-fill to end of page 0 (length usually 348 → pad to 4096) |
| 0x1000.. | — | `pages[]` | the data pages (GreedyRange of `AlignedPage`) |

**FileHeaderEntry (table pointer), 16 bytes** (one per table type): (confirmed `pdbfile.py:5-10`)

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0 | 4 | `page_type` | `PageTypeEnum` (table type, see 2.3) |
| 4 | 4 | `empty_candidate` | |
| 8 | 4 | `first_page` | points to a "strange" page that then links to the first real data page |
| 12 | 4 | `last_page` | |

So the header is effectively a list of (table_type → page chain) pointers. Each table's
pages are reached by following `first_page` then `next_index` links.

### 2.2 Data page header (`AlignedPage`, 40-byte header)
(confirmed: `page.py:62-92`)

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0x00 | 4 | (padding) | always 0 |
| 0x04 | 4 | `index` | this page's index (in units of 4096 B) |
| 0x08 | 4 | `page_type` | `PageTypeEnum` |
| 0x0C | 4 | `next_index` | next page in chain (4096-B units); ends at an empty page (may be past EOF) |
| 0x10 | 4 | `u1` | per-table sequence number |
| 0x14 | 4 | (padding) | |
| 0x18 | 1 | `entry_count_small` | row count (low) |
| 0x19 | 1 | `u3` | bitmask (1st track: 32) |
| 0x1A | 1 | `u4` | often 0; larger for dense pages |
| 0x1B | 1 | `u5` | flags; bit 0x40 set ⇒ "strange" page |
| 0x1C | 2 | `free_size` | free bytes (excluding tail data) |
| 0x1E | 2 | `payload_size` | |
| 0x20 | 2 | `overridden_entries` | rows overriding earlier blocks (ignore if 8191) |
| 0x22 | 2 | `entry_count_large` | used when > small and != 8191 (artwork, playlist_map) |
| 0x24 | 2 | `u9` | 1004 for strange pages, else 0 |
| 0x26 | 2 | `u10` | 0 except 1 for synchistory |
| 0x28 | — | row data ... | `entries_start` = here; rows grow forward |

Derived flags (confirmed `page.py:80-85`):
- `is_strange_page = (index != 0) && (u5 & 0x40)`
- `is_empty_page   = (index == 0) && (u9 == 0)`
- `entry_count = entry_count_large` if `small < large` AND not strange/empty AND `large != 8191`,
  else `entry_count_small`. The 8191 sentinel and the large/small mismatch are real-world
  quirks (artwork & playlist_map pages have far more rows than `entry_count_small` claims).

### 2.3 Page/table type enum (`PageTypeEnum`, Int32ul)
(confirmed: `pagetype.py`)

| Value | python-prodj name | prolink/Crate Digger name | Parsed? |
|------:|-------------------|---------------------------|---------|
| 0 | `block_tracks` | TRACKS | yes |
| 1 | `block_genres` | GENRES | yes |
| 2 | `block_artists` | ARTISTS | yes |
| 3 | `block_albums` | ALBUMS | yes |
| 4 | `block_labels` | LABELS | yes |
| 5 | `block_keys` | KEYS | yes |
| 6 | `block_colors` | COLORS | yes |
| 7 | `block_playlists` | PLAYLIST_TREE | yes |
| 8 | `block_playlist_map` | PLAYLIST_ENTRIES | yes |
| 9 | `block_unknown4` | (unknown) | no |
| 10 | `block_unknown5` | (unknown) | no |
| 11 | `block_unknown6` | (unknown) | no |
| 12 | `block_unknown7` | (unknown) | no |
| 13 | `block_artwork` | ARTWORK | yes |
| 14 | `block_unknown8` | (unknown) | no |
| 15 | `block_unknown9` | (unknown) | no |
| 16 | `block_columns` | COLUMNS | no (UI column defs) |
| 17 | `block_unknown1` | (unknown) | no |
| 18 | `block_unknown2` | (unknown) | no |
| 19 | `block_synchistory` | HISTORY | no |

prolink-connect's table→entity mapping confirms the names: TRACKS, ARTISTS, GENRES, ALBUMS,
LABELS, COLORS, KEYS, ARTWORK, PLAYLIST_TREE, PLAYLIST_ENTRIES (`rekordbox.ts:405-435`).
HISTORY is noted as a TODO (`rekordbox.ts:434`).

### 2.4 Row layout within a page: reverse index + presence bitmask

Rows are stored **forward from `entries_start`**, but their offsets are stored in a
**reverse index at the end of the page**, in groups of up to 16. (confirmed: `page.py:28-60`)

- The page footer (`PageFooter`) is read from `page_start + 4096`, then seeks backwards.
- `ReverseIndexArray` (one group of ≤16 rows): for each entry there is a 2-byte
  `entry_offset` (relative to `entries_start`) pointing at the row struct
  (`ReverseIndexedEntry`, `page.py:28-45`).
- **Row presence bitmask:** two 16-bit fields per group, bit-swapped:
  - `entry_enabled` — 16 flags (bit set ⇒ row slot is live) (confirmed `page.py:55`)
  - `entry_enabled_override` — 16 flags (confirmed `page.py:56`)
  Layout: `[16×2B offsets][2B entry_enabled][2B entry_enabled_override]` per group,
  i.e. 36 bytes for a full 16-entry group (the `Seek(-36 …)` confirms, `page.py:57`).
- A known wart: `entry_enabled` for the **last** group reports nonexistent entries; the
  parser zips `reversed(entries)` with `reversed(entry_enabled)` and skips disabled slots
  when collecting rows. (confirmed: `page.py:47-49`, `pdbdatabase.py:75-79`).

To enumerate live rows of a table: walk the page chain for that `page_type`, and for each
page iterate the reverse-index groups, taking entries where `entry_enabled` is true.
(confirmed: `pdbdatabase.py:72-80`).

### 2.5 "Strange" pages
Every table chain begins with a "strange" page (`u5 & 0x40`, `u9 == 1004`) filled with the
constant `0x3fffffff` / `0xf8ffff1f` patterns; it just links to the first real data page.
(confirmed: `page.py:14-26`). The header's `first_page` points at this page.

---

## 3. Track row layout (`block_tracks`)

Magic byte `0x24` marks a track row (`TRACK_ENTRY_MAGIC`). All integers little-endian.
(confirmed: `track.py`). The fixed part is followed by a 21-entry string-offset table, then
the PioStrings themselves.

### 3.1 Fixed fields

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0x00 | 2 | `magic` | const **0x24** |
| 0x02 | 2 | `index_shift` | page index << 5 (0x00, 0x20, 0x40 …) |
| 0x04 | 4 | `bitmask` | flags (unparsed) |
| 0x08 | 4 | `sample_rate` | Hz |
| 0x0C | 4 | `composer_index` | artist id (composer) |
| 0x10 | 4 | `file_size` | bytes |
| 0x14 | 4 | `u1` | some id? |
| 0x18 | 2 | `u2` | "always 19048?" |
| 0x1A | 2 | `u3` | "always 30967?" |
| 0x1C | 4 | `artwork_id` | → `block_artwork` |
| 0x20 | 4 | `key_id` | → `block_keys` (parser: "not sure") |
| 0x24 | 4 | `original_artist_id` | → `block_artists` |
| 0x28 | 4 | `label_id` | → `block_labels` |
| 0x2C | 4 | `remixer_id` | → `block_artists` |
| 0x30 | 4 | `bitrate` | kbps |
| 0x34 | 4 | `track_number` | |
| 0x38 | 4 | `bpm_100` | tempo × 100 (also called `tempo`; rekordbox.ts divides by 100) |
| 0x3C | 4 | `genre_id` | → `block_genres` |
| 0x40 | 4 | `album_id` | → `block_albums` (album-artist stored on the album row) |
| 0x44 | 4 | `artist_id` | → `block_artists` |
| 0x48 | 4 | `id` | **rekordbox track id** (the key used everywhere) |
| 0x4C | 2 | `disc_number` | |
| 0x4E | 2 | `play_count` | |
| 0x50 | 2 | `year` | |
| 0x52 | 2 | `sample_depth` | bits ("not sure") |
| 0x54 | 2 | `duration` | seconds |
| 0x56 | 2 | `u4` | "always 41?" |
| 0x58 | 1 | `color_id` | → `block_colors` (1-byte!) |
| 0x59 | 1 | `rating` | 0–5 |
| 0x5A | 2 | `u5` | default 1 |
| 0x5C | 2 | `u6` | alternating 2/3 |
| 0x5E | 2×21 | `str_idx[21]` | offset table (each = byte offset from `entry_start` to a PioString) |

(confirmed offsets derived by summing field sizes in `track.py:6-39`.)

### 3.2 String offset table → PioStrings

`str_idx[i]` is a 16-bit offset from the row's `entry_start` to the i-th PioString.
`IndexedPioString(i)` dereferences `entry_start + str_idx[i]`. (confirmed: `piostring.py:26-27`,
`track.py:39-60`). The 21 indexed strings:

| idx | python-prodj field | prolink-connect field | Notes |
|----:|--------------------|-----------------------|-------|
| 0 | `str_u1` | — | empty |
| 1 | `texter` | — | |
| 2 | `str_u2` | — | (not track number — comment says guess was wrong) |
| 3 | `str_u3` | — | often empty / low binary 0x01/0x02 |
| 4 | `str_u4` | — | often empty / low binary |
| 5 | `message` | — | |
| 6 | `kuvo_public` | `kuvoPublic` | "ON" or empty |
| 7 | `autoload_hotcues` | `autoloadHotcues` | "ON" or empty |
| 8 | `str_u5` | — | |
| 9 | `str_u6` | — | empty |
| 10 | `date_added` | `dateAdded` | date string |
| 11 | `release_date` | `releaseDate` | |
| 12 | `mix_name` | `mixName` | |
| 13 | `str_u7` | — | empty |
| 14 | `analyze_path` | `analyzePath` | **path to ANLZ .DAT** (drives NFS fetch) |
| 15 | `analyze_date` | `analyzeDate` | |
| 16 | `comment` | `comment` | |
| 17 | `title` | `title` | |
| 18 | `str_u8` | — | always empty; newer rekordbox only |
| 19 | `filename` | `fileName` | base filename |
| 20 | `path` | `filePath` | **full path to the audio file** (mount path) |

(confirmed: `track.py:40-60`; field names cross-checked in `rekordbox.ts:261-317`.)

Key takeaways for our use: a track's audio is at `path` (idx 20), its analysis at
`analyze_path` (idx 14, with `.EXT` derived by extension swap), and its artwork via
`artwork_id → block_artwork.path`.

### 3.3 Other table rows (all little-endian)

- **Artist** (`artist.py`): magic `0x60` (short) or `0x64` (long). `[magic:2][index_shift:2][id:4]`
  then for long form a 2-byte `unknown` + 2-byte `name_idx`; short form uses 1-byte each.
  `name` is an `OffsetPioString(name_idx)`. (confirmed)
- **Album** (`album.py`): magic `0x80`. `[magic:2][index_shift:2][pad:4][album_artist_id:4][id:4][pad:4][unknown:1][name_idx:1][name]`. (confirmed)
- **Genre** (`genre.py`): `[id:4][name:PioString]`. (confirmed)
- **Key** (`key.py`): `[id:4][id2:4 (dup of id)][name:PioString]`. (confirmed)
- **Label** (`label.py`): `[id:4][name:PioString]`. (confirmed)
- **Color** (`color.py`): `[pad:4][id_dup:1][id:1][pad:2][name:PioString]` — note **1-byte id** matching the track's 1-byte `color_id`. (confirmed)
- **Artwork** (`artwork.py`): `[id:4][path:PioString]`. (confirmed)
- **Playlist tree** (`playlist.py`): `[folder_id:4][pad:4][sort_order:4][id:4][is_folder:4][name:PioString]`; `folder_id`=0 is root, `is_folder`=1 ⇒ folder. (confirmed)
- **Playlist map / entries** (`playlist_map.py`): `[entry_index:4][track_id:4][playlist_id:4]` — ordering via `entry_index`. (confirmed)

Playlist resolution: filter `playlist_map` by `playlist_id`, sort by `entry_index`, then
join to tracks by `track_id`. (confirmed: `pdbdatabase.py:66-70`).

---

## 4. PioString encoding

Variable-length string with a 1-byte leading length/flag selector. (confirmed: `piostring.py:3-19`)

`[length_byte][ ... ]`

Three cases keyed on the first byte:

1. **Short ASCII (default case):** the first byte is an odd-ish packed length. The actual
   text length is `(length_byte - 1) // 2 - 1` bytes of ASCII, read inline immediately after.
   (confirmed `piostring.py:15-18`). So a byte value `L` encodes `(L-1)/2 - 1` chars; the LSB
   acts as a flag and the value is roughly `2*len + 3`. Used for strings ≤ ~127 bytes.

2. **Long ASCII — selector `0x40`:** followed by a 2-byte little-endian length field where
   `actual_length = stored - 4` (the adapter adds/subtracts 4), then 1 padding byte, then
   `actual_length` bytes of ASCII. (confirmed `piostring.py:7-10`). Used when text > 127 bytes.

3. **UTF-16 — selector `0x90`:** followed by a 2-byte little-endian length (`actual_length =
   stored - 4`), then the text as **UTF-16 big-endian** (`utf-16-be`). (confirmed `piostring.py:11-14`).
   Used for non-ASCII content (e.g. CJK, accented titles).

Helpers: `OffsetPioString(idx)` reads at `entry_start + idx`; `IndexedPioString(i)` reads at
`entry_start + str_idx[i]`. (confirmed `piostring.py:22-27`).

When generating a .pdb ourselves, the safe encoding is: ASCII strings via case 1 (compute
`length_byte = 2*(len+1)+1`), switching to UTF-16-BE (case 3) when any character is non-ASCII.
(inferred from the decode logic.)

---

## 5. ANLZ files (`ANLZ0000.DAT` / `.EXT`)

Tagged ("chunked") binary format, **all big-endian**. Header magic `PMAI`. Each section is a
`PMxx`/`Pxxx` tag with its own size. (confirmed: `usbanlz.py`). The original format reference is
the reverse-engineering writeup cited in `usbanlz.py:3`; dysentery/Crate Digger document the
fuller set (PSSI etc.).

### 5.1 File header (`AnlzFile`, `PMAI`)
(confirmed `usbanlz.py:144-154`)

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0x00 | 4 | `type` | const "PMAI" (ASCII) |
| 0x04 | 4 | `head_size` | header length (Int32 BE) |
| 0x08 | 4 | `file_size` | total file size |
| 0x0C | 16 | `u1..u4` | 4 unknown Int32 |
| 0x1C.. | — | `tags[]` | GreedyRange of `AnlzTag` |

### 5.2 Tag header (`AnlzTag`)
(confirmed `usbanlz.py:126-142`)

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | `type` (4-char ASCII fourcc) |
| 4 | 4 | `head_size` |
| 8 | 4 | `tag_size` |
| 12.. | — | `content` (switch on `type`; default = skip `tag_size-12` bytes) |

### 5.3 Tag table

| FourCC | Content struct | File | Meaning | Parsed here? |
|--------|----------------|------|---------|--------------|
| `PMAI` | (file header) | both | File magic / root | yes |
| `PPTH` | `AnlzTagPath` | both | Track file path (UTF-16-BE, `payload_size-2` bytes) | yes (confirmed `usbanlz.py:5-9`) |
| `PVBR` | `AnlzTagVbr` | DAT | VBR seek index: 400× Int32 + 1 | yes (confirmed `usbanlz.py:11-15`) |
| `PQTZ` | `AnlzTagQuantize` | DAT | **Beat grid**: pad4, const `0x80000`, then `PrefixedArray(Int32, tick)` | yes (confirmed `usbanlz.py:23-27`) |
| `PQT2` | `AnlzTagQuantize2` | EXT | Extended beat grid (nxs2): 2 bpm objects + entry_count | yes (confirmed `usbanlz.py:29-40`) |
| `PWAV` | `AnlzTagWaveform` | DAT | **Preview waveform** (small): const `0x10000`, `payload_size`×Int8 | yes (confirmed `usbanlz.py:42-46`) |
| `PWV2` | `AnlzTagWaveform` | DAT | Tiny preview waveform | yes (confirmed `usbanlz.py:137`) |
| `PWV3` | `AnlzTagBigWaveform` | EXT | **Whole / detailed waveform** (scroll): const `0x960000`, `payload_size`×Int8 | yes (confirmed `usbanlz.py:47-52,138`) |
| `PWV4` | `AnlzTagColorWaveform` | EXT | **Color preview waveform**: word_size 6, `6*payload_size`×Int8s | yes (confirmed `usbanlz.py:53-58,139`) |
| `PWV5` | `AnlzTagColorBigWaveform` | EXT | **Color detailed/HD waveform**: word_size 2, `payload_size`×Int16 | yes (confirmed `usbanlz.py:59-64,140`) |
| `PCOB` | `AnlzTagCueObject` | both | **Cue/loop list** (memory + hotcues), array of `PCPT` | yes (confirmed `usbanlz.py:100-105,136`) |
| `PCO2` | `AnlzTagCueObject2` | EXT | **nxs2 cue list** (extended), array of `PCP2` | yes (confirmed `usbanlz.py:119-124,141`) |
| `PSSI` | — (not implemented) | EXT | **Song structure** (phrase analysis, mood) | no — falls to default skip (inferred; documented by Crate Digger / `rekordbox.ts:158`) |

(Note: `PWV2` is mapped to the same `AnlzTagWaveform` struct as `PWAV` in `usbanlz.py:137`.)

### 5.4 Selected content structures

**Beat grid tick** (`AnlzQuantizeTick`, `usbanlz.py:17-21`):
`[beat:2 (1..4)][bpm_100:2][time:4 (ms from start)]`. prolink-connect exposes each as
`{offset:time, bpm:tempo/100, count:beatNumber}` (`rekordbox.ts:365-371`).

**Cue point** (`AnlzCuePoint` = `PCPT`, `usbanlz.py:78-93`):
`[type "PCPT":4][head_size:4][tag_size:4][hotcue_number:4 (0=memory)][status (0=disabled,4=enabled):4][const 0x10000:4][order_first:2][order_last:2][type (1=single,2=loop):1][pad:1][const 1000:2][time:4][time_end:4 (default -1)][pad:16]`.
Cue object wrapper (`PCOB`): `[type (0=memory,1=hotcue):4][count:4][memory_count:4][entries…]`.

**nxs2 cue point** (`AnlzCuePoint2` = `PCP2`, `usbanlz.py:107-117`):
`[type "PCP2":4][head_size:4][tag_size:4][hotcue_number:4][u2:4][time:4][time_end:4][u1:4][pad:56]`.

**Waveforms** — preview is unpacked by python-prodj as `(line & 0x1f, line >> 5)` i.e. lower 5
bits = height, upper 3 bits = whiteness/color band (confirmed `pdbprovider.py:168`). Color
detailed waveform (`PWV5`) is Int16 words. (confirmed `usbanlz.py:63`).

### 5.5 What each consumer extracts
- python-prodj DAT: `PWAV→preview_waveform`, `PCOB→cue_points`, `PQTZ→beatgrid`
  (confirmed `usbanlzdatabase.py:54-59`).
- python-prodj EXT: `PWV3→waveform`, `PWV4→color_preview_waveform`, `PWV5→color_waveform`
  (confirmed `usbanlzdatabase.py:61-68`).
- prolink-connect DAT: `BEAT_GRID`, `CUES`; EXT: `WAVE_COLOR_SCROLL` (HD waveform).
  CUES_2, SONG_STRUCTURE, WAVE_PREVIEW, WAVE_SCROLL, WAVE_COLOR_PREVIEW are explicitly
  noted as **not yet extracted** (confirmed `rekordbox.ts:140-162`).

---

## 6. Relationship to the live ProLink protocol (and objective #2)

There are two ways a ProLink client gets a player's library; both ultimately derive from these
exact files:

1. **dbserver (remotedb, TCP):** the player runs a metadata server that answers structured
   queries (track metadata, menus, waveforms, beat grids, cues). The server's answers are
   produced *from* `export.pdb` + the ANLZ files. python-prodj's `PDBProvider` is a drop-in that
   reproduces the dbserver semantics purely by parsing the downloaded `.pdb`/ANLZ
   (see `pdbprovider.handle_request` mapping `metadata`, `title`, `artist`, `album`, `genre`,
   `playlist`, `artwork`, `waveform`, `beatgrid`, etc., `pdbprovider.py:361-407`).

2. **NFS file access (UDP RPC):** the player exports its media over NFS. A client can simply
   **download `export.pdb`, parse it locally, then NFS-fetch each ANLZ/audio/artwork file by the
   paths stored in the rows.** This is what both reference implementations do:
   - python-prodj: `nfs.enqueue_download(ip, slot, "/PIONEER/rekordbox/export.pdb", …)`
     (`pdbprovider.py:56`), then per-track `analyze_path` (+EXT) and `artwork.path` via NFS
     (`pdbprovider.py:91-92,153`).
   - prolink-connect: `fetchFile({device, slot, path:'PIONEER/rekordbox/export.pdb'})` then
     `hydrateDatabase` into an in-memory SQLite mirror, with ANLZ resolved lazily via an
     `AnlzResolver` that reads over NFS (`localdb/index.ts:184-217`, `rekordbox.ts:39,126-165`).

   prolink-connect's local mirror schema (`schema.ts`) is a clean target reference for the
   *minimum* fields a consumer cares about: track (id, title, duration, bitrate, tempo, rating,
   comment, file_path, file_name, sample_rate/depth, play_count, year, mix_name, autoload_hotcues,
   kuvo_public, file_size, analyze_path, release_date, analyze_date, date_added + FKs to
   artist/album/genre/color/label/key/artwork/original_artist/remixer/composer), plus
   artist/album/genre/color/label/key/artwork (id+name/path), playlist (id, is_folder, name,
   parent_id), playlist_entry (id, sort_index, playlist_id, track_id).

**Implication for objective #2 (serving our own library from Mixxx):** to look like a real CDJ
to ProLink clients we must **generate a valid `export.pdb` plus per-track `ANLZ0000.DAT/.EXT`**
from Mixxx's SQLite library and expose them over NFS (and/or answer dbserver queries from the
same in-memory model). The pdb generator must reproduce: 4096-byte paged layout, the strange→data
page chaining, the reverse-index + `entry_enabled` bitmask, the track row offset table + PioStrings,
and consistent id references across tables. The ANLZ generator must emit at minimum `PMAI` +
`PPTH` + `PQTZ` (beat grid) + `PWAV`/`PWV2` (preview) in `.DAT`, and `PWV3`/`PWV5` (waveforms) +
`PCO2` (cues) + optionally `PSSI` in `.EXT`. Mixxx already has beatgrids, cues, and can compute
waveforms, so the mapping is tractable; the hard part is byte-exact pdb paging and PioString
encoding.

---

## Summary

The rekordbox export library on USB/SD lives under `PIONEER/` (or `.PIONEER/` on HFS+) as a
single paged DeviceSQL file `PIONEER/rekordbox/export.pdb` (4096-byte pages, a 16-byte
table-pointer header, page chains per table type, rows addressed by an end-of-page reverse
index plus a 16-bit `entry_enabled` presence bitmask), accompanied by per-track tagged analysis
files `ANLZ0000.DAT`/`.EXT` (big-endian `PMAI` container with `PQTZ` beat grids, `PWAV/PWV2/PWV3/
PWV4/PWV5` waveforms, `PCOB/PCO2` cues, `PPTH` path, `PSSI` song structure) whose exact location
is read from each track row's `analyze_path` string. Track rows carry fixed numeric fields
(ids, sample_rate, file_size, bpm×100, duration, FK ids to artist/album/genre/key/color/label/
artwork) followed by a 21-entry offset table into variable-length **PioStrings** (short-ASCII /
0x40 long-ASCII / 0x90 UTF-16-BE). Both reference parsers (python-prodj-link's construct parser
and prolink-connect's Kaitai-based `rekordbox-parser`) confirm every offset and enum value above.
The live ProLink protocol's dbserver responses are derived from exactly these files, and a client
can bypass dbserver entirely by NFS-downloading `export.pdb` + ANLZ + audio/artwork directly — so
for objective #2 we will generate and serve our own `export.pdb` + ANLZ from Mixxx's library.

## Gaps / open questions
- **pdb write path is unspecified here.** Both repos only *read* `.pdb`. Byte-exact generation
  (page allocation order, `sequence`/`u1` counters, the 8191 sentinel rules, exact
  `entry_enabled` semantics, padding) is not covered by these sources — needs validation against
  a real player or rekordbox export, ideally via Crate Digger / `crate-digger`'s writer or
  Deep-Symmetry docs.
- **PioString short-form length math** (`(L-1)//2-1`) is confirmed for *decode*; the exact
  *encode* rule (and the LSB flag meaning) should be byte-verified before generating strings.
- **`PSSI` song-structure layout** is not implemented in either parser (skipped). If we need to
  emit/serve phrase data, consult dysentery/Crate Digger directly.
- **`exportExt.pdb` and `share.db`** are not parsed by either repo — schema unknown from these
  sources. Newer rekordbox stores My-Tags/extended cue comments there; relevance to CDJ playback
  TBD.
- **ANLZ directory naming (`USBANLZ`) and artwork `.jpg` locations** are inferred conventions;
  the code is path-driven via `analyze_path` / `artwork.path`, so confirm actual on-disk layout
  from a real export.
- **`.2EX`/"EX2" analysis variant** is mentioned (`rekordbox.ts:295`) but unparsed — format unknown.
- Several track fields remain unknown (`bitmask`, `u1..u6`, `str_u1..u8`); `key_id`/`sample_depth`
  are flagged "not sure" in the parser. Validate before relying on them.
