# Regeneration Auras And Fountains

## Contract

OpenRealm implements the Warcraft III regeneration-aura base codes `Aoar`, `Aabr`, and
`Aarm` as passive, data-driven auras. Stock/custom aliases continue to resolve through
`AbilityData.code`, so aliases such as Fountain of Health `ACnr -> Aoar` and Fountain
of Mana `ANre -> Aarm` use their own authored level row while sharing the base runtime
handler.

The implementation deliberately follows the existing OpenRealm passive-aura model:
recipients do not retain a persistent aura buff. A regeneration consumer queries live
nearby sources when health/mana regeneration is evaluated. This keeps source removal,
`UnitAddAbility`/`UnitRemoveAbility`, save/load, and range changes from needing a second
piece of recipient state.

## Data Flow

```text
source unit ability alias
  -> AbilityData.code (`Aoar`, `Aabr`, or `Aarm`)
  -> alias level Area / targs / DataA / DataB
  -> live source scan in s_hero_passives.c
  -> strongest matching contribution for that base-code family
  -> G_RunEntity regeneration in g_phys.c
```

`DataA` is the authored regeneration amount. `DataB != 0` selects maximum-resource
percentage mode, matching the Warsmash `MAXHPGEN` / `MAXMPGEN` template contract:

- `Aoar` / `Aabr`: `DataA * target.max_health` when `DataB` is true, otherwise flat HP/sec.
- `Aarm`: `DataA * target.max_mana` when `DataB` is true, otherwise flat mana/sec.

Two sources of the same base-code family do not add; the strongest eligible value wins.
`Aoar` and `Aabr` are distinct health-regeneration families and may both contribute.

## Ability Ownership

Aura lookup covers the three ability ownership paths used by OpenRealm:

- static `UnitAbilities.abilList` entries;
- runtime `UnitAddAbility` entries in `edictAbilities_s.added`;
- learned Hero entries in `heroabilities`.

The alias, rather than the normalized base code, remains authoritative for `Area`,
`targs`, `DataA`, and `DataB`. This is required for custom-object-data aliases.

## Target Filtering

The local aura predicate consumes the authored `targs` list and supports the target
categories needed by the stock regeneration family and nearby custom variants:

- `air`, `ground`, `structure`;
- `friend`/`allies`, `enemy`/`enemies`, `neutral`, `self`, `notself`;
- `organic`, `mechanical`;
- `hero`, `nonhero`;
- `vulnerable`, `invulnerable`;
- living targets only.

Aura participation also requires both the source and recipient to be active rather than
hidden or gameplay-invisible. `RF_HIDDEN` covers explicit hidden state and temporary
invisibility such as Invisibility/Wind Walk; Permanent Invisibility uses its separate
runtime state. Fog-of-war visibility and true-sight detection do not alter this rule: an
invisible unit does not provide or receive an aura merely because a viewer can detect it.
Eligibility changes in cached aura families are observed on the normal two-second aura
refresh cadence.
The same `S_AuraUnitActive` predicate is also used by Endurance Aura's direct movement-
and attack-speed scans, keeping its source/recipient visibility rule aligned with the cached
aura families.

OpenRealm currently represents these classifications with its existing unit target type,
Hero predicate, alliance state, and invulnerability flag. This is not a general replacement
for the broader WC3 target-mask system; keep future target-category work centralized rather
than adding fountain rawcode checks.

## Natural Regeneration

`UnitBalance.regenType` (`uhrt`) gates the unit's *natural* HP regeneration. It must not
gate an external healing aura. `G_RunEntity` therefore computes regeneration aura HP
separately and adds natural/Strength/Unholy regeneration only when `G_UnitRegeneratesHP`
allows it.

This matters for units whose authored natural regeneration is `none`, `night`, or (until
blight exists) `blight`: an eligible Fountain of Health can still restore them.

Mana regeneration remains the existing per-second path plus the new `Aarm` contribution.
All values are multiplied by `FRAMETIME / 1000.0f`, so the authored rates remain per-second
rather than per-frame.

## Presentation

Regeneration aura presentation follows the same independent-effect-edict path as other
Warcraft ability/buff art.  The strongest live source in each regeneration family also
retains its authored `buffID`.  If an alias leaves `BuffID` empty, its base regeneration
ability supplies the authored buff.  While a recipient remains eligible, OpenRealm
resolves that buff through `AbilityBuffData.TargetArt` and maintains one persistent target effect
for that family.  The effect follows the target, loops its `stand` sequence, and is
destroyed through the ordinary effect lifecycle when the recipient leaves range, the
source disappears, targeting changes, or another strongest source selects different art.
Aura values and presentation reconciliation refresh once every two seconds per recipient,
matching the retail aura refresh cadence and keeping aura scans off the per-frame hot path.

This intentionally mirrors Warsmash's display-buff behavior: aura `TargetArt` is present
while the aura affects the unit, not spawned again on every HP/mana regeneration tick.
`Aoar`, `Aabr`, and `Aarm` own independent presentation slots, while duplicate sources
of one family still collapse to the strongest source selected by gameplay.

## Known Gaps

This change still does not add:

- recipient buff icons or general persistent buff handles;
- restoration looping sound ownership/lifetime;
- a generic engine-wide non-stacking stat-modifier system;
- full Warcraft target-classification parity beyond the categories above.

Those presentation/lifecycle features should be added only after their retail/data contract
is verified; they are not required for the regeneration calculation itself.

## Verification

Tests in `games/warcraft-3/game/tests/t_spell.c` cover alias-to-base resolution,
percentage-of-maximum HP/mana regeneration, flat health regeneration, strongest-source
selection, mechanical exclusion, range, passive registrations, and shared hidden/invisible
source and recipient rejection. `t_combat.c` verifies that an external health-regeneration
aura still heals a unit whose natural `uhrt` mode is `none`, and that Endurance Aura's
attack-speed consumer follows the same hidden/invisible eligibility rule.
