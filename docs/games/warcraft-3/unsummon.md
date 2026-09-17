# Unsummon Building

## Contract

`Auns` is ROC/TFT `CAbilityUnsummon` (parent `AAsm`). No `AbilityData` aliases
share `code=Auns`.

| Field | Stock | OE label | Runtime |
| --- | ---: | --- | --- |
| `DataA` | 0.5 | `recoveredResources` | fraction of building gold/lumber cost refunded |
| `DataB` | 50 | `acumulationStep` | unresolved; retail gradual refund step (not used by instant path) |
| `targs` | ROC `structure,debris,player`; TFT `structure,player` | — | owned living structure |
| `Cost` / `Cool` / `Rng` | 0 / 0 / 0 | — | free, no cooldown; `Rng<=0` is always in range |

Ubertip: "Unsummons your building to regain \<Auns,DataA1,\>% of the spent
resources." Confirms DataA is the refund fraction (0.5 → 50%).

Invalid targets (reject without spending mana): enemy building, non-structure
unit, dead. `S_SpellAllowsTarget` does not enforce `structure`/`player`;
`CAbilityUnsummon` `A_VALIDATE` does.

TFT class list also has `Buns` / `CBuffUnsummon`, but AbilityData leaves
`BuffID` empty and AbilityBuffData has no `Buns` row. No buff is applied.

## Data Flow

```text
AbilityData.slk (Auns)
  -> DataA recoveredResources, targs structure+player
UnitBalance goldCost / lumberCost
  -> same cost source cancel-build records on construction.paid
CAbilityUnsummon
  -> A_VALIDATE: alive + same player + G_UnitIsBuilding
  -> A_EXECUTE: refund floor(cost * DataA) gold/lumber, unit_die(building)
```

Cost is read from `building->data.UnitBalance` when present, otherwise
`G_UnitBalance(class_id)` — the same UnitBalance fields `G_ChargeBuilding` /
construction payment recording use. Do not hardcode building prices. Cancel
construction still refunds 75% of the recorded payment; Unsummon refunds
authored DataA of the building row cost on a completed structure.

## Known Gaps

- Retail Unsummon damages the building over time (demolish) and refunds in
  DataB-sized steps while the Acolyte works. This implementation kills and
  refunds immediately.
- `debris` ROC targs token and `Buns` presentation buff are unimplemented.

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Auns
```

## Registry row (for `s_skills.c`)

```c
{ "Auns", CAbilityUnsummon, AB_SPELL, SPELL_TARGET_UNIT },  /* Unsummon Building */
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.unsummon*'
```

Focused tests cover procedure lookup, authored non-stock DataA refund against
UnitBalance costs, and rejects for enemy building / non-structure / dead
without mana spend.
