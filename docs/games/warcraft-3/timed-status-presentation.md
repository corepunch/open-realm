# Timed Status Presentation

## Contract

Warcraft III's selected-unit countdown bar is a generic presentation of specific timed statuses, not a Militia-specific widget and not a timer for every temporary buff. The simulation owns expiry; the HUD only renders the selected unit's remaining fraction.

OpenRealm currently opts these statuses into the timed bar:

| Buff | Producer | Expiry behavior |
| --- | --- | --- |
| `Bmil` | Human `Amil` Call to Arms transform | `S_MilitiaExpire()` restores Peasant form |
| `BTLF` | summoned-unit timed life | unit dies when the status expires |

Other timed statuses such as `Bstu` remain ordinary simulation statuses and do not claim the selected-unit countdown bar. Extend `unit_statusshowstimedbar()` only when the corresponding Warcraft/Warsmash buff is known to use the timed-life presentation.

The presentation matches Warsmash's important policy rules:

- one `SimpleProgressIndicator` slot, not one bar per buff;
- only a single selected unit can expose the bar;
- only the local player's own selected unit exposes the timer;
- the timer label comes from `AbilityBuffData.slk` / profile `Bufftip` for the qualifying buff;
- a timed-bar buff is not also emitted into the ordinary `Status:` icon strip;
- the bar drains from `1.0` to `0.0`; gameplay expiry never depends on the HUD value.

## Runtime State

`heroabilitystatus_t` in `games/warcraft-3/game/g_local.h` stores:

```text
code         buff/ability rawcode
level        active level; zero means unused slot
timestamp    authoritative expiry time in milliseconds; zero means persistent
duration_ms  original timed-status duration in milliseconds; zero means persistent
```

`unit_addtimedstatus()` sets both `timestamp` and `duration_ms`. `Refresh`, `Stack`, and `Replace` status updates refresh both values when a positive duration is supplied. Ability cooldown records also retain their duration, although cooldowns do not opt into the status-panel bar.

`unit_statusremainingfraction()` computes:

```text
max(0, timestamp - now) / duration_ms
```

clamped to `0..1`. `unit_findtimedbarstatus()` scans `abilstatus[]` in slot order and returns the last live qualifying record. This mirrors Warsmash's one-slot behavior, where a later qualifying buff population overwrites the same progress indicator.

The full `heroabilitystatus_t` array is already part of the raw `edict_t` save record, so `duration_ms` follows the existing save/load record automatically. Changing the struct changes `sizeof(edict_t)`; the save header already rejects incompatible layouts rather than attempting backwards compatibility.

## HUD Data Flow

```text
qualifying timed status on selected unit
        -> unit_findtimedbarstatus()
        -> remaining / original duration
        -> UI_PLAYERSTAT_SELECTION_TIMED_STATUS (0..USHRT_MAX)
        -> normal player-state snapshot delta
        -> UI_STAT_SELECTION_TIMED_STATUS
        -> SimpleProgressIndicator
```

`G_UpdateClientInfoPanels()` already runs once per server frame. `UpdateSelectedLiveStats()` uses that path to update HP, mana, and the normalized timed-status fraction without rebuilding the entire FDF layer every frame.

`common/shared.h` reserves the generic `UI_PLAYERSTAT_SELECTION_TIMED_STATUS` player-state slot and `UI_STAT_SELECTION_TIMED_STATUS` frame binding. `UI_STAT_SELECTION_TIMED_STATUS` is deliberately assigned reserved high-byte value `250`: `uiFrame_t.stat` is encoded as `NFT_BYTE`, while the established selection/context bindings occupy `251..255`. A compile-time assertion and UI-frame delta regression keep every special binding inside that byte-sized wire contract. `client/cl_layout.c` converts the transmitted `USHORT` value back to `0..1` for the ordinary `FT_SIMPLESTATUSBAR` renderer. These symbols are deliberately generic and contain no Warcraft race/unit/spell fiction because they live in shared/client code.

The infopanel initializes the retail `SimpleProgressIndicator` frame with Warsmash's presentation assets:

```text
bar texture     SimpleProgressBarConsole
border texture  SimpleProgressBarBorder
bar colour      RGBA 65,130,210,255
width           0.180 FDF units
```

The FDF continues to own the frame's authored anchor and height.

## Label And Buff Icons

When the selected unit has a qualifying status, `WriteSimpleUnitHeader()` resolves the status rawcode through `StatusBuffCode()` and `StatusBuffField(..., "Bufftip")`. That text temporarily owns `SimpleClassValue`, matching Warsmash's `timedLifeBar()` callback.

When no qualifying timer is active, the ordinary class behavior returns:

- Hero: localized `INFOPANEL_LEVEL_CLASS` text plus Hero XP bar;
- non-Hero: no synthetic class line.

`StatusBuffArt()` rejects every `unit_statusshowstimedbar()` status before the ordinary status-icon path. This prevents `Bmil`/`BTLF` from appearing both as an icon and as the countdown bar.

## Selection And Ownership

The timer is exposed only when:

```text
selection count == 1
selected unit owner == local player's player number
qualifying status exists and has not expired
```

Multi-selection uses `UI_WriteMultiselect()` and never serializes the single-unit progress indicator. Foreign/allied/enemy selections receive a zero timer player stat and no progress-indicator frame. `G_SetUnitPlayer()` invalidates the info panel so an ownership transfer cannot leave a stale timer label visible.

Status add/remove/expiry already calls `G_InvalidateUnitInfoPanel()`, so starting `Bmil` or `BTLF` shows the bar and expiry/removal hides it on the next infopanel refresh. Reselecting midway reads the existing simulation timestamp and does not restart the bar.

## Known Gap Versus Warsmash

Warsmash locally decrements its cached remaining duration every rendered frame between authoritative UI refreshes. OpenRealm currently updates the normalized timer through ordinary server/player-state snapshots instead. The simulation and displayed fraction remain authoritative and correct, but very low snapshot rates can make the visual drain step rather than interpolate perfectly smoothly.

Do not move expiration to the client to fix that. A future interpolation pass should transmit enough timing information to predict presentation only, then reconcile to the server value.

Avatar and other Warsmash buffs that opt into `isTimedLifeBar()` are not automatically classified here because their corresponding OpenRealm gameplay paths are not yet implemented/verified. Add them alongside their gameplay implementation rather than guessing from duration alone.

## Verification

Focused tests cover:

- `Bmil` / `BTLF` eligibility and ordinary `Bstu` exclusion;
- original duration retention and half-duration fraction calculation;
- deterministic last-qualifying-status selection;
- own-unit-only player-state publication;
- normalized player-state client binding;
- player-state delta survival;
- save/load persistence of `duration_ms`.

Suggested focused commands for the developer test pass:

```sh
make test-wc3-engine WC3_PATTERN='wc3_combat.timed_status_bar*'
make test-wc3-engine WC3_PATTERN='wc3_game.hud_timed_status*'
make test-wc3-engine WC3_PATTERN='wc3_save.round_trip_edict_and_player_state'
make test
```

The standalone client-layout/network assertion runs as part of the repository-wide `make test`, which remains the final regression check.

## See Also

- [Call to Arms and Militia](call-to-arms-and-militia.md)
- [Economy and Unit Presentation](economy-and-unit-presentation.md)
- [Save/Load](save-load.md)
