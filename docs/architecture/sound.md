# Sound Architecture

See also: [Warcraft III — Unit Sound System](../games/warcraft-3/sounds.md).

Based on Quake 2's sound system. Sound is a client-side subsystem with server-mediated triggering via configstrings and
dedicated `svc_sound` packets.

## Entity Sound Events (One-Shot)

Unit sounds (attack, death, movement) are delivered through `svc_sound`, independently of entity snapshot deltas. Entity
sources carry an entity/channel pair; explicitly positioned sounds carry a packed world origin. Sounds without either
source are non-positional and are still delivered by the server packet path.

### Protocol

1. **Server game code** registers a WAV file path via `gi.SoundIndex(path)` → sound index.
2. `gi.Sound` or `gi.PositionedSound` calls the server import boundary with entity/channel, volume, attenuation, and offset.
3. `SV_StartSound` encodes the Quake 2 packet flags and sends `svc_sound` to the selected clients.
4. **Client** (`cl_parse.c`) decodes the packet, resolves configstring path and entity-relative origin, then calls `S_PlaySoundPacket`.
5. `S_PlaySoundPacket` loads the WAV from the MPQ and mixes it with the packet's volume and attenuation.

### Legacy Entity Event Types (`entity_event_t` in `common/shared.h`)

| Value | Name | Trigger |
|-------|------|---------|
| 0 | `EV_NONE` | No event |
| 1 | `EV_ATTACK` | Attack swing began |
| 2 | `EV_DEATH` | Unit died |
| 3 | `EV_MOVE` | Footstep / movement sound |

`CHAN_OWNER` is a sound-channel delivery bit, not an entity event; it is stripped before the packet is sent.

### WC3 Sound Registration

At map load, `G_RegisterUnitSounds` reads the unit's `usnd` label from `unitUI.slk` and registers authored `What`, `Yes`, `Ready`, `YesAttack`, and death assets. WC3 also loads `UnitCombatSounds.slk` and `UISounds.slk`; construction-complete and command-error sounds resolve through the local player's `war3skins.txt` fields into `UISounds.slk`. See `docs/games/warcraft-3/sounds.md` for the full lookup chains and current gaps.

WC3 acknowledgements and ready sounds use `CHAN_OWNER | CHAN_RELIABLE`. When game code passes the connected client's own edict (for example local UI, dialogue, or minimap presentation), the server resolves that exact edict to the connection first; it must not assume the game's Warcraft player number equals the engine client slot. For ordinary unit-source owner sounds, it falls back to the entity's player ownership. World events such as attacks, death, and tree impacts use ordinary entity-relative `gi.Sound` calls.

### Key Files

| File | Role |
|------|------|
| `games/warcraft-3/game/g_monster.c` | `G_RegisterUnitSounds` — sound index registration at spawn |
| `games/warcraft-3/game/g_sound.c` | WC3 `UISounds.slk`, owner-only sounds, and command-error sound dispatch |
| `games/warcraft-3/game/g_events.c` | `G_RunEntities` — clears `s.event`/`s.sound` each frame |
| `client/cl_fx.c` | `CL_EntityEvent` — fires sounds on event |
| `sound/s_sound.c` | `S_PlaySoundFile` — raw MPQ path playback |



## Assets

Standard mono WAV files stored under `sound/` relative to the game search path. Names are relative paths (e.g. `"infantry/infpain1.wav"` resolves to `sound/infantry/infpain1.wav`).

**Sexed sounds** use a `*` prefix (e.g. `"*pain25_1.wav"`) and are resolved per player model at play time to `#players/<model>/<base>`, falling back to `player/male/<base>`.

## Registration (two-tier)

### Server side

`SV_SoundIndex(name)` (`server/sv_init.c`) assigns a 1-based index and stores the name in `sv.configstrings[CS_SOUNDS + i]`. The game DLL calls this through `gi.soundindex(name)`.

### Client side

`CL_RegisterSounds()` (`client/cl_parse.c`) iterates all `CS_SOUNDS` configstrings and calls `S_RegisterSound()` for each. This creates an `sfx_t` record but **does not load WAV data** until the sound is actually played (lazy loading).

## Playing a sound

Game code calls:

```c
gi.sound(entity, channel, soundindex, volume, attenuation, time_offset);
```

### Pipeline

| Step | Function | File | Description |
|------|----------|------|-------------|
| 1 | `gi.sound()` | game DLL | Game initiates the sound |
| 2 | `PF_StartSound()` | `server/sv_game.c` | Wraps entity, calls `SV_StartSound` |
| 3 | `SV_StartSound()` | `server/sv_send.c` | Encodes `svc_sound`, multicasts via PHS |
| 4 | `CL_ParseStartSoundPacket()` | `client/cl_parse.c` | Client reads the network message |
| 5 | `S_StartSound()` | `client/snd_dma.c` | Validates, loads WAV if needed, creates `playsound_t`, inserts into sorted pending queue |
| 6 | `S_PaintChannels()` | `client/snd_mix.c` | Mixer picks up playsounds when their time arrives |
| 7 | `S_IssuePlaysound()` | `client/snd_dma.c` | Picks channel, spatializes, assigns to `channel_t` |
| 8 | `S_PaintChannelFrom8/16()` | `client/snd_mix.c` | Mixes PCM samples into paintbuffer with volume scaling |
| 9 | `S_TransferPaintBuffer()` | `client/snd_mix.c` | Writes mixed samples into DMA output buffer |
| 10 | `SNDDMA_Submit()` | `win32/snd_win.c` / `linux/snd_linux.c` | Submits DMA buffer to audio driver |

## Pain / Hurt sounds

### Player pain

Triggered in `P_DamageFeedback()` (`game/p_view.c:132`), not in `player_pain()` (which is empty). Called at end of each frame:

```c
r = 1 + (rand()&1);
player->pain_debounce_time = level.time + 0.7;  // 0.7 sec throttle

if (player->health < 25)      l = 25;
else if (player->health < 50) l = 50;
else if (player->health < 75) l = 75;
else                          l = 100;

gi.sound(player, CHAN_VOICE,
         gi.soundindex(va("*pain%i_%i.wav", l, r)),  // e.g. "*pain50_2.wav"
         1, ATTN_NORM, 0);
```

Pre-registered at map load in `game/g_spawn.c`:

```c
gi.soundindex("*pain25_1.wav");  gi.soundindex("*pain25_2.wav");
gi.soundindex("*pain50_1.wav");  gi.soundindex("*pain50_2.wav");
gi.soundindex("*pain75_1.wav");  gi.soundindex("*pain75_2.wav");
gi.soundindex("*pain100_1.wav"); gi.soundindex("*pain100_2.wav");
```

### Monster pain

Each `game/m_*.c` file caches pain sound indices at spawn and plays them from AI pain frames:

```c
// Registration (spawn)
sound_pain1 = gi.soundindex("infantry/infpain1.wav");
sound_pain2 = gi.soundindex("infantry/infpain2.wav");

// Trigger (pain AI frame)
gi.sound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
```

## Key structs

| Struct | File | Purpose |
|--------|------|---------|
| `sfx_t` | `client/snd_loc.h` | Sound asset record: name, registration sequence, cache pointer |
| `sfxcache_t` | `client/snd_loc.h` | Loaded/resampled PCM data: length, loopstart, speed, width, trailing sample data |
| `playsound_t` | `client/snd_loc.h` | Pending sound in the queue: sfx, volume, attenuation, entity, origin, begin time |
| `channel_t` | `client/snd_loc.h` | Active mixing channel: sfx, left/right volume, end time, entity info |
| `wavinfo_t` | `client/snd_mem.c` | Parsed WAV header: rate, width, stereo, loopstart, numframes |

## Design principles

- **Lazy loading:** WAV data is not loaded until the sound is first played or during `S_EndRegistration` cleanup.
- **Playsound queue:** `S_StartSound` does not play immediately. It creates a `playsound_t` sorted by time into `s_pendingplays`. The mixer picks them up when their time arrives.
- **32 mixing channels** (`MAX_CHANNELS`). `S_PickChannel` uses priority: same entity+channel always overrides; monster sounds never override player sounds; otherwise the channel with least time remaining is evicted.
- **Spatialization:** Every frame, `S_Update` re-spatializes all active channels based on listener position. Sounds from the player entity always play at full volume.
- **Configstring indexing:** Game code uses integer indices; server stores names in configstrings; client resolves indices to `sfx_t` pointers via `cl.sound_precache[]`.

## Key files

| File | Role |
|------|------|
| `client/sound.h` | Public API: `S_Init`, `S_StartSound`, `S_RegisterSound`, etc. |
| `client/snd_loc.h` | Private structs: `sfx_t`, `sfxcache_t`, `playsound_t`, `channel_t` |
| `client/snd_dma.c` | Core manager: registration, `S_StartSound`, channel picking, spatialization |
| `client/snd_mem.c` | WAV loading and caching: `S_LoadSound`, `GetWavinfo`, `ResampleSfx` |
| `client/snd_mix.c` | PCM mixing: `S_PaintChannels`, `S_PaintChannelFrom8/16` |
| `client/cl_parse.c` | Client network: `CL_ParseStartSoundPacket`, `CL_RegisterSounds` |
| `server/sv_send.c` | Server: `SV_StartSound` — encodes and multicasts sound events |
| `server/sv_init.c` | `SV_SoundIndex` — configstring index assignment |
| `server/sv_game.c` | `PF_StartSound` + import table wiring (`gi.sound`, `gi.soundindex`) |
| `game/game.h` | `game_import_t`: `gi.sound`, `gi.soundindex`, `gi.positioned_sound` |
| `game/p_view.c` | Player pain sound trigger: `P_DamageFeedback()` |
| `game/m_*.c` | Monster pain sounds (each monster file) |
