# Creep combat effects

## Authored contracts

Checked with `ability_audit -raw` against ROC and TFT. `AbilityMetaData.slk`
and `UI/WorldEditStrings.txt` resolve the radius/damage field labels.

| Ability | Fields and behavior |
| --- | --- |
| Disease Cloud `Aapl`, `Aap1`–`Aap4` | DataA infection lifetime (120 seconds), DataB DPS (1), Area acquisition radius. `Aap4` has zero damage. ROC omits BuffID; TFT uses `Bapl` (and cloud presentation `Bplg`). Infection remains after leaving the source. |
| Pulverize `Awar`, `ACpv` | DataA percent chance, DataB damage, DataC full-damage radius, DataD half-damage radius. Area is zero. ROC/TFT `Awar` uses 25/60/250/350. |
| Incinerate `ANic`, `ANia` | TFT only. DataA stacking bonus; DataB/C full explosion damage/radius; DataD/E outer damage/radius; DataF explosion delay. Area is zero. `BNic` lasts Dur. Any lethal source while marked triggers the explosion once. |
| Web `Aweb`, `ACwb` | Air-only enemy spell. DataA landing/rising time, DataB starting height, DataC bound melee range; Dur/HeroDur lifetime. TFT BuffID is `Bwea,Bweb`; ROC omits it and uses the known WebAir token. Shares Ensnare's bind/height mechanism and restores flight after the last bind. |
| Monsoon `ANmo`, `ACmo` | TFT only. Fixed selected point, Area radius, DataA damage, DataB interval, DataC building damage factor, Dur channel lifetime. Moving/cancelling the caster ends the channel. |

## Ownership and persistence

Disease Cloud and Incinerate live in their ability modules. Generic status
tick/death messages dispatch through the applying rawcode. Source identity,
rank and tick deadline travel with the status; source pointers use the save
schema's entity fixups and incarnation checks reject reused slots. The
Incinerate mark is installed before attack damage so ordinary lethal hits and
other damage sources reach the same death callback. Its delayed explosion
uses an ability-owned, save-rostered thinker at the death position.

Web delegates its bind lifecycle to Ensnare; the shared height updater runs
once per unit. Both buff families participate in last-bind restoration.
Monsoon owns a fixed-point channel thinker rather than borrowing Bladestorm's
caster-following thinker.

The Web procedure handles `A_AUTOCAST_ON`/`A_AUTOCAST_SET` through
`CAbilityModalSpell`; `AB_AUTOCAST` and acquisition alone do not enable its
command-card toggle. Save tests must install the flyer's `UnitData` row, since
load rebinds immutable tables and deliberately discards stack-backed test rows.

Source removal/incarnation changes invalidate pending damage; a living or dead
source in the same edict incarnation remains valid for an existing infection
or mark. New status pointers are `F_EDICT` fields, and the new thinkers are
appended to `save_cfunctions[]`. The enlarged edict record rejects older saves
through the existing save-header size check.

This does not implement Plague Toss (`Apts`): it is a separate attack-triggered
cloud producer, with zero Area/DataA/DataB and TFT UnitID `uplg`, not an aura row.
Incinerate Arrow's separate mana/autocast policy and complete target-mask
semantics remain outside these combat-lifecycle fixes.

## Verification

`t_creep_regression.c` covers the six PR #479 failures through attack/cast and
scheduler entry points, with stock zero Area cells. Further cases cover outer
rings, expiry/dispel, source invalidation, off-origin channels and save/load.

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.creep_regression*'
make test-wc3-engine WC3_PATTERN='wc3_save.creep*'
make test
```

The original tests supplied damage in Disease Cloud's duration cell and
invented nonzero Area cells for Pulverize and Incinerate. These fixtures must
retain the authored column meanings even when numerical values are customized.

See also [creep aliases](creep-ability-aliases.md), [Ensnare](ensnare.md),
[save/load](save-load.md), and [attack damage](attack-damage.md).
