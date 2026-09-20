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

Starts a temporary explicit playlist. OpenRealm treats Warcraft's random flag as **random initial song only**: the first playable entry is chosen randomly, then the remaining entries advance sequentially from that point. The explicit list is one-pass; after every playable entry in that pass has completed, map music is started again from the stored map-list policy. This is a compatibility policy inferred from Blizzard's "random initial song" terminology plus mapper observations; direct retail measurement can still revise it.

```text
PlayMusicEx(name, frommsecs, fadeinmsecs)
```

Uses the same one-pass lifecycle, starts the initial track from `frommsecs`, and applies a linear fade from zero to the configured music volume over `fadeinmsecs`.

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

The client snapshots the interrupted track index and the audible playback position before replacing its decoder. The generic PCM stream counts frames actually consumed by the audio device, so the snapshot excludes decoded-but-not-yet-heard buffered PCM. A `music_snapshot` acknowledgement writes that position and selected track back into the persisted `GAMECLIENT` restore descriptor; `EndThematicMusic()`, natural thematic EOF, reconnect, and save/load therefore restore the same ordinary session at the best available audible position. Exact sample-level retail behavior is not claimed, because FFmpeg seeks may land on an earlier codec key/frame boundary.

## Playlist Behavior

The generic client stores at most 32 resolved paths for one active playlist. `random=true` affects only the initial selection. After the client reports the chosen track with `music_selected`, retained server state records that exact index and marks the current session sequential while leaving the stored map policy unchanged.

Persistent map music loops sequentially from that selected start point:

```text
playlist A B C D
random initial = C
C -> D -> A -> B -> C -> ...
```

Explicit `PlayMusic` / `PlayMusicEx` uses the same random-initial-then-sequential order but tracks which entries have already played. It does not replay a successful entry during the current pass; after the final playable entry, it restores map music. Missing/undecodable entries are skipped without consuming a successful-entry slot, so an `A(valid), B(missing), C(valid)` pass plays A/C once each rather than replaying A to compensate for B.

Thematic music is one-shot regardless of playlist length: the selected theme track restores the interrupted ordinary session at EOF rather than advancing.

These random-initial and explicit-one-pass rules are deliberate compatibility assumptions based on the strongest available Warcraft editor/community evidence, not a claim of direct retail instrumentation. They should be revised if a bounded retail test demonstrates different post-initial ordering.

When a selected path cannot be opened/decoded, the client scans the remaining playlist entries. An all-invalid map playlist becomes silent rather than indexing an empty array or terminating the map; later map replacement/clear acknowledges that silent session immediately because no EOF can arrive. An all-invalid explicit/theme request immediately completes its temporary lifecycle and restores the appropriate underlying/map state. No per-track debug logging is part of this path.

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
- volume;
- a monotonic consumed-frame counter reset by `S_StreamStart()`.

`S_StreamPlayedFrames()` is presentation timing, not simulation time. Music combines it with the last explicit seek/start millisecond to snapshot the position the audio device has actually consumed before thematic replacement.

The SDL callback mixes both streams before one-shot sound channels. Starting/stopping a movie stream therefore no longer resets music-buffer state.

Movies suspend music while they own full-screen presentation, then restore the previous music pause state when the movie ends or is skipped. See [pre-rendered-movies.md](pre-rendered-movies.md).

## Network Contract

`svc_music` is a reliable server-to-client presentation message. `musicCommand_t` is game-neutral and contains no Warcraft race/unit/spell identifiers.

Current payloads:

| Command | Payload after command byte |
|---|---|
| `MUSIC_CMD_SET_MAP` | `byte random`, `long index`, `long session_id`, `string playlist` |
| `MUSIC_CMD_CLEAR_MAP` | none |
| `MUSIC_CMD_PLAY` | `byte random`, `long index`, `long start_ms`, `long fade_ms`, `long played_mask`, `long session_id`, `string playlist` |
| `MUSIC_CMD_STOP` | `byte fade_out` |
| `MUSIC_CMD_RESUME` | none |
| `MUSIC_CMD_PLAY_THEMATIC` | `long index`, `long start_ms`, `long session_id`, `string playlist` |
| `MUSIC_CMD_END_THEMATIC` | none |
| `MUSIC_CMD_SET_VOLUME` | `long 0..127` |
| `MUSIC_CMD_SET_POSITION` | `long milliseconds` |
| `MUSIC_CMD_SET_THEMATIC_VOLUME` | `long 0..127` |
| `MUSIC_CMD_SET_THEMATIC_POSITION` | `long milliseconds` |

Each new map/explicit/thematic session receives a per-client nonzero `session_id`. Client lifecycle acknowledgements echo this opaque id instead of reconstructing identity from playlist text or selected indexes; this avoids false rejection when `Music.slk` comma lists normalize differently, a random initial song is not index 0, or an undecodable entry falls through to a later path. Reliable client commands are:

| Client command | Meaning |
|---|---|
| `music_selected <session> <index> <position_ms> <played_mask>` | selected/advanced to a playable track; updates retained exact index, one-pass progress, and start/seek position |
| `music_snapshot <theme_session> <restore_session> <index> <position_ms> <played_mask>` | theme has suspended an ordinary session at this audible position and one-pass progress |
| `music_finished <session>` | one-pass/theme/deferred-map session reached its lifecycle boundary |

The Warcraft game module resolves each recipient's skin before serialization. `GetLocalPlayer()`-scoped JASS uses `currentplayer`; global calls update/send each game-client slot independently. Reliable command ordering lets the client finish an old session, start the replacement locally, send `music_finished` for the old id, then `music_selected` for the replacement; the server commits the lifecycle transition before accepting the new selection. Stale ids are ignored after a newer session supersedes them.

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

## Compatibility Assumptions And Remaining Gaps

OpenRealm now implements three behaviors as explicit **best-evidence compatibility assumptions**, rather than leaving internally inconsistent placeholder behavior: `random=true` randomizes the initial song only and then proceeds sequentially; explicit `PlayMusic` is a one-pass override that returns to map music; and thematic music restores the interrupted track at its client-observed audible position. These choices are documented hypotheses and should change if direct retail measurement contradicts them.

The remaining unresolved areas are:

- `GetSoundFileDuration` still returns `0`; a synchronous JASS query cannot safely depend on a client-only decoder without a different ownership design.
- `StopMusic(true)` still pauses immediately because the exact retail fade duration has not been established. The low-confidence two-second estimate is deliberately **not** encoded as a constant.
- codec seeking is millisecond/stream-time based rather than sample-exact; the consumed-frame snapshot identifies what the mixer heard, but FFmpeg may decode from an earlier seek boundary.
- the continuously advancing ordinary playback head is reported when tracks are selected and when thematic music snapshots it, not every frame; a save taken during ordinary music can therefore resume from the last reported start/seek rather than the exact current millisecond.
- menu `GlueMusic` / `ChatMusic` and the options music checkbox/slider are not yet wired to this gameplay music controller.
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
3. Exercise `SetMapMusic(..., true, 0)` with at least four tracks; note the random initial entry, then confirm subsequent EOF transitions advance sequentially and wrap.
4. While a map track is playing, call `SetMapMusic` with a different list; the audible track should finish before the new list starts. Repeat with `ClearMapMusic`; the audible track should finish and then stop.
5. Start map music, call `PlayMusic` with a multi-track list, and let the explicit list complete; each playable explicit track should occur once in sequential order from the random initial entry, then map music should return.
6. Start explicit or map music, note the audible timestamp, play a thematic track, and let it reach EOF without calling `EndThematicMusic`; the interrupted track should return at approximately the same audible position.
7. Repeat the thematic test with an early `EndThematicMusic()` and with a `war3mapSkin.txt` `[CustomSkin]` music override.
8. Exercise `PlayMusicEx` with a nonzero start position and fade-in.
9. `StopMusic(false)` then `ResumeMusic()`; the same track should continue rather than select the next track.
10. Play a pre-rendered movie while music is active; movie audio should play alone and music should resume afterward.
11. Build without `FFMPEG=1`; maps should still run without a music-decoder/link dependency.
