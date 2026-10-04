# Retail pathfinding corpus

Target: `game.dll` **1.27.1.7085**, with the binary/CRT hashes in the
[behavior ledger](retail-pathfinding.md#binary-and-evidence-conventions).
The version1 inventory is
`tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json`; its runner is
`tools/ghidra/run_wc3_pathfinding_corpus.py`. Evidence levels retain the ledger's
S/O/C/L definitions. **C means composed original calls**, not automatically
production-engine parity. Engine comparisons are named explicitly.

## Inventory and acceptance

The inventory now has **327 entries**: **51** distinct original-code oracle
scripts plus **64** declared variants, **121** archived JSONL audits and **91**
stronger live contracts. Each entry supplies an argument vector, pinned inputs,
fresh report filename, expected exit/status, report checks, evidence level,
scope and exclusions. Repository numerical/scenario fixtures carry SHA256
checksums separately. Scratch exports and failed producer attempts remain
outside the accepted inventory unless given their own rejection contract.

| Entries | Expected status | Meaning |
| --- | --- | --- |
| 108 original oracles/compositions/engine comparisons | `verified` / `original-model-engine-exact` / `original-engine-exact` | Assertions and explicit result checks pass within the recorded scope |
| Four adaptive size-2 cases | `known-reference-difference`, exit1 | Retain all four original/reference differences |
| Three deliberate controls | `counterfactual-control` | Intervention is explicit and does not certify native behavior |
| 113 completed archives | `archive-consistent` | Generic audit passes; stronger omitted numerical/scenario requirements remain omitted |
| Eight rejected archives | `archive-rejected`, exit1 | Diagnostic rejection remains visible and grants no live evidence |
| Six numerical replays and four profile replays | `live-exact-replay` / `live-profile-replay` | Stronger checker verifies original raw decisions/commits or profile words |
| Forty public/native/compiler/heading/arrival/speed/pose/clock/placement/random/yield/spawn repeat contracts | `live-*-repeat` | Order lifecycle, scalar, angle, power, compiled real/integer and byte inputs retain their own strict producer/word/provenance checks |

The latest long-route entry repeats32 full original128-cell initial routes and
160 admitted refills, preserving nonzero coarse indices and complete buffers.
It also compares480 original search-obstruction flags with production C. The
[engine port](retail-pathfinding-engine.md#successive-long-refills-select-the-original-fine-index)
uses that flag to select the original initial fine index; controlled source
positions are distinct from full retail physical long-route motion.

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

Blocked point admission adds `oracle-point-placement`, two completed archives
and `live-blocked-position-repeat`:2,304 complete original calls retain1,152
exact C endpoints; seven actual candidate searches and six scalar writes repeat.
Fresh acceptance is `blocked-position-corpus-final-accepted-261001`; the inventory
now contains71 pinned fixtures. [Scope and engine evidence](retail-pathfinding-engine.md#blocked-placement-reaches-the-engine)
keep the five-ring embedded Stop producer and bridge/outside-map admission open.

MAP-03.8 adds one complete public terrain-native oracle, with1,040 query/write
cases and3,130 original calls, exact production helper results and untouched
neighbor/occupancy/mode guards. `terrain-natives-corpus-accepted-261001` is the
fresh accepted report. The inventory now pins72 repository fixtures. See
[engine terrain integration](retail-pathfinding-engine.md#terrain-pathing-natives-reach-the-engine).

Owner RNG payoff17 adds `oracle-random-owner`:1,408 complete original/C next,
public integer/real and overlap-direction calls with both state words checked,
plus11 actual public seed prefixes. Secondary per-unit/TLS reseeding is excluded.
`random-owner-corpus-accepted-261001` is the fresh composed/live report. Inventory:209 entries,
82 oracles (43 scripts/39 variants),100 archived captures,27 live contracts;
73 pinned repository fixtures. See [engine integration](retail-pathfinding-engine.md#deterministic-owner-random-state-reaches-public-natives).

Scene40's `live-random-owner-repeat` requires all539 public seed/query returns,
raw inputs/results and owner-before/after words to match both the original
oracle and the second completed capture. The10s loading-key attempt is kept
outside accepted evidence because it did not reach scheduled samples.

The repulsion-kernels entry adds the complete settings initializer/default
shipped CRT and1,024 authentic category-producer calls, plus705 exact production
C pair/tail slice comparisons. Its exclusions retain complete proximity,
application and scheduler composition; those kernel counts do not certify a
whole crowd trajectory. The versioned fixture also records the first seeded
overlap used by the actual engine scheduler regression. Accepted fresh result:
`repulsion-kernels-corpus-final-261001/corpus-results.json` under the analysis
root. The repository inventory contains74 checksummed fixtures.


## Ordinary adaptive engine comparison

`adaptive-engine-size1` and `adaptive-engine-size2` add712 full original requests
against the production adaptive header. Every result, route coordinate word,
point count, charged pop and created-node count agrees. Size2 intentionally
retains the original's four known unlimited graph-reference differences and
exit1. Fresh acceptance is `adaptive-engine-corpus-final-261001/corpus-results.json`;
reports record the executed production library hash. O0 repeat and bounded-work
reports are documented with the [engine port](retail-pathfinding-engine.md#adaptive-search-reaches-long-distance-move).
This makes212 entries:44 scripts plus41 variants,100 archive audits and27 live
contracts. Supplied classifications and preallocated storage remain explicit;
these comparisons do not certify map producers, special edges, capacity limits
or full gameplay trajectories.


## Complete fine-route trajectory reference

`fine-route-trajectory-reference` freezes and repeats the controlled original
34-tick wall detour twice, comparing position, velocity, heading and route-index
words to `retail-fine-route-trajectory-1.27.json`. The68 steps agree. The actual
engine callback independently reproduces that complete trace and its22 saved
continuation ticks; see [engine payoff20](retail-pathfinding-engine.md#retained-fine-routes-reproduce-a-complete-retail-detour).
Fresh acceptance: `fine-route-trajectory-corpus-accepted-261001/corpus-results.json`.
The initial tuple/list export comparison was rejected and retained as diagnostic;
JSON canonicalization corrected that structural mismatch without changing words.
The current inventory has213 entries (44 scripts,42 variants,100 archive audits,
27 live contracts) and75 checksummed fixtures. Controlled radius/profile and
elapsed inputs exclude stock producer and real primary-owner cadence claims.

## Ordinary unit activation and maze budgets

`unit-default-route-reference` freezes eight complete original165ae0 requests
on two winding64-cell maps across four footprint classes. Original166060 enables
adaptive routing with400 adaptive/700 fine attempts, even when source and goal
are within48 cells. Engine world regressions compare every coarse/fine point and
both indices, and a public Move reaches the unchanged destination through the
full maze with600 exact saved continuation frames.

`unit-fine-budget-reference` isolates original148100 on the same maps. All16
700/2048 controls have different budget-dependent partial endpoints; production
C matches the complete request headers and reconstructed words. The engine local
leg builder retains the700-word witnesses independently of adaptive admission.
Strict fresh replay is `unit-default-corpus-accepted-261001/corpus-results.json`.
The inventory contains230 entries:94 oracles,106 archives,30 live contracts,
with84 pinned repository fixtures. Group5000 composition, admission timing and
full physical retail maze motion remain separate contracts. See
[ordinary path defaults](retail-pathfinding-engine.md#ordinary-path-defaults-enable-adaptive-routing).


Payoff30 extends `oracle-yield` to compare all8,640 original single-candidate
choices and25 countdown gates with production C. Two fresh owned crowd archives
and `live-moving-yield-repeat-261001` retain source/map provenance and compare
all2,503 ordered decisions, every actor wait-state change and424 delayed calls;
the independent repeat has identical normalized digest. The two added frozen
fixtures bring the pinned inventory to86. Accepted strict run:
`moving-yield-corpus-accepted-v2-261001/corpus-results.json`.
See [ordinary Move integration and exclusions](retail-pathfinding-engine.md#ordered-moving-waits-reach-ordinary-move).

Payoff31 adds `oracle-wait-heading`, two owned raw caller archives and
`live-wait-heading-repeat-261001`. Complete original16fbd0 consumes1,008
controlled native waits; two live runs match all424 actual countdown callers
against C, including the nested gate's untouched final goal/source and stop/turn
outputs. The strict verifier rejects incomplete operands, changed words/gates,
mispaired counters, provenance loss and truncated/incomplete captures. Two
additional frozen fixtures bring the pinned inventory to88. Accepted fresh run:
`wait-heading-corpus-accepted-v2-261001/corpus-results.json`. See
[engine payoff31](retail-pathfinding-engine.md#waiting-headings-preserve-the-native-caller)
for the reproduced world-coordinate failures and public saved continuation.


### Peer retry production transaction

Payoff32 strengthens `oracle-retry` with336 complete original initialization
and2016 null-target transitions compared to production C, including actual
registry/group lookup and owner random state. Two new archive audits and
`live-peer-retry-repeat-261001` verify42 initializations plus55 transitions
with identical raw-word digest
`b22624f045c5916d77deae4f2c07ed815a42a577796e6851175d5eb89331068a`.
The accepted fresh four-entry result is
`peer-retry-corpus-accepted-261001/corpus-results.json`. Ninety pinned fixtures
include the complete original counter cases and100 selected live rows. O0/O2
checks reject ten damaged provenance/input/state/completion variants each.
The engine owns the peer20 fine-reset/coarse-retention transaction and public
saved continuation; full source admission, other blocked-vector kinds, group
producers, terminal/perimeter ownership and stock crowds remain separate.

Public spawn payoff33 adds two complete scene44 archive audits and one strict
repeat contract. It compares eight class1 overlap/fractional admissions, sixteen
fresh mover position commits,104 world queries and239 motion decisions through
production C. Engine CreateUnit/AtLoc consumes the verified rings and sentinel
pose; saved scheduler continuation is covered separately. The corpus now pins91
fixtures. See [public spawn admission](retail-pathfinding-engine.md#public-spawn-admission-and-initial-mover-pose)
for the exact destination matrix and remaining producer limits.


## Public spawn-to-Move lifetimes

`live-public-spawn-motion-repeat-261001` strengthens the spawn producer witness
with all247 velocity/position/facing commits and247 predicted arrival evaluations
across eight actual actor lifetimes. Each retains its initial spawn state, entire
commit chain and one natural final old-velocity stop. Mover pointers are reused;
case markers define lifetimes, so two distinct addresses do not mean two actors.
`verify_wc3_spawn_motion_trace.py` composes the existing strict spawn and motion
checks with per-lifetime identity, prediction, range and arrival replay. The two
complete captures share journey digest
`c9bdf0f74634db0555f2625776af7d97d8838997999efd5a6f8c37c6a593bf96`.

Raw inputs are `runtime/spawn-motion-live-{first,repeat}-261001.jsonl`;
`spawn-motion-corpus-producers-261001/corpus-results.json` freshly accepts both
archives and their stronger repeat contract. The frozen
`retail-public-spawn-motion-1.27.json` pins247 complete commits, the corresponding
arrival inputs, source/map identities and a small derived ground WPM control.
The engine's public-order table is checked against these original words.
The original phase producer and broader terrain/profile/group composition remain
separate requirements. See [the actual engine consumer](retail-pathfinding-engine.md#clear-public-routes-retain-native-waypoints).

## Normal periodic timer admission

Owned `spawn-phase-live-{first,repeat}-261001.jsonl` adds primary clock observers
to the unchanged scene44 map/source. Presentation interleaving differs between
captures; the6000 primary advances,1000 owner callbacks and247 movement commits
retain their established exact digests. The strict checker composes the existing
clock and spawn-motion contracts, then checks both position writes for each birth
inside primary timer dispatch. Births occur after20+400*i advances; phases are
2,0,4,2,0,4,2,0. At phase0 the due owner has already completed before public spawn.

`retail-public-spawn-phase-1.27.json` freezes this compact timeline and the SHA256
of the existing motion fixture. It duplicates no binary assets or large clock
stream. The two archive audits and repeated live contract produce fresh reports
in `spawn-phase-corpus-final-261001`. The [engine producer regression](retail-pathfinding-engine.md#public-timer-admission-reaches-move-from-zero)
drives ordinary TimerStart from zero, with no per-actor clock/phase supplied.
General timer scalar deadlines, short/zero periods and other actor profiles remain open.

## Public oblique singleton group handoffs

`live-public-oblique-repeat-261001` verifies two source/map-pinned scene45
captures and a third read-only geometry observer. All689 commits,684 scalar
movement decisions,6000 primary advances,1000 owners, birth phases2/4/0 and
five natural stops agree; two stops advance an intermediate group destination
without replacing the one public point order per life. The checker composes
production arithmetic, predicted arrival and state-chain validation with the
observed group-to-member destination sequence.

The frozen fixture supplies the observed terrain and static-object geometry
as compact counted byte runs; read-only eligibility decoding is explicit.
The near-zero predecessor without the clear terrain control stays a named,
SHA-pinned diagnostic exclusion because it ends in forced arrival. Public
scene/scenery construction and hierarchy invalidation remain separate.
The [engine integration](retail-pathfinding-engine.md#public-oblique-move-retains-the-singleton-group-destination)
uses ordinary timers/frames from zero and includes260 original saved
continuation commits. Generic archive audits alone do not certify that parity.


Payoff37 adds `live-group-point-admission-repeat-261001`: four public point-order
forms admit the first twelve of fourteen insertion-order units in two complete
owned repeats. Its strict checker verifies all-candidate attach before admission
and one shared original CMoveReq per call,48 admitted members and eight excluded
members. The engine implements the bounded snapshot and numeric/Loc adapters;
shared physical movement remains GROUP-04.6. The captures deliberately cap motion
rows and grant no complete trajectory evidence. See [public group point
admission](retail-pathfinding-engine.md#public-group-point-orders-admit-twelve-members).


Payoff38 adds `live-public-pair-repeat-261001`: two complete public point-group
lifetimes repeat60 all-member decision/commit visits,115 commits,113 scalar
decisions and601 public samples. Frozen member/pose/phase words preserve the
blocked left slot, nearest fine endpoint, survivor offset and retry/forced-arrival
chain. Production arithmetic replays every commit; a separate normal-frame
engine regression matches115 commits and87 saved suffix commits. The supplied
scene geometry and wider GROUP-04.6 gaps remain explicit. See [public pair
movement](retail-pathfinding-engine.md#public-pair-movement-uses-a-shared-move-owner).


Payoff40 adds `live-owner-change-repeat-261002`: two independent script-only
public ownership-change captures repeat365 lifecycle records,17 motion commits,
two native Stops, class0 to1 and306 markers. The strict checker pins every raw
word and callback/class order and verifies the engine header. Arithmetic replays
all17 commits; the normal-frame engine test checks16 observable motion commits
and the final Stop state, including saved continuations before and after its
shared frame. Extra trailing clock rows are checked for observer completeness
and grant no new numerical-clock claim. The unchanged scheduler oracle is rerun
fresh alongside this contract. Inventory is258 entries/99 frozen fixtures.
See [ordinary player queues](retail-pathfinding-engine.md#ordinary-fine-search-belongs-to-the-unit-player).


Payoff41 adds `live-selected-point-repeat-261002`: actual externally issued
Win32 Move input reaches the original player3 packet8 producer, two complete
attach-before-admit/publication sequences and shared physical lifetimes. Both
captures repeat all228 relative pose/velocity/facing commits, while retaining
different supplied absolute input clocks. The fresh checker verifies every
original phase hash,452 scalar decisions,456 numerical commits and four natural
completions, plus all helper/observer/map provenance and the engine header.
Normal engine frames separately reproduce all456 commits and304 saved suffix
commits; the existing115-commit public pair also passes through selection.
Inventory is259 entries/100 frozen fixtures. Failed X11 mouse and preliminary
helper captures remain diagnostic evidence outside this contract. See
[selected ground Move](retail-pathfinding-engine.md#selected-ground-move-uses-the-shared-physical-owner).


Payoff42 adds `live-selected-queued-repeat-261002`: external native Shift Move
packet9 retains both current heads, appends one common request and naturally
activates a singleton before rebuilding the matching pair. The strict checker
verifies two previous-request neighbor searches (no match, then match), four
callback candidates, all request/ready/member and owner-phase words,1012 scalar
decisions,1020 exact numerical commits, eight natural arrivals and522 owner
passes across six cohorts. Both captures repeat all510 absolute motion words.
Normal engine frames match the same1020 commits and684 saved suffix commits.
The boundary input retains its exact primary clock and pre-owner counter.
Inventory is260 entries/101 frozen fixtures. Preliminary detour and earlier
observer captures remain diagnostic evidence outside the accepted final
contract. See [selected Shift ownership](retail-pathfinding-engine.md#selected-shift-move-retains-request-ownership-through-staggered-arrival).


Payoff43 adds `live-selected-idle-shift-repeat-261002`: actual native packet9
from idle appends both empty heads and immediately activates one shared physical
pair. The reused strict selected checker verifies the explicit Shift policy,
complete provenance/phase records,452 scalar decisions,456 exact numerical
commits and four natural completions. Both independent captures repeat all228
absolute motion words. Normal engine frames match456 commits and304 saved suffix
commits; the earlier input clock's distinct position low bits remain frozen.
Inventory is261 entries/102 fixtures. See [idle Shift movement](retail-pathfinding-engine.md#selected-idle-shift-starts-the-shared-physical-owner-immediately).


Payoff44 adds `live-selected-double-queued-repeat-261002`: two native Shift
requests preserve current heads1 to2 to3 and retain the latest submitted request
across both FIFO activations. The checker verifies four previous-cohort searches,
eight candidates, five physical cohorts per capture,1098 scalar decisions,
1110 exact commits, twelve natural arrivals and590 owner passes. Both captures
repeat all555 absolute motion words; normal engine frames match1110 commits and
864 saved suffix commits. Separate bulk clock telemetry is explicitly off;
producer/commit clocks and retained clock contracts stay exact. Loading/timing
attempts remain diagnostic evidence outside this accepted contract. Inventory
is262 entries/103 fixtures. See [two pending Shift moves](retail-pathfinding-engine.md#two-pending-shift-moves-retain-submission-history-and-physical-generations).


Payoff45 adds `live-selected-mixed-captures-261002`: four complete native ground
Shift journeys with one active and one idle selected unit. Earlier clicks let
the idle peer finish before cohort acquisition; later clicks retain a moving
peer that joins. The strict checker requires null target identity, appends
without replacing the active head, and starts the idle head immediately. Four
separate input clocks retain separate motion tables:1498 scalar decisions,1510
exact commits, twelve arrivals and1365 owner visits. Normal engine frames match
all1510 commits and1020 saved suffix commits. A missing trace-end attempt and a
click that actually targeted a moving unit remain diagnostic, outside acceptance.
Inventory is263 entries/104 fixtures; eight fresh strict contracts retain all
earlier selected/public/owner witnesses. See [mixed Shift movement](retail-pathfinding-engine.md#mixed-active-and-idle-shift-share-one-submitted-request).


Payoff46 adds `live-selected-independent-repeat-261002`: two native selected
ground Shift journeys preserve two distinct active singleton heads, then create
and join the same later cohort. All511 absolute motion commits repeat. Normal
frames match1022 commits and844 saved suffix commits. The existing mixed-input
checker handles this explicit admission mode without relaxing its earlier
contract. Inventory is264 entries/105 fixtures; nine fresh contracts retain all
selected/public/owner witnesses. See [independent active owners](retail-pathfinding-engine.md#independent-active-owners-accept-the-same-pending-ground-request).


Payoff47 adds `live-follow-velocity-repeat-261002`: two complete bounded Smart
Follow witnesses repeat1015 absolute commits each. The visible friendly target
walks and changes speed; the user head persists until authored Stop. The checker
requires416 scalar decisions,2030 exact commits,116 target reloads and2030 owner
visits across both captures. Native task arrival is distinct from user-head
completion. Normal engine frames and three Save80 continuations independently
match1015 plus2595 suffix commits. Inventory is265 entries/106 fixtures with48
strict live contracts; ten fresh selected/public/owner/Follow contracts preserve
the earlier witnesses. See [moving-target integration](retail-pathfinding-engine.md#smart-follow-tracks-a-moving-target-through-a-speed-change).

Payoff48 adds `live-follow-target-reuse-captures-261002`, covering four complete
original death/removal and actual pool/public-handle reuse captures. The strict
verifier checks fresh canonical generation, immediate head cancellation,
no implicit replacement adoption, explicit Smart reacquisition, every owner
phase and948 full motion rows per capture. Main/target markers are frozen
per retirement mode because KillUnit retains a dead target until removal.
`retail-follow-target-reuse-1.27.json` supplies the frozen expectations;
`verify_wc3_follow_target_reuse_trace.py` performs independent C scalar checks.
The corpus now declares266 entries,107 fixtures and49 strict live entries.
[Normal engine/save evidence](retail-pathfinding-engine.md#follow-cancels-synchronously-before-target-pool-reuse)
is separate from these captured-input C checks. Wider queued/combat callbacks
remain open; bounded Stop is not natural Follow head completion.

Payoff49 adds `live-follow-target-teleport-captures-261002`: eight original
captures for axis and placement setters at tick90 during target travel and
tick100 after target arrival. `retail-follow-target-teleport-1.27.json` freezes
four public journeys and three distinct motion sequences; the verifier checks
8182 commits/owner passes,2158 scalar decisions and464 target reloads. It also
checks setter current-order distinctions, preserved Follow heads and cached
new destination acceptance. Per-capture hashes retain the two immutable source
archives. Inventory now267 entries/108 fixtures/50 strict live entries.
[Engine/saved continuations](retail-pathfinding-engine.md#follow-tracks-public-target-teleports-without-premature-point-settling)
remain separate evidence. Collision resizing and denied delayed replan producers
remain open.

Payoff50 adds `live-follow-target-resize-captures-261002`: ten complete native
captures for five public Chaos growth/shrink, retained-range control, fresh
admission and research-gated journeys. `retail-follow-target-resize-1.27.json`
freezes every radius/type marker, canonical target generation, motion/owner phase,
range, refresh and replan result. The verifier checks10150 commits/owner passes,
2160 scalar decisions and584 reloads. Fresh admission deliberately contains two
same-clock Smart150 calls, both retained as public inputs. The research control
inherits original Sca1 Requires=Roch and observes type/radius commit at151.
Three immutable source archives retain each capture's actual producer hashes;
incorrect W3A level, research and collision-type setups remain rejected controls.
Inventory now268 entries/109 fixtures/51 strict live entries. The fresh runner
rebuilds production C and passes the new contract plus13 previous public/owner
contracts. [Engine and saved trajectories](retail-pathfinding-engine.md#follow-retains-active-range-and-admits-resized-targets-with-half-edge-approaches)
remain independent regressions. Moving-unit occupancy and exact automatic
morph timing remain open.


Payoff51 adds `live-moving-radius-captures-261002`: eight complete original
captures for moving Chaos growth, nine below/equal/above radius boundaries,
research delivery and read-only type-task/timer handoff witnesses. The verifier
checks6510 commits/owner passes,4858 scalar decisions and40 public resizes.
The two extended witnesses retain7798 scalar periodic rearms, partitioned into
6000 owner and1798 public timer requests, proving retained-deadline addition.
The fixture pins full canonical phases, public type/order markers, mover identity,
changed physical owner, fine geometry and primary clock words. Secondary clock
wall cadence remains outside the deterministic comparison.

Inventory269 entries/110 fixtures/52 strict live entries;25 freshly executed
capture contracts pass. [Production journeys and Save81 continuations](retail-pathfinding-engine.md#moving-radius-changes-retain-point-motion-and-scalar-owner-deadlines)
provide separate engine evidence. General scalar timeout/getter boundaries,
paused remainder, epoch crossings, heap ordering and other movement families
remain open.


Payoff52 adds `live-group-radius-captures-261002`: twelve complete original
captures repeat public group Move with a peer growing, shrinking or disappearing.
The base observer and read-only footprint observer preserve identical canonical
motion and owner phases. The verifier checks3324 commits,3304 decisions,2920
owner passes,20 real routing-radius queries and1460 observed path+b4 words.
It separately derives the unbound live maximum from actual resolved members;
that value may decrease while the old route retains its sampled footprint.
Wrong cached footprint, stale owner, singleton reissue and incomplete extent
controls reject the evidence. Each scene publishes one group order.

Inventory270 entries/111 fixtures/53 strict live entries;26 fresh capture
contracts pass. [Normal engine and Save81 journeys](retail-pathfinding-engine.md#local-group-maximum-and-retained-route-footprint-have-separate-lifetimes)
provide independent lifecycle regressions. Bound shared7c maximum publication,
other movement/target families and arbitrary terrain remain open.

## Public blocked-terrain point Move

Payoff53 adds `live-blocked-goal-captures-261002`, two independent unmodified
public Move captures through a5x5 blocked terrain destination. The frozen fixture
contains all207 motion commits, canonical owner phases, six complete searches
and routes, actual retry initialization/results, forced arrival, and natural
can't-path task/order cleanup. Both primary clock sequences and canonical
motion digests agree. Host/presentation subdivisions vary; the verifier checks
each capture's own closing counts rather than claiming those counts repeat.

The strict verifier replays414 velocity commits/410 decisions and2000 primary
owner callbacks against production numerical C. Eleven negative/regeneration
checks reject wrong adjusted retry goals, random draws, premature refill,
terminal buffer clearing, skipped final angular gate and retained order heads.
All27 fresh capture contracts pass. Inventory271 entries/112 fixtures/54 strict
live entries. [Normal engine and Save82 continuations](retail-pathfinding-engine.md#blocked-point-goals-retain-the-click-through-retry-and-forced-arrival)
are separate complete production-entry regressions. Wider outside/overlap,
classes/masks and queued/combat recovery remain open.


## Public outside point Move and coordinate clipping

Payoff54 adds `live-outside-goal-captures-261002`: two complete outside-west
journeys and two12-input public edge matrices. The strict contract verifies
26 public admissions,382 exact movement commits,376 decisions and2000 primary
owner callbacks across the repeats. Raw task coordinates remain separate from
clipped routing operands. Original `oracle-maps` additionally compares108
coordinate prefixes with production C, while retaining its576 world-to-grid
boundary cases and existing map assertions.

The corpus now contains272 entries,114 pinned repository fixtures and55 strict
live contracts. `runtime/outside-goal-strict-final-261002/corpus-results.json`
runs28 fresh capture contracts plus the map oracle. Four immutable source/map
provenance checks and284 pathfinding Python checks pass. The complete engine
journey and four Save82 continuations are described in [outside point goals](retail-pathfinding-engine.md#outside-point-goals-clip-routing-while-retaining-the-public-click).


## Captain home formation and the explicit engine handoff gap

Payoff55 adds `live-captain-home-captures-261002`: six complete captures repeat
stationary home admission, retained-roster InitAssault, and full/shortfall/retry
Boolean predicates. All1,080 original commits (1,068 physical recruit commits
plus12 stopped virtual-actor commits),1,062 decisions and6,000 primary owner
callbacks pass numerical replay. The complete178-row recruit reference is
frozen independently of the engine's33-commit admission window. The report
requires `whole_engine_parity=false` and `private_handoff_remains_open=true`;
reference replay cannot close the missing private task transition.

The inventory has273 entries,115 pinned fixtures and56 strict live contracts.
`runtime/captain-home-strict-final-261002/corpus-results.json` runs29 fresh
capture contracts plus the original map oracle. Six source/map provenance
checks pass. Engine and saved admission evidence, current limitations, owned
probe setup and diagnostic exclusions are in [captain home integration](retail-pathfinding-engine.md#captain-home-recruitment-and-formation-retries-reach-move).

## Stationary captain range and whole movement control

`live-captain-range-captures-261003` preserves five original captures: two
near-home repeats, one equivalent readonly blocker capture and two farther
source repeats. All retain the170-update native range timeline and1000 owner
callbacks. The near/far private handoff occurs at exact words`3ffffff8` and
`403ffffc`; complete recruit motion has178 and250 commits respectively.
The corpus fixture and verifier retain full references and immutable source/map
hashes. The blocker capture preserves causal evidence: category2/radius0 home
actor covers one cell, becomes eligible after private point handoff, and drives
the limited search to701 pops with12 object rejections. Earlier v1/v2 range
observers are diagnostic only; v2 overwrote an event discriminator with packet
words. Use the corrected v3 source for admitted evidence.

The earlier `live-captain-home-captures-261002` still retains six complete
original journeys; its current engine claim now covers all178 commits rather
than the initial33-only admission. Captured bytes and provenance have not changed.
Moving actors, larger rosters, default town-home producers and general AI
restoration remain explicit backlog work. See [engine payoff56](retail-pathfinding-engine.md#stationary-captain-range-callback-and-zero-radius-occupancy).

Current payoff56 inventory:274 entries,116 pinned fixtures and57 strict live contracts.


## Stationary two-recruit captain evidence

`live-captain-pair-captures-261003` pins two complete captures:
`runtime/captain-pair-v1-late-key-first-261003.jsonl` and
`runtime/captain-pair-v1-complete-first-261003.jsonl`, with frozen source under
`runtime/captain-pair-source-v1-261003`. Their371 total velocity commits each
include369 physical commits and two virtual actor commits; full phase hashes
repeat. `verify_wc3_captain_pair_trace.py` checks producer metadata/archive
bytes, both physical trajectories, all-entered callback nesting, one two-pass
shared request, generation retention,218 physical footprint publications,
152 shared7c updates and1000 primary owners per capture. Production normal-frame
C tests separately verify the complete engine journey and saved suffixes.

Initial recruit tasks are independent target-follow owners. The first membership
enter cannot prepare a point batch; only the second does. This avoids certifying
an engine that moves both immediately as one formation. Source terrain is explicit.
The final native public sample covers the primary recruit; both physical arrival
commits and owner teardown are captured. Larger/mixed batches and forced retries
are outside this evidence. See [engine payoff57](retail-pathfinding-engine.md#stationary-captain-pair-private-followers-to-shared-arrival).

The premature12s-key `captain-pair-v1-first-261003` capture stayed at tick0 and
contains no admitted AI journey. It is diagnostic only and has no corpus entry.
Accepted captures continue at30s and retain the full final marker. Current
inventory:275 entries,117 pinned fixtures and58 strict live contracts.


### Complete mixed captain and blocked-home captures

Payoff58 adds `live-captain-mixed-captures-261003` and
`live-captain-blocked-captures-261003`, each with two complete immutable owned
captures. The generalized strict pair verifier pins producer bytes, full native
motion and phase words, all-entered shared admission and complete footprint
publication. Mixed references preserve the32-to31 live maximum with cached32.
Blocked references preserve all fourteen retries, forced arrival and complete
buffer/task recovery; neither a truncated retry stream nor a fabricated terminal
buffer can pass. Independent whole-engine journeys and saved continuations are
in the [engine ledger](retail-pathfinding-engine.md#mixed-captain-pairs-and-blocked-home-retries).
Fresh34/34 selected contracts include both additions and the original-map oracle.
Inventory is277 entries,119 pinned repository fixtures and60 strict live contracts;
325 pathfinding Python checks pass. Auxiliary cell-watch evidence is kept separate
from the two certified repeats.


### Complete captain owned-pool mutation captures

Payoff59 adds `live-captain-owned-pool-captures-261003`: eight immutable
complete captures cover transfer, same-owner no-op, delayed removal/recreation
and partial AddAssault. Each pair repeats all physical and virtual phases,
range callbacks, request admission and cleanup. The literal reference retains
908 physical commits (1,814 across eight captures), 8,000 primary owners and
the distinct birth generations when a mover address is reused.

`retail-captain-pool-1.27.json` pins each capture's metadata/hash plus complete
motion, producer, admission, range and lifecycle records.
`verify_wc3_captain_pool_trace.py` rejects wrong recruitment, reused birth
generation, omitted delayed removal, duplicate partial recruitment, premature
shared admission and changed retry membership. The engine matches all 908
commits and 4,344 saved suffix commits after explicit bot-VM retirement, with
pre-recruitment restores restarting the public AI timer.

Accepted capture names, frozen map/source locations and the excluded
preliminary/interrupted runs are recorded in
[owned-pool ordering](retail-pathfinding-engine.md#captain-owned-pool-order-survives-transfer-and-reused-slots).
The inventory now contains 278 entries, 120 repository fixtures and 61 strict
live contracts. `runtime/captain-pool-strict-final-261003/corpus-results.json`
freshly executes all 35 current parity contracts after source changes; every
source fingerprint matches the final tools. Larger batches, active membership
changes, moving captains and default town homes remain outside this contract.

Checkpoint60 adds `live-captain-three-captures-261003` and the complete
`retail-captain-three-1.27.json` fixture. Inventory is 279 entries, 121 fixtures
and 62 strict live entries. Fresh `runtime/captain-three-strict-final-261003/`
passes all 36 selected contracts, including both complete three-recruit captures
(1,216 physical commits, 2,000 owner callbacks). Primary clock, full scalar
motion, all-entered admission and retained retry membership are verified;
capped auxiliary hierarchy/dirty diagnostics remain outside that completeness
claim. Accepted capture sources/maps remain frozen outside the repository.


Checkpoint61 adds `live-captain-thirteen-captures-261003` and the complete
`retail-captain-thirteen-1.27.json` fixture. Inventory is280 entries,122 fixtures
and63 strict live entries. Both complete captures retain7294 physical commits
and2000 owners in total. The contract includes all-entered12+1 admission, primary
clock, complete motion/lifecycle, physical shared footprints and original W3E
support-level geometry. The engine matches3647 commits plus15768 saved suffix
commits. Mixed-radius13 captures are retained separately until their shared
parameter owner is implemented; they do not certify this homogeneous fixture.


Fresh `runtime/captain-thirteen-strict-final-v2-261003/corpus-results.json` passes
all37 selected parity contracts with134 matching final source fingerprints.
The earlier runner invocation rejected the new entry's malformed additional
capture envelope before verification; it is retained as failed and certifies
nothing. The corrected final run executes both immutable captures freshly.


Checkpoint62 adds `live-captain-approach-captures-261003` and
`retail-captain-approach-1.27.json`. Inventory is281 entries,123 fixtures and64
strict live entries. Two full native mixed13 captures retain5462 physical commits
each and14 original authored-range calls each. The engine regression is explicitly
bounded to3471 pre-batch/activation commits through9000ms, plus17368 saved suffix
commits. The strict contract rejects whole-engine parity claims and a closed
shared-owner gap. Physical arrival ranges and actual enabled attack maximum90
are independent inputs; all primary clock and scalar integration events are
checked. Auxiliary getter call counts outside simulation movement are not a
determinism claim. Accepted source trees and traces remain frozen externally.


Full debug/release RoC/TFT passes2403 tests and1336346 assertions per edition.
The required full release repository suite and357 pathfinding Python checks pass.
Fresh `runtime/captain-approach-strict-final-261003/corpus-results.json` passes38
selected contracts with135 matching final source fingerprints.
`captain-approach-validation-final-261003.json` pins validation logs and saved
Ghidra readback. Failed initial range-probe/corpus/test runs remain archived and
do not certify this final result.

Checkpoint63 adds `live-captain-shared-captures-261003` and
`retail-captain-shared-1.27.json`. Inventory is282 entries,124 fixtures and65
strict live entries. The new contract reuses both frozen mixed-range captures,
checks353 shared footprints and325 publications across two generations, and
checks the literal127-row engine footprint header. Engine parity is explicitly
bounded to4560 commits through12 seconds; recovery reentry remains open.
The full native5462-row motion reference and the private approach contract are
unchanged. Tests reject owner-generation aliasing, cached/live radius conflation,
lost final-batch maximum, wrong prior-speed publication/reference count and an
expanded engine scope.

Fresh `runtime/captain-shared-strict-final-v2-261003/corpus-results.json` passes
39/39 contracts with136 matching source fingerprints. Full debug/release
RoC/TFT suites pass2405 tests/1484868 assertions per edition; required repository
tests and365 pathfinding Python checks pass. Saved Ghidra readback confirms600
roles,51 layouts/318 fields,266 explicit ABIs,53 globals and no unsaved changes.


Checkpoint64 adds `live-captain-departure-captures-261003` and
`retail-captain-departure-1.27.json`. Inventory is283 entries,125 fixtures and66
strict live entries. Two complete newly instrumented read-only captures preserve
the existing5462-row native journey and both shared-owner generations. The
strict contract additionally authenticates unit births against original task
admissions, all34 inner/outer counter changes,38 callback begin/end observations,
15 target-order calls and the12-second old-velocity stop/private-group binding.
Engine scope is4867 physical commits before15000ms and7917 saved suffix commits;
full logical roster retention and the second shared publication remain open.
The verifier rejects any full-engine claim while those fields remain unresolved.

Fresh `runtime/captain-range-strict-final-261003/corpus-results.json` passes40/40
contracts with137 matching source fingerprints. Saved Ghidra readback confirms
604 roles,51 layouts/318 fields,270 explicit ABIs,53 globals and no unsaved
changes. Full debug/release RoC/TFT suites pass2406 tests and1613585 assertions per edition.
The required release repository suite,373 pathfinding Python checks and40 fresh
corpus contracts pass with137 matching source fingerprints.


## Complete mixed13 logical-roster reentry contract

`live-captain-reentry-captures-261003` authenticates both complete frozen readonly
membership captures through `retail-captain-reentry-1.27.json`. The new contract
hashes the prior departure/shared references, retains their historical prefix
scope, and checks the complete stationary alive scene:5462 physical motion rows,
353 live/cached shared footprints, two distinct shared generations and nine exact
logical membership deadlines. Its literal engine header includes both generations
and all inner/outer masks. Eight corruption controls prevent truncated prefixes,
wrong deadlines/members/counts, source substitution or a claim of whole-pathfinder
completion. Inventory is284 entries/126 fixtures/67 strict live entries;381 Python
checks pass. Fresh `runtime/captain-reentry-strict-261003/corpus-results.json`
passes41/41 contracts with138 matching source fingerprints. Full debug/release
RoC/TFT pass2407 tests/1798757 assertions per edition; the required release
repository suite passes. Saved Ghidra readback retains605 roles,51 layouts/320
fields,271 explicit ABIs,53 globals and no unsaved changes.


## Mixed13 largest-recruit cancellation contract

`live-captain-cancel-captures-261003` authenticates two early and two late complete
Stop captures through `retail-captain-cancel-1.27.json`. All11051 distinct scene
commits and753 shared footprints are tied to literal C headers and full native
producer/admission/range/lifecycle state. Retry target addresses are authenticated
against recorded virtual captain fine-grid records before canonical comparison;
the adjacent blocker slot remains unchanged. Twelve negative controls reject
foreign/aliased/missing target records, missing timing variants, conflated
footprints/references/generations and broader scope claims. Inventory is285 entries,
127 fixtures and68 strict live entries.393 Python checks pass; fresh
`runtime/captain-cancel-strict-261003/corpus-results.json` passes42/42 contracts
with139 matching source fingerprints. Full debug/release RoC/TFT pass2409
tests/2199857 assertions per edition; the required repository suite passes. Saved
Ghidra readback has606 roles,52 layouts/323 fields,272 explicit ABIs and53 globals.


## Final-binding Stop and repeated AI initialization contract

`retail-captain-last-binding-1.27.json` authenticates both complete readonly all13
Stop captures,3549 physical commits,12 shared footprints and every final shared
publication. Before-call identity records preserve the collected generation;
post-call identity is invalid. The first post-Stop shared prepass still observes
two bound groups; the next observes zero references and collects the owner.
A second public AI call loads sources without entering main. Exact marker/native
sequences, full canonical motion/lifecycle state and literal engine references
are required. Eight Save88 continuations preserve idle movement and the admission
gate. Private AI VM continuations and RemoveUnit/retarget/reuse remain open.
Fresh `runtime/captain-last-binding-strict-261003/corpus-results.json` passes
43/43 contracts with140 matching source fingerprints. All404 Python checks pass;
full debug/release RoC/TFT pass2410 tests/2236214 assertions per edition, and the
required repository suite passes. Inventory is286 entries/128 fixtures/69
strict live entries. Historical largest-only and natural-completion contracts
retain their narrower claims.

## Moving captain initial travel

`live-captain-go-home-captures-261003` authenticates the first/repeat south
GoHome captures with30000-sample velocity limits. Unlike the capped eastern
controls, each complete capture has as many recorded velocity commits as its
footer reports. The strict contract verifies scalar commits and the1000 observed
primary owner callbacks, then compares the same6251-commit engine reference
through23.8s:6058 physical commits,193 moving virtual commits and398 shared
footprints. Both motion and footprint digests repeat exactly.

The scope explicitly requires `complete_scene_journeys=false` and
`private_retry_continuation_remains_open=true`. A closed capture is not proof
that the private follower has naturally completed. Whole-pathfinder claims,
31-second engine claims, missing virtual motion, ordinary-unit actor profiles,
capped captures, lost500-versus200 range state and reused shared generations
are rejected by separate negative controls. The literal header is authenticated
against the capture-derived reference. Inventory is287 entries/129 pinned
fixtures/70 strict live entries. See [the native implementation](retail-pathfinding-engine.md#public-captaingohome-and-moving-virtual-captain)
for the first remaining retry mismatch at23.91s.

Validation: full debug/release RoC/TFT pass2412 tests/2427132 assertions per edition.
All416 Python pathfinding checks and44 fresh corpus contracts pass with141
source fingerprints; inventory287 entries/129 fixtures/70 strict live entries.
The required release `make test` also passes.

## Accepted captain destination clears its old wait

`live-captain-retarget-captures-261003` composes the original GoHome reference
with only the subsequent1168 motion rows and226 footprint rows through27.2s.
The total7419 commits include306 virtual commits;624 footprints and10606 saved
suffix commits are exact in actual engine frames. The native counter1821 witness
requires pending delay20, changed1/ready1, timestamps0/0 and final delay0 with
heading3fffd0f5. A stalled old partial route cannot satisfy that contract.
The first/repeat full captures remain independently authenticated and uncapped.
One-point fine refill at27.27s and complete moving journey remain explicitly
open. Inventory is288 entries/130 pinned fixtures/71 strict live entries.
See [the reset ordering and next mismatch](retail-pathfinding-engine.md#changed-captain-destination-resets-pending-waits).

Validation: full debug/release RoC/TFT pass2413 tests/2627883 assertions per edition.
All429 Python checks and45 fresh corpus contracts pass with142 source
fingerprints. Inventory288 entries/130 fixtures/71 strict live entries.
The required release `make test` also passes.

## Partial captain refill through the observer completion marker

`live-captain-refill-captures-261003` authenticates the same two uncapped
GoHome sources, both prior fixture hashes and only the514 new motion rows/188
new footprint rows. Its30-second domain contains7933 motion commits,327 virtual
commits and812 footprints. Counter1933 requires exact fractional source
reconstruction,701/700 exhausted search and adaptive7→2 with retry6 retained.
Counter1996 requires adaptive2→0 and explicit stop despite a tiny heading
error. The actual engine also repeats7310 saved suffix commits.

The30-second authored completion marker disables footprint observation. Later
velocity rows remain in the authenticated raw capture, but do not extend this
combined movement/footprint contract. Scope requires natural completion and
whole retail pathfinding to remain open. Eighteen controls reject altered
source, search, retry, coarse index, stop, actor motion and observation extents.
Inventory is289 entries/131 pinned fixtures/72 strict live entries. See
[the game-owned consumer fixes](retail-pathfinding-engine.md#partial-fine-refill-and-stopped-coarse-handoff).

Payoff70 validation:46 fresh contracts pass with143 source fingerprints;447
Python pathfinding checks and the required release `make test` pass. Debug/
release RoC/TFT each pass2477 tests/2804132 assertions. These results certify
the30-second combined observer domain; longer travel remains open.

Payoff71 adds `live-blocker-lifecycle-captures-261003`: two actual public
resource/destructible lifetimes,26 fine/hierarchy snapshots, eight following
fine requests and764 exact scalar velocity/position/facing commits. Seven
nonnull collections per run retire through the complete native free. Negative
controls reject retained footprints/hierarchy, missing depletion, missing fine
requests, incomplete collection retirement and failed/late observation.
The inventory now has290 entries/132 fixtures/73 strict live contracts.
See [the engine removal fix](retail-pathfinding-engine.md#blocker-removal-owns-static-route-invalidation).


Payoff72 adds `live-widget-overlap-captures-261003`: two complete file-backed
creation-order repeats retain all18 public pose/fine/hierarchy snapshots and
six actual nonnull collection retirements per run. `retail-widget-overlap-1.27.json`
is the133rd pinned fixture. The three existing widget oracles additionally
check288 production snap and27 map-clamp cases against full original functions.
The canonical creation-order builder reproduces the historical map byte hash;
its optional32-cell observer patch retains all static movement lanes and
complete highest-parent coverage. Historical source identity remains pinned to
the immutable external package. Five fresh strict contracts pass in
`runtime/widget-overlap-strict-c11-261003/`. See
[engine creation integration](retail-pathfinding-engine.md#authored-widget-creation-preserves-snapped-pose-and-rotation).


Payoff73 adds `live-map-load-captures-261003`: two complete WPM-backed loads
retain196608 raw decoded fine flags and291696 initial hierarchy classifications.
The134th fixture, `retail-map-load-1.27.json`, pins the actual WPM byte hash and
all numeric inputs/outputs in compact runs. Missing file/constructor calls,
changed dimensions, terrain or padding/parent classes fail acceptance. The
schema fixture now preserves55 layouts/357 fields,288 explicit ABIs and59
globals. Six fresh strict load/map/widget reports pass in
`runtime/map-load-strict-final-261003/`. See [engine initialization](retail-pathfinding-engine.md#file-backed-maps-retain-native-hierarchy-allocation).

## Constructed map coordinates

Payoff74 strengthens existing `oracle-map_construction`; no new script or entry.
The135th fixture, `retail-constructed-map-coordinates-1.27.json`, freezes25
complete original terrain-origin/no-file maps, full hierarchy RLE,2500 corner
word/cell/inverse records and400 four-lane edits with exact grid reversals.
The fresh strict runner requires matching original/C case counts. Literal
engine inputs, maps, edits, indices and complete classifications are verified
by Python; the production regression passes930530 assertions. Native proximity
allocation is checked explicitly; engine occupancy uses BoxEdicts.
Inventory remains292 entries/45 oracle scripts/96 oracle variants/121 archives/
75 strict live contracts. Fresh map/map-construction/file-load contracts pass
at `runtime/map-coordinates-strict74/corpus-results.json`. See
[engine coverage](retail-pathfinding-engine.md#constructed-map-corners-and-padding-reach-engine-regression-coverage).

## Passage matrix

Payoff75 adds one variant of the existing fine-grid oracle:
`oracle-grid-passages-engine` requires3200 core/repeat/full fractional
requests and C search comparisons over50 shapes/four lanes/four offsets.
Frozen `retail-passage-matrix-1.27.json` also contains6400 native footprint
outputs and exact complete/partial reconstructed words. The production
engine regression compares every endpoint and2720 admitted route pairs.
Numeric C geometry/routes are deduplicated and checked against this original
fixture by Python. No synthetic public admission/clock claim.
Inventory is293 entries/45 scripts/97 oracle variants/121 archives/75 strict
live contracts/136 fixtures. Five fresh static/mixed/partial/all-lane/passage
contracts pass in `runtime/passage-strict75/`. See
[engine coverage](retail-pathfinding-engine.md#passage-matrix-covers-lanes-footprints-corners-and-offsets).

## Full queue composition

Payoff76 adds one variant of the existing fine-grid oracle:
`oracle-grid-queue-composition-engine` uses one natural48x48 static input,
complete core/reuse/fractional consumers and1068 original/C queue records.
Frozen `retail-fine-queue-1.27.json` includes the700-work admitted-but-unpopped
goal control and50-point partial centre route. The strict runner compiles
the optional read-only C pop observer and requires190 stale pops, one
reopening,886 equal keys, exact original records and701 charged partial work.
The engine reproduces two endpoint errors before the fix, then its full
queue/reuse and ordinary mover-budget regressions match all literal words.
Inventory is294 entries/45 scripts/98 oracle variants/121 archives/75 strict
live contracts/137 fixtures. Four fresh fine/partial/passage/queue variants
pass in `runtime/fine-queue-strict76/`; the queue comparison passes separately
at O0. See [engine payoff](retail-pathfinding-engine.md#full-queue-composition-preserves-partial-goal-centres).

## Fine stamp wrap

Payoff77 adds`oracle-grid-stamp-wrap-engine`as a variant of the existing grid
oracle. Native setup increments a once-seeded counter through`ffff`,0,1,2;
retained-map results match four clean controls and production C, including every
final semantic node and complete fractional route. The new numeric fixture is
`retail-fine-stamp-wrap-1.27.json`; it authenticates the existing queue fixture's
terrain instead of duplicating it. G_BuildUnitMoveLocalRoute also matches all
nodes/work/routes in both class traversal orders over retained engine storage.
Inventory is295 entries/45 scripts/99 oracle contracts/121 archives/75 strict
live contracts/138 fixtures. Both fresh queue and wrap contracts pass in
`runtime/fine-wrap-strict77/`. This is a seeded counter boundary, not a full
historical-cycle or capacity certificate. See
[engine payoff](retail-pathfinding-engine.md#fine-stamp-wrap-preserves-complete-engine-request-state).

## Adaptive stamp and class restoration

Payoff78 adds`oracle-adaptive-stamp-wrap-engine`, one variant of the existing
adaptive oracle. Eight complete original400-work requests traverse the actual
DWORD stamp boundary over warmed metadata, four lanes and sizes1/2. All511
final semantic nodes/work/fractional routes equal clean controls and C;
G_BuildUnitMoveFineRoute repeats the literal states over retained real engine
maps and buffers. The fixture authenticates native lane order/masks and keeps
the warmup/seeded-history scope explicit. No new script or capture is added.

Existing`oracle-maps`and`oracle-world-grid-engine`now require three actual
no-fly-only exclusion/restore cases. Their native flag byte41/0/41 reveals the
engine post-exclusion ground-mask error; eight reproduced stale classifications
are fixed and every cached lane/level is restored. Frozen map/scalar expectations
and adaptive known-reference differences remain required. The inventory is296
entries/45 scripts/100 oracle contracts/121 archives/75 strict live contracts/
139 fixtures. New saved adaptive types preserve partial-prefix/unknown-byte
limits. Six fresh adaptive/wrap/map contracts pass in
`runtime/adaptive-wrap-strict-final78/`; the adaptive wrap comparison also
passes at O0. Required RoC/TFT suites pass2490 tests/4393170 assertions per
edition and481 Python checks. See [engine payoff](retail-pathfinding-engine.md#adaptive-reuse-restores-the-original-ground-classifications).

## Terrain-produced adaptive classification

Payoff79 adds one composition to the existing adaptive script, not another
oracle script or capture. `oracle-adaptive-terrain-producer-engine` executes
288 actual terrain setters, complete padded classification and four unchanged
size2 requests. Its54 ordinary classification witnesses,27 rejected tuples,
2,206 class bytes and all final nodes/routes are frozen in
`retail-adaptive-terrain-producer-1.27.json`. The reduced passage still differs
from the ordinary reference graph; expected exit1 and four differences remain
mandatory. Supplied empty storage/map headers, special-edge exclusions and the
remaining public mover journey are explicit.

The inventory is now297 entries/45 scripts/101 oracle contracts (56 variants)/
121 archives/75 strict live contracts/140 fixtures. Four contracts retain native
reference differences; deliberate controls remain three. Literal C fixtures
and actual engine route/classification assertions keep the research synchronized
with reimplementation. See [engine payoff](retail-pathfinding-engine.md#terrain-produced-adaptive-passages-preserve-the-retail-veto)
and [producer inventory](retail-pathfinding-search.md#pathing-producer-and-update-inventory).

The three final fresh contracts pass in `runtime/terrain-producer79-strict-pinned/`
with149 source pins. Native/engine comparisons match at O0/O2; required complete
RoC/TFT suites pass2,492 tests/4,406,232 assertions per edition and482 Python
checks. These totals certify the current merged branch and this fixture chunk,
not complete Warcraft III pathfinding fidelity.

## Producer-built passage completion

Payoff80 adds `live-adaptive-passage-captures-261003`, with two full original
file-backed/public-order captures and `retail-adaptive-passage-1.27.json`.
Its strict verifier checks all nine complete/partial searches, two retries,
forced arrival and task/order retirement,434 numerical decisions/heading errors,
437 complete raw motion/facing commits,6000 primary advances and1000 owner
callbacks per capture. The known coarse reference difference remains in its
separate oracle contract; it is not reclassified as successful routing.

The engine literal is generated from frozen original words. Normal game frames
and six Save90 continuations agree in RoC/TFT (12,420 assertions,437 ordinary
and800 suffix commits per edition). Failed loading/input and incomplete sample
attempts are retained as external experiments, not accepted fixtures.
Inventory is298 entries/45 original oracle scripts/101 oracle contracts
(56 variants)/121 archived audits/76 strict live contracts/141 fixtures.
See [whole journey payoff](retail-pathfinding-engine.md#producer-built-size2-passage-reaches-full-retail-failure).


## Overlapping target producer and engine history

Payoff81 adds `oracle-spatial-engine` and
`live-target-overlap-captures-261003`. The former executes1,280 original
rectangle updates and compares81,920 production active-cell orders, including
retained intersections, lazy removals and stable original cleanup. The latter
requires two complete actual target/blocker public-order captures:501 exact
motion/facing commits each, nine watched cell chains, three fine searches,
full primary clock and order lifecycle. The foreign-first700-budget failure,
target-first39-pop identity termination despite blockage, and blocker removal
are all mandatory. Seven fixture/counterfactual checks reject loss of chain
order, outcome, samples, commits or the empty terrain producer.

The production engine consumes the same general active history and matches the
whole public journey plus eight Save91 continuations. Native link-vector
allocation/refcounts/stamp representation is explicitly excluded from this
engine comparison; original storage remains checked independently. See
[engine implementation](retail-pathfinding-engine.md#overlapping-targets-retain-fine-cell-insertion-history)
and [spatial evidence](retail-pathfinding-separation.md#engine-active-cell-history).
The inventory contains102 oracle contracts (45 scripts plus57 variants),121
archive audits,77 strict live contracts and142 pinned repository fixtures.

## Compiled chained expressions and public movement

Payoff82 adds `live-chained-expression-captures-261003`, retaining two complete
actual compiled27-expression captures and all ten operand procedure calls.
`verify_wc3_expression_trace.py` authenticates source/map/binary metadata,
bracketed native argument/result words, all three TimerStart calls, the actual
Move destination,304 public markers,430 decisions and432 velocity/facing
commits per run. The real file-backed flat map and all primary/owner updates
are checked with the production numerical probe. No output is reconstructed
from a decimal R2S marker or inferred from the source expression's spelling.

Frozen inputs/results and the entire literal trajectory are in
`retail-chained-expressions-1.27.json` / `retail_expressions.h`. The engine tests
execute real JASS, inspect raw hashtable intermediate/native-result words and
compare normal-frame movement plus eight Save92 continuations in both editions.
The mixed-language regression keeps JASS software arithmetic separate from
Galaxy even after another parser runs. See [expression payoff](retail-pathfinding-engine.md#compiled-expressions-retain-retail-arithmetic-and-evaluation-order).

Captures are `runtime/expression82-final-first.jsonl` and
`runtime/expression82-final-repeat.jsonl`. The preliminary19-expression pair is
exploratory. Each final run retains its own full observer counts, including
variable presentation subdivisions and bounded spatial samples; common primary
clocks, operand/public-order timeline, route/task lifetime and movement words
must repeat exactly. Full lexical/opcode grammar, arithmetic-fault VM lifetime
and unrelated scenario construction remain their existing tasks.

## Regional terrain publication and retained movement

Payoff83 adds `live-terrain-cache-captures-261003`, pinned to complete
`runtime/terrain83-first.jsonl` / `runtime/terrain83-repeat.jsonl`. The fixture
`retail-terrain-cache-1.27.json` retains every observed terrain write, seven
regional fine/adaptive snapshots,12 owned searches, public marker/task lifetime,
724 literal motion commits and the complete primary clock. Both runs have
identical geometry, routing, decisions, ownership phases and motion. Presentation
subdivision and sampled observer extents remain pinned per run separately.

`verify_wc3_terrain_cache_trace.py` checks native producers before numerical
C/retail motion and primary-clock comparisons. It rejects premature terrain
publication, global publication of the remote patch, missing second publication,
lost terrain on footprint removal, an adaptive search substituted for the local
fine retry, incomplete parent/marker/motion lifetimes and changed motion words.
The C literal header is regenerated from the frozen native data and checked
against it. Original assets/binary/decompilation remain external.

Strict O0/O2 reports are `runtime/terrain83-o0-verification.json` and
`runtime/terrain83-o2-verification.json`. The fresh
`runtime/terrain83-strict-final/corpus-results.json` passes seven fresh contracts:
numeric-engine, blocked goal, Captain blocked goal, widget overlap, map load,
compiled expressions and terrain publication (7/7). The inventory is
302 entries/45 original scripts/102 oracle contracts/121 archived audits/79
stronger live contracts/144 fixtures. A copied ancillary capture reference in
the expression entry now correctly pins its actual second expression capture;
the existing strict expression verifier already checked both actual inputs.

The production game regression independently runs public JASS orders/terrain
edits/trees and normal5ms RunFrame through all three natural completions. It
checks724 motion commits, all seven snapshots and4164 suffix commits after
eight Save93 states in each edition. Save-record rejection tests cover malformed
shape, class and extent. See [engine publication and ownership](retail-pathfinding-engine.md#fine-terrain-edits-retain-regional-hierarchy-publication).
General categorical/special accelerator eligibility, nested/reentrant exclusions
and original allocator/unload lifetimes retain FOOT-03/MAP-04/05/06.

```sh
python3 tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /path/to/game.dll --archive /path/to/pathfinding-1.27 \
  --output /new/output/directory \
  --only numeric-engine-exact \
  --only live-chained-expression-captures-261003 \
  --only live-terrain-cache-captures-261003
```

The output directory must not exist. The archive contains both recorded native
captures; observer/source/map/binary hashes and completion controls are required.

## Speed, turn and boundary restart lifecycle

Payoff84 adds `live-movement-lifecycle-captures-261003`. Two complete captures
`runtime/lifecycle84-first.jsonl` / `runtime/lifecycle84-repeat.jsonl` match all
763 motion commits,762 steering decisions,21 searches,24 event-boundary states,
23 complete cell chains and all324 public markers. Their speed/retarget/Stop,
axis/native placement and owner lifetimes are independently pinned. Native
ordinary speed0 requests clamp to150; stationary zero velocity comes from the
turn window. The verifier rejects changed Stop velocity, boundary pose,
occupancy, stationary facing, clamps and incomplete lifetimes.

The same verifier executes34 original default decimal formatter controls with
real startup/software arithmetic; only integer `sprintf` is delegated. R2SW
and shipped CRT string registration are explicitly excluded. The actual engine
JASS native checks those raw words, and the ordinary frame regression compares
all movement and4877 suffix commits from eleven saves. See [engine lifecycle](retail-pathfinding-engine.md#timed-speed-changes-stationary-turns-and-boundary-restarts).

Strict O0/O2 reports are `runtime/lifecycle84-o0-final-verification.json` /
`runtime/lifecycle84-o2-final-verification.json`. Fresh
`runtime/lifecycle84-strict-first/corpus-results.json` passes numeric-engine,
terrain publication and movement lifecycle (3/3). This addition gives304 entries,
45 original oracle scripts/102 oracle contracts,121 archives,81 strict live
contracts and146 pinned fixtures. The terrain entry's unused ancillary `captures`
list now names its own two actual captures; its primary/second inputs and strict
verifier were already correctly pinned.

```sh
python3 tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /path/to/game.dll --archive /path/to/pathfinding-1.27 \
  --output /new/output/directory \
  --only numeric-engine-exact \
  --only live-terrain-cache-captures-261003 \
  --only live-movement-lifecycle-captures-261003
```

The output directory must not exist. All actual capture/source/map/binary hashes,
observer completion/counts and original formatter controls are required.


## Region callback lifecycle

The manifest now has304 entries:102 original executable oracle contracts
(57 argument variants),121 archive audits and81 strict live contracts;147
repository fixtures are pinned. The new `live-region-callbacks-captures-261003`
contract checks both complete native30-second scene85 lifetimes,18 executed
original world-rectangle controls and a literal engine motion/state header.
The pair agrees on629 commits,628 decisions,1000 owner callbacks,318 public
markers,18 event states and17 complete watched-cell chains per run. Public
world/support queries and event primary clocks are frozen separately from the
committed pose clock. No post-removal commit is accepted.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary '/run/media/lofcz/ssd_external/Games/w3/game.dll' \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/region85-recheck \
  --only live-region-callbacks-captures-261003 \
  --only live-movement-lifecycle-captures-261003 --only numeric-engine-exact
```

Use a new output directory. All three fresh contracts pass; separate O0/O2
reports match. The engine regression retains the independent Move-owned region
baseline in Save94 and checks3418 saved suffix commits across normal and10/25/
50ms server frames. Original region allocation/lazy stamps, region geometry
mutation, reentrant filters and bridge/water/UI-limit presentation remain
explicitly excluded. See [engine contract](retail-pathfinding-engine.md#region-callbacks-observe-committed-movement-and-retain-forced-changes).


## Pathing toggle, pause and displacement lifecycle

`live-movement-bypasses-captures-261003` pins the two complete original
`bypass86-first.jsonl` / `bypass86-repeat.jsonl` captures. The inventory now has
305 entries:45 original oracle scripts/102 contracts (57 argument variants),
121 archive audits and82 strict live contracts;148 repository fixture hashes
are pinned. The accepted pair repeats773 commits,769 decisions,13 searches,
10 states,9 complete cell chains and310 public markers per run. The paused
order head and synchronous `IsUnitPaused` values are observed independently.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /tmp/wc3-bypass86-fresh \
  --only live-movement-bypasses-captures-261003 \
  --only live-region-callbacks-captures-261003 \
  --only numeric-engine-exact
```

The output directory must be new. The final strict report passes3/3; fresh
`bypass86-final-O0.json` and `bypass86-final-O2.json` compare all scalar/clock
words and complete route/producers. `retail-movement-bypasses-1.27.json` and
`retail_movement_bypasses.h` retain literal expectations. The engine regression
repeats773 normal commits and6171 Save95 suffix commits through actual native
calls and5/10/25/50ms frames. It does not claim other order families, nested
suspensions or movement modes. See [engine ownership](retail-pathfinding-engine.md#pathing-queries-and-scripted-pause-preserve-distinct-owners).

The earlier `bypass86-startup-aborted.jsonl` lacks a footer and used an older map
when the builder correctly refused overwrite. It is preserved externally as
excluded scratch evidence; the fresh86b map and accepted pair carry matching
embedded-source, map and observer hashes.

## Ground/flight/ground and public teleports

`live-movement-modes-captures-261003` pins both complete
`runtime/mode87-spatial-first.jsonl` and `mode87-spatial-repeat.jsonl` captures.
`tools/frida/verify_wc3_movement_modes_trace.py` checks their original binary,
map/source hashes, complete observer extents, authored terrain/hierarchy,
public markers, profile/query/category publication, Chaos primary phases,
737 committed poses/velocities/facings,734 decisions and15 complete route
searches per capture. It also compares643 own-record observations, excluding
allocator stamps/refcounts but retaining record kinds, rectangles, flags and
query/class/category. The literal C header comes from the frozen fixture and
is checked byte-for-byte. Original presentation clocks are excluded explicitly.

Reproduce with the existing isolated Frida/Wine setup and
`--scenario movement_modes` in `make_wc3_pathfinding_map.py`, then the ordinary trace
flags for task/map/motion/velocity/clock/heading/profile/numeric events. Retain
`--watch-cell 24 26 --samples 10000 --seconds 170`. The observer additionally
reads the active mover's own fine chain during native position queries without
calling new retail functions. `runtime/mode87-source-v2` preserves the exact
capture sources; the earlier v1 captures lack own-record proof.

## Temporary speed modifier travel and removal

`live-speed-modifiers-captures-261004` pins both complete
`runtime/modifier88-v4-{first,repeat}.jsonl` captures and
`retail-speed-modifiers-1.27.json`. Each contains594 physical commits
(590 mover/four caster),588 decisions,13 searches,317 position/order markers,
317 buff queries and eight removal filters. The map digest is
`d305b5e623ee16a18e5dc78771e68346faabe714b617230962a4e4394e5d81e0`.
The strict verifier checks every public filter input/result and native effective
speed word, not only the final position or a trajectory hash. It rejects unknown
route-blocker identities and normalizes the caster's allocation addresses after
joining fine-object/mover observations. Independent O0/O2 arithmetic comparisons
match both captures.

The engine reference includes the590 main-mover commits and17 public boundary
states. It replays the captured spell application callbacks through their real
owning procedures. Spell windup/resource timing and the caster's four steering
commits remain ORDER-01.13; this contract does not claim those as engine matches.
The caster retains actual occupancy during reverse routing. Nine Save97
checkpoints reproduce4356 suffix commits across four server frame sizes, with
all literal speed/buff and movement markers checked.


The extended captain contract `live-captain-extended-captures-261004` retains
120 authored seconds of group and footprint observation. Both captures repeat
the accepted30-second prefix and total10986 engine commits/865 shared footprints.
The visible birth8 Follow task remains standing under the original persistent
completion gate; the contract explicitly rejects claiming natural task
reclamation. Scalar-clock verification accepts the observed24010 primary
advances/4000 owner callbacks, with the completion boundary separate. Additional
host-duration velocity commits receive numerical checks but are outside the
repeated group/clock domain. See the
[extended engine journey](retail-pathfinding-engine.md#extended-captain-travel-ends-in-a-retained-visible-follow-owner).


The fine-storage entries execute five complete original requests across node/
heap growth, natural32,768-node saturation, subsequent success and actual
metadata compaction/free-list reuse. The engine entry compares every semantic
node and fractional route word; host storage substitutes only external Storm
allocation imports, with forced relocation. Storm OOM and adaptive capacity
policy remain excluded. `live-fine-storage-captures-261004` independently pins
and repeats the actual fine constructor and first node/open growth observations;
its three observations do not certify the other captured movement events.
The inventory has153 pinned numerical/type/scenario fixtures.


## Complete public fine results

Payoff91 closes FINE-04.1/04.2 together. `retail-fine-public-results-1.27.json`
freezes24 path-owned167ce0 setup/165ae0 consumption cases and four reuse controls.
All classes cover same-cell, blocked source/goal, disconnected, zero-budget and
suppressed target identity. C compares every fractional route, work/node count,
initial index and endpoint mismatch. Same-cell setup charges zero work and
preserves previous obstruction/class/stamp; it does not test source occupancy.

`retail-fine-public-results-live-1.27.json` pins two complete140-second read-only
captures of12 authored orders. All292 full fine requests repeat and match C at
O0/O2, including four same-cell shortcuts and terrain edits under active movers.
The ordinary Move producer recovers a blocked source before a replacement
order; an already moving owner can refill from the still-blocked fractional
source. These producer paths remain distinct. The strict checker reconstructs
all terrain transactions and compares every full request; it does not certify
velocity or full physical recovery parity. Inventory is314 entries,106 oracle
contracts/46 scripts/60 variants,121 archives,87 live contracts and155 fixtures.

## Adaptive storage and ushort metadata

`oracle-adaptive-storage` and its engine variant execute original constructors,
all hierarchy reducers, six complete searches and five path-owned wrappers.
They pin all semantic node hashes, exact fractional route words and retained
capacities, including native65536/65537→0/1 metadata alias. A4,096-entry
explicit enqueue/pop prefix proves2,048/4,096 open growth and exact queue order;
a following public request clears it over retained backing. That prefix does
not certify naturally occurring gameplay heap saturation. Storm OOM, physical
trajectories and the Way Gate ID allocator remain excluded.

`live-adaptive-storage-captures-261004` independently repeats constructor,
fixed256-record index backing and initial node/open allocation in two bounded
read-only captures. These are storage witnesses, not full movement matches.
[Engine payoff92](retail-pathfinding-engine.md#adaptive-storage-grows-beyond-the-fine-identity-limit)
ports dynamic level/node backing, adaptive open growth, ushort metadata and
route/save extent discipline. Inventory is317 entries,108 oracle contracts
from47 scripts/61 variants,121 archives,88 live contracts and157 fixtures.

## Native Way Gate pool and public exhaustion

`oracle-waygate-pool` executes the unchanged CPaWarp constructor and the
allocation/activation/getter/release bridges. `oracle-waygate-pool-engine`
compares the shared production allocator for259 allocations and every
availability byte. Native and engine O0/O2 contracts preserve IDs1..255, zero
exhaustion, invalid/duplicate releases and first-free refills17/255/0.

`live-waygate-pool-captures-261004` pins two complete public producer tails
with776 repeated semantic events. Hash/length/metadata, all availability/count
words,256 active queries and the final three allocations are checked. The
producer JASS was archived under wc3_movement_bypasses_probe.j and is identical
to the committed capacity probe. There is no hooked explicit completion marker;
completeness is the expected full event tail and installed footer. These are
pool/public-state witnesses, not portal trajectories or overlap evidence.
Inventory is320 entries,110 oracle contracts (48 scripts/62 variants),121
archives,89 strict live contracts and159 pinned fixtures. See the
[engine allocation lifetime](retail-pathfinding-engine.md#way-gate-exhaustion-retains-ability-owned-allocation).

## Source-marker overlap and ordinary route effects

`oracle-gate-markers` and its engine variant execute the original world-to-fine
bridge, inclusive source writes and parent publications for both creation orders
and their removals. They compare64 complete ordinary adaptive requests, every
route word and every final semantic node word. `oracle-cells-engine` additionally
compares the actual production parent reducer against20736 original controls.

`live-gate-markers-captures-261004` pins two complete public producers, each with
eight full marker/class snapshots and the explicit tick9 completion marker.
All2206 hierarchy class bytes and base source IDs repeat and match the native
fixture. Strict checks retain PE/map/observer provenance, footer and raw lengths.
Accepted archived JASS used the capacity filename; the committed overlap probe
is byte-identical. No portal search or physical traversal claim is made.

[Engine payoff94](retail-pathfinding-engine.md#way-gate-overlap-publishes-ordinary-routing-history)
retains these publications and erased overlap history through Save99. Inventory
is324 entries,113 original oracle contracts from49 scripts/64 variants,121
archives,90 strict live contracts and162 pinned fixtures.

Payoff95 adds two original/engine portal contracts and one completed live contract.
The4608 special searches retain all route,node,distance and work words; the2016
consumer controls explicitly supply owner lookup and placement outcomes. Public
placement is independently exercised by two completed cached/fresh/disable
journeys with410 exact production motion commits and3094 saved continuation
commits. The strict checker pins source/map/PE and capture bytes and compares all
407 decisions,410 commits,11 requests/routes,six consumers and two warps. It
normalizes only wall timestamps and declared heap addresses. Failed/unfinished
captures and the host-duration timer tail remain outside this accepted contract.
