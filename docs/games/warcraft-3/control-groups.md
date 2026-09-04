# Control Groups

## Contract

Warcraft III control groups allow players to assign a selection of units to a number key (0–9), append newly selected units to an existing group, and recall that group later. The stored group is entirely client-side; recalling it uses the normal server-authoritative selection mechanism.

## Key Bindings

| Key | Action |
|-----|--------|
| Ctrl+0–9 | Replace the control group with the current selection |
| Shift+0–9 | Append the current selection to the control group without changing the active selection |
| 0–9 | Recall the control group |

If Ctrl and Shift are both held, Ctrl assignment takes precedence. This preserves the existing Ctrl+number replacement path and avoids interpreting Ctrl+Shift+number as an append.

## Implementation

### Storage

Control groups are stored in static arrays in `client/cl_input_w3.c`:

```c
static DWORD cg_ids[10][MAX_SELECTED_ENTITIES];
static DWORD cg_count[10];
```

Each group stores up to `MAX_SELECTED_ENTITIES` (64) entity IDs. Warcraft III's authoritative `CMD_Select` path currently applies `WC3_SELECTION_LIMIT` (12) when the group is recalled, so the client storage ceiling is intentionally broader than the server-side simultaneous-selection ceiling.

### Assign (Ctrl+N)

When Ctrl+0–9 is pressed:
1. The current `cl.selection.num_selected` and `cl.selection.entity_nums` replace the stored group.
2. The count is clamped to `MAX_SELECTED_ENTITIES`.
3. Control-group double-tap state is reset.

Assignment does not itself send a selection command because the assigned units are already the active selection.

### Append (Shift+N)

When Shift+0–9 is pressed:
1. `CL_ControlGroupAppendUnique` in `client/cl_control_groups.c` walks the current selection in order.
2. IDs already present in the group are skipped.
3. New IDs are appended after existing members.
4. Existing members win when `MAX_SELECTED_ENTITIES` capacity is reached.
5. The active `cl.selection` is left unchanged and no `select` command or unit-UI refresh is sent.
6. Control-group double-tap state is reset so Shift+N cannot count as the first half of a camera-focus double tap.

Appending to an empty group therefore behaves like assignment, while appending a partially overlapping selection adds only new entity IDs.

Keeping append separate from recall is important for the Warcraft III workflow: a player can select newly produced units, Shift+N them into a larger army group, then immediately issue an order only to those newly selected units.

### Recall (N)

When 0–9 is pressed (without Ctrl or Shift):
1. If `cg_count[g] > 0`, `CL_ApplySelection` is called with the stored IDs.
2. `CL_ApplySelection` writes a `select` command to the netchan and updates the local selection cache.
3. `CL_RequestUnitUI` refreshes the UI with the new local selection hint.
4. The server's Warcraft III `CMD_Select` path remains authoritative and filters/reconciles the actual selected unit set.

### Camera Focus

Recall always selects immediately. A second deliberate press of the same group within `CL_CONTROL_GROUP_DOUBLE_TAP_MS` (500 ms) centers the camera on the average position of live, modeled, selectable members. Key-repeat events are consumed but do not count as a second deliberate press.

Ctrl+N and Shift+N both reset double-tap recall state.

## Modal Window Interaction

`CL_HandleGameKey` is called from `CL_Input` in the main event loop. The function checks `CL_GameplayInputReady()` before processing any keys. `CL_GameplayInputReady()` returns `false` when:

- `cls.key_dest != key_game`
- `cls.state != ca_active`
- `cl.playerstate.client_ui_state != CLIENT_UI_GAME`
- `SCR_LayoutModalActive()` returns true (WC3 only)

This means all digit presses, including Ctrl+N assignment and Shift+N append, are swallowed while a modal window is open. This is intentional: blocking control-group mutation behind Quest/Log/modals prevents accidental group changes during UI interactions.

## Map Lifecycle Reset

`CL_InputModeResetMap` clears all group IDs, counts, and double-tap timing. `CL_MapLoading` calls it when a new map begins, so entity IDs from a previous map cannot be recalled in the new map.

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

`games/warcraft-3/tests/test_control_groups.c` covers the pure append contract:

- existing-order preservation and duplicate suppression;
- append-to-empty behavior;
- existing-member priority at capacity.

The network parser has rejection coverage in `tests/test_net.c`; in-engine selection behavior is covered by `games/warcraft-3/game/tests/t_api.c`.

## References

- `client/cl_input_w3.c` — Warcraft III control-group key handling and stored groups
- `client/cl_control_groups.c` — pure append/deduplication helper
- `client/cl_input.c` — main input dispatch, calls `CL_HandleGameKey`
- `client/cl_parse.c` — `CL_ParseSetSelection` for server-authoritative selection reconciliation
- `games/warcraft-3/game/g_commands.c` — authoritative `CMD_Select` filtering and 12-unit selection ceiling
- `docs/games/warcraft-3/selection-and-control.md` — server-authoritative selection contract
