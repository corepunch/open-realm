# Warcraft III JASS Groups

## Ownership and lifecycle

`CreateGroup` returns a light handle to a heap-allocated `ggroup_t`. `level.groups` is a growable table of pointers to those objects; `level.num_groups` is the high-water handle ordinal and `level.group_capacity` is only pointer-table capacity. Growing the table never moves an existing `ggroup_t`, so live JASS handle addresses remain stable.

`DestroyGroup` releases the logical slot by clearing its `ggroup_t`; `G_AllocJassGroup()` always searches inactive slots before appending another handle ordinal. `GroupClear` is deliberately different: it removes members but keeps the group handle live. Map replacement and game shutdown free the pointer table plus every allocated group object after the JASS VM is closed.

All implemented group natives validate handles with `G_JassGroupValid()`. A destroyed group therefore behaves as invalid until its slot is reused. As with Warcraft native handles generally, scripts must not retain and use a handle after calling `DestroyGroup`.

## Why the registry is dynamic

The original OpenRealm registry used `level.groups[MAX_GROUPS]` with `MAX_GROUPS == 1024`. Reusing `DestroyGroup` holes fixed ordinary temporary-group churn, but real Blizzard campaign JASS can intentionally or accidentally retain more than 1024 group handles.

Prologue02 provides a concrete compatibility case. `Trig_Shaman_Acquired_Conditions` runs every 0.50 seconds and contains:

```jass
CountUnitsInGroup(GetUnitsOfPlayerAndTypeId(udg_AP1_Player, 'oshm')) > 0
```

The authored function does not set `bj_wantDestroyGroup` and does not call `DestroyGroup`, so each false poll leaves the group returned by `GetUnitsOfPlayerAndTypeId` live. `Trig_Subgroup_Event` independently creates another group on its 1.00-second poll path. Runtime diagnostics showed roughly 502 live groups from each trigger before the old 1024-slot registry failed.

Do not special-case these map functions, auto-destroy arbitrary `CountUnitsInGroup` arguments, or merely raise a fixed `MAX_GROUPS`. The compatible model is a growable handle registry that still recycles explicitly destroyed slots. Warsmash follows the same broad model: its `CreateGroup` native allocates new `UnitGroup` objects with increasing handle IDs rather than enforcing a 1024-group table.

Each `ggroup_t` embeds up to `MAX_GROUP_SIZE` entity pointers, so group objects are allocated only as handle ordinals are actually exposed. The growable pointer table itself starts at `JASS_GROUP_INITIAL_CAPACITY` and doubles as needed. This avoids reserving many megabytes merely to raise a static limit.

## Leak diagnostics

Set `wc3_group_debug 1` to retain non-persistent ownership diagnostics for live JASS groups. Diagnostics are allocated in a parallel growable side table only when debugging is enabled; normal gameplay does not pay that per-slot metadata cost.

The side table records the immediate creator, active JASS call chain, and trigger ordinal. Pointer-table expansion emits `WC3_GROUP_DEBUG grow old_capacity=... new_capacity=... highwater=...`, which is the expected confirmation when a map legitimately passes the former 1024-handle boundary. `DestroyGroup` records a matching free and clears that slot's metadata. If a real allocation failure occurs, OpenRealm emits one `WC3_GROUP_DEBUG allocation-failed` summary followed by grouped `WC3_GROUP_DEBUG chain ...` lines with live, allocated, freed, outstanding, trigger, and call-path counts. There is no longer a synthetic `registry is full` failure at 1024 groups.


## Save/load

JASS `group` handles serialize as stable ordinal IDs in the dynamic pointer table. The save stream writes the complete prefix through `level.num_groups`; each record persists `inuse`, `num_units`, and entity-index membership. Destroyed holes remain present so later live handle ordinals do not shift.

Load first ensures that the pointer table contains the saved number of stable group objects, then restores every record before the JASS VM snapshot resolves group handle IDs. `G_SaveJassHandle()` and `G_LoadJassHandle()` use `G_JassGroupIndex()` / `G_JassGroupByIndex()` rather than pointer arithmetic because the group objects are no longer contiguous.

Save format version 13 introduced the group registry outside the inline `level_fields` schema and writes it explicitly. The current format is version 32; versions 14–17 added research-event callback context, natural-creep sleep, environmental distance-fog, and persistent WC3 unit vertex-colour state. Later format changes are documented in [Save/Load](save-load.md), including the mutable Blight state added by version 32. Older saves are intentionally rejected by the exact-version guard. Runtime creation has no 1024-style handle ceiling; the save reader/writer separately rejects more than 65,536 group ordinals as a corrupt/oversized-save safety bound.

## Verification

Regression coverage should include:

- repeated allocate/destroy cycles thousands of times without growing beyond one reusable slot;
- more than 1024 simultaneously live groups, including stable handle IDs above the former limit;
- JASS `CreateGroup` -> `DestroyGroup` returning the slot to the allocator;
- `GroupClear` keeping a live handle;
- save/load retaining live group identity and membership while rejecting destroyed handles.

### Prologue02 subgroup diagnostics

With `+set wc3_quest_debug 1`, trigger ordinals 208-213 emit `WC3_SUBGROUP` lines for definitions, registrations, enable/disable transitions, direct evaluation/execution, and dispatcher matches/results. Use these together with `wc3_group_debug 1` when diagnosing future campaign script behavior.
