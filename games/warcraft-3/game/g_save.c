#include "g_local.h"

typedef enum {
    F_INT,
    F_FLOAT,
    F_LSTRING,            // string on disk, pointer in memory, TAG_LEVEL
    F_GSTRING,            // string on disk, pointer in memory, TAG_GAME
    F_VECTOR,
    F_REGION,
    F_ANGLEHACK,
    F_EDICT,            // index on disk, pointer in memory
    F_ITEM,                // index on disk, pointer in memory
    F_TRIGGER,          // index on disk, pointer in memory
    F_TIMER,            // index on disk, pointer in memory
    F_EVENT,            // index on disk, pointer in memory
    F_FUNCTION,            // JASS function name; timers/triggers
    F_FUNCTION_LIST,
    F_CFUNCTION,           // C callback roster index; edict think/stand/die
    F_MMOVE,
    F_STRUCT,
    F_STRUCT_RING,
    F_IGNORE
} fieldtype_t;

typedef struct {
    LPCSTR name;
    DWORD ofs;
    fieldtype_t type;
    size_t size;
    DWORD array_size;
    uintptr_t flags; /* field flags, or child schema pointer for F_STRUCT */
    DWORD count_ofs;
} field_t;

typedef struct {
    field_t const *fields;
    DWORD read_ofs, write_ofs;
} fieldRing_t;

#define F_METADATA(kind, ...) F_METADATA_INNER(kind, __VA_ARGS__)
#define F_METADATA_INNER(kind, ...) F_METADATA_##kind(__VA_ARGS__)
#define F_METADATA_F_STRUCT(count, schema) count, (uintptr_t)(schema)
#define F_METADATA_F_IGNORE(count, flags) count, flags
#define F_METADATA_F_INT(...) 0, 0
#define F_METADATA_F_FLOAT(...) 0, 0
#define F_METADATA_F_LSTRING(...) 0, 0
#define F_METADATA_F_GSTRING(...) 0, 0
#define F_METADATA_F_VECTOR(...) 0, 0
#define F_METADATA_F_REGION(...) 0, 0
#define F_METADATA_F_ANGLEHACK(...) 0, 0
#define F_METADATA_F_EDICT(count, flags) count, flags
#define F_METADATA_F_ITEM(count, flags) count, flags
#define F_METADATA_F_TRIGGER(count, flags) count, flags
#define F_METADATA_F_TIMER(count, flags) count, flags
#define F_METADATA_F_EVENT(count, flags) count, flags
#define F_METADATA_F_FUNCTION(...) 0, 0
#define F_METADATA_F_FUNCTION_LIST(...) 0, 0
#define F_METADATA_F_CFUNCTION(...) 0, 0
#define F_METADATA_F_MMOVE(...) 0, 0
#define F(TYPE, x, kind, ...) { #x, FOFS(TYPE, x) - (HANDLE)NULL, kind, sizeof(((struct TYPE *)NULL)->x), F_METADATA(kind, ##__VA_ARGS__), UINT32_MAX }
#define TF(TYPE, x, kind, ...) { #x, offsetof(TYPE, x), kind, sizeof(((TYPE *)NULL)->x), F_METADATA(kind, ##__VA_ARGS__), UINT32_MAX }
#define FC(TYPE, x, kind, count, schema, count_field) { #x, FOFS(TYPE, x) - (HANDLE)NULL, kind, sizeof(((struct TYPE *)NULL)->x), count, (uintptr_t)(schema), FOFS(TYPE, count_field) - (HANDLE)NULL }
#define FR(TYPE, x, count, ring) { #x, FOFS(TYPE, x) - (HANDLE)NULL, F_STRUCT_RING, sizeof(((struct TYPE *)NULL)->x), count, (uintptr_t)(ring), UINT32_MAX }
#define TFC(TYPE, x, kind, count, count_field) { #x, offsetof(TYPE, x), kind, sizeof(((TYPE *)NULL)->x), count, 0, offsetof(TYPE, count_field) }

enum {
    FIELD_NONE,
    FIELD_RUNTIME = 1 << 0,
};

static DWORD const save_magic = MAKEFOURCC('W', '3', 'S', 'V');
static DWORD const save_commit = MAKEFOURCC('W', '3', 'O', 'K');
static DWORD const save_version = 9; // edict C callbacks persist as F_CFUNCTION roster index+name hash
#define MAX_SAVE_STRING (1u << 20) // bytes; bounds quest-string allocations from corrupt saves
#define UMOVE_RELOC_RANGE (64 << 20) // bytes; every umove_t is static data in libgame, so a valid offset from the anchor stays well inside one module image

/* F_MMOVE anchor: umove_t instances are file-scope statics, so a move pointer
 * survives a save as a signed offset from a fixed symbol in the same data segment. */
static umove_t umove_reloc;

_Static_assert(sizeof(umove_t *) == 8, "F_MMOVE packs a relocation offset and a validation hash into the pointer field");
_Static_assert(sizeof(void (*)(LPEDICT)) == 8, "F_CFUNCTION packs a roster index and a name hash into the pointer field");

typedef struct {
    LPCSTR name;
    void *func;
} saveCFunction_t;

#define SAVE_CFUNCTION(fn) { .name = #fn, .func = (void *)(fn) }

/* Append-only: the 1-based index is part of the save format. Reordering rejects older saves.
 * idle/move/run/attack have no production assignments; they still use F_CFUNCTION so a later
 * assignment must be rostered here or WriteGame fails instead of writing an ASLR address. */
static saveCFunction_t const save_cfunctions[] = {
    SAVE_CFUNCTION(monster_think),
    SAVE_CFUNCTION(blight_mine_think),
    SAVE_CFUNCTION(G_FreeEdict),
    SAVE_CFUNCTION(G_EffectThink),
    SAVE_CFUNCTION(G_EffectValidateTarget),
    SAVE_CFUNCTION(blizzard_think),
    SAVE_CFUNCTION(flame_strike_tick),
    SAVE_CFUNCTION(siphon_mana_think),
    SAVE_CFUNCTION(unit_stand),
    SAVE_CFUNCTION(unit_birth),
    SAVE_CFUNCTION(unit_die),
    SAVE_CFUNCTION(tree_stand),
    SAVE_CFUNCTION(tree_birth),
    SAVE_CFUNCTION(tree_pain),
    SAVE_CFUNCTION(tree_die),
};

static int SaveCFunctionIndex(void *func) {
    if (!func) return 0;
    FOR_LOOP(i, sizeof(save_cfunctions) / sizeof(save_cfunctions[0]))
        if (save_cfunctions[i].func == func) return (int)i + 1;
    return -1;
}

typedef struct {
    DWORD magic, version, edict_size, num_edicts, max_clients;
    DWORD script_identity, quests, groups, triggers, timers, events;
    PATHSTR map_path;
} SAVEHEADER;

typedef struct { DWORD checksum, commit; } SAVEFOOTER;

typedef enum {
    JASS_HANDLE_ENTITY,
    JASS_HANDLE_PLAYER,
    JASS_HANDLE_QUEST,
    JASS_HANDLE_QUESTITEM,
    JASS_HANDLE_EVENT,
    JASS_HANDLE_TRIGGER,
    JASS_HANDLE_GROUP,
    JASS_HANDLE_TIMER,
} jassHandleDomain_t;

static struct { LPCSTR type; jassHandleDomain_t domain; } const jass_handle_domains[] = {
    { "unit", JASS_HANDLE_ENTITY },
    { "widget", JASS_HANDLE_ENTITY },
    { "destructable", JASS_HANDLE_ENTITY },
    { "item", JASS_HANDLE_ENTITY },
    { "effect", JASS_HANDLE_ENTITY },
    { "player", JASS_HANDLE_PLAYER },
    { "quest", JASS_HANDLE_QUEST },
    { "questitem", JASS_HANDLE_QUESTITEM },
    { "event", JASS_HANDLE_EVENT },
    { "trigger", JASS_HANDLE_TRIGGER },
    { "group", JASS_HANDLE_GROUP },
    { "timer", JASS_HANDLE_TIMER },
};

static field_t const save_event_fields[] = {
    F(gevent_s, type, F_INT),
    F(gevent_s, subject, F_EDICT, 0, FIELD_NONE),
    F(gevent_s, trigger, F_TRIGGER, 0, FIELD_NONE),
    F(gevent_s, timer, F_TIMER, 0, FIELD_NONE),
    F(gevent_s, region, F_REGION),
    F(gevent_s, range, F_FLOAT),
    F(gevent_s, state, F_INT),
    F(gevent_s, limitop, F_INT),
    F(gevent_s, limitval, F_FLOAT),
    F(gevent_s, inuse, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const save_game_event_fields[] = {
    F(gameevent_s, type, F_INT),
    F(gameevent_s, edict, F_EDICT, 0, FIELD_NONE),
    F(gameevent_s, source, F_EDICT, 0, FIELD_NONE),
    F(gameevent_s, responseTo, F_EVENT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const group_fields[] = {
    TFC(ggroup_t, units, F_EDICT, MAX_GROUP_SIZE, num_units),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const trigger_fields[] = {
    F(gtrigger_s, disabled, F_INT),
    F(gtrigger_s, actions, F_FUNCTION_LIST),
    F(gtrigger_s, conditions, F_FUNCTION_LIST),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const timer_fields[] = {
    F(gtimer_s, duration, F_INT),
    F(gtimer_s, remaining, F_INT),
    F(gtimer_s, periodic, F_INT),
    F(gtimer_s, paused, F_INT),
    F(gtimer_s, running, F_INT),
    F(gtimer_s, handler, F_FUNCTION),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const questitem_fields[] = {
    F(gquestitem_s, description, F_LSTRING),
    F(gquestitem_s, completed, F_INT),
    F(gquestitem_s, inuse, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const quest_fields[] = {
    F(gquest_s, title, F_LSTRING),
    F(gquest_s, description, F_LSTRING),
    F(gquest_s, iconPath, F_LSTRING),
    F(gquest_s, discovered, F_INT),
    F(gquest_s, required, F_INT),
    F(gquest_s, completed, F_INT),
    F(gquest_s, failed, F_INT),
    F(gquest_s, enabled, F_INT),
    F(gquest_s, inuse, F_INT),
    FC(gquest_s, items, F_STRUCT, MAX_QUESTITEMS, questitem_fields, num_items),
    { NULL, 0, 0, 0, 0, 0 }
};

static fieldRing_t const game_event_ring = {
    save_game_event_fields,
    FOFS(level_locals, events.read) - (HANDLE)NULL,
    FOFS(level_locals, events.write) - (HANDLE)NULL
};

static field_t const level_fields[] = {
    F(level_locals, framenum, F_INT),
    F(level_locals, time, F_INT),
    F(level_locals, timeofday.elapsed, F_FLOAT),
    F(level_locals, timeofday.pending, F_FLOAT),
    F(level_locals, timeofday.pending_valid, F_INT),
    F(level_locals, timeofday.suspended, F_INT),
    F(level_locals, camera_bounds, F_VECTOR),
    F(level_locals, started, F_INT),
    F(level_locals, scriptsStarted, F_INT),
    F(level_locals, waypoints.base, F_INT),
    F(level_locals, waypoints.cursor, F_INT),
    F(level_locals, waypoints.count, F_INT),
    F(level_locals, quests, F_STRUCT, MAX_QUESTS, quest_fields),
    FC(level_locals, groups, F_STRUCT, MAX_GROUPS, group_fields, num_groups),
    FC(level_locals, triggers, F_STRUCT, MAX_TRIGGERS, trigger_fields, num_triggers),
    FC(level_locals, timers, F_STRUCT, MAX_TIMERS, timer_fields, num_timers),
    F(level_locals, events.handlers, F_STRUCT, MAX_EVENTS, save_event_fields),
    FR(level_locals, events.queue, MAX_EVENT_QUEUE, &game_event_ring),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const entity_state_fields[] = {
    TF(entityState_t, origin, F_VECTOR),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const link_fields[] = {
    F(link_s, prev, F_IGNORE, 0, FIELD_RUNTIME),
    F(link_s, next, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const construction_fields[] = {
    TF(edictConstruction_s, primary_builder, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const rally_fields[] = {
    TF(edictRally_s, entity, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const revival_fields[] = {
    TF(edictRevival_s, producer, F_EDICT, 0, FIELD_NONE),
    TF(edictRevival_s, queue_next, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const militia_fields[] = {
    TF(edictMilitia_s, partner, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMilitia_s, partner_spawn_time, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMilitia_s, returning, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const goldmine_fields[] = {
    TF(edictGoldMine_s, mine, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const item_fields[] = {
    TF(edictItem_s, carrier, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const destructable_fields[] = {
    TF(edictDestructable_s, alive_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictDestructable_s, death_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictDestructable_s, drop_sets, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictDestructable_s, drop_sets_count, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const cargo_fields[] = {
    TF(edictCargo_s, units, F_EDICT, MAX_CARGO, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const abilities_fields[] = {
    TFC(edictAbilities_s, added, F_INT, MAX_ABILITIES, added_count),
    TFC(edictAbilities_s, removed, F_INT, MAX_ABILITIES, removed_count),
    TFC(edictAbilities_s, permanent, F_INT, MAX_ABILITIES, permanent_count),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const movement_fields[] = {
    TF(edictMovement_s, attackmove_waypoint, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_a, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_b, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_target, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, follow_target, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const edict_data_fields[] = {
    TF(edictData_s, UnitProfile, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, UnitBalance, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, UnitData, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, UnitUI, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, UnitWeapons, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, UnitAbilities, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, Doodads, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, ItemData, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictData_s, DestructableData, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const client_menu_fields[] = {
    TF(clientMenu_s, on_entity_selected, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, on_location_selected, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, cmdbutton, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, refresh, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const client_camera_fields[] = {
    TF(clientCamera_s, target_controller, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

/* Every persistent and process-owned edict field crossing the save boundary is represented here. */
field_t edict_fields[] = {
    F(edict_s, class_id, F_INT),
    F(edict_s, variation, F_INT),
    F(edict_s, build_project, F_INT),
    F(edict_s, spawn_time, F_INT),
    F(edict_s, harvested_lumber, F_INT),
    F(edict_s, harvested_gold, F_INT),
    F(edict_s, heatmap2, F_INT),
    F(edict_s, peonsinside, F_INT),
    F(edict_s, aiflags, F_INT),
    F(edict_s, damage, F_INT),
    F(edict_s, collision, F_FLOAT),
    F(edict_s, s, F_STRUCT, 1, entity_state_fields),
    F(edict_s, construction, F_STRUCT, 1, construction_fields),
    F(edict_s, rally, F_STRUCT, 1, rally_fields),
    F(edict_s, revival, F_STRUCT, 1, revival_fields),
    F(edict_s, goldmine, F_STRUCT, 1, goldmine_fields),
    F(edict_s, inventory, F_EDICT, MAX_INVENTORY, FIELD_NONE),
    F(edict_s, cargo, F_STRUCT, 1, cargo_fields),
    F(edict_s, item, F_STRUCT, 1, item_fields),
    F(edict_s, ground_next, F_EDICT, 0, FIELD_NONE),
    F(edict_s, movement, F_STRUCT, 1, movement_fields),
    F(edict_s, goalentity, F_EDICT, 0, FIELD_NONE),
    F(edict_s, combatentity, F_EDICT, 0, FIELD_NONE),
    F(edict_s, secondarygoal, F_EDICT, 0, FIELD_NONE),
    F(edict_s, owner, F_EDICT, 0, FIELD_NONE),
    F(edict_s, build, F_EDICT, 0, FIELD_NONE),
    F(edict_s, client, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, area, F_STRUCT, 1, link_fields),
    F(edict_s, destructable, F_STRUCT, 1, destructable_fields),
    F(edict_s, abilities, F_STRUCT, 1, abilities_fields),
    F(edict_s, animation, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, currentmove, F_MMOVE),
    F(edict_s, militia, F_STRUCT, 1, militia_fields),
    F(edict_s, stand, F_CFUNCTION),
    F(edict_s, birth, F_CFUNCTION),
    F(edict_s, prethink, F_CFUNCTION),
    F(edict_s, think, F_CFUNCTION),
    F(edict_s, die, F_CFUNCTION),
    F(edict_s, idle, F_CFUNCTION),
    F(edict_s, move, F_CFUNCTION),
    F(edict_s, run, F_CFUNCTION),
    F(edict_s, attack, F_CFUNCTION),
    F(edict_s, pain, F_CFUNCTION),
    F(edict_s, data, F_STRUCT, 1, edict_data_fields),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const client_fields[] = {
    F(client_s, ps.name, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, ps.texts, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, mapplayer, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, menu, F_STRUCT, 1, client_menu_fields),
    F(client_s, camera, F_STRUCT, 1, client_camera_fields),
    F(client_s, rally_indicator, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static void ClearRuntimeFields(void *object, field_t const *fields, DWORD flags) {
    for (field_t const *field = fields; field->name; field++) {
        DWORD count = field->array_size ? field->array_size : 1;
        size_t size = field->array_size ? field->size / field->array_size : field->size;
        switch (field->type) {
        case F_STRUCT:
            FOR_LOOP(i, count) ClearRuntimeFields((BYTE *)object + field->ofs + i * size, (field_t const *)field->flags, flags);
            break;
        case F_IGNORE:
            if (field->flags == flags) memset((BYTE *)object + field->ofs, 0, field->size);
            break;
        default: break;
        }
    }
}

static BOOL SaveBytes(FILE *f, LPCVOID data, size_t size) { return fwrite(data, 1, size, f) == size; }
static BOOL LoadBytes(FILE *f, void *data, size_t size) { return fread(data, 1, size, f) == size; }
static BOOL WriteJassBytes(void *context, void *data, DWORD size) { return SaveBytes(context, data, size); }
static BOOL ReadJassBytes(void *context, void *data, DWORD size) { return LoadBytes(context, data, size); }
static BOOL WriteMappedFields(FILE *f, field_t const *fields, BYTE *base);
static BOOL ReadMappedFields(FILE *f, field_t const *fields, BYTE *base);
static BOOL WriteString(FILE *f, LPCSTR text);
static BOOL ReadString(FILE *f, LPSTR *text);
static DWORD ActiveEventCount(void);

static DWORD SaveHash(DWORD hash, LPCVOID data, size_t size) {
    BYTE const *bytes = data;
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

/* A committed checksum rejects truncation and corruption before ReadGame mutates live state. */
static BOOL WriteFooter(FILE *f) {
    BYTE bytes[4096];
    long payload;
    DWORD checksum = 2166136261u;
    SAVEFOOTER footer;
    if (fflush(f) || (payload = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) return false;
    while (payload > 0) {
        size_t size = MIN((size_t)payload, sizeof(bytes));
        if (fread(bytes, 1, size, f) != size) return false;
        checksum = SaveHash(checksum, bytes, size); payload -= (long)size;
    }
    footer = (SAVEFOOTER){ checksum, save_commit };
    return fseek(f, 0, SEEK_END) == 0 && SaveBytes(f, &footer, sizeof(footer));
}

static BOOL ReadFooter(FILE *f) {
    BYTE bytes[4096];
    long payload;
    DWORD checksum = 2166136261u;
    SAVEFOOTER footer;
    if (fseek(f, 0, SEEK_END) || (payload = ftell(f)) < (long)sizeof(footer)) return false;
    payload -= sizeof(footer);
    if (fseek(f, payload, SEEK_SET) || !LoadBytes(f, &footer, sizeof(footer)) || footer.commit != save_commit ||
        fseek(f, 0, SEEK_SET)) return false;
    for (long remaining = payload; remaining > 0;) {
        size_t size = MIN((size_t)remaining, sizeof(bytes));
        if (fread(bytes, 1, size, f) != size) return false;
        checksum = SaveHash(checksum, bytes, size); remaining -= (long)size;
    }
    return checksum == footer.checksum && fseek(f, 0, SEEK_SET) == 0;
}

/* Save files carry the canonical map path so the server can rebuild the map before restoring state. */
BOOL G_GetSaveMap(LPCSTR filename, LPSTR map, DWORD map_size) {
    FILE *f = fopen(filename, "rb");
    SAVEHEADER header;
    DWORD magic, version;
    if (!f || !map || !map_size) { if (f) fclose(f); return false; }
    if (!ReadFooter(f) || !LoadBytes(f, &magic, sizeof(magic)) || !LoadBytes(f, &version, sizeof(version)) ||
        magic != save_magic || version != save_version || fseek(f, 0, SEEK_SET) || !LoadBytes(f, &header, sizeof(header)) ||
        !header.map_path[0]) {
        if (f) fclose(f);
        return false;
    }
    strlcpy(map, header.map_path, map_size);
    fclose(f);
    return true;
}

void G_ClearSaveRegistries(void) {
    FOR_LOOP(i, level.num_triggers) {
        DELETE_LIST(TRIGGERACTION, level.triggers[i].actions, gi.MemFree);
        DELETE_LIST(TRIGGERCONDITION, level.triggers[i].conditions, gi.MemFree);
    }
}

static BOOL RestoreRegistrySlots(DWORD groups, DWORD timers, DWORD triggers, DWORD events) {
    if (groups < level.num_groups || timers < level.num_timers || triggers < level.num_triggers ||
        events < ActiveEventCount() || groups > MAX_GROUPS || timers > MAX_TIMERS ||
        triggers > MAX_TRIGGERS || events > MAX_EVENTS)
        return false;
    while (level.num_groups < groups) if (!G_AllocJassGroup()) return false;
    while (level.num_timers < timers) if (!G_AllocJassTimer()) return false;
    while (level.num_triggers < triggers) if (!G_AllocJassTrigger()) return false;
    while (ActiveEventCount() < events) if (!G_MakeEvent(0)) return false;
    return true;
}

/* VM state follows native domains so load-side handle relocation sees restored objects. */
static BOOL WriteJass(FILE *f) {
    BOOL present = level.vm != NULL;
    JASSSNAPSHOT snapshot = { f, WriteJassBytes };
    return SaveBytes(f, &present, sizeof(present)) && (!present || jass_writesnapshot(level.vm, &snapshot));
}

static BOOL ReadJass(FILE *f) {
    BOOL present;
    JASSSNAPSHOT snapshot = { f, ReadJassBytes };
    if (!LoadBytes(f, &present, sizeof(present)) || present > 1 || present != (level.vm != NULL)) {
        fprintf(stderr, "WC3 LoadGame: JASS VM lifecycle does not match save\n");
        return false;
    }
    return !present || jass_readsnapshot(level.vm, &snapshot);
}

static DWORD ActiveQuestCount(void) {
    DWORD count = 0;
    FOR_EACH_QUEST(quest) count++;
    return count;
}

static DWORD ActiveEventCount(void) {
    DWORD count = 0;
    FOR_EACH_EVENT(event) count++;
    return count;
}

static BOOL EventId(LPEVENT value, DWORD *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value >= level.events.handlers && value < level.events.handlers + MAX_EVENTS) {
        *id = (DWORD)(value - level.events.handlers); return value->inuse;
    }
    return false;
}

static LPEVENT EventById(DWORD id) {
    return id < MAX_EVENTS && level.events.handlers[id].inuse ? &level.events.handlers[id] : NULL;
}

static BOOL TriggerIndex(LPTRIGGER value, DWORD *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value < level.triggers || value >= level.triggers + level.num_triggers) return false;
    *id = (DWORD)(value - level.triggers); return true;
}

static BOOL TimerIndex(LPGTIMER value, DWORD *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value < level.timers || value >= level.timers + level.num_timers) return false;
    *id = (DWORD)(value - level.timers); return true;
}

static DWORD TriggerCodeCount(TRIGGERACTION const *list) {
    DWORD n = 0;
    for (; list; list = list->next) n++;
    return n;
}

static BOOL WriteTriggerCodeList(FILE *f, TRIGGERACTION const *list) {
    DWORD n = TriggerCodeCount(list);
    if (!SaveBytes(f, &n, sizeof(n))) return false;
    for (; list; list = list->next) if (!WriteString(f, jass_functionname(list->func))) return false;
    return true;
}

static BOOL ReadTriggerCodeList(FILE *f, TRIGGERACTION **list) {
    DWORD n;
    TRIGGERACTION **tail;
    if (!LoadBytes(f, &n, sizeof(n))) return false;
    DELETE_LIST(TRIGGERACTION, *list, gi.MemFree);
    *list = NULL;
    tail = list;
    FOR_LOOP(i, n) {
        LPSTR name = NULL;
        TRIGGERACTION *item = gi.MemAlloc(sizeof(*item));
        if (!item || !ReadString(f, &name)) { free(name); if (item) gi.MemFree(item); return false; }
        item->func = name ? jass_functionbyname(level.vm, name) : NULL;
        if (name && !item->func) { free(name); gi.MemFree(item); return false; }
        free(name);
        *tail = item;
        tail = &item->next;
    }
    return true;
}

static BOOL JassHandleDomain(LPCSTR type, jassHandleDomain_t *domain) {
    FOR_LOOP(i, sizeof(jass_handle_domains) / sizeof(*jass_handle_domains)) {
        if (!strcmp(type, jass_handle_domains[i].type)) { *domain = jass_handle_domains[i].domain; return true; }
    }
    return false;
}

static HANDLE JassListHandle(jassHandleDomain_t domain, DWORD id) {
    DWORD index = 0;
    if (domain == JASS_HANDLE_QUEST) {
        if (id < MAX_QUESTS && level.quests[id].inuse) return &level.quests[id];
    } else if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_QUEST(quest)
            FOR_EACH_QUESTITEM(quest, item) if (index++ == id) return item;
    } else if (domain == JASS_HANDLE_EVENT) {
        return EventById(id);
    } else if (domain == JASS_HANDLE_TRIGGER && id < level.num_triggers) return &level.triggers[id];
    else if (domain == JASS_HANDLE_TIMER && id < level.num_timers) return &level.timers[id];
    return NULL;
}

/* Native pointers cross the save boundary only through stable domain-specific indexes. */
BOOL G_SaveJassHandle(LPCSTR type, HANDLE value, DWORD *id) {
    jassHandleDomain_t domain;
    DWORD index = 0;
    if (!JassHandleDomain(type, &domain) || !value) return false;
    if (domain == JASS_HANDLE_ENTITY) {
        LPEDICT ent = value;
        uintptr_t ptr = (uintptr_t)ent, base = (uintptr_t)g_edicts;
        if (ptr < base || ptr >= base + sizeof(*g_edicts) * globals.num_edicts || (ptr - base) % sizeof(*g_edicts)) {
            fprintf(stderr, "WC3 SaveGame: %s handle %p outside edict table [%p, %p)\n", type, value,
                (void *)g_edicts, (void *)(g_edicts + globals.num_edicts));
            return false;
        }
        if (!ent->inuse) {
            fprintf(stderr, "WC3 SaveGame: %s handle %p is unused edict %ld\n", type, value, (long)(ent - g_edicts));
            return false;
        }
        *id = (DWORD)(ent - g_edicts); return true;
    }
    if (domain == JASS_HANDLE_PLAYER) {
        FOR_LOOP(i, game.max_clients) if (value == &game.clients[i].ps) { *id = i; return true; }
        return false;
    }
    if (domain == JASS_HANDLE_GROUP) {
        if ((ggroup_t *)value < level.groups || (ggroup_t *)value >= level.groups + level.num_groups) return false;
        *id = (DWORD)((ggroup_t *)value - level.groups); return true;
    }
    if (domain == JASS_HANDLE_TIMER) {
        return TimerIndex(value, id);
    }
    if (domain == JASS_HANDLE_QUEST) {
        if ((LPQUEST)value >= level.quests && (LPQUEST)value < level.quests + MAX_QUESTS && ((LPQUEST)value)->inuse) {
            *id = (DWORD)((LPQUEST)value - level.quests); return true;
        }
        return false;
    }
    if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_QUEST(quest)
            FOR_EACH_QUESTITEM(quest, item) { if (item == value) { *id = index; return true; } index++; }
        return false;
    }
    if (domain == JASS_HANDLE_EVENT) {
        return EventId(value, id);
    }
    return TriggerIndex(value, id);
}

HANDLE G_LoadJassHandle(LPCSTR type, DWORD id) {
    jassHandleDomain_t domain;
    if (!JassHandleDomain(type, &domain)) return NULL;
    if (domain == JASS_HANDLE_ENTITY) return id < globals.num_edicts && g_edicts[id].inuse ? g_edicts + id : NULL;
    if (domain == JASS_HANDLE_PLAYER) return id < (DWORD)game.max_clients ? &game.clients[id].ps : NULL;
    if (domain == JASS_HANDLE_GROUP) return id < level.num_groups ? &level.groups[id] : NULL;
    if (domain == JASS_HANDLE_TIMER) return id < level.num_timers ? &level.timers[id] : NULL;
    return JassListHandle(domain, id);
}

static BOOL WriteString(FILE *f, LPCSTR text) {
    size_t size = text ? strlen(text) + 1 : 0;
    DWORD len;

    if (size > MAX_SAVE_STRING) return false;
    len = (DWORD)size;
    return SaveBytes(f, &len, sizeof(len)) && (!len || SaveBytes(f, text, len));
}

static BOOL ReadString(FILE *f, LPSTR *text) {
    DWORD len;
    LPSTR value = NULL;

    if (!LoadBytes(f, &len, sizeof(len)) || len > MAX_SAVE_STRING) return false;
    if (len) {
        value = malloc(len);
        if (!value || !LoadBytes(f, value, len) || value[len - 1]) { free(value); return false; }
    }
    free(*text); *text = value;
    return true;
}

static BOOL WriteField1(field_t const *field, BYTE *base) {
    DWORD count = field->count_ofs != UINT32_MAX ? *(DWORD *)(base + field->count_ofs) :
        field->array_size ? field->array_size : 1;
    size_t size = field->array_size ? field->size / field->array_size : field->size;
    int index;

    if (field->count_ofs != UINT32_MAX && count > field->array_size) {
        fprintf(stderr, "WC3 SaveGame: field %s count %u exceeds %u\n", field->name, count, field->array_size); return false;
    }
    if (field->type == F_STRUCT) {
        FOR_LOOP(i, count) {
            for (field_t const *child = (field_t const *)field->flags; child->name; child++)
                if (!WriteField1(child, base + field->ofs + i * size)) return false;
        }
        return true;
    }
    if (!size || field->type == F_IGNORE) return true;
    FOR_LOOP(i, count) {
        void *p = base + field->ofs + i * size;
        switch (field->type) {
        case F_EDICT: {
            LPEDICT value = *(LPEDICT *)p;
            uintptr_t ptr = (uintptr_t)value, base = (uintptr_t)g_edicts;
            if (value && (ptr < base || ptr >= base + sizeof(*g_edicts) * globals.num_edicts ||
                (ptr - base) % sizeof(*g_edicts))) {
                fprintf(stderr, "WC3 SaveGame: field %s[%u] points outside g_edicts (%p)\n",
                    field->name, i, (void *)value);
                return false;
            }
            index = value ? (int)(value - g_edicts) : -1; *(int *)p = index; break;
        }
        case F_MMOVE: {
            umove_t const *move = *(umove_t *const *)p;
            memset(p, 0, size);
            if (!move) break;
            *(int *)p = (int)((BYTE const *)move - (BYTE const *)&umove_reloc);
            *(DWORD *)((BYTE *)p + 4) = SaveHash(0, move->animation, strlen(move->animation) + 1);
            break;
        }
        case F_CFUNCTION: {
            void *func = *(void **)p;
            int index = SaveCFunctionIndex(func);
            memset(p, 0, size);
            if (!func) break;
            if (index < 1) {
                fprintf(stderr, "WC3 SaveGame: field %s[%u] C callback %p is not in the save roster\n",
                    field->name, i, func);
                return false;
            }
            *(int *)p = index;
            *(DWORD *)((BYTE *)p + 4) = SaveHash(0, save_cfunctions[index - 1].name, strlen(save_cfunctions[index - 1].name) + 1);
            break;
        }
        default: break;
        }
    }
    return true;
}

/* Restore entity and client pointers after the raw edict block is read. */
static BOOL ReadField(field_t const *field, BYTE *base) {
    DWORD count = field->count_ofs != UINT32_MAX ? *(DWORD *)(base + field->count_ofs) :
        field->array_size ? field->array_size : 1;
    size_t size = field->array_size ? field->size / field->array_size : field->size;

    if (field->count_ofs != UINT32_MAX && count > field->array_size) {
        fprintf(stderr, "WC3 LoadGame: field %s count %u exceeds %u\n", field->name, count, field->array_size); return false;
    }
    if (field->type == F_STRUCT) {
        FOR_LOOP(i, count) {
            for (field_t const *child = (field_t const *)field->flags; child->name; child++)
                if (!ReadField(child, base + field->ofs + i * size)) return false;
        }
        return true;
    }
    if (!size || field->type == F_IGNORE) return true;
    FOR_LOOP(i, count) {
        void *p = base + field->ofs + i * size;
        int index = *(int *)p;
        switch (field->type) {
        case F_EDICT:
            if (index < -1 || index >= globals.max_edicts) {
                fprintf(stderr, "WC3 LoadGame: field %s[%u] has invalid edict index %d\n", field->name, i, index);
                return false;
            }
            *(LPEDICT *)p = index < 0 ? NULL : g_edicts + index;
            break;
        case F_MMOVE: {
            DWORD hash = *(DWORD *)((BYTE *)p + 4);
            umove_t *move = (umove_t *)((BYTE *)&umove_reloc + index);
            if (!index && !hash) { *(umove_t **)p = NULL; break; }
            /* Reject a save written by a different build before dereferencing the move. */
            if (index < -UMOVE_RELOC_RANGE || index > UMOVE_RELOC_RANGE || (uintptr_t)move % _Alignof(umove_t) ||
                !move->animation || SaveHash(0, move->animation, strlen(move->animation) + 1) != hash) {
                fprintf(stderr, "WC3 LoadGame: field %s[%u] move offset %d does not resolve in this build\n",
                    field->name, i, index);
                return false;
            }
            *(umove_t **)p = move;
            break;
        }
        case F_CFUNCTION: {
            DWORD hash = *(DWORD *)((BYTE *)p + 4);
            int nfunctions = (int)(sizeof(save_cfunctions) / sizeof(save_cfunctions[0]));
            if (!index && !hash) { *(void **)p = NULL; break; }
            if (index < 1 || index > nfunctions ||
                SaveHash(0, save_cfunctions[index - 1].name, strlen(save_cfunctions[index - 1].name) + 1) != hash) {
                fprintf(stderr, "WC3 LoadGame: field %s[%u] C callback index %d does not resolve in this build\n",
                    field->name, i, index);
                return false;
            }
            *(void **)p = save_cfunctions[index - 1].func;
            break;
        }
        default: break;
        }
    }
    return true;
}

/* Convert one schema pointer to its stable save-domain index without mutating the live object. */
static BOOL WriteMappedIndex(field_t const *field, void *ptr, int *index) {
    switch (field->type) {
    case F_EDICT:
    case F_ITEM: {
        LPEDICT value = *(LPEDICT *)ptr;
        uintptr_t addr = (uintptr_t)value, base = (uintptr_t)g_edicts;
        if (value && (addr < base || addr >= base + sizeof(*g_edicts) * globals.num_edicts ||
            (addr - base) % sizeof(*g_edicts))) return false;
        *index = value ? (int)(value - g_edicts) : -1; return true;
    }
    case F_TRIGGER: {
        DWORD id;
        if (!TriggerIndex(*(LPTRIGGER *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    case F_TIMER: {
        DWORD id;
        if (!TimerIndex(*(LPGTIMER *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    case F_EVENT: {
        DWORD id;
        if (!EventId(*(LPEVENT *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    default: return false;
    }
}

/* Resolve one schema index directly into the pointer domain declared by its field type. */
static BOOL ReadMappedIndex(field_t const *field, void *ptr, int index) {
    if (index < -1) return false;
    switch (field->type) {
    case F_EDICT:
    case F_ITEM:
        if (index >= (int)globals.max_edicts) return false;
        *(LPEDICT *)ptr = index < 0 ? NULL : g_edicts + index; return true;
    case F_TRIGGER:
        if (index >= (int)level.num_triggers) return false;
        *(LPTRIGGER *)ptr = index < 0 ? NULL : &level.triggers[index]; return true;
    case F_TIMER:
        if (index >= (int)level.num_timers) return false;
        *(LPGTIMER *)ptr = index < 0 ? NULL : &level.timers[index]; return true;
    case F_EVENT:
        if (index >= (int)ActiveEventCount()) return false;
        *(LPEVENT *)ptr = index < 0 ? NULL : EventById(index); return true;
    default: return false;
    }
}

/* Serialize mapped records, converting pointer-domain fields to stable indexes from their field types. */
static BOOL WriteMappedFields(FILE *f, field_t const *fields, BYTE *base) {
    for (; fields->name; fields++) {
        DWORD count = fields->count_ofs != UINT32_MAX ? *(DWORD *)(base + fields->count_ofs) :
            fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->count_ofs != UINT32_MAX && count > fields->array_size) return false;
        if (fields->count_ofs != UINT32_MAX && !SaveBytes(f, &count, sizeof(count))) return false;
        switch (fields->type) {
        case F_STRUCT:
            FOR_LOOP(i, count) if (!WriteMappedFields(f, (field_t const *)fields->flags, base + fields->ofs + i * size)) return false;
            break;
        case F_STRUCT_RING: {
            fieldRing_t const *ring = (fieldRing_t const *)fields->flags;
            DWORD read = *(DWORD *)(base + ring->read_ofs), write = *(DWORD *)(base + ring->write_ofs);
            count = write - read;
            if (count > fields->array_size || !SaveBytes(f, &count, sizeof(count))) return false;
            FOR_LOOP(i, count) if (!WriteMappedFields(f, ring->fields,
                base + fields->ofs + ((read + i) % fields->array_size) * size)) return false;
            break;
        }
        case F_FUNCTION_LIST:
            if (!WriteTriggerCodeList(f, *(TRIGGERACTION **)(base + fields->ofs))) return false;
            break;
        case F_FUNCTION:
            if (!WriteString(f, jass_functionname(*(LPCJASSFUNC *)(base + fields->ofs)))) return false;
            break;
        case F_LSTRING:
        case F_GSTRING:
            if (!WriteString(f, *(LPCSTR *)(base + fields->ofs))) return false;
            break;
        case F_EDICT:
        case F_ITEM:
        case F_TRIGGER:
        case F_TIMER:
        case F_EVENT:
            FOR_LOOP(i, count) {
                int index;
                if (!WriteMappedIndex(fields, base + fields->ofs + i * size, &index)) {
                    fprintf(stderr, "WC3 SaveGame: cannot resolve mapped field %s[%u]\n", fields->name, i); return false;
                }
                if (!SaveBytes(f, &index, sizeof(index))) return false;
            }
            break;
        default:
            if (!SaveBytes(f, base + fields->ofs, fields->size)) return false;
            break;
        }
    }
    return true;
}

/* Restore mapped records, resolving pointer-domain fields from stable indexes declared by their field types. */
static BOOL ReadMappedFields(FILE *f, field_t const *fields, BYTE *base) {
    for (; fields->name; fields++) {
        DWORD count = fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->count_ofs != UINT32_MAX) {
            if (!LoadBytes(f, &count, sizeof(count)) || count > fields->array_size) return false;
            *(DWORD *)(base + fields->count_ofs) = count;
        }
        switch (fields->type) {
        case F_STRUCT:
            FOR_LOOP(i, count) if (!ReadMappedFields(f, (field_t const *)fields->flags, base + fields->ofs + i * size)) return false;
            break;
        case F_STRUCT_RING: {
            fieldRing_t const *ring = (fieldRing_t const *)fields->flags;
            if (!LoadBytes(f, &count, sizeof(count)) || count > fields->array_size) return false;
            *(DWORD *)(base + ring->read_ofs) = 0; *(DWORD *)(base + ring->write_ofs) = count;
            FOR_LOOP(i, count) if (!ReadMappedFields(f, ring->fields, base + fields->ofs + i * size)) return false;
            break;
        }
        case F_FUNCTION_LIST:
            if (!ReadTriggerCodeList(f, (TRIGGERACTION **)(base + fields->ofs))) return false;
            break;
        case F_FUNCTION: {
            LPSTR name = NULL;
            if (!ReadString(f, &name)) return false;
            *(LPCJASSFUNC *)(base + fields->ofs) = name ? jass_functionbyname(level.vm, name) : NULL;
            if (name && !*(LPCJASSFUNC *)(base + fields->ofs)) { free(name); return false; }
            free(name);
            break;
        }
        case F_LSTRING:
        case F_GSTRING:
            if (!ReadString(f, (LPSTR *)(base + fields->ofs))) return false;
            break;
        case F_EDICT:
        case F_ITEM:
        case F_TRIGGER:
        case F_TIMER:
        case F_EVENT:
            FOR_LOOP(i, count) {
                int index;
                if (!LoadBytes(f, &index, sizeof(index))) return false;
                if (!ReadMappedIndex(fields, base + fields->ofs + i * size, index)) {
                    fprintf(stderr, "WC3 LoadGame: invalid mapped field %s[%u] index=%d\n", fields->name, i, index); return false;
                }
            }
            break;
        default:
            if (!LoadBytes(f, base + fields->ofs, fields->size)) return false;
            break;
        }
    }
    return true;
}

static BOOL WriteEdict(FILE *f, LPCEDICT ent) {
    edict_t temp = *ent;
    field_t const *field;

    ClearRuntimeFields(&temp, edict_fields, FIELD_RUNTIME);
    for (field = edict_fields; field->name; field++)
        if (!WriteField1(field, (BYTE *)&temp)) return false;
    return SaveBytes(f, &temp, sizeof(temp));
}

static BOOL WriteClient(FILE *f, LPCGAMECLIENT client) {
    GAMECLIENT temp = *client;
    int target = client->camera.target_controller ? (int)(client->camera.target_controller - g_edicts) : -1;

    /* Client pointers and callbacks are process-owned; text storage remains inline in GAMECLIENT. */
    ClearRuntimeFields(&temp, client_fields, FIELD_RUNTIME);
    if (target < -1 || target >= (int)globals.max_edicts) return false;
    return SaveBytes(f, &temp, sizeof(temp)) && SaveBytes(f, &target, sizeof(target));
}

static BOOL ReadClient(FILE *f, LPGAMECLIENT client, int *target) {
    if (!LoadBytes(f, client, sizeof(*client)) || !LoadBytes(f, target, sizeof(*target))) return false;
    if (*target < -1 || *target >= (int)globals.max_edicts) return false;
    client->ps.name = client->jass.name;
    FOR_LOOP(i, PLAYERTEXT_COUNT) client->ps.texts[i] = client->playerTextCursor[i] ?
        client->playerTextStorage[i][client->playerTextCursor[i] & PLAYER_TEXT_MASK] : NULL;
    client->mapplayer = level.mapinfo && client->ps.number < MAX_PLAYERS ? level.mapinfo->players + client->ps.number : NULL;
    client->menu.on_entity_selected = NULL; client->menu.on_location_selected = NULL;
    client->menu.cmdbutton = NULL; client->menu.refresh = NULL;
    client->camera.target_controller = NULL;
    client->rally_indicator = NULL;
    return true;
}

static BOOL ReadEdict(FILE *f, LPEDICT ent) {
    field_t const *field;

    if (!LoadBytes(f, ent, sizeof(*ent))) return false;
    for (field = edict_fields; field->name; field++)
        if (!ReadField(field, (BYTE *)ent)) return false;
    /* Table rows are process-owned; C callbacks already came back through F_CFUNCTION. */
    if (ent->class_id) G_BindEntityData(ent);
    return true;
}

BOOL WriteGame(LPCSTR filename) {
    FILE *f = fopen(filename, "w+b");
    SAVEHEADER header = {
        .magic = save_magic, .version = save_version, .edict_size = sizeof(edict_t), .num_edicts = globals.num_edicts,
        .max_clients = game.max_clients, .script_identity = level.vm ? jass_programidentity(level.vm) : 0,
        .quests = ActiveQuestCount(), .groups = level.num_groups, .triggers = level.num_triggers, .timers = level.num_timers,
        .events = ActiveEventCount()
    };
    strlcpy(header.map_path, level.map_path, sizeof(header.map_path));

    BOOL ok = false;
    if (!f) { fprintf(stderr, "WC3 SaveGame: cannot open %s\n", filename); return false; }
    if (!SaveBytes(f, &header, sizeof(header))) { fprintf(stderr, "WC3 SaveGame: failed at header\n"); goto done; }
    if (!WriteMappedFields(f, level_fields, (BYTE *)&level)) {
        fprintf(stderr, "WC3 SaveGame: failed at level fields\n"); goto done;
    }
    FOR_LOOP(i, game.max_clients) {
        if (!WriteClient(f, game.clients + i)) { fprintf(stderr, "WC3 SaveGame: failed at client %d\n", i); goto done; }
    }
    FOR_LOOP(i, globals.num_edicts) {
        BOOL used = g_edicts[i].inuse;
        if (!SaveBytes(f, &used, sizeof(used))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d inuse\n", i); goto done; }
        if (used && !SaveBytes(f, &i, sizeof(i))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d index\n", i); goto done; }
        if (used && !WriteEdict(f, g_edicts + i)) {
            fprintf(stderr, "WC3 SaveGame: failed at edict %d class=%08x\n", i, g_edicts[i].class_id); goto done;
        }
    }
    if (!WriteJass(f)) { fprintf(stderr, "WC3 SaveGame: failed at jass\n"); goto done; }
    if (!WriteFooter(f)) { fprintf(stderr, "WC3 SaveGame: failed at footer/checksum\n"); goto done; }
    ok = true;
done:
    fclose(f);
    if (!ok) remove(filename);
    return ok;
}

BOOL ReadGame(LPCSTR filename) {
    FILE *f = fopen(filename, "rb");
    SAVEHEADER header = { 0 };
    DWORD index;
    int targets[MAX_CLIENTS];

    if (!f) { fprintf(stderr, "WC3 LoadGame: cannot open %s\n", filename); return false; }
    if (!ReadFooter(f)) { fprintf(stderr, "WC3 LoadGame: invalid footer/checksum\n"); fclose(f); return false; }
    if (!LoadBytes(f, &header.magic, sizeof(header.magic)) || !LoadBytes(f, &header.version, sizeof(header.version)) ||
        fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "WC3 LoadGame: invalid header\n"); fclose(f); return false;
    }
    if (header.version != save_version || !LoadBytes(f, &header, sizeof(header))) {
        fprintf(stderr, "WC3 LoadGame: invalid header\n"); fclose(f); return false;
    }
    {
        DWORD script = level.vm ? jass_programidentity(level.vm) : 0;
        LPCSTR field = NULL;
        if (header.magic != save_magic) field = "magic";
        else if (header.edict_size != sizeof(edict_t)) field = "edict_size";
        else if (header.num_edicts > globals.max_edicts) field = "num_edicts";
        else if (header.max_clients != game.max_clients) field = "max_clients";
        else if (header.script_identity != script) field = "script_identity";
        else if (header.quests != ActiveQuestCount()) field = "quests";
        else if (header.groups < level.num_groups) field = "groups";
        else if (header.triggers < level.num_triggers) field = "triggers";
        else if (header.timers < level.num_timers) field = "timers";
        else if (header.events < ActiveEventCount()) field = "events";
        else if (!header.map_path[0] || strcasecmp(header.map_path, level.map_path)) field = "map_path";
        else if (!RestoreRegistrySlots(header.groups, header.timers, header.triggers, header.events)) field = "registry_slots";
        if (field) {
            fprintf(stderr, "WC3 LoadGame: header mismatch field=%s version=%u edict_size=%u/%zu edicts=%u/%u\n",
                    field, header.version, header.edict_size, sizeof(edict_t), header.num_edicts, globals.max_edicts);
            fprintf(stderr, "WC3 LoadGame: clients=%u/%u script=%u/%u quests=%u/%u groups=%u/%u triggers=%u/%u\n",
                    header.max_clients, game.max_clients, header.script_identity, script,
                        header.quests, ActiveQuestCount(), header.groups, level.num_groups, header.triggers, level.num_triggers);
            fprintf(stderr, "WC3 LoadGame: timers=%u/%u events=%u/%u map='%s'/'%s'\n",
                        header.timers, level.num_timers, header.events, ActiveEventCount(), header.map_path, level.map_path);
            fclose(f); return false;
        }
    }
    if (!ReadMappedFields(f, level_fields, (BYTE *)&level) || level.waypoints.count > MAX_WAYPOINTS ||
        (level.waypoints.count && (level.waypoints.count != MAX_WAYPOINTS || level.waypoints.cursor >= MAX_WAYPOINTS ||
        header.num_edicts < level.waypoints.count ||
        level.waypoints.base > header.num_edicts - level.waypoints.count)) ||
        (!level.waypoints.count && (level.waypoints.base || level.waypoints.cursor))) {
        fprintf(stderr, "WC3 LoadGame: failed at level state\n"); fclose(f); return false;
    }
    /* Restore the Q2-style server tick before the next frame; all persisted deadlines use it. */
    gi.SetGameTime(level.time);
    FOR_LOOP(i, game.max_clients) if (!ReadClient(f, game.clients + i, targets + i)) {
        fprintf(stderr, "WC3 LoadGame: failed at client %d\n", i); fclose(f); return false;
    }
    /* The baseline map already linked these same edict addresses. Clear its
     * spatial tree before raw records overwrite their area links, then rebuild
     * one authoritative set below; retaining both creates cyclic area lists. */
    gi.ClearWorld();
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = header.num_edicts;
    FOR_LOOP(i, header.num_edicts) {
        BOOL used;
        if (!LoadBytes(f, &used, sizeof(used))) {
            fprintf(stderr, "WC3 LoadGame: failed at edict %d inuse\n", i); fclose(f); return false;
        }
        if (!used) continue;
        if (!LoadBytes(f, &index, sizeof(index)) || index >= globals.max_edicts || !ReadEdict(f, g_edicts + index)) {
            fprintf(stderr, "WC3 LoadGame: failed at edict %d data\n", i); fclose(f); return false;
        }
    }
    /* JASS sound-handle playback parameters are transient presentation state,
     * not VM-owned payload bytes. Clear old pointer keys before snapshot handles
     * are reconstructed so a reused allocation cannot inherit stale state. */
    G_JassSoundRuntimeReset();
    if (!ReadJass(f)) { fprintf(stderr, "WC3 LoadGame: failed at jass\n"); fclose(f); return false; }
    FOR_LOOP(i, game.max_clients) g_edicts[i].client = game.clients + i;
    FOR_LOOP(i, game.max_clients) game.clients[i].camera.target_controller = targets[i] < 0 ? NULL : g_edicts + targets[i];
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->rally_indicator && ent->owner && ent->owner->client)
            ent->owner->client->rally_indicator = ent;
    }
    FOR_LOOP(i, globals.num_edicts) if (g_edicts[i].inuse && gi.LinkEntity) gi.LinkEntity(g_edicts + i);
    fclose(f);
    fprintf(stderr, "WC3 LoadGame: restored %s edicts=%u\n", filename, header.num_edicts);
    return true;
}
