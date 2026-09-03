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

Reign of Chaos heroes have one compatibility exception. Warsmash adds the
stock `AInv` ability to a hero when the ROC map (`war3map.w3i` format version
`<= 24`) does not already author an inventory-derived ability in the unit's
normal ability list. OpenRealm mirrors that at capacity resolution time rather
than mutating immutable unit metadata: an ROC hero with no authored inventory
ability receives stock `AInv` capacity, while TFT-format maps do not get this
fallback. An explicitly authored inventory-derived ability still wins, including
the existing Backpack gating rules for stock non-hero inventory abilities.

Consequently a normal hero inventory resolves to six slots, while a custom
inventory ability may expose fewer slots. Pickup, explicit slot insertion,
client item use/drop commands, JASS slot operations, and HUD enumeration all
respect the resolved capacity.

## Contextual Pickup

A client right-click still produces the existing `smart <entity>` command.
The server recognizes a world item before harvest, attack, or move handling and
starts a pickup order for each selected inventory-capable unit.

Smart target dispatch is per selected unit, not per primary subgroup. The
server walks every controllable selected unit and asks its normal Smart resolver
to accept the same item target. A Footman rejecting the item therefore does not
veto a later selected Hero: the Hero's inventory capability starts the pickup
order, while the Footman receives no replacement order. This is especially
important for ROC campaign heroes whose implicit `AInv` fallback is not present
in `UnitAbilities.slk`.

The order keeps the item as its goal and checks it every simulation tick. It
moves while the center-to-center distance is greater than
`ITEM_PICKUP_RANGE` (150 world units), then attempts the authoritative
inventory transition. The order stops if the item was removed, hidden, picked
up by another unit, or otherwise left world state.

When every slot exposed by the inventory ability is occupied, the transition
does not modify either entity. The world item stays linked and visible, and
contextual pickup displays an `Inventory is full.` message to the owning player.

## Active Item Use

The focused-unit `inventory <slot>` command resolves the carried item's
`abilList` from the normalized `ItemData_t` row in authored order and dispatches
the first registered item ability it can handle. This matters because
`abilList` is an `ItemData.slk` field; `FindConfigValue` searches TXT/INI
configuration and therefore cannot be the primary lookup for a real carried
item. A TXT/INI lookup remains only as a fallback when the typed field is
absent. Immediate effects use `ability_t.item_use`, which returns
true only when the gameplay effect actually applies. Current handlers cover the
existing heal, mana, permanent-life/stat, experience/level, and figurine item
abilities, plus stock item-defense AOE (`AIda`, used by Scroll of Protection).
Their successful presentation uses the same ability `TargetArt`
resolver documented in [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md).

For `AIda`, OpenRealm reads the authored defense amount, area, normal/hero
duration, target mask and BuffID. Eligible friendly units receive the timed
`Bdef` status. Combat armor calculation and the displayed armor value both
include the status bonus while it is live, so normal status expiration removes
the bonus automatically.

For a synchronous successful use, the server publishes
`EVENT_PLAYER_UNIT_USE_ITEM` and `EVENT_UNIT_USE_ITEM` and then calls
`G_ConsumeItemCharge`. Failed uses (for example, healing an already full-health
unit) publish neither event and consume no charge. Every successful synchronous charged-item use decrements a positive runtime
charge count. If that reaches zero on a perishable item, `G_RemoveItem` destroys
it, clearing the slot and reversing passive item-stat hooks. A non-perishable
item also decrements to zero but remains present.

Existing item abilities that enter an asynchronous targeting command through
`ability_t.cmd` are still dispatched, but the click handler cannot yet know
whether that later target operation succeeds. It therefore does not consume
their charge or publish a success event at click time. That completion path is
explicitly future work rather than speculative charge consumption.

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
single-charge item. A zero-charge non-perishable item therefore keeps its icon
but has no number overlay. Successful synchronous use of a perishable item now
consumes its runtime charge and removes the item at zero; cooldown/disabled-state
presentation remains separate active-item work.

For Human02 this means a carried Scroll of Protection is handled generically:
rawcode `spro` resolves its item UI data, appears in the first free slot, and
shows its initial charge count of `1`. No HUD code checks for `spro`.

### Selected-unit inventory panel state

Inventory visibility is capability-defined independently of hero presentation.
For any non-empty selection, `LAYER_INVENTORY` resolves the current focused unit
through `G_GetMainSelectedUnit()` and authors that unit's inventory state. A
multi-selection never merges inventories and does not search for the first unit
that happens to have inventory capacity: focusing a Footman covers the inventory
area, while focusing a Hero in the same still-selected group immediately authors
that Hero's slots. `inventory <slot>` and `dropitem <slot>` already use the same
focused-unit lookup, so the displayed inventory and the unit receiving the item
action stay aligned.

The focused unit authors one of three states:

- capacity `0`: cover the underlying six-slot console area with the local
  player's race-skin `ConsoleInventoryCoverTexture`;
- capacity `1..5`: leave the valid slots visible and cover each slot outside
  capacity with `ConsoleInventoryNoCapacity`;
- capacity `6`: leave all six normal slots visible.

Both texture keys come from `UI\war3skins.txt`, using the local player's race
section with `Default` fallback. Classic ROC data used by OpenRealm does not
provide a usable inventory-cover FDF, so the cover is one of the WC3 native
frames constructed in C: a static `FRAMEDEF` owns the confirmed texture crop,
`ALPHAKEY` mode, `0.128 x 0.175` size, and bottom-right screen anchor. No
project-owned production FDF is shipped. The selected unit determines inventory
capability and contents; the local player's console skin determines cover/filler
artwork. Hero/non-hero stats remain an independent info-panel decision.

The cover samples only the useful lower portion of the packed console BLP
(`V=0.380859375..1.0`) rather than squeezing the complete image canvas into the
frame. This crop came from the bounded runtime asset diagnostic that established
the useful pixels begin at row 195 of the 512-pixel source image.

Stock non-hero inventory abilities are capability-gated by their Backpack
upgrade rather than becoming active merely because the alias appears in
`UnitAbilities.slk`. For example, the Footman carries `Aihn` in its authored
ability list, but its two inventory slots remain covered until Human Backpack
`Rhpm` is researched. The same rule applies to the stock Orc, Night Elf and
Undead unit-inventory variants. Plain `AInv` and custom `AInv`-derived inventory
abilities are not implicitly gated.

`UpgradeData.slk` is now normalized for research costs/times and its four
effect slots, with `ratx`, `ratd`, and `rarm` implemented generically for Blacksmith-style
stat research. The stock inventory-ability-to-Backpack relationships remain a
small explicit table in `g_items.c` because that Backpack effect has not yet been
moved onto the generic upgrade-effect dispatcher.

## Inventory Refresh Lifecycle

Player/client edicts occupy the reserved `[0, max_clients)` range and are not
ordinary `inuse` gameplay entities. Inventory refresh therefore iterates those
reserved client slots and gates on the explicit `GAMECLIENT.connected` state.
`G_ClientBegin` marks the slot connected after the handshake, while map-player
initialization clears the state before the next map begins.

A previous refresh path incorrectly required the reserved player edict itself to
be `inuse`. Pickup still completed, but the refresh was skipped and the server
never resent `LAYER_INVENTORY`.

Item-state changes refresh only `LAYER_INVENTORY` through
`G_RefreshInventoryLayer`. They do not rebuild the portrait or info panel. This
keeps item transitions independent of unrelated portrait/FDF presentation and
avoids requiring a full selected-unit HUD rebuild merely because an item moved
or its charge count changed.

The bounded diagnostic for this boundary is:

```text
+set sv_debug_layout 1 +com_frame_limit 100
```

A successful carried-item refresh should produce a new `layer=6` layout write;
an occupied item contributes at least one textured command-button frame.

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

This slice includes generic item icon/tooltips, ability-defined capacity,
runtime/displayed charges, and successful synchronous use of the existing
immediate item ability handlers. Perishable synchronous uses consume one charge
and destroy the item at zero. Existing passive-effect hooks remain attached to
inventory entry and exit.

Still missing are automatic `powerup` acquisition/use, asynchronous targeted
item completion and its charge/event semantics, `cooldownID`/`ignoreCD` item
cooldowns and disabled icons, slot swapping, giving, and death-drop rules.

The implementation is derived from observable behavior and Warcraft III data
formats described by the clean-room specification. It does not depend on
another engine's item implementation.

## Validation

The `wc3_items.*` in-engine tests cover world-state initialization, data-driven
capacity (including reduced, zero, above-storage-limit, and implicit ROC Hero
`AInv` cases),
first-empty-slot insertion,
full-inventory failure, pickup range and revalidation, drop identity, renderer
visibility flags, carried-item removal, connection-state refresh gating, charge
initialization/preservation, carried-charge refresh/no-op behavior, perishable
use decrement/removal, non-perishable decrement-without-removal behavior, JASS charge access,
and generic `spro` Art/Tip/Ubertip/charge presentation.
They also cover mixed-selection Smart pickup where a non-inventory unit is the
first selected entity and a later ROC Hero must still receive the item order.
Minimal `AbilityData.slk`, `UnitAbilities.slk`, `ItemData.slk`, `ItemFunc.txt`,
`ItemStrings.txt`, and `war3skins.txt` fixtures keep these tests data-driven in
both ROC and TFT test runs. Inventory-panel tests additionally cover the
no-inventory cover, reduced-capacity fillers, full-capacity absence of fillers,
race-skin selection, and the native cover frame's crop/geometry. Synthetic
ability-only unit IDs such as `H001` have no `UnitBalance` life value, so the
shared test-unit allocator initializes a minimum positive life value without
weakening the runtime rule that zero-life units are corpses and cannot be selected.

`AIat` is represented as `unitAttack_t.temporaryDamageBonus`, so it survives Hero stat recomputation and is rendered as a separate green/red attack modifier. `AIde` uses `temporary_armor_bonus` so Hero Agility recomputation likewise preserves item armor. See [Attack Damage](attack-damage.md).
