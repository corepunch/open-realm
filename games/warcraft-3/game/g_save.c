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
    F_CLIENT,            // index on disk, pointer in memory
    F_TRIGGER,          // index on disk, pointer in memory
    F_TIMER,            // index on disk, pointer in memory
    F_EVENT,            // index on disk, pointer in memory
    F_FUNCTION,
    F_MMOVE,
    F_IGNORE
} fieldtype_t;

typedef struct {
    LPCSTR name;
    DWORD ofs;
    fieldtype_t type;
    size_t size;
    DWORD array_size;
    DWORD flags;
} field_t;

#define F(TYPE, x, kind, ...) { #x, FOFS(TYPE, x) - (HANDLE)NULL, kind, sizeof(((struct TYPE *)NULL)->x), ##__VA_ARGS__ }

enum {
    FIELD_NONE,
    FIELD_RUNTIME = 1 << 0,
    FIELD_CLIENT = 1 << 1,
};

static DWORD const save_magic = MAKEFOURCC('W', '3', 'S', 'V');
static DWORD const save_commit = MAKEFOURCC('W', '3', 'O', 'K');
static DWORD const save_version = 2;
#define MAX_SAVE_STRING (1u << 20) // bytes; bounds quest-string allocations from corrupt saves

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

static ggroup_t *save_groups[MAX_JASS_GROUPS];
static LPGTIMER save_timers[MAX_JASS_TIMERS];
static LPTRIGGER save_triggers[MAX_JASS_TRIGGERS];

typedef struct saveTimer_s {
    DWORD duration, remaining;
    BOOL periodic, paused, running;
} SAVE_TIMER;

typedef struct saveGroup_s {
    DWORD num_units;
} SAVE_GROUP;

static field_t const save_event_fields[] = {
    F(gevent_s, type, F_INT),
    F(gevent_s, subject, F_EDICT),
    F(gevent_s, trigger, F_TRIGGER),
    F(gevent_s, timer, F_TIMER),
    F(gevent_s, region, F_REGION),
    F(gevent_s, range, F_FLOAT),
    F(gevent_s, state, F_INT),
    F(gevent_s, limitop, F_INT),
    F(gevent_s, limitval, F_FLOAT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const save_game_event_fields[] = {
    F(gameevent_s, type, F_INT),
    F(gameevent_s, edict, F_EDICT),
    F(gameevent_s, source, F_EDICT),
    F(gameevent_s, responseTo, F_EVENT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const group_fields[] = {
    F(saveGroup_s, num_units, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const trigger_fields[] = {
    F(gtrigger_s, disabled, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const timer_fields[] = {
    F(saveTimer_s, duration, F_INT),
    F(saveTimer_s, remaining, F_INT),
    F(saveTimer_s, periodic, F_INT),
    F(saveTimer_s, paused, F_INT),
    F(saveTimer_s, running, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const quest_fields[] = {
    F(gquest_s, discovered, F_INT),
    F(gquest_s, required, F_INT),
    F(gquest_s, completed, F_INT),
    F(gquest_s, failed, F_INT),
    F(gquest_s, enabled, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const questitem_fields[] = {
    F(gquestitem_s, completed, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

/* Every persistent and process-owned field crossing the save boundary is represented here. */
field_t fields[] = {
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
    F(edict_s, s.origin, F_VECTOR),
    F(edict_s, construction.primary_builder, F_EDICT),
    F(edict_s, rally.entity, F_EDICT),
    F(edict_s, revival.producer, F_EDICT),
    F(edict_s, revival.queue_next, F_EDICT),
    F(edict_s, goldmine.mine, F_EDICT),
    F(edict_s, inventory, F_EDICT, MAX_INVENTORY, FIELD_NONE),
    F(edict_s, cargo.units, F_EDICT, MAX_CARGO, FIELD_NONE),
    F(edict_s, item.carrier, F_EDICT),
    F(edict_s, ground_next, F_EDICT),
    F(edict_s, movement.attackmove_waypoint, F_EDICT),
    F(edict_s, movement.patrol_a, F_EDICT),
    F(edict_s, movement.patrol_b, F_EDICT),
    F(edict_s, movement.patrol_target, F_EDICT),
    F(edict_s, movement.follow_target, F_EDICT),
    F(edict_s, goalentity, F_EDICT),
    F(edict_s, combatentity, F_EDICT),
    F(edict_s, secondarygoal, F_EDICT),
    F(edict_s, owner, F_EDICT),
    F(edict_s, build, F_EDICT),
    F(edict_s, client, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, area.prev, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, area.next, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, destructable.alive_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, destructable.death_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, destructable.drop_sets, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, destructable.drop_sets_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, added_abilities, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, added_abilities_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, removed_abilities, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, removed_abilities_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, permanent_abilities, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, permanent_abilities_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, animation, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, currentmove, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, militia.partner, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, militia.partner_spawn_time, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, militia.returning, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, stand, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, birth, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, prethink, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, think, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, die, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, idle, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, move, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, run, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, attack, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, pain, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitProfile, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitBalance, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitData, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitUI, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitWeapons, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, UnitAbilities, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, Doodads, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, ItemData, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, DestructableData, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, ps.name, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, ps.texts, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, mapplayer, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, menu.on_entity_selected, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, menu.on_location_selected, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, menu.cmdbutton, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, menu.refresh, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, camera.target_controller, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    F(client_s, rally_indicator, F_IGNORE, 0, FIELD_RUNTIME | FIELD_CLIENT),
    { NULL, 0, 0, 0, 0, 0 }
};

static void ClearRuntimeFields(void *object, DWORD flags) {
    for (field_t const *field = fields; field->name; field++)
        if (field->flags == flags) memset((BYTE *)object + field->ofs, 0, field->size);
}

static BOOL SaveBytes(FILE *f, LPCVOID data, size_t size) { return fwrite(data, 1, size, f) == size; }
static BOOL LoadBytes(FILE *f, void *data, size_t size) { return fread(data, 1, size, f) == size; }
static BOOL WriteJassBytes(void *context, void *data, DWORD size) { return SaveBytes(context, data, size); }
static BOOL ReadJassBytes(void *context, void *data, DWORD size) { return LoadBytes(context, data, size); }
static BOOL WriteMappedFields(FILE *f, field_t const *fields, BYTE *base);
static BOOL ReadMappedFields(FILE *f, field_t const *fields, BYTE *base);
static BOOL WriteString(FILE *f, LPCSTR text);
static BOOL ReadString(FILE *f, LPSTR *text);
static DWORD EventCount(void);

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
    FOR_LOOP(i, MAX_JASS_GROUPS) { if (save_groups[i]) jass_free(save_groups[i]); save_groups[i] = NULL; }
    FOR_LOOP(i, MAX_JASS_TIMERS) { if (save_timers[i]) jass_free(save_timers[i]); save_timers[i] = NULL; }
    FOR_LOOP(i, MAX_JASS_TRIGGERS) {
        if (!save_triggers[i]) continue;
        DELETE_LIST(TRIGGERACTION, save_triggers[i]->actions, gi.MemFree);
        DELETE_LIST(TRIGGERCONDITION, save_triggers[i]->conditions, gi.MemFree);
        jass_free(save_triggers[i]); save_triggers[i] = NULL;
    }
}

static BOOL RestoreRegistrySlots(DWORD groups, DWORD timers, DWORD triggers, DWORD events) {
    if (groups < level.num_groups || timers < level.num_timers || triggers < level.num_triggers ||
        events < EventCount() || groups > MAX_JASS_GROUPS || timers > MAX_JASS_TIMERS ||
        triggers > MAX_JASS_TRIGGERS || events > MAX_EVENTS)
        return false;
    while (level.num_groups < groups) {
        DWORD i = level.num_groups;
        ggroup_t *group = jass_alloc(sizeof(*group));
        if (!group || !G_RegisterJassGroup(group)) return false;
        memset(group, 0, sizeof(*group)); save_groups[i] = group;
    }
    while (level.num_timers < timers) {
        DWORD i = level.num_timers;
        LPGTIMER timer = jass_alloc(sizeof(*timer));
        if (!timer || !G_RegisterJassTimer(timer)) return false;
        memset(timer, 0, sizeof(*timer)); save_timers[i] = timer;
    }
    while (level.num_triggers < triggers) {
        DWORD i = level.num_triggers;
        LPTRIGGER trigger = jass_alloc(sizeof(*trigger));
        if (!trigger || !G_RegisterJassTrigger(trigger)) return false;
        memset(trigger, 0, sizeof(*trigger)); save_triggers[i] = trigger;
    }
    while (EventCount() < events) if (!G_MakeEvent(0)) return false;
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

BOOL G_RegisterJassGroup(ggroup_t *group) {
    if (!group || level.num_groups >= MAX_JASS_GROUPS) return false;
    level.groups[level.num_groups++] = group;
    return true;
}

BOOL G_RegisterJassTrigger(LPTRIGGER trigger) {
    if (!trigger || level.num_triggers >= MAX_JASS_TRIGGERS) return false;
    level.triggers[level.num_triggers++] = trigger;
    return true;
}

static DWORD QuestCount(void) {
    DWORD count = 0;
    FOR_EACH_QUEST(quest) count++;
    return count;
}

static DWORD EventCount(void) {
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
    FOR_LOOP(i, level.num_triggers) if (level.triggers[i] == value) { *id = i; return true; }
    return false;
}

static BOOL TimerIndex(LPGTIMER value, DWORD *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    FOR_LOOP(i, level.num_timers) if (level.timers[i] == value) { *id = i; return true; }
    return false;
}

static BOOL WriteEvents(FILE *f) {
    DWORD handlers = EventCount(), queued = level.events.write - level.events.read;
    if (!SaveBytes(f, &handlers, sizeof(handlers))) return false;
    FOR_EACH_EVENT(event) if (!WriteMappedFields(f, save_event_fields, (BYTE *)event)) return false;
    if (queued > MAX_EVENT_QUEUE || !SaveBytes(f, &queued, sizeof(queued))) return false;
    FOR_LOOP(i, queued) {
        GAMEEVENT const *event = &level.events.queue[(level.events.read + i) % MAX_EVENT_QUEUE];
        if (!WriteMappedFields(f, save_game_event_fields, (BYTE *)event)) return false;
    }
    return true;
}

static BOOL ReadEvents(FILE *f) {
    DWORD handlers, queued;
    if (!LoadBytes(f, &handlers, sizeof(handlers)) || handlers != EventCount()) return false;
    FOR_EACH_EVENT(event)
        if (!ReadMappedFields(f, save_event_fields, (BYTE *)event)) return false;
    if (!LoadBytes(f, &queued, sizeof(queued)) || queued > MAX_EVENT_QUEUE) return false;
    level.events.read = 0; level.events.write = queued;
    FOR_LOOP(i, queued) {
        GAMEEVENT *item = level.events.queue + i;
        if (!ReadMappedFields(f, save_game_event_fields, (BYTE *)item)) return false;
    }
    return true;
}

static BOOL WriteGroups(FILE *f) {
    if (!SaveBytes(f, &level.num_groups, sizeof(level.num_groups))) return false;
    FOR_LOOP(i, level.num_groups) {
        ggroup_t const *group = level.groups[i];
        SAVE_GROUP save = { .num_units = group ? group->num_units : 0 };
        if (!group || group->num_units > MAX_GROUP_SIZE || !WriteMappedFields(f, group_fields, (BYTE *)&save)) return false;
        FOR_LOOP(k, group->num_units) {
            int index = group->units[k] ? (int)(group->units[k] - g_edicts) : -1;
            if (index < 0 || index >= (int)globals.num_edicts || !SaveBytes(f, &index, sizeof(index))) return false;
        }
    }
    return true;
}

static BOOL ReadGroups(FILE *f) {
    DWORD count;
    if (!LoadBytes(f, &count, sizeof(count)) || count != level.num_groups) {
        fprintf(stderr, "WC3 LoadGame: JASS group count does not match initialized script\n"); return false;
    }
    FOR_LOOP(i, count) {
        ggroup_t *group = level.groups[i];
        SAVE_GROUP save;
        DWORD units;
        if (!group || !ReadMappedFields(f, group_fields, (BYTE *)&save) || (units = save.num_units) > MAX_GROUP_SIZE) return false;
        group->num_units = 0;
        FOR_LOOP(k, units) {
            int index;
            if (!LoadBytes(f, &index, sizeof(index))) return false;
            if (index < 0 || index >= (int)globals.num_edicts || !g_edicts[index].inuse) {
                fprintf(stderr, "WC3 LoadGame: dropping stale group[%u] member index=%d\n", i, index);
                continue;
            }
            group->units[group->num_units++] = g_edicts + index;
        }
    }
    return true;
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

static BOOL WriteTriggers(FILE *f) {
    if (!SaveBytes(f, &level.num_triggers, sizeof(level.num_triggers))) return false;
    FOR_LOOP(i, level.num_triggers) {
        LPTRIGGER trigger = level.triggers[i];
        if (!trigger || !WriteMappedFields(f, trigger_fields, (BYTE *)trigger) ||
            !WriteTriggerCodeList(f, trigger->actions) ||
            !WriteTriggerCodeList(f, (TRIGGERACTION *)trigger->conditions)) return false;
    }
    return true;
}

static BOOL ReadTriggers(FILE *f) {
    DWORD count;
    if (!LoadBytes(f, &count, sizeof(count)) || count != level.num_triggers) {
        fprintf(stderr, "WC3 LoadGame: JASS trigger count does not match initialized script\n"); return false;
    }
    FOR_LOOP(i, count) {
        LPTRIGGER trigger = level.triggers[i];
        if (!trigger || !ReadMappedFields(f, trigger_fields, (BYTE *)trigger) ||
            !ReadTriggerCodeList(f, &trigger->actions) ||
            !ReadTriggerCodeList(f, (TRIGGERACTION **)&trigger->conditions)) return false;
    }
    return true;
}

static BOOL WriteTimers(FILE *f) {
    if (!SaveBytes(f, &level.num_timers, sizeof(level.num_timers))) return false;
    FOR_LOOP(i, level.num_timers) {
        LPGTIMER timer = level.timers[i];
        if (!timer) return false;
        SAVE_TIMER save = { .duration = timer->duration, .remaining = G_TimerRemaining(timer), .periodic = timer->periodic,
            .paused = timer->paused, .running = timer->running };
        if (!WriteMappedFields(f, timer_fields, (BYTE *)&save) || !WriteString(f, jass_functionname(timer->handler))) return false;
    }
    return true;
}

static BOOL ReadTimers(FILE *f) {
    DWORD count;
    if (!LoadBytes(f, &count, sizeof(count)) || count != level.num_timers) {
        fprintf(stderr, "WC3 LoadGame: JASS timer count does not match initialized script\n"); return false;
    }
    FOR_LOOP(i, count) {
        LPGTIMER timer = level.timers[i];
        LPSTR handler = NULL;
        SAVE_TIMER save;
        if (!timer || !ReadMappedFields(f, timer_fields, (BYTE *)&save) ||
            !ReadString(f, &handler)) { free(handler); return false; }
        timer->duration = save.duration; timer->remaining = save.remaining; timer->periodic = save.periodic;
        timer->paused = save.paused; timer->running = save.running;
        timer->handler = handler ? jass_functionbyname(level.vm, handler) : NULL;
        if (handler && !timer->handler) { free(handler); return false; }
        free(handler);
        timer->started = gi.GetTime(); timer->timeout = timer->remaining;
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
    } else if (domain == JASS_HANDLE_TRIGGER && id < level.num_triggers) return level.triggers[id];
    else if (domain == JASS_HANDLE_TIMER && id < level.num_timers) return level.timers[id];
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
        FOR_LOOP(i, level.num_groups) if (level.groups[i] == value) { *id = i; return true; }
        return false;
    }
    if (domain == JASS_HANDLE_TIMER) {
        FOR_LOOP(i, level.num_timers) if (level.timers[i] == value) { *id = i; return true; }
        return false;
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
    FOR_LOOP(i, level.num_triggers) if (level.triggers[i] == value) { *id = i; return true; }
    return false;
}

HANDLE G_LoadJassHandle(LPCSTR type, DWORD id) {
    jassHandleDomain_t domain;
    if (!JassHandleDomain(type, &domain)) return NULL;
    if (domain == JASS_HANDLE_ENTITY) return id < globals.num_edicts && g_edicts[id].inuse ? g_edicts + id : NULL;
    if (domain == JASS_HANDLE_PLAYER) return id < (DWORD)game.max_clients ? &game.clients[id].ps : NULL;
    if (domain == JASS_HANDLE_GROUP) return id < level.num_groups ? level.groups[id] : NULL;
    if (domain == JASS_HANDLE_TIMER) return id < level.num_timers ? level.timers[id] : NULL;
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

static BOOL WriteQuests(FILE *f) {
    DWORD count = 0;

    FOR_EACH_QUEST(quest) count++;
    if (!SaveBytes(f, &count, sizeof(count))) return false;
    FOR_EACH_QUEST(quest) {
        DWORD items = 0;
        FOR_EACH_QUESTITEM(quest, item) items++;
        if (!WriteString(f, quest->title) || !WriteString(f, quest->description) || !WriteString(f, quest->iconPath) ||
            !WriteMappedFields(f, quest_fields, (BYTE *)quest) ||
            !SaveBytes(f, &items, sizeof(items))) return false;
        FOR_EACH_QUESTITEM(quest, item)
            if (!WriteString(f, item->description) || !WriteMappedFields(f, questitem_fields, (BYTE *)item)) return false;
    }
    return true;
}

static BOOL ReadQuests(FILE *f) {
    DWORD count = 0, live = 0;

    FOR_EACH_QUEST(quest) live++;
    if (!LoadBytes(f, &count, sizeof(count))) return false;
    if (count != live) {
        fprintf(stderr, "WC3 LoadGame: quest count does not match live JASS handles (%u saved, %u live)\n", count, live);
        return false;
    }
    FOR_EACH_QUEST(quest) {
        DWORD items = 0, live_items = 0;
        FOR_EACH_QUESTITEM(quest, item) live_items++;
        if (!ReadString(f, &quest->title) || !ReadString(f, &quest->description) || !ReadString(f, &quest->iconPath) ||
            !ReadMappedFields(f, quest_fields, (BYTE *)quest) ||
            !LoadBytes(f, &items, sizeof(items))) return false;
        if (items != live_items) {
            fprintf(stderr, "WC3 LoadGame: quest item count does not match live JASS handles (%u saved, %u live)\n",
                items, live_items);
            return false;
        }
        FOR_EACH_QUESTITEM(quest, item)
            if (!ReadString(f, &item->description) || !ReadMappedFields(f, questitem_fields, (BYTE *)item)) return false;
    }
    return true;
}

static BOOL WriteField1(field_t const *field, BYTE *base) {
    size_t size = field->array_size ? field->size / field->array_size : field->size;
    DWORD count = field->array_size ? field->array_size : 1;
    int index;

    if (!size) return true;
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
        case F_CLIENT: {
            LPGAMECLIENT value = *(LPGAMECLIENT *)p;
            uintptr_t ptr = (uintptr_t)value, base = (uintptr_t)game.clients;
            if (value && (ptr < base || ptr >= base + sizeof(*game.clients) * game.max_clients ||
                (ptr - base) % sizeof(*game.clients))) {
                fprintf(stderr, "WC3 SaveGame: field %s[%u] points outside client table (%p)\n", field->name, i, (void *)value);
                return false;
            }
            index = value ? (int)(value - game.clients) : -1; *(int *)p = index; break;
        }
        default: break;
        }
    }
    return true;
}

/* Restore entity and client pointers after the raw edict block is read. */
static BOOL ReadField(field_t const *field, BYTE *base) {
    size_t size = field->array_size ? field->size / field->array_size : field->size;
    DWORD count = field->array_size ? field->array_size : 1;

    if (!size) return true;
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
        case F_CLIENT:
            if (index < -1 || index >= game.max_clients) {
                fprintf(stderr, "WC3 LoadGame: field %s[%u] has invalid client index %d\n", field->name, i, index);
                return false;
            }
            *(LPGAMECLIENT *)p = index < 0 ? NULL : game.clients + index;
            break;
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
        *(LPTRIGGER *)ptr = index < 0 ? NULL : level.triggers[index]; return true;
    case F_TIMER:
        if (index >= (int)level.num_timers) return false;
        *(LPGTIMER *)ptr = index < 0 ? NULL : level.timers[index]; return true;
    case F_EVENT:
        if (index >= (int)EventCount()) return false;
        *(LPEVENT *)ptr = index < 0 ? NULL : EventById(index); return true;
    default: return false;
    }
}

/* Serialize mapped records, converting pointer-domain fields to stable indexes from their field types. */
static BOOL WriteMappedFields(FILE *f, field_t const *fields, BYTE *base) {
    for (; fields->name; fields++) {
        DWORD count = fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->type == F_EDICT || fields->type == F_ITEM || fields->type == F_TRIGGER ||
            fields->type == F_TIMER || fields->type == F_EVENT) {
            FOR_LOOP(i, count) {
                int index;
                if (!WriteMappedIndex(fields, base + fields->ofs + i * size, &index)) {
                    fprintf(stderr, "WC3 SaveGame: cannot resolve mapped field %s[%u]\n", fields->name, i); return false;
                }
                if (!SaveBytes(f, &index, sizeof(index))) return false;
            }
        } else if (!SaveBytes(f, base + fields->ofs, fields->size)) return false;
    }
    return true;
}

/* Restore mapped records, resolving pointer-domain fields from stable indexes declared by their field types. */
static BOOL ReadMappedFields(FILE *f, field_t const *fields, BYTE *base) {
    for (; fields->name; fields++) {
        DWORD count = fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->type == F_EDICT || fields->type == F_ITEM || fields->type == F_TRIGGER ||
            fields->type == F_TIMER || fields->type == F_EVENT) {
            FOR_LOOP(i, count) {
                int index;
                if (!LoadBytes(f, &index, sizeof(index))) return false;
                if (!ReadMappedIndex(fields, base + fields->ofs + i * size, index)) {
                    fprintf(stderr, "WC3 LoadGame: invalid mapped field %s[%u] index=%d\n", fields->name, i, index); return false;
                }
            }
        } else if (!LoadBytes(f, base + fields->ofs, fields->size)) return false;
    }
    return true;
}

static BOOL WriteEdict(FILE *f, LPCEDICT ent) {
    edict_t temp = *ent;
    field_t const *field;

    ClearRuntimeFields(&temp, FIELD_RUNTIME);
    for (field = fields; field->name; field++)
        if (!field->flags && !WriteField1(field, (BYTE *)&temp)) return false;
    return SaveBytes(f, &temp, sizeof(temp));
}

static BOOL WriteClient(FILE *f, LPCGAMECLIENT client) {
    GAMECLIENT temp = *client;
    int target = client->camera.target_controller ? (int)(client->camera.target_controller - g_edicts) : -1;

    /* Client pointers and callbacks are process-owned; text storage remains inline in GAMECLIENT. */
    ClearRuntimeFields(&temp, FIELD_RUNTIME | FIELD_CLIENT);
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
    for (field = fields; field->name; field++)
        if (!field->flags && !ReadField(field, (BYTE *)ent)) return false;
    /* Raw callback addresses are invalid across processes; class data determines the persistent callback family. */
    if (ent->class_id) { G_BindEntityData(ent); G_BindEntityRuntime(ent); }
    return true;
}

BOOL WriteGame(LPCSTR filename) {
    FILE *f = fopen(filename, "w+b");
    SAVEHEADER header = {
        .magic = save_magic, .version = save_version, .edict_size = sizeof(edict_t), .num_edicts = globals.num_edicts,
        .max_clients = game.max_clients, .script_identity = level.vm ? jass_programidentity(level.vm) : 0,
        .quests = QuestCount(), .groups = level.num_groups, .triggers = level.num_triggers, .timers = level.num_timers,
        .events = EventCount()
    };
    strlcpy(header.map_path, level.map_path, sizeof(header.map_path));

    BOOL ok = false;
    if (!f) { fprintf(stderr, "WC3 SaveGame: cannot open %s\n", filename); return false; }
    if (!SaveBytes(f, &header, sizeof(header))) { fprintf(stderr, "WC3 SaveGame: failed at header\n"); goto done; }
    if (!SaveBytes(f, &level.framenum, sizeof(level.framenum))) { fprintf(stderr, "WC3 SaveGame: failed at framenum\n"); goto done; }
    if (!SaveBytes(f, &level.time, sizeof(level.time))) { fprintf(stderr, "WC3 SaveGame: failed at time\n"); goto done; }
    if (!SaveBytes(f, &level.timeofday, sizeof(level.timeofday))) { fprintf(stderr, "WC3 SaveGame: failed at time of day\n"); goto done; }
    if (!SaveBytes(f, &level.started, sizeof(level.started))) { fprintf(stderr, "WC3 SaveGame: failed at started\n"); goto done; }
    if (!SaveBytes(f, &level.scriptsStarted, sizeof(level.scriptsStarted))) { fprintf(stderr, "WC3 SaveGame: failed at scriptsStarted\n"); goto done; }
    if (!SaveBytes(f, &level.waypoints, sizeof(level.waypoints))) { fprintf(stderr, "WC3 SaveGame: failed at waypoint state\n"); goto done; }
    FOR_LOOP(i, game.max_clients) {
        if (!WriteClient(f, game.clients + i)) { fprintf(stderr, "WC3 SaveGame: failed at client %d\n", i); goto done; }
    }
    if (!WriteQuests(f)) { fprintf(stderr, "WC3 SaveGame: failed at quests\n"); goto done; }
    FOR_LOOP(i, globals.num_edicts) {
        BOOL used = g_edicts[i].inuse;
        if (!SaveBytes(f, &used, sizeof(used))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d inuse\n", i); goto done; }
        if (used && !SaveBytes(f, &i, sizeof(i))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d index\n", i); goto done; }
        if (used && !WriteEdict(f, g_edicts + i)) {
            fprintf(stderr, "WC3 SaveGame: failed at edict %d class=%08x\n", i, g_edicts[i].class_id); goto done;
        }
    }
    if (!WriteGroups(f)) { fprintf(stderr, "WC3 SaveGame: failed at groups\n"); goto done; }
    if (!WriteTriggers(f)) { fprintf(stderr, "WC3 SaveGame: failed at triggers\n"); goto done; }
    if (!WriteTimers(f)) { fprintf(stderr, "WC3 SaveGame: failed at timers\n"); goto done; }
    if (!WriteEvents(f)) { fprintf(stderr, "WC3 SaveGame: failed at events\n"); goto done; }
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
        else if (header.quests != QuestCount()) field = "quests";
        else if (header.groups < level.num_groups) field = "groups";
        else if (header.triggers < level.num_triggers) field = "triggers";
        else if (header.timers < level.num_timers) field = "timers";
        else if (header.events < EventCount()) field = "events";
        else if (!header.map_path[0] || strcasecmp(header.map_path, level.map_path)) field = "map_path";
        else if (!RestoreRegistrySlots(header.groups, header.timers, header.triggers, header.events)) field = "registry_slots";
        if (field) {
            fprintf(stderr, "WC3 LoadGame: header mismatch field=%s version=%u edict_size=%u/%zu edicts=%u/%u\n",
                    field, header.version, header.edict_size, sizeof(edict_t), header.num_edicts, globals.max_edicts);
            fprintf(stderr, "WC3 LoadGame: clients=%u/%u script=%u/%u quests=%u/%u groups=%u/%u triggers=%u/%u\n",
                    header.max_clients, game.max_clients, header.script_identity, script,
                    header.quests, QuestCount(), header.groups, level.num_groups, header.triggers, level.num_triggers);
            fprintf(stderr, "WC3 LoadGame: timers=%u/%u events=%u/%u map='%s'/'%s'\n",
                    header.timers, level.num_timers, header.events, EventCount(), header.map_path, level.map_path);
            fclose(f); return false;
        }
    }
    if (!LoadBytes(f, &level.framenum, sizeof(level.framenum)) || !LoadBytes(f, &level.time, sizeof(level.time)) ||
        !LoadBytes(f, &level.timeofday, sizeof(level.timeofday)) ||
        !LoadBytes(f, &level.started, sizeof(level.started)) || !LoadBytes(f, &level.scriptsStarted, sizeof(level.scriptsStarted)) ||
        !LoadBytes(f, &level.waypoints, sizeof(level.waypoints)) || level.waypoints.count > MAX_WAYPOINTS ||
        (level.waypoints.count && (level.waypoints.count != MAX_WAYPOINTS || level.waypoints.cursor >= MAX_WAYPOINTS ||
        header.num_edicts < level.waypoints.count ||
        level.waypoints.base > header.num_edicts - level.waypoints.count)) ||
        (!level.waypoints.count && (level.waypoints.base || level.waypoints.cursor))) {
        fprintf(stderr, "WC3 LoadGame: failed at level state\n"); fclose(f); return false;
    }
    FOR_LOOP(i, game.max_clients) if (!ReadClient(f, game.clients + i, targets + i)) {
        fprintf(stderr, "WC3 LoadGame: failed at client %d\n", i); fclose(f); return false;
    }
    if (!ReadQuests(f)) { fprintf(stderr, "WC3 LoadGame: failed at quests\n"); fclose(f); return false; }
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
    if (!ReadGroups(f)) { fprintf(stderr, "WC3 LoadGame: failed at groups\n"); fclose(f); return false; }
    if (!ReadTriggers(f)) { fprintf(stderr, "WC3 LoadGame: failed at triggers\n"); fclose(f); return false; }
    if (!ReadTimers(f)) { fprintf(stderr, "WC3 LoadGame: failed at timers\n"); fclose(f); return false; }
    if (!ReadEvents(f)) { fprintf(stderr, "WC3 LoadGame: failed at events\n"); fclose(f); return false; }
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
