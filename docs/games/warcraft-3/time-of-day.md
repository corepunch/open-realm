# Warcraft III Time Of Day

## Contract

Warcraft III gameplay owns one authoritative daily clock in `level.timeofday`. `games/warcraft-3/game/g_main.c` advances that
clock from the active `Misc` data (`Dawn`, `Dusk`, `DayHours`, and `DayLength`); JASS, sight, regeneration, and presentation consume
the same value rather than maintaining independent wall-clock timers.

The generic millisecond `level.time` is the persisted server/game clock used by timers and other systems. `G_RunFrame()` advances
it by `FRAMETIME`; it is not process uptime and is not the Warcraft day phase. Timer deadlines therefore remain valid across save/load.

## Simulation Data Flow

```text
MiscGame data
  Dawn / Dusk / DayHours / DayLength
                |
                v
        level.timeofday
                |
          G_GetTimeOfDay()
        /       |        \
       v        v         v
     JASS    sight/regen  HUD phase stat
```

`G_SetTimeOfDay()` queues an explicit value and `G_UpdateTimeOfDay()` applies it on the next simulation update. This matches the
Warsmash ordering used by `SetFloatGameState(GAME_STATE_TIME_OF_DAY, ...)`. `G_SuspendTimeOfDay(true)` stops ordinary progression,
but does not prevent a queued explicit set from applying.

Daytime is the half-open interval `Dawn <= time < Dusk`. Exact Dawn is day; exact Dusk is night. Existing FOW sight-radius and
night-regeneration consumers call `G_IsNight()` and therefore follow the same thresholds.

## HUD Time Indicator

The in-game clock is server-authored through `svc_layout`; `ui.dll` does not construct it.

`G_UpdateTimeOfDay()` publishes `G_GetTimeOfDay() / DayHours` into the generic numeric player-stat slot
`UI_PLAYERSTAT_ENV_PHASE` (`stats[16]`). The value is quantized to the existing `USHORT` `playerState_t.stats[]` representation, so no
network struct is widened. `common/msg.c` already transports the `stats[16..17]` pair together. Slot 17 remains
`UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR`; the two must not share a USHORT.

`UI_WriteConsoleBackdrop()` appends one `FT_SPRITE` under `ConsoleUI` when the recipient's `war3skins.txt` resolves the
`TimeOfDayIndicator` model key. The sprite selects MDX sequence `#0` and binds its `stat` to the normalized day-phase slot. The layout
is therefore sent once while ordinary snapshot deltas update the phase.

The generic layout client interprets a numeric stat binding on `FT_SPRITE` as a normalized animation phase and emits an animation
selector such as `#0@0.500000`. The WC3 MDX renderer consumes the `@ratio` suffix and scrubs the selected sequence directly instead
of advancing it from render time. This same explicit-ratio path also makes existing UI animation selectors such as loading progress
bars deterministic.

This mirrors the important Warsmash ownership rule: the clock model does **not** advance itself. It is a presentation of the
authoritative simulation time.

## DNC Lighting

Warsmash keeps two hidden Day/Night Cycle MDX instances: one for terrain and one for units. Generated Warcraft map scripts call
`SetDayNightModels(terrainDNC, unitDNC)` from `main()`, so the map script already supplies the correct environment-specific assets;
OpenRealm must not invent a tileset-to-DNC filename table. Warsmash holds both instances on sequence 0, scrubs them with the same
`time / DayHours` ratio used by the HUD clock, and uses the **first light** from each model as the base terrain/unit world light.

OpenRealm now follows that ownership contract:

1. `SetDayNightModels` registers both authored paths through `gi.ModelIndex()`. The ordinary `CS_MODELS` pool therefore owns the
   resources and late calls use the existing reliable configstring resynchronization path.
2. The native publishes the resulting model indices through `CS_TERRAIN_LIGHT_MODEL` and `CS_ENTITY_LIGHT_MODEL`; these small
   configstrings contain decimal model indices, not duplicated paths.
3. The generic client resolves those indices to loaded model handles and copies `UI_PLAYERSTAT_ENV_PHASE` onto `viewDef_t` without a
   Warcraft include. `R_SetupEnvironmentLighting` samples sequence 0 of each DNC model and writes evaluated `ENVIRONLIGHT` samples.
4. Terrain consumes `viewDef.terrainLight` through `R_SetDefaultLighting`. Non-portrait MDX entities prepend `viewDef.entityLight`
   before any model-local lights.
5. Missing/unloaded DNC models or DNC models without a light leave `valid == 0`, so the previous fixed terrain/unit lighting remains.
   Portraits deliberately retain the separate portrait-lighting path.

See [Environment Lighting](../../architecture/environment-lighting.md) for the engine sample contract.

The DNC light uses the authored MDX color, intensity, ambient color/intensity, attenuation, node orientation, and animated key tracks.
Because the phase advances continuously, dusk and dawn are continuous authored lighting transitions; the binary `Dawn`/`Dusk`
gameplay thresholds do not switch the renderer between hard-coded day/night colors. The terrain and model shaders clamp the final
accumulated light factor to `[0, 1]`, matching Warsmash before texture modulation rather than relying on framebuffer saturation.

### MDX light-track compatibility

Two MDX animation details are easy to miss and materially affect the stock DNC appearance:

- Warsmash reverses the red/blue components of **animated** light `KLAC` (Color) and `KLBC` (AmbColor) vectors before they are
  consumed. Static `MdlxLight.color` / `ambientColor` fields are not put through that animation-track conversion. OpenRealm mirrors
  this at light evaluation time, after interpolation; component-wise interpolation makes that equivalent to flipping every authored
  key and tangent before interpolation. Without it, the Lordaeron night track presents as warm brown/orange instead of cool blue.
- For a global-sequence key track whose first authored key lies beyond the declared global-sequence duration, Warsmash treats that
  first value as a constant. This includes the zero-duration-global-sequence pattern used by DNC-style node rotations. Returning the
  default transform instead leaves a directional light pointing along the unrotated model axis. `MDLX_GetModelKeytrackValue()`
  therefore preserves the first authored key for this case.

These rules belong to the generic MDX animation/light implementation, not to `SetDayNightModels`: ordinary animated model lights
benefit from the same compatibility behavior.

This is the high-confidence terrain/unit DNC base-light contract, not complete Warsmash world-light parity. Remaining gaps include the
separate target DNC model, portrait-light natives/ownership, aggregation of arbitrary scene lights exactly like Warsmash's world light
manager, and driving the shadow-map projection direction from the animated DNC directional light. The current shadow map still uses
the renderer's existing light matrix even while DNC color/intensity/direction affect surface lighting.

## JASS Coverage

Implemented against the authoritative clock:

- `SetFloatGameState(GAME_STATE_TIME_OF_DAY, value)`
- `GetFloatGameState(GAME_STATE_TIME_OF_DAY)`
- `SuspendTimeOfDay`
- `TriggerRegisterGameStateEvent` for the time-of-day float state, firing on false-to-true condition entry

Still intentionally unresolved:

- `SetTimeOfDayScale` / `GetTimeOfDayScale` (no matching behavior was found in the inspected Warsmash snapshot)
- temporary/false time-of-day presentation (for example Moonstone-style alternate clock sequence)

## Verification

The focused simulation tests are:

```bash
build/bin/openwarcraft3 +dedicated 1 +test 'wc3_time.*'
```

The `wc3_time` coverage includes `SetDayNightModels` model registration/configstring publication. Generic renderer coverage verifies
that sequence-0 DNC light sampling follows a normalized phase and that the default world shader exposes the environment-light inputs.
`MDLX_SampleFirstLight()` and the shared MDX light evaluator live in `r_mdx_light.c`. The normal renderer unity build discovers that
file automatically, while the standalone `test-renderer-model` and `test-renderer-shadows` targets list it explicitly alongside the
animation/interpolation units. Keep this dependency explicit: these tests intentionally avoid linking the full geoset/render path.
The generic client tests cover preservation of the player-stat pair containing the environment-phase slot and conversion of a bound sprite
stat into an `@ratio` animation selector. Runtime verification should additionally confirm that the race-specific clock model tracks
JASS time changes immediately, terrain and units transition continuously through dusk/night/dawn, `SuspendTimeOfDay(true)` freezes
both clock and lighting, and maps without DNC models retain the legacy fixed lighting.

## See Also

- [Environment Lighting](../../architecture/environment-lighting.md)
- [Fog And Cinematics](fog-and-cinematics.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [HUD Media Lifetime](hud-media.md)
- [Server-Authored UI Payloads](../../architecture/ui-payloads.md)
