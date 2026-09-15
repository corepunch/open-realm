# Leaderboards And Counted Objective HUDs

OpenRealm implements Warcraft III leaderboards as server-owned JASS state rendered through the stock `UI\\FrameDef\\UI\\LeaderBoard.fdf` frame. This covers campaign-style counted objectives such as a title plus a numeric row that changes as units are trained.

## Ownership

A leaderboard handle owns its label, display flag, style defaults, ordered items, colors, and requested row count. Each item owns a copied label, integer value, optional player number, item style flags, and optional label/value color overrides. The map/JASS script owns the actual counter; the leaderboard only stores and presents values supplied through natives such as `LeaderboardSetItemValue`.

`PlayerSetLeaderboard` stores one leaderboard registry index per Warcraft player. A board is visible to a client only when that player is assigned the board and its per-client `LeaderboardDisplay(lb, true)` visibility bit is set. Global calls affect all client slots; calls in `GetLocalPlayer()` context affect only that player. Switching assignments does not destroy either board.

## Implemented Native Surface

All 27 leaderboard natives registered by `api_module.c` are backed by state:

- create/destroy, display/query, item count, requested row sizing;
- add/remove/remove-player/clear;
- stable sort by value, player, or label in either direction;
- player-item membership/index lookup;
- board label get/set;
- per-player assignment get/set;
- board title/value colors and board style flags;
- item value/label/style and item label/value colors.

Strings pass through `G_LevelString()` on mutation, so map `TRIGSTR_` strings follow the existing WTS path. Native item indexes are zero-based.

## HUD

`UI_LoadHudLeaderboards()` loads the authored `LeaderBoard.fdf` root, backdrop, title, and list container. `LAYER_LEADERBOARD` is a dedicated server-authored layout layer appended after the timer-dialog layer, so leaderboard refreshes do not resend unrelated HUD panels.

The stock title/backdrop/container provide the board chrome. Runtime rows are emitted as text frames parented to `LeaderboardListContainer`: a left name/label column and a right integer-value column. Row height and width are derived from the authored list-container dimensions when available, with conservative font-based fallbacks.

The common campaign-counter case therefore renders as a stock leaderboard title plus a changing value without rebuilding the leaderboard handle.

`LeaderboardSetSizeByItemCount` controls the number of presented row slots; it does not mutate `LeaderboardGetItemCount`.

## Save/Load And Reconnect

Save format version 24 persists the fixed leaderboard registry, all item/style/color state, player assignments, and JASS `leaderboard` handle identity through stable registry indexes. Layout payloads are transient; load marks connected clients dirty so the board is republished on the next server frame. `ClientBegin` also publishes the assigned board for a joining/reconnecting client.

## Known Limits

- `showIcons` and per-item `showIcon` are stored but icons are not rendered yet because classic icon source/packing semantics are not established confidently.
- Exact retail row-column widths, player-color/name styling, and backdrop resizing are not yet pixel-matched. Current rows are laid out relative to the authored list container rather than hard-coding a campaign-specific HUD.
- Only the player's assigned leaderboard is presented, matching the `PlayerSetLeaderboard` ownership model. Multiboard interaction/repositioning remains separate work.

These limits do not block the counted-objective path where a campaign script creates a board, assigns it to the player, adds one numeric row, and updates that row from 0 through a target value.

## Regression Coverage

Tests cover creation, labels, item insertion/update, stable sorting/player lookup, assignment/display state, and save/load restoration of handle alias identity and item/color state.

## Compact HUD Placement

The board is anchored to the same widescreen-aware top-right position and top offset as the TimerDialog. Its root, backdrop, and list container are compacted to the actual visible row count.
