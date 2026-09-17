# Runtime Unit Spawn Lifecycle

## Contract

`SP_SpawnAtLocation()` is the presentation-aware runtime spawn path. It binds
unit data, initializes the edict, installs the normal unit callbacks, and calls
`birth`, which selects the birth animation and stores the authored build time in
`edict.wait`.

`SP_SpawnAtLocationNoBirth()` performs the same initialization without that
presentation lifecycle. It is appropriate when the caller owns the resulting
state, such as restoring a gold-mine overlay.

The runtime spawn path links the edict only after `SP_CallSpawn()` completes.
`SP_SpawnUnit()` derives a building's collision radius from its authored data;
linking first leaves the server broad-phase bounds at zero and lets movers walk
through the building even though its entity state reports collision.

JASS `CreateUnit` is an immediate creation operation. `unit_create()` therefore
uses the no-birth path, enters `stand` once, applies the requested facing, and
activates food. It must not call `SP_SpawnAtLocation()` followed by `stand()`:
`stand()` changes the animation but does not clear the birth wait, leaving a
ready unit with a stale build-time delay.

Construction, training, and summons use `SP_SpawnAtLocation()` directly because
their owning systems may consume the birth presentation or replace it with a
construction/hidden lifecycle afterward.

## Verification

The regression is covered by
`wc3_api.createunit_starts_ready_without_birth_delay` in
`games/warcraft-3/game/tests/t_api.c`. It installs a minimal UnitUI fixture,
creates a real runtime unit through `unit_create()`, and checks that the unit
is in `stand` with `wait == 0`.

`wc3_api.createunit_links_building_collision_bounds` covers the corresponding
server-link contract with a synthetic building row and verifies that its
collision-sized bounds are visible to `BoxEdicts()`.

Run both Warcraft III data modes with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.createunit_starts_ready_without_birth_delay'
```

See [Human07 Mission Troubleshooting](human07-troubleshooting.md) for the complete
mission investigation, deferred `RemoveUnit` semantics, `SuicidePlayer`, and the
confirmed building-link regression.
