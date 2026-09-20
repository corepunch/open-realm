# Arthas Hero Abilities

## Contract

Arthas does not have a separate hard-coded spell set. Campaign Paladin Arthas uses the ordinary Human hero abilities `AHhb`
(Holy Light), `AHds` (Divine Shield), `AHad` (Devotion Aura), and `AHre` (Resurrection); Death Knight Arthas uses `AUdc`
(Death Coil), `AUdp` (Death Pact), `AUau` (Unholy Aura), and `AUan` (Animate Dead); Warcraft III 2.0.3 data may use `AUa2` for
the ability-preserving Animate Dead variant. Unit/hero object data chooses which abilities a particular campaign Arthas unit owns,
while the shared ability procedures implement their behavior.

Ability values remain authored-data driven. `S_SpellData`, `S_SpellNumber`, `S_SpellDuration`, `G_AbilityLevel`, and ability
art lookup provide amounts, ranges, durations, buffs, and effects rather than Paladin/Death-Knight constants in gameplay code.

## Implemented Behavior

| Code | Runtime contract |
|---|---|
| `AHhb` | Unit-target Holy Light rejects self and full-health friendly targets. Friendly living targets are healed by authored DataA; enemy Undead targets take half that amount as spell damage. Validation occurs before mana/cooldown commitment. |
| `AHds` | No-target Divine Shield sets invulnerability for authored HeroDur/Dur, applies the authored BuffID as a timed status, and restores the pre-cast invulnerability boolean when its thinker expires. |
| `AHad` | Passive Devotion Aura. The shared Hero-aura cache resolves `AHad`-derived aliases and reads that alias's authored Area/DataA/DataB/Targets Allowed. Flat mode adds DataA armor; authored DataB / `Had2` (`Percent Bonus`) scales DataA only by the recipient's original `UnitBalance.def` Defense Base, not agility/upgrades/current armor. `G_UnitArmorValue()` consumes the strongest valid temporary bonus. Recipients periodically reconcile the winning aura's authored BuffID and target art, and the HUD renders that BuffID virtually without mutating gameplay status slots. It is not a clickable spell. |
| `AHre` | No-target Resurrection rejects an empty cast, revives nearby ordinary friendly non-Hero/non-building raisable corpses with `G_ReviveCorpse()`, prefers higher-level corpses before lower-level corpses and then the nearest corpse for equal levels when the authored count is exceeded, preserves original ownership, honors authored DataB / `Hre2` (`Raised Units Are Invulnerable`) without clearing unrelated invulnerability when disabled, and spawns authored caster and target art. |
| `AUdc` | Unit-target Death Coil rejects self and full-health friendly Undead targets. It launches authored MissileArt at AbilityFunc `Missilespeed` (1000 fallback); heal/damage is applied on impact: full DataA healing for friendly Undead, half DataA damage for enemy non-Undead. The missile records the target edict's `spawn_time`, so impact rejects a removed/reused target slot instead of affecting the new incarnation. Enemy impact SpecialArt is emitted only when the shared spell-damage path actually applies damage. |
| `AUdp` | Unit-target Death Pact requires a living friendly Undead non-Hero target and applies authored DataA-DataE. Normal DataA/DataB mode converts the victim's current life into caster mana/life. DataC/DataD select Warcraft/Warsmash fixed-value mode: the authored mana/life values are deducted from the caster and their sum is removed directly from the victim's life; nonzero DataC also requires the target to currently have mana. When DataE is zero the victim is killed as a sacrifice rather than by spell damage, so authored `invu` targets can still be consumed; that sacrifice is marked non-raisable/no-decay and disappears after its death animation instead of feeding corpse abilities. A nonzero DataE leaves the target alive unless fixed-value life loss itself kills it. |
| `AUau` | Passive Unholy Aura resolves `AUau`-derived aliases and uses that alias's authored Area/DataA/DataB/DataC/Targets Allowed through the shared Hero-aura cache. Movement speed and HP regeneration consume the strongest valid in-range aura contribution. When authored DataC / `Uau3` (`Percent Bonus`) is enabled, DataB is interpreted as a fraction of each recipient's maximum life per second; otherwise it remains flat HP/sec. Recipients reconcile the winning BuffID and target art, and the HUD renders that BuffID virtually without fabricating gameplay status state. |
| `AUan` / `AUa2` | No-target Animate Dead rejects an empty cast, accepts nearby ordinary non-Hero/non-building raisable corpses regardless of allegiance, prefers higher-level corpses before lower-level corpses when the authored count is exceeded, retires corpse death/decay order state, restores full life without reactivating food, changes ownership to the caster, preserves the revived edict's existing ability ownership, honors authored DataB / `Hre2` (`Raised Units Are Invulnerable`), marks the raised unit as a temporary summon whose later death cannot be raised again, applies non-dispellable lifecycle `BTLF`, excludes the unit from Dispel/Purge summoned-unit damage, relinks the unit, and emits authored caster and target art. `AUa2`, introduced by Warcraft III 2.0.3 for the ability-preserving Animate Dead variant, is registered to this same implementation when present in loaded ability data. |

The Hero-aura cache refreshes combat aura values on the existing `AURA_UPDATE_MS` cadence. It resolves the actual learned alias for
Devotion, Brilliance, Unholy, Vampiric, Trueshot, and Thorns Aura, then applies that alias's authored Area and Targets Allowed before
selecting the strongest contribution. Devotion and Unholy Aura recipient presentation use the same cadence: the winning BuffID is cached for HUD-only status presentation and each
persistent target-art overlay is replaced or removed as aura membership changes. No fake `abilstatus[]` entry is created. Unholy DataC additionally selects whether DataB is flat HP/sec or a fraction of each recipient's maximum life per second.

## Data Flow

```text
Hero/unit ability list
        -> S_AbilityItem / registered procedure
        -> shared spell validation (ownership, mana, cooldown, range, targs)
        -> ability-specific validation
        -> authored AbilityData / AbilityFunc values
        -> ability-owned runtime effect
        -> shared combat / aura / corpse / projectile consumers
```

Death Coil deliberately follows the existing Storm Bolt projectile architecture: a `MOVETYPE_FLYMISSILE` owns the target and caster,
then its `umove_t.endfunc` applies the spell effect on arrival. Damage/healing is not committed when the cast is issued.

Resurrection and Animate Dead both reuse corpse edicts rather than spawning type-only replacements. `G_UnitIsRaisableCorpse()` is the
shared eligibility gate for corpse-consuming abilities; Death Pact and temporary Animated Dead units deliberately opt out of that lifecycle.
`G_ReviveCorpse()` owns ordinary Resurrection's complete corpse-to-live transition. Animate Dead uses the equivalent death-state cleanup
but deliberately does not call `G_ActivateUnitFood()`, because the raised temporary unit is controlled by timed life rather than ordinary
production food accounting. `BTLF` is treated as lifecycle ownership and is not removed by Dispel/Purge/Devour Magic. Animated Dead is
also identified by its `summon_ability` procedure and excluded from the summoned-unit damage component of those dispels; ordinary magical
statuses on it may still be dispelled normally.

## Known Boundaries

- Resurrection prefers higher-level eligible corpses and then the nearest corpse to the caster for equal levels. Animate Dead prefers
  higher-level corpses but still retains stable entity-enumeration order among equal levels; classic behavior documents that final tie as
  random, which remains intentionally deferred to avoid introducing a new RNG-order contract here.
- Resurrection/Animate Dead normal-damage invulnerability is now data-driven through authored DataB / `Hre2` rather than a hard-coded
  classic-vs-modern switch. Modern `AUa2` is recognized and uses the current in-place ability-preserving path. Disease Cloud,
  `Uan3`/inherit-upgrades behavior, and legacy `AUan` ability-stripping differences remain separate work. The current in-place revival
  preserves the corpse edict's existing ability ownership, but that is not a claim that every retail era exposes the same animated-unit abilities.
- Divine Shield still stores/restores the previous `invulnerable` boolean. This preserves pre-existing invulnerability but cannot
  safely represent a second invulnerability source that begins while Divine Shield is active. A general modifier-source contract is
  needed before replacing that state model.
- Holy Light/Death Coil use the current shared spell-damage path; richer Warcraft damage type/defense/immunity distinctions and
  localized retail command-error keys remain separate work.
- Death Pact now honors DataC/DataD fixed-value conversion and DataE leave-target-alive semantics in addition to the stock DataA/DataB
  current-life conversion. Richer localized target/resource command errors remain part of the shared spell-error work rather than Death Pact itself.
- Devotion and Unholy Aura now reconcile authored BuffID/target-art presentation for recipients without occupying gameplay status
  slots. Brilliance, Vampiric, Trueshot, and Thorns Aura still rely on their existing mechanical consumers and do not yet share this
  presentation layer.

## Verification

Synthetic production-path coverage is in `games/warcraft-3/game/tests/t_spell.c`:

- `wc3_spell.hero_passives_use_authored_data_and_runtime_consumers` verifies authored Devotion Area/DataA affects armor and expires
  from the shared aura cache when the target leaves range.
- `wc3_spell.hero_aura_aliases_honor_authored_target_masks` verifies a custom Devotion alias uses its own authored Area/DataA/Targets
  Allowed instead of silently falling back to the base `AHad` row.
- `wc3_spell.devotion_aura_percent_bonus_uses_authored_base_defense` verifies authored `Had2` percentage mode uses the recipient's raw
  `UnitBalance.def` Defense Base rather than runtime/real armor.
- `wc3_spell.devotion_aura_recipient_presents_authored_buff_and_target_art` verifies the HUD-facing recipient BuffID and target-art
  presentation follow aura membership and are removed after the target leaves range.
- `wc3_spell.unholy_aura_percent_regen_and_recipient_presentation` verifies authored `Uau3` max-life-percent regeneration plus the
  Unholy recipient BuffID/target-art lifecycle.
- `wc3_spell.holy_light_rejects_full_health_friendly_target` verifies a wasted heal does not commit the spell.
- `wc3_spell.death_coil_uses_projectile_and_rejects_self_or_full_health_ally` verifies delayed impact, enemy damage, allied-Undead
  healing, self/full-health rejection, and target-incarnation cancellation.
- `wc3_spell.divine_shield_applies_authored_buff_for_its_duration` verifies invulnerability and the authored timed buff share the
  configured duration.
- `wc3_spell.animate_dead_prefers_higher_level_corpse_and_restores_temporary_state` verifies empty-cast rejection, higher-level corpse
  preference, death-state cleanup, temporary ownership, authored raised-unit invulnerability, non-raisable lifecycle, `BTLF`, and no food reactivation.
- `wc3_spell.resurrection_prefers_higher_level_friendly_corpse` verifies higher-level-then-nearest corpse preference, original ownership,
  and authored raised-unit invulnerability while preserving ordinary revival.
- `wc3_spell.death_pact_validates_undead_nonhero_and_full_resources` verifies target race/lifecycle, useful-resource validation,
  invulnerable-target sacrifice, and non-raisable/no-decay corpse state.
- `wc3_spell.death_pact_datae_can_leave_target_alive` verifies the authored DataE variant performs conversion without sacrificing the
  target.
- `wc3_spell.death_pact_datac_datad_fixed_value_conversion` verifies DataC/DataD fixed-value resource loss, matching target life loss,
  and the DataC unit-with-mana target requirement.
- `wc3_spell.animate_dead_modern_aua2_registration` verifies modern `AUa2` resolves to the Animate Dead procedure.
- `wc3_spell.animate_dead_prefers_higher_level_corpse_and_restores_temporary_state` also verifies in-place animation preserves existing
  unit ability ownership while the temporary lifecycle is applied.
- `wc3_spell.dispel_preserves_timed_life_status` verifies Dispel cannot remove `BTLF` and make a temporary unit permanent.
- `wc3_spell.dispel_does_not_damage_animated_dead` and
  `wc3_spell.purge_does_not_deal_summon_damage_to_animated_dead` verify Animated Dead remains exempt from dispel-style summoned-unit
  damage while ordinary Purge status behavior remains available.

Existing `wc3_ability_lifecycle.resurrection_retires_death_state_and_rejects_empty_cast` covers ordinary corpse resurrection cleanup.

## See Also

- [Hero Ability Progression](hero-abilities.md)
- [Adding Warcraft III Abilities](ability-implementation-plan.md)
- [Ability Coverage](architecture/ability-coverage.md)
- [Ability And Item Effects](ability-and-item-effects.md)
