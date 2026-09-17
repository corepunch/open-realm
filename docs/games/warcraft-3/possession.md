# Possession

## Contract

`Apos` is TFT/ROC `CAbilityPossession` (parent `AAsm`). `ACps` is an
`AbilityData` alias whose `code` is `Apos`, so it shares `CAbilityPossession`
and reads its own row through `abilityitem_t.code`.

`Aps2` is TFT-only `CAbilityPossessionTwo` (parent `Apos`, own `code=Aps2`).
It is **not** an `Apos` alias and is **not** Charm (`ANch` / `CAbilityCharm`).

| Rawcode | Archive | Class | Channel | Effect |
| --- | --- | --- | --- | --- |
| `Apos` | ROC and TFT | `CAbilityPossession` | no | immediate ownership transfer; caster dies |
| `ACps` | ROC and TFT | `CAbilityPossession` (`code=Apos`) | no | same; creep cost/tooltip row |
| `Aps2` | TFT only | `CAbilityPossessionTwo` | yes (`AB_CHANNEL`) | stun lock for `Dur`, then transfer; caster dies |

Shared `targs`: `ground,nonhero,enemy,organic,neutral`. Air, heroes, mechanical,
allies, dead, and spell-immune units are invalid. `DataA` is max allowed
`UnitBalance.level` (stock 5); level 0 disables the gate.

### Aps2 channel fields (OE)

| Field | Stock | OE label | Runtime |
| --- | ---: | --- | --- |
| `DataA` | 5 | `creepMaxLv` | same max-level gate as `Apos` |
| `DataB` | 1.66 | `damageIncrease` | caster attack-damage taken multiplier while `Bpoc` is active |
| `DataC` | 1 | `invulnerable` | non-zero → target `invulnerable` for the channel |
| `DataD` | 1 | `magicImmunity` | non-zero → target spell-immune for the channel |
| `BuffID` | `Bpos,Bpoc` | — | `Bpos` on target (stun); `Bpoc` on caster (damage amp, not `stunned`) |
| `Dur`/`HeroDur` | 4.5 | — | channel length; successful expiry performs the takeover |

`Bpoc` must **not** set `ent->stunned`: `spell_run_frame` cancels any channel on
stun, and an external `Bstu` is what aborts Possession. Caster lock is the
normal `AB_CHANNEL` origin/move cancel. Target `Bpos` is a real stun status.

On abort (caster death/stun/move, target death, cancel): strip `Bpos`/`Bpoc`,
restore prior target invulnerability, no ownership change. On success: strip
buffs, transfer `s.player`, kill the caster body.

## Data Flow

```text
AbilityData.slk (Apos / ACps / Aps2)
  -> targs, Cost, Rng, Dur, DataA-D, BuffID
CAbilityPossession / CAbilityPossessionTwo
  -> A_VALIDATE: nonhero + DataA level (+ enemy/neutral)
  -> A_EXECUTE Apos: G_SetUnitPlayer + unit_die(caster)
  -> A_EXECUTE Aps2: channel thinker, Bpos/Bpoc, optional invuln
S_ResolveAttackHit
  -> S_PossessionDamageTaken multiplies by Bpoc.data (DataB * 1000)
S_UnitSpellImmune
  -> Bpos with DataD flag blocks further spells on the victim
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Apos
build/bin/ability_audit -data 'data/Warcraft III' -raw Aps2
build/bin/ability_audit -data 'data/Warcraft III' -raw ACps
```

## Registry rows (for `s_skills.c`)

```c
{ "Apos", CAbilityPossession, AB_SPELL, SPELL_TARGET_UNIT },
{ "ACps", CAbilityPossession, AB_SPELL, SPELL_TARGET_UNIT },
{ "Aps2", CAbilityPossessionTwo, AB_SPELL | AB_CHANNEL, SPELL_TARGET_UNIT },
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.possession*'
```

Focused tests cover alias/procedure lookup, targs rejects without mana spend,
authored DataA gate, instant takeover + caster death, Aps2 channel success and
abort, DataB attack amp, and DataC invulnerability during the channel.
