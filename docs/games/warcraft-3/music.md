# Warcraft III Music Playback

## Contract

Warcraft music is long-form client presentation, not an ordinary `sound` handle and not positional world audio. The Warcraft game module owns JASS semantics and Warcraft data lookup; the generic client owns playlist playback; the sound mixer owns decoded PCM streams.

```text
war3map.j
    -> SetMapMusic / PlayMusic / thematic natives
    -> games/warcraft-3/game/g_music.c
         -> per-recipient war3skins.txt lookup
         -> Music.slk alias expansion
         -> reliable svc_music
    -> client/cl_parse.c
    -> client/cl_music.c
         -> playlist state
         -> optional FFmpeg decode
         -> S_STREAM_MUSIC
    -> sound/s_sound.c
    -> SDL audio device
```

Do not route background music through `svc_sound`, `S_PlaySoundFile`, unit channels, or a world entity. The one-shot WC3 sound path remains WAV-oriented and has different ownership/lifetime semantics.

## Build Modes

The default build keeps FFmpeg optional. Music commands and state still exist without FFmpeg, but no compressed music decoder is available and playback remains silent.

Enable the decoder with:

```bash
make FFMPEG=1
```

The Warcraft III build already uses these pkg-config libraries for pre-rendered movies and now reuses them for music:

```text
libavformat
libavcodec
libavutil
libswscale
libswresample
```

`client/cl_music.c` only requires audio demux/decode/resampling; `libswscale` remains part of the shared Warcraft FFmpeg build because movies need it.

## Warcraft Data Lookup

### `war3skins.txt`

JASS usually refers to a logical skin name such as:

```jass
call SetMapMusic("Music", true, 0)
```

`Theme_PlayerString()` resolves that value for the represented local player:

1. map-authored `war3mapSkin.txt` `[CustomSkin]` override;
2. race section (`Human`, `Orc`, `Undead`, `NightElf`);
3. `Default` section fallback;
4. versioned `<field>_V0` for RoC or `<field>_V1` for TFT at the same override/base layer;
5. original JASS string as fallback.

The map skin is loaded after the map archive is mounted, so Game Interface overrides such as `Music_V0` / `Music_V1` win over stock `war3skins.txt`. This version fallback matches the existing menu theme resolver instead of making gameplay music invent a second skin policy.

Direct Warcraft paths containing `\\` bypass skin lookup and remain paths.

### `UI\\SoundInfo\\Music.slk`

Retail never shipped `Music.slk` in either MPQ, so the typed loader marks it optional: a missing file stays silent with zero rows, and `G_MusicData` keeps returning its static zero row. `G_MusicResolvePlaylist` (`g_music.c:30-54`) then falls back to the raw skin token as a direct path. The fixture MPQ ships a `Music.slk` with a `TestMusic` row so playlist alias expansion stays covered.

The real per-edition playlist comes from the skin layer, not the SLK. `Theme_PlayerString` first checks map `war3mapSkin.txt` `[CustomSkin]`, then the recipient's stock race/`Default` sections and their versioned `<field>_V0` (RoC) / `<field>_V1` (TFT) aliases. Only the resolved token is checked against `Music.slk` row names before `FileNames` expansion.

`Music.slk` is loaded as typed Warcraft metadata. Only two fields are needed by the current playback contract:

```text
row key
FileNames
```

For each semicolon-delimited skin/JASS token, `g_music.c` checks whether the token is a `Music.slk` row. If so, it substitutes `FileNames`; otherwise the token is retained as a direct path. `FileNames` may itself contain comma-delimited tracks.

The server sends the resulting path/playlist string to the correct client. The generic client then splits both `;` and `,` separators and never needs to understand Warcraft skins or SLKs.

## Startup And `ClientBegin`

`G_LoadMap()` starts `war3map.j main()` before the network client has necessarily completed `ClientBegin`. A packet-only music implementation therefore loses standard startup calls such as `SetMapMusic("Music", true, 0)`.

Music semantics are retained per Warcraft `GAMECLIENT` in `wc3MusicState_t` even while that slot is disconnected. `G_ClientBegin()` calls `G_MusicSyncClient()` after the slot becomes presentation-safe. The sync sends:

- ordinary music volume;
- thematic music volume;
- stored map music;
- any explicit/thematic current session that supersedes map music;
- paused state where applicable.

This is the same general rule used by other server-authored presentation state: JASS may mutate state before a client can receive layout/presentation packets, so the state must not exist only in the outgoing message buffer.

## JASS Semantics Implemented

### Map music

```text
SetMapMusic(name, random, index)
```

Stores the default map playlist. If no music is currently active, it also becomes the active source, which makes generated map startup music audible after `ClientBegin`. If a map-music track is already audible, changing the map list does **not** cut that track off: the client retains the current list until that track reaches EOF, then starts the newly configured map list using its requested random/index policy.

```text
ClearMapMusic()
```

Clears the stored default playlist. It does not forcibly destroy an already audible current map track; that track is allowed to finish, then EOF becomes silence instead of advancing the old list.

### Explicit music

```text
PlayMusic(name)
```

Starts an explicit playlist and currently uses random selection for a multi-track value, matching the Warsmash reference implementation.

```text
PlayMusicEx(name, frommsecs, fadeinmsecs)
```

Starts from `frommsecs` and applies a linear fade from zero to the configured music volume over `fadeinmsecs`.

### Stop / resume

```text
StopMusic(fadeOut)
ResumeMusic()
```

`StopMusic` currently pauses the stream immediately while preserving decoder position and buffered data. `ResumeMusic` resumes the same track rather than advancing the playlist.

The `fadeOut` boolean is intentionally accepted but not assigned a guessed duration. The JASS API provides no fade duration, current Warsmash ignores the flag, and exact retail timing has not yet been measured.

### Position and volume

```text
SetMusicVolume(0..127)
SetMusicPlayPosition(milliseconds)
```

Volume maps linearly to `0.0 .. 1.0` at the music stream. Seeking uses FFmpeg's stream timestamp seek and resets decoder/resampler/PCM-buffer state before refill.

### Thematic music

```text
PlayThematicMusic(name)
PlayThematicMusicEx(name, frommsecs)
EndThematicMusic()
SetThematicMusicVolume(0..127)
SetThematicMusicPlayPosition(milliseconds)
```

Thematic music uses the one physical music stream but separate logical source/volume state. It is a temporary **one-shot** overlay:

- beginning thematic music snapshots the ordinary map/explicit session that it interrupts;
- thematic EOF automatically restores that session instead of advancing/looping the thematic playlist;
- `EndThematicMusic()` performs the same restoration early;
- a `SetMapMusic` / `ClearMapMusic` change made while an interrupted map track is underneath the theme remains pending and takes effect when that restored map track later reaches EOF.

The retained game state also preserves the interrupted session across save/load and client synchronization. Exact live decoder position is still not sampled continuously, so restoration reopens the interrupted selected track rather than promising sample-accurate continuation from the instant the theme began.

## Playlist Behavior

The generic client stores at most 32 resolved paths for one active playlist.

Sequential map/explicit mode:

```text
A -> B -> C -> A -> ...
```

Random mode independently chooses a track each time, so the same track may be chosen twice consecutively. This matches current Warsmash behavior; retail random/shuffle semantics after the initial selection remain a verification item. Thematic music is different: it is one-shot and restores the interrupted ordinary session at EOF rather than advancing its playlist.

When a selected path cannot be opened/decoded, the client scans the remaining playlist entries. An all-invalid playlist becomes silent rather than indexing an empty array or terminating the map. No per-track debug logging is part of this path.

## Optional FFmpeg Decoder

`client/cl_music.c` follows the proven movie-audio pattern:

1. ask `FS_ResolveLoosePath()` for a normal disk path;
2. otherwise extract the virtual Warcraft asset to `openrealm-music.tmp` under the user path;
3. `avformat_open_input()` and locate the best audio stream;
4. open the codec with libavcodec;
5. resample incrementally to stereo S16 / 44.1 kHz with libswresample;
6. queue PCM into `S_STREAM_MUSIC`;
7. detect decoder EOF and advance the playlist after queued PCM drains.

The decoder does not load the full song into RAM. It targets roughly half a second of queued PCM while the mixer stream itself has a two-second capacity.

A future `AVIOContext` backed directly by the virtual filesystem could remove temporary extraction, but that is not required for correct first-pass playback.

## Generic PCM Streams

The former singleton movie `S_Raw*` buffer is now a generic pair of long-form PCM streams:

```text
S_STREAM_MOVIE
S_STREAM_MUSIC
```

Each stream independently owns:

- ring buffer;
- active flag;
- pause flag;
- volume.

The SDL callback mixes both streams before one-shot sound channels. Starting/stopping a movie stream therefore no longer resets music-buffer state.

Movies suspend music while they own full-screen presentation, then restore the previous music pause state when the movie ends or is skipped. See [pre-rendered-movies.md](pre-rendered-movies.md).

## Network Contract

`svc_music` is a reliable server-to-client presentation message. `musicCommand_t` is game-neutral and contains no Warcraft race/unit/spell identifiers.

Current payloads:

| Command | Payload after command byte |
|---|---|
| `MUSIC_CMD_SET_MAP` | `byte random`, `long index`, `string playlist` |
| `MUSIC_CMD_CLEAR_MAP` | none |
| `MUSIC_CMD_PLAY` | `long start_ms`, `long fade_ms`, `string playlist` |
| `MUSIC_CMD_STOP` | `byte fade_out` |
| `MUSIC_CMD_RESUME` | none |
| `MUSIC_CMD_PLAY_THEMATIC` | `long start_ms`, `string playlist` |
| `MUSIC_CMD_END_THEMATIC` | none |
| `MUSIC_CMD_SET_VOLUME` | `long 0..127` |
| `MUSIC_CMD_SET_POSITION` | `long milliseconds` |
| `MUSIC_CMD_SET_THEMATIC_VOLUME` | `long 0..127` |
| `MUSIC_CMD_SET_THEMATIC_POSITION` | `long milliseconds` |

The Warcraft game module resolves each recipient's skin before serialization. `GetLocalPlayer()`-scoped JASS uses `currentplayer`; global calls update/send each game-client slot independently. When a one-shot thematic track reaches natural EOF, the client sends the internal `music_theme_end` string command so the retained server-side presentation state restores the same ordinary session and save/reconnect synchronization does not resurrect an already-finished theme. When a deferred `SetMapMusic` or `ClearMapMusic` finally takes effect after the old map track reaches EOF, the client likewise sends `music_map_commit` so retained state advances to the new map list (or silence) at the same moment.

## Important Files

| File | Role |
|---|---|
| `games/warcraft-3/game/g_music.c` | Warcraft JASS music state, skin/SLK resolution, per-recipient sync |
| `games/warcraft-3/game/g_commands.c` | internal client acknowledgements for natural music state transitions |
| `games/warcraft-3/game/api/api_sound.h` | Music native implementations |
| `games/warcraft-3/game/g_metadata.c` | Typed `Music.slk` loading |
| `games/warcraft-3/game/hud/hud_write.c` | map override plus race/default/versioned skin lookup |
| `common/shared.h` | generic `musicCommand_t` presentation commands |
| `common/common.h` | `svc_music` network opcode |
| `client/cl_parse.c` | `svc_music` decode |
| `client/cl_music.c` | playlist, optional FFmpeg decode, seek/fade/EOF handling |
| `sound/s_local.h`, `sound/s_sound.c` | independent long-form PCM stream mixer |
| `client/cl_movie.c` | movie/music suspend interaction |

## Known Gaps / Do Not Guess

The current implementation deliberately leaves these unresolved rather than assigning unverified Warcraft semantics:

- `GetSoundFileDuration` still returns `0`; a synchronous JASS query cannot safely depend on a client-only decoder without a different ownership design.
- `StopMusic(true)` still pauses immediately because the exact retail fade duration has not been established.
- `SetMapMusic`'s index is treated as a zero-based initial-song index; exact retail random-vs-shuffle behavior *after* the initial selection still needs observation.
- thematic restoration preserves the interrupted logical session and JASS-known selection/seek state, but not the continuously advancing decoder timestamp or a client-randomized track choice that was never reported to the server.
- whether ordinary `PlayMusic` should itself be one-shot and fall back to map music after its supplied list finishes still needs direct retail verification.
- menu `GlueMusic` / `ChatMusic` and the options music checkbox/slider are not yet wired to this gameplay music controller.
- semantic per-client music fields are part of the raw `GAMECLIENT` save snapshot and are re-sent to clients after load, but the continuously advancing decoder playback head is not synchronized back to the server; a restored save can only reuse the last JASS-requested start/seek position.
- builds without `FFMPEG=1` have no fallback MP3 decoder.

## Verification

No automated or local compile/run validation is implied by this document. With original game data, developer verification should cover both RoC and TFT builds.

Build with music decoding:

```bash
make FFMPEG=1
```

Useful behavioral cases:

1. Start a Human campaign map whose generated script calls `SetMapMusic("Music", true, 0)`; music should begin after connection without requiring a later trigger.
2. Repeat with another race and confirm the playlist follows that player's `war3skins.txt` section.
3. Exercise sequential `SetMapMusic(..., false, index)` and let at least one track reach EOF.
4. While a map track is playing, call `SetMapMusic` with a different list; the audible track should finish before the new list starts. Repeat with `ClearMapMusic`; the audible track should finish and then stop.
5. Start explicit or map music, play a thematic track, and let it reach EOF without calling `EndThematicMusic`; the interrupted ordinary session should return automatically.
6. Repeat the thematic test with an early `EndThematicMusic()` and with a `war3mapSkin.txt` `[CustomSkin]` music override.
7. Exercise `PlayMusicEx` with a nonzero start position and fade-in.
8. `StopMusic(false)` then `ResumeMusic()`; the same track should continue rather than select the next track.
9. Play a pre-rendered movie while music is active; movie audio should play alone and music should resume afterward.
10. Build without `FFMPEG=1`; maps should still run without a music-decoder/link dependency.
