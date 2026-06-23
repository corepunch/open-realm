# Sound Architecture

Based on Quake 2's sound system. Sound is a client-side subsystem with server-mediated triggering via configstrings and multicast messages.

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
