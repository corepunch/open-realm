# Browser ISO 9660 reader (Warcraft III disc import, patches 1–6)

## Scope and ownership

`port/web/site/iso9660.js` is a standalone, dependency-free file *indexer* for
2048-byte-sector ISO 9660 CD images. It uses `Blob.slice()` and `arrayBuffer()`
to inspect descriptors and bounded directory pages. It never reads an entire ISO
into memory, unpacks an MPQ, accesses OPFS or imports files automatically.

Its reference is the repository's native `tools/isoextract.c`. Both use the
primary volume descriptor and prefer a recognized Joliet supplementary volume
descriptor for filenames, strip `;N` file versions, and ignore the `.` and `..`
directory records. The browser reader additionally validates both byte-order
copies of on-disc integers, checks extents against the Blob size, limits
traversal, detects directory loops and normalized path collisions, and rejects
unsupported interleaved/multi-extent records rather than returning partial data.

Patch 2 connects this module to `site/install-worker.js` and patch 3 adds
composition of up to eight ISO images and directly selected MPQ/W3M/W3X files
in a **single** import. Each ISO is scanned for the recognized game MPQs and
only their source byte ranges are copied through the existing 1 MiB OPFS
writer. Patch 5 also detects `Setup.mpq` for a conservative, known-map
extraction profile; movies, installer executables and arbitrary MPQs are
not unpacked. Archive names are matched
case-insensitively; repeated archive names within one ISO are ambiguous, and
conflicting names across ISOs or direct uploads are rejected, with the source
names reported. The importer does not guess which duplicate is newer.

The *combined* selection must contain `War3.mpq` (from an ISO or an upload).
A TFT ISO without it can be combined with the RoC ISO in the same operation.
An expansion-only selection is rejected rather than creating an unlaunchable
manifest. A subsequent import still **replaces**, rather than augments, the
active manifest. To install RoC and TFT, select both together; patch 3 does not
merge a newly selected ISO with the previous installation. Patch 4 exposes ISO
selection in both browser pages.

Before creating a new generation, the worker validates each source's MPQ
header and checks `navigator.storage.estimate()` when available. An estimate
showing insufficient remaining quota rejects the import without replacing the
previous generation. Missing, denied or invalid estimates do not block the
operation, because quotas are advisory and actual OPFS writes can still fail.
The existing staged-generation cleanup protects the prior manifest on copy
errors and cancellation. Already-committed generations are not deleted by
normal imports: they may be in use by a running engine, so their bytes still
count against storage usage. A user-triggered persistent-storage request is a
follow-up UI consideration, not an automatic permission request in the worker.
The worker prevents overlapping imports within one page, but does not
coordinate commits across multiple open browser tabs. Do not install from two
tabs concurrently; cross-tab locking and recovery need separate work.

`install-worker.js` accepts the existing `postMessage({type:'import',files})`
message, where `files` is an ordered array containing ISO images, direct game
archives, or both (maximum 512 input files and eight ISO images). The worker
now produces a version-3 generation manifest, which also supports
nested `Maps/...` paths. `filesystem-bridge.js` validates these paths and
reads their OPFS entries; native MPQ loading does not change. Name matching is
case-insensitive and traverses ISO subdirectories, but repeated candidate names
are rejected as ambiguous. A shared MPQ structure validator checks archive size and table boundaries;
this does not prove the disc archives are complete or playable.

## Usage from a browser worker

Load with `importScripts('iso9660.js')` in the installer worker (the
experimental engine build now packages it). The API is also exported via
CommonJS to allow dependency-free Node.js tests.

```js
const controller = new AbortController();
const iso = await ISO9660.openISO(file, {signal: controller.signal});
const entries = await iso.listFiles();
const archives = await iso.findFiles('War3.mpq');

// The worker uses this zero-copy slice with the existing OPFS writer.
for (const entry of archives) {
  const bytes = file.slice(entry.offset, entry.offset + entry.size);
  // Consume bytes without copying the whole disc.
}
```

`listFiles()` returns an immutable array of immutable
`{path, name, offset, size}` entries representing **regular files only**.
`path` is root-relative with `/` separators and no ISO `;N` version suffix.
`offset` and `size` are bytes and identify the exact on-image data range.
`findFiles(name)` matches basename at any directory depth; supply a
slash-qualified relative path for an exact path match. Searches are
case-insensitive. A complete scan is cached for later queries. Abort the
supplied signal to reject in-flight operations with an `AbortError`.

A malformed or unsupported input raises `ISO9660Error` with the reason.
In particular, a parse failure must not be mistaken for an empty disc.

## On-disk fields and constraints

| Field | Offset | Contract |
|---|---:|---|
| Volume descriptor sequence | sector 16 onward | `CD001`, descriptor version 1, type 1 primary, type 2 supplementary, type 255 terminator |
| Joliet escape sequence | descriptor byte 88 | `%/@`, `%/C`, or `%/E` |
| Logical block size | descriptor byte 128 | both-endian 16-bit number, must be 2048 |
| Root directory record | descriptor byte 156 | 34+ byte directory record |
| Directory record length | record byte 0 | nonzero records must be wholly inside one logical sector |
| Extended attribute length | record byte 1 | blocks preceding content data |
| Extent LBA | record byte 2 | both-endian 32-bit number |
| Content length | record byte 10 | both-endian 32-bit number |
| Flags | record byte 25 | bit 1 directory, bit 2 associated, bit 7 multi-extent |
| Name length / identifier | record byte 32 / 33 | ISO single-byte or Joliet UCS-2 big-endian |

Directory zero-length records indicate padding to the next 2048-byte sector.
The reader loads directory pages of at most 256 KiB and limits directory size
to 64 MiB, depth to 32 levels, directories to 10,000 and normalized paths to
100,000. These are protection limits, not retail Warcraft III constraints.
It rejects cross-sector directory records, multi-extent and interleaved file
layouts, non-2048 logical sector sizes, truncated extents, invalid Joliet
surrogates, unsafe dot path components, and repeated directory extents.
Characters that are unsafe in target filesystem names (`/`, `\\`, `:`, control
characters) are replaced with `_`, consistent with `tools/isoextract.c`.
Normalized duplicate paths (case-insensitive) are errors rather than silently
overwriting an archive.

## Verification

Run the synthetic parser tests separately from the native game suite:

```sh
node --test port/web/tests/iso9660.test.cjs
```

The tests generate ISO directory and volume-descriptor bytes in memory. They
cover primary/Joliet selection, nested files, sector padding, byte extents,
filename normalization, lookup, cancellation, corrupt records, cycles and
unsupported encodings. They use no proprietary media or installed WC3 data.

These patches do not verify a retail Reign of Chaos or Frozen Throne image,
real-disc `Setup.mpq` extraction, a real browser import or final game launch. Testing real
discs and launching the experimental engine are follow-up tasks.

See also [Browser platform](browser.md) for OPFS installer ownership, runtime
mounting and experimental engine build constraints.

## Patch 2 verification

Synthetic worker-level tests: `node --test port/web/tests/iso-import.test.cjs`.
Parser tests: `node --test port/web/tests/iso9660.test.cjs`.
The worker tests exercise recognized/canonical names, ignored installer
containers, source-byte copying, unchanged standalone MPQ imports, rejection
of duplicate/invalid/expansion-only images, and cancellation preserving the
previous OPFS generation. They use no retail data.

## Patch 3 verification

Run `node --test port/web/tests/iso-import.test.cjs` for worker-level synthetic
fixtures covering combined RoC/TFT ISOs, mixed ISO and standalone MPQs in
either order, duplicate conflicts across sources, source count limits, quota
preflight, optional estimate failures and write-failure rollback. The existing
single-ISO, direct-MPQ and cancellation cases remain in the same file. These
tests do not require a retail Warcraft III disc.

## Patch 4 browser UI and packaging

Both `port/web/site/engine-shell.html` and `port/web/site/shell.html`
accept `.iso,.mpq,.w3m,.w3x` as multi-file input. Select the original
Reign of Chaos and Frozen Throne images **together** when both are needed;
each import creates a replacement manifest and does not merge with an existing
installation. Images are scanned within `install-worker.js`, not on the UI
thread, and only recognized archives from each ISO are mounted. Patch 5
extracts a known subset of `Setup.mpq` maps. An `.iso` filename containing a raw-sector BIN image does
not make it a supported ISO 9660 image.

The worker reports `phase` events (`scanning` with a disc `source`,
`extracting`, `validating`, `storage`, `copying`, `committing`), one `discovered` event after
MPQ-header validation, and the existing `progress`, `done`, `list` and `error`
events. Each discovered entry has `name`, `size` and `source`; `bytes` is the
combined copy size, not the selected ISO size. `progress` retains its existing
`copied`, `total` and `file` fields, plus `index`, `count`, `fileCopied` and
`fileSize`. The UI renders these values as plain text, never HTML. Before
copy size is known, the progress bar is indeterminate. The final manifest
write occurs after copying, so a 100% copy indication is not success until
`done` arrives. Cancelling an import and failed writes preserve the previous
manifest through the worker's staged-generation cleanup.

The experimental engine build already copies `installer.js`, `install-worker.js`
and `iso9660.js` in `port/web/build-engine.py`; no change to that target was
needed. Patch 4 adds those files as dependencies and copies them into the
`build/web/` foundation output from `port/web/build.mk`; its `shell.html`
previously referenced `installer.js` without packaging it.

Run focused tests:

```sh
node --test port/web/tests/iso9660.test.cjs port/web/tests/iso-import.test.cjs port/web/tests/installer-ui.test.cjs
```

`installer-ui.test.cjs` exercises actual browser handler code with a synthetic
DOM and Worker harness; it does not establish browser CSS/layout, browser
storage quotas, or retail ISO compatibility. The engine remains experimental.

## Patch 5 — original RoC installer maps

`port/web/site/setup-mpq.js` implements a bounded, filename-driven MPQ reader
for map payloads, **not** a general-purpose installer. It decrypts MPQ hash
and block tables, looks up known member names, and reads uncompressed or
zlib-compressed sectors (`DecompressionStream('deflate')`). Unsupported
compression types, encrypted tables with invalid extents, and oversized
entries are rejected. Entire retail MPQs are not loaded into JavaScript
memory, but each extracted map is buffered and validated as MPQ content;
individual map size is limited to 32 MiB and the total map set to 128 MiB.

The initial rename profile maps `File00000069.w3x` through
`File00000112.w3x` to 44 original RoC melee/scenario map paths, based on a
published disassembly of a **specific Polish retail CD**. It is accepted only
if *all 44* names can be found in the installer hash table. ISOs
without `War3.mpq` are excluded from this RoC-only mapping, even if their
Setup.mpq contains similarly numbered filenames. This is an
observed example, **not** proof that every language, release or TFT disc uses
those numeric filenames. Unsupported layouts emit a visible warning and
are skipped. Once a profile matches, damaged maps or unsupported codecs
reject the import rather than silently writing a partial map collection.
No executables, DLLs, CD keys or installer code are extracted or executed.

The importer writes maps as e.g. `Maps/(4)LostTemple.w3m` inside the staged
installation. Generation-manifest version 3 validates these nested paths;
versions 1 and 2 remain readable. This enables the existing native filesystem
map discovery to find loose `Maps/` contents without mounting `Setup.mpq`.
`Setup.mpq` itself is never listed as a game archive. Direct MPQ uploads,
multiple ISO selections, cancellation and atomic manifest commits remain.

Fixture tests are in `port/web/tests/setup-mpq-fixture.cjs` and
`port/web/tests/iso-import.test.cjs` (synthetic encrypted MPQ index tables,
raw and zlib sectors, map-name mapping, unsupported layouts and codec
failures, nested OPFS paths). Run:

```sh
node --test port/web/tests/*.test.cjs
```

The first real-disc compatibility milestone must verify the filename mapping,
`Setup.mpq` compression methods, map display names and browser
`DecompressionStream` support. Support for unknown TFT installers,
PKWARE/implode-compressed payloads, archive encryption variants and movie
installation remains unimplemented; do not advertise universal CD support.

## Patch 6 — 2002 demo identification and TFT installer handling

The September 2002 demo ISO listing supplied for compatibility planning includes
`War3Demo.ico`, `SCDemo.exe`, and a smaller but otherwise valid `War3.mpq`.
When an ISO contains both of those distinctive root files, the importer
classifies it as a **demo installation** regardless of the ISO filename. The
follow-on demo patch replaces the earlier Patch 6 rejection: it imports
`War3.mpq` and available `RetailMaps/` map files, storing the maps in `Maps/`
with their original filenames. It intentionally skips the demo `Setup.mpq`
rather than trying the retail RoC map profile. Demo discs must be imported
separately from retail ISOs and standalone MPQs; custom standalone maps remain
allowed. This is positive identification of **that disc layout**, not a
complete retail-vs-demo classifier for loose MPQs or every demo edition.

The version-3 manifest records `edition: "demo"` and
`completeness: "demo"`; new non-demo imports use `edition: "retail"`.
The UI displays the demo label after import and on subsequent visits, while
the launcher announces demo mode during startup. This does not establish that
all demo gameplay or cinematics work with the experimental browser engine.

The May 2003 Frozen Throne ISO listing supplied for planning includes only a
large `Setup.mpq` rather than a visible `War3x.mpq`. The browser now checks
this installer for MPQ members explicitly named `War3x.mpq` and
`War3xLocal.mpq`. When present and independently recognizable as MPQ data,
those members can be extracted without running any installer code:

- Uncompressed, unencrypted members are returned as Blob views of the ISO.
- Zlib-compressed sector-based members are decoded sector by sector, with
  size/offset checks and bounded per-sector allocations.
- Single-unit compressed members remain limited to 32 MiB of packed data;
  PKWARE implode, bzip2, and checksum-protected members remain unsupported.
- Decoded archive size is capped at 512 MiB per member and 768 MiB combined.
- Anonymous `FileNNNNNNNN` members are **not** assigned guessed archive names.

If a selected TFT installer supplies no `War3x.mpq` (and none was provided by
other selected files), the import **fails without replacing the existing
manifest**. It does not silently commit a Reign-of-Chaos-only installation.
The actual 441,517,482-byte `Setup.mpq` from the listed May 2003 disc has **not**
been supplied, so its internal naming/layout remains unknown. The new code
does not establish that this particular disc can yield the required expansion
files. For now an installed-game `War3x.mpq` and `War3xLocal.mpq` remain a
supported way to provide that data alongside RoC.

Synthetic coverage in `node --test port/web/tests/iso-import.test.cjs`
includes named TFT installer MPQs (one multi-sector compressed), unknown
installer layouts, invalid nested archives, incomplete expansion members,
rollback, and the demo-disc fingerprint. No retail media or files are required
for the tests. A separate real-media investigation of TFT's anonymous
installer members is still needed before claiming full original-CD support.

## Patch 7 — discover anonymous MPQs in original TFT Setup.mpq

The May 2003 Frozen Throne installer in the supplied `7z l` listing contains
`Setup.mpq` (~441.5 MB), not loose `War3x.mpq` files. A raw MPQ block-table
inspection of that installer found 245 occupied blocks. Uncompressed blocks
155 and 156 contain complete MPQ headers and self-consistent archive sizes of
284,508,109 and 44,795,849 bytes respectively. Blocks 135–137 contain RIFF
AVI payloads, not MPQs. This explains why attempts to open `War3x.mpq` or
`(listfile)` as members of the outer installer failed, even though the MPQ
itself can be opened. **Their final game-archive identities are not proven by
those sizes alone.** These measurements came from a user-supplied listing and
header probes, not from retail ISO bytes in the source tree.

`site/setup-mpq.js` now scans occupied block-table entries without requiring a
`(listfile)`. It supports two distinct recovery paths:

- When an MPQ member is explicitly named `War3.mpq`, `War3Local.mpq`,
  `War3x.mpq`, `War3xLocal.mpq`, or `War3Patch.mpq`, use named hash lookup and
  the existing bounded MPQ reader (including its zlib sector decoding).
- For anonymous members, scan **unencrypted, uncompressed, whole-file blocks**
  for a valid, self-contained MPQ header. Check internal hash-table membership
  for multiple known Warcraft III file paths before assigning a canonical
  archive role. Do not identify archives by block number, size, order, or by
  merely finding `MPQ\x1a`. Ambiguous or unrecognized archives are skipped
  with a notice. Plain archive byte ranges remain `Blob.slice()` views into the
  source ISO: no temporary 284 MiB JavaScript copy is needed.

The conservative internal-path fingerprints cover RoC base/localized content
and TFT expansion/expansion-localized content. A patch archive does not have a
stable content-only signature, so anonymous patch archives are not guessed;
`War3Patch.mpq` is imported only when explicitly named. File contents and
hash membership can vary between release editions, so the fingerprints may
fail to classify a legitimate archive. A TFT input that fails to provide
`War3x.mpq` still rejects the installation without committing a partial
manifest. Base `War3.mpq` is still required and normally comes from the RoC
CD; no assertion is made that the TFT CD contains another base archive.

Synthetic fixture tests include a 245-entry outer installer with anonymous
archives at the observed indices, named non-expansion archives, anonymous
base/localized candidates, missing/ambiguous fingerprints, duplicate sources,
and preservation of the previous installation on failure. These tests do not
prove the fingerprint paths exist in the actual May 2003 embedded archives.
To verify that disc, import **both RoC and TFT ISOs together** and inspect the
worker's `discovered` list before any conclusion about complete retail support.

## ISO importer hardening and outstanding evidence

A shared `site/mpq-header.js` validator checks MPQ version, header size,
archive-size bounds and decrypted-table **location** bounds before OPFS commit
and again at engine startup; it does not yet check decrypted table contents.
Direct `.mpq` members permit up to 4096 trailing bytes for signatures or padding. This catches
bad headers previously accepted by a `MPQ\x1a` signature check alone.

The importer uses the Web Locks API to serialize commits and protect active
WasmFS generations while pruning old storage. The active and immediately
preceding generations are retained; earlier, non-leased generations are
reclaimed. The missing `War3xLocal.mpq` archive is reported as a partial
expansion installation rather than full TFT support.

The native source and synthetic tests cannot independently establish the
identity of anonymous MPQs inside the **user's May 2003 retail `Setup.mpq`**:
the posted block signatures establish that entries 155/156 are intact MPQ
containers but not their exact game role. Fingerprints currently reflect
candidate internal paths and still require real-archive validation. No test or
change here claims retail TFT compatibility has been established.
