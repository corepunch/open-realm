# Barkskin

## Contract

`Abar` is a unit-target modal autocast spell. It is not a self-toggle: `barkskinon` and `barkskinoff` select or clear the caster's autocast policy, while manual casts remain available and disabling autocast does not remove an existing target buff.

The spell accepts living friendly targets allowed by the authored target mask. Automatic acquisition chooses the nearest eligible friendly unit in authored `Rng`, skips units that already have `Bbar`, and issues the ordinary unit-target cast path.

## Data Flow

`AbilityData.slk` supplies `targs`, `Rng`, `Dur`, `HeroDur`, `DataA`, and `BuffID`. ROC exposes the armor field as `Data1`; TFT exposes it as `DataA`. The normalized ability schema makes both available through `S_SpellData(Abar, level, 1)`.

Execution adds the authored `BuffID` as a timed status. `G_UnitArmorValue()` reads active `Bbar` status and adds `Abar/DataA` to the unit's base armor. It does not mutate `armor_value`, so expiration removes the contribution from combat and HUD calculations without extra persisted state. Dead units are rejected as cast targets; ordinary timed-status lifetime remains authoritative after death.

## Registry And Ownership

`CAbilityBarkskin` owns validation, execution, target acquisition, and the on/off orders in `skills/s_nightelf_abilities.c`. Unhandled messages delegate directly to `CAbilityModalSpell`. The `ability_t` row uses `AB_SPELL | AB_AUTOCAST`, `SPELL_TARGET_UNIT`, and the two order names; it deliberately does not use `AB_TOGGLE`.

## Verification

Focused ROC/TFT fixtures are `wc3_spell.barkskin_roc_schema_uses_timed_friendly_armor_buff` and `wc3_spell.barkskin_tft_schema_uses_timed_friendly_armor_buff`. They cover registry policy, manual validation, nearest autocast acquisition, already-buffed exclusion, refresh, expiration, dead-target rejection, and restoration of unmodified base armor.