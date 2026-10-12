# Rexxar's Stash (OrcX01) — Object Data and Inventory Compatibility

## Contract

The extracted OrcX01 report identifies **one map-placed building**, custom unit
`h003` based on Human Farm `hhou`, at `(320, -1728)` facing 270 degrees. The
original map script hides it during initialization and calls `ShowUnit` after
Meet Thrall cleanup. **The engine must not spawn a second stash, special-case
`h003` in inventory code, or substitute a bank UI.**

The following fields come from the user's extracted **`war3map.w3u`** report,
not from the test fixture or a directly mounted OrcX01 archive:

| W3U field | Extracted value | OpenRealm consumer |
|---|---|---|
| `unam` | `Rexxar's Stash` | `UnitProfile.name` |
| `upgr` | `Ropm` | `UnitBalance.upgrades` (unit's upgrade association) |
| `uabi` | `Aion,Avul` | `UnitAbilities.abilList` |
| `umdl` | `buildings\\other\\WyvernCage\\WyvernCage.mdl` | `UnitUI.modelFile` |
| `usca` | `0.6` | `UnitUI.modelScale` |
| `ussc` | `2.0` | `UnitUI.selectionScale` |
| `upap` | empty | `UnitBalance.preventPlace` |
| `usnd` | `ArcaneObservatory` | `UnitUI.soundLabel` |
| `ufma` | `0` | `UnitBalance.foodMade` |

`G_SetMapUnitOverrides` merges custom `h003` into per-map typed rows inherited
from `hhou`. `G_BindEntityData` attaches those rows before `SP_SpawnUnit`, which
uses the model path/scale/selection data and initializes invulnerability from
`Avul`. `ShowUnit` controls `RF_HIDDEN`; hidden units are excluded from normal
selection and from player-issued inventory handoffs.

## `Aion`, `Ropm`, and six inventory slots

`Aion` is Orc Unit Inventory (`AInv` mechanic). `Ropm` is Orc Backpack.
`upgr=Ropm` is **not** a grant of researched tech. OpenRealm checks the owner's
**actual** `G_GetPlayerTechResearchedLevel(owner, Ropm)` before exposing `Aion`.
Thus the original campaign player/owner setup, including
`SetPlayerTechResearched`, must be examined; never grant Backpack globally to
make `h003` work. `Avul` is independent of the inventory ability.

The extracted map description says the stash holds **six** items. Stock Orc
Backpack carriers normally hold two. OpenRealm resolves capacity from the
effective `AInv` ability's `inv1` / `DataA1` and caps it at `MAX_INVENTORY=6`.
An original-object `Aion` change in `war3map.w3a` setting `inv1=6` would be
applied by `G_SetMapAbilityOverrides`, but **the actual OrcX01 `war3map.w3a`
has not been supplied**. This patch intentionally does not invent such an
override or force `h003` to six slots without proof.

`inv3` (Can Use Items), `inv4` (Can Get Items) and `inv5` (Can Drop Items)
remain the effective ability's independent authoring decisions. They are not
inferred from the six-slot map description. The inventory HUD reads
`G_InventoryCapacity` for non-Hero units as well as Heroes.

When `SetPlayerTechResearched` or `AddPlayerTechResearched` changes a stock
Backpack tech, the active inventory capacity can change for a **selected**
unit. `G_ApplyTechLevelToOwnedUnits` now calls
`G_RefreshInventoryAfterTechChange` after the researched level has changed,
refreshing the focused inventory for the owner and any other viewer who has
selected the owner's affected carrier. This path is independent of the
upgrade's `UpgradeData` stat-effect row, and it deliberately does not move,
spawn or delete inventory items.

## Campaign storage and cross-map persistence

Per the supplied extracted JASS report, OrcX01 moves six stash slots into a
hidden temporary `Obla` unit, stores that carrier in `Campaigns.w3v` under
mission `OrcX02` and key `Stash`, then removes the temporary unit. Patch A
implements generic targeted item handoff and scripted transfer; Patch B
handles the temporary carrier's item cleanup after the game-cache value
snapshot. See [inventory and items](inventory-and-items.md) and
[campaign game cache](campaign-game-cache.md).

The **exact** OrcX02 restoration script, stash owner/player ID, any Backpack
research grant, original W3A changes, and return-visit spawning sequence
remain unverified without original OrcX map scripts and ability data.

## Verification

Headless regression fixtures in `t_items.c` are synthetic representations of
this reported W3U object and of a **hypothetical** original-table W3A override
(`Aion.inv1=6`). They check object-data inheritance, researched-versus-unresearched
Backpack, stock two-slot and authored six-slot capacities, six-slot insertion,
non-Hero Can Use/Get/Drop rules, hidden selection filtering, and refreshing an
already-selected carrier when Backpack research changes. They do **not** assert
that the unavailable original map contains that W3A override.

Run after building, for both ROC and TFT test configurations:

```bash
make test-wc3-engine WC3_PATTERN='wc3_items.rexxar_stash_*'
```

Runtime-only verification: start OrcX01, ensure the placed stash at the
reported coordinates is hidden before Thrall and shown afterward; inspect its
owner and tech state; confirm six effective slots; transfer items into and out
of it and enter OrcX02 without losing or duplicating them. If capacity remains
two, inspect actual `war3map.w3a` and the gameplay scripts **before** changing
the engine. Tests do not establish model appearance, sound or the retail map's
quest-trigger ordering.
