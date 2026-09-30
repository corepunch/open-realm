# Game / Client Boundary — the Universal Client

OpenRealm follows the Quake 2 / Quake 3 split: **the game module owns logic, the client is a universal
player for whatever the game authored.** `games/<game>/game/` is the project's `game.dll`; `client/` is
`client.dll`. The goal is that `client/` could run a game it has never heard of, given only that game's
server, renderer module and data.

This is a stronger rule than "no game-specific names in engine code" (see
[server-selected effects](server-selected-effects.md)). A change can be perfectly game-agnostic in its
naming and still be a violation, because it moves a *decision* or a *formula* out of the game module.

## Who owns what

| Layer | Owns | Must not contain |
|-------|------|------------------|
| `games/<game>/game/` (game.dll) | Rules, scripts and natives, timers, state machines, formulas and waveforms, data-table lookups, what each player sees and when | Rendering, platform input |
| `server/` | Transport, snapshots, delta compression, client slots | Game rules |
| `client/` (client.dll) | Decoding messages, interpolating between two server samples, predicting the player's own generic input, drawing server-authored payloads, window/input/audio devices | Anything that answers "what should happen" or "how does this effect behave" |
| `games/<game>/renderer/` behind `re.*` | Per-frame presentation that needs the game's own assets: model animation and events, particles, terrain, weather | Simulation state, gameplay decisions |
| `games/<game>/menu/` | Main-menu / glue screens only | In-game UI (that is server-authored; see [ui-system.md](ui-system.md)) |

## The test

Before adding code under `client/`, ask:

1. **Does it evaluate something?** A formula, oscillator, curve, random pick, timer, threshold or state
   machine whose parameters came from the server is logic. Evaluate it in the game and send the result.
2. **Would another game need different behaviour here?** If yes, the client is being taught one game's rules.
3. **Are the new `playerState_t` / `entityState_t` / message fields *parameters* or *results*?** Parameters
   (magnitude, rate, mode flags, rule ids) mean the consumer has to know how to interpret them, so the logic
   went with them. Results (a position, an offset, an index of a registered asset, a visibility bit) keep the
   client dumb.
4. **Could the client do this with only "interpolate, predict my own input, draw"?** If not, it does not
   belong there.

Quake 2 is the reference for every one of these: damage and earthquake kicks are computed in the game's
`p_view.c` and sent as `kick_angles` / `viewoffset`; HUD content is a server-authored layout string;
temp entities carry a type and the data needed to draw it, never a rule. Quake 3's `cgame` is the
counter-example people reach for — OpenRealm has no `cgame`. Per-frame presentation that genuinely cannot
run at the server tick lives in the game's *renderer* module, which is loaded per game and may read that
game's assets.

## Where a feature goes

| Need | Put it in | Reaches the client as |
|------|-----------|------------------------|
| Scripted or rule-driven view change (pan, shake, controller) | game | existing `playerState_t` view fields, or generic results such as `viewoffset` / `eyeoffset` |
| Per-player visibility, HUD content, cursor, tooltips | game | `uiflags`, stats, `svc_layout`, `svc_window`, registered image/model indices |
| Effect whose *look* depends on one game's art or tables | game selects, that game's renderer resolves | generic index + discriminant flags ([server-selected effects](server-selected-effects.md)) |
| Continuous visual simulation at render rate (particles, MDX event children, terrain ripples, weather) | `games/<game>/renderer/` | a one-shot typed event or configstring that starts it; the client only forwards it to `re.*` |
| Smoothing between two server samples | client | — (this is the client's job) |
| Prediction of the local player's own camera/input | client, using only generic input contracts | — |

Accept the cost of the server tick. A value evaluated in the game is sampled at 10 Hz and interpolated by
the client; that is the intended trade. Do not move evaluation into `client/` to gain frequency — if render-rate
motion is genuinely required, it is renderer-module presentation started by a game event, or it is a
contract question for the developer.

## Worked example: camera noise (PR 547)

`CameraSetTargetNoise` / `CameraSetSourceNoise` were first implemented by adding the native's
`magnitude`, `velocity` and `vertOnly` to `playerState_t` and evaluating a sine oscillator in
`client/cl_view.c`. Nothing in it was named after Warcraft, and the engine-boundary audit passed. It was
still wrong:

- the waveform, its per-axis constants and the vertical-only rule were logic in the client;
- the new player-state fields were parameters of one game's native, not view state;
- another game wanting a different shake would have needed a second client code path.

The fix keeps the natives' inputs in `gameClient_t.camera.noise[]`, evaluates the oscillator in
`G_RunClients()` once per server frame, and sends two generic results, `playerState_t.viewoffset` and
`playerState_t.eyeoffset`. `Matrix4_getCameraMatrix()` interpolates the two latest samples and adds them.
The offsets are separate from `vieworigin` for the same reason Quake 2 separates `viewoffset` from the
player origin: game code, input focus movement and camera getters keep reading a steady camera. See
[WC3 cinematics](../games/warcraft-3/cinematics.md) and [network.md](network.md).

## Review checklist

For any diff touching `client/`:

- No new function there takes a rate, magnitude, curve, seed or mode and returns a value.
- No new branch there depends on which effect, native, ability or game mode is active.
- New wire fields are results the client can use without interpretation.
- The same behaviour is reachable for another game by changing only `games/<other>/`.
- The engine-boundary audit (`python3 tools/engine_boundary_audit.py`) is necessary but not sufficient:
  it matches names, and a logic leak has no tell-tale name.
