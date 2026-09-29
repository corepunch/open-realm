# Warcraft III Reforged camera zoom

OpenRealm keeps Reforged's player-controlled camera zoom policy separate from scripted JASS camera fields.

## Retail policy represented here

The player preferences are:

- default camera distance: `1650` by default;
- maximum camera distance: `3000` by default;
- interactive minimum: `1250` unless a map forces another value.

Warcraft III W3I format 32 adds default/max zoom override values and format 33 adds the minimum value.  The values are authoritative only when the corresponding map flags are set:

- `force_default_camera_zoom` (`0x100000`);
- `force_maximum_camera_zoom` (`0x200000`);
- `force_minimum_camera_zoom` (`0x400000`).

`CL_GameCameraZoomPolicy()` resolves the effective player-controlled range.  Player preferences are read from archived `wc3_camera_default_distance` and `wc3_camera_max_distance` cvars.  Enabled W3I force values replace the corresponding preference and the resulting policy is normalized so `minimum <= default <= maximum`.

`CL_GameDefaultCamera()` uses the map-forced effective default when a map enables the relevant W3I flags.  This keeps server-authored spawn and `ResetToGameCamera` state compatible with maps that force camera zoom without teaching the generic client about Warcraft map flags.

## Player controls

The WC3 default config binds:

- mouse wheel up/down to interactive zoom;
- Page Up/Page Down to the same zoom policy through the keyboard path;
- F5 to `zoomdefault`, which returns to the effective player/default distance.

The generic `zoom` command asks the selected game for its zoom policy before falling back to the legacy generic `camera_min_distance` / `camera_max_distance` cvars.  The WC3 config currently uses `zoom_speed 100` world units per discrete key/wheel notch.  That step is an OpenRealm input-tuning value, not a claimed retail constant; the retail-compatible constraints are the resolved min/default/max values and controls.

## Scripted camera fields are intentionally separate

`SetCameraField`, `AdjustCameraField`, camera setups, and cinematic camera state are not clamped to the player's interactive zoom range.  Blizzard documents maximum zoom as limiting how far the player can zoom with interactive controls, while custom maps historically use camera natives for authored distances outside normal player zoom settings.

Therefore the architecture is:

```text
mouse wheel / Page Up / Page Down
    -> CL_GameCameraZoomPolicy
    -> clamp to effective min/max

F5
    -> effective default

JASS camera fields
    -> authored value, no player-zoom clamp
```

## Known remaining gap

The archived player default is client-local, while the initial WC3 camera sample and JASS `ResetToGameCamera` are server-authored.  This patch does not add a new network/userinfo contract solely to transmit the player's preferred default distance to the server.  As a result, maps without a forced default still spawn/reset through the established `1650` server default until the player uses interactive zoom/F5.  Map-forced defaults are applied on both sides because they are part of authoritative W3I data.

A future implementation can close the per-player start/reset gap only after defining a typed client-to-server preference contract; do not make the generic server inspect WC3 cvar names or clamp scripted camera fields as a shortcut.
