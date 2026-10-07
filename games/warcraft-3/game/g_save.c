#include "g_local.h"
#include "../common/wc3_pathing_records.h"
#include "../common/wc3_pathing_regions.h"
#include "../common/wc3_pathing_cell.h"
#include <stdint.h>
#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);
void setup_test_world(void);
#endif

typedef enum {
    F_INT,
    F_FLOAT,
    F_LSTRING,            // string on disk, pointer in memory, TAG_LEVEL
    F_GSTRING,            // string on disk, pointer in memory, TAG_GAME
    F_VECTOR,
    F_REGION,
    F_REGION_REGISTRY,
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
    cstring_t name;
    uint32_t ofs;
    fieldtype_t type;
    size_t size;
    uint32_t array_size;
    uintptr_t flags; /* field flags, or child schema pointer for F_STRUCT */
    uint32_t count_ofs;
} field_t;

typedef struct {
    field_t const *fields;
    uint32_t read_ofs, write_ofs;
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
#define F_METADATA_F_REGION_REGISTRY(count, schema) count, (uintptr_t)(schema)
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
#define F(TYPE, x, kind, ...) { #x, offsetof(struct TYPE, x), kind, sizeof(((struct TYPE *)NULL)->x), F_METADATA(kind, ##__VA_ARGS__), UINT32_MAX }
#define TF(TYPE, x, kind, ...) { #x, offsetof(TYPE, x), kind, sizeof(((TYPE *)NULL)->x), F_METADATA(kind, ##__VA_ARGS__), UINT32_MAX }
#define FC(TYPE, x, kind, count, schema, count_field) { #x, offsetof(struct TYPE, x), kind, sizeof(((struct TYPE *)NULL)->x), count, (uintptr_t)(schema), offsetof(struct TYPE, count_field) }
#define FR(TYPE, x, count, ring) { #x, offsetof(struct TYPE, x), F_STRUCT_RING, sizeof(((struct TYPE *)NULL)->x), count, (uintptr_t)(ring), UINT32_MAX }
#define TFC(TYPE, x, kind, count, count_field) { #x, offsetof(TYPE, x), kind, sizeof(((TYPE *)NULL)->x), count, 0, offsetof(TYPE, count_field) }

enum {
    FIELD_NONE,
    FIELD_RUNTIME = 1 << 0,
};

static uint32_t const save_magic = MAKEFOURCC('W', '3', 'S', 'V');
static uint32_t const save_commit = MAKEFOURCC('W', '3', 'O', 'K');
/* Format125 retains support flags between height selection and the next
 * refresh. Reconstructing deep water from the position loses AMPH's lag. */
static uint32_t const save_version = 125;
#define MAX_SAVE_STRING (1u << 20) // bytes; bounds quest-string allocations from corrupt saves
#define MAX_SAVE_GROUP_HANDLES 65536u // corrupt-save bound only; runtime group registry itself grows dynamically
#define UMOVE_RELOC_RANGE (64 << 20) // bytes; every umove_t is static data in libgame, so a valid offset from the anchor stays well inside one module image

/* F_MMOVE anchor: umove_t instances are file-scope statics, so a move pointer
 * survives a save as a signed offset from a fixed symbol in the same data segment. */
static umove_t umove_reloc;

_Static_assert(sizeof(umove_t *) == 8, "F_MMOVE packs a relocation offset and a validation hash into the pointer field");
_Static_assert(sizeof(void (*)(edict_t *)) == 8, "F_CFUNCTION packs a roster index and a name hash into the pointer field");

typedef struct {
    cstring_t name;
    void *func;
} saveCFunction_t;

#define SAVE_CFUNCTION(fn) { .name = #fn, .func = (void *)(fn) }

/* The 1-based index is part of this save format. Bump the version when
 * changing roster indexes or names; incompatible saves are rejected.
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
    SAVE_CFUNCTION(human_ability_think),
    SAVE_CFUNCTION(rain_of_fire_think),
    SAVE_CFUNCTION(starfall_think),
    SAVE_CFUNCTION(death_and_decay_think),
    SAVE_CFUNCTION(tranquility_think),
    SAVE_CFUNCTION(earthquake_think),
    SAVE_CFUNCTION(whirlwind_think),
    SAVE_CFUNCTION(rain_of_chaos_think),
    SAVE_CFUNCTION(inferno_think),
    SAVE_CFUNCTION(volcano_think),
    SAVE_CFUNCTION(pocket_factory_think),
    SAVE_CFUNCTION(exhume_think),
    SAVE_CFUNCTION(stasis_trap_think),
    SAVE_CFUNCTION(dark_portal_think),
    SAVE_CFUNCTION(healing_spray_think),
    SAVE_CFUNCTION(cannibalize_think),
    SAVE_CFUNCTION(possession_two_think),
    SAVE_CFUNCTION(lsh_think),
    SAVE_CFUNCTION(far_sight_think),
    SAVE_CFUNCTION(chain_lightning_think),
    SAVE_CFUNCTION(mass_teleport_think),
    SAVE_CFUNCTION(divine_shield_think),
    SAVE_CFUNCTION(unsummon_think),
    /* New callbacks must only be appended: these indices are serialized. */
    SAVE_CFUNCTION(graveyard_think),
    SAVE_CFUNCTION(corpse_cargo_approach_think),
    SAVE_CFUNCTION(cannibalize_approach_think),
    SAVE_CFUNCTION(incinerate_explode_think),
    SAVE_CFUNCTION(monsoon_think),
    SAVE_CFUNCTION(S_SpellTargetApproachThink),
    SAVE_CFUNCTION(land_mine_think),
    SAVE_CFUNCTION(death_damage_aoe_think),
    SAVE_CFUNCTION(reincarnation_think),
    SAVE_CFUNCTION(acid_bomb_think),
    SAVE_CFUNCTION(morph_end),
};

static int SaveCFunctionIndex(void *func) {
    if (!func) return 0;
    FOR_LOOP(i, sizeof(save_cfunctions) / sizeof(save_cfunctions[0]))
        if (save_cfunctions[i].func == func) return (int)i + 1;
    return -1;
}

typedef struct {
    uint32_t magic, version, edict_size, num_edicts, max_clients;
    uint32_t script_identity, quests, groups, triggers, timers, events;
    PATHSTR map_path;
} saveHeader_t;

typedef struct { uint32_t checksum, commit; } saveFooter_t;

typedef enum {
    JASS_HANDLE_ENTITY,
    JASS_HANDLE_PLAYER,
    JASS_HANDLE_QUEST,
    JASS_HANDLE_QUESTITEM,
    JASS_HANDLE_EVENT,
    JASS_HANDLE_TRIGGER,
    JASS_HANDLE_GROUP,
    JASS_HANDLE_TIMER,
    JASS_HANDLE_TIMERDIALOG,
    JASS_HANDLE_LEADERBOARD,
    JASS_HANDLE_MULTIBOARD,
    JASS_HANDLE_MULTIBOARDITEM,
    JASS_HANDLE_TEXTTAG,
    JASS_HANDLE_HASHTABLE,
    JASS_HANDLE_WEATHER,
    JASS_HANDLE_LIGHTNING,
    JASS_HANDLE_REGION,
} jassHandleDomain_t;

static struct { cstring_t type; jassHandleDomain_t domain; } const jass_handle_domains[] = {
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
    { "timerdialog", JASS_HANDLE_TIMERDIALOG },
    { "leaderboard", JASS_HANDLE_LEADERBOARD },
    { "multiboard", JASS_HANDLE_MULTIBOARD },
    { "multiboarditem", JASS_HANDLE_MULTIBOARDITEM },
    { "texttag", JASS_HANDLE_TEXTTAG },
    { "hashtable", JASS_HANDLE_HASHTABLE },
    { "weathereffect", JASS_HANDLE_WEATHER },
    { "lightning", JASS_HANDLE_LIGHTNING },
    { "region", JASS_HANDLE_REGION },
};

static field_t const timer_dialog_fields[] = {
    F(gtimerdialog_s, timer, F_TIMER, 0, FIELD_NONE),
    F(gtimerdialog_s, inuse, F_INT),
    F(gtimerdialog_s, title_set, F_INT),
    F(gtimerdialog_s, title_color_set, F_INT),
    F(gtimerdialog_s, time_color_set, F_INT),
    F(gtimerdialog_s, visible_clients, F_INT),
    F(gtimerdialog_s, title_color, F_INT),
    F(gtimerdialog_s, time_color, F_INT),
    F(gtimerdialog_s, title, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const weather_fields[] = {
    TF(gweather_t, inuse, F_INT),
    TF(gweather_t, enabled, F_INT),
    TF(gweather_t, handle_id, F_INT),
    TF(gweather_t, effect_id, F_INT),
    TF(gweather_t, bounds, F_VECTOR),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const lightning_state_fields[] = {
    TF(lightningEffect_t, handle, F_INT),
    TF(lightningEffect_t, effect_id, F_INT),
    TF(lightningEffect_t, source, F_VECTOR),
    TF(lightningEffect_t, target, F_VECTOR),
    TF(lightningEffect_t, color, F_INT),
    TF(lightningEffect_t, start_time, F_INT),
    TF(lightningEffect_t, end_time, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const lightning_fields[] = {
    TF(gLightning_t, inuse, F_INT),
    TF(gLightning_t, state, F_STRUCT, 1, lightning_state_fields),
    TF(gLightning_t, source_entity, F_EDICT, 0, FIELD_NONE),
    TF(gLightning_t, source_spawn_time, F_INT),
    TF(gLightning_t, target_entity, F_EDICT, 0, FIELD_NONE),
    TF(gLightning_t, target_spawn_time, F_INT),
    TF(gLightning_t, script_color, F_FLOAT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const save_event_fields[] = {
    F(gevent_s, type, F_INT),
    F(gevent_s, subject, F_EDICT, 0, FIELD_NONE),
    F(gevent_s, subject_spawn_time, F_INT),
    F(gevent_s, subject_spawn_tracked, F_INT),
    F(gevent_s, trigger, F_TRIGGER, 0, FIELD_NONE),
    F(gevent_s, timer, F_TIMER, 0, FIELD_NONE),
    F(gevent_s, filter, F_FUNCTION),
    F(gevent_s, region, F_REGION),
    F(gevent_s, range, F_FLOAT),
    F(gevent_s, state, F_INT),
    F(gevent_s, limitop, F_INT),
    F(gevent_s, limitval, F_FLOAT),
    F(gevent_s, variable, F_LSTRING),
    F(gevent_s, inuse, F_INT),
    F(gevent_s, handle_generation, F_INT),
    F(gevent_s, generation_exhausted, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const box2_fields[] = {
    TF(box2_t, min, F_VECTOR), TF(box2_t, max, F_VECTOR),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const region_fields[] = {
    F(gregion_s, rects, F_STRUCT, MAX_REGION_SIZE, box2_fields),
    F(gregion_s, num_rects, F_INT), F(gregion_s, inuse, F_INT),
    F(gregion_s, generation, F_INT), F(gregion_s, exhausted, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const save_game_event_fields[] = {
    F(gameevent_s, type, F_INT),
    F(gameevent_s, edict, F_EDICT, 0, FIELD_NONE),
    F(gameevent_s, edict_spawn_time, F_INT),
    F(gameevent_s, edict_spawn_tracked, F_INT),
    F(gameevent_s, source, F_EDICT, 0, FIELD_NONE),
    F(gameevent_s, source_spawn_time, F_INT),
    F(gameevent_s, source_spawn_tracked, F_INT),
    F(gameevent_s, value, F_INT),
    F(gameevent_s, point, F_VECTOR),
    F(gameevent_s, has_point, F_INT),
    F(gameevent_s, responseTo, F_EVENT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const group_fields[] = {
    /* handle_id is runtime identity derived from the table ordinal and is not serialized. */
    TF(ggroup_t, inuse, F_INT),
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
    F(gtimer_s, scalar_timeout, F_FLOAT),
    F(gtimer_s, scalar_timing, F_INT),
    F(gtimer_s, scalar_period, F_FLOAT),
    F(gtimer_s, scalar_residual, F_FLOAT),
    F(gtimer_s, scalar_paused_remaining, F_FLOAT),
    F(gtimer_s, scalar_segments, F_INT),
    F(gtimer_s, scalar_remaining_segments, F_INT),
    F(gtimer_s, scalar_sequence, F_INT),
    F(gtimer_s, scalar_segmented, F_INT),
    F(gtimer_s, scalar_resume, F_INT),
    F(gtimer_s, destroyed, F_INT),
    F(gtimer_s, destroy_pending, F_INT),
    F(gtimer_s, destroy_next, F_IGNORE, 0, FIELD_RUNTIME),
    F(gtimer_s, scalar_fired_clock.time, F_FLOAT),
    F(gtimer_s, scalar_fired_clock.epoch, F_INT),
    F(gtimer_s, scalar_fired_clock.span, F_FLOAT),
    F(gtimer_s, scalar_heap_index, F_IGNORE, 0, FIELD_RUNTIME),
    F(gtimer_s, scalar_deadline.time, F_FLOAT),
    F(gtimer_s, scalar_deadline.epoch, F_INT),
    F(gtimer_s, scalar_deadline.span, F_FLOAT),
    F(gtimer_s, duration, F_INT),
    F(gtimer_s, remaining, F_INT),
    F(gtimer_s, updated, F_INT),
    F(gtimer_s, generation, F_INT),
    F(gtimer_s, periodic, F_INT),
    F(gtimer_s, paused, F_INT),
    F(gtimer_s, running, F_INT),
    F(gtimer_s, handler, F_FUNCTION),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const leaderboard_item_fields[] = {
    F(gleaderboarditem_s, label, F_INT),
    F(gleaderboarditem_s, value, F_INT),
    F(gleaderboarditem_s, player, F_INT),
    F(gleaderboarditem_s, show_label, F_INT),
    F(gleaderboarditem_s, show_value, F_INT),
    F(gleaderboarditem_s, show_icon, F_INT),
    F(gleaderboarditem_s, label_color_set, F_INT),
    F(gleaderboarditem_s, value_color_set, F_INT),
    F(gleaderboarditem_s, label_color, F_INT),
    F(gleaderboarditem_s, value_color, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const leaderboard_fields[] = {
    F(gleaderboard_s, inuse, F_INT),
    F(gleaderboard_s, displayed_clients, F_INT),
    F(gleaderboard_s, show_label, F_INT),
    F(gleaderboard_s, show_names, F_INT),
    F(gleaderboard_s, show_values, F_INT),
    F(gleaderboard_s, show_icons, F_INT),
    F(gleaderboard_s, label_color_set, F_INT),
    F(gleaderboard_s, value_color_set, F_INT),
    F(gleaderboard_s, label_color, F_INT),
    F(gleaderboard_s, value_color, F_INT),
    F(gleaderboard_s, size_by_item_count, F_INT),
    F(gleaderboard_s, item_count, F_INT),
    F(gleaderboard_s, label, F_INT),
    FC(gleaderboard_s, items, F_STRUCT, MAX_LEADERBOARD_ITEMS, leaderboard_item_fields, item_count),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const multiboard_cell_fields[] = {
    F(gmultiboardcell_s, value, F_INT),
    F(gmultiboardcell_s, icon, F_INT),
    F(gmultiboardcell_s, width, F_FLOAT),
    F(gmultiboardcell_s, show_value, F_INT),
    F(gmultiboardcell_s, show_icon, F_INT),
    F(gmultiboardcell_s, value_color_set, F_INT),
    F(gmultiboardcell_s, value_color, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const multiboard_fields[] = {
    F(gmultiboard_s, inuse, F_INT),
    F(gmultiboard_s, displayed_clients, F_INT),
    F(gmultiboard_s, minimized_clients, F_INT),
    F(gmultiboard_s, rows, F_INT),
    F(gmultiboard_s, cols, F_INT),
    F(gmultiboard_s, title, F_INT),
    F(gmultiboard_s, cells, F_STRUCT, MAX_MULTIBOARD_CELLS, multiboard_cell_fields),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const multiboard_item_fields[] = {
    F(gmultiboarditem_s, inuse, F_INT),
    F(gmultiboarditem_s, refs, F_INT),
    F(gmultiboarditem_s, board, F_INT),
    F(gmultiboarditem_s, row, F_INT),
    F(gmultiboarditem_s, col, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const texttag_fields[] = {
    F(gtexttag_s, inuse, F_INT),
    F(gtexttag_s, visible_clients, F_INT),
    F(gtexttag_s, permanent, F_INT),
    F(gtexttag_s, height, F_FLOAT),
    F(gtexttag_s, height_offset, F_FLOAT),
    F(gtexttag_s, x, F_FLOAT),
    F(gtexttag_s, y, F_FLOAT),
    F(gtexttag_s, xvel, F_FLOAT),
    F(gtexttag_s, yvel, F_FLOAT),
    F(gtexttag_s, age, F_FLOAT),
    F(gtexttag_s, lifespan, F_FLOAT),
    F(gtexttag_s, fadepoint, F_FLOAT),
    F(gtexttag_s, color, F_INT),
    F(gtexttag_s, unit, F_EDICT, 0, FIELD_NONE),
    F(gtexttag_s, text, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

/* entries/capacity stay process-owned; WriteHashtables persists the entry payload. */
static field_t const hashtable_fields[] = {
    F(ghashtable_s, inuse, F_INT),
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
    FOFS(level_locals, events.read) - (handle_t)NULL,
    FOFS(level_locals, events.write) - (handle_t)NULL
};

static field_t const move_fine_budget_fields[] = {
    TF(moveFineBudget_t, work, F_INT),
    TF(moveFineBudget_t, countdown, F_INT),
    TF(moveFineBudget_t, count, F_INT),
    TF(moveFineBudget_t, limit, F_INT),
    TF(moveFineBudget_t, head, F_EDICT, 0, FIELD_NONE),
    TF(moveFineBudget_t, tail, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_coarse_budget_fields[] = {
    TF(moveCoarseBudget_t, work, F_INT),
    TF(moveCoarseBudget_t, countdown, F_INT),
    TF(moveCoarseBudget_t, count, F_INT),
    TF(moveCoarseBudget_t, limit, F_INT),
    TF(moveCoarseBudget_t, head, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveCoarseBudget_t, tail, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_coarse_request_fields[] = {
    TF(moveCoarseRequest_t, prev, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveCoarseRequest_t, next, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveCoarseRequest_t, sequence, F_INT, 2),
    TF(moveCoarseRequest_t, time, F_INT),
    TF(moveCoarseRequest_t, player, F_INT),
    TF(moveCoarseRequest_t, policy, F_INT),
    TF(moveCoarseRequest_t, queued, F_INT),
    TF(moveCoarseRequest_t, waiting, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const level_fields[] = {
    F(level_locals, framenum, F_INT),
    F(level_locals, time, F_INT),
    F(level_locals, repulse_head, F_EDICT, 0, FIELD_NONE),
    F(level_locals, repulse_phase, F_INT),
    F(level_locals, pathing_random.sum, F_INT),
    F(level_locals, pathing_random.index, F_INT),
    F(level_locals, pathing_counter, F_INT),
    F(level_locals, move_fine_responsive, F_INT),
    F(level_locals, move_fine_budgets, F_STRUCT, MAX_PLAYERS, move_fine_budget_fields),
    F(level_locals, move_coarse_budgets, F_STRUCT, MAX_PLAYERS*3, move_coarse_budget_fields),
    F(level_locals, move_coarse_sequence, F_INT, 2),
    F(level_locals, timer_sequence, F_INT),
    F(level_locals, pathing_owner_sequence, F_INT),
    F(level_locals, timer_clock_valid, F_INT),
    F(level_locals, timer_clock.time, F_FLOAT),
    F(level_locals, timer_clock.epoch, F_INT),
    F(level_locals, timer_clock.span, F_FLOAT),
    F(level_locals, timer_source_clock.time, F_FLOAT),
    F(level_locals, timer_source_clock.epoch, F_INT),
    F(level_locals, timer_source_clock.span, F_FLOAT),
    F(level_locals, timer_release_head, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, timer_heap, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, timer_heap_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, timer_integer_bits, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, timer_integer_top, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, pathing_clock.time, F_FLOAT),
    F(level_locals, pathing_clock.epoch, F_INT),
    F(level_locals, pathing_clock.span, F_FLOAT),
    F(level_locals, pathing_msec, F_INT),
    F(level_locals, pathing_owner_deadline.time, F_FLOAT),
    F(level_locals, pathing_owner_deadline.epoch, F_INT),
    F(level_locals, pathing_owner_deadline.span, F_FLOAT),
    F(level_locals, pathing_owner_clock_valid, F_INT),
    F(level_locals, pathing_phase, F_INT),
    F(level_locals, pathing_due, F_INT),
    F(level_locals, scheduled_frame, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, scheduled_think, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, stock.item_slots, F_INT),
    F(level_locals, stock.unit_slots, F_INT),
    F(level_locals, timeofday.elapsed, F_FLOAT),
    F(level_locals, timeofday.pending, F_FLOAT),
    F(level_locals, timeofday.pending_valid, F_INT),
    F(level_locals, timeofday.suspended, F_INT),
    F(level_locals, timeofday.false_time.hour, F_INT),
    F(level_locals, timeofday.false_time.minute, F_INT),
    F(level_locals, timeofday.false_time.ticks_remaining, F_INT),
    F(level_locals, timeofday.false_time.active, F_INT),
    F(level_locals, timeofday.false_time.initialized, F_INT),
    F(level_locals, environment_fog.active.style, F_INT),
    F(level_locals, environment_fog.active.start, F_FLOAT),
    F(level_locals, environment_fog.active.end, F_FLOAT),
    F(level_locals, environment_fog.active.density, F_FLOAT),
    F(level_locals, environment_fog.active.color, F_VECTOR),
    F(level_locals, environment_fog.defaults.style, F_INT),
    F(level_locals, environment_fog.defaults.start, F_FLOAT),
    F(level_locals, environment_fog.defaults.end, F_FLOAT),
    F(level_locals, environment_fog.defaults.density, F_FLOAT),
    F(level_locals, environment_fog.defaults.color, F_VECTOR),
    F(level_locals, environment_fog.defaults_valid, F_INT),
    F(level_locals, camera_bounds, F_VECTOR),
    F(level_locals, started, F_INT),
    F(level_locals, scriptsConfigured, F_INT),
    F(level_locals, scriptsStarted, F_INT),
    F(level_locals, ai_vm_initialized, F_INT),
    F(level_locals, pending_consumed_item_cleanup, F_INT),
    F(level_locals, waypoints.base, F_INT),
    F(level_locals, waypoints.cursor, F_INT),
    F(level_locals, waypoints.count, F_INT),
    F(level_locals, next_move_group_id, F_INT),
    F(level_locals, next_move_group_sequence, F_INT, 2),
    F(level_locals, next_move_shared_id, F_INT, 2),
    F(level_locals, next_unit_seq, F_INT, 2),
    F(level_locals, move_shared, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, move_shared_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, move_shared_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, move_groups, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, move_groups_count, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, move_group_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    F(level_locals, next_weather_id, F_INT),
    F(level_locals, weather_effects, F_STRUCT, MAX_WEATHER_EFFECTS, weather_fields),
    F(level_locals, next_lightning_id, F_INT),
    F(level_locals, lightning_effects, F_STRUCT, MAX_LIGHTNING_EFFECTS, lightning_fields),
    F(level_locals, quests, F_STRUCT, MAX_QUESTS, quest_fields),
    FC(level_locals, triggers, F_STRUCT, MAX_TRIGGERS, trigger_fields, num_triggers),
    FC(level_locals, timers, F_STRUCT, MAX_TIMERS, timer_fields, num_timers),
    F(level_locals, timer_dialogs, F_STRUCT, MAX_TIMERDIALOGS, timer_dialog_fields),
    F(level_locals, leaderboards, F_STRUCT, MAX_LEADERBOARDS, leaderboard_fields),
    F(level_locals, player_leaderboards, F_INT),
    F(level_locals, multiboards, F_STRUCT, MAX_MULTIBOARDS, multiboard_fields),
    F(level_locals, multiboard_items, F_STRUCT, MAX_MULTIBOARD_ITEMS, multiboard_item_fields),
    F(level_locals, texttags, F_STRUCT, MAX_TEXTTAGS, texttag_fields),
    F(level_locals, hashtables, F_STRUCT, MAX_HASHTABLES, hashtable_fields),
    FC(level_locals, regions, F_REGION_REGISTRY, MAX_REGIONS, region_fields, num_regions),
    F(level_locals, events.handlers, F_STRUCT, MAX_EVENTS, save_event_fields),
    FR(level_locals, events.queue, MAX_EVENT_QUEUE, &game_event_ring),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const entity_state_fields[] = {
    TF(entityState_t, origin, F_VECTOR),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const unit_info_fields[] = {
    TF(unitInfo_t, PropWindow, F_FLOAT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const artillery_fields[] = {
    TF(artillery_t, attack_type, F_INT), TF(artillery_t, area_targets, F_INT),
    TF(artillery_t, targets_allowed, F_INT),
    TF(artillery_t, area_full, F_FLOAT), TF(artillery_t, area_medium, F_FLOAT),
    TF(artillery_t, area_small, F_FLOAT), TF(artillery_t, factor_medium, F_FLOAT),
    TF(artillery_t, factor_small, F_FLOAT), { NULL, 0, 0, 0, 0, 0 }
};

static field_t const link_fields[] = {
    F(link_s, prev, F_IGNORE, 0, FIELD_RUNTIME),
    F(link_s, next, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const construction_fields[] = {
    TF(construction_t, primary_builder, F_EDICT, 0, FIELD_NONE),
    TF(construction_t, worker, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const rally_fields[] = {
    TF(rally_t, entity, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const revival_fields[] = {
    TF(revival_t, producer, F_EDICT, 0, FIELD_NONE),
    TF(revival_t, queue_next, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const sacrifice_fields[] = {
    TF(sacrifice_t, worker, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const unsummon_fields[] = {
    TF(unsummon_t, target, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const militia_fields[] = {
    TF(militia_t, partner, F_IGNORE, 0, FIELD_RUNTIME),
    TF(militia_t, partner_spawn_time, F_IGNORE, 0, FIELD_RUNTIME),
    TF(militia_t, returning, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const goldmine_fields[] = {
    TF(goldMine_t, mine, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const mineoverlay_fields[] = {
    TF(mineOverlay_t, parent, F_EDICT, 0, FIELD_NONE),
    TF(mineOverlay_t, caster, F_EDICT, 0, FIELD_NONE),
    TF(mineOverlay_t, entangle_tree, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const acolyte_mine_fields[] = {
    TF(acolyteMine_t, mine, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const item_fields[] = {
    TF(item_t, carrier, F_EDICT, 0, FIELD_NONE),
    TF(item_t, pending_use_carrier, F_EDICT, 0, FIELD_NONE),
    TF(item_t, soul_target, F_EDICT, 0, FIELD_NONE),
    TF(item_t, pending_use_carrier_spawn_time, F_INT),
    TF(item_t, pending_use_slot, F_INT),
    TF(item_t, soul_target_spawn_time, F_INT),
    /* drop_id, user_data / pawnable_* are plain values retained by the raw edict record. */
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const destructable_fields[] = {
    TF(destructable_t, blighted, F_INT),
    TF(destructable_t, alive_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    TF(destructable_t, death_pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    TF(destructable_t, drop_sets, F_IGNORE, 0, FIELD_RUNTIME),
    TF(destructable_t, drop_sets_count, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const cargo_fields[] = {
    TF(cargo_t, units, F_EDICT, MAX_CARGO, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const abilities_fields[] = {
    TFC(edictAbilities_s, added, F_INT, MAX_ABILITIES, added_count),
    TFC(edictAbilities_s, removed, F_INT, MAX_ABILITIES, removed_count),
    TFC(edictAbilities_s, permanent, F_INT, MAX_ABILITIES, permanent_count),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const avatar_fields[] = {
    TF(avatar_t, level, F_INT),
    TF(avatar_t, armor, F_FLOAT),
    TF(avatar_t, health, F_FLOAT),
    TF(avatar_t, damage, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const polymorph_fields[] = {
    TF(polymorph_t, ability, F_INT),
    TF(polymorph_t, buff, F_INT),
    TF(polymorph_t, form_type, F_INT),
    TF(polymorph_t, original_model, F_INT),
    TF(polymorph_t, original_scale, F_FLOAT),
    TF(polymorph_t, original_move_speed, F_FLOAT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const raven_fields[] = {
    TF(raven_t, fly_height, F_FLOAT),
    TF(raven_t, rise_start, F_FLOAT),
    TF(raven_t, rise_duration, F_FLOAT),
    TF(raven_t, rise_state, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const ensnare_fields[] = {
    TF(ensnare_t, adjust, F_FLOAT),
    TF(ensnare_t, height, F_FLOAT),
    TF(ensnare_t, start, F_INT),
    TF(ensnare_t, phase, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const ancient_root_fields[] = {
    TF(ancientRoot_t, destination, F_VECTOR),
    TF(ancientRoot_t, approach_goal, F_EDICT, 0, FIELD_NONE),
    TF(ancientRoot_t, approach_goal_spawn_time, F_INT),
    TF(ancientRoot_t, mode, F_INT),
    TF(ancientRoot_t, ability, F_INT),
    TF(ancientRoot_t, unit_type, F_INT),
    TF(ancientRoot_t, rooted_defense_type, F_INT),
    TF(ancientRoot_t, transition_end_time, F_INT),
    TF(ancientRoot_t, mobile_collision, F_FLOAT),
    TF(ancientRoot_t, rooted_collision, F_FLOAT),
    TF(ancientRoot_t, has_mobile_collision, F_INT),
    TF(ancientRoot_t, has_rooted_collision, F_INT),
    TF(ancientRoot_t, rooted_turning, F_INT),
    TF(ancientRoot_t, approaching, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const repulse_fields[] = {
    TF(struct edictRepulse_s, state.vector, F_VECTOR),
    TF(struct edictRepulse_s, state.packed, F_INT),
    TF(struct edictRepulse_s, next, F_EDICT, 0, FIELD_NONE),
    TF(struct edictRepulse_s, active, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const queued_order_fields[] = {
    TF(unitOrder_t, owner_context, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const unit_order_storage_fields[] = {
    TF(unitOrderStorage_t, entries, F_STRUCT, MAX_UNIT_ORDER_QUEUE, queued_order_fields),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_route_fields[] = {
    TF(moveFineRoute_t, adaptive_admission, F_STRUCT, 1, move_coarse_request_fields),
    TF(moveFineRoute_t, group_admission, F_STRUCT, 1, move_coarse_request_fields),
    TF(moveFineRoute_t, points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, count, F_INT),
    TF(moveFineRoute_t, index, F_INT),
    TF(moveFineRoute_t, mask, F_INT),
    TF(moveFineRoute_t, partial, F_INT),
    TF(moveFineRoute_t, adaptive_points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, adaptive_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, adaptive_count, F_INT),
    TF(moveFineRoute_t, adaptive_index, F_INT),
    TF(moveFineRoute_t, adaptive_goal, F_VECTOR),
    TF(moveFineRoute_t, adaptive_radius, F_FLOAT),
    TF(moveFineRoute_t, adaptive_mask, F_INT),
    TF(moveFineRoute_t, adaptive_revision, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, group_points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, group_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, group_count, F_INT),
    TF(moveFineRoute_t, group_index, F_INT),
    TF(moveFineRoute_t, group_goal, F_VECTOR),
    TF(moveFineRoute_t, group_request, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, group_geometry, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveFineRoute_t, group_radius, F_FLOAT),
    TF(moveFineRoute_t, group_mask, F_INT),
    TF(moveFineRoute_t, group_revision, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_member_fields[] = {
    TF(moveGroupMember_t, unit, F_EDICT, 0, FIELD_NONE),
    TF(moveGroupMember_t, spawn, F_INT),
    TF(moveGroupMember_t, flags, F_INT),
    TF(moveGroupMember_t, offset, F_VECTOR),
    TF(moveGroupMember_t, destination, F_VECTOR),
    TF(moveGroupMember_t, world_destination, F_VECTOR),
    TF(moveGroupMember_t, speed, F_FLOAT),
    TF(moveGroupMember_t, heading, F_FLOAT),
    TF(moveGroupMember_t, arrival_range, F_FLOAT),
    TF(moveGroupMember_t, arrived, F_INT),
    TF(moveGroupMember_t, in_range, F_INT),
    TF(moveGroupMember_t, forced_arrival, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_shared_fields[] = {
    TF(moveShared_t, id, F_INT, 2),
    TF(moveShared_t, references, F_INT),
    TF(moveShared_t, inuse, F_INT),
    TF(moveShared_t, speed, F_FLOAT),
    TF(moveShared_t, next_speed, F_FLOAT),
    TF(moveShared_t, radius, F_FLOAT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const move_group_fields[] = {
    TF(moveGroup_t, id, F_INT),
    TF(moveGroup_t, request_id, F_INT),
    TF(moveGroup_t, sequence, F_INT, 2),
    TF(moveGroup_t, shared_id, F_INT, 2),
    TF(moveGroup_t, flags, F_INT),
    TF(moveGroup_t, age, F_INT),
    TF(moveGroup_t, completion_counter, F_INT),
    TF(moveGroup_t, cooldown, F_INT),
    TF(moveGroup_t, inuse, F_INT),
    TF(moveGroup_t, initialized, F_INT),
    TF(moveGroup_t, individual, F_INT),
    TF(moveGroup_t, ticking, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveGroup_t, newer, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveGroup_t, older, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveGroup_t, slot, F_IGNORE, 0, FIELD_RUNTIME),
    TF(moveGroup_t, goal, F_VECTOR),
    TF(moveGroup_t, point, F_VECTOR),
    TF(moveGroup_t, target, F_EDICT, 0, FIELD_NONE),
    TF(moveGroup_t, target_spawn, F_INT),
    TF(moveGroup_t, target_refresh, F_INT),
    TF(moveGroup_t, heading, F_FLOAT),
    TF(moveGroup_t, radius, F_FLOAT),
    TF(moveGroup_t, route, F_STRUCT, 1, move_route_fields),
    { "members", offsetof(moveGroup_t,members), F_STRUCT, sizeof(((moveGroup_t *)0)->members), BZ_WC3_GROUP_ORDER_UNITS, (uintptr_t)move_member_fields, offsetof(moveGroup_t,count) },
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const movement_fields[] = {
    TF(struct edictMovement_s, fine_route.adaptive_admission, F_STRUCT, 1, move_coarse_request_fields),
    TF(struct edictMovement_s, fine_route.group_admission, F_STRUCT, 1, move_coarse_request_fields),
    TF(edictMovement_s, fine_prev, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, fine_next, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, fine_queued, F_INT),
    TF(edictMovement_s, formation_rank, F_INT),
    TF(edictMovement_s, fine_class, F_INT),
    TF(edictMovement_s, fine_request_time, F_INT),
    TF(edictMovement_s, adaptive_disabled, F_INT),
    TF(struct edictMovement_s, fine_route.points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.count, F_INT),
    TF(struct edictMovement_s, fine_route.index, F_INT),
    TF(struct edictMovement_s, fine_route.mask, F_INT),
    TF(struct edictMovement_s, fine_route.partial, F_INT),
    TF(struct edictMovement_s, fine_route.adaptive_points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.adaptive_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.adaptive_count, F_INT),
    TF(struct edictMovement_s, fine_route.adaptive_index, F_INT),
    TF(struct edictMovement_s, fine_route.adaptive_goal, F_VECTOR),
    TF(struct edictMovement_s, fine_route.adaptive_radius, F_FLOAT),
    TF(struct edictMovement_s, fine_route.adaptive_mask, F_INT),
    TF(struct edictMovement_s, fine_route.adaptive_revision, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.group_points, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.group_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.group_count, F_INT),
    TF(struct edictMovement_s, fine_route.group_index, F_INT),
    TF(struct edictMovement_s, fine_route.group_goal, F_VECTOR),
    TF(struct edictMovement_s, fine_route.group_request, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.group_geometry, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, fine_route.group_radius, F_FLOAT),
    TF(struct edictMovement_s, fine_route.group_mask, F_INT),
    TF(struct edictMovement_s, fine_route.group_revision, F_IGNORE, 0, FIELD_RUNTIME),
    TF(struct edictMovement_s, repulse, F_STRUCT, 1, repulse_fields),
    TF(edictMovement_s, fine_pose, F_VECTOR),
    TF(edictMovement_s, pose_valid, F_INT),
    TF(edictMovement_s, pose_world, F_VECTOR),
    TF(edictMovement_s, sampled_pose, F_VECTOR),
    TF(edictMovement_s, pose_clock.time, F_FLOAT),
    TF(edictMovement_s, pose_clock.epoch, F_INT),
    TF(edictMovement_s, pose_clock.span, F_FLOAT),
    TF(edictMovement_s, clock_valid, F_INT),
    TF(edictMovement_s, region_position, F_VECTOR),
    TF(edictMovement_s, region_valid, F_INT),
    TF(edictMovement_s, wait_delay, F_INT),
    TF(edictMovement_s, retry_count, F_INT),
    TF(edictMovement_s, point_forced_arrival, F_INT),
    TF(edictMovement_s, wait_blocker, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, group_id, F_INT),
    TF(edictMovement_s, previous_request_id, F_INT),
    /* Shared route jobs and resumable movement directions are process-local
     * caches. In particular route_resume_goal is an edict pointer, so clear
     * the complete route-resume/wait record on save and rebuild it after load. */
    TF(edictMovement_s, route_resume_direction, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_goal_origin, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_goal, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_goal_spawn, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_time, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_radius, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_flags, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_valid, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, route_resume_active, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, path_wait_active, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, path_wait_start, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, path_wait_goal_number, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, path_wait_goal_spawn, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, path_wait_origin, F_IGNORE, 0, FIELD_RUNTIME),
    TF(edictMovement_s, waygate_target, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, waygate_goal, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, attackmove_waypoint, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_a, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_b, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, patrol_target, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, follow_target, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, follow_target_spawn_time, F_INT, 0, FIELD_NONE),
    F(edictMovement_s, holding_position, F_INT),
    F(edictMovement_s, guard_position, F_VECTOR),
    F(edictMovement_s, guard_state, F_INT),
    F(edictMovement_s, explicit_allied_attack, F_INT),
    TF(edictMovement_s, cargo_unload_pending, F_INT),
    TF(edictMovement_s, cargo_unload_ability, F_INT),
    TF(edictMovement_s, cargo_unload_goal, F_EDICT, 0, FIELD_NONE),
    TF(edictMovement_s, cargo_unload_goal_spawn_time, F_INT),
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
    TF(clientMenu_s, supports_order_queue, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, order_queued, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, order_alt, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, order_queue_chained, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, ability_item, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, ability_item_spawn_time, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, dragged_item, F_IGNORE, 0, FIELD_RUNTIME),
    TF(clientMenu_s, dragged_item_spawn_time, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const client_camera_fields[] = {
    TF(clientCamera_s, target_controller, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const sleep_fields[] = {
    TF(sleep_t, can_sleep, F_INT),
    TF(sleep_t, sleeping, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const channel_fields[] = {
    TF(channel_t, code, F_INT),
    TF(channel_t, serial, F_INT),
    TF(channel_t, owner_spawn_time, F_INT),
    TF(channel_t, target_spawn_time, F_INT),
    TF(channel_t, origin, F_VECTOR),
    { NULL, 0, 0, 0, 0, 0 }
};

/* Status sources survive saves by entity index; all scalar payload/timing fields remain in the raw record. */
static field_t const status_fields[] = {
    TF(heroabilitystatus_t, source, F_EDICT, 0, FIELD_NONE),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const unit_status_fields[] = {
    TF(unitStatusStorage_t, slots, F_STRUCT, MAX_UNIT_STATUSES, status_fields),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const shop_stock_item_fields[] = {
    F(shopStockItem_s, id, F_INT),
    F(shopStockItem_s, current, F_INT),
    F(shopStockItem_s, maximum, F_INT),
    F(shopStockItem_s, delay_start, F_INT),
    F(shopStockItem_s, delay_end, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const stock_fields[] = {
    F(stock_s, item_slots, F_INT),
    F(stock_s, unit_slots, F_INT),
    F(stock_s, items_initialized, F_INT),
    FC(stock_s, items, F_STRUCT, MAX_SHOP_STOCK, shop_stock_item_fields, item_count),
    F(stock_s, units_initialized, F_INT),
    FC(stock_s, units, F_STRUCT, MAX_SHOP_STOCK, shop_stock_item_fields, unit_count),
    { NULL, 0, 0, 0, 0, 0 }
};

/* Every persistent and process-owned edict field crossing the save boundary is represented here. */
field_t edict_fields[] = {
    F(edict_s, order_queue.entries, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, scheduled_think_frame, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, construction, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, research, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, rally, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, food, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, buildwork, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, revival, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, sacrifice, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, unsummon, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, shadowmeld, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, militia, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, polymorph, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, raven, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, blight_growth, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, ensnare, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, ancient_root, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, goldmine, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, mineoverlay, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, acolyte_mine, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, item, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, destructable, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, cargo, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, stock, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, waygate, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, artillery, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, avatar, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, sleep, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, channel, F_IGNORE, 0, FIELD_RUNTIME),

    F(edict_s, class_id, F_INT),
    F(edict_s, variation, F_INT),
    F(edict_s, build_project, F_INT),
    F(edict_s, build_preview, F_EDICT, 0, FIELD_NONE),
    F(edict_s, spawn_time, F_INT),
    F(edict_s, own_seq, F_INT, 2),
    F(edict_s, summon_ability, F_INT),
    F(edict_s, permanent_invisibility_reveal_until, F_INT),
    F(edict_s, forced_visibility_count, F_INT),
    F(edict_s, shared_vision, F_INT),
    F(edict_s, harvested_lumber, F_INT),
    F(edict_s, harvested_gold, F_INT),
    F(edict_s, heatmap2, F_INT),
    F(edict_s, peonsinside, F_INT),
    F(edict_s, aiflags, F_INT),
    F(edict_s, autocast_code, F_INT),
    F(edict_s, abilstatus, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, damage, F_INT),
    F(edict_s, projectile_attack_type, F_INT),
    F(edict_s, projectile_reflected, F_INT),
    F(edict_s, collision, F_FLOAT),
    F(edict_s, attack_cooldown_active, F_INT),
    F(edict_s, attack_cooldown_remaining, F_FLOAT),
    F(edict_s, attack_cooldown_end_time, F_INT),
    F(edict_s, attack_backswing_end_time, F_INT),
    F(edict_s, unitinfo, F_STRUCT, 1, unit_info_fields),
    F(edict_s, movement.captain_home.actor, F_EDICT, 0, 0),
    F(edict_s, movement.captain_home.roster_actor, F_EDICT, 0, 0),
    F(edict_s, movement.captain_actor_members, F_INT),
    F(edict_s, movement.captain_actor_type, F_INT),
    F(edict_s, movement.captain_actor_owned, F_INT),
    F(edict_s, movement.captain_actor_siege, F_INT),
    F(edict_s, movement.captain_home.active, F_INT),
    F(edict_s, movement.captain_home.entered, F_INT),
    F(edict_s, movement.captain_home.outer, F_INT),
    F(edict_s, movement.captain_home.member_index, F_INT),
    F(edict_s, movement.captain_home.home.x, F_FLOAT),
    F(edict_s, movement.captain_home.home.y, F_FLOAT),
    F(edict_s, movement.captain_home.due.time, F_FLOAT),
    F(edict_s, movement.captain_home.due.epoch, F_INT),
    F(edict_s, movement.captain_home.due.span, F_FLOAT),
    F(edict_s, movement.type_rebind_pending, F_INT),
    F(edict_s, movement.pause_order_id, F_INT),
    F(edict_s, movement.pause_resume_pending, F_INT),
    F(edict_s, movement.pause_deadline.time, F_FLOAT),
    F(edict_s, movement.pause_deadline.epoch, F_INT),
    F(edict_s, movement.pause_deadline.span, F_FLOAT),
    F(edict_s, movement.type_rebind_deadline.time, F_FLOAT),
    F(edict_s, movement.type_rebind_deadline.epoch, F_INT),
    F(edict_s, movement.type_rebind_deadline.span, F_FLOAT),
    F(edict_s, chaos.code, F_INT),
    F(edict_s, chaos.phase, F_INT),
    F(edict_s, chaos.deadline.time, F_FLOAT),
    F(edict_s, chaos.deadline.epoch, F_INT),
    F(edict_s, chaos.deadline.span, F_FLOAT),
    F(edict_s, sound_profile, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, attack_profiles, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, attack_overrides, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, s, F_STRUCT, 1, entity_state_fields),
    F(edict_s, hero_shortcut_alert_until, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, inventory, F_EDICT, MAX_INVENTORY, FIELD_NONE),
    F(edict_s, ground_next, F_EDICT, 0, FIELD_NONE),
    F(edict_s, movement, F_STRUCT, 1, movement_fields),
    F(edict_s, current_order_id, F_INT),
    F(edict_s, goalentity, F_EDICT, 0, FIELD_NONE),
    F(edict_s, attack_target_spawn_time, F_INT),
    F(edict_s, item_drop, F_EDICT, 0, FIELD_NONE),
    F(edict_s, spell_item, F_EDICT, 0, FIELD_NONE),
    F(edict_s, soul_trap_head, F_EDICT, 0, FIELD_NONE),
    F(edict_s, soul_trap_carrier, F_EDICT, 0, FIELD_NONE),
    F(edict_s, soul_trap_next, F_EDICT, 0, FIELD_NONE),
    F(edict_s, soul_trap_item, F_EDICT, 0, FIELD_NONE),
    F(edict_s, soul_trap_head_spawn_time, F_INT),
    F(edict_s, soul_trap_carrier_spawn_time, F_INT),
    F(edict_s, soul_trap_next_spawn_time, F_INT),
    F(edict_s, soul_trap_item_spawn_time, F_INT),
    F(edict_s, soul_trap_viewer, F_INT),
    F(edict_s, soul_trapped_ability_added, F_INT),
    F(edict_s, soul_possession_added, F_INT),
    F(edict_s, combatentity, F_EDICT, 0, FIELD_NONE),
    F(edict_s, secondarygoal, F_EDICT, 0, FIELD_NONE),
    F(edict_s, owner, F_EDICT, 0, FIELD_NONE),
    F(edict_s, build, F_EDICT, 0, FIELD_NONE),
    F(edict_s, client, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, pathtex, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, area, F_STRUCT, 1, link_fields),
    F(edict_s, abilities, F_STRUCT, 1, abilities_fields),
    F(edict_s, permanent_health_bonus, F_FLOAT),
    F(edict_s, temporary_health_bonus, F_FLOAT),
    F(edict_s, temporary_mana_bonus, F_FLOAT),
    F(edict_s, mana_regen_bonus, F_FLOAT),
    F(edict_s, animation_speed, F_FLOAT),
    F(edict_s, animation_override, F_INT),
    F(edict_s, animation, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, animation_request, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, animation_props, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, currentmove, F_MMOVE),
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
    F(client_s, selection_dirty, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, cursor_signal, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, cursor_missing_reported, F_IGNORE, 0, FIELD_RUNTIME),
    /* Modal ownership is live client-window/session state. Persisting it from
     * an Esc-menu save can reload a client as paused without a live window. */
    F(client_s, modal_flags, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, quest_dialog_open, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, quest_until, F_IGNORE, 0, FIELD_RUNTIME),
    /* The window class belongs to the connecting client, which re-reports it before begin. */
    F(client_s, canvas, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, jass.disabled_abilities, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, jass.disabled_ability_capacity, F_IGNORE, 0, FIELD_RUNTIME),
    F(client_s, menu, F_STRUCT, 1, client_menu_fields),
    F(client_s, camera, F_STRUCT, 1, client_camera_fields),
    F(client_s, rally_indicator, F_IGNORE, 0, FIELD_RUNTIME),
    { NULL, 0, 0, 0, 0, 0 }
};

static void ClearRuntimeFields(void *object, field_t const *fields, uint32_t flags) {
    for (field_t const *field = fields; field->name; field++) {
        uint32_t count = field->array_size ? field->array_size : 1;
        size_t size = field->array_size ? field->size / field->array_size : field->size;
        switch (field->type) {
        case F_STRUCT:
            FOR_LOOP(i, count) ClearRuntimeFields((uint8_t *)object + field->ofs + i * size, (field_t const *)field->flags, flags);
            break;
        case F_IGNORE:
            if (field->flags == flags) memset((uint8_t *)object + field->ofs, 0, field->size);
            break;
        default: break;
        }
    }
}

static bool SaveBytes(FILE *f, void const *data, size_t size) { return fwrite(data, 1, size, f) == size; }
static bool LoadBytes(FILE *f, void *data, size_t size) { return fread(data, 1, size, f) == size; }
static bool DisabledAbilityBytes(uint32_t count, size_t *size) {
    if (!size || (count && sizeof(uint32_t) > SIZE_MAX / (size_t)count)) return false;
    *size = (size_t)count * sizeof(uint32_t);
    return true;
}
static bool WriteJassBytes(void *context, void *data, uint32_t size) { return SaveBytes(context, data, size); }
static bool ReadJassBytes(void *context, void *data, uint32_t size) { return LoadBytes(context, data, size); }
static bool WriteMappedFields(FILE *f, field_t const *fields, uint8_t *base);
static bool ReadMappedFields(FILE *f, field_t const *fields, uint8_t *base);
static bool WriteString(FILE *f, cstring_t text);
static bool ReadString(FILE *f, string_t *text);
static uint32_t ActiveEventCount(void);

static uint32_t SaveHash(uint32_t hash, void const *data, size_t size) {
    uint8_t const *bytes = data;
    while (size--) hash = (hash ^ *bytes++) * 16777619u;
    return hash;
}

/* A committed checksum rejects truncation and corruption before ReadGame mutates live state. */
static bool WriteFooter(FILE *f) {
    uint8_t bytes[4096];
    long payload;
    uint32_t checksum = 2166136261u;
    saveFooter_t footer;
    if (fflush(f) || (payload = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) return false;
    while (payload > 0) {
        size_t size = MIN((size_t)payload, sizeof(bytes));
        if (fread(bytes, 1, size, f) != size) return false;
        checksum = SaveHash(checksum, bytes, size); payload -= (long)size;
    }
    footer = (saveFooter_t){ checksum, save_commit };
    return fseek(f, 0, SEEK_END) == 0 && SaveBytes(f, &footer, sizeof(footer));
}

static bool ReadFooter(FILE *f) {
    uint8_t bytes[4096];
    long payload;
    uint32_t checksum = 2166136261u;
    saveFooter_t footer;
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
bool G_GetSaveMap(cstring_t filename, string_t map, uint32_t map_size) {
    FILE *f = fopen(filename, "rb");
    saveHeader_t header;
    uint32_t magic, version;
    if (!f || !map || !map_size) { if (f) fclose(f); return false; }
    if (!ReadFooter(f) || !LoadBytes(f, &magic, sizeof(magic)) || !LoadBytes(f, &version, sizeof(version)) ||
        magic != save_magic || version != save_version || fseek(f, 0, SEEK_SET) || !LoadBytes(f, &header, sizeof(header)) ||
        header.edict_size != sizeof(edict_t) || !header.map_path[0]) {
        fprintf(stderr, "WC3 LoadGame: invalid or incompatible save map header %s\n", filename);
        if (f) fclose(f);
        return false;
    }
    strlcpy(map, header.map_path, map_size);
    fclose(f);
    return true;
}

void G_ClearSaveRegistries(void) {
    FOR_LOOP(i, level.num_triggers) {
        DELETE_LIST(gTriggerAction_t, level.triggers[i].actions, gi.MemFree);
        DELETE_LIST(gTriggerCondition_t, level.triggers[i].conditions, gi.MemFree);
    }
}

static bool RestoreRegistrySlots(uint32_t groups, uint32_t timers, uint32_t triggers, uint32_t events) {
    if (groups < level.num_groups || timers < level.num_timers || triggers < level.num_triggers ||
        groups > MAX_SAVE_GROUP_HANDLES || timers > MAX_TIMERS ||
        triggers > MAX_TRIGGERS || events > MAX_EVENTS)
        return false;
    if (!G_EnsureJassGroupSlots(groups)) return false;
    while (level.num_timers < timers) if (!G_AllocJassTimer()) return false;
    while (level.num_triggers < triggers) if (!G_AllocJassTrigger()) return false;
    /* Event slots are a fixed serialized table. Preserve holes and retired
     * registrations from the save instead of padding the live map registry. */
    return true;
}

/* VM state follows native domains so load-side handle relocation sees restored objects. */
static bool WriteJass(FILE *f) {
    uint8_t present = level.vm != NULL;
    jassSnapshot_t snapshot = { f, WriteJassBytes };
    return SaveBytes(f, &present, sizeof(present)) && (!present || jass_writesnapshot(level.vm, &snapshot));
}

static bool ReadJass(FILE *f) {
    uint8_t present;
    jassSnapshot_t snapshot = { f, ReadJassBytes };
    if (!LoadBytes(f, &present, sizeof(present)) || present > 1 || present != (level.vm != NULL)) {
        fprintf(stderr, "WC3 LoadGame: JASS VM lifecycle does not match save\n");
        return false;
    }
    return !present || jass_readsnapshot(level.vm, &snapshot);
}

static uint32_t ActiveQuestCount(void) {
    uint32_t count = 0;
    FOR_EACH_QUEST(quest) count++;
    return count;
}

static uint32_t ActiveEventCount(void) {
    uint32_t count = 0;
    FOR_EACH_EVENT(event) count++;
    return count;
}

static bool EventId(event_t *value, uint32_t *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value >= level.events.handlers && value < level.events.handlers + MAX_EVENTS) {
        *id = (uint32_t)(value - level.events.handlers); return value->inuse;
    }
    return false;
}

static event_t *EventById(uint32_t id) {
    return id < MAX_EVENTS && level.events.handlers[id].inuse ? &level.events.handlers[id] : NULL;
}

static bool TriggerIndex(trigger_t *value, uint32_t *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value < level.triggers || value >= level.triggers + level.num_triggers) return false;
    *id = (uint32_t)(value - level.triggers); return true;
}

static bool TimerIndex(gtimer_t *value, uint32_t *id) {
    if (!value) { *id = UINT32_MAX; return true; }
    if (value < level.timers || value >= level.timers + level.num_timers) return false;
    *id = (uint32_t)(value - level.timers); return true;
}

static uint32_t TriggerCodeCount(gTriggerAction_t const *list) {
    uint32_t n = 0;
    for (; list; list = list->next) n++;
    return n;
}

static bool WriteTriggerCodeList(FILE *f, gTriggerAction_t const *list) {
    uint32_t n = TriggerCodeCount(list);
    if (!SaveBytes(f, &n, sizeof(n))) return false;
    for (; list; list = list->next) if (!WriteString(f, jass_functionname(list->func))) return false;
    return true;
}

static bool ReadTriggerCodeList(FILE *f, gTriggerAction_t **list) {
    uint32_t n;
    gTriggerAction_t **tail;
    if (!LoadBytes(f, &n, sizeof(n))) return false;
    DELETE_LIST(gTriggerAction_t, *list, gi.MemFree);
    *list = NULL;
    tail = list;
    FOR_LOOP(i, n) {
        string_t name = NULL;
        gTriggerAction_t *item = gi.MemAlloc(sizeof(*item));
        if (!item || !ReadString(f, &name)) { free(name); if (item) gi.MemFree(item); return false; }
        item->func = name ? jass_functionbyname(level.vm, name) : NULL;
        if (name && !item->func) { free(name); gi.MemFree(item); return false; }
        free(name);
        *tail = item;
        tail = &item->next;
    }
    return true;
}

static bool JassHandleDomain(cstring_t type, jassHandleDomain_t *domain) {
    FOR_LOOP(i, sizeof(jass_handle_domains) / sizeof(*jass_handle_domains)) {
        if (!strcmp(type, jass_handle_domains[i].type)) { *domain = jass_handle_domains[i].domain; return true; }
    }
    return false;
}

static handle_t JassListHandle(jassHandleDomain_t domain, uint32_t id) {
    uint32_t index = 0;
    if (domain == JASS_HANDLE_QUEST) {
        if (id < MAX_QUESTS && level.quests[id].inuse) return &level.quests[id];
    } else if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_QUEST(quest)
            FOR_EACH_QUESTITEM(quest, item) if (index++ == id) return item;
    } else if (domain == JASS_HANDLE_EVENT) {
        return EventById(id);
    } else if (domain == JASS_HANDLE_WEATHER) {
        if (id < MAX_WEATHER_EFFECTS && level.weather_effects[id].inuse) return &level.weather_effects[id];
    } else if (domain == JASS_HANDLE_LIGHTNING) {
        if (id < MAX_LIGHTNING_EFFECTS && level.lightning_effects[id].inuse) return &level.lightning_effects[id];
    } else if (domain == JASS_HANDLE_TRIGGER && id < level.num_triggers) return &level.triggers[id];
    else if (domain == JASS_HANDLE_TIMER && id < level.num_timers) return &level.timers[id];
    else if (domain == JASS_HANDLE_TIMERDIALOG && id < MAX_TIMERDIALOGS && level.timer_dialogs[id].inuse)
        return &level.timer_dialogs[id];
    else if (domain == JASS_HANDLE_LEADERBOARD && id < MAX_LEADERBOARDS && level.leaderboards[id].inuse)
        return &level.leaderboards[id];
    else if (domain == JASS_HANDLE_MULTIBOARD && id < MAX_MULTIBOARDS && level.multiboards[id].inuse)
        return &level.multiboards[id];
    else if (domain == JASS_HANDLE_MULTIBOARDITEM && id < MAX_MULTIBOARD_ITEMS && level.multiboard_items[id].inuse)
        return &level.multiboard_items[id];
    else if (domain == JASS_HANDLE_TEXTTAG && id < MAX_TEXTTAGS && level.texttags[id].inuse)
        return &level.texttags[id];
    else if (domain == JASS_HANDLE_HASHTABLE && id < MAX_HASHTABLES && level.hashtables[id].inuse)
        return &level.hashtables[id];
    return NULL;
}

/* Native pointers cross the save boundary only through stable domain-specific indexes. */
bool G_SaveJassHandle(cstring_t type, handle_t value, uint32_t *id) {
    jassHandleDomain_t domain;
    uint32_t index = 0;
    if (!JassHandleDomain(type, &domain) || !value) return false;
    if (domain == JASS_HANDLE_ENTITY) {
        edict_t *ent = value;
        uintptr_t ptr = (uintptr_t)ent, base = (uintptr_t)g_edicts;
        if (ptr < base || ptr >= base + sizeof(*g_edicts) * globals.num_edicts || (ptr - base) % sizeof(*g_edicts)) {
            fprintf(stderr, "WC3 SaveGame: %s handle %p outside edict table [%p, %p)\n", type, value,
                (void *)g_edicts, (void *)(g_edicts + globals.num_edicts));
            return false;
        }
        if (!ent->inuse || G_IsDeferredFree(ent)) {
            fprintf(stderr, "WC3 SaveGame: %s handle %p is unused edict %ld\n", type, value, (long)(ent - g_edicts));
            return false;
        }
        *id = (uint32_t)(ent - g_edicts); return true;
    }
    if (domain == JASS_HANDLE_PLAYER) {
        FOR_LOOP(i, game.max_clients) if (value == &game.clients[i].ps) { *id = i; return true; }
        return false;
    }
    if (domain == JASS_HANDLE_GROUP) {
        if (!G_JassGroupValid(value)) return false;
        return G_JassGroupIndex(value, id);
    }
    if (domain == JASS_HANDLE_TIMER) {
        return TimerIndex(value, id);
    }
    if (domain == JASS_HANDLE_TIMERDIALOG) {
        timerdialog_t *dialog = value;
        if (dialog < level.timer_dialogs || dialog >= level.timer_dialogs + MAX_TIMERDIALOGS || !dialog->inuse)
            return false;
        *id = (uint32_t)(dialog - level.timer_dialogs);
        return true;
    }
    if (domain == JASS_HANDLE_LEADERBOARD) {
        leaderboard_t *board = value;
        uintptr_t ptr = (uintptr_t)board, base = (uintptr_t)level.leaderboards;
        size_t span = sizeof(level.leaderboards);
        if (!board || ptr < base || ptr >= base + span ||
            (ptr - base) % sizeof(*board) != 0 || !board->inuse) return false;
        *id = (uint32_t)((ptr - base) / sizeof(*board));
        return true;
    }
    if (domain == JASS_HANDLE_MULTIBOARD) {
        multiboard_t *board = value;
        uintptr_t ptr = (uintptr_t)board, base = (uintptr_t)level.multiboards;
        size_t span = sizeof(level.multiboards);
        if (!board || ptr < base || ptr >= base + span ||
            (ptr - base) % sizeof(*board) != 0 || !board->inuse) return false;
        *id = (uint32_t)((ptr - base) / sizeof(*board));
        return true;
    }
    if (domain == JASS_HANDLE_MULTIBOARDITEM) {
        multiboardItem_t *item = value;
        uintptr_t ptr = (uintptr_t)item, base = (uintptr_t)level.multiboard_items;
        size_t span = sizeof(level.multiboard_items);
        if (!item || ptr < base || ptr >= base + span ||
            (ptr - base) % sizeof(*item) != 0 || !item->inuse) return false;
        *id = (uint32_t)((ptr - base) / sizeof(*item));
        return true;
    }
    if (domain == JASS_HANDLE_TEXTTAG) {
        texttag_t *tag = value;
        uintptr_t ptr = (uintptr_t)tag, base = (uintptr_t)level.texttags;
        size_t span = sizeof(level.texttags);
        if (!tag || ptr < base || ptr >= base + span ||
            (ptr - base) % sizeof(*tag) != 0 || !tag->inuse) return false;
        *id = (uint32_t)((ptr - base) / sizeof(*tag));
        return true;
    }
    if (domain == JASS_HANDLE_HASHTABLE) {
        if (!G_HashtableIndex(value, id)) return false;
        return true;
    }
    if (domain == JASS_HANDLE_WEATHER) {
        gweather_t *effect = value;
        if (effect < level.weather_effects || effect >= level.weather_effects + MAX_WEATHER_EFFECTS || !effect->inuse)
            return false;
        *id = (uint32_t)(effect - level.weather_effects);
        return true;
    }
    if (domain == JASS_HANDLE_LIGHTNING) {
        gLightning_t *effect = value;
        uintptr_t pointer = (uintptr_t)effect, base = (uintptr_t)level.lightning_effects;
        if (!effect || pointer < base || pointer >= base + sizeof(level.lightning_effects) ||
            (pointer - base) % sizeof(*effect) || !effect->inuse) return false;
        *id = (uint32_t)((pointer - base) / sizeof(*effect));
        return true;
    }
    if (domain == JASS_HANDLE_REGION) {
        region_t *region = G_RegionFromHandle(value);
        if (!region) return false;
        *id = (uint32_t)(region - level.regions);
        return true;
    }
    if (domain == JASS_HANDLE_QUEST) {
        if ((quest_t *)value >= level.quests && (quest_t *)value < level.quests + MAX_QUESTS && ((quest_t *)value)->inuse) {
            *id = (uint32_t)((quest_t *)value - level.quests); return true;
        }
        return false;
    }
    if (domain == JASS_HANDLE_QUESTITEM) {
        FOR_EACH_QUEST(quest)
            FOR_EACH_QUESTITEM(quest, item) { if (item == value) { *id = index; return true; } index++; }
        return false;
    }
    if (domain == JASS_HANDLE_EVENT) {
        event_t *event = G_EventFromHandle(value);
        return event && EventId(event, id);
    }
    return TriggerIndex(value, id);
}

handle_t G_LoadJassHandle(cstring_t type, uint32_t id) {
    jassHandleDomain_t domain;
    if (!JassHandleDomain(type, &domain)) return NULL;
    if (domain == JASS_HANDLE_ENTITY) return id < globals.num_edicts && g_edicts[id].inuse ? g_edicts + id : NULL;
    if (domain == JASS_HANDLE_PLAYER) return id < (uint32_t)game.max_clients ? &game.clients[id].ps : NULL;
    if (domain == JASS_HANDLE_GROUP) {
        ggroup_t *group = G_JassGroupByIndex(id);
        return group && group->inuse ? group : NULL;
    }
    if (domain == JASS_HANDLE_TIMER) return id < level.num_timers ? &level.timers[id] : NULL;
    if (domain == JASS_HANDLE_TIMERDIALOG)
        return id < MAX_TIMERDIALOGS && level.timer_dialogs[id].inuse ? &level.timer_dialogs[id] : NULL;
    if (domain == JASS_HANDLE_LEADERBOARD)
        return id < MAX_LEADERBOARDS && level.leaderboards[id].inuse ? &level.leaderboards[id] : NULL;
    if (domain == JASS_HANDLE_MULTIBOARD)
        return id < MAX_MULTIBOARDS && level.multiboards[id].inuse ? &level.multiboards[id] : NULL;
    if (domain == JASS_HANDLE_MULTIBOARDITEM)
        return id < MAX_MULTIBOARD_ITEMS && level.multiboard_items[id].inuse ? &level.multiboard_items[id] : NULL;
    if (domain == JASS_HANDLE_TEXTTAG)
        return id < MAX_TEXTTAGS && level.texttags[id].inuse ? &level.texttags[id] : NULL;
    if (domain == JASS_HANDLE_HASHTABLE)
        return id < MAX_HASHTABLES && level.hashtables[id].inuse ? &level.hashtables[id] : NULL;
    if (domain == JASS_HANDLE_LIGHTNING)
        return id < MAX_LIGHTNING_EFFECTS && level.lightning_effects[id].inuse ? &level.lightning_effects[id] : NULL;
    if (domain == JASS_HANDLE_REGION)
        return G_RegionHandle(id);
    if (domain == JASS_HANDLE_EVENT) {
        event_t *event = EventById(id);
        return G_EventHandle(event);
    }
    return JassListHandle(domain, id);
}

static bool WriteString(FILE *f, cstring_t text) {
    size_t size = text ? strlen(text) + 1 : 0;
    uint32_t len;

    if (size > MAX_SAVE_STRING) return false;
    len = (uint32_t)size;
    return SaveBytes(f, &len, sizeof(len)) && (!len || SaveBytes(f, text, len));
}

static bool ReadString(FILE *f, string_t *text) {
    uint32_t len;
    string_t value = NULL;

    if (!LoadBytes(f, &len, sizeof(len)) || len > MAX_SAVE_STRING) return false;
    if (len) {
        value = malloc(len);
        if (!value || !LoadBytes(f, value, len) || value[len - 1]) { free(value); return false; }
    }
    free(*text); *text = value;
    return true;
}

static bool WriteField1(field_t const *field, uint8_t *base) {
    uint32_t count = field->count_ofs != UINT32_MAX ? *(uint32_t *)(base + field->count_ofs) :
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
            edict_t *value = *(edict_t * *)p;
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
            *(int *)p = (int)((uint8_t const *)move - (uint8_t const *)&umove_reloc);
            *(uint32_t *)((uint8_t *)p + 4) = SaveHash(0, move->animation, strlen(move->animation) + 1);
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
            *(uint32_t *)((uint8_t *)p + 4) = SaveHash(0, save_cfunctions[index - 1].name, strlen(save_cfunctions[index - 1].name) + 1);
            break;
        }
        default: break;
        }
    }
    return true;
}

/* Restore entity and client pointers after the raw edict block is read. */
static bool ReadField(field_t const *field, uint8_t *base) {
    uint32_t count = field->count_ofs != UINT32_MAX ? *(uint32_t *)(base + field->count_ofs) :
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
            if (index < -1 || index >= globals.num_edicts) {
                fprintf(stderr, "WC3 LoadGame: field %s[%u] has invalid edict index %d\n", field->name, i, index);
                return false;
            }
            *(edict_t * *)p = index < 0 ? NULL : g_edicts + index;
            break;
        case F_MMOVE: {
            uint32_t hash = *(uint32_t *)((uint8_t *)p + 4);
            umove_t *move = (umove_t *)((uint8_t *)&umove_reloc + index);
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
            uint32_t hash = *(uint32_t *)((uint8_t *)p + 4);
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
static bool WriteMappedIndex(field_t const *field, void *ptr, int *index) {
    switch (field->type) {
    case F_EDICT:
    case F_ITEM: {
        edict_t *value = *(edict_t * *)ptr;
        uintptr_t addr = (uintptr_t)value, base = (uintptr_t)g_edicts;
        if (value && (addr < base || addr >= base + sizeof(*g_edicts) * globals.num_edicts ||
            (addr - base) % sizeof(*g_edicts))) return false;
        *index = value ? (int)(value - g_edicts) : -1; return true;
    }
    case F_TRIGGER: {
        uint32_t id;
        if (!TriggerIndex(*(trigger_t * *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    case F_TIMER: {
        uint32_t id;
        if (!TimerIndex(*(gtimer_t * *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    case F_EVENT: {
        uint32_t id;
        if (!EventId(*(event_t * *)ptr, &id)) return false;
        *index = id == UINT32_MAX ? -1 : (int)id; return true;
    }
    default: return false;
    }
}

/* Resolve one schema index directly into the pointer domain declared by its field type. */
static bool ReadMappedIndex(field_t const *field, void *ptr, int index) {
    if (index < -1) return false;
    switch (field->type) {
    case F_EDICT:
    case F_ITEM:
        if (index >= (int)globals.max_edicts) return false;
        *(edict_t * *)ptr = index < 0 ? NULL : g_edicts + index; return true;
    case F_TRIGGER:
        if (index >= (int)level.num_triggers) return false;
        *(trigger_t * *)ptr = index < 0 ? NULL : &level.triggers[index]; return true;
    case F_TIMER:
        if (index >= (int)level.num_timers) return false;
        *(gtimer_t * *)ptr = index < 0 ? NULL : &level.timers[index]; return true;
    case F_EVENT: {
        event_t *event;
        if (index < 0) { *(event_t * *)ptr = NULL; return true; }
        event = EventById((uint32_t)index);
        if (!event) return false;
        *(event_t * *)ptr = event; return true;
    }
    default: return false;
    }
}

/* Serialize mapped records, converting pointer-domain fields to stable indexes from their field types. */
static bool WriteMappedFields(FILE *f, field_t const *fields, uint8_t *base) {
    for (; fields->name; fields++) {
        uint32_t count = fields->count_ofs != UINT32_MAX ? *(uint32_t *)(base + fields->count_ofs) :
            fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->count_ofs != UINT32_MAX && count > fields->array_size) return false;
        if (fields->count_ofs != UINT32_MAX && !SaveBytes(f, &count, sizeof(count))) return false;
        switch (fields->type) {
        case F_REGION_REGISTRY: {
            region_t const *regions = (region_t const *)(base + fields->ofs);
            FOR_LOOP(i, count) if (!WriteMappedFields(f, (field_t const *)fields->flags,
                (uint8_t *)(regions + i))) return false;
            break;
        }
        case F_STRUCT:
            FOR_LOOP(i, count) if (!WriteMappedFields(f, (field_t const *)fields->flags, base + fields->ofs + i * size)) return false;
            break;
        case F_STRUCT_RING: {
            fieldRing_t const *ring = (fieldRing_t const *)fields->flags;
            uint32_t read = *(uint32_t *)(base + ring->read_ofs), write = *(uint32_t *)(base + ring->write_ofs);
            count = write - read;
            if (count > fields->array_size || !SaveBytes(f, &count, sizeof(count))) return false;
            FOR_LOOP(i, count) if (!WriteMappedFields(f, ring->fields,
                base + fields->ofs + ((read + i) % fields->array_size) * size)) return false;
            break;
        }
        case F_FUNCTION_LIST:
            if (!WriteTriggerCodeList(f, *(gTriggerAction_t **)(base + fields->ofs))) return false;
            break;
        case F_FUNCTION:
            if (!WriteString(f, jass_functionname(*(jassFunc_t const * *)(base + fields->ofs)))) return false;
            break;
        case F_REGION: {
            region_t *region = *(region_t * *)(base + fields->ofs);
            uint32_t id = UINT32_MAX;
            if (region && !G_SaveJassHandle("region", region, &id)) {
                fprintf(stderr, "WC3 SaveGame: cannot resolve region field %s\n", fields->name); return false;
            }
            if (!SaveBytes(f, &id, sizeof(id))) return false;
            break;
        }
        case F_LSTRING:
        case F_GSTRING:
            if (!WriteString(f, *(cstring_t *)(base + fields->ofs))) return false;
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
static bool ReadMappedFields(FILE *f, field_t const *fields, uint8_t *base) {
    for (; fields->name; fields++) {
        uint32_t count = fields->array_size ? fields->array_size : 1;
        size_t size = fields->array_size ? fields->size / fields->array_size : fields->size;
        if (fields->count_ofs != UINT32_MAX) {
            if (!LoadBytes(f, &count, sizeof(count)) || count > fields->array_size) return false;
            *(uint32_t *)(base + fields->count_ofs) = count;
        }
        switch (fields->type) {
        case F_REGION_REGISTRY: {
            region_t *regions = (region_t *)(base + fields->ofs);
            field_t const *schema = (field_t const *)fields->flags;
            memset(regions, 0, fields->size);
            FOR_LOOP(i, count) {
                if (!ReadMappedFields(f, schema, (uint8_t *)(regions + i)) || regions[i].num_rects > MAX_REGION_SIZE ||
                    regions[i].generation > REGION_HANDLE_GENERATION_MAX || regions[i].inuse > 1 || regions[i].exhausted > 1)
                    return false;
            }
            break;
        }
        case F_STRUCT:
            FOR_LOOP(i, count) if (!ReadMappedFields(f, (field_t const *)fields->flags, base + fields->ofs + i * size)) return false;
            break;
        case F_STRUCT_RING: {
            fieldRing_t const *ring = (fieldRing_t const *)fields->flags;
            if (!LoadBytes(f, &count, sizeof(count)) || count > fields->array_size) return false;
            *(uint32_t *)(base + ring->read_ofs) = 0; *(uint32_t *)(base + ring->write_ofs) = count;
            FOR_LOOP(i, count) if (!ReadMappedFields(f, ring->fields, base + fields->ofs + i * size)) return false;
            break;
        }
        case F_FUNCTION_LIST:
            if (!ReadTriggerCodeList(f, (gTriggerAction_t **)(base + fields->ofs))) return false;
            break;
        case F_FUNCTION: {
            string_t name = NULL;
            if (!ReadString(f, &name)) return false;
            *(jassFunc_t const * *)(base + fields->ofs) = name ? jass_functionbyname(level.vm, name) : NULL;
            if (name && !*(jassFunc_t const * *)(base + fields->ofs)) {
                fprintf(stderr,"WC3 LoadGame: unresolved function field %s name='%s'\n",fields->name,name);
                free(name); return false;
            }
            free(name);
            break;
        }
        case F_REGION: {
            uint32_t id;
            if (!LoadBytes(f, &id, sizeof(id))) return false;
            *(region_t * *)(base + fields->ofs) = id == UINT32_MAX ? NULL : G_LoadJassHandle("region", id);
            if (id != UINT32_MAX && !*(region_t * *)(base + fields->ofs)) return false;
            break;
        }
        case F_LSTRING:
        case F_GSTRING:
            if (!ReadString(f, (string_t *)(base + fields->ofs))) return false;
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

static bool WriteGroups(FILE *f) {
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = G_JassGroupByIndex(i);
        if (!group || !WriteMappedFields(f, group_fields, (uint8_t *)group)) {
            fprintf(stderr, "WC3 SaveGame: failed at group %u\n", (unsigned)i);
            return false;
        }
    }
    return true;
}

static bool ReadGroups(FILE *f, uint32_t count) {
    if (!G_EnsureJassGroupSlots(count)) return false;
    level.first_free_group = count;
    FOR_LOOP(i, count) {
        ggroup_t *group = G_JassGroupByIndex(i);
        if (!group || !ReadMappedFields(f, group_fields, (uint8_t *)group)) {
            fprintf(stderr, "WC3 LoadGame: failed at group %u\n", (unsigned)i);
            return false;
        }
        group->handle_id = i;
        if (!group->inuse) {
            group->num_units = 0;
            if (i < level.first_free_group) level.first_free_group = i;
        }
    }
    return true;
}

/* Nested HT_HANDLE types without a host domain (location/lightning/...) restore as null. */
static void hashtable_log_unsupported_type(cstring_t type) {
    static char last[MAX_HASHTABLE_TYPE];
    if (!type) type = "";
    if (!strcmp(last, type)) return;
    snprintf(last, sizeof(last), "%s", type);
    fprintf(stderr, "WC3 LoadGame: hashtable nested handle type '%s' has no host domain; restoring null\n", type);
}

static bool WriteHashtableEntry(FILE *f, hashtableEntry_t const *e) {
    uint32_t type = (uint32_t)e->type;
    int handle_id = -1;
    if (!SaveBytes(f, &e->parent, sizeof(e->parent)) || !SaveBytes(f, &e->child, sizeof(e->child)) ||
        !SaveBytes(f, &type, sizeof(type))) return false;
    switch (e->type) {
    case HT_INTEGER: return SaveBytes(f, &e->value.integer, sizeof(e->value.integer));
    case HT_REAL: return SaveBytes(f, &e->value.real, sizeof(e->value.real));
    case HT_BOOLEAN: return SaveBytes(f, &e->value.boolean, sizeof(e->value.boolean));
    case HT_STRING: return SaveBytes(f, e->value.string, sizeof(e->value.string));
    case HT_HANDLE:
        if (!SaveBytes(f, e->handle_type, sizeof(e->handle_type))) return false;
        if (e->value.handle && e->handle_type[0]) {
            uint32_t id = 0;
            if (G_SaveJassHandle(e->handle_type, e->value.handle, &id)) handle_id = (int)id;
            /* Stale or unsupported nested handles become null; keep the type string. */
        }
        return SaveBytes(f, &handle_id, sizeof(handle_id));
    default:
        fprintf(stderr, "WC3 SaveGame: unknown hashtable slot type %u\n", (unsigned)type);
        return false;
    }
}

static bool ReadHashtableEntry(FILE *f, hashtableEntry_t *e) {
    uint32_t type = 0;
    int handle_id = -1;
    memset(e, 0, sizeof(*e));
    if (!LoadBytes(f, &e->parent, sizeof(e->parent)) || !LoadBytes(f, &e->child, sizeof(e->child)) ||
        !LoadBytes(f, &type, sizeof(type))) return false;
    e->type = (hashtableSlotType_t)type;
    switch (e->type) {
    case HT_INTEGER: return LoadBytes(f, &e->value.integer, sizeof(e->value.integer));
    case HT_REAL: return LoadBytes(f, &e->value.real, sizeof(e->value.real));
    case HT_BOOLEAN: return LoadBytes(f, &e->value.boolean, sizeof(e->value.boolean));
    case HT_STRING: return LoadBytes(f, e->value.string, sizeof(e->value.string));
    case HT_HANDLE: {
        jassHandleDomain_t domain;
        if (!LoadBytes(f, e->handle_type, sizeof(e->handle_type)) ||
            !LoadBytes(f, &handle_id, sizeof(handle_id))) return false;
        e->handle_type[sizeof(e->handle_type) - 1] = 0;
        if (handle_id < 0 || !e->handle_type[0]) { e->value.handle = NULL; return true; }
        if (!JassHandleDomain(e->handle_type, &domain)) {
            hashtable_log_unsupported_type(e->handle_type);
            e->value.handle = NULL;
            return true;
        }
        e->value.handle = G_LoadJassHandle(e->handle_type, (uint32_t)handle_id);
        return true;
    }
    default:
        fprintf(stderr, "WC3 LoadGame: unknown hashtable slot type %u\n", (unsigned)type);
        return false;
    }
}

static bool WriteHashtables(FILE *f) {
    FOR_LOOP(i, MAX_HASHTABLES) {
        hashtable_t *table = &level.hashtables[i];
        uint32_t count;
        if (!table->inuse) continue;
        count = table->num_entries;
        if (count > MAX_HASHTABLE_ENTRIES) {
            fprintf(stderr, "WC3 SaveGame: hashtable %u entry count %u exceeds %u\n",
                (unsigned)i, (unsigned)count, (unsigned)MAX_HASHTABLE_ENTRIES);
            return false;
        }
        if (!SaveBytes(f, &count, sizeof(count))) return false;
        FOR_LOOP(j, count) {
            if (!WriteHashtableEntry(f, table->entries + j)) {
                fprintf(stderr, "WC3 SaveGame: failed at hashtable %u entry %u\n", (unsigned)i, (unsigned)j);
                return false;
            }
        }
    }
    return true;
}

static bool ReadHashtables(FILE *f) {
    FOR_LOOP(i, MAX_HASHTABLES) {
        hashtable_t *table = &level.hashtables[i];
        uint32_t count = 0;
        if (table->entries) { gi.MemFree(table->entries); table->entries = NULL; }
        table->num_entries = table->capacity = 0;
        if (!table->inuse) continue;
        if (!LoadBytes(f, &count, sizeof(count)) || count > MAX_HASHTABLE_ENTRIES) {
            fprintf(stderr, "WC3 LoadGame: failed at hashtable %u header\n", (unsigned)i);
            return false;
        }
        if (count && !G_HashtableReserve(table, count)) return false;
        FOR_LOOP(j, count) {
            if (!ReadHashtableEntry(f, table->entries + j)) {
                fprintf(stderr, "WC3 LoadGame: failed at hashtable %u entry %u\n", (unsigned)i, (unsigned)j);
                return false;
            }
            table->num_entries = j + 1;
        }
    }
    return true;
}

/* Route buffers use one bounded, finite-point payload contract for both Move owners. */
static bool WriteMoveRouteBuffers(FILE *f, moveFineRoute_t const *route) {
    if (route->count > BZ_WC3_FINE_NODES || (route->count && (!route->points || route->index >= route->count))) {
        fprintf(stderr,"WC3 SaveGame: invalid fine route count=%u index=%u\n",route->count,route->index);
        return false;
    }
    if (route->adaptive_count>BZ_WC3_ACC_ROUTE_NODES ||
        (route->adaptive_count && (!route->adaptive_points || route->adaptive_index>=route->adaptive_count))) {
        fprintf(stderr,"WC3 SaveGame: invalid adaptive route count=%u index=%u\n",route->adaptive_count,route->adaptive_index);
        return false;
    }
    if (route->group_count>BZ_WC3_ACC_ROUTE_NODES ||
        (route->group_count && (!route->group_points || route->group_index>=route->group_count))) {
        fprintf(stderr,"WC3 SaveGame: invalid group route count=%u index=%u\n",route->group_count,route->group_index);
        return false;
    }
    return (!route->count || SaveBytes(f,route->points,route->count*sizeof(*route->points))) &&
        (!route->adaptive_count || SaveBytes(f,route->adaptive_points,route->adaptive_count*sizeof(*route->adaptive_points))) &&
        (!route->group_count || SaveBytes(f,route->group_points,route->group_count*sizeof(*route->group_points)));
}

static bool ReadMoveRouteBuffers(FILE *f, moveFineRoute_t *route) {
    route->points = NULL; route->adaptive_points=NULL; route->group_points=NULL;
    route->capacity=route->adaptive_capacity=route->group_capacity=0;
    if (route->count > BZ_WC3_FINE_NODES || (route->count && route->index >= route->count)) return false;
    if (route->count) {
        G_ReserveMoveRouteBuffer(&route->points,&route->capacity,route->count);
        if (!route->points || !LoadBytes(f,route->points,route->count*sizeof(*route->points))) {
            free(route->points); route->points = NULL; return false;
        }
        FOR_LOOP(i,route->count) if (!isfinite(route->points[i].x) || !isfinite(route->points[i].y)) {
            free(route->points); route->points = NULL; return false;
        }
    }
    if (route->adaptive_count) {
        if (route->adaptive_count>BZ_WC3_ACC_ROUTE_NODES || route->adaptive_index>=route->adaptive_count ||
            !isfinite(route->adaptive_goal.x) || !isfinite(route->adaptive_goal.y) || !isfinite(route->adaptive_radius)) {
            free(route->points); route->points=NULL; return false;
        }
        G_ReserveMoveRouteBuffer(&route->adaptive_points,&route->adaptive_capacity,route->adaptive_count);
        if (!route->adaptive_points || !LoadBytes(f,route->adaptive_points,route->adaptive_count*sizeof(*route->adaptive_points))) {
            free(route->points); free(route->adaptive_points); route->points=route->adaptive_points=NULL; return false;
        }
        FOR_LOOP(i,route->adaptive_count) if (!isfinite(route->adaptive_points[i].x) || !isfinite(route->adaptive_points[i].y)) {
            free(route->points); free(route->adaptive_points); route->points=route->adaptive_points=NULL; return false;
        }
    }
    if (route->group_count) {
        if (route->group_count>BZ_WC3_ACC_ROUTE_NODES || route->group_index>=route->group_count ||
            !isfinite(route->group_goal.x) || !isfinite(route->group_goal.y) || !isfinite(route->group_radius)) {
            free(route->points); free(route->adaptive_points); route->points=route->adaptive_points=NULL; return false;
        }
        G_ReserveMoveRouteBuffer(&route->group_points,&route->group_capacity,route->group_count);
        if (!route->group_points || !LoadBytes(f,route->group_points,route->group_count*sizeof(*route->group_points))) {
            free(route->points); free(route->adaptive_points); free(route->group_points);
            route->points=route->adaptive_points=route->group_points=NULL; return false;
        }
        FOR_LOOP(i,route->group_count) if (!isfinite(route->group_points[i].x) || !isfinite(route->group_points[i].y)) {
            free(route->points); free(route->adaptive_points); free(route->group_points);
            route->points=route->adaptive_points=route->group_points=NULL; return false;
        }
    }
    return true;
}

/* Verify the intrusive admission graph before following any saved pointer.
 * Counts bound traversal, including cycles and disconnected queued edicts. */
static bool ValidMoveFineRequests(void) {
    uint32_t total=0,queued=0;
    if (*(uint8_t const *)&level.move_fine_responsive>1) return false;
    FOR_LOOP(i,MAX_PLAYERS) {
        moveFineBudget_t const *budget=level.move_fine_budgets+i;
        if (budget->count>globals.num_edicts || budget->countdown>1) return false;
        if (budget->limit && (budget->limit<BZ_WC3_FINE_OWNER_WORK ||
            budget->limit>MAX_ENTITIES*(BZ_WC3_UNIT_FINE_WORK+1u) ||
            (!level.move_fine_responsive && budget->limit!=BZ_WC3_FINE_OWNER_WORK))) return false;
        /* Admission may overshoot by one bounded search; a saved counter near
         * UINT32_MAX cannot be produced by the owner and could wrap on charge. */
        if (budget->work>(uint64_t)MAX(budget->limit,BZ_WC3_FINE_OWNER_WORK)+
            BZ_WC3_UNIT_FINE_WORK+1u) return false;
        edict_t const *prev=NULL,*unit=budget->head;
        uint32_t count=0;
        while (unit) {
            uintptr_t ptr=(uintptr_t)unit,base=(uintptr_t)g_edicts;
            if (ptr<base || ptr>=base+globals.num_edicts*sizeof(*g_edicts) || (ptr-base)%sizeof(*g_edicts)) return false;
            if (++count>budget->count || !unit->inuse || G_IsDeferredFree(unit) ||
                *(uint8_t const *)&unit->movement.fine_queued!=1 || unit->movement.fine_prev!=prev ||
                unit->movement.fine_class!=i || S_MoveSchedulingClass(unit)!=i) return false;
            prev=unit; unit=unit->movement.fine_next;
        }
        if (count!=budget->count || prev!=budget->tail) return false;
        total+=count;
        if (total>globals.num_edicts) return false;
    }
    FOR_LOOP(i,globals.num_edicts) {
        edict_t const *unit=g_edicts+i;
        if (*(uint8_t const *)&unit->movement.fine_queued>1 || unit->movement.fine_class>=MAX_PLAYERS || unit->movement.formation_rank>15) return false;
        if (unit->movement.fine_queued) queued++;
        else if (unit->movement.fine_prev || unit->movement.fine_next) return false;
    }
    return queued==total;
}

/* Members must be live and generation-matched exactly once. An empty ordinary
 * owner remains valid until its next Move visit, including across save/load.
 * The JASS collection is independent: destroying it does not cancel this Move. */
static bool ValidMoveGroup(moveGroup_t const *group) {
    if (!group->id || !group->sequence || group->sequence>level.next_move_group_sequence ||
        group->count>BZ_WC3_GROUP_ORDER_UNITS || group->cooldown>66 ||
        *(uint8_t const *)&group->inuse!=1 || *(uint8_t const *)&group->initialized>1 || group->ticking ||
        *(uint8_t const *)&group->individual>1 ||
        (group->individual && (group->count>1 || group->shared_id || group->target)) ||
        !isfinite(group->goal.x) || !isfinite(group->goal.y) || !isfinite(group->point.x) ||
        !isfinite(group->point.y) || !isfinite(group->heading) || !isfinite(group->radius) || group->radius<0)
        return false;
    if (group->shared_id && !S_FindMoveShared(group->shared_id)) return false;
    if (group->target) {
        uintptr_t ptr=(uintptr_t)group->target,base=(uintptr_t)g_edicts;
        if (ptr<base || ptr>=base+globals.num_edicts*sizeof(*g_edicts) || (ptr-base)%sizeof(*g_edicts) ||
            !group->target->inuse || G_IsDeferredFree(group->target) ||
            group->target->spawn_time!=group->target_spawn || !(group->flags&0x1000) ||
            group->target_refresh<0 || group->target_refresh>297) return false;
    } else if (group->target_spawn || group->target_refresh || (group->flags&0x1000)) return false;
    FOR_LOOP(i,group->count) {
        moveGroupMember_t const *member=group->members+i;
        uintptr_t ptr=(uintptr_t)member->unit,base=(uintptr_t)g_edicts;
        if (ptr<base || ptr>=base+globals.num_edicts*sizeof(*g_edicts) || (ptr-base)%sizeof(*g_edicts)) return false;
        edict_t const *unit=member->unit;
        if (!unit->inuse || G_IsDeferredFree(unit) || unit->spawn_time!=member->spawn ||
            unit->movement.group_id!=group->id || !unit->goalentity ||
            !isfinite(member->offset.x) || !isfinite(member->offset.y) ||
            !isfinite(member->destination.x) || !isfinite(member->destination.y) ||
            !isfinite(member->world_destination.x) || !isfinite(member->world_destination.y) ||
            !isfinite(member->speed) || member->speed<0 || !isfinite(member->heading) ||
            !isfinite(member->arrival_range) || member->arrival_range<0 ||
            *(uint8_t const *)&member->arrived>1 || *(uint8_t const *)&member->in_range>1 ||
            *(uint8_t const *)&member->forced_arrival>1) return false;
        if (group->target) {
            if (group->target->movement.captain_actor_type) {
                if (!unit->movement.captain_home.active || unit->movement.captain_home.actor!=group->target) return false;
            } else if (unit->movement.follow_target!=group->target || unit->goalentity!=group->target) return false;
        }
        FOR_LOOP(j,i) if (group->members[j].unit==unit) return false;
    }
    return true;
}

/* Transpose compacted region memberships once, O(cells+links+objects).
 * Save/load must not scan the map separately for each tree or region. */
static bool WriteMoveSpatial(FILE *f) {
    S_CompactMoveFineSpatial();
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    uint32_t count=0,search_stamp=G_GetMoveFineSearchStamp(),total=0;
    uint32_t *offset=calloc((size_t)map->raw_objects+1,sizeof(*offset)),*cells=NULL,*cursor=NULL;
    if(!offset)gi.error("Move save: cannot allocate region offsets");
    FOR_LOOP(i,map->raw_objects) {
        wc3RecordObject_t const *object=wc3_records_object(map,i);
        offset[i]=total;
        if((object->flags&WC3_RECORD_REGION) && object->stamp!=UINT32_MAX)total+=object->refs;
    }
    offset[map->raw_objects]=total;
    if(total) {
        cells=wc3_records_memory(NULL,(size_t)total*sizeof(*cells));
        cursor=wc3_records_memory(NULL,((size_t)map->raw_objects+1)*sizeof(*cursor));
        memcpy(cursor,offset,((size_t)map->raw_objects+1)*sizeof(*cursor));
        FOR_LOOP(cell,map->width*map->height) {
            for(uint32_t link=map->cells[cell]&WC3_RECORD_END;link!=WC3_RECORD_END;link=map->links[link].next&WC3_RECORD_END) {
                uint32_t id=map->links[link].payload;
                if(wc3_records_object(map,id)->flags&WC3_RECORD_REGION)cells[cursor[id]++]=cell;
            }
        }
    }
    FOR_LOOP(i,globals.num_edicts)if(G_GetMoveSpatialObject(i) || S_GetMoveRegions(i)->count)count++;
    bool success=SaveBytes(f,&map->query,sizeof(map->query)) && SaveBytes(f,&search_stamp,sizeof(search_stamp)) &&
        SaveBytes(f,&count,sizeof(count));
    FOR_LOOP(i,globals.num_edicts) {
        wc3RecordObject_t const *object=G_GetMoveSpatialObject(i);
        wc3RegionCollection_t const *regions=S_GetMoveRegions(i);
        if(!object && !regions->count)continue;
        uint32_t ordinary=object!=NULL;
        if(!success || !g_edicts[i].inuse || !SaveBytes(f,&i,sizeof(i)) ||
            !SaveBytes(f,&ordinary,sizeof(ordinary))) {success=false;break;}
        if(object && (object->stamp==UINT32_MAX || !SaveBytes(f,&object->box,sizeof(object->box)) ||
            !SaveBytes(f,&object->stamp,sizeof(object->stamp)))) {success=false;break;}
        if(!SaveBytes(f,&regions->count,sizeof(regions->count))) {success=false;break;}
        if(!regions->count)continue;
        moveRegionSave_t state;S_GetMoveRegionState(i,&state);
        uint32_t header[]={state.width,state.height,state.turn,state.published};
        if(!SaveBytes(f,header,sizeof(header)) || !SaveBytes(f,state.center,sizeof(state.center)) ||
            !SaveBytes(f,state.pixels,(size_t)state.width*state.height)) {success=false;break;}
        FOR_LOOP(slot,regions->count) {
            uint32_t id=regions->objects[slot],n=offset[id+1]-offset[id];
            wc3RecordObject_t const *region=wc3_records_object(map,id);
            if(region->stamp==UINT32_MAX || !SaveBytes(f,&region->stamp,sizeof(region->stamp)) ||
                !SaveBytes(f,&n,sizeof(n)) || (n && !SaveBytes(f,cells+offset[id],(size_t)n*sizeof(*cells)))) {
                success=false;break;
            }
        }
    }
    free(offset);free(cursor);free(cells);return success;
}

/*14d000 restores each object in save order. New identities are stable and
 * pointers, allocator slot IDs and movement-era publication ranks are absent. */
static bool ReadMoveSpatial(FILE *f) {
    uint32_t query,search_stamp,count,index,stamp;uint8_t *pixels=NULL;
    bool seen[MAX_ENTITIES]={0};
    G_ClearMoveSpatial();
    if(!LoadBytes(f,&query,sizeof(query)) || !LoadBytes(f,&search_stamp,sizeof(search_stamp)) || search_stamp>UINT16_MAX ||
        !LoadBytes(f,&count,sizeof(count)) || count>globals.num_edicts)return false;
    FOR_LOOP(i,count) {
        uint32_t ordinary,region_count;
        if(!LoadBytes(f,&index,sizeof(index)) || index>=globals.num_edicts || seen[index] || !g_edicts[index].inuse ||
            !LoadBytes(f,&ordinary,sizeof(ordinary)) || ordinary>1)goto failed;
        if(ordinary) {
            wc3FineBox_t box;
            if(!LoadBytes(f,&box,sizeof(box)) || !LoadBytes(f,&stamp,sizeof(stamp)) || stamp==UINT32_MAX ||
                !G_LoadMoveSpatialObject(index,&box))goto failed;
            wc3_records_owned(S_GetMoveFineSpatial(),index)->stamp=stamp;
        }
        if(!LoadBytes(f,&region_count,sizeof(region_count)) || region_count>4 ||
            (region_count && region_count<3) || (!ordinary && !region_count))goto failed;
        seen[index]=true;if(!region_count)continue;
        moveRegionSave_t state={0};uint32_t header[4];
        if(!LoadBytes(f,header,sizeof(header)) || !LoadBytes(f,state.center,sizeof(state.center)))goto failed;
        state.width=header[0];state.height=header[1];state.turn=header[2];state.published=header[3];
        uint64_t size=(uint64_t)state.width*state.height;
        if(!size || size>(1u<<24) || state.turn>3 || state.published>1)goto failed;
        pixels=wc3_records_memory(NULL,size);state.pixels=pixels;
        if(!LoadBytes(f,pixels,size) || !S_LoadMoveRegions(index,region_count,&state))goto failed;
        free(pixels);pixels=NULL;
        wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
        wc3RegionCollection_t const *regions=S_GetMoveRegions(index);
        FOR_LOOP(slot,region_count) {
            uint32_t n,previous=0,id=regions->objects[slot];
            if(!LoadBytes(f,&stamp,sizeof(stamp)) || stamp==UINT32_MAX || !LoadBytes(f,&n,sizeof(n)) ||
                n>map->width*map->height || (!state.published && n))goto failed;
            FOR_LOOP(cell,n) {
                uint32_t position;
                if(!LoadBytes(f,&position,sizeof(position)) || position>=map->width*map->height ||
                    (cell && position<=previous))goto failed;
                wc3_records_prepend(map,position,id,WC3_RECORD_INSERT);previous=position;
            }
            wc3_records_object(map,id)->stamp=stamp;
        }
    }
    S_GetMoveFineSpatial()->query=query;G_SetMoveFineSearchStamp(search_stamp);return true;
failed:
    free(pixels);G_ClearMoveSpatial();return false;
}

static bool WriteMoveProximity(FILE *f) {
    S_CompactMoveProximity();
    uint32_t query=S_GetMoveProximityQuery(),count=0;
    if(!SaveBytes(f,&query,sizeof(query)))return false;
    FOR_LOOP(i,globals.num_edicts)if(S_GetMoveProximity(i))count++;
    if(!SaveBytes(f,&count,sizeof(count)))return false;
    FOR_LOOP(i,globals.num_edicts) {
        wc3FineBox_t const *box=S_GetMoveProximity(i);
        uint32_t stamp=S_GetMoveProximityStamp(i);
        if(box && (!g_edicts[i].inuse || stamp==UINT32_MAX || !SaveBytes(f,&i,sizeof(i)) ||
            !SaveBytes(f,box,sizeof(*box)) || !SaveBytes(f,&stamp,sizeof(stamp))))return false;
    }
    return true;
}

static bool ReadMoveProximity(FILE *f) {
    S_ClearMoveProximity();
    uint32_t query,count,index;
    if(!LoadBytes(f,&query,sizeof(query)) || !LoadBytes(f,&count,sizeof(count)) || count>globals.num_edicts)return false;
    FOR_LOOP(i,count) {
        wc3FineBox_t box;uint32_t stamp;
        if(!LoadBytes(f,&index,sizeof(index)) || !LoadBytes(f,&box,sizeof(box)) ||
            !LoadBytes(f,&stamp,sizeof(stamp)) || stamp==UINT32_MAX || !S_LoadMoveProximity(index,box)) {
            S_ClearMoveProximity();return false;
        }
        S_SetMoveProximityStamp(index,stamp);
    }
    S_SetMoveProximityQuery(query);
    S_ResetMoveSpatialMaintenance();
    return true;
}

/* Shared parameters precede groups so every restored binding can resolve.
 * Zero references remain valid until the next Move owner prepass collects them. */
static bool WriteMoveShared(FILE *f) {
    if (!S_ValidateMoveShared()) return false;
    uint32_t count=0;
    FOR_LOOP(i,ARRAY_COUNT(level.move_shared)) if (level.move_shared[i].inuse) count++;
    if (count>MAX_SAVE_GROUP_HANDLES || !SaveBytes(f,&count,sizeof(count))) return false;
    FOR_LOOP(i,ARRAY_COUNT(level.move_shared)) {
        moveShared_t temp=level.move_shared[i];
        if (temp.inuse && !WriteMappedFields(f,move_shared_fields,(uint8_t *)&temp)) return false;
    }
    return true;
}

static bool ReadMoveShared(FILE *f) {
    uint32_t count=0;
    if (!LoadBytes(f,&count,sizeof(count)) || count>MAX_SAVE_GROUP_HANDLES) goto failed;
    if (!count) return true;
    level.move_shared=calloc(count,sizeof(*level.move_shared));
    if (!level.move_shared) goto failed;
    ARRAY_COUNT(level.move_shared)=level.move_shared_capacity=count;
    FOR_LOOP(i,count) {
        moveShared_t *shared=level.move_shared+i;
        if (!ReadMappedFields(f,move_shared_fields,(uint8_t *)shared) ||
            *(uint8_t const *)&shared->inuse!=1 || !shared->id || shared->id>level.next_move_shared_id ||
            !isfinite(shared->speed) || shared->speed<0 || !isfinite(shared->next_speed) || shared->next_speed<0 ||
            !isfinite(shared->radius) || shared->radius<0) goto failed;
        FOR_LOOP(j,i) if (level.move_shared[j].id==shared->id) goto failed;
    }
    return true;
failed:
    S_ClearMoveGroups();
    G_ResetWaypointCache();
    return false;
}

/* Store active owners after edicts so loading can validate restored generations. */
static bool WriteMoveGroups(FILE *f) {
    uint32_t count=0;
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) if (level.move_groups[i]->inuse) count++;
    if (count>globals.num_edicts || !SaveBytes(f,&count,sizeof(count))) return false;
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t const *group=level.move_groups[i];
        if (!group->inuse) continue;
        if (!ValidMoveGroup(group)) return false;
        FOR_LOOP(j,i) if (level.move_groups[j]->inuse && (level.move_groups[j]->id==group->id || level.move_groups[j]->sequence==group->sequence)) return false;
        moveGroup_t temp=*group;
        ClearRuntimeFields(&temp,move_group_fields,FIELD_RUNTIME);
        if (!WriteMappedFields(f,move_group_fields,(uint8_t *)&temp) || !WriteMoveRouteBuffers(f,&group->route)) return false;
    }
    return true;
}

/* Rebuild process-owned allocations; failed payloads release every partial owner. */
static bool ReadMoveGroups(FILE *f) {
    uint32_t count=0;
    if (!LoadBytes(f,&count,sizeof(count)) || count>globals.num_edicts) goto failed;
    if (!count) return true;
    level.move_groups=calloc(count,sizeof(*level.move_groups));
    if (!level.move_groups) goto failed;
    level.move_group_capacity=count;
    FOR_LOOP(i,count) {
        moveGroup_t *group=calloc(1,sizeof(*group));
        if (!group) goto failed;
        level.move_groups[ARRAY_COUNT(level.move_groups)++]=group;
        bool mapped=ReadMappedFields(f,move_group_fields,(uint8_t *)group);
        /* Clear even a partial record before cleanup can touch curve addresses. */
        ClearRuntimeFields(group,move_group_fields,FIELD_RUNTIME);
        if (!mapped || !ValidMoveGroup(group) || !ReadMoveRouteBuffers(f,&group->route)) goto failed;
        FOR_LOOP(j,i) {
            moveGroup_t const *other=level.move_groups[j];
            if (other->id==group->id || other->sequence==group->sequence) goto failed;
            FOR_LOOP(m,group->count) FOR_LOOP(n,other->count)
                if (group->members[m].unit && group->members[m].unit==other->members[n].unit) goto failed;
        }
    }
    return true;
failed:
    S_ClearMoveGroups();
    return false;
}

/* Configuration values are saved, never intern addresses. The ownership mask
 * preserves mutable overrides; shared defaults are re-interned on restore. */
static bool WriteAttackProfiles(FILE *f, edict_t const *ent) {
    uint32_t owned = (ent->attack_overrides[0] ? 1u : 0u) | (ent->attack_overrides[1] ? 2u : 0u);
    if (!SaveBytes(f, &owned, sizeof(owned))) return false;
    FOR_LOOP(i, 2) {
        unitAttack_t const *defaults = ent->attack_profiles[i] ? ent->attack_profiles[i] : unit_attack_empty + i;
        if (!SaveBytes(f, defaults, sizeof(*defaults))) return false;
        if ((owned & (1u << i)) && !SaveBytes(f, ent->attack_overrides[i], sizeof(*defaults))) return false;
    }
    return true;
}

static bool ReadAttackProfiles(FILE *f, edict_t *ent) {
    uint32_t owned;
    if (!LoadBytes(f, &owned, sizeof(owned)) || (owned & ~3u)) return false;
    FOR_LOOP(i, 2) {
        unitAttack_t defaults;
        if (!LoadBytes(f, &defaults, sizeof(defaults))) return false;
        ent->attack_profiles[i] = S_InternAttackProfile(&defaults, i);
        if ((owned & (1u << i)) && !LoadBytes(f, S_AttackProfileWrite(ent, i), sizeof(defaults))) return false;
    }
    return true;
}

static bool WriteUnitSoundProfile(FILE *f, edict_t const *ent) {
    unitSoundProfile_t const *value = G_UnitSoundProfile(ent);
    if (value->num_select > MAX_UNIT_SELECT_SOUNDS || value->num_yes > MAX_UNIT_SELECT_SOUNDS ||
        value->num_ready > MAX_UNIT_SELECT_SOUNDS || value->num_chop > 3) {
        fprintf(stderr, "WC3 SaveGame: invalid unit sound variant count\n");
        return false;
    }
    return SaveBytes(f, value, sizeof(*value));
}

/* Numeric media identities are restored by the saved configstring registry,
 * as for attack profiles. Profile addresses never enter the saved stream. */
static bool ReadUnitSoundProfile(FILE *f, edict_t *ent) {
    unitSoundProfile_t value;
    if (!LoadBytes(f, &value, sizeof(value))) return false;
    if (value.num_select > MAX_UNIT_SELECT_SOUNDS || value.num_yes > MAX_UNIT_SELECT_SOUNDS ||
        value.num_ready > MAX_UNIT_SELECT_SOUNDS || value.num_chop > 3) {
        fprintf(stderr, "WC3 LoadGame: invalid unit sound variant count\n");
        return false;
    }
    G_SetUnitSoundProfile(ent, &value);
    return true;
}

static bool WriteUnitAnimationText(FILE *f, edict_t const *ent) {
    char request[WC3_ANIMATION_REQUEST_SIZE] = {0}, properties[WC3_ANIMATION_PROPERTIES_SIZE] = {0};
    strlcpy(request, G_UnitAnimationRequest(ent), sizeof(request));
    strlcpy(properties, G_UnitAnimationProperties(ent), sizeof(properties));
    return SaveBytes(f, request, sizeof(request)) && SaveBytes(f, properties, sizeof(properties));
}

static bool ReadUnitAnimationText(FILE *f, edict_t *ent) {
    char request[WC3_ANIMATION_REQUEST_SIZE], properties[WC3_ANIMATION_PROPERTIES_SIZE];
    if (!LoadBytes(f, request, sizeof(request)) || !LoadBytes(f, properties, sizeof(properties))) return false;
    if (!memchr(request, 0, sizeof(request)) || !memchr(properties, 0, sizeof(properties))) {
        fprintf(stderr, "WC3 LoadGame: unterminated unit animation text\n");
        return false;
    }
    G_StoreUnitAnimationRequest(ent, request);
    G_StoreUnitAnimationProperties(ent, properties);
    return true;
}

static bool WriteEdict(FILE *f, edict_t const *ent) {
    edict_t temp = *ent;
    field_t const *field;

    if (ent->order_queue.head >= MAX_UNIT_ORDER_QUEUE ||
        ent->order_queue.count > MAX_UNIT_ORDER_QUEUE ||
        (ent->order_queue.count && !ent->order_queue.entries)) {
        fprintf(stderr, "WC3 SaveGame: invalid order queue on edict %u\n", ent->s.number);
        return false;
    }
    ClearRuntimeFields(&temp, edict_fields, FIELD_RUNTIME);
    for (field = edict_fields; field->name; field++)
        if (!WriteField1(field, (uint8_t *)&temp)) return false;
    return SaveBytes(f, &temp, sizeof(temp)) && WriteAttackProfiles(f, ent) &&
           WriteUnitSoundProfile(f, ent) && WriteUnitAnimationText(f, ent) &&
           WriteMoveRouteBuffers(f,&ent->movement.fine_route);
}

static bool WriteClient(FILE *f, gameClient_t const *client) {
    gameClient_t temp = *client;
    int target = client->camera.target_controller ? (int)(client->camera.target_controller - g_edicts) : -1;
    uint32_t const disabled_count = client->jass.disabled_ability_count;
    size_t disabled_bytes;

    /* Client pointers and callbacks are process-owned; text storage remains inline in GAMECLIENT. */
    ClearRuntimeFields(&temp, client_fields, FIELD_RUNTIME);
    if (target < -1 || target >= (int)globals.max_edicts) return false;
    if (!DisabledAbilityBytes(disabled_count, &disabled_bytes) ||
        (disabled_count && !client->jass.disabled_abilities)) {
        fprintf(stderr, "WC3 SaveGame: invalid disabled ability count %u\n", (unsigned)disabled_count);
        return false;
    }
    if (!SaveBytes(f, &temp, sizeof(temp)) || !SaveBytes(f, &target, sizeof(target)) ||
        !SaveBytes(f, &disabled_count, sizeof(disabled_count))) return false;
    return !disabled_count || SaveBytes(f, client->jass.disabled_abilities, disabled_bytes);
}

static bool ReadClient(FILE *f, gameClient_t *client, int *target) {
    gameClient_t temp;
    uint32_t *disabled_abilities = NULL;
    uint32_t disabled_count = 0;
    size_t disabled_bytes;

    if (!LoadBytes(f, &temp, sizeof(temp)) || !LoadBytes(f, target, sizeof(*target)) ||
        !LoadBytes(f, &disabled_count, sizeof(disabled_count))) return false;
    if (*target < -1 || *target >= (int)globals.max_edicts) return false;
    if (!DisabledAbilityBytes(disabled_count, &disabled_bytes)) {
        fprintf(stderr, "WC3 LoadGame: invalid disabled ability count %u\n", (unsigned)disabled_count);
        return false;
    }
    if (disabled_count) {
        disabled_abilities = malloc(disabled_bytes);
        if (!disabled_abilities || !LoadBytes(f, disabled_abilities, disabled_bytes)) {
            fprintf(stderr, "WC3 LoadGame: failed to read disabled ability list\n");
            free(disabled_abilities);
            return false;
        }
    }

    G_ClearPlayerAbilityAvailability(client);
    *client = temp;
    G_ResetPlayerTechIndexes();
    client->jass.disabled_abilities = disabled_abilities;
    client->jass.disabled_ability_count = disabled_count;
    client->jass.disabled_ability_capacity = disabled_count;
    client->ps.name = client->jass.name;
    FOR_LOOP(i, PLAYERTEXT_COUNT) client->ps.texts[i] = client->playerTextCursor[i] ?
        client->playerTextStorage[i][client->playerTextCursor[i] & PLAYER_TEXT_MASK] : NULL;
    client->mapplayer = level.mapinfo && client->ps.number < MAX_PLAYERS ? level.mapinfo->players + client->ps.number : NULL;
    client->menu.on_entity_selected = NULL; client->menu.on_location_selected = NULL;
    client->menu.cmdbutton = NULL; client->menu.refresh = NULL;
    client->menu.supports_order_queue = false;
    client->menu.order_queued = false;
    client->menu.order_alt = false;
    client->menu.order_queue_chained = false;
    client->menu.dragged_item = NULL; client->menu.dragged_item_spawn_time = 0;
    client->cursor_signal = false;
    client->ps.stats[UI_PLAYERSTAT_CURSOR_FLAGS] = 0;
    client->cursor_missing_reported = false;
    client->ps.stats[UI_PLAYERSTAT_CURSOR_INTERACTION] = 0;
    client->ps.stats[UI_PLAYERSTAT_CURSOR_IMAGE] = 0;
    client->camera.target_controller = NULL;
    client->rally_indicator = NULL;
    return true;
}

static bool WriteTerrainPathing(FILE *f) {
    uint32_t const size = G_GetTerrainPathingStateSize();
    uint8_t *data = NULL;
    bool ok;

    if (!SaveBytes(f, &size, sizeof(size))) return false;
    if (!size) return true;
    data = gi.MemAlloc(size);
    if (!data) return false;
    ok = G_GetTerrainPathingState(data, size) && SaveBytes(f, data, size);
    gi.MemFree(data);
    return ok;
}

static bool ReadTerrainPathing(FILE *f) {
    uint32_t size, expected;
    uint8_t *data = NULL;
    bool ok;

    if (!LoadBytes(f, &size, sizeof(size))) return false;
    expected = G_GetTerrainPathingStateSize();
    if (size != expected) return false;
    if (!size) return true;
    data = gi.MemAlloc(size);
    if (!data) return false;
    ok = LoadBytes(f, data, size) && G_SetTerrainPathingState(data, size);
    gi.MemFree(data);
    return ok;
}

typedef struct { uint32_t size; point2_t maps[4]; } moveAdaptiveHeader_t;

static moveAdaptiveHeader_t MoveAdaptiveHeader(void) {
    moveAdaptiveHeader_t header={.size=G_GetMoveAdaptiveStateSize()};
    FOR_LOOP(i,4) header.maps[i]=G_GetMoveAdaptiveMapSize(i);
    return header;
}

static bool WriteMoveAdaptive(FILE *f) {
    moveAdaptiveHeader_t header=MoveAdaptiveHeader();
    if (!SaveBytes(f,&header,sizeof(header))) return false;
    if (!header.size) return true;
    uint8_t *data=gi.MemAlloc(header.size);
    if (!data) return false;
    bool ok=G_GetMoveAdaptiveState(data,header.size) && SaveBytes(f,data,header.size);
    gi.MemFree(data); return ok;
}

static bool ReadMoveAdaptive(FILE *f) {
    moveAdaptiveHeader_t header,expected=MoveAdaptiveHeader();
    if (!LoadBytes(f,&header,sizeof(header)) || memcmp(&header,&expected,sizeof(header))) return false;
    if (!header.size) return true;
    uint8_t *data=gi.MemAlloc(header.size);
    if (!data) return false;
    bool ok=LoadBytes(f,data,header.size) && G_SetMoveAdaptiveState(data,header.size);
    gi.MemFree(data); return ok;
}

static bool WriteBlight(FILE *f) {
    uint32_t const size = G_GetBlightStateSize();
    uint8_t *data = NULL;
    bool ok;

    if (!SaveBytes(f, &size, sizeof(size))) return false;
    if (!size) return true;
    data = gi.MemAlloc(size);
    if (!data) return false;
    ok = G_GetBlightState(data, size) && SaveBytes(f, data, size);
    gi.MemFree(data);
    return ok;
}

static bool ReadBlight(FILE *f) {
    uint32_t size, expected;
    uint8_t *data = NULL;
    bool ok;

    if (!LoadBytes(f, &size, sizeof(size))) return false;
    expected = G_GetBlightStateSize();
    if (size != expected) return false;
    if (!size) return true;
    data = gi.MemAlloc(size);
    if (!data) return false;
    ok = LoadBytes(f, data, size) && G_SetBlightState(data, size);
    gi.MemFree(data);
    return ok;
}

static bool ReadEdict(FILE *f, edict_t *ent) {
    field_t const *field;

    if (!LoadBytes(f, ent, sizeof(*ent))) return false;
    ClearRuntimeFields(ent, edict_fields, FIELD_RUNTIME);
    if (ent->order_queue.head >= MAX_UNIT_ORDER_QUEUE ||
        ent->order_queue.count > MAX_UNIT_ORDER_QUEUE) {
        fprintf(stderr, "WC3 LoadGame: invalid order queue bounds on edict %u\n", ent->s.number);
        return false;
    }
    moveFineRoute_t *route = &ent->movement.fine_route;
    if (!ReadAttackProfiles(f, ent) || !ReadUnitSoundProfile(f, ent) || !ReadUnitAnimationText(f, ent) || !ReadMoveRouteBuffers(f,route)) return false;
    for (field = edict_fields; field->name; field++)
        if (!ReadField(field, (uint8_t *)ent)) return false;
    /* Table rows are process-owned; C callbacks already came back through F_CFUNCTION. */
    if (ent->class_id) {
        G_BindEntityData(ent);
        /* animation is a process-owned model pointer and is deliberately not serialized.
         * Re-resolve it from the persisted logical request plus per-unit animation tags. */
        if (G_UnitAnimationRequest(ent)[0])
            ent->animation = G_GetUnitAnimation(ent, G_UnitAnimationRequest(ent));
    }
    return true;
}

static int CompareOwnerSequence(void const *a, void const *b) {
    uint64_t x=*(uint64_t const *)a,y=*(uint64_t const *)b;
    return (x>y)-(x<y);
}

/* Pool order is unique, nonzero for spawned units and bounded by the saved
 * allocator. Sorting avoids a quadratic scan when saving crowded maps. */
static bool ValidOwnedUnits(void) {
    uint64_t *seq=gi.MemAlloc(globals.num_edicts*sizeof(*seq));
    uint32_t count=0;
    bool valid=true;
    FILTER_EDICTS(unit,unit->inuse) {
        if (!unit->own_seq) {
            if (unit->svflags&SVF_MONSTER) valid=false;
            continue;
        }
        if (unit->own_seq>level.next_unit_seq || unit->s.player>=MAX_PLAYERS) valid=false;
        seq[count++]=unit->own_seq;
    }
    qsort(seq,count,sizeof(*seq),CompareOwnerSequence);
    FOR_LOOP(i,count) if (i && seq[i]==seq[i-1]) valid=false;
    gi.MemFree(seq);
    return valid;
}

/* Pool records use the same raw-record and pointer-fixup contract as edicts. */
typedef struct {
    char const *name;
    size_t offset, size;
    field_t const *fields;
    void *(*alloc)(void);
} savePool_t;

static field_t const scalar_pool_fields[] = { { NULL, 0, 0, 0, 0, 0 } };
#define POOL_ALLOC(Name) static void *SaveAlloc##Name(void) { return G_Alloc##Name(); }
POOL_ALLOC(Construction)
POOL_ALLOC(Research)
POOL_ALLOC(Rally)
POOL_ALLOC(Food)
POOL_ALLOC(UnitStatus)
POOL_ALLOC(UnitOrders)
POOL_ALLOC(Buildwork)
POOL_ALLOC(Revival)
POOL_ALLOC(Sacrifice)
POOL_ALLOC(Unsummon)
POOL_ALLOC(ShadowMeld)
POOL_ALLOC(Militia)
POOL_ALLOC(Polymorph)
POOL_ALLOC(Raven)
POOL_ALLOC(BlightGrowth)
POOL_ALLOC(Ensnare)
POOL_ALLOC(AncientRoot)
POOL_ALLOC(GoldMine)
POOL_ALLOC(MineOverlay)
POOL_ALLOC(AcolyteMine)
POOL_ALLOC(Item)
POOL_ALLOC(Destructable)
POOL_ALLOC(Cargo)
POOL_ALLOC(Stock)
POOL_ALLOC(Waygate)
POOL_ALLOC(Artillery)
POOL_ALLOC(Avatar)
POOL_ALLOC(Sleep)
POOL_ALLOC(Channel)

static savePool_t const save_pools[] = {
    { "construction", offsetof(edict_t, construction), sizeof(construction_t), construction_fields, SaveAllocConstruction },
    { "research", offsetof(edict_t, research), sizeof(research_t), scalar_pool_fields, SaveAllocResearch },
    { "rally", offsetof(edict_t, rally), sizeof(rally_t), rally_fields, SaveAllocRally },
    { "food", offsetof(edict_t, food), sizeof(food_t), scalar_pool_fields, SaveAllocFood },
    { "abilstatus", offsetof(edict_t, abilstatus), sizeof(unitStatusStorage_t), unit_status_fields, SaveAllocUnitStatus },
    { "order_queue", offsetof(edict_t, order_queue.entries), sizeof(unitOrderStorage_t), unit_order_storage_fields, SaveAllocUnitOrders },
    { "buildwork", offsetof(edict_t, buildwork), sizeof(buildwork_t), scalar_pool_fields, SaveAllocBuildwork },
    { "revival", offsetof(edict_t, revival), sizeof(revival_t), revival_fields, SaveAllocRevival },
    { "sacrifice", offsetof(edict_t, sacrifice), sizeof(sacrifice_t), sacrifice_fields, SaveAllocSacrifice },
    { "unsummon", offsetof(edict_t, unsummon), sizeof(unsummon_t), unsummon_fields, SaveAllocUnsummon },
    { "shadowmeld", offsetof(edict_t, shadowmeld), sizeof(shadowMeld_t), scalar_pool_fields, SaveAllocShadowMeld },
    { "militia", offsetof(edict_t, militia), sizeof(militia_t), militia_fields, SaveAllocMilitia },
    { "polymorph", offsetof(edict_t, polymorph), sizeof(polymorph_t), polymorph_fields, SaveAllocPolymorph },
    { "raven", offsetof(edict_t, raven), sizeof(raven_t), raven_fields, SaveAllocRaven },
    { "blight_growth", offsetof(edict_t, blight_growth), sizeof(blightGrowth_t), scalar_pool_fields, SaveAllocBlightGrowth },
    { "ensnare", offsetof(edict_t, ensnare), sizeof(ensnare_t), ensnare_fields, SaveAllocEnsnare },
    { "ancient_root", offsetof(edict_t, ancient_root), sizeof(ancientRoot_t), ancient_root_fields, SaveAllocAncientRoot },
    { "goldmine", offsetof(edict_t, goldmine), sizeof(goldMine_t), goldmine_fields, SaveAllocGoldMine },
    { "mineoverlay", offsetof(edict_t, mineoverlay), sizeof(mineOverlay_t), mineoverlay_fields, SaveAllocMineOverlay },
    { "acolyte_mine", offsetof(edict_t, acolyte_mine), sizeof(acolyteMine_t), acolyte_mine_fields, SaveAllocAcolyteMine },
    { "item", offsetof(edict_t, item), sizeof(item_t), item_fields, SaveAllocItem },
    { "destructable", offsetof(edict_t, destructable), sizeof(destructable_t), destructable_fields, SaveAllocDestructable },
    { "cargo", offsetof(edict_t, cargo), sizeof(cargo_t), cargo_fields, SaveAllocCargo },
    { "stock", offsetof(edict_t, stock), sizeof(stock_t), stock_fields, SaveAllocStock },
    { "waygate", offsetof(edict_t, waygate), sizeof(waygate_t), scalar_pool_fields, SaveAllocWaygate },
    { "artillery", offsetof(edict_t, artillery), sizeof(artillery_t), artillery_fields, SaveAllocArtillery },
    { "avatar", offsetof(edict_t, avatar), sizeof(avatar_t), avatar_fields, SaveAllocAvatar },
    { "sleep", offsetof(edict_t, sleep), sizeof(sleep_t), sleep_fields, SaveAllocSleep },
    { "channel", offsetof(edict_t, channel), sizeof(channel_t), channel_fields, SaveAllocChannel },
};

static void *SavePoolSlot(edict_t const *ent, savePool_t const *pool) {
    void *slot;
    memcpy(&slot, (uint8_t const *)ent + pool->offset, sizeof(slot));
    return slot;
}

static bool WritePool(FILE *f, savePool_t const *pool) {
    uint32_t count = 0;
    void *temp = malloc(pool->size);
    if (!temp) return false;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && SavePoolSlot(g_edicts + i, pool)) count++;
    bool ok = SaveBytes(f, &count, sizeof(count));
    FOR_LOOP(i, globals.num_edicts) {
        void *slot = SavePoolSlot(g_edicts + i, pool);
        uint32_t index = i;
        if (!ok || !g_edicts[i].inuse || !slot) continue;
        memcpy(temp, slot, pool->size);
        ClearRuntimeFields(temp, pool->fields, FIELD_RUNTIME);
        for (field_t const *field = pool->fields; ok && field->name; field++)
            ok = WriteField1(field, temp);
        ok = ok && SaveBytes(f, &index, sizeof(index)) && SaveBytes(f, temp, pool->size);
    }
    free(temp);
    return ok;
}

static bool ReadPool(FILE *f, savePool_t const *pool) {
    uint32_t count;
    if (!LoadBytes(f, &count, sizeof(count)) || count >= DESTRUCTABLE_POOL_CAP || count > globals.num_edicts) return false;
    FOR_LOOP(n, count) {
        uint32_t index;
        if (!LoadBytes(f, &index, sizeof(index)) || index >= globals.num_edicts ||
            !g_edicts[index].inuse || SavePoolSlot(g_edicts + index, pool)) return false;
        void *slot = pool->alloc();
        memcpy((uint8_t *)(g_edicts + index) + pool->offset, &slot, sizeof(slot));
        if (!LoadBytes(f, slot, pool->size)) return false;
        ClearRuntimeFields(slot, pool->fields, FIELD_RUNTIME);
        for (field_t const *field = pool->fields; field->name; field++)
            if (!ReadField(field, slot)) return false;
    }
    return true;
}

static bool WritePools(FILE *f) {
    FOR_LOOP(i, sizeof(save_pools) / sizeof(save_pools[0])) {
        if (!WritePool(f, save_pools + i)) {
            fprintf(stderr, "WC3 SaveGame: failed at %s pool\n", save_pools[i].name);
            return false;
        }
    }
    return true;
}

static bool ReadPools(FILE *f) {
    FOR_LOOP(i, sizeof(save_pools) / sizeof(save_pools[0])) {
        if (!ReadPool(f, save_pools + i)) {
            fprintf(stderr, "WC3 LoadGame: failed at %s pool\n", save_pools[i].name);
            return false;
        }
    }
    return true;
}

bool WriteGame(cstring_t filename) {
    FILE *f = fopen(filename, "w+b");
    saveHeader_t header = {
        .magic = save_magic, .version = save_version, .edict_size = sizeof(edict_t), .num_edicts = globals.num_edicts,
        .max_clients = game.max_clients, .script_identity = level.vm ? jass_programidentity(level.vm) : 0,
        .quests = ActiveQuestCount(), .groups = level.num_groups, .triggers = level.num_triggers, .timers = level.num_timers,
        .events = ActiveEventCount()
    };
    strlcpy(header.map_path, level.map_path, sizeof(header.map_path));

    if (level.num_groups > MAX_SAVE_GROUP_HANDLES) {
        fprintf(stderr, "WC3 SaveGame: group handle count %u exceeds save safety bound %u\n",
                (unsigned)level.num_groups, (unsigned)MAX_SAVE_GROUP_HANDLES);
        return false;
    }
    bool ok = false;
    if (!f) { fprintf(stderr, "WC3 SaveGame: cannot open %s\n", filename); return false; }
    if (level.ai_vm_initialized >> MAX_PLAYERS) { fprintf(stderr,"WC3 SaveGame: invalid AI initialization players\n"); goto done; }
    FILTER_EDICTS(unit,unit->inuse && unit->order_queue.count) {
        if (!unit->order_queue.entries) {
            fprintf(stderr, "WC3 LoadGame: missing order queue on edict %u\n", unit->s.number);
            fclose(f); return false;
        }
    }
    if (!S_ValidateWaygateIds()) { fprintf(stderr,"WC3 SaveGame: invalid Way Gate identities\n"); goto done; }
    if (!ValidOwnedUnits()) { fprintf(stderr,"WC3 SaveGame: invalid unit owned-pool order\n"); goto done; }
    if (!S_ValidateCaptainHomeActors(false)) { fprintf(stderr,"WC3 SaveGame: invalid captain actor references\n"); goto done; }
    if (!ValidMoveFineRequests()) { fprintf(stderr,"WC3 SaveGame: invalid fine-request FIFO\n"); goto done; }
    if (!S_ValidateMoveCoarseRequests()) { fprintf(stderr,"WC3 SaveGame: invalid coarse-request FIFOs\n"); goto done; }
    if (!SaveBytes(f, &header, sizeof(header))) { fprintf(stderr, "WC3 SaveGame: failed at header\n"); goto done; }
    if (!WriteMappedFields(f, level_fields, (uint8_t *)&level)) {
        fprintf(stderr, "WC3 SaveGame: failed at level fields\n"); goto done;
    }
    if (!WriteTerrainPathing(f)) { fprintf(stderr, "WC3 SaveGame: failed at terrain pathing state\n"); goto done; }
    if (!WriteMoveAdaptive(f)) { fprintf(stderr,"WC3 SaveGame: failed at adaptive publication state\n"); goto done; }
    if (!WriteBlight(f)) { fprintf(stderr, "WC3 SaveGame: failed at blight state\n"); goto done; }
    if (!WriteGroups(f)) goto done;
    FOR_LOOP(i, game.max_clients) {
        if (!WriteClient(f, game.clients + i)) { fprintf(stderr, "WC3 SaveGame: failed at client %d\n", i); goto done; }
    }
    FOR_LOOP(i, globals.num_edicts) {
        bool used = g_edicts[i].inuse;
        if (!SaveBytes(f, &used, sizeof(used))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d inuse\n", i); goto done; }
        if (used && !SaveBytes(f, &i, sizeof(i))) { fprintf(stderr, "WC3 SaveGame: failed at edict %d index\n", i); goto done; }
        if (used && !WriteEdict(f, g_edicts + i)) {
            fprintf(stderr, "WC3 SaveGame: failed at edict %d class=%08x\n", i, g_edicts[i].class_id); goto done;
        }
    }
    if (!WritePools(f)) { fprintf(stderr, "WC3 SaveGame: failed at lifecycle pools\n"); goto done; }
    if (!WriteMoveSpatial(f)) { fprintf(stderr,"WC3 SaveGame: failed at fine spatial history\n"); goto done; }
    if (!WriteMoveProximity(f)) { fprintf(stderr,"WC3 SaveGame: failed at proximity spatial history\n"); goto done; }
    if (!WriteMoveShared(f)) { fprintf(stderr,"WC3 SaveGame: failed at shared Move parameters\n"); goto done; }
    if (!WriteMoveGroups(f)) { fprintf(stderr,"WC3 SaveGame: failed at physical Move groups\n"); goto done; }
    /* After edicts: nested HT_HANDLE unit/item slots call G_LoadJassHandle, which
     * requires restored inuse bits. SV_Map runs main() first, so a pre-edict
     * resolve would see baseline slots and drop script-created units. */
    if (!WriteHashtables(f)) { fprintf(stderr, "WC3 SaveGame: failed at hashtables\n"); goto done; }
    if (!WriteJass(f)) { fprintf(stderr, "WC3 SaveGame: failed at jass\n"); goto done; }
    if (!WriteFooter(f)) { fprintf(stderr, "WC3 SaveGame: failed at footer/checksum\n"); goto done; }
    ok = true;
done:
    fclose(f);
    if (!ok) remove(filename);
    return ok;
}

bool ReadGame(cstring_t filename) {
    FILE *f = fopen(filename, "rb");
    saveHeader_t header = { 0 };
    bool current_nonregion_event_slots[MAX_EVENTS] = { 0 };
    uint32_t index;
    int targets[MAX_CLIENTS];

    if (!f) { fprintf(stderr, "WC3 LoadGame: cannot open %s\n", filename); return false; }
    if (!ReadFooter(f)) { fprintf(stderr, "WC3 LoadGame: invalid footer/checksum\n"); fclose(f); return false; }
    if (!LoadBytes(f, &header.magic, sizeof(header.magic)) || !LoadBytes(f, &header.version, sizeof(header.version)) ||
        fseek(f, 0, SEEK_SET)) {
        fprintf(stderr, "WC3 LoadGame: invalid header\n"); fclose(f); return false;
    }
    if (header.version != save_version) {
        fprintf(stderr, "WC3 LoadGame: incompatible save version %u (expected %u)\n", header.version, save_version);
        fclose(f); return false;
    }
    if (!LoadBytes(f, &header, sizeof(header))) {
        fprintf(stderr, "WC3 LoadGame: invalid header\n"); fclose(f); return false;
    }
    {
        uint32_t script = level.vm ? jass_programidentity(level.vm) : 0;
        cstring_t field = NULL;
        if (header.magic != save_magic) field = "magic";
        else if (header.edict_size != sizeof(edict_t)) field = "edict_size";
        else if (header.num_edicts > globals.max_edicts) field = "num_edicts";
        else if (header.max_clients != game.max_clients) field = "max_clients";
        else if (header.script_identity != script) field = "script_identity";
        else if (header.quests != ActiveQuestCount()) field = "quests";
        else if (header.groups < level.num_groups) field = "groups";
        else if (header.triggers < level.num_triggers) field = "triggers";
        else if (header.timers < level.num_timers) field = "timers";
        else if (header.events > MAX_EVENTS) field = "events";
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
    FOR_LOOP(i, MAX_EVENTS) {
        event_t *event = &level.events.handlers[i];
        current_nonregion_event_slots[i] = event->inuse &&
            event->type != EVENT_GAME_ENTER_REGION && event->type != EVENT_GAME_LEAVE_REGION;
    }
    S_ClearMoveGroups();
    S_ClearMoveFineRequests();
    S_ResetAbilityTimers();
    G_ResetWaypointCache();
    S_InvalidateAuraSources();
    /* Retail reload constructs fresh search owners; only maps/stamps and
     * unit-owned curves are saved. Do not reuse an unsaved nearest-node chain. */
    G_FreeMovePathCache();
    G_ClearMoveSpatial();
    if (!ReadMappedFields(f, level_fields, (uint8_t *)&level)) {
        fprintf(stderr, "WC3 LoadGame: failed at level state\n"); fclose(f); return false;
    }
    FOR_LOOP(i,level.num_timers) {
        gtimer_t const *timer=level.timers+i;
        int32_t epoch=(int32_t)(timer->scalar_deadline.epoch-level.pathing_clock.epoch);
        if(timer->scalar_timing && (!isfinite(timer->scalar_timeout) ||
            !isfinite(timer->scalar_period) || !isfinite(timer->scalar_residual) ||
            !isfinite(timer->scalar_paused_remaining) || !isfinite(timer->scalar_deadline.time) ||
            !isfinite(timer->scalar_deadline.span) || timer->scalar_deadline.span<=0 ||
            timer->scalar_segments>UINT16_MAX || timer->scalar_remaining_segments>timer->scalar_segments ||
            (timer->scalar_segmented && !timer->scalar_remaining_segments) ||
            (timer->running && !timer->paused && (epoch < -1 || epoch > 1)))) {
            fprintf(stderr,"WC3 LoadGame: invalid scalar timer at slot %u\n",(unsigned)i);
            fclose(f);return false;
        }
    }
    if(level.timer_clock_valid && (!isfinite(level.timer_clock.time) ||
       !isfinite(level.timer_clock.span) || level.timer_clock.span<=0 ||
       !isfinite(level.timer_source_clock.time) || !isfinite(level.timer_source_clock.span) ||
       level.timer_source_clock.span<=0)) {
        fprintf(stderr,"WC3 LoadGame: invalid timer publication clock\n");fclose(f);return false;
    }
    ClearRuntimeFields(&level, level_fields, FIELD_RUNTIME);
    G_ResetMoveRegionEvents();
    if (level.ai_vm_initialized >> MAX_PLAYERS) { fprintf(stderr,"WC3 LoadGame: invalid AI initialization players\n"); fclose(f); return false; }
    FOR_LOOP(i, MAX_EVENTS) if (current_nonregion_event_slots[i] && !level.events.handlers[i].inuse) {
        fprintf(stderr, "WC3 LoadGame: saved event registry dropped live non-region slot %u\n", (unsigned)i);
        fclose(f); return false;
    }
    if (ActiveEventCount() != header.events) {
        fprintf(stderr, "WC3 LoadGame: event count mismatch saved=%u restored=%u\n",
                (unsigned)header.events, (unsigned)ActiveEventCount());
        fclose(f); return false;
    }
    if (level.waypoints.count > MAX_WAYPOINTS ||
        (level.waypoints.count && (level.waypoints.count != MAX_WAYPOINTS || level.waypoints.cursor >= MAX_WAYPOINTS ||
        header.num_edicts < level.waypoints.count ||
        level.waypoints.base > header.num_edicts - level.waypoints.count)) ||
        (!level.waypoints.count && (level.waypoints.base || level.waypoints.cursor))) {
        fprintf(stderr, "WC3 LoadGame: failed at level state\n"); fclose(f); return false;
    }
    FOR_LOOP(i, MAX_EVENTS) if (level.events.handlers[i].handle_generation > EVENT_HANDLE_GENERATION_MAX ||
        level.events.handlers[i].generation_exhausted > 1) {
        fprintf(stderr, "WC3 LoadGame: invalid event handle generation at slot %u\n", (unsigned)i);
        fclose(f); return false;
    }
    if (!ReadTerrainPathing(f)) { fprintf(stderr, "WC3 LoadGame: failed at terrain pathing state\n"); fclose(f); return false; }
    if (!ReadMoveAdaptive(f)) { fprintf(stderr,"WC3 LoadGame: failed at adaptive publication state\n"); fclose(f); return false; }
    if (!ReadBlight(f)) { fprintf(stderr, "WC3 LoadGame: failed at blight state\n"); fclose(f); return false; }
    G_ResetJassGroupDebug();
    if (!ReadGroups(f, header.groups)) { fclose(f); return false; }
    /* Restore the Q2-style server tick before the next frame; all persisted deadlines use it. */
    gi.SetGameTime(level.time);
    FOR_LOOP(i, game.max_clients) if (!ReadClient(f, game.clients + i, targets + i)) {
        fprintf(stderr, "WC3 LoadGame: failed at client %d\n", i); fclose(f); return false;
    }
    /* The baseline map already linked these same edict addresses. Clear its
     * spatial tree before raw records overwrite their area links, then rebuild
     * one authoritative set below; retaining both creates cyclic area lists. */
    gi.ClearWorld();
    M_ResetMoveMembers();
    /* Release process-owned curve allocations before raw edict records replace their addresses. */
    FOR_LOOP(i,globals.num_edicts) S_FreeMoveRoute(g_edicts+i);
    G_PoolsReset();
    G_ClearEdictStorage(globals.max_edicts);
    G_ResetSpawnCache();
    S_ResetWaygateCache();
    globals.num_edicts = header.num_edicts;
    G_MarkEdictStorageUsed(header.num_edicts);
    FOR_LOOP(i, header.num_edicts) {
        bool used;
        if (!LoadBytes(f, &used, sizeof(used))) {
            fprintf(stderr, "WC3 LoadGame: failed at edict %d inuse\n", i); fclose(f); return false;
        }
        if (!used) continue;
        if (!LoadBytes(f, &index, sizeof(index)) || index >= globals.max_edicts || !ReadEdict(f, g_edicts + index)) {
            fprintf(stderr, "WC3 LoadGame: failed at edict %d data\n", i); fclose(f); return false;
        }
    }
    if (!ReadPools(f)) { fprintf(stderr, "WC3 LoadGame: failed at lifecycle pools\n"); fclose(f); return false; }
    if (!S_RestoreMoveRepulsors()) { fprintf(stderr,"WC3 LoadGame: invalid repulsor owner links\n"); fclose(f); return false; }
    if (!S_ValidateWaygateIds()) { fprintf(stderr,"WC3 LoadGame: invalid Way Gate identities\n"); fclose(f); return false; }
    if (!ReadMoveSpatial(f)) { fprintf(stderr,"WC3 LoadGame: failed at fine spatial history\n"); fclose(f); return false; }
    if (!ReadMoveProximity(f)) { fprintf(stderr,"WC3 LoadGame: failed at proximity spatial history\n"); fclose(f); return false; }
    if (!ValidOwnedUnits()) { fprintf(stderr,"WC3 LoadGame: invalid unit owned-pool order\n"); fclose(f); return false; }
    if (!S_ValidateCaptainHomeActors(true)) { fprintf(stderr,"WC3 LoadGame: invalid captain actor references\n"); fclose(f); return false; }
    if (!ValidMoveFineRequests()) { fprintf(stderr,"WC3 LoadGame: invalid fine-request FIFO\n"); fclose(f); return false; }
    if (!ReadMoveShared(f)) { fprintf(stderr,"WC3 LoadGame: failed at shared Move parameters\n"); fclose(f); return false; }
    if (!ReadMoveGroups(f)) { fprintf(stderr,"WC3 LoadGame: failed at physical Move groups\n"); fclose(f); return false; }
    if (!S_RestoreMoveCoarseRequests()) { fprintf(stderr,"WC3 LoadGame: invalid coarse-request FIFOs\n"); S_ClearMoveGroups(); fclose(f); return false; }
    if (!S_ValidateMoveShared()) {
        fprintf(stderr,"WC3 LoadGame: invalid shared Move references\n");
        S_ClearMoveGroups(); fclose(f); return false;
    }
    /* Nested hashtable unit/item handles resolve here, after edict inuse is restored. */
    if (!ReadHashtables(f)) { fprintf(stderr, "WC3 LoadGame: failed at hashtables\n"); fclose(f); return false; }
    /* Sound-handle presentation state is part of the VM-owned handle payload;
     * the snapshot version rejects older layouts before reconstruction. */
    if (!ReadJass(f)) { fprintf(stderr, "WC3 LoadGame: failed at jass\n"); fclose(f); return false; }
    {
        saveFooter_t footer;
        /* ReadFooter already verified the checksum. The current payload must
         * end exactly here; no optional or unknown trailing records. */
        if (!LoadBytes(f, &footer, sizeof(footer)) || footer.commit != save_commit ||
            fgetc(f) != EOF || ferror(f)) {
            fprintf(stderr, "WC3 LoadGame: payload does not match the current save layout\n");
            fclose(f); return false;
        }
    }
    G_ResetSelectionSoundState();
    G_CommandErrorReset();
    FOR_LOOP(i, game.max_clients) g_edicts[i].client = game.clients + i;
    FOR_LOOP(i, game.max_clients) game.clients[i].camera.target_controller = targets[i] < 0 ? NULL : g_edicts + targets[i];
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        if (ent->inuse && ent->rally_indicator && ent->owner && ent->owner->client)
            ent->owner->client->rally_indicator = ent;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        if (!ent->inuse) continue;
        if (ent->destructable) G_RestoreDestructableData(ent);
        M_TrackMove(ent);
        S_TrackMoveTimers(ent);
        if (gi.LinkEntity) gi.LinkEntity(ent);
    }
    G_RebuildTimerQueue();
    G_RebuildSelectionIndex();
    S_RebuildAbilityTimers();
    G_RebuildSavedMovePathing();
    fclose(f);
    /* Cinefilters are transient client presentation, not part of the save
     * contract. Map reload can leave its baseline black filter displayed;
     * terminate that stale filter before the restored gameplay snapshot is
     * published, otherwise the client remains behind an opaque fade. */
    level.cinefilter.displayed = false;
    /* Configstrings were rebuilt while reloading the map. Re-publish the
     * restored authoritative scene fog before client-side presentation resumes. */
    G_EnvironmentFogPublish();
    /* Client-side decoders are presentation state, not part of the save file.
     * Re-emit the restored semantic music state for clients that remained
     * connected across the load. */
    FOR_LOOP(i, game.max_clients) if (game.clients[i].connected) {
        G_MusicSyncClient(game.clients + i);
        UI_UpdateCursorPresentation(game.clients + i);
    }
    /* svc_layout layers are client presentation state and are not serialized.
     * Force the restored timer-dialog model to republish on the next frame. */
    FOR_LOOP(i, MIN((uint32_t)game.max_clients, (uint32_t)MAX_CLIENTS)) {
        level.timer_dialog_dirty_clients |= 1u << i;
        level.timer_dialog_last_index[i] = -1;
        level.timer_dialog_last_seconds[i] = -1;
        level.leaderboard_dirty_clients |= 1u << i;
    }
    G_DisableStartingResourceCheatForLoadedGame();
    fprintf(stderr, "WC3 LoadGame: restored %s edicts=%u\n", filename, header.num_edicts);
    return true;
}

#ifdef BZ_TESTS
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);

TEST(wc3_save, adaptive_publication_rejects_invalid_shape_class_and_truncation) {
    reset_entities(); setup_test_world();
    uint32_t size=G_GetMoveAdaptiveStateSize();
    uint8_t *before=malloc(size),*after=malloc(size);
    T_ASSERT(before && after); if(!before || !after){free(before);free(after);return;}
    T_ASSERT(G_GetMoveAdaptiveState(before,size));
    FOR_LOOP(i,6) {
        FILE *file=tmpfile();T_NOT_NULL(file);if(!file)break;
        moveAdaptiveHeader_t header=MoveAdaptiveHeader();
        if(i==0)header.size++;
        if(i==1)header.maps[0].x++;
        if(i==2)header.maps[3].y++;
        memcpy(after,before,size);
        if(i==3)after[0]=3;
        /* Marker IDs use all byte values; retain rejection at the last class byte. */
        if(i==4)after[size-header.maps[0].x*header.maps[0].y-1]=255;
        T_ASSERT(SaveBytes(file,&header,sizeof(header)));
        T_ASSERT(SaveBytes(file,after,size-(i==5)));
        rewind(file);T_ASSERT(!ReadMoveAdaptive(file));fclose(file);
        T_ASSERT(G_GetMoveAdaptiveState(after,size));T_ASSERT(!memcmp(before,after,size));
    }
    FILE *file=tmpfile();T_NOT_NULL(file);
    if(file){T_ASSERT(WriteMoveAdaptive(file));rewind(file);T_ASSERT(ReadMoveAdaptive(file));fclose(file);}
    free(before);free(after);reset_entities();setup_test_world();
}

TEST(wc3_save, spell_approach_callback_uses_current_roster_identity) {
    int const index = SaveCFunctionIndex((void *)S_SpellTargetApproachThink);

    T_ASSERT(index > 0);
    if (index > 0 && index <= (int)(sizeof(save_cfunctions) / sizeof(save_cfunctions[0])))
        T_STREQ(save_cfunctions[index - 1].name, "S_SpellTargetApproachThink");
}

TEST(wc3_save, disabled_player_abilities_grow_and_round_trip) {
    static char const digits[] = "0123456789";
    cstring_t const filename = "/tmp/openwarcraft3-wc3-disabled-abilities-save.bin";
    enum { ABILITY_COUNT = 96 };
    gameClient_t *client;
    uint32_t abilities[ABILITY_COUNT];

    reset_entities();
    setup_test_world();
    client = &game.clients[0];
    for (uint32_t i = 0; i < ABILITY_COUNT; i++) {
        abilities[i] = MAKEFOURCC('A', '0', digits[i / 10], digits[i % 10]);
        G_SetPlayerAbilityAvailable(client, abilities[i], false);
    }
    T_EQ(client->jass.disabled_ability_count, (uint32_t)ABILITY_COUNT);
    T_ASSERT(WriteGame(filename));

    G_SetPlayerAbilityAvailable(client, abilities[32], true);
    G_SetPlayerAbilityAvailable(client, MAKEFOURCC('A', '0', '9', '9'), false);
    T_ASSERT(ReadGame(filename));
    client = &game.clients[0];
    T_EQ(client->jass.disabled_ability_count, (uint32_t)ABILITY_COUNT);
    FOR_LOOP(i, ABILITY_COUNT) T_ASSERT(!G_IsPlayerAbilityAvailable(client, abilities[i]));
    T_ASSERT(G_IsPlayerAbilityAvailable(client, MAKEFOURCC('A', '0', '9', '9')));

    G_SetPlayerAbilityAvailable(client, abilities[32], true);
    T_ASSERT(G_IsPlayerAbilityAvailable(client, abilities[32]));
    T_ASSERT(!G_IsPlayerAbilityAvailable(client, abilities[95]));
    remove(filename);
}

static bool write_save_fixture_header(cstring_t source_path, cstring_t output_path, uint32_t version, uint32_t edict_size) {
    uint8_t buffer[4096];
    saveHeader_t header;
    long payload;
    FILE *source = fopen(source_path, "rb"), *output = NULL;
    if (!source || fseek(source, 0, SEEK_END) || (payload = ftell(source)) < (long)sizeof(saveFooter_t) ||
        fseek(source, 0, SEEK_SET) || !LoadBytes(source, &header, sizeof(header))) goto fail;
    payload -= sizeof(saveFooter_t);
    header.version = version;
    header.edict_size = edict_size;
    output = fopen(output_path, "w+b");
    if (!output || !SaveBytes(output, &header, sizeof(header))) goto fail;
    for (long remaining = payload - (long)sizeof(header); remaining > 0;) {
        size_t size = MIN((size_t)remaining, sizeof(buffer));
        if (!LoadBytes(source, buffer, size) || !SaveBytes(output, buffer, size)) goto fail;
        remaining -= (long)size;
    }
    if (!WriteFooter(output)) goto fail;
    fclose(source); fclose(output);
    return true;
fail:
    if (source) fclose(source);
    if (output) fclose(output);
    remove(output_path);
    return false;
}

TEST(wc3_save, rejects_invalid_captain_actor_reference) {
    cstring_t names[]={"movement.captain_home.actor","movement.captain_home.roster_actor"};
    FOR_LOOP(i,2) {
        field_t const *field=NULL;
        for(field_t const *f=edict_fields;f->name;f++)if(!strcmp(f->name,names[i]))field=f;
        T_NOT_NULL(field);if(!field)continue;
        edict_t raw={0};
        int index=globals.num_edicts;
        memcpy((uint8_t *)&raw+field->ofs,&index,sizeof(index));
        T_ASSERT(!ReadField(field,(uint8_t *)&raw));
        index=-2;
        memcpy((uint8_t *)&raw+field->ofs,&index,sizeof(index));
        T_ASSERT(!ReadField(field,(uint8_t *)&raw));
    }
}

/* Thousands of orders may precede the event pass. Preserve their unread
 * point payloads and FIFO order through wrap and save relocation. */
TEST(wc3_save, thousands_of_pending_point_orders_survive_save) {
    reset_entities(); setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,128);
    level.events.read=level.events.write=MAX_EVENT_QUEUE-17;
    uint32_t published=0;
    FOR_LOOP(i,4096) {
        vec2_t point={(float)i+.25f,-(float)i-.75f};
        gameEvent_t *event=G_PublishEventWithPoint(&(gameEventPointParams_t){
            .edict=unit,.type=EVENT_UNIT_ISSUED_POINT_ORDER,.value=(int32_t)i,.point=&point});
        if(!event)break;
        published++;
    }
    T_EQ(published,4096u);
    if(published!=4096) {reset_entities();return;}
    cstring_t file="/tmp/wc3-thousands-point-events.bin";
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    T_EQ(level.events.read,0u); T_EQ(level.events.write,published);
    FOR_LOOP(i,published) {
        gameEvent_t const *event=level.events.queue+i;
        T_ASSERT(event->edict==unit);
        T_EQ(event->type,EVENT_UNIT_ISSUED_POINT_ORDER);
        T_EQ(event->value,(int32_t)i); T_ASSERT(event->has_point);
        T_EQ(event->point.x,(float)i+.25f); T_EQ(event->point.y,-(float)i-.75f);
    }
    G_RunEvents(); T_EQ(level.events.read,level.events.write);
    remove(file); reset_entities();
}

/* Save before recruitment, then insert a new unit after loading: both high
 * sequence words and allocator state must survive, with corrupt order rejected. */
TEST(wc3_save, owned_pool_order_survives_save_and_rejects_invalid_sequences) {
    reset_entities(); setup_test_world();
    level.next_unit_seq=(uint64_t)UINT32_MAX+42;
    edict_t *first=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,128);
    edict_t *second=alloc_test_unit(MAKEFOURCC('h','f','o','o'),256,128);
    G_SetUnitPlayer(first,1); G_SetUnitPlayer(first,0);
    uint64_t order=first->own_seq,next=level.next_unit_seq,peer=second->own_seq;
    T_ASSERT(order>peer);
    cstring_t file="/tmp/wc3-owned-pool-order.bin";
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    T_EQ(first->own_seq,order); T_EQ(second->own_seq,peer); T_EQ(level.next_unit_seq,next);
    G_SetUnitPlayer(first,0); T_EQ(first->own_seq,order); T_EQ(level.next_unit_seq,next);
    edict_t *new=alloc_test_unit(MAKEFOURCC('h','f','o','o'),384,128);
    T_EQ(new->own_seq,next+1); T_ASSERT(ValidOwnedUnits());
    new->own_seq=first->own_seq; T_ASSERT(!ValidOwnedUnits()); T_ASSERT(!WriteGame(file));
    new->own_seq=level.next_unit_seq+1; T_ASSERT(!ValidOwnedUnits()); T_ASSERT(!WriteGame(file));
    new->own_seq=0; new->svflags|=SVF_MONSTER;
    T_ASSERT(!ValidOwnedUnits()); T_ASSERT(!WriteGame(file));
    remove(file); reset_entities(); setup_test_world();
}

/* The raw curve tail is untrusted even after the outer checksum succeeds.
 * Reject impossible extents/indices, nonfinite points and truncated payloads
 * without retaining serialized process addresses or failed allocations. */
TEST(wc3_save, adaptive_route_extents_preserve_all_ushort_parent_identities) {
    vec2_t *points=malloc(BZ_WC3_ACC_ROUTE_NODES*sizeof(*points));T_NOT_NULL(points);if(!points)return;
    FOR_LOOP(i,BZ_WC3_ACC_ROUTE_NODES)points[i]=(vec2_t){(float)i+.25f,(float)i+.75f};
    moveFineRoute_t written={.adaptive_points=points,.adaptive_count=BZ_WC3_ACC_ROUTE_NODES,
        .adaptive_index=BZ_WC3_ACC_ROUTE_NODES-1,.group_points=points,.group_count=BZ_WC3_ACC_ROUTE_NODES,
        .group_index=BZ_WC3_ACC_ROUTE_NODES-1},restored=written;
    FILE *file=tmpfile();T_NOT_NULL(file);if(!file){free(points);return;}
    T_ASSERT(WriteMoveRouteBuffers(file,&written));rewind(file);T_ASSERT(ReadMoveRouteBuffers(file,&restored));
    T_ASSERT(restored.adaptive_points && !memcmp(points,restored.adaptive_points,BZ_WC3_ACC_ROUTE_NODES*sizeof(*points)));
    T_ASSERT(restored.group_points && !memcmp(points,restored.group_points,BZ_WC3_ACC_ROUTE_NODES*sizeof(*points)));
    free(restored.adaptive_points);free(restored.group_points);fclose(file);free(points);
}

TEST(wc3_save, route_capacities_are_rebuilt_from_logical_payloads) {
    vec2_t points[]={{1.25f,2.75f},{3.25f,4.75f},{5.25f,6.75f},{7.25f,8.75f},{9.25f,10.75f}};
    moveFineRoute_t written={.points=points,.count=3,.index=2,.capacity=1024,
        .adaptive_points=points,.adaptive_count=4,.adaptive_index=3,.adaptive_capacity=2048,
        .group_points=points,.group_count=5,.group_index=4,.group_capacity=4096},restored=written;
    FILE *file=tmpfile();T_NOT_NULL(file);if(!file)return;
    T_ASSERT(WriteMoveRouteBuffers(file,&written));
    T_EQ(ftell(file),sizeof(vec2_t)*12);
    rewind(file);T_ASSERT(ReadMoveRouteBuffers(file,&restored));
    T_EQ(restored.capacity,128);T_EQ(restored.adaptive_capacity,128);T_EQ(restored.group_capacity,128);
    T_ASSERT(!memcmp(points,restored.points,3*sizeof(*points)));
    T_ASSERT(!memcmp(points,restored.adaptive_points,4*sizeof(*points)));
    T_ASSERT(!memcmp(points,restored.group_points,5*sizeof(*points)));
    vec2_t *backing=restored.points;
    G_ReserveMoveRouteBuffer(&restored.points,&restored.capacity,2);
    T_ASSERT(backing==restored.points);T_EQ(restored.capacity,128);
    free(restored.points);free(restored.adaptive_points);free(restored.group_points);fclose(file);
}

TEST(wc3_save, rejects_invalid_fine_route_payloads) {
    FOR_LOOP(i,12) {
        FILE *file=tmpfile(); T_NOT_NULL(file); if (!file) continue;
        edict_t raw={0},restored={0};
        raw.movement.fine_route=(moveFineRoute_t){.points=(vec2_t *)(uintptr_t)1,.count=1,.mask=2};
        if (i==0) raw.movement.fine_route.count=BZ_WC3_FINE_NODES+1;
        if (i==1) raw.movement.fine_route.index=1;
        if (i>=4 && i<8) {
            raw.movement.fine_route.adaptive_points=(vec2_t *)(uintptr_t)1;
            raw.movement.fine_route.adaptive_count=1;
            if (i==4) raw.movement.fine_route.adaptive_count=BZ_WC3_ACC_ROUTE_NODES+1;
            if (i==5) raw.movement.fine_route.adaptive_index=1;
        }
        if (i>=8) {
            raw.movement.fine_route.group_points=(vec2_t *)(uintptr_t)1;
            raw.movement.fine_route.group_count=1;
            if (i==8) raw.movement.fine_route.group_count=BZ_WC3_ACC_ROUTE_NODES+1;
            if (i==9) raw.movement.fine_route.group_index=1;
        }
        T_ASSERT(SaveBytes(file,&raw,sizeof(raw)));
        if (i==2) T_ASSERT(SaveBytes(file,&(vec2_t){NAN,0},sizeof(vec2_t)));
        if (i>=4 && i<8) T_ASSERT(SaveBytes(file,&(vec2_t){1,2},sizeof(vec2_t)));
        if (i==6) T_ASSERT(SaveBytes(file,&(vec2_t){NAN,0},sizeof(vec2_t)));
        if (i>=8) T_ASSERT(SaveBytes(file,&(vec2_t){1,2},sizeof(vec2_t)));
        if (i==10) T_ASSERT(SaveBytes(file,&(vec2_t){NAN,0},sizeof(vec2_t)));
        rewind(file);
        T_ASSERT(!ReadEdict(file,&restored));
        T_ASSERT(!restored.movement.fine_route.points);
        T_ASSERT(!restored.movement.fine_route.adaptive_points);
        T_ASSERT(!restored.movement.fine_route.group_points);
        fclose(file);
    }
}

TEST(wc3_save, fine_spatial_rectangles_reject_invalid_records) {
    reset_entities();setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    wc3FineBox_t good={{3,4},{4,5}};
    FOR_LOOP(c,9) {
        FILE *file=tmpfile();T_NOT_NULL(file);if(!file)continue;
        wc3FineBox_t box=good;
        uint32_t count=c==7 ? globals.num_edicts+1 : c==6 ? 2 : 1;
        uint32_t index=c==0 ? globals.num_edicts : c==1 ? 0 : unit->s.number;
        if(c==2)box.max.x=box.min.x;
        if(c==3)box.max.y=box.min.y-1;
        if(c==4)box.max.x=box.min.x+5;
        if(c==5){box.min.x=INT32_MIN;box.max.x=INT32_MAX;}
        uint32_t stamp=7,query=11,search_stamp=4;T_ASSERT(SaveBytes(file,&query,sizeof(query)));
        T_ASSERT(SaveBytes(file,&search_stamp,sizeof(search_stamp)));
        T_ASSERT(SaveBytes(file,&count,sizeof(count)));
        T_ASSERT(SaveBytes(file,&index,sizeof(index)));
        T_ASSERT(SaveBytes(file,&(uint32_t){1},sizeof(uint32_t)));
        if(c!=8){T_ASSERT(SaveBytes(file,&box,sizeof(box)));T_ASSERT(SaveBytes(file,&stamp,sizeof(stamp)));}
        T_ASSERT(SaveBytes(file,&(uint32_t){0},sizeof(uint32_t)));
        if(c==6){T_ASSERT(SaveBytes(file,&index,sizeof(index)));T_ASSERT(SaveBytes(file,&(uint32_t){1},sizeof(uint32_t)));T_ASSERT(SaveBytes(file,&box,sizeof(box)));T_ASSERT(SaveBytes(file,&stamp,sizeof(stamp)));}
        rewind(file);T_ASSERT(!ReadMoveSpatial(file));
        T_EQ(G_GetMoveSpatialSerial(),0);
        fclose(file);
    }
    T_ASSERT(G_LoadMoveSpatialObject(unit->s.number,&good));
    FILE *file=tmpfile();T_NOT_NULL(file);
    if(file){T_ASSERT(WriteMoveSpatial(file));T_EQ(ftell(file),sizeof(uint32_t)*7+sizeof(good));
        G_ClearMoveSpatial();rewind(file);T_ASSERT(ReadMoveSpatial(file));
        T_EQ(G_GetMoveSpatialSerial(),1);
        T_EQ(G_GetMoveSpatialObject(unit->s.number)->stamp,1);fclose(file);}
    reset_entities();setup_test_world();
}

TEST(wc3_save, mixed_sparse_regions_reload_in_owner_order_and_retain_inverse_pixels) {
    extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
    extern void CM_SetupTestWorldBounds(box2_t const *);
    FOR_LOOP(order,2) {
        reset_entities();setup_test_world();uint8_t terrain[16*16]={0};
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});CM_SetupTestPathmap(16,16,terrain);
        edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),224,224);unit->svflags|=SVF_MONSTER;
        unit->collision=31;unit->s.model=1;
        edict_t *widget=G_Spawn();widget->s.origin2=(vec2_t){208,208};widget->s.model=1;
        struct {uint16_t width,height;color32_t map[9];} texture={3,3,{
            {255,0,255,255},{0,0,0,255},{255,0,255,255},
            {0,0,0,255},{255,0,255,255},{0,0,0,255},
            {255,0,255,255},{0,0,0,255},{255,0,255,255}}};
        widget->pathtex=(pathTex_t *)&texture;
        if(order){S_PublishMoveRegions(widget);G_PublishMoveSpatialObject(unit);}
        else {G_PublishMoveSpatialObject(unit);S_PublishMoveRegions(widget);}
        wc3SpatialRecords_t *map=S_GetMoveFineSpatial();T_EQ(map->records,19);
        FILE *file=tmpfile();T_NOT_NULL(file);if(!file)continue;
        T_ASSERT(WriteMoveSpatial(file));uint32_t stamp=map->query;
        rewind(file);T_ASSERT(ReadMoveSpatial(file));fclose(file);T_EQ(map->query,stamp);T_EQ(map->records,19);
        wc3RegionCollection_t const *regions=S_GetMoveRegions(widget-g_edicts);T_EQ(regions->count,3);
        uint32_t head=map->cells[6*16+6]&WC3_RECORD_END;
        T_EQ(map->links[head].payload,regions->objects[2]); /* widget saved after unit */
        FOR_LOOP(i,3)T_EQ(wc3_records_object(map,regions->objects[i])->refs,5);
        wc3CellQuery_t query={.mode=WC3_CELL_FINE,.mask=0x02000002,.target=WC3_RECORD_END};
        T_EQ(wc3_records_cell(map,(wc3FinePoint_t){6,5},0,&query).value,1); /* real hole */
        T_EQ(wc3_records_cell(map,(wc3FinePoint_t){5,5},0,&query).value,0);
        S_UnrasterMoveRegions(widget);T_EQ(map->records,34);wc3_records_compact(map,false);T_EQ(map->records,4);
        reset_entities();setup_test_world();
    }
}

TEST(wc3_save, fine_work_bound_rejects_unreachable_saved_wrap_state) {
    bool saved_policy=level.move_fine_responsive;
    reset_entities();setup_test_world();S_ClearMoveFineRequests();
    cstring_t file="/tmp/wc3-fine-work-bound.bin";
    moveFineBudget_t *budget=level.move_fine_budgets;
    FOR_LOOP(mode,2) {
        level.move_fine_responsive=mode!=0;
        budget->limit=mode ? 4*(BZ_WC3_UNIT_FINE_WORK+1u) : BZ_WC3_FINE_OWNER_WORK;
        uint32_t bound=budget->limit+BZ_WC3_UNIT_FINE_WORK+1u;
        budget->work=bound;
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
        T_EQ(budget->work,bound);
        budget->work=bound+1;T_ASSERT(!WriteGame(file));
        budget->work=UINT32_MAX;T_ASSERT(!WriteGame(file));
        budget->work=0;T_ASSERT(WriteGame(file));
    }
    remove(file);reset_entities();setup_test_world();level.move_fine_responsive=saved_policy;
}

TEST(wc3_save, responsive_fine_grant_and_policy_continue_after_restore) {
    bool saved_policy=level.move_fine_responsive;
    reset_entities(); setup_test_world();
    level.move_fine_responsive=true;
    edict_t *units[4];
    moveFineBudget_t *budget=level.move_fine_budgets;
    budget->work=BZ_WC3_FINE_OWNER_WORK+1;
    FOR_LOOP(i,4) {
        units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128+64*i,128);
        T_ASSERT(!S_AdmitUnitMoveFineRequest(units[i]));
    }
    S_BeginAbilityOwnerUpdates();
    T_EQ(budget->limit,4*(BZ_WC3_UNIT_FINE_WORK+1));
    T_ASSERT(S_AdmitUnitMoveFineRequest(units[0]));
    S_ChargeUnitMoveFineRequest(units[0],BZ_WC3_UNIT_FINE_WORK+1);
    cstring_t file="/tmp/openwarcraft3-responsive-fine.bin";
    T_ASSERT(WriteGame(file));
    S_ClearMoveFineRequests(); level.move_fine_responsive=false;
    T_ASSERT(ReadGame(file));
    T_ASSERT(level.move_fine_responsive); T_ASSERT(ValidMoveFineRequests());
    T_EQ(budget->limit,4*(BZ_WC3_UNIT_FINE_WORK+1));
    T_EQ(budget->work,BZ_WC3_UNIT_FINE_WORK+1); T_EQ(budget->countdown,1);
    T_EQ(budget->count,3);
    FOR_LOOP(i,3) {
        T_ASSERT(S_AdmitUnitMoveFineRequest(units[i+1]));
        S_ChargeUnitMoveFineRequest(units[i+1],BZ_WC3_UNIT_FINE_WORK+1);
    }
    T_EQ(budget->count,0); T_ASSERT(ValidMoveFineRequests());
    uint32_t grant=budget->limit;
    budget->limit=1; T_ASSERT(!ValidMoveFineRequests());
    budget->limit=UINT32_MAX; T_ASSERT(!ValidMoveFineRequests());
    budget->limit=grant; level.move_fine_responsive=false; T_ASSERT(!ValidMoveFineRequests());
    level.move_fine_responsive=true; T_ASSERT(ValidMoveFineRequests());
    remove(file); reset_entities(); setup_test_world(); level.move_fine_responsive=saved_policy;
}

/* A pending fine FIFO and its independent interval clock must survive a real save. */
TEST(wc3_save, fine_request_fifo_and_interval_continue_after_restore) {
    reset_entities(); setup_test_world();
    edict_t *first=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,128);
    edict_t *second=alloc_test_unit(MAKEFOURCC('h','f','o','o'),256,128);
    cstring_t file="/tmp/openwarcraft3-fine-request-fifo.bin";
    level.pathing_counter=2000;
    level.move_fine_budgets[0].work=BZ_WC3_FINE_OWNER_WORK+1;
    level.move_fine_budgets[0].countdown=1;
    T_ASSERT(!S_AdmitUnitMoveFineRequest(first));
    T_ASSERT(!S_AdmitUnitMoveFineRequest(second));
    T_ASSERT(!S_AdmitUnitMoveFineRequest(first));
    T_EQ(level.move_fine_budgets[0].count,2);
    T_EQ(first->movement.fine_request_time,0);
    T_ASSERT(WriteGame(file));
    S_ClearMoveFineRequests();
    S_ResetAbilityTimers();
    T_ASSERT(ReadGame(file));
    T_ASSERT(ValidMoveFineRequests());
    T_EQ(level.pathing_counter,2000); T_EQ(level.move_fine_budgets[0].countdown,1);
    T_EQ(level.move_fine_budgets[0].work,BZ_WC3_FINE_OWNER_WORK+1);
    T_EQ(level.move_fine_budgets[0].head,first); T_EQ(level.move_fine_budgets[0].tail,second);
    T_EQ(first->movement.fine_next,second); T_EQ(second->movement.fine_prev,first);
    level.move_fine_budgets[0].work=BZ_WC3_FINE_OWNER_WORK;
    T_ASSERT(!S_AdmitUnitMoveFineRequest(second));
    T_ASSERT(S_AdmitUnitMoveFineRequest(first));
    T_EQ(first->movement.fine_request_time,2000);
    T_ASSERT(WriteGame(file));
    first->movement.fine_request_time=0; level.pathing_counter=0;
    T_ASSERT(ReadGame(file));
    T_EQ(first->movement.fine_request_time,2000); T_EQ(level.pathing_counter,2000);
    T_EQ(level.move_fine_budgets[0].head,second); T_EQ(level.move_fine_budgets[0].count,1);
    T_ASSERT(S_AdmitUnitMoveFineRequest(second));
    T_ASSERT(!S_AdmitUnitMoveFineRequest(first));
    level.pathing_counter=2009; T_ASSERT(!S_AdmitUnitMoveFineRequest(first));
    level.pathing_counter=2010; T_ASSERT(S_AdmitUnitMoveFineRequest(first));
    T_EQ(first->movement.fine_request_time,2010);
    level.pathing_counter=5; T_ASSERT(S_AdmitUnitMoveFineRequest(first));
    T_EQ(first->movement.fine_request_time,5);
    T_ASSERT(ValidMoveFineRequests());
    remove(file); reset_entities(); setup_test_world();
}

TEST(wc3_save, rejects_invalid_fine_request_graphs) {
    reset_entities(); setup_test_world();
    edict_t *first=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,128);
    edict_t *second=alloc_test_unit(MAKEFOURCC('h','f','o','o'),256,128);
    level.move_fine_budgets[0].work=BZ_WC3_FINE_OWNER_WORK+1;
    T_ASSERT(!S_AdmitUnitMoveFineRequest(first)); T_ASSERT(!S_AdmitUnitMoveFineRequest(second));
    typeof(level.move_fine_budgets[0]) original=level.move_fine_budgets[0];
    FOR_LOOP(i,12) {
        level.move_fine_budgets[0]=original;
        first->movement.fine_prev=NULL; first->movement.fine_next=second; first->movement.fine_queued=true;
        second->movement.fine_prev=first; second->movement.fine_next=NULL; second->movement.fine_queued=true;
        if (i==0) level.move_fine_budgets[0].count=1;
        if (i==1) level.move_fine_budgets[0].tail=first;
        if (i==2) level.move_fine_budgets[0].head=(edict_t *)(uintptr_t)1;
        if (i==3) first->movement.fine_next=(edict_t *)(uintptr_t)1;
        if (i==4) second->movement.fine_prev=NULL;
        if (i==5) second->movement.fine_next=first;
        if (i==6) first->movement.fine_next=NULL;
        if (i==7) second->movement.fine_queued=false;
        if (i==8) *(uint8_t *)&second->movement.fine_queued=2;
        if (i==9) level.move_fine_budgets[0].countdown=2;
        if (i==10) level.move_fine_budgets[0].count=globals.num_edicts+1;
        if (i==11) second->inuse=false;
        T_ASSERT(!ValidMoveFineRequests());
        T_ASSERT(!WriteGame("/tmp/openwarcraft3-invalid-fine-request.bin"));
        second->inuse=true;
    }
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(wc3_save, rejects_invalid_shared_move_payloads) {
    reset_entities(); setup_test_world(); S_ClearMoveGroups();
    uint64_t old_id=level.next_move_shared_id; level.next_move_shared_id=7;
    FOR_LOOP(i,13) {
        FILE *file=tmpfile(); T_NOT_NULL(file); if (!file) continue;
        uint32_t count=i==0 ? MAX_SAVE_GROUP_HANDLES+1 : i==10 ? 2 : 1;
        moveShared_t raw={.id=7,.inuse=true,.speed=150,.next_speed=270,.radius=63};
        if(i==1)raw.id=0;
        if(i==2)raw.id=8;
        if(i==3)raw.inuse=false;
        if(i==4)raw.speed=NAN;
        if(i==5)raw.speed=-1;
        if(i==6)raw.next_speed=INFINITY;
        if(i==7)raw.next_speed=-1;
        if(i==8)raw.radius=NAN;
        if(i==9)raw.radius=-1;
        if(i==12)*(uint8_t *)&raw.inuse=2;
        T_ASSERT(SaveBytes(file,&count,sizeof(count)));
        if(i!=11)T_ASSERT(WriteMappedFields(file,move_shared_fields,(uint8_t *)&raw));
        if(i==10)T_ASSERT(WriteMappedFields(file,move_shared_fields,(uint8_t *)&raw));
        rewind(file); T_ASSERT(!ReadMoveShared(file));
        T_NULL(level.move_shared); T_EQ(ARRAY_COUNT(level.move_shared),0); T_EQ(level.move_shared_capacity,0);
        fclose(file);
    }
    FILE *file=tmpfile(); T_NOT_NULL(file);
    if(file) {
        uint32_t count=1;
        moveShared_t raw={.id=7,.references=1,.inuse=true,.speed=FLT_MAX,.next_speed=150,.radius=31};
        T_ASSERT(SaveBytes(file,&count,sizeof(count)));
        T_ASSERT(WriteMappedFields(file,move_shared_fields,(uint8_t *)&raw));
        rewind(file); T_ASSERT(ReadMoveShared(file));
        /* The record is readable, but an orphan reference must reject the
         * completed registry. Zero references await the next owner prepass. */
        T_ASSERT(!S_ValidateMoveShared()); T_ASSERT(!WriteMoveShared(file));
        level.move_shared[0].references=0; T_ASSERT(S_ValidateMoveShared());
        rewind(file); T_ASSERT(WriteMoveShared(file));
        S_ClearMoveGroups(); rewind(file); T_ASSERT(ReadMoveShared(file));
        T_ASSERT(S_ValidateMoveShared()); T_EQ(level.move_shared[0].id,7);
        T_EQ(level.move_shared[0].speed,FLT_MAX); T_EQ(level.move_shared[0].next_speed,150);
        T_EQ(level.move_shared[0].radius,31); fclose(file);
    }
    S_ClearMoveGroups(); level.next_move_shared_id=old_id;
    reset_entities(); setup_test_world();
}

/* Current mapped group records reject bad ownership, generations and curve
 * tails even when their outer container can be read successfully. */
TEST(wc3_save, rejects_invalid_physical_group_payloads) {
    reset_entities(); setup_test_world(); level.waypoints=(typeof(level.waypoints)){0};
    edict_t *first=alloc_test_unit(MAKEFOURCC('h','p','e','a'),128,128);
    edict_t *second=alloc_test_unit(MAKEFOURCC('h','p','e','a'),256,128);
    vec2_t point={512,512};
    groupPointOrder_t request={.units={{first,first->spawn_time},{second,second->spawn_time}},
        .count=2,.order="move",.order_id=G_OrderId("move"),.point=&point};
    T_ASSERT(G_IssueGroupPointOrder(&request)); T_EQ(ARRAY_COUNT(level.move_groups),1);
    moveGroup_t original=*level.move_groups[0];
    S_ClearMoveGroups();
    FOR_LOOP(i,25) {
        FILE *file=tmpfile(); T_NOT_NULL(file); if (!file) continue;
        moveGroup_t raw=original; uint32_t count=i==0 ? globals.num_edicts+1 : i==13 ? 2 : 1;
        raw.route.points=raw.route.adaptive_points=raw.route.group_points=(vec2_t *)(uintptr_t)1;
        if (i==1) raw.count=0;
        if (i==2) raw.members[0].spawn++;
        if (i==3) raw.members[0].unit=NULL;
        if (i==4) raw.members[1]=raw.members[0];
        if (i==5) raw.id++;
        if (i==6) raw.goal.x=NAN;
        if (i==14) raw.sequence=0;
        if (i==15) raw.sequence=level.next_move_group_sequence+1;
        if (i==16) raw.members[0].arrival_range=NAN;
        if (i==17) raw.members[0].arrival_range=-1;
        if (i==18) raw.target_refresh=16;
        if (i==19) raw.flags|=0x1000;
        if (i==20) raw.shared_id=1;
        if (i==21) raw.cooldown=67;
        if (i==22) *(uint8_t *)&raw.individual=2;
        if (i==23) raw.individual=true; /* A private route cannot own two members. */
        if (i==24) {raw.count=1;raw.individual=true;raw.target=second;raw.flags|=0x1000;}
        if (i>=7 && i<13) { raw.route.group_count=1; raw.route.group_index=0; }
        if (i==7) raw.route.group_count=BZ_WC3_ACC_ROUTE_NODES+1;
        if (i==8) raw.route.group_index=1;
        T_ASSERT(SaveBytes(file,&count,sizeof(count)));
        T_ASSERT(WriteMappedFields(file,move_group_fields,(uint8_t *)&raw));
        if (i==9) T_ASSERT(SaveBytes(file,&(vec2_t){NAN,0},sizeof(vec2_t)));
        if (i==11 || i==12) {
            /* The counted member payload has one extent and stable indices. */
            field_t prefix[sizeof(move_group_fields)/sizeof(*move_group_fields)];
            memcpy(prefix,move_group_fields,sizeof(prefix));
            FOR_LOOP(f,sizeof(prefix)/sizeof(*prefix)) if (prefix[f].name && !strcmp(prefix[f].name,"members")) {
                prefix[f].name=NULL; break;
            }
            FILE *head=tmpfile(); T_NOT_NULL(head);
            if (head) {
                T_ASSERT(WriteMappedFields(head,prefix,(uint8_t *)&raw));
                long offset=sizeof(count)+ftell(head)+(i==11 ? sizeof(raw.count) : 0);
                fclose(head); T_EQ(fseek(file,offset,SEEK_SET),0);
                int index=i==11 ? (int)globals.max_edicts : BZ_WC3_GROUP_ORDER_UNITS+1;
                T_ASSERT(SaveBytes(file,&index,sizeof(index)));
            }
        }
        if (i==13) T_ASSERT(WriteMappedFields(file,move_group_fields,(uint8_t *)&raw));
        rewind(file); bool loaded=ReadMoveGroups(file);
        if (i==1) {
            /* Empty owners await their next Move visit. The old rejection
             * expectation left this live registry in place for the next read. */
            T_ASSERT(loaded); T_EQ(ARRAY_COUNT(level.move_groups),1);
            if (loaded) T_EQ(level.move_groups[0]->count,0);
            S_ClearMoveGroups();
        } else T_ASSERT(!loaded);
        T_NULL(level.move_groups); T_EQ(ARRAY_COUNT(level.move_groups),0); T_EQ(level.move_group_capacity,0);
        fclose(file);
    }
    moveGroup_t follow=original;
    follow.count=1; follow.target=second; follow.target_spawn=second->spawn_time;
    follow.target_refresh=16; follow.flags|=0x1000;
    first->movement.follow_target=S_SetMoveGoal(first, &first->goalentity, second);
    T_ASSERT(ValidMoveGroup(&follow));
    FOR_LOOP(i,5) {
        moveGroup_t invalid=follow;
        if(i==0)invalid.target_spawn++;
        if(i==1)invalid.target_refresh=-1;
        if(i==2)invalid.target_refresh=298;
        if(i==3)invalid.flags&=~0x1000u;
        if(i==4)first->movement.follow_target=NULL;
        T_ASSERT(!ValidMoveGroup(&invalid));
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_save, rejects_previous_combat_cargo_format_before_restoring_world) {
    cstring_t filename = "/tmp/openwarcraft3-save-current-format.bin";
    cstring_t old_filename = "/tmp/openwarcraft3-save-previous-combat-cargo-format.bin";
    saveHeader_t header;
    char map[sizeof(((saveHeader_t *)0)->map_path)];
    setup_test_world();
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    T_ASSERT(WriteGame(filename));
    FILE *f = fopen(filename, "rb");
    T_NOT_NULL(f);
    if (!f) return;
    T_ASSERT(LoadBytes(f, &header, sizeof(header)));
    fclose(f);
    T_ASSERT(write_save_fixture_header(filename, old_filename, 56, header.edict_size));
    unit->user_data = 777;
    T_ASSERT(!G_GetSaveMap(old_filename, map, sizeof(map)));
    T_ASSERT(!ReadGame(old_filename));
    T_EQ(unit->user_data, 777);
    remove(filename); remove(old_filename);
}

TEST(wc3_save, rejects_layout_mismatch_before_selecting_map) {
    cstring_t filename = "/tmp/openwarcraft3-save-current-layout.bin";
    cstring_t bad_filename = "/tmp/openwarcraft3-save-layout-mismatch.bin";
    char map[sizeof(((saveHeader_t *)0)->map_path)];
    setup_test_world();
    reset_entities();
    T_ASSERT(WriteGame(filename));
    T_ASSERT(write_save_fixture_header(filename, bad_filename, save_version, sizeof(edict_t) - 1));
    T_ASSERT(!G_GetSaveMap(bad_filename, map, sizeof(map)));
    T_ASSERT(!ReadGame(bad_filename));
    remove(filename); remove(bad_filename);
}

TEST(wc3_save, rejects_prior_save_versions) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-save-prior-format.bin";
    cstring_t old_paths[] = {
        "/tmp/openwarcraft3-wc3-save-version-39.bin",
        "/tmp/openwarcraft3-wc3-save-version-40.bin",
        "/tmp/openwarcraft3-wc3-save-version-41.bin",
        "/tmp/openwarcraft3-wc3-save-version-42.bin",
        "/tmp/openwarcraft3-wc3-save-version-43.bin",
        "/tmp/openwarcraft3-wc3-save-version-44.bin",
        "/tmp/openwarcraft3-wc3-save-version-45.bin",
        "/tmp/openwarcraft3-wc3-save-version-46.bin",
        "/tmp/openwarcraft3-wc3-save-version-47.bin",
        "/tmp/openwarcraft3-wc3-save-version-48.bin",
        "/tmp/openwarcraft3-wc3-save-version-49.bin",
        "/tmp/openwarcraft3-wc3-save-version-50.bin",
        "/tmp/openwarcraft3-wc3-save-version-51.bin",
        "/tmp/openwarcraft3-wc3-save-version-52.bin",
        "/tmp/openwarcraft3-wc3-save-version-53.bin",
        "/tmp/openwarcraft3-wc3-save-version-54.bin",
        "/tmp/openwarcraft3-wc3-save-version-55.bin",
        "/tmp/openwarcraft3-wc3-save-version-56.bin",
        "/tmp/openwarcraft3-wc3-save-version-57.bin",
        "/tmp/openwarcraft3-wc3-save-version-58.bin",
        "/tmp/openwarcraft3-wc3-save-version-59.bin",
        "/tmp/openwarcraft3-wc3-save-version-60.bin",
        "/tmp/openwarcraft3-wc3-save-version-61.bin",
        "/tmp/openwarcraft3-wc3-save-version-62.bin",
        "/tmp/openwarcraft3-wc3-save-version-63.bin",
        "/tmp/openwarcraft3-wc3-save-version-64.bin",
        "/tmp/openwarcraft3-wc3-save-version-65.bin",
        "/tmp/openwarcraft3-wc3-save-version-66.bin",
        "/tmp/openwarcraft3-wc3-save-version-67.bin",
        "/tmp/openwarcraft3-wc3-save-version-68.bin",
        "/tmp/openwarcraft3-wc3-save-version-69.bin",
        "/tmp/openwarcraft3-wc3-save-version-70.bin",
        "/tmp/openwarcraft3-wc3-save-version-71.bin",
        "/tmp/openwarcraft3-wc3-save-version-72.bin",
        "/tmp/openwarcraft3-wc3-save-version-73.bin",
        "/tmp/openwarcraft3-wc3-save-version-74.bin",
        "/tmp/openwarcraft3-wc3-save-version-75.bin",
        "/tmp/openwarcraft3-wc3-save-version-76.bin",
        "/tmp/openwarcraft3-wc3-save-version-77.bin",
        "/tmp/openwarcraft3-wc3-save-version-78.bin",
        "/tmp/openwarcraft3-wc3-save-version-79.bin",
        "/tmp/openwarcraft3-wc3-save-version-80.bin",
        "/tmp/openwarcraft3-wc3-save-version-81.bin",
        "/tmp/openwarcraft3-wc3-save-version-82.bin",
        "/tmp/openwarcraft3-wc3-save-version-83.bin",
        "/tmp/openwarcraft3-wc3-save-version-84.bin",
        "/tmp/openwarcraft3-wc3-save-version-85.bin",
        "/tmp/openwarcraft3-wc3-save-version-86.bin",
        "/tmp/openwarcraft3-wc3-save-version-87.bin",
        "/tmp/openwarcraft3-wc3-save-version-88.bin",
        "/tmp/openwarcraft3-wc3-save-version-89.bin",
        "/tmp/openwarcraft3-wc3-save-version-90.bin",
        "/tmp/openwarcraft3-wc3-save-version-91.bin",
        "/tmp/openwarcraft3-wc3-save-version-92.bin",
        "/tmp/openwarcraft3-wc3-save-version-93.bin",
        "/tmp/openwarcraft3-wc3-save-version-94.bin",
        "/tmp/openwarcraft3-wc3-save-version-95.bin",
        "/tmp/openwarcraft3-wc3-save-version-96.bin",
        "/tmp/openwarcraft3-wc3-save-version-97.bin",
        "/tmp/openwarcraft3-wc3-save-version-98.bin",
        "/tmp/openwarcraft3-wc3-save-version-99.bin",
        "/tmp/openwarcraft3-wc3-save-version-100.bin",
        "/tmp/openwarcraft3-wc3-save-version-101.bin",
        "/tmp/openwarcraft3-wc3-save-version-102.bin",
        "/tmp/openwarcraft3-wc3-save-version-103.bin",
        "/tmp/openwarcraft3-wc3-save-version-109.bin",
        "/tmp/openwarcraft3-wc3-save-version-110.bin",
        "/tmp/openwarcraft3-wc3-save-version-111.bin",
        "/tmp/openwarcraft3-wc3-save-version-112.bin",
        "/tmp/openwarcraft3-wc3-save-version-114.bin",
        "/tmp/openwarcraft3-wc3-save-version-115.bin",
        "/tmp/openwarcraft3-wc3-save-version-116.bin",
        "/tmp/openwarcraft3-wc3-save-version-117.bin",
        "/tmp/openwarcraft3-wc3-save-version-118.bin",
        "/tmp/openwarcraft3-wc3-save-version-119.bin",
        "/tmp/openwarcraft3-wc3-save-version-120.bin",
        "/tmp/openwarcraft3-wc3-save-version-123.bin",
        "/tmp/openwarcraft3-wc3-save-version-124.bin",
    };
    uint32_t const old_versions[] = { 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 109, 110, 111, 112, 114, 115, 116, 117, 118, 119, 120, 123, 124 };

    reset_entities();
    setup_test_world();
    T_ASSERT(WriteGame(filename));
    FOR_LOOP(i, sizeof(old_versions) / sizeof(*old_versions)) {
        T_ASSERT(write_save_fixture_header(filename, old_paths[i], old_versions[i], sizeof(edict_t)));
        T_NE(save_version, old_versions[i]);
        T_ASSERT(!ReadGame(old_paths[i]));
        remove(old_paths[i]);
    }
    remove(filename);
}

TEST(wc3_save, cargo_unload_rejects_unallocated_goal_index) {
    setup_test_world();
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t restored;
    int const index = globals.num_edicts;
    FILE *f = tmpfile();
    T_NOT_NULL(f);
    if (!f) return;
    T_ASSERT(WriteEdict(f, unit));
    T_ASSERT(fseek(f, offsetof(edict_t, movement.cargo_unload_goal), SEEK_SET) == 0);
    T_ASSERT(SaveBytes(f, &index, sizeof(index)));
    rewind(f);
    T_ASSERT(!ReadEdict(f, &restored));
    fclose(f);
}

TEST(wc3_save, cargo_unload_rejects_foreign_goal_pointer) {
    cstring_t filename = "/tmp/openwarcraft3-save-foreign-cargo-goal.bin";
    setup_test_world();
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    S_SetMoveGoal(unit, &unit->movement.cargo_unload_goal, (edict_t *)(uintptr_t)1);
    T_ASSERT(!WriteGame(filename));
    S_SetMoveGoal(unit, &unit->movement.cargo_unload_goal, NULL);
    remove(filename);
}

TEST(wc3_save, current_combat_cargo_state_round_trips_without_migration) {
    cstring_t filename = "/tmp/openwarcraft3-save-current-combat-cargo.bin";
    setup_test_world();
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *goal = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    int const index = unit - g_edicts, goal_index = goal - g_edicts;
    unit->attack_cooldown_active = true;
    unit->attack_cooldown_remaining = 17.5f;
    unit->attack_cooldown_end_time = 12000;
    unit->attack_backswing_end_time = 9000;
    unit->attack_target_spawn_time = goal->spawn_time;
    unit->movement.cargo_unload_pending = true;
    unit->movement.cargo_unload_ability = MAKEFOURCC('A','t','d','p');
    S_SetMoveGoal(unit, &unit->movement.cargo_unload_goal, goal);
    unit->movement.cargo_unload_goal_spawn_time = goal->spawn_time;
    unit->unitinfo.PropWindow = 0.0f;
    T_ASSERT(WriteGame(filename));
    unit->attack_cooldown_active = false;
    unit->movement.cargo_unload_pending = false;
    S_SetMoveGoal(unit, &unit->movement.cargo_unload_goal, NULL);
    unit->unitinfo.PropWindow = 1.0f;
    T_ASSERT(ReadGame(filename));
    unit = g_edicts + index;
    goal = g_edicts + goal_index;
    T_ASSERT(unit->attack_cooldown_active);
    T_FEQ(unit->attack_cooldown_remaining, 17.5f, 0.001f);
    T_EQ(unit->attack_cooldown_end_time, 12000);
    T_EQ(unit->attack_backswing_end_time, 9000);
    T_EQ(unit->attack_target_spawn_time, goal->spawn_time);
    T_FEQ(unit->unitinfo.PropWindow, 0.0f, 0.001f);
    T_ASSERT(unit->movement.cargo_unload_pending);
    T_EQ(unit->movement.cargo_unload_ability, MAKEFOURCC('A','t','d','p'));
    T_EQ(unit->movement.cargo_unload_goal, goal);
    T_EQ(unit->movement.cargo_unload_goal_spawn_time, goal->spawn_time);
    remove(filename);
}

TEST(wc3_save, rejects_unexpected_trailing_payload) {
    cstring_t filename = "/tmp/openwarcraft3-save-extra-payload.bin";
    uint32_t const payload[] = { MAKEFOURCC('W','3','E','X'), 1, 0 };
    setup_test_world();
    reset_entities();
    T_ASSERT(WriteGame(filename));
    FILE *f = fopen(filename, "r+b");
    T_NOT_NULL(f);
    if (!f) return;
    T_ASSERT(fseek(f, -(long)sizeof(saveFooter_t), SEEK_END) == 0);
    T_ASSERT(SaveBytes(f, payload, sizeof(payload)));
    T_ASSERT(WriteFooter(f));
    rewind(f);
    T_ASSERT(ReadFooter(f)); /* The failure is a layout mismatch, not a bad checksum. */
    fclose(f);
    T_ASSERT(!ReadGame(filename));
    remove(filename);
}

TEST(wc3_save, current_format_uses_current_entity_layout) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-save-current-envelope.bin";
    saveHeader_t header = { 0 };
    FILE *f;

    reset_entities();
    setup_test_world();
    T_ASSERT(WriteGame(filename));
    f = fopen(filename, "rb");
    T_NOT_NULL(f);
    if (f) {
        T_ASSERT(LoadBytes(f, &header, sizeof(header)));
        fclose(f);
    }
    T_EQ(header.version, save_version);
    T_EQ(header.edict_size, sizeof(edict_t));
    remove(filename);
}

TEST(wc3_save, rejects_mismatched_entity_layout) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-save-waygate-current.bin";
    cstring_t old_path = "/tmp/openwarcraft3-wc3-save-old-edict-size.bin";

    reset_entities();
    setup_test_world();
    T_ASSERT(WriteGame(filename));
    T_ASSERT(write_save_fixture_header(filename, old_path, save_version, sizeof(edict_t) - 1));
    T_ASSERT(!ReadGame(old_path));
    remove(old_path);
    remove(filename);
}

#endif

#ifdef BZ_TESTS
/* All ordinary player rows retain their own work, FIFO order and interval clock. */
TEST(wc3_save, fine_player_rows_continue_independently_after_restore) {
    reset_entities(); setup_test_world();
    edict_t *units[MAX_PLAYERS][2];
    cstring_t file="/tmp/openwarcraft3-fine-player-rows.bin";
    level.pathing_counter=2000;
    FOR_LOOP(i,MAX_PLAYERS) {
        moveFineBudget_t *budget=level.move_fine_budgets+i;
        budget->work=BZ_WC3_FINE_OWNER_WORK+1+i; budget->countdown=i%2;
        FOR_LOOP(k,2) {
            units[i][k]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128+64*i,128+64*k);
            units[i][k]->s.player=i;
            T_ASSERT(!S_AdmitUnitMoveFineRequest(units[i][k]));
        }
    }
    T_ASSERT(ValidMoveFineRequests()); T_ASSERT(WriteGame(file));
    S_ClearMoveFineRequests(); T_ASSERT(ReadGame(file)); T_ASSERT(ValidMoveFineRequests());
    FOR_LOOP(i,MAX_PLAYERS) {
        moveFineBudget_t *budget=level.move_fine_budgets+i;
        T_EQ(budget->work,BZ_WC3_FINE_OWNER_WORK+1+i); T_EQ(budget->countdown,i%2);
        T_EQ(budget->head,units[i][0]); T_EQ(budget->tail,units[i][1]); T_EQ(budget->count,2);
        T_EQ(units[i][0]->movement.fine_class,i);
        budget->work=BZ_WC3_FINE_OWNER_WORK;
        T_ASSERT(!S_AdmitUnitMoveFineRequest(units[i][1]));
        T_ASSERT(S_AdmitUnitMoveFineRequest(units[i][0]));
        T_EQ(units[i][0]->movement.fine_request_time,2000);
        T_ASSERT(S_AdmitUnitMoveFineRequest(units[i][1]));
        T_EQ(budget->count,0);
    }
    T_ASSERT(ValidMoveFineRequests()); remove(file); reset_entities(); setup_test_world();
}

TEST(wc3_save, rejects_fine_queues_in_wrong_player_rows) {
    reset_entities(); setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,128);
    level.move_fine_budgets[0].work=BZ_WC3_FINE_OWNER_WORK+1;
    T_ASSERT(!S_AdmitUnitMoveFineRequest(unit));
    moveFineBudget_t original=level.move_fine_budgets[0];
    FOR_LOOP(i,4) {
        unit->s.player=unit->movement.fine_class=0;
        level.move_fine_budgets[0]=original; level.move_fine_budgets[1]=(moveFineBudget_t){0};
        if (i==0) unit->movement.fine_class=1;
        if (i==1) unit->s.player=1;
        if (i==2) level.move_fine_budgets[1]=original;
        if (i==3) unit->movement.fine_class=MAX_PLAYERS;
        T_ASSERT(!ValidMoveFineRequests());
        T_ASSERT(!WriteGame("/tmp/openwarcraft3-invalid-fine-player.bin"));
    }
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(wc3_save, all_sparse_pools_restore_records_and_entity_references) {
    cstring_t const filename = "/tmp/openwarcraft3-wc3-pools.bin";
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    FOR_LOOP(i, sizeof(save_pools) / sizeof(save_pools[0])) {
        void *slot = save_pools[i].alloc();
        memcpy((uint8_t *)unit + save_pools[i].offset, &slot, sizeof(slot));
    }

    unit->construction->progress = 13.5f;
    unit->construction->worker = target;
    unit->research->upgrade = MAKEFOURCC('R','h','m','e');
    unit->research->progress = 9.25f;
    unit->food->used = 3;
    unit->food->made = 10;
    unit->abilstatus[MAX_UNIT_STATUSES - 1] = (heroabilitystatus_t){
        .code = MAKEFOURCC('B','c','r','i'), .level = 3,
        .timestamp = 17000, .duration_ms = 19000,
        .data = MAKEFOURCC('A','c','r','i'), .source = target,
        .source_spawn_time = 123, .rank = 2, .next_tick = 11000
    };
    unit->buildwork->ability = MAKEFOURCC('A','r','e','p');
    unit->buildwork->gold_accum = 2.5f;
    unit->shadowmeld->fade_start = 1750;
    unit->shadowmeld->hide_order_active = true;
    unit->blight_growth->ability = MAKEFOURCC('A','b','l','i');
    unit->blight_growth->radius = 192.0f;
    unit->waygate->initialized=true; unit->waygate->edge_id=1;
    unit->waygate->destination = (vec2_t){128, 256};
    unit->waygate->active = true;
    unit->cargo->count = 1;
    unit->cargo->units[0] = target;
    unit->ancient_root->mode = ANCIENT_ROOTING;
    S_SetMoveGoal(unit, &unit->ancient_root->approach_goal, target);
    FILE *raw = tmpfile();
    T_NOT_NULL(raw);
    if (raw) {
        edict_t saved;
        T_ASSERT(WriteEdict(raw, unit));
        rewind(raw);
        T_ASSERT(LoadBytes(raw, &saved, sizeof(saved)));
        FOR_LOOP(i, sizeof(save_pools) / sizeof(save_pools[0])) {
            T_NULL(SavePoolSlot(&saved, save_pools + i));
            T_NOT_NULL(SavePoolSlot(unit, save_pools + i));
        }
        fclose(raw);
    }
    T_ASSERT(WriteGame(filename));
    G_PoolsReleaseEdict(unit);
    T_ASSERT(ReadGame(filename));
    FOR_LOOP(i, sizeof(save_pools) / sizeof(save_pools[0])) {
        T_NOT_NULL(SavePoolSlot(unit, save_pools + i));
        T_NULL(SavePoolSlot(target, save_pools + i));
    }
    T_ASSERT(unit->construction);
    T_FEQ(unit->construction->progress, 13.5f, 0.001f);
    T_ASSERT(unit->construction->worker == target);
    T_EQ(unit->research->upgrade, MAKEFOURCC('R','h','m','e'));
    T_FEQ(unit->research->progress, 9.25f, 0.001f);
    T_EQ(unit->food->used, 3); T_EQ(unit->food->made, 10);
    heroabilitystatus_t const *status = unit->abilstatus + MAX_UNIT_STATUSES - 1;
    T_EQ(status->code, MAKEFOURCC('B','c','r','i')); T_EQ(status->level, 3);
    T_EQ(status->timestamp, 17000); T_EQ(status->duration_ms, 19000);
    T_EQ(status->data, MAKEFOURCC('A','c','r','i')); T_EQ(status->source, target);
    T_EQ(status->source_spawn_time, 123); T_EQ(status->rank, 2); T_EQ(status->next_tick, 11000);
    FOR_LOOP(i, MAX_UNIT_STATUSES - 1) T_EQ(unit->abilstatus[i].level, 0);
    T_EQ(unit->buildwork->ability, MAKEFOURCC('A','r','e','p'));
    T_FEQ(unit->buildwork->gold_accum, 2.5f, 0.001f);
    T_EQ(unit->shadowmeld->fade_start, 1750);
    T_ASSERT(unit->shadowmeld->hide_order_active);
    T_EQ(unit->blight_growth->ability, MAKEFOURCC('A','b','l','i'));
    T_FEQ(unit->blight_growth->radius, 192.0f, 0.001f);
    T_ASSERT(unit->waygate->active);
    T_FEQ(unit->waygate->destination.x, 128, 0.001f);
    T_FEQ(unit->waygate->destination.y, 256, 0.001f);
    T_EQ(unit->cargo->count, 1);
    T_ASSERT(unit->cargo->units[0] == target);
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    T_ASSERT(unit->ancient_root->approach_goal == target);
    remove(filename);
}
#endif
