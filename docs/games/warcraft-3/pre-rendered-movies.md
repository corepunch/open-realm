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
The renderer only exposes generic dynamic RGBA texture create/update operations. The sound mixer exposes a generic
44.1-kHz stereo S16 raw stream, following the same ownership split as Quake-style cinematic audio.

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
        -> swresample -> stereo S16/44100 -> S_RawSamples
    -> EOF or Escape
    -> resume preserved MenuAction
```

Movie rendering uses the renderer's actual UI scene rectangle and letterboxes/pillarboxes to preserve the source aspect
ratio. While active, movie input consumes keyboard/mouse interaction so hidden gameplay/menu controls cannot be
activated. Escape terminates playback. The system cursor is hidden while the movie is drawn.

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

OpenRealm's `games/warcraft-3/menu/screens/single_player.c` now parses those three records into separate campaign cinematic
slots. As a first integration step, builds compiled with `FFMPEG=1` add each authored cinematic to the existing mission
`MapListBox`. The temporary rows follow the campaign structure rather than the declaration order in the data: `IntroCinematic`
and `OpenCinematic` precede the numbered missions, while `EndCinematic` follows the final mission. The temporary diagnostic
presentation is deliberately plain text:

```text
Cinematic: <header>: <display name>
```

The menu normalizes logical movie names such as `HumanEd` to `Movies\HumanEd.mpq` and invokes the existing typed
`menuImport.PlayMovie(path)` callback when that row is selected. If a cinematic filename already contains a directory it
is preserved; a bare filename with an extension is placed under `Movies\`.

These rows are compiled out of the mission list when `BZ_FFMPEG` is absent, so a normal build made without `FFMPEG=1`
continues to show the unchanged mission list. Parsing the metadata is still harmless in that build and keeps campaign data
handling independent from the decoder capability.

This first-pass list intentionally does **not** claim retail availability semantics: all three authored cinematic records
are shown when FFmpeg is enabled, regardless of mission-play filtering. That is useful for validating data parsing and
playback before progression persistence exists. The retail-compatible follow-up remains:

1. Replace the temporary `Cinematic:` rows with the Warcraft camera-button FDF art (`CampaignListBox` /
   `StandardCampaignCameraButton*`).
2. Gate Open/End rows with the state written by `SetOpCinematicAvailable` / `SetEdCinematicAvailable`; those natives are
   still stubs, so unlock persistence must be implemented before claiming retail-compatible availability.
3. Determine whether Intro availability is always data-authored/default-open or has its own progression rule in the
   original client before hiding it behind a guessed state bit.
4. Keep mission-play visibility (`wc3_campaign_mission_visibility`) separate from cinematic availability. A played map
   and an unlocked cinematic are different progression facts.

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
