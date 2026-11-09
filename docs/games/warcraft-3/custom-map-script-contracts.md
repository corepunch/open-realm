# Custom-Map Script and Object-Data Contracts

Custom maps lean on retail behaviour that stock melee data rarely exercises:
abilities added and removed by script, buffs tested by rawcode, `Channel`-based
spells whose effect lives entirely in triggers, and map-wide `Misc` constants.
This page lists those contracts, where OpenRealm implements them, and the
regression test that pins each one.

The evidence map was Hero Line Wars RoC 5.3b (`Maps\Download\HLWRoCv5.3b.w3m`,
protected, 1.27 `common.j`, ~6,000 registered events). Every behaviour below is
generic; no engine code tests a map rawcode.

## Object data

| Contract | Owner | Test |
| --- | --- | --- |
| Hero max HP, mana and armor derive from `HP`/`manaN`/`def` plus `StrHitPointBonus`, `IntManaBonus`, `AgiDefenseBonus` and `AgiDefenseBase` (stock 25 / 15 / 0.3 / -2). `realHP`/`realM`/`realdef` are World Editor previews made with stock constants and are only a fallback for rows without `HP`. | `G_RecomputeHeroStats` (`m_unit.c`), constants in `InitConstants` (`g_main.c`) | `wc3_spell.hero_vitals_follow_map_misc_attribute_constants` |
| `war3map.w3h` custom buffs (simple object-modification layout, like `w3u`). Unset fields (`fart`, `ftip`, ...) inherit from the base buff, as the World Editor does. | `CM_ReadBuffs` (`common/world.c`), `MapBuffText` in `FindConfigValue` (`g_metadata.c`) | `wc3_api.map_buff_text_inherits_unset_fields_from_base_buff` |
| A missing `Researchbuttonpos` means `0,0` in the learn menu. | `ui_selectskill` (`s_selectskill.c`) | - |
| Hero command `Tip`/`Ubertip` are per-level lists. Live buttons show the learned rank, or level 1 before it is learned. | `G_BuildCommandButtonState` (`hud_unit.c`) | `wc3_spell.hero_command_tooltip_shows_only_the_learned_level` |

## Abilities

| Contract | Owner | Test |
| --- | --- | --- |
| `Channel` (`ANcl`) is a real spell. `DataB` is the target type (0 none, 1 unit, 2 point, 3 unit or point), `DataA` the follow-through time, and `DataC` bit 1 makes the button visible. The effect comes from map `SPELL_*` triggers. | `CAbilityChannel` (`s_area_spell.c`), `S_SpellTargetTypeForCode` (`s_spell.c`) | `wc3_spell.channel_target_type_follows_alias_datab` |
| Unit-or-point spells open a target mode where a unit click targets the unit and a ground click targets the point. Previously they were cast with no target, so `GetSpellTargetLoc` returned `(0,0)`. | `spell_cmd` (`s_spell.c`) | in-game: Channel with `DataB 3` |
| Item bonus abilities (`AIat`, `AIde`, `AIml`, attribute bonuses, `Arel`) also apply when a script adds them to a unit: `UnitAddAbility` sends `A_ENABLE` and `UnitRemoveAbility` sends `A_DISABLE`. Maps build damage bonuses from bit-flag sets of such abilities. | `BZ_ITEM_BONUS_PROC` (`s_item_stats.c`) | `wc3_spell.item_attack_bonus_added_to_unit_applies_and_reverts`, `wc3_spell.item_life_regeneration_bonus_applies_and_reverts` |
| Poison and orb attacks use the owned alias, so a custom alias applies its own `BuffID` and duration. | `S_PoisonOnHit`, `S_OrbOnHit` | covered by poison/orb suites |
| Aura target token `player` limits recipients to the source's owner. Auras on runtime-added aliases (a dummy unit given `AOae` by script) reach recipients. | `aura_allows_target` (`s_hero_passives.c`) | `wc3_spell.runtime_added_aura_alias_buffs_owned_hero_only` |
| Frost Armor adds `DataB` armor through a status modifier and slows melee attackers with `Bfro` for the ability's `Dur`. | `CAbilityFrostArmor` and status-event dispatch (`s_ability_stubs.c`, `s_attack.c`) | `wc3_spell.frost_armor_alias_adds_armor_and_slows_melee_attackers` |
| Only the stock Demon Hunter forms (`Edmm`, `Eevm`) gain `AEmb`/`AEim`/`AEev` on Metamorphosis. A custom `AEme` alias morphs into its own form with that form's hero list. | `AbilityMetamorphosis_Execute` | `wc3_spell.custom_metamorphosis_form_does_not_gain_demon_hunter_skills` |

## JASS

| Contract | Owner | Test |
| --- | --- | --- |
| `GetUnitAbilityLevel(u, buff)` returns the buff level when the unit has that status; `UnitRemoveAbility(u, buff)` removes it. | `api_unit.h` | `wc3_api.ability_natives_read_and_remove_buff_rawcodes` |
| A group filter run inside a trigger action keeps `GetTriggerUnit()`; `GetFilterUnit()` is separate (`jassContext_t.filter_unit`). | `jass_evaluateboolexpr` (`jdo.c`) | `wc3_api.group_filter_keeps_trigger_unit_separate_from_filter_unit` |
| `And`/`Or`/`Not` composites, null triggers as no-ops, locals shadowing globals, `\"` escapes. | `jdo.c`, `jlex.c` | `wc3_api.boolexpr_composites_and_event_id`, `wc3_api.custom_map_jass_semantics` |
| `GetTriggerEventId` exposes the current event type, including player chat. | `api_trigger.h`, `g_commands.c` | `wc3_api.player_chat_fires_matching_triggers` |
| Legacy player-unit event numbering is selected from the parsed `EVENT_PLAYER_UNIT_SELL` initializer; unrecognized forms warn and use the modern layout. | `g_spawn.c`, `jdo.c` | `wc3_api.legacy_event_detection_reads_parsed_constant` |

## Presentation

- World hover names a Hero by its proper name with a `LEVEL` line and shows the health bar only. `hover_value` carries the level with `ENT_HOVER_LABELED_VALUE`; the label is the nametag frame's tooltip.
- Options > Gameplay > Health Bars (or `cl_healthbars 1`) shows own and allied bars. Holding `[` shows friendly bars, `]` shows enemy bars, and either Alt key shows all visible unit bars including mana without names. Destructibles are excluded. The client reuses the game-authored hover layout.
- Enter opens a centered message prompt using the multiboard backdrop. Chat input and the prompt are client-local; the game supplies its layout and key binding.
- In-game menu labels come from `GlobalStrings.fdf` (`KEY_RESTART_MISSION`, `KEY_RETURN_TO_GAME`, `KEY_RESUME_GAME`).

## Diagnosis notes

- When a map effect "does nothing", check the trigger condition first. Several bugs above were a false condition, not a missing effect: a buff rawcode read as an ability level, `GetTriggerUnit()` replaced by the filter unit, or a spell event without a point.
- Map scripts often move a paused hero with `SetUnitX/Y` and `SetUnitFlyHeight` after adding and removing `Arav`. A hero flying toward the map centre means the spell event had no target point.

See also [DotA Custom-Map Playability](dota-map-playability.md) and [ability implementation](ability-implementation.md).
