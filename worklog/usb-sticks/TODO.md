# USB sticks — open items

What is left from debugging the friends' sticks from the 2026-10-04 gig
(copies on BIGBOY2, `/Volumes/BIGBOY2/TriMixxx-USB-dumps`). Fixed so far: a
load no longer freezes the deck, only the loaded track is copied (no hover
pre-copy), the copy shares the stick with CDJs, and a streamed WAV/AIFF opens
without reading its end (fork `1e9bb12`, `d6960af`, `f61bcc8`).

Reproduce anything here on an emulated deck with the stick at a real stick's
speed: `pi-qemu deck stick NAME insert SANDISK-E02C`, then
`pi-qemu deck stick NAME speed SANDISK-E02C 4M` (a cheap USB 2 stick reads 3 to
5 MB/s), and `pi-qemu deck stick NAME reads SANDISK-E02C` for what the Pi read,
file by file.

## 1. The Pro DJ Link server reads every folder on a stick when it goes in

`Vfs::mount` (`mixxx/lib/prolink/crates/prolink/src/serve/vfs.rs`, called from
`graft()` in `serve/player.rs`) lists every directory and stats every file of
a new medium before it is served, to build the NFS tree. On the SanDisk copy
(1049 tracks, 2149 folders) that read 72 MB, almost all of it directory
clusters, and took ~30 s at 4 MB/s, ~48 s at 2 MB/s. A rekordbox stick has a
folder per track under `PIONEER/USBANLZ` plus `Contents/Artist/Album`, so it
grows with the library: about 70 MB per 1000 tracks. Meanwhile the stick is
saturated, right when a DJ starts browsing and loading, and "mounted a medium"
(when CDJs can see the stick) comes only at the end.

- **Fix:** a lazy tree. List a directory the first time a player looks into
  it (`LOOKUP` or `READDIR` on its handle). A CDJ only ever walks the paths the
  pdb gives it, so only those folders get read.
- **Watch for:** handles are hashes of paths, cut to 12 bytes by a CDJ (F28);
  the NFC/case-insensitive fallback in `lookup`; `preserve()` for a phantom
  medium; and the `files=` count in the mount log, which a lazy tree no longer
  knows up front.
- **Done when:** `stick reads` right after an insert shows no folder walk, and
  a CDJ (or a second emulated deck on the link) still browses and plays off it.

## 2. Three SoundSourceProxy tests fail in the test container

`mixxx-test` in the `unittest` Docker stage fails
`SoundSourceProxyTest.firstSoundTest`, `.freeModeGarbage` and
`.taglibStringToEnumFileType`, with or without the stick changes (checked by
reverting `f61bcc8`). They look like library versions in the container
(Debian trixie, FFmpeg 7.1.5), not deck code:

- `firstSoundTest`: the first audible frame of the M4A test files is off by
  two (1168, expected 1166).
- `freeModeGarbage`: `free_mode_garbage.mp3` will not open at all.
- `taglibStringToEnumFileType`: TagLib reports a file type Mixxx has no enum for.

- **Do:** check each against the library versions (upstream Mixxx's CI pins
  others), then fix the expectation, or skip the test in this build with the
  reason.
- **Done when:** `docker buildx build --target unittest --build-arg
  GTEST_FILTER='SoundSource*' ...` in `mixxx/` passes.
