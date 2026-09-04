# WC3 Resource-Gain Floating Text

## Scope

Warcraft resource income has two separate responsibilities:

1. gameplay commits the final amount to the owning player's resource state; and
2. presentation reports that committed amount as a transient world label such as `+10`.

For ordinary workers the label belongs to **deposit**, not extraction. A Peasant may leave a Gold Mine carrying ten gold without changing the player's gold total or creating a gain label. When it reaches a compatible drop-off, the income transaction commits and a label is spawned from the worker's position. Lumber follows the same rule. Direct-income paths such as the current Wisp and Blighted Gold Mine implementations emit at the source unit when their existing income transaction commits.

`G_CreditResourceIncome()` is the common commit boundary. It calls the existing pure `G_ApplyResourceIncome()` upkeep calculation, updates `playerState_t.stats`, and only then calls `G_ResourceGainEvent()` with the **net credited amount**. This keeps the visual number consistent with the resource bar under Low/High Upkeep. Existing `GAME_MSG_HARVEST_DEPOSIT_*` messages remain transition observations with `{type, actor, target}` and are not overloaded with presentation data.

## Server-selected presentation contract

`games/warcraft-3/game/g_resource_text.c` owns Warcraft data lookup and turns GOLD/LUMBER into resolved presentation values. The generic engine sees only `TE_FLOATING_TEXT` with this payload:

```text
POSITION world anchor
STRING   final text (for example "+7")
LONG     packed RGBA colour
SHORT    registered font index
LONG     lifetime in milliseconds
LONG     fade-start in milliseconds
FLOAT    horizontal screen velocity in pixels/second
FLOAT    vertical screen velocity in pixels/second
```

There are deliberately no `TE_GOLD_TEXT` / `TE_LUMBER_TEXT` shared enums and no resource-name table in `client/`. This follows [server-selected-effects.md](../../architecture/server-selected-effects.md): game code resolves game-specific content, while shared/client code transports and renders generic resolved presentation.

Current Warsmash accepts a player index in `unitGainResourceEvent()` but drops it before `SimulationRenderController.spawnTextTag()`. OpenRealm therefore multicasts this parity event without owner filtering. The client still draws it only while its world anchor projects inside the active world scissor. This document does not infer stricter retail multiplayer visibility from Warsmash's unused player-index parameter.

## Warcraft data and fallback rules

The active merged Misc cache already loads `UI\\MiscData.txt`, `UI\\MiscUI.txt`, and `war3mapMisc.txt`. `G_ResourceGainEvent()` reads:

```text
GoldTextColor / LumberTextColor
GoldTextLifetime / LumberTextLifetime
GoldTextFadeStart / LumberTextFadeStart
GoldTextHeight / LumberTextHeight
```

`*TextColor` is authored as `alpha,red,green,blue` and is converted to the engine's RGBA `COLOR32`. The label uses the active `MasterFont` skin entry, falling back to `Fonts\\FRIZQT__.TTF`.

When active data omits a field, the implementation has narrow stock-compatible fallbacks:

| Style | Colour (RGBA) | Lifetime | Fade start | Height |
|---|---:|---:|---:|---:|
| Gold | `255,220,0,255` | 2.0 s | 1.0 s | 0.024 UI |
| Lumber | `0,200,80,255` | 2.0 s | 1.0 s | 0.024 UI |

The font-size conversion mirrors Warsmash's built-in text-tag path: Warsmash renders at `TextHeight * 0.5` UI height, while OpenRealm fonts are registered in authored thousandths, so `0.024 * 500 = 12` pixels for stock data.

Warsmash parses `GoldTextVelocity`/`LumberTextVelocity`, but its built-in numeric `spawnTextTag()` currently constructs the tag with a hard-coded `(0, 60)` screen-pixel velocity. OpenRealm intentionally follows that current built-in behavior instead of claiming the parsed velocity is authoritative: labels rise at 60 px/s with no horizontal drift.

## Client lifecycle

`client/cl_tent.c` owns a bounded pool of transient floating labels. A label captures its world anchor at spawn; it is not attached to the worker afterward. Therefore a Peasant can immediately walk back to the mine while the number continues rising from the deposit location.

Every frame after the 3D world is rendered:

1. `SCR_ProjectWorldPoint()` projects the stored world anchor through the active view-projection matrix.
2. If the projected point is outside the world scissor, the label is not drawn that frame.
3. Screen-space travel is applied; positive vertical velocity moves upward.
4. Alpha remains at the authored value until fade start, then scales to zero over the remaining lifetime.
5. A small black shadow is drawn, then the resolved coloured label.
6. When lifetime expires, the pool slot is released.

Multiple gains are independent objects. Two workers depositing ten gold at the same time produce two `+10` labels rather than an aggregated `+20`.

## Implemented income hooks

The common commit helper is used at the existing high-confidence income sites:

- normal Gold Mine worker deposit (`s_goldmine.c`);
- normal lumber worker deposit (`s_harvest_lumber.c`);
- Call to Arms' immediate carried-resource return (`s_militia.c`);
- the currently implemented Wisp direct lumber credit;
- the currently implemented Blighted Gold Mine direct gold credit.

The event source is the entity that owns the current transaction. Normal worker deposits and militia conversion therefore originate at the worker. Current direct Wisp income originates at the Wisp; current Blighted Mine income originates at the mine.

## Deliberate non-goals / remaining parity gaps

This patch does **not** use floating text as a reason to redesign incomplete gameplay systems:

- Wisp harvesting currently credits once and consumes the Wisp; Warsmash's persistent periodic Wisp harvesting remains a separate gameplay gap.
- `Aegm` Entangled Mine is still only a marker in OpenRealm, so there is no correct income transaction to instrument yet.
- Gold/lumber bounty semantics and their distinct Warsmash `Bounty` / `LumberBounty` styles remain separate work.
- General JASS `texttag` natives (`CreateTextTag`, `SetTextTag*`, etc.) remain unimplemented. `TE_FLOATING_TEXT` is a generic one-shot presentation primitive, not a JASS handle/lifetime implementation.
- The resource label does not modify harvesting orders, carry state, camera, fog, selection, or HUD resource accounting.

## Regression coverage

`wc3_food.credited_gold_emits_net_resource_gain_world_text` checks the server-side contract without external Warcraft data. At 70% Gold Upkeep, gross income 10 must:

- credit exactly 7 gold;
- emit text `+7`, not `+10`;
- anchor ten world units above the source position;
- select the generic `TE_FLOATING_TEXT` event;
- use the fallback gold colour/timing/font size when Misc data is absent;
- encode 0/60 px/s built-in motion; and
- multicast exactly once.

Runtime visual validation should additionally exercise ordinary gold and lumber deposits in both ROC and TFT data modes and confirm independent rising/fading labels while workers immediately resume their normal harvesting loop.
