# Anti-Magic Shell

## Contract

`Aams` is TFT `CAbilityAntiMagicShell` (parent `AAsm`). `Aam2` and `ACam` are
`AbilityData` aliases whose `code` is `Aams`, so they share `CAbilityAntiMagicShell`
and read their own rows through `abilityitem_t.code`.

Authored `DataC` selects the shell kind:

| Rawcode | Archive | DataC | Buff applied | Effect |
| --- | --- | ---: | --- | --- |
| `Aams` | ROC and TFT | 0 | `Bams` | cannot be targeted by spells; `S_SpellDamage` is rejected |
| `ACam` | ROC and TFT | 0 | `Bams` | same immunity; creep cost/tooltip row |
| `Aam2` | TFT melee | 300 | `Bam2` | unit stays targetable; spell damage consumes the pool |

ROC `AbilityData.slk` omits `BuffID`. The strings table still names `Bams`, so
an empty BuffID with empty DataC applies `Bams`. TFT rows author
`BuffID1=Bams,Bam2`; empty DataC uses the first token, non-zero DataC uses the
second. Do not treat DataA/DataB as the shield amount.

`Aami` / item `AIxs` (`code=Aami`, `CAbilityAntiMagicShellInstant`) remain
unregistered. `AIxs` has its own duration, cooldown, and DataB row and is not
an `Aams` alias.

## Data Flow

```text
AbilityData.slk (Aams / Aam2 / ACam)
  -> DataC, Dur/HeroDur, BuffID, targs, Cost, Rng
CAbilityAntiMagicShell
  -> unit_addtimedstatus(Bams or Bam2)
S_UnitSpellImmune
  -> Bams only (Avatar also uses this predicate)
S_SpellDamage
  -> reject Bams; S_AntiMagicShellAbsorb consumes Bam2.remaining
S_ResolveAttackHit / T_Damage
  -> physical damage is not absorbed
```

Remaining Bam2 absorption is `heroabilitystatus_t.data` on the `Bam2` slot. It
is part of the raw `edict_t` save record. Recasting `Aam2` replaces the buff
and resets the pool to authored DataC. Overflow damage after the pool hits
zero applies through `T_Damage` and `S_SpellDamage` returns true so other
spell effects (stun, etc.) may proceed. Fully absorbed hits return false.

`Aams` targs are `air,ground` (no allegiance filter). `Aam2` targs include
`friend,self`, so enemies are invalid.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aams
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.anti_magic_shell*'
```

Focused tests cover Bams targeting immunity, physical pass-through, recast
rejection while immune, expiry, the ROC missing-BuffID fallback, Aam2
absorption/overflow/refresh, alias procedure sharing, and save/load of the
remaining pool.
