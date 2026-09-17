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
second. Do not treat DataA/DataB as the shield amount for unit AMS.

### Item Instant Shell (`Aami` / `AIxs`)

`Aami` is TFT `CAbilityAntiMagicShellInstant` (parent `Aams`). There is no
`AbilityData` alias row for `Aami` itself; item `AIxs` authors `code=Aami` and
is the live AbilityData row. Anti-Magic Potion (`pams`) uses `abilList=AIxs`,
`cooldownID=Aami`, `uses=1`, `perishable=1`. Ubertip: immunity to magical spells
for `<AIxs,Dur1>` seconds.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Aami` | class only | register for `FindAbilityForCommand` / cooldownID; no AbilityData row |
| `AIxs` | ROC and TFT | item Instant AMS; `code=Aami`; not an `Aams` alias |

| Field | ROC `AIxs` | TFT `AIxs` | MetaData |
| --- | --- | --- | --- |
| `targs` / `cost` / `rng` | `air,ground` / 0 / 0 | same | unit-target item use (`AB_SPELL`) |
| `cool` / `Dur` / `HeroDur` | 0 / 90 / 90 | 30 / 15 / 15 | item row owns duration |
| `DataA` | 0 | 0 | Ixs1 Damage To Summoned Units |
| `DataB` | 0 | 10 | Ixs2 Magic Damage Reduction |
| `DataC` | 0 | 0 | Ams3 Shield Life → empty applies `Bams` |
| `BuffID` | empty | `Bams,Bam2` | empty + DataC=0 → `Bams` fallback |

`AIxs` / `Aami` share `CAbilityAntiMagicShellInstant`, which reuses the same
Bams/Bam2 apply and `S_AntiMagicShellAbsorb` paths as unit AMS (DataC selects
the buff). DataA summon damage and DataB magic-damage reduction are authored
but not wired into damage yet (same gap as unit `Ams1`/`Ams2`).

## Data Flow

```text
AbilityData.slk (Aams / Aam2 / ACam)
  -> DataC, Dur/HeroDur, BuffID, targs, Cost, Rng
CAbilityAntiMagicShell
  -> unit_addtimedstatus(Bams or Bam2)
AbilityData.slk (AIxs, code=Aami) + class Aami
  -> item Dur/Cool/DataB/DataC/BuffID via abilityitem_t.code
CAbilityAntiMagicShellInstant
  -> same Bams/Bam2 apply + S_AntiMagicShellAbsorb as unit shell
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
build/bin/ability_audit -data 'data/Warcraft III' -raw AIxs
build/bin/ability_audit -data 'data/Warcraft III' -raw Aami
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.anti_magic_shell*'
```

Focused tests cover Bams targeting immunity, physical pass-through, recast
rejection while immune, expiry, the ROC missing-BuffID fallback, Aam2
absorption/overflow/refresh, unit alias procedure sharing, item `AIxs`/`Aami`
Instant procedure lookup with authored item duration/DataC→Bams, and save/load
of the remaining Bam2 pool.
