# Warcraft III Pre-Rendered Movies

## Contract

Classic Warcraft III campaign movies are client presentation, not simulation state. JASS chooses a logical movie with
`PlayCinematic("HumanEd")`; `games/warcraft-3/game/api/api_misc.h` converts that to the Warcraft asset convention
`Movies\\HumanEd.mpq` and calls the generic `game_import.QueueMovie` callback. The game module must not link FFmpeg or
decode video itself.

`QueueMovie` is a deferred-session interposer. If the same simulation frame later requests `ChangeLevel`, `EndGame`, or
another `gi.MenuAction`, `client/cl_main.c` freezes the outgoing server simulation, plays the queued movie, then performs
the preserved map/menu action after EOF or Escape. This avoids replacing the world while JASS is on the C stack and also
avoids advancing the outgoing map behind a full-screen movie.

`SetCinematicScene` / triggered dialogue are unrelated in-engine presentation. They do not use this movie path.

## Optional FFmpeg Backend

The default build has no FFmpeg dependency. Enable the movie decoder with:

```bash
make FFMPEG=1
```

The Warcraft III build then requires only these pkg-config packages:

```text
libavformat
libavcodec
libavutil
libswscale
libswresample
```

`client/cl_movie.c` owns demux/decode, A/V scheduling, Escape-to-skip, archive extraction, and full-screen presentation.
The renderer only exposes generic dynamic RGBA texture create/update operations. The sound mixer exposes a generic 44.1-kHz stereo S16 stream slot, following the same ownership split as Quake-style cinematic audio. Movie and music streams are independent; see [music.md](music.md).

The decoder first asks `FS_ResolveLoosePath()` for a real disk path. This lets libavformat stream a normal loose
`Movies\\*.mpq` file without loading the whole movie into memory. If the movie is found only inside an archive,
`FS_ExtractFile()` copies it to the per-user `openrealm-movie.tmp` file for the duration of playback and removes that file
when playback ends. Do not hold `FS_OpenFile()` open for a movie: the archive API's global file lock is intentionally
short-lived and would block unrelated asset reads.

The backend is format-driven. Do not special-case the `.mpq` filename extension in the decoder; libavformat probes the
actual media container. Warcraft's movie files use the historical movie naming convention even though the payload is
media rather than a normal game-data MPQ archive.

For direct diagnosis from the console:

```text
playmovie HumanEd
playmovie "Movies\\HumanEd.mpq"
```

The first form expands to `Movies\\HumanEd.mpq`. A build without `FFMPEG=1` keeps the command but reports that movie
playback is disabled.

## Renderer And Audio Flow

```text
PlayCinematic("HumanEd")
    -> gi.QueueMovie("Movies\\HumanEd.mpq")
    -> client defers pending MenuAction
    -> CL_PlayMovie
        -> libavformat
        -> libavcodec
        -> swscale -> RGBA -> renderer dynamic texture
        -> swresample -> stereo S16/44100 -> S_StreamSamples(S_STREAM_MOVIE)
    -> EOF or Escape
    -> resume preserved MenuAction
```

Movie rendering uses the renderer's actual UI scene rectangle and letterboxes/pillarboxes to preserve the source aspect
ratio. While active, movie input consumes keyboard/mouse interaction so hidden gameplay/menu controls cannot be
activated. Escape terminates playback. The system cursor is hidden while the movie is drawn. If background music is active, movie startup suspends it before the movie PCM stream starts; EOF/Escape restores the prior music pause state. The separate `S_STREAM_MOVIE` and `S_STREAM_MUSIC` buffers prevent either decoder from resetting the other.

## Campaign Selection Metadata

Warcraft campaign data has three movie-like fields in each campaign section:

```text
IntroCinematic
OpenCinematic
EndCinematic
```

The Warsmash reference parser (`CampaignMenuData`) reads each field with exactly the same three-column shape as a mission:

```text
header, display name, filename
```

and stores them separately from the numbered `Mission0`, `Mission1`, ... list. Current Warsmash parses these three fields
but its current mission-list construction iterates only `getMissions()`, so the reference code does not itself provide a
finished movie-selection implementation.

OpenRealm's `games/warcraft-3/menu/screens/single_player.c` currently parses `Mission*`/`File*` entries only. A retail-style
campaign movie list should therefore be implemented in that screen rather than as an unrelated top-level movie browser:

1. Extend `singlePlayerCampaign_t` with intro/open/end cinematic records and parse the three fields with the same quoted
   triple parser already used for modern `MissionN` entries.
2. Add cinematic rows to the mission-selection list using the Warcraft camera-button FDF art (`CampaignListBox` /
   `StandardCampaignCameraButton*`) instead of presenting them as ordinary map rows.
3. Bind those rows to the existing typed `menuImport.PlayMovie(path)` callback. The menu must not construct a console
   command to cross the client boundary.
4. Gate Open/End rows with the state written by `SetOpCinematicAvailable` / `SetEdCinematicAvailable`; those natives are
   still stubs, so unlock persistence must be implemented before claiming retail-compatible availability.
5. Keep mission-play visibility (`wc3_campaign_mission_visibility`) separate from cinematic availability. A played map
   and an unlocked cinematic are different progression facts.

The current patch intentionally adds the typed menu playback callback but does not invent unlock persistence or show all
cinematics unconditionally. That keeps the decoder usable now while leaving campaign selection semantics data-driven.

## Known Pitfalls

- `PlayCinematic` is not `SetCinematicScene`; one is pre-rendered movie playback, the other is in-engine dialogue/cinematic
  UI.
- Do not synchronously play or change maps from the JASS native. Both operations cross client/session boundaries only
  after `SV_Frame` returns.
- Do not load an entire movie with `FS_ReadFile()` on memory-constrained targets. Prefer a loose disk path or temporary
  extraction so FFmpeg can seek/stream normally.
- Do not expose FFmpeg types through `server/game.h`, `client/menu.h`, or renderer public structs. FFmpeg remains a client
  implementation detail under `BZ_FFMPEG`.
- The camera-selection rows are not safe to expose as "unlocked" until `SetOpCinematicAvailable` and
  `SetEdCinematicAvailable` have persistent state.

## Verification

The implementation is designed to be tested by the developer with original game data; automated tests should not depend
on local Warcraft movies. Useful manual cases are:

```bash
make FFMPEG=1
./build/bin/openwarcraft3 -data "data/Warcraft III" +playmovie HumanEd
```

Then verify:

- aspect ratio is preserved with black bars where necessary;
- audio is synchronized closely enough to dialogue/video and does not underrun;
- Escape skips immediately and the hidden menu/game does not receive the key;
- a campaign `PlayCinematic` followed by `ChangeLevel` plays the movie before loading the next map;
- a campaign `PlayCinematic` followed by `EndGame` plays the movie before rebuilding the frontend;
- a build made without `FFMPEG=1` has no FFmpeg link dependency and continues the deferred map/menu transition if movie
  playback is unavailable.
