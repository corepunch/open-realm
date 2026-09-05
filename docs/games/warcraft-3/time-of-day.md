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

### Temporary / false time

Warsmash has a second clock state for `AIct` (`itemchangetimeofday`, used by Moonstone-style items). OpenRealm mirrors the inspected
Warsmash contract in `TIMEOFDAY.false_time`:

- `G_SetFalseTimeOfDay(hour, minute, duration)` creates an uninitialized override whose lifetime is stored in simulation ticks;
- the first `G_UpdateTimeOfDay()` initializes it, after which `G_GetTimeOfDay()` returns `hour + minute / 60`;
- while `false_time.active`, the canonical `elapsed` clock does not advance, regardless of the ordinary suspend flag;
- a queued `G_SetTimeOfDay()` retargets the active false clock instead of changing canonical `elapsed`;
- expiry removes the override and exposes the unchanged canonical clock again;
- time-of-day limit events compare the effective value before/after initialization, retargeting, replacement, and expiry just like
  ordinary clock changes.

The false-clock fields are part of save format v10, so saving during an active override preserves both the authored temporary time and
its remaining simulation-tick lifetime.

`games/warcraft-3/game/skills/s_item.c` registers `AIct` and reads its authored `DataA1`, `DataB1`, and `Dur1` values as hour,
minute, and false-time duration. This follows Warsmash's `core/assets/abilityBehaviors/itemSimple.json`; no item-specific clock value
is hard-coded in gameplay.

Daytime is the half-open interval `Dawn <= time < Dusk`. Exact Dawn is day; exact Dusk is night. Existing FOW sight-radius and
night-regeneration consumers call `G_IsNight()` and therefore follow the same thresholds.

## Developer Time-Of-Day Cheats

The `day` and `night` console commands are `sv_cheats`-gated shortcuts that queue an explicit write through the same
`G_SetTimeOfDay()` path used by JASS:

```sh
set sv_cheats 1
day
night
```

They target the middle of the map-authored day and night intervals rather than hard-coding clock values. With stock
`Dawn=6`, `Dusk=18`, and `DayHours=24`, `day` selects 12:00 and `night` selects 00:00. Custom `Misc` data therefore keeps
the cheats inside its own authored phases. The queued write is consumed by `G_UpdateTimeOfDay()`, so ordinary time-of-day
game-state events, HUD phase, sight/regeneration rules, and DNC lighting see the same authoritative transition.

`wc3_cheat_timeofday_scale` multiplies only ordinary day/night clock progression. It is a developer CVar (no `sv_cheats`
gate) and defaults to `1`; for example:

```sh
set wc3_cheat_timeofday_scale 20
# or at launch:
+set wc3_cheat_timeofday_scale 20
```

runs the authored day/night cycle at 20x speed. A stock 480-second cycle therefore completes in about 24 real seconds. Set it back
to `1` for normal timing. Zero, negative, non-numeric, and non-finite values fall back to `1` so an accidental cheat value cannot
freeze or corrupt the clock.

The multiplier does **not** change `FRAMETIME`, simulation speed, movement, AI, JASS timers, trigger sleeps, cinematics, or camera
timing. Explicit `SetFloatGameState(GAME_STATE_TIME_OF_DAY, ...)` writes remain exact, and `SuspendTimeOfDay(true)` still suspends
ordinary progression regardless of the cheat scale. Because every gameplay/presentation consumer reads the authoritative
`level.timeofday`, accelerated dawn/dusk changes propagate to sight, regeneration, game-state events, the HUD clock, and DNC phase
consumers together.

## HUD Time Indicator

The in-game clock is server-authored through `svc_layout`; `ui.dll` does not construct it.

`G_UpdateTimeOfDay()` publishes `G_GetTimeOfDay() / DayHours` into the generic numeric player-stat slot
`UI_PLAYERSTAT_ENV_PHASE` (`stats[16]`). The value is quantized to the existing `USHORT` `playerState_t.stats[]` representation, so no
network struct is widened. `common/msg.c` already transports the `stats[16..17]` pair together. Slot 17 remains
`UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR`; the two must not share a USHORT.

`UI_PLAYERSTAT_ENV_VARIANT` (`stats[23]`) is `0` for the canonical clock and `1` while an initialized false clock is active. It is
transported by the existing `stats[22..23]` pair. The time-of-day `FT_SPRITE` sets `UIFLAG_SPRITE_STAT_SEQUENCE` and stores that
secondary stat index in `frame.value`, allowing the generic layout client to rewrite its explicit `#0` selector to `#1` while
retaining `UI_PLAYERSTAT_ENV_PHASE` as the `@ratio` binding. This reproduces Warsmash's normal sequence 0 / false-time sequence 1
clock presentation without resending the HUD layer.

`UI_WriteConsoleBackdrop()` appends one `FT_SPRITE` under `ConsoleUI` when the recipient's `war3skins.txt` resolves the
`TimeOfDayIndicator` model key. The sprite starts on MDX sequence `#0`, binds its `stat` to the normalized day-phase slot, and binds
its sequence selector to the environment-variant slot. The layout is therefore sent once while ordinary snapshot deltas update both
the phase and false-time presentation state.

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

- Warcraft animated MDX color tracks use BGR component order while static color fields are usually already RGB. GeosetAnimation
  colors are the important exception used by the HUD clock: Warsmash swizzles both the static GeosetAnimation base color and
  animated `KGAC` keys from BGR to RGB. OpenRealm mirrors that through `MDLX_GetGeosetAnimationStaticColor()` and
  `MDLX_GetAnimatedColorTrackValue()`. DNC light `KLAC`/`KLBC` tracks and geoset-animation `KGAC` tracks therefore share the same
  visible color convention, and the time-of-day indicator's authored cool-blue night glow no longer presents as warm red/orange.
  This is a semantic float-component conversion performed before shader upload; it is deliberately separate from `PIXEL_BGRA`
  texture byte handling, so desktop GL native BGRA support and GLES CPU BGRA-to-RGBA fallback produce the same model color.
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
- Warsmash-style false-time state used by `AIct` (`itemchangetimeofday`)

Still intentionally unresolved:

- `SetTimeOfDayScale` / `GetTimeOfDayScale`: a fresh inspection of `WarsmashModEngine-main` found no implementation or registered
  native to mirror, so OpenRealm keeps these placeholders rather than inventing a scale contract.

Warsmash also registers a non-retail `SetFalseTimeOfDay` helper inside its ability-builder JASS environment. OpenRealm registers the
same host native so an extension script that explicitly declares it can call the simulation operation, but deliberately does not add
that non-retail declaration to the bundled Warcraft `common.j` API.

## Verification

The focused simulation tests are:

```bash
build/bin/openwarcraft3 +dedicated 1 +test 'wc3_time.*'
```

The `wc3_time` coverage includes false-time initialization/expiry, canonical-clock freezing, `SetTimeOfDay` retargeting, HUD variant
publication, and `SetDayNightModels` model registration/configstring publication. `wc3_items` verifies that `AIct` reads the fixture
ability's `DataA1`, `DataB1`, and `Dur1`. Generic client coverage verifies that a sprite can combine a secondary stat-selected `#N`
sequence with its normalized `@ratio` phase and that the environment-variant stat survives player-state deltas. Generic renderer
coverage verifies that sequence-0 DNC light sampling follows a normalized phase and that the default world shader exposes the
environment-light inputs.
`MDLX_SampleFirstLight()` and the shared MDX light evaluator live in `r_mdx_light.c`. The normal renderer unity build discovers that
file automatically, while the standalone `test-renderer-model` and `test-renderer-shadows` targets list it explicitly alongside the
animation/interpolation units. Keep this dependency explicit: these tests intentionally avoid linking the full geoset/render path.
For runtime cheat verification, enable `sv_cheats`, run `day` and `night`, and confirm the HUD clock, DNC lighting, sight,
regeneration, and time-of-day event consumers all follow the new phase. Then set `wc3_cheat_timeofday_scale 20`, watch the HUD
clock cross Dawn/Dusk rapidly, restore `wc3_cheat_timeofday_scale 1`, and confirm normal cadence resumes without changing
unit/AI/timer speed.

The generic client tests cover preservation of the player-stat pair containing the environment-phase slot and conversion of a bound sprite
stat into an `@ratio` animation selector. Runtime verification should additionally confirm that the race-specific clock model tracks
JASS time changes immediately, terrain and units transition continuously through dusk/night/dawn, `SuspendTimeOfDay(true)` freezes
both clock and lighting, an `AIct`/Moonstone-style item switches the clock model to its authored false-time sequence then returns to
the frozen canonical phase on expiry, and maps without DNC models retain the legacy fixed lighting.

## See Also

- [Environment Lighting](../../architecture/environment-lighting.md)
- [Fog And Cinematics](fog-and-cinematics.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [HUD Media Lifetime](hud-media.md)
- [Server-Authored UI Payloads](../../architecture/ui-payloads.md)
