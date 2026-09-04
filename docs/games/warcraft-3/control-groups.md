# Control Groups

## Contract

Numbered control groups are a generic client feature stored on `cl.groups`. Warcraft III and StarCraft II ship default 0–9 binds; other games can bind `group` the same way. Recalling a group uses the normal server-authoritative selection mechanism.

## Key Bindings

Shipped in `games/warcraft-3/share/config.cfg` and `games/starcraft-2/share/config.cfg`. Do **not** add number-key handling in `CL_Input`; new gameplay hotkeys belong in `bind` lines. WoW does not ship these binds because 0–9 are action-bar slots.

| Key | Bind | Action |
|-----|------|--------|
| Ctrl+0–9 | `bind CTRL+N "group assign N"` | Replace the control group with the current selection |
| Shift+0–9 | `bind SHIFT+N "group add N"` | Append the current selection without changing the active selection |
| 0–9 | `bind N "group N"` | Recall the control group |

Strokes match exactly in ctrl, alt, shift order (see [modifier key binds](../../architecture/runtime.md#key-bindings)). `CTRL+SHIFT+N` is a separate bind from `CTRL+N`; both assign.

## Implementation

### Storage

Groups live on `struct client_state` next to `cl.selection`:

```c
struct {
    DWORD entity_nums[MAX_SELECTED_ENTITIES];
    DWORD num_selected;
} groups[MAX_CONTROL_GROUPS];
DWORD group_last;
DWORD group_last_ms;
```

Each group stores up to `MAX_SELECTED_ENTITIES` (64) entity IDs. Warcraft III's authoritative `CMD_Select` path currently applies `WC3_SELECTION_LIMIT` (12) when the group is recalled, so the client storage ceiling is intentionally broader than the server-side simultaneous-selection ceiling.

### Command

`group` is registered in `CL_InitInput` via `CL_ControlGroupsInit`. Config binds invoke it through `Key_Event` / `Cbuf_AddText` like every other gameplay hotkey.

| Command | Effect |
|---------|--------|
| `group N` | Recall group N |
| `group add N` | Append the current selection to group N |
| `group assign N` | Replace group N with the current selection |

The command no-ops unless `CL_GameplayInputReady()` and no client window is modal, so Quest/Log cannot mutate groups behind the overlay.

### Assign (Ctrl+N)

When `group assign N` runs:
1. The current `cl.selection` replaces `cl.groups[N]`.
2. The count is clamped to `MAX_SELECTED_ENTITIES`.
3. Control-group double-tap state is reset.

Assignment does not itself send a selection command because the assigned units are already the active selection.

### Append (Shift+N)

When `group add N` runs:
1. `CL_ControlGroupAppendUnique` in `client/cl_control_groups.c` walks the current selection in order.
2. IDs already present in the group are skipped.
3. New IDs are appended after existing members.
4. Existing members win when `MAX_SELECTED_ENTITIES` capacity is reached.
5. The active `cl.selection` is left unchanged and no `select` command or unit-UI refresh is sent.
6. Control-group double-tap state is reset so Shift+N cannot count as the first half of a camera-focus double tap.

Appending to an empty group therefore behaves like assignment, while appending a partially overlapping selection adds only new entity IDs.

Keeping append separate from recall is important for the Warcraft III workflow: a player can select newly produced units, Shift+N them into a larger army group, then immediately issue an order only to those newly selected units.

### Recall (N)

When `group N` runs:
1. If `cl.groups[g].num_selected > 0`, `CL_ApplySelection` is called with the stored IDs.
2. `CL_ApplySelection` writes a `select` command to the netchan and updates the local selection cache.
3. `CL_RequestUnitUI` refreshes the UI with the new local selection hint.
4. The server's Warcraft III `CMD_Select` path remains authoritative and filters/reconciles the actual selected unit set.

### Camera Focus

Recall always selects immediately. A second deliberate press of the same group within `CL_GROUP_TAP_MS` (500 ms) centers the camera on the average position of live, modeled, selectable members. SDL key-repeat is ignored for gameplay `Key_Event` so holding a number cannot count as the second tap.

`group assign` and `group add` both reset double-tap recall state.

## Modal Window Interaction

`Key_Event` calls `CL_WindowKeyEvent` before looking up binds, so a focused or modal client window consumes the key. `group` also refuses to run when `CL_GameplayInputReady()` is false or `CL_WindowModalActive()` is true:

- `cls.key_dest != key_game`
- `cls.state != ca_active`
- `cl.playerstate.client_ui_state != CLIENT_UI_GAME`
- `SCR_LayoutModalActive()` returns true (WC3 only)
- a `UI_WINDOW_MODAL` client window is open

This blocks control-group mutation behind Quest/Log/modals.

## Map Lifecycle Reset

`CL_ControlGroupsReset` clears `cl.groups` and double-tap timing. `CL_BeginLoadingMap` and `CL_ClearState` call it so entity IDs from a previous map cannot be recalled in the new map.

## Filtering on Recall

Recall sends the stored IDs to the server, where the authoritative Warcraft III selection path filters invalid, dead, hidden, fogged, non-selectable, and otherwise ineligible entities. Camera centering independently ignores IDs that are invalid, unmodeled, dead, or marked non-selectable.

The client deliberately does **not** prune stored group membership based only on its current entity snapshot. Client visibility/state may be incomplete, while the server owns final selection legality; eagerly deleting an ID during append or recall could therefore destroy a still-valid control-group member. Stored IDs remain until reassignment, map reset, or process exit.

## Lifetime

Groups are client-local and last until reassignment, map reset, or process exit. They are not persisted across game sessions.

## Verification

### Manual Testing

1. **Append without recall**: assign A+B to group 1, select C+D, press Shift+1. C+D must remain selected. Press 1 afterward and confirm A+B+C+D are recalled subject to the server selection limit/filtering.
2. **Deduplication**: assign A+B+C, then select B+C+D and press Shift+1. Recalling group 1 must preserve A+B+C+D ordering without duplicate B/C entries.
3. **Empty-group append**: with an unused group, select A+B and press Shift+N. Pressing N afterward must recall A+B.
4. **Capacity**: append to a full stored group and confirm existing members are never displaced by later additions.
5. **Rapid double-tap on a group that has units off-screen**: Camera should recenter only on the second unmodified number press within 500 ms, not after Ctrl+N or Shift+N; holding the key should not recenter.
6. **Load a new map with existing control groups assigned**: Confirm they are empty on the new map.

### Automated Tests

`games/warcraft-3/tests/test_control_groups.c` (`client_groups.*`) covers the pure append contract:

- existing-order preservation and duplicate suppression;
- append-to-empty behavior;
- existing-member priority at capacity.

`games/warcraft-3/tests/test_keys.c` covers `SHIFT+1` / `CTRL+1` / `ALT+MOUSE1` bind-name parsing and Ctrl-over-Shift slot selection.

The network parser has rejection coverage in `tests/test_net.c`; in-engine selection behavior is covered by `games/warcraft-3/game/tests/t_api.c`.

## References

- `client/client.h` — `cl.groups` / `cl.group_last` on `struct client_state`
- `client/cl_control_groups.c` — `group` command and append helper
- `games/warcraft-3/share/config.cfg` — default `group` / `SHIFT+N` / `CTRL+N` binds
- `client/cl_input_w3.c` — `+camleft`/`+camright`/`+camnorth`/`+camsouth`
- `client/keys.c` / `client/keys_name.h` — modifier bind table and lookup
- `client/cl_input.c` — SDL events to `Key_Event`
- `client/cl_parse.c` — `CL_ParseSetSelection` for server-authoritative selection reconciliation
- `games/warcraft-3/game/g_commands.c` — authoritative `CMD_Select` filtering and 12-unit selection ceiling
- `docs/games/warcraft-3/selection-and-control.md` — server-authoritative selection contract
- [Modifier key binds](../../architecture/runtime.md#key-bindings)
