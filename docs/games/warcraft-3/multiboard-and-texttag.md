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

`IsMultiboardDisplayed` and `MultiboardSuppressDisplay` are registered. Other `common.txt` natives (`MultiboardClear`, `SetTextTagPos`, …) remain outstanding.

## Presentation Gaps

- **Multiboard HUD:** `hud_multiboard.c` now emits `svc_layout` for a displayed custom board (title, cell text/colors/icons, minimized title) or the automatic Team Resources display. Single-board ownership is per client; showing a custom board replaces the previous custom board. Suppression hides the presentation without changing requested visibility.
- **Retail parity gaps:** stock multiboard FDF skin/geometry, cell width, UI minimize interaction and leaderboard coexistence still need asset-driven verification. Custom boards take precedence over Team Resources, and suppression hides both; see [Team Resources and Advanced Shared Control](team-resources.md). The current panel uses a conservative provisional layout.
- **TextTag draw:** state is stored only. Live JASS texttags are **not** wired to `TE_FLOATING_TEXT` yet. Resource-gain labels remain a one-shot path ([resource-gain-text.md](resource-gain-text.md)).

Do not widen `entityState_t` / `playerState_t` for either widget.

## Save/Load

Save format version 69 persists multiboards, item views, and texttags (including `texttag.unit` via `F_EDICT`) and JASS handle identity through registry indexes, plus per-client suppression and Team Resources collapse flags.

## Verification

```bash
make test-wc3-engine WC3_PATTERN='wc3_api.multiboard*'
make test-wc3-engine WC3_PATTERN='wc3_api.texttag*'
make test-wc3-engine WC3_PATTERN='wc3_api.leaderboard*'
```

Tests live in `games/warcraft-3/game/tests/t_multiboard.c`.

Team Resources is a distinct engine-managed display on `LAYER_GAME_2`; see [Team Resources and Advanced Shared Control](team-resources.md).
