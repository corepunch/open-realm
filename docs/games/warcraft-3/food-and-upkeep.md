# Warcraft III Food, Supply, And Upkeep

## Contract

Food is generic unit data, not a Farm special case. `UnitBalance.slk` fields `fused` / `fmade` (and their object-data aliases) populate `UnitBalance_t.foodUsed` / `foodMade`. The authoritative runtime aggregate remains the owning player's `PLAYERSTATE_RESOURCE_FOOD_USED` and `PLAYERSTATE_RESOURCE_FOOD_CAP`, but every live `edict_t` also owns the amount it has actually contributed through `edict.food.used` / `edict.food.made`.

Keep those two levels separate:

```text
UnitBalance foodUsed/foodMade
        -> per-edict accounted food
        -> owning player Food Used/Food Cap
```

Do not add or subtract the type value directly in lifecycle code. Use `G_SetUnitFoodUsed()` / `G_SetUnitFoodMade()` so repeated activation is idempotent and death, removal, and ownership changes can reverse exactly the amount previously accounted.

`G_ActivateUnitFood()` activates both values for an already active unit. Construction and training deliberately use narrower transitions because their two food values become active at different times.

## Lifecycle

Pre-placed live units are normalized through the same per-edict setters during `G_ClientBegin()`. Runtime CreateUnit-style creation, summons, figurines, and debug unit spawning activate food after the entity exists. Dead units do not contribute.

Construction is:

```text
placement accepted
    -> validate Food Used against current cap
    -> spawn foundation
    -> foundation owns its Food Used
    -> Food Made remains zero while construction.active
    -> completion assigns UnitBalance.foodMade to the building
```

This gives symmetric provider behavior:

```text
completed food provider -> +Food Made
provider death/removal  -> -the same Food Made
```

Destroying providers may therefore leave `Food Used > Food Cap`. Existing units and active training reservations remain alive/accounted; the over-cap state only prevents a new positive-food reservation from succeeding.

`unit_die()` and `G_FreeEdict()` clear the edict's accounted Food Used and Food Made immediately. Food is therefore released at the death transition rather than corpse decay. `G_SetUnitPlayer()` transfers any accounted contribution from the old owner to the new owner without re-reading type data, so Charm and `SetUnitOwner` preserve the exact runtime contribution.

Hero revival reactivates the revived hero's unit-data food contribution. Trigger/native revival is a forced lifecycle transition; altar-specific queue/cost policy remains a separate production feature.

## Training Queue Reservation

The existing training queue stores hidden queued units as linked edicts through `edict.build`. Food reservation uses those same entities; no parallel queue structure is required.

Only the active head may reserve food. The reservation is attempted immediately when an empty queue receives its first item, and immediately when completion/cancellation promotes a waiting item to the head; the per-frame retry remains authoritative while a head is food-blocked:

```text
producer->build (active head)
        -> G_ReserveTrainingFood()
        -> Used + head.foodUsed <= Cap ?
              yes: head owns food.used and training advances
              no:  progress remains unchanged and the next tick retries

head->build (later queue item)
        -> food.used remains zero until it becomes the head
```

Immediate head reservation matters for command-time checks. Once item A becomes the active head, a second Train click must observe A's newly accounted Food Used; only later waiting items are omitted from the food total. Gold/lumber remain charged at queue insertion for every item.

`G_GetTrainCommandState()` also reports current gold, lumber, and food shortages, but that UI/order check is not the authoritative reservation. Supply can change while an item waits, so `ai_train_build()` re-checks at the active queue slot.

Resource shortages use `BUILD_COMMAND_UNAFFORDABLE`, not the prerequisite-only `BUILD_COMMAND_DISABLED` state. This keeps the train/build icon visible and clickable while still rejecting the order authoritatively. `SP_TrainUnit()` re-checks the state on click and sends the specific reason (`Not enough gold`, `Not enough lumber`, or `Not enough food`) through the gameplay message layer. Requirement-disabled commands remain inert.

Completion does not charge Food Used again. The queue entity already owns the reservation and becomes the visible trained unit; completion only activates `foodMade` if the trained unit type provides supply. This is the ownership transfer:

```text
hidden queue edict with food.used=N
        -> reveal same edict
        -> visible unit still owns food.used=N
```

A food reservation failure emits `Not enough food` once when that item first becomes the active blocked head. The item records that notification state so the per-frame retry does not spam the player. When supply later becomes available, successful reservation clears the waiting state and refreshes the selected producer's queue panel.

The build-queue panel uses zero `starttime/endtime` as a server-authored sentinel for a food-blocked head. The client holds the production bar at zero and continues drawing all waiting queue icons. When the head eventually reserves food, the server rewrites the panel with normal timing, so the progress bar starts from that transition rather than pretending training continued while supply-blocked.

Queue icons are cancellation targets. The server receives `canceltrain <slot>`, unlinks that exact hidden training edict, refunds its configured gold/lumber cost, releases only food actually owned by `edict.food.used`, and frees the hidden entity. Cancelling an unreserved blocked head therefore cannot reduce some other unit's Food Used. If the head is cancelled, the next item becomes active and immediately attempts its own reservation.

Destroying or explicitly removing a producer runs the same queue-cancellation ownership path for every queued item: all paid gold/lumber is refunded, the active reservation (if any) is released, and hidden queue entities are freed. Producer destruction does not briefly activate later queue items while cancelling the queue.

## Food Ceiling And Debug Override

`Misc.FoodCeiling` initializes `PLAYERSTATE_FOOD_CAP_CEILING`. Provider accounting keeps the complete raw `PLAYERSTATE_RESOURCE_FOOD_CAP`; validation and HUD presentation use `G_GetEffectiveFoodCap()`, which clamps that raw value to the positive ceiling. This preserves provider contributions above the ceiling so later provider death can subtract the correct amount.

Runtime CVar:

```sh
+set wc3_food_limits 0
```

With food limits disabled, positive-food production no longer fails because `Food Used + cost > effective cap`. Food Used is still reserved/accounted, so the resource bar and upkeep continue to describe the actual army. Gold, lumber, tech requirements, producer lists, and placement rules are unaffected. The default is `wc3_food_limits 1`.

## Upkeep

Upkeep is derived from authoritative Food Used, not Food Cap. `InitConstants()` reads the Warcraft gameplay constants `Misc.UpkeepUsage`, `Misc.UpkeepGoldTax`, and `Misc.UpkeepLumberTax`; `war3mapMisc.txt` can therefore override the base game data through the existing misc-data load order. Tax values are fractions removed from income, so standard `0.00,0.30,0.60` gold tax produces 100%, 70%, and 40% income.

`G_SetUnitFoodUsed()` recomputes the rates whenever accounted food changes. Direct JASS `SetPlayerState(PLAYER_STATE_RESOURCE_FOOD_USED, ...)` does the same. Fresh player state initializes both upkeep-rate slots to 100 before the first food recomputation.

Resource acquisition remains split into gross gathering and player income. Workers keep the full carried amount; income sites now commit through `G_CreditResourceIncome()`, which uses the pure `G_ApplyResourceIncome()` calculation, credits the resulting net amount, then emits the matching resource-gain presentation event. Standard lumber remains 100%, while the generic lumber-rate player state is still honored if explicitly changed. Blighted-mine and Wisp interval income use the same commit helper rather than bypassing player income policy. The floating `+N` therefore reports the same net amount that reached the resource bar; see [resource-gain-text.md](resource-gain-text.md).

Stored resources are never retroactively changed when the upkeep tier changes.

## HUD And JASS

The resource bar displays `FoodUsed/FoodCap` when cap is nonzero and only `FoodUsed` when cap is zero. Supply text turns red when `FoodUsed > FoodCap`. The upkeep tier is still derived by `G_GetPlayerUpkeepTier()` from the active `UpkeepUsage` constants; HUD code does not maintain a second threshold table. `ResourceBarUpkeepText` resolves `UPKEEP_NONE`, `UPKEEP_LOW`, or `UPKEEP_HIGH` from `GlobalStrings.fdf` when present and retains the existing English labels only as a missing-data fallback.

Gold, lumber, supply, and upkeep hover help use the retail global-string identifiers `RESOURCE_UBERTIP_GOLD`, `RESOURCE_UBERTIP_LUMBER`, `RESOURCE_UBERTIP_SUPPLY`, and `RESOURCE_UBERTIP_UPKEEP`. `UI_LoadHudConsole()` explicitly loads `GlobalStrings.fdf` before the four stable named value frames from `ResourceBar.fdf` are serialized. Retail `ResourceBar.fdf` leaves the resource icon textures unnamed, so OpenRealm does not invent icon-frame names or hard-coded replacement hit rectangles. The named value frames retain FDF-owned geometry and are the authoritative passive hover targets.

The resource tooltip heading is live player state, not just the static Ubertip body. Gold, lumber, and food use the externalized `COLON_GOLD`, `COLON_LUMBER`, and `COLON_FOOD` labels followed by a literal `{value}` marker in the server-authored tooltip. The client expands that marker from the hovered frame's `Stat` binding through the same `SCR_GetStringValue()` path used to draw the visible bar. This is intentional: a console layout can have been serialized before the newest playerstate snapshot arrives, so baking the server's then-current Gold/Lumber/Food value into the tooltip can leave a stale `0` while the bar itself correctly displays a newer value. Food therefore also shares the exact `FoodUsed/FoodCap`/ceiling formatter used by the visible text. Upkeep uses `COLON_UPKEEP` plus the current localized `UPKEEP_*` label, followed by `COLON_GOLD_INCOME_RATE` and the current `PLAYERSTATE_GOLD_UPKEEP_RATE`. The console refresh cache includes both upkeep-rate player states so an upkeep-rate change refreshes the server-authored Upkeep heading.

Upkeep string layout is version-aware. If `RESOURCE_UBERTIP_UPKEEP` already contains multiple numeric tier ranges, OpenRealm treats that as a complete authored/localized legend and preserves it verbatim. Otherwise it prefers the split `RESOURCE_UBERTIP_UPKEEP_INFO` (`min-max`, localized tier label, income percent) or `RESOURCE_UBERTIP_UPKEEP_INFO_WOOD` (separate Gold/Lumber rates) format. If neither split key exists, OpenRealm still appends a fallback legend in the same row shape instead of leaving the player without an explanation of No/Low/High Upkeep. All generated rows come from the active `UpkeepUsage` thresholds, `FoodCeiling`, and the same tier-rate helpers used by `G_RecomputePlayerUpkeep()`; an unbounded final tier is shown with `N+ Food` rather than silently omitted. The `_WOOD` shape is selected when gameplay data actually taxes lumber. Thus `war3mapMisc.txt` threshold/tax overrides and generated upkeep legend rows share one source of truth.

The console layer always serializes the ordinary `FT_TOOLTIPTEXT` presentation frame because resource help must work even when no unit is selected and no command-card layer exists. Client layout hover treats any frame with non-empty `uiFrame_t.tooltip` as hoverable without making it clickable. Tooltip selection is generic across frame types, while drawing is restricted to the layer/window that owns the hovered frame so the same tooltip is not painted once per HUD layer.

The generic passive-hover contract and ownership rules are documented in [UI authoring](../../ui-authoring.md).

The JASS rawcode natives `GetFoodMade(unitId)` and `GetFoodUsed(unitId)` read generic `UnitBalance` object data. Unit-instance `GetUnitFoodMade` / `GetUnitFoodUsed` continue to expose the unit type's configured values; the internal `edict.food` fields are bookkeeping, not a new JASS handle contract.

## Known Gaps

- Queue cancellation now covers ordinary trained-unit entries and producer death/removal. Research/upgrade queues and altar-specific Hero revival are separate production systems and still need their own cancellation/cost lifecycles.
- Hero altar production/revival still needs its command-specific food validation/reservation and displayed revival cost; direct `G_ReviveHero()` only restores the live unit's accounting.
- Food-block feedback currently uses the existing gameplay text overlay. Race-specific Warcraft command-error sounds/localized `Nofood` strings need a dedicated command-error presentation path rather than hard-coded sound guesses in queue code.
- `PLAYERSTATE_GOLD_GATHERED` / `PLAYERSTATE_LUMBER_GATHERED` remain storage/API states. This patch does not guess whether their Warcraft-compatible cumulative semantics are gross harvested or net credited.
- `war3map.w3u` unit-object overrides remain subject to the existing normalized object-data merge limitations documented elsewhere.
- Classic data sets that embed multiple numeric tier ranges directly inside `RESOURCE_UBERTIP_UPKEEP` remain authored text. Data sets without an embedded table get generated rows from the active upkeep constants, using split `RESOURCE_UBERTIP_UPKEEP_INFO[_WOOD]` strings when available and a compact fallback row format otherwise.

## Verification

The focused engine tests are:

```sh
make test-wc3-engine WC3_PATTERN='wc3_food.*'
make test-wc3-engine WC3_PATTERN='wc3_building.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.trained_unit_*'
make test-wc3-engine WC3_PATTERN='wc3_combat.hero_revive'
```

Also run the existing gold/lumber movement tests because upkeep is applied at their deposit boundary:

```sh
make test-wc3-engine WC3_PATTERN='wc3_movement.gold_*'
make test-wc3-engine WC3_PATTERN='wc3_movement.lumber_*'
```

Manual HUD verification should also cover hovering all four resource values with no unit selected, then with a command card visible, to confirm exactly one tooltip backdrop is drawn and moving onto a command button transfers tooltip ownership cleanly. Confirm the Gold/Lumber/Food headings contain the current amounts and continue changing after the HUD layout was first sent, and that the Upkeep heading contains the current Gold income rate. Verify both upkeep-data shapes: an archive exposing `RESOURCE_UBERTIP_UPKEEP_INFO` should use the authored row format, while an archive with only a prose `RESOURCE_UBERTIP_UPKEEP` body should still receive the generated No/Low/High tier legend. Also verify an archive/language with the `UPKEEP_*` global strings so the upkeep label is externalized rather than falling back to English.

These commands are verification guidance only; do not replace the test fixture archives with local Warcraft data.
