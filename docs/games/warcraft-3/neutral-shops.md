# Warcraft III Neutral Shops And Mercenary Camps

## Contract

OpenRealm's neutral-shop path is data-driven from unit/item tables rather than shop-specific rawcodes. A live unit with a non-empty
`UnitProfile.sellItems` (`Sellitems` / `usei`) is an item shop; a live unit with a non-empty `UnitProfile.sellUnits` (`Sellunits` /
`useu`) is a unit shop. Neutral Passive shops are public; owned/racial shops require ordinary `G_UnitCanControl()` authority. Enemy
shops are not made usable merely because they carry merchandise, and allied `Aall` sharing remains explicit future policy. A Neutral
Passive shop remains a neutral world selection: this does **not** grant the selecting player ordinary `G_UnitCanControl()` authority
over the building.

The supported purchase flows are:

```text
selected neutral item shop
    -> resolve nearby inventory patron for local player
    -> build item buttons from Sellitems
    -> validate stock/resources/inventory
    -> SP_SpawnAtLocation(item)
    -> G_PickupItem(patron, item)

selected neutral unit shop
    -> resolve nearby Aneu-eligible patron for local player
    -> build non-Hero unit buttons from Sellunits
    -> validate stock/resources/food
    -> SP_SpawnAtLocationNoBirth(unit)
    -> find deterministic legal building exit
    -> activate food for the purchasing player
```

`Sellunits` support in this path intentionally excludes Heroes. Taverns share the same object-data field but require separate Hero
availability, tier/limit, and neutral-Hero purchase rules; treating those as ordinary mercenaries would be incorrect.

Pawn/sell-back reuses inventory drag target mode. Dropping a pawnable carried item on an item shop while within the authored
`GiveItemRange` removes the item and refunds the local player using `Misc.PawnItemRate`.

## Data Flow

| Behavior | Source |
| --- | --- |
| Item merchandise list | `UnitProfile.sellItems` (`Sellitems` / `usei`) |
| Unit merchandise list | `UnitProfile.sellUnits` (`Sellunits` / `useu`), non-Hero units only |
| Shop item-slot capacity | `SetAllItemTypeSlots` / `SetItemTypeSlots`, stored in `edict.stock.item_slots` |
| Shop unit-slot capacity | `SetAllUnitTypeSlots` / `SetUnitTypeSlots`, stored in `edict.stock.unit_slots` |
| Item gold/lumber price | `ItemData.goldcost`, `ItemData.lumbercost` |
| Unit gold/lumber/food price | `UnitBalance.goldCost`, `lumberCost`, `foodUsed` |
| Item maximum / replenish / start | `ItemData.stockMax`, `stockRegen`, `stockStart` |
| Unit maximum / replenish / start | `UnitBalance.stockMax`, `stockRegen`, `stockStart` |
| Pawn permission | `Apit` marker on the shop plus `ItemData.pawnable` |
| Pawn refund factor | `Misc.PawnItemRate` |
| Item handoff range | `Misc.GiveItemRange` |
| Neutral-shop activation range | Aneu/Aall `DataA`; 450 world-unit fallback for minimal/custom data |

A zero `stockStart` begins at `stockMax`. A positive start delay begins at zero stock; the first copy becomes available when the delay
expires, and further copies replenish every `stockRegen` until `stockMax`. Item and unit runtime stock are separate arrays on the shop
edict and are shared between players, so one player's purchase changes what every player sees.

Stock updates are lazy: command-card queries and purchase attempts advance expired timers. No per-frame shop thinker is needed. The
stock fields are inline numeric `edict_t` state and are represented by `stock_fields` in `g_save.c`; save format 28 preserves item and
unit counts plus absolute restock deadlines, while `level.stock` preserves the global item/unit slot defaults. No `F_EDICT` pointer
fixups are required.

## Patron Selection

Warsmash's `CAbilityNeutralBuilding` retains one selected interaction unit per player and periodically reacquires it. OpenRealm's
current implementation resolves the patron deterministically when UI/purchase state is requested. It reads Aneu/Aall `DataA` for
activation radius and `DataB` for the authored interaction type (inventory unit, non-building, or any unit), then chooses the nearest
eligible living player-owned **unit** with entity number as a stable tie-break. Item shops without Aneu/Aall preserve their older
inventory-carrier rule; `Sellunits` has no safe interaction fallback and therefore requires valid Aneu/Aall `DataB` data.

Item purchase still requires inventory capacity even when Aneu is more permissive. Unit purchase does not incorrectly require an
inventory. The selected item shop's inventory panel borrows the resolved inventory patron's display while the shop remains the selected
world entity. OpenRealm still resolves patrons on demand rather than persisting Warsmash's explicit per-player selected patron.

## Command Card And Purchase

`Get_Commands_f()` has a narrow neutral-shop exception to its normal control gate. `G_GetShopButtons()` combines authored `Sellitems`
and supported `Sellunits` entries into the 12-slot command card. Item art/text/cost formatting continues to use ItemFunc/ItemStrings
and `ItemData`; unit art/text/cost formatting reuses the normal unit command-button path and `UnitBalance`.

A shop button click is handled before the ordinary `G_UnitCanControl()` check. The authoritative path first verifies that the selected
shop actually carries the raw object ID, then dispatches to item or unit purchase. Item purchase revalidates patron, stock, inventory
space, gold and lumber. Unit purchase revalidates patron, stock, gold, lumber and food before spawning anything.

A successful mercenary hire is immediate rather than a training queue. The new unit is created directly for the purchasing player
without a Birth presentation, moved to the same deterministic legal producer-exit search used by trained units, then has food activated.
Gold/lumber and shared stock are deducted only after spawn/placement succeeds. Failures leave resources and stock unchanged.

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
- Neutral Hero/Tavern sales are not implemented; Hero entries in `Sellunits` are deliberately excluded from the mercenary path.
- Unit-sale trigger events (`EVENT_UNIT_SELL` / player-unit sale context) are not published yet.
- JASS `AddItemToStock`, `AddItemToAllStock`, `RemoveItemFromStock`, and unit-stock counterparts are not registered yet.
- Power-up/auto-use-on-acquire item semantics remain part of the broader item lifecycle and are not special-cased by the shop.
- Shop merchandise tech-tree availability beyond authored stock timing is not yet modeled.

## Verification

`games/warcraft-3/game/tests/t_items.c` covers:

- nearby inventory-unit item-shop patron resolution;
- item purchase cost deduction and authoritative inventory handoff;
- out-of-range item purchase rejection with unchanged resources;
- shared item stock exhaustion and `stockRegen` replenishment;
- non-inventory mercenary patron resolution;
- immediate mercenary ownership, resource deduction, and food activation;
- unit `stockStart` / `stockRegen` / `stockMax` command availability;
- failed mercenary food validation preserving stock/resources;
- pawnable item removal and `PawnItemRate` refund.

`games/warcraft-3/game/tests/t_game.c` additionally round-trips both item and unit shop stock/timer state through the save schema. The
test fixture's `spro` and `nmer` rows carry explicit price/food/stock metadata. The focused gameplay suite is
`+dedicated 1 +test 'wc3_items.*'`; save changes must also run the save tests and the full suite required by `CONTRIBUTING.md`.
