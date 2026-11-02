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

## Executable RED/GREEN C fixtures in this patch

`games/warcraft-3/game/tests/t_retail_item_effect_regression.c` is a new
standalone `shared/test.h` suite using game module production entry points:

1. `ocor_reduces_effective_armor_and_expires`: Held `ocor`/`AIcb` adds the
   verified +5 attack; actual landed hit applies `BIcb`; **effective combat
   armor** (`G_UnitArmorValue`) must fall by 5, remain reduced just before 5 s,
   then recover on expiry. The current OpenRealm buff-only handling will make
   the armor assertion **RED** (known missing consumer).
2. `ocor_nonstock_armor_reduction_uses_DataB_not_constant`: Deliberately
   change `AIcb.DataB` to 7 and duration to 3 s to prove the implementation
   reads the injected data rather than hard-coding the stock 5.
3. `oven_combines_attack_bonus_and_single_poison_pulse`: The verified
   `oven → AIpb,Apo2` mapping grants +5 attack; the held item applies both
   poison tokens, but only the first deals **9 damage/s**, not two ticks.
   Damage must stop after the authored six seconds and dropping the item must
   remove the passive attack bonus.
4. `pams_blocks_hostile_magic_until_15_seconds`: The audited stock
   `pams → AIxs (code=Aami)` 15-second Bams immunity stops real spell damage
   during the effect; damaging spells work again once it expires. A separate
   existing 17-second non-stock alias test is in `t_anti_magic_shell.c`.
5. `oli2_AIll_has_retail_orb_slow_purge_dispatch`: Retains the exact
   `oli2 → AIll` mapping with **`code=AIsb`**, not `AIlb`, and requires a
   non-passive ability dispatch. This is currently expected to fail until
   AIsb handling is implemented.
6. `oli2_nonstock_guaranteed_proc_dispels_enemy_positive_buff`: Forces the
   proc by changing the authored `DataB` 35 to **100** in the test fixture,
   hits a unit with a genuinely magical positive `Binf` status through
   `S_ResolveAttackHit`, then requires the buff to disappear. The stock 35%
   chance is documented in the audit and Blizzard page; a deterministic 100%
   check avoids probabilistic failure while proving the hook exists.

`games/warcraft-3/game/tests/t_retail_shop_contract.c` independently registers
**45** shop listings with the verified gold/lumber, `stockMax`, `stockRegen`,
`stockStart`, `Makeitems` or `Sellitems`, and purchase transactions. The **62
shop-specific item effects** are indexed by
`t_retail_shop_effect_cases.inc` and linked to the separate gameplay matrix.
The 45 purchase tests should not be misrepresented as 62 effect tests.

### Follow-on implementation order

1. Fix and validate **`AIcb` armor consumer** and `AIll` on-hit effects with
   the currently red fixtures; add a non-proc control and the 150-damage
   summoned-unit variant once `AIsb` semantics are established.
2. Wire more of the **62 shop effect rows** to production orders (`G_UseItem`,
   targeted item UI selection callbacks, attack hits, `G_RunEntities` and
   inventory add/remove) using the shared case table. A test that only calls
   `S_AbilityItem` or only finds the BuffID is NOT complete.
3. Extract unresolved item ID→`abilList` bindings for **non-shop** items from
   both archives; then implement the **266 reference-scenario** cases in C.
   Keep changed synthetic fields for every family, and the retail-shaped row.
4. Complete `ItemFunc.txt` prerequisite fields and races in purchase fixtures.
   No presumed tech requirements should be suppressed to make a test pass.
5. Add ROC column variant fixtures alongside TFT. Document any true source
   disagreements and representative map overrides before adding assertions.

## Testing policy

These C assertions are expected to be RED where the engine is missing the
behavior. **No compilation or gameplay tests were run locally.** Follow
`AGENTS.md` and `CONTRIBUTING.md`. Suggested patterns for your own runs:

```sh
make test-wc3-engine WC3_PATTERN='retail_shop_contract.*'
make test-wc3-engine WC3_PATTERN='retail_item_effect.*'
```

### Suggested commit message

`test(wc3): add retail-first item shop and artifact behavior regressions`
