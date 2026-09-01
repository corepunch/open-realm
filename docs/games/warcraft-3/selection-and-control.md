# Unit Selection And Control

## Contract

Warcraft III selection is server-authoritative and keeps three decisions separate:

```text
visible/selectable -> relationship presentation -> control authority
```

`games/warcraft-3/game/g_commands.c` owns the shared policy:

- `G_UnitCanBeSelected(client, ent)` accepts a live `SVF_MONSTER` that is in use, not hidden, not `EF_NOT_SELECTABLE`, and actively visible through `G_FowPlayerCanHoverEntity`.
- `G_SelectionRelation(viewer, ent)` returns friend, neutral, or enemy independently of command authority.
- `G_UnitCanControl(client, ent)` is a pure authority check: it accepts locally owned units and passive allies that grant `ALLIANCE_SHARED_CONTROL`, independent of fog/hover/selectability state. Active selection/order paths separately reject dead, hidden, and unselectable entities. Neutral Hostile and Neutral Passive slots never become controllable through alliance-bit accidents.

This means a visible foreign unit may be inspected without giving the viewer ownership or order authority.

## Selection Flow

`client/cl_input.c` sends entity numbers through `select`. `CMD_Select` revalidates every candidate server-side; client picking is not authority.

For rectangle selection, a controllable non-building unit has the existing WC3-style mobile-unit preference. If one is present, buildings and non-controllable foreign entries are dropped from that selection. If no controllable mobile unit is present, all selectable entries may be selected, including enemy and neutral units. The authoritative server selection is capped at 12 entries even though the generic client-side cache remains sized by `MAX_SELECTED_ENTITIES`.

Targeted ability callbacks (`menu.on_entity_selected`) are a separate path: a left click completes the pending target action instead of replacing the unit selection.

## Relationship Presentation

`G_CustomizeEntity` converts `G_SelectionRelation` to recipient-relative entity flags:

| Relationship | Snapshot flag | Ring family |
|---|---|---|
| friend / own / shared control | neither | green |
| passive ally / Neutral Passive | `EF_NEUTRAL` | yellow |
| enemy / Neutral Hostile | `EF_HOSTILE` | red |

The neutral player slots are `PLAYER_NEUTRAL_AGGRESSIVE == 12` and `PLAYER_NEUTRAL_PASSIVE == 15`.

`renderer/r_ents.c` uses the same flags for both hover and full selected circles. The selection-circle texture and radius remain the existing WC3 data/model choice; relationship colours are currently renderer constants rather than `SelectionCircle/ColorFriend`, `ColorNeutral`, and `ColorEnemy` skin data.

## Control Boundary

Never use `FOR_SELECTED_UNITS` for a player-issued multi-unit order. Use:

```c
FOR_CONTROLLABLE_SELECTED_UNITS(client, ent)
```

The controllable filter is used by Smart/SmartPoint, Move, Attack/Attack-move, Stop, Hold Position, Patrol, Repair, Harvest/Return Resources, and Rally target callbacks. `CMD_Button`, `CMD_Research`, inventory use/drop, cancellation, training, and Rally command entry also validate the focused unit before acting.

`Get_Commands_f` clears the command card for a selected unit that the local player cannot control. A foreign building may still use the ordinary inspection panel, but its production queue is not serialized to the viewer. Shared-control units retain command-card access.

Selection acknowledgement voices are queued only for controllable selections. Ordinary non-Neutral-Passive foreign selections use the `InterfaceClick` UI sound instead of playing the selected unit's authored `What` response. Neutral Passive critter-specific response rules remain unresolved, so that owner class is not forced through the generic click path.

## Selection Lifetime

`G_UpdateClientSelections` runs immediately after `G_FowUpdate` each server frame. It scans the raw per-player selection bits (not `G_IsEntitySelected`, which intentionally hides already-invalid entities), removes any entry that no longer passes `G_UnitCanBeSelected`, then refreshes portrait/info/inventory and command presentation for connected clients.

Consequences:

- an enemy that leaves active vision stops being selected;
- a hidden or newly unselectable unit has its stale selection bit removed.

Death has a stronger immediate path in `unit_die`: it sets health to zero, clears all selection bits, sets `EF_NOT_SELECTABLE`, releases any held animation frame, and starts the death animation. The order entry points reject dead units as well, so a corpse cannot be picked or receive a new movement/order that would replace its death animation. `G_ReviveHero` clears `EF_NOT_SELECTABLE` when the persistent Hero edict is revived.

## Known Gaps

The following are deliberately not inferred by the current implementation:

- invisibility/detection-aware selectability (`IsUnitDetected`/`IsUnitInvisible` coverage is incomplete);
- Shift-click toggle semantics (Shift-drag addition exists separately);
- Ctrl-click and double-click same-type expansion;
- WC3 priority/level/rawcode sorting of the 12 selected entries;
- neutral-shop patron interaction (`Aneu`/`Apit` remains unfinished);
- data-driven `SelectionCircle` relationship colours;
- Neutral Passive critter-specific selection response rules;
- exact retail behavior for `ALLIANCE_SHARED_ADVANCED_CONTROL`.

Do not bypass these gaps by weakening `G_UnitCanControl` or by restoring owner checks inside selection itself. Selection and command authority must remain separate.

## Verification

In-engine coverage is in `games/warcraft-3/game/tests/t_api.c` and `t_unit.c` for relationship classification, visible foreign selectability, shared-control authority, dead-unit non-selectability, selection removal, and Hero revival restoring selectability.

Useful targeted commands after building the test binary:

```bash
make test-wc3-engine WC3_PATTERN='wc3_api.*selection*'
make test-wc3-engine WC3_PATTERN='wc3_api.*control*'
make test-wc3-engine WC3_PATTERN='wc3_unit.*die*'
```

Runtime verification should also cover an enemy walking from visible terrain into fog, Neutral Passive/Hostile circle colours, and attempting Smart/command-card orders while a foreign unit is selected.
