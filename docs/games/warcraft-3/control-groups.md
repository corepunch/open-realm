# Control Groups

## Contract

Warcraft III control groups allow players to assign a selection of units to a number key (0–9) and recall that selection later with a single keypress. The implementation is entirely client-side and does not involve server authority beyond the normal selection mechanism.

## Key Bindings

| Key | Action |
|-----|--------|
| Ctrl+0–9 | Assign current selection to the control group |
| 0–9 | Recall the control group |

## Implementation

### Storage

Control groups are stored in static arrays in `client/cl_input_w3.c`:

```c
static DWORD cg_ids[10][MAX_SELECTED_ENTITIES];
static DWORD cg_count[10];
```

Each group stores up to `MAX_SELECTED_ENTITIES` (64) entity IDs.

### Assign (Ctrl+N)

When Ctrl+0–9 is pressed:
1. The current `cl.selection.num_selected` and `cl.selection.entity_nums` are copied to `cg_count[g]` and `cg_ids[g]`.
2. The count is clamped to `MAX_SELECTED_ENTITIES`.

### Recall (N)

When 0–9 is pressed (without Ctrl):
1. If `cg_count[g] > 0`, `CL_ApplySelection` is called with the stored IDs.
2. `CL_ApplySelection` writes a `select` command to the netchan and updates `cl.selection`.
3. `CL_RequestUnitUI` refreshes the UI with the new selection.

### Camera Focus

Recall always selects immediately. A second deliberate press of the same group within `CL_CONTROL_GROUP_DOUBLE_TAP_MS` (500 ms) centers the camera on the average position of live, modeled, selectable members. Key-repeat events are consumed but do not count as a second deliberate press.

## Modal Window Interaction

`CL_HandleGameKey` is called from `CL_Input` in the main event loop. The function checks `CL_GameplayInputReady()` before processing any keys. `CL_GameplayInputReady()` returns `false` when:

- `cls.key_dest != key_game`
- `cls.state != ca_active`
- `cl.playerstate.client_ui_state != CLIENT_UI_GAME`
- `SCR_LayoutModalActive()` returns true (WC3 only)

This means all digit presses (including Ctrl+N for assign) are swallowed while a modal window is open. This is intentional: blocking reassignment behind Quest/Log/modals prevents accidental group changes during UI interactions.

## Map Lifecycle Reset

`CL_InputModeResetMap` clears all group IDs, counts, and double-tap timing. `CL_MapLoading` calls it when a new map begins, so entity IDs from a previous map cannot be recalled in the new map.

## Filtering on Recall

Recall sends the stored IDs to the server, where the authoritative selection path filters invalid, dead, hidden, fogged, and non-selectable entities. Camera centering independently ignores IDs that are invalid, unmodeled, dead, or marked non-selectable; the group storage itself remains unchanged until reassigned or the map resets.

## Lifetime

Groups are client-local and last until reassignment, map reset, or process exit. They are not persisted across game sessions.

## Verification

### Manual Testing

1. **Rapid double-tap on a group that has units off-screen**: Camera should recenter only on the second press within 500 ms, not the first; holding the key should not recenter.

2. **Load a new map with existing control groups assigned**: Confirm they are empty on the new map.

### Automated Tests

The network parser has rejection coverage in `tests/test_net.c`; in-engine selection behavior is covered by `games/warcraft-3/game/tests/t_api.c`.

## References

- `client/cl_input_w3.c` — Control group storage and key handling
- `client/cl_input.c` — Main input dispatch, calls `CL_HandleGameKey`
- `client/cl_parse.c` — `CL_ParseSetSelection` for server-authoritative selection
- `docs/games/warcraft-3/selection-and-control.md` — Server-authoritative selection contract
