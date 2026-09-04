# Environment Lighting

## Contract

The renderer consumes **evaluated** scene lights, not a title's authored source.

`ENVIRONLIGHT` in `common/shared.h` is the semantic sample: direction (toward the light),
directional color/intensity, ambient color/intensity, `type` (`RMODELLIGHTTYPE`), and `valid`.
Type `0` is omni, so presence is `valid`, not `type == 0`.

`viewDef_t` carries two samples:

- `terrainLight` — world/ground
- `entityLight` — models; when invalid, WC3 reuses `terrainLight`

`MODELLIGHTING` in `renderer/r_shader.h` remains the 8-slot shader pack. Convert with
`R_EnvironLightFromModel` / `R_LightingFromEnviron`. Do not put `MODELLIGHTING` on the wire.

## Data Flow

```text
game-owned config
  WC3: SetDayNightModels → CS_TERRAIN_LIGHT_MODEL / CS_ENTITY_LIGHT_MODEL
  WoW: Light*.dbc (missing in 1.5) / synthesized sun
  SC2: map sc2MapLighting_t
        |
        v
playerState.stats[UI_PLAYERSTAT_ENV_PHASE]   (normalized clock; 0 if unused)
        |
        v
client copies optional model handles + phase onto viewDef  (no game #ifdef)
        |
        v
R_SetupEnvironmentLighting()   (per-game hook in games/*/renderer/r_game.c)
        |
        v
viewDef.terrainLight / entityLight
        |
        v
R_SetDefaultLighting / R_SetModelLighting
```

`client/cl_view.c` must not include a game header or compile-guard the clock slot.
PR #296 originally resolved `WC3_UI_PLAYERSTAT_TIME_PHASE` behind `#if !defined(WOW) && !defined(SC2)`;
that is the same shared-dispatcher mistake as camera `#ifdef WC3` (see
[server-selected-effects.md](server-selected-effects.md)).

## Configuration vs current conditions

| | Owner | Channel |
|---|---|---|
| Authored source (DNC MDX, Light.dbc, map rig) | `games/<game>/` | configstrings, DBC, map data |
| Normalized day phase | server game module | `UI_PLAYERSTAT_ENV_PHASE` (`stats[16]`) |
| Evaluated sample | game renderer hook | `viewDef` `ENVIRONLIGHT` |

Do not unify the authored sources into one common config struct. They are incommensurable.
The shared object is the evaluated sample.

`UI_PLAYERSTAT_ENV_PHASE` occupies `stats[16]`, packed with cinematic portrait color in the
existing `stats[16]` `NFT_LONG`. It must not reuse slot 17 (`UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR`).

## PlayerState

Evaluated `ENVIRONLIGHT` is **not** on `playerState_t` yet. The 32-bit player-state field mask has
spare bits (`MSG_FIELD_COUNT(playerStateFields)` in `common/msg.c`); a new netField still needs an
explicit table entry. Lighting is world-global, and WC3/WoW evaluation sources live in the renderer
(MDX tracks, camera-dependent DBC). `cinematic_portrait`/`team`/`color`/`race` pack as one `NFT_LONG`.

The interpolated quantity today is the phase stat at snapshot rate (10 Hz). The game renderer
samples authored tracks at that phase every render frame. Camera-style `lightstate[0/1]` lerp of
evaluated RGB waits on a 64-bit mask (or a packed `NFT_ENVLIGHT`) and must snap on explicit
`SetTimeOfDay` rather than smearing the jump.

## Game hooks

`R_SetupEnvironmentLighting` is declared in `renderer/r_game.h` and called from `R_RenderFrame`
after the view copy. Each game fills `tr.viewDef.terrainLight` / `entityLight`:

| Game | Source |
|---|---|
| WC3 | sequence 0, first light of the DNC models, at `environmentPhase` |
| WoW | `Wow_SunDirection` / `WOW_LIGHT_*` (later Light.dbc at camera) |
| SC2 | map key directional (`SC2_LIGHT_KEY`) |

`valid == 0` keeps each path's historical fallback.

## Known Pitfalls

- Do not put MDX model handles on `viewDef` as the lighting contract. They are WC3 sampling
  inputs. Draw paths read `ENVIRONLIGHT`.
- Do not evaluate WoW lights on the server: classic `Light.dbc` is camera-position dependent.
- Midnight wrap: do not lerp phase 0.99→0.01 without wrap handling; that flashes midday.
- HUD clock sprites bind to `UI_PLAYERSTAT_ENV_PHASE`. That slot is a clock, not a light.

## Verification

```bash
build/bin/openwarcraft3 +dedicated 1 +test 'wc3_time.*'
make test-renderer-model
```

`renderer_shader.environ_light_converts_to_model_lighting` locks the sample↔shader conversion
and the distinct `stats[16]` / `stats[17]` slots. Runtime: terrain and units follow dusk/night/dawn
continuously; `SuspendTimeOfDay(true)` freezes both clock and lighting; maps without DNC models
keep the legacy fixed sun.

## See Also

- [Shared model shader contracts](model-shader.md)
- [Client configstrings and view](client.md)
- [WC3 time of day](../games/warcraft-3/time-of-day.md)
- [WoW sun / Light.dbc](../games/world-of-warcraft/terrain-and-world-rendering.md)
