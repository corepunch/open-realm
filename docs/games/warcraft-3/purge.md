# Purge

## Contract

`Aprg` is `CAbilityPurge` (parent `AAsm`). `Apg2` is a TFT melee alias whose
`code` is `Aprg`, so it shares `CAbilityPurge` and reads its own row through
`abilityitem_t.code`. Item `AIlp` (`CAbilityLightningPurge`, parent `Aprg`) is
registered on the same procedure. Creep `ACpu` also has `code=Aprg` but remains
unregistered here. Orb/totem `AIpg` / `AIps` are out of scope.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Aprg` | ROC and TFT | classic Purge (no pause) |
| `Apg2` | TFT only | Shaman melee Purge; adds unit/hero pause |
| `AIlp` | ROC and TFT | Item Purge / Lightning Purge |
| `ACpu` | ROC and TFT | creep Purge — unregistered |

Casting strips every timed status from the target, applies the Purge slow buff,
and deals `DataC` damage when `target->owner` is set (summoned).

## Authored fields

WorldEdit `AbilityMetaData` labels (`WESTRING_AEVAL_PRG*`):

| Field | Stock `Aprg` | Stock `Apg2` | Meta name | Runtime |
| --- | ---: | ---: | --- | --- |
| `DataA` | 5 | 5 | Movement Update Frequency | initial slow; see below |
| `DataB` | 0 | 0 | Attack Update Frequency | unused |
| `DataC` | 400 | 400 | Summoned Unit Damage | `S_SpellDamage` when `owner` set |
| `DataD` | 0 | 3 | Unit Pause Duration | non-hero immobilize window (seconds) |
| `DataE` | 0 | 1 | Hero Pause Duration | hero immobilize window (seconds) |
| `DataF` | — | — | Mana Loss | unused |
| `Dur` / `HeroDur` | 15 / 5 | 15 / 5 | — | slow buff lifetime (`Bprg`) |
| `BuffID` | empty (ROC) / `Bprg` (TFT) | `Bprg` | — | empty falls back to `Bprg` |

`Apg2` ubertip: enemy units are immobilized for `<DataD>` seconds, then slowed
by a factor of `<DataA>` while recovering over `<Dur>`. Immobilization is a
move lock (reject `order_move`, 100% `S_PurgeMoveReduction`), not a separate
stun buff. Do not treat stock `DataA=5` as “zero speed”; pause comes from
`DataD`/`DataE`.

### DataA slow + gradual recovery

Ubertip “factor of DataA” means stock `DataA=5` starts at speed `1/5`. Fixtures
often author a remaining-speed complement in `(0,1]` (e.g. `0.5`); those use
`1 - DataA` as the initial reduction. `DataA <= 0` is a full stop.

After any pause window, reduction lerps from that initial value down to `0`
over the remaining buff lifetime (`Dur - pause`). At the instant pause ends,
reduction is still the full initial slow; later samples are weaker.

## Data Flow

```text
AbilityData.slk (Aprg / Apg2 / AIlp)
  -> DataA/C/D/E, Dur/HeroDur, BuffID, targs, Cost, Rng
CAbilityPurge
  -> clear timed statuses (skip S_StatusIsUndispellable; Cyclone DataA==0)
  -> unit_addtimedstatus(BuffID or Bprg); store spell->code in status.data
  -> S_SpellDamage(DataC) when target->owner
S_PurgeMoveReduction / S_PurgeIsImmobilized
  -> read DataA/D/E from status.data (casting rawcode), not hardcoded Aprg
  -> after pause: initial slow * (1 - elapsed_after_pause / slow_window)
s_move order_move / ai_move_walk
  -> S_PurgeIsImmobilized rejects/stops translation (same family as Bens/BEer)
```

Purge ubertip says “Removes all buffs”, but Cyclone authors `Can Be Dispelled`
(`DataA`). ROC `DataA=0` cyclone survives Purge's clear loop; TFT non-zero does
not. Unit-target selection of an already-cycloned unit is still blocked by
`S_SpellAllowsTarget`; see [Cyclone](cyclone.md).

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aprg
build/bin/ability_audit -data 'data/Warcraft III' -raw Apg2
build/bin/ability_audit -data 'data/Warcraft III' -raw AIlp
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.purge*'
```

Focused tests cover alias procedure sharing, Aprg authored DataA slow,
Apg2 DataD immobilize + expiry restore, Hero DataE pause, summoned DataC
damage, and gradual post-pause recovery. Existing `t_spell.c` Purge cases must
keep passing.
