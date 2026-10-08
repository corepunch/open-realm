# Warcraft III Race Mechanics

## Scope

The shared WC3 simulation should own common state such as construction progress, food accounting, resources, unit lifetime, and pathing, while race-specific abilities/behaviors own the different state machines used by Human, Orc, Undead, Night Elf, and Naga units.

This document records the source comparison used for OpenRealm's race-specific construction and economy work and the remaining race-mechanic gaps. It is deliberately narrower than a general unit-data reference: a mechanic is listed here when two races perform the same RTS concept through materially different simulation state.

Reference behavior was compared against the bundled Warsmash sources:

- `CBehaviorHumanBuild.java`
- `CBehaviorOrcBuild.java`
- `CBehaviorUndeadBuild.java`
- `CBehaviorNightElfBuild.java`
- `CAbilityNagaBuild.java`
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
| Naga | Mur'gul builder is hidden, paused, invulnerable inside construction | building-owned autonomous clock | builder is released beside the building |

All strategies begin at 10% maximum life and hold the Birth animation to authoritative construction progress. Human remains paused unless a valid Human Repair participant advances it. Orc, Undead, Night Elf, and Naga construction advance once per simulation frame through `G_RunConstructionFrame()` and add the corresponding fraction of `(max_life - start_life)` rather than deriving HP from absolute progress; damage to an unfinished building therefore remains damage.

`skills/s_build.c` honours Warcraft's authored Naga build ability before race fallback: a worker that owns `AGbu`, including a custom alias whose base resolves to `AGbu`, enters `CONSTRUCTION_NAGA` even when `UnitData.race` is not Naga. Human Repair and the remaining stock race strategies continue through their existing dispatch, while unknown/custom workers without a recognized construction style retain the legacy construction fallback.

## Worker ownership and lifetime

`edict.construction.primary_builder` remains the Human Repair owner. Race strategies that temporarily own a worker use the separate `edict.construction.worker` reference plus its `spawn_time`; these are distinct because Human can have multiple Repair participants while Orc/Night Elf/Naga construction has one internal worker and Undead only retains the summoner for the opening work animation.

An internal worker is `RF_HIDDEN`, paused, and temporarily invulnerable. `RF_HIDDEN` makes it hollow to OpenRealm collision/pathing and keeps it out of ordinary selection/idle-worker presentation. Its pre-construction invulnerability state is restored when released.

`G_StopConstruction()` is the common non-completion teardown. It:

1. cancels Human Repair participants;
2. releases an Orc/Night Elf/Naga worker or an unfinished Ancient Wisp;
3. releases an Undead summoner if its short summon window is still active;
4. restores an Ancient Wisp's authored Food Used when construction does not finish;
5. clears construction state and the held Birth animation.

Both unit death and direct `G_FreeEdict()`/`RemoveUnit` call this path. Direct removal must not strand a hidden/paused worker.

On successful Ancient completion, the Wisp has already relinquished its Food Used and is removed instead of released. For cancellation/destruction the Wisp survives and its authored `UnitBalance.foodUsed` is restored.

## Naga construction and terrain predicates

Warsmash gives Naga a distinct `AGbu` / `CAbilityNagaBuild`, but that ability deliberately reuses `CBehaviorOrcBuild`. OpenRealm mirrors the worker-inside lifecycle while retaining `CONSTRUCTION_NAGA` as a distinct simulation identity: the Mur'gul builder is hidden, paused, and temporarily invulnerable while the unfinished structure advances autonomously, then the shared completion/cancellation/destruction path restores the worker.

Building terrain legality remains data-driven. The common `preventPlace` / `requirePlace` parser understands Warcraft/Warsmash `unwalkable`, `unbuildable`, `unflyable`, `blockvision`, `blighted`, `unfloat`, and compound `unamph`. `unamph` is true only when a cell is both unwalkable and unswimmable; the same predicate rule is used by authoritative placement and the client preview grid. Naga construction does not bypass these pathing rules. OpenRealm's existing default building masks still apply, so stock shallow-water parity should be verified from the authored Naga object data before relaxing a global placement policy.

## Naga Submerge

`Asb1`, `Asb2`, `Asb3`, and `ANsu` use Warcraft's paired `submerge` / `unsubmerge` orders. The ability row supplies the normal unit type in Data A and the submerged unit type in UnitID, so the runtime transforms the same edict between authored forms rather than hard-coding Myrmidon, Royal Guard, or Snap Dragon rawcodes. Entering Submerge is legal only on terrain that is swimmable and not walkable; attempting it elsewhere reports the Warcraft command-error key `Cantsubmergethere`. Surfacing is the inverse transform.

The submerged state is stored as an ability status so the alternate command state and save snapshot remain attached to the same unit identity. The submerged form uses `RF_HIDDEN` through the existing gameplay-invisibility visibility path: owners/shared vision can still receive the unit while hostile viewers require ordinary detection. The alternate unit row remains authoritative for movement speed, attacks, model, and other submerged-form stats. Exact retail morph animation/effect timing is presentation follow-up work rather than a reason to duplicate those stats in ability code.

The normal Warcraft UI skin/error-string selector remains four-race indexed. Campaign Naga interface responses should therefore come from the same map/game-interface override mechanisms used by Warcraft campaign content; OpenRealm must not invent a fifth built-in `war3skins` race category merely because `RACE_NAGA` exists in gameplay data.

## Undead summon window

Warsmash keeps the Acolyte in its work behavior for approximately 2.267 seconds after the structure is created. OpenRealm stores this as `construction.worker_release_time` using the authoritative simulation clock (`level.time` / `G_Time()`). The building starts autonomous progress immediately; releasing the Acolyte does not alter the construction clock.

The value is a construction-behavior compatibility constant, not an animation-file duration discovered at runtime.

## Ancient classification

Night Elf Wisp consumption is selected from the spawned building's object data. OpenRealm checks the comma-separated `UnitBalance.type` classification first and falls back to `UnitData.unitClassification`; the `ancient` token enables `construction.consumes_worker`.

Do not infer Ancient status from rawcodes or model names.

## Repair boundary

Only `CONSTRUCTION_HUMAN` may use Human Repair as a construction clock. Standard Repair and Human `Arep` must reject active Orc, Undead, Night Elf, and Naga construction so Repair cannot accidentally become a second progress source.

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
When a map script removes a neutral mine before creating a Haunted Mine, `RemoveUnit` defers freeing that edict until the current
tick ends. Preplaced overlay binding skips deferred mines and creates a live parent at the same location, preserving the replacement's
gold and allowing Haunted Mine death to restore a working ordinary mine.

Undead Acolytes use `Aaha` rather than conventional Harvest. `Abgm` DataC and DataD define the number and radius of fixed ring slots.
An Acolyte walks into ability range, selects the nearest free slot, snaps to its deterministic ring point, remains visible in
`stand work`, and owns `{mine, mine_spawn_time, slot}` until retasked, killed, removed, or the mine disappears. `Abgm` DataA/DataB
drive direct gold income from the parent. The current Warsmash source uses integer `maxMiners / activeMiners` when stretching the
interval; OpenRealm intentionally preserves that integer behavior rather than substituting fractional scaling.
The stock `autoharvestgold` immediate order also starts this `Aaha` behavior by selecting the nearest valid Haunted Mine owned by
the Acolyte. Undead campaign setup uses that targetless order after `BlightGoldMineForPlayer` creates the Haunted Mine; the same
order remains supported for `Ahar` workers and Entangled Mine Wisps.

Night Elf Entangled Mines reuse the existing `Aenc` cargo contract. Mining Wisps are hidden/paused cargo occupants, and the mine uses
first-through-fifth secondary animation tags for occupancy. `Aegm` DataA/DataB advance a persistent round-robin slot index before
each occupancy test; occupied turns pay from the parent's finite gold pool and empty turns do not. Parent depletion kills the
Entangled overlay, whose normal death path ejects cargo and restores the original mine. Loading is rejected until Entangled
construction completes, but a Wisp rallied or Smart-ordered to the incomplete overlay retains a stand-pose boarding behavior and
retries the same cargo load as soon as construction completes. `CAbilityEntangledGoldMine` advances income through the ordinary
`A_UPDATE` scheduler. The Tree's `CAbilityEntangle` handles death and removal, retiring the overlay immediately rather than waiting
for corpse decay. The movement regressions cover the real income scheduler, incomplete-mine Wisp wait/retry, and Tree-death
restoration.

`Aent` has two distinct initiation paths. Normal `entangle`/`autoentangle` create the authored overlay and enter autonomous Night Elf
construction. `entangleinstant`/`autoentangleinstant` bind the same overlay immediately without starting the construction clock; this
is the path used by Blizzard's standard Night Elf melee-start script after it creates the Tree of Life near the starting Gold Mine.
The script-owned mine search and starting-unit placement remain outside the C game module. Explicit target orders are routed through
the unit's authored target-order dispatcher rather than being special-cased in JASS or melee setup.

Entangle range is measured against the Gold Mine's authored pathing footprint where available. The caster's collision radius plus the
authored `Aent` range must reach that footprint; non-footprint targets fall back to the ordinary sum-of-collision-radii unit-range
calculation. This is required for stock-style `Aent` ranges that are much smaller than the centre-to-centre distance between the two
large buildings. After an uprooted Ancient finishes Root, the Root lifecycle performs one deterministic nearby-mine acquisition and
starts ordinary non-instant Entangle through the same target validation/execution owner. Initial melee creation does not use this
auto-root path because Blizzard's melee script explicitly issues `entangleinstant`.

`mineoverlay.parent` and `acolyte_mine.mine` are persistent edict references with `F_EDICT` fixups. Save format 21 adds those fields
and the associated scalar timing/index/slot state.

Haunted Mine ring `EffectArt` follows the same deterministic slot positions/facing as Warsmash and is removed with the overlay; Acolyte wrong-target, wrong-owner, and full-ring failures use Warcraft `CommandStrings` error keys. Entangle uses `Targetgoldmine`, retains the casting Tree's persistent caster art, and hides/marks Aent permanent only for the live overlay lifetime. Wisp lumber remains separate from Entangled gold cargo and now uses persistent direct DataA income, one-Wisp-per-tree reservation, TargetArt at DataC height, and authored looped harvest audio.

## Moon Well replenish

The manual `Ambt` replenish cast follows Warsmash's `CAbilityMoonWell` ordering and data meanings:

- `DataB` is well-mana spent per target hit point restored;
- `DataA` is well-mana spent per target mana point restored;
- one cast restores missing life first, then spends the remaining Moon Well mana on missing target mana.

The fields are ratios, not per-cast caps. A full-health friendly target with missing mana is therefore still a valid replenish target. Nearest-valid autocast honors `DataC` and the authored `Area` acquisition radius; stock ROC and TFT `Ambt` have `Area=400` and `Rng=99999`, so cast range must not substitute when Area is zero. `DataE` gates the well's natural mana regeneration to night, and construction suppresses that natural regeneration until the construction completion tick. External mana regeneration bonuses and auras retain their ordinary behavior. Persistent water `EffectArt` height follows `DataD * current_mana_fraction`.

## Undead defensive tower upgrades

Ziggurat -> Spirit Tower/Nerubian Tower does not require a dedicated race-specific morph path. The shared `UnitProfile.Upgrade` / `uupt` building-upgrade lifecycle preserves the existing edict, source Food Made contribution during progress, owner/position/health ratio, and then binds the completed target's authored unit/weapon data. See [Building Construction](building-construction.md).

The ordinary Attack ability now enforces Attack 1's authored target mask for unit targets as well as destructables, allows enemy structures to participate in acquisition when the weapon permits them, and prevents `AI_IMMOBILE` defensive buildings from automatically locking onto targets outside actual weapon range that they cannot chase. See [Attack Damage](attack-damage.md).

This does **not** complete the Undead tower contract by itself: Ziggurat/Spirit Tower terrain Blight now rides on the shared dynamic-Blight field, but retail verification remains separate.

## Remaining race-specific gaps

Dynamic Blight simulation is now implemented through a WC3-owned mutable field seeded from the shared WPM pathing flags: map-authored WPM Blight initializes it, `Abli` expands/removes it from authored data, the five JASS Blight natives share it, `requirePlace=blighted` reports `Offblight`, `uhrt=blight` reads it, and save format 32 persists it. Client terrain rendering/preview synchronization and one-way Blighted-tree presentation are also implemented through the generic terrain-mask, image, and vertex-colour channels. See [Blight](blight.md); retail visual verification remains separate.

Undead corpse mechanics now share authored raisability and lifetime state: `deathType` controls raise/decay eligibility, ordinary corpses use map `DecayTime` then `BoneDecayTime`, decaying structures use `StructureDecayTime`, and active consumers can reserve a corpse without its timer expiring underneath them. `Acan` Cannibalize keeps its corpse until channel end, heals continuously from DataA and stops at full life; `Arai`/`ACrd`/`AIrd` Raise Dead consume a valid corpse into the two authored DataA×DataC / DataB×DataD summon groups with timed life and authored buff/effect identity, and `Raiu`/UnitID drives the stock 25-unit-type oldest-first summon cap. Both use `Cantfindcorpse`. Corpse presentation now stretches `Decay Flesh`, `Decay Bone`, and Hero `Dissipate` over the matching gameplay timers with same-family animation fallback. `Agyd` Graveyards generate their authored `UnitID` on `Cool`, with DataA cap, DataB/Gyd2 spawn radius, and DataC/Gyd3 count radius. Meat Wagon `Sch2`/`Amtc` cargo stores real corpse edicts; corpse consumers treat stored remains at the Wagon's current position, and unloading a bone-phase corpse restarts map `BoneDecayTime`. `Amel` Get Corpse supports the shared autocast scheduler over authored acquisition range and target data. Generic `WPN_ARTILLERY` attacks use ranged projectile launch, the unit's authored minimum range, authored three-band splash damage, Warsmash-style point locking at the damage point, and explicit Attack Ground; Disease Cloud remains separate work. See [Corpse Lifecycle, Cannibalize, and Raise Dead](corpse-mechanics.md) and [Attack Damage](attack-damage.md).

Undead worker conversion/destruction mechanics are also implemented at the broad-race level: `Auns` now channels authored `DataB` demolition damage, grants temporary `Buns` spell immunity, and returns the `DataA` resource pool progressively only for HP removed by Unsummon; `Asac`/`Alam` now queue the fixed Shade result at a Sacrificial Pit using the Shade's authored build time while hiding the Acolyte and preserving its food slot. See [Unsummon](unsummon.md) and [Undead Sacrifice](sacrifice.md). Remaining work in these two mechanics is presentation/command-error polish plus upgraded-building accumulated-cost parity for Unsummon.

The principal remaining Night Elf race-mechanics gap is full Ancient Root/Uproot classification, footprint, ability, attack, defense, and movement transitions. The current `Aroo` handler remains a placeholder `no_pathing`/movetype toggle; it must not be treated as retail-compatible merely because the rawcode is recognized. Stable rooted and uprooted attack slots follow each root ability's authored `AbilityData` `DataA`/`DataB` masks: `Aro2` (Ancient Protector) permits a rooted attack, while `Aro1` and `Aroo` do not. The server resolves that mask from the authored root ability even before the per-unit root state is initialized, using the current structure/mobile state to select rooted versus uprooted data. Attack slots remain disabled during the actual root/unroot morph. Root/Uproot still needs one shared dynamic structure/attack-state seam so targeting, Repair, combat, pathing, and command availability change atomically.

## Verification

After building, focused automated coverage should include:

```sh
make test-wc3-engine WC3_PATTERN='wc3_ancient_root.*'
make test-wc3-engine WC3_PATTERN='wc3_building.*'
make test-wc3-engine WC3_PATTERN='wc3_combat.*acquisition*'
make test-wc3-engine WC3_PATTERN='wc3_save.*construction*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*mine*'
make test-wc3-engine WC3_PATTERN='wc3_save.racial_gold_mine*'
make test-wc3-engine WC3_PATTERN='wc3_spell.moon_well_*'
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.blight_*'
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
9. Moon Well replenish heals before restoring mana; autocast honors DataC and nearest valid range, natural well regeneration is night-gated by DataE, and water height tracks remaining mana.
10. Wisp lumber remains attached to one reserved tree, credits periodic direct lumber, retargets when its tree is unavailable, and retires TargetArt plus looped harvest sound when harvesting stops.
11. Haunted construction hides the original mine; Acolytes occupy distinct visible ring slots, scale direct income, and free slots when retasked.
12. Entangled Mine Wisps board through cargo, including waiting at an incomplete mine and automatically boarding after completion; income follows occupied round-robin slots, depletion ejects Wisps/restores the parent mine, and the casting Tree's Entangle command remains hidden/permanent only for that overlay lifetime.
13. `entangleinstant` creates a completed overlay for melee initialization, ordinary Entangle remains under construction, footprint-aware range accepts an adjacent large Gold Mine, and Root completion auto-entangles an eligible nearby mine once.
14. Save/load during Haunted/Entangled mining preserves parent references, Entangle caster identity, income timing/index state, and Acolyte slot ownership.
