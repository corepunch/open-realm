# Captain home, retreat and point policies

## Recovered contract and implementation

The public non-combat home policy is event-driven. `G_BotSetCaptainHome` and actual
member removal invoke the recovered `9d08e0` policy. An occupied home change
updates the authored home, preserving the current actor and retained point request.
`InitAssault` retains state; it does not turn an active attack Captain into state1.
An empty existing actor must remain in place until reevaluation: moving it first
makes the near-home test suppress the required point request.

With a Town and a Captain outside its home predicate, flee-disabled or state1
Captains return only when their roster is empty. With flee enabled, the cleared
strength-ready bit or `strength_count < 3 && member_count < 7` requests home.
The retreat flag is native `6c&2`, independent of the state enum. Near-home GoHome
clears it only when the range-enter count is at least half the roster. Empty
rosters and state1 place the actor through ordinary Move admission before the
retained home request at range500.

`CaptainAttack` is now registered for player-bound AI. It sets state2, clears the
retreat flag and publishes a point request at range200. The virtual actor's goal
event returns home when it reaches the retained point predicate; an event outside
that predicate republishes range200 while retaining the original request range.
The actual ordinary physical-group completion path delivers this callback.

`S_CaptainPointMove` keeps virtual actor and physical roster publication together.
Its captured speeds are 500 (`43fa0000`) during retreat, 9999 (`461c3c00`) for an
empty roster, and the slowest member on the covered ordinary roster paths.
The existing scalar near-home predicate remains `distance_squared <= 2*r*r+5000`
for range at least100; a host Euclidean500 test is not an equivalent boundary test.

The repeating `d01c1` request is registered at actor creation with period1 second.
`G_RunCaptainTimers` retains its absolute deadline independently of private AI
script execution. Its idle-member reissue uses `S_ReissueCaptainUnit`, preserving
range-listener membership, entered/outer flags and deadlines. Temporary enrollment
is a different transition and must not be used to implement periodic reissue.
The old frame-polled health/power ratio and persistence timer are removed.

## Counter identity and persistence

`CaptainAI_UpdateRosterCountsAndRanges` at `6f9d0650` changes the signed `c0`
counter on roster additions/removals. Campaign mode counts every member.
Assembly at `9d06d3` passes **unit+b8** to `057fd0`. The actual registered
`GetUnitState` native (`204050`, registration `209277`, name string `a97fb0`)
calls `687270`; its MANA case2 passes precisely **unit+b8** to that helper.
The melee rule therefore uses current mana zero or a non-illusion Hero at the
mutation point. It does not sum life or recompute the counter on every update.
The engine now stores `strength_count` and applies these deltas at its roster
writers. A public melee mutation capture is still needed for the composed lifetime.

Save133 persists the policy bits, signed strength counter, retained point/range
and periodic deadline alongside the logical roster. Cold engine load restores
these without retaining the private VM. Logical encounter order and physical
member indices remain separate. Earlier layouts are rejected.

## Evidence and automated verification

The [research handoff](retail-pathfinding-handoffs/GROUP-03.4.7.3/HANDOFF.md)
contains four twice-observed public policy scenes and their controls. Its decoded
policy verifier reproduces24 reevaluations and16 goal-event GoHome calls.
That verifier's distant-scene geometric approximation does not replace the
original scalar boundary predicate used by the engine.

Additional readonly observations in `runtime/captain-policy159/` under the
[report root](retail-pathfinding.md#binary-and-evidence-conventions) freeze12
speed publications and128 periodic updates (64 per Captain) in two repeats.
All373 public markers match the existing observer-free control with the identical
packed map hash. The sole complete Preload-file difference is PreloadEnd's wall
clock; the verifier normalizes only that non-simulation field, retaining every
public marker and every frozen speed/update word. Metadata pins observer,
controller, map, DLL and capture hashes. Six verifier tests cover acceptance and rejection of changed
speed/deadline/policy data, missing updates and an instrumented control.

```sh
python3 tools/ghidra/run_wc3_pathfinding_corpus.py \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /tmp/captain-policy159-fresh \
  --only live-captain-policy-speed-and-period
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_bot.*'
build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 +test 'wc3_bot.*'
```

The production regressions first failed eight assertions for empty GoHome,
removal-driven retreat and the missing native. A further empty home-change test
failed three assertions before the placement/publication ordering fix. Tests now
exercise count-versus-life decisions, speed words, native admission, goal-event
return, retreat cancellation and cold saved policy/deadline restoration.
The existing23 public Captain movement tests still pass2,606,567 assertions in
Classic, including captured GoHome and saved continuations.

Ghidra names/comments are saved and mirrored in `MapPathfinding.java`: creation
period and state arguments, distinct authored/retained points, runtime speed
constants, mana operand provenance, periodic update and near-home inverse.

## Retained point speed policy (Payoff160)

The virtual actor does not always take the slowest member's speed directly.
Original `9d4c20` traverses the logical roster. A member contributes only with
a Move owner at `unit+1ec`, without the cargo bit `unit+5c.10`, and without a
resolved current public `board` head. Public `OrderId("board")` is852043;
`load` is852046. These are different commands, and the engine resolves the
comparison through its order registry.

If an eligible member lacks the Cargo Drop class (`Adro`), both retained target
identities are unresolved, `cc+c4 < bc`, and the retained request differs from
authored home, retail multiplies the minimum by:

```
soft_add(soft_mul(soft_from_int(total_roster_count), float_word(0x3c321643)),
         float_word(0x3f3d37a7))
```

The home comparison subtracts each axis with the retail scalar helper and tests
absolute difference strictly below `3a83126f` on both axes. Equality applies the
multiplier. Ordinary host floating-point arithmetic changes observable words.
The factor uses total logical membership, including members excluded from the
minimum. Retreat still overrides with500; an empty eligible roster retains9999.

Five public six-Footman scenes each have two completed observed runs and an
observer-free control. Early non-home `CaptainAttack` publishes217.173904
(`43592c85`) from270-speed members, then270 (`43870000`) on range entry. A180-speed
minimum publishes144.782593 (`4310c858`), then180. An all-Adro roster stays270.
Removing the180-speed member's implicit Move owner leaves five270 inputs,
but the six-member factor still gives `43592c85`. A home request stays270.
Every public marker, including position/order samples through completion,
matches the repeats and control. PreloadEnd wall time is excluded.

Move now computes this ordinary retained-point policy from the logical roster,
using software scalars. Point roster assembly also uses that roster rather than
scanning every map entity. The all-entered callback recomputes speed before
replacing the virtual actor request; otherwise the early reduction would persist
after admission. Amov exposes its existing implicit owner through the flat
intrinsic ability query, so public removal/re-add and saved removed codes affect
minimum eligibility without editing authored metadata.

Four production-path regressions first fail nine assertions and then cover
early/recovered speed, a non-stock minimum, home replacement, Adro removal,
implicit Move removal/re-add and cold saved point state. Existing captured
Captain journeys remain separate checks. This verifies the speed policy, not
word-exact engine reproduction of all five new complete trajectories.

Frozen inputs and the strict verifier are
`tools/ghidra/fixtures/retail-captain-speed160-1.27.json` and
`tools/ghidra/research/verify_captain_speed160_live.py`. Raw captures/maps are in
`runtime/captain-speed160/` under the report root. The map wrapper's
`--legacy-markers` option reproduces the initial away/home maps without the two
auxiliary OrderId fields; the away archive is rebuilt byte-identically.
The failed second initial attachment is preserved and excluded; the completed
third observation is the second away repeat. The observer only reads/hook-records
the owned process; it invokes no retail functions and writes no game memory.

Focused Classic and TFT validation each passes103 bot tests,373 movement
tests,127 unit tests and27 ability-dispatch tests. The movement suite has
5,226,344 assertions per mode. A mistaken `wc3_skills` filter ran zero tests;
it is excluded, and the actual `wc3_ability_dispatch` suite ran in both modes.
All843 pathfinding Python checks and the fresh strict five-scene corpus pass.

Ghidra saves the decoded query, refresh/implicit-owner comments and both factor
labels; the mapper carries the same evidence. Combat targets, the extra `cc`
counter and special siege range counts still require their full engine owners.
They must not be approximated from generic combat activity or weapon type.

## Payoff159 batch checkpoint

The production game and test targets build. The full repository checkpoint
passes with the isolated native SDL2 runtime: Classic and TFT each pass2,877
tests and11,022,431 assertions; all833 pathfinding Python checks pass. The exact
staged snapshot also passes all833 checks and the fresh strict policy corpus.
The initial host run stopped in the documented SDL2-to-SDL3 text-event
conversion crash; GDB showed no test failures before that external-library fault.
The engine was not changed to bypass the input regression.

## Remaining scope

GROUP-03.4.7.3 stays open. No whole-trajectory parity claim is made for the new
retreat/Attack scenes. Combat target identities and engagement/strength branches,
retail mid-Captain save/load, public melee mana mutations, siege special range
counts, wider speed eligibility/target compositions, default Town home
creation, larger rosters and cross-clock/callback scheduler compositions still
need their evidence and engine integration. The existing handoff proposes extra
IDs for some gaps; this integration keeps them under the existing open leaf.

See [temporary enrollment](retail-pathfinding-captain-enrollment.md) for the
separate enrollment-before-buff and duplicate/withdrawal transitions.


## TeleportCaptain relocates the retained actor (Payoff290)

The AI native does real widget placement. The previous engine implementation
set `botCaptain_t.position/position_valid`, so logical queries reported a new
location while the physical Captain actor, its occupancy and its followers
remained at the old position. `G_BotTeleportCaptain` now passes the attack
Captain to Move-owned `S_TeleportCaptainActor`. This uses the existing retail
admission geometry and scalar placement, publishes the actor's position change
and invalidates the logical position override. It retains velocity, the physical
group, fine/adaptive route storage and indices, the public order, authored home,
request goal/range, roster and member positions. It neither issues Stop nor
retreats/reorders members. Placement uses the existing bounded admission search;
this adds no roster scan, route allocation or second scheduler.

The original AI binding at `6f9be339` selects `6f9c0490` (the normal VM's separate
binding is not the AI implementation). That cdecl native reads current-context
Town identity `2c/30` through `6f061320`. Town selector `6f9c64c0` resolves attack
Captain identity `2ac/2b0`; it does not use the defense identity. Captain wrapper
`6f9d7ac0` copies the two raw scalar inputs to private locals and calls widget
placement `6f653510` with arguments
`[xptr, yptr, 1, 1, ffffffff, 0, 0, 0, 0, ffffffff, 0]`.
Its scalar pointers address the numeric word itself. They differ from the
FloatMini pointers used by GoHome wrapper `6f9d6ed0`, whose values are at `+4`.
GoHome then calls Stop recovery `6f9d7600`; explicit Teleport does not.
`6f653510` clamps/admit-checks the point through virtual `dc`, applies path
texture parity when requested and notifies the committed world-position change.

Two independent owned B/C observations and an observer-free control run the
same stock Human02 geometry with six Footmen and seven public AI teleports.
All250 public map/AI query markers agree. All14 native entry/exit boundaries
agree across the two hooked runs after removing only process addresses and the
unsourced FloatMini auxiliary words; the original rows retain those words.
The28 observed boundaries retain velocity, physical-group identity, Captain
state, home/request/range, roster and target fields. No teleport enters Stop
recovery. Fractional and blocked requests demonstrate admission rather than
blindly copying the requested world coordinates. The final two calls cover an
empty roster with a retained actor. Extra periodic observations are retained in
the bundle but are not certified across repeats.

The12-case original-instruction oracle composes the native, Town selector and
Captain wrapper. It controls identity payload lookup and terminal placement
completion, checking attack selection, raw scalar-pointer ABI, local input
copies, exact argument flags and stack ownership. It is not a replacement for
full placement execution. The live observations cover placement; the engine
regression supplies their unchanged pre-call fine poses and velocities against
the unchanged Human02 WPM/W3E support fixture. It tests seven exact destinations,
retained movement/roster state and cold saves before and after relocation,
without claiming that it replays the preceding public journey. The harness must
create Captains through `G_BotCreateCaptains`: directly setting campaign mode
leaves their creation clocks invalid for physical member save references.

The focused verifier allows300 seconds per engine filter for the broad
23-scene Captain journey check; the shared runner keeps its180-second default
for existing callers. Timeouts remain incomplete runs, never passing evidence.

Reproduce with `tools/ghidra/verify_wc3_pathing_teleport290.py --binary <game.dll>
--report <fresh.json>`. The immutable observation bundle and source pins are
`tools/ghidra/fixtures/retail-teleport290-1.27.json{,.gz}`; the read-only observer,
map builder and public JASS/AI probes are under `tools/frida/research/teleport290_*`.
`Teleport290Evidence.java` and `MapPathfinding.java` preserve seven mapped
functions,253 instruction guards and binding xrefs in the saved Ghidra program.
The archive is `research/GROUP-03.4.7.3/payoff290/` under the 1.27 report root.
No previous retail expected values are changed. Save layout174 and the network
format are unchanged. This chunk makes no frame-budget or full-motion claim.

`ResetCaptainLocs` is separate: AI native `6f9bee60` calls `6f9c9c10`, which
fetches default AI locations through `6f9bb750`, updates selected homes and
teleports attack and defense Captains. It does not copy the currently authored
homes. Payoff291 below integrates the shared default-location producer and first actor
creation. Independent Town timers, exceptional coordinate domains and wider
combat/roster/goal lifetimes remain
under BASE-01.4/GROUP-03.4.7.3; this bounded integration closes neither broad leaf.


## Shared default homes and ordered ResetCaptainLocs (Payoff291)

`CreateCaptains` now obtains its attack and defense homes from a map-owned
`CTownSpot`. `ResetCaptainLocs` refreshes that spot from its live anchors, calls
the attack home callback and teleports the attack actor, then performs the same
ordered pair for defense. It preserves the existing home-callback policy and
ordinary physical admission. Authored homes, shared defaults and admitted actor
positions are separate state: moving the defaults does not rewrite every old
Captain request, and competing actors need not admit to identical positions.

The original `9c5360` first uses Town slot `1b8`. If absent, it admits the player's
**assigned start-location slot** through `9c01f0`; it does not use the player
number or world zero. Attack construction receives native state2; defense gets
state1. Their identities occupy `2ac/2b0` and `2b8/2bc` respectively.

A spot stores independent building, mine and overlay identities. With no live
building/mine, refresh retains its defaults. One live anchor supplies both homes.
With both anchors, `9bfe80` computes, in original software-scalar order:

```
delta = building.xyz - mine.xyz
length = sqrt((delta.x*delta.x + delta.y*delta.y) + delta.z*delta.z)
defense.xy = mine.xy + delta.xy*0.5
attack.xy = building.xy + (delta.xy*300)*(1/length)
```

The runtime scalar at `d3c7d8` is300. The Z term and multiply/divide order matter;
seven labelled supplied-input cases cover absent, coincident, nonplanar,
subpixel and large coordinates. The original binary executes all arithmetic,
with only external identity, position and FloatMini notification completion
controlled. First nine oracle results also equal live refresh words.

`g_bot_town.c` owns the shared append-only pool and four ordered slots per Town.
Natural mine registration and gold-return building admission use real
owner-excluding coarse distance, preserving strict thresholds80/70, encounter
ties and tightening150/5000 work budgets. Workers/ordinary structures select
among owner slots; ordinary combat units use the global pool. Logical unit
associations carry stable pool indexes and owner identities. Derived per-player
reference counts replace repeated owned-unit scans during slot removal: prune
and compact at most four slots. Anchor backreferences make withdrawal constant
time. Global identities are retained until map teardown, even after owner slots
are cleared. Explicit authored homes and construction `ShiftTownSpot` hints stay
independent.

Save176 serializes logical homes, anchor edict identities/spawn generations,
owner slots and unit associations. Load rebuilds derived membership counts.
Invalid indexes, inconsistent owners/counts and truncated oversized pool data
are rejected. Fresh and cold-save tests drive actual creation, spatial selection,
physical reset, mine/hall removal, fallback creation and shutdown.

Two read-only original-terrain runs preserve151 public markers against an
unhooked control, including member positions and public Captain queries.
Their101 ordered native boundaries agree exactly, including four actor creates,
nine refreshes and seven resets. A separate bare-start observation witnesses the
fallback but is labelled exploratory because it has no independent repeat/control.
Failed flat-map launches and the superseded temporary Frida ReturnValue observer
remain diagnostic archive material, excluded from the accepted fixture.

Reproduce with `tools/ghidra/verify_wc3_pathing_captain291.py --binary <game.dll>
--report <fresh.json>`. Frozen fixtures, sixteen original calls,1537 original
instruction guards and engine tests are checked together. `Captain291Evidence.java`,
`MapPathfinding.java` and the focused `retail-captain291-types-1.27.json` retain
21 function annotations, two newly recovered partial layouts, six explicit x86
ABIs and three globals. Saved Ghidra exports are read back and compared.

No old retail expected words were edited. Two earlier captured movement tests
now supply their required initial pose through assigned-start setup; their
expected trajectories remain unchanged. Old engine-only assertions that creation
left idle, absent actors were corrected from native construction evidence. The motion recorders now limit their projection to captured roster members;
the extended GoHome fixture observes the attack actor from its first captured
GoHome commit. Constructor/empty-roster recovery commits before that captured
window are outside that older fixture, and remain a separate producer contract.
No early constructor motion parity is inferred from the later trace. The
minimal archive lacks a Town Hall ability row, so the new lifecycle test supplies
its authored `Argd` gold-return capability explicitly.

The independent initial Town-selection timer `9cc6b0`, special `ugol` overlay
producer `9c0280`, moved-slot owner policy inverses and broader combat/roster
compositions remain open in BASE-01.4/GROUP-03.4.7.3. This chunk does not establish
complete retail save parity, preceding unit trajectories, or a frame-budget win.
Archive: `research/GROUP-03.4.7.3/payoff291/` under the1.27 report root.


Final focused validation on the merged coarse-waypoint/Hero-name commits passes
351 tests /2,649,785 assertions per Classic/TFT, plus two adjacent tests /585
assertions per edition. Eleven evidence checks and37 corpus checks pass. These
are focused checks; the full repository checkpoint is not claimed here.


## StopGathering selects the initial Town spot (Payoff292)

The public AI native `StopGathering` resolves the current Town and enters
`TownAI_SelectInitialSpot`. It does not issue Stop to harvesters, release mine
occupants or discard an active unit order. The previous OpenRealm implementation
inferred cancellation from the name and was wrong. Its old engine-only test is
corrected; no historical retail expected fixture is changed.

The native registration at `6f9be280..28f` supplies `StopGathering` at `6fb850c0`,
`()`/void at `6fa974ac`, and callback `6f9c02a0`. That callback resolves the
current AI context identity and tail-calls `6f9cc6b0`. This corrects Payoff291's
unverified description of an independent timer producer.

With an existing first Town slot, selection returns without changing it.
Otherwise the original owned list is traversed newest first, skipping Unit20
bit0. The first worker (Unit248 bit10) wins immediately. Without a worker,
the last encountered structure wins; without a structure, the last other unit
wins. These are newest worker, oldest structure and oldest combat unit in
owned insertion order. Reused edict indexes are not insertion order.
The selected unit enters ordinary `TownAI_AdmitSpot(unit,null)`, whose shared
building anchor and defaults then feed actual Captain construction.

`G_BotStopGathering` now performs this selection in one O(entity-count) pass
using retained `own_seq`; it needs no sort, allocation or quadratic predecessor
search. A present spot is an O(1) return. Shared admission now has one owning
implementation for this native, gold-return buildings and assigned-start
fallback. Ordinary creation and callbacks remain synchronous.

Three public original-terrain scenarios each have two read-only instrumented
runs and an observer-free control. All80 recorded public markers per run agree,
including ongoing Move orders and unit coordinates. Each observed run contains
11 identical selected native boundaries: the first call admits one spot, actual
attack/defense actors receive that spot's defaults, and the repeated native
leaves selection unchanged. Three samples after `PreloadGenEnd` exist only in
the observer stream; these are frozen separately, not invented as on-disk output.

Eight controlled original-code cases execute the selector's real instructions:
empty, combat, structure, worker, disabled worker, existing spot, all disabled
and reordered owned nodes. Identity resolution, node-to-Unit lookup, structure
classification and terminal admission recording are supplied dependencies;
this is not a complete original allocation/placement journey. The live captures
independently establish ordinary priority and Captain construction. The100
instruction guards also include the public registration and callback.

Engine regressions cover actual creation, priority, owned reinsertion, removed
worker, empty/no-op selection, real Captain homes and cold engine save/load.
The minimal test archive omits worker classification, so this test supplies its
`peon` type through object-data rows while preserving other authored defaults.
The existing public AI-script test now requires active harvest orders, carried
resources, hidden mine occupancy and invulnerability to remain intact.

The native also clears Town2d0 bit0. Original fixtures verify that write, but
OpenRealm does not yet model the complete automatic economy/enrollment policy
that consumes the bit. That policy, overlay admission, moved-slot inverses and
broader combat remain within BASE-01.4/GROUP-03.4.7.3. This change closes no broad
TODO and makes no full-retail-save, complete movement-journey or FPS claim.

Reproduce the focused evidence and engine checks with:

```sh
python tools/ghidra/verify_wc3_pathing_captain292.py \
  --binary /path/to/retail/game.dll --data build/tests \
  --report /tmp/captain292-fresh.json
```

The original-code calls require Unicorn. `Captain292Evidence.java` saves the
registration/selector comments and exports assembly/xrefs; saved readback is
identical. `MapPathfinding.java` retains the mapping. Raw captures, failed-first
and final tests, exact sources and artifacts are archived under
`research/GROUP-03.4.7.3/payoff292/` in the1.27 report root.

Final focused validation passes353 tests /2,649,844 assertions per Classic/TFT;
9 evidence and37 corpus Python checks pass.615 historical retail fixture files
and527 prior corpus contracts remain unchanged. This is cadence7/12after the
Payoff287 checkpoint, with no new full-suite claim.

## Combat contact is a Captain producer (Payoff358)

The earlier handoff described `6f9d8460` as a member-attacked handler. Its two
actual callers establish a wider producer: fresh Attack acquisition calls it
at `6f49419b`, and `Unit_CallForHelp` calls `6f69c810` after recipient traversal;
that bridge resolves the member's logical Captain and calls it at `6f69c835`.
The help path reaches this producer once per admitted source query, after ally
notifications and before setting the source suppression bit and timer. Merely
notifying each nearby unit would duplicate or reorder this Captain event.

Far from authored home, a Captain with no retreat bit, GroupsFlee enabled and
its strength-ready flag set reads the retained c0 roster counter. When c0 is
zero or negative, combat contact calls ordinary GoHome with range500 without
setting retreat bit2. This differs from removal-driven retreat. With positive
c0 the handler enters the separate target-engagement policy. Near home, combat
contact clears retreat only when c4 is at least the signed half of membership.
The broader engagement and follow-target branches remain in GROUP-03.4.7.3.

The public retail scene uses six stock Sorceresses under `SetMeleeAI`, each
with current mana100, then CaptainAttack south and one scripted damage source
while the virtual actor is away from home. Its recorded c0 is zero. Contact
replaces retained south/range200 with authored north/range500; flags stay1001,
so CaptainRetreating remains false. A preliminary Footman probe demonstrated
that writing `UNIT_STATE_MAX_MANA` does not create a mana pool in this retail
version: its public mana readings stayed zero. That probe cannot stand in for
the Sorceress roster. The original map and its capture are preserved separately.

Ghidra's complete contact, acquisition, roster-count and GoHome instruction
bodies and incoming references are exported by `Captain358Evidence.java`.
The contact comment was saved in `game.dll`. Reports, owned runtime captures
and rejected diagnostics are under `research/GROUP-03.4.7.3/payoff358/` in the
1.27 report root. This section does not establish complete combat target
routing, full actor/member trajectories or retail save/load parity.

The failing-first engine test drove a moving six-member mana-only melee roster,
round-tripped its retained owner before contact, called real `T_Damage`, then
round-tripped the resulting home request and replaced it with CaptainAttack.
A removed source cannot override that replacement. Classic and TFT each pass
22 assertions in the new regression; the strict contact/home/arrival verifier
runs three tests and57 assertions per edition. Twelve evidence rejection tests
cover missing contact/GoHome, invented retreat/count/range, corrupted public
positions, ownership, instrumented control and incomplete completion. No save
layout or network contract changed.

## Retained request versus hostile-source route (Payoff358 continuation)

Combat contact near home continues into `9cf410` when retreat is clear, or when
at least half the logical roster has entered range. The same source engagement
continuation applies far from home with GroupsFlee disabled. A hostile source
must be visible to the Captain owner and valid for at least one member's Attack.
The common first-source branch rotates the identity into follow114 and refreshes
movement through `9d4600` with policy0 and range200. The resulting active54/5c
point belongs to the source; retained d4/dc and e4 still belong to CaptainAttack.
Using GoHome alone or replacing the retained point with the source loses this
continuation. The physical actor first approaches the captured source position;
its goal event refreshes the live source and can enter a persistent target task.

The near-home retail scene has two read-only repeats and an observer-free
control with378 identical public markers. At counter1424 the follow identity is
published, the active route changes to the hostile source, and the authored home
request/range200 remains retained. Actor completion refreshes that source again
at1609. A second scene removes the source at1624: `9d7760` reevaluates first,
rotates null into the follow role, then restores the retained request with
policy1/range200. Logical roster count remains6 and c0 remains0; flags1001 become
3001 without retreat bit2. Two repeats and a control retain380 identical markers.

A third scene changes one Sorceress's mana to zero and removes it in the same
callback. Native `9d0650` subtracts using current mana, leaving c0=-1. Combat
contact subsequently requests home500 with no retreat. Two repeats and a control
retain353 identical markers. Recounting current members or clamping the signed
counter would change this observed state.

The strict continuation fixture freezes all nine completed runs,1111 public
markers, six contact boundaries and two source-loss boundaries. It checks1158
original instruction guards; its source scripts and Ghidra `game.dll` comments
are saved. The near-home production regression first failed15/17 assertions on
the source route and its save restoration; adding source removal then failed
24/26 assertions on stale identity and route. After the owner changes it passes
32 assertions, including a second round trip after source removal. The signed
counter regression passes21, and incompatible version201 rejection adds seven:
the verifier runs three tests and60 assertions in both Classic and TFT. Twenty-five
combined evidence tests reject missing or contaminated contact/loss evidence.

Save202 stores active_goal independently of the retained request and saves the
role-typed target/follow/backup entity identities with their birth stamps. It
also retains the request-restored flag2000. No network format changes. Native
retail saves are not part of this proof. Full allied support/overflow/Hero
policies, semantic target-change callbacks, cross-owner subscription ordering,
independent acquisition and complete actor/member trajectories remain within
GROUP-03.4.7.3; this continuation does not close that leaf.

### Independent Attack acquisition while a Captain point request is active

The Captain's periodic group update is not the producer of a fresh hostile
contact. Original `Attack_OnAcquireTimer` (`6f493ff0`) rearms the Attack request
before testing execution guards. The idle interval is `0.5` seconds and the
retained-target interval is `1.0` second. Its fresh-target path first publishes
Attack through `6f4943f0`, then notifies the Captain from return address
`6f4941a0`. This request survives replacement of the current movement task.

Two instrumented captures and a control run of the clean acquisition scene
produce the same 379 public markers. At counter 1557, a Captain member first
publishes Attack state 1; the subsequent Captain contact sees a six-member
mana-only roster with `c0 == 0`, restores its retained home request with range 500,
and reissues the member into state 7 before the acquisition timer returns. The
returned member has no Attack target. An immediate or persistent Attack target
is therefore not the expected result of this particular fresh contact.

At counter 1570, the members receive another Attack publication from the separate
`493af7` notification path. Read-only packet tracing identifies zero-damage
AllyCombatAlert packets (flags 4, type 16, amount 0), from `49bb50` with outer caller
`69506c`. Removing Slow from all actors leaves this result unchanged. These
later packets are not evidence of an acquisition-timer replay, and the upstream
producer remains outside this verified boundary.

The engine adds an independently owned Attack primary request for the missing
`d0014` AI point-movement domain. It retains the logical deadline and serial
through save/load and rebuilds the derived timer heap. Paused and execution-
disabled actors retain their timer while skipping acquisition. The bounded scan
uses the existing unsigned Attack priority, committed target distance for strict
ties, target eligibility, and the weak-alternative fallback. Ordinary explicit
Move (`d0012`) does not gain this voluntary scan. The constructor registers its
own INIT-only procedure so it neither duplicates engine Attack lifecycle events
nor changes the authored Attack procedure's cached dispatch contract.

The production regression starts six armed Captain members on a point route,
introduces a hostile actor without dealing damage, and uses a deliberately long
weapon windup to keep the contact independent of the damage/help producer. The
valid pre-change baseline passes 18 of 21 assertions: only the retained home,
range 500 and automatic-combat speed-cap consequences fail. It also verifies pause
and execution-disable inverses and a cold save/load before enabling acquisition.
The implementation additionally checks that the exact saved request deadline,
epoch and serial survive the reload. Save 205 adds the independent Attack timer
record; version 204 is rejected explicitly.

The immutable acquisition fixture is
`tools/ghidra/fixtures/retail-captain358-acquire-1.27.json`; its verifier guards 1300
original instructions and the complete publication/contact/reissue ordering.
This is a bounded addition under GROUP-03.4.7.3. Legacy polling domains, full
neutral and candidate admission, the acquired-target script event, cross-owner
subscription ordering and complete trajectories still require their own retail
proof and engine work within that existing leaf.

The common timer drain owns epoch rebasing. A newly armed scan retains its raw
future scalar deadline, even when it exceeds the current span. Normalizing it
inside the producer makes that future request appear overdue in the outgoing
span and repeatedly drain. The existing `target168_pending_fade_rebases` public
timer regression exposed this defect: the original failing run reached drain
visit3001 with a selected Attack request in epoch2 while the owner remained in
epoch0. The fix leaves normalization to the shared primary-request rebase.

The final acquisition gate runs three production tests in each edition: the
no-damage Captain acquisition and saved inverse, incompatible-version rejection,
and the existing scalar rebase regression. Both editions pass all 46 assertions.
The evidence validator also rejects missing rebase coverage.

Payoff363 composes Move, Attack and Captain target callbacks in the common
per-event registration history, including synchronous public source removal,
owner transfer and death. Native whole-transaction repeats/control and saved
engine ownership are documented in
[mixed target event ownership](retail-pathfinding-target-events.md).
The broader policy and trajectory scope of GROUP-03.4.7.3 remains open.

## Independent timers and older construction/teleport regressions

The Save206 full-suite checkpoint exposed four older fixtures that consumed the
first global primary request as though it belonged to Build or SpellCast. GDB
shows the independent Attack scan is legitimately earlier: its deadline is0.5,
while the tested Teleport cast deadline is1.375. The new acquisition timer is
preserved. Construction and Teleport fixtures now query their actual owner via
`A_PRIMARY_TIMER_NEXT`; every original deadline, serial, ordering, lifecycle and
save assertion remains.

Before correction the four tests fail61 assertions per edition. After the
fixture-only correction, all three Build296 and five Teleport327 tests pass480
assertions per edition. The production game module has the identical SHA-256
`0e377813fb233ac2fe52492e3e3020160a4d9a6cc7db4cc064287141b0d68ca3`.
The corrected private snapshot passes the complete repository checkpoint:
9,437 native tests across33 JUnit reports, with no failures, plus2,485 Python
tests. The32 ordinary native summaries account for9,429 tests and40,342,353
assertions; the separate power-lifetime harness contributes eight more tests.
The run uses the native SDL2 library, avoiding the host SDL2-to-SDL3 compatibility
layer that crashed the initial client-input diagnostic. Diagnostics and the exact
two-file correction are archived under `GROUP-03.4.7.3/payoff358/checkpoint206-*`;
the complete repeat log is `SEP-01.2/phoenix365/root-fixture206-checkpoint.log`.
