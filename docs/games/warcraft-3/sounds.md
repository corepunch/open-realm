# Warcraft III — Unit Sound System

## Sound Catalog Files

Unit sounds live in two SLK tables shipped in `War3.mpq`:

| File | Purpose |
|------|---------|
| `UI/SoundInfo/UnitAckSounds.slk` | Acknowledgement (what/yes/attack/pissed/ready/warcry) sounds per unit |
| `UI/SoundInfo/UnitCombatSounds.slk` | Combat impact/swing sounds by weapon/armor type |
| `UI/SoundInfo/UISounds.slk` | Interface sounds (button clicks, etc.) |
| `UI/SoundInfo/AnimSounds.slk` | Sounds triggered from MDX animation events |

## SLK Column Layout

Both `UnitAckSounds.slk` and `UnitCombatSounds.slk` share the same column schema (X1–X19):

| Column | Key | Description |
|--------|-----|-------------|
| X1 | *(row key)* | Sound label, e.g. `"FootmanYesAttack"` |
| X2 | `FileNames` | Comma-separated WAV filenames (random selection) |
| X3 | `DirectoryBase` | Directory prefix, e.g. `"Units\Human\Footman\"` |
| X4 | `Volume` | 0–127 integer volume |
| X5 | `Pitch` | 1.0 = normal |
| X6 | `PitchVariance` | Random pitch variation range |
| X7 | `Priority` | Playback priority |
| X8 | `Channel` | Audio channel (1 = voice, 5 = combat) |
| X9 | `Flags` | `WANT3D`, `RANDOMPITCH`, `NODUPEUSERNAMES`, etc. |
| X10–X12 | `MinDistance`, `MaxDistance`, `DistanceCutoff` | 3D attenuation |
| X19 | `EAXFlags` | EAX reverb preset name |

Full path = `DirectoryBase + filename`, e.g. `Units\Human\Footman\FootmanYesAttack1.wav`.

## Unit Sound Label (`usnd`)

`Units/unitUI.slk`, column X3 (`unitSound`) maps each unit's four-char ID to a sound label:

```
hfoo → "Footman"
hpea → "Peasant"
Hamg → "HeroArchMage"
```

This label is the base for all per-unit sound lookups: `{label}What`, `{label}Yes`, `{label}YesAttack`, `{label}Pissed`, `{label}Ready`, `{label}Warcry`.

Death sounds are **not** in `UnitAckSounds.slk`. They are raw WAV files at `{modelDir}\{ModelName}Death.wav` (e.g. `Units\Human\Footman\FootmanDeath.wav`).

## Sound Events Per Unit (Footman Example)

| Label | Files | Trigger |
|-------|-------|---------|
| `FootmanWhat` | `FootmanWhat1-4.wav` | Click-to-select (acknowledgement) |
| `FootmanYes` | `FootmanYes1-4.wav` | Move order |
| `FootmanYesAttack` | `FootmanYesAttack1-3.wav` | Attack order / swing |
| `FootmanPissed` | `FootmanPissed1-4.wav` | Repeated clicks (idle taunts) |
| `FootmanReady` | `FootmanReady1.wav` | Unit created / train complete |
| `FootmanWarcry` | `FootmanWarcry1.wav` | Special (not commonly triggered) |
| *(raw file)* | `FootmanDeath.wav` | Unit death |

## Combat Sounds

`UnitCombatSounds.slk` maps weapon/armor type combinations to hit sounds.
The unit's weapon type column (`ucs1`/`ucs2` in `UnitWeapons.slk`) and armor type (`udty` in `unitUI.slk`) select the sound set.
Examples:

| Label | Use |
|-------|-----|
| `MetalHeavyBashEthereal` | Ethereal hit |
| `AxeMediumChopWood` | Wood-chop by medium axe |

## Building Sounds

Buildings use `BuildingSoundLabel` (from `*UnitFunc.txt`) which maps to looping construction sounds. Movement sounds use `MovementSoundLabel`.

## OpenWarcraft3 Implementation

The typed SLK registry loads `UI/SoundInfo/UnitAckSounds.slk` into a flat `unitSoundRow_t` array. Sound names are not FOURCCs, so each row owns its complete source name and `G_UnitAckSound` compares that string. At unit spawn, `G_RegisterUnitSounds` reads the `usnd` label from `unitUI.slk` and registers:
- `sound_select[]` ← every `{label}What` file (up to the authoritative ROC/TFT maximum of six)
- `sound_attack` ← `{label}YesAttack` first file path via `gi.SoundIndex`
- `sound_death` ← `{label}Death.wav` raw path via `gi.SoundIndex`

On selection, the server chooses a random `sound_select` entry and queues it until
`G_RunEntities`, after that function clears the previous frame's one-shot event.
The selected entity then carries `EV_ACK` plus the sound configstring index for one snapshot.
`G_CustomizeEntity` strips that event from snapshots sent to clients that did
not select the entity, keeping acknowledgement voices local to the selecting player.
On attack swing (`attack_melee`/`attack_ranged`): `s.event = EV_ATTACK; s.sound = sound_attack`.
On death (`unit_die`): `s.event = EV_DEATH; s.sound = sound_death`.
Client fires `S_PlaySoundFile` on any non-zero `s.event`.

`CL_PrepRefresh` registers every populated `CS_SOUNDS` entry through
`S_RegisterSound`, which creates the path-keyed handle and decodes its PCM before
the client sends `begin`. The event path therefore performs only a configstring
lookup and cached playback; archive I/O and resampling never occur on the click.
Later `CS_SOUNDS` updates are registered directly by `CL_ParseConfigString`.

Classic WC3 unit WAVs are multi-sector, encrypted MPQ entries. Their first sector may use zlib while later sectors
use Blizzard adaptive Huffman plus mono ADPCM (`0x41`). The in-tree MPQ reader must decode every sector before the
sound cache parses the WAV; accepting a partial MPQ read produces a valid 4096-byte RIFF prefix and audibly reduces
lines such as “What?” to “Wh-”. WoW UI sounds do not necessarily use this compression combination, which is why the
same mixer can play them completely while WC3 unit responses are cropped.
