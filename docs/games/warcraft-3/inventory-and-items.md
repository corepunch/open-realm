# Inventory And World Items

The inventory subsystem keeps one server-authoritative item entity as it moves
between the world and an ability-defined unit inventory. The same edict is
retained across pickup and drop; normal transitions do not destroy and recreate
it. `MAX_INVENTORY` remains the six-slot storage/UI ceiling, while gameplay
capacity comes from the unit's inventory ability.

## State Contract

Item lifecycle state lives on the game edict alongside the existing unit
inventory pointers.

| State | `carrier` | `inventory_slot` | `in_world` | Presentation |
| --- | --- | ---: | --- | --- |
| World | `NULL` | `-1` | `true` | Linked, visible, sent to clients |
| Carried | Unit edict | valid slot | `false` | Unlinked, `RF_HIDDEN`, `SVF_NOCLIENT`; shown by inventory UI |

The item edict also owns its mutable charge count. `SP_SpawnItem` initializes
`item.charges` from ItemData `uses` (`iuse`); pickup and drop preserve it.
`GetItemCharges` and `SetItemCharges` read and update that same runtime state.

For a carried item, the carrier's matching inventory slot must point back to
the item. `G_AddItemToSlot`, `G_PickupItem`, `G_DropItemAt`, `G_DropItem`, and
`G_RemoveItem` own the world/inventory relationship.

## Inventory Capability And Capacity

Inventory is ability-defined, not hero-defined. `G_InventoryCapacity` scans the
unit's normal ability list for an ability whose AbilityData `code` is `AInv`.
Its first data slot is Warcraft III field `inv1` (Item Capacity). `AB_Data`
resolves the archive-version spelling (`Data11` in ROC, `DataA1` in TFT).
Capacity is clamped to the six-slot OpenRealm storage/UI ceiling.

Consequently a normal hero inventory resolves to six slots, while a custom
inventory ability may expose fewer slots. Pickup, explicit slot insertion,
client item use/drop commands, JASS slot operations, and HUD enumeration all
respect the resolved capacity.

## Contextual Pickup

A client right-click still produces the existing `smart <entity>` command.
The server recognizes a world item before harvest, attack, or move handling and
starts a pickup order for each selected inventory-capable unit.

The order keeps the item as its goal and checks it every simulation tick. It
moves while the center-to-center distance is greater than
`ITEM_PICKUP_RANGE` (150 world units), then attempts the authoritative
inventory transition. The order stops if the item was removed, hidden, picked
up by another unit, or otherwise left world state.

When every slot exposed by the inventory ability is occupied, the transition
does not modify either entity. The world item stays linked and visible, and
contextual pickup displays an `Inventory is full.` message to the owning player.

## Inventory Presentation

The HUD has no item-specific branches. `G_GetInventory` walks only the selected
unit's exposed slots and `G_BuildInventoryItem` resolves presentation by the
carried item's rawcode through the already-loaded Warcraft III UI config tables:

```text
item rawcode
    -> ItemFunc.txt / ItemStrings.txt
    -> Art / Tip / Ubertip
    -> gameInventoryItem_t
    -> LAYER_INVENTORY command button
```

`Art` is passed through the same theme indirection used by command-card art and
then registered through `gi.ImageIndex` when the server authors the inventory
frame. The parsed INI tables and image registry are already persistent engine
state, so no second item-UI cache is maintained.

The runtime charge count is copied into `gameInventoryItem_t`. `WriteInventory`
draws a bottom-right number overlay whenever `charges > 0`, including a
single-charge item. A zero-charge item therefore keeps its icon but has no
number overlay. Cooldown/disabled-state presentation and charge consumption are
separate active-item work.

For Human02 this means a carried Scroll of Protection is handled generically:
rawcode `spro` resolves its item UI data, appears in the first free slot, and
shows its initial charge count of `1`. No HUD code checks for `spro`.

## Drop And Script Paths

`G_DropItemAt` performs the inverse transition and places the same item edict
at a requested world position. `G_DropItem` uses the carrier's current position.
The existing JASS natives route through this lifecycle:

- `UnitAddItem`
- `UnitAddItemById`
- `UnitAddItemToSlotById`
- `UnitRemoveItem`
- `UnitRemoveItemFromSlot`
- `RemoveItem`
- `SetItemPosition` for items already in world state
- `GetItemCharges`
- `SetItemCharges`

The client command `dropitem <slot>` exposes a direct zero-based drop path for
the main selected unit. Inventory drag/drop interaction is a later UI phase.

Successful transitions and carried-item charge changes refresh the inventory
layer for clients currently selecting the carrier. Hidden entities are also
excluded from renderer hit and rectangle tests while snapshot removal is in
flight.

## Phase Boundary

This slice includes generic item icon/tooltips, ability-defined capacity, and
runtime/displayed charges. It deliberately does not consume charges, destroy a
perishable item at zero, implement automatic use, active target modes, item-use
cooldowns/disabled icons, slot swapping, giving, or death-drop rules. Existing
passive-effect hooks remain attached to inventory entry and exit.

The implementation is derived from observable behavior and Warcraft III data
formats described by the clean-room specification. It does not depend on
another engine's item implementation.

## Validation

The `wc3_items.*` in-engine tests cover world-state initialization, data-driven
capacity (including a reduced-capacity inventory), first-empty-slot insertion,
full-inventory failure, pickup range and revalidation, drop identity, renderer
visibility flags, carried-item removal, charge initialization/preservation,
JASS charge access, and generic `spro` Art/Tip/Ubertip/charge presentation.
Minimal `AbilityData.slk`, `UnitAbilities.slk`, `ItemData.slk`, `ItemFunc.txt`,
and `ItemStrings.txt` fixtures keep these tests data-driven in both ROC and TFT
test runs.
