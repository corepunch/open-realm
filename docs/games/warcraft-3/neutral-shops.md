# Warcraft III Neutral Item Shops

## Contract

OpenRealm's neutral item-shop path is data-driven from the unit and item tables rather than a Goblin-Merchant-specific rawcode.
A live unit with a non-empty `UnitProfile.sellItems` (`Sellitems` / `usei`) is treated as an item shop. Neutral Passive shops are
public; owned/racial shops require ordinary `G_UnitCanControl()` authority. Enemy shops are not made usable merely because they carry
`Sellitems`, and allied `Aall` sharing remains explicit future policy. A Neutral Passive shop remains a neutral world selection: this
does **not** grant the selecting player ordinary `G_UnitCanControl()` authority over the building.

The currently supported interaction is:

```text
selected neutral item shop
    -> resolve nearby patron for local player
    -> build item buttons from Sellitems
    -> validate stock/resources/inventory
    -> SP_SpawnAtLocation(item)
    -> G_PickupItem(patron, item)
```

Pawn/sell-back reuses inventory drag target mode. Dropping a pawnable carried item on an item shop while within the authored
`GiveItemRange` removes the item and refunds the local player using `Misc.PawnItemRate`.

## Data Flow

| Behavior | Source |
| --- | --- |
| Merchandise list | `UnitProfile.sellItems` (`Sellitems` / `usei`) |
| Shop item-slot capacity | `SetAllItemTypeSlots` / `SetItemTypeSlots`, stored in `edict.stock.item_slots` |
| Gold/lumber price | `ItemData.goldcost`, `ItemData.lumbercost` |
| Maximum shared stock | `ItemData.stockMax` |
| Replenish interval | `ItemData.stockRegen` seconds |
| Initial availability delay | `ItemData.stockStart` seconds |
| Pawn permission | `Apit` marker on the shop plus `ItemData.pawnable` |
| Pawn refund factor | `Misc.PawnItemRate` |
| Item handoff range | `Misc.GiveItemRange` |
| Neutral-shop activation range | Aneu/Aall `DataA`; 450 world-unit fallback for minimal/custom data |

`ItemData.stockStart == 0` begins at `stockMax`. A positive start delay begins at zero stock; the first item becomes available when
the delay expires, and further copies replenish every `stockRegen` until `stockMax`. Runtime stock is shared on the shop edict, so
one player's purchase changes what every player sees.

Stock updates are lazy: command-card queries and purchase attempts advance expired timers. No per-frame shop thinker is needed.
The stock fields are inline numeric `edict_t` state and are also represented by `stock_fields` in `g_save.c`; save format 22 preserves
current counts and absolute restock deadlines, while `level.stock` preserves the global item/unit slot defaults. No `F_EDICT` pointer
fixups are required.

## Patron Selection

Warsmash's `CAbilityNeutralBuilding` retains one selected interaction unit per player and periodically reacquires it. OpenRealm's
current implementation resolves the patron deterministically when UI/purchase state is requested:

1. living, player-owned units with active inventory capacity inside the neutral-building activation radius;
2. nearest candidate wins, with entity number as a stable tie-break.

This deliberately supports Heroes plus Backpack/custom inventory units without weakening control rules for neutral or enemy units. The selected
shop's inventory panel borrows the resolved patron's inventory display while the shop remains the selected world entity. Inventory
use, drag, and drop commands resolve through that same displayed patron, so presentation and authoritative inventory ownership cannot
diverge while the neutral building is selected.

## Command Card And Purchase

`Get_Commands_f()` has one narrow exception to its normal control gate: an item shop may publish a command card even though the local
player cannot control it. `G_GetShopItemButtons()` builds buttons directly from `Sellitems`; item art/text still comes from the normal
ItemFunc/ItemStrings lookup and item costs are formatted from `ItemData`, not `UnitBalance`.

A shop button click is handled before the ordinary `G_UnitCanControl()` check and is accepted only when the currently focused entity
is still an item shop and the four-character command names one of its live stock entries. `G_ShopPurchaseItem()` then revalidates:

- a patron is currently in range;
- stock is available;
- the patron has a free inventory slot;
- local-player gold and lumber cover the item price.

Only after successful item creation and inventory acquisition are resources and stock decremented. Failure leaves both unchanged.

## Pawn / Sell Back

`ItemDrag` already owns right-click inventory drag target mode. Its entity callback now recognizes neutral item shops. A sale succeeds
only when:

- the dragged item is still carried by a local-player-controlled inventory unit;
- the target shop carries the `Apit` purchase-item marker ability;
- `ItemData.pawnable` is true;
- carrier and shop are within `Misc.GiveItemRange` plus collision radii.

The refund is `ceil(authored cost * PawnItemRate)` independently for gold and lumber, matching the Warsmash behavior. The removed item
is not inserted into the shop's authored merchandise or stock.

## Known Gaps

- `Aall` shop-sharing/allied-building policy is not implemented.
- OpenRealm does not yet retain/select a patron with Aneu's explicit `neutralinteract` button or persistent selection indicator; it
  reacquires deterministically from range instead.
- Walking an out-of-range Hero to the shop before pawning is not implemented; pawn targeting succeeds only when already in range.
- `Sellunits` and unit-shop stock are still data-only.
- JASS `AddItemToStock`, `AddItemToAllStock`, `RemoveItemFromStock`, and unit-stock counterparts are not registered yet.
- Power-up/auto-use-on-acquire item semantics remain part of the broader item lifecycle and are not special-cased by the shop.
- Item-specific tech-tree availability beyond authored stock timing is not yet modeled.

## Verification

`games/warcraft-3/game/tests/t_items.c` covers:

- nearby inventory-unit patron resolution;
- purchase cost deduction and authoritative inventory handoff;
- out-of-range purchase rejection with unchanged resources;
- shared stock exhaustion and `stockRegen` replenishment;
- pawnable item removal and `PawnItemRate` refund.

The test fixture's `spro` row includes explicit gold/lumber and stock metadata for these scenarios. The task intentionally does not
require a local build/test run; after building, the focused suite is `+dedicated 1 +test 'wc3_items.*'`.
