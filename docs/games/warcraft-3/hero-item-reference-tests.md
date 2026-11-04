# Warcraft III Hero Items — Five Blizzard Category References

## Scope and authority

This extends the shop [retail contract](retail-shop-item-tests.md) and the neutral
creep [source-coverage tests](neutral-creep-ability-tests.md), with item behavior
across five **additional** Blizzard reference pages:

- [Charged items](https://classic.battle.net/war3/basics/heroitemscharged.shtml)
- [Power-up items](https://classic.battle.net/war3/basics/heroitemspowerups.shtml)
- [Consumed items](https://classic.battle.net/war3/basics/heroitemsconsumed.shtml)
- [Permanent items](https://classic.battle.net/war3/basics/heroitemspermanent.shtml)
- [Artifact items](https://classic.battle.net/war3/basics/heroitemsartifact.shtml)

The source-link and acceptance matrices are:

- [`hero-item-reference-contracts.csv`](hero-item-reference-contracts.csv) — **133 entries**, each with its page, behavioral promise, negative/boundary requirement, source parameters, and *only verified* rawcode/`abilList` information.
- [`hero-item-reference-test-matrix.csv`](hero-item-reference-test-matrix.csv) — **266 distinct positive/negative acceptance scenarios**, intended for agents to convert to C headless gameplay regressions. **These matrix rows are not C tests and have not run.**

The 30 shop item rawcodes and their `abilList`s come from the user's **installed
ROC + TFT retail archive audit**. Unverified non-shop entries deliberately
leave both rawcode fields empty: **do not infer a rawcode from the item name,
a website label, a demo fixture, or a `code=` parent identifier.** Extract
`Units/ItemData.slk`, `Units/ItemFunc.txt`, and the relevant `AbilityData.slk`
rows to bind those cases before treating them as executable regressions.

Object data is authoritative for *item and ability identity, authored values,
max charges, target mask, level, cost, cooldown, duration, buff IDs, and slot
rules*. The Blizzard category pages explain behavioral intent and useful
secondary evidence. Where a page and locally installed retail data conflict,
**test the installed retail values**, record the discrepancy, and avoid
copying the page's numeric constant blindly. The web reference values in the
CSV are not object-data verification.

## Distinct behavior branches the extra pages require

| Family | Positive oracle | Critical negative/lifecycle oracle |
|---|---|---|
| Charged items | Cast/target real unit, area or point; obey per-item charges | Wrong allegiance, no charges, cooldown, interrupted cast: never remove an item charge prematurely |
| Power-ups | Pickup immediately changes hero/group state without an inventory slot | Full inventory still permits a power-up; enemy/outside radius targets unchanged; only one pickup reward |
| Consumables | Direct recovery, HoT/DoT, summon, temporary status or construction | Damage/cast interrupts healing where stated; cap resources, expire summons/status, validate placement |
| Worn permanent items | Pickup changes movement, attack, armor, regeneration, hero attributes or item aura | Drop, sale, hero death, save/load, recompute, conflicting auras, no unintended stacking |
| Artifacts | Same production path as carried/consumed items at stronger authored magnitudes | Orb priority, non-stacking effects with additive damage, lifesteal based on actual dealt damage |

### Specific newly captured contracts

- **Wand of Illusion** creates an allied copy which deals *zero* attack damage,
  takes *double* attack damage, expires after *60 seconds*, and cannot target
  an enemy. Both the zero damage and 2× damage intake need actual combat checks.
- **Rune of the Watcher** creates a detection ward with **no natural timer**;
  ordinary Sentry Wards have a limited lifespan. This requires two different
  expiration fixtures.
- **Rune of Dispel Magic** is an immediate pickup with a **250** summons-only
  damage value; **Wand of Neutralization** dispels buffs without damaging
  summons. They must not reuse an unqualified “dispel = summon damage” oracle.
- **Clarity / Healing Salve / Rejuvenation potions / Scroll of Regeneration**
  need real simulation ticks *and* interruption from ordinary weapon damage and
  damaging spells; each affected unit stops its own periodic recovery.
- **Ankh of Reincarnation** must revive the actual hero **7 seconds** after
  death with **500 HP** (when consistent with the inspected archive data),
  consume the item once, and avoid duplicating the hero/corpse.
- **Boots of Speed** give **one +60 movement-speed benefit even if two pairs
  are held**. Add/drop the second pair independently, perform a real movement
  order, and compare distance per simulation tick; don't compare only a tooltip.
  `AIms` is currently a TODO in `s_skills.c` — preserve a failing behavioral
  check instead of replacing it with an item-list validation.
- **Artifact Orb of Frost, Mask of Death and other Orbs:** extra damage from
  multiple orbs stacks but only one on-hit orb effect applies; inventory order
  and manually activated vs autocast attack modifiers choose the effect.
  Mask of Death lifesteal uses **50% of actual attack damage dealt** rather
  than arbitrary raw damage or spell damage.
- **Aura items** do not double-stack with their matching Hero auras; source
  notes allow Warsong Battle Drums to stack with Kodo War Drums (different
  effect group). Check both cases with two nearby allies and a distant control.
- **Runed Bracers** reduce magical incoming damage by **33%** while leaving
  otherwise identical physical attack damage unchanged.

## Current executable item regressions

`games/warcraft-3/game/tests/t_retail_item_effect_regression.c` uses the game
module's production inventory, attack, spell-damage and entity-scheduler paths.
The latest full suite passes all six tests in both classic and TFT modes:

1. `ocor_applies_timed_corruption_status`: Equips `ocor`, lands an attack,
   checks the `BIcb` state, verifies expiry through `G_RunEntity`, and checks
   that the item attack bonus is removed on drop. `BIcb` remains state-only;
   this test does not claim an armor reduction consumer exists.
2. `oven_combines_attack_bonus_and_single_poison_pulse`: The held `AIpb,Apo2`
   item grants +5 attack and applies `BIpb,BIpd`; the scheduler applies one
   poison pulse per second for six seconds, then stops, and item drop removes
   the attack bonus.
3. `pams_blocks_hostile_magic_until_15_seconds`: The `pams → AIxs (code=Aami)`
   immunity blocks real spell damage for its authored 15 seconds, then expires.
   A separate non-stock duration case is in `t_anti_magic_shell.c`.
4. `oli2_AIll_retail_alias_has_no_registered_handler`: Confirms the `oli2 →
   AIll (code=AIsb)` binding is retained and explicitly records that no
   non-passive handler is registered.
5. `ocor_nonstock_data_keeps_status_without_armor_consumer`: Injects a
   non-stock duration/data row and checks the status lifecycle while confirming
   no armor change is currently implemented.
6. `oli2_AIll_does_not_purge_until_handler_exists`: Confirms the current
   unimplemented `AIsb` behavior does not silently remove a positive buff.

`games/warcraft-3/game/tests/t_retail_shop_contract.c` independently registers
**45** shop listings with the verified gold/lumber, `stockMax`, `stockRegen`,
`stockStart`, `Makeitems` or `Sellitems`, and purchase transactions. The **62
shop-specific item effects** are indexed by
`t_retail_shop_effect_cases.inc` and linked to the separate gameplay matrix.
The 45 purchase tests should not be misrepresented as 62 effect tests.

The 266-row matrix above remains an inventory of positive/negative acceptance
scenarios, not executable C coverage. The tests listed here cover only the
behaviors named in their descriptions. `AIcb` armor reduction, `AIsb` Orb of
Lightning behavior, item recovery interruption, and the other matrix-only
scenarios remain open until verified from archive data and exercised through
production gameplay paths.

## Testing policy

Run the focused cases in both classic and TFT modes with:

```sh
make test-wc3-engine WC3_PATTERN='retail_shop_contract.*'
make test-wc3-engine WC3_PATTERN='retail_item_effect.*'
```
