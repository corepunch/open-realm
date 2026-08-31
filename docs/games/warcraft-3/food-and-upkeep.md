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

Only the active head may reserve food:

```text
producer->build (active head)
        -> G_ReserveTrainingFood()
        -> Used + head.foodUsed <= Cap ?
              yes: head owns food.used and training advances
              no:  progress remains unchanged and the next tick retries

head->build (later queue item)
        -> food.used remains zero until it becomes the head
```

`G_GetTrainCommandState()` also reports current gold, lumber, and food shortages, but that UI/order check is not the authoritative reservation. Supply can change while an item waits, so `ai_train_build()` re-checks at the active queue slot.

Resource shortages use `BUILD_COMMAND_UNAFFORDABLE`, not the prerequisite-only `BUILD_COMMAND_DISABLED` state. This keeps the train/build icon visible and clickable while still rejecting the order authoritatively. `SP_TrainUnit()` re-checks the state on click and sends the specific reason (`Not enough gold`, `Not enough lumber`, or `Not enough food`) through the gameplay message layer. Requirement-disabled commands remain inert.

Completion does not charge Food Used again. The queue entity already owns the reservation and becomes the visible trained unit; completion only activates `foodMade` if the trained unit type provides supply. This is the ownership transfer:

```text
hidden queue edict with food.used=N
        -> reveal same edict
        -> visible unit still owns food.used=N
```

Destroying or explicitly removing a producer clears any food reservation owned by its training queue so a hidden orphan cannot strand Food Used. The current queue still has no user-facing train-item cancellation/refund path. When that lifecycle is implemented it must clear only the cancelled item's actual `edict.food.used` reservation; later queue entries normally own zero food. Gold/lumber refund policy for producer destruction remains separate from this food cleanup.

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

Resource acquisition remains split into gross gathering and player income. Workers keep the full carried amount; gold/lumber deposit calls `G_ApplyResourceIncome()` immediately before crediting the player. Standard lumber remains 100%, while the generic lumber-rate player state is still honored if explicitly changed. Blighted-mine and Wisp interval income use the same helper rather than bypassing player income policy.

Stored resources are never retroactively changed when the upkeep tier changes.

## HUD And JASS

The resource bar displays `FoodUsed/FoodCap` when cap is nonzero and only `FoodUsed` when cap is zero. Supply text turns red when `FoodUsed > FoodCap`. The upkeep label uses `No Upkeep`, `Low Upkeep`, and `High Upkeep` from the same 50/80 thresholds.

The JASS rawcode natives `GetFoodMade(unitId)` and `GetFoodUsed(unitId)` read generic `UnitBalance` object data. Unit-instance `GetUnitFoodMade` / `GetUnitFoodUsed` continue to expose the unit type's configured values; the internal `edict.food` fields are bookkeeping, not a new JASS handle contract.

## Known Gaps

- Training queue cancellation/refund economics are still missing because the current command lifecycle has no train-item cancel operation. Producer death/removal releases any active food reservation, but the pre-existing hidden queue entities and gold/lumber refund policy still need an explicit cancellation lifecycle. Do not infer cancellation from queue presence.
- Hero altar production/revival still needs its command-specific food validation/reservation and displayed revival cost; direct `G_ReviveHero()` only restores the live unit's accounting.
- `PLAYERSTATE_GOLD_GATHERED` / `PLAYERSTATE_LUMBER_GATHERED` remain storage/API states. This patch does not guess whether their Warcraft-compatible cumulative semantics are gross harvested or net credited.
- Upkeep HUD strings remain authored directly by the current resource-bar code; localization through Warcraft UI string data is separate work.
- `war3map.w3u` unit-object overrides remain subject to the existing normalized object-data merge limitations documented elsewhere.

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

These commands are verification guidance only; do not replace the test fixture archives with local Warcraft data.
