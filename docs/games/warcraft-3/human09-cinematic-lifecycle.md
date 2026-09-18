# Warcraft III Human09 Cinematic Lifecycle

## Contract

Human09's generated JASS script owns the cinematic transitions. OpenRealm must
preserve the server-side state changes made by those actions and synchronize
the resulting presentation state to the client.

The relevant production paths are:

- `games/warcraft-3/game/api/api_player.h`: JASS selection changes;
- `games/warcraft-3/game/g_commands.c`: deferred client selection packets;
- `games/warcraft-3/game/g_events.c`: movement-driven region entry;
- `games/warcraft-3/game/g_gamecache.c`: campaign Hero restoration;
- `games/warcraft-3/game/api/api_item.h`: item recreation from empty inventory slots.

## Confirmed Failure Evidence

The Human09 Frostmourne ending reaches `Trig_FrostmourneCinematicEnd_Actions`
and then calls `CreateItemLoc` for each of Muradin's six inventory slots. Empty
slots produce `GetItemTypeId(null) == 0`. Before the fix, the first
`CreateItem(0, ...)` entered the generic entity spawner and the process exited
without returning from that native.

`CreateItem` now rejects both item ID `0` and nonzero IDs without an authored
`ItemData.file`, logs the invalid request as an error, and returns a null item
handle. This lets the JASS cleanup continue for empty inventory slots.

## Selection and Region State

JASS selection is stored as one bit per player on each entity. `ClearSelection`
and `SelectUnit(unit, false)` must clear only the active player's bit; using
`selected &= player_bit` incorrectly discarded all other bits. Scripted changes
mark the affected client dirty, and `G_UpdateClientSelections` emits one
`svc_set_selection` after the simulation frame rather than one packet per unit.

Movement-driven region entry uses the unit's previous and final simulation
positions. The regression test issues a real movement order, advances the
entity simulation, dispatches queued events, and verifies that
`GetEnteringUnit()` is the moving unit.

## Campaign Hero Restoration

`RestoreUnit` creates a fresh entity. If a cached Hero has zero or negative
health, restoring that value would create an immediately dead Hero even though
the map expects a usable carried-over Hero. OpenRealm restores such Heroes at
25% of their cached maximum health; living Heroes retain their exact cached
health.

## Verification

The automated coverage is in `games/warcraft-3/game/tests/t_api.c`:

- `jass_selection_masks_and_sync_are_deferred` covers per-player masks and the
  deferred client packet;
- `movement_crossing_region_publishes_entering_unit` covers an actual movement
  order crossing a region;
- `gamecache_restore_dead_hero_at_quarter_health` covers dead Hero restoration;
- `create_item_rejects_empty_item_id` covers empty and unresolved item IDs.

Run the bounded focused test with:

```sh
make test-assets openwarcraft3-tests
./build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_api.*'
```

Run the complete required verification with:

```sh
make test
```
