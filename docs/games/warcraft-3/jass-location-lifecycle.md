# JASS location lifecycle

`RemoveLocation` (`game/api/api_math.h`) invalidates the VM-owned location
reference shared by all JASS copies of that handle. `jdo.c` rejects access
through `jass_checkhandle`, so `GetLocationX/Y/Z` return their existing null
fallback and `MoveLocation` does nothing. Removing a null/already removed
location is harmless. Memory stays VM-owned until its last reference retires;
no alias can accidentally dereference a freed payload.

`jstate.h` carries an explicit `invalid` bit instead of repurposing `id = 0`.
This is important because value-handle types such as `race` use zero IDs and
must remain valid. JASS snapshot writing encodes invalidated handles using
the existing null encoding; snapshots never serialize a zero-ID owned payload.
Existing, valid owned handles still use ID-based alias deduplication. Restored
removed globals become null, so they cannot be used after load.

Tests: `wc3_api.remove_location_invalidates_aliases` and
`wc3_api.removed_location_global_survives_save_as_null` exercise native dispatch,
aliases, idempotence, null, and a full game save/load round-trip. Changes to
location serialization should update these tests and save/load documentation.
