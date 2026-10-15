# Browser ISO 9660 reader (Warcraft III disc import, patch 1)

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

The current `site/install-worker.js` does **not** load this module: enabling
ISO uploads, selection and copying are separate follow-up patches. Until then
the existing MPQ/W3M/W3X-only installer and game-data manifest are unchanged.

## Usage from a browser worker

Load with `importScripts('iso9660.js')` once it is included by a future browser
build (packaging is not part of this patch). The API is also exported via
CommonJS to allow dependency-free Node.js tests.

```js
const controller = new AbortController();
const iso = await ISO9660.openISO(file, {signal: controller.signal});
const entries = await iso.listFiles();
const archives = await iso.findFiles('War3.mpq');

// Patch 2 will reuse this zero-copy slice with the existing OPFS writer.
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

Patch 1 intentionally does **not** verify a retail Reign of Chaos or Frozen
Throne image, any `Setup.mpq` contents, the browser UI or final game launch.
Testing real discs and packaging the script are follow-up tasks.

See also [Browser platform](browser.md) for OPFS installer ownership, runtime
mounting and experimental engine build constraints.
