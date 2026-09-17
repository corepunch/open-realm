# Ancestral Spirit

## Contract

`Aast` is the TFT Spirit Walker ability Ancestral Spirit. The TFT class registry maps it to
`CAbilityAncestralSpirit` with parent `AAcs` (`CAbilityClosestTargetSpell`). Casting is implicit:
the ability selects the nearest eligible corpse instead of entering unit-target mode.

An eligible corpse is:

- the original edict for a unit that completed the authoritative death transition (`SVF_DEADMONSTER` plus zero health) with rawcode `otau` (Tauren);
- an ordinary non-Hero unit;
- owned by the caster's player; and
- within the authored cast range.

The `player` target token means same owner, not any passive ally. An allied player's Tauren corpse
is therefore ineligible even when `ALLIANCE_PASSIVE` is set. A successful cast revives the original
edict, preserving its JASS handle, owner, rawcode, and spawn identity. It clears corpse/death state,
restores food accounting, returns the unit to its stand move, relinks it, and restores
`DataA * maxHealth` hit points. The revived unit is no longer a corpse and cannot be selected by a
subsequent cast.

## Authoritative Data

`War3x.mpq:Units/AbilityData.slk` contains the active `Aast` row. `War3.mpq` has no `Aast` row, so the
ability is TFT-only even though some installed ROC-local string tables also contain its text.

| Field | TFT value | Runtime meaning |
| --- | ---: | --- |
| `targs1` | `ground,player,dead` | dead ground unit owned by the caster |
| `Rng1` | `350` | closest-corpse search radius |
| `Cost1` | `250` | mana committed only after a corpse validates |
| `Cool1` | `30` | cooldown committed only after validation |
| `DataA1` | `1` | fraction of maximum health restored |
| `DataB1` | `0.25` | authored but not needed by the confirmed visible contract |

`Units/AbilityStrings/War3x/OrcAbilityStrings.txt` confirms the visible rule: “Raises a fallen
non-Hero Tauren from the dead” and restores `<Aast,DataA1,%>%` of its hit points.

The raw archive row can be inspected without extracting the MPQ:

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3x.mpq' cat 'Units/AbilityData.slk' \
  | awk -F';' 'BEGIN{y=0} {for(i=1;i<=NF;i++) if($i ~ /^Y[0-9]+$/) y=substr($i,2)} y==1 || y==166 {print}'
```

## Runtime Ownership

`CAbilityAncestralSpirit` in `skills/s_orc_abilities.c` owns target eligibility and closest-target
selection. It delegates shared command, mana, cooldown, and spell-event behavior to
`CAbilitySimpleSpell` through the validated-spell procedure contract.

`G_ReviveCorpse` in `m_unit.c` owns the reusable ordinary-corpse state transition. Resurrection uses
the same mechanism at full health. Hero revival remains separate: Hero corpses proceed through the
Altar lifecycle and are never eligible for either ordinary corpse path.

## Verification

The in-engine `wc3_spell.ancestral_spirit_revives_nearest_owned_nonhero_tauren` test drives
`S_CastNoTargetSpell` with a synthetic `Aast` SLK row whose nontrivial `DataA=0.4` proves the health
fraction is data-driven. It covers nearest selection, same-edict and owner preservation, food reactivation,
reuse prevention, and rejection of health-only pseudo-corpses, living, Hero, non-Tauren, allied, and
out-of-range units in both ROC and TFT fixture runs:

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.ancestral_spirit_revives_nearest_owned_nonhero_tauren'
```
