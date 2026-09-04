# World of Warcraft (Classic) — Unit Sound System

## Sound Catalog: SoundEntries.dbc

All WoW sounds are indexed in `DBFilesClient\SoundEntries.dbc`. The 29-field classic layout and the per-creature
`CreatureSoundData.dbc` event slots are documented in [`docs/dbc-reference.md`](dbc-reference.md#sound-tables).

Full path = `directoryBase + "\" + file[n]`.

## Unit Sound Wiring

Units reference `SoundEntries` IDs through creature templates in the database (server-side) or through `CreatureSoundData.dbc` (client-side display). Key linking DBCs for classic:

| DBC | Role |
|-----|------|
| `CreatureSoundData.dbc` | Per-creature: maps sound event slots to `SoundEntries` IDs |
| `CreatureDisplayInfo.dbc` | Links creature display ID to `CreatureSoundData` record |
| `CreatureModelData.dbc` | Footstep sound set ID per model |

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
through the `menuImport_t` function table.

Entity event sounds (unit attack, death) use `S_PlaySoundFile` via `CL_EntityEvent`.
