# AI Upgrade Costs And Town Spot

## Contract

Warcraft III exposes `GetUpgradeGoldCost(integer id)`, `GetUpgradeWoodCost(integer id)`,
`SetUpgrade(integer id)`, and `ShiftTownSpot(real x, real y)` through `common.ai`. OpenRealm
implements them in the AI JASS context, so the current AI player comes from
`jass_getcontext(j)->playerState`; there is no explicit player parameter.

`GetUpgradeGoldCost` and `GetUpgradeWoodCost` are next-level queries. They read the AI player's
researched level, add one, and pass that target level through the same `UpgradeData.slk`
base/mod cost helpers used by research. An unknown upgrade, missing AI player, or request beyond
`maxLevel` returns zero. `GetUpgradeLumberCost` remains registered only as an OpenRealm
compatibility alias for older local scripts; retail `common.ai` names the native
`GetUpgradeWoodCost`.

`SetUpgrade` is a one-shot request. `common.ai` owns desired-level planning, next-level cost
checks, retry timing, and build-loop ordering. OpenRealm selects a live, completed, owned producer
whose ordinary research command is currently available and calls the shared `G_QueueResearch`
path. That path remains authoritative for requirements, resource payment, per-player in-progress
locking, queue limits, research time, events, cancellation/refund, and completion. A duplicate
request therefore returns false while the same upgrade is already in progress instead of creating
a second AI-only research state.

`ShiftTownSpot` stores a persistent per-AI construction-search override. It does not move a Town
Hall, worker, captain, camera, pathing state, or any other world entity. While the override is
valid, `G_BotBuildNearTown` searches legal building sites around that coordinate instead of the
selected town hall's origin. A later `ShiftTownSpot` replaces the stored coordinate; stopping or
restarting the AI clears it with the rest of `bot_t`.

## Data Flow

```text
GetUpgradeWoodCost(id)
  -> current AI player
  -> G_GetPlayerTechResearchedLevel(player, id) + 1
  -> UpgradeData.maxLevel guard
  -> G_UpgradeLumberCost(id, next_level)
  -> integer lumber cost

SetUpgrade(id)
  -> current AI player
  -> find owned completed producer with BUILD_COMMAND_AVAILABLE
  -> G_QueueResearch(producer, id)
  -> ordinary research validation/payment/queue/events/completion
  -> boolean accepted

ShiftTownSpot(x, y)
  -> current AI player's bot_t.town_spot
  -> later SetProduce(... building ...)
  -> G_BotBuildNearTown
  -> legal placement search around town_spot
```

Upgrade costs are authored by `Units\UpgradeData.slk`: `goldbase`, `goldmod`, `lumberbase`,
`lumbermod`, and `maxlevel`. Do not duplicate their arithmetic in the AI layer.

## Compatibility Evidence

Retail `common.ai` declares `GetUpgradeGoldCost`, `GetUpgradeWoodCost`, and `ShiftTownSpot`. Its
`StartUpgrade` helper checks the current upgrade level, queries the gold and wood costs, then calls
`SetUpgrade`. Warcraft AI documentation labels the cost query as the cost of the next upgrade.
Community AI testing documents `ShiftTownSpot` as moving the town's build spot; a common example
is shifting it into shallow water to start a Shipyard and shifting it back afterward so later
buildings are not placed by the water.

These sources establish the next-level cost contract and construction-placement effect. They do
not establish retail's exact invalid-rawcode or max-level return values, so OpenRealm uses the
existing safe zero convention for those cases.

## Verification

`wc3_bot.query_natives_read_authoritative_player_state` uses fixture `UpgradeData.slk` values where
level 3 differs from level 1 and 2, proving that the AI query follows the player's researched
level rather than a fixed base cost. The same script exercises both retail `GetUpgradeWoodCost`
and the legacy `GetUpgradeLumberCost` alias.

`wc3_bot.set_upgrade_uses_normal_research_queue_and_rejects_duplicate_request` executes
`SetUpgrade('Rhme')` from a player-private AI VM, verifies that the normal producer queue owns the
research, that standard level-one costs are deducted, and that an immediate duplicate request is
rejected by the shared in-progress lock.

`wc3_bot.shift_town_spot_redirects_subsequent_build_search` executes `ShiftTownSpot` from an AI
script, then asks `G_BotProduce` for a building and verifies the accepted build waypoint is near
the shifted construction center while the Town Hall itself remains unmoved.

Per `CONTRIBUTING.md`, code and executable fixture changes require building the affected targets
and running `make test` before committing. For documentation-only changes, validate text, relative
links, and `git diff --check`; no game launch or full suite is needed.
