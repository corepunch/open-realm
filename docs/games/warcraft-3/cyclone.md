# Cyclone

## Contract

`Acyc` is TFT `CAbilityCyclone` (parent `AAsm`). `ACcy`, `SCc1`, `Acny`, and item
`AIcy` are `AbilityData` aliases whose `code` is `Acyc`, so they share
`CAbilityCyclone` and read their own rows through `abilityitem_t.code`.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Acyc` | ROC and TFT | Druid of the Talon Cyclone |
| `ACcy` | ROC and TFT | creep Cyclone |
| `SCc1` | ROC and TFT | Cenarius Cyclone |
| `Acny` | TFT | naga Cyclone (`organic` in targs) |
| `AIcy` | ROC and TFT | item Cyclone (`code=Acyc`); Staff of Cyclone (`wcyc`) |

Casting tosses one living unit into the air until `Dur` / `HeroDur` expires. While
cycloned the victim cannot move, attack, or cast, and others cannot attack or
target it with spells. Physical `S_ResolveAttackHit` / `T_Damage` also no-op.
The persistent visual is authored on the `Bcyc` buff: `Effectart` points to
`Abilities\\Spells\\NightElf\\Cyclone\\CycloneTarget.mdl`, and
`Targetattach=sprite,first` attaches it to the victim. ROC 1.0 and TFT 1.29
both use this `EffectArt` field. The status effect stays until Cyclone ends,
including when Tornado applies the same `Bcyc` lift status.

## ROC vs TFT

| Field | ROC `Acyc` | TFT `Acyc` | ROC `AIcy` | TFT `AIcy` |
| --- | --- | --- | --- | --- |
| `targs` | `ground,enemy,neutral` | `ground,enemy,neutral,organic` | `ground,enemy,neutral` | `ground,enemy,neutral` (no `organic`) |
| `cost` / `cool` / `rng` | 150 / 5 / 600 | 150 / 5 / 600 | 0 / 0 / 600 | 0 / 0 / 600 |
| `Dur` / `HeroDur` | 30 / 6 | 20 / 6 | 30 / 6 | 20 / 5.6 |
| `DataA` | 0 | 1 (`Can Be Dispelled`) | 0 | 1 |
| `BuffID` | empty | `Bcyc,Bcy2` | empty | `Bcyc,Bcy2` |

TFT unit Cyclone adds the `organic` token so mechanical units fail
`S_SpellAllowsTarget`. Item `AIcy` keeps `ground,enemy,neutral` in TFT (ubertip
still says non-mechanical; enforce via authored targs only). ROC rows omit
`BuffID`; apply `Bcyc` as the documented fallback (same pattern as empty
`Aams` → `Bams`). `unit_addtimedstatus` reads only the first four characters, so
`Bcyc,Bcy2` applies `Bcyc`. Do not invent `DataC`.

`DataA` is dispel eligibility only (`Can Be Dispelled` / OE `dispelable`):

| `DataA` | Archive default | Dispel / Purge / Devour Magic |
| ---: | --- | --- |
| `0` | ROC `Acyc` | leave the Cyclone buff |
| non-zero | TFT `Acyc` (`1`) | remove it with other timed statuses |

`CAbilityCyclone` stores the applying rawcode in `abilstatus.data` after
`unit_addtimedstatus` (same pattern as Purge). `S_StatusIsUndispellable` reads
DataA from that rawcode — never hardcode `Acyc`. Dispel Magic and Purge both
skip undispellable slots; other buffs on the same unit still clear.

### Secondary Cyclone buff and lift restoration

`S_UnitIsCycloned` recognizes both `Bcyc` and `Bcy2`. Stock TFT's comma-separated
`BuffID` chooses its first entry (`Bcyc`), but custom maps can author `Bcy2`
as the sole first buff. Either rawcode must lift the victim and restore its
pre-Cyclone `FlyHeight` on dispel or natural expiry. Reapplying the same status
or attaching another Cyclone status preserves the original height in each
status's `next_tick` payload; the lift ends only when the final Cyclone status
is removed. The target-art attachment still follows the actual buff rawcode. Tornado’s
`Atsp` uses the same restoration helper when its `Bcyc` status ends, so a
concurrent `Bcy2` status retains the lift until it also expires or is dispelled.

`wc3_spell.cyclone_bcy2_only_cast_restores_height_on_dispel_and_expiry` and
`wc3_spell.cyclone_bcy2_replacement_keeps_original_height` exercise the
production execute/removal callbacks, recast and overlapping statuses using a
non-stock `BuffID=Bcy2` fixture.

Unit-target Purge cannot currently select a cycloned unit through
`S_SpellAllowsTarget` (Cyclone blocks spell targeting). AoE Dispel/Devour still
hit the victim. Purge's clear loop honors DataA when execute runs (tested via
`A_EXECUTE`). Retail TFT allows Purge to end Cyclone; relaxing the targeting
gate is separate work.

## Data Flow

```text
AbilityData.slk (Acyc / ACcy / SCc1 / Acny / AIcy)
  -> targs, Cost, Rng, Dur/HeroDur, BuffID, DataA
CAbilityCyclone
  -> A_VALIDATE: S_SpellAllowsTarget(code, ...) (rejects cycloned / wrong targs)
  -> A_EXECUTE: unit_addtimedstatus(first BuffID token or "Bcyc"); status.data = applying code
S_UnitIsCycloned (Bcyc or Bcy2)
  -> s_move order_move / walk
  -> s_attack can_attack / attack_target_is_valid / T_Damage
  -> s_spell spell_validate / S_SpellAllowsTarget
S_StatusIsUndispellable (Cyclone DataA==0 via status.data)
  -> CAbilityDispelMagic / CAbilityPurge clear loops skip the buff
```

Neutral is an allowed allegiance in `targs`. Do not require `S_SpellIsEnemy` alone.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Acyc
build/bin/ability_audit -data 'data/Warcraft III' -raw AIcy
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.cyclone*'
```

Focused tests cover alias procedure lookup (including item `AIcy`), TFT
organic/mechanical and ally filters, already-cycloned rejection without mana
spend, move/attack/spell/damage locks, ROC empty-BuffID → `Bcyc`, HeroDur vs Dur,
authored `Bcyc.EffectArt` attachment on a cast, expiry restore, recast,
item-row authored duration/DataA dispel via
`S_StatusIsUndispellable`, and DataA dispel eligibility (non-stock DataA≠0
removed by Adis/Purge; DataA=0 survives while other buffs and mana spend still
apply).
