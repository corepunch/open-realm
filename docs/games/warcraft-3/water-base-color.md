# Water Base Color

## Contract

`SetWaterBaseColor(red, green, blue, alpha)` is a persistent WC3 presentation override for terrain water. The game module clamps each JASS integer to `0..255`, stores the authoritative RGBA value in level state, and publishes normalized components through the generic `CS_SCENE_WATER_COLOR` configstring. The universal client copies that scene value into `viewDef_t`; it does not interpret Warcraft JASS or reach into the WC3 renderer directly.

The default map value is neutral opaque white, so maps that never call the native preserve the renderer's previous appearance. A later call replaces the current tint rather than multiplying with the previous call. The state is serialized with the level and republished after load.

## Data Flow

```text
JASS SetWaterBaseColor(r, g, b, a)
    -> games/warcraft-3/game/api/api_misc.h
    -> level.water_base_color
    -> CS_SCENE_WATER_COLOR (normalized RGBA)
    -> client/cl_view.c
    -> viewDef_t.waterBaseColor
    -> WC3 W3M water layer
    -> default world shader u_baseColor
    -> texture * vertex color * water base color
```

The generic default-world shader carries a `baseColor` multiplier because the WC3 water surface already uses that shader. `R_SetupGL` initializes the multiplier to white, and `R_DrawTerrainSegment` substitutes `viewDef.waterBaseColor` only for `MAPLAYERTYPE_WATER`, restoring white afterwards. Ground, cliffs, blight, splats, models, pathing and terrain topology are unchanged.

## Alpha

Water vertices already encode depth-derived opacity. The runtime alpha multiplies that authored/per-vertex alpha instead of replacing it, so `alpha = 255` preserves existing water opacity and lower values reduce it. RGB likewise multiplies the sampled water texture.

## Persistence and Networking

The configstring is authoritative for connected/reconnecting clients during the map lifetime. Save format version 67 adds `level.water_base_color`; `ReadGame()` republishes it after the map reload rebuilds configstrings, matching the existing environmental-fog pattern. Older save formats remain intentionally incompatible.

## Verification

Automated coverage is intended to check the game-side contract without retail assets:

- `wc3_api.set_water_base_color_clamps_and_publishes_rgba` checks argument consumption, `0..255` clamping, retained RGBA state, and `CS_SCENE_WATER_COLOR` publication.
- the WC3 save round-trip fixture includes a non-default water color and verifies all four components after reload.

A manual visual check can call:

```jass
call SetWaterBaseColor(255, 0, 0, 255)
call TriggerSleepAction(2.00)
call SetWaterBaseColor(0, 128, 255, 128)
call TriggerSleepAction(2.00)
call SetWaterBaseColor(255, 255, 255, 255)
```

Expected: existing water changes tint immediately, the second call also reduces water opacity, and the final call returns to the neutral renderer multiplier. No terrain geometry, water height, pathing, fog-of-war or simulation state changes.
