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
as its interval; `DataB`/`Gyd2` supplies the gravestone/spawn radius while `DataC`/`Gyd3`
supplies the nearby-corpse count radius. OpenRealm implements that producer through the
shared corpse lifecycle. Exhume stores its interval in `Dur` instead.

## Meat Wagon cargo

Retail places exhumed corpses inside Meat Wagon cargo (`Amel` / `Amed` / `Amtc`).
OpenRealm now stores the actual dead edict in the authored eight-slot `Sch2` cargo
hold, suspending its decay state. `Amtc` is corpse-only storage: it contributes corpse capacity
but does not qualify the Meat Wagon for ordinary living-unit Load or Smart/right-click boarding.
`Amel` Get Corpse is an autocast-capable command: its scheduler searches the holder's authored
acquisition range, applies the ability's target mask, and then reuses the corpse approach/load
path. Because stock `Amel` does not author a
friend/player ownership token, eligible enemy corpses are not rejected by a hard-coded owner check. The `DataA=5` Exhume limit remains independent of
the eight-slot transport capacity, so Exhume stops at the authored five matching
corpses. Stored corpse identity remains on the hidden edict, while its effective gameplay
position follows the current Wagon position for corpse-consuming abilities. Raise Dead and
Cannibalize consume the stored edict directly; `Amed` unloads it through the shared cargo
placement path. A corpse already in its bone/remains phase restarts `BoneDecayTime` when
unloaded rather than resuming the frozen remainder. See [Corpse Lifecycle, Cannibalize,
and Raise Dead](corpse-mechanics.md).

## Runtime Ownership

```text
AbilityData.slk (Aexh)
  -> Dur, DataA, UnitID
CAbilityExhumeCorpses (AB_PASSIVE | AB_UPDATE)
  -> A_UPDATE arms a classless thinker on units that have Aexh
exhume_think / G_RunEntities
  -> if under DataA cap: SP_SpawnAtLocationNoBirth(UnitID), mark dead, load into Sch2
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
