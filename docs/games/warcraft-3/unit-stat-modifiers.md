# Unit Stat Modifiers

This is the implementation contract for additive unit bonuses and damage
prevention. Keep the owning ability responsible for acquisition, expiry,
interruption, and removal. Shared helpers own the arithmetic and the persistent
ledger for a derived stat; they do not replace ability ownership.

## One authoritative value per stat

- Treat SLK/DBC unit rows as immutable base data. Do not write bonuses back into
  those tables or repeatedly add deltas to a value that will later be rebuilt.
- Store persistent additive modifiers in named ledgers. Unit-local bonuses
  belong on the edict; researched `rmnx` mana belongs to player tech and is
  queried through `G_UnitUpgradeEffectBonus`. Runtime values such as
  `armor_value` and `damageBase` are derived caches, not independent sources
  of truth.
- Use the same operation for ordinary add/remove paths. Removal passes the
  inverse delta through that operation, keeping the ledger and live value in
  sync. A bespoke ability transition may use different current-resource rules,
  but must still update the ledger (Avatar expiry clamps current HP instead of
  preserving its fraction).
- Item maximum-health and maximum-mana bonuses preserve the unit's current
  fraction. `rmnx` research moves current mana by the absolute maximum delta.
  Keep each bonus in its authoritative ledger so Hero attribute recomputation
  and save/load do not erase it.

Current examples:

| Stat/effect | Ledger and calculation | Consumer |
| --- | --- | --- |
| Attack damage | `unitAttack_t.permanentDamageBonus` / `temporaryDamageBonus`; primary Hero attribute is rebuilt from `UnitWeapons` | `ai_rolldamage1`, then attack-resolution modifiers; use `G_Apply*AttackDamageBonus` |
| Armor | `permanent_armor_bonus` / `temporary_armor_bonus`; `armor_value` is rebuilt from base armor and Agility | `G_UnitArmorValue`, shared by physical damage and HUD; use `G_Apply*ArmorBonus` |
| Maximum health | `permanent_health_bonus` / `temporary_health_bonus`; use `G_Apply*MaxHealthBonus` | Hero recomputation and health cap |
| Maximum mana | Hero Intelligence, player-owned `rmnx` research via `G_UnitUpgradeEffectBonus`, plus `temporary_mana_bonus`; temporary changes use `G_ApplyTemporaryMaxManaBonus` | Hero recomputation and mana cap |

Add another ledger only when the stat needs a separately owned contribution
that must survive recomputation or persistence. Timed buffs whose value can be
looked up from the active status and authored ability row can remain derived
inputs, as `Bdef` is in `G_UnitArmorValue`. `G_TransformUnitType` snapshots and
reapplies temporary armor, attack, health, and mana modifiers around the new
unit-type bind; add coverage there when introducing another persistent ledger.

## Damage paths are intentionally distinct

`T_Damage(target, source, amount)` applies an already calculated amount. It
handles hard invulnerability, Mana Shield, Spirit Link, damage events, health,
and death. It does not apply weapon-type multipliers or armor.

- Ordinary physical attacks calculate base damage and dice, then call
  `G_AttackDamage` for attack-type/defense-type and armor math. The hit then
  passes through `S_ResolveAttackHit` for attack abilities such as critical
  strike, attack bonuses/reductions, and on-hit behavior before `T_Damage`.
- Spell paths that need magic immunity or Anti-Magic Shell use
  `S_SpellDamage`. Spell damage does not implicitly use the physical armor
  formula; each ability follows its authored contract.
- Direct `T_Damage` callers provide damage in the units and mitigation state
  required by their effect. Do not route a pre-mitigated attack back through
  `G_AttackDamage` or assume a direct hit received spell-immunity checks.
- `invulnerable` is a hard damage gate. Spell immunity is a target/impact rule;
  Mana Shield is a finite damage-to-mana conversion. Keep these as distinct
  mechanics, not generic numeric modifiers.

JASS `UnitDamageTarget` currently accepts but does not model its attack type,
damage type, and weapon type arguments; it calls `T_Damage` with the supplied
amount. Preserve that limitation explicitly until typed JASS damage is
implemented.

## Change and test checklist

For a new modifier, identify its owning procedure, base value, ledger, read
sites, ordering relative to other modifiers, and inverse/expiry path. Add a
production-path regression that covers a non-default authored value, stacking
or competing sources where relevant, removal/expiry, Hero recomputation, and
save/load if new persistent state is added. For damage changes, test the
relevant entry path (weapon, spell, or direct damage) and the nearest bypass
path so mitigation is neither skipped nor applied twice.
