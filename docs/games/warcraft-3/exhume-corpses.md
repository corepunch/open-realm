# Exhume Corpses

## Contract

`Aexh` is TFT-only `CAbilityExhume` (parent `AAsm`). OpenWarcraft3 names the
procedure `CAbilityExhumeCorpses`. There are no `code=` aliases.

ROC `AbilityData.slk` has no `Aexh` row (`AbilityData: not found`).

Passive Meat Wagon research: every `Dur` seconds the wagon generates one corpse of
the authored `UnitID` type, up to `DataA` corpses owned by that wagon. Stock TFT
ubertip: "Generates a corpse within the Meat Wagon every \<Aexh,Dur1\> seconds."

## Authoritative Fields

| Field | Stock Aexh L1 | Runtime meaning |
| --- | ---: | --- |
| `Dur` | `15` | seconds between corpse spawns (`S_SpellDuration`) |
| `DataA` | `5` | max corpses this wagon may keep (same shape as `Agyd`) |
| `UnitID` | `ugho` | corpse unit rawcode (`S_SpellUnitId`); Ghoul, not Crypt Fiend |
| `HeroDur` / `Cost` / `Cool` / `Rng` / `BuffID` | empty/0 | unused |

Wiki text that says "Crypt Fiend" is wrong for stock `AbilityData`; Neoseeker and
the SLK agree on Ghoul (`ugho`). AbilityMetaData / ubertip / brief beat wiki.

`Agyd` (Graveyard Create Corpse) shares DataA=5 + UnitID=ugho and uses `Cool=15`
as its interval. Exhume stores the interval in `Dur` instead. Agyd remains a
separate TODO.

## Cargo is out of scope

Retail places exhumed corpses inside Meat Wagon cargo (`Amel` / `Amed` / `Amtc`).
Those load/drop abilities are not implemented here. This procedure spawns an
ordinary dead edict near the wagon (`owner = wagon`) so Raise Dead / Cannibalize
can consume it from the ground until cargo lands.

## Runtime Ownership

```text
AbilityData.slk (Aexh)
  -> Dur, DataA, UnitID
CAbilityExhumeCorpses (AB_PASSIVE | AB_UPDATE)
  -> A_UPDATE arms a classless thinker on units that have Aexh
exhume_think / G_RunEntities
  -> if under DataA cap: SP_SpawnAtLocationNoBirth(UnitID) then mark dead
  -> next pulse after Dur seconds
```

## Diagnostic Workflow

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw Aexh
```

## Verification

```sh
make test-wc3-engine WC3_PATTERN='wc3_spell.exhume*'
```
