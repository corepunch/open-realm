# Entity Sound Architecture

Unit sounds fire via a one-shot event mechanism mirroring Quake 2's `s.event` pattern:

1. **Server (game code):** At spawn, `G_RegisterUnitSounds` reads the unit's `usnd` label from `unitUI.slk`, looks it up in `UI/SoundInfo/UnitAckSounds.slk`, and calls `gi.SoundIndex(path)` to register the file path as a configstring. The returned index is cached in `edict->sound_attack` / `edict->sound_death`.
2. **Trigger:** On attack swing, `s.event = EV_ATTACK; s.sound = sound_attack`. On death, `s.event = EV_DEATH; s.sound = sound_death`. Both are cleared to zero at the start of every `G_RunEntities` frame so they fire for exactly one snapshot.
3. **Client:** `CL_ReadPacketEntities` calls `CL_EntityEvent(ent)` when `ent->event != 0`. It resolves `cl.configstrings[CS_SOUNDS + ent->sound]` to a file path and calls `S_PlaySoundFile(path)`.

## Key Files

| File | Role |
|------|------|
| `common/shared.h` | `entity_event_t` enum (`EV_NONE`, `EV_ATTACK`, `EV_DEATH`, `EV_MOVE`) |
| `games/warcraft-3/game/g_monster.c` | `G_RegisterUnitSounds` — spawns sound indices from SLK |
| `games/warcraft-3/game/m_unit.c` | `unit_die` — sets `EV_DEATH` event |
| `games/warcraft-3/game/skills/s_attack.c` | `attack_melee`/`attack_ranged` — sets `EV_ATTACK` event |
| `games/warcraft-3/game/g_events.c` | `G_RunEntities` — clears `s.event`/`s.sound` each frame |
| `client/cl_fx.c` | `CL_EntityEvent` — maps event to `S_PlaySoundFile` |
| `client/cl_parse.c` | Calls `CL_EntityEvent` after each entity delta |
| `sound/s_sound.c` | `S_PlaySoundFile(path)` — raw MPQ path playback |

## WC3 Sound Table Investigation

- `UI/SoundInfo/UnitAckSounds.slk` — columns: row key = `{label}{Suffix}`, `FileNames` (comma-separated), `DirectoryBase`.
- `UI/SoundInfo/UnitCombatSounds.slk` — same schema; combat impact sounds by weapon/armor type.
- Unit label: `unitUI.slk` column `unitSound` (field key `"usnd"`), e.g. `"Footman"`.
- Death sounds are raw files: `{modelDir}\{ModelName}Death.wav` (not in the SLK).
- Use `build/bin/mpqtool` to inspect: `cat "UI/SoundInfo/UnitAckSounds.slk"` and grep for the label.

## Per-Game Sound Documentation

- `doc/games/warcraft-3/docs/sounds.md`
- `doc/games/starcraft-2/docs/sounds.md`
- `doc/games/world-of-warcraft/docs/sounds.md`
