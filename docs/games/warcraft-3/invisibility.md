# Warcraft III invisibility mechanics

## Contract

Gameplay invisibility is player-relative. Ability code establishes the gameplay state; `S_UnitIsInvisibleToPlayer()` and the existing detector/shared-vision path decide whether a particular viewer can perceive the unit. Generic `RF_HIDDEN` uses such as cargo, mines, training, revival, or script-hidden units are not automatically detector-revealable.

The recovered TFT class table in `games/warcraft-3/tft-ability-classes.txt` identifies the relevant stock classes: `AIvi -> CAbilityItemInvis`, `AOwk -> CAbilityWindWalk`, `Agho -> CAbilityGhost`, `Aeth -> CAbilityGhostVisible`, and `Abur -> CAbilityBurrow`. It also records `Agho` as deriving from `Apiv`; this is evidence for sharing the existing player-relative invisibility/detection contract rather than creating a second visibility subsystem.

See [Shadow Meld](shadowmeld.md) for the separate `Ashm`/`Ahid` night/stationary lifecycle.

## Current ownership

- `Apiv` Permanent Invisibility owns its authored transition/reveal window and is consumed by the shared viewer-relative visibility query.
- `Aivs`/`Binv` temporary Invisibility uses `RF_HIDDEN` only as an invisibility presentation state; attack/ability commits remove `Binv` through the existing Human ability path.
- `AOwk`/`ANwk` Wind Walk owns `BOwk`. Movement already reads authored Data A for the speed modifier. `BOwk.data` stores the applying ability rawcode so aliases keep their own authored Data C/cooldown and survive save/load.
- `Agho` Ghost caches persistent invisibility in `unit->runtime.flags`; detector coverage reveals it per viewer without removing the Ghost state. Like the repository's other gameplay-invisible states, Ghost is excluded by the shared aura-active predicate.
- `AIvi` Item Temporary Invisibility is an `AB_ITEM` ability and applies the existing `Binv` state to the selected living carrier. Duration is read through `S_SpellDuration()`, including HeroDur for hero carriers.

Selected friendly units retain their selection focus and control while invisible. `G_IsEntitySelected()` applies the same viewer-aware invisibility rule as selection admission, so portrait, status, command, and order paths continue to resolve the selected unit. Owner/shared-vision snapshots also receive a ghosted vertex alpha (0.35 multiplied by authored alpha); hostile visibility remains governed by fog and detection. Timed status expiry clears `RF_HIDDEN`, restoring ordinary presentation automatically.

`SP_SpawnUnit()` initializes authored unit abilities through `S_UnitAbilityEvent(..., A_UNIT_INIT)`. That event must dispatch both innate hooks and the unit's authored `UnitAbilities.abilList`; otherwise passive traits such as a Shade's `Agho` never initialize and the unit remains visible. The regression `wc3_spell.authored_ghost_initializes_on_unit_spawn_event` exercises this event with `Agho` in the authored list.

## Wind Walk lifecycle

`CAbilityWindWalk` deliberately does not start cooldown during the ordinary spell-commit step. The cooldown begins when `BOwk` ends. Expiry reaches `CAbilityWindWalk` through the existing `abilstatus.data -> UnitDispatchStatus(..., A_STATUS_REMOVE)` lifecycle; attack/other-ability breaks call the same cleanup through `S_WindWalkEnd()`.

While `BOwk` is active, both steering and final movement commits ignore dynamic unit collision but retain static terrain/building pathing. `S_StatusIsUndispellable()` treats `BOwk` as non-dispellable. The breaking attack reads Data C from the rawcode saved in `BOwk.data`, rather than assuming `AOwk`.

## Item invisibility data flow

```text
AIvi registry row
    -> CAbilityItemInvis
    -> S_SpellCurrentCode()
    -> S_SpellDuration(code, 1, G_UnitIsHero(target))
    -> Binv timed status
    -> existing temporary-invisibility break/detection rules
```

Tests use deliberately non-stock Dur/HeroDur values so this path cannot pass by hard-coding the stock potion duration.

## Save/load

No new edict pointer or callback field is introduced. Ghost uses an existing persisted `runtime.flags` bit, and Wind Walk uses the already-persisted `heroabilitystatus_t.data` field for applying-ability identity. Focused save/load tests cover both semantics so future serializer changes cannot silently drop them.

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

- `games/warcraft-3/game/tests/t_spell.c`: authored Wind Walk duration/cooldown, `AOwk`/`ANwk` procedure coverage, undispellable `BOwk`, applying-rawcode save/load, and Ghost aura exclusion through the shared gameplay-invisibility contract.
- `games/warcraft-3/game/tests/t_collision.c`: Wind Walk movement through a live unit while retaining the normal movement order path.
- `games/warcraft-3/game/tests/t_items.c`: non-stock `AIvi` Dur/HeroDur, selection/control while active, timed expiry/recast, and invalid dead-carrier use.
- `games/warcraft-3/game/tests/t_shadowmeld.c`: owner ghost alpha for timed invisibility.
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
