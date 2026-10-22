# Hippogryph target coupling and dismount

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
outside this implementation. Accepted native probes add legacy `Acoa`/`Acoh`
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

The `Acoi` family remains a command stub in the engine. Accepted native producer
witnesses now establish the stock aliases and their distinct public entries:

| Alias | Actor | DataA partner | DataB | Rng | Area | UnitID |
| --- | --- | --- | ---: | ---: | ---: | --- |
| Aco2 | Archer | ehip | 0 | 99999 | 900 | ehpr |
| Aco3 | Hippogryph | earc | 1 | 0 | 900 | ehpr |

Natural patched actors expose these abilities after Reht level 1. The stock
witness has no relevant unit or ability-list overrides. Base RoC AbilityData
has no Acoi/Aco2/Aco3 rows; preserve effective patched archive provenance when
creating test fixtures, including RoC's Data11/Data21 versus TFT's DataA1/DataB1.

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
`engine_parity:false`; lifecycle, pause/resume, callback interleavings and saved
continuation need their own acceptance before porting those behaviors. See
[the ongoing producer inventory](retail-pathfinding-engine.md#couple-instant-area-range-and-order-production).
