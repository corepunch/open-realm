# Warcraft III gamepad controls (OpenRealm)

This document describes the SDL2 controller layer for OpenRealm's Warcraft III
module. It is an **OpenRealm design**, not a claim about retail Warcraft III's
controller support. Keyboard/mouse and controller input remain available together.

## Enablement and button style

The installed Warcraft III `share/config.cfg` enables controller support. SDL2
`GameController` abstraction supports compatible Xbox, Nintendo, PlayStation and
mapped USB/Bluetooth pads. Only the first compatible connected controller is
used. Disconnecting it clears held controller inputs; another controller can be
picked up when the input loop next runs.

| Archived console cvar | Default | Meaning |
|---|---:|---|
| `cl_gamepad` | `1` (WC3) | Enable controller input; `0` disables it |
| `cl_gamepad_nintendo` | `0` | `0`: Xbox action labels; `1`: swap A/B and X/Y *functions* for Nintendo-style labels |
| `cl_gamepad_deadzone` | `0.2` | Deadzone of thumbsticks / triggers |
| `cl_gamepad_hold_ms` | `180` | A-hold threshold for the paint-selection brush |
| `cl_gamepad_double_ms` | `350` | Same-type double-tap window |
| `cl_gamepad_group_hold_ms` | `550` | RT+D-pad hold threshold for assigning groups |
| `cl_gamepad_paint_radius` | `72` | Brush radius, in on-screen pixels (clamped to 24–200) |
| `cl_gamepad_zoom_rate` | `3.0` | Right-stick zoom rate (mouse-wheel steps per second) |
| `cl_gamepad_menu_cursor_speed` | `600` | Left-stick cursor speed in modal windows, pixels/second |
| `cl_gamepad_fast_pan_multiplier` | `5.0` | Left-stick camera pan multiplier while LB is held |

For example `seta cl_gamepad_nintendo "1"` in the user console or config
makes the physical Nintendo A/B and X/Y functions follow their Nintendo labels.
The option does **not** modify the OS/SDL button mapping database.

## Gameplay controls (Xbox labels)

| Input | Behaviour |
|---|---|
| Left stick | Pan camera like the arrow keys (camera-relative) |
| LB, hold | Pan the camera 5× faster with the left stick |
| LB, tap | Toggle status-panel cursor mode; D-pad moves between selected unit icons |
| LB, tap again or B | Exit status-panel cursor mode |
| A in status-panel cursor mode | Select/focus the icon under the cursor; completes an active unit-target spell |
| Right stick up/down | Zoom in/out within existing Warcraft player camera limits |
| Right-stick click | Reset camera to the game-selected default zoom |
| A, tap | Normal left-click selection / HUD click; accept the focused radial command when it is open |
| A, hold | Draw a circular paint ring; first hit replaces selection, RT adds, or LT removes; later hits apply live |
| A, double-tap | Select all visible selectable units of the same type as the unit under the cursor |
| B | Cancel the radial menu when open; otherwise cancel the current command, selection mode, or menu (Esc) |
| X | Contextual right click (move, interact, attack, rally, etc.); toggle autocast for a focused autocast command while the radial is open |
| Y, tap and release | Open the bottom-right ten-sector radial menu from real clickable command-card icons |
| Y, hold | Choose a radial-menu sector; release Y to activate the focused command |
| Left stick while radial is open | Choose a radial-menu sector |
| RT while radial is open | Advance to the next populated radial page |
| Select / Back | Escape (cancel the current command or close the active menu) |
| RB, tap | Select visible on-screen units except units currently gathering resources |
| RB, double-tap | Select all visible units on-screen, including gatherers |
| RT (tap and release) | Cycle the focused unit subgroup, like Tab (not when used with another control) |
| LT (tap and release) | Cycle the focused unit subgroup backward |
| D-pad up | Set global rally for controllable unit-producing buildings at cursor world position |
| D-pad left | Jump camera through owned/controlled town halls; highest upgrade tier first, then oldest instance |
| D-pad down | Jump through nearby clusters of *currently attacking* controllable units |
| D-pad right | Jump to recent alert location, like Space (`quickcamera`) |
| RT + A, tap | Add cursor unit to selection |
| RT + A, hold | Paint-select additively, preserving the existing selection |
| LT + A, hold | Paint-select subtractively, removing touched units from selection |
| RT + B, tap | Remove cursor unit from selection |
| RT + A, double-tap | Add same-type units visible on camera to selection |
| RT + B, double-tap | Remove same-type units visible on camera from selection |

The cursor recentres on gamepad activity to the **centre of the rendered camera
viewport**, not the whole window (the bottom HUD occupies non-gameplay space).
The normalized viewport rectangle is converted to top-left-origin SDL pixels.
Moving the physical mouse keeps the normal mouse cursor behaviour. The left
stick pans the terrain
*under* this centred cursor; holding A paints as new units move through its
visible circular ring. Unit ownership, fog of war, selected-unit capacity,
order validation and other selection restrictions remain enforced by existing
gameplay paths on the server.

### Control groups

| RT + D-pad | Group number | Tap | Hold (550 ms default) | Double-tap |
|---|---:|---|---|---|
| Up | 1 | Recall | Assign current selection | Recall and centre camera |
| Right | 2 | Recall | Assign current selection | Recall and centre camera |
| Down | 3 | Recall | Assign current selection | Recall and centre camera |
| Left | 4 | Recall | Assign current selection | Recall and centre camera |

Double-tap camera centring follows the **existing** client control-group
implementation, which compares recall timestamps. Assignment and recall use
the very same local numbered groups as Ctrl+1–4 and 1–4 on the keyboard.

### Menu shortcuts and modal navigation

| Combination | Action |
|---|---|
| Start + D-pad up | Quick save (`save quick`) |
| Start + D-pad down | Quick load (`load quick`) |
| Start + LB | Open Quests (F9) |
| Start + RB | Open main game Menu (F10) |
| Start + LT | Open Allies (F11) |
| Start + RT | Send `chat` shortcut — **not yet implemented by baseline WC3 game module** |

In a modal menu, left stick moves the mouse cursor instead of the camera. D-pad
moves to the nearest clickable server-authored button in that direction; native
client menus fall back to their keyboard arrow navigation. A clicks, B cancels.
The radial menu remains open after Y is released. A activates the focused
command and closes it; B closes it without issuing the command. Pressing Y
while the radial is open closes it without issuing the focused command.
Hold-to-confirm
is enabled by default through the archived `cl_gamepad_radial_hold_confirm`
cvar. Holding Y opens the menu after 250 ms, and releasing Y activates the
focused command. Set the cvar to `0` to use only tap-to-open and A-to-confirm.
Mouse movement is ignored while the radial menu is open. After confirming or
cancelling, return the left stick to its deadzone before it resumes camera
movement; this prevents the selection direction from panning the camera as soon
as the menu closes.

Tap LB (press and release within 250 ms) to enter status-panel cursor mode.
The cursor starts on the currently focused selected-unit icon. D-pad left/right
moves between icons in the current row; up/down moves one row while preserving
the column where possible. A clicks the icon through the ordinary multiselect
path, so selecting a unit or completing an active unit-target spell remains
server-authoritative. Tap LB again or press B to leave the mode. Holding LB
instead keeps its fast camera-pan modifier active.

**Current limitation:** The supplied OpenRealm baseline defines a `ChatDialog`
FDF binding but does not expose a working gameplay `chat` game command or chat
window. Start+RT is reserved and sends that command; it cannot open a working
chat UI until the game module implements one. The screenshot's Chat(F12) button
should not be interpreted as evidence that the feature is wired up.

## Ten-sector radial layout

The ten sectors are numbered **0–9 clockwise**, with sector 0 at 12 o'clock,
sector 5 at 6 o'clock, and the left-stick vector selecting the nearest sector.
Only *enabled, actually clickable* game command buttons are placed in the menu.
Passive command indicators and disabled buttons are excluded. While the radial
is open, the ordinary square command-button grid is hidden. The radial uses
translucent black sector backgrounds, with the focused sector filled in the
player's team colour. The ring radius and each sector's square icon are scaled
from the actual HUD button size (and fitted to small windows); icons sit at the outer edge of their sectors,
with their outer edges aligned to the radial ring. All ten sectors have enough
separation for a complete icon. Each sector draws its **own command button's** original texture
coordinates, tint, active glow, and cooldown shade, not a placeholder icon.

Moving the left stick into an occupied sector highlights that command. The
focused command shows **the same localized/dynamic tooltip** that hovering the
corresponding ordinary command button with a mouse would show, using the
existing Warcraft tooltip font, border, backdrop and text wrapping. The tooltip
is positioned to the left of the radial menu to keep command icons visible.
Releasing the stick to neutral clears focus and its tooltip; releasing Y over a
focused command activates it through the existing validated command path.
Stick up selects the 12 o'clock sector, down selects 6 o'clock, and left/right
select the nearest corresponding sectors. The selected sector is filled with
the local player's configured Warcraft III team color.

### Ordinary unit/building command cards

The real Warcraft 4×3 command positions are identified as top `C1..C4`,
middle `B1..B4`, bottom `A1..A4`.

| Sector (clockwise) | Page 1 | Page 2 |
|---:|---|---|
| 0 (12 o'clock) | B4 | B1 |
| 1 | C3 | Inventory 3 |
| 2 | C4 | Inventory 4 |
| 3 | A4 | — |
| 4 | A3 | Inventory 6 |
| 5 (6 o'clock) | B3 | B2 |
| 6 | A2 | — |
| 7 | A1 | Inventory 5 |
| 8 | C1 | Inventory 1 |
| 9 | C2 | Inventory 2 |

This ordering preserves the centre placements at 12/6 o'clock even though a
command card has 12 positions and the radial has only ten; B1 and B2 are
therefore on page 2. The six inventory slots share that second page.

### Construction menus

Construction commands are flagged by the WC3 HUD authoring code, without
requiring the client to guess from an ability rawcode. They appear sequentially
starting at noon, then clockwise. Items 1–10 occupy page 1, 11–12 page 2.
Tap and release Y to open the radial, then RT to change page and A to confirm
the highlighted sector. With `cl_gamepad_radial_hold_confirm` enabled, holding Y
also opens it and confirms the highlighted command when released. Selecting a
command still uses the original server-authored command
string and ordinary validation/targeting logic.

## Implementation ownership

* `client/cl_input.c`: SDL controller lifecycle, bindings, tap/hold timing,
  client-side candidate selection, camera input, menu pointer events.
* `client/cl_scrn.c`: paints circular brush, collects server-authored
  clickable UI frames for the ten-slot radial / directional menu navigation,
  and exposes multiselect status-panel icon geometry for controller navigation,
  and reuses the authored command icon and tooltip presentation when radial-focused.
* `renderer/r_draw.c`: pixel-space selection circle and radial-sector outlines.
* `games/warcraft-3/game/hud/hud_commands.c` and `skills/s_build.c`: explicit
  construction-command metadata, sent in a shared UI flag.
* `games/warcraft-3/game/g_commands.c`: permission-checked global rally,
  ranked town hall camera cycle and active attacker-cluster camera cycle.
* `games/warcraft-3/share/config.cfg`: per-game defaults.

The client sends normal commands to the existing WC3 server. The implementation
does not add gameplay-specific save fields or change map/JASS logic.

## Suggested manual verification (not performed for this patch)

Verify on an Xbox-labelled pad and a Nintendo-layout pad: mouse/gamepad cursor
handoff and viewport-centred positioning (especially with bottom HUD); both
camera sticks and reset; single click, hold-paint and double tap;
all four RT add/remove variants; 12-slot unit/build menus and inventory page;
black corner coverage, correct per-sector icons and stick-hover tooltip parity;
no display of command indicators/disabled commands; radial targeting and
spell orders; all four D-pad quick actions; control-group assign/recall/centre;
Start combinations; both native and server-authored modals; save/load;
controller unplug/replug; 4:3, 16:9, and 640×480 canvases. No build or tests
were run by the patch author, per request.
