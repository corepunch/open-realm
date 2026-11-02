# Warcraft III invisibility mechanics

## Contract

Gameplay invisibility is player-relative. Ability code establishes the gameplay state; `S_UnitIsInvisibleToPlayer()` and the existing detector/shared-vision path decide whether a particular viewer can perceive the unit. Generic `RF_HIDDEN` uses such as cargo, mines, training, revival, or script-hidden units are not automatically detector-revealable.

The JASS visibility queries use that same authoritative path. `IsUnitDetected(unit, player)` delegates to `S_UnitIsDetectedByPlayer()` and reports player-local true-sight coverage without mutating the target. `IsUnitInvisible(unit, player)` delegates to `S_UnitIsInvisibleToPlayer()`, including owner/shared-vision access and detector reveals. Null unit/player handles return false. The natives intentionally do not infer invisibility from every generic `RF_HIDDEN` state.

## AI and JASS queries

The AI native `UnitInvis(unit)` asks whether the unit currently has an intrinsic active invisibility state. It uses `S_UnitHasInvisibilityState()` and does not take a player or fold in detection. This matches stock `common.ai`, which separately evaluates `UnitInvis(target)` and `IsUnitDetected(target, ai_player)` before deciding whether to keep tracking a target. The query returns false for invalid or dead units and for generic hidden state. Permanent Invisibility's reveal window and Shadow Meld activation timing come from their existing state predicates; merely owning an ability does not make the unit invisible.

Keep the JASS concepts distinct:

| Query | Meaning |
|---|---|
| `UnitInvis(unit)` | Intrinsic/current active invisibility, independent of viewer or detection |
| `IsUnitDetected(unit, player)` | Whether the specified player has detection for the unit |
| `IsUnitInvisible(unit, player)` | Effective viewer-relative invisibility after ownership/shared vision and detection |

The callbacks are implemented in `api_unit.h` by delegating to the existing player-relative helpers; this keeps their JASS contract aligned with gameplay visibility without introducing another detection path.

The recovered TFT class table in `games/warcraft-3/tft-ability-classes.txt` identifies the relevant stock classes: `AIvi -> CAbilityItemInvis`, `AOwk -> CAbilityWindWalk`, `Agho -> CAbilityGhost`, `Aeth -> CAbilityGhostVisible`, and `Abur -> CAbilityBurrow`. It also records `Agho` as deriving from `Apiv`; both use the shared active-invisibility state, while viewer-relative visibility and detection remain separate queries.

See [Shadow Meld](shadowmeld.md) for the separate `Ashm`/`Ahid` night/stationary lifecycle.

## Current ownership

- `Apiv` Permanent Invisibility owns an authored scalar fade and primary-timer publication. Viewer-relative queries read the published state; synchronous TargetLost is delivered after publication. See [exact fade scheduling](retail-pathfinding-target-visibility.md#delayed-invisibility-publication-payoff168).
- `Aivs` and `AIvi` own the same concrete Binv class, independently of the authored BuffID used for display. Both retain the applying ability rawcode and use a saved primary expiry. AIvi supplies transition2 to the shared fade; Aivs supplies authored DataA. `RF_HIDDEN` remains presentation state.
- `AOwk`/`ANwk` Wind Walk owns `BOwk`. `BOwk.data` stores the applying ability rawcode so aliases keep their own authored Data C/cooldown and survive save/load.
- `Agho` Ghost caches persistent invisibility in `unit->runtime.flags`; detector coverage reveals it per viewer without removing the Ghost state. Like the repository's other gameplay-invisible states, Ghost is excluded by the shared aura-active predicate.
- Human tower `Adts` detection uses its authored 900 range only for the tower owner after `Rhse` (Magic Sentry) is researched; tower rows contain `Adts` before the upgrade, but that ability is inactive until then.
- `AIvi` is an `AB_ITEM` ability applied to the actual inventory carrier, independently of selection. Fresh ordinary instances use authored Dur for nonheroes and HeroDur for heroes. Native initialization retains a zero nonhero duration additive; native serialized nonzero additives are not imported by this engine.

Selected friendly units retain their selection focus and control while invisible. `G_IsEntitySelected()` applies the same viewer-aware invisibility rule as selection admission, so portrait, status, command, and order paths continue to resolve the selected unit. Owner/shared-vision snapshots also receive a ghosted vertex alpha (0.35 multiplied by authored alpha); hostile visibility remains governed by fog and detection. Timed status expiry clears `RF_HIDDEN`, restoring ordinary presentation automatically.

Gameplay-invisible friendly units continue to reveal fog for their owner and shared-vision viewers as they move. `RF_HIDDEN` suppresses fog sight only when the unit is hidden from its own owner, preserving the ordinary hidden-unit behavior for cargo, training, and script-hidden entities.

`SP_SpawnUnit()` initializes authored unit abilities through `S_UnitAbilityEvent(..., A_UNIT_INIT)`. That event must dispatch both innate hooks and the unit's authored `UnitAbilities.abilList`; otherwise passive traits such as a Shade's `Agho` never initialize and the unit remains visible. The regression `wc3_spell.authored_ghost_initializes_on_unit_spawn_event` exercises this event with `Agho` in the authored list.

## Wind Walk lifecycle

`CAbilityWindWalk` declares cooldown-on-status-removal and active-status event dispatch in its registry row. The shared spell processor reads the cooldown policy without checking Wind Walk rawcodes; cooldown begins when `BOwk` ends. Generic status policy events are sent only to abilities that opt into them, and the Wind Walk procedure verifies that the event's status is `BOwk`. This matters because `heroabilitystatus_t.data` may instead hold an ability-specific numeric payload, such as Anti-Magic Shell absorption. The Wind Walk procedure owns movement collision policy, attack damage/break behavior, spell-commit breaks, and cleanup.

While `BOwk` is active, both steering and final movement commits ignore dynamic unit collision but retain static terrain/building pathing. `S_StatusIsUndispellable()` treats `BOwk` as non-dispellable. The breaking attack reads Data C from the rawcode saved in `BOwk.data`, rather than assuming `AOwk`. Wind Walk and temporary Invisibility share `RF_HIDDEN`; removing either effect preserves the flag while the other remains active.

## Item invisibility data flow

```text
G_UseItem or UI Inventory
    -> G_UseImmediateItemAbility(actual carrier, held item, UI controller, alias)
    -> CAbilityItemInvis
    -> concrete Binv class + authored display ID + applying alias
    -> independent saved primary expiry
    -> S_TemporaryInvisibilityAcquire(transition = 2)
    -> shared scalar fade / target notification / counted suppression
```

The logical class stays Binv. A nonzero authored BuffID context updates its
displayed ID; a zero context preserves the prior ID on refresh. Both producers
find that class even when it displays a custom ID, so refresh retains one
contribution. AIvi uses transition2 rather than DataA.
The common scalar starts slightly above zero, so the scheduled fade publication
is slightly earlier than the transition parameter; it is not a fixed two-second
integer timeout. The item alias still supplies Dur/HeroDur and source identity.

Original531a80 refreshes context first. Positive duration only extends an owner
when strictly greater than its actual primary-clock remainder; shorter/equal
refresh preserves deadline and sequence. Zero/negative duration cancels expiry.
Refresh retains the status instance, fade request and suppression token. Expiry,
dispel and break release the shared contribution through its ordinary owner.

`wc3_items.invisibility322_binv_class_fade_refresh_and_saved_primary_expiry`
uses real inventory items with conflicting custom BuffID/DataA, all five duration
cases, charges, pending-fade save/load and real primary timer draining. The
original-byte oracle and its service-stub limits are documented in
[Attack recovery](retail-pathfinding-attack-recovery.md#item-invisibility-fade-and-expiry-checkpoint322).

Scripted Pause freezes Binv's private expiry on the first clock hold and
rearms its saved remainder on the last inverse. Sleep's physical suspension
alone does not own that clock. The shared fade continues independently.
A new Binv inherits the current clock count after starting its fade. Refresh
reads the canceled expiry's zero remainder, so positive refresh can start a
new running request while paused; unpause replaces it with the old frozen
remainder. Nonpositive refresh also preserves that frozen remainder. These
rules follow original427610/43b0e0,531420/532fa0 and531a80, rather than a
blanket paused-unit timer policy. Full public Sleep/Apiv composition remains
a separate B01 check.

Checkpoint324 adds real JASS Sleep admission, Apiv/item attachment in both
orders, independent owner removal and paused cold save/load. A fresh retail
observation/control pair agrees on246 public records. Production is unchanged.
Checkpoint325 implements those notifications for the existing profiled spell
owner, in attachment order before public CHANNEL/CAST callbacks. Binv's
native flag100 is its pending-publication guard: an action preserves a pending
owner and removes a notified owner. Apiv releases and reacquires its own
contribution. The next gap is routing ordinary item activation through its
actual owned command/cast lifecycle; see the exact callers in
[checkpoint324](retail-pathfinding-attack-recovery.md#public-sleep-and-independent-invisibility-owners-checkpoint324).

## Save/load

Ghost uses a persisted `runtime.flags` bit, and Wind Walk uses the persisted `heroabilitystatus_t.data` applying-ability identity. Save137 stores Permanent Invisibility's scalar origin, slope and pending primary request; its heap membership is rebuilt. No edict pointer or C callback is added. Focused save/load tests cover pending fade and these other semantics.

## Planned compatibility decisions for unresolved retail details

The remaining retail evidence is not precise enough to recover every low-level collision or animation-frame boundary. The rules below are the documented implementation policy for the still-unimplemented `Aeth` and `Abur` work; they are not claims that those mechanics are already present in the runtime. Keep each policy centralized so later corrections remain local.

### `Aeth` Ghost (Visible)

`Aeth` does **not** grant invisibility and never enters the detector-gated visibility path. It grants a Ghost collision policy only.

Use an asymmetric dynamic-unit blocking rule:

```text
normal mover -> Ghost Visible blocker : does not block
Ghost Visible mover -> normal blocker : normal unit collision still blocks
Ghost Visible mover -> Ghost Visible blocker : does not block
```

In other words, the Ghost/Ghost-Visible property belongs to the potential **blocker**: a unit carrying that policy does not obstruct other moving units, but carrying the policy does not itself grant Wind-Walk-style phasing through ordinary blockers. This preserves the repeated editor/community description that other units can pass through Ghost units while avoiding the stronger and less-supported claim that Ghost movement is identical to Wind Walk.

The policy affects dynamic unit collision only. Terrain, cliffs, static blockers, map bounds, and building footprints remain authoritative unless a separate authored field says otherwise. Do not implement this by zeroing the unit collision radius; combat, proximity, targeting, overlays, and other radius consumers still need the authored radius.

For building placement, honor `Aeth`'s authored **Does Not Block Buildings** behavior when the normalized ability data exposes it. When enabled, the unit does not invalidate a prospective building footprint solely because its dynamic unit body overlaps the site. This exception must stay in placement policy; it does not remove static pathing or make the unit globally non-solid. If the field cannot be recovered for a particular data version, default to normal building blocking rather than assuming the exception.

### `Abur` Burrow

`Abur` is a dedicated modal ability with four explicit states:

```text
NORMAL
  -> BURROWING
  -> BURROWED
  -> UNBURROWING
  -> NORMAL
```

Use **completion-boundary semantics** for the uncertain animation-frame details. Do not gradually apply gameplay properties during the animation. This gives deterministic behavior and a clean save/load contract while remaining easy to adjust if retail observation later establishes a different frame boundary.

On entry to `BURROWING`:

- stop the current move/attack action through the normal order lifecycle;
- lock new movement, attack, and spell/ability commits for the transition;
- keep the unit visible and otherwise in its normal regen/aura state until the transition completes;
- play/select the authored burrow transition sequence where available.

When the Burrow transition completes, enter `BURROWED` atomically:

- add the Burrow gameplay-invisibility cause;
- keep the unit stationary;
- reject manual attack and automatic acquisition;
- apply authored Burrow regeneration (stock Fiend/Beetle values are test expectations, not hard-coded logic);
- suppress ordinary Blight regeneration so it does not stack with Burrow regeneration;
- exclude the unit as an aura recipient;
- expose the Unburrow command instead of Burrow;
- remain burrowed if detected: detection reveals the burrowed presentation but never cancels the state.

On entry to `UNBURROWING`, retain the full `BURROWED` gameplay state for the duration of the transition. The unit therefore remains stationary, non-attacking, Burrow-regenerating, aura-ineligible, and detector-gated until Unburrow completes. When the transition completes, remove all Burrow gameplay modifiers atomically and return to `NORMAL`.

This deliberately chooses a conservative inverse contract: Burrow properties start only after Burrow finishes and end only after Unburrow finishes. It avoids a half-burrowed unit being simultaneously visible but using hidden-unit combat/regen rules, and it avoids frame-specific constants that are not supported by current evidence.

During either transition, incompatible new orders are rejected rather than cancelling the transition implicitly. Death/ability removal is an explicit inverse: cancel the transition, clear Burrow gameplay modifiers/timers, and continue the ordinary death/removal lifecycle without leaving stale invisibility, regen, aura, or movement state. Save/load must preserve the modal state and remaining transition time if OpenRealm supports saving during the transition.

Fully burrowed units are treated as stationary modal units, not merely invisible normal units. Systems with state-specific eligibility (for example transport/teleport mechanics) should query a centralized `S_UnitIsBurrowed()`-style predicate rather than checking `Abur`, `RF_HIDDEN`, or unit rawcodes independently.

### Confidence and future correction

High-confidence retail behavior remains authoritative: `Aeth` is visible Ghost-style behavior; Burrow grants invisibility, prevents attacking, changes regeneration, does not stack normal Blight regeneration, and excludes aura effects. The asymmetric `Aeth` blocker rule and the exact Burrow transition boundaries are explicit OpenRealm compatibility decisions based on the best available feedback rather than claimed retail facts. If controlled retail testing later disproves either choice, update this section and the single owning policy/state machine together.

## Verification

Focused automated coverage lives in:

- `games/warcraft-3/game/tests/t_spell.c`: authored Wind Walk duration/cooldown, `AOwk`/`ANwk` procedure coverage, undispellable `BOwk`, applying-rawcode save/load, overlapping Wind Walk/temporary-invisibility expiry, and Ghost aura exclusion through the shared gameplay-invisibility contract.
- `games/warcraft-3/game/tests/t_collision.c`: Wind Walk movement through a live unit while retaining the normal movement order path.
- `games/warcraft-3/game/tests/t_items.c`: non-stock `AIvi` Dur/HeroDur/BuffID, selection/control while active, timed expiry/recast and break cleanup, invalid dead-carrier use, and rejection without visibility changes when the status table is full.
- `games/warcraft-3/game/tests/t_spell.c`: non-stock `Aivs` BuffID, temporary-invisibility break/expiry ownership, and Wind Walk overlap in both expiry orders.
- `games/warcraft-3/game/tests/t_shadowmeld.c`: owner ghost alpha for timed invisibility.
- `games/warcraft-3/game/tests/t_game.c`: a moving Binv unit keeps revealing newly entered fog.
- `games/warcraft-3/game/tests/t_wards.c`: Ghost lifecycle, player-relative invisibility, and save/load.

When validating locally, use focused patterns before the full suite, for example:

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.wind_walk*'
make test-wc3-engine WC3_PATTERN='wc3_collision.wind_walk*'
make test-wc3-engine WC3_PATTERN='wc3_items.invisibility_item*'
make test-wc3-engine WC3_PATTERN='wc3_spell.ghost*'
make test-wc3-engine WC3_PATTERN='wc3_save.ghost*'
```

This implementation was designed to consume normalized Warcraft data rather than hard-coded unit rawcodes or stock numeric tables, preserving ROC/TFT/custom-object compatibility at the existing accessor boundary.

## Ordinary AIvi item command: checkpoint326

The implementation routes public `UnitUseItem` and the profiled no-target AIvi path
through Inventory admission and the ordinary cast owner. Its intrinsic phase
clocks are zero; it replaces Move rather than preserving a private inline Move.
Other item handlers that open target cursors are not implicitly profiled casts.
Retained effect code, source attachment and active completion are separate:
CHANNEL removal publishes ENDCAST using the old item metadata, then subsequent
retained phases expose spell ID0. A live user head can still permit the effect;
Stop removes that permission. Successful completion follows ENDCAST with one
synchronous USE_ITEM callback. Callback replacement heads remain owned.

Both editions match81 projected order/phase/state rows from237 complete
native public rows; the two new tests pass209 assertions per edition. Additional cold-save/repick/sibling-alias/replacement-Move
cases are engine regressions, not new retail oracles or public paused-admission
proof. The initial red/green fixture attempts omitted unit-spell constants from
the minimal common.j; they are excluded as failing-first product evidence.

## Per-item availability: checkpoint327

Each profiled AIvi attachment has its own ordered availability node, even when
two items share an alias. Settled scripted Pause rejects public `UnitUseItem`
without issuing an order or changing the head; the inverse restores admission.
Save245 preserves tokens, signed counters and list order. Detaching one item
cannot release its sibling's owner.

The [guarded original327 evidence](../../../tools/ghidra/fixtures/research/invisibility327-availability-original.json)
traces admission through2190c0→206b20→69dc80/3fc460 and first disable407600.
The latter clears pending publication before ENDCAST, while the old cast is
still active; active completion clears afterward. The component test observes
item depth 1/order 0 before AInv is visited, rejects nested same-item use, and
releases after factory unwind. Fresh zero-time AIvi has neither 4 nor 800 task
ownership; ordinary phases do not write 44. This is static-chain plus engine
proof, not a new live Pause-during-cast timeline or a claim about every item.

Canonical item ownership also shares 0x1000/0x4000 across recursive executions.
New CHANNEL3fbb80 clears 0x4000; the completion inverse emits ENDCAST, clears 0x4000,
decrements charge, publishes USE_ITEM on the retained item's current carrier,
then rereads perishability. A nested same-instance cast cannot leave an outer
per-cast flag that consumes again. The component regression first failed two
assertions out of 159 and now passes; it does not claim public reentrant admission.
FINISH-disable and ENDCAST-transfer component cases check one USE, current-carrier
identity and sibling preservation. Queued690930 admission retains weak Item
identity/incarnation plus numeric slot command: same-item/same-slot repick
executes, but a different same-alias replacement does not.

Both-edition focused checks pass 2,326 tests/286,242 assertions; all five fresh
gates and normal/nine-checkpoint saved C299 pass. No full327 suite is claimed. The early
sibling-effect assertion incorrectly expected synchronous execution immediately
after PauseUnit(false); the corrected fixture drains the real resume scheduler.
It is not failing-first product evidence. Mixed-contributor B01 work remains open.

## Wind Walk common ownership: checkpoint328

AOwk58ce50 reads DataA as fade duration and DataB as the raw additive movement
speed contribution. BOwk59daa0 enters the shared63ce40 contribution/fade owner;
it retains concrete BOwk identity, primary expiry and the inherited private
Pause clock. Its removal releases its own contribution and speed before the
existing cooldown inverse. It does not substitute a hidden-flag toggle.

The original owner traversal caches NEXT, dispatches v178 before v12c for CAST,
and removes BOwk at v178. CHANNEL and BOwk's action12c are no-ops; Binv's action
removal must not be copied onto BOwk. [original328 evidence](../../../tools/ghidra/fixtures/research/windwalk328-ownership-original.json) and the
[byte checker](../../../tools/ghidra/research/windwalk328_ownership_check.py) guard 21 bodies, 35 vtable words and 102 calls, with six negative
controls. This is static evidence, not a new live overlap capture.

Save246 carries BOwk through the common saved owner/clock validation. The
tests use configured AOwk and external Aivs in both attachment orders, distinct
fade/speed data, saved shared ownership, either removal inverse and real primary
expiry. Both editions pass 117 Wind Walk assertions, including the component
scripted-clock save/inverse and CHANNEL-versus-CAST boundary. The complete spell
and save cohorts pass 1,720 test executions/171,686 assertions; five fresh
regression gates and normal/nine-save C299 pass. No new live overlap witness
or B01 closure is claimed.

Checkpoint329 returns the applied BOwk instance before synchronous publication.
The outer Wind Walk effect applies speed only to that exact instance. A recovery
callback regression removes the old buff and applies the same alias at rank2:
without the guard, the outer rank1 overwrote its speed (27/28 assertions passed);
with the guard, both editions pass all28. This is an effect-service mutation
component, not proof of public reentrant Wind Walk admission.

Full329 verification at `71126adb4` passes 9,476 tests/60,733,200 assertions
across both editions. All606 source,16 artifact and42 asset hashes remain frozen;
there are no failures, errors or skips. B01 remains open.

## Per-alias Ghost and Permanent Invisibility: candidate330

Configured Agho and Apiv attachments each own their contribution under their
exact alias and authored Dur. They share the unit's fade and contribution count;
ACTION releases/reacquires only the invoking alias's contribution. A remaining
sibling therefore preserves the shared fade, while a lone owner's action resets
its own authored duration. Ghost's private-clock notification is a no-op; it
does not inherit Binv's private expiry suspension.

The separate native Attack10000000 query bit uses OR/AND ownership, not a
reference count or timer suspension. Each passive owner retains its held80
state: removing one held owner clears the bit even when another remains, and
the remaining owner's contribution-only ACTION does not reacquire that bit.
Save247 records per-alias ownership and validates the shared count.
Type rebinding detaches each vanished raw alias through its normal inverse.
Deferred removal completes the contribution and held-bit releases on the same
actor, with identity checks after synchronous callbacks. Retained aliases and
newly introduced destination fades keep their independent ownership.

[original330 evidence](../../../tools/ghidra/fixtures/research/ghost330-original.json) and the [byte checker](../../../tools/ghidra/research/ghost330_check.py) guard 14 bodies, 24 vtable words and 43 calls, with
eight negative controls. Focused checks pass 1,764 tests/213,182 assertions;
all five broader gates pass 1,474 tests/7,096,974 assertions across both editions.
The pending/published morph inverse passes 324 assertions per edition for Apiv,
Agho and a custom Ghost alias. Normal and saved299 comparisons retain exact
2,066 public/5,342 motion rows each. There is no new full native overlap capture. Source
DataB target behavior and DataC spatial behavior remain explicit limits. The
prior full329 result remains 9,476 tests/60,733,200 assertions at `71126adb4`;
B01 and all 25 whole leaves remain open.
