# Warcraft III Race Mechanics

## Scope

The shared WC3 simulation should own common state such as construction progress, food accounting, resources, unit lifetime, and pathing, while race-specific abilities/behaviors own the different state machines used by Human, Orc, Undead, and Night Elf units.

This document records the source comparison used for OpenRealm's race-specific construction and economy work and the remaining race-mechanic gaps. It is deliberately narrower than a general unit-data reference: a mechanic is listed here when two races perform the same RTS concept through materially different simulation state.

Reference behavior was compared against the bundled Warsmash sources:

- `CBehaviorHumanBuild.java`
- `CBehaviorOrcBuild.java`
- `CBehaviorUndeadBuild.java`
- `CBehaviorNightElfBuild.java`
- `CUnit.java`
- `CAbilityOverlayedMine.java`
- `CAbilityBlightedGoldMine.java`
- `CAbilityBlight.java` / `CAbilityTypeDefinitionBlight.java`
- `CAbilityAcolyteHarvest.java` / `CBehaviorAcolyteHarvest.java`
- `CAbilityEntangleGoldMine.java` / `CAbilityEntangledMine.java`
- `CAbilityCargoHoldEntangledMine.java`

under `core/src/com/etheller/warsmash/viewer5/handlers/w3x/simulation/` in WarsmashModEngine.

## Construction strategy contract

OpenRealm stores an explicit `constructionType_t` on the unfinished building. The structure owns the relationship to any temporarily committed worker so cancellation, death, `RemoveUnit`, save/load, and normal completion all use the same teardown path.

| Strategy | Worker state | Progress owner | Completion |
| --- | --- | --- | --- |
| Human | Peasant remains external | Human `Arep` Repair / Power Build | builder remains alive |
| Orc | Peon is hidden, paused, invulnerable inside construction | building-owned autonomous clock | Peon is released beside the building |
| Undead | Acolyte remains visible for the summon work window | building-owned autonomous clock | Acolyte is released after the summon window; building continues |
| Night Elf, non-Ancient | Wisp is hidden, paused, invulnerable inside construction | building-owned autonomous clock | Wisp is released beside the building |
| Night Elf, Ancient | Wisp is hidden inside construction and its Food Used is removed | building-owned autonomous clock | Wisp is consumed |

All strategies begin at 10% maximum life and hold the Birth animation to authoritative construction progress. Human remains paused unless a valid Human Repair participant advances it. Orc, Undead, and Night Elf construction advance once per simulation frame through `G_RunConstructionFrame()` and add the corresponding fraction of `(max_life - start_life)` rather than deriving HP from absolute progress; damage to an unfinished building therefore remains damage.

`skills/s_build.c` chooses the Human path only when `UnitData.race` is Human and the builder exposes the existing Human Repair capability, and otherwise dispatches the three other standard strategies from `UnitData.race`. Unknown/custom workers retain the legacy construction fallback rather than silently being assigned a standard-race lifecycle.

## Worker ownership and lifetime

`edict.construction.primary_builder` remains the Human Repair owner. Race strategies that temporarily own a worker use the separate `edict.construction.worker` reference plus its `spawn_time`; these are distinct because Human can have multiple Repair participants while Orc/Night Elf construction has one internal worker and Undead only retains the summoner for the opening work animation.

An internal worker is `RF_HIDDEN`, paused, and temporarily invulnerable. `RF_HIDDEN` makes it hollow to OpenRealm collision/pathing and keeps it out of ordinary selection/idle-worker presentation. Its pre-construction invulnerability state is restored when released.

`G_StopConstruction()` is the common non-completion teardown. It:

1. cancels Human Repair participants;
2. releases an Orc/Night Elf worker or an unfinished Ancient Wisp;
3. releases an Undead summoner if its short summon window is still active;
4. restores an Ancient Wisp's authored Food Used when construction does not finish;
5. clears construction state and the held Birth animation.

Both unit death and direct `G_FreeEdict()`/`RemoveUnit` call this path. Direct removal must not strand a hidden/paused worker.

On successful Ancient completion, the Wisp has already relinquished its Food Used and is removed instead of released. For cancellation/destruction the Wisp survives and its authored `UnitBalance.foodUsed` is restored.

## Undead summon window

Warsmash keeps the Acolyte in its work behavior for approximately 2.267 seconds after the structure is created. OpenRealm stores this as `construction.worker_release_time` using the authoritative simulation clock (`level.time` / `G_Time()`). The building starts autonomous progress immediately; releasing the Acolyte does not alter the construction clock.

The value is a construction-behavior compatibility constant, not an animation-file duration discovered at runtime.

## Ancient classification

Night Elf Wisp consumption is selected from the spawned building's object data. OpenRealm checks the comma-separated `UnitBalance.type` classification first and falls back to `UnitData.unitClassification`; the `ancient` token enables `construction.consumes_worker`.

Do not infer Ancient status from rawcodes or model names.

## Repair boundary

Only `CONSTRUCTION_HUMAN` may use Human Repair as a construction clock. Standard Repair and Human `Arep` must reject active Orc, Undead, and Night Elf construction so Repair cannot accidentally become a second progress source.

Completed buildings continue through the ordinary Repair behavior documented in [Building Construction](building-construction.md).

## Save/load

The construction strategy, release time, worker-state flags, Food-consumption flag, and worker `spawn_time` live in the raw `edict_t` snapshot. `construction.worker`, like `construction.primary_builder`, is an edict pointer and therefore has an explicit `F_EDICT` fixup in `g_save.c`.

Save format version 20 introduced the construction-worker reference. Version 21 adds racial gold-mine overlay/Acolyte references and their timing/slot state. Older layouts are rejected rather than interpreting a shifted `edict_t` or an un-fixed raw pointer.

## Race-specific gold mining

The ordinary `Agld` mine remains the finite resource owner for every race. Human/Orc workers use the established hidden-inside,
carry, return, and deposit state machine. A mine with one or more conventional workers inside also carries the `work` animation
property, removed when occupancy returns to zero.

Haunted and Entangled mines instead use `edict.mineoverlay.parent` plus the parent's spawn generation. The overlay hides/pauses the
ordinary mine while it exists, but never copies its `resources`; death or `RemoveUnit` restores the parent. Normal `isBuildOn`
construction binds an Undead overlay to the exact mine found by authoritative placement. `Aent` creates the authored Night Elf
resulting UnitID at the target mine and starts the autonomous Night Elf construction clock without attaching a Wisp.

Undead Acolytes use `Aaha` rather than conventional Harvest. `Abgm` DataC and DataD define the number and radius of fixed ring slots.
An Acolyte walks into ability range, selects the nearest free slot, snaps to its deterministic ring point, remains visible in
`stand work`, and owns `{mine, mine_spawn_time, slot}` until retasked, killed, removed, or the mine disappears. `Abgm` DataA/DataB
drive direct gold income from the parent. The current Warsmash source uses integer `maxMiners / activeMiners` when stretching the
interval; OpenRealm intentionally preserves that integer behavior rather than substituting fractional scaling.

Night Elf Entangled Mines reuse the existing `Aenc` cargo contract. Mining Wisps are hidden/paused cargo occupants, and the mine uses
first-through-fifth secondary animation tags for occupancy. `Aegm` DataA/DataB advance a persistent round-robin slot index before
each occupancy test; occupied turns pay from the parent's finite gold pool and empty turns do not. Parent depletion kills the
Entangled overlay, whose normal death path ejects cargo and restores the original mine. Loading is rejected until Entangled
construction completes.

`mineoverlay.parent` and `acolyte_mine.mine` are persistent edict references with `F_EDICT` fixups. Save format 21 adds those fields
and the associated scalar timing/index/slot state.

Haunted Mine ring `EffectArt` now follows the same deterministic slot positions/facing as Warsmash and is removed with the overlay; Acolyte wrong-target, wrong-owner, and full-ring failures use Warcraft `CommandStrings` error keys. Known remaining gaps are primarily Entangle cast/icon/effect presentation and broader retail visual verification. Wisp lumber remains separate from Entangled gold cargo.

## Moon Well replenish

The manual `Ambt` replenish cast follows Warsmash's `CAbilityMoonWell` ordering and data meanings:

- `DataB` is well-mana spent per target hit point restored;
- `DataA` is well-mana spent per target mana point restored;
- one cast restores missing life first, then spends the remaining Moon Well mana on missing target mana.

The fields are ratios, not per-cast caps. A full-health friendly target with missing mana is therefore still a valid replenish target. Autocast selection, `DataE` night-only regeneration gating, and `DataD` water-height presentation remain separate gaps.

## Undead defensive tower upgrades

Ziggurat -> Spirit Tower/Nerubian Tower does not require a dedicated race-specific morph path. The shared `UnitProfile.Upgrade` / `uupt` building-upgrade lifecycle preserves the existing edict, source Food Made contribution during progress, owner/position/health ratio, and then binds the completed target's authored unit/weapon data. See [Building Construction](building-construction.md).

The ordinary Attack ability now enforces Attack 1's authored target mask for unit targets as well as destructables, allows enemy structures to participate in acquisition when the weapon permits them, and prevents `AI_IMMOBILE` defensive buildings from automatically locking onto targets outside actual weapon range that they cannot chase. See [Attack Damage](attack-damage.md).

This does **not** complete the Undead tower contract by itself: Ziggurat/Spirit Tower terrain Blight now rides on the shared dynamic-Blight field, but retail verification remains separate.

## Remaining race-specific gaps

Dynamic Blight simulation is now implemented through a WC3-owned mutable field seeded from the shared WPM pathing flags: map-authored WPM Blight initializes it, `Abli` expands/removes it from authored data, the five JASS Blight natives share it, `requirePlace=blighted` reports `Offblight`, `uhrt=blight` reads it, and save format 32 persists it. See [Blight](blight.md). Runtime client terrain rendering/preview synchronization and Blighted-tree presentation remain separate gaps.

The following items remain outside this patch's high-confidence isolated scope:

- Wisp periodic lumber harvesting and per-tree Wisp reservation;
- full Ancient Root/Uproot classification, footprint, ability, attack, defense, and movement transitions;
- Moon Well autocast, night-only mana regeneration, and water-level presentation. Manual replenish now restores life first and then mana using the authored DataB/DataA ratios.

Existing placeholder handlers for those mechanics must not be treated as retail-compatible merely because their rawcode is recognized.

## Verification

After building, focused automated coverage should include:

```sh
make test-wc3-engine WC3_PATTERN='wc3_building.*'
make test-wc3-engine WC3_PATTERN='wc3_combat.*acquisition*'
make test-wc3-engine WC3_PATTERN='wc3_save.*construction*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*mine*'
make test-wc3-engine WC3_PATTERN='wc3_save.racial_gold_mine*'
make test-wc3-engine WC3_PATTERN='wc3_spell.moon_well_*'
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.*blight*'
make test
```

Runtime checks should cover at least:

1. Orc Peon disappears into a newly placed structure, cannot be selected/ordered/collided with while inside, and reappears when construction completes.
2. Cancelling, destroying, or removing unfinished Orc construction immediately releases the Peon.
3. Undead Acolyte begins a visible summon/work animation, becomes available after the summon window, and the building continues without it.
4. A Night Elf Wisp is committed inside ordinary construction and returns on completion.
5. A Wisp constructing an Ancient stops contributing Food Used while inside and is consumed on completion.
6. Cancelling/destroying an unfinished Ancient returns the Wisp and restores its Food Used.
7. Human construction still pauses when its primary builder stops and still supports Power Build; Repair does not accelerate autonomous race construction.
8. Save/load during each worker-owned construction state preserves the correct worker relationship and release/consumption behavior.
9. Moon Well replenish heals before restoring mana and accepts a full-health friendly unit that is missing mana.
10. Haunted construction hides the original mine; Acolytes occupy distinct visible ring slots, scale direct income, and free slots when retasked.
11. Entangled Mine Wisps board through cargo, income follows occupied round-robin slots, and depletion ejects Wisps/restores the parent mine.
12. Save/load during Haunted/Entangled mining preserves parent references, income timing/index state, and Acolyte slot ownership.
