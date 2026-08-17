# WoW Cinematics

## First-login race flyby

The race intro is client-rendered and selected entirely from client data:

```text
ChrRaces.dbc CinematicSequenceID
  -> CinematicSequences.dbc ID, SoundID, 8 camera IDs
  -> CinematicCamera.dbc ID, model path, origin[3], facing
  -> Cameras\Flyby<Race>.m2
```

Mounted 1.12 archive checks establish these examples:

| Race | Sequence | Camera | Model |
| --- | ---: | ---: | --- |
| Orc | 21 | 235 | `Cameras\FlybyOrc.m2` |
| Human | 81 | 142 | `Cameras\FlyByHuman.m2` |

`m2tool --info Cameras\FlybyOrc.m2` identifies a classic MD20 version-256 model
with one animation, one camera, and no vertices. DBC strings use the historical
`.mdx` spelling; archive lookup resolves the corresponding M2 asset.

### Fast archive verification

These local archive facts have already been established; use the narrow commands
below instead of searching unrelated repositories:

```sh
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\ChrRaces.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\ChrRaces.dbc' 9
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CinematicSequences.dbc' 10
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ dump 'DBFilesClient\CinematicCamera.dbc' 10
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ str 'DBFilesClient\CinematicCamera.dbc' 1 1
build/bin/m2tool -mpq data/world-of-warcraft/model.MPQ -model 'Cameras\FlybyOrc.m2' --info
```

Verified 1.12 layouts/results (field layouts consolidated in
[`docs/dbc-reference.md`](dbc-reference.md#cinematic-tables)):

- `ChrRaces.dbc`: 9 records, 29 fields; Human row 0 field 16 is sequence 81,
  Orc row 1 field 16 is sequence 21.
- `CinematicSequences.dbc`: 10 records, 10 fields; sequence 21 references camera
  235 and sequence 81 references camera 142.
- `CinematicCamera.dbc`: 10 records, 7 fields; row 1 is camera 235 with
  `Cameras\FlybyOrc.mdx`, row 7 is camera 142 with `Cameras\FlyByHuman.mdx`.
- `FlybyOrc.m2` is camera-only: classic MD20 version 256, one animation, one
  camera, zero vertices.

Code search can also stay narrow: M2 camera track evaluation already exists in
`M2_CameraView` in `renderer/m2/r_m2.c`. The missing work is first-login state
transport, a renderer camera API, client cinematic state, and input interruption;
it is not M2 camera interpolation research.

The server does not author camera samples. AzerothCore sends `AT_LOGIN_FIRST` for
a newly created character, then the client follows the selected M2 camera track.
The existing renderer already evaluates classic and modern M2 camera tracks in
`M2_CameraView`; it is not yet exposed as a client view-camera API.

## Required playback lifecycle

Implement playback as a Quake-style client cinematic/spectator state only after
the first-login bit is carried through the character/login state:

1. Read the selected race's sequence and camera records from the three DBCs.
2. Register the camera-only M2 and expose its evaluated view through the renderer API.
3. Enter a non-playing client cinematic state before normal player control starts.
4. Advance from the M2 animation clock, including camera origin/facing offsets.
5. Exit at track completion or on Escape/action input, then restore player view and input.

Do not infer first login from map entry and do not auto-play on every login. That would
replace the authoritative server trigger with a behavioral fallback.
