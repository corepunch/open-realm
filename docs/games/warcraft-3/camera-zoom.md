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

After map media is ready, the shared client sends those two local preferences as a typed input before `begin`. The
server accepts that preference during signon, stores it on the WC3 player client, and applies the effective default in
`G_ClientBegin` before starting map scripts. This initializes the first camera sample with the selected default.
W3I forced values still take precedence, and scripts that set camera fields after startup retain control. The generic
client has no WC3 cvar names; each game supplies preferences through `CL_GameCameraPreferences()`.

`CL_GameDefaultCamera()` supplies the built-in camera fallback. Server-side `ResetToGameCamera` resolves the connected player's `wc3_camera_default_distance` and `wc3_camera_max_distance` preferences through the same W3I policy, so a map-forced default still takes precedence. This makes cinematic exits such as Human02's intro reset to the player's configured gameplay zoom.

## Player controls

The WC3 default config binds:

- mouse wheel up/down to interactive zoom;
- Page Up/Page Down to the same zoom policy through the keyboard path;
- F5 to `zoomdefault`, which returns to the effective player/default distance;
- touchscreens: deliberate two-finger opposing pinch (outward to zoom in,
  inward to zoom out), using the same player-controlled camera distance limits.
  `cl_touch_pinch_sensitivity` defaults to `0.3`; moving both contacts together
  pans without zooming even when their SDL motion events arrive separately.

Wheel zoom is ignored while the pointer is over a gameplay UI layout or client-managed window. Page Up/Page Down remain keyboard controls when a nonmodal window is open; modal windows block them through the normal gameplay-input gate.

The generic `zoom` command asks the selected game for its zoom policy before falling back to the legacy generic `camera_min_distance` / `camera_max_distance` cvars.  The WC3 config currently uses `zoom_speed 100` world units per discrete key/wheel notch.  That step is an OpenRealm input-tuning value, not a claimed retail constant; the retail-compatible constraints are the resolved min/default/max values and controls.

## Scripted camera fields are intentionally separate

`SetCameraField`, `AdjustCameraField`, camera setups, and cinematic camera state are not clamped to the player's interactive zoom range.  Blizzard documents maximum zoom as limiting how far the player can zoom with interactive controls, while custom maps historically use camera natives for authored distances outside normal player zoom settings.

Therefore the architecture is:

```text
mouse wheel / Page Up / Page Down / touchscreen pinch
    -> CL_GameCameraZoomPolicy
    -> clamp to effective min/max

F5
    -> effective default

JASS camera fields
    -> authored value, no player-zoom clamp
```

## Startup coverage

`wc3_api.client_begin_applies_local_camera_default_before_map_scripts` exercises the typed preference through the real
`ClientInput` and `ClientBegin` callbacks and verifies both the user default and a forced W3I default. The shared
`typed_controller_input_roundtrip_and_validation` test covers serialization and invalid values; the server network
test verifies that the preference is accepted before spawn while ordinary input remains gated.
