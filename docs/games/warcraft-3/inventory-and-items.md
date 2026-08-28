# Inventory And World Items

The first inventory delivery slice establishes one server-authoritative item
entity that moves between the world and a six-slot unit inventory. The same
edict is retained across pickup and drop; normal transitions do not destroy
and recreate it.

## State Contract

Item lifecycle state lives on the game edict alongside the existing unit
inventory pointers.

| State | `carrier` | `inventory_slot` | `in_world` | Presentation |
| --- | --- | ---: | --- | --- |
| World | `NULL` | `-1` | `true` | Linked, visible, sent to clients |
| Carried | Unit edict | `0..5` | `false` | Unlinked, `RF_HIDDEN`, `SVF_NOCLIENT` |

For a carried item, the carrier's matching inventory slot must point back to
the item. `G_AddItemToSlot`, `G_PickupItem`, `G_DropItemAt`, `G_DropItem`, and
`G_RemoveItem` are the only phase-one functions that mutate this relationship.

Only units whose data-driven normal ability list contains `AInv` can accept an
item. Inventory insertion chooses the first empty slot unless a caller requests
a particular slot.

## Contextual Pickup

A client right-click still produces the existing `smart <entity>` command.
The server recognizes a world item before harvest, attack, or move handling and
starts a pickup order for each selected inventory-capable unit.

The order keeps the item as its goal and checks it every simulation tick. It
moves while the center-to-center distance is greater than
`ITEM_PICKUP_RANGE` (150 world units), then attempts the authoritative
inventory transition. The order stops if the item was removed, hidden, picked
up by another unit, or otherwise left world state.

When all six slots are occupied, the transition does not modify either entity.
The world item stays linked and visible, and contextual pickup displays an
`Inventory is full.` message to the owning player.

## Drop And Script Paths

`G_DropItemAt` performs the inverse transition and places the same item edict
at a requested world position. `G_DropItem` uses the carrier's current position.
The existing JASS natives now route through this lifecycle:

- `UnitAddItem`
- `UnitAddItemById`
- `UnitAddItemToSlotById`
- `UnitRemoveItem`
- `UnitRemoveItemFromSlot`
- `RemoveItem`
- `SetItemPosition` for items already in world state

The client command `dropitem <slot>` exposes a direct zero-based drop path for
the main selected unit. Inventory drag/drop interaction is a later UI phase.

Successful transitions refresh the inventory layer for clients currently
selecting the carrier. Hidden entities are also excluded from renderer hit and
rectangle tests while snapshot removal is in flight.

## Phase Boundary

This slice deliberately does not add charges, consumable destruction, automatic
use, active target modes, slot swapping, giving, loot tables, or death-drop
rules. Existing passive-effect hooks remain attached to inventory entry and
exit, but completing the data-driven item-effect system is the next delivery
phase. Pickup/drop event context is deferred with the broader item-event work.

The implementation is derived from observable behavior and Warcraft III data
formats described by the clean-room specification. It does not depend on
another engine's item implementation.

## Validation

The `wc3_items.*` in-engine tests cover world-state initialization, capability
checks, first-empty-slot insertion, full-inventory failure, pickup range and
revalidation, drop identity, renderer visibility flags, and carried-item
removal. Minimal `UnitAbilities.slk` and `ItemData.slk` fixtures keep these tests
data-driven.
