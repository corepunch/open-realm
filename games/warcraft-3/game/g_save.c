#include "g_local.h"

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

/* Every pointer in edict_s that survives a save must be represented here. */
field_t fields[] = {
    EDICTFIELD(class_id, F_INT),
    EDICTFIELD(variation, F_INT),
    EDICTFIELD(build_project, F_INT),
    EDICTFIELD(spawn_time, F_INT),
    EDICTFIELD(harvested_lumber, F_INT),
    EDICTFIELD(harvested_gold, F_INT),
    EDICTFIELD(heatmap2, F_INT),
    EDICTFIELD(peonsinside, F_INT),
    EDICTFIELD(aiflags, F_INT),
    EDICTFIELD(damage, F_INT),
    EDICTFIELD(collision, F_FLOAT),
    EDICTFIELD(s.origin, F_VECTOR),
    EDICTFIELD(construction.primary_builder, F_EDICT),
    EDICTFIELD(rally.entity, F_EDICT),
    EDICTFIELD(revival.producer, F_EDICT),
    EDICTFIELD(revival.queue_next, F_EDICT),
    EDICTFIELD(goldmine.mine, F_EDICT),
    EDICTFIELD(inventory, F_EDICT, MAX_INVENTORY),
    EDICTFIELD(cargo.units, F_EDICT, MAX_CARGO),
    EDICTFIELD(item.carrier, F_EDICT),
    EDICTFIELD(ground_next, F_EDICT),
    EDICTFIELD(movement.attackmove_waypoint, F_EDICT),
    EDICTFIELD(movement.patrol_a, F_EDICT),
    EDICTFIELD(movement.patrol_b, F_EDICT),
    EDICTFIELD(movement.patrol_target, F_EDICT),
    EDICTFIELD(movement.follow_target, F_EDICT),
    EDICTFIELD(goalentity, F_EDICT),
    EDICTFIELD(combatentity, F_EDICT),
    EDICTFIELD(secondarygoal, F_EDICT),
    EDICTFIELD(owner, F_EDICT),
    EDICTFIELD(build, F_EDICT),
    { NULL, 0, 0, 0 }
};

typedef struct {
    DWORD ofs, size;
} runtimeField_t;

#define RUNTIMEFIELD(x) { FOFS(edict_s, x) - (HANDLE)NULL, sizeof(((edict_t *)NULL)->x) }
#define CLIENTRUNTIMEFIELD(x) { FOFS(client_s, x) - (HANDLE)NULL, sizeof(((GAMECLIENT *)NULL)->x) }

/* Process-owned edict data is one schema too: it is cleared before the raw record is written,
 * then rebuilt from class data or the live world after loading. */
static runtimeField_t const runtime_fields[] = {
    RUNTIMEFIELD(client),
    RUNTIMEFIELD(pathtex),
    RUNTIMEFIELD(area.prev),
    RUNTIMEFIELD(area.next),
    RUNTIMEFIELD(destructable.alive_pathtex),
    RUNTIMEFIELD(destructable.death_pathtex),
    RUNTIMEFIELD(destructable.drop_sets),
    RUNTIMEFIELD(destructable.drop_sets_count),
    RUNTIMEFIELD(added_abilities),
    RUNTIMEFIELD(added_abilities_count),
    RUNTIMEFIELD(removed_abilities),
    RUNTIMEFIELD(removed_abilities_count),
    RUNTIMEFIELD(permanent_abilities),
    RUNTIMEFIELD(permanent_abilities_count),
    RUNTIMEFIELD(animation),
    RUNTIMEFIELD(currentmove),
    RUNTIMEFIELD(stand),
    RUNTIMEFIELD(birth),
    RUNTIMEFIELD(prethink),
    RUNTIMEFIELD(think),
    RUNTIMEFIELD(die),
    RUNTIMEFIELD(idle),
    RUNTIMEFIELD(move),
    RUNTIMEFIELD(run),
    RUNTIMEFIELD(attack),
    RUNTIMEFIELD(pain),
    RUNTIMEFIELD(UnitProfile),
    RUNTIMEFIELD(UnitBalance),
    RUNTIMEFIELD(UnitData),
    RUNTIMEFIELD(UnitUI),
    RUNTIMEFIELD(UnitWeapons),
    RUNTIMEFIELD(UnitAbilities),
    RUNTIMEFIELD(Doodads),
    RUNTIMEFIELD(ItemData),
    RUNTIMEFIELD(DestructableData),
};

/* Client callbacks and cross-object pointers are rebuilt from live game state after the fixed copy loads. */
static runtimeField_t const client_runtime_fields[] = {
    CLIENTRUNTIMEFIELD(ps.name),
    CLIENTRUNTIMEFIELD(ps.texts),
    CLIENTRUNTIMEFIELD(mapplayer),
    CLIENTRUNTIMEFIELD(menu.on_entity_selected),
    CLIENTRUNTIMEFIELD(menu.on_location_selected),
    CLIENTRUNTIMEFIELD(menu.cmdbutton),
    CLIENTRUNTIMEFIELD(menu.refresh),
    CLIENTRUNTIMEFIELD(camera.target_controller),
    CLIENTRUNTIMEFIELD(rally_indicator),
};

static void ClearRuntimeFields(void *object, runtimeField_t const *fields, DWORD count) {
    FOR_LOOP(i, count) memset((BYTE *)object + fields[i].ofs, 0, fields[i].size);
}

static BOOL SaveBytes(FILE *f, LPCVOID data, size_t size) { return fwrite(data, 1, size, f) == size; }
static BOOL LoadBytes(FILE *f, void *data, size_t size) { return fread(data, 1, size, f) == size; }
static BOOL WriteJassBytes(void *context, void *data, DWORD size) { return SaveBytes(context, data, size); }
static BOOL ReadJassBytes(void *context, void *data, DWORD size) { return LoadBytes(context, data, size); }
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

static ggroup_t *save_groups[MAX_JASS_GROUPS];
static LPGTIMER save_timers[MAX_JASS_TIMERS];
static LPTRIGGER save_triggers[MAX_JASS_TRIGGERS];

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
        triggers > MAX_JASS_TRIGGERS || events > MAX_JASS_EVENTS)
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
    FOR_EACH_LIST(QUEST const, quest, level.quests) count++;
    return count;
}

static DWORD EventCount(void) {
    DWORD count = 0;
    FOR_EACH_LIST(EVENT const, event, level.events.handlers) count++;
    return count;
}

static BOOL EventId(LPEVENT value, DWORD *id) {
    DWORD index = 0;
    if (!value) { *id = UINT32_MAX; return true; }
    FOR_EACH_LIST(EVENT, event, level.events.handlers) {
        if (event == value) { *id = index; return true; }
        index++;
    }
    return false;
}

static LPEVENT EventById(DWORD id) {
    FOR_EACH_LIST(EVENT, event, level.events.handlers) {
        if (!id--) return event;
    }
    return NULL;
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
    FOR_EACH_LIST(EVENT, event, level.events.handlers) {
        int subject = event->subject ? (int)(event->subject - g_edicts) : -1;
        DWORD trigger, timer;
        if (subject >= (int)globals.num_edicts || !TriggerIndex(event->trigger, &trigger) || !TimerIndex(event->timer, &timer) ||
            !SaveBytes(f, &event->type, sizeof(event->type)) || !SaveBytes(f, &subject, sizeof(subject)) ||
            !SaveBytes(f, &trigger, sizeof(trigger)) || !SaveBytes(f, &timer, sizeof(timer)) ||
            !SaveBytes(f, &event->region, sizeof(event->region)) || !SaveBytes(f, &event->range, sizeof(event->range)) ||
            !SaveBytes(f, &event->state, sizeof(event->state)) || !SaveBytes(f, &event->limitop, sizeof(event->limitop)) ||
            !SaveBytes(f, &event->limitval, sizeof(event->limitval)))
            return false;
    }
    if (queued > MAX_EVENT_QUEUE || !SaveBytes(f, &queued, sizeof(queued))) return false;
    FOR_LOOP(i, queued) {
        GAMEEVENT const *event = &level.events.queue[(level.events.read + i) % MAX_EVENT_QUEUE];
        int edict = event->edict ? (int)(event->edict - g_edicts) : -1;
        int source = event->source ? (int)(event->source - g_edicts) : -1;
        DWORD response;
        if (edict >= (int)globals.num_edicts || source >= (int)globals.num_edicts || !EventId(event->responseTo, &response) ||
            !SaveBytes(f, &event->type, sizeof(event->type)) || !SaveBytes(f, &edict, sizeof(edict)) ||
            !SaveBytes(f, &source, sizeof(source)) || !SaveBytes(f, &response, sizeof(response))) return false;
    }
    return true;
}

static BOOL ReadEvents(FILE *f) {
    DWORD handlers, queued;
    LPEVENT event;
    if (!LoadBytes(f, &handlers, sizeof(handlers)) || handlers != EventCount()) return false;
    for (event = level.events.handlers; event; event = event->next) {
        int subject;
        DWORD trigger, timer;
        if (!LoadBytes(f, &event->type, sizeof(event->type)) || !LoadBytes(f, &subject, sizeof(subject)) ||
            !LoadBytes(f, &trigger, sizeof(trigger)) || !LoadBytes(f, &timer, sizeof(timer)) ||
            !LoadBytes(f, &event->region, sizeof(event->region)) || !LoadBytes(f, &event->range, sizeof(event->range)) ||
            !LoadBytes(f, &event->state, sizeof(event->state)) || !LoadBytes(f, &event->limitop, sizeof(event->limitop)) ||
            !LoadBytes(f, &event->limitval, sizeof(event->limitval)) ||
            subject < -1 || subject >= (int)globals.num_edicts ||
            (trigger != UINT32_MAX && trigger >= level.num_triggers) ||
            (timer != UINT32_MAX && timer >= level.num_timers)) return false;
        event->subject = subject < 0 ? NULL : g_edicts + subject;
        event->trigger = trigger == UINT32_MAX ? NULL : level.triggers[trigger];
        event->timer = timer == UINT32_MAX ? NULL : level.timers[timer];
    }
    if (!LoadBytes(f, &queued, sizeof(queued)) || queued > MAX_EVENT_QUEUE) return false;
    level.events.read = 0; level.events.write = queued;
    FOR_LOOP(i, queued) {
        GAMEEVENT *item = level.events.queue + i;
        int edict, source;
        DWORD response;
        if (!LoadBytes(f, &item->type, sizeof(item->type)) || !LoadBytes(f, &edict, sizeof(edict)) ||
            !LoadBytes(f, &source, sizeof(source)) || !LoadBytes(f, &response, sizeof(response)) ||
            edict < -1 || edict >= (int)globals.num_edicts || source < -1 || source >= (int)globals.num_edicts ||
            (response != UINT32_MAX && response >= EventCount())) return false;
        item->edict = edict < 0 ? NULL : g_edicts + edict;
        item->source = source < 0 ? NULL : g_edicts + source;
        item->responseTo = response == UINT32_MAX ? NULL : EventById(response);
    }
    return true;
}

static BOOL WriteGroups(FILE *f) {
    if (!SaveBytes(f, &level.num_groups, sizeof(level.num_groups))) return false;
    FOR_LOOP(i, level.num_groups) {
        ggroup_t const *group = level.groups[i];
        if (!group || group->num_units > MAX_GROUP_SIZE || !SaveBytes(f, &group->num_units, sizeof(group->num_units))) return false;
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
        DWORD units;
        if (!group || !LoadBytes(f, &units, sizeof(units)) || units > MAX_GROUP_SIZE) return false;
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
        if (!trigger || !SaveBytes(f, &trigger->disabled, sizeof(trigger->disabled)) ||
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
        if (!trigger || !LoadBytes(f, &trigger->disabled, sizeof(trigger->disabled)) ||
            !ReadTriggerCodeList(f, &trigger->actions) ||
            !ReadTriggerCodeList(f, (TRIGGERACTION **)&trigger->conditions)) return false;
    }
    return true;
}

static BOOL WriteTimers(FILE *f) {
    if (!SaveBytes(f, &level.num_timers, sizeof(level.num_timers))) return false;
    FOR_LOOP(i, level.num_timers) {
        LPGTIMER timer = level.timers[i];
        DWORD remaining = G_TimerRemaining(timer);
        if (!timer || !SaveBytes(f, &timer->duration, sizeof(timer->duration)) ||
            !SaveBytes(f, &remaining, sizeof(remaining)) || !SaveBytes(f, &timer->periodic, sizeof(timer->periodic)) ||
            !SaveBytes(f, &timer->paused, sizeof(timer->paused)) || !SaveBytes(f, &timer->running, sizeof(timer->running)) ||
            !WriteString(f, jass_functionname(timer->handler))) return false;
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
        if (!timer || !LoadBytes(f, &timer->duration, sizeof(timer->duration)) ||
            !LoadBytes(f, &timer->remaining, sizeof(timer->remaining)) ||
            !LoadBytes(f, &timer->periodic, sizeof(timer->periodic)) ||
            !LoadBytes(f, &timer->paused, sizeof(timer->paused)) || !LoadBytes(f, &timer->running, sizeof(timer->running)) ||
            !ReadString(f, &handler)) { free(handler); return false; }
        timer->handler = handler ? jass_functionbyname(level.vm, handler) : NULL;
        if (handler && !timer->handler) { free(handler); return false; }
        free(handler);
        timer->started = gi.GetTime(); timer->timeout = timer->remaining;
    }
    return true;
}

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

static BOOL JassHandleDomain(LPCSTR type, jassHandleDomain_t *domain) {
    FOR_LOOP(i, sizeof(jass_handle_domains) / sizeof(*jass_handle_domains)) {
        if (!strcmp(type, jass_handle_domains[i].type)) { *domain = jass_handle_domains[i].domain; return true; }
    }
    return false;
}

static HANDLE JassListHandle(jassHandleDomain_t domain, DWORD id) {
    DWORD index = 0;
    if (domain == JASS_HANDLE_QUEST) {
        FOR_EACH_LIST(QUEST, quest, level.quests) if (index++ == id) return quest;
    } else if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_LIST(QUEST, quest, level.quests)
            FOR_EACH_LIST(QUESTITEM, item, quest->items) if (index++ == id) return item;
    } else if (domain == JASS_HANDLE_EVENT) {
        FOR_EACH_LIST(EVENT, event, level.events.handlers) {
            if (index++ != id) continue;
            return event;
        }
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
        FOR_EACH_LIST(QUEST, quest, level.quests) { if (quest == value) { *id = index; return true; } index++; }
        return false;
    }
    if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_LIST(QUEST, quest, level.quests)
            FOR_EACH_LIST(QUESTITEM, item, quest->items) { if (item == value) { *id = index; return true; } index++; }
        return false;
    }
    if (domain == JASS_HANDLE_EVENT) {
        FOR_EACH_LIST(EVENT, event, level.events.handlers) {
            if (event == value) { *id = index; return true; }
            index++;
        }
        return false;
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

    FOR_EACH_LIST(QUEST const, quest, level.quests) count++;
    if (!SaveBytes(f, &count, sizeof(count))) return false;
    FOR_EACH_LIST(QUEST const, quest, level.quests) {
        DWORD items = 0;
        FOR_EACH_LIST(QUESTITEM const, item, quest->items) items++;
        if (!WriteString(f, quest->title) || !WriteString(f, quest->description) || !WriteString(f, quest->iconPath) ||
            !SaveBytes(f, &quest->discovered, sizeof(quest->discovered)) ||
            !SaveBytes(f, &quest->required, sizeof(quest->required)) ||
            !SaveBytes(f, &quest->completed, sizeof(quest->completed)) ||
            !SaveBytes(f, &quest->failed, sizeof(quest->failed)) || !SaveBytes(f, &quest->enabled, sizeof(quest->enabled)) ||
            !SaveBytes(f, &items, sizeof(items))) return false;
        FOR_EACH_LIST(QUESTITEM const, item, quest->items)
            if (!WriteString(f, item->description) || !SaveBytes(f, &item->completed, sizeof(item->completed))) return false;
    }
    return true;
}

static BOOL ReadQuests(FILE *f) {
    DWORD count = 0, live = 0;

    FOR_EACH_LIST(QUEST const, quest, level.quests) live++;
    if (!LoadBytes(f, &count, sizeof(count))) return false;
    if (count != live) {
        fprintf(stderr, "WC3 LoadGame: quest count does not match live JASS handles (%u saved, %u live)\n", count, live);
        return false;
    }
    FOR_EACH_LIST(QUEST, quest, level.quests) {
        DWORD items = 0, live_items = 0;
        FOR_EACH_LIST(QUESTITEM const, item, quest->items) live_items++;
        if (!ReadString(f, &quest->title) || !ReadString(f, &quest->description) || !ReadString(f, &quest->iconPath) ||
            !LoadBytes(f, &quest->discovered, sizeof(quest->discovered)) ||
            !LoadBytes(f, &quest->required, sizeof(quest->required)) ||
            !LoadBytes(f, &quest->completed, sizeof(quest->completed)) ||
            !LoadBytes(f, &quest->failed, sizeof(quest->failed)) || !LoadBytes(f, &quest->enabled, sizeof(quest->enabled)) ||
            !LoadBytes(f, &items, sizeof(items))) return false;
        if (items != live_items) {
            fprintf(stderr, "WC3 LoadGame: quest item count does not match live JASS handles (%u saved, %u live)\n",
                items, live_items);
            return false;
        }
        FOR_EACH_LIST(QUESTITEM, item, quest->items)
            if (!ReadString(f, &item->description) || !LoadBytes(f, &item->completed, sizeof(item->completed))) return false;
    }
    return true;
}

/* Convert entity and client pointers to stable save-file indexes. */
static size_t field_size(fieldtype_t type) {
    switch (type) {
    case F_INT: return sizeof(int);
    case F_FLOAT: return sizeof(float);
    case F_VECTOR: return sizeof(VECTOR3);
    case F_EDICT: return sizeof(LPEDICT);
    case F_CLIENT: return sizeof(LPGAMECLIENT);
    default: return 0;
    }
}

static BOOL WriteField1(field_t const *field, BYTE *base) {
    size_t size = field_size(field->type);
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
    size_t size = field_size(field->type);
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

static BOOL WriteEdict(FILE *f, LPCEDICT ent) {
    edict_t temp = *ent;
    field_t const *field;

    ClearRuntimeFields(&temp, runtime_fields, sizeof(runtime_fields) / sizeof(*runtime_fields));
    for (field = fields; field->name; field++) if (!WriteField1(field, (BYTE *)&temp)) return false;
    return SaveBytes(f, &temp, sizeof(temp));
}

static BOOL WriteClient(FILE *f, LPCGAMECLIENT client) {
    GAMECLIENT temp = *client;
    int target = client->camera.target_controller ? (int)(client->camera.target_controller - g_edicts) : -1;

    /* Client pointers and callbacks are process-owned; text storage remains inline in GAMECLIENT. */
    ClearRuntimeFields(&temp, client_runtime_fields, sizeof(client_runtime_fields) / sizeof(*client_runtime_fields));
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
    for (field = fields; field->name; field++) if (!ReadField(field, (BYTE *)ent)) return false;
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
