# Server-Selected Presentation Effects

## Pattern

When a game needs to attach a race/unit/spell-specific model or effect to an entity
(building-on-fire, frost, poison, an aura ring), the shared network layer must stay
completely ignorant of *which* content is selected. It only carries:

- `BYTE entityState_t.effect` — a registered model index, same shape as `model`/`model2`.
- `USHORT entityState_t.effect_flags` — an explicit discriminant (`EFX_MODEL`, `EFX_SPLAT`,
  `EFX_ATTACH_SLOTS`, ...) plus a packed slot/parameter mask (`EFX_SLOT_MASK`/`EFX_SLOT_SHIFT`).

All content resolution — race lookup, asset path construction, threshold/tier logic —
happens server-side in `games/<game>/game/skills/`, which registers the resolved model
and writes the generic index + flags. The client-side `games/<game>/renderer/` hook
consumes `effect`/`effect_flags` generically; it must not contain a table of per-race
or per-spell asset paths.

## Anti-pattern (do not repeat)

An earlier version of building-damage fire rendering (see PR #241, commit `5be1bfc`)
added `EF_BUILDING_FIRE_UNDEAD` / `EF_BUILDING_FIRE_NIGHTELF` directly to the shared
`common/shared.h` flags enum, and a matching per-race asset table to
`games/warcraft-3/renderer/r_game.c`. This baked WC3-specific race identity into the
generic engine layer and the client renderer, in violation of the engine/game boundary.
It was corrected in commit `8d89a18` by replacing the named per-race flags with the
generic `effect`/`effect_flags` mechanism above and moving all race/tier resolution into
the new `games/warcraft-3/game/skills/s_onfire.c`.

## Reference implementation

- `games/warcraft-3/game/skills/s_onfire.c` — race lookup, damage-tier staging, model
  registration.
- `common/shared.h` — `EFX_*` enum, `entityState_t.effect`/`effect_flags`.
- `games/warcraft-3/renderer/r_game.c` `R_RenderModel()` — generic consumer, no
  per-race branching.

See also: [Building Damage Rendering](../games/warcraft-3/building-damage-rendering.md)

## Anti-pattern #2: `#ifdef` branch in a shared dispatcher (do not repeat)

PR #242 initially added `#ifdef WC3 ... strcmp(command, "wc3_selection") ... #endif` directly inside the
shared `CL_ParseGameCommand()` in `client/cl_parse.c`. That was wrong because selection synchronization is
a generic client-state operation, not WC3 UI behavior. The corrected implementation uses the generic
`set_selection` command and parses it in `CL_ParseGameCommand()` without a game guard; only the server-side
shortcut producer remains under `games/warcraft-3/`.

## Review rule

When a shared dispatcher needs a new game behavior, first locate its function-table callback. If the callback
exists, implement the command in the selected game's module. If it does not exist, add a narrow table entry;
do not substitute a per-game preprocessor guard or a hardcoded command branch in the shared dispatcher.
