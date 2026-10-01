# Retail pathfinding corpus

Target: `game.dll` **1.27.1.7085**, with the binary/CRT hashes in the
[behavior ledger](retail-pathfinding.md#binary-and-evidence-conventions).
The version1 inventory is
`tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json`; its runner is
`tools/ghidra/run_wc3_pathfinding_corpus.py`. Evidence levels retain the ledger's
S/O/C/L definitions. **C means composed original calls**, not automatically
production-engine parity. Engine comparisons are named explicitly.

## Inventory and acceptance

The inventory now has **200 entries**: **40** distinct original-code oracle
scripts plus **39** declared variants, **96** archived JSONL audits and **25**
stronger live contracts. Each entry supplies an argument vector, pinned inputs,
fresh report filename, expected exit/status, report checks, evidence level,
scope and exclusions. Repository numerical/scenario fixtures carry SHA256
checksums separately. Scratch exports and failed producer attempts remain
outside the accepted inventory unless given their own rejection contract.

| Entries | Expected status | Meaning |
| --- | --- | --- |
| 74 original oracles/compositions/engine comparisons | `verified` / `original-model-engine-exact` / `original-engine-exact` | Assertions and explicit result checks pass within the recorded scope |
| Two adaptive size-2 cases | `known-reference-difference`, exit1 | Retain all four original/reference differences |
| Three deliberate controls | `counterfactual-control` | Intervention is explicit and does not certify native behavior |
| 88 completed archives | `archive-consistent` | Generic audit passes; stronger omitted numerical/scenario requirements remain omitted |
| Eight rejected archives | `archive-rejected`, exit1 | Diagnostic rejection remains visible and grants no live evidence |
| Five numerical replays and four profile replays | `live-exact-replay` / `live-profile-replay` | Stronger checker verifies original raw decisions/commits or profile words |
| Sixteen public/native/compiler/heading/arrival/speed/pose/clock/placement repeat contracts | `live-*-repeat` | Order lifecycle, scalar, angle, power, compiled real/integer and byte inputs retain their own strict producer/word/provenance checks |

NUM-01.13 adds original CRT byte classification and the complete authored-name
producer:514 public S2R/parser calls and1,040 classifier observations repeat,
including raw high-byte Move coordinates. Earlier decimal oracles now execute
exact shipped CRT code instead of an ASCII digit stub, with all established
numerical digests unchanged. The byte contract checks loaded CRT identity and
active default locale, raw input bytes, signed promotion and case brackets.
See [public decimal bytes](retail-pathfinding-engine.md#public-decimal-byte-grammar-and-crt-locale).
Public word parity does not establish complete engine trajectory parity.

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


MAP-03.7 adds `widget-solid-failure`: original stock Footman mask publication,
supplied solid9×9 widget, seven unchanged position/zero-velocity ticks, one
can't-path recovery, task/user queue drain and full group/path/payload/wrapper
reclamation. Its frozen original outcome is independent of the thirteen-tick
narrow-mask journey. Inventory:56 original-code oracles,69 capture archives and9
live contracts. Whole owner/retry cadence and successful public widget creation
remain outside this result.


MAP-03.7 validation: final umbrella tests pass85 pathfinding tool tests and
37,286 assertions in2,155 engine tests per Classic/TFT. Construction displacement
passes85 assertions in normal and forced DEBUG Move builds; normal is restored.
WC3/SC2 production builds pass. Fresh `map-03.7-final-corpus/corpus-results.json`
passes134/134 with source fingerprints unchanged. Manifest SHA256
`0a691e0657ae0affaf25860d725e2a7f0d08cf6d10a58486e2ac8f2cda98750a`;
summary SHA256
`94fc67963332bff10a955bbc43297951f5e607c78d00657650a4302195bf33fe`.
Logs are `/tmp/wc3-map-03.7-final-full-suite.log`, `wc3-map-03.7-debug.log`,
`wc3-map-03.7-normal-restored.log` and `wc3-map-03.7-production.log`.


NUM-01.9 adds `oracle-power`, two complete source-pinned Pow archives and
`live-public-power-repeat`. Inventory is57 original-code oracles,71 archives and10
live contracts. The power oracle distinguishes24,423 completed numerical/ABI
calls from173 budget-stopped signed exponent loops. Both compiler modes preserve
the frozen digest; the live contract repeats40 actual public Pow word pairs.
Numerical result parity does not imply parity with the unobserved original VM
watchdog on nonreturning inputs. Existing trajectory fixtures remain unchanged.


NUM-01.9 validation: final umbrella tests pass88 pathfinding tool tests and
37,328 assertions in2,157 engine tests per Classic/TFT; the numeric API selection
passes136 assertions in5 cases. WC3/SC2 production builds pass. Fresh
`num-01.9-final-corpus/corpus-results.json` passes138/138 with unchanged source
fingerprints. Manifest SHA256
`56adeb3edd9890dde9cbef33318a3508f67645d3173aa4634b138993713b8f61`;
summary SHA256
`ecb01952b670dcaeb6fb1e17fc5cd81606c20b61f5f9a9ac8b8d5902e7d044ca`.
Logs: `/tmp/wc3-num-01.9-final-full-suite.log`,
`/tmp/wc3-num-01.9-final-corpus.log` and
`/tmp/wc3-num-01.9-production.log`. Original/model/C digest for both compiler
modes is `7cf000cfa0a565663f3828dc3186a88efe463c7a9a27ea12494024ba0d13430e`.


NUM-01.10/11 strengthen existing numeric/power entries with81,688 exact rounding
calls and executed original shared scalar initializers. Inventory remains138
entries; historical trajectory digests are unchanged. Final umbrella tests pass89
pathfinding tool tests and37,328 assertions in2,157 engine tests per Classic/TFT.
WC3/SC2 production builds pass. Fresh
`num-01.10-final-corpus/corpus-results.json` passes138/138 with source fingerprints
unchanged. Manifest SHA256
`678dabdaae7123c4934bd5b593bb4f56a157d63723174605da5ff493e31500a1`;
summary SHA256
`92e2e4f19572feb153179b2fe9c63ef35de31292c9fefc61b85d9a49613532ee`.
Logs: `/tmp/wc3-num-01.10-full-suite.log`,
`/tmp/wc3-num-01.10-final-corpus.log`,
`/tmp/wc3-num-01.10-production.log`.


NUM-01.13 checkpoint: the native CRT classifier, public high-byte producer,
raw parsed Move destination and engine object-name/SubString/S2R regression
are integrated. The fresh `num-01.13-validated-corpus/corpus-results.json`
passes150/150 outcomes with all63 recorded source hashes unchanged. Manifest
SHA256 `46392d70bffedb3045a29861e5acf5459897b2d89f952ee75b22dafd3accbf61`;
summary SHA256 `b7dba2bf7abc87d1521fabf072637fb2662381783f80d04db80d3a8335546aee`.
All previous numerical/trajectory fingerprints remain fixed. Full `make test`
passes40,186 assertions/2,163 WC3 cases per Classic/TFT fixture and96 pathfinding
tool tests; WC3 and SC2 production builds pass. Logs are
`/tmp/wc3-num13-full-test-registered.log`, `/tmp/wc3-num13-production.log`,
`/tmp/wc3-num13-validated-corpus.log` and `/tmp/wc3-num13-final-tool-tests.log`.

NUM-01.20 adds the composed heading alias oracle, two complete turn archives and
their strict nested-pointer/word repeat checker. Four new entries pass in fresh
`num-01.20-status-validated-corpus`; the preceding150-entry complete run remains
`num-01.13-validated-corpus`. The new run is a selected four-entry check, not a
claim that all154 entries were rerun. Fixture mutation tests retain the negative
Acos-input witness. See [vector-heading relationships](retail-pathfinding-engine.md#vector-heading-operand-relationships).

## Static fine-search engine comparison

`oracle-grid-engine` adds288 original/C comparisons and288 C reuse repeats on
the recovered four-class legal graphs. It compares the entire cell parent chain,
cost, charged work and allocated node count. The frozen
`retail-fine-grid-1.27.json` contains72 shared terrain maps and288 original
results; asset-free tests run the production header at O0/O2. Equal-key heap and
cheaper-open/closed relaxation witnesses retain the original queue oracle's
expectations. The288 complete maps contain stale entries but no closed reopen;
the latter is covered separately by those relaxation witnesses.

This port changes Move's nearby detours while retaining the current engine
radius/corner legality and visible-point adapter. It does not close retail
footprint admission, dynamic target exits, adaptive long routing or final
trajectory parity. The fresh selected run is
`fine-engine-validated-corpus/corpus-results.json`; the earlier full150-entry
run remains historical. No full155-entry rerun is claimed.

## Class-footprint engine comparisons

`oracle-corridors-engine` compares24 complete original four-class corridor
searches with production C plus24 reuse repeats. `oracle-endpoints-engine`
compares1,184 complete original static endpoint validations with the production
class/cover geometry. Its84 original dynamic validations remain original-only.
`retail-footprint-endpoints-1.27.json` freezes the static inputs/results for
asset-free O0/O2 tests. The selected fresh run is
`foot-engine-validated-final-corpus/corpus-results.json`; no full157-entry rerun is
claimed. Engine nearest-ring correction and the then-current Bresenham/long-field
adapters retain the explicit historical limits in the
[class integration](retail-pathfinding-engine.md#retail-collision-classes-reach-routing-and-stepping).


## All-class sampler and waypoint engine comparison

`oracle-segment-engine` compares43,244 complete original samples with C result
and every queried cell, across four classes, two masks, sixteen directions,
seven positions and eight lengths. The frozen sampler fixture retains results
and a digest of ordered cell queries; asset-free tests repeat at O0/O2. Sixteen
original normalizer word triples and123 original selection/commit indices on the
reachable frozen fine chains also match. The class0 baseline entry remains
unchanged in scope. Fresh selected `segments-engine-validated-corpus/corpus-results.json`
passes both entries; no full158-entry run is claimed. See [engine consumers and
limits](retail-pathfinding-engine.md#retail-segment-sampling-and-waypoint-selection).


## Idle-object engine and live profile contracts

`oracle-grid-objects-engine` executes192 mixed-chain searches plus original
metadata repeats and complete requests, comparing actual C eligibility and
entering-strip expansion before the search. The pinned new fixture is
`retail-fine-objects-1.27.json`. `live-ground-fine-objects` and
`live-air-fine-objects` pin fresh observer/map provenance and require complete
profile publications, committed-vector/20000000 flag transitions and idle
blocker records. The original captures have3012/1661 commits,90/9 starts and
stops,56/9 fine searches, and2423/0 idle hits. These are observed profiles and
policy transitions, not a trajectory repeat certificate.

`idle-objects-final-corpus/corpus-results.json` passes the static baseline,
new original/C oracle and both live contracts:4/4 selected entries. An earlier
attempt under system Python failed visibly because Unicorn was absent; the
accepted run uses the existing analysis environment. No full161-entry rerun is
claimed. See [engine behavior and limits](retail-pathfinding-engine.md#idle-objects-affect-nearby-move-routes).


## Nearest partial-route budget boundaries

`oracle-grid-partials-engine` adds208 full mixed-object searches/repeats/requests
and1456 original complete requests around the final work boundary. Exact C
comparisons include failed-result nearest nodes, squared distances, parent
chains, charged pops and node counts, repeated at O0/O2 by the asset-free suite.
The fixture `retail-fine-partials-1.27.json` also pins original wrapper coordinates.
The corpus compares C parent chains, not an invented complete reconstruction.

`partials-engine-final-corpus/corpus-results.json` passes3/3 selected entries
(static baseline, mixed objects, partial routes). The earlier161-entry selected
idle-object checkpoint remains historical; no full162-entry rerun is claimed.
See [engine partial route behavior](retail-pathfinding-engine.md#nearest-partial-routes-survive-blocked-goals).

## Stock movement masks and water-lane engine checks

The stock profile table capture pins seven authored movetp rows and14 paired
getter/publication observations. Its strict table verifier rejects truncation,
rawcode/mover reassignment and changed mask words. Three compact fixtures retain
those observations, all256 original WPM words and384 all-mask fine-search cases.
`live-movement-profile-table`, `oracle-load-movement-masks-engine` and
`oracle-grid-movement-profiles-engine` pass alongside the previous mixed-object
engine entry in a fresh four-entry run. No whole165-entry rerun is claimed.
See [engine masks](retail-pathfinding-engine.md#authored-movement-masks-reach-terrain-and-object-queries)
for actual Move/footprint/command regressions and explicit support/trajectory gaps.

## Fine reconstruction endpoint words

`oracle-fine-reconstruction-engine` compares all3,840 original eight-direction
fine chains with production C coordinate words and repeated storage. Its compact
fixture is hash-pinned;9,216 coarse reconstructions remain original/model only.
The fresh endpoint/partial two-entry run passes; it does not claim the whole
166-entry inventory. See [fine route endpoints](retail-pathfinding-engine.md#exact-fine-route-endpoints)
for the engine fractional-order regression and remaining admission/arrival gaps.

## Point Move arrival engine comparison

`oracle-arrival-engine` compares2342 complete original predicate calls with C raw
words. `live-point-arrival-repeat` checks actual zero-range input/publication,
all183 predicted-pose evaluations and commits, the final previous-velocity stop,
and identical repeat decisions. Both accepted captures and the failed first
observer capture are individually archived; the latter has no evidence and must
retain its access-violation rejection. The current inventory is171 entries:
71 oracle variants from37 scripts,82 archive audits and18 live contracts.
The asset-free tests preserve all2342 predicate words and reject damaged producer,
publication, pairing, heading, force and completion records. See
[point Move arrival](retail-pathfinding-engine.md#point-move-arrival) for the engine
change and remaining clock/producer limits.


## Ordinary authored speed producers

The five new speed entries execute the original1846 normal clamp/disabled-gate
cases with and without production C, audit both owned speed-input captures and
replay their public-native/mover/commit contracts through
`verify_wc3_speed_inputs.py`. All five declared outcomes pass in the fresh
`speed-inputs-corpus-fresh-260930` report directory. The inventory is now176
entries:73 original-oracle entries,84 archived captures and19 strict live
contracts. The two new fixtures pin the original raw clamp outputs and actual
public producer/source/map inputs. The high setter changes a moving cap; the low
setter occurs after arrival. This bounded witness closes MOVE-01.4, while
immediate low-cap integration and whole clock/trajectory parity remain open.


## Immediate public speed drop

`speed-cap-corpus-final-261001/corpus-results.json` freshly reproduces all five
new declared outcomes: complete original cap-transition execution, production
C comparison, two archived travel captures and their strict repeated live
contract. The inventory is181 entries (39 oracle scripts plus36 variants,
86 archive audits and20 live contracts). Both retained preliminary corpus
attempts expose manifest metadata mistakes; neither is accepted as the
checkpoint. The source archives themselves were unchanged.

`verify_wc3_speed_drop.py` requires the same original scalar cap transition,
consumption by the next velocity commit, full public native/bridge sequence,
actor identity, bounds/default words,300 JASS samples and exact repeated
motion digests. Engine nonzero-elapsed clock production remains excluded.


## Flat bonus queries and published caps

`item-speed-corpus-fresh-261001/corpus-results.json` reproduces all seven new
declared outcomes: the existing complete speed oracle with126 production C
bonus comparisons, four frozen capture audits and two repeated live contracts.
The inventory is188 entries:39 original scripts plus37 variants,90 archive
audits and22 live contracts.

The quiet scene certifies five speed compositions and102 velocity commits;
the explicit setter/reissue scene certifies12 compositions and92 commits.
`verify_wc3_item_speed.py` requires actor/attached-bonus identity, authored
words, immutable default, complete300-sample runs, all cap transitions and
consumption of the last published cap. Both scenes repeat three word digests.
The invalid Footman-inventory attempt remains an unaccepted diagnostic archive
(`item-speed-first-261001.jsonl`); its explicit admission-failure markers
prevent using it as a Boots experiment.


## Exact mixed formation geometry

The existing `oracle-motion` and `motion-engine-exact` entries now require865
complete formation layouts, including720 mixed-radius/rank/heading/tie/moving
clock cases. The engine entry additionally requires865 raw production C
comparisons. The frozen `retail-formation-layout-1.27.json` input/output fixture
is hash-pinned in the inventory and replayed twice at O0/O2 by the repository
math tests. The inventory remains188 entries; these are strengthened existing
entries rather than a new original-code script.

Fresh outcomes live in `formation-corpus-final-261001/corpus-results.json`.
The enlarged geometry domain does not certify larger-than-twelve groups,
original UI selection production or the complete refresh-to-motion engine
chain. The existing archival captures are unchanged.


## Direct world-to-grid boundaries

`oracle-maps` now requires576 additional complete original boundary edits;
`oracle-world-grid-engine` requires all576 production coordinate comparisons.
The frozen six-word fine/cell/inverse outputs are replayed twice at O0/O2 and
hash-pinned as `retail-world-grid-boundaries-1.27.json`. The inventory is189
entries:39 original scripts plus38 variants,90 archive audits and22 live
contracts. Fresh accepted outcomes are
`world-grid-corpus-final-261001/corpus-results.json`.

The matrix covers original world/fine conversion and scalar inverse composition;
it does not certify the complete public coordinate/metadata producers or
proximity/adaptive padding. Existing live archives are unchanged.

The motion oracle and engine comparison also require18 retained-native-pose
sequences,288 commits and139 world-space integration differences. The frozen
native pose fixture is hash pinned, and repeated O0/O2 checks compare every
fine/world word. These controlled elapsed0.1 intervals leave the public owner
clock and velocity phase as explicit engine work; see
[retained fine pose](retail-pathfinding-engine.md#retained-fine-pose-reaches-move).


## Public axis-position writes and following Move

The two motion entries now require576 complete original writes and576
following Move commits; the engine entry additionally requires each C result.
The two axis-position archives and strict repeated public contract add three
entries. Inventory:192 entries,77 oracles (39 scripts/38 variants),92 archive
audits and23 live contracts;65 fixtures are hash-pinned.

`live-axis-position-repeat` requires40 native calls,40 queries, eight writes,
unchanged velocity/facing,167 motion/velocity commits and exact repeat.
The frozen original matrix and actual captures are replayed at O0/O2 with
negative mutation controls. Fresh accepted results live in
`axis-position-corpus-final-261001/corpus-results.json`.
Engine geometry now consumes this result through public SetUnitX/Y; owner
clock cadence, between-frame prediction and other forced setters remain open.
See [the engine payoff and limits](retail-pathfinding-engine.md#public-axis-position-writes-retain-the-next-move-step).


## Primary clock and fine target integration

The primary-clock source/owner repeat adds two archive audits and one strict
live contract. Motion oracles compare6,000 original primary advances,1,944
clock controls,1,000 controlled commits and300 predicted queries; the engine
runs the complete controlled30-second trajectory. See [primary clocks](retail-pathfinding-engine.md#primary-clock-reaches-move-and-predicted-positions).

`oracle-fine-targets` and `engine-fine-targets` add2,304 original core/full
requests with200 identity completions. They pin suppression, terrain precedence,
active/link/category eligibility and both overlapping supplied-chain orders;
production C compares complete parent chains, cost, work and node creation.
The inventory has197 entries (79 oracles from40 scripts/39 variants,94 archives,
24 live contracts) and68 hash-pinned fixtures. Fresh accepted target outcomes
are in `fine-target-corpus-accepted-261001/corpus-results.json`.
Runtime target-link chronology remains FINE-01.6; see [engine scope](retail-pathfinding-engine.md#target-identity-exits-reach-the-engine).

The forced-position scene adds two completed archives and
`live-forced-position-repeat`. Its strict checker verifies all28 public calls,
60 predicted queries, four nested Stop integrations and four scalar placements,
then compares99 motion and velocity commits. Both captures repeat exactly.
Fresh acceptance is in `forced-position-corpus-accepted-261001`; the inventory
contains69 pinned fixtures. See [the engine port](retail-pathfinding-engine.md#forced-position-stop-reaches-the-engine)
for the public Move/Patrol, queued-order, exact-word and save/load regressions.
