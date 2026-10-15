# Browser ISO 9660 reader (Warcraft III disc import, patches 1–4)

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
writer. `Setup.mpq`, movies and arbitrary archives inside an ISO are skipped:
installer-package contents are **not** unpacked. Archive names are matched
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
archives, or both (maximum 512 input files and eight ISO images). Its output remains
a version-2 generation manifest with the same archive names/sizes. No changes
to `filesystem-bridge.js` or native MPQ loading are required. Name matching is
case-insensitive and traverses ISO subdirectories, but repeated candidate names
are rejected as ambiguous. Only bounded MPQ header validation is performed;
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
`Setup.mpq` extraction, a real browser import or final game launch. Testing real
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
thread, and only recognized archives from each ISO are imported. `Setup.mpq`
is not unpacked. An `.iso` filename containing a raw-sector BIN image does
not make it a supported ISO 9660 image.

The worker reports `phase` events (`scanning` with a disc `source`,
`validating`, `storage`, `copying`, `committing`), one `discovered` event after
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
