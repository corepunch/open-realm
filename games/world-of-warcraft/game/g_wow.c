#include "g_landmarks.h"
#include "g_crafting.h"
#include "g_quest_tracker.h"
#include "g_wow_local.h"
#include "g_character_save.h"
#include "../common/wow_config.h"
#include "../common/wow_view.h"
#include "common/stb_dbc.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <strings.h>

/* Generated server tables are included here so the unity source tree contains no generated files. */
#include "build/generated/g_playercreateinfo.c"
#include "build/generated/g_creatures.c"
#include "build/generated/g_vendor_spawns.c"
#include "build/generated/g_quests.c"
#include "build/generated/g_weapons.c"
#include "build/generated/g_areatrigger_teleport.c"

wowTaxiGraph_t wow_taxi_graph;
static bool wow_taxi_attempted;

struct game_import gi;
struct game_export globals;

static uint32_t G_WriteClientDatagram(edict_t *ent, uint8_t *data, uint32_t size) {
    (void)ent;
    if (size < sizeof(uint16_t)) return 0;
    memset(data, 0, sizeof(uint16_t));
    return sizeof(uint16_t);
}
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
wowClient_t wow_clients[MAX_CLIENTS];
/* Configstring model indices for spell impact visuals; set during map load. */
static int wow_firebolt_impact_model = 0;
static int wow_frostbolt_impact_model = 0;
char wow_loading_texture[MAX_PATHLEN] = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
char wow_loading_title[128] = "World of Warcraft";

/* Pending cross-map teleport: set by Wow_CheckAreaTriggers / warp command before
 * gi.MenuAction("map", ...) fires; consumed once by Wow_SpawnEntities on the new map. */
typedef struct { bool pending; float x, y, z, orientation; } wowPendingTeleport_t;
static wowPendingTeleport_t wow_pending_teleport;

#define WOW_MAX_AREA_TRIGS 256
static wowAreatrig_t wow_area_trigs[WOW_MAX_AREA_TRIGS];
static uint32_t       wow_area_trig_count;
enum {
    WOW_PLAYER_EQUIPMENT_UPPER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_LOWER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_HANDS = 1,
    WOW_PLAYER_EQUIPMENT_FEET = 1
};
static wowMove_t wow_move_cast = { "SpellCastDirected", NULL, NULL };

static struct {
    uint32_t flags;
    float yaw;
    float pitch;
    float distance;
} wow_move = {
    .pitch = 342.0f,
    .distance = 8.0f,
};

#define WOW_MAX_SPAWNS_PER_FRAME 64
#define WOW_MAX_WALK_SLOPE_TAN 1.1917536f // rise/run; tan(50 degrees); blocks mountain faces while retaining authored ramps
#define WOW_GROUND_EPSILON 0.01f // world units; distinguishes a terrain floor from a higher WMO floor at the same XY

#define WOW_MAX_SPAWNS_PER_FRAME 64
/* Per-frame spawn budget (declared extern in g_wow_local.h). */
uint32_t wow_spawns_this_frame = 0;
static uint64_t wow_spawn_serial;
static uint32_t wow_service_refresh_ms;

/* Starting inventory — 16 slots total; first 6 shown in the HUD quick-access bar,
 * all 16 visible in the backpack window.  Slots 6-15 start empty and fill with loot.
 * Spells never go here — they belong in actions[]. */
static wowHudIcon_t const wow_start_inventory[WOW_UI_INVENTORY_SLOTS] = {
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",          "Worn Knapsack",           1 },
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",          "Worn Knapsack",           1 },
    { "Interface\\Icons\\INV_Potion_51.blp",            "Minor Healing Potion",    2, 118 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp",         "Tough Jerky",             5, 117 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", "Worn Shortsword",         1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp",         "Copper Coin",            10 },
};

typedef struct {
    wowHudIcon_t icon;
    uint32_t spell;
} wowActionDef_t;

/* Every visible icon and its command share this binding. An empty icon leaves
 * the slot unbound, so unfinished abilities cannot invoke a different action. */
static wowActionDef_t const wow_actions_warrior[WOW_UI_ACTION_SLOTS] = {
    [0] = { { "Interface\\Icons\\Ability_Warrior_Cleave.blp", "Attack", 1 }, WOW_SPELL_ATTACK },
    [3] = { { "Interface\\Icons\\Spell_Nature_HealingTouch.blp", "Healing Touch", 1 }, WOW_SPELL_HEALING_TOUCH },
};

static wowActionDef_t const wow_actions_mage[WOW_UI_ACTION_SLOTS] = {
    [0] = { { "Interface\\Icons\\Ability_Warrior_Cleave.blp", "Attack", 1 }, WOW_SPELL_ATTACK },
    [3] = { { "Interface\\Icons\\Spell_Nature_HealingTouch.blp", "Healing Touch", 1 }, WOW_SPELL_HEALING_TOUCH },
    [4] = { { "Interface\\Icons\\Spell_Fire_FireBolt02.blp", "Fireball", 1 }, WOW_SPELL_FIREBOLT },
    [5] = { { "Interface\\Icons\\Spell_Frost_FrostBolt02.blp", "Frostbolt", 1 }, WOW_SPELL_FROSTBOLT },
    [6] = { { "Interface\\Icons\\Spell_Fire_Fireball.blp", "Fire Blast", 1 }, WOW_SPELL_FIRE_BLAST },
};

/* -------------------------------------------------------------------------
 * Loot system: item definitions + loot templates keyed by creature display_id.
 * Items use real WoW 1.12 entry IDs and icon paths from the MPQ archives.
 * Icon rendering falls back to the placeholder texture if the BLP is absent.
 * -------------------------------------------------------------------------*/
typedef struct { uint32_t entry; cstring_t name; cstring_t icon; uint32_t stack_limit, heal; } wowItemDef_t;
static wowItemDef_t const wow_item_defs[] = {
    { 118, "Minor Healing Potion", "Interface\\Icons\\INV_Potion_51.blp", 5, 35 },
    { 117, "Tough Jerky", "Interface\\Icons\\INV_Misc_Food_24.blp", 20, 20 },
    { 1251, "Linen Bandage", "Interface\\Icons\\INV_Misc_Bandage_18.blp", 20, 0 },
    { 2581, "Heavy Linen Bandage", "Interface\\Icons\\INV_Misc_Bandage_18.blp", 20, 0 },
    { 2589,  "Linen Cloth",       "Interface\\Icons\\INV_Fabric_Linen_01.blp", 20, 0 },
    { 4234,  "Light Leather",     "Interface\\Icons\\INV_Misc_Leather_01.blp", 20, 0 },
    { 12208, "Raw Boar Ribs",     "Interface\\Icons\\INV_Misc_Food_11.blp", 20, 0 },
    { 6083,  "Kobold Candle",     "Interface\\Icons\\INV_Misc_Candle_01.blp", 20, 0 },
    { 7974,  "Murloc Eye",        "Interface\\Icons\\INV_Misc_MonsterScales_01.blp", 20, 0 },
    { 811,   "Stringy Wolf Meat", "Interface\\Icons\\INV_Misc_Food_52.blp", 20, 0 },
    { 2318,  "Wolf Pelt",         "Interface\\Icons\\INV_Misc_Pelt_Wolf_01.blp", 20, 0 },
    { 5504,  "Boar Tusk",         "Interface\\Icons\\INV_Misc_BoneTusk_02.blp", 20, 0 },
};
#define WOW_ITEM_DEF_COUNT (sizeof(wow_item_defs) / sizeof(wow_item_defs[0]))

#define WOW_LOOT_DROP_MAX 4
typedef struct { uint32_t item_entry; uint32_t chance_pct; uint32_t min_qty, max_qty; } wowLootDrop_t;
typedef struct {
    uint32_t display_id;
    uint32_t copper_min, copper_max; /* rolled copper (added to player wallet on loot open) */
    wowLootDrop_t drops[WOW_LOOT_DROP_MAX];
    uint32_t drop_count;
} wowLootEntry_t;

static wowLootEntry_t const wow_loot_table[] = {
    { WOW_CREATURE_DISPLAY_WOLF, 10, 40, {
        { 811,  80, 1, 2 }, /* Stringy Wolf Meat — common food drop */
        { 2318, 40, 1, 1 }, /* Wolf Pelt — leather source */
        { 4234, 30, 1, 1 }, /* Light Leather */
    }, 3 },
    { WOW_CREATURE_DISPLAY_BOAR, 10, 40, {
        { 12208, 80, 1, 2 }, /* Raw Boar Ribs */
        { 5504,  35, 1, 1 }, /* Boar Tusk — quest/vendor item */
        { 4234,  25, 1, 1 }, /* Light Leather */
    }, 3 },
    { WOW_CREATURE_DISPLAY_KOBOLD, 5, 25, {
        { 2589, 70, 1, 4 }, /* Linen Cloth — humanoid standard drop */
        { 6083, 25, 1, 1 }, /* Kobold Candle — flavor item */
    }, 2 },
    { WOW_CREATURE_DISPLAY_MURLOC, 5, 25, {
        { 7974, 55, 1, 1 }, /* Murloc Eye — quest/vendor item */
        { 2589, 45, 1, 2 }, /* Linen Cloth */
    }, 2 },
};
#define WOW_LOOT_TABLE_COUNT (sizeof(wow_loot_table) / sizeof(wow_loot_table[0]))

static wowItemDef_t const *Wow_ItemByEntry(uint32_t entry) {
    FOR_LOOP(i, WOW_ITEM_DEF_COUNT)
        if (wow_item_defs[i].entry == entry) return &wow_item_defs[i];
    return NULL;
}

static uint64_t wow_loot_generation;

/* Roll loot for a freshly-killed creature; results stored on the corpse entity. */
void Wow_RollLoot(edict_t *ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    wowLootEntry_t const *tmpl = NULL;

    if (!local) return;
    local->loot_generation = ++wow_loot_generation;
    local->loot_count = 0;
    local->loot_copper = 0;
    memset(local->loot_items, 0, sizeof(local->loot_items));

    FOR_LOOP(i, WOW_LOOT_TABLE_COUNT)
        if (wow_loot_table[i].display_id == local->display_id) { tmpl = &wow_loot_table[i]; break; }
    if (!tmpl) return;

    if (tmpl->copper_max > tmpl->copper_min)
        local->loot_copper = tmpl->copper_min + (uint32_t)(rand() % (int)(tmpl->copper_max - tmpl->copper_min + 1));
    else
        local->loot_copper = tmpl->copper_min;

    FOR_LOOP(i, tmpl->drop_count) {
        wowLootDrop_t const *drop = &tmpl->drops[i];
        wowItemDef_t const *item = Wow_ItemByEntry(drop->item_entry);
        uint32_t slot = local->loot_count;

        if (slot >= WOW_MAX_LOOT_ITEMS) break;
        if ((uint32_t)(rand() % 100) >= drop->chance_pct || !item) continue;
        uint32_t qty = drop->min_qty;
        if (drop->max_qty > drop->min_qty) qty += (uint32_t)(rand() % (int)(drop->max_qty - drop->min_qty + 1));
        snprintf(local->loot_items[slot].icon, sizeof(local->loot_items[slot].icon), "%s", item->icon);
        snprintf(local->loot_items[slot].name, sizeof(local->loot_items[slot].name), "%s", item->name);
        local->loot_items[slot].count = qty;
        local->loot_items[slot].item_id = item->entry;
        local->loot_count++;
    }
}

/* Find the nearest corpse entity within range that still has items to loot. */
edict_t *Wow_FindNearestCorpse(edict_t *ent, float range) {
    edict_t *best = NULL;
    float best_dist2 = range * range;

    if (!ent) return NULL;
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts && i < WOW_MAX_EDICTS; i++) {
        edict_t *c = &wow_edicts[i];
        wowEntityLocal_t *local;
        vec2_t delta;
        float dist2;

        if (!c->inuse) continue;
        local = Wow_EntityLocal(c);
        if (!local || local->think != Wow_RunCorpseFrame) continue;
        /* Include corpses with no items if they still have copper (gold auto-loots). */
        if (!local || (local->loot_count == 0 && local->loot_copper == 0)) continue;
        delta = Vector2_sub(&c->s.origin2, &ent->s.origin2);
        dist2 = delta.x * delta.x + delta.y * delta.y;
        if (dist2 < best_dist2) { best_dist2 = dist2; best = c; }
    }
    return best;
}

#define WOW_MISSING_ANIMATION_LOG_SLOTS 128

typedef struct {
    uint32_t model;
    char name[64];
} wowMissingAnimationLog_t;

typedef struct {
    uint32_t id;
    uint32_t directory_offset;
    uint32_t unused;
    uint32_t title_offset;
} wowMapDbc_t;

static wowMissingAnimationLog_t wow_missing_animation_log[WOW_MISSING_ANIMATION_LOG_SLOTS];

static void Wow_LogMissingAnimation(edict_t *ent, cstring_t animation_name, bool invalid_interval) {
    uint32_t model;

    if (!ent || !animation_name || !*animation_name) {
        return;
    }
    model = ent->s.model;
    FOR_LOOP(i, WOW_MISSING_ANIMATION_LOG_SLOTS) {
        wowMissingAnimationLog_t *entry = &wow_missing_animation_log[i];

        if (entry->model == model && !strcasecmp(entry->name, animation_name)) {
            return;
        }
        if (entry->model == 0) {
            entry->model = model;
            strncpy(entry->name, animation_name, sizeof(entry->name) - 1);
            fprintf(stderr, "WoW missing animation: entity=%u model=%u animation=%s%s\n", (unsigned)ent->s.number, (unsigned)model, animation_name, invalid_interval ? " invalid-interval" : "");
            return;
        }
    }

    fprintf(stderr, "WoW missing animation: entity=%u model=%u animation=%s%s\n", (unsigned)ent->s.number, (unsigned)model, animation_name, invalid_interval ? " invalid-interval" : "");
}

float Wow_Clamp(float value, float min_value, float max_value) {
    return MAX(min_value, MIN(value, max_value));
}

static cstring_t Wow_PathBasename(cstring_t path) {
    cstring_t slash = strrchr(path, '/');
    cstring_t backslash = strrchr(path, '\\');

    if (slash && backslash) {
        return MAX(slash, backslash) + 1;
    }
    if (slash) {
        return slash + 1;
    }
    if (backslash) {
        return backslash + 1;
    }
    return path;
}

static void Wow_MapNameFromPath(cstring_t path, string_t out, uint32_t out_size) {
    cstring_t base;
    size_t len;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!path || !*path) {
        return;
    }

    base = Wow_PathBasename(path);
    len = strlen(base);
    if (len > 4 && !strcasecmp(base + len - 4, ".wdt")) {
        len -= 4;
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, base, len);
    out[len] = '\0';
}

typedef struct { uint32_t id; cstring_t path; } gLoadingScreenRec_t;
static stbDbcField_t const loading_screen_schema[] = {
    { 0, offsetof(gLoadingScreenRec_t, id),   STB_DBC_U32 },
    { 2, offsetof(gLoadingScreenRec_t, path), STB_DBC_STR },
};
static stbDbcCache_t loading_screen_dbc;

static bool Wow_ResolveLoadingScreenById(uint32_t loading_screen_id, string_t out, uint32_t out_size) {
    int idx;
    gLoadingScreenRec_t const *rec;
    if (!out || out_size == 0) return false;
    if (!Stb_DbcCacheLoad(&loading_screen_dbc, "DBFilesClient\\LoadingScreens.dbc", &g_dbc_io)) return false;
    Stb_DbcCacheDecode(&loading_screen_dbc, loading_screen_schema,
                       sizeof(loading_screen_schema) / sizeof(loading_screen_schema[0]),
                       sizeof(gLoadingScreenRec_t), &g_dbc_io);
    idx = Stb_DbcCacheFindID(&loading_screen_dbc, loading_screen_id, &g_dbc_io);
    if (idx < 0) return false;
    rec = STB_DBC_ROW(loading_screen_dbc, gLoadingScreenRec_t, idx);
    if (!rec->path || !*rec->path) return false;
    snprintf(out, out_size, "%s", rec->path);
    return true;
}

static void Wow_SelectLoadingScreen(cstring_t map_path) {
    uint8_t *data;
    uint32_t size = 0;
    stbDbc_t h;
    char map_name[128] = { 0 };

    snprintf(wow_loading_texture, sizeof(wow_loading_texture), "%s", "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp");
    snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", "World of Warcraft");

    if (!map_path || !*map_path) {
        return;
    }

    Wow_MapNameFromPath(map_path, map_name, sizeof(map_name));
    if (!map_name[0]) {
        return;
    }

    data = gi.ReadFile("DBFilesClient\\Map.dbc", &size);
    if (!Stb_DbcValid(data, size, &h) || h.fields < 4 || h.record_size < sizeof(wowMapDbc_t)) {
        SAFE_DELETE(data, gi.MemFree);
        return;
    }

    uint8_t const *records_base = Stb_DbcRecords(data);
    uint8_t const *strings_base = Stb_DbcStrings(data, &h);
    FOR_LOOP(record_index, h.records) {
        uint8_t const *record = records_base + record_index * h.record_size;
        wowMapDbc_t const *map = (wowMapDbc_t const *)record;
        cstring_t map_dir = Stb_DbcString(strings_base, h.string_size, map->directory_offset);

        if (!map_dir || strcasecmp(map_dir, map_name)) {
            continue;
        }

        uint32_t loading_screen_id = Stb_DbcRead32(record + (h.fields - 1) * sizeof(uint32_t));
        cstring_t map_title = Stb_DbcString(strings_base, h.string_size, map->title_offset);

        if (map_title && *map_title) {
            snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", map_title);
        } else {
            snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", map_name);
        }

        if (!Wow_ResolveLoadingScreenById(loading_screen_id, wow_loading_texture, sizeof(wow_loading_texture))) {
            snprintf(wow_loading_texture, sizeof(wow_loading_texture), "%s", "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp");
        }

        fprintf(stderr, "Wow_SelectLoadingScreen: map=%s title=%s loadingId=%u texture=%s\n",
                map_name, wow_loading_title, (unsigned)loading_screen_id, wow_loading_texture);
        gi.MemFree(data);
        return;
    }

    gi.MemFree(data);
}

/* ---- Spell Visual DBC Chain ----
 * Resolves spell visual IDs to M2 model paths via the DBC chain:
 *   SpellVisual.dbc → SpellVisualKit.dbc → SpellVisualEffectName.dbc
 *
 * Pattern derived from WoWee's SpellVisualSystem::loadSpellVisualDbc().
 * The server loads these once at map start and caches the results. */

#define WOW_MAX_SPELL_VISUALS 512
#define WOW_MAX_SPELL_VISUAL_EFFECTS 1024

/* SpellVisual chain record structs + column→field schemas. Consumers read named
 * fields; Stb_DbcParseRows fills them from the DBC columns. */
typedef struct { uint32_t id; cstring_t filepath; } gSpellVisualEffectNameRec_t;
static stbDbcField_t const spell_visual_effect_name_schema[] = {
    { 0, offsetof(gSpellVisualEffectNameRec_t, id),       STB_DBC_U32 },
    { 2, offsetof(gSpellVisualEffectNameRec_t, filepath), STB_DBC_STR },
};

/* Kit effect slots (classic layout, validated against WoWee): head 3, chest 4,
 * base 5, left 6, right 7, breath 8, special 11-13. */
typedef struct { uint32_t id, head, chest, base, left, right, breath, special[3]; } gSpellVisualKitRec_t;
static stbDbcField_t const spell_visual_kit_schema[] = {
    {  0, offsetof(gSpellVisualKitRec_t, id),      STB_DBC_U32 },
    {  3, offsetof(gSpellVisualKitRec_t, head),    STB_DBC_U32 },
    {  4, offsetof(gSpellVisualKitRec_t, chest),   STB_DBC_U32 },
    {  5, offsetof(gSpellVisualKitRec_t, base),    STB_DBC_U32 },
    {  6, offsetof(gSpellVisualKitRec_t, left),    STB_DBC_U32 },
    {  7, offsetof(gSpellVisualKitRec_t, right),   STB_DBC_U32 },
    {  8, offsetof(gSpellVisualKitRec_t, breath),  STB_DBC_U32 },
    { 11, offsetof(gSpellVisualKitRec_t, special), STB_DBC_U32, 3 },
};

typedef struct { uint32_t id, precast_kit, cast_kit, impact_kit, missile_effect; } gSpellVisualRec_t;
static stbDbcField_t const spell_visual_schema[] = {
    { 0, offsetof(gSpellVisualRec_t, id),             STB_DBC_U32 },
    { 1, offsetof(gSpellVisualRec_t, precast_kit),    STB_DBC_U32 },
    { 2, offsetof(gSpellVisualRec_t, cast_kit),       STB_DBC_U32 },
    { 3, offsetof(gSpellVisualRec_t, impact_kit),     STB_DBC_U32 },
    { 8, offsetof(gSpellVisualRec_t, missile_effect), STB_DBC_U32 },
};

typedef struct {
    uint32_t visual_id;
    cstring_t cast_path;
    cstring_t impact_path;
    cstring_t missile_path;
} wowSpellVisual_t;

static wowSpellVisual_t wow_spell_visuals[WOW_MAX_SPELL_VISUALS];
static uint32_t wow_spell_visual_count = 0;
static bool wow_spell_visuals_loaded = false;

/* Cached effect name paths: effect_name_id → M2 path */
typedef struct {
    uint32_t id;
    cstring_t path;
} wowSpellVisualEffect_t;

static wowSpellVisualEffect_t wow_visual_effects[WOW_MAX_SPELL_VISUAL_EFFECTS];
static uint32_t wow_visual_effect_count = 0;

stbDbcIO_t const g_dbc_io = { G_DbcRead, G_DbcFreeFile, G_DbcAlloc, G_DbcFreeMem };

static cstring_t Wow_FindVisualEffectPath(uint32_t effect_id) {
    FOR_LOOP(i, wow_visual_effect_count) {
        if (wow_visual_effects[i].id == effect_id) {
            return wow_visual_effects[i].path;
        }
    }
    return NULL;
}

/* Resolve a kit ID to the best M2 path by probing effect slots.
 * WoWee probes: SpecialEffect0 > BaseEffect > LeftHand > RightHand > Chest > Head > Breath */
static cstring_t Wow_ResolveKitPath(gSpellVisualKitRec_t const *kit) {
    /* Probe effect slots in priority order (WoWee): SpecialEffect0 > BaseEffect >
     * LeftHand > RightHand > Chest > Head > Breath. */
    static ptrdiff_t const probe[] = {
        offsetof(gSpellVisualKitRec_t, special),
        offsetof(gSpellVisualKitRec_t, base),
        offsetof(gSpellVisualKitRec_t, left),
        offsetof(gSpellVisualKitRec_t, right),
        offsetof(gSpellVisualKitRec_t, chest),
        offsetof(gSpellVisualKitRec_t, head),
        offsetof(gSpellVisualKitRec_t, breath),
    };
    FOR_LOOP(i, sizeof(probe) / sizeof(probe[0])) {
        uint32_t eff_id = *(uint32_t const *)((uint8_t const *)kit + probe[i]);
        if (eff_id) {
            cstring_t path = Wow_FindVisualEffectPath(eff_id);
            if (path && *path) return path;
        }
    }
    return NULL;
}

static void Wow_LoadSpellVisualDbcs(void) {
    stbDbcCache_t fx = { 0 }, kit = { 0 }, sv = { 0 };
    stbDbcIO_t const *io = &g_dbc_io;

    if (wow_spell_visuals_loaded) return;
    wow_spell_visuals_loaded = true;

    /* Phase 1: SpellVisualEffectName.dbc → effect ID → M2 path */
    if (Stb_DbcCacheLoad(&fx, "DBFilesClient\\SpellVisualEffectName.dbc", io) &&
        Stb_DbcCacheDecode(&fx, spell_visual_effect_name_schema, sizeof(spell_visual_effect_name_schema) / sizeof(spell_visual_effect_name_schema[0]),
                           sizeof(gSpellVisualEffectNameRec_t), io) &&
        wow_visual_effect_count == 0) {
        FOR_LOOP(i, fx.records) {
            if (wow_visual_effect_count >= WOW_MAX_SPELL_VISUAL_EFFECTS) break;
            gSpellVisualEffectNameRec_t const *rec = STB_DBC_ROW(fx, gSpellVisualEffectNameRec_t, i);
            cstring_t path = rec->filepath;
            if (rec->id && path && *path) {
                /* Convert .mdx/.mdl extensions to .m2 (WoWee pattern) */
                size_t len = strlen(path);
                char *buf = gi.MemAlloc(len + 1);
                if (buf) {
                    memcpy(buf, path, len + 1);
                    if (len > 4) {
                        char *ext = buf + len - 4;
                        if (!strcasecmp(ext, ".mdx") || !strcasecmp(ext, ".mdl")) {
                            ext[1] = 'm'; ext[2] = '2'; ext[3] = '\0';
                        }
                    }
                    wow_visual_effects[wow_visual_effect_count].id = rec->id;
                    wow_visual_effects[wow_visual_effect_count].path = buf;
                    wow_visual_effect_count++;
                }
            }
        }
        fprintf(stderr, "WoW: loaded %u spell visual effects from SpellVisualEffectName.dbc\n", (unsigned)wow_visual_effect_count);
    }
    Stb_DbcCacheFree(&fx, io);

    /* Phase 2: SpellVisualKit.dbc → kit ID → effect slots (decoded once, resolved inline). */
    Stb_DbcCacheLoad(&kit, "DBFilesClient\\SpellVisualKit.dbc", io);
    Stb_DbcCacheDecode(&kit, spell_visual_kit_schema, sizeof(spell_visual_kit_schema) / sizeof(spell_visual_kit_schema[0]),
                       sizeof(gSpellVisualKitRec_t), io);

    /* Phase 3: SpellVisual.dbc → visual ID → cast/impact/missile paths */
    if (Stb_DbcCacheLoad(&sv, "DBFilesClient\\SpellVisual.dbc", io) &&
        Stb_DbcCacheDecode(&sv, spell_visual_schema, sizeof(spell_visual_schema) / sizeof(spell_visual_schema[0]),
                           sizeof(gSpellVisualRec_t), io) &&
        wow_spell_visual_count == 0) {
        FOR_LOOP(i, sv.records) {
            if (wow_spell_visual_count >= WOW_MAX_SPELL_VISUALS) break;
            gSpellVisualRec_t const *rec = STB_DBC_ROW(sv, gSpellVisualRec_t, i);
            if (!rec->id) continue;

            cstring_t cast_path = NULL, impact_path = NULL, missile_path = NULL;
            int ki;

            /* Resolve cast kit → M2 path (fall back to precast kit) */
            if (rec->cast_kit && kit.valid && (ki = Stb_DbcCacheFindID(&kit, rec->cast_kit, io)) >= 0)
                cast_path = Wow_ResolveKitPath(STB_DBC_ROW(kit, gSpellVisualKitRec_t, ki));
            if (!cast_path && rec->precast_kit && kit.valid && (ki = Stb_DbcCacheFindID(&kit, rec->precast_kit, io)) >= 0)
                cast_path = Wow_ResolveKitPath(STB_DBC_ROW(kit, gSpellVisualKitRec_t, ki));

            /* Resolve impact kit → M2 path */
            if (rec->impact_kit && kit.valid && (ki = Stb_DbcCacheFindID(&kit, rec->impact_kit, io)) >= 0)
                impact_path = Wow_ResolveKitPath(STB_DBC_ROW(kit, gSpellVisualKitRec_t, ki));

            /* Missile model: direct effect name reference */
            if (rec->missile_effect)
                missile_path = Wow_FindVisualEffectPath(rec->missile_effect);

            /* Store if we found at least one path */
            if (cast_path || impact_path || missile_path) {
                wowSpellVisual_t *spv = &wow_spell_visuals[wow_spell_visual_count++];
                spv->visual_id = rec->id;
                spv->cast_path = cast_path;
                spv->impact_path = impact_path;
                spv->missile_path = missile_path;
            }
        }
        fprintf(stderr, "WoW: loaded %u spell visuals from SpellVisual.dbc\n", (unsigned)wow_spell_visual_count);
    }

    Stb_DbcCacheFree(&sv, io);
    Stb_DbcCacheFree(&kit, io);
}

/* Look up the spell visual for a given visual ID.
 * Returns NULL if no visual is found. */
static wowSpellVisual_t const *Wow_FindSpellVisual(uint32_t visual_id) {
    FOR_LOOP(i, wow_spell_visual_count) {
        if (wow_spell_visuals[i].visual_id == visual_id) {
            return &wow_spell_visuals[i];
        }
    }
    return NULL;
}

float Wow_TerrainHeight(float x, float y) {
    return CM_GetHeightAtPoint(x, y);
}

float Wow_FloorHeight(float x, float y, float z) { return CM_WowFloorHeight(x, y, z, 1.5f); }

/* Terrain must obey the outdoor slope limit; reachable WMO steps use their authored collision floor instead. */
bool Wow_TerrainMoveWalkable(vec3_t const *from, vec3_t const *to, float terrain) {
    float dx, dy, dist;
    if (fabsf(to->z - terrain) > WOW_GROUND_EPSILON) return true;
    dx = to->x - from->x; dy = to->y - from->y; dist = sqrtf(dx * dx + dy * dy);
    return fabsf(to->z - from->z) <= dist * WOW_MAX_WALK_SLOPE_TAN + WOW_GROUND_EPSILON;
}

static float Wow_ViewPitch(float wrapped_pitch) {
    return wrapped_pitch > 180.0f ? 360.0f - wrapped_pitch : -wrapped_pitch;
}

static void Wow_AngleVectors(float yaw, vec2_t *forward, vec2_t *right) {
    float angle = (float)DEG2RAD(yaw);
    float sy = sinf(angle);
    float cy = cosf(angle);

    if (forward) {
        forward->x = cy;
        forward->y = sy;
    }
    if (right) {
        right->x = sy;
        right->y = -cy;
    }
}

uint32_t Wow_EntityIndex(edict_t const *ent) {
    if (!ent || ent < wow_edicts || ent >= wow_edicts + WOW_MAX_EDICTS) {
        return WOW_MAX_EDICTS;
    }
    return (uint32_t)(ent - wow_edicts);
}

wowEntityLocal_t *Wow_EntityLocal(edict_t const *ent) {
    uint32_t index = Wow_EntityIndex(ent);

    if (index >= WOW_MAX_EDICTS) {
        return NULL;
    }
    return &wow_entity_locals[index];
}

animation_t const *Wow_SetEntityAnimation(edict_t *ent, cstring_t animation_name) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    animation_t const *anim;

    if (!ent || !local || !animation_name || ent->s.model == 0) {
        if (local) {
            local->animation = NULL;
        }
        return NULL;
    }
    anim = G_GetAnimation(ent->s.model, animation_name);
    if (!anim || anim->interval[1] <= anim->interval[0]) {
        Wow_LogMissingAnimation(ent, animation_name, anim != NULL);
        local->animation = NULL;
        return NULL;
    }
    if (local->animation != anim) {
        ent->s.frame = anim->interval[0];
        local->animation = anim;
    }
    return local->animation;
}

bool Wow_SetEntityMoveFirstAnimation(edict_t *ent, wowMove_t *move, cstring_t const *animation_names) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || !move) {
        return false;
    }
    if (local->currentmove == move && local->animation) {
        return true;
    }
    for (cstring_t const *name = animation_names; name && *name; name++) {
        if (Wow_SetEntityAnimation(ent, *name)) {
            local->currentmove = move;
            return true;
        }
    }
    local->currentmove = NULL;
    return false;
}

bool Wow_SetEntityMove(edict_t *ent, wowMove_t *move) {
    cstring_t names[2];

    if (!move || !move->animation) {
        return false;
    }
    names[0] = move->animation;
    names[1] = NULL;
    return Wow_SetEntityMoveFirstAnimation(ent, move, names);
}

void Wow_AdvanceEntityFrame(edict_t *ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    uint32_t next_frame;

    if (!ent || !local || !local->animation) {
        return;
    }
    next_frame = ent->s.frame + FRAMETIME;
    if (ent->s.frame < local->animation->interval[0] ||
        ent->s.frame >= local->animation->interval[1] ||
        next_frame >= local->animation->interval[1]) {
        ent->s.frame = local->animation->interval[0];
    } else {
        ent->s.frame = next_frame;
    }
}

/* ---- Projectile system (WC3-style homing missiles) ---- */

/* Forward declarations for functions defined later in this file. */
static edict_t *Wow_EdictByNumber(uint32_t number);
static edict_t *Wow_FindNearestAttackTarget(edict_t *ent, float range);

static bool Wow_IsAttackTarget(edict_t const *caster, edict_t const *target) {
    wowEntityLocal_t *local = target ? Wow_EntityLocal(target) : NULL;
    return target && target != caster && target->inuse && (target->svflags & SVF_MONSTER) &&
        local && !local->dead && local->health > 0 &&
        (local->hostile || (target->s.flags & EF_HOSTILE));
}

#define WOW_FIREBOLT_SPEED      25.0f
#define WOW_FIREBOLT_DAMAGE     2
#define WOW_FIREBOLT_RANGE      30.0f
#define WOW_FIREBOLT_MANA_COST  10
#define WOW_FIREBOLT_CAST_TIME  1500  /* 1.5s cast time (Classic Fireball) */
#define WOW_FROSTBOLT_SPEED     20.0f
#define WOW_FROSTBOLT_DAMAGE    3
#define WOW_FROSTBOLT_RANGE     30.0f
#define WOW_FROSTBOLT_MANA_COST 15
#define WOW_FROSTBOLT_SLOW_MS   2000
#define WOW_FROSTBOLT_CAST_TIME 2500  /* 2.5s cast time (Classic Frostbolt) */
#define WOW_HEALING_TOUCH_HEAL  2
#define WOW_HEALING_TOUCH_MANA_COST 8
#define WOW_MANA_MAX            100
#define WOW_MANA_REGEN_PER_SEC  3

/* Spell definition table: each spell is a row with function pointers.
 * Pattern follows Quake 2's gitem_t itemlist[] — data-driven, no enum switch. */

static void Wow_SpellAttack(edict_t *caster, edict_t *target) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (cl) cl->enemy = target;
    if (cl && cl->attack) cl->attack(caster);
}
static void Wow_FireFireballRank(edict_t *caster, edict_t *target, uint32_t damage);
static void Wow_SpellFireball(edict_t *caster, edict_t *target) { Wow_FireFirebolt(caster, target); }
static void Wow_SpellFireballRank2(edict_t *caster, edict_t *target) { Wow_FireFireballRank(caster, target, 31 + (uint32_t)(rand() % 15)); }
static void Wow_SpellFrostbolt(edict_t *caster, edict_t *target) { Wow_FireFrostbolt(caster, target); }
static void Wow_SpellFireBlast(edict_t *caster, edict_t *target) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (!cl || !target) return;
    cl->mana -= wow_spells[WOW_SPELL_FIRE_BLAST].mana_cost;
    /* Spell.dbc rank 1: base points 23 plus a 1–9 roll. */
    Wow_ApplyDamage(target, caster, 24 + (uint32_t)(rand() % 9));
}
static void Wow_SpellHealingTouch(edict_t *caster, edict_t *target) { (void)target; Wow_HealingTouch(caster); }

wowSpellDef_t const wow_spells[WOW_SPELL_COUNT] = {
    [WOW_SPELL_ATTACK]        = { "Attack",        Wow_SpellAttack,           0,    0,  5.0f, NULL,                NULL,               0, 0 },
    [WOW_SPELL_FIREBOLT]      = { "Fireball",      Wow_SpellFireball,      1500,   10, 30.0f, "SpellCastDirected", "ReadySpellDirected", 133, 0 },
    [WOW_SPELL_FROSTBOLT]     = { "Frostbolt",     Wow_SpellFrostbolt,     2500,   15, 30.0f, "SpellCastDirected", "ReadySpellDirected", 116, 0 },
    [WOW_SPELL_HEALING_TOUCH] = { "Healing Touch", Wow_SpellHealingTouch,      0,    WOW_HEALING_TOUCH_MANA_COST,  0.0f, NULL, NULL, 0, 0 },
    [WOW_SPELL_FIRE_BLAST]    = { "Fire Blast", Wow_SpellFireBlast, 0, 40, 20.0f, NULL, NULL, 2136, 8000 },
    [WOW_SPELL_FIREBALL_RANK2] = { "Fireball (Rank 2)", Wow_SpellFireballRank2, 2000, 45, 30.0f, "SpellCastDirected", "ReadySpellDirected", 143, 0 },
};
uint32_t const wow_spell_count = sizeof(wow_spells) / sizeof(wow_spells[0]);

bool Wow_SpellKnown(uint32_t class_id, uint32_t spell) {
    if (spell == WOW_SPELL_ATTACK || spell == WOW_SPELL_HEALING_TOUCH) return true;
    return class_id == WOW_CLASS_MAGE && (spell == WOW_SPELL_FIREBOLT ||
        spell == WOW_SPELL_FROSTBOLT || spell == WOW_SPELL_FIRE_BLAST || spell == WOW_SPELL_FIREBALL_RANK2);
}

bool Wow_PlayerKnowsSpell(edict_t *ent, uint32_t spell) {
    if (!ent || !ent->client || !Wow_SpellKnown(Wow_GetPlayerClass(), spell)) return false;
    return spell != WOW_SPELL_FIREBALL_RANK2 || ((wowClient_t *)ent->client)->fireball_rank2_learned;
}

cstring_t Wow_SpellIcon(uint32_t spell) {
    static cstring_t const icons[WOW_SPELL_COUNT] = {
        "Interface\\Icons\\Ability_Warrior_Cleave.blp",
        "Interface\\Icons\\Spell_Fire_FireBolt02.blp",
        "Interface\\Icons\\Spell_Frost_FrostBolt02.blp",
        "Interface\\Icons\\Spell_Nature_HealingTouch.blp",
        "Interface\\Icons\\Spell_Fire_Fireball.blp",
        "Interface\\Icons\\Spell_Fire_FireBolt02.blp"
    };
    return spell < WOW_SPELL_COUNT ? icons[spell] : "";
}


/* SPELL_NONE / SPELL_FIREBOLT etc. defined in g_wow_local.h */

/* Spell.dbc: maps spell_dbc_id → SpellVisual ID.
 * Classic 1.12 layout: 148 fields, SpellVisualID at field 115. */
#define WOW_MAX_SPELL_VISUAL_MAP 256
static uint32_t wow_spell_visual_map[WOW_MAX_SPELL_VISUAL_MAP]; /* index = spell_dbc_id, value = visual_id */
static bool wow_spell_dbc_loaded = false;

static void Wow_LoadSpellDbc(void) {
    uint8_t *data = NULL;
    uint32_t size = 0;
    stbDbc_t h;
    uint32_t visual_field;

    if (wow_spell_dbc_loaded) return;
    wow_spell_dbc_loaded = true;
    memset(wow_spell_visual_map, 0, sizeof(wow_spell_visual_map));

    data = gi.ReadFile("DBFilesClient\\Spell.dbc", &size);
    if (!Stb_DbcValid(data, size, &h)) {
        SAFE_DELETE(data, gi.MemFree);
        return;
    }
    /* Classic Spell.dbc has 148 fields with SpellVisualID at 115.
     * WotLK has 234 fields with SpellVisual at 131. Pick the right field. */
    visual_field = h.fields >= 200 ? 131 : 115;
    if (visual_field >= h.fields) { SAFE_DELETE(data, gi.MemFree); return; }

    {
        uint8_t const *records_base = Stb_DbcRecords(data);
        FOR_LOOP(i, h.records) {
            uint8_t const *record = records_base + i * h.record_size;
            uint32_t id = Stb_DbcRead32(record);
            if (id < WOW_MAX_SPELL_VISUAL_MAP) {
                wow_spell_visual_map[id] = Stb_DbcRead32(record + visual_field * sizeof(uint32_t));
            }
        }
    }
    fprintf(stderr, "WoW: loaded %u spells from Spell.dbc\n", (unsigned)h.records);
    Wow_LoadSpellVisualDbcs();
    SAFE_DELETE(data, gi.MemFree);
}

/* Resolve a spell's DBC spell ID → SpellVisual ID → missile M2 model.
 * Returns 0 if no DBC data is available. */
uint32_t Wow_SpellMissileModel(uint32_t spell_dbc_id) {
    static uint32_t resolved_models[WOW_MAX_SPELL_VISUAL_MAP];
    static bool model_resolved[WOW_MAX_SPELL_VISUAL_MAP];

    if (spell_dbc_id == 0 || spell_dbc_id >= WOW_MAX_SPELL_VISUAL_MAP) return 0;
    if (model_resolved[spell_dbc_id]) return resolved_models[spell_dbc_id];
    model_resolved[spell_dbc_id] = true;

    if (!wow_spell_dbc_loaded) Wow_LoadSpellDbc();
    uint32_t visual_id = wow_spell_visual_map[spell_dbc_id];
    if (!visual_id) return 0;

    wowSpellVisual_t const *sv = Wow_FindSpellVisual(visual_id);
    if (!sv || !sv->missile_path) return 0;

    uint32_t sz;
    handle_t buf = gi.ReadFile(sv->missile_path, &sz);
    if (!buf) return 0;
    resolved_models[spell_dbc_id] = G_RegisterModel(sv->missile_path);
    gi.MemFree(buf);
    fprintf(stderr, "WoW: DBC missile model for spell %u: %s (idx %u)\n", (unsigned)spell_dbc_id, sv->missile_path, (unsigned)resolved_models[spell_dbc_id]);
    return resolved_models[spell_dbc_id];
}

/* Resolve a spell's DBC spell ID → SpellVisual ID → impact M2 model configstring index. */
uint32_t Wow_SpellImpactModel(uint32_t spell_dbc_id) {
    static uint32_t resolved_impacts[WOW_MAX_SPELL_VISUAL_MAP];
    static bool impact_resolved[WOW_MAX_SPELL_VISUAL_MAP];

    if (spell_dbc_id == 0 || spell_dbc_id >= WOW_MAX_SPELL_VISUAL_MAP) return 0;
    if (impact_resolved[spell_dbc_id]) return resolved_impacts[spell_dbc_id];
    impact_resolved[spell_dbc_id] = true;

    if (!wow_spell_dbc_loaded) Wow_LoadSpellDbc();
    uint32_t visual_id = wow_spell_visual_map[spell_dbc_id];
    if (!visual_id) return 0;

    wowSpellVisual_t const *sv = Wow_FindSpellVisual(visual_id);
    if (!sv || !sv->impact_path) return 0;

    uint32_t sz;
    handle_t buf = gi.ReadFile(sv->impact_path, &sz);
    if (!buf) return 0;
    resolved_impacts[spell_dbc_id] = gi.ModelIndex(sv->impact_path);
    gi.MemFree(buf);
    fprintf(stderr, "WoW: DBC impact model for spell %u: %s (idx %u)\n", (unsigned)spell_dbc_id, sv->impact_path, (unsigned)resolved_impacts[spell_dbc_id]);
    return resolved_impacts[spell_dbc_id];
}

uint32_t Wow_FireboltModel(void) {
    static uint32_t model = 0;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        /* Try DBC-resolved missile path first, fall back to hardcoded paths. */
        uint32_t dbc_model = Wow_SpellMissileModel(133);
        if (dbc_model) { model = dbc_model; return model; }
        cstring_t const paths[] = {
            "Spells\\Fireball_Missile_High.m2",
            "Spells\\Fireball_Missile_Low.m2",
            "Spells\\FireBolt_Missile_Low.m2",
            "Spells\\FireShot_Missile.m2",
            NULL
        };
        for (cstring_t const *p = paths; *p; p++) {
            uint32_t sz;
            handle_t buf = gi.ReadFile(*p, &sz);
            if (buf) {
                model = G_RegisterModel(*p);
                gi.MemFree(buf);
                fprintf(stderr, "WoW: firebolt model loaded: %s (idx %u)\n", *p, (unsigned)model);
                break;
            }
        }
        if (!model)
            fprintf(stderr, "WoW: no firebolt model in MPQ\n");
    }
    return model;
}

/* ---- Cast State Machine ---- */

static void Wow_BeginSpellCast(edict_t *caster, uint32_t spell_id, uint32_t target_num) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    edict_t *target = Wow_EdictByNumber(target_num);
    if (!cl || spell_id >= wow_spell_count) return;
    wowSpellDef_t const *def = &wow_spells[spell_id];
    cl->attack_damage_time = 0; cl->attack_backswing_time = 0; cl->attack_time = 0;
    cl->cast_spell     = spell_id;
    cl->cast_duration  = def->cast_time;
    cl->cast_remaining = def->cast_time;
    cl->cast_target    = target_num;
    cl->cast_origin    = (vec2_t){ caster->s.origin.x, caster->s.origin.y };
    cl->cast_release_time = 0;
    if (def->ready_anim) {
        cstring_t anim_names[] = { def->ready_anim, NULL };
        wowMove_t ready_move = { def->ready_anim, NULL, NULL };
        Wow_SetEntityMoveFirstAnimation(caster, &ready_move, anim_names);
    }
    if (target && target != caster) Wow_FaceTarget(caster, target);
    cl->gcd_time = WOW_GCD_MS;
}

static void Wow_CancelSpellCast(edict_t *caster) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (!cl) return;
    cl->cast_spell = SPELL_NONE;
    cl->cast_duration = cl->cast_remaining = cl->cast_target = cl->cast_release_time = 0;
    Wow_SetStandMove(caster);
    /* Mana is NOT consumed on cancel; movement/interrupt refunds the cost */
}

static void Wow_CompleteSpellCast(edict_t *caster) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (!cl || cl->cast_spell == SPELL_NONE) return;
    edict_t *target = Wow_EdictByNumber(cl->cast_target);
    uint32_t spell = cl->cast_spell;
    cl->cast_spell = SPELL_NONE;
    cl->cast_duration = cl->cast_remaining = 0;
    /* The projectile launches at cast completion while the character plays the non-looping release sequence. */
    {
        static cstring_t const release_anims[] = { "SpellCastDirected", "SpellCastOmni", "Spell", NULL };
        if (Wow_SetEntityMoveFirstAnimation(caster, &wow_move_cast, release_anims) && cl->animation)
            cl->cast_release_time = cl->animation->interval[1] - cl->animation->interval[0];
    }
    /* Mana consumed on completion; no cost if interrupted or cancelled */
    if (spell < wow_spell_count) {
        wowSpellDef_t const *def = &wow_spells[spell];
        cl->spell_cooldown[spell] = def->cooldown_ms;
        if (def->mana_cost)
            cl->mana = cl->mana >= def->mana_cost ? cl->mana - def->mana_cost : 0;
        if (def->cast && target && target->inuse)
            def->cast(caster, target);
        else if (!target || def->range == 0.0f)
            def->cast(caster, NULL);  /* self-targeted or instant */
    }
    cl->cast_target = 0;
    if (spell < wow_spell_count && wow_spells[spell].cooldown_ms && caster->client &&
        caster->client->ps.client_ui_state == CLIENT_UI_GAME)
        UI_WriteWowActionBar(caster);
}

/* Per-frame cast progress. Returns true while entity is casting (locked). */
static bool Wow_RunSpellCast(edict_t *ent) {
    wowEntityLocal_t *cl = Wow_EntityLocal(ent);
    if (!cl) return false;
    if (cl->cast_release_time > 0) {
        cl->cast_release_time -= cl->cast_release_time > FRAMETIME ? FRAMETIME : cl->cast_release_time;
        if (cl->cast_release_time == 0) Wow_SetStandMove(ent);
        return true;
    }
    if (cl->cast_spell == SPELL_NONE) return false;

    /* Movement interrupt: if caster moved from cast-start position, cancel cast */
    if (fabsf(ent->s.origin.x - cl->cast_origin.x) > 0.1f ||
        fabsf(ent->s.origin.y - cl->cast_origin.y) > 0.1f) {
        Wow_CancelSpellCast(ent);
        return false;
    }

    /* Target validation: if target dies/vanishes, cancel cast */
    if (cl->cast_target) {
        edict_t *target = Wow_EdictByNumber(cl->cast_target);
        wowEntityLocal_t *target_local = target ? Wow_EntityLocal(target) : NULL;
        if (!target || !target->inuse || (target_local && target_local->dead) ||
            (wow_spells[cl->cast_spell].range > 0 &&
             (!Wow_IsAttackTarget(ent, target) ||
              Vector2_len(&(vec2_t){ target->s.origin2.x - ent->s.origin2.x,
                                     target->s.origin2.y - ent->s.origin2.y }) > wow_spells[cl->cast_spell].range))) {
            Wow_CancelSpellCast(ent);
            return false;
        }
    }

    cl->cast_remaining -= cl->cast_remaining > FRAMETIME ? FRAMETIME : cl->cast_remaining;
    if (cl->cast_remaining == 0) {
        Wow_CompleteSpellCast(ent);
        return cl->cast_release_time > 0;
    }
    return true; /* still casting */
}

/* Each frame: advance active projectile toward its target. */
void Wow_RunProjectile(edict_t *ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    edict_t *target;

    if (!ent || !local || local->think != Wow_RunProjectile || !ent->inuse) {
        return;
    }
    target = Wow_EdictByNumber(local->projectile_target);
    if (!target || !target->inuse) {
        ent->inuse = false;
        return;
    }
    {
        vec2_t const t2 = (vec2_t){ target->s.origin.x, target->s.origin.y };
        vec2_t const p2 = (vec2_t){ ent->s.origin.x, ent->s.origin.y };
        vec2_t delta = Vector2_sub(&t2, &p2);
        float dist = sqrtf(delta.x * delta.x + delta.y * delta.y);
        float step = local->projectile_speed * ((float)FRAMETIME / 1000.0f);

        if (dist <= step) {
            /* Hit the target — delegate damage to the shared combat path (Q2 T_Damage analog). */
            edict_t *caster = Wow_EdictByNumber(local->projectile_caster);
            wowEntityLocal_t *target_local = Wow_EntityLocal(target);
            Wow_ApplyDamage(target, caster, local->projectile_damage);
            /* slow_timer on the projectile encodes the debuff duration to apply. */
            if (target_local && !target_local->dead && local->slow_timer > 0)
                target_local->slow_timer = MAX(target_local->slow_timer, local->slow_timer);
            /* Broadcast a client-side impact effect to all nearby observers. */
            {
                bool is_frost = local->slow_timer > 0;
                int impact_model = is_frost ? wow_frostbolt_impact_model : wow_firebolt_impact_model;
                tempEvent_t te = is_frost ? TE_FROSTBOLT_IMPACT : TE_FIREBOLT_IMPACT;
                if (impact_model > 0) {
                    gi.Write(PF_BYTE, &(int32_t){ svc_temp_entity });
                    gi.Write(PF_BYTE, &(int32_t){ te });
                    gi.Write(PF_POSITION, &ent->s.origin);
                    gi.Write(PF_SHORT, &(int32_t){ impact_model });
                    gi.multicast(&ent->s.origin, MULTICAST_PVS);
                }
            }
            ent->inuse = false;
            return;
        }
        /* Move toward the target's gameplay center; exact animated impact tags are renderer-owned. */
        ent->s.origin.x += delta.x * step / dist;
        ent->s.origin.y += delta.y * step / dist;
        {
            float target_chest_z = G_GetAttachmentZ(target->s.model, 20);
            /* WoW attachment 20 is the chest; server hit testing remains independent of renderer bones. */
            if (target_chest_z <= 0) target_chest_z = target->s.radius * 2.0f;
            float target_z = target->s.origin.z + target_chest_z * target->s.scale;
            ent->s.origin.z += (target_z - ent->s.origin.z) * step / dist;
        }
        local->projectile_yaw = (float)RAD2DEG(atan2f(delta.y, delta.x));
        ent->s.angle = (float)DEG2RAD(local->projectile_yaw);
    }
}

void Wow_FireFirebolt(edict_t *caster, edict_t *target) {
    Wow_FireFireballRank(caster, target, WOW_FIREBOLT_DAMAGE);
}

static void Wow_FireFireballRank(edict_t *caster, edict_t *target, uint32_t damage) {
    wowEntityLocal_t *caster_local;
    wowEntityLocal_t *pl;
    edict_t *proj;
    float yaw;

    if (!caster || !target || caster == target || !target->inuse) {
        return;
    }
    {
        wowEntityLocal_t *target_local = Wow_EntityLocal(target);
        if (target_local && target_local->dead) {
            return;
        }
    }
    caster_local = Wow_EntityLocal(caster);
    if (!caster_local || caster_local->dead) return;
    proj = Wow_Spawn();
    if (!proj) return;

    pl = Wow_EntityLocal(proj);
    if (!pl) return;

    pl->think = Wow_RunProjectile;
    {
        vec2_t delta = Vector2_sub(&(vec2_t){ target->s.origin.x, target->s.origin.y }, &(vec2_t){ caster->s.origin.x, caster->s.origin.y });
        yaw = (float)RAD2DEG(atan2f(delta.y, delta.x));
    }
    pl->projectile_target = target->s.number;
    pl->projectile_caster = caster->s.number;
    pl->projectile_speed  = WOW_FIREBOLT_SPEED;
    pl->projectile_damage = damage;
    pl->projectile_yaw = yaw;
    pl->projectile_pitch = 0.0f;
    {
        float hand_z = G_GetAttachmentZ(caster->s.model, 1);
        /* TODO: the renderer must eventually seed the visual from M2_AttachmentMatrix at the release frame. */
        if (hand_z <= 0) hand_z = caster->s.radius;
        proj->s.origin.z = caster->s.origin.z + hand_z * caster->s.scale;
    }

    proj->s.origin.x = caster->s.origin.x;
    proj->s.origin.y = caster->s.origin.y;
    proj->s.origin2 = (vec2_t){ proj->s.origin.x, proj->s.origin.y };
    proj->s.angle  = (float)DEG2RAD(yaw);
    proj->s.model  = Wow_FireboltModel();
    proj->s.scale  = 0.8f;
    proj->s.radius = 0.5f;
    proj->s.player = caster->s.player;
    /* Ranged spells use the selected target, not the melee combat target; leaving
     * this field set would make the generic frame loop chase after launch. */
    caster_local->enemy = NULL;
}

uint32_t Wow_FrostboltModel(void) {
    static uint32_t model = 0;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        /* Try DBC-resolved missile path first, fall back to hardcoded paths. */
        uint32_t dbc_model = Wow_SpellMissileModel(116);
        if (dbc_model) { model = dbc_model; return model; }
        cstring_t const paths[] = {
            "Spells\\FrostBolt_Missile_Low.m2",
            "Spells\\Frostbolt_Missile.m2",
            "Spells\\FrostShot_Missile.m2",
            NULL
        };
        for (cstring_t const *p = paths; *p; p++) {
            uint32_t sz;
            handle_t buf = gi.ReadFile(*p, &sz);
            if (buf) {
                model = G_RegisterModel(*p);
                gi.MemFree(buf);
                fprintf(stderr, "WoW: frostbolt model loaded: %s (idx %u)\n", *p, (unsigned)model);
                break;
            }
        }
        if (!model) fprintf(stderr, "WoW: no frostbolt model in MPQ\n");
    }
    return model;
}

/* Firebolt impact burst: DBC SpellVisual chain first, hardcoded paths as fallback. */
uint32_t Wow_FireboltImpactModel(void) {
    static uint32_t model = 0;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        uint32_t dbc_model = Wow_SpellImpactModel(133);
        if (dbc_model) { model = dbc_model; return model; }
        cstring_t const paths[] = {
            "Spells\\FireBolt_ImpactDD_Med_Chest.m2",
            "Spells\\Fire_ImpactDD_Med_Chest.m2",
            NULL
        };
        for (cstring_t const *p = paths; *p; p++) {
            uint32_t sz;
            handle_t buf = gi.ReadFile(*p, &sz);
            if (buf) {
                model = gi.ModelIndex(*p);
                gi.MemFree(buf);
                fprintf(stderr, "WoW: firebolt impact model loaded: %s (idx %u)\n", *p, (unsigned)model);
                break;
            }
        }
        if (!model) fprintf(stderr, "WoW: no firebolt impact model in MPQ\n");
    }
    return model;
}

/* Frostbolt impact burst: DBC SpellVisual chain first, hardcoded paths as fallback. */
uint32_t Wow_FrostboltImpactModel(void) {
    static uint32_t model = 0;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        uint32_t dbc_model = Wow_SpellImpactModel(116);
        if (dbc_model) { model = dbc_model; return model; }
        cstring_t const paths[] = {
            "Spells\\Ice_ImpactDD_Med_Chest.m2",
            "Spells\\Ice_ImpactDD_Low_Chest.m2",
            NULL
        };
        for (cstring_t const *p = paths; *p; p++) {
            uint32_t sz;
            handle_t buf = gi.ReadFile(*p, &sz);
            if (buf) {
                model = gi.ModelIndex(*p);
                gi.MemFree(buf);
                fprintf(stderr, "WoW: frostbolt impact model loaded: %s (idx %u)\n", *p, (unsigned)model);
                break;
            }
        }
        if (!model) fprintf(stderr, "WoW: no frostbolt impact model in MPQ\n");
    }
    return model;
}

/* Fire a Frostbolt: like Firebolt but slower, hits harder, and slows the target. */
void Wow_FireFrostbolt(edict_t *caster, edict_t *target) {
    wowEntityLocal_t *caster_local, *pl;
    edict_t *proj;
    float yaw;

    if (!caster || !target || caster == target || !target->inuse) return;
    {
        wowEntityLocal_t *tl = Wow_EntityLocal(target);
        if (tl && tl->dead) return;
    }
    caster_local = Wow_EntityLocal(caster);
    if (!caster_local || caster_local->dead) return;
    proj = Wow_Spawn();
    if (!proj) return;
    pl = Wow_EntityLocal(proj);
    if (!pl) return;

    pl->think = Wow_RunProjectile;
    {
        vec2_t delta = Vector2_sub(&(vec2_t){ target->s.origin.x, target->s.origin.y }, &(vec2_t){ caster->s.origin.x, caster->s.origin.y });
        yaw = (float)RAD2DEG(atan2f(delta.y, delta.x));
    }
    pl->projectile_target = target->s.number;
    pl->projectile_caster = caster->s.number;
    pl->projectile_speed  = WOW_FROSTBOLT_SPEED;
    pl->projectile_damage = WOW_FROSTBOLT_DAMAGE;
    pl->projectile_yaw    = yaw;
    pl->projectile_pitch  = 0.0f;
    {
        float hand_z = G_GetAttachmentZ(caster->s.model, 1);
        /* TODO: the renderer must eventually seed the visual from M2_AttachmentMatrix at the release frame. */
        if (hand_z <= 0) hand_z = caster->s.radius;
        proj->s.origin.z = caster->s.origin.z + hand_z * caster->s.scale;
    }
    /* Reuse slow_timer field to signal that this projectile applies a slow on hit.
     * A non-zero value in the projectile local means "apply slow on impact". */
    pl->slow_timer = WOW_FROSTBOLT_SLOW_MS;

    proj->s.origin.x = caster->s.origin.x;
    proj->s.origin.y = caster->s.origin.y;
    proj->s.origin2 = (vec2_t){ proj->s.origin.x, proj->s.origin.y };
    proj->s.angle   = (float)DEG2RAD(yaw);
    proj->s.model   = Wow_FrostboltModel();
    proj->s.scale   = 0.8f;
    proj->s.radius  = 0.5f;
    proj->s.player  = caster->s.player;
    /* Ranged spells use the selected target, not the melee combat target; leaving
     * this field set would make the generic frame loop chase after launch. */
    caster_local->enemy = NULL;
}

void Wow_HealingTouch(edict_t *caster) {
    wowEntityLocal_t *local;

    if (!caster) return;
    local = Wow_EntityLocal(caster);
    if (!local || local->dead) return;

    if (local->mana < WOW_HEALING_TOUCH_MANA_COST) return;
    local->mana -= WOW_HEALING_TOUCH_MANA_COST;
    local->health = MIN(local->health + WOW_HEALING_TOUCH_HEAL, 100);
    /* Play a cast animation if available. */
    static cstring_t const heal_anims[] = { "SpellCastOmni", "Cast", "Attack1H", NULL };
    Wow_SetEntityMoveFirstAnimation(caster, &wow_move_cast, heal_anims);
}

/* Find a target in range for the firebolt spell.  Prefers current selection,
   then the current melee enemy, then nearest enemy. */
edict_t *Wow_FindSpellTarget(edict_t *ent, float range) {
    if (ent && ent->client && ((wowClient_t *)ent->client)->selected_entity) {
        edict_t *t = Wow_EdictByNumber(((wowClient_t *)ent->client)->selected_entity);
        if (Wow_IsAttackTarget(ent, t)) {
            vec2_t delta = Vector2_sub(&t->s.origin2, &ent->s.origin2);
            if (sqrtf(delta.x * delta.x + delta.y * delta.y) <= range) {
                return t;
            }
        }
        return NULL;
    }
    {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);
        if (local && Wow_IsAttackTarget(ent, local->enemy)) {
            vec2_t delta = Vector2_sub(&local->enemy->s.origin2, &ent->s.origin2);
            if (sqrtf(delta.x * delta.x + delta.y * delta.y) <= range) {
                return local->enemy;
            }
        }
    }
    return Wow_FindNearestAttackTarget(ent, range);
}

static void Wow_UpdateCamera(edict_t *ent) {
    gameCamera_t cam;
    if (!ent || !ent->client) return;
    CL_GameDefaultCamera(&cam);
    ent->client->ps.vieworigin = (vec3_t){ ent->s.origin.x, ent->s.origin.y, ent->s.origin.z + WOW_CAMERA_EYE_HEIGHT };
    ent->client->ps.viewangles = Wow_EulerFromCamera(Wow_ViewPitch(wow_move.pitch), wow_move.yaw);
    ent->client->ps.distance = wow_move.distance;
    player_set_lens(&ent->client->ps, &cam);
}

static void Wow_UpdatePlayerHud(edict_t *ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    player_t *ps;

    if (!ent || !ent->client || !local) {
        return;
    }
    ps = &ent->client->ps;
    ps->stats[WOW_STAT_HEALTH] = (uint16_t)local->health;
    ps->stats[WOW_STAT_HEALTH_MAX] = 100;
    ps->stats[WOW_STAT_POWER] = (uint16_t)local->mana;
    ps->stats[WOW_STAT_POWER_MAX] = WOW_MANA_MAX;
    ps->stats[WOW_STAT_LEVEL] = 1;
    ps->stats[WOW_STAT_XP] = (uint16_t)MIN(local->experience, 0xFFFFu);
    ps->stats[WOW_STAT_XP_MAX] = 400;
    ps->stats[WOW_STAT_COPPER] = (uint16_t)MIN(local->copper, 0xFFFFu);
    /* Cast progress: remaining ms and total ms for client-side cast bar */
    ps->stats[WOW_STAT_CAST_PROGRESS] = (uint16_t)(local->cast_spell != SPELL_NONE ? local->cast_remaining : 0);
    ps->stats[WOW_STAT_CAST_MAX] = (uint16_t)(local->cast_spell != SPELL_NONE ? local->cast_duration : 0);
    ps->stats[WOW_STAT_SELECTED_ACTION] = (uint16_t)local->selected_action_slot;
    /* Tick down damage-flash overlay timers (displayed in g_ui.c) */
    wowClient_t *wc = (wowClient_t *)ent->client;
    if (wc->incoming_dmg_timer > FRAMETIME) wc->incoming_dmg_timer -= FRAMETIME;
    else wc->incoming_dmg_timer = 0;
    if (wc->outgoing_dmg_timer > FRAMETIME) wc->outgoing_dmg_timer -= FRAMETIME;
    else wc->outgoing_dmg_timer = 0;
    /* Tick down loot animation timer; reset to Stand when done. */
    if (local->loot_anim_timer > FRAMETIME) local->loot_anim_timer -= FRAMETIME;
    else if (local->loot_anim_timer) { local->loot_anim_timer = 0; Wow_SetStandMove(ent); }
}

static void Wow_MovePlayerFrame(edict_t *ent) {
    Wow_AdvanceEntityFrame(ent);
}

static edict_t *Wow_EdictByNumber(uint32_t number) {
    if (number >= (uint32_t)globals.num_edicts || number >= WOW_MAX_EDICTS) {
        return NULL;
    }
    if (!wow_edicts[number].inuse) {
        return NULL;
    }
    return &wow_edicts[number];
}

static edict_t *Wow_FindNearestAttackTarget(edict_t *ent, float range) {
    edict_t *best = NULL;
    float best_dist2 = range * range;

    if (!ent) {
        return NULL;
    }

    for (uint32_t i = globals.max_clients; i < (uint32_t)globals.num_edicts && i < WOW_MAX_EDICTS; i++) {
        edict_t *candidate = &wow_edicts[i];
        vec2_t delta;
        float dist2;

        if (!Wow_IsAttackTarget(ent, candidate)) {
            continue;
        }

        delta = Vector2_sub(&candidate->s.origin2, &ent->s.origin2);
        dist2 = delta.x * delta.x + delta.y * delta.y;
        if (dist2 < best_dist2) {
            best_dist2 = dist2;
            best = candidate;
        }
    }

    return best;
}

edict_t *Wow_Spawn(void) {
    edict_t *ent = NULL;
    uint32_t index;

    if (wow_spawns_this_frame >= WOW_MAX_SPAWNS_PER_FRAME)
        return NULL;
    wow_spawns_this_frame++;

    if (globals.num_edicts < globals.max_edicts) {
        index = globals.num_edicts++;
        ent = &wow_edicts[index];
    } else {
        for (index = MAX_CLIENTS; index < WOW_MAX_EDICTS; index++) {
            if (!wow_edicts[index].inuse) {
                ent = &wow_edicts[index];
                break;
            }
        }
    }
    if (!ent) {
        return NULL;
    }

    memset(ent, 0, sizeof(*ent));
    memset(&wow_entity_locals[index], 0, sizeof(wow_entity_locals[index]));
    ent->inuse = true;
    wow_entity_locals[index].spawn_serial = ++wow_spawn_serial;
    if (!wow_entity_locals[index].spawn_serial) wow_entity_locals[index].spawn_serial = ++wow_spawn_serial;
    ent->s.number = index;
    return ent;
}

/* Quake-style userinfo parser: find value for key in "\key\value\key\value" string.
   Returns pointer to a static buffer with the null-terminated value, or fallback
   if key not found.  Two rotating buffers so two calls don't stomp each other
   (same pattern as Q3 Info_ValueForKey in q_shared.c). */
/* Read selected character data from the single userinfo-style cvar set by the
   UI.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCvars(char *race, size_t race_sz, char *sex, size_t sex_sz, uint32_t *class_out, uint32_t *appearance_out) {
    cstring_t val;

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    *class_out = WOW_CLASS_WARRIOR;
    *appearance_out = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);

    val = gi.CvarString(WOW_CVAR_PLAYERINFO, "");
    if (val[0]) {
        cstring_t v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) *class_out = (uint32_t)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) *appearance_out = (uint32_t)strtoul(v, NULL, 10);
    }
}

/* Map selection happens before LoadMap, but remains authored by the server playercreateinfo table. */
static uint32_t Wow_SelectedPlayerCreateMap(void) {
    char race[64], sex[64];
    uint32_t class_id, appearance;

    if (!gi.CvarString(WOW_CVAR_PLAYERINFO, "")[0])
        return ~0u;
    Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
    return Wow_PlayerCreateMap(race, class_id);
}

/* Read the selected character's race/sex from the dedicated player configstring for
   server-authored UI (unit-frame portrait).  Fallback matches Wow_InitPlayer. */
void Wow_GetPlayerRaceSex(char *race, size_t race_sz, char *sex, size_t sex_sz) {
    cstring_t val = gi.GetConfigstring(CS_PLAYERSKINS);

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    if (val && val[0]) {
        cstring_t v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
    }
}

/* Read selected character data from the dedicated player configstring set by
   Wow_Init.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCS(char *race, size_t race_sz, char *sex, size_t sex_sz, uint32_t *class_out, uint32_t *appearance_out) {
    cstring_t val;

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    *class_out = WOW_CLASS_WARRIOR;
    *appearance_out = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);

    val = gi.GetConfigstring(CS_PLAYERSKINS);
    if (val && val[0]) {
        cstring_t v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) *class_out = (uint32_t)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) *appearance_out = (uint32_t)strtoul(v, NULL, 10);
    }
}

/* Server-authored UI resolves class-sensitive quest text from playerinfo. */
uint32_t Wow_GetPlayerClass(void) {
    char race[64], sex[64]; uint32_t class_id, appearance;
    Wow_ReadSelectedCharFromCS(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
    return class_id;
}

/* Character progress is keyed by the opaque ID supplied by character select.
 * No selected ID means transient development/preview sessions. */
static char wow_character_id[17], wow_character_path[1024];
static bool wow_character_started, wow_character_save_blocked, wow_character_same;
static uint32_t wow_character_copper, wow_character_xp;

static bool Wow_CharacterFlush(void) {
    if (!wow_character_started || !wow_character_path[0] || wow_character_save_blocked) return true;
    if (!Wow_CharacterSavePath(wow_character_path, &wow_clients[0], &wow_entity_locals[0])) {
        fprintf(stderr, "WoW: could not save character progress\n");
        return false;
    }
    return true;
}

static bool Wow_CharacterPrepare(void) {
    char next[17] = {0}, path[1024] = {0};
    cstring_t info = gi.CvarString(WOW_CVAR_PLAYERINFO, "");
    cstring_t id = Wow_InfoValueForKey(info, "charid", "");
    if (strlen(id) == 16) {
        bool valid = true;
        for (uint32_t i = 0; i < 16; i++)
            if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) valid = false;
        if (valid) memcpy(next, id, 16);
    }
    wow_character_same = wow_character_started && !strcmp(next, wow_character_id);
    if (!Wow_CharacterFlush() && !wow_character_same) return false;
    if (wow_character_same) {
        wow_character_copper = wow_entity_locals[0].copper;
        wow_character_xp = wow_entity_locals[0].experience;
        return true;
    }
    memset(&wow_clients[0], 0, sizeof(wow_clients[0]));
    memcpy(wow_character_id, next, sizeof(next));
    wow_character_save_blocked = false;
    if (next[0] && gi.SavePath) {
        char rel[64]; snprintf(rel, sizeof(rel), "wow-character-%s.sav", next);
        gi.SavePath(rel, path, sizeof(path));
    }
    snprintf(wow_character_path, sizeof(wow_character_path), "%s", path);
    wow_character_started = true;
    return true;
}

static void Wow_CharacterFinish(edict_t *ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    if (wow_character_same) {
        local->copper = wow_character_copper;
        local->experience = wow_character_xp;
    } else if (wow_character_path[0]) {
        int result = Wow_CharacterLoadPath(wow_character_path, &wow_clients[0], local);
        if (result < 0) {
            fprintf(stderr, "WoW: character save invalid or unreadable; preserving file\n");
            wow_character_save_blocked = true;
        }
    }
    Wow_UpdatePlayerHud(ent);
}

static void Wow_InitPlayer(edict_t *ent, vec2_t spawn_origin, int32_t spawn_location) {
    player_t *ps;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    float height = Wow_TerrainHeight(spawn_origin.x, spawn_origin.y);
    char race[64], sex[64];
    uint32_t class_id, appearance;
    char model_path[MAX_PATHLEN * 2];

    /* Read selected character from the dedicated player configstring (set by Wow_Init from cvars). */
    Wow_ReadSelectedCharFromCS(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);

    memset(ent, 0, sizeof(*ent));
    if (local) {
        memset(local, 0, sizeof(*local));
        local->cast_spell = SPELL_NONE;
        /* 255 is the wire/UI sentinel for no selected action; zero would highlight slot 0 at spawn. */
        local->selected_action_slot = 255;
        local->hostile = false;
        local->home = spawn_origin;
        local->yaw = wow_move.yaw;
        local->health = 100;
        local->mana = WOW_MANA_MAX;
        local->experience = 120;
        local->attack_damage_point = 250;
        local->attack_backswing = 450;
        local->weapon_entry = WOW_START_WEAPON_ENTRY;
    }
    ent->client = &wow_clients[0].client;
    ent->inuse = true;
    ent->s.number = 0;
    snprintf(model_path, sizeof(model_path), "Character\\%s\\%s\\%s%s.m2", race, sex, race, sex);
    ent->s.model = G_RegisterModel(model_path);
    ent->s.model2 = G_RegisterModel(WOW_PLAYER_WEAPON_MODEL);
    ent->s.appearance = appearance;
    ent->s.equipment = Wow_PackEquipment(WOW_PLAYER_EQUIPMENT_UPPER_BODY, WOW_PLAYER_EQUIPMENT_LOWER_BODY, WOW_PLAYER_EQUIPMENT_HANDS, WOW_PLAYER_EQUIPMENT_FEET);
    ent->s.origin = (vec3_t){ spawn_origin.x, spawn_origin.y, height };
    ent->s.origin2 = (vec2_t){ ent->s.origin.x, ent->s.origin.y };
    ent->s.angle = (float)DEG2RAD(wow_move.yaw);
    ent->s.scale = 1.0f;
    ent->s.radius = 1.0f;
    ent->s.flags = EF_GROUND_ANCHOR;
    local->idle = Wow_AIIdle;
    local->move = NULL;
    local->attack = Wow_AIAttack;
    local->pain = Wow_AIPain;
    Wow_SetStandMove(ent);

    ps = &ent->client->ps;
    memset(ps, 0, sizeof(*ps));
    ps->number = 0;
    ps->start_location = spawn_location;
    snprintf(wow_clients[0].name, sizeof(wow_clients[0].name), "%s", "Thrall");
    {
        wowActionDef_t const *actions = (class_id == WOW_CLASS_MAGE)
            ? wow_actions_mage : wow_actions_warrior;
        if (!wow_clients[0].inventory_initialized) {
            memcpy(wow_clients[0].inventory, wow_start_inventory, sizeof(wow_start_inventory));
            wow_clients[0].inventory_initialized = true;
        }
        wow_clients[0].bank_target = wow_clients[0].bank_entry = 0;
        wow_clients[0].merchant_target = wow_clients[0].merchant_entry = 0;
        wow_clients[0].merchant_buyback_view = false;
        if (!wow_character_same) FOR_LOOP(i, WOW_UI_ACTION_SLOTS) {
            wow_clients[0].actions[i] = actions[i].icon;
            wow_clients[0].action_spells[i] = actions[i].icon.icon[0] ? actions[i].spell : SPELL_NONE;
        }
        Wow_EntityLocal(ent)->copper = 1234; /* starting copper balance */
        fprintf(stderr, "WoW: action bar initialized for class %u\n", (unsigned)class_id);
    }
    Wow_UpdateCamera(ent);
    ps->client_ui_state = CLIENT_UI_LOADING;
    ps->name = wow_clients[0].name;
    Wow_UpdatePlayerHud(ent);
}

static void Wow_Init(void) {
    wow_character_started = wow_character_save_blocked = false;
    wow_character_id[0] = wow_character_path[0] = 0;
    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    memset(wow_clients, 0, sizeof(wow_clients));

    globals.edicts = wow_edicts;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = MAX_CLIENTS;
    globals.num_edicts = MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);
}

static void Wow_Shutdown(void) {
    Wow_CharacterFlush();
    Wow_ClearMapOverlayCache();
    WowTaxi_Free(&wow_taxi_graph);
    wow_taxi_attempted = false;
    G_FreeModels();
    globals.edicts = NULL;
    globals.num_edicts = 0;
}

static bool Wow_SpawnEntities(void);

static bool Wow_LoadMap(cstring_t mapFilename) {
    /* "preview" pseudo-map: no collision, no spawns, black background.
     * The renderer logs a missing WDT and draws nothing for the world.
     * Use as: make run-wow-preview  then type "quest <id>" in-game. */
    if (!strcmp(mapFilename, "preview")) {
        gi.ClearWorld();
        gi.configstring(CS_PLAYERSKINS,
            "\\race\\Human\\sex\\Male\\class\\2\\appearance\\0");
        Wow_InitPlayer(&wow_edicts[0], (vec2_t){0, 0}, -1);
        return true;
    }
    if (!CM_LoadMap(mapFilename, gi.LoadingFrame)) {
        return false;
    }
    if (!Wow_CharacterPrepare()) return false;
    gi.ApplyLobbySettings((mapInfo_t *)CM_GetMapInfo());
    gi.ClearWorld();
    return Wow_SpawnEntities();
}


static void Wow_ThinkUnit(edict_t *ent) {
    wowEntityLocal_t *el = Wow_EntityLocal(ent);
    if (el && el->slow_timer > 0)
        el->slow_timer = el->slow_timer > FRAMETIME ? el->slow_timer - FRAMETIME : 0;
    Wow_RunCreatureFrame(ent);
}
static void Wow_ThinkProjectile(edict_t *ent) { Wow_RunProjectile(ent); }
static void Wow_ThinkDynamicObject(edict_t *ent) { Wow_RunDynamicObjectFrame(ent); }

/* Build the WDT path for a numeric map ID by scanning Map.dbc field 1 (directory).
 * Returns true and fills out on success; false when the DBC is absent or the ID
 * is not present.  Callers must provide a buffer of at least MAX_PATHLEN bytes. */
static bool Wow_WdtPathForMapId(uint32_t map_id, string_t out, uint32_t out_size) {
    uint8_t *data; uint32_t size = 0; stbDbc_t h; bool found = false;
    data = gi.ReadFile("DBFilesClient\\Map.dbc", &size);
    if (!Stb_DbcValid(data, size, &h) || h.fields < 2 || h.record_size < sizeof(wowMapDbc_t))
        { SAFE_DELETE(data, gi.MemFree); return false; }
    uint8_t const *recs = Stb_DbcRecords(data), *strs = Stb_DbcStrings(data, &h);
    FOR_LOOP(i, h.records) {
        wowMapDbc_t const *m = (wowMapDbc_t const *)(recs + i * h.record_size);
        if (m->id != map_id) continue;
        cstring_t dir = Stb_DbcString(strs, h.string_size, m->directory_offset);
        if (dir && *dir) { snprintf(out, out_size, "World\\Maps\\%s\\%s.wdt", dir, dir); found = true; }
        break;
    }
    gi.MemFree(data);
    return found;
}

/* Load AreaTrigger.dbc records for the current map into wow_area_trigs[].
 * Called at end of Wow_SpawnEntities so triggers are ready for RunFrame. */
static void Wow_LoadAreaTriggers(void) {
    uint8_t *data; uint32_t size = 0, map_id; stbDbc_t h;
    wow_area_trig_count = 0;
    map_id = CM_WowGetMapId();
    data = gi.ReadFile("DBFilesClient\\AreaTrigger.dbc", &size);
    /* AreaTrigger.dbc: 10 uint32/float fields, no string block — record_size == 40. */
    if (!Stb_DbcValid(data, size, &h) || h.fields != 10 || h.record_size != sizeof(wowAreatrig_t))
        { SAFE_DELETE(data, gi.MemFree); return; }
    uint8_t const *base = Stb_DbcRecords(data);
    FOR_LOOP(i, h.records) {
        wowAreatrig_t const *t = (wowAreatrig_t const *)(base + i * h.record_size);
        if (t->map_id != map_id) continue;
        if (!Wow_AreaTrigTeleportById(t->id)) continue; /* no destination, skip */
        if (wow_area_trig_count >= WOW_MAX_AREA_TRIGS) break;
        wow_area_trigs[wow_area_trig_count++] = *t;
    }
    fprintf(stderr, "WoW: loaded %u area triggers for map %u\n",
            (unsigned)wow_area_trig_count, (unsigned)map_id);
    gi.MemFree(data);
}

/* Per-frame overlap test between the player and all cached area triggers.
 * Sphere: dist < radius.  Box: player in local-frame AABB after orientation rotation.
 * On a hit: saves destination to wow_pending_teleport and calls MenuAction("map", ...).
 * Guards on pending to avoid re-entering before the map change completes. */
static void Wow_CheckAreaTriggers(edict_t *ent) {
    char wdt[MAX_PATHLEN];
    if (wow_pending_teleport.pending || !wow_area_trig_count) return;
    FOR_LOOP(i, wow_area_trig_count) {
        wowAreatrig_t const *t = &wow_area_trigs[i];
        wowAreatrigTeleport_t const *dest = Wow_AreaTrigTeleportById(t->id);
        float dx = ent->s.origin.x - t->x, dy = ent->s.origin.y - t->y, dz = ent->s.origin.z - t->z;
        if (!dest) continue;
        if (t->radius > 0.0f) {
            if (dx*dx + dy*dy + dz*dz > t->radius * t->radius) continue;
        } else {
            float co = cosf(t->box_orientation), so = sinf(t->box_orientation);
            if (fabsf( dx*co + dy*so) > t->box_x) continue;
            if (fabsf(-dx*so + dy*co) > t->box_y) continue;
            if (fabsf(dz)             > t->box_z) continue;
        }
        wow_pending_teleport = (wowPendingTeleport_t){ true,
            dest->target_x, dest->target_y, dest->target_z, dest->target_orientation };
        if (Wow_WdtPathForMapId(dest->target_map, wdt, sizeof(wdt))) {
            fprintf(stderr, "WoW: area trigger %u → map %u (%s)\n",
                    (unsigned)t->id, (unsigned)dest->target_map, wdt);
            gi.MenuAction("map", wdt);
        } else {
            fprintf(stderr, "WoW: area trigger %u: no WDT for map %u\n",
                    (unsigned)t->id, (unsigned)dest->target_map);
            wow_pending_teleport.pending = false;
        }
        return;
    }
}

/* First spawn point index on map_id regardless of race — used when the
 * selected character's race has no playercreateinfo entry on the target map
 * (e.g. loading map=0 with an Orc char before a +warp repositions the player). */
static uint32_t Wow_AnySpawnIndexForMap(uint32_t map_id) {
    FOR_LOOP(i, Wow_SpawnCount()) {
        wowSpawnPoint_t const *sp = Wow_SpawnByIndex(i);
        if (sp && sp->map == map_id) return i;
    }
    return ~0u;
}

static bool Wow_SpawnEntities(void) {
    for (uint32_t i = 0; i < MAX_CLIENTS; ++i) {
        wow_clients[i].taxi_flying = false;
        wow_clients[i].taxi_open = false;
    }
    char race[64], sex[64];
    uint32_t class_id, appearance, spawn_index;
    int32_t spawn_location = -1;
    vec2_t spawn_origin = { 0.0f, 0.0f };
    char buf[MAX_PATHLEN];

    /* Read race before spawn selection so the player starts in their race's
       home zone (e.g. Orcs in Valley of Trials, not Northshire). */
    Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);

    if (wow_pending_teleport.pending) {
        /* Cross-map teleport: destination was saved by Wow_CheckAreaTriggers or warp command. */
        spawn_origin = (vec2_t){ wow_pending_teleport.x, wow_pending_teleport.y };
        fprintf(stderr, "WoW: pending teleport → map=%u (%.1f %.1f)\n",
                (unsigned)CM_WowGetMapId(), spawn_origin.x, spawn_origin.y);
    } else {
        spawn_index = Wow_SelectSpawnPoint(race, class_id);
        if (spawn_index == ~0u) {
            uint32_t map_id = CM_WowGetMapId();
            if (Wow_HasSpawnForMap(map_id)) {
                /* Race/class has no spawn on this map but other races do.
                 * Fall back to any available spawn — a deferred +warp will
                 * reposition the player; don't reject the whole map load. */
                uint32_t fb = Wow_AnySpawnIndexForMap(map_id);
                fprintf(stderr, "WoW: race=%s class=%u has no spawn on map=%u; using fallback\n",
                        race, (unsigned)class_id, (unsigned)map_id);
                if (fb == ~0u) return false;
                vec3_t const *fsp = Wow_GetSpawnPos(fb);
                if (fsp) { spawn_origin = (vec2_t){ fsp->x, fsp->y }; spawn_location = (int32_t)fb; }
            } else {
            /* No playercreateinfo for ANY race on this map — it's a dungeon/instance.
             * Fall back to the areatrigger_teleport destination for this map. */
            wowAreatrigTeleport_t const *at = Wow_AreaTrigSpawnForMap(map_id);
            if (at) {
                wow_pending_teleport = (wowPendingTeleport_t){ true,
                    at->target_x, at->target_y, at->target_z, at->target_orientation };
                spawn_origin = (vec2_t){ at->target_x, at->target_y };
                fprintf(stderr, "WoW: dungeon map=%u; using areatrigger spawn '%s'\n",
                        (unsigned)map_id, at->name);
            } else {
                fprintf(stderr, "WoW: no spawn for map=%u (no playercreateinfo, no areatrigger)\n",
                        (unsigned)map_id);
                return false;
            }
            } /* end else-dungeon */
        } else {
            vec3_t const *sp = Wow_GetSpawnPos(spawn_index);
            if (sp) {
                spawn_origin = (vec2_t){ sp->x, sp->y };
                spawn_location = (int32_t)spawn_index;
                fprintf(stderr, "WoW: spawn race=%s at (%.1f %.1f)\n", race, sp->x, sp->y);
            }
        }
    }
    {
        char preview[MAX_PATHLEN];
        snprintf(preview, sizeof(preview), "%s", wow_loading_texture);
        for (char *p = preview; *p; p++) if (*p == '\\') *p = '/';
        strlcpy(buf, "\\title\\", sizeof(buf));
        strlcat(buf, wow_loading_title, sizeof(buf));
        strlcat(buf, "\\preview\\", sizeof(buf));
        strlcat(buf, preview, sizeof(buf));
    }
    gi.configstring(WOW_CS_MAPINFO, buf);
    /* Re-populate the playerinfo configstring from cvars after SV_Map's
       memset cleared all configstrings (same pattern as Q3: game module
       re-sets configstrings after the server wipes them on map load). */
    snprintf(buf, sizeof(buf), "\\race\\%s\\sex\\%s\\class\\%u\\appearance\\%u", race, sex, (unsigned)class_id, (unsigned)appearance);
    gi.configstring(CS_PLAYERSKINS, buf);
    wow_move.flags = 0;
    wow_move.yaw = 0.0f;
    wow_move.pitch = 328.0f;
    wow_move.distance = 8.5f;
    wow_spawns_this_frame = 0;
    Wow_InitPlayer(&wow_edicts[0], spawn_origin, spawn_location);
    Wow_CharacterFinish(&wow_edicts[0]);
    /* Apply authoritative z and orientation from pending teleport AFTER InitPlayer so
     * the SQL z overrides the terrain-height fallback used for dungeon interiors. */
    if (wow_pending_teleport.pending) {
        edict_t *p = &wow_edicts[0];
        p->s.origin.z = wow_pending_teleport.z;
        p->s.angle    = wow_pending_teleport.orientation;
        wow_pending_teleport.pending = false;
        Wow_UpdateCamera(p);
    }
    globals.num_edicts = MAX_CLIENTS;
    Wow_SpawnAmbientCreatures(&spawn_origin);
    /* Initial world population is intentionally split into budgets so the
     * imported quest anchors do not starve ambient creatures or vice versa. */
    wow_spawns_this_frame = 0;
    Wow_SpawnQuestLocations(&spawn_origin);
    wow_spawns_this_frame = 0;
    Wow_SpawnVendors(&spawn_origin);
    wow_service_refresh_ms = 0;
    Wow_SpawnGameObjects(&spawn_origin);
    /* Register spell impact models via DBC SpellVisual chain.
     * Falls back to hardcoded paths if DBC is unavailable. */
    Wow_LoadSpellDbc();
    wow_firebolt_impact_model = Wow_FireboltImpactModel();
    wow_frostbolt_impact_model = Wow_FrostboltImpactModel();
    fprintf(stderr, "WoW: impact models — fire=%d frost=%d\n", wow_firebolt_impact_model, wow_frostbolt_impact_model);
    fprintf(stderr, "WoW doodads: static ADT doodads are renderer-owned and not synced as entities\n");
    Wow_LoadAreaTriggers();
    return true;
}

static bool Wow_TryPlayerStep(edict_t *ent, float dx, float dy) {
    vec3_t const from = ent->s.origin;
    vec3_t to = { from.x + dx, from.y + dy, from.z };
    /* WMO floors can sit above the outdoor ground inside buildings. */
    to.z = Wow_FloorHeight(to.x, to.y, from.z);
    if ((dx != 0.0f || dy != 0.0f) &&
        (CM_WowMoveBlocked(&from, &to) ||
         !Wow_TerrainMoveWalkable(&from, &to, Wow_TerrainHeight(to.x, to.y))))
        return false;
    ent->s.origin = to;
    return true;
}


static bool Wow_TaxiLoad(void) {
    uint32_t ns = 0, ps = 0, qs = 0;
    void *n, *p, *q;
    if (wow_taxi_graph.node_count) return true;
    if (wow_taxi_attempted) return false;
    wow_taxi_attempted = true;
    n = g_dbc_io.read("DBFilesClient\\TaxiNodes.dbc", &ns);
    p = g_dbc_io.read("DBFilesClient\\TaxiPath.dbc", &ps);
    q = g_dbc_io.read("DBFilesClient\\TaxiPathNode.dbc", &qs);
    if (n && p && q) WowTaxi_Load(&wow_taxi_graph, n, ns, p, ps, q, qs);
    if (n) g_dbc_io.free(n);
    if (p) g_dbc_io.free(p);
    if (q) g_dbc_io.free(q);
    return wow_taxi_graph.node_count != 0;
}
static uint32_t Wow_TaxiNearest(edict_t *ent, float range) {
    size_t i;
    float best = range * range;
    uint32_t id = 0;
    for (i = 0; i < wow_taxi_graph.node_count; i++) {
        wowTaxiNode_t const *n = &wow_taxi_graph.nodes[i];
        float x = n->x - ent->s.origin.x;
        float y = n->y - ent->s.origin.y;
        float z = n->z - ent->s.origin.z;
        float d = x*x + y*y + z*z;
        if (n->map == CM_WowGetMapId() && n->id < 65536 && d < best) {
            best = d;
            id = n->id;
        }
    }
    return id;
}
static bool Wow_TaxiNpcValid(edict_t *player, edict_t *npc) {
    wowEntityLocal_t *p, *n;
    wowCreature_t const *creature;
    float dx, dy;
    if (!player || !player->inuse || !player->client || !npc || !npc->inuse || player == npc)
        return false;
    p = Wow_EntityLocal(player);
    n = Wow_EntityLocal(npc);
    if (!p || p->dead || !n || n->dead || n->hostile || !n->creature_entry) return false;
    creature = Wow_CreatureByEntry(n->creature_entry);
    if (!creature || !(creature->npc_flags & 0x2000)) return false;
    dx = player->s.origin.x - npc->s.origin.x;
    dy = player->s.origin.y - npc->s.origin.y;
    return dx * dx + dy * dy <= 100.0f;
}

static bool Wow_TaxiOpenNpc(edict_t *player, edict_t *npc) {
    wowClient_t *client;
    uint32_t source;
    if (!Wow_TaxiNpcValid(player, npc) || !Wow_TaxiLoad()) return false;
    source = Wow_TaxiNearest(npc, 100.0f);
    client = (wowClient_t *)player->client;
    client->taxi_source = source;
    client->taxi_npc = npc->s.number;
    client->taxi_npc_serial = Wow_EntityLocal(npc)->spawn_serial;
    if (source) client->taxi_known[source / 8] |= (uint8_t)(1u << (source % 8));
    client->taxi_page = 0;
    client->taxi_open = true;
    UI_WriteTaxi(player);
    return true;
}

static void Wow_TaxiCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    wowClient_t *c = (wowClient_t *)ent->client;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    uint32_t source;
    wowTaxiRoute_t route;
    if (!c || !local || local->dead || c->taxi_flying || !Wow_TaxiLoad()) return;
    if (argc > 1 && !strcasecmp(argv[1], "close")) {
        c->taxi_open = false;
        c->taxi_npc = 0;
        UI_WriteTaxi(ent);
        return;
    }
    if (c->taxi_open && c->taxi_npc) {
        edict_t *npc = Wow_EdictByNumber(c->taxi_npc);
        wowEntityLocal_t *npc_local = npc ? Wow_EntityLocal(npc) : NULL;
        source = npc_local && npc_local->spawn_serial == c->taxi_npc_serial &&
                 Wow_TaxiNpcValid(ent, npc) ? Wow_TaxiNearest(npc, 100.0f) : 0;
    } else {
        source = Wow_TaxiNearest(ent, 10.0f);
    }
    if (!source) {
        c->taxi_source = 0;
        c->taxi_npc = 0;
        c->taxi_open = true;
        UI_WriteTaxi(ent);
        return;
    }
    c->taxi_known[source / 8] |= (uint8_t)(1u << (source % 8));
    if (argc > 1 && c->taxi_open && c->taxi_source == source &&
        (!strcasecmp(argv[1], "next") || !strcasecmp(argv[1], "previous"))) {
        if (!strcasecmp(argv[1], "next") && c->taxi_page < 65535) c->taxi_page++;
        if (!strcasecmp(argv[1], "previous") && c->taxi_page) c->taxi_page--;
        UI_WriteTaxi(ent);
        return;
    }
    if (argc > 1) {
        char *end;
        unsigned long id = strtoul(argv[1], &end, 10);
        if(*end || id>=65536 || !c->taxi_open || c->taxi_source!=source ||
           !WowTaxi_Route(&wow_taxi_graph,source,(uint32_t)id,c->taxi_known,sizeof(c->taxi_known),&route) ||
           route.price>local->copper) return;
        local->copper -= (uint32_t)route.price;
        c->taxi_flight = route;
        c->taxi_leg = 0;
        c->taxi_point = 0;
        c->taxi_wait_ms = 0;
        c->taxi_flying = true;
        Wow_CancelSpellCast(ent);
        local->enemy = NULL;
        c->taxi_open = false;
        UI_WriteTaxi(ent);
    } else {
        c->taxi_source = source;
        c->taxi_npc = 0;
        c->taxi_page = 0;
        c->taxi_open = true;
        UI_WriteTaxi(ent);
    }
}
static bool Wow_TaxiStep(edict_t *ent) {
    float budget = 32.0f * FRAMETIME / 1000.0f;
    wowClient_t *c = (wowClient_t *)ent->client;
    if (!c->taxi_flying) return false;
    if (c->taxi_wait_ms) {
        c->taxi_wait_ms -= c->taxi_wait_ms > FRAMETIME ? FRAMETIME : c->taxi_wait_ms;
        Wow_UpdateCamera(ent);
        Wow_UpdatePlayerHud(ent);
        return true;
    }
    while (budget > 0 && c->taxi_leg < c->taxi_flight.count) {
        wowTaxiPoint_t const *point = NULL;
        for (size_t i = 0; i < wow_taxi_graph.path_count; i++) {
            wowTaxiPath_t const *path = &wow_taxi_graph.paths[i];
            if (path->id == c->taxi_flight.paths[c->taxi_leg] &&
                c->taxi_point < path->point_count) {
                point = &wow_taxi_graph.points[path->first_point + c->taxi_point];
                break;
            }
        }
        if (!point) {
            c->taxi_leg++;
            c->taxi_point = 0;
            continue;
        }
        float x = point->x - ent->s.origin.x;
        float y = point->y - ent->s.origin.y;
        float z = point->z - ent->s.origin.z;
        float d = sqrtf(x*x + y*y + z*z);
        float amount = d < budget ? d : budget;
        if (d > 0.0001f) {
            ent->s.origin.x += x * amount / d;
            ent->s.origin.y += y * amount / d;
            ent->s.origin.z += z * amount / d;
            ent->s.angle = atan2f(y, x);
        }
        budget -= amount;
        if (d <= amount + 0.0001f) {
            c->taxi_point++;
            if (point->delay) {
                c->taxi_wait_ms = point->delay > UINT32_MAX / 1000u
                    ? UINT32_MAX : point->delay * 1000u;
                break;
            }
        } else break;
    }
    ent->s.origin2 = (vec2_t){ent->s.origin.x, ent->s.origin.y};
    if (c->taxi_leg >= c->taxi_flight.count) c->taxi_flying = false;
    Wow_UpdateCamera(ent);
    Wow_UpdatePlayerHud(ent);
    return true;
}

static void Wow_RunFrame(void) {
    edict_t *ent = &wow_edicts[0];
    vec2_t forward;
    vec2_t right;
    vec2_t dir = { 0.0f, 0.0f };
    float len;
    bool moving;
    bool locked;

    wow_spawns_this_frame = 0;

    /* Flight state is per-client even though ordinary movement currently drives client 0. */
    for (uint32_t i = 1; i < MAX_CLIENTS; ++i) {
        edict_t *other = &wow_edicts[i];
        if (other->inuse && other->client) Wow_TaxiStep(other);
    }

    if (!ent->inuse || !ent->client) {
        return;
    }

    wow_service_refresh_ms += FRAMETIME;
    if (wow_service_refresh_ms >= 5000) {
        wow_service_refresh_ms = 0;
        Wow_SpawnVendors(&ent->s.origin2);
    }

    if (Wow_TaxiStep(ent)) goto process_entities;

    Wow_AngleVectors(wow_move.yaw, &forward, &right);

    if (wow_move.flags & WOW_MOVE_FORWARD) {
        dir.x += forward.x;
        dir.y += forward.y;
    }
    if (wow_move.flags & WOW_MOVE_BACK) {
        dir.x -= forward.x;
        dir.y -= forward.y;
    }
    if (wow_move.flags & WOW_MOVE_LEFT) {
        dir.x -= right.x;
        dir.y -= right.y;
    }
    if (wow_move.flags & WOW_MOVE_RIGHT) {
        dir.x += right.x;
        dir.y += right.y;
    }

    len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    moving = len > 0.001f;
    float step = WOW_WALK_SPEED * FRAMETIME / 1000.0f;
    float input_step = moving ? step / len : 0.0f;
    Wow_TryPlayerStep(ent, dir.x * input_step, dir.y * input_step);
    Wow_CheckAreaTriggers(ent); /* check dungeon/zone portals after position is settled */
    /* Run spell cast state machine before entity lock check.
     * Cast animation plays via Wow_AdvanceEntityFrame; cooldowns tick down. */
    {
        wowEntityLocal_t *cl = Wow_EntityLocal(ent);
        if (cl) {
            if (cl->recently_bandaged_ms)
                cl->recently_bandaged_ms -= MIN(cl->recently_bandaged_ms, FRAMETIME);
            if (cl->bandage_ticks) {
                if (moving || cl->dead || !cl->health) cl->bandage_ticks = 0;
                else if (cl->bandage_tick_ms <= FRAMETIME) {
                    cl->health = MIN(100u, cl->health + cl->bandage_heal);
                    --cl->bandage_ticks;
                    cl->bandage_tick_ms = 1000;
                    Wow_UpdatePlayerHud(ent);
                } else cl->bandage_tick_ms -= FRAMETIME;
            }
            if (cl->gcd_time > 0)
                cl->gcd_time -= cl->gcd_time > FRAMETIME ? FRAMETIME : cl->gcd_time;
            FOR_LOOP(i, WOW_SPELL_COUNT)
                cl->spell_cooldown[i] -= cl->spell_cooldown[i] > FRAMETIME ? FRAMETIME : cl->spell_cooldown[i];
        }
    }
    bool casting = Wow_RunSpellCast(ent);
    if (casting) {
        Wow_AdvanceEntityFrame(ent);
        Wow_UpdateCamera(ent);
        Wow_UpdatePlayerHud(ent);  /* expose cast progress to client */
        /* Skip the rest: no movement/chase/attack during cast */
        goto process_entities;
    }
    locked = Wow_AIAdvanceLockedFrame(ent);
    /* Auto-chase: move toward enemy when in combat, not pressing WASD, and
     * not locked in an animation (attack/cast/pain).  This comes after
     * Wow_AIAdvanceLockedFrame so a spell-cast timer prevents chase from
     * overriding the cast animation. */
    bool pursuing = false;
    if (!locked && !moving && Wow_EntityAffectingCombat(ent)) {
        edict_t *enemy = Wow_EntityLocal(ent)->enemy;
        vec2_t delta = Vector2_sub(&enemy->s.origin2, &ent->s.origin2);
        float dist = Vector2_len(&delta);
        if (dist > WOW_MELEE_RANGE) {
            float approach = MIN(step, dist - WOW_MELEE_RANGE) / dist;
            pursuing = Wow_TryPlayerStep(ent, delta.x * approach, delta.y * approach);
            if (pursuing) {
                moving = true;
                dir = delta;
                Wow_CheckAreaTriggers(ent);
            }
        }
    }
    if (!locked && Wow_EntityAffectingCombat(ent)) {
        Wow_EntityLocal(ent)->attack(ent);
        /* If the attack started, treat as locked so the Run animation below
         * doesn't overwrite the swing. */
        {
            wowEntityLocal_t *l = Wow_EntityLocal(ent);
            if (l && (l->attack_damage_time > 0 || l->attack_backswing_time > 0))
                locked = true;
        }
    }
    /* Match accepted pursuit movement while manual backpedal keeps facing forward. */
    float facing = (float)DEG2RAD(wow_move.yaw);
    if (pursuing || (len > 0.001f && !((wow_move.flags & WOW_MOVE_BACK) && !(wow_move.flags & WOW_MOVE_FORWARD))))
        facing = atan2f(dir.y, dir.x);
    if (locked) {
        Wow_UpdateCamera(ent);
    } else if (moving
        ? Wow_SetDirectionalMove(ent, wow_move.flags)
        : (Wow_EntityAffectingCombat(ent)
            ? Wow_SetCombatReadyAnimation(ent)
            : Wow_SetStandMove(ent))) {
        ent->s.angle = facing;
        Wow_MovePlayerFrame(ent);
        Wow_UpdateCamera(ent);
    } else {
        ent->s.angle = facing;
        Wow_UpdateCamera(ent);
    }
    /* Regen mana every frame: WOW_MANA_REGEN_PER_SEC / (1000/FRAMETIME) per tick. */
    {
        wowEntityLocal_t *pl = Wow_EntityLocal(ent);
        if (pl && pl->mana < WOW_MANA_MAX) {
            /* Use integer accumulation scaled by FRAMETIME to avoid per-frame float drift. */
            static uint32_t mana_accum = 0;
            mana_accum += (uint32_t)(WOW_MANA_REGEN_PER_SEC * FRAMETIME);
            if (mana_accum >= 1000) {
                uint32_t ticks = mana_accum / 1000;
                mana_accum %= 1000;
                pl->mana = MIN(pl->mana + ticks, WOW_MANA_MAX);
            }
        }
    }
    Wow_UpdatePlayerHud(ent);

process_entities:
    {
        wowClient_t *wc = (wowClient_t *)ent->client;
        wc->exploration_sample_ms += FRAMETIME;
        if (wc->exploration_sample_ms >= 500) {
            wc->exploration_sample_ms = 0;
            Wow_ExploreArea(&wc->exploration, CM_WowAreaAtPoint(ent->s.origin.x, ent->s.origin.y));
        }
        if (wc->worldmap_open && !wc->worldmap_choosing && !wc->worldmap_landmarks_list) {
            wc->worldmap_refresh += FRAMETIME;
            if (wc->worldmap_refresh >= 1000) {
                wc->worldmap_refresh = 0;
                UI_ResolveWorldMap(ent);
                UI_WriteWorldMap(ent);
            }
        }
    }
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++) {
        edict_t *e = &wow_edicts[i];
        wowEntityLocal_t *local = Wow_EntityLocal(e);
        if (e->inuse && local && local->think)
            local->think(e);
    }
    if (ent->client->ps.client_ui_state == CLIENT_UI_GAME)
        UI_WriteWowCombatFeedback(ent, false);
}

static cstring_t Wow_GetThemeValue(cstring_t filename) {
    return filename ? filename : "";
}

static bool Wow_PlayerIsMoving(void) { return wow_move.flags & BZ_WOW_MOVE_MASK; }

static void Wow_SelectEntity(edict_t *ent, edict_t *target) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    uint32_t old = wc->selected_entity;
    edict_t *old_target = old ? Wow_EdictByNumber(old) : NULL;

    if (old_target && old_target != target)
        old_target->selected &= ~(1 << ent->client->ps.number);
    if (target && target != ent && target->inuse) {
        target->selected |= (1 << ent->client->ps.number);
        wc->selected_entity = target->s.number;
    } else {
        wc->selected_entity = 0;
    }
    /* Share authoritative target changes with client groups, including cycle/interact and rejected selections. */
    gi.Write(PF_BYTE, &(int32_t){svc_set_selection});
    gi.Write(PF_BYTE, &(int32_t){wc->selected_entity ? 1 : 0});
    if (wc->selected_entity) gi.Write(PF_LONG, &(int32_t){wc->selected_entity});
    gi.unicast(ent);
}

void Wow_QuestAwardKillCredit(edict_t *attacker, uint32_t display_id) {
    wowClient_t *wc;
    bool changed = false;

    if (!attacker || !attacker->client) return;
    wc = (wowClient_t *)attacker->client;
    FOR_LOOP(i, wc->quest_count) {
        svQuestEntry_t *qs = &wc->quest_log[i];
        wowQuestDetail_t const *detail;
        bool all_done;

        if (qs->status != SV_QUEST_ACTIVE) continue;
        detail = Wow_QuestDetail(qs->quest_id);
        if (!detail || !detail->kill_objective_count) continue;
        all_done = true;
        FOR_LOOP(j, detail->kill_objective_count) {
            if (detail->kill_objectives[j].display_id != display_id) {
                if (wc->kill_progress[i][j] < detail->kill_objectives[j].required_count)
                    all_done = false;
                continue;
            }
            if (wc->kill_progress[i][j] < detail->kill_objectives[j].required_count) {
                wc->kill_progress[i][j]++;
                changed = true;
            }
            if (wc->kill_progress[i][j] < detail->kill_objectives[j].required_count)
                all_done = false;
        }
        if (all_done)
            qs->status = SV_QUEST_COMPLETE;
    }
    if (changed) UI_WriteWowHud(attacker);
}

bool Wow_QuestWasRewarded(wowClient_t const *client, uint32_t quest_id) {
    return quest_id < sizeof(client->rewarded_quests) * 8 &&
        (client->rewarded_quests[quest_id / 8] & (1u << (quest_id % 8)));
}

static void Wow_RemoveQuest(wowClient_t *client, uint32_t slot) {
    uint32_t remaining = client->quest_count - slot - 1;
    Wow_QuestTrackerRemove(client->quest_tracked, client->quest_count, slot);
    memmove(&client->quest_log[slot], &client->quest_log[slot + 1],
            remaining * sizeof(client->quest_log[0]));
    memmove(&client->kill_progress[slot], &client->kill_progress[slot + 1],
            remaining * sizeof(client->kill_progress[0]));
    client->quest_count--;
    memset(&client->quest_log[client->quest_count], 0, sizeof(client->quest_log[0]));
    memset(&client->kill_progress[client->quest_count], 0, sizeof(client->kill_progress[0]));
}

/* Quest chains unlock after the predecessor's reward has been claimed. */
static bool Wow_QuestPrereqMet(wowClient_t *client, uint32_t quest_id) {
    return !quest_id || Wow_QuestWasRewarded(client, quest_id);
}

static bool Wow_AddQuest(wowClient_t *client, uint32_t quest_id) {
    wowQuestDetail_t const *detail = Wow_QuestDetail(quest_id);
    if (!detail || quest_id >= sizeof(client->rewarded_quests) * 8 ||
        Wow_QuestWasRewarded(client, quest_id)) return false;
    if (!Wow_QuestPrereqMet(client, detail->prev_quest)) return false;
    if (!SV_QuestAdd(client->quest_log, &client->quest_count, SV_MAX_QUEST_LOG, quest_id)) return false;
    client->quest_tracked[client->quest_count - 1] = true;
    return true;
}

/* Resolve one physical quest NPC's repeated queststarter rows to the first
 * quest currently available to this player. The old one-edict-per-row path
 * made overlapping duplicates select an arbitrary later quest. */
static uint32_t Wow_QuestForGiver(wowClient_t *client, wowEntityLocal_t const *local) {
    uint32_t group = Wow_QuestGiverGroup(local->quest_id, &local->home);
    uint32_t available = 0, active = 0;
    if (group == WOW_QUEST_GIVER_GROUP_NONE)
        return Wow_QuestWasRewarded(client, local->quest_id) ? 0 : local->quest_id;
    FOR_LOOP(i, Wow_QuestGiverGroupCount(group)) {
        wowQuestGiver_t const *cur = Wow_QuestGiverInGroup(group, i);
        wowQuestDetail_t const *detail;
        if (Wow_QuestWasRewarded(client, cur->quest_id)) continue;
        svQuestEntry_t *state = SV_QuestFind(client->quest_log, client->quest_count, cur->quest_id);
        if (state) {
            if (state->status == SV_QUEST_COMPLETE) return cur->quest_id;
            if (state->status == SV_QUEST_ACTIVE && !active) active = cur->quest_id;
            continue;
        }
        detail = Wow_QuestDetail(cur->quest_id);
        if (!available && detail && Wow_QuestPrereqMet(client, detail->prev_quest))
            available = cur->quest_id;
    }
    return available ? available : active;
}

/* Only a live nearby selected giver can authorize a quest transaction. */
static bool Wow_QuestGiverValid(edict_t *player, uint32_t quest_id) {
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *player_local = Wow_EntityLocal(player);
    edict_t *npc = client->selected_entity ? Wow_EdictByNumber(client->selected_entity) : NULL;
    wowEntityLocal_t *local = npc ? Wow_EntityLocal(npc) : NULL;
    if (!player->inuse || !player_local || player_local->dead || !npc || !npc->inuse ||
        !local || local->dead || !local->quest_id || !(npc->s.flags & EF_HAS_QUEST)) return false;
    vec2_t delta = Vector2_sub(&npc->s.origin2, &player->s.origin2);
    if (delta.x * delta.x + delta.y * delta.y > 100.0f) return false;
    uint32_t group = Wow_QuestGiverGroup(local->quest_id, &local->home);
    if (group == WOW_QUEST_GIVER_GROUP_NONE) return local->quest_id == quest_id;
    FOR_LOOP(i, Wow_QuestGiverGroupCount(group))
        if (Wow_QuestGiverInGroup(group, i)->quest_id == quest_id) return true;
    return false;
}

static void Wow_CompleteQuest(edict_t *player, uint32_t quest_id) {
    if (!player || !player->client) return;
    wowClient_t *client = (wowClient_t *)player->client;
    svQuestEntry_t *state = SV_QuestFind(client->quest_log, client->quest_count, quest_id);
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    wowQuestDetail_t const *detail;
    if (!state || !local || !player->inuse ||
        quest_id >= sizeof(client->rewarded_quests) * 8) return;
    detail = Wow_QuestDetail(quest_id);
    if (!detail) return;
    if (state->status != SV_QUEST_COMPLETE) return;
    uint32_t slot = (uint32_t)(state - client->quest_log);
    client->rewarded_quests[quest_id / 8] |= 1u << (quest_id % 8);
    Wow_RemoveQuest(client, slot);
    local->experience += MIN(detail->reward_xp, UINT32_MAX - local->experience);
    local->copper += MIN(detail->reward_gold, UINT32_MAX - local->copper);
    Wow_UpdatePlayerHud(player);
    if (client->message_count < WOW_UI_MAX_MESSAGES) {
        wowUiMessage_t *message = &client->messages[client->message_count++];
        memset(message, 0, sizeof(*message));
        message->message_id = quest_id;
        message->kind = WOW_UI_MESSAGE_QUEST_REWARD;
        message->flags = WOW_UI_MESSAGE_UNREAD;
        message->quest_id = quest_id;
        snprintf(message->title, sizeof(message->title), "Quest complete");
        snprintf(message->body, sizeof(message->body), "%s\n\n%s", detail->title, detail->reward_text);
    }
}

static bool Wow_CheatsEnabled(void) {
    return atoi(gi.CvarString("sv_cheats", "0")) != 0;
}

/* Cheat replies belong to the issuing client; stderr alone only reaches the server operator. */
static void Wow_CheatPrintf(edict_t *ent, cstring_t fmt, ...) {
    char text[1024];
    int32_t opcode = svc_console_print;
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", text);
    gi.Write(PF_BYTE, &opcode);
    gi.Write(PF_STRING, text);
    gi.unicast(ent);
}

static void Wow_CheatHelp(edict_t *ent) {
    Wow_CheatPrintf(ent, "WoW: cheats: give all|health [amount]|mana [amount]|gold [amount]|xp [amount]; god; kill");
}

static void Wow_GiveCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    uint32_t amount;

    if (!Wow_CheatsEnabled()) {
        Wow_CheatPrintf(ent, "WoW: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!local || argc < 2) {
        Wow_CheatHelp(ent);
        return;
    }
    amount = argc >= 3 ? (uint32_t)strtoul(argv[2], NULL, 10) : 0;
    if (!strcasecmp(argv[1], "all")) {
        local->health = 100;
        local->mana = WOW_MANA_MAX;
        local->experience = ent->client->ps.stats[WOW_STAT_XP_MAX];
        local->copper += MIN(100000u, UINT32_MAX - local->copper);
    } else if (!strcasecmp(argv[1], "health")) {
        local->health = MIN(100, amount ? amount : 100);
    } else if (!strcasecmp(argv[1], "mana")) {
        local->mana = MIN(WOW_MANA_MAX, amount ? amount : WOW_MANA_MAX);
    } else if (!strcasecmp(argv[1], "gold")) {
        local->copper += MIN(amount, UINT32_MAX - local->copper);
    } else if (!strcasecmp(argv[1], "xp")) {
        local->experience += MIN(amount, UINT32_MAX - local->experience);
    } else {
        Wow_CheatPrintf(ent, "WoW: unsupported give target '%s'", argv[1]);
        Wow_CheatHelp(ent);
        return;
    }
    Wow_UpdatePlayerHud(ent);
    UI_WriteWowHud(ent);
    Wow_CheatPrintf(ent, "WoW: give %s applied", argv[1]);
}

static void Wow_CheatCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!Wow_CheatsEnabled()) {
        Wow_CheatPrintf(ent, "WoW: cheats are disabled; set sv_cheats 1");
        return;
    }
    if (!local) {
        Wow_CheatPrintf(ent, "WoW: cheat '%s' requires a player entity", argv[0]);
        return;
    }
    if (!strcasecmp(argv[0], "god")) {
        local->godmode = !local->godmode;
        Wow_CheatPrintf(ent, "WoW: god %s", local->godmode ? "on" : "off");
    } else if (!strcasecmp(argv[0], "kill")) {
        Wow_AIDie(ent, NULL);
        Wow_CheatPrintf(ent, "WoW: player killed");
    } else {
        (void)argc;
        Wow_CheatPrintf(ent, "WoW: unsupported cheat command '%s'", argv[0]);
    }
}

#define WOW_LOOT_RANGE 10.0f

static bool Wow_CanLootCorpse(edict_t *ent, edict_t *corpse) {
    wowEntityLocal_t *player = Wow_EntityLocal(ent);
    wowEntityLocal_t *local = corpse ? Wow_EntityLocal(corpse) : NULL;
    if (!ent || !ent->inuse || !ent->client || !player || player->dead ||
        !corpse || !corpse->inuse || !local || local->think != Wow_RunCorpseFrame)
        return false;
    vec2_t delta = Vector2_sub(&corpse->s.origin2, &ent->s.origin2);
    return delta.x * delta.x + delta.y * delta.y <= WOW_LOOT_RANGE * WOW_LOOT_RANGE;
}

static void Wow_CloseLoot(wowClient_t *client) {
    client->loot_target = 0;
    client->loot_generation = 0;
    client->loot_snap_count = 0;
    memset(client->loot_snap, 0, sizeof(client->loot_snap));
}

static wowEntityLocal_t *Wow_LootTarget(edict_t *ent) {
    wowClient_t *client = (wowClient_t *)ent->client;
    edict_t *corpse = client->loot_target ? Wow_EdictByNumber(client->loot_target) : NULL;
    wowEntityLocal_t *local = corpse ? Wow_EntityLocal(corpse) : NULL;
    if (!Wow_CanLootCorpse(ent, corpse) || !local || !client->loot_generation ||
        local->loot_generation != client->loot_generation) {
        Wow_CloseLoot(client);
        return NULL;
    }
    return local;
}

static void Wow_RefreshLootSnapshot(wowClient_t *client, wowEntityLocal_t const *corpse) {
    memcpy(client->loot_snap, corpse->loot_items, sizeof(client->loot_snap));
    client->loot_snap_count = corpse->loot_count;
    if (!client->loot_snap_count) Wow_CloseLoot(client);
}

uint32_t Wow_InventoryStackLimit(uint32_t item_id) {
    wowItemDef_t const *def = Wow_ItemByEntry(item_id);
    return def ? def->stack_limit : 0;
}

bool Wow_StorageAdd(wowHudIcon_t *slots, uint32_t slot_count, wowHudIcon_t const *item) {
    if (!slots || !item || !item->count || !item->icon[0]) return false;
    wowHudIcon_t source = *item; /* input may alias a destination slot */
    item = &source;
    uint32_t limit = Wow_InventoryStackLimit(item->item_id);
    uint64_t capacity = 0;
    for (uint32_t i = 0; i < slot_count; i++) {
        wowHudIcon_t const *v = &slots[i];
        if (!v->icon[0] && !v->count) capacity += limit ? limit : item->count;
        else if (limit && v->item_id == item->item_id && v->count < limit)
            capacity += limit - v->count;
    }
    if (capacity < item->count) return false;
    uint32_t remaining = item->count;
    if (limit) for (uint32_t i = 0; i < slot_count && remaining; i++) {
        wowHudIcon_t *v = &slots[i];
        if (!v->icon[0] || v->item_id != item->item_id || v->count >= limit) continue;
        uint32_t n = MIN(remaining, limit - v->count);
        v->count += n; remaining -= n;
    }
    for (uint32_t i = 0; i < slot_count && remaining; i++) {
        wowHudIcon_t *v = &slots[i];
        if (v->icon[0] || v->count) continue;
        *v = *item;
        v->count = limit ? MIN(remaining, limit) : remaining;
        remaining -= v->count;
    }
    return true;
}

bool Wow_InventoryAdd(wowClient_t *client, wowHudIcon_t const *item) {
    return client && Wow_StorageAdd(client->inventory, WOW_UI_INVENTORY_SLOTS, item);
}

bool Wow_StorageRemove(wowHudIcon_t *slots, uint32_t slot_count, uint32_t slot, uint32_t count) {
    if (!slots || slot >= slot_count || !count || count > slots[slot].count) return false;
    wowHudIcon_t *item = &slots[slot];
    item->count -= count;
    if (!item->count) memset(item, 0, sizeof(*item));
    return true;
}

bool Wow_InventoryRemove(wowClient_t *client, uint32_t slot, uint32_t count) {
    return client && Wow_StorageRemove(client->inventory, WOW_UI_INVENTORY_SLOTS, slot, count);
}

/* Inventory commands use server-owned slots. */
static bool Wow_InventoryMove(wowClient_t *client, uint32_t from, uint32_t to, bool split) {
    if (from >= WOW_UI_INVENTORY_SLOTS || to >= WOW_UI_INVENTORY_SLOTS || from == to) return false;
    wowHudIcon_t *src = &client->inventory[from], *dst = &client->inventory[to];
    if (!src->icon[0] || !src->count) return false;
    if (split) {
        if (src->count < 2 || dst->icon[0]) return false;
        *dst = *src;
        dst->count = src->count / 2;
        src->count -= dst->count;
    } else if (!dst->icon[0]) {
        *dst = *src;
        memset(src, 0, sizeof(*src));
    } else if (src->item_id && src->item_id == dst->item_id && Wow_InventoryStackLimit(src->item_id)) {
        uint32_t limit = Wow_InventoryStackLimit(src->item_id);
        if (dst->count >= limit) return false;
        uint32_t n = MIN(src->count, limit - dst->count);
        dst->count += n;
        src->count -= n;
        if (!src->count) memset(src, 0, sizeof(*src));
    } else {
        wowHudIcon_t tmp = *src; *src = *dst; *dst = tmp;
    }
    return true;
}

static bool Wow_InventoryUse(edict_t *ent, uint32_t slot) {
    if (slot >= WOW_UI_INVENTORY_SLOTS) return false;
    wowClient_t *client = (wowClient_t *)ent->client;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    wowHudIcon_t *item = &client->inventory[slot];
    if (!local || local->dead || !local->health || local->health >= 100 || !item->count) return false;
    if (item->item_id == 1251 || item->item_id == 2581) {
        if (local->recently_bandaged_ms || local->bandage_ticks || local->cast_spell != SPELL_NONE ||
            local->cast_release_time || Wow_PlayerIsMoving()) return false;
        local->bandage_heal = item->item_id == 1251 ? 11 : 19;
        local->bandage_ticks = 6;
        local->bandage_tick_ms = 1000;
        local->recently_bandaged_ms = 60000;
        Wow_InventoryRemove(client, slot, 1);
        return true;
    }
    wowItemDef_t const *def = Wow_ItemByEntry(item->item_id);
    if (!def || !def->heal) return false;
    local->health = MIN(100u, local->health + def->heal);
    Wow_InventoryRemove(client, slot, 1);
    return true;
}

static bool Wow_InventorySlotArg(cstring_t arg, uint32_t *slot) {
    char *end;
    if (!arg[0] || arg[0] < '0' || arg[0] > '9') return false;
    unsigned long value = strtoul(arg, &end, 10);
    if (*end || value >= WOW_UI_INVENTORY_SLOTS) return false;
    *slot = (uint32_t)value;
    return true;
}

static bool Wow_TakeLootSlot(wowClient_t *client, wowEntityLocal_t *corpse, uint32_t slot) {
    if (slot >= WOW_MAX_LOOT_ITEMS || !client->loot_snap[slot].icon[0] ||
        memcmp(&client->loot_snap[slot], &corpse->loot_items[slot], sizeof(wowHudIcon_t)))
        return false;
    if (Wow_InventoryAdd(client, &corpse->loot_items[slot])) {
        memset(&corpse->loot_items[slot], 0, sizeof(corpse->loot_items[slot]));
        if (corpse->loot_count) corpse->loot_count--;
        return true;
    }
    return false;
}

/* Open the loot window for a specific corpse entity.  Snapshots items into the
 * client struct, auto-takes copper, and triggers the player loot animation. */
static void Wow_OpenLootTarget(edict_t *ent, edict_t *corpse) {
    wowClient_t *client = (wowClient_t *)ent->client;
    wowEntityLocal_t *player_local = Wow_EntityLocal(ent);
    wowEntityLocal_t *corpse_local = corpse ? Wow_EntityLocal(corpse) : NULL;

    if (!Wow_CanLootCorpse(ent, corpse) || !corpse_local ||
        (corpse_local->loot_count == 0 && corpse_local->loot_copper == 0)) return;

    client->loot_target = corpse->s.number;
    client->loot_generation = corpse_local->loot_generation;
    memcpy(client->loot_snap, corpse_local->loot_items, sizeof(client->loot_snap));
    client->loot_snap_count = corpse_local->loot_count;

    /* Copper auto-loots on open (classic WoW behaviour). */
    if (corpse_local->loot_copper > 0 && player_local) {
        uint32_t amount = MIN(corpse_local->loot_copper, UINT32_MAX - player_local->copper);
        player_local->copper += amount;
        corpse_local->loot_copper -= amount;
        ent->client->ps.stats[WOW_STAT_COPPER] = (uint16_t)MIN(player_local->copper, 0xFFFFu);
    }

    /* Loot animation: plays once then Wow_UpdatePlayerHud resets to Stand. */
    if (player_local) {
        Wow_SetEntityAnimation(ent, "Loot");
        player_local->loot_anim_timer = 1200; /* ms; long enough for bend-down pose */
    }
    UI_WriteWowHud(ent);
}

static void Wow_ClientCommand(edict_t *ent, uint32_t argc, cstring_t argv[]) {
    if (((wowClient_t *)ent->client)->taxi_flying) return;
    if (argc >= 1 && !strcasecmp(argv[0], "taxi")) {
        Wow_TaxiCommand(ent, argc, argv);
    } else if (argc >= 1 && !strcasecmp(argv[0], "give")) {
        Wow_GiveCommand(ent, argc, argv);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "god") || !strcasecmp(argv[0], "kill"))) {
        Wow_CheatCommand(ent, argc, argv);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t quest_id = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
        edict_t *selected = ((wowClient_t *)ent->client)->selected_entity
            ? Wow_EdictByNumber(((wowClient_t *)ent->client)->selected_entity) : NULL;
        wowEntityLocal_t *selected_local = selected ? Wow_EntityLocal(selected) : NULL;
        if (!quest_id && selected_local)
            quest_id = Wow_QuestForGiver(client, selected_local);
        if (!quest_id || !Wow_QuestDetail(quest_id)) {
            fprintf(stderr, "WoW: quest UI has no server data for quest %u\n", (unsigned)quest_id);
            return;
        }
        if (!Wow_QuestGiverValid(ent, quest_id)) return;
        client->quest_id = quest_id;
        client->quest_journal_detail = false;
        client->quest_open = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "quest_inspect")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t id = (uint32_t)strtoul(argv[1], NULL, 10);
        if (!SV_QuestFind(client->quest_log, client->quest_count, id)) return;
        client->quest_id = id;
        client->quest_journal_detail = true;
        client->quest_open = true;
        client->questlog_open = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_back")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->quest_open = false;
        client->quest_journal_detail = false;
        client->questlog_open = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->quest_open = false;
        client->questlog_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_accept")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t quest_id = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : client->quest_id;
        if (quest_id && client->quest_open && !client->quest_journal_detail && client->quest_id == quest_id && Wow_QuestGiverValid(ent, quest_id)) Wow_AddQuest(client, quest_id);
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_complete")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t quest_id = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : client->quest_id;
        if (client->quest_open && !client->quest_journal_detail && client->quest_id == quest_id && Wow_QuestGiverValid(ent, quest_id)) Wow_CompleteQuest(ent, quest_id);
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_abandon")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t quest_id = argc >= 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : client->quest_id;
        svQuestEntry_t *state = SV_QuestFind(client->quest_log, client->quest_count, quest_id);
        if (state) {
            Wow_RemoveQuest(client, (uint32_t)(state - client->quest_log));
            client->quest_open = false;
            UI_WriteWowHud(ent);
        }
    } else if (argc >= 2 && !strcasecmp(argv[0], "quest_track")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t id = (uint32_t)strtoul(argv[1], NULL, 10);
        svQuestEntry_t *state = SV_QuestFind(client->quest_log, client->quest_count, id);
        if (state) Wow_QuestTrackerToggle(client->quest_tracked, client->quest_count, (uint32_t)(state - client->quest_log));
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "questlog")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->questlog_open = !client->questlog_open;
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "questlog_page")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        if (!strcasecmp(argv[1], "next") &&
            client->questlog_offset + WOW_QUEST_PAGE_SIZE < client->quest_count)
            client->questlog_offset += WOW_QUEST_PAGE_SIZE;
        else if (!strcasecmp(argv[1], "prev"))
            client->questlog_offset -= MIN(client->questlog_offset, WOW_QUEST_PAGE_SIZE);
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "message_open")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t message_id = (uint32_t)strtoul(argv[1], NULL, 10);
        FOR_LOOP(i, client->message_count) {
            if (client->messages[i].message_id != message_id) continue;
            client->message_open_id = message_id;
            client->messages[i].flags &= (uint8_t)~WOW_UI_MESSAGE_UNREAD;
            UI_WriteWowHud(ent);
            break;
        }
    } else if (argc >= 1 && !strcasecmp(argv[0], "message_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->message_open_id = 0;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "loot")) {
        /* Open loot window for the nearest corpse within melee+loot range. */
        edict_t *corpse = Wow_FindNearestCorpse(ent, WOW_LOOT_RANGE);
        if (corpse) Wow_OpenLootTarget(ent, corpse);
    } else if (argc >= 2 && !strcasecmp(argv[0], "loot_take")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        char *end;
        unsigned long slot = strtoul(argv[1], &end, 10);
        if (!argv[1][0] || *end || slot >= WOW_MAX_LOOT_ITEMS) return;
        if (argc >= 3) {
            unsigned long long generation = strtoull(argv[2], &end, 10);
            if (!argv[2][0] || *end || generation != client->loot_generation) return;
        }
        wowEntityLocal_t *corpse = Wow_LootTarget(ent);
        if (corpse) {
            Wow_TakeLootSlot(client, corpse, (uint32_t)slot);
            Wow_RefreshLootSnapshot(client, corpse);
        }
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "loot_all")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        if (argc >= 2) {
            char *end;
            unsigned long long generation = strtoull(argv[1], &end, 10);
            if (!argv[1][0] || *end || generation != client->loot_generation) return;
        }
        wowEntityLocal_t *corpse = Wow_LootTarget(ent);
        if (corpse) {
            FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) Wow_TakeLootSlot(client, corpse, i);
            Wow_RefreshLootSnapshot(client, corpse);
        }
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "loot_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        Wow_CloseLoot(client);
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "worldmap")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        if (argc == 1) {
            client->worldmap_open = !client->worldmap_open;
            client->worldmap_continent = false;
            client->worldmap_selection = 0;
            client->worldmap_choosing = false;
            client->worldmap_landmarks_list = false;
            client->worldmap_page = 0;
        } else if (!client->worldmap_open) {
            return;
        } else if (!strcasecmp(argv[1], "landmarks")) {
            client->worldmap_landmarks_visible = !client->worldmap_landmarks_visible;
        } else if (!strcasecmp(argv[1], "places")) {
            client->worldmap_landmarks_list = !client->worldmap_landmarks_list;
            client->worldmap_choosing = false;
            client->worldmap_landmarks_page = 0;
        } else if (!strcasecmp(argv[1], "landmark_prev") && client->worldmap_landmarks_list && client->worldmap_landmarks_page) {
            --client->worldmap_landmarks_page;
        } else if (!strcasecmp(argv[1], "landmark_next") && client->worldmap_landmarks_list && client->worldmap_landmarks_next) {
            ++client->worldmap_landmarks_page;
        } else if (!strcasecmp(argv[1], "landmark") && argc == 3 && client->worldmap_landmarks_list) {
            char *end;
            unsigned long row = strtoul(argv[2], &end, 10);
            uint32_t size = 0;
            if (!*argv[2] || *end || !row || row > UINT32_MAX) return;
            uint8_t *data = gi.ReadFile("DBFilesClient\\AreaPOI.dbc", &size);
            wowLandmark_t landmark;
            bool valid = Wow_LandmarkRead(data, size, (uint32_t)row - 1, CM_WowGetMapId(),
                                          client->worldmap_left, client->worldmap_right,
                                          client->worldmap_top, client->worldmap_bottom, &landmark);
            SAFE_DELETE(data, gi.MemFree);
            if (!valid) return;
            client->worldmap_landmarks_selected = (uint32_t)row;
            client->worldmap_landmarks_visible = true;
            client->worldmap_landmarks_list = false;
        } else if (!strcasecmp(argv[1], "continent")) {
            client->worldmap_landmarks_list = false;
            client->worldmap_continent = true;
            client->worldmap_selection = 0;
            client->worldmap_choosing = false;
        } else if (!strcasecmp(argv[1], "current")) {
            client->worldmap_landmarks_list = false;
            client->worldmap_continent = false;
            client->worldmap_selection = 0;
            client->worldmap_choosing = false;
        } else if (!strcasecmp(argv[1], "zones")) {
            client->worldmap_landmarks_list = false;
            client->worldmap_choosing = !client->worldmap_choosing;
            client->worldmap_page = 0;
        } else if (!strcasecmp(argv[1], "previous") && client->worldmap_choosing && client->worldmap_page) {
            client->worldmap_page--;
        } else if (!strcasecmp(argv[1], "next") && client->worldmap_choosing && client->worldmap_has_next_page) {
            client->worldmap_page++;
        } else if (!strcasecmp(argv[1], "zone") && argc == 3 && client->worldmap_choosing) {
            char *end;
            unsigned long row = strtoul(argv[2], &end, 10);
            if (!*argv[2] || *end || row == 0 || row > UINT32_MAX) return;
            uint32_t old = client->worldmap_selection;
            client->worldmap_selection = (uint32_t)row;
            if (!UI_ResolveWorldMap(ent)) {
                client->worldmap_selection = old;
                UI_ResolveWorldMap(ent);
                return;
            }
            client->worldmap_continent = false;
            client->worldmap_choosing = false;
        } else {
            return;
        }
        client->worldmap_refresh = 0;
        if (client->worldmap_open) UI_ResolveWorldMap(ent);
        UI_WriteWorldMap(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "bag_select")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t slot;
        if (!client->backpack_open || !Wow_InventorySlotArg(argv[1], &slot)) return;
        if (client->backpack_selected && client->backpack_selected != slot + 1) {
            Wow_InventoryMove(client, client->backpack_selected - 1, slot, client->backpack_splitting);
            client->backpack_splitting = false;
            client->backpack_selected = 0;
        } else {
            client->backpack_selected = client->backpack_selected ? 0 : slot + 1;
            client->backpack_splitting = false;
        }
        UI_WriteWowHud(ent);
    } else if (argc == 1 && !strcasecmp(argv[0], "bag_split_select")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        if (!client->backpack_open || !client->backpack_selected) return;
        client->backpack_splitting = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "bag_use")) {
        uint32_t slot;
        if (!Wow_InventorySlotArg(argv[1], &slot)) return;
        Wow_InventoryUse(ent, slot);
        UI_WriteWowHud(ent);
    } else if (argc == 3 && (!strcasecmp(argv[0], "bag_move") || !strcasecmp(argv[0], "bag_split"))) {
        uint32_t from, to;
        if (!Wow_InventorySlotArg(argv[1], &from) || !Wow_InventorySlotArg(argv[2], &to)) return;
        Wow_InventoryMove((wowClient_t *)ent->client, from, to, !strcasecmp(argv[0], "bag_split"));
        ((wowClient_t *)ent->client)->backpack_selected = 0;
        UI_WriteWowHud(ent);
    } else if (argc == 1 && !strcasecmp(argv[0], "crafting")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->crafting_open = !client->crafting_open;
        if (client->crafting_open) client->spellbook_open = false;
        UI_WriteWowHud(ent);
    } else if (argc == 2 && !strcasecmp(argv[0], "craft")) {
        char *end;
        if (!argv[1] || argv[1][0] < '0' || argv[1][0] > '9') return;
        unsigned long spell = strtoul(argv[1], &end, 10);
        if (*end || spell > UINT32_MAX) return;
        Wow_Craft(ent, (uint32_t)spell);
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "spellbook")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->spellbook_open = !client->spellbook_open;
        if (client->spellbook_open) client->crafting_open = false;
        UI_WriteWowHud(ent);
    } else if (argc == 1 && !strcasecmp(argv[0], "spellbook_train_fireball")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        wowEntityLocal_t *local = Wow_EntityLocal(ent);
        if (Wow_GetPlayerClass() != WOW_CLASS_MAGE || !local || local->experience < 400 || client->fireball_rank2_learned) return;
        client->fireball_rank2_learned = true;
        for (uint32_t i = 0; i < WOW_UI_ACTION_SLOTS; ++i) {
            if (client->action_spells[i] != WOW_SPELL_FIREBOLT) continue;
            client->action_spells[i] = WOW_SPELL_FIREBALL_RANK2;
            snprintf(client->actions[i].name, sizeof(client->actions[i].name), "%s", wow_spells[WOW_SPELL_FIREBALL_RANK2].name);
        }
        client->spellbook_selected = WOW_SPELL_FIREBALL_RANK2;
        UI_WriteWowHud(ent);
    } else if (argc == 2 && !strcasecmp(argv[0], "spellbook_select")) {
        char *end; unsigned long spell;
        if (!argv[1] || argv[1][0] < '0' || argv[1][0] > '9') return;
        spell = strtoul(argv[1], &end, 10);
        if (*end || spell >= wow_spell_count || !Wow_PlayerKnowsSpell(ent, (uint32_t)spell)) return;
        ((wowClient_t *)ent->client)->spellbook_selected = (uint32_t)spell;
        UI_WriteWowHud(ent);
    } else if (argc == 3 && !strcasecmp(argv[0], "spellbook_bind")) {
        char *end; unsigned long slot, spell;
        wowClient_t *client = (wowClient_t *)ent->client;
        if (!argv[1] || argv[1][0] < '0' || argv[1][0] > '9' || !argv[2]) return;
        slot = strtoul(argv[1], &end, 10);
        if (*end || slot >= WOW_UI_ACTION_SLOTS) return;
        if (!strcmp(argv[2], "clear")) {
            client->action_spells[slot] = SPELL_NONE;
            memset(&client->actions[slot], 0, sizeof(client->actions[slot]));
        } else {
            if (argv[2][0] < '0' || argv[2][0] > '9') return;
            spell = strtoul(argv[2], &end, 10);
            if (*end || spell >= wow_spell_count || !Wow_PlayerKnowsSpell(ent, (uint32_t)spell)) return;
            client->action_spells[slot] = (uint32_t)spell;
            snprintf(client->actions[slot].icon, sizeof(client->actions[slot].icon), "%s", Wow_SpellIcon((uint32_t)spell));
            snprintf(client->actions[slot].name, sizeof(client->actions[slot].name), "%s", wow_spells[spell].name);
            client->actions[slot].count = 1;
        }
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "backpack")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->backpack_open = !client->backpack_open;
        client->backpack_selected = 0;
        client->backpack_splitting = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "respawn")) {
        char race[64], sex[64]; uint32_t class_id, appearance;
        Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
        uint32_t idx = Wow_SelectSpawnPoint(race, class_id);
        if (idx == ~0u) {
            fprintf(stderr, "WoW: no respawn for race=%s class=%u; using Orc Warrior spawn\n", race, (unsigned)class_id);
            idx = Wow_SelectSpawnPoint("Orc", WOW_CLASS_WARRIOR);
        }
        if (idx != ~0u) Wow_TeleportPlayer(ent, idx);
    } else if (argc >= 2 && !strcasecmp(argv[0], "warp")) {
        /* warp <name>: teleport to a named WorldSafeLoc on the current map,
         * or perform a cross-map teleport via areatrigger_teleport by name.
         * Usable at runtime and from +warp on the command line (forwarded via client). */
        cstring_t query = argv[1];
        char wdt[MAX_PATHLEN];
        uint32_t n = CM_WowGetAllSpawnCount(); bool found = false;
        /* First: search WorldSafeLocs on current map (same-map warp). */
        FOR_LOOP(i, n) {
            cstring_t nm = CM_WowGetSpawnName(i); vec3_t const *pos;
            uint32_t qlen = (uint32_t)strlen(query), nlen; bool match = false; uint32_t j;
            if (!nm) continue;
            nlen = (uint32_t)strlen(nm);
            if (nlen < qlen) continue; /* guard unsigned subtraction below */
            for (j = 0; !match && j <= nlen - qlen; j++)
                if (!strncasecmp(nm + j, query, qlen)) match = true;
            if (!match) continue;
            pos = CM_WowGetSpawnPos(i);
            if (!pos) continue;
            Wow_TeleportPlayerToPos(ent, pos->x, pos->y, pos->z, 0.0f);
            found = true; break;
        }
        /* Second: search areatrigger_teleport by name (cross-map warp). */
        if (!found) {
            wowAreatrigTeleport_t const *at = Wow_AreaTrigTeleportByName(query);
            if (at && Wow_WdtPathForMapId(at->target_map, wdt, sizeof(wdt))) {
                wow_pending_teleport = (wowPendingTeleport_t){ true,
                    at->target_x, at->target_y, at->target_z, at->target_orientation };
                fprintf(stderr, "WoW: warp '%s' → map %u (%s)\n", query, (unsigned)at->target_map, wdt);
                gi.MenuAction("map", wdt);
                found = true;
            }
        }
        if (!found)
            fprintf(stderr, "WoW: warp '%s': no matching WorldSafeLoc or areatrigger destination\n", query);
    } else if (argc >= 5 && (!strcasecmp(argv[0], "move") || !strcasecmp(argv[0], "wowmove"))) {
        wow_move.flags = (uint32_t)strtoul(argv[1], NULL, 10);
        wow_move.yaw = (float)atof(argv[2]);
        wow_move.pitch = Wow_Clamp((float)atof(argv[3]), WOW_CAMERA_MIN_PITCH, WOW_CAMERA_MAX_PITCH);
        wow_move.distance = Wow_Clamp((float)atof(argv[4]), WOW_CAMERA_MIN_DISTANCE, WOW_CAMERA_MAX_DISTANCE);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "select"))) {
        Wow_SelectEntity(ent, argc >= 2 ? Wow_EdictByNumber((uint32_t)strtoul(argv[1], NULL, 10)) : NULL);
    } else if (argc == 1 && !strcasecmp(argv[0], "bank_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->bank_target = client->bank_entry = 0;
        UI_WriteWowHud(ent);
    } else if (argc == 6 && (!strcasecmp(argv[0], "bank_deposit") || !strcasecmp(argv[0], "bank_withdraw"))) {
        wowClient_t *client = (wowClient_t *)ent->client;
        uint32_t values[5];
        for (uint32_t i = 0; i < 5; i++) {
            char *end;
            if (argv[i + 1][0] < '0' || argv[i + 1][0] > '9') return;
            unsigned long value = strtoul(argv[i + 1], &end, 10);
            if (*end || value > UINT32_MAX) return;
            values[i] = (uint32_t)value;
        }
        bool deposit = !strcasecmp(argv[0], "bank_deposit");
        if (values[0] >= (deposit ? WOW_UI_INVENTORY_SLOTS : 16) || values[1] != client->bank_revision || values[4] > 1) return;
        wowHudIcon_t const *item = deposit ? &client->inventory[values[0]] : &client->bank[values[0]];
        if (item->item_id != values[2] || item->count != values[3]) return;
        Wow_BankTransfer(ent, deposit, values[0], values[4] != 0);
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "merchant_quest")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        edict_t *npc = Wow_MerchantValid(ent) ? Wow_EdictByNumber(client->merchant_target) : NULL;
        wowEntityLocal_t *npc_local = npc ? Wow_EntityLocal(npc) : NULL;
        uint32_t quest_id = npc_local ? Wow_QuestForGiver(client, npc_local) : 0;
        if (!Wow_QuestGiverValid(ent, quest_id)) return;
        if (!quest_id || !Wow_QuestDetail(quest_id)) return;
        client->merchant_target = client->merchant_entry = 0;
        client->quest_journal_detail = false;
        client->quest_id = quest_id;
        client->quest_open = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "merchant_view")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        if (!Wow_MerchantValid(ent)) return;
        client->merchant_buyback_view = !client->merchant_buyback_view;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "merchant_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->merchant_target = client->merchant_entry = 0;
        UI_WriteWowHud(ent);
    } else if (argc >= 3 && (!strcasecmp(argv[0], "merchant_buy") || !strcasecmp(argv[0], "merchant_sell") || !strcasecmp(argv[0], "merchant_buyback"))) {
        char *end;
        unsigned long slot = strtoul(argv[1], &end, 10);
        if (!argv[1][0] || *end || slot > UINT32_MAX) return;
        unsigned long revision = strtoul(argv[2], &end, 10);
        if (!argv[2][0] || *end || revision > UINT32_MAX || revision != ((wowClient_t *)ent->client)->merchant_revision) return;
        if (!strcasecmp(argv[0], "merchant_sell")) {
            wowClient_t *client = (wowClient_t *)ent->client;
            unsigned long id, count;
            if (argc < 5 || slot >= WOW_UI_INVENTORY_SLOTS) return;
            id = strtoul(argv[3], &end, 10);
            if (!argv[3][0] || *end || id > UINT32_MAX) return;
            count = strtoul(argv[4], &end, 10);
            if (!argv[4][0] || *end || count > UINT32_MAX ||
                client->inventory[slot].item_id != id || client->inventory[slot].count != count) return;
        }
        if (!strcasecmp(argv[0], "merchant_buy")) Wow_MerchantBuy(ent, (uint32_t)slot);
        else if (!strcasecmp(argv[0], "merchant_sell")) Wow_MerchantSell(ent, (uint32_t)slot);
        else Wow_MerchantBuyback(ent, (uint32_t)slot);
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "interact")) {
        edict_t *target = Wow_EdictByNumber((uint32_t)strtoul(argv[1], NULL, 10));
        wowEntityLocal_t *target_local = target ? Wow_EntityLocal(target) : NULL;
        Wow_SelectEntity(ent, target && target != ent ? target : NULL);
        if (target_local && target_local->think == Wow_RunCorpseFrame) {
            /* Right-clicked a corpse: open loot window. */
            Wow_OpenLootTarget(ent, target);
        } else if (target_local && Wow_TaxiOpenNpc(ent, target)) {
            /* Flight-master interaction opens a server-validated route list. */
        } else if (target_local && Wow_BankOpen(ent, target)) {
            UI_WriteWowHud(ent);
        } else if (target_local && Wow_MerchantOpen(ent, target)) {
            UI_WriteWowHud(ent);
        } else if (target_local && target_local->quest_id) {
            wowClient_t *client = (wowClient_t *)ent->client;
            uint32_t quest_id = Wow_QuestForGiver(client, target_local);
            if (!quest_id || !Wow_QuestDetail(quest_id) || !Wow_QuestGiverValid(ent, quest_id)) return;
            client->quest_journal_detail = false;
            client->quest_id = quest_id;
            client->quest_open = true;
            UI_WriteWowHud(ent);
        } else {
            wowEntityLocal_t *local = Wow_EntityLocal(ent);
            if (!local || local->dead || !local->attack) return;
            local->enemy = target && target != ent ? target : NULL;
            local->attack(ent);
        }
    } else if (argc >= 1 && (!strcasecmp(argv[0], "wow_cycle_target") || !strcasecmp(argv[0], "cycletarget"))) {
        uint32_t old = ((wowClient_t *)ent->client)->selected_entity;
        uint32_t start = old > 0 ? old + 1 : MAX_CLIENTS;
        for (uint32_t i = start; i < (uint32_t)globals.num_edicts; i++) {
            edict_t *t = &wow_edicts[i];
            if (Wow_IsAttackTarget(ent, t)) {
                Wow_SelectEntity(ent, t);
                return;
            }
        }
        for (uint32_t i = MAX_CLIENTS; i < start && i < (uint32_t)globals.num_edicts; i++) {
            edict_t *t = &wow_edicts[i];
            if (Wow_IsAttackTarget(ent, t)) {
                Wow_SelectEntity(ent, t);
                return;
            }
        }
    } else if (argc >= 1 && (!strcasecmp(argv[0], "attack") || !strcasecmp(argv[0], "wowattack"))) {
        edict_t *target = argc >= 2
            ? Wow_EdictByNumber((uint32_t)strtoul(argv[1], NULL, 10))
            : Wow_FindNearestAttackTarget(ent, WOW_MELEE_RANGE);
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (!ent || !local || local->dead || !local->attack) {
            return;
        }
        local->enemy = target && target != ent ? target : NULL;
        Wow_SelectEntity(ent, target && target != ent ? target : NULL);
        local->attack(ent);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "stopattack") || !strcasecmp(argv[0], "wowstopattack"))) {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (local) {
            Wow_CancelSpellCast(ent);  /* interrupt any active cast */
            local->enemy = NULL;
            local->attack_time = 0;
            local->attack_damage_time = 0;
            local->attack_backswing_time = 0;
            local->attack_damage_done = false;
            local->pain_time = 0;
        }
        if (!local || !local->dead) {
            Wow_SetStandMove(ent);
        }
    } else if (argc >= 2 && (!strcasecmp(argv[0], "wow_action") || !strcasecmp(argv[0], "spellbook_cast"))) {
        char *end;
        unsigned long parsed;
        if (!argv[1] || argv[1][0] < '0' || argv[1][0] > '9') return;
        parsed = strtoul(argv[1], &end, 10);
        bool book_cast = !strcasecmp(argv[0], "spellbook_cast");
        if (*end || !ent->client || (book_cast ? parsed >= wow_spell_count : parsed >= WOW_UI_ACTION_SLOTS)) return;
        uint32_t slot = book_cast ? 255 : (uint32_t)parsed;
        uint32_t spell = book_cast ? (uint32_t)parsed : ((wowClient_t *)ent->client)->action_spells[slot];
        if (book_cast && !Wow_PlayerKnowsSpell(ent, spell)) return;
        if (spell >= wow_spell_count) return;
        wowSpellDef_t const *def = &wow_spells[spell];
        wowEntityLocal_t *cl = Wow_EntityLocal(ent);

        if (!cl || cl->dead || !cl->health || (cl->cast_spell != SPELL_NONE) || cl->cast_release_time || cl->gcd_time > 0 || cl->spell_cooldown[spell] > 0) return;

        if (def->cast_time > 0 && Wow_PlayerIsMoving()) {
            fprintf(stderr, "WoW: %s — cannot cast while moving\n", def->name);
            return;
        }

        if (def->mana_cost > cl->mana) {
            fprintf(stderr, "WoW: %s — not enough mana\n", def->name);
            return;
        }

        edict_t *target = def->range > 0 ? Wow_FindSpellTarget(ent, def->range) : NULL;
        /* For instant melee spells, accept the selected target even when out of
         * range — the auto-chase in Wow_RunFrame closes the gap automatically. */
        if (spell == WOW_SPELL_ATTACK && !target && ent->client && ((wowClient_t *)ent->client)->selected_entity) {
            edict_t *t = Wow_EdictByNumber(((wowClient_t *)ent->client)->selected_entity);
            if (Wow_IsAttackTarget(ent, t))
                target = t;
        }
        if (def->range > 0 && !target) {
            fprintf(stderr, "WoW: %s — no target in range\n", def->name);
            return;
        }

        cl->bandage_ticks = 0; /* a successfully started spell interrupts First Aid */
        cl->selected_action_slot = slot;
        if (def->cast_time > 0) {
            Wow_BeginSpellCast(ent, spell, target ? target->s.number : 0);
        } else {
            cl->gcd_time = WOW_GCD_MS;
            cl->spell_cooldown[spell] = def->cooldown_ms;
            if (def->cast) def->cast(ent, target);
        }
        if (ent->client->ps.client_ui_state == CLIENT_UI_GAME)
            UI_WriteWowActionBar(ent);

    }
}

typedef enum { QUEST_MARKER_NONE = 0, QUEST_MARKER_AVAILABLE, QUEST_MARKER_ACTIVE, QUEST_MARKER_COMPLETE } questMarker_t;

/* Returns the highest-priority marker for this NPC relative to the given player:
 * COMPLETE > ACTIVE > AVAILABLE > NONE. */
static questMarker_t Wow_QuestMarkerForGiver(wowClient_t *client, wowEntityLocal_t const *local) {
    uint32_t group;
    questMarker_t best = QUEST_MARKER_NONE;
    if (!local->quest_id) return QUEST_MARKER_NONE;
    group = Wow_QuestGiverGroup(local->quest_id, &local->home);
    if (group == WOW_QUEST_GIVER_GROUP_NONE) {
        svQuestEntry_t *state = SV_QuestFind(client->quest_log, client->quest_count, local->quest_id);
        if (Wow_QuestWasRewarded(client, local->quest_id)) return QUEST_MARKER_NONE;
        if (state && state->status == SV_QUEST_COMPLETE) return QUEST_MARKER_COMPLETE;
        if (state && state->status == SV_QUEST_ACTIVE) return QUEST_MARKER_ACTIVE;
        return state ? QUEST_MARKER_NONE : QUEST_MARKER_AVAILABLE;
    }
    FOR_LOOP(i, Wow_QuestGiverGroupCount(group)) {
        wowQuestGiver_t const *cur = Wow_QuestGiverInGroup(group, i);
        svQuestEntry_t *e;
        e = SV_QuestFind(client->quest_log, client->quest_count, cur->quest_id);
        if (!e) {
            wowQuestDetail_t const *detail = Wow_QuestDetail(cur->quest_id);
            if (!Wow_QuestWasRewarded(client, cur->quest_id) && detail && Wow_QuestPrereqMet(client, detail->prev_quest))
                best = MAX(best, QUEST_MARKER_AVAILABLE);
            continue;
        }
        if (e->status == SV_QUEST_REWARDED) continue;
        if (e->status == SV_QUEST_COMPLETE) return QUEST_MARKER_COMPLETE;
        if (e->status == SV_QUEST_ACTIVE) best = QUEST_MARKER_ACTIVE;
    }
    return best;
}

/* Author recipient-specific hover and quest presentation from the private creature state. */
static void Wow_CustomizeEntity(uint32_t player, edict_t const *ent, entityState_t *state) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    if (player >= MAX_CLIENTS) return;
    state->flags &= ~EF_HOVER_HEALTH;
    state->stats[ENT_HEALTH] = state->stats[ENT_MANA] = 0;
    if (local && state->model && (ent->svflags & SVF_MONSTER) && !(ent->svflags & SVF_DEADMONSTER) &&
        !local->dead && local->health && !(state->flags & EF_NOT_SELECTABLE)) {
        state->flags |= EF_HOVER_HEALTH;
        state->stats[ENT_HEALTH] = (uint8_t)(MIN(local->health, 100u) * 255u / 100u);
        state->stats[ENT_MANA] = (uint8_t)(MIN(local->mana, WOW_MANA_MAX) * 255u / WOW_MANA_MAX);
    }
    /* Vanilla reveals a creature's overhead name only after that recipient selects it. */
    if (wow_clients[player].selected_entity != state->number) state->name = 0;
    if (!local || !local->quest_id || !(state->flags & EF_HAS_QUEST)) return;
    state->flags &= ~(EF_HAS_QUEST | EF_QUEST_COMPLETE);
    switch (Wow_QuestMarkerForGiver(&wow_clients[player], local)) {
    case QUEST_MARKER_AVAILABLE:
        state->model2 = local->quest_available_model;
        state->renderfx |= RF_ATTACH_OVERHEAD;
        break;
    case QUEST_MARKER_COMPLETE: state->flags |= EF_HAS_QUEST | EF_QUEST_COMPLETE; break;
    case QUEST_MARKER_ACTIVE:   state->flags |= EF_HAS_QUEST; break;
    default: break;
    }
}

/* Actor movement owns focus; camera input changes only the orbit around that actor. */
static void Wow_ClientInput(edict_t *ent, inputCmd_t const *cmd) {
    if (cmd->action == BZ_INPUT_MOVE) {
        wow_move.flags = cmd->move.buttons;
    } else if (cmd->action == BZ_INPUT_VIEW && ent->client->ps.client_ui_state == CLIENT_UI_GAME) {
        vec3_t native = Wow_CameraFromEuler(&cmd->view.angles);
        wow_move.yaw = native.y;
        wow_move.pitch = Wow_Clamp(360.0f - native.x, WOW_CAMERA_MIN_PITCH, WOW_CAMERA_MAX_PITCH);
        wow_move.distance = Wow_Clamp(cmd->view.distance, WOW_CAMERA_MIN_DISTANCE, WOW_CAMERA_MAX_DISTANCE);
        Wow_UpdateCamera(ent);
    }
}

static void Wow_ClientBegin(edict_t *ent) {
    if (!ent) {
        return;
    }
    ent->client = &wow_clients[0].client;
    ent->client->ps.client_ui_state = CLIENT_UI_GAME;
    UI_WriteWowHud(ent);
    UI_WriteWorldMap(ent);
    UI_WriteWowCombatFeedback(ent, true);
    UI_WriteWowHover(ent);
    UI_WriteWelcomeWindow(ent);
}

/* WoW runs one persistent local character whose state outlives the connection, and Wow_ClientBegin rebuilds
 * every HUD layer on the next begin, so there is no connection-scoped state to release yet. */
static void Wow_ClientDisconnect(edict_t *ent) { if (ent == &wow_edicts[0]) Wow_CharacterFlush(); }

/* Map.dbc/LoadingScreens.dbc identify loading art without loading WDT/ADT terrain or spawning entities. */
static bool Wow_PrepareMap(cstring_t filename) {
    Wow_SelectLoadingScreen(filename);
    UI_WriteLoadingLayout(NULL);
    return true;
}

static edict_t *Wow_ClientEntity(uint32_t playernum) {
    return globals.edicts + playernum;
}

struct game_export *GetGameAPI(struct game_import *import) {
    gi = *import;
    (void)gi;

    globals.Init = Wow_Init;
    globals.Shutdown = Wow_Shutdown;
    globals.RunFrame = Wow_RunFrame;
    globals.GetThemeValue = Wow_GetThemeValue;
    globals.ClientEntity = Wow_ClientEntity;
    globals.ClientCommand = Wow_ClientCommand;
    globals.ClientInput = Wow_ClientInput;
    globals.PrepareMap = Wow_PrepareMap;
    globals.ClientBegin = Wow_ClientBegin;
    globals.ClientDisconnect = Wow_ClientDisconnect;
    globals.CanSeeEntity = NULL;
    globals.CustomizeEntity = Wow_CustomizeEntity;
    globals.WriteClientDatagram = G_WriteClientDatagram;
    globals.PlayerCreateMap = Wow_SelectedPlayerCreateMap;
    globals.LoadMap = Wow_LoadMap;
    globals.GetWorldBounds = CM_GetWorldBounds;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);

    return &globals;
}
