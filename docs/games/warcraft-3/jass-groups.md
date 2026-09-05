# Warcraft III JASS Groups

## Ownership and lifecycle

`CreateGroup` returns a light handle to one slot in `level.groups[MAX_GROUPS]`. `level.num_groups` is the high-water mark for slots that have ever been exposed during the current map; it is not the number of currently live groups. Each `ggroup_t` carries `inuse`, so a destroyed slot can be reused without moving any other live group pointer.

`DestroyGroup` must release the slot, not merely clear its membership. `G_FreeJassGroup()` clears the whole slot and `G_AllocJassGroup()` searches reusable inactive slots before extending the high-water mark. `GroupClear` is deliberately different: it removes members but keeps the group handle live.

All implemented group natives validate the handle with `G_JassGroupValid()`. A destroyed group therefore behaves as an invalid handle until that slot is reused. As with Warcraft native handles generally, scripts must not retain and use a handle after calling `DestroyGroup`.

## Campaign exhaustion failure

A long-running campaign exposed the lifecycle bug in Prologue01: the old `DestroyGroup` only set `num_units = 0`, while every `CreateGroup` permanently incremented `level.num_groups`. After 1024 creations, `CreateGroup` raised `CreateGroup: group registry is full`. `jass_rterror()` aborts the current coroutine, so this was not harmless logging: any remaining trigger actions in that thread were skipped.

Do not fix this by only increasing `MAX_GROUPS`. Blizzard helpers routinely create and destroy temporary groups; destroyed slots must be recyclable.

## Save/load

The save format persists the full group slot prefix through `level.num_groups`, including each slot's `inuse` flag and membership list. JASS `group` handles serialize as stable slot indexes only while the slot is live. Load resolves an index only when that restored slot is `inuse`. This preserves pointer identity for live groups while allowing destroyed holes in the registry.

Changing the serialized group record to include lifecycle state required WC3 save format version 11. Version 10 saves are intentionally rejected.

## Verification

Regression coverage should include:

- repeated allocate/destroy cycles well past `MAX_GROUPS` without growing the high-water mark beyond one reusable slot;
- JASS `CreateGroup` -> `DestroyGroup` returning the slot to the allocator;
- `GroupClear` keeping a live handle;
- save/load retaining live group identity and rejecting destroyed handles.
