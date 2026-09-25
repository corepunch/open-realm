# Sound Architecture

See also: [Warcraft III — Unit Sound System](../games/warcraft-3/sounds.md) and
[Warcraft III — Music Playback](../games/warcraft-3/music.md).

Based on Quake 2's sound system. Sound is a client-side subsystem with server-mediated triggering via configstrings and
dedicated `svc_sound` packets.

One-shot sounds load mono PCM WAV or MP3 files from the virtual filesystem into the same S16 / 44.1-kHz mono cache.
`sound/s_sound.c` keeps WAV loop markers; `sound/s_mp3.c` uses vendored minimp3, downmixes stereo frames, and resamples
MP3 dialogue without an FFmpeg build dependency. A failed decode is remembered for the current sound registration
sequence, so a bad loop/sample is not read and diagnosed again every frame.
The vendored Huffman bit reader bounds reads at the main-data buffer end; preserve this guard when updating minimp3
because the upstream report [#145](https://github.com/lieff/minimp3/issues/145) identifies the same unchecked reads.


## Long-Form PCM Streams

Movies and background music use client-owned stereo S16 / 44.1-kHz ring buffers rather than one-shot `sfx_t` channels. `sound/s_local.h` defines generic `S_STREAM_MOVIE` and `S_STREAM_MUSIC` slots; each has independent active, pause, volume, buffer state, and a consumed-frame counter reset by `S_StreamStart()`. `S_StreamPlayedFrames()` exposes that counter under the audio-device lock so presentation code can snapshot the amount actually heard without treating decoded-but-buffered PCM as elapsed playback. The SDL callback mixes both streams before ordinary SFX.

WC3 music is transported separately with reliable `svc_music`: game-specific code resolves `war3skins.txt` and `Music.slk`, while `client/cl_music.c` owns playlist and optional FFmpeg decoding. Movies use `S_STREAM_MOVIE` and temporarily suspend `S_STREAM_MUSIC` without resetting its decoder/buffer. Keep new long-form sources generic at the `client/`/`sound/` boundary; game-specific aliases and metadata stay under `games/<game>/`.

## Entity Sound Events (One-Shot)

Unit sounds (attack, death, movement) are delivered through `svc_sound`, independently of entity snapshot deltas. Entity
sources carry an entity/channel pair; explicitly positioned sounds carry a packed world origin. Sounds without either
source are non-positional and are still delivered by the server packet path.

### Protocol

1. **Server game code** registers a WAV or MP3 file path via `gi.SoundIndex(path)` → sound index.
2. `gi.Sound` or `gi.PositionedSound` calls the server import boundary with entity/channel, volume, attenuation, and offset.
3. `SV_StartSound` encodes the Quake 2 packet flags and sends `svc_sound` to the selected clients.
4. **Client** (`cl_parse.c`) decodes the packet, resolves configstring path and entity-relative origin, then calls `S_PlaySoundPacket`.
5. `S_PlaySoundPacket` loads and normalizes the WAV or MP3 from the MPQ, then mixes it with the packet's volume and attenuation.

### Legacy Entity Event Types (`entity_event_t` in `common/shared.h`)

| Value | Name | Trigger |
|-------|------|---------|
| 0 | `EV_NONE` | No event |
| 1 | `EV_ATTACK` | Attack swing began |
| 2 | `EV_DEATH` | Unit died |
| 3 | `EV_MOVE` | Footstep / movement sound |

`CHAN_OWNER` is a sound-channel delivery bit, not an entity event; it is stripped before the packet is sent.

### WC3 Sound Registration

At map load, `G_RegisterUnitSounds` reads the unit's `usnd` label from `unitUI.slk` and caches authored `What`, `Yes`, `Ready`, and death assets. `YesAttack` and `Pissed` are selected from `UnitAckSounds.slk` at the interaction that owns them rather than being treated as weapon-swing sounds. WC3 also loads `UnitCombatSounds.slk`, `UISounds.slk`, and optional `AmbienceSounds.slk`, `AbilitySounds.slk`, `AnimSounds.slk`, and `DialogSounds.slk`. JASS sound-label constructors resolve those catalogs plus UnitAck/UnitCombat labels; SLK-backed one-shots retain authored row volume. Construction `BuildingSoundLabel` uses the generic snapshot-synchronised loop channel, construction-complete and command-error sounds resolve through the local player's `war3skins.txt` fields, and basic attack impacts combine the attacker's weapon-sound class with the target armor material. See `games/warcraft-3/sounds.md` for the full lookup chains and current gaps.

WC3 acknowledgements and ready sounds use `CHAN_OWNER | CHAN_RELIABLE`. When game code passes the connected client's own edict (for example local UI, dialogue, or minimap presentation), the server resolves that exact edict to the connection first; it must not assume the game's Warcraft player number equals the engine client slot. For ordinary unit-source owner sounds, it falls back to the entity's player ownership. World events such as attacks, death, and tree impacts use ordinary entity-relative `gi.Sound` calls.

Owner matching uses `client->edict->client->ps.number`, the game-published identity used by snapshots, rather than
the server's lobby `playernum`. A spawned connection without a bound game client cannot receive owner-routed
world-entity sounds. The packed entity/channel field is unsigned on decode; its high bit belongs to the entity
number, not a sign. See the [unit-sound regression notes](../games/warcraft-3/sounds.md#unit-acknowledgement-and-completion-sounds).

### Key Files

| File | Role |
|------|------|
| `games/warcraft-3/game/g_monster.c` | `G_RegisterUnitSounds` — sound index registration at spawn |
| `games/warcraft-3/game/g_sound.c` | WC3 keyed sound tables, owner-only UI sounds, ability/effect and combat-impact sounds, JASS label resolution, and command-error dispatch |
| `client/cl_view.c` | reconciles persistent snapshot `entityState_t.sound` loops by entity number |
| `sound/s_sound.c` | one-shot packet playback plus generic persistent loop mixing |
| `sound/s_mp3.c` | MP3 frame decoding and conversion to the one-shot mono PCM cache |
| `client/cl_fx.c` | `CL_EntityEvent` — fires sounds on event |
| `sound/s_sound.c` | `S_PlaySoundFile` — raw MPQ path playback |



## Snapshot-Synchronised Looping Entity Sounds

Persistent world effects use ordinary `entityState_t.sound` rather than repeatedly emitting `svc_sound`. During scene construction, `CL_AddEntities` begins a loop-generation pass, reconciles every active entity carrying a sound configstring through `S_UpdateLoopingSound`, then retires loop channels that were not seen in the current snapshot. This makes start, movement, sound-alias replacement, entity removal, channel interruption, and save/load convergence follow authoritative snapshots without WC3-specific state in the mixer.

The generic mixer owns only the resolved sound path, source entity number, current XY origin, attenuation, and generation. When the sample reaches its end, a persistent channel returns to the WAV cue `loopstart` when valid and otherwise loops from frame zero. Game-specific code remains responsible for resolving authored aliases into a server sound configstring and putting that index on the presenting entity. WC3 currently uses this for `BuildingSoundLabel` construction loops and for effect-owned `Effectsoundlooped` paths.

This path is intentionally separate from one-shot `svc_sound`: an ability may play `Effectsound` once at cast/effect start while a persistent area-effect entity carries `Effectsoundlooped` until that entity disappears or clears `s.sound`.

## Renderer-authored one-shot sounds

Model animation sounds are client presentation, not server simulation state. The shared renderer contract exposes a generic positional one-shot callback (`refImport_t.PlaySoundAt`); the Warcraft III renderer resolves MDX `SND` events through `AnimLookups.slk` / `AnimSounds.slk` and invokes that callback when an animation crosses the authored event key. The shared client and mixer do not know WC3 event names or SLK keys.

WC3 evaluates these event tracks before frustum culling so a client-visible model may still be audible while off-screen. The renderer owns event-key de-duplication per entity/model and resolves the animated event node to world space through the existing MDX node matrices. This local path currently passes authored volume; pitch variance and authored panner distances require a future generic mixer/API extension rather than WC3-specific client branches.


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

For owner-only/player-local presentation, the game must only issue the sound when the target player has a connected client. Warcraft III JASS loops may set a local-player context for all authored player slots; disconnected slots are simulation identities, not network recipients, and must be skipped before `gi.sound()`/`gi.positioned_sound()`.

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
