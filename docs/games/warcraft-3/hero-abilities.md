# Hero Ability Progression

## Contract

Hero skill progression is split across three sources of truth:

- `UnitAbilities_t::abilList` — ordinary unit abilities already available to the unit.
- `UnitAbilities_t::heroAbilList` — the ordered candidate skills a Hero may learn.
- `AbilityData_t` — per-skill `levels`, `reqLevel`, and `levelSkip` progression data.

Runtime learned Hero skills remain in `edict_t::heroabilities[]`; one entry stores one ability rawcode and its current rank. `doodadHero_t::skillpoints` stores unspent Hero skill points.

`heroAbilList` is not a list of already learned abilities. Learning rank 1 creates the runtime entry; later learning increments the same entry.

## Progression

A newly initialized Hero starts at level 1 with one skill point. Runtime-created Heroes use the same progression initializer after their object data is bound, so their level/point state does not depend on whether presentation-only UnitUI/model data is available. A map-placed Hero may already have a non-zero level from `war3mapUnits.doo`; because that file stores the level but not an unspent-point field, `G_HeroInitializeProgression()` seeds the level-derived point budget and subtracts any runtime Hero ranks that were already populated. This happens before authored map-script `SelectHeroSkill` calls consume the configured starting skills. `G_HeroSetXP()` grants one additional skill point for every later Hero level crossed and publishes both `EVENT_PLAYER_HERO_LEVEL` and `EVENT_UNIT_HERO_LEVEL` for each crossed level.

The required Hero level for the next rank is:

```text
reqLevel + current_rank * levelSkip
```

When an ability has `levelSkip == 0`, `Misc/HeroAbilityLevelSkip` is used. The Warcraft default is 2 when the gameplay constant is unavailable.

`G_HeroSkillState()` is the authoritative eligibility check. It verifies:

1. the rawcode is present in the unit's `heroAbilList`;
2. the ability exists in `AbilityData.slk` and has another rank;
3. the Hero has an unspent skill point;
4. the Hero meets the next-rank level requirement.

`G_HeroLearnSkill()` performs that check, advances exactly one rank, and consumes exactly one point. Both the in-game `research` command and the JASS `SelectHeroSkill` native route through this function. Point mutation itself is centralized in `G_HeroModifySkillPoints()` so level-up grants, learned-skill consumption, and scripted point changes share the same non-negative storage and command-card invalidation.

## Direct Skill-Point Scripting

`common.j` exposes `GetHeroSkillPoints(unit)` and `UnitModifySkillPoints(unit, delta)`. The second argument is a signed **delta**, not an absolute value. Blizzard.j's `ModifyHeroSkillPoints` ADD/SUB/SET wrapper is implemented in terms of that delta native, including SET as `requested - GetHeroSkillPoints(hero)`.

OpenRealm returns zero for `GetHeroSkillPoints` on a non-Hero and rejects `UnitModifySkillPoints` on a non-Hero. Positive deltas add unspent points (clamped to `INT32_MAX` so the JASS integer getter stays non-negative); negative deltas remove points and clamp at zero. Attempting another negative change when the pool is already zero returns false. Direct point changes do not alter Hero XP or level and do not publish Hero-level events.

This means an initialization trigger may independently configure both progression axes, for example setting Hero XP/level and then adding extra unspent points before normal interaction starts. OpenRealm does not yet emulate retail's obscure positive `UnitModifySkillPoints` capacity limit based on the Hero's remaining learnable ranks: enforcing it before map/campaign object ability overrides are merged would incorrectly reject custom Hero skill trees.


## Experience And Map Startup

Hero XP is one progression state regardless of source. `AddHeroXP`, `SetHeroXP`, `SetHeroLevel`, kill XP, `AIem` experience items, and `AIlm` level items all converge on `G_HeroSetXP()` / `G_HeroXPForLevel()`. `SetHeroXP` is raise-only: asking for XP at or below the current value leaves both XP and level unchanged, matching the Warsmash contract and avoiding a high-level Hero with an XP total below its level threshold.

`G_HeroXPForLevel()` reads the active `Misc/NeedHeroXP` per-level requirement table and extends it with `NeedHeroXPFormulaA/B/C` when the authored list is exhausted. The values are accumulated to produce the XP required to *reach* a Hero level. With stock data this gives 0, 200, 500, 900, ... cumulative XP for levels 1, 2, 3, 4, .... If Misc data is unavailable entirely, OpenRealm falls back directly to the stock per-level requirements `200,300,400,...`; it does not extrapolate from a synthetic one-entry table. `war3mapMisc.txt` participates in the same loaded Misc cache, so map gameplay-constant overrides affect JASS, items, HUD progress, and level calculation through the same lookup.

Kill XP first searches `HeroExpRange` for alive, non-illusion, XP-enabled Heroes owned by the killer or covered by the killer player's directional `ALLIANCE_SHARED_XP`. If no local receiver exists and `Misc/GlobalExperience` is enabled, the same eligibility check is repeated without the distance limit. This is a fallback, not an additional second award.

There is no separate “starting XP” subsystem. Preplaced units exist before the map's `war3map.j` `main()` runs, so an initialization action may call `AddHeroXP`, `SetHeroXP`, or `SetHeroLevel` on an existing Hero and use the ordinary progression path before normal interaction begins. Trigger registration order still matters: a Hero-level handler only observes transitions published after that handler is registered. Campaign `StoreUnit`/`RestoreUnit` is a separate persistence path and preserves the resulting Hero progression across maps.

## Skill Menu

`skills/s_selectskill.c` enumerates `heroAbilList` in authored order. Each button is built with the next rank rather than hardcoded rank 1, so Research tooltip selection uses the rank being purchased and `%d` level placeholders are replaced with that next rank. Warcraft ability strings commonly store a quoted single ResearchTip; the shared list lexer must leave its cursor on the terminating NUL when that quoted item has no following comma. Advancing a missing comma produces the invalid `0x1` parser address and crashes when rank 2 asks for another list item.

- level-locked skills remain visible but disabled and append `Requires: Level N`;
- skills with no available points remain visible but disabled;
- maxed skills are omitted from the learn menu;
- Research UI fields, including the Research hotkey, are used for learn buttons.

The ordinary command card treats `abilList` and `heroAbilList` independently: a Hero may have ordinary abilities and a Select Skill button at the same time.

## JASS

Implemented Hero-progression natives:

- `GetHeroSkillPoints` reads the current unspent point pool.
- `UnitModifySkillPoints` applies a signed delta without changing XP or Hero level.
- `SetHeroXP` and `AddHeroXP` use the shared XP transition; `SetHeroXP` does not lower XP.
- `SetHeroLevel` raises a Hero through `G_HeroSetXP()`, preserving XP-driven skill points and both Hero-level event families.
- `SelectHeroSkill` uses normal candidate/point/level/max-rank validation.
- `GetUnitAbilityLevel` reports the current learned Hero rank and treats an ordinary ability listed in `abilList` as level 1.

Direct runtime ability mutation (`UnitAddAbility`, `UnitRemoveAbility`, `IncUnitAbilityLevel`, `SetUnitAbilityLevel`) is still incomplete because OpenRealm does not yet have a general runtime ability collection separate from the four Hero skill slots. Do not implement those natives by silently treating `heroabilities[]` as an unlimited generic ability store.

## Known Boundaries

Map/campaign object modifications are not yet merged into the typed runtime unit/ability rows. `war3map.w3u` parsing exists, but a map-specific `heroAbilList`, and `war3map.w3a`/campaign overrides for `levels`, `reqLevel`, `levelSkip`, or Research UI fields, still require the object-data merge layer.

`SetPlayerAbilityAvailable` also remains separate work; player-wide ability disable state needs explicit ownership and runtime/UI gating rather than a Hero-menu-only special case.

`SetHeroLevel` still does not lower a Hero. Requests at or below the current level are ignored; implementing level loss needs explicit XP/stat/event semantics rather than reversing the raise path opportunistically.

Hero-level events are queued with the unit pointer rather than an immutable level payload. A multi-level XP gain publishes both player-unit and unit-specific events per crossed level, but handlers that run later observe the unit's then-current level; preserving an intermediate-level snapshot would require an event-context change.

The JASS `showEyeCandy` arguments on `SetHeroXP`, `AddHeroXP`, and `SetHeroLevel` are still ignored. Level-up sound/light/portrait presentation needs a separate renderer/client presentation contract. `MaxLevelHeroesDrainExp` and `SummonedKillFactor` are present in Warcraft gameplay data but are not yet consumed by OpenRealm's kill-XP path; do not infer their edge-case semantics from the key names alone.

The Select Skill command button displays the Hero's non-zero unspent skill-point count, and learn buttons display the next rank. Multi-selection suppression remains UI work.

Campaign carry-over is now separate from Hero progression itself: `StoreUnit` and `RestoreUnit` preserve the Hero level, XP, unspent skill points, and learned ranks through the campaign game cache. See [campaign-game-cache.md](campaign-game-cache.md). Starting a later campaign map without a saved prior-map cache still uses that map's authored fallback Hero state.

## Verification

Focused in-engine tests live in `games/warcraft-3/game/tests/t_combat.c` and `games/warcraft-3/game/tests/t_api.c` and cover:

- level-derived initial point budgets for map-placed Heroes;
- stock `NeedHeroXP` cumulative thresholds and a fixture-authored table/formula extension;
- one skill point per Hero level crossed;
- player-unit and unit-specific Hero-level events for every crossed level;
- raise-only XP semantics;
- `GlobalExperience` fallback when no eligible Hero is inside `HeroExpRange`;
- JASS `main()` granting startup XP through the ordinary Hero progression path;
- JASS `GetHeroSkillPoints` and signed `UnitModifySkillPoints` changes, including zero clamping and non-Hero rejection;
- map-start direct skill-point awards that leave XP and level unchanged;
- first-rank learning and point consumption;
- no-point disabling;
- `reqLevel + current_rank * levelSkip` rank gates;
- maximum-rank rejection;
- ultimate-style first-rank level gates;
- rejection of skills not present in `heroAbilList`;
- quoted single-value and quoted multi-value list parsing used by Research tooltips.

Run the relevant suite when validating locally:

```bash
make test-wc3-engine WC3_PATTERN="wc3_combat.hero_*"
make test-wc3-engine WC3_PATTERN="wc3_combat.grant_kill_xp_*"
make test-wc3-engine WC3_PATTERN="wc3_api.hero_xp_*"
make test-wc3-engine WC3_PATTERN="wc3_api.hero_skill_points_*"
```
