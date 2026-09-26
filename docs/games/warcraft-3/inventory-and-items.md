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

Reign of Chaos heroes have one compatibility exception. On ROC maps
(`war3map.w3i` format version `<= 24`), a hero without an authored inventory
ability receives stock `AInv` capacity. Classic ROC archives omit that row;
in that case the built-in hero inventory has six slots and supports item use,
pickup and drop, retaining items on death. This also applies to an explicit
`AInv` on a ROC hero when its data row is absent. An existing `AInv` row remains
authoritative, including zero capacity. TFT-format maps and non-heroes do not
receive this missing-row default. Explicit custom inventory abilities and
Backpack upgrade gates retain their authored behavior.

Consequently a normal hero inventory resolves to six slots, while a custom
inventory ability may expose fewer slots. Pickup, explicit slot insertion,
client item use/drop commands, JASS slot operations, and HUD enumeration all
respect the resolved capacity.

### ROC hero checkerboards: attribute bonus mistaken for inventory

The five magenta slots reported on Undead04 came from incorrect capacity, not
missing item icons. `07cc8967` classified `AIab` as a ROC inventory alias and
scanned for its first positive Data A value when `AInv` was absent. Retail ROC
`Units/AbilityData.slk` lists `AIa1` first: its code is `AIab`, its name is
`AgilityBonus (+1)`, and `Data11=1`. `AIa6` is likewise `AgilityBonus (+6)`.
Both the extracted demo and TFT class registries identify `AIab` as
`CAbilityAttributeBonus`; only `AInv` is `CAbilityInventory`.

That scan gave Arthas one slot and caused `WriteInventory` to request five
`ConsoleInventoryNoCapacity` overlays. The inspected ROC `UI/war3skins.txt`
does not define that key, producing a logged unresolved image and the magenta
placeholder. Correcting the inventory classification and classic hero default
removes those erroneous overlays. It does not supply missing blocked-slot art
for custom reduced-capacity inventories using this old skin table.

`wc3_items.roc_hero_inventory_does_not_read_attribute_bonus_as_capacity` uses
the ROC `Data11` schema with `AIa1`, `AIa3`, and `AIa6`, with no `AInv` row.
It drives capacity, item permissions, selection and `G_RefreshInventoryLayer`
for implicit and explicit hero inventory, asserting six slots and no blocked-slot
texture requests. Companion tests exclude attribute bonuses from non-hero
inventory and preserve custom four-slot and zero-slot capacities. Existing
`DataA1` fixtures cover TFT inventory and Backpack permissions.

A bounded ROC framebuffer check used an Undead04 diagnostic copy with mission
triggers disabled, created level-4 `Uear` for player 3, added `ktrm` in slot 0,
and selected him. `screenshot 30` with `+com_frame_limit 200` showed that item
and five normal empty slots, with no `ConsoleInventoryNoCapacity` request.
The native terrain and ROC archive assets were unchanged; no mission progression
or save/load behavior was inferred from this visual check.

Run `make test-wc3-engine WC3_PATTERN='wc3_items.*'`. To inspect the native
evidence without launching the game:

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat Units/AbilityData.slk
build/bin/mpqtool -mpq 'data/Warcraft III/War3.mpq' cat UI/war3skins.txt
rg 'AIab|AInv' games/warcraft-3/{demo,tft}-ability-classes.txt
```

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
absent. Immediate effects declare `AB_ITEM` and handle `A_ITEM_USE`, which returns
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

Successful item uses complete through `G_CompleteItemUse`, which publishes
`EVENT_PLAYER_UNIT_USE_ITEM` and `EVENT_UNIT_USE_ITEM` with the used item as the
event source and then applies charge/perishable semantics. Failed uses (for
example, healing an already full-health unit) publish neither event and consume
no charge. A successful charged-item use decrements a positive runtime charge
count. Non-perishable items remain present at zero charges. A final perishable
charge detaches the item from gameplay immediately but keeps its edict/handle
alive until queued use-item events and any sleeping JASS action carrying that
event context have finished; this keeps `GetManipulatedItem()` valid while the
trigger responds to the use. The retained item is then retired automatically.

Item abilities that enter the shared spell targeting command now bind their
originating inventory item to that command. Unit-target abilities preserve the
item plus spawn generation while walking into cast range. Charge consumption
and use-item events occur only after successful `A_EXECUTE`; rejected targets,
interrupted approaches, and stale/moved source items do not consume the item.
The same completion hook is used for point/no-target spell commands where the
shared spell pipeline can report execution success.

`SetItemDroppable` supplies a runtime override of authored `ItemData.droppable`.
Player/manual drop paths honor that effective value. Explicit JASS inventory
manipulation can move an undroppable quest item deliberately. A missing
ItemData row is logged and rejects dropping instead of silently treating the
item as droppable. Drop-on-death remains a separate inventory policy, matching
Warcraft's distinct droppable and death-drop controls. Synthetic tests that
replace `ItemData.slk` must include the `droppable` column and set it on rows
whose scenario expects player drops; an omitted boolean parses as false and
correctly blocks the manual drop path.

## Soul Gem (`gsou` / `soul`, `AIso` / `Asou`)

Soul Trap is owned by `CAbilitySoulTrap` in `skills/s_item.c`. `gsou` supplies
the targeted `AIso` ability; successful execution follows the shared spell
target and range checks, then binds the existing target edict to the carrier.
The stock `AIso` target mask (`enemy,ground,hero`) selects enemy Heroes through
authored data. The handler does not hard-code a Hero check, so custom target
masks remain usable.

Capture does not enter `unit_die`, change ownership, or replace the unit handle.
It records a carrier-owned linked list and a target-side carrier pointer with
spawn generations, marks the target `AI_SOUL_TRAPPED`, cancels its current
orders/channel, hides and unlinks it, and clears selection. `G_RunEntity` skips
world movement and unit think while trapped; status timers and construction or
upgrade progress still use their normal paths. The shared `G_UnitIsWorldActive`
check keeps trapped units out of selection, control, spell targeting, and
region-touch processing. The target remains alive with its original health and
JASS handle.

The carrier receives the `Asou` lifecycle ability. Its death releases every
bound target at the carrier's death coordinates, removes the corresponding
filled `soul` item, and clears the forced reveal. Removing `Asou` from the
trapped target also ends the relationship; Orc08 uses this during its Jaina
ritual sequence, then moves Grom and runs its campaign-specific presentation.
Removing a carrier without death uses relationship cleanup so targets are not
left orphaned. There is no generic duration timer.

The filled item cannot be dropped, including through scripted point-drop or
death-drop paths. Orc08's cinematic swaps the item through `UnitRemoveItem`
and `UnitAddItem`; removal temporarily detaches it from its slot while keeping
its carrier binding, and only that carrier can put it back. This does not expose
the item as a world pickup or permit transfer.

The carrier receives a generic unit-specific forced-visibility reference for
the trapped Hero's owner. Fog queries apply that reference before ordinary
invisibility checks and resolve shared vision through the normal alliance
rules. The reveal follows carrier movement and does not mark surrounding fog
cells visible or explored. It ends when the relationship is removed.

Final-charge item use must preserve the `GetManipulatedItem()` handle while
JASS handles `EVENT_PLAYER_UNIT_USE_ITEM`. `G_CompleteItemUse` detaches the
perishable `gsou` immediately but retains its edict while a use event or sleeping
JASS response references it. After the response finishes,
`S_SoulTrapFinalizeConsumedItem` binds the map-created `soul` item to that
specific trapped target (or creates one from the `soul` ItemData row when no
script created it), then makes the filled item nondroppable. Each item and
target retain their own link, so multiple traps do not share one filled item.

The local War3local `Orc08.w3m` script confirms these integration points:

- `Trig_Thrall_Uses_Soul_Gem_Gets_Soul` listens for Player 0's use-item event,
  waits 0.10 seconds, adds `soul` to Thrall, makes it nondroppable, and completes
  the Grom capture objective.
- The ritual path removes the filled item and removes `Asou` from `udg_Grom`
  before moving or replacing Grom.
- `Trig_Grom_Dead` calls `ReviveHeroLoc` with the Grom-pop rectangle center.
  The JASS native now delegates to `G_ReviveHero`, preserving the unit edict and
  restoring its live Hero state at that location.

The focused tests are `wc3_items.soul_gem*`,
`wc3_items.consumed_perishable*`, `wc3_api.revive_hero_location*`, and
`wc3_save.soul_trap_links*`; `wc3_items.soul_trap_remove_target_cleans_bound_item`
also covers cleanup when the trapped target is removed, and
`wc3_items.soul_gem_pending_approach_round_trips_save` covers a pending targeted
cast across save/load. They run against both ROC and TFT fixture schemas.
They cover target-mode entry, stock target filtering, no death event, the
Orc08-style scripted filled-item slot swap, delayed use-event identity, visibility,
multiple captures on one carrier, carrier-death release, and relationship
save/load. Run them with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_items.soul_gem*'
make test-wc3-engine WC3_PATTERN='wc3_items.consumed_perishable*'
make test-wc3-engine WC3_PATTERN='wc3_api.revive_hero_location*'
make test-wc3-engine WC3_PATTERN='wc3_save.soul_trap_links*'
make test-wc3-engine WC3_PATTERN='wc3_items.orc08_scripted_soul_slot_swap*'
make test-wc3-engine WC3_PATTERN='wc3_items.missing_item_data*'
```

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

Inventory ability Data B through Data E are authoritative runtime permissions:

- `inv2` / Data B (`Drop Items On Death`) releases carried items when the carrier dies;
- `inv3` / Data C (`Can Use Items`) gates active item commands and carried passive abilities;
- `inv4` / Data D (`Can Get Items`) gates player-issued pickup orders; and
- `inv5` / Data E (`Can Drop Items`) gates player-issued drop/handoff/pawn orders.

A carrier with `inv3=0` can still transport items without receiving their
abilities. A carrier with `inv2=1` releases each carried item into world state at
its death position without playing a manual-drop response sound. `inv4` and
`inv5` are deliberately order permissions, not absolute mutation locks: direct
JASS/native add/remove operations still use the underlying pickup/drop primitive,
matching Warsmash's distinction between order validation and scripted
`giveItem`/`dropItem`. The stock Backpack contract uses these fields for ordinary
carriers, whereas Hero `AInv` enables normal player inventory interaction and
retains inventory through death.

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

`SetItemDropID` is a metadata-only native. It stores the unit rawcode used by
later item-drop logic on the item instance; it does not move the item, change
inventory ownership, or refresh the inventory layer.

The client command `dropitem <slot>` remains a direct zero-based drop-at-feet
path for the main selected unit. Inventory buttons now also use the existing
command-button secondary/right-click channel: right-clicking an occupied slot
sends `itemdrag <slot>` and enters a server-owned point-target mode. The next
left-click on terrain starts a real drop behavior for that exact item instance.
If the requested point is farther than `ITEM_DROP_RANGE`, the carrier walks
into range before releasing the item; the behavior revalidates the carried item
on each tick. A successful point drop removes passive item effects, unhides and
relinks the same item entity, and runs the requested point through the shared
WC3 deterministic unstuck search before placing it. Right-click or the normal
Cancel command leaves the target mode without dropping the item.

When a usable neutral item shop is selected, its inventory layer intentionally presents the resolved nearby patron's inventory.
The same patron resolution is used by inventory use/drag/drop commands so the visible slots continue to address their authoritative
carrier. Shop purchase, stock, and pawn rules are documented separately in [Neutral Shops And Mercenary Camps](neutral-shops.md).

This slice intentionally does not yet implement Warsmash's held-item cursor
art, inventory-slot swapping, or allied-unit give-item targeting. Inventory
Data D/Data E (`CanGetItems` / `CanDropItems`) are enforced on player orders
while direct script/native insertion and removal continue to bypass those
order permissions.

Successful transitions and carried-item charge changes refresh the inventory
layer for clients currently selecting the carrier. Hidden entities are also
excluded from renderer hit and rectangle tests while snapshot removal is in
flight.

## Phase Boundary

This slice includes generic item icon/tooltips, ability-defined capacity,
runtime/displayed charges, and successful synchronous use of the existing
immediate item ability handlers when Inventory Data C permits item use.
Perishable synchronous uses consume one charge and destroy the item at zero.
Passive item effects attach on inventory entry only for carriers whose
Inventory ability permits item use; held orb/poison attack hooks use the same
permission. Detach/removal always reverses an effect that was already applied,
even if `CanUseItems` changed while the item was carried, so permission changes
cannot leak a permanent stat bonus.

Still missing are automatic `powerup` acquisition/use, `cooldownID`/`ignoreCD`
item cooldowns and disabled icons, held-item cursor art, slot swapping, and
allied-unit giving. Soul Trap implements the stock target/capture/reveal/release
flow; unusual custom `AIso` targets and their subsystem-specific behavior still
need parity coverage.

The implementation is derived from observable behavior and Warcraft III data
formats described by the clean-room specification. It does not depend on
another engine's item implementation.

## Validation

The `wc3_items.*` in-engine tests cover world-state initialization, data-driven
capacity (including reduced, zero, above-storage-limit, and implicit ROC Hero
`AInv` cases),
first-empty-slot insertion,
full-inventory failure, pickup range and revalidation, drop identity, point-drop
deferred execution/movement/revalidation, save/load of the active drop-item pointer, renderer
visibility flags, carried-item removal, connection-state refresh gating, charge
initialization/preservation, carried-charge refresh/no-op behavior, perishable
use decrement/removal, non-perishable decrement-without-removal behavior, JASS charge access,
and generic `spro` Art/Tip/Ubertip/charge presentation.
`SetItemDropID` coverage includes rawcode overwrite, null-handle tolerance,
inventory/drop preservation, and setter-to-save/load round trips.
They also cover mixed-selection Smart pickup where a non-inventory unit is the
first selected entity and a later ROC Hero must still receive the item order,
Human Backpack carriers refusing active item use, and Data B death policy where
a non-Hero carrier drops its items while Hero inventory retains them.
Minimal `AbilityData.slk`, `UnitAbilities.slk`, `ItemData.slk`, `ItemFunc.txt`,
`ItemStrings.txt`, and `war3skins.txt` fixtures keep these tests data-driven in
both ROC and TFT test runs. Inventory-panel tests additionally cover the
no-inventory cover, reduced-capacity fillers, full-capacity absence of fillers,
race-skin selection, and the native cover frame's crop/geometry. Synthetic
ability-only unit IDs such as `H001` have no `UnitBalance` life value, so the
shared test-unit allocator initializes a minimum positive life value without
weakening the runtime rule that zero-life units are corpses and cannot be selected.

`AIat` is represented as `unitAttack_t.temporaryDamageBonus`, so it survives Hero stat recomputation and is rendered as a separate green/red attack modifier. `AIde` uses `temporary_armor_bonus` so Hero Agility recomputation likewise preserves item armor. See [Attack Damage](attack-damage.md).
