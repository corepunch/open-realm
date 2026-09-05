# Hero Ability Progression

## Contract

Hero skill progression is split across three sources of truth:

- `UnitAbilities_t::abilList` — ordinary unit abilities already available to the unit.
- `UnitAbilities_t::heroAbilList` — the ordered candidate skills a Hero may learn.
- `AbilityData_t` — per-skill `levels`, `reqLevel`, and `levelSkip` progression data.

Runtime learned Hero skills remain in `edict_t::heroabilities[]`; one entry stores one ability rawcode and its current rank. `doodadHero_t::skillpoints` stores unspent Hero skill points.

`heroAbilList` is not a list of already learned abilities. Learning rank 1 creates the runtime entry; later learning increments the same entry.

## Progression

A newly initialized Hero starts at level 1 with one skill point. A map-placed Hero may already have a non-zero level from `war3mapUnits.doo`; because that file stores the level but not an unspent-point field, `G_HeroInitializeProgression()` seeds the level-derived point budget and subtracts any runtime Hero ranks that were already populated. This happens before authored map-script `SelectHeroSkill` calls consume the configured starting skills. `G_HeroSetXP()` grants one additional skill point for every later Hero level crossed and publishes one `EVENT_PLAYER_HERO_LEVEL` event per crossed level.

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

`G_HeroLearnSkill()` performs that check, advances exactly one rank, and consumes exactly one point. Both the in-game `research` command and the JASS `SelectHeroSkill` native route through this function.

## Skill Menu

`skills/s_selectskill.c` enumerates `heroAbilList` in authored order. Each button is built with the next rank rather than hardcoded rank 1, so Research tooltip selection uses the rank being purchased and `%d` level placeholders are replaced with that next rank. Warcraft ability strings commonly store a quoted single ResearchTip; the shared list lexer must leave its cursor on the terminating NUL when that quoted item has no following comma. Advancing a missing comma produces the invalid `0x1` parser address and crashes when rank 2 asks for another list item.

- level-locked skills remain visible but disabled and append `Requires: Level N`;
- skills with no available points remain visible but disabled;
- maxed skills are omitted from the learn menu;
- Research UI fields, including the Research hotkey, are used for learn buttons.

The ordinary command card treats `abilList` and `heroAbilList` independently: a Hero may have ordinary abilities and a Select Skill button at the same time.

## JASS

Implemented Hero-progression natives:

- `SetHeroLevel` raises a Hero through `G_HeroSetXP()`, preserving XP-driven skill points and per-level events.
- `SelectHeroSkill` uses normal candidate/point/level/max-rank validation.
- `GetUnitAbilityLevel` reports the current learned Hero rank and treats an ordinary ability listed in `abilList` as level 1.

Direct runtime ability mutation (`UnitAddAbility`, `UnitRemoveAbility`, `IncUnitAbilityLevel`, `SetUnitAbilityLevel`) is still incomplete because OpenRealm does not yet have a general runtime ability collection separate from the four Hero skill slots. Do not implement those natives by silently treating `heroabilities[]` as an unlimited generic ability store.

## Known Boundaries

Map/campaign object modifications are only partially merged into typed runtime rows. `war3map.w3u` now applies registered `UnitProfile`/`UnitUI` fields such as `uani` and `umdl`, but a map-specific `heroAbilList`, Balance/Data/Weapons/Abilities unit fields, and `war3map.w3a`/campaign overrides for `levels`, `reqLevel`, `levelSkip`, or Research UI fields still require the broader object-data merge layer.

`SetPlayerAbilityAvailable` also remains separate work; player-wide ability disable state needs explicit ownership and runtime/UI gating rather than a Hero-menu-only special case.

`SetHeroLevel` still does not lower a Hero. Requests at or below the current level are ignored; implementing level loss needs explicit XP/stat/event semantics rather than reversing the raise path opportunistically.

Hero-level events are queued with the unit pointer rather than an immutable level payload. A multi-level XP gain publishes one event per crossed level, but handlers that run later observe the unit's then-current level; preserving an intermediate-level snapshot would require an event-context change.

The Select Skill command button displays the Hero's non-zero unspent skill-point count, and learn buttons display the next rank. Multi-selection suppression remains UI work.

Campaign carry-over is now separate from Hero progression itself: `StoreUnit` and `RestoreUnit` preserve the Hero level, XP, unspent skill points, and learned ranks through the campaign game cache. See [campaign-game-cache.md](campaign-game-cache.md). Starting a later campaign map without a saved prior-map cache still uses that map's authored fallback Hero state.

## Verification

Focused in-engine tests live in `games/warcraft-3/game/tests/t_combat.c` and cover:

- level-derived initial point budgets for map-placed Heroes;
- one skill point per Hero level crossed;
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
```
