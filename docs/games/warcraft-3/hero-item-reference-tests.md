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

The shop item rawcodes and their `abilList`s come from the user's installed
ROC + TFT retail archive audit. Non-shop rows with empty rawcode fields remain
unresolved until their exact item key and ability list are matched in both
archives; do not infer an identity from a similar web display name. The local
archive check found naming differences (including several distinct
Replenishment Potion entries), so names in the reference matrix must not be
treated as item IDs. Web names and numeric values remain behavior references,
not substitutes for retail object data.

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
The latest full suite checkpoint passed these listed tests in both classic and TFT modes:

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
4. `oli2_AIll_resolves_attack_bonus_handler`: Confirms the `oli2 → AIll
   (code=AIsb)` binding and registers its authored +5 attack bonus. On-hit
   behavior is separately exercised by the Lightning proc cases below.
5. `ocor_nonstock_data_keeps_status_without_armor_consumer`: Injects a
   non-stock duration/data row and checks the status lifecycle while confirming
   no armor change is currently implemented.
6. `oli2_AIll_applies_authored_attack_bonus_and_purge_proc`,
   `oli2_AIll_damages_enemy_summon_with_nested_ai_pg_value`, and
   `oli2_AIll_respects_zero_proc_chance`: Exercise the held-item hit path,
   35% proc configuration, purge/slow, summoned-unit damage, and the zero
   chance boundary. `AIll` is the Orb of Lightning alias; its parent code
   `AIsb` is shared and does not make every `AIsb` alias a Lightning item.
7. `regeneration_items_follow_target_damage_and_scheduler`: Uses non-stock
   `AIsl`/`AIpl` data through item-use dispatch, ordinary attack damage, and
   `G_RunEntity`. It verifies AIsl's area recipients tick independently,
   attack or spell damage cancels only the damaged recipient, AIpl affects only
   its user, and attack damage cancels AIpl recovery. The test runs under both classic and TFT.
8. `orb_of_slow_grants_air_attack_and_authored_slow_proc`: Equips retail
   `oslo`/`AIsb`, checks a melee unit gains and loses its air-target missile
   attack with the item, then drives the hit proc through nested `AIos` and
   checks authored movement/attack slow modifiers and Hero duration.
9. `orb_of_fire_splashes_full_attack_damage_to_nearby_enemies`: Checks the
   authored area radius, full damage on nearby enemies from melee and missile
   hits, and exclusion of allies and out-of-radius enemies.
10. `bladebane_armor_grants_nearby_units_one_armor`: Equips retail `blba`
    (`AId7,AIad`), checks the one-point Devotion Aura armor bonus on an ally,
    then checks it disappears after the recipient leaves aura range.
11. `gloves_of_spell_mastery_control_summoned_units_from_inventory`: Uses
    retail `gvsm` from inventory, selects an enemy summon, and checks that it
    transfers to the Hero.

`wc3_ability_lifecycle.staff_of_sanctuary_teleports_stuns_and_heals_until_full`
also exercises the installed `ANsa` contract with non-stock `DataA=7`: the
friendly unit relocates to a completed Town Hall, gains the `BNsa` stun state,
heals on explicit scheduler ticks, and loses Sanctuary as soon as it reaches
full health. This focused regression passes in classic and TFT.

`games/warcraft-3/game/tests/t_retail_shop_contract.c` independently registers
**45** shop listings with the verified gold/lumber, `stockMax`, `stockRegen`,
`stockStart`, `Makeitems` or `Sellitems`, and purchase transactions. The **62
shop-specific item effects** are indexed by
`t_retail_shop_effect_cases.inc` and linked to the separate gameplay matrix.
The 45 purchase tests should not be misrepresented as 62 effect tests.

The 266-row matrix above remains an inventory of positive/negative acceptance
scenarios, not executable C coverage. The tests listed here cover only the
behaviors named in their descriptions. `AIcb` armor reduction, full
retail-value regeneration totals and the other matrix-only scenarios remain open until verified
from archive data and exercised through production gameplay paths.

## Testing policy

Run the focused cases in both classic and TFT modes with:

```sh
make test-wc3-engine WC3_PATTERN='retail_shop_contract.*'
make test-wc3-engine WC3_PATTERN='retail_item_effect.*'
```
