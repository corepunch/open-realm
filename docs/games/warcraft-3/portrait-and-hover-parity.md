# Cinematic portraits and destructable hover

## Retail evidence

Verified with the installed WC3 TFT 1.27.1.7085 `game.dll`, SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Addresses below use image base `0x6f000000`; relocate against the loaded module.
Ghidra decompilation and xrefs were checked against live Frida calls in
`Maps/FrozenThrone/Campaign/NightElfX01.w3x`.

| Entry | Observation |
|---|---|
| `0x6f2124f0` | `SetCinematicScene` receives scene and voice durations separately. |
| `0x6f373670` | Routes gameplay transmissions and cinematic presentation separately. |
| `0x6f3d0a90` | Cinematic scene schedules scene expiry and an earlier voice expiry. |
| `0x6f3cd2c0` | Adds talk tag `0x26` when its talking argument is true. |
| `0x6f1a42b0` | Animation request: object in ECX, tag array in EDX, count on stack. |
| `0x6f394d50` | World hover selects the normal/selection cursor state. |
| `0x6f38a9c0` | Cursor state dispatch; gate hover uses state 1, leaving uses state 0. |
| `0x6f3528e0` | Registers cursor animation names/tokens. |

Frida recorded the first Maiev transmission with scene duration 8.658 s and
voice duration 7.158 s. The cinematic portrait requested tags `[10, 38]`,
then `[10]` when the voice timer fired. Subsequent opening transmissions
repeated this lifecycle, including Huntress dialogue. These are cinematic
voice timers, not the selected-unit audio callback path.

Moving the mouse from empty ground to the opening Elven Gate produced
`0x6f394fec -> 0x6f38a9c0`, state 1, animation tag `[1]` (`Select`). Moving
away produced state 0 and tag `[0]` (`Normal`). Repeating this transition
confirmed it was hover-driven, without issuing an order. The gate showed a
neutral highlight and its name, without a unit health bar.

The isolated comparison used Wine prefix `/tmp/openrealm-portrait-wine`,
Xvfb `:94`, and Frida port 27046. Other investigations used the original
prefix/display; sharing it caused focus interference and a second retail
instance initialization failure. Use separate prefixes for concurrent runs.

## Portrait root cause and fix

The authored MDX sequences are:

| Model | Idle | Speaking |
|---|---|---|
| `Units/NightElf/HeroWarden/HeroWarden_Portrait.mdx` | `Portrait - 1`, 1667–3167 | `Portrait Talk - 1`, 5000–8000 |
| `Units/NightElf/Huntress/Huntress_Portrait.mdx` | `portrait`, 25400–26667 | `portrait talk`, 28000–30667 |

`SetCinematicScene` and `hud_cinematic.c` already transmitted the speaking
state. `R_SelectUISequence` tried exact case-sensitive names, then picked the
first sequence beginning with `Portrait` for both idle and speaking requests.
Maiev consequently used her idle track while the HUD requested `Portrait Talk`;
the Huntress's lowercase names also missed the lookup. The permissive fallback
predated this investigation (`git blame`: `c8584ee02`, later `dcdbbb5ad`).

The UI selector now recognizes case-insensitive portrait names with numeric
variant suffixes and distinguishes the Talk tag from idle. A model lacking a
talk track retains its authored idle portrait. Exact and indexed animation
requests retain their existing behavior. The exact-name lookup moved beside
the UI selector so its regression uses the production lookup, not a test copy.
This does not implement retail's full weighted/random animation selection.

A live OpenRealm Frida trace after the fix recorded `Portrait Talk` frames
7600, 7700, 7800, 7900, then 5000, within Maiev's authored talk interval.
The rendered capture shows the mouth changing during dialogue. Xvfb uses
software rendering, so wall-clock playback speed is not a timing-parity claim.

## Gate root cause and fix

Two independent gates prevented the retail behavior:

1. `G_CustomizeEntity` restricted hover metadata to `SVF_MONSTER`; destructables
   are `SVF_STATIC_SCENERY`, so the client rejected them as hover targets.
2. `SCR_DrawCursor` supplied only tint and scrolling direction. The WC3 renderer
   retained `Normal` over every world target.

Living, visible, selectable destructables now publish their authored localized
name and neutral relationship through the existing snapshot fields. Dead,
hidden, fog-ineligible and unselectable objects lose that metadata. No unit
health/mana bars or command ownership are granted. The name comes from
`DestructableData.Name` (ROC alias `name`); `WESTRING_*` keys resolve through
`UI/WorldEditGameStrings.txt`, section `WorldEditStrings`. That gameplay
localization file is distinct from the editor-only `WorldEditStrings.txt`.

The client passes its validated world-hover state in `drawCursor_t.hover`.
The WC3 renderer selects `Select` while hovering; scroll artwork has priority.
Changing animation resets its epoch, while continued hover advances the same
loop. Leaving the object restores `Normal`. Surrounding quotes retained by the INI cache are removed before publishing
the name. Other games may ignore the new
local renderer input; no snapshot or save format changes are required.
A rendered OpenRealm run and Frida trace confirmed `Normal` → `Select` →
`Normal` over the opening gate. The cursor model/race selection remains a
separate concern.

## Verification and artifacts

Regressions reproduced the failures before production fixes:

- `mdx_ui.cinematic_portrait_talk_and_idle`: Maiev numbered variants, advancing
  talk frames, return to idle, and reversed sequence order.
- `mdx_ui.huntress_lowercase_portrait_and_idle_only_model`: lowercase names and
  an authored portrait without a talk track.
- `renderer_cursor.gate_hover_animation_lifecycle`: normal → select → scrolling
  → normal, tint, and animation epoch persistence.
- `wc3_api.customize_entity_gate_hover_lifecycle`: snapshot name/localization,
  neutral relation, no unit bars, death, hiding and unselectable restoration.
  The fixture deliberately resolves the key to `Ancient Elven Gate`.

```sh
make test-mdx-ui
make test-renderer-model
make test-wc3-engine WC3_PATTERN=wc3_api.customize_entity_gate_hover_lifecycle
```

Ignored local evidence is under `build/portrait-audit/`: hash-checked
`retail.py`, `retail.jsonl`, Ghidra JSON/xrefs, model metadata,
`retail-speaking.mp4`, `native-speaking.mp4`, `native.jsonl`,
`retail-gate.png`, and before/after test logs. The initial shared-session
recording is not comparison evidence; use the isolated `retail-speaking` run.

See [triggered dialogue](triggered-dialogue.md),
[scroll cursors](scroll-cursors.md), and [NightElfX01 flow](nightelfx01-flow.md).

Final validation: MDX UI tests passed (4 tests, 25 assertions); renderer-model
checks passed (106 tests, 5547 assertions); snapshot customization tests passed
in both ROC and TFT modes (21 tests, 74 assertions each). Separate ROC and TFT
`wc3_*` runs each passed 1945 tests / 34952 assertions. `make test TEST_JOBS=4`
was attempted again after the fixes and failed in
`client_input.menu_sdl_input_is_exclusive_with_world_presentation`.
The debugger located the SIGSEGV inside `/usr/lib/libSDL3.so.0`, called through
`libSDL2-2.0.so.0` at `client/cl_input.c:1528`. The preceding save-game messages
are not its stack. This is the same input-library failure recorded by the
previous NightElfX01 investigation; the overall suite is not green.
`full-crash-final.txt` retains the backtrace. `make openwarcraft3` succeeded.
