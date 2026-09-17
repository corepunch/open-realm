# Stasis Trap

## Contract

`Asta` is ROC/TFT `CAbilityStasisTrap` (parent `AAsm`). No `AbilityData` aliases.
Point cast summons an invisible timed ward (`UnitID`) that arms after `DataA`,
then detonates when an enemy land unit enters `DataB`, stunning enemies in
`DataC` and destroying itself plus other stasis wards in that radius.

| Rawcode | Archive | Notes |
| --- | --- | --- |
| `Asta` | ROC and TFT | Witch Doctor Stasis Trap |

## Authoritative Fields

WorldEdit `AbilityMetaData` (`WESTRING_AEVAL_STA*`):

| Field | Meta | Stock ROC | Stock TFT | Runtime meaning |
| --- | --- | ---: | ---: | --- |
| `DataA` | Activation Delay | `10` | `10` | seconds before the trap can trigger |
| `DataB` | Detection Radius | `250` | `250` | trigger radius around the ward |
| `DataC` | Detonation Radius | `500` | `400` | stun + peer-ward destroy radius |
| `DataD` | Stun Duration | `12` | `6` | non-hero stun seconds |
| `UnitID` | Ward Unit Type | empty | `otot` | ward unit (`S_SpellUnitId`) |
| `Dur` | | `150` | `150` | ward timed life (`BTLF`) |
| `HeroDur` | | `4` | `2.5` | hero stun seconds |
| `BuffID` | | empty | `Bsta` | stun status on victims |
| `Area` | | `0` | `0` | unused; radii are DataB/C |

ROC omits `UnitID` and `BuffID`. Empty BuffID falls back to `Bsta` (AbilityStrings
buff name). Fixture tests always author a UnitID.

`otot` UnitAbilities carries only `Aeth`; arm/detect/detonate are owned by `Asta`,
not by an ability on the ward unit. TFT class `Bstt` (`CBuffStasisTrapTrigger`) is
the arming presentation buff and is not required for the mechanical contract.

## Data Flow

```text
AbilityData.slk (Asta)
  -> UnitID, Dur, DataA–D, HeroDur, BuffID
CAbilityStasisTrap
  -> S_SummonAt(caster, UnitID, point, Dur)  // ward.owner = caster
  -> ward.summon_ability = Asta; RF_HIDDEN
  -> classless thinker (stasis_trap_think)
G_RunEntities
  -> after DataA: if enemy land in DataB, stun DataC with DataD/HeroDur
  -> destroy peer Asta wards in DataC; kill self
unit_refreshstatusflags
  -> Bsta sets stunned (with Bstu / BUsl / Bpos)
```

Air units (`TARG_AIR`) do not trigger or take the stun. Structures are out of
the land-unit contract.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Asta
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.stasis_trap*'
```
