# Spirit Link

## Contract

`Aspl` is TFT-only `CAbilitySpiritLink` (parent `AAsm`). ROC `AbilityData`
has no `Aspl` row. It is **not** CreepSleep (`ACsp`); the old TODO mapping is
wrong.

| Rawcode | Archive | Class | Notes |
| --- | --- | --- | --- |
| `Aspl` | TFT | `CAbilitySpiritLink` | Spirit Walker unit ability |
| `Aspp` | TFT | `code=Aspl` | Rune of Spirit Link; unregistered (item/rune) |

| Field | Stock | Meaning |
| --- | ---: | --- |
| `DataA` | 0.5 | Fraction of post-mitigation damage distributed among linked units |
| `DataB` | 4 | Max units linked per cast (nearest in Area, including the click target) |
| `Area` | 500 | Gather radius around the unit target |
| `Rng` | 750 | Cast range to the click target |
| `Dur`/`HeroDur` | 75 | Buff lifetime (seconds) |
| `Cost` | 75 | Mana |
| `BuffID` | `Bspl` | Spirit Link status |
| `targs` | `air,ground,friend,self,organic` | Mechanical/enemy/dead invalid |

Ubertip: links `DataB` units; distributes `DataA` of taken damage across Spirit
Linked units. All living allied units that currently hold `Bspl` share with
each other (not a per-cast clique ID). Redirected share damage is never fatal:
it clamps the victim to 1 HP and clears `Bspl`.

## Data Flow

```text
AbilityData.slk (Aspl)
  -> DataA, DataB, Area, Dur, BuffID, targs, Cost, Rng
CAbilitySpiritLink
  -> A_EXECUTE: up to DataB nearest valid units in Area of click target
  -> unit_addtimedstatus(Bspl); status.data = applying rawcode
T_Damage
  -> S_SpiritLinkRedirect (one call site)
  -> primary keeps (1-DataA)+DataA/n; others take DataA/n (flat, no re-entry)
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aspl
```

## Registry rows (for `s_skills.c`)

```c
{ "Aspl", CAbilitySpiritLink, AB_SPELL, SPELL_TARGET_UNIT },
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.spirit_link*'
```

Focused tests cover procedure lookup, authored DataA/DataB (non-stock), area
cap, damage split to linked allies, unlinked immunity, expiry, and recast
refresh. `Aspp` remains unregistered.
