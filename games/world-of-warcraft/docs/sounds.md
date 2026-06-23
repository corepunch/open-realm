# World of Warcraft (Classic) — Unit Sound System

## Sound Catalog: SoundEntries.dbc

All WoW sounds are indexed in `DBFilesClient\SoundEntries.dbc`.

### Classic DBC Layout (29 fields, 116 bytes/record)

| Field | Name | Type | Description |
|-------|------|------|-------------|
| 0 | ID | uint32 | Primary key (kit ID) |
| 1 | type | uint32 | Sound type (1=Spell, 2=UI, 3=Footsteps, 4=PropAmbience, …) |
| 2 | name | string | Display/lookup name |
| 3–12 | file[0..9] | string | Up to 10 WAV/MP3 paths |
| 13–22 | freq[0..9] | uint32 | Weight for random selection (0 = never play) |
| 23 | directoryBase | string | Path prefix prepended to each file |
| 24 | volumeFloat | float | Base playback volume |
| 25 | flags | uint32 | Playback flags |
| 26 | minDistance | float | 3D min distance |
| 27 | distanceCutoff | float | Hard cutoff distance |
| 28 | eaxdef | uint32 | EAX reverb preset index |
| 29 | advancedID | uint32 | Linked advanced sound entry |

Full path = `directoryBase + "\" + file[n]`.

## Unit Sound Wiring

Units reference `SoundEntries` IDs through creature templates in the database (server-side) or through `CreatureSoundData.dbc` (client-side display). Key linking DBCs for classic:

| DBC | Role |
|-----|------|
| `CreatureSoundData.dbc` | Per-creature: maps sound event slots to `SoundEntries` IDs |
| `CreatureDisplayInfo.dbc` | Links creature display ID to `CreatureSoundData` record |
| `CreatureModelData.dbc` | Footstep sound set ID per model |

`CreatureSoundData.dbc` contains fields for each event: `soundExertionID`, `soundExertionCriticalID`, `soundInjuryID`, `soundInjuryCriticalID`, `soundInjuryCrowdID`, `soundDeathID`, `soundStepID[4]` (per foot), `soundAggroID`, `soundWingFlapID`, `soundWingGlideID`, `soundAlertID`, and others.

## Sound Kit Selection

When multiple files exist for a kit (fields 3–12), the engine randomly selects one
weighted by `freq[n]`: files with `freq[n] == 0` are skipped. Files with higher
`freq[n]` values are selected more often.

## OpenWarcraft3 Implementation

The sound module (`sound/s_sound.c`) loads `DBFilesClient\SoundEntries.dbc` at init
and builds a hash by kit name. Playback:
- `S_PlaySound(kit_id)` — play by numeric DBC ID
- `S_PlaySoundByName(name)` — play by kit name string (hash lookup)
- `S_PlaySoundFile(path)` — play raw MPQ path (for WC3 entity event sounds)

UI sounds triggered by Lua/FDF use `PlaySound(kit_id)` / `PlaySoundByName(name)`
through the `uiImport_t` function table.

Entity event sounds (unit attack, death) use `S_PlaySoundFile` via `CL_EntityEvent`.
