# `ksy/` — the wire formats, as machine-checkable schemas

Kaitai Struct definitions for the Pro DJ Link protocol and the rekordbox
side-files, written from [`../docs/PROTOCOL.md`](../docs/PROTOCOL.md) and the
evidence in [`../docs/FINDINGS.md`](../docs/FINDINGS.md).

These are the source of truth for the **parse** direction in Mixxx. Run
[`regenerate.sh`](regenerate.sh) after editing one; it writes the C++ into
`../../mixxx/src/network/prolink/generated/` and the Python into
`../tests/generated/`, and **both are checked in**.

## Status

| Schema | Covers | Validated against |
|---|---|---|
| `prolink_djl.ksy` | UDP 50000 — discovery, claim chain, keep-alive | **7833 packets / 38 capture files**, all 8 observed types, zero field disagreements with `prolinks_poc.proto.djl` |
| `prolink_status.ksy` | UDP 50002 — status, media query, settings | *not written yet* |
| `prolink_dbserver.ksy` | TCP 1051 — the metadata protocol | **11809 messages**, both directions, zero disagreements with `prolinks_poc.proto.dbserver` — including the byte count consumed, so the framing is checked and not just the contents |
| `rekordbox_mysetting.ksy` | `PIONEER/*SETTING*.DAT` | *not written yet* |

`export.pdb` and the ANLZ files are **not** here: Mixxx already vendors
crate-digger's schemas at `mixxx/lib/rekordbox-metadata/`, and both sides of this
feature reuse them.

## Two constraints that shape every schema here

### Kaitai cannot generate C++ serializers

Writing is supported for the Java and Python targets only, and the runtime
vendored at `mixxx/lib/kaitai/` (0.11) has no `write_*` methods at all. So these
schemas give us readers; the **writers are hand-written** in
`mixxx/src/network/prolink/wire/` and their unit tests round-trip through the
generated readers. See `../research/10`, "Kaitai Struct: what it can and cannot
do here".

### Do not use `encoding:` for anything but ASCII

Mixxx compiles the Kaitai runtime with **`KS_STR_ENCODING_NONE`**
(`mixxx/CMakeLists.txt:2705`), under which `kstream::bytes_to_str` is a
pass-through that returns its input unchanged:

```cpp
std::string kaitai::kstream::bytes_to_str(const std::string src, const char *) {
    return src;   // KS_STR_ENCODING_NONE
}
```

For `encoding: ASCII` that is the identity function and therefore correct — which
is why `prolink_djl.ksy` uses it for the 20-byte device name.

**For UTF-16 it is silently wrong.** A field declared `encoding: UTF-16BE` would
come back as raw bytes with no error, no exception, and no truncation — it would
simply be mojibake for every non-ASCII string, and correct-looking for every
ASCII one. That is precisely the failure mode of O6, where a PioString read as
big-endian from the wrong offset round-tripped perfectly for ASCII and produced
`カガミ` as garbage, so a 692-track library parsed cleanly and the bug survived
three sessions.

So: **declare UTF-16 fields as byte arrays** (`size:`, no `type`, no `encoding`)
and decode them with `QString::fromUtf16` / `fromUtf8` in the calling C++. This
applies to the media name in the `0x06` media-query response (UTF-16**BE**, §3.3)
and to every dbserver string (UTF-16**BE**, counted in characters including the
NUL, §5.1) — note those are the opposite endianness to the NFS layer's UTF-16LE,
which is itself a trap worth not compounding.

## Provenance

Every non-obvious field carries its finding number in a `doc:` string, so the
question a maintainer asks first — "where did this magic number come from" — is
answered in place. The chain is: our capture → `FINDINGS.md` F*n* →
`PROTOCOL.md` → the `doc:` here → the generated parser.

**Licensed GPL-2.0-or-later**, authored from our own captures of our own
hardware. Not derived from any other project's schema. This matters:
`mixxx/lib/rekordbox-metadata/*.ksy` is EPL-1.0 (from Deep-Symmetry's
crate-digger) and predates us; nothing here may pick up that lineage, or the
combined work loses the GPLv2 option. See `../research/10`, "Licensing".
