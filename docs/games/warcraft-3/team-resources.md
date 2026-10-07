# Team Resources And Advanced Shared Control

## Contract

Warcraft III has directional passive alliances, basic shared control, and advanced
shared control. The viewer is the first index of `level.alliances[viewer][owner]`.
`G_UnitCanControl` allows ordinary orders when the viewer treats the unit owner
as a passive ally and either control flag is set. `G_UnitCanSpendResources`
requires advanced control for an allied unit, while the owning player can always
use their own active unit. Neither helper transfers resource ownership.

A controller of another player's Barracks must spend the *owner's* gold and lumber,
not the controller's. Resource costs are charged by the producer/research/revival
paths against the producer's owning player. An advanced-control bit by itself
must not convert a hostile player into a friendly controllable army.

## Implemented Paths

| Surface | Authority boundary |
| --- | --- |
| Normal allied move/attack/hold and selection | `G_UnitCanControl` |
| Training and reviving a Hero via `CLIENTCOMMAND(Button)` | `G_UnitCanSpendResources` |
| Research, building upgrades | `CLIENTCOMMAND(Research/Upgrade)` advanced gate |
| Worker construction placement menu and click | `s_build.c` advanced gate |
| Production queue cancellation and cancel command | `G_UnitCanSpendResources` |
| Production command-card display | `Get_Commands_f` disables spending buttons, including the Build submenu entry, without advanced sharing; `UI_WriteBuildQueue` offers queue cancel hit targets only to viewers who may cancel |
| Alliance changes | `G_SetPlayerAlliance` invalidates shortcuts and source player's command card |

The permission checks apply to *player-initiated* orders; internal AI, map script
and simulation calls such as `SP_TrainUnit` deliberately do not require a local
UI controller. This is essential for bots and campaign triggers.

## Unimplemented And Unverified

- A provisional Team Resources `svc_layout` renderer now exists on `LAYER_GAME_3`
  (`WC3_LAYER_MULTIBOARD`); `LAYER_GAME_2` is the command-error overlay.
  It shows eligible allies' names, gold, lumber and used/cap food values.
  Custom multiboards take precedence, and multiboard suppression hides the panel.
- The retail board's exact stock FDF geometry, minimize buttons, custom-board fallback priority,
  and food display formatting remain unverified against retail assets.
  Current geometry is provisional, not pixel-perfect.
- The F11 Allies dialog has a single stock Units checkbox. It displays either
  basic or advanced shared-control permission as checked; turning it off stages
  revocation of both permissions, and accepting the dialog applies both bits.
  Turning it on grants basic control only. A separate advanced-control control
  is not present, and no unverified retail UI has been invented.
- The F11 dialog's grant/revoke normalization is regression-covered by
  `wc3_allies.*` tests in `hud/hud_allies.c`.
- Multiplayer abandoned-player transitions and any rejoin-specific policy are
  not implemented here.
- Individual ability-driven resource spending and neutral shop purchases need
  separate path-by-path inspection; stock unit production guards do not prove
  coverage of every custom ability.
- There are permission helper tests and command-card invalidation tests, but
  not full multiplayer command-level end-to-end integration coverage.

## Verification

The relevant source is `g_commands.c`, `g_utils.c`, `hud/hud_infopanel.c`,
`hud/hud_allies.c`, `skills/s_build.c` and `skills/s_train.c`.
Regression tests live in `tests/t_api.c`, `tests/t_building.c`, `tests/t_multiboard.c` (panel layer, visibility, scheduler path and leaderboard stacking), and
`hud/hud_allies.c`. Save/load round-trip coverage for multiboard suppression
and local Team Resources collapse state lives in `tests/t_game.c`.

Suggested local checks (not run when producing this patch):

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.advanced_shared_control*'
make test-wc3-engine WC3_PATTERN='wc3_building.advanced_control*'
make test-wc3-engine WC3_PATTERN='wc3_allies.*'
make test
```

See also [Selection and Control](selection-and-control.md) and
[Multiboard and TextTag Natives](multiboard-and-texttag.md).

## Team Resources Display Wiring

`G_CanViewTeamResources(viewer, owner)` drives eligibility. The UI reads the
owner's authoritative resource state; normal harvesting, spending and resource
bar invalidation trigger refreshes for permitted viewers. Alliance mutations
invalidate the source viewer's panel. A map-created multiboard uses the same
`WC3_LAYER_MULTIBOARD` slot rather than rendering alongside Team Resources.

The UI does not grant sharing or transfer resources. A client cannot obtain
resource values for non-eligible players through this panel.
The square control beside the Team Resources title toggles only that client's resource rows. The title and control remain visible while collapsed, and the local collapse state is saved with the game.
