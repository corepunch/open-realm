# Multiboard And TextTag JASS Natives

Server-owned registries for DotA-style scoreboards and floating combat/gold text.
Parent tracking: [dota-map-playability.md](dota-map-playability.md) / GitHub #434.

## Contract

| Handle | Registry | Notes |
| --- | --- | --- |
| `multiboard` | `level.multiboards[]` | Title, rows/cols, cells, per-client display/minimize bits |
| `multiboarditem` | `level.multiboard_items[]` | Refcounted view into one cell; `ReleaseItem` frees the view |
| `texttag` | `level.texttags[]` | Text, color, unit/world anchor, velocity, lifespan, visibility |

`MultiboardDisplay` / `MultiboardMinimize` / `SetTextTagVisibility` follow the leaderboard local model: `currentplayer` (GetLocalPlayer context) updates one client slot; a global call updates every connected client mask.

Destroying a multiboard sets `item->board = -1` so outstanding item handles no longer mutate cells. Cell data lives on the board, not on the item view.

## Implemented DotA Surface

Multiboard (17): create/destroy, display, minimize/`IsMultiboardMinimized`, title, row/column count, get/release item, per-item style/value/color/width/icon, broadcast style/width.

TextTag (10): create/destroy, text+height, color, `PosUnit`, velocity, visibility, permanent, lifespan, fadepoint.

Other `common.txt` natives (`IsMultiboardDisplayed`, `MultiboardClear`, `SetTextTagPos`, …) are not registered yet.

## Presentation Gaps

- **Multiboard HUD:** simulation state and dirty bits exist (`multiboard_dirty_clients`). No FDF/`svc_layout` publisher yet — unlike leaderboards in [leaderboards.md](leaderboards.md).
- **TextTag draw:** state is stored only. Live JASS texttags are **not** wired to `TE_FLOATING_TEXT` yet. Resource-gain labels remain a one-shot path ([resource-gain-text.md](resource-gain-text.md)).

Do not widen `entityState_t` / `playerState_t` for either widget.

## Save/Load

Save format version 28 persists multiboards, item views, and texttags (including `texttag.unit` via `F_EDICT`) and JASS handle identity through registry indexes.

## Verification

```bash
make test-wc3-engine WC3_PATTERN='wc3_api.multiboard*'
make test-wc3-engine WC3_PATTERN='wc3_api.texttag*'
make test-wc3-engine WC3_PATTERN='wc3_api.leaderboard*'
```

Tests live in `games/warcraft-3/game/tests/t_multiboard.c`.
