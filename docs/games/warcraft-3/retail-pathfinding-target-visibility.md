# Move target visibility and cached arrival

Payoff166 implements Smart unit-target refusal and cached hidden-target
arrival. Smart refuses an unseen target before replacing an active order or
appending a Shift successor. Payoff218 corrects the earlier generalization to
Move: the original native converts an unseen Move target into its captured
point, while a visible hostile target can retain Follow. See
[the verified native fallback and preserved Smart expectations](retail-pathfinding-engine.md#move-normalizes-unseen-targets-before-admission-payoff218). A running group keeps pursuing its last
sampled position while hidden. At that position it validates the target and
ends the Follow parent if validation fails; lifting fog does not revive it.

Payoff251 below closes TARGET-03.1 by integrating the policy mapping and
optional-target admission. TARGET-03.2 retains broader TargetLost producers,
transient-state and policy compositions.

## Original contract

The original is Warcraft III 1.27.1.7085 `game.dll`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Complete Ghidra function bodies were inspected and task-tagged annotations
saved and read back at the following addresses. `MapPathfinding.java` retains
these findings with earlier evidence.

| Owner | Address | Relevant behavior |
| --- | --- | --- |
| Target validation | `6f5fb940` | Null/dead or failed flags-0/mode-4 unit visibility returns `0xdd`; hidden unit returns cargo `0xa9` or `0xaa`, except its transient `0x800000` window. An ordinary non-unit widget returns zero without the visibility query; a hidden widget returns `0xaa`. |
| Order admission | `6f5fbad0` | A failed unit visibility query returns `0xba` before task mutation. Wider order/self/special branches remain open. |
| Target request | `6f05a5c0` | Stop/invalidate the retained local path before publishing a fresh target task. Preserve storage identity; discard counts, indexes and pending requests. |
| Member decision | `6f16a790` | While `unseen_counter` is nonzero, temporarily use a 0.49-fine-cell arrival range, then restore the authored range. |
| Completion | `6f16c390` | Persistent completion is suppressed while `unseen_counter <= 32`. Once eligible, the owning Move arrival handler validates the target. |
| Persistent arrival | `6f5fa7a0` | A valid target retains its Follow task; an invalid target unwinds it and permits the next order. |
| Approach arrival | `6f5ff8b0` | A valid target installs persistent Follow; an invalid target ends the approach. |
| Buffer reset | `6f168740` | Result-clear mask `0xcfffffff` retains the path's `0x01000000` warp-marker bit. |

Assembly is authoritative for the non-unit validation branch: the widget stays
in ESI while the optional unit bridge is held separately in ECX. The current
decompiler incorrectly renders the absent-unit branch as a null widget access.

## Repeated retail evidence

The delivered TARGET-03.2 archive has two observed loss runs and their public
control, plus two observed reacquisition runs and their public control. The
strict verifier pins every capture and preload hash, requires completed public
markers, checks observer/control equality and re-evaluates native policy
records. Six captures cover 17 loss policies, five reacquisition policies,
14,064 public markers and 754 hidden owner visits across observed repeats.

For the ordinary public fog-persistent scene, the engine fixture takes 238
unrounded native owner entries directly from the captured `gtick` rows. Each
entry has 25 words covering clock counter, refresh countdown, unseen count,
cached group destination, admission timestamps, route counts/indexes, scalar
position/velocity, retained arrival range and local retry/wait state. Empty,
unpublished destination columns are excluded from engine comparison; no
rounded report value is used as a numerical expectation.

Re-run the archive verification with:

```sh
python3 tools/ghidra/research/verify_target166_live.py \
  --expected tools/ghidra/fixtures/retail-target-visibility166-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/retail_target_fog166.h \
  --output /tmp/target166.json
```

There was no new live retail capture in Payoff166. These are freshly verified
archived public captures; Ghidra inspection and engine execution are current.
Reproduction scripts and observers are kept in `tools/frida/research/`.

## Engine integration and save ownership

Move owns target status, admission, local-path invalidation and arrival cleanup.
The existing sampler already retained the hidden destination and countdown.
The new raw trace first failed at the approach-to-Follow transition, where the
old local cache survived. After clearing it at the actual task boundary, the
trace exposed premature hidden arrival: the engine used normal Follow range
and lacked the persistent 32-visit completion gate. Both are now implemented.

The public engine test creates and orders units through timed JASS natives,
starts fog through a real modifier and advances ordinary server frames. It
matches all 238 owner entries, then repeats the final 28 entries after saving
inside the hidden episode. Separate production regressions cover immediate
and Shift rejection without disturbing existing orders, hidden approach
completion, hidden persistent completion and no resumption after fog lifts.
Before the changes, the first three regressions failed 21 of 44 assertions;
the cold-save suffix subsequently failed because active fog was not restored.

Fog modifiers previously lived in VM-owned blobs while their active pointer
list lived outside the VM snapshot. Restoring a started blob never rebuilt its
membership. Save135 therefore gives modifiers stable game-owned records and
borrowed JASS handles. The save stream stores all records, including stopped
and script-unreferenced records, then active IDs in their application order.
The loader restores them before binding VM handles and does not reapply an
immediate Start operation. Aliases bind to the same record; destroyed handles
serialize as null. Registry storage and lookup are independent of active-list
order, and records stay address-stable when the registry grows.

Regression coverage also checks overlapping writes, unreferenced active
modifiers, stopped records, aliases, destroy/restart rejection, malformed
active IDs and Save134 rejection. Earlier save formats are rejected according
to the repository's current-layout policy.

## Focused validation

Classic and TFT each pass movement (386 tests / 5,654,221 assertions), API
(351 / 56,739), fog (6 / 213,649) and save (194 / 27,605). These are the
affected production suites; the full repository suite was not run for this
chunk under the authorized pathfinding checkpoint cadence.

## Synchronous world-presence loss (Payoff167)

ShowUnit(false) and successful Cargo entry now notify the retained Move parent
after publishing the target's absent/loaded state. This ends an invalid Follow
before the producer returns, including while still approaching. A waiting
successor activates once; showing or unloading the target does not recreate
the retired order. Temporary combat retains its owning ability and public head
when only its suspended Follow parent is canceled.

Complete original bodies and assembly establish the following contracts:

| Address | Contract |
| --- | --- |
| `6f688300`, `6f651010` | World-presence retirement sets Unit+20 bit1 before synchronously dispatching TargetLost `0xd01a4`. The producer resolves its two -1 player arguments from the target's owner. |
| `6f5ff490` | Validate the event target first. A valid result retains the order; absent/loaded results `0xaa`/`0xa9` retire the Move parent. Wider reissue branches remain open. |
| `6f5fc640`, `6f5fbea0` | Each target-task admission clears the old retained target and unsubscribes before binding/registering the new target. Approach-to-persistent handoff also renews registration. |
| `6f5ff020`, `6f652dd0` | Retain canonical target identity, then register/unregister this Move on that widget's TargetLost event when the subscription argument is enabled. |
| `6f6901c0` | Parallel registration for Unit owner-change `0xd01a2`; that engine policy remains open. |
| `6f0725d0` | New event/subscriber pairs append; updating an existing pair's remap does not move it. The task handoff unregisters first, so its subsequent registration appends. |

Ghidra comments, three recovered names and three assembly-derived x86
prototypes were saved and read back. The two subscription helpers have ECX
owner, stack subscriber/enabled and RET8; RetainTarget has ECX Move, stack
target/enabled and RET8. `MapPathfinding.java` and the persistent type schema
retain these contracts. Ghidra's thiscall parser creates the ECX `this`
parameter implicitly; declaring it again incorrectly adds a stack argument.

The delivered retail repeats contain actual subscribed approach and persistent
task calls (`a3=1` in both), plus the hide/cargo event chains. The strict verifier
checks state publication before the event, validation and handler ordering,
public cancellation/no resumption and hide completion within the same native
call and owner counter. The cargo event occurs during its actual Load approach
at local tick53..54, rather than at the scripted tick60 producer marker. It
checks the unrounded event fields and repeats; earlier six-capture provenance
and observer controls are required before these narrower contracts are used.

```sh
python3 tools/ghidra/research/verify_target167_live.py \
  --expected tools/ghidra/fixtures/retail-target-loss167-1.27.json \
  --visibility tools/ghidra/fixtures/retail-target-visibility166-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/retail_target_fog166.h \
  --output /tmp/target167.json
```

Move keeps a doubly linked subscriber list per target: registration and removal
are O(1), notification and its stack snapshot are O(K) for K subscribers. No
world scan or heap allocation is used for hide/cargo notification. An immutable
delivery identity consists of subscriber slot, incarnation and registration
rank. Removing/reissuing a later subscriber excludes the new registration from
the already-running delivery; nested delivery sees current registrations.
Generic Attack/Repair removal notification retains its existing separate scan.

Save136 stores logical ranks and their global sequence, validates them in
O(N log N), then reconstructs derived lists in rank order. It never substitutes
edict allocation order. All production Follow writers use the Move-owned setter;
direct release also unlinks its derived subscription before clearing the edict.

The new production regressions initially fail for public hide, successful cargo
entry (six assertions), handoff renewal (four) and direct release (two).
Nine focused tests now cover immediate approach/persistent loss, queued
successor, cargo inverse, non-allocation delivery order, renewal, cold load,
malformed save state and controlled nested mutation. Three followers with4,096
unrelated entities produce exactly three notification visits. Nested mutation
and multi-follower ordering are mechanism tests backed by the original
subscription/dispatch contracts; no new multi-follower live retail claim is made.

Five bounded capture attempts in isolated research environments B/C failed
before gameplay, including observer-free attempts and an already working
TARGET-03 map. The Blizzard error dialog and incomplete captures are retained
under `research/payoff167/` and `runtime/payoff167/`; they are not successful
controls or gameplay evidence. Current `target167_*` scripts retain the intended
three-follower public probe. This chunk uses the complete delivered retail
repeats and current Ghidra inspection instead of interpreting failed launches.

Classic and TFT each pass movement395 tests/5,654,395 assertions, unit127/12,708,
combat188/4,620, API351/56,739, save194/27,608, game151/45,507,
order lifecycle35/527 and Way Gate18/2,222. Python visibility/provenance tests
pass17 cases and corpus tests30; the exact staged tree passes393 fixture contracts
and a fresh strict target-loss archive run. The full repository checkpoint is
not repeated for this chunk under the authorized validation cadence.

## Delayed invisibility publication (Payoff168)

`Apiv` now publishes Permanent Invisibility through its ability-owned primary
timer, then delivers synchronous TargetLost to the existing Follow subscriber
list. Queries read the published runtime bit; they no longer infer publication
from a rounded millisecond deadline. Owner/shared-vision Follow survives;
undetected neutral Follow cancels. Removing Apiv does not resume a canceled
order.

Original assembly establishes this chain:

| Address | Contract |
| --- | --- |
| `631360` | Enable reads the authored level duration through `414ff0`, then calls `6696e0`. Its separate `414b50` flag branch remains outside this implementation. |
| `6696e0` | Increment Unit+114 contribution count; only the first contribution constructs the fade. Zero duration calls `68b780` immediately. A positive isolated fade installs near-zero value `3a83126f`, reciprocal duration slope and an upward threshold-one listener. |
| `161c30` | Cancel the previous listener request, check source/direction/slope, compute remaining crossing time and schedule at most four seconds ahead. Slope below `3556bf95` produces no request. The timer minimum is `38d1b717`. |
| `1618a0`, `162250`, `162290` | Evaluate the clamped scalar using original elapsed-time arithmetic; accept strict distance below `3ba3d70a` (0.005), otherwise rearm. Remaining time uses native subtraction/division. |
| `690490`, `68b780` | Listener event `d01d4` refreshes the unit. A nonzero contribution count publishes Unit+5c bit `01000000` before `651010(-1,-1)` TargetLost. |
| `694b20` | Last contribution removal clears the bit before optional reveal notification. Zero-count refresh does not deliver inverse TargetLost. |

The new engine heap contains only pending Apiv fades: next request O(1),
insert/cancel O(log N), no allocations during visibility reads. Deadlines share
the existing primary timer's deterministic serial ordering. Epoch rebasing
changes pending deadlines while preserving the scalar's original epoch/time.
Save137 retains origin, slope, deadline, serial and active state; heap links are
derived and rebuilt after load. There is no new wire field.

Evidence is split deliberately. The delivered TARGET-03.2 Frida archives contain
two exact observed repeats of stock Apiv scenes3/4/5, plus the prerequisite
six-capture repeat/control/provenance checks. `verify_target168_live.py` verifies
deferred publication, synchronous validation and retained/canceled public heads
until the scene's explicit Stop, then checks Stop clears the order. These are
reused complete captures, not a new live launch.

`verify_target168_fade.py` executes complete original listener consumers and
shipped CRT/static initializers for27 controlled duration/origin cases. It
checks frozen request/publication words and the production test header. Timer
wrapper ownership and the event receiver are explicit stand-ins; this is not a
claim of complete public modifier construction or full scene trajectories.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_target168_fade.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --fixture tools/ghidra/fixtures/retail-invisibility-fade168-1.27.json \
  --header games/warcraft-3/game/tests/retail_fade168.h --output /tmp/fade168.json
python tools/ghidra/research/verify_target168_live.py \
  --expected tools/ghidra/fixtures/retail-invisibility-loss168-1.27.json \
  --visibility tools/ghidra/fixtures/retail-target-visibility166-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/retail_target_fog166.h --output /tmp/loss168.json
```

Failing-first tests initially exposed14 missing publication/cancellation
assertions; expanded deadline cases exposed158 failures and the tiny-slope case
three more. Seven final production regressions pass19,540 assertions per
Classic/TFT, covering public ability mutation in approach and persistent Follow,
all27 listener chains, cold save, exact public timer delivery, cancellation,
reveal restart, epoch rollover and4,096 equal-deadline/canceled owners.
Neighboring spell401, game151, unit127, combat188 and API351 tests pass in both
modes. The earlier full Classic movement run passed401 tests/5,673,883 assertions;
the final epoch/slope additions were checked with the focused runs. Full repo
validation remains on the authorized batch cadence.

The neighboring ability-dispatch suite also exposed three global timer messages
incorrectly accepted with a unit owner. The timer hooks now reject unit-directed
calls, matching Move's global-maintenance contract.

Ghidra annotations, two partial scalar/listener layouts, Unit+114 and eight
assembly-verified ABIs are saved and mirrored in the mapper/type schema. Raw
assembly, readback, red/green logs and verifier reports are retained under
`/GitHub/wc3-analysis/runtime/payoff168/`.

This advances TARGET-03.1/03.2 without closing their full scopes. Overlapping
contributors, Ghost/buff/Shadow Meld publication, arbitrary reveal/level-change
composition, negative/exceptional fades, detection/global policy producers and
presentation alpha remain required. No new TODO leaves are introduced.

## Remaining scope

At Payoff168, the full archived policy matrix was research evidence rather
than integrated behavior. Subsequent Payoffs182,215..224 and251 below implement
Blink, ShowMap, reveal, caller policies, point reissue and owner-change work.
Broader dynamic TargetLost compositions remain open under TARGET-03.2. The
multi-member blocked-arrival branch with the 20-visit and squared-distance-256
gates also remains outside this singleton implementation.

See [remaining task scopes](retail-pathfinding-todo.md),
[target refresh](retail-pathfinding-target-delays.md), and
[save/load](save-load.md).

## Blink notification window (Payoff182)

Blink had relocated the caster without emitting retail's TargetLost. Visible
Follow happened to survive, but a Blink into fog left the old pursuit active
until later owner work. `blink_execute` now publishes its destination before
notifying the existing target-specific subscriber list. It closes the temporary
world-hidden exception before destination art; the old route and target-refresh
countdown stay retained when validation succeeds.

Native `4c95c0` copies software scalar words atf8/100, queries support/admission,
invokes Unit virtual180, sets widget20.800000 at4c9622, calls complete651010,
clears800000 at4c9634, then publishes relocation completion through6510b0.
The flag is a shared unit bit: nested Blink clears it without restoring an
outer value. `5fb940` still rejects death/null and performs flags0/mode4 detection
and fog. Non-unit widgets never receive this unit-only exception.

`target_loss_transient` is a runtime field. Save145 clears it in the copied
serialized edict and again on load, leaving the live callback unchanged. A save
inside the Blink notification resumes with ordinary validation. Save144 is
explicitly rejected. Presentation/network state has no new field.

The handoff's plain-return assumption was incomplete: assembly4c9648 is RET8.
The two unused stack words, owner30 and scalarf8/100 layout are now typed,
saved and read back in Ghidra, with the mapper and schema synchronized.

Verification:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_blink.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --fixture tools/ghidra/fixtures/retail-blink-validation182-1.27.json \
  --output /tmp/blink-original182.json
python3 tools/ghidra/research/verify_target182_blink.py \
  --expected tools/ghidra/fixtures/retail-target-visibility166-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/retail_target_fog166.h \
  --output /tmp/blink-live182.json
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
  build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.target182_*'
```

The90-case original oracle executes the full Blink, event-packet construction
and Move validation bodies, with position admission, virtual receivers/dead/type
queries and visibility as controlled boundaries. It does not establish whole
spell timing, full fog policy or placement parity. The archive verifier rebuilds
all22 prepared loss/reacquisition scenes from four complete Frida observations,
compares two observer-free controls and checks the reachable Blink notification
at7968 after public issue7957. No new retail run occurred in this chunk.
Seven production regressions cover actual public Blink dispatch, retained and
cancelled Follow, nested callback clearing and a callback-time cold save.
The wider visibility TODOs remain open; no new TODOs are introduced.

## Visibility policies and optional target admission (Payoff251)

TARGET-03.1 is closed by the policy mapping, original-code matrix and public
admission regressions below. TARGET-03.2 retains broader dynamic loss producers
and their compositions. Earlier fixtures remain byte-for-byte unchanged.

### Query policy and producers

`6f66fdd0` first requires a world and its `+3e0` lifecycle Boolean. TLS slot13
record flag`0x200` adds flag1. `6f1dd920` evaluates detection and fog independently;
`6f699b20` supplies the per-unit reveal fallback after an unsuccessful query.
Ownership/shared vision bypasses detection, but does not bypass the mode4 cell.

| Input | Effect | Verified producers/consumers |
| --- | --- | --- |
| Flag1 | Skip fog, retain detection | `5fd270` optional target filter; Attack `4968e0` with requireVisibility0 |
| Flag2 | Skip detection, retain fog | Move virtual63 at `5fa49b` |
| Flag4 | Force evaluation of detection; Boolean result unchanged | `1dd920` branch; all combinations in the original-code matrix |
| TLS`0x200` | Add flag1 | Synchronized ShowMap command, already integrated in Payoff215 |
| Unit`+148/+14c` | Direct/alliance-expanded reveal bypasses failed fog/detection | `699b20`; UnitShareVision and cached expansion integrated in Payoff217 |
| World`+3e0` | Enable the entire widget-visibility query | Lifecycle writers below; distinct from FogEnable/FogMaskEnable and ShowMap |

Complete assembly bodies identify lifecycle writes to `+3e0`: reset/teardown
`1dfaf0` clears at`1dfaf9`; transition preparation`1e05a0` clears at`1e05c6`;
load-game`1e3730` sets at`1e37e6`; fresh-map start`1e5a10` sets at`1e5a47`;
final pathing initialization`1eab40` sets at`1eab56`, before installing target
visibility callback`23a760`. The two-instruction setter`1e9930` also sets it.
These are static producer findings, not new live reload measurements.
`WC3WorldVisibilityPrefix` records the verified vision/player-count/gate fields
in [the partial type schema](../../../tools/ghidra/fixtures/retail-visibility251-types-1.27.json).
Twelve task-tagged comments, the setter function and type were saved in Ghidra
and read back with `changed=false`; `MapPathfinding.java` preserves the mapping.

The original-code oracle executes `66fdd0`, `1dd920`, `1ddff0`, `1ddee0`,
`1e0b80` and `699b20` unmodified. Its 6,912 cases cover flags0..7, TLS off/on,
null/disabled/enabled world, invisibility, owner/detector masks, absent/direct/
expanded reveal, masked/fogged/visible cells and modes4/7. Controlled boundary
stand-ins provide TLS lookup, owner/bridge virtuals, fine pose, canonical
mask access and the CRT cookie check. This proves those supplied policy cases;
it does not establish that every combination is reachable through public play.

### Reachable validation branches

The [36-row handoff branch table](../../../tools/ghidra/fixtures/research/TARGET-03.1-expected.json)
is integrated unchanged, together with both frozen TARGET-03.2 source tables.
Its generator is rerun to check every row and witness. Two assembly corrections
are recorded here instead of rewriting the historical table: V3 has a visible
non-unit half returning0 without a visibility query; A1's function entry is
`5fa7a0`, not`5fa7b0` (call`5fa80f`, return`5fa814`).

| Validator/order condition | Result and observation |
| --- | --- |
| Null/dead target | `5fb940` returns`0xdd` |
| Non-unit widget | Hidden`0xaa`; otherwise0 without a unit-visibility query |
| Hidden unit outside transient`0x800000` | Loaded`0xa9`; otherwise`0xaa` |
| Unit transient`0x800000` | Skip hidden-state gate, still evaluate visibility |
| Flags0/mode4 visibility fails | `5fb940` returns`0xdd`; order check`5fbad0` returns`0xba` |
| Self | Order check rejects; this is separate from target validator success |
| Optional Move admission fails | Native`207160` flags6 validates captured point and constructs a NULL-target packet |
| Smart admission fails | Reject before active-task or FIFO mutation |

The frozen Payoff182 original validation fixture is re-executed unchanged,
including 64 branch combinations and null target. Earlier public policy
captures supply the live hidden/cargo/visibility/arrival witnesses; Payoffs215,
217 and218 supply ShowMap, reveal and Move visibility-fallback evidence.
The remaining dynamic loss-handler reissue combinations belong to TARGET-03.2.

### Engine payoff and fresh public evidence

Previously only visibility failure became a point Move. The native fallback
also accepts self (`0xdd`), ShowUnit-hidden (`0xaa`) and dead (`0xdd`) targets.
`CAbilityMove` now normalizes these before replacing the current task or adding
a Shift successor. The FIFO stores the issue-time predicted point without a
retained target identity; queued activation also excludes optional self binding.
Removed handles remain rejected before native validation. Smart continues to
reject all three and preserves the active head, physical group and FIFO.

Two fresh read-only retail observations and an observer-free control each
complete 39 identical public markers. Eight scenes compare Move/Smart for self,
hidden, dead and ordinary visible units. Each observation records125 matching
public/native events:39 markers,13 task factories,4 Move admissions,5 target
packets and63 query entries. Move results`dd/aa/dd/0` produce three NULL-target
packets and a retained visible target; only visible Smart succeeds. Observer
tick labels at native boundaries refer to the preceding public marker, not a
new precise simulation timestamp. The first launch failed before probe startup
and is archived as rejected evidence.

Fog owns `G_FowPlayerCanQueryUnit` with named independent skip-fog and
skip-detection flags. Move's existing Track query delegates flags0; Attack's
existing detection-only policy delegates flag1. This is constant-work dispatch
with no allocations or new entity scans. The game API serves mode4; the oracle's
mode7 coverage is not an engine mode7 claim. Synthetic tests without a fog grid
retain their prior convention, which is not the retail world lifecycle gate.

Two production admission regressions first failed29 of125 assertions before
the fix. They cover immediate/Shift self, hidden and dead normalization, Smart refusal,
exact retained point words, FIFO shape and cold save/load. A third regression supplies policy
coverage using576 literal
original-code rows using actual fog writes, synchronized ShowMap, unit reveal
and detector state. Save163 remains unchanged.

Reproduce the complete focused original/evidence/engine verification with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_work251.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/visibility251-new.json
```

The verifier pins2,521 original instructions, capture sources, public/control
bytes, the literal engine matrix and frozen branch tables. Classic/TFT run
separately for each named test filter, with nonzero test counts and clean JUnit
required. Nine Python rejection tests cover identity/result changes, truncated
captures, instrumented controls, preload bytes and incomplete/changed matrices.
Classic and TFT each pass349 tests /268,724 assertions.
Focused validation follows the authorized twelve-commit cadence; full
checkpoint250 already passed. Raw evidence is archived under
`research/TARGET-03.1/payoff251/` in the original-analysis workspace.

## Authoritative fog publication and reacquisition (Payoff252)

Retail fog is an ordered primary-clock request, not per-frame derived work.
`008260` constructs4/10 using the shipped scalar helpers: the period is
`3eccccce`, which differs from a host0.4 literal. `28ba80` arms embedded UI
control280 for event80269; `289260` rearms before tail-calling `251ac0`.
Map start1e5d77 registers it, and teardown1dfb11 cancels it. The CRC loop
`24f570` is unrelated to fog scheduling; its16-row grouping is not an
incremental visibility algorithm.

Two new read-only five-scene repeats and an unhooked control have identical
1,067 public markers each. Each observed run records245 authoritative fog
compositions with identical owner counters. In the short-fog Follow scene,
DestroyFogModifier runs at1300, fog composition at1304, and the first visible
Follow sampler at1305. It resets unseen14 to0 while retaining countdown2;
reacquisition does not bypass the next destination sampling deadline.
All618 follower and624 target raw rows match both earlier archived retail
observations. The frozen expected fixtures were not regenerated from the engine.

The engine dispatches fog through the existing ordered primary timer merge,
including exact scalar deadlines, registration serials and300-second rebasing.
It retains the completed plane between requests, rather than rebuilding it at
the end of every server frame. This removes repeated fog work and fixes early
reacquisition. It does not establish a measured frame-rate improvement.

A save during the hidden window exposed a second omission: completed visibility
and exploration were absent from saves. Retail250d00/250120 save/load their
four original planes. Engine Save164 now saves logical current visibility and
exploration, consumed-viewer membership, the next fog deadline and its serial.
Load reconstructs row/packed caches and marks client deltas dirty without
recomposing visibility; ClientBegin preserves the restored plane. Save163 and
older formats are rejected. Network messages are unchanged.

The failing-first public-JASS replay now compares every follower owner row
1108..1474 and target row1091..1474, then repeats from a cold save at follower
row190 while the modifier remains active. Additional checks cover deferred Stop
publication, epoch rebasing, plane restoration and corrupt/truncated state.
The complete raw fixture remains available beyond this bounded engine gate.
A separate original point-task restart at1482 first diverges in the target at1483
and in the follower destination at1495; it is explicitly excluded, not rewritten.
TARGET-03.2 remains open. Existing synchronous modifier-start application is
unchanged; these deadline-aligned scenes do not certify off-deadline Start.

Reproduce the focused contract with:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_fog252.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/fog252-fresh.json
```

The capture bundle and specification are
`tools/ghidra/fixtures/retail-fog252-1.27.json{,.gz}`. Native instructions,
callers and a partial fog-plane structure are reproducible through
`tools/ghidra/research/Work252Evidence.java` and recorded in
`MapPathfinding.java`. The external archive is
`research/TARGET-03.2/payoff252/`. Its first two attempts reached the inherited
campaign shadow-map size error and were interrupted; they are excluded. The new
map changes only `war3map.shd` to64x64 zero bytes, leaving probe, pathing, terrain
and object data byte-identical. The accepted observations are3/4 and control1.

## Public point and location fog queries (Payoff253)

`IsVisibleToPlayer`, `IsFoggedToPlayer`, `IsMaskedToPlayer` and their three
location variants now read completed simulation fog through `G_FowPointState`.
They previously returned false unconditionally. Each query performs a bounded
coordinate conversion and two cell reads; it does not replay sources, expand
alliances or allocate state. Location wrappers resolve the location and call the
same classifier. Null locations/players return false.

Original coordinate owners206910/2053d0/2058c0 clamp through
`Widget_ClampWorldPoint`, convert through1ec0c0 and call1e0b80 with the current
visible and masked words. Masked2c is positive unexplored storage; the engine's
logical explored plane is its inverse. The original wrappers force visible
bits12..15, so the four neutral players always see the queried cell.
The calling client's fog policy applies independently of the queried player:

| Mask enabled | Fog enabled | Visible cell | Explored cell | Unexplored cell |
|---|---|---|---|---|
| No | No | Visible | Visible | Visible |
| Yes | No | Visible | Visible | Masked |
| No | Yes | Visible | Fogged | Fogged |
| Yes | Yes | Visible | Fogged | Masked |

The256-case original-instruction comparison covers all four flag pairs,
all16 players and all four visible/explored bit combinations, including visible
with exploration unset. It checks preserved registers, return-stack balance,
input-object immutability and stack guards. The engine matches these outputs.
This is a query policy; it does not change the stored planes.

Two read-only retail observations and an unhooked control agree on all32 public
markers. Before activation the three locations are masked. The keep, Stop and
Destroy cases all reveal synchronously in their creation/Start turn at counter1094.
Stop/Destroy remain visible through sample24 before composition1104, then become
fogged. The remaining modifier stops at1127, stays visible through sample32
before composition1130, then becomes fogged. Location and coordinate queries
agree at every marker. Disabling both display flags reports visible immediately;
restoring them recovers masked without a fog composition.

The actual public-JASS engine replay matches every marker and owner counter,
then repeats the suffix after cold save/load between activation and the next
composition. Existing same-turn exploration tests and earlier retail fixtures
remain unchanged. This establishes the public creation/Start sequence; it does
not attribute synchronous painting to the small original Start flag setter alone.

A failed first probe used radius96, which truncates to zero on this retail map's
128-unit fog grid and writes nothing. Radius160 produces the accepted witness.
The engine still uses64-unit fog geometry: matching these three query centers
does not certify arbitrary circle footprints, blocker geometry or nonfinite
coordinate handling. Those gaps remain under TARGET-03.2 and the existing
coordinate/domain audits. The independent point-task restart exposed in
Payoff252 also remains open.

Reproduce with a fresh report:

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_fog_queries253.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/fog-queries253-fresh.json
```

`retail-fog-queries253-1.27.json{,.gz}` freezes accepted observations11/12 and
control4 with original preload bytes, native instructions and source hashes.
`Work253Evidence.java` saves query owners, xrefs and the corrected masked-plane
field; `MapPathfinding.java` records the same evidence. The external archive
`research/TARGET-03.2/payoff253/` retains unsuccessful/no-op experiments separately.
On these owned Wine environments, invoke `point214_ui_input.exe PID continue`
directly: it is a shell wrapper, not a PE to pass to Wine. An X11 Space event
alone did not advance DirectInput's loading screen.

## Attack guard timers restart neutral movement (Payoff254)

The point-task restart excluded by Payoff252 comes from Attack's guard-return
request, not a Move retry or fog callback. A neutral mobile unit retains its
spawn guard anchor while obeying a point Move. Attack evaluates that anchor
periodically; a point arrival evaluates it again. Issuing another Move does not
cancel the separate Attack request.

| Original owner | Address | Contract |
|---|---|---|
| Poll initialization | `6f00b570` | Initialize `d6bf60` to two game seconds. This is independent of map tuning. |
| Attach / guard task | `6f497c50` / `6f49e880` | Neutral/structure policy retains the anchor and starts evaluation. Guard task `d014a` runs after point completion. Ordinary player mobile units use a different anchor/cancellation branch. |
| Range evaluation | `6f495610` | Mobile predicted pose against captured X `2ac`, Y `2b4`, and authored `Misc.GuardDistance` at `27c`, through the existing source-collision predicate. |
| Poll | `6f499030` / `6f49d220` | `d01ad` evaluates every two seconds; an outside result switches to return. |
| Return arm | `6f497330` | Replace the request with nonperiodic `d01ae`, using authored `Misc.GuardReturnTime`. |
| Return expiry | `6f497fc0` | Replace the active task with Move `d0012` to the retained anchor. Stop/invalidate commits pending motion and clears velocity between owner visits. |
| Continuation | `6f498ff0` | Appended `d0013` requests optional `Asla` SleepAlways. It is not a public Stop command. |

In the retained scene, the target starts at1568/288 with range600. Polls1157
and1224 find it inside; poll1290 finds it outside and arms five seconds. Its
first point arrival1316 rearms five seconds. Public Move1324 leaves that request
live. Expiry1482 replaces its task; target words first change1483, followed by
the follower's destination sample1495. Retail also visits the old empty owner
at1483. That duplicate remains in the raw fixture.

Two final read-only Frida observations and an observer-free control agree on all
215 public markers each. All618 follower and624 target rows remain byte-for-byte
equal to the original Payoff252 capture. The observer records the return
producer's native call chain, timer delay/periodicity, anchor, range and pending
pose before/after replacement. Its raw mover field named `clock` is offset8c
and is not used as a time source.

The engine now keeps the request in Attack's existing indexed primary-timer
heap. Updates cost logarithmic time in active Attack requests; expiry does not
scan all scenery. Guard anchor, captured range, phase, deadline and insertion
serial survive Save165; load rebuilds heap membership. Ownership changes, death
and entity removal unlink the request. Other innate owner-change notifications
remain intact, and Move receives its notification once despite its two registry
roles. The ordinary player Stop guard remains a separate policy.

Timer rearming uses the exact request deadline. Physical range tests and pending
pose integration use the current source-clock quantum. These clocks differ when
a request falls between quanta. Using the deadline for both produced a small,
reproducible position overshoot; preserving the distinction fixes the original
word comparison without altering its expectations.

`wc3_movement.target254_neutral_guard_restart_matches_retained_retail_rows`
executes the public JASS scene through counter1723 and repeats the suffix after
a cold save before expiry. It checks616 follower visits,25 words each, and the
first target visit per counter. Empty-buffer destinations without a published
pointer retain the earlier explicit comparison scope. The scripted Stop and
its last two visits, the duplicate empty owner, structure-disabled heading,
early global neutral-passive gates, optional sleep continuation and wider
creep/damage/JASS guard policies are not certified by this slice. TARGET-03.2
remains open for the remaining policy compositions.

The minimal movement fixture lacks UnitWeapons.slk. This test installs an
authored `weapsOn` row to create the Attack owner that the retail Footman already
has; it changes no expected motion word. Lifecycle tests use non-stock guard
range700 and return time7 to verify tuning, then test owner transfer, death,
removal, rebasing and save/load. The first regression failed at target1483 and
the follower suffix before the implementation.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_guard254.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/guard254-fresh.json
```

`retail-guard254-1.27.json{,.gz}` retains final observations4/5 and control1,
374 original instructions and source hashes. `Work254Evidence.java` saves the
partial `WC3AttackGuardPrefix`, function comments and xrefs; `MapPathfinding.java`
retains the same mapping. External archive `research/TARGET-03.2/payoff254/`
also keeps earlier read-only observations, failing-first logs and the identical
saved-program readback. The map builder only shortens the original probe to its
first complete scene; terrain, pathing and object data are unchanged. See also
[guard ownership](guard-position.md#retail-guard-system-follow-up).

### Periodic guard request identity (Payoff255)

An inside guard poll returns without touching its timer. The original event
request dispatcher `0542d0` then rearms the same request through `053630`,
retaining its unsigned serial. Guard evaluation uses this event-timer path,
not the agent dispatcher `054370` / `053710`. Both families preserve periodic
identity, but hooking only the latter records no guard requests.

Final read-only observations5/6 confirm four guard rearms per run, at owner
counters1157,1224,1619,1686. The first pair retains serial12; the later pair
retains76. Canceled initial serial10, replaced return38 and retired poll76 pop
without callbacks. The active return41 expires1482. All618/624 trajectory words
and215 public markers remain equal to the prior captures and observer-free
control. Request pointers are compared within each capture; normalized records
retain deadlines, periods, flags, serials and event values across captures.

The engine now advances the existing poll deadline without allocating a new
request serial. A two-unit ordered-drain regression failed four serial checks
before this change and passes afterward, including late catch-up and a cold
save. This keeps ties stable and avoids unnecessary global serial consumption.
The heap remains logarithmic in active requests. Explicit guard-task reevaluation
and transitions into return still allocate a replacement request as before.

`retail-guard255-1.27.json{,.gz}` pins the13 request events per repeat and139
original instructions. `Work255Evidence.java` and `MapPathfinding.java` preserve
the actual event-clock call chain. Initial observations1..4 retain valid motion
but missed the request class, so they are excluded from request evidence.
A controller attempt without Python `-I` failed before launch because a local
`/tmp/dis.py` shadows the standard module; use isolated Python for temporary
controllers. The external archive is `research/TARGET-03.2/payoff255/`.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_guard255.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --report /tmp/guard255-fresh.json
```

### Complete listed target-loss and reacquisition policies (Payoff256)

TARGET-03.2 closes the17 delivered loss policies and5 short-fog reacquisition
policies. The engine implementations from166..255 remain the owners; this
checkpoint joins their regressions to the immutable retail scene contracts and
adds the missing compositions. It introduces no case-specific movement rules.

| Delivered policy | Production regression coverage |
|---|---|
| Persistent/approaching Smart hidden arrival | target166 hidden arrival and exact public fog trajectory |
| Shared vision overridden by afterUnits fog | target256 after_units_fog |
| Undetected/owned/shared Apiv loss | target168 approach/persistent and exact fade timer tests |
| RemoveUnit / KillUnit | public_smart_follow original target remove/kill reuse trajectories |
| ShowUnit / cargo entry and undo | target167 synchronous world-presence producer tests |
| Blink temporary visibility window | target182 public Blink, nested/dead/fog checks |
| Owner transfer | target221 public transfer, immediate recovery and saved ownership |
| Paused target | target256 paused_target through cold save and delayed resume |
| Already fogged target at order time | target166 refusal and target251 admission/FIFO tests |
| Smart issued during Apiv fade, then explicit reissue | target256 order_during_fade with non-stock2.75 authored duration |
| Attack invisibility/fog | target223 synchronous loss/recovery and target222 cached hidden chase |
| Five short-fog reacquisitions | target256 Smart persistent/approach, Attack and Move, each fresh and saved; exact original Smart252/254 rows retained |

The new reacquisition test retains14/40/26/13/13 hidden visits in minimal
arenas, then checks the remaining countdown before the first new target sample.
A target becoming visible clears unseen immediately but does not bypass that
countdown or replace the physical owner. Hidden cached destinations, owner
identity and public head survive a cold save in each composition.

The pause composition exposed a production gap. `S_RunMoveTimers` restored the
retained point command at its delayed deadline but did not create its physical
group. This left movement to the ordinary entity-order fallback. Both original
loss captures instead show the new target group first at counter9491 after
unpause9490, ahead of the retained follower group; the target gains velocity at
9492. The old paused target group has one zero-velocity retirement visit9291.
The engine now creates the new group when delayed resume activates its task,
without publishing another user-issued order event. No save layout changes.

The first new regression failed because the target remained outside the physical
owner scheduler. An initial harness also omitted `S_RunMoveTimers`; correcting
that omission alone still failed. After the engine fix, the paused target moves
through the owner pass and the follower retains its original identity. The
unchanged pause86 regression still matches773 original commits and6171 saved
continuation commits. No expectation was rewritten to accept the change.

`retail-target-policies256-1.27.json` assigns every delivered scene to executable
engine tests, pins the existing research expectations, retains the two original
resume-owner witnesses and516 original instruction guards. The verifier reruns
the complete six-capture166 audit, including all repeated policy states and
observer-free public markers, before executing the selected engine cases.
`Work256Evidence.java` and `MapPathfinding.java` save the policy/owner joins in
Ghidra; final saved-program readback is identical.

```sh
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/verify_wc3_pathing_target_policies256.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --report /tmp/target-policies256-fresh.json
```

This closes the listed policy outcomes and resulting owner/order/route behavior.
It does not certify every movement word in all22 full retail scenes: those
geometry, numeric, target-family and multi-member domains retain their existing
TARGET-02.1, NUM-02.3 and group task ownership. Earlier exact166/252/254 and86
fixtures remain unchanged. No new original save or live capture is claimed at
this checkpoint; the immutable delivered captures are reverified directly.
