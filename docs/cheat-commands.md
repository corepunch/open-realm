# Cheat commands

The engine exposes `sv_cheats`, defaulting to `0`. Game commands are server-authoritative and are rejected until cheats are enabled:

```
set sv_cheats 1
give ...
```

This follows the Quake 2 model: `give` is a game/server command, while `+give` is a late command-line command that runs after the map command when both are supplied. For example:

```
openwarcraft3 +set sv_cheats 1 +map Azeroth +give all
```

The Quake 2 reference implementation supports `give all`, health, weapons, ammo, armor and power items, plus `god`, `notarget`, and `noclip`; it also refuses these commands in deathmatch unless `sv_cheats` is enabled. See the [Quake 2 g_cmds.c source](https://unix.superglobalmegacorp.com/cgi-bin/cvsweb.cgi/quake2/game/g_cmds.c?cvsroot=quake2%3Bf%3Dh%3Bonly_with_tag%3DiD%3Bcontent-type%3Dtext%2Fx-cvsweb-markup%3Bln%3D1%3Brev%3D1.1.1.1).

## Warcraft III

For map-start resource testing, `wc3_cheat_starting_resources` is a session-only CVar. Set it before loading the map:

```
openwarcraft3 +set wc3_cheat_starting_resources 1 +map "Maps/Campaign/Human02.w3m"
```

When enabled, the map load arms a one-shot +5000 gold / +5000 lumber grant for each used human-controlled slot. The grant is not tied directly to `war3map.j main()`: OpenRealm waits until that human client is connected, user control is enabled, and the ordinary gameplay UI has returned from any intro cinematic, then adds the bonus to the resource values authored by the map at that boundary. Computer and neutral players are unchanged, values clamp to the player-state storage limit, and each human client can receive the grant only once per fresh map load. Save-game restoration preserves the saved resource totals and disables any still-pending map-start grant so loading cannot award another +5000. The CVar is sampled when the map is spawned; changing it mid-map does not grant resources until the next map load/restart. Because this is a pre-map CVar rather than a client cheat command, it does not require `sv_cheats`. The default is `0`.

The playable-state boundary is necessary for campaign maps. A Human02 runtime trace showed the generated initialization setting the human slot to `0` gold / `0` lumber, followed by a later `EVENT_PLAYER_END_CINEMATIC` path that authored the real starting values as `300` gold / `50` lumber. Applying the cheat immediately after `main()` therefore produced `5000/5000` only for the campaign trigger to overwrite it with `300/50`. Waiting until the cinematic returns control makes the same map resolve to `5300/5050`. Melee maps without an intro cinematic receive the grant on their first playable server frame after initialization.

Diagnostics use the `WC3_CHEAT_RESOURCES` prefix. With the CVar enabled, relevant lines are intentionally sparse: `armed` records that the map latched the cheat, `defer` records a connected human still held by cinematic/input state, human `SetPlayerState` resource writes expose map-authored late values, and `apply` records the final before/after totals. For Human02 the expected useful tail is conceptually:

```text
WC3_CHEAT_RESOURCES defer ... gold=0 lumber=0
WC3_CHEAT_RESOURCES SetPlayerState ... state=1 ... requested=300
WC3_CHEAT_RESOURCES SetPlayerState ... state=2 ... requested=50
WC3_CHEAT_RESOURCES apply ... gold=300->5300 lumber=50->5050
```

Resource commands target the issuing player and are additive, so they work even when no unit is selected:

```
give gold <amount>
give lumber <amount>
give res <amount>       # add the amount to both gold and lumber
```

Amounts are non-negative integers and clamp at the player-state resource storage limit rather than wrapping. These commands require `sv_cheats 1`; they are independent of `wc3_cheat_starting_resources`, which remains useful when a reproducible bonus should be applied automatically at map start.

Terminal result cheats also target the issuing player and do not require a selected unit:

```
win
lose
```

Both require `sv_cheats 1`. They intentionally use the same authoritative player-removal/result transition as JASS `RemovePlayer`: `win` records `PLAYER_GAME_RESULT_VICTORY` and publishes `EVENT_PLAYER_VICTORY`, while `lose` records `PLAYER_GAME_RESULT_DEFEAT` and publishes `EVENT_PLAYER_DEFEAT`. Existing map result triggers, ending cinematics, pause-aware result-event draining, and the normal campaign/result dialog continuation therefore remain in control; the cheats do not directly switch maps or force a UI screen. Repeating either command after the player has already been removed is a no-op, matching `RemovePlayer` idempotence.

Time-of-day phase cheats set the authoritative Warcraft clock directly and require `sv_cheats 1`:

```
day
night
```

The commands use the active map's authored `Dawn`, `Dusk`, and `DayHours` values and choose the midpoint of the requested phase. Stock Warcraft values therefore produce 12:00 for `day` and 00:00 for `night`, while maps that override their day/night thresholds still land inside their own authored phase. The write goes through `G_SetTimeOfDay()`, so HUD clock state, DNC lighting, sight/regeneration rules, and time-of-day trigger conditions consume the same authoritative change. Explicit time sets still apply while ordinary time-of-day progression is suspended.

For accelerated environment testing, `wc3_cheat_timeofday_scale` multiplies only ordinary Warcraft III day/night progression. `1` is normal speed; for example `set wc3_cheat_timeofday_scale 20` runs the clock at 20x while simulation movement, AI, timers, trigger sleeps, and cinematics retain normal timing. `SuspendTimeOfDay(true)` still freezes ordinary progression, explicit JASS/cheat time sets remain exact, and invalid/non-positive values fall back to 1x. This CVar does not require `sv_cheats`.

Unit-oriented commands target the first selected unit:

```
give item <rawcode>
give ability <rawcode>
give xp <amount>       # hero only
god                    # toggle selected player unit invulnerability
kill                   # kill the player unit
```

`give item` uses the normal item spawn and pickup path, so inventory capacity and passive item effects remain authoritative. `research <rawcode>` remains available for the existing non-cheat research/debug path.

## World of Warcraft

Commands target the player:

```
give all
give health [amount]
give mana [amount]
give gold <amount>      # copper
give xp <amount>
god                     # toggle player invulnerability
kill
```

WoW’s current prototype action bar is class-authored rather than a learned-spell container, so `give spell` and item creation by database entry are intentionally not claimed yet. They should be added only when the server has a real spell-known/item-entry model to mutate.

## Console ownership

The client owns the console/menu input destination. A `CLIENT_UI_GAME` player-state update must not overwrite `key_console` or `key_menu`; otherwise the console opens for one frame and is immediately hidden by the next server snapshot.
