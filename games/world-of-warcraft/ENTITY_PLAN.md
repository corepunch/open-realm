# WoW Entity Spawn Architecture Plan

**Reference (client):** WoWee's `EntitySpawner` + `EntityController` (Kelsidavis/WoWee)
**Reference (server):** AzerothCore's `GridObjectLoader` + `ObjectMgr` (azerothcore/azerothcore-wotlk)
**Context:** OpenWarcraft3 is standalone — no network server. Entities are locally simulated.

---

## Current State (4 types)

| Type | Spawning | Data Source |
|------|----------|-------------|
| `WOW_ENTITY_PLAYER` | `Wow_InitPlayer()` — edict #0 | DBC + cvars |
| `WOW_ENTITY_CREATURE` | `Wow_SpawnAmbientCreatures()` — 64 spiral-placed | 4 hardcoded display_ids |
| `WOW_ENTITY_PROJECTILE` | `Wow_Spawn()` from ability code | Casting system |
| *(none)* | — | — |

**Missing:** GAMEOBJECT, CORPSE, DYNAMICOBJECT, ITEM, CONTAINER

### How WoW servers load entities (AzerothCore)

AzerothCore's `GridObjectLoader::LoadAllCellsInGrid()` is the authoritative server-side spawn path:

1. **Data source:** MySQL database tables `creature` + `gameobject` (one row per spawn instance)
2. **Spatial indexing:** Maps are divided into 64×64 grid cells. `sObjectMgr->GetGridObjectGuids(mapId, spawnMode, gridId)` returns the list of creature and gameobject GUIDs for a given grid cell.
3. **Per-spawn data:** `CreatureData` struct has id (template entry), position, orientation, spawntime, wander_distance, movement type, per-spawn override for health/mana/flags. `GameObjectData` has id (template entry), position, rotation (quaternion), state, animprogress.
4. **Templates:** Per-spawn instances reference `CreatureTemplate` (from `creature_template` table) or `GameObjectTemplate` (from `gameobject_template` table) via the `id` field. Templates define name, model display_ids, faction, level, NPC flags, loot, spells, etc.
5. **Grid loading:** When a player enters a grid cell, `LoadCreatures(guid_set, map)` and `LoadGameObjects(guid_set, map)` create full `Creature`/`GameObject` world objects. Each object calls `LoadFromDB(guid, map)` which joins the spawn row with the template row.
6. **Unloading:** When a grid cell has no players, `GridObjectUnloader` deletes all objects in that cell. Dynamic objects and corpses are also tracked per-grid.

For our standalone engine, we replace the database with:
- **Creatures:** Placed procedurally (spiral around player) + later from DBC-driven spawn data
- **GameObjects:** Extracted from ADT `MDDF`/`MODF` chunks (already parsed by renderer) + mapped to template entries via `GameObjectDisplayInfo.dbc`

---

## Target State (8 types → WoWee parity)

| Type | Spawning | Data Source |
|------|----------|-------------|
| `WOW_ENTITY_PLAYER` | `Wow_InitPlayer()` — edict #0 | DBC + cvars |
| `WOW_ENTITY_UNIT` | `Wow_SpawnAmbientCreatures()` + future DBC table | CreatureDisplayInfo.dbc |
| `WOW_ENTITY_GAMEOBJECT` | **NEW** — from ADT MDDF/MODF doodads marked interactive | ADT chunks + DBC |
| `WOW_ENTITY_CORPSE` | **NEW** — `Wow_AIDie()` spawns corpse entity | Death location |
| `WOW_ENTITY_PROJECTILE` | `Wow_Spawn()` from ability code | Casting system |
| `WOW_ENTITY_DYNAMICOBJECT` | **NEW** — AoE spell effects | Spell data |
| `WOW_ENTITY_ITEM` | **NEW** — dropped loot (deferred) | Loot tables |
| `WOW_ENTITY_CONTAINER` | **NEW** — lootable chests (deferred) | GameObject type=3 |

---

## Phase 1: Struct & Enum Expansion

### 1.1 Expand `wowEntityKind_t` → 8 types

```c
typedef enum {
    WOW_ENTITY_NONE,
    WOW_ENTITY_PLAYER,
    WOW_ENTITY_UNIT,          // renamed from CREATURE (WoWee uses UNIT)
    WOW_ENTITY_GAMEOBJECT,    // NEW
    WOW_ENTITY_CORPSE,        // NEW
    WOW_ENTITY_PROJECTILE,
    WOW_ENTITY_DYNAMICOBJECT, // NEW
    WOW_ENTITY_ITEM,          // NEW (placeholder)
    WOW_ENTITY_CONTAINER,     // NEW (placeholder)
} wowEntityKind_t;
```

### 1.2 Expand `wowEntityLocal_t` with union fields per `kind`

Keep the existing fields for UNIT/PROJECTILE. Add:

```c
/* GameObject fields (kind == WOW_ENTITY_GAMEOBJECT) */
DWORD go_entry;        // GameObject template entry
DWORD go_type;         // GAMEOBJECT_TYPE: 0=door, 3=chest, 5=chair, 8=spell_focus, etc.
DWORD go_state;        // 0=ready/closed, 1=active/open, 2=destroyed
BOOL  go_interactive;  // selectable by player (door, chest, chair vs rock, tree)
DWORD go_loot_state;   // 0=not lootable, 1=lootable, 2=looted

/* Corpse fields (kind == WOW_ENTITY_CORPSE) */
DWORD corpse_owner;    // entity number of dead player/creature
DWORD corpse_timer;    // ms until corpse decays

/* DynamicObject fields (kind == WOW_ENTITY_DYNAMICOBJECT) */
DWORD dyn_spell_id;    // spell that created this
DWORD dyn_caster;      // entity number of caster
DWORD dyn_radius;      // effect radius
DWORD dyn_duration;    // ms remaining
```

### 1.3 Add `FlatFieldMap`-style field store

WoWee stores entity fields in a `FlatFieldMap` (sorted `{uint16_t index, uint32_t value}` pairs).  
We keep `wowEntityLocal_t` as a flat struct for performance (no heap allocations).  
The `kind` field determines which union members are active.

---

## Phase 2: Type-Handler Dispatch Pattern

### 2.1 Define handler table

```c
typedef void (*wowEntityCreateFn)(LPEDICT ent, struct wowEntityCreateParams *params);
typedef void (*wowEntityThinkFn)(LPEDICT ent);
typedef void (*wowEntityDieFn)(LPEDICT ent, LPEDICT attacker);

typedef struct {
    wowEntityCreateFn create;
    wowEntityThinkFn  think;
    wowEntityDieFn    die;
} wowEntityHandler_t;

static wowEntityHandler_t wow_entity_handlers[WOW_ENTITY_COUNT];
```

Each `kind` registers its handler at init. `Wow_RunFrame` dispatches via `kind` instead of `if` chains.

### 2.2 Per-kind handlers

| Kind | create | think | die |
|------|--------|-------|-----|
| PLAYER | `Wow_InitPlayer` | `Wow_RunPlayerFrame` | *(never)* |
| UNIT | `Wow_MonsterStart` | `Wow_AIRunFrame` | `Wow_AIDie` |
| GAMEOBJECT | `Wow_GameObjectCreate` | `Wow_GameObjectThink` | `Wow_GameObjectDie` |
| CORPSE | `Wow_CorpseCreate` | `Wow_CorpseThink` | `Wow_CorpseRemove` |
| PROJECTILE | `Wow_ProjectileCreate` | `Wow_RunProjectile` | `Wow_ProjectileDie` |
| DYNAMICOBJECT | `Wow_DynamicObjectCreate` | `Wow_DynamicObjectThink` | `Wow_DynamicObjectDie` |

---

## Phase 3: GameObject Spawning

### 3.1 Where game objects come from

The renderer already parses ADT `MDDF` (M2 doodads) and `MODF` (WMO placements) chunks.  
These are stored as `wowDoodadDef_t` / `wowMapObjDef_t` and rendered as static geometry.

**Approach:** After map load, iterate the doodad table. For each doodad that is *interactive*  
(has a DBC GameObject display entry, or is a known interactive type), spawn a  
`WOW_ENTITY_GAMEOBJECT` edict.

### 3.2 Interactive detection

GameObjects are identified by their model path. The path maps to a `GameObjectDisplayInfo.dbc`  
entry, which links to a `GameObject` template entry. The template's `type` field determines  
behavior:

| Type | Name | Interactive? |
|------|------|-------------|
| 0 | DOOR | Yes — open/close |
| 1 | BUTTON | Yes — press |
| 2 | QUESTGIVER | Yes — quest accept |
| 3 | CHEST | Yes — loot |
| 5 | CHAIR | Yes — sit |
| 8 | SPELL_FOCUS | Maybe — ritual circles |
| 10 | GOOBER | Yes — generic interact |
| 19 | MAILBOX | Yes — mail |
| 22 | FLAGSTAND | Yes — capture |
| 25 | FISHINGHOLE | Yes — fish |

Non-interactive types (doodads, trees, rocks, grass) remain renderer-only.

### 3.3 Implementation

```c
void Wow_SpawnGameObjects(void) {
    // Iterate wow_world.doodads linked list (already parsed by renderer)
    // For each doodad:
    //   - Look up model path → GameObjectDisplayInfo.dbc → display_id
    //   - If no DBC match, skip (static doodad)
    //   - Look up display_id → GameObject entry (from a pre-cached table)
    //   - If type is interactive, spawn WOW_ENTITY_GAMEOBJECT edict
    //   - Set go_entry, go_type, go_interactive
    //   - Place at doodad's world position
}
```

**Data needed:** `GameObjectDisplayInfo.dbc` mapping model paths to display_ids.  
If this DBC is unavailable, we hardcode common interactive model paths (doors, chests, chairs).

---

## Phase 4: Corpse Spawning

### 4.1 When a unit dies

`Wow_AIDie()` currently sets `local->dead = true` and plays death animation.  
Add: spawn a `WOW_ENTITY_CORPSE` edict at the death location, linked to the dead unit.

```c
void Wow_AIDie(LPEDICT ent, LPEDICT attacker) {
    // ... existing death logic ...
    Wow_SpawnCorpse(ent);  // NEW
}

LPEDICT Wow_SpawnCorpse(LPEDICT dead_entity) {
    LPEDICT corpse = Wow_Spawn();
    if (!corpse) return NULL;
    wowEntityLocal_t *local = Wow_EntityLocal(corpse);
    local->kind = WOW_ENTITY_CORPSE;
    local->corpse_owner = Wow_EntityIndex(dead_entity);
    local->corpse_timer = 300000; // 5 minutes
    corpse->s.origin = dead_entity->s.origin;
    corpse->s.model = dead_entity->s.model; // same model, death pose
    // ... set flags, bounds ...
    return corpse;
}
```

### 4.2 Corpse think

Counts down `corpse_timer`. At 0, removes entity.  
After looted: changes state to `looted` (bones-only model).

---

## Phase 5: DynamicObject Spawning

### 5.1 Spell AoE zones

When a spell with a persistent area effect is cast (Blizzard, Consecration, etc.),  
spawn a `WOW_ENTITY_DYNAMICOBJECT` to mark the zone.

For now: spawn a placeholder for fireball/frostbolt impact (replaces temp_entity pattern).  
Full AoE spell support (ring visuals, ground textures) comes later.

```c
LPEDICT Wow_SpawnDynamicObject(DWORD spell_id, LPEDICT caster, VECTOR2 origin, DWORD radius, DWORD duration) {
    LPEDICT dobj = Wow_Spawn();
    // ...
    local->kind = WOW_ENTITY_DYNAMICOBJECT;
    local->dyn_spell_id = spell_id;
    local->dyn_caster = Wow_EntityIndex(caster);
    local->dyn_radius = radius;
    local->dyn_duration = duration;
    // Use spell's visual model or a generic target circle
    return dobj;
}
```

---

## Phase 6: Per-Frame Spawn Budget

WoWee limits spawn processing to 2ms/frame to avoid frame stalls.  
We adopt the same for `Wow_SpawnEntities` and `Wow_SpawnGameObjects`:

```c
#define WOW_MAX_SPAWNS_PER_FRAME 32

static DWORD wow_spawns_this_frame = 0;

LPEDICT Wow_Spawn(void) {
    if (wow_spawns_this_frame >= WOW_MAX_SPAWNS_PER_FRAME) {
        // Defer remaining spawns to next frame
        wow_spawns_deferred = true;
        return NULL;
    }
    wow_spawns_this_frame++;
    // ... existing allocation logic ...
}

void Wow_RunFrame(void) {
    wow_spawns_this_frame = 0;
    // ... process deferred spawns ...
}
```

This is simpler than WoWee's async approach and sufficient for our standalone model.

---

## Phase 7: Name/Info Caching

WoWee caches names from server query responses. We cache from DBC at init time:

```c
typedef struct {
    DWORD display_id;
    char  name[64];
    char  subname[64];
    DWORD type;     // CreatureType
    DWORD family;
    DWORD rank;
} wowCreatureInfo_t;

static wowCreatureInfo_t wow_creature_info_cache[256];
static DWORD wow_creature_info_count = 0;

void Wow_CacheCreatureInfos(void) {
    // Parse CreatureDisplayInfo.dbc → CreatureModelData.dbc
    // Store name/type/family/rank per display_id
}
```

---

## Implementation Order

1. **Expand enum + struct** (`g_wow_local.h`)
2. **Add type-handler table** (`g_wow.c`, `g_wow_local.h`)
3. **Implement Corpse spawning** (`g_wow.c`)
4. **Implement GameObject spawning** (`g_wow_gameobject.c`, new file)
5. **Implement DynamicObject** (`g_wow.c`)
6. **Per-frame spawn budget** (`g_wow.c`)
7. **Name/info caching** (`m_creature.c`)
8. **Tests** for each new type
