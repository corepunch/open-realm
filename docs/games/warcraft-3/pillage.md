# Orc Pillage (`Asal`) — OpenRealm implementation

## Behaviour

- Research `Ropg` at the Orc Great Hall/Stronghold/Fortress through existing
  research commands. Units whose Upgrades Used list contains `Ropg` and own
  stock `Asal` cannot Pillage before the owning player completes this tech.
  Existing and later units consult player research state when their attacks land;
  disabling/removing the researched tech stops subsequent Pillage rewards.
- Runtime additions and custom aliases with `code=Asal` use the existing owned
  ability resolver. Authored data selects **DataA/Sal1** (total fraction of
  construction costs available from full eligible HP damage) and **DataB/Sal2**
  (HP damage accumulation threshold). Invalid non-positive or nonfinite values
  disable rewards safely.
- Only a resolved **primary attack** hit can Pillage. `T_DamageInternal` resolves
  invulnerability, Mana Shield and Spirit Link, then passes capped HP loss
  before the victim's death callback can free its edict. Plain `T_Damage`,
  spell/secondary splash, thorns and scripted damage do not Pillage.
- A target must be a currently rooted/live structure with a `UnitBalance` cost,
  positive max HP, completed construction and an allowed ownership relationship.
  Uprooted Ancients do not qualify. Empty/zero-cost structures produce nothing.
- Each building gets an optional `pillage_t` component on the first qualifying
  hit; its damage and fractional resource remainders are partitioned by
  **attacking player**. For each whole step of eligible HP damage, grant
  `buildingCost * DataA * completedDamage / targetMaxHealth` for each resource,
  carrying fractional payouts between hits. Gold/lumber use
  `G_CreditResourceIncome`, preserving standard upkeep and resource events.
- Repairs do not award anything, but damage inflicted afterward qualifies.
  No once-per-building cap or frame polling is used.

## Persistence

The component record is defined in `G_COMPONENTS` as stable ID **29** and
participates in generated typed pools and game-save serialization; no persistent
edict pointer was added. Save version **117** invalidates older incompatible
saves as required by `CONTRIBUTING.md`. Recycled/deleted buildings release
components through their usual lifecycle.

## Validation for user-run builds

The source-only tests (`wc3_spell.pillage_*`) cover research gating, direct
custom alias, accumulation and fractional remainders, construction/structure
exclusions and damage-source gating. These tests should be run by the maintainer
on both Classic and TFT game data. Additional retail-oriented manual cases:

1. Research Pillage using the real Orc Town Hall interface, then hit an enemy
   Farm with Peon/Grunt/Raider and verify income/HUD/upkeep.
2. Use two different owners attacking the same structure and verify each owner
   only receives resources from their own hits.
3. Repair between hits, compare low/high upkeep, and test lethal strikes,
   Mana Shield, Spirit Link and Ethereal immunity.
4. Use custom Asal-derived ability values for DataA/DataB; compare unusual
   accumulation steps and resource rounding to retail.
5. Save while the target has fractional damage/reward state; load, attack
   again and ensure neither loss nor duplicate income.

## Limits and parity risks

The implementation uses current HP max for the calculation; retail's precise
cost/HP snapshot under upgrades and temporary max-HP changes requires focused
verification. Nondefault accumulation steps and floating-point rounding may
produce off-by-one differences from retail integer math. Per-step resource
payouts pass through the engine's generic upkeep credit path, whose
rounding may differ from retail Pillage. Custom ability target filters honor
basic owner relationships; exotic target flags remain unverified. These are
**not** validated retail-equivalent results.
