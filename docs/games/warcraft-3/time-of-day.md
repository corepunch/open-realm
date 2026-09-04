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

`G_UpdateTimeOfDay()` publishes `G_GetTimeOfDay() / DayHours` into the WC3-private numeric player-stat slot
`WC3_UI_PLAYERSTAT_TIME_PHASE`. The value is quantized to the existing `USHORT` `playerState_t.stats[]` representation, so no network
struct is widened. `common/msg.c` already transports the `stats[16..17]` pair together.

`UI_WriteConsoleBackdrop()` appends one `FT_SPRITE` under `ConsoleUI` when the recipient's `war3skins.txt` resolves the
`TimeOfDayIndicator` model key. The sprite selects MDX sequence `#0` and binds its `stat` to the normalized day-phase slot. The layout
is therefore sent once while ordinary snapshot deltas update the phase.

The generic layout client interprets a numeric stat binding on `FT_SPRITE` as a normalized animation phase and emits an animation
selector such as `#0@0.500000`. The WC3 MDX renderer consumes the `@ratio` suffix and scrubs the selected sequence directly instead
of advancing it from render time. This same explicit-ratio path also makes existing UI animation selectors such as loading progress
bars deterministic.

This mirrors the important Warsmash ownership rule: the clock model does **not** advance itself. It is a presentation of the
authoritative simulation time.

## DNC Lighting Status

Warsmash loads separate terrain and unit Day/Night Cycle (DNC) MDX models and scrubs both to the same `time / DayHours` ratio. The
first animated light from those models becomes the base terrain/unit world light; `SetDayNightModels` can replace the DNC model
paths.

OpenRealm does **not** yet implement that global DNC-light contract. The WC3 MDX renderer can evaluate light tracks embedded in a
model, but those lights currently belong to the rendered model itself; there is no renderer/world interface for one hidden DNC model
to provide the base light for every terrain or unit draw. `SetDayNightModels` therefore remains a placeholder. Do not substitute a
binary dawn/dusk tint: that would lose the authored continuous DNC color/intensity animation and would not match Warsmash.

The next lighting implementation should add a game-specific DNC owner that loads the requested terrain/unit models, evaluates their
first lights at the authoritative normalized phase, and passes only generic lighting data through the renderer boundary.

## JASS Coverage

Implemented against the authoritative clock:

- `SetFloatGameState(GAME_STATE_TIME_OF_DAY, value)`
- `GetFloatGameState(GAME_STATE_TIME_OF_DAY)`
- `SuspendTimeOfDay`
- `TriggerRegisterGameStateEvent` for the time-of-day float state, firing on false-to-true condition entry

Still intentionally unresolved:

- `SetTimeOfDayScale` / `GetTimeOfDayScale` (no matching behavior was found in the inspected Warsmash snapshot)
- `SetDayNightModels` visual DNC ownership
- temporary/false time-of-day presentation (for example Moonstone-style alternate clock sequence)

## Verification

The focused simulation tests are:

```bash
build/bin/openwarcraft3 +dedicated 1 +test 'wc3_time.*'
```

The generic client tests cover preservation of the player-stat pair containing the WC3 phase slot and conversion of a bound sprite
stat into an `@ratio` animation selector. Runtime verification should additionally confirm that the race-specific clock model tracks
JASS time changes immediately, freezes under `SuspendTimeOfDay(true)`, and resumes without the HUD drifting from sight/regeneration
state.

## See Also

- [Fog And Cinematics](fog-and-cinematics.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [HUD Media Lifetime](hud-media.md)
- [Server-Authored UI Payloads](../../architecture/ui-payloads.md)
