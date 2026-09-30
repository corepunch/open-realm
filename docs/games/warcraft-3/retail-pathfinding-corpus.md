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
selected variants, **69** archived JSONL captures and **nine** stronger live-input
replays from the documented report
root. Ghidra exports, reducer attempts and other scratch reports support those
experiments; they are not additional independently accepted corpus entries.
Every entry supplies an argument vector, inputs, fresh report filename,
expected exit/status, report checks, evidence level, scope and exclusions.
Repository numerical/scenario fixtures carry SHA256 checksums separately.
ORDER-01.4 adds registered2039d0 at42 frozen singleton/FIFO states plus18
invalid-backing controls, repeated identically (124 native calls including
restored-valid checks). Cdecl stack, callee registers, SEH and Unit references
are checked. Existing VM backing is supplied; full construction stays excluded.
GROUP-04.1 adds callback-mutation checks to the motion oracle. GROUP-04.3 adds
three variants: open/wall callback-timed mover reuse and a missing-registry-alias
counterfactual. GROUP-04.4 adds two mixed-speed callback-completion/survivor variants with178 exact C world commits and empty teardown. GROUP-04.8 adds two completed-member reclamation/reuse through survivor arrival variants, also178 exact C commits. GROUP-04.5 adds two actual survivor point-replacement/refresh journeys with638 exact C commits. NUM-01.5/06 add one repeated public numeric/parser and parsed Move admission capture; NUM-01.8 adds one48-pair public-angle repeat. The inventory now has **133 entries**; fixture hashes and Ghidra
annotation-script fingerprints cover those extensions. MAP-03.4 adds the
original-mask Footman escape variant and eight complete negative public-widget
producer captures. Frozen route states cover every tick of both seven/thirteen
tick variants; negative captures do not imply a successful escape. See [callback reuse](retail-pathfinding-movement.md#callback-timed-handle-reclamation-and-reuse).

| Entries | Expected status | Meaning |
| --- | --- | --- |
| 32 ordinary oracles, frozen owner baseline and its full current-order native query variant, five pair fixtures, two engine comparisons, two callback-reuse variants two callback-completion variants two completion/reuse journeys and two survivor replacement journeys | `verified` | Oracle assertions and the listed report checks must pass within their stated scope |
| Two native adaptive size-2 cases | `known-reference-difference`, exit1 | Preserve the four retail/reference reachability differences, one per traversal lane |
| Two modified adaptive controls and one missing-registry-alias control | `counterfactual-control`, exit0 | Deliberate hierarchy/result interventions explain the native difference; they do not certify native behavior |
| 62 completed archives | `archive-consistent` | Current generic analyzer accepts the frozen capture; omitted scenario requirements remain omitted |
| Seven incomplete/failed archives | `archive-rejected`, exit1 | Retain their diagnostic rejection and grant no live evidence |
| Five numerical replays and one profile replay | `live-exact-replay` / `live-profile-replay` | Captured raw decisions/commits or getter/publication words match the stronger checker; three entries also compare a repeat capture |

The three further live entries require repeated public order lifecycle,67
bracketed numeric/25 parser witnesses, and48 raw public angle pairs, respectively. The new numeric capture
embeds source and map hashes and compares exact parsed Move destination words;
it does not certify a full engine trajectory.

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


## Owned-path factory checkpoint

`group-04.7-native-path-factory-validated-corpus/corpus-results.json` freshly reproduces all116 declared outcomes after allocating baseline individual paths through the original path factory. Frozen raw motion/identity expectations are unchanged. Callback reuse now additionally asserts the owner958 path free header/link, live3→2 and retained allocation count, while complete ordinary group release retains one/two individual paths. This closes GROUP-04.7's fixture accounting; it does not close the actual survivor refresh/reuse composition in04.5. All recorded source hashes matched at completion; no new live witness is claimed.


## Completed-member reuse checkpoint

`group-04.8-completed-reuse-frozen-corpus/corpus-results.json` freshly reproduces118/118 declared outcomes with unchanged source fingerprints. The new open/wall matrices combine original completion, mover reclamation/generation reuse and survivor arrival, checking178 commits against the production velocity adapter. All frozen survivor trajectories match completion-without-reuse controls; final path pool live1 and empty group/order state are retained. This closes04.8 independently of actual formation refresh/new destinations in04.5.


## Actual survivor replacement checkpoint

`group-04.5-survivor-retarget-frozen-corpus/corpus-results.json` freshly reproduces120/120 declared outcomes with unchanged source fingerprints. Two new four-case matrices run original680320 survivor point replacement after completion/reuse, actual new-request route/layout, new-goal arrival and old/new group teardown. The production velocity adapter matches638 commits exactly. The engine server-frame regression independently checks slot preservation, ownership reset and arrival; its full tests pass without a speculative behavior change. Same-group pursuit refresh and the current-order public query remain separately assigned tasks.

ORDER-01.6 adds `live-order-lifecycle-repeat`: two complete owned public-JASS captures, all300 timer samples each and320 identical timer/health markers. The analyzer rejects historical/animation-derived current heads, missing Hold damage, incomplete captures and changed repeats. `retail-order-lifecycle-1.27.json` records generated-map/observer hashes separately after capture; it does not upgrade old header provenance or certify raw float/whole-engine trajectory parity. ORDER-01.7/08 integrate the bounded Hold/Follow engine query slices; synchronous target-removal timing remains01.15.


MAP-03.4 stock-mask checkpoint: fresh
`map-03.4-stock-final-corpus/corpus-results.json` reproduces **133/133** declared
outcomes with unchanged source fingerprints at completion. Manifest SHA256:
`8a6a9e6aeeab8b7ad79e85fa024ba0a37f5775e8887b6836f8a11d828848efc8`.
Summary SHA256:
`39754dca2b4d64f4b966f407fef683b48d019f5f762fec08ad417917af61850d`.
Full ROC/TFT suites pass37,126 assertions in2,142 engine tests each;85 pathfinding
tool tests and both WC3/SC2 production builds pass. The original-mask extension
adds no engine representation or frame-cadence change. The earlier idle admission
fix remains active; inside-footprint escape integration remains MAP-03.7.
