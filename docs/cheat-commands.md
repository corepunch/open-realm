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

Commands target the first selected unit:

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
