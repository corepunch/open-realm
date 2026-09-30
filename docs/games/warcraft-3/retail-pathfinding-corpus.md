# Retail pathfinding corpus

Target: `game.dll` **1.27.1.7085**, with the binary/CRT hashes in the
[behavior ledger](retail-pathfinding.md#binary-and-evidence-conventions).
The version1 inventory is
`tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json`; its runner is
`tools/ghidra/run_wc3_pathfinding_corpus.py`. Evidence levels retain the ledger's
S/O/C/L definitions. **C means composed original calls**, not automatically
production-engine parity. Engine comparisons are named explicitly.

## Inventory and acceptance

BASE-05.1 records all **32** `verify_wc3_pathing_*.py` oracle scripts, **12**
selected variants, **61** archived JSONL captures and **six** stronger live-input
replays from the documented report
root. Ghidra exports, reducer attempts and other scratch reports support those
experiments; they are not additional independently accepted corpus entries.
Every entry supplies an argument vector, inputs, fresh report filename,
expected exit/status, report checks, evidence level, scope and exclusions.
Repository numerical/scenario fixtures carry SHA256 checksums separately.
GROUP-04.1 adds callback-mutation checks to the motion oracle. GROUP-04.3 adds
three variants: open/wall callback-timed mover reuse and a missing-registry-alias
counterfactual. The inventory now has **114 entries**; fixture hashes and Ghidra
annotation-script fingerprints cover those extensions. See [callback reuse](retail-pathfinding-movement.md#callback-timed-handle-reclamation-and-reuse).

| Entries | Expected status | Meaning |
| --- | --- | --- |
| 32 ordinary oracles, frozen owner baseline, five pair fixtures, two engine comparisons, two callback-reuse variants | `verified` | Oracle assertions and the listed report checks must pass within their stated scope |
| Two native adaptive size-2 cases | `known-reference-difference`, exit1 | Preserve the four retail/reference reachability differences, one per traversal lane |
| Two modified adaptive controls and one missing-registry-alias control | `counterfactual-control`, exit0 | Deliberate hierarchy/result interventions explain the native difference; they do not certify native behavior |
| 54 completed archives | `archive-consistent` | Current generic analyzer accepts the frozen capture; omitted scenario requirements remain omitted |
| Seven incomplete/failed archives | `archive-rejected`, exit1 | Retain their diagnostic rejection and grant no live evidence |
| Five numerical replays and one profile replay | `live-exact-replay` / `live-profile-replay` | Captured raw decisions/commits or getter/publication words match the stronger checker; three entries also compare a repeat capture |

The native adaptive entries use `--size-input 1`, both with the full seeded
map corpus and with `pathing-adaptive-size2-passage.json`. The reduced case
must still report four differences. `--disable-promotion` marks parents mixed;
`--force-east-boundary --east-boundary 18 13` changes one predicate result.
Those intervention flags are explicit in the manifest. See the
[reduced hierarchy witness](retail-pathfinding-search.md#size-2-traversal-and-a-reduced-hierarchy-witness)
for the exact partial routes and causal evidence.

The rejected archives are `follow-arrival.jsonl`, `follow.jsonl`,
`ground-crowd-blockers.jsonl`, `open.jsonl`, `remove-reorder20.jsonl`,
`remove20-retry.jsonl` and `remove20.jsonl`, all under `runtime/`. They contain
missing completion or failed capture records. Recording their expected
rejection prevents them from becoming passing movement witnesses. A missing,
changed or further truncated archive is an **unexpected runner failure**, even
when its original capture was already diagnostic.

Historical metadata retains map, configured duration/sample cap, Frida version,
binary identity, observer flags and watched cell when recorded. It does not
embed the original observer-source or map/data hashes. Archive analysis is
reproducible; an exact fresh live recreation of every historical observer is
not claimed. New scenario manifests and adjacent per-capture provenance records
remain the authority for stronger reproduction. The inventory does not reopen
or silently upgrade the existing evidence.

## Running fresh reports

Run from the repository root with a Python environment containing Unicorn.
The archive root must contain the inventoried local JSONL captures; they and
retail binaries remain outside the repository.

```sh
python3 tools/ghidra/run_wc3_pathfinding_corpus.py --check-only
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /tmp/wc3-pathfinding-corpus-new
```

`--output` must not already exist. The runner checks binary, sibling CRT and
fixture hashes, builds a fresh production math probe when needed, and runs
each command with a bounded per-entry timeout (default1800 seconds).
`--only ENTRY_ID` can be repeated for a subset. A report is required even when
exit1 is expected; its exact mismatch count/result must still satisfy the
manifest. Capture bytes, metadata, completion and analyzer result are checked.
No shell evaluates the command vectors, and no historical report is read as a
new result.

`corpus-results.json` records actual commands, source/manifest/engine hashes,
elapsed time, exit status, fresh report hashes and any failure. Its
`verified_expected_status` property means precisely that the declared outcome
was reproduced. The recorded `expected_status` distinguishes native verified
cases, known differences, counterfactual controls and rejected archives.
The runner exits nonzero for any unexpected outcome, missing report, damaged
input or timeout. Individual stdout/stderr remain in adjacent `.log` files.

The asset-free regression `tests/test_wc3_pathfinding_corpus.py` rejects missing
scripts, altered fixture/capture hashes, changed metadata/completion, stale or
absent reports, changed known differences and unexpected process exits.
It runs in `make test-pathfinding-tools` and `make test`.

## Fresh checkpoint

BASE-05.3 uses the new output directory
`base-05.3-fresh-provenance-corpus-20260929/` under the ledger's report root. Its final
`corpus-results.json` and per-entry reports record **all111 reproduced expected
statuses**:40 verified, two native differences, two counterfactual controls,
54 consistent archives, seven rejected archives and six stronger live-input
replays. Entry execution took323.1 seconds in this local run; this is not an
engine performance measurement. Manifest SHA256:
`43c9cc4aa43e4171d7fa2976f53593d271764325ddebc40a3e7eac330aa649ce`.
Summary SHA256:
`d78a1cdcdd092b8a22cb00469d39aa53a1ddb3ec91aa56169c1f27a0dc0ca5fb`.
All recorded source hashes still matched at completion. No fresh live capture
is substituted for this archive audit.

The repeat velocity case reproduced191 scalar decisions and192 exact velocity,
position and facing commits. The stock repeat reproduced183 scalar decisions,
183 heading errors and184 velocity/position/facing commits. Both repeat digests
matched. The separate facing capture and stock Footman getter/publication replay
also passed. These counts overlap previous evidence; they are not new coverage
percentages.

Remaining extensions belong to the existing backlog: public entry/field coverage
and exclusions in BASE-01/03, general supported inputs in BASE-02, shared-member
mutation in GROUP-04, and broader engine trajectories in NUM-02.3/E2E. Passing
this corpus does not close the whole-replacement READY gates.
