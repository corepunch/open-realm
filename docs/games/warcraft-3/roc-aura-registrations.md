# RoC Aura Registrations And Devotion Aura Mapping

## Finding

RoC 1.0's `AbilityData.slk` schema has no `BuffID` column. That absence led to
the initial assumption that `AHad` recipients must use the ability's
`TargetArt`, the same ground-pattern model used by the caster. Static analysis
of the original RoC gameplay DLL shows that assumption is wrong for Devotion
Aura: the executable associates `AHad` with buff class `BHad`, whose profile
selects the separate recipient glow model.

The proven RoC behavior is therefore a source pattern on the caster and the
`BHad` glow on eligible recipients. The relationship is encoded in engine
behavior rather than in the RoC ability data row. This investigation followed
the `AHad` path specifically; the registration inventory below is not evidence
that every aura's ability-to-buff relationship has been traced.

## Finding The RoC Gameplay DLL

The RoC installation image is
`data/war3cds/Warcraft 3 Reign of Chaos/Setup.mpq`. Its installer manifest
`SetupDat/inst_files.ins` identifies the gameplay payload as
`Files100/Game.dll`. To locate the entry in the MPQ, use the repository's
listfile-assisted scanner and the recovered filename list:

```sh
python3 tools/list_setup_mpq.py \
  'data/war3cds/Warcraft 3 Reign of Chaos/Setup.mpq' \
  tools/WarcraftIIIFileListZezulaLadik.txt
```

The listed internal path is `Files100\\Game.dll` (MPQ paths use backslashes).
Extract that entry with `mpqtool` and analyze the extracted PE statically. The
RoC payload examined here was 7,725,118 bytes, SHA-256
`9970c5af4ff4821f8d593249e06af803d8132bef4c7d015d6a1423223c51a378`.
These details identify the evidence precisely; the original game was not
launched.

## Static Analysis Method

The PE was inspected with radare2 and Ghidra. First, RTTI class names and
rawcodes were searched to find registration call sites and class constructors.
The registration helper's call arguments were decoded into virtual address,
rawcode, class name, and declared parent. The executable contains 717 calls to
that helper; the inventory below includes the 27 aura-related ability and buff
classes.

Then the `AHad` registration and nearby class methods were followed in Ghidra.
Targeted decompilation was used for those methods; full automatic analysis was
not needed. No PDB was available. The important distinction is that RTTI
registration establishes which classes exist, while the `AHad` method trace
establishes the ability-to-buff lookup. A class's declared parent is not that
lookup relationship.

## Aura Class Registration Inventory

Addresses are virtual addresses in the extracted `Files100\\Game.dll`. The
rawcode and class names are executable strings. `parent` is the registered
RTTI parent, not a linked ability or buff unless independently shown.

| Registration VA | Rawcode | Registered class | Parent |
|---:|---|---|---|
| `0x6f2fa919` | `aura` | `CAbilityAura` | `ABon` |
| `0x6f3c0ca9` | `BAUR` | `CBuffAura` | `buff` |
| `0x6f2fbfb9` | `AHab` | `CAbilityAuraBrilliance` | `aura` |
| `0x6f2fba09` | `BHab` | `CBuffAuraBrilliance` | `BAUR` |
| `0x6f2fcb19` | `AOac` | `CAbilityAuraCommand` | `aura` |
| `0x6f2fc6a9` | `BOac` | `CBuffAuraCommand` | `BAUR` |
| `0x6f2fcf29` | `Aakb` | `CAbilityAuraKotoBeast` | `aura` |
| `0x6f2fcd89` | `Bakb` | `CBuffAuraKotoBeast` | `BOac` |
| `0x6f2fd8e9` | `AHad` | `CAbilityAuraDevotion` | `aura` |
| `0x6f2fd6f9` | `BHad` | `CBuffAuraDevotion` | `BAUR` |
| `0x6f2fdc89` | `AOae` | `CAbilityAuraEndurance` | `aura` |
| `0x6f2fdef9` | `BOae` | `CBuffAuraEndurance` | `BAUR` |
| `0x6f2fe859` | `Aapl` | `CAbilityAuraPlague` | `aura` |
| `0x6f2fecb9` | `Bapl` | `CBuffAuraPlague` | `BlDT` |
| `0x6f2ffba9` | `Aoar` | `CAbilityAuraRegenLife` | `aura` |
| `0x6f2ff429` | `Boar` | `CBuffAuraRegenLife` | `BAUR` |
| `0x6f2ffe39` | `Aarm` | `CAbilityAuraRegenMana` | `aura` |
| `0x6f2ff7e9` | `Barm` | `CBuffAuraRegenMana` | `BAUR` |
| `0x6f301639` | `Aasp` | `CAbilityAuraSpell` | `AAsp` |
| `0x6f3019e9` | `AEah` | `CAbilityAuraThorns` | `aura` |
| `0x6f301829` | `BEah` | `CBuffAuraThorns` | `BAUR` |
| `0x6f301ec9` | `AEar` | `CAbilityAuraTrueshot` | `aura` |
| `0x6f301d29` | `BEar` | `CBuffAuraTrueshot` | `BOac` |
| `0x6f3026d9` | `AUau` | `CAbilityAuraUnholy` | `aura` |
| `0x6f302189` | `BUau` | `CBuffAuraUnholy` | `BAUR` |
| `0x6f303009` | `AUav` | `CAbilityAuraVampiric` | `aura` |
| `0x6f302e59` | `BUav` | `CBuffAuraVampiric` | `BAUR` |

`Aasp` has no corresponding `CBuffAuraSpell` registration in this set. The
unusual parents for `Bakb`, `Bapl`, and `BEar` are preserved as registered;
they should not be interpreted as data-level BuffID edges.

## Traced Devotion Relationship

The relevant executable evidence is:

| Address | Directly observed behavior |
|---:|---|
| `0x6f2fd8e9` | Registers rawcode `AHad` as `CAbilityAuraDevotion`. |
| `0x6f2fd6f9` | Registers rawcode `BHad` as `CBuffAuraDevotion`. |
| `0x6f2fd9f0` | `CAbilityAuraDevotion` method returns the `AHad` identifier. |
| `0x6f2fda10` | `CAbilityAuraDevotion` method returns the `BHad` identifier. |
| `0x6f2fda20` | Uses `BHad` as a lookup key and sends the resolved result through aura handling. |

Together, these method references show an engine-side `AHad`→`BHad` relation.
The class registrations alone would not establish that. The trace is specific
to the original RoC DLL and Devotion Aura.

## Data Profile Cross-Check

In RoC `War3.mpq`, `Units/HumanAbilityFunc.txt` contains:

| Rawcode | Field | Value |
|---|---|---|
| `[AHad]` | `Targetart` | `Abilities\\Spells\\Human\\DevotionAura\\DevotionAura.mdl` |
| `[AHad]` | `Targetattach` | `origin` |
| `[BHad]` | `Targetart` | `Abilities\\Spells\\Other\\GeneralAuraTarget\\GeneralAuraTarget.mdl` |
| `[BHad]` | `Targetattach` | `origin` |

The `BHad` profile and its recipient model are present in RoC, not merely
introduced by TFT. In the later TFT data, `AbilityData.slk` explicitly assigns
`AHad.BuffID = BHad` at each rank. The RoC `AbilityData.slk` does not expose a
`BuffID` column, while its ability profile already contains `AHad` and `BHad`
art definitions. This combination explains how RoC could have distinct source
and recipient visuals without storing the relationship in that SLK field.

## Limits And Interpretation

This is static evidence of the RoC executable's class registration and the
Devotion lookup path. It supports an implicit/hardcoded mapping (hypothesis C)
for `AHad`→`BHad`; it contradicts the claim that the RoC data schema's missing
`BuffID` proves identical caster and recipient visuals. The matching profile
art paths support the intended visual roles, but this investigation did not
observe the original executable rendering a live aura. Runtime observation
would be needed to confirm the final visual result, including any renderer
conditions not represented in the inspected path.

The analysis did not trace the other aura class methods or exhaustively
inventory every executable mapping. Do not infer their relationships from
matching names, RTTI parents, or registration order. For OpenRealm, resolve RoC
Devotion recipients through `BHad` just as TFT's explicit `BuffID` does, while
keeping the mapping version-aware and avoiding an unverified universal rule.

See [Aura Targets And Overlays](aura-targets-and-overlays.md) for the current
engine presentation lifecycle and regression coverage.
