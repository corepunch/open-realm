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

See also: [Building Damage Rendering](../games/warcraft-3/building-damage-rendering.md) and [WC3 Ability, Buff, And Item Presentation Effects](../games/warcraft-3/ability-and-item-effects.md).

## Anti-pattern #4: `#ifdef` around environment lighting in `client/` (do not repeat)

PR #296 published generic `CS_TERRAIN_LIGHT_MODEL` / `CS_ENTITY_LIGHT_MODEL` slots, then resolved
`WC3_UI_PLAYERSTAT_TIME_PHASE` in `client/cl_view.c` behind `#if !defined(WOW) && !defined(SC2)`
and included `games/warcraft-3/common/ui_constants.h`. Day phase is an ordinary player stat, and
the evaluated scene light is an ordinary view sample. The client copies optional model-index
configstrings plus `UI_PLAYERSTAT_ENV_PHASE`; `R_SetupEnvironmentLighting` in `games/*/renderer`
evaluates those inputs into `viewDef.terrainLight` / `entityLight`. See
[Environment Lighting](environment-lighting.md).

## Anti-pattern #3: `#ifdef WC3` around generic camera samples (do not repeat)

PR #287 added `playerState.camera_render` and `#ifdef WC3` copies in `CL_ParsePlayerInfo`. Target height offset, near clip, and far clip are ordinary camera sample fields, not Warcraft-only data. Hiding them behind a compile guard also consumed the final 32-bit player-state field. They now live in generic `vieworigin` (world-space look-at) plus `znear`/`zfar` packed with `distance`. `viewangles` travel on the same snapshot (Euler degrees, `ROTATE_ZYX`); the client converts them to a quaternion and slerps. Do not add a parallel `viewquat` field — Euler→quat is lossless. Camera target bounds do not travel — the client clamps with `CM_GetWorldBounds()`. Unused WoW map metadata moved to one WoW map-info configstring. The player-state delta mask remains 32 bits. The server composes Z and must always write `znear`/`zfar` the same way it writes `fov`; the client copies the sample with no keep-previous fallback.

## Anti-pattern #2: `#ifdef` branch in a shared dispatcher (do not repeat)

PR #242 initially added `#ifdef WC3 ... strcmp(command, "wc3_selection") ... #endif` directly inside the
shared `CL_ParseGameCommand()` in `client/cl_parse.c`. That was wrong because selection synchronization is
a generic client-state operation, not WC3 UI behavior. The corrected implementation uses the generic
`select` command and parses it in `CL_ParseGameCommand()` without a game guard; only the server-side
shortcut producer remains under `games/warcraft-3/`.

## Review rule

When a shared dispatcher needs a new game behavior, first locate its function-table callback. If the callback
exists, implement the command in the selected game's module. If it does not exist, add a narrow table entry;
do not substitute a per-game preprocessor guard or a hardcoded command branch in the shared dispatcher.

## Selection-scoped world indicators

Persistent local markers such as Warcraft III's Rally destination are ordinary game-owned edicts. The game resolves and registers the model, sets `SVF_OWNER_ONLY`, and stores the recipient in `entityState_t.player`; `SV_BuildClientFrame` excludes the edict from every other client's snapshot. Point markers use an authoritative world origin, while widget markers use the normal linked-edict movement path to follow their target.

The owning game client keeps the marker edict pointer so selection changes update or free the same entity. Save/load does not serialize that runtime cache pointer: the marker's saved `owner` edict reconstructs it after pointer fixups. One-shot acknowledgements such as move and attack confirmations remain temporary events.

## One-shot resolved world text

Short-lived world text follows the same ownership rule as model effects. `TE_FLOATING_TEXT` is a generic temporary event: it carries an already-resolved string, RGBA colour, font index, lifetime/fade timings, screen-space velocity, and world anchor. The owning game must resolve semantic identity such as Warcraft gold/lumber before writing the event. Shared/client code must not grow `TE_GOLD_TEXT`, race/resource tables, or game-data lookups.

The client captures the event anchor and projects it through the active camera each draw. It does not create a persistent network entity and does not attach the text to the source after spawn. This is appropriate for one-shot feedback such as a committed resource gain; persistent/script-addressable text belongs to a separate handle/entity contract. See [WC3 Resource-Gain Floating Text](../games/warcraft-3/resource-gain-text.md).
