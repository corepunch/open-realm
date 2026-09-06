# Unit Selection And Control

## Contract

Warcraft III selection is server-authoritative and keeps three decisions separate:

```text
visible/selectable -> relationship presentation -> control authority
```

`games/warcraft-3/game/g_commands.c` owns the shared policy:

- `G_UnitCanBeSelected(client, ent)` accepts a live `SVF_MONSTER` that is in use, not hidden, not `EF_NOT_SELECTABLE`, and actively visible through `G_FowPlayerCanHoverEntity`.
- `G_SelectionRelation(viewer, ent)` returns friend, neutral, or enemy independently of command authority.
- `G_UnitCanControl(client, ent)` is a pure authority check: it accepts locally owned units and passive allies that grant `ALLIANCE_SHARED_CONTROL`, independent of fog/hover/selectability state. Active selection/order paths separately reject dead, hidden, and unselectable entities. Reserved neutral owners use the same alliance table; their default state simply does not grant shared control.
- Alliance state is directional, matching the JASS `SetPlayerAlliance(source, other, type, value)` contract. `G_SetPlayerAlliance` changes only `level.alliances[source][other]`; callers that want mutual alliance must set both directions. Friend/enemy tests use `ALLIANCE_PASSIVE` specifically—shared vision, shared XP, or shared-control bits by themselves do not make a hostile unit friendly.

`G_InitPlayerAlliances()` establishes Warcraft's map-start defaults before map
script execution. Reserved `PLAYER_NEUTRAL_PASSIVE` (15) receives a bilateral
`ALLIANCE_PASSIVE` relation with every player. Ordinary W3I slots below the four
reserved neutral owners whose `playerType == kPlayerTypeNeutral` receive the same
bilateral passive defaults. `PLAYER_NEUTRAL_AGGRESSIVE` (12), Neutral Victim
(13), and Neutral Extra (14) are not made passive allies by that rule. These are
initial values, not permanent owner-ID exceptions: later `SetPlayerAlliance`
calls may revoke Neutral Passive friendship or ally Neutral Aggressive.

Relationship consumers use `G_PlayerTreatsPlayerAsAlly(source, other)`. The
arguments are deliberately directional: the acting/querying player is the
source and the target unit's owner is the other player. JASS `IsUnitAlly` and
`IsUnitEnemy`, Smart orders, Follow, spell relationship checks, selection-ring
classification, and automatic acquisition use that same direction.

This means a visible foreign unit may be inspected without giving the viewer ownership or order authority.

## Selection Flow

`client/cl_input.c` sends entity numbers through `select`. `CMD_Select` revalidates every candidate server-side; client picking is not authority.

Presentation-only JASS `effect` handles are deliberately outside this widget-selection contract. `G_SpawnModelEffect()` marks their edicts `EF_NOT_SELECTABLE`, which becomes `RF_NOT_SELECTABLE` client-side.  Their MDX still renders, but `R_TraceEntity()` and rectangle selection ignore it so clicks pass through to real widgets or terrain.  This matches the JASS type hierarchy (`effect extends agent`, not `widget`) and prevents tutorial waypoint/special-effect art from intercepting right-click movement.

For rectangle selection, a controllable non-building unit has the existing WC3-style mobile-unit preference. If one is present, buildings and non-controllable foreign entries are dropped from that selection. If no controllable mobile unit is present, all selectable entries may be selected, including enemy and neutral units. The authoritative server selection is capped at 12 entries even though the generic client-side cache remains sized by `MAX_SELECTED_ENTITIES`.

Persistent Hero and idle-worker HUD shortcuts reuse this authority boundary but have a separate retained HUD/cycling lifecycle; see [Persistent Hero And Idle-Worker Shortcuts](unit-shortcuts.md). Shortcut-driven server selections are mirrored back into the client selection cache with the existing `GameCommand` transport.

Targeted ability callbacks (`menu.on_entity_selected`) are a separate path: a left click completes the pending target action instead of replacing the unit selection.

## Focused Unit In A Multi-Selection

Selection membership and focused-unit presentation are separate state. The server
retains one focused entity from the current selection; `G_GetMainSelectedUnit()`
returns that entity while it remains selected, then falls back to the first unit
in canonical Warcraft selection order if the focus becomes invalid.
`G_SelectEntity()` still establishes an initial focus while membership is being
built, while the completed `select` command replaces that provisional choice with
the first canonically ordered unit. `G_FocusSelectedUnit()` changes focus without
changing any selection bits. Focus is transient UI/input state and is reset when
map-player state is initialized rather than being added to the save format.

### Multi-selection ordering

`G_GetOrderedSelectedUnits()` is the shared authoritative ordering path used by
the status panel and default-focus fallback. It reproduces the comparator in
Warsmash `MeleeUI.selectWidgets()`:

1. `UnitData.slk` `prio` / `UnitData_t.priority`, descending;
2. `UnitBalance.slk` `level` / `UnitBalance_t.level`, descending;
3. unit type rawcode, descending in canonical Warcraft `War3ID` byte order.

The third comparison needs an explicit conversion. OpenRealm's `MAKEFOURCC`
stores the first character in the low byte (`'hfoo'` is little-endian in the
native integer), while Warsmash `War3ID` stores the first character in the high
byte before comparing its integer value. Comparing `edict.class_id` directly is
therefore not equivalent; `G_SelectionRawcodeValue()` converts to canonical
Warcraft byte order first.

The sort is stable for equal comparator keys. OpenRealm's authoritative selection
membership is currently only an entity bit per player, so it does not retain the
original click/rectangle insertion order. Equal unit types therefore preserve the
server's deterministic edict scan order rather than Warsmash's original input-list
order. Reproducing that final within-type detail would require retaining selection
insertion order as additional transient state.

The completed `select` command uses the first sorted unit for both default focus
and the initial selection acknowledgement sound, matching Warsmash's flow where
`selectUnits()` chooses `selectedUnits.get(0)` after sorting. Clicking a later
multiselect portrait may still explicitly focus that unit without reordering the
panel.

`FT_MULTISELECT` is one packed frame, but each payload item carries its entity
number and a focused-subgroup flag. `client/cl_scrn.c` hit-tests the authored icon
grid using the frame rectangle plus `uiMultiselect_t.offset`, consumes both mouse
edges over an icon, and sends `focus <entity>` on release. The server validates
that the entity is still selected before accepting the focus change. It then
rebuilds the full focused-selection presentation (info panel, inventory, portrait,
and command card), so focus cannot change only the command card while leaving the
status panel stale.

The focused subgroup follows Warsmash's `RenderUnit.groupsWith()` rule: selected
units with the same unit type (`class_id` in OpenRealm) are marked focused together.
The client draws the active skin's `SelectedSubgroupHighlight` behind those icons.
Selection membership remains unchanged; the concrete clicked unit is still the
focused unit used by focused-unit commands, inventory, and the persistent unit
portrait. The portrait remains visible for multiselections and follows focus; its
live HP/mana bindings follow the same focused entity.

When an entity-target command is active, the same multiselect-icon click is routed
to `menu.on_entity_selected` instead of changing focus. This preserves the
Warsmash behavior where a selected-unit portrait can be used as the target of the
active command. Point-only target modes do not change selection focus from such a
click.

Focused-unit consumers include the command card, inventory use/drop commands and
order-response selection. The complete selection remains authoritative for
multi-unit Smart/Move/Attack-style orders. Inventory presentation follows the
same focused-unit rule; see [Inventory And World Items](inventory-and-items.md).

OpenRealm still does not reproduce Warsmash's focused/unfocused icon scaling,
keyboard subgroup cycling, or the Warsmash behavior where clicking the
already-focused exact icon collapses the group to that one unit. Those are
presentation/navigation gaps, not reasons to merge inventory state across the
group.

## Relationship Presentation

`G_CustomizeEntity` converts `G_SelectionRelation` to recipient-relative entity flags:

| Relationship | Snapshot flag | Ring family |
|---|---|---|
| friend / own / shared control | neither | green |
| passive ally without shared control | `EF_NEUTRAL` | yellow |
| non-passive relationship | `EF_HOSTILE` | red |

The commonly visible reserved neutral player slots are
`PLAYER_NEUTRAL_AGGRESSIVE == 12` and `PLAYER_NEUTRAL_PASSIVE == 15`. Their
usual red/yellow presentation comes from the default alliance matrix, not a
hard-coded color exception, so map scripts can change the relationship at
runtime.

`renderer/r_ents.c` uses the same flags for both hover and full selected circles. The selection-circle texture and radius remain the existing WC3 data/model choice; relationship colours are currently renderer constants rather than `SelectionCircle/ColorFriend`, `ColorNeutral`, and `ColorEnemy` skin data.

## Control Boundary

Never use `FOR_SELECTED_UNITS` for a player-issued multi-unit order. Use:

```c
FOR_CONTROLLABLE_SELECTED_UNITS(client, ent)
```

The controllable filter is used by Smart/SmartPoint, Move, Attack/Attack-move, Stop, Hold Position, Patrol, Repair, Harvest/Return Resources, and Rally target callbacks. `CMD_Button`, `CMD_Research`, inventory use/drop, cancellation, training, and Rally command entry also validate the focused unit before acting.

Entity Smart orders are resolved independently for every controllable selected
unit. One unit rejecting a target must not abort the loop. For example, when a
Footman and a Hero are selected and the player right-clicks a world item, the
Footman may reject that widget while the Hero's inventory accepts it and starts
pickup. The first/primary selected entity is used for focused HUD/response
presentation, not as a capability gate for the rest of the selection. See
[Inventory And World Items](inventory-and-items.md) for the ROC Hero `AInv`
fallback and item lifecycle.

For a live allied unit target that is not consumed by a higher-priority Smart
interaction such as Repair, Smart uses a persistent unit-target Move/follow
order rather than copying the target's current coordinates. `movement.follow_target`
remains the authoritative default movement goal. Follow stopping distance comes
from `Misc.FollowRange` for units and `Misc.StructureFollowRange` for targets
carrying `EF_BUILDING`, with the two collision radii as a hard lower bound. These
values are loaded through the normal `Units\MiscData.txt` / `war3mapMisc.txt`
chain and are deliberately independent of the follower's attack `AcquireRange`.
The follower may still auto-acquire nearby enemies using `AcquireRange`, and resumes
following after that combat ends. Point Move, Attack-Move, Patrol, Stop, and
Hold Position replace this persistent follow goal. Explicit target Attack is a
combat detour and may return to the retained follow goal afterward, matching the
Warsmash default-behavior split. This includes Neutral Passive units while their
directional passive alliance remains enabled; revoking that alliance makes the
same unit a Smart Attack candidate instead.

`Get_Commands_f` clears the command card for a selected unit that the local player cannot control. A foreign building may still use the ordinary inspection panel, but its production queue is not serialized to the viewer. Shared-control units retain command-card access.

Selection acknowledgement voices are queued only for controllable selections. Ordinary non-Neutral-Passive foreign selections use the `InterfaceClick` UI sound instead of playing the selected unit's authored `What` response. Neutral Passive critter-specific response rules remain unresolved, so that owner class is not forced through the generic click path.

## Selection Lifetime

`G_UpdateClientSelections` runs immediately after `G_FowUpdate` each server frame. It scans the raw per-player selection bits (not `G_IsEntitySelected`, which intentionally hides already-invalid entities), removes any entry that no longer passes `G_UnitCanBeSelected`, then calls `G_SyncClientSelection()`. That helper mirrors the authoritative surviving entity list with `svc_set_selection` and refreshes portrait/info/inventory plus command presentation. The same helper is used after normal `select` filtering, server-driven Hero/idle-worker shortcuts, and immediate death removal, so the client-side current-selection cache cannot retain entities the server discarded.

Consequences:

- an enemy that leaves active vision stops being selected;
- a hidden or newly unselectable unit has its stale selection bit removed.

Death has a stronger immediate path in `unit_die`: it snapshots which players had the unit selected, sets health to zero, clears all selection bits, sets `EF_NOT_SELECTABLE`, releases any held animation frame, and starts the death animation. After death/event bookkeeping it synchronizes every connected affected client. This is necessary because clearing the raw bit before `G_UpdateClientSelections()` means the later revalidation pass cannot discover which client lost membership. The info-panel cache also retains the last non-single selection count so a multiselect that shrinks but remains a multiselect is reserialized instead of leaving the corpse icon behind. The order entry points reject dead units as well, so a corpse cannot be picked or receive a new movement/order that would replace its death animation. `G_ReviveHero` clears `EF_NOT_SELECTABLE` when the persistent Hero edict is revived.

## Known Gaps

Numbered group assignment, Shift+number append, recall, and double-tap camera focus are documented separately in [Control Groups](control-groups.md). Shift order queuing is documented in [Shift Order Queue](order-queue.md); the remaining Shift-click item below is selection toggling, not command queuing.

The following are deliberately not inferred by the current implementation:

- invisibility/detection-aware selectability (`IsUnitDetected`/`IsUnitInvisible` coverage is incomplete);
- Shift-click toggle semantics (Shift-drag addition exists separately);
- Ctrl-click and double-click same-type expansion;
- exact Warsmash within-identical-type insertion ordering (selection membership currently retains only per-player bits, so stable ties use edict scan order);
- neutral-shop patron interaction (`Aneu`/`Apit` remains unfinished);
- data-driven `SelectionCircle` relationship colours;
- Neutral Passive critter-specific selection response rules;
- exact retail behavior for `ALLIANCE_SHARED_ADVANCED_CONTROL`.

Combat reaction remains a separate gap from relationship classification:
OpenRealm does not yet apply Warsmash's PEON suppression/`canFlee` civilian
reaction policy when a non-combat unit is damaged. Likewise, a hostile
relationship does not yet imply correct per-weapon attackability because
`UnitWeapons.targs1`/`targs2` are not enforced by ordinary Attack; see
[Warcraft III Attack Damage](attack-damage.md#known-gaps).

Do not bypass these gaps by weakening `G_UnitCanControl` or by restoring owner checks inside selection itself. Selection and command authority must remain separate.

## Verification

In-engine coverage is in `games/warcraft-3/game/tests/t_api.c` and `t_unit.c` for relationship classification, visible foreign selectability, shared-control authority, dead-unit non-selectability, selection removal, Hero revival restoring selectability, and Warsmash priority/level/canonical-rawcode multiselect ordering. `t_items.c` additionally covers mixed-selection Smart item pickup with a non-inventory unit first in the selection.

Useful targeted commands after building the test binary:

```bash
make test-wc3-engine WC3_PATTERN='wc3_api.*'
make test-wc3-engine WC3_PATTERN='wc3_unit.*'
```

Runtime verification should also cover an enemy walking from visible terrain into fog, Neutral Passive/Hostile circle colours, and attempting Smart/command-card orders while a foreign unit is selected.
For multiselect ordering, drag-select a deliberately mixed group (for example a Hero plus several different unit types) and confirm the status-panel icons remain grouped/sorted by priority, level, then rawcode regardless of entity spawn order; the first sorted unit should own the initial command card and selection response.
