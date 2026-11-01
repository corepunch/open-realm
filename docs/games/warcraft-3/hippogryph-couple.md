# Hippogryph target coupling, Couple Instant and dismount

## Scope and data

`Acoa` (`CAbilityCoupleArcher`) and `Acoh` (`CAbilityCoupleHippogryph`) own
explicit target coupling. Their abstract parent `Acou` has no AbilityData row.
`DataA` selects the reciprocal partner type; `UnitID` selects the result at the
ability's current rank. Missing partner/result data rejects admission. The
partner must be living, movable, same-owner and have reciprocal coupling data.
Alliance alone does not satisfy ownership.

| Ability | Public order | Numeric order |
| --- | --- | ---: |
| Acoa | mounthippogryph | 852143 |
| Acoh | loadarcher | 852142 |
| Reciprocal continuation | Internal, no public name asserted | 852141 |

Patched stock `Aco2`/`Aco3` use `code=Acoi` (Couple Instant), a separate family
with its own implementation described below. Accepted native probes add legacy `Acoa`/`Acoh`
explicitly. Base RoC blank DataA/UnitID rows do not describe effective patched
archive data; archive precedence and rejected stock setup are detailed below.

## Game-owned paired command

The implementation registers Acoa/Acoh as `AB_COMMAND`, with command
UI, public target admission and queued activation entering the same game-owned
owner. The target-selection callback checks `G_UnitCanControl`. Each actor owns
an ordinary target chase with captured range zero; the configured ability range
is a terminal check, not the approach radius. The reciprocal actor publishes
852141 with the initiator as target. Both actors must reach their ready state
before merge. The incoming last-ready ability/rank supplies UnitID; the
Hippogryph supplies the result position and facing.

Pair state records partner incarnation, configured ability, mutually linked
serials, active/reciprocal/ready flags and suspension. Cancellation unlinks the
pair before issuing the peer's Stop, allowing synchronous issued-order callbacks.
Explicit Stop retires authored logical owners through `S_UnitAbilityMoveLeave`
after old physical cleanup and before publishing its new order. Lifetime is
rechecked after that callback; only then is the new Stop revision captured.
This makes peer Stop notification precede the original Stop notification.
The early leave reports `NULL`, the actual Stand procedure. Reporting the
command handler `CAbilityStop` instead makes Build discard its retained Haunted
Mine completion handoff. The descriptor correction preserves that handoff and
coupling event order; see the [Stop regression and validation accounting](retail-pathfinding-engine.md#stop-reports-the-actual-stand-procedure).
The measured peer nested-Move case survives. A nested Move on the original actor
inside peer Stop is superseded by the outer Stop in native185; this does not
establish precedence for arbitrary replacement orders or prove successful removal.

Pause/resume retains actor roles: resuming the initiator reconstructs the pair;
resuming the reciprocal actor replays its own continuation and can wait ready
alone. The native observation is bounded, not proof of permanent waiting.

Static callback-revalidation188 follows the post-event admission chain:
`67abe0` publishes issued-target notification; `679cc0` then selects through
`6857e0`, which walks the current ability list and calls `39a5a0`. The coupling
virtual chain `43b760 -> 4be6c0 -> 4beda0` rechecks the internal order, same
owner, partner type and reciprocal coupling data. The implementation therefore
rechecks the original configured ability's presence and resolves the peer's live
alias after the callback. Focused mutation tests cover ownership and ability
removal, including the original actor and configured peer alias. No native
callback ability-removal/owner-change capture establishes the exact failed-head
retirement timing; the static admission chain and engine regression results must
remain distinct claims.

## Save224 and validation status

Save224 serializes the sparse `couple_t` pool, translating its partner as
`F_EDICT` and preserving incarnation, ability, serials and boolean state.
The edict's runtime pool pointer is `F_IGNORE`/`FIELD_RUNTIME`; the loader
allocates and reconnects the pool. `S_CoupleApproachComplete` is registered as
an `F_CFUNCTION` callback through ordinary physical-group persistence. Save223
and earlier layouts are rejected.

Focused checkpoint190 passes 13 coupling tests / 4,826 assertions per edition,
plus 6 terminal-effect tests / 33 assertions and the Save223 rejection test.
The suite covers public/UI orders, same-owner gates, single configured aliases,
queued invalid targets, callback mutation, pause roles, linked and lone ready
saves, cold continuation, and distinct result/pose ownership. Full checkpoint175
predates these changes. Focused tests do not establish complete
native trajectories, all callback interleavings or multi-alias selection policy.
The integrated main build exits 0 and repeats 13 coupling tests / 4,826 assertions
per edition. Private checkpoint190 additionally passes Unit, Order Lifecycle,
Ability Dispatch, Save, Pools, Entangle, Militia, Root and explicit Stop suites:
1,282 test executions / 376,842 assertions across 26 real processes. That total
includes the separately repeated Save223 rejection test. Two zero-test wildcard
selections are excluded and replaced by passing explicit Stop prefixes.

Fresh main checkpoint191 passes all five existing native comparison gates per
edition: Captain473 10/156,882; Group012 20/1,146,726; SupportGoal 472/2,000,955;
Waygate377 39/229,803; point Drop 133/11,432 (tests/assertions). These preserve their
own existing scope and do not certify exact coupling trajectories. Records are
`couple-review190-checkpoint.json` and `couple191-integrated-checkpoint.json`
under `/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/`.

Single configured-alias coverage must not be described as simultaneous-alias
precedence. TARGET-01.2 remains open with 25 leaves; no whole leaf closes.

Static readmission exports are preserved in
[`coupling-readmission`](../../../tools/ghidra/fixtures/research/coupling-readmission/).

## Dismount

`Adec` remains a separate immediate spell: DataA and DataB select the two
companions spawned at the rider, then the rider is removed. Its existing effect
coverage does not establish native dismount trajectory or selection behavior.

## Original target-order producer and lifecycle

The 1.27.1.7085 original DLL (SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`)
provides the directly measured target-coupling contract below. Full checkpoint175
predates the paired-command implementation and provides no validation of it.

Stock availability depends on archive precedence. Base `war3.mpq` lists
`earc=Acoa,Ashm` and `ehip=Acoh`; expansion/patch lists use `Aco2`/`Aco3`
(`code=Acoi`) instead. `Reht` does not add missing abilities. Base RoC blank
DataA/UnitID cells must not be confused with the effective patched rows.
The accepted native probes explicitly add `Acoa` and `Acoh` through JASS,
set Reht level 1, and override only their rank-one ranges to 333. The rejected natural
stock setup is preserved separately. This evidence concerns legacy/custom
explicit target coupling, not the patched-stock instant family.

| Entry | Numeric order | Native behavior |
| --- | ---: | --- |
| `mounthippogryph` | 852143 | Archer initiates |
| `loadarcher` | 852142 | Hippogryph initiates |
| Internal reciprocal | 852141 | Other actor targets the initiator; no public name asserted |

Producer `6f4e3c60` records the partner identity and creates target tasks
`d018a` and `d0171`, each with literal zero range, then submits the reciprocal
order. It ignores the reciprocal admission result. Both actors approach even
at initial separation 256 with authored range 333. Ready callback `6f4ef6f0`
requires two distinct ready owners before merge; authored range is checked at
completion rather than passed as the approach radius. Admission `6f4beda0`
requires the same owner, authored partner type, movement ability and reciprocal
coupling ability. Directional alliance alone is insufficient.

Readiness evidence184 preserves the scoped selector/merge decompilation and
capture rows in
[`coupling-readiness`](../../../tools/ghidra/fixtures/research/coupling-readiness/).
In accepted producer scene 0, Archer/Hippogryph collision radii are 32/8 world
units and maximum velocities are 270/400, respectively; both publish an arrival
range of 1.25 fine units. These measured inputs must be retained when comparing
ready timing. Selector `6f5fda10` dispatches `d0171` and `d0174` to `6f5ff8b0`
with the same nonpersistent argument and a different final subscription/validation
argument; this establishes no arrival-radius difference between the selectors.
Merge `6f4c2f20` reads replacement UnitID at the rank of the incoming, last-ready
ability. Its separate position/facing selection chooses the Hippogryph, so that
anchor does not imply that the Hippogryph ability supplies UnitID. This native evidence does not itself certify implementation or trajectory parity.

Cancellation `6f4bd000` clears the local partner/ready state and invokes the
linked peer inverse `6f4e4250`, which submits Stop 851972. Measured outcomes:

- Stop or removal of either actor stops the surviving paired approach.
- Replacement Move survives on its actor and stops the peer.
- Pausing the initiating actor cancels the peer; resume reconstructs the public
  order and reciprocal order, and the pair merges.
- Pausing the reciprocal actor cancels the initiator; resume replays only the
  internal order. That actor approaches and waits alone through measured age 348.
  The bounded observation does not prove permanent waiting.
- Stop from either actor after the first ready callback cancels the pair.

The public event probe directly observes issued-target publication for 852141.
Peer Stop publishes its issued-immediate event before the explicit actor's Stop
notification. A nested Move from the reciprocal target event suppresses that
actor's coupling constructor while leaving the initiating order admitted;
a nested Move from the peer Stop event also survives. These are the two measured
callback cases, not a proof of all reentrancy. Trigger getters are read before
nesting; restoration of outer trigger-response context was not measured. A
reported target role 0 means neither watched actor, not necessarily a null target.

A separate original-actor probe185 establishes the distinction: in the peer's
Stop callback, a Move issued to the original Archer is accepted, then superseded
by the outer Archer Stop. The final head is zero and the nested Move causes no
position change. This repeats across two observations and an unhooked control
(366 matching public markers). Do not generalize peer-directed nested Move
survival to the original actor or change common order precedence on that basis.
A RemoveUnit attempt at this frontier leaves the original type, life and pose
unchanged through the bounded capture. This does not establish successful
removal or explain why it did not occur; engine lifetime checks remain necessary.

Portable producer/lifecycle specs and compressed captures are
`tools/ghidra/fixtures/retail-coupling-{producer,lifecycle}-1.27.json[.gz]`.
The corresponding `tools/ghidra/research/coupling_*_evidence.py` auditors check
source/map pins, actor incarnations, task callers/ranges, two-ready ordering,
trace footers and exact public streams against an unhooked control. Producer
has 999 accepted markers and a separate 999-marker rejected setup; lifecycle
has 1791 matching markers. Events180 adds 711 matching markers. Each set contains
two observations and one control. Inherited `itemdrop_*` source filenames and
`TARGET-01.2-militia` metadata labels are preserved literally; coupling map,
source hashes and `COUP` markers identify the actual experiment.

Hook ticks are the last public marker, not exact event times. These corpora
remain `engine_parity:false`: no complete trajectory, HP transfer, selection
replacement, dismount or simultaneous configured-alias policy is certified.
See [producer inventory](retail-pathfinding-engine.md#coupling-target-command-producer).

## Couple Instant native producer

Accepted native producer witnesses establish the stock aliases and their distinct
public entries. The game-owned implementation is described separately below:

| Alias | Actor | DataA partner | DataB | Rng | Area | UnitID |
| --- | --- | --- | ---: | ---: | ---: | --- |
| Aco2 | Archer | ehip | 0 | 99999 | 900 | ehpr |
| Aco3 | Hippogryph | earc | 1 | 0 | 900 | ehpr |

Natural patched actors expose these abilities after Reht level 1. The stock
witness has no relevant unit or ability-list overrides. Base RoC AbilityData
has no Acoi/Aco2/Aco3 rows; preserve effective patched archive provenance when
creating test fixtures, including RoC's Data11/Data12 versus TFT's DataA1/DataB1.

`coupleinstant` (852508) is immediate: it selects a matching same-owner movable
partner with an Acoi-family ability inside authored Area. It publishes the
issued-immediate event, constructs its own common spell approach, then publishes
an issued-target event with the selected partner and the same 852508 order ID.
For Archer DataB0, coordination then issues peer `coupletarget` (852507); that
peer target event precedes its own approach construction. Hippo DataB1 creates
no reciprocal order.

Explicit `coupletarget` (852507) enters the ordinary target-spell path. The direct
Archer command does not coordinate the peer: after approach/facing it retains
852507 through bounded age 398 without a rider. Hippo's immediate and explicit
commands merge. Terminal `4ca180` uses the current caster's position/facing and
current-rank UnitID when DataB is nonzero; DataB0 waits. Do not reuse legacy
Couple's two-ready handshake or infer permanent waiting from the bounded trace.

The common producer `40a7a0` creates `d0174` with authored Rng for a far approach
or FLT_MAX for facing-only work. A separate map changes only both aliases'
rank-one Rng to 333, retaining opposite DataB values and Area900. Far requests
from both roles then publish raw 333; near-away requests publish FLT_MAX and
turn without translation. With Hippo/Archer radii 8/32, actual center distances
373,373.25,372.75 classify inside/outside/inside and publish
FLT_MAX/333/FLT_MAX. The equality and inside Hippo cases stay at 1536,1024 and
merge there. Thus Area selection, cast Rng, collision geometry and DataB have
separate ownership.

Portable specs and bundles are
`tools/ghidra/fixtures/retail-coupling-instant-{stock,range}-1.27.json[.gz]`;
auditors are `tools/ghidra/research/coupling_instant_{stock,range}_evidence.py`.
Stock preserves 4,617 public records and range preserves 6,900, each comprising
two observations and an unhooked control. Fifty-four focused evidence tests
verify full streams, task callers/identities, raw cache values, actual spawn
poses, source/map pins and semantic rejection controls. A rejected truncated
stock-v1, unrun stock-v2 and failed range observation remain excluded.

Runtime alias attribution combines stock rows, public ability levels and cache
fields; no direct runtime alias field was exported. These producer corpora keep
`engine_parity:false`; these captures alone do not certify engine execution or
saved continuation. The separate lifecycle witness below covers interruptions. See
[the ongoing producer inventory](retail-pathfinding-engine.md#couple-instant-area-range-and-order-production).


### Couple Instant interruptions

The portable `retail-coupling-instant-lifecycle-1.27.json[.gz]` preserves
8,706 public records from two observations and an unhooked control. Its 31
focused tests validate the complete streams, native identities, map and source
pins, and mutations of role-specific outcomes. Eleven fresh far stock pairs
cover uninterrupted merging and Stop, Move, removal, pause/resume and ability
removal on either actor. Intervention occurs after both command heads are live.

Stopping Archer publishes Archer–Hippo–Archer Stop events; stopping Hippo
publishes Archer–Hippo. Pausing Archer cancels both commands and resuming does
not reconstruct them. Pausing Hippo cancels Archer, but Hippo resumes its own
852507 and merges. Removing Aco2 clears both; removing Aco3 stops Archer while
Hippo retains its physical 852507 approach, finishing without a rider. Actor
removal produces no rider. These asymmetric inverses are distinct from legacy
Couple's paired-ready state.

Merged originals retain their rawcodes with zero life before later handle
retirement. Immediate `G_FreeEdict` would contradict these public observations.
The retained types in the late paused-Hippo sample do not establish a different
retirement policy: that scene has a shorter post-merge observation window.
This evidence remains native-only; implementation and saved-continuation checks
are separate. Run the portable checks with:

```sh
python3 -m unittest discover -s tests -p 'test_wc3_pathfinding_coupling_instant_lifecycle.py'
```


### Couple Instant callback replacement and merge retirement

The accepted native `callback-v1` witness uses six fresh pairs, two observations
and an unhooked control: 1,625 identical public records per run. It replaces
orders with Move at distinct synchronous event boundaries:

| Callback boundary | Observed continuation |
| --- | --- |
| Initial issued-immediate 852508 | Nested source Move survives; no coupling approach is constructed. |
| Synthesized issued-target 852508 | Source approach already exists and is canceled by Move; peer 852507 still starts and can merge with the moving source. |
| Reciprocal issued-target 852507 | Nested peer Move suppresses peer approach construction; source retains its lone 852508 wait. |
| Source Stop callback during an outer source Stop | The outer Stop supersedes the nested source Move. |
| Source Stop callback during an outer peer Stop | The nested source Move survives the outer peer Stop. |

A blanket revision check after every callback cannot reproduce these results.
The normal immediate/target admission guards apply before construction, while
synthetic selected-target publication occurs after source construction and does
not cancel the remaining peer coordination. The witness verifies restoration
of `GetIssuedOrderId` and `GetTriggerUnit` after nested callbacks; it does not
measure target-response restoration or callback removal/owner/ability mutations.

Static terminal `4ca180` first calls each original unit's virtual slot `0x84`
(`6882e0`, world removal), then creates the configured result. On success it
calls slot `0x124` (`698a40`) on the originals. That setter clamps life to zero
through `058190` and dispatches internal `d01e6` via `650ed0`; it is not a direct
storage destructor. In the two observed merge scenes, public death callbacks
arrive for Hippo then Archer. The rider already exists at the Hippo callback,
which sees Hippo life 0 and Archer life 310; the Archer callback sees both at 0.
Both original rawcodes still resolve. Later samples lose both handles.

Stock and authored-range traces put the first both-type-zero sample 150 probe
ticks after the first rider sample (3 seconds). These are sampled endpoints,
not an exact reclamation deadline. Static follow-up resolves the authored delay
to each original unit's `UnitData.slk` `death` value: `6b0870` binds the field
name, `6a9310` reads it, `66bf40` caches it at profile offset `0x74`, and
`678020` retrieves it by rawcode. The `d0155` handler `69bef0` supplies this
value to `69c620`, scheduling `d019d`. Merge clears corpse-eligibility bit
`0x200`, omitting flesh/bone phases while retaining that authored death delay.
The frozen field/continuation exports are in
`tools/ghidra/fixtures/research/coupling-instant-retirement/`.
The life threshold listener `6984c0` emits `d019f` into `679bb0`; the separate
`d01e6` notification is not itself the death trigger. Do not replace this
lifecycle with immediate
`G_FreeEdict`, or equate world removal with the later public death event.
Original exports and accepted capture provenance are retained under
`/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-instant193/` in
`merge-lifetime/` and `callback-v1/`. The portable `retail-coupling-instant-callback-1.27.json[.gz]` includes these
exports and streams; 36 focused tests verify callback identity, heads, exact
streams, sequential death observations and semantic mutations. Run:

```sh
python3 -m unittest discover -s tests -p 'test_wc3_pathfinding_coupling_instant_callback.py'
```

### Couple Instant implementation and validation

`CAbilityCoupleInstant` owns the stock aliases through `AB_TARGET_APPROACH`.
The shared spell path constructs the physical unit-target approach; the concrete
owner handles automatic selection, admission, synthetic and reciprocal events,
DataB-zero waiting, completion and role-specific inverses. It does not reuse the
legacy two-ready handshake. Command publication preserves the callback ordering
above, including continuation after the synthetic selected-target callback.
Removal preparation runs before generic head retirement, preserving the native
peer-head observations and selected-target replay. Pause/resume remains asymmetric.

Merge first marks both inputs absent from world queries and publishes target loss,
then creates the configured rider. On success it applies life zero, internal
ability death and synchronous public death in the merging-actor/partner order.
Original identities remain readable until each raw type's authored `UnitData.death`
deadline. The existing seven-flag buff removal call includes classified positive,
negative and aura buffs while excluding timed life; it adds no protected-buff
policy override. Save226 retains the sparse owner, approach/wait continuation and
absolute deferred-release deadlines; see [save/load](save-load.md).

Private checkpoint204 passed 11 tests and 15,338 assertions in each edition,
covering public/UI admission, range and Area boundaries, selection, interruption,
callbacks, retirement and saved continuation. Its source hashes and results are
in `couple-instant204-checkpoint.json` under the TARGET-01.2 runtime. Merged checkpoint205 passes 2,030 tests and 284,102 assertions across 14
RoC/TFT invocations, covering Acoi, legacy Couple, Entangle, order lifecycle,
ability dispatch, save/load and ordinary spells. Its source and binary hashes
remain unchanged after execution; see `couple-instant205-checkpoint.json`.
These are focused checks. Current-source corpus guard replay/refresh and the
next full-suite run are deferred to the next commit batch.

The native corpora retain their own provenance and `engine_parity:false` scope.
No full trajectory, arbitrary alias configuration, universal tie ordering or
unmeasured callback mutation parity is claimed. TARGET-01.2 remains open with
25 leaves; this implementation does not close the broader producer inventory or
complete firing-poll requirement.

### Public reissue and spell lifecycle: corrected native evidence

The public reissue conclusions used by commits `86583c980` and `194a55333`
are withdrawn. Their bounded original-code harnesses supplied controlled task
class/resolution inputs; the internal task tag is not interchangeable with the
public order ID. Passing checkpoint209 tests therefore does not validate the
old public `852507` inverse exemption or waiting-reissue admission expectations.

Live witness211 runs the real public orders in four stock scenes, with two
read-only observations and an unhooked control. All three match 1,692 public
markers. Reissuing `coupletarget` (852507) on the approaching Hippogryph returns
true and publishes Archer Stop before the Hippogryph target event. Reissuing
`coupleinstant` (852508) also returns true, publishing Archer Stop, Hippogryph
immediate order, then its synthetic target event. Either reissue on the waiting
Archer returns false, publishes no intervention order event and leaves the
existing heads unchanged. All four pairs subsequently merge.

The accepted observer-v3 repeat uses the unchanged211 map/probe and matches its
complete unhooked public stream. The waiting Archer has signed ability counters
`+0x3c/+0x40/+0x44 = [0,0,1]`; validator `3fc460` returns `0xb9` for both
reissues. Its source flag `+0x5c & 0x40000000` is clear. The approaching
Hippogryph has all three counters zero. Its explicit and immediate reissues
release the existing Aco3 reservation on the Archer and Aco2 reservation on the
Hippogryph, then acquire a fresh Aco3 reservation with count zero to one.
Failed observer-v2 captures are preserved and excluded: adjacent interior hooks
were unsafe. These results come from observer-v3, not that failed run.

Witness212 adds passive public spell listeners and a fresh control. Both
observations and control match 1,724 markers; removing the 32 spell-event records
exactly restores211's 1,692-marker stream. Each actor emits CHANNEL, CAST, EFFECT
and ENDCAST, with no FINISH in these four uninterrupted/reissue paths. The
actor's `+0x44` is zero during CHANNEL, one during CAST/EFFECT and zero during
ENDCAST. The waiting Archer retains an open spell lifecycle. This is a signed
ability busy contribution, not evidence for a separate blanket waiting predicate
or an authored timed cooldown.

Witness213 interrupts the first Archer CHANNEL callback with one public Stop.
Two observations and a fresh unhooked control match 433 markers. The original
inverse `40bba0` decrements `+0x44` from zero to minus one before ENDCAST; that
callback observes minus one. Issued Stop events follow Archer, Hippogryph,
Archer. The nested Stop returns true with both heads zero. The outer CHANNEL
continuation then calls `407740`, incrementing minus one to zero, and emits
FINISH without CAST or EFFECT. No rider appears. Neither clamping the counter
at zero nor skipping the post-callback increment reproduces this sequence.

CHANNEL and nested ENDCAST retain the original Hippo target. After Stop,
resumed CHANNEL and FINISH have a null target. The public role-zero marker alone
only means neither watched actor; null is established by the original getter and
captured cleared identity. Stop cleanup writes `+0xe4/+0xe8 = -1` at
`4390f3/4390fa`, after ENDCAST. Completion clears these fields again at
`40e312/40e31c`, then `40e3a0` publishes FINISH through `67d130` before its active
owner guard. Acoi getter `435910`, with profile `4358d0` returning `0x100004`,
returns null for that invalid identity and has no self-target fallback.

Frozen sources, maps, raw streams, audits, summaries and hashes are retained
under TARGET-01.2 runtime directories `coupling-instant211-public-reissue-v1/`
(including `observer-v3/`), `coupling-instant212-channel-v1/` and
`coupling-instant213-channel-stop-v1/`. The213 callback evidence covers Stop;
Move/removal and arbitrary callback mutations are not inferred from it.

The engine now removes the unsupported public reissue exemption and admits
new Acoi orders through a separate signed busy counter. Its specialized approach
publishes the shared spell phase packets, retaining the exact availability owner
across callbacks. ENDCAST precedes peer inverse for ordinary cancellation; death
publishes peer inverse before retiring its own spell. Owned event frames expose
the live target after nested cleanup without storing stack pointers in saves.
Save227 serializes signed busy counts and the open channel, clears the runtime
availability pointer and rebuilds its retained reference on load.

Checkpoints216/217 pass 2,046 tests and 284,604 assertions across 14 focused
RoC/TFT processes, with zero failures/errors and unchanged source/binary hashes.
Coverage includes public approach/wait reissues, the212 phase/counter sequence,
213 CHANNEL Stop with ENDCAST at minus one and FINISH at zero, null live targets,
and waiting-channel save/load followed by Stop. The associated ability, order,
spell and save suites also pass. No full-suite rerun was performed for this
correction; previous green checkpoints remain historical evidence only.
TARGET-01.2 remains open; these checks do not establish wider callback parity.

The configured-key reservation contract uses target-attached `ATrg` markers that
use the configured raw ability ID from field `+0x34` and the acquiring player,
with counted acquisition/release. The four original configured-key controls in
`coupling-instant208-reservation/configured-key-results.json` execute initializer
`41ad80`/reader `6ac5b0` and marker initializer `59ccd0`: `Aco2`, `Aco3`, `X001`
and base `Acoi` retain their respective raw keys. Admission count gate `48bed0`
queries that raw key, whereas selector `402060` queries runtime class `Acoi`.
Thus stock alias markers affect admission but do not match the selector's
reservation preference; the base `Acoi` key does match. The additional owner
relationship is Shared Spells (type 4), from marker owner to querying owner,
without a Passive or reciprocal requirement. These bounded original controls
seed marker attachment and stub unrelated profile services; they do not establish
public competition or general marker lifetime behavior. The211 observer-v3
reissue captures establish the specific release/reacquire sequence above.

The accepted stock214 witness adds two observations and an unhooked control,
with 1,533 identical public records across the three streams. A standalone Archer
wait holds one `Aco2`/player marker on an inactive Hippogryph. A fresh Archer's
explicit and automatic orders reject without issued events or replacement of
its prior Move. Stopping the first Archer releases the marker and permits retry;
with a free second Hippogryph present, automatic selection chooses that eligible
candidate. This proves admission filtering, not runtime-class reservation
preference. Sources, map, streams and audit are retained in
`coupling-instant214-public-reservation-v1/`; the accepted map is v2, while the
unrun provisional v1 is excluded. The separate `range333-v1/` approach capture
also passes two observations and a fresh control, matching 1,525 public markers.
No source terminal event occurs before its intervention, and the runtime ability
cache and physical task both carry the authored range333. It establishes the
same rejection, release/retry and free-candidate outcomes during approach.
Neither capture alone proves the separate runtime-class preference comparator.

The engine owns a counted reservation lease in each Acoi state,
keyed by target incarnation, configured ability ID and acquiring player. Its
generation makes repeated release harmless. Admission uses the exact raw key;
selector preference separately queries runtime `Acoi` and directional Shared
Spells. Derived lookup indexes serve these queries without scanning all world
entities. Save228 persists the held lease and rebuilds the indexes after load;
process-owned indexes are not serialized. Checkpoints218/219 pass 2,058 tests
and 287,862 assertions across 16 focused RoC/TFT processes, with no failures or
errors and unchanged source/binary hashes. They include public rejection,
free-candidate selection, release, save/load and target-slot reuse regressions;
registry tests additionally cover raw-key/owner separation, captured incarnation,
stale release tokens, index reconstruction and directional Shared Spells. The
associated ability/order/spell/save suites also pass. Runtime-class preference
has bounded-original-code predicate evidence, not a new public competition
capture. The green216/217 checkpoint above predates the reservation patch.

Full checkpoint220 validates commit `93d54c6d1`, including reservation commit
`c114321c7`, in an isolated worktree. `make test TEST_JOBS=16` exited zero in
846.94 seconds: 36 fresh JUnit reports contain 10,750 native tests with no
failures/errors, and all 3,332 pathfinding Python tests pass. All five native
gates (Captain473, Group012, Itemdrop, Waygate377 and SupportGoal), 120 focused
Python checks and direct corpus validation also pass. Source, native evidence,
test assets and the WC3 test module stayed unchanged; make rebuilt the test
launcher, whose before/after hashes are recorded separately in runtime
`couple-instant220-validation/checkpoint.json`. This baseline excludes the
in-progress221 trace/observer changes. TARGET-01.2 remains open with 25 leaves;
full-suite success does not establish the remaining native firing-poll coverage.

## Complete firing polls and physical owner order (221–226)

The range333214 observer already recorded every firing: 200 `16e910` arrival
calls and 200 `16fe20` velocity commits, with no event cap. Investigation221–226
preserves their global order, actor association, absolute primary-clock words,
arrival inputs/error/inside/result and committed fine pose/velocity/facing.
Marker age is not the owner clock. The ordinary engine fixture first matched
all 72 stationary-scene polls and commits; moving-pair timer placement then
needed the original 20 ms JASS callback before owner processing. Those clock
corrections did not establish the remaining motion equivalence.

The earliest remaining difference is global row164, counter1368: native visits
A2, H2, then old A1; the candidate visited H2, A2, then A1. Comparing each actor
separately hid this cause until H2's seventeenth postcommit diverged. Original
physical construction publishes H2 before A2; `PathGroup_Activate` (`169840`)
calls `16c880` at `1698fc` with the owner-list sentinel at `PathWorld_Owner+3b0`.
The insertion places each new group immediately after that sentinel, producing
the observed newest-first order. `16c390` is the later completion scan, not
the registration operation.

Native225 shows that H2 counter1385 accepts the refreshed destination and
steers toward fine waypoint `(28.5,40.5)`. Native226 records target A2's stored
rectangle as YX `[38,25,41,28]`; the earlier candidate instead read the previous
committed rectangle `[38,24,41,27]`. Native target detection first flips at
queried cell `(27,39)`, yielding 15 fine points at index0. The candidate's stale
rectangle yields 16 points at index0, starting at `(27.5,40.5)`. This is not a
reason to replace stored occupancy with predicted occupancy or skip a waypoint.
A bounded GDB counterfactual changing only the candidate cohort order passes
the strict comparison of all 400 globally ordered rows. It is diagnostic
evidence, not a production implementation or validation checkpoint.

Both225 and226 have two observations and fresh unhooked controls. All 1,525
public markers and all 400 original poll/commit rows remain identical to214;
226 also preserves225's route decisions. Its 552 cell queries repeat with the
same geometry, links, endpoint decisions and route buffers, except for volatile
per-cell eligibility visit stamps. Raw stamps remain in the captures; identical
global spatial-query history is not claimed. The projection compares captured
fields only and excludes C's additional precommit diagnostic phase.

Durable runtime artifacts are [the strict projection](/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-polls224-strict/native214-strict.json),
[first global mismatch](/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-polls224-strict/first-global-order-difference.json),
[225 route audit](/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-route225/validated-route-summary.json),
[226 spatial audit](/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-route226/validated-spatial-summary.json),
and [original insertion assembly](/GitHub/wc3-analysis/runtime/claude-TARGET-01.2/coupling-order226-static/insert.asm).
These are external research artifacts. The repository also retains the literal
native400-row C/JSON fixture, pinned capture provenance and portable verifier
`tools/ghidra/research/coupling_instant214_poll_projection.py`. Its read-only
`--check --capture-dir <range333-v1>` verifies both native observations, the fresh
control audit and the compiled C header's ordered literal rows. No clock shift,
tolerance, sorting or selective-row omission is allowed.

The game now retains a provisional target-approach cohort during synchronous
coordination. It is absent from the active owner list, including derived-list
rebuilds. Finalization validates the source incarnation/revision, group
identity/generation and receiver before publishing through the ordinary
newest-first insertion. Replaced or removed owners cannot republish; an obsolete
surviving provisional generation is retired without touching its successor.
Save229 rejects direct synchronous ReadGame/WriteGame during publication; public
JASS save requests keep their deferred frame-boundary behavior. The pending flag
is runtime-only and loaded physical order is reconstructed from saved admission
sequences. Committed spatial occupancy and path arithmetic are unchanged.

Checkpoint227 passes 2,062 focused tests and 319,710 assertions across both
editions. Ordinary public execution now matches all400 globally ordered native
arrival/postcommit rows exactly, followed by every engine row in the cold-save
suffix. Public callback tests execute the pending-list rebuild, direct save/load
rejection, Move successor and removal checks. Checkpoint228 passes all five
native gates in both editions, 120 focused Python tests and the614-entry,
3,316-pin corpus manifest. Frozen source and binaries remain unchanged through
those checks. Full220 at `93d54c6d1` remains the earlier baseline; it does not
include this Save229 correction. No full-suite rerun is claimed for this patch.

Checkpoint231 adds the nine accepted193 authored-range333 scenes: all 532
arrival/postcommit rows match in global order, with exact raw clocks and words,
in both editions. The native/header/control projector also passes against both
engine logs. All 22 Couple Instant tests pass per edition (44 tests and 111,180
assertions combined). The fixture uses the actual retail Hippogryph turn rate
0.4 and turn window 61, and preserves the original CpMark lifecycle. This is a
test-fixture correction; production behavior is unchanged. Saved continuation
uses ReadGame from a file after clearing the path state; it does not establish
an SV_Map cold restart. No new full-suite or five-gate run is claimed.

Checkpoint232 adds the six stock-v3 scenes with ranges 0/99999: all 540
ordered native rows match ordinary engine execution and the literal header in
both editions, with fresh native/control verification. All 23 Couple Instant
tests pass per edition (46 tests and 160,440 assertions combined). Production
behavior is unchanged. The stock 540, authored-range333 532 and moving-pair214 400
are separate datasets, totaling 1,472 projected rows; they are not one continuous
capture. Saved suffixes use path-state-cleared ReadGame, not an SV_Map cold
restart. No full-suite or five-gate rerun is claimed for this fixture addition.

Native233 supplies the remaining ready and literal-boundary witness: two
observations and a fresh unhooked control across 11 scenes, with 2,150 identical
public markers. All orders are accepted; the measured range and heading tests
select immediate execution or physical tasks. The three adjacent authored range
words `43a67fd3/43a67fd4/43a67fd5` at distance373 produce false/true/true range
results. Public degree inputs `413758c0/413758c1/413758c2` produce adjacent
headings with true/false/false facing results; `413758b5` produces exactly the
`0.2`-radian window and passes. Only three scenes construct tasks, totaling six
arrival calls and six velocity commits. Already-facing ready scenes construct
none. The earlier193 distances 373, 373.25 and 372.75 remain quarter-world
offsets, not adjacent IEEE754 values.

Save230 corrects ready completion chronology. During producer construction the
ability records readiness without opening CHANNEL. Automatic orders publish the
synthetic target and perform peer coordination first; completion then revalidates
the surviving source/target and owner before consuming readiness. Ready Archer
automatic pairing therefore channels only the Hippogryph before merging; ready
Hippogryph automatic pairing publishes its synthetic target before its own
CHANNEL. Cancellation clears pending readiness. These flags are runtime-only,
and synchronous save/load remains rejected during publication. Checkpoint235
passes 2,070 tests and 417,320 assertions across both editions, including ready
chronology and callback ownership regressions. This focused result is separate
from the earlier full220 baseline and does not claim a new full-suite run.

The portable233 projector independently checks both native observations and the
fresh control, all public/native chronology, and the literal header's 12 poll
rows plus 59 issued/spell event tuples. It rejects missing, extra, reordered and
changed header rows. Typed ability/unit identifiers use native big-endian ASCII
FourCC words; engine host-packed identifiers require field-specific conversion.
Order IDs, clocks, floats and other integers must not be byte-swapped. Only
explicit pointer fields and registry slot/generation pairs are normalized;
public order IDs and numerical inputs remain exact. The original capture task
label and map sidecar's omission of custom alias rows are preserved and explained
in the provenance; independently parsed map bytes and live alias/range fields
prove those rows.

Checkpoint240 verifies the complete eleven-scene engine replay in both editions:
all 59 issued/spell events and all 12 poll rows match, including the final
cleanup event's raw clock. All 26 Couple Instant tests pass per edition (52 tests and 164,654
assertions combined), and the saved full suffix repeats exactly. The fixture waits for the original completion
marker within a hard bound and requires the restored suffix to take the same
number of steps. Iteration236's event-registration fixture error and238's early
stop are excluded from successful replay evidence; neither changes native
expectations. This save continuation uses warm ReadGame, unlike the earlier
path-state-cleared193/214 suffixes; neither is an SV_Map cold restart. The broader
validation is composite: the 20 other236 focused runs pass 2,804 tests and
17,698,746 assertions; the240 Couple runs pass separately,
not another full suite or a repeated235 aggregate. All five237 native gates pass against the
frozen240 candidate in both editions: Captain473, Group012, Itemdrop,
Waygate377 and SupportGoal. Checkpoint241 also passes all 120 focused Python
tests and validates the corpus with 614 entries and 3,316 pins. See `coupling_instant233_ready_projection.py`
and `retail_couple_instant233_ready.provenance.json` for the native scope.

### Full checkpoint242

Full checkpoint242 validates committed `af9c1c5b8`: literal
`make test TEST_JOBS=16` exits0 in 871.96 seconds, with 10,764 native tests,
60,749,479 assertions and 3,332 pathfinding Python tests. All 1,138 source
hashes remain unchanged. The launcher and WC3 module were rebuilt; their before
and after hashes are recorded, not claimed identical. The asset check compares
32 files captured during the run against the final files, not a pre-run asset
freeze. See runtime `couple-instant242-validation/full-summary.json` and
`independent-final-audit.json`. This full baseline predates the Save231 port.

### Bounded force and stop parameter audit

The exact Acoi vtable `6fafc8dc+34c` resolves to `4358d0`, whose instruction bytes
`b8 04 00 10 00 c3` return the constant `0x00100004`. Consequently the shared
producer's ability-return bypass bit `0x02000000` is absent for this class and
its data aliases. Slot `408` resolves to `417f90`; `40a7a0` supplies buffer0.
The ordinary unit-target branch emits `d0174` with authored range or FLT_MAX,
with a literal NULL final argument, or emits no physical task when both range
and facing pass. The trailing action3 is an internal task; it does not establish
a public Stop event by itself.

Caster `+60` bit8 has a distinct, scoped producer. `608ed0` sets it at `60918c`,
calls transient dispatch `688790` at `609195`, then clears it at `60919a`.
The preparatory-item collector accepts exact AIma/AImr/AIra/AIre classes.
Its source gate requires Town AI enrollment (`+5c` bit4), not merely computer
control, Hero status or immobility. `40a7a0` snapshots the bypass before invoking
its own preparation helper, so an ordinary outer Acoi producer does not gain
that bypass from its own preparatory dispatch. Existing observer `unit.flags`
reads `+5c`, not `+60`, and cannot prove this bypass's value.

### Preparatory-item callback producer: accepted native evidence, pending port

Native243 (`coupling-public243-setup-v4/stable-observer-v2`) accepts two
observations and a fresh unhooked control: all 92 public markers agree, and the
131 scoped native rows repeat exactly. Native244
(`coupling-public244-target-context`) adds 12 spell-context records for 104
public markers per run while preserving the complete243 baseline. A Town-owned
Hippogryph with authored maximum mana 200, current mana 0, a mana potion and the
required rank2 Rhpt research issues a costed outer spell. Its preparatory item order
publishes a synchronous callback that successfully issues Acoi. The nested
`40a7a0` entry observes actual caster `+60=8`; it produces no physical task or
arrival poll. The ordinary comparison has **21 Acoi arrivals**. A separate Town
AI arrival before Acoi accounts for the capture's 22 total rows; it is not an
extra Acoi firing poll.

Forced Acoi publishes CHANNEL, FINISH and ENDCAST, without CAST/EFFECT. The
cause is the target-mode state, before range validation: `40a7a0` enters CHANNEL
before setting `0x10000`; `402850` rejects the class-bit4/`0x8000` state without
that mode, and `3fbc54..3fbc64` routes failed validation through `40e030`.
`GetSpellTargetUnit` (`202d00 -> 2027b0 -> v3c8=435910`) returns NULL for this
fresh state. This is a getter proof, not an inference from an unmatched role ID.
The independent X/Y getters still expose cached Archer coordinates during
CHANNEL, then zero during FINISH/ENDCAST. Potion phases target the caster with
zero X/Y. Native244 preserves those distinct target and point semantics.

The potion publishes all five spell phases but leaves mana 0 and its one charge
unchanged after nested Acoi clears the user head. Original `3fa400` gates the
effect on **any live current user head**, not the original order revision.
AIma/AImr/AIre/AIra all resolve `v34c=0x00200001`; none has the `0x02000000`
exception. Successful ordinary use executes the authored resource body.
Charge commitment is armed only on the successful effect path and runs after
ENDCAST: `65b6c0` decrements before publishing use-item events, retaining the
last-charge item identity for their context. Authored `uses=0` sets the native
unlimited flag; a depleted positive-use item is different. Supporting getters,
constructor fields and instructions are retained in `coupling-force237-audit`.

Rejected setup history remains separate: 238 had ineffective zero maximum mana
and no Town flag;239 did not provide a human player and failed loading;241
reached the missing-research gate;243's original inline observer did not finish.
The accepted stable observer and fresh control do not establish the isolated
cause of that earlier failure. None of those incomplete/rejected runs contributes
to accepted counts.

Engine246 originally matched the first 49 public rows before the outer Nomana
rejection; its old 22-arrival fixture count also included the background Town
row. Checkpoint248 now passes the complete 104-marker/12-context replay and 21
owned Acoi arrivals in both editions. Its broader 22 processes pass 2,388 tests
and 447,208 assertions, including all 27 Couple Instant tests per edition. This
validates the243/244 subset before the following ownership corrections.

Native249 (`coupling-public249-ownership-v1`) accepts two observations and a
fresh control: 598 public markers, 737 scoped native rows, 35 contexts and 55 Move
records, with 20 negative controls. Its six scenes establish:

- Ordinary preparation restores mana 150 before FINISH/ENDCAST, where charge 1
  remains. Synchronous USE_ITEM sees charge 0 while still held; the item is gone
  before the outer spell's CHANNEL. The outer spell then spends 50, leaving 100.
- Nested Move is accepted and preserves its public head; potion and outer spell
  continue. Outer CHANNEL/CAST occur at 502, EFFECT 517 and FINISH/ENDCAST 543.
  A revision-only abort would contradict this measured continuation.
- Removing the source suppresses potion and outer phases. Its briefly retained
  type/life handle does not establish continued world presence.
- Removing the potion suppresses both sets of phases and leaves mana 0.
- The transfer attempt returns false and the original carrier completes normal
  use. Successful transfer is unmeasured.
- Removing the outer Xinf ability still allows potion completion/consumption,
  but no outer spell phases; settled mana is 150.

Checkpoint252 verifies native249 scene1 in both editions, standalone and in
the broader run: all 122 public records and 10 raw primary-clock phase records
match. The actor does not translate during the cast, the private Move resumes
without a second issued Move, and saves during facing and casting reproduce the
suffix after clearing path state before ReadGame. This is not an SV_Map cold
restart. The fixture now matches the 2048-unit world, 17×17 W3E and 64×64 WPM,
including inherited hfoo repulse=2 and parameter/group/priority=17; stock earc,
ehip and ehpr use repulse 0/1/1. Iteration251's mismatched fixture profile is retained as
rejected replay evidence. Build252 is warning-free; 24 broader processes pass
2,398 tests and 454,700 assertions with zero failures.

Save231 stores the suspended Move as a pointer-free `moveContinuation_t`
(destination, speed and public order ID). The approach channel transfers and
clears its value when the scheduled cast takes ownership; cancellation discards
it. Load rejects nonfinite values, unsupported order IDs, mismatched public
heads or a continuation whose approach/cast owner is absent. Completion may
resume it only while the finishing cast still owns the actor. The separate
item-identity section follows the entity pools and validates row identities,
free-list state and completeness against actual inventory attachments. See the
[Save231 contract](save-load.md) for the persisted fields and validation scope.

The Save231 candidate owns preparation resource
dispatch, scoped bypass/epoch guards, carrier item effects and saved item-ability
identities. The new USE_ITEM correction dispatches synchronously after decrement
and removes an exhausted perishable only after the guarded callback. Candidate
ordering uses retained identity slots, family priority and native LIFO reuse,
with a snapshot before callbacks. The local identity domain does not claim
literal native global agent ID numbers. Checkpoint253 reruns the 122-record/
10-clock native249 replay and passes 24 focused processes: 2,400 tests and 464,334
assertions, with zero failures. Both editions also pass explicit later Stop/Move
cancellation and current-format malformed-save tests: a nonfinite facing
continuation and a mismatched cast public head reject ReadGame, followed by a
successful valid restore. These interventions are engine regressions, not
additional native oracles. All five native gates pass as composite evidence:
four 253 reports plus the 254 Itemdrop report (136 tests/11,495 assertions after
the added AI restoration item case). The normal non-BZ_TESTS 253 module builds
without warnings. Production sources are identical through 255; only the Couple
Instant test file changed. This is not a claim that all five gates ran against
the later test binary, nor a new full-suite result.

The suspected successful-direct-cast staging leak was retracted. The new 255
direct-API/item-callback-Move regression passes 34 assertions in each edition:
`spell_commit -> S_SpellCancelChannel -> S_CancelSpellCast` already clears the
temporary continuation. No production fix was made for that hypothesis.
Other postvalidation rejection or EFFECT/save compositions are not proved by
this test and remain within their broader ORDER-01.13 scope. The 255 broader
focused run passes all 24 processes: 2,402 tests and 464,402 assertions, with
zero failures. Final verification also passes all 120 focused Python tests in
42.506 seconds and directly loads the corpus manifest: 614 entries and 3,316
pins. The five native gates retain the composite253/254 provenance above.

Native251 (`coupling-cantpath251-v1`) separately accepts 478 public markers,
727 scoped rows and 38 typed records with 20 negative controls. A real blocking
wall causes the Archer's terminal cannot-path inverse: explicit approach releases
its reservation without peer Stop; auto approach releases both reservations and
publishes one Hippo Stop. Fresh-actor eligibility and a later merge show retry
remains possible. Native observer tick labels are not callback clocks. The
portable projector preserves that scope; its engine port remains a draft, not a
verified producer case. Unrelated availability/channel combinations remain
ORDER-01.13.
**TARGET-01.2 remains open: 25 open leaves, zero whole leaves closed.**
