# Format coverage matrix

40 renderings of one source track — `6 SENSE - Mechanical Mania.mp3`, 7:05,
48 kHz stereo — spanning every audio format a CDJ-2000NXS claims to support.

**Each file's title tag and filename are its format**, so a track can be
identified from the deck's display alone. That is the point: two metadata
fields are still unattributed (`docs/FINDINGS.md` F31, F32) and reading them
requires knowing which file produced them.

Artist is `FORMAT TEST` on all 40 so they group together when browsing.

| Family | Variants | Rates |
|---|---|---|
| MP3 MPEG-1 | 32, 64, 128, 192, 256, 320 kbps | 44.1, 48 kHz |
| MP3 MPEG-2 | 16, 32, 64, 96, 128, 160 kbps | 22.05, 24 kHz |
| AAC (`.m4a`) | 16k/8k mono → 320k/44.1k stereo | 8–44.1 kHz |
| WAV | 16-bit, 24-bit | 44.1, 48 kHz |
| AIFF | 16-bit, 24-bit | 44.1, 48 kHz |

**MPEG-2 is at 22.05/24 kHz, not 44.1/48.** Layer III's 16–160 kbps range only
exists in the low-sampling-frequency extension; 44.1 and 48 kHz are MPEG-1 by
definition, so a "MPEG-2 at 48 kHz" file cannot be made.

AIFF needs `-write_id3v2 1`. Without it ffmpeg writes only AIFF's native
NAME/ANNO chunks, which carry the title but drop artist, BPM and key — the
files would then analyse differently from every other row purely because of the
container.

Regenerate with `tools/make-format-matrix.sh`. 1.0 GB total; the four 24-bit
lossless files are ~120 MB each.

## What to look for once rekordbox has analysed these

1. **`GET_TRACK_INFO` item 6, type `0x2f`** — carried `1` for the one MP3 we
   have ever seen. If it is a codec identifier it should differ across MP3 /
   AAC / WAV / AIFF. This is the field that most plausibly drives the
   "CDJ DOES NOT DECODE THIS FORMAT" message (F31).
2. **`GET_TRACK_INFO` item 1, type `0x04`** — also `1`, and `disc_number` was
   the only field of that track equal to 1. Every file here is disc 1, so this
   matrix cannot settle it; a track tagged disc 2 would.
3. **`PVBR`** — our `GET_VBR_INDEX` reply serves this tag, and it gates
   playback. Does rekordbox write it for CBR MP3? For AAC? For WAV and AIFF,
   which have no bitrate variation at all? If it is absent, we serve an empty
   blob and playback may fail for those formats.
4. **The metadata `bitrate` item (`0x10`)** — what does it report for 24-bit
   lossless?
