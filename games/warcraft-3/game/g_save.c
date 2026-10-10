#include "g_local.h"
#include <stdint.h>
#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);
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
/* Version 68 adds construction_t duration_ms and Tiny construction type to version 67 dialog generations. */
/* Version 69 adds persistent Awan scheduling and internal-order ownership. */
/* Version 70 persists multiboard suppression state. */
/* Version 71 persists local Team Resources collapse state. */
/* Version 72 persists the source/recipient role of Hero aura effects. */
/* Version 73 persists mutable destructable occluder levels and queued animation names. */
/* Version 74 replaces the byte-wise FNV footer checksum with the word-wise SaveChecksum. */
/* Version 75 records the per-unit UnitIgnoreAlarm flag in serialized edicts. */
/* Version 76 persists texttag presentation readiness and slot generations. */
/* Version 77 persists Wisp tree egress ownership and its local exit target. */
/* Version 78 changes cinematic portrait state from a model byte to a presence flag plus 16-bit stat. */
/* Version 79 adds explicit status source ability identity and payload separation. */
/* Version 80 adds categorical state contributions to WC3 status records. */
/* Version 81 adds status-owned numeric modifier descriptors. */
/* Version 82 adds explicit status dispel and transfer classification. */
/* Version 83 adds status instance identities and explicit stacking policy. */
static uint32_t const save_version = 83;
#define SAVE_STREAM_BUFFER (1u << 20) // bytes; save files are several MB of field writes, so a large stdio buffer avoids per-4 KB syscalls
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
    JASS_HANDLE_DIALOG,
    JASS_HANDLE_BUTTON,
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
    { "dialog", JASS_HANDLE_DIALOG },
    { "button", JASS_HANDLE_BUTTON },
    { "leaderboard", JASS_HANDLE_LEADERBOARD },
    { "multiboard", JASS_HANDLE_MULTIBOARD },
    { "multiboarditem", JASS_HANDLE_MULTIBOARDITEM },
    { "texttag", JASS_HANDLE_TEXTTAG },
    { "hashtable", JASS_HANDLE_HASHTABLE },
    { "weathereffect", JASS_HANDLE_WEATHER },
    { "lightning", JASS_HANDLE_LIGHTNING },
    { "region", JASS_HANDLE_REGION },
};

static field_t const jass_dialog_fields[] = {
    TF(jassDialog_t, inuse, F_INT),
    TF(jassDialog_t, id, F_INT),
    TF(jassDialog_t, visible_players, F_INT),
    TF(jassDialog_t, message, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
};

static field_t const jass_dialog_button_fields[] = {
    TF(jassDialogButton_t, inuse, F_INT),
    TF(jassDialogButton_t, id, F_INT),
    TF(jassDialogButton_t, dialog_id, F_INT),
    TF(jassDialogButton_t, hotkey, F_INT),
    TF(jassDialogButton_t, quit, F_INT),
    TF(jassDialogButton_t, score_screen, F_INT),
    TF(jassDialogButton_t, text, F_INT),
    { NULL, 0, 0, 0, 0, 0 }
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
    F(gevent_s, dialog_id, F_INT),
    F(gevent_s, button_id, F_INT),
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
    F(gameevent_s, dialog_id, F_INT),
    F(gameevent_s, button_id, F_INT),
    F(gameevent_s, dialog_player, F_INT),
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
    F(gtimer_s, duration, F_INT),
    F(gtimer_s, remaining, F_INT),
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
    F(gtexttag_s, has_text, F_INT),
    F(gtexttag_s, has_position, F_INT),
    F(gtexttag_s, visible_clients, F_INT),
    F(gtexttag_s, generation, F_INT),
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

static field_t const level_fields[] = {
    F(level_locals, framenum, F_INT),
    F(level_locals, time, F_INT),
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
    F(level_locals, pending_consumed_item_cleanup, F_INT),
    F(level_locals, waypoints.base, F_INT),
    F(level_locals, waypoints.cursor, F_INT),
    F(level_locals, waypoints.count, F_INT),
    F(level_locals, next_weather_id, F_INT),
    F(level_locals, weather_effects, F_STRUCT, MAX_WEATHER_EFFECTS, weather_fields),
    F(level_locals, next_lightning_id, F_INT),
    F(level_locals, lightning_effects, F_STRUCT, MAX_LIGHTNING_EFFECTS, lightning_fields),
    F(level_locals, quests, F_STRUCT, MAX_QUESTS, quest_fields),
    FC(level_locals, triggers, F_STRUCT, MAX_TRIGGERS, trigger_fields, num_triggers),
    FC(level_locals, timers, F_STRUCT, MAX_TIMERS, timer_fields, num_timers),
    F(level_locals, timer_dialogs, F_STRUCT, MAX_TIMERDIALOGS, timer_dialog_fields),
    F(level_locals, dialog_count, F_INT),
    F(level_locals, dialog_button_count, F_INT),
    F(level_locals, dialogs, F_STRUCT, MAX_JASS_DIALOGS, jass_dialog_fields),
    F(level_locals, dialog_buttons, F_STRUCT, MAX_JASS_DIALOG_BUTTONS, jass_dialog_button_fields),
    F(level_locals, leaderboards, F_STRUCT, MAX_LEADERBOARDS, leaderboard_fields),
    F(level_locals, player_leaderboards, F_INT),
    F(level_locals, multiboards, F_STRUCT, MAX_MULTIBOARDS, multiboard_fields),
    F(level_locals, multiboard_suppressed_clients, F_INT),
    F(level_locals, team_resources_collapsed_clients, F_INT),
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

static field_t const unit_attack_fields[] = {
    TF(unitAttack_t, backswingPoint, F_FLOAT),
    TF(unitAttack_t, rangeBuffer, F_FLOAT),
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
    TF(destructable_t, occluder_height, F_FLOAT),
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

static field_t const movement_fields[] = {
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
    TF(edictMovement_s, wisp_egress_tree, F_EDICT, 0, FIELD_NONE),
    F(edictMovement_s, wisp_egress_tree_spawn_time, F_INT),
    F(edictMovement_s, wisp_egress_target, F_VECTOR),
    F(edictMovement_s, wisp_egress_active, F_INT),
    F(edictMovement_s, wisp_egress_target_valid, F_INT),
    F(edictMovement_s, holding_position, F_INT),
    F(edictMovement_s, guard_position, F_VECTOR),
    F(edictMovement_s, guard_state, F_INT),
    F(edictMovement_s, explicit_allied_attack, F_INT),
    F(edictMovement_s, cargo_unload_pending, F_INT),
    F(edictMovement_s, cargo_unload_ability, F_INT),
    TF(edictMovement_s, cargo_unload_goal, F_EDICT, 0, FIELD_NONE),
    F(edictMovement_s, cargo_unload_goal_spawn_time, F_INT),
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
    F(heroabilitystatus_t, instance_id, F_INT),
    F(heroabilitystatus_t, stack_policy, F_INT),
    TF(heroabilitystatus_t, source, F_EDICT, 0, FIELD_NONE),
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
    F(edict_s, summon_ability, F_INT),
    F(edict_s, aura_effect_role, F_INT),
    F(edict_s, wander_next_time, F_INT),
    F(edict_s, wander_random_state, F_INT),
    F(edict_s, wander_goal, F_EDICT, 0, FIELD_NONE),
    F(edict_s, wander_waypoint, F_EDICT, 0, FIELD_NONE),
    F(edict_s, wander_goal_generation, F_INT),
    F(edict_s, waypoint_generation, F_INT),
    F(edict_s, permanent_invisibility_reveal_until, F_INT),
    F(edict_s, forced_visibility_count, F_INT),
    F(edict_s, shared_vision, F_INT),
    F(edict_s, harvested_lumber, F_INT),
    F(edict_s, harvested_gold, F_INT),
    F(edict_s, heatmap2, F_INT),
    F(edict_s, peonsinside, F_INT),
    F(edict_s, aiflags, F_INT),
    F(edict_s, autocast_code, F_INT),
    F(edict_s, abilstatus, F_STRUCT, MAX_UNIT_STATUSES, status_fields),
    F(edict_s, next_status_instance_id, F_INT),
    F(edict_s, damage, F_INT),
    F(edict_s, projectile_attack_type, F_INT),
    F(edict_s, projectile_reflected, F_INT),
    F(edict_s, collision, F_FLOAT),
    F(edict_s, attack_cooldown_active, F_INT),
    F(edict_s, attack_cooldown_remaining, F_FLOAT),
    F(edict_s, attack_cooldown_end_time, F_INT),
    F(edict_s, attack_backswing_end_time, F_INT),
    F(edict_s, unitinfo, F_STRUCT, 1, unit_info_fields),
    F(edict_s, attack1, F_STRUCT, 1, unit_attack_fields),
    F(edict_s, attack2, F_STRUCT, 1, unit_attack_fields),
    F(edict_s, s, F_STRUCT, 1, entity_state_fields),
    F(edict_s, hero_shortcut_alert_until, F_IGNORE, 0, FIELD_RUNTIME),
    F(edict_s, inventory, F_EDICT, MAX_INVENTORY, FIELD_NONE),
    F(edict_s, ground_next, F_EDICT, 0, FIELD_NONE),
    F(edict_s, movement, F_STRUCT, 1, movement_fields),
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

/* Footer checksum: four independent 64-bit FNV-style lanes over native words. Saves are raw native
 * structs already, so native byte order costs no portability; the lanes keep multiplies pipelined. */
typedef struct { uint64_t lane[4]; } saveChecksum_t;

static void SaveChecksumInit(saveChecksum_t *sum) {
    FOR_LOOP(i, 4) sum->lane[i] = 0xcbf29ce484222325ull + i;
}

/* Callers feed whole buffers; only the final call may end on a partial 32-byte block. */
static void SaveChecksumUpdate(saveChecksum_t *sum, void const *data, size_t size) {
    uint8_t const *bytes = data;
    uint64_t word[4];
    for (; size >= sizeof(word); size -= sizeof(word), bytes += sizeof(word)) {
        memcpy(word, bytes, sizeof(word));
        FOR_LOOP(i, 4) sum->lane[i] = (sum->lane[i] ^ word[i]) * 0x100000001b3ull;
    }
    while (size--) sum->lane[0] = (sum->lane[0] ^ *bytes++) * 0x100000001b3ull;
}

static uint32_t SaveChecksumFinal(saveChecksum_t const *sum) {
    uint64_t hash = sum->lane[0];
    for (int i = 1; i < 4; i++) hash = (hash ^ sum->lane[i]) * 0x100000001b3ull;
    return (uint32_t)(hash ^ (hash >> 32));
}

/* A committed checksum rejects truncation and corruption before ReadGame mutates live state. */
static bool WriteFooter(FILE *f) {
    static uint8_t bytes[1u << 16]; // multiple of the 32-byte checksum block
    long payload;
    saveChecksum_t checksum;
    saveFooter_t footer;
    SaveChecksumInit(&checksum);
    if (fflush(f) || (payload = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) return false;
    while (payload > 0) {
        size_t size = MIN((size_t)payload, sizeof(bytes));
        if (fread(bytes, 1, size, f) != size) return false;
        SaveChecksumUpdate(&checksum, bytes, size); payload -= (long)size;
    }
    footer = (saveFooter_t){ SaveChecksumFinal(&checksum), save_commit };
    return fseek(f, 0, SEEK_END) == 0 && SaveBytes(f, &footer, sizeof(footer));
}

static bool ReadFooter(FILE *f) {
    static uint8_t bytes[1u << 16]; // multiple of the 32-byte checksum block
    long payload;
    saveChecksum_t checksum;
    saveFooter_t footer;
    SaveChecksumInit(&checksum);
    if (fseek(f, 0, SEEK_END) || (payload = ftell(f)) < (long)sizeof(footer)) return false;
    payload -= sizeof(footer);
    if (fseek(f, payload, SEEK_SET) || !LoadBytes(f, &footer, sizeof(footer)) || footer.commit != save_commit ||
        fseek(f, 0, SEEK_SET)) return false;
    for (long remaining = payload; remaining > 0;) {
        size_t size = MIN((size_t)remaining, sizeof(bytes));
        if (fread(bytes, 1, size, f) != size) return false;
        SaveChecksumUpdate(&checksum, bytes, size); remaining -= (long)size;
    }
    return SaveChecksumFinal(&checksum) == footer.checksum && fseek(f, 0, SEEK_SET) == 0;
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
    if (domain == JASS_HANDLE_DIALOG) {
        jassDialog_t *dialog = G_JassDialog(value);
        if (!dialog) return false;
        *id = dialog->id; return true;
    }
    if (domain == JASS_HANDLE_BUTTON) {
        jassDialogButton_t *button = G_JassDialogButton(value);
        if (!button) return false;
        *id = button->id; return true;
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
    if (domain == JASS_HANDLE_DIALOG) return G_JassDialogById(id);
    if (domain == JASS_HANDLE_BUTTON) return G_JassDialogButtonById(id);
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
            if (name && !*(jassFunc_t const * *)(base + fields->ofs)) { free(name); return false; }
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

static bool WriteEdict(FILE *f, edict_t const *ent) {
    edict_t temp = *ent;
    field_t const *field;

    ClearRuntimeFields(&temp, edict_fields, FIELD_RUNTIME);
    for (field = edict_fields; field->name; field++)
        if (!WriteField1(field, (uint8_t *)&temp)) return false;
    return SaveBytes(f, &temp, sizeof(temp));
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
    for (field = edict_fields; field->name; field++)
        if (!ReadField(field, (uint8_t *)ent)) return false;
    /* Table rows are process-owned; C callbacks already came back through F_CFUNCTION. */
    if (ent->class_id) {
        G_BindEntityData(ent);
        /* animation is a process-owned model pointer and is deliberately not serialized.
         * Re-resolve it from the persisted logical request plus per-unit animation tags. */
        if (ent->animation_request[0])
            ent->animation = G_GetUnitAnimation(ent, ent->animation_request);
    }
    return true;
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
    G_PoolsReset();
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
    if (f) setvbuf(f, NULL, _IOFBF, SAVE_STREAM_BUFFER);
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
    if (!SaveBytes(f, &header, sizeof(header))) { fprintf(stderr, "WC3 SaveGame: failed at header\n"); goto done; }
    if (!WriteMappedFields(f, level_fields, (uint8_t *)&level)) {
        fprintf(stderr, "WC3 SaveGame: failed at level fields\n"); goto done;
    }
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
    if (f) setvbuf(f, NULL, _IOFBF, SAVE_STREAM_BUFFER);
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
    if (!ReadMappedFields(f, level_fields, (uint8_t *)&level)) {
        fprintf(stderr, "WC3 LoadGame: failed at level state\n"); fclose(f); return false;
    }
    FOR_LOOP(i, MAX_EVENTS) if (current_nonregion_event_slots[i] && !level.events.handlers[i].inuse) {
        fprintf(stderr, "WC3 LoadGame: saved event registry dropped live non-region slot %u\n", (unsigned)i);
        fclose(f); return false;
    }
    if (ActiveEventCount() != header.events) {
        fprintf(stderr, "WC3 LoadGame: event count mismatch saved=%u restored=%u\n",
                (unsigned)header.events, (unsigned)ActiveEventCount());
        fclose(f); return false;
    }
    if (level.dialog_count > MAX_JASS_DIALOGS || level.dialog_button_count > MAX_JASS_DIALOG_BUTTONS) {
        fprintf(stderr, "WC3 LoadGame: dialog slot counts %u/%u exceed capacity\n", level.dialog_count, level.dialog_button_count);
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
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = header.num_edicts;
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
        if (gi.LinkEntity) gi.LinkEntity(ent);
    }
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
    /* Choice dialogs are re-sent by ClientBegin on the post-load reconnect (Q2 layouts start from the client's first
     * frame); sending here too published the window twice. */
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

TEST(wc3_save, spell_approach_callback_uses_current_roster_identity) {
    int const index = SaveCFunctionIndex((void *)S_SpellTargetApproachThink);

    T_ASSERT(index > 0);
    if (index > 0 && index <= (int)(sizeof(save_cfunctions) / sizeof(save_cfunctions[0])))
        T_STREQ(save_cfunctions[index - 1].name, "S_SpellTargetApproachThink");
}

TEST(wc3_save, disabled_player_abilities_grow_and_round_trip) {
    static char const digits[] = "0123456789";
    cstring_t const filename = Test_TempPath("wc3-disabled-abilities-save.bin");
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

TEST(wc3_save, rejects_previous_combat_cargo_format_before_restoring_world) {
    cstring_t filename = Test_TempPath("save-current-format.bin");
    cstring_t old_filename = Test_TempPath("save-previous-combat-cargo-format.bin");
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

TEST(wc3_save, rejects_version_77_after_cinematic_portrait_format_change) {
    cstring_t filename = Test_TempPath("save-portrait-format-current.bin");
    cstring_t previous_filename = Test_TempPath("save-portrait-format-v77.bin");
    saveHeader_t header;
    char map[sizeof(((saveHeader_t *)0)->map_path)];

    setup_test_world();
    reset_entities();
    T_ASSERT(WriteGame(filename));
    FILE *f = fopen(filename, "rb");
    T_NOT_NULL(f);
    if (!f) return;
    T_ASSERT(LoadBytes(f, &header, sizeof(header)));
    fclose(f);
    T_EQ(header.version, 78);
    T_ASSERT(write_save_fixture_header(filename, previous_filename, 77, header.edict_size));
    T_ASSERT(!G_GetSaveMap(previous_filename, map, sizeof(map)));
    T_ASSERT(!ReadGame(previous_filename));
    remove(filename); remove(previous_filename);
}

TEST(wc3_save, rejects_layout_mismatch_before_selecting_map) {
    cstring_t filename = Test_TempPath("save-current-layout.bin");
    cstring_t bad_filename = Test_TempPath("save-layout-mismatch.bin");
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
    PATHSTR filename;
    uint32_t const old_versions[] = { 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82 };

    /* The version fixtures wrap Test_TempPath's ring; retain the source path independently. */
    strlcpy(filename, Test_TempPath("wc3-save-prior-format.bin"), sizeof(filename));
    reset_entities();
    setup_test_world();
    T_ASSERT(WriteGame(filename));
    FOR_LOOP(i, sizeof(old_versions) / sizeof(*old_versions)) {
        PATHSTR name;
        cstring_t old_path;
        snprintf(name, sizeof(name), "wc3-save-version-%u.bin", old_versions[i]);
        old_path = Test_TempPath(name);
        T_ASSERT(write_save_fixture_header(filename, old_path, old_versions[i], sizeof(edict_t)));
        T_NE(save_version, old_versions[i]);
        T_ASSERT(!ReadGame(old_path));
        remove(old_path);
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
    cstring_t filename = Test_TempPath("save-foreign-cargo-goal.bin");
    setup_test_world();
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->movement.cargo_unload_goal = (edict_t *)(uintptr_t)1;
    T_ASSERT(!WriteGame(filename));
    unit->movement.cargo_unload_goal = NULL;
    remove(filename);
}

TEST(wc3_save, elevator_occluder_and_pending_animation_round_trip) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"ID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y2;X1;K\"DTrx\"\n"
        "C;Y2;X2;K\"Doodads/Cinematic/ElevatorPuzzle/ElevatorPuzzle.mdx\"\n"
        "E\n";
    cstring_t const filename = Test_TempPath("save-elevator-state.bin");
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved;
    edict_t *deck;
    int index;

    reset_entities();
    setup_test_world();
    saved = G_SetSLKRows("DestructableData", rows);
    deck = G_Spawn();
    index = (int)(deck - g_edicts);
    deck->class_id = MAKEFOURCC('D', 'T', 'r', 'x');
    deck->s.class_id = deck->class_id;
    G_BindEntityData(deck);
    deck->destructable = G_AllocDestructable();
    deck->destructable->occluder_height = 256.0f;
    strlcpy(deck->queued_animation, "stand third", sizeof(deck->queued_animation));
    T_ASSERT(WriteGame(filename));
    deck->destructable->occluder_height = 0.0f;
    deck->queued_animation[0] = '\0';
    T_ASSERT(ReadGame(filename));
    deck = &g_edicts[index];
    T_NOT_NULL(deck->destructable);
    if (deck->destructable) T_FEQ(deck->destructable->occluder_height, 256.0f, 0.001f);
    T_STREQ(deck->queued_animation, "stand third");
    remove(filename);
    G_SetSLKRows("DestructableData", saved);
    free_slk_rows(rows);
}

TEST(wc3_save, current_combat_cargo_state_round_trips_without_migration) {
    cstring_t filename = Test_TempPath("save-current-combat-cargo.bin");
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
    unit->movement.cargo_unload_goal = goal;
    unit->movement.cargo_unload_goal_spawn_time = goal->spawn_time;
    unit->movement.wisp_egress_tree = goal;
    unit->movement.wisp_egress_tree_spawn_time = goal->spawn_time;
    unit->movement.wisp_egress_target = (vec2_t){ -32.0f, 48.0f };
    unit->movement.wisp_egress_active = true;
    unit->movement.wisp_egress_target_valid = true;
    unit->unitinfo.PropWindow = 0.0f;
    T_ASSERT(WriteGame(filename));
    unit->attack_cooldown_active = false;
    unit->movement.cargo_unload_pending = false;
    unit->movement.cargo_unload_goal = NULL;
    unit->movement.wisp_egress_tree = NULL;
    unit->movement.wisp_egress_active = false;
    unit->movement.wisp_egress_target_valid = false;
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
    T_ASSERT(unit->movement.wisp_egress_active);
    T_ASSERT(unit->movement.wisp_egress_target_valid);
    T_EQ(unit->movement.wisp_egress_tree, goal);
    T_EQ(unit->movement.wisp_egress_tree_spawn_time, goal->spawn_time);
    T_FEQ(unit->movement.wisp_egress_target.x, -32.0f, 0.001f);
    T_FEQ(unit->movement.wisp_egress_target.y, 48.0f, 0.001f);
    remove(filename);
}

TEST(wc3_save, rejects_unexpected_trailing_payload) {
    cstring_t filename = Test_TempPath("save-extra-payload.bin");
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
    cstring_t filename = Test_TempPath("wc3-save-current-envelope.bin");
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
    cstring_t filename = Test_TempPath("wc3-save-waygate-current.bin");
    cstring_t old_path = Test_TempPath("wc3-save-old-edict-size.bin");

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
TEST(wc3_save, all_sparse_pools_restore_records_and_entity_references) {
    cstring_t const filename = Test_TempPath("wc3-pools.bin");
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
    unit->buildwork->ability = MAKEFOURCC('A','r','e','p');
    unit->buildwork->gold_accum = 2.5f;
    unit->shadowmeld->fade_start = 1750;
    unit->shadowmeld->hide_order_active = true;
    unit->blight_growth->ability = MAKEFOURCC('A','b','l','i');
    unit->blight_growth->radius = 192.0f;
    unit->waygate->destination = (vec2_t){128, 256};
    unit->waygate->active = true;
    unit->cargo->count = 1;
    unit->cargo->units[0] = target;
    unit->ancient_root->mode = ANCIENT_ROOTING;
    unit->ancient_root->approach_goal = target;
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
