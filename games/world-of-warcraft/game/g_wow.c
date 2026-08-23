#include "g_wow_local.h"
#include "common/stb_dbc.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

/* Generated server tables are included here so the unity source tree contains no generated files. */
#include "build/generated/g_playercreateinfo.c"
#include "build/generated/g_creatures.c"
#include "build/generated/g_quests.c"
#include "build/generated/g_weapons.c"
#include "build/generated/g_areatrigger_teleport.c"

struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
wowClient_t wow_clients[WOW_MAX_CLIENTS];
/* Configstring model indices for spell impact visuals; set during map load. */
static int wow_firebolt_impact_model = 0;
static int wow_frostbolt_impact_model = 0;
static char wow_loading_texture[MAX_PATHLEN] = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
static char wow_loading_title[128] = "World of Warcraft";

/* Pending cross-map teleport: set by Wow_CheckAreaTriggers / warp command before
 * gi.MenuAction("map", ...) fires; consumed once by Wow_SpawnEntities on the new map. */
typedef struct { BOOL pending; FLOAT x, y, z, orientation; } wowPendingTeleport_t;
static wowPendingTeleport_t wow_pending_teleport;

#define WOW_MAX_AREA_TRIGS 256
static WOWAREATRIG wow_area_trigs[WOW_MAX_AREA_TRIGS];
static DWORD       wow_area_trig_count;
enum {
    WOW_PLAYER_EQUIPMENT_UPPER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_LOWER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_HANDS = 1,
    WOW_PLAYER_EQUIPMENT_FEET = 1
};
static wowMove_t wow_move_cast = { "SpellCastDirected", NULL, NULL };

static struct {
    DWORD flags;
    FLOAT yaw;
    FLOAT pitch;
    FLOAT distance;
} wow_move = {
    .pitch = 342.0f,
    .distance = 8.0f,
};

#define WOW_MAX_SPAWNS_PER_FRAME 64

#define WOW_MAX_SPAWNS_PER_FRAME 64
/* Per-frame spawn budget (declared extern in g_wow_local.h). */
DWORD wow_spawns_this_frame = 0;

/* Starting inventory — 16 slots total; first 6 shown in the HUD quick-access bar,
 * all 16 visible in the backpack window.  Slots 6-15 start empty and fill with loot.
 * Spells never go here — they belong in actions[]. */
static wowHudIcon_t const wow_start_inventory[WOW_UI_INVENTORY_SLOTS] = {
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",          "Worn Knapsack",           1 },
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",          "Worn Knapsack",           1 },
    { "Interface\\Icons\\INV_Potion_51.blp",            "Minor Healing Potion",    2 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp",         "Tough Jerky",             5 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", "Worn Shortsword",         1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp",         "Copper Coin",            10 },
};

/* Warrior action bar — abilities only.
 * Slots 0-2 map to WOW_SPELL_ATTACK; slot 3 to WOW_SPELL_HEALING_TOUCH.
 * Slots 4-11 are empty until the player earns more abilities. */
static wowHudIcon_t const wow_actions_warrior[WOW_UI_ACTION_SLOTS] = {
    { "Interface\\Icons\\Ability_Warrior_Cleave.blp",      "Heroic Strike",  1 },
    { "Interface\\Icons\\Ability_Warrior_Charge.blp",      "Charge",         1 },
    { "Interface\\Icons\\Ability_Warrior_BattleShout.blp", "Battle Shout",   1 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp",   "Healing Touch",  1 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
};

/* Mage action bar — abilities only.
 * Slot 0 → auto-attack; slot 3 → heal; slots 4-5 → fire/frost. */
static wowHudIcon_t const wow_actions_mage[WOW_UI_ACTION_SLOTS] = {
    { "Interface\\Icons\\Ability_Warrior_Cleave.blp",      "Attack",         1 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp",   "Healing Touch",  1 },
    { "Interface\\Icons\\Spell_Fire_FireBolt02.blp",       "Fireball",       1 },
    { "Interface\\Icons\\Spell_Frost_FrostBolt02.blp",     "Frostbolt",      1 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
    { "",                                                   "",               0 },
};

/* -------------------------------------------------------------------------
 * Loot system: item definitions + loot templates keyed by creature display_id.
 * Items use real WoW 1.12 entry IDs and icon paths from the MPQ archives.
 * Icon rendering falls back to the placeholder texture if the BLP is absent.
 * -------------------------------------------------------------------------*/
typedef struct { DWORD entry; LPCSTR name; LPCSTR icon; } wowItemDef_t;
static wowItemDef_t const wow_item_defs[] = {
    { 2589,  "Linen Cloth",       "Interface\\Icons\\INV_Fabric_Linen_01.blp" },
    { 4234,  "Light Leather",     "Interface\\Icons\\INV_Misc_Leather_01.blp" },
    { 12208, "Raw Boar Ribs",     "Interface\\Icons\\INV_Misc_Food_11.blp" },
    { 6083,  "Kobold Candle",     "Interface\\Icons\\INV_Misc_Candle_01.blp" },
    { 7974,  "Murloc Eye",        "Interface\\Icons\\INV_Misc_MonsterScales_01.blp" },
    { 811,   "Stringy Wolf Meat", "Interface\\Icons\\INV_Misc_Food_52.blp" },
    { 2318,  "Wolf Pelt",         "Interface\\Icons\\INV_Misc_Pelt_Wolf_01.blp" },
    { 5504,  "Boar Tusk",         "Interface\\Icons\\INV_Misc_BoneTusk_02.blp" },
};
#define WOW_ITEM_DEF_COUNT (sizeof(wow_item_defs) / sizeof(wow_item_defs[0]))

#define WOW_LOOT_DROP_MAX 4
typedef struct { DWORD item_entry; DWORD chance_pct; DWORD min_qty, max_qty; } wowLootDrop_t;
typedef struct {
    DWORD display_id;
    DWORD copper_min, copper_max; /* rolled copper (added to player wallet on loot open) */
    wowLootDrop_t drops[WOW_LOOT_DROP_MAX];
    DWORD drop_count;
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

static wowItemDef_t const *Wow_ItemByEntry(DWORD entry) {
    FOR_LOOP(i, WOW_ITEM_DEF_COUNT)
        if (wow_item_defs[i].entry == entry) return &wow_item_defs[i];
    return NULL;
}

/* Roll loot for a freshly-killed creature; results stored on the corpse entity. */
void Wow_RollLoot(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    wowLootEntry_t const *tmpl = NULL;

    if (!local) return;
    local->loot_count = 0;
    local->loot_copper = 0;
    memset(local->loot_items, 0, sizeof(local->loot_items));

    FOR_LOOP(i, WOW_LOOT_TABLE_COUNT)
        if (wow_loot_table[i].display_id == local->display_id) { tmpl = &wow_loot_table[i]; break; }
    if (!tmpl) return;

    if (tmpl->copper_max > tmpl->copper_min)
        local->loot_copper = tmpl->copper_min + (DWORD)(rand() % (int)(tmpl->copper_max - tmpl->copper_min + 1));
    else
        local->loot_copper = tmpl->copper_min;

    FOR_LOOP(i, tmpl->drop_count) {
        wowLootDrop_t const *drop = &tmpl->drops[i];
        wowItemDef_t const *item = Wow_ItemByEntry(drop->item_entry);
        DWORD slot = local->loot_count;

        if (slot >= WOW_MAX_LOOT_ITEMS) break;
        if ((DWORD)(rand() % 100) >= drop->chance_pct || !item) continue;
        DWORD qty = drop->min_qty;
        if (drop->max_qty > drop->min_qty) qty += (DWORD)(rand() % (int)(drop->max_qty - drop->min_qty + 1));
        snprintf(local->loot_items[slot].icon, sizeof(local->loot_items[slot].icon), "%s", item->icon);
        snprintf(local->loot_items[slot].name, sizeof(local->loot_items[slot].name), "%s", item->name);
        local->loot_items[slot].count = qty;
        local->loot_count++;
    }
}

/* Find the nearest corpse entity within range that still has items to loot. */
LPEDICT Wow_FindNearestCorpse(LPEDICT ent, FLOAT range) {
    LPEDICT best = NULL;
    FLOAT best_dist2 = range * range;

    if (!ent) return NULL;
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts && i < WOW_MAX_EDICTS; i++) {
        LPEDICT c = &wow_edicts[i];
        wowEntityLocal_t *local;
        VECTOR2 delta;
        FLOAT dist2;

        if (!c->inuse || c->think != Wow_RunCorpseFrame) continue;
        local = Wow_EntityLocal(c);
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
    DWORD model;
    char name[64];
} wowMissingAnimationLog_t;

typedef struct {
    DWORD id;
    DWORD directory_offset;
    DWORD unused;
    DWORD title_offset;
} wowMapDbc_t;

static wowMissingAnimationLog_t wow_missing_animation_log[WOW_MISSING_ANIMATION_LOG_SLOTS];

static void Wow_LogMissingAnimation(LPEDICT ent, LPCSTR animation_name, BOOL invalid_interval) {
    DWORD model;

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

FLOAT Wow_Clamp(FLOAT value, FLOAT min_value, FLOAT max_value) {
    return MAX(min_value, MIN(value, max_value));
}

static LPCSTR Wow_PathBasename(LPCSTR path) {
    LPCSTR slash = strrchr(path, '/');
    LPCSTR backslash = strrchr(path, '\\');

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

static void Wow_MapNameFromPath(LPCSTR path, LPSTR out, DWORD out_size) {
    LPCSTR base;
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

typedef struct { DWORD id; LPCSTR path; } gLoadingScreenRec_t;
static stbDbcField_t const loading_screen_schema[] = {
    { 0, offsetof(gLoadingScreenRec_t, id),   STB_DBC_U32 },
    { 2, offsetof(gLoadingScreenRec_t, path), STB_DBC_STR },
};
static stbDbcCache_t loading_screen_dbc;

static BOOL Wow_ResolveLoadingScreenById(DWORD loading_screen_id, LPSTR out, DWORD out_size) {
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

static void Wow_SelectLoadingScreen(LPCSTR map_path) {
    LPBYTE data;
    DWORD size = 0;
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

    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\Map.dbc", &size) : NULL;
    if (!Stb_DbcValid(data, size, &h) || h.fields < 4 || h.record_size < sizeof(wowMapDbc_t)) {
        SAFE_DELETE(data, gi.MemFree);
        return;
    }

    BYTE const *records_base = Stb_DbcRecords(data);
    BYTE const *strings_base = Stb_DbcStrings(data, &h);
    FOR_LOOP(record_index, h.records) {
        BYTE const *record = records_base + record_index * h.record_size;
        wowMapDbc_t const *map = (wowMapDbc_t const *)record;
        LPCSTR map_dir = Stb_DbcString(strings_base, h.string_size, map->directory_offset);

        if (!map_dir || strcasecmp(map_dir, map_name)) {
            continue;
        }

        DWORD loading_screen_id = Stb_DbcRead32(record + (h.fields - 1) * sizeof(DWORD));
        LPCSTR map_title = Stb_DbcString(strings_base, h.string_size, map->title_offset);

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
typedef struct { DWORD id; LPCSTR filepath; } gSpellVisualEffectNameRec_t;
static stbDbcField_t const spell_visual_effect_name_schema[] = {
    { 0, offsetof(gSpellVisualEffectNameRec_t, id),       STB_DBC_U32 },
    { 2, offsetof(gSpellVisualEffectNameRec_t, filepath), STB_DBC_STR },
};

/* Kit effect slots (classic layout, validated against WoWee): head 3, chest 4,
 * base 5, left 6, right 7, breath 8, special 11-13. */
typedef struct { DWORD id, head, chest, base, left, right, breath, special[3]; } gSpellVisualKitRec_t;
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

typedef struct { DWORD id, precast_kit, cast_kit, impact_kit, missile_effect; } gSpellVisualRec_t;
static stbDbcField_t const spell_visual_schema[] = {
    { 0, offsetof(gSpellVisualRec_t, id),             STB_DBC_U32 },
    { 1, offsetof(gSpellVisualRec_t, precast_kit),    STB_DBC_U32 },
    { 2, offsetof(gSpellVisualRec_t, cast_kit),       STB_DBC_U32 },
    { 3, offsetof(gSpellVisualRec_t, impact_kit),     STB_DBC_U32 },
    { 8, offsetof(gSpellVisualRec_t, missile_effect), STB_DBC_U32 },
};

typedef struct {
    DWORD visual_id;
    LPCSTR cast_path;
    LPCSTR impact_path;
    LPCSTR missile_path;
} wowSpellVisual_t;

static wowSpellVisual_t wow_spell_visuals[WOW_MAX_SPELL_VISUALS];
static DWORD wow_spell_visual_count = 0;
static BOOL wow_spell_visuals_loaded = false;

/* Cached effect name paths: effect_name_id → M2 path */
typedef struct {
    DWORD id;
    LPCSTR path;
} wowSpellVisualEffect_t;

static wowSpellVisualEffect_t wow_visual_effects[WOW_MAX_SPELL_VISUAL_EFFECTS];
static DWORD wow_visual_effect_count = 0;

stbDbcIO_t const g_dbc_io = { G_DbcRead, G_DbcFreeFile, G_DbcAlloc, G_DbcFreeMem };

static LPCSTR Wow_FindVisualEffectPath(DWORD effect_id) {
    FOR_LOOP(i, wow_visual_effect_count) {
        if (wow_visual_effects[i].id == effect_id) {
            return wow_visual_effects[i].path;
        }
    }
    return NULL;
}

/* Resolve a kit ID to the best M2 path by probing effect slots.
 * WoWee probes: SpecialEffect0 > BaseEffect > LeftHand > RightHand > Chest > Head > Breath */
static LPCSTR Wow_ResolveKitPath(gSpellVisualKitRec_t const *kit) {
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
        DWORD eff_id = *(DWORD const *)((BYTE const *)kit + probe[i]);
        if (eff_id) {
            LPCSTR path = Wow_FindVisualEffectPath(eff_id);
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
            LPCSTR path = rec->filepath;
            if (rec->id && path && *path) {
                /* Convert .mdx/.mdl extensions to .m2 (WoWee pattern) */
                size_t len = strlen(path);
                char *buf = gi.MemAlloc ? gi.MemAlloc(len + 1) : NULL;
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

            LPCSTR cast_path = NULL, impact_path = NULL, missile_path = NULL;
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
static wowSpellVisual_t const *Wow_FindSpellVisual(DWORD visual_id) {
    FOR_LOOP(i, wow_spell_visual_count) {
        if (wow_spell_visuals[i].visual_id == visual_id) {
            return &wow_spell_visuals[i];
        }
    }
    return NULL;
}

FLOAT Wow_TerrainHeight(FLOAT x, FLOAT y) {
    return CM_GetHeightAtPoint(x, y);
}

FLOAT Wow_FloorHeight(FLOAT x, FLOAT y, FLOAT z) { return CM_WowFloorHeight(x, y, z, 1.5f); }

static FLOAT Wow_ViewPitch(FLOAT wrapped_pitch) {
    return wrapped_pitch > 180.0f ? 360.0f - wrapped_pitch : -wrapped_pitch;
}

static void Wow_AngleVectors(FLOAT yaw, LPVECTOR2 forward, LPVECTOR2 right) {
    FLOAT angle = (FLOAT)DEG2RAD(yaw);
    FLOAT sy = sinf(angle);
    FLOAT cy = cosf(angle);

    if (forward) {
        forward->x = cy;
        forward->y = sy;
    }
    if (right) {
        right->x = sy;
        right->y = -cy;
    }
}

DWORD Wow_EntityIndex(LPCEDICT ent) {
    if (!ent || ent < wow_edicts || ent >= wow_edicts + WOW_MAX_EDICTS) {
        return WOW_MAX_EDICTS;
    }
    return (DWORD)(ent - wow_edicts);
}

wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent) {
    DWORD index = Wow_EntityIndex(ent);

    if (index >= WOW_MAX_EDICTS) {
        return NULL;
    }
    return &wow_entity_locals[index];
}

LPCANIMATION Wow_SetEntityAnimation(LPEDICT ent, LPCSTR animation_name) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPCANIMATION anim;

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

BOOL Wow_SetEntityMoveFirstAnimation(LPEDICT ent, LPWOWMOVE move, LPCSTR const *animation_names) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!ent || !local || !move) {
        return false;
    }
    if (local->currentmove == move && local->animation) {
        return true;
    }
    for (LPCSTR const *name = animation_names; name && *name; name++) {
        if (Wow_SetEntityAnimation(ent, *name)) {
            local->currentmove = move;
            return true;
        }
    }
    local->currentmove = NULL;
    return false;
}

BOOL Wow_SetEntityMove(LPEDICT ent, LPWOWMOVE move) {
    LPCSTR names[2];

    if (!move || !move->animation) {
        return false;
    }
    names[0] = move->animation;
    names[1] = NULL;
    return Wow_SetEntityMoveFirstAnimation(ent, move, names);
}

void Wow_AdvanceEntityFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    DWORD next_frame;

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
static LPEDICT Wow_EdictByNumber(DWORD number);
static LPEDICT Wow_FindNearestAttackTarget(LPEDICT ent, FLOAT range);

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
#define WOW_GCD_MS              1500  /* 1.5s global cooldown */
#define WOW_MANA_MAX            100
#define WOW_MANA_REGEN_PER_SEC  3

/* Spell definition table: each spell is a row with function pointers.
 * Pattern follows Quake 2's gitem_t itemlist[] — data-driven, no enum switch. */

static void Wow_SpellAttack(LPEDICT caster, LPEDICT target) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (cl) cl->enemy = target;
    if (caster->attack) caster->attack(caster);
}
static void Wow_SpellFireball(LPEDICT caster, LPEDICT target) { Wow_FireFirebolt(caster, target); }
static void Wow_SpellFrostbolt(LPEDICT caster, LPEDICT target) { Wow_FireFrostbolt(caster, target); }
static void Wow_SpellHealingTouch(LPEDICT caster, LPEDICT target) { (void)target; Wow_HealingTouch(caster); }

wowSpellDef_t const wow_spells[] = {
    [WOW_SPELL_ATTACK]        = { "Attack",        Wow_SpellAttack,           0,    0,  5.0f, NULL,                NULL,               0   },
    [WOW_SPELL_FIREBOLT]      = { "Fireball",      Wow_SpellFireball,      1500,   10, 30.0f, "SpellCastDirected", "ReadySpellDirected", 133 },
    [WOW_SPELL_FROSTBOLT]     = { "Frostbolt",     Wow_SpellFrostbolt,     2500,   15, 30.0f, "SpellCastDirected", "ReadySpellDirected", 116 },
    [WOW_SPELL_HEALING_TOUCH] = { "Healing Touch", Wow_SpellHealingTouch,      0,   15,  0.0f, NULL,                NULL,               0   },
};
DWORD const wow_spell_count = sizeof(wow_spells) / sizeof(wow_spells[0]);

/* SPELL_NONE / SPELL_FIREBOLT etc. defined in g_wow_local.h */

/* Spell.dbc: maps spell_dbc_id → SpellVisual ID.
 * Classic 1.12 layout: 148 fields, SpellVisualID at field 115. */
#define WOW_MAX_SPELL_VISUAL_MAP 256
static DWORD wow_spell_visual_map[WOW_MAX_SPELL_VISUAL_MAP]; /* index = spell_dbc_id, value = visual_id */
static BOOL wow_spell_dbc_loaded = false;

static void Wow_LoadSpellDbc(void) {
    LPBYTE data = NULL;
    DWORD size = 0;
    stbDbc_t h;
    DWORD visual_field;

    if (wow_spell_dbc_loaded) return;
    wow_spell_dbc_loaded = true;
    memset(wow_spell_visual_map, 0, sizeof(wow_spell_visual_map));

    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\Spell.dbc", &size) : NULL;
    if (!Stb_DbcValid(data, size, &h)) {
        SAFE_DELETE(data, gi.MemFree);
        return;
    }
    /* Classic Spell.dbc has 148 fields with SpellVisualID at 115.
     * WotLK has 234 fields with SpellVisual at 131. Pick the right field. */
    visual_field = h.fields >= 200 ? 131 : 115;
    if (visual_field >= h.fields) { SAFE_DELETE(data, gi.MemFree); return; }

    {
        BYTE const *records_base = Stb_DbcRecords(data);
        FOR_LOOP(i, h.records) {
            BYTE const *record = records_base + i * h.record_size;
            DWORD id = Stb_DbcRead32(record);
            if (id < WOW_MAX_SPELL_VISUAL_MAP) {
                wow_spell_visual_map[id] = Stb_DbcRead32(record + visual_field * sizeof(DWORD));
            }
        }
    }
    fprintf(stderr, "WoW: loaded %u spells from Spell.dbc\n", (unsigned)h.records);
    Wow_LoadSpellVisualDbcs();
    SAFE_DELETE(data, gi.MemFree);
}

/* Resolve a spell's DBC spell ID → SpellVisual ID → missile M2 model.
 * Returns 0 if no DBC data is available. */
DWORD Wow_SpellMissileModel(DWORD spell_dbc_id) {
    static DWORD resolved_models[WOW_MAX_SPELL_VISUAL_MAP];
    static BOOL model_resolved[WOW_MAX_SPELL_VISUAL_MAP];

    if (spell_dbc_id == 0 || spell_dbc_id >= WOW_MAX_SPELL_VISUAL_MAP) return 0;
    if (model_resolved[spell_dbc_id]) return resolved_models[spell_dbc_id];
    model_resolved[spell_dbc_id] = true;

    if (!wow_spell_dbc_loaded) Wow_LoadSpellDbc();
    DWORD visual_id = wow_spell_visual_map[spell_dbc_id];
    if (!visual_id) return 0;

    wowSpellVisual_t const *sv = Wow_FindSpellVisual(visual_id);
    if (!sv || !sv->missile_path) return 0;

    DWORD sz;
    HANDLE buf = gi.ReadFile ? gi.ReadFile(sv->missile_path, &sz) : NULL;
    if (!buf) return 0;
    resolved_models[spell_dbc_id] = G_RegisterModel(sv->missile_path);
    gi.MemFree(buf);
    fprintf(stderr, "WoW: DBC missile model for spell %u: %s (idx %u)\n", (unsigned)spell_dbc_id, sv->missile_path, (unsigned)resolved_models[spell_dbc_id]);
    return resolved_models[spell_dbc_id];
}

/* Resolve a spell's DBC spell ID → SpellVisual ID → impact M2 model configstring index. */
DWORD Wow_SpellImpactModel(DWORD spell_dbc_id) {
    static DWORD resolved_impacts[WOW_MAX_SPELL_VISUAL_MAP];
    static BOOL impact_resolved[WOW_MAX_SPELL_VISUAL_MAP];

    if (spell_dbc_id == 0 || spell_dbc_id >= WOW_MAX_SPELL_VISUAL_MAP) return 0;
    if (impact_resolved[spell_dbc_id]) return resolved_impacts[spell_dbc_id];
    impact_resolved[spell_dbc_id] = true;

    if (!wow_spell_dbc_loaded) Wow_LoadSpellDbc();
    DWORD visual_id = wow_spell_visual_map[spell_dbc_id];
    if (!visual_id) return 0;

    wowSpellVisual_t const *sv = Wow_FindSpellVisual(visual_id);
    if (!sv || !sv->impact_path) return 0;

    DWORD sz;
    HANDLE buf = gi.ReadFile ? gi.ReadFile(sv->impact_path, &sz) : NULL;
    if (!buf) return 0;
    resolved_impacts[spell_dbc_id] = gi.ModelIndex(sv->impact_path);
    gi.MemFree(buf);
    fprintf(stderr, "WoW: DBC impact model for spell %u: %s (idx %u)\n", (unsigned)spell_dbc_id, sv->impact_path, (unsigned)resolved_impacts[spell_dbc_id]);
    return resolved_impacts[spell_dbc_id];
}

DWORD Wow_FireboltModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
        /* Try DBC-resolved missile path first, fall back to hardcoded paths. */
        DWORD dbc_model = Wow_SpellMissileModel(133);
        if (dbc_model) { model = dbc_model; return model; }
        LPCSTR const paths[] = {
            "Spells\\Fireball_Missile_High.m2",
            "Spells\\Fireball_Missile_Low.m2",
            "Spells\\FireBolt_Missile_Low.m2",
            "Spells\\FireShot_Missile.m2",
            NULL
        };
        for (LPCSTR const *p = paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
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

static void Wow_BeginSpellCast(LPEDICT caster, DWORD spell_id, DWORD target_num) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    LPEDICT target = Wow_EdictByNumber(target_num);
    if (!cl || spell_id >= wow_spell_count) return;
    wowSpellDef_t const *def = &wow_spells[spell_id];
    cl->attack_damage_time = 0; cl->attack_backswing_time = 0; cl->attack_time = 0;
    cl->cast_spell     = spell_id;
    cl->cast_duration  = def->cast_time;
    cl->cast_remaining = def->cast_time;
    cl->cast_target    = target_num;
    cl->cast_origin    = (VECTOR2){ caster->s.origin.x, caster->s.origin.y };
    cl->cast_release_time = 0;
    if (def->ready_anim) {
        LPCSTR anim_names[] = { def->ready_anim, NULL };
        wowMove_t ready_move = { def->ready_anim, NULL, NULL };
        Wow_SetEntityMoveFirstAnimation(caster, &ready_move, anim_names);
    }
    if (target) Wow_FaceTarget(caster, target);
    cl->gcd_time = WOW_GCD_MS;
}

static void Wow_CancelSpellCast(LPEDICT caster) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (!cl) return;
    cl->cast_spell = SPELL_NONE;
    cl->cast_duration = cl->cast_remaining = cl->cast_target = cl->cast_release_time = 0;
    Wow_SetStandMove(caster);
    /* Mana is NOT consumed on cancel; movement/interrupt refunds the cost */
}

static void Wow_CompleteSpellCast(LPEDICT caster) {
    wowEntityLocal_t *cl = Wow_EntityLocal(caster);
    if (!cl || cl->cast_spell == SPELL_NONE) return;
    LPEDICT target = Wow_EdictByNumber(cl->cast_target);
    DWORD spell = cl->cast_spell;
    cl->cast_spell = SPELL_NONE;
    cl->cast_duration = cl->cast_remaining = 0;
    /* The projectile launches at cast completion while the character plays the non-looping release sequence. */
    {
        static LPCSTR const release_anims[] = { "SpellCastDirected", "SpellCastOmni", "Spell", NULL };
        if (Wow_SetEntityMoveFirstAnimation(caster, &wow_move_cast, release_anims) && cl->animation)
            cl->cast_release_time = cl->animation->interval[1] - cl->animation->interval[0];
    }
    /* Mana consumed on completion; no cost if interrupted or cancelled */
    if (spell < wow_spell_count) {
        wowSpellDef_t const *def = &wow_spells[spell];
        if (def->mana_cost)
            cl->mana = cl->mana >= def->mana_cost ? cl->mana - def->mana_cost : 0;
        if (def->cast && target && target->inuse)
            def->cast(caster, target);
        else if (!target || def->range == 0.0f)
            def->cast(caster, NULL);  /* self-targeted or instant */
    }
    cl->cast_target = 0;
}

/* Per-frame cast progress. Returns TRUE while entity is casting (locked). */
static BOOL Wow_RunSpellCast(LPEDICT ent) {
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
        LPEDICT target = Wow_EdictByNumber(cl->cast_target);
        wowEntityLocal_t *target_local = target ? Wow_EntityLocal(target) : NULL;
        if (!target || !target->inuse || (target_local && target_local->dead)) {
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
void Wow_RunProjectile(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPEDICT target;

    if (!ent || !local || ent->think != Wow_RunProjectile || !ent->inuse) {
        return;
    }
    target = Wow_EdictByNumber(local->projectile_target);
    if (!target || !target->inuse) {
        ent->inuse = false;
        return;
    }
    {
        VECTOR2 const t2 = (VECTOR2){ target->s.origin.x, target->s.origin.y };
        VECTOR2 const p2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
        VECTOR2 delta = Vector2_sub(&t2, &p2);
        FLOAT dist = sqrtf(delta.x * delta.x + delta.y * delta.y);
        FLOAT step = local->projectile_speed * ((FLOAT)FRAMETIME / 1000.0f);

        if (dist <= step) {
            /* Hit the target — delegate damage to the shared combat path (Q2 T_Damage analog). */
            LPEDICT caster = Wow_EdictByNumber(local->projectile_caster);
            wowEntityLocal_t *target_local = Wow_EntityLocal(target);
            Wow_ApplyDamage(target, caster, local->projectile_damage);
            /* slow_timer on the projectile encodes the debuff duration to apply. */
            if (target_local && !target_local->dead && local->slow_timer > 0)
                target_local->slow_timer = MAX(target_local->slow_timer, local->slow_timer);
            /* Broadcast a client-side impact effect to all nearby observers. */
            {
                BOOL is_frost = local->slow_timer > 0;
                int impact_model = is_frost ? wow_frostbolt_impact_model : wow_firebolt_impact_model;
                tempEvent_t te = is_frost ? TE_FROSTBOLT_IMPACT : TE_FIREBOLT_IMPACT;
                if (impact_model > 0) {
                    gi.Write(PF_BYTE, &(LONG){ svc_temp_entity });
                    gi.Write(PF_BYTE, &(LONG){ te });
                    gi.Write(PF_POSITION, &ent->s.origin);
                    gi.Write(PF_SHORT, &(LONG){ impact_model });
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
            FLOAT target_chest_z = G_GetAttachmentZ(target->s.model, 20);
            /* WoW attachment 20 is the chest; server hit testing remains independent of renderer bones. */
            if (target_chest_z <= 0) target_chest_z = target->s.radius * 2.0f;
            FLOAT target_z = target->s.origin.z + target_chest_z * target->s.scale;
            ent->s.origin.z += (target_z - ent->s.origin.z) * step / dist;
        }
        local->projectile_yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
        ent->s.angle = (FLOAT)DEG2RAD(local->projectile_yaw);
    }
}

void Wow_FireFirebolt(LPEDICT caster, LPEDICT target) {
    wowEntityLocal_t *caster_local;
    wowEntityLocal_t *pl;
    LPEDICT proj;
    FLOAT yaw;

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

    proj->think = Wow_RunProjectile;
    {
        VECTOR2 delta = Vector2_sub(&(VECTOR2){ target->s.origin.x, target->s.origin.y }, &(VECTOR2){ caster->s.origin.x, caster->s.origin.y });
        yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
    }
    pl->projectile_target = target->s.number;
    pl->projectile_caster = caster->s.number;
    pl->projectile_speed  = WOW_FIREBOLT_SPEED;
    pl->projectile_damage = WOW_FIREBOLT_DAMAGE;
    pl->projectile_yaw = yaw;
    pl->projectile_pitch = 0.0f;
    {
        FLOAT hand_z = G_GetAttachmentZ(caster->s.model, 1);
        /* TODO: the renderer must eventually seed the visual from M2_AttachmentMatrix at the release frame. */
        if (hand_z <= 0) hand_z = caster->s.radius;
        proj->s.origin.z = caster->s.origin.z + hand_z * caster->s.scale;
    }

    proj->s.origin.x = caster->s.origin.x;
    proj->s.origin.y = caster->s.origin.y;
    proj->s.origin2 = (VECTOR2){ proj->s.origin.x, proj->s.origin.y };
    proj->s.angle  = (FLOAT)DEG2RAD(yaw);
    proj->s.model  = Wow_FireboltModel();
    proj->s.scale  = 0.8f;
    proj->s.radius = 0.5f;
    proj->s.player = caster->s.player;
    /* EF_GROUND_ANCHOR routes the renderer through the grounded-actor matrix path
     * (yaw-only around Z), which is correct for spell projectiles.  Without it
     * R_GameEntityMatrix applies the doodad Euler angles (rotation.y-90, rotation.z-90)
     * to a zero-rotation entity, which lifts the mesh far above the origin. */
    proj->s.flags  = EF_GROUND_ANCHOR;
    /* Ranged spells use the selected target, not the melee combat target; leaving
     * this field set would make the generic frame loop chase after launch. */
    caster_local->enemy = NULL;
}

DWORD Wow_FrostboltModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
        /* Try DBC-resolved missile path first, fall back to hardcoded paths. */
        DWORD dbc_model = Wow_SpellMissileModel(116);
        if (dbc_model) { model = dbc_model; return model; }
        LPCSTR const paths[] = {
            "Spells\\FrostBolt_Missile_Low.m2",
            "Spells\\Frostbolt_Missile.m2",
            "Spells\\FrostShot_Missile.m2",
            NULL
        };
        for (LPCSTR const *p = paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
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
DWORD Wow_FireboltImpactModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
        DWORD dbc_model = Wow_SpellImpactModel(133);
        if (dbc_model) { model = dbc_model; return model; }
        LPCSTR const paths[] = {
            "Spells\\FireBolt_ImpactDD_Med_Chest.m2",
            "Spells\\Fire_ImpactDD_Med_Chest.m2",
            NULL
        };
        for (LPCSTR const *p = paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
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
DWORD Wow_FrostboltImpactModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
        DWORD dbc_model = Wow_SpellImpactModel(116);
        if (dbc_model) { model = dbc_model; return model; }
        LPCSTR const paths[] = {
            "Spells\\Ice_ImpactDD_Med_Chest.m2",
            "Spells\\Ice_ImpactDD_Low_Chest.m2",
            NULL
        };
        for (LPCSTR const *p = paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
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
void Wow_FireFrostbolt(LPEDICT caster, LPEDICT target) {
    wowEntityLocal_t *caster_local, *pl;
    LPEDICT proj;
    FLOAT yaw;

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

    proj->think = Wow_RunProjectile;
    {
        VECTOR2 delta = Vector2_sub(&(VECTOR2){ target->s.origin.x, target->s.origin.y }, &(VECTOR2){ caster->s.origin.x, caster->s.origin.y });
        yaw = (FLOAT)RAD2DEG(atan2f(delta.y, delta.x));
    }
    pl->projectile_target = target->s.number;
    pl->projectile_caster = caster->s.number;
    pl->projectile_speed  = WOW_FROSTBOLT_SPEED;
    pl->projectile_damage = WOW_FROSTBOLT_DAMAGE;
    pl->projectile_yaw    = yaw;
    pl->projectile_pitch  = 0.0f;
    {
        FLOAT hand_z = G_GetAttachmentZ(caster->s.model, 1);
        /* TODO: the renderer must eventually seed the visual from M2_AttachmentMatrix at the release frame. */
        if (hand_z <= 0) hand_z = caster->s.radius;
        proj->s.origin.z = caster->s.origin.z + hand_z * caster->s.scale;
    }
    /* Reuse slow_timer field to signal that this projectile applies a slow on hit.
     * A non-zero value in the projectile local means "apply slow on impact". */
    pl->slow_timer = WOW_FROSTBOLT_SLOW_MS;

    proj->s.origin.x = caster->s.origin.x;
    proj->s.origin.y = caster->s.origin.y;
    proj->s.origin2 = (VECTOR2){ proj->s.origin.x, proj->s.origin.y };
    proj->s.angle   = (FLOAT)DEG2RAD(yaw);
    proj->s.model   = Wow_FrostboltModel();
    proj->s.scale   = 0.8f;
    proj->s.radius  = 0.5f;
    proj->s.player  = caster->s.player;
    proj->s.flags   = EF_GROUND_ANCHOR; /* see Wow_FireFirebolt for rationale */
    /* Ranged spells use the selected target, not the melee combat target; leaving
     * this field set would make the generic frame loop chase after launch. */
    caster_local->enemy = NULL;
}

void Wow_HealingTouch(LPEDICT caster) {
    wowEntityLocal_t *local;

    if (!caster) return;
    local = Wow_EntityLocal(caster);
    if (!local || local->dead) return;

    if (local->mana < WOW_HEALING_TOUCH_MANA_COST) return;
    local->mana -= WOW_HEALING_TOUCH_MANA_COST;
    local->health = MIN(local->health + WOW_HEALING_TOUCH_HEAL, 100);
    /* Play a cast animation if available. */
    static LPCSTR const heal_anims[] = { "SpellCastOmni", "Cast", "Attack1H", NULL };
    Wow_SetEntityMoveFirstAnimation(caster, &wow_move_cast, heal_anims);
}

/* Find a target in range for the firebolt spell.  Prefers current selection,
   then the current melee enemy, then nearest enemy. */
LPEDICT Wow_FindSpellTarget(LPEDICT ent, FLOAT range) {
    if (ent && ent->client && ent->client->ps.selected_entity) {
        LPEDICT t = Wow_EdictByNumber(ent->client->ps.selected_entity);
        if (t && t != ent && t->inuse) {
            VECTOR2 delta = Vector2_sub(&t->s.origin2, &ent->s.origin2);
            if (sqrtf(delta.x * delta.x + delta.y * delta.y) <= range) {
                return t;
            }
        }
    }
    {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);
        if (local && local->enemy && local->enemy != ent && local->enemy->inuse) {
            VECTOR2 delta = Vector2_sub(&local->enemy->s.origin2, &ent->s.origin2);
            if (sqrtf(delta.x * delta.x + delta.y * delta.y) <= range) {
                return local->enemy;
            }
        }
    }
    return Wow_FindNearestAttackTarget(ent, range);
}

static void Wow_UpdateCamera(LPEDICT ent) {
    if (!ent || !ent->client) {
        return;
    }
    ent->client->ps.origin = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
    ent->client->ps.viewangles = (VECTOR3){ Wow_ViewPitch(wow_move.pitch), wow_move.yaw, 0.0f };
    ent->client->ps.viewquat = Quaternion_fromEuler(&MAKE(VECTOR3, wow_move.pitch, 0.0f, wow_move.yaw), ROTATE_ZYX);
    ent->client->ps.fov = 45.0f;
    ent->client->ps.distance = wow_move.distance;
}

static void Wow_UpdatePlayerHud(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPPLAYER ps;

    if (!ent || !ent->client || !local) {
        return;
    }
    ps = &ent->client->ps;
    ps->stats[WOW_STAT_HEALTH] = (USHORT)local->health;
    ps->stats[WOW_STAT_HEALTH_MAX] = 100;
    ps->stats[WOW_STAT_POWER] = (USHORT)local->mana;
    ps->stats[WOW_STAT_POWER_MAX] = WOW_MANA_MAX;
    ps->stats[WOW_STAT_LEVEL] = 1;
    ps->stats[WOW_STAT_XP] = 120;
    ps->stats[WOW_STAT_XP_MAX] = 400;
    ps->stats[WOW_STAT_COPPER] = (USHORT)MIN(local->copper, 0xFFFFu);
    /* Cast progress: remaining ms and total ms for client-side cast bar */
    ps->stats[WOW_STAT_CAST_PROGRESS] = (USHORT)(local->cast_spell != SPELL_NONE ? local->cast_remaining : 0);
    ps->stats[WOW_STAT_CAST_MAX] = (USHORT)(local->cast_spell != SPELL_NONE ? local->cast_duration : 0);
    ps->stats[WOW_STAT_SELECTED_ACTION] = (USHORT)local->selected_action_slot;
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

static void Wow_WriteHudIcon(wowHudIcon_t const *icon, DWORD slot) {
    char command[64];
    char count[32];

    snprintf(command, sizeof(command), "wow_action %u", (unsigned)slot);
    snprintf(count, sizeof(count), "%u", (unsigned)icon->count);
    gi.Write(PF_STRING, icon->icon);
    gi.Write(PF_STRING, icon->name);
    gi.Write(PF_STRING, count);
    gi.Write(PF_STRING, command);
    gi.Write(PF_BYTE, &(LONG){ slot < 9 ? '1' + (LONG)slot : slot == 9 ? '0' : 0 });
}

static void Wow_WriteInventoryIcon(wowHudIcon_t const *icon, DWORD slot) {
    char count[32];

    snprintf(count, sizeof(count), "%u", (unsigned)icon->count);
    gi.Write(PF_STRING, icon->icon);
    gi.Write(PF_STRING, icon->name);
    gi.Write(PF_STRING, count);
    gi.Write(PF_BYTE, &(LONG){ slot });
}

static void Wow_SendPlayerUi(LPEDICT ent) {
    wowClient_t *client = &wow_clients[0];

    if (!ent || !gi.Write || !gi.unicast) {
        return;
    }
    gi.Write(PF_BYTE, &(LONG){ svc_unit_ui });
    gi.Write(PF_BYTE, &(LONG){ 1 });
    gi.Write(PF_SHORT, &(LONG){ ent->s.number });
    gi.Write(PF_BYTE, &(LONG){ WOW_UI_ACTION_SLOTS });
    FOR_LOOP(slot, WOW_UI_ACTION_SLOTS) {
        Wow_WriteHudIcon(&client->actions[slot], slot);
    }
    gi.Write(PF_BYTE, &(LONG){ WOW_UI_INVENTORY_SLOTS });
    FOR_LOOP(slot, WOW_UI_INVENTORY_SLOTS) {
        Wow_WriteInventoryIcon(&client->inventory[slot], slot);
    }
    gi.Write(PF_BYTE, &(LONG){ 0 });
    gi.unicast(ent);
}

static void Wow_MovePlayerFrame(LPEDICT ent) {
    Wow_AdvanceEntityFrame(ent);
}

static LPEDICT Wow_EdictByNumber(DWORD number) {
    if (number >= (DWORD)globals.num_edicts || number >= WOW_MAX_EDICTS) {
        return NULL;
    }
    if (!wow_edicts[number].inuse) {
        return NULL;
    }
    return &wow_edicts[number];
}

static LPEDICT Wow_FindNearestAttackTarget(LPEDICT ent, FLOAT range) {
    LPEDICT best = NULL;
    FLOAT best_dist2 = range * range;

    if (!ent) {
        return NULL;
    }

    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts && i < WOW_MAX_EDICTS; i++) {
        LPEDICT candidate = &wow_edicts[i];
        VECTOR2 delta;
        FLOAT dist2;

        if (!candidate->inuse || candidate == ent || !(candidate->svflags & SVF_MONSTER)) {
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

LPEDICT Wow_Spawn(void) {
    LPEDICT ent = NULL;
    DWORD index;

    if (wow_spawns_this_frame >= WOW_MAX_SPAWNS_PER_FRAME)
        return NULL;
    wow_spawns_this_frame++;

    if (globals.num_edicts < globals.max_edicts) {
        index = globals.num_edicts++;
        ent = &wow_edicts[index];
    } else {
        for (index = WOW_MAX_CLIENTS; index < WOW_MAX_EDICTS; index++) {
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
    ent->s.number = index;
    return ent;
}

/* Quake-style userinfo parser: find value for key in "\key\value\key\value" string.
   Returns pointer to a static buffer with the null-terminated value, or fallback
   if key not found.  Two rotating buffers so two calls don't stomp each other
   (same pattern as Q3 Info_ValueForKey in q_shared.c). */
static LPCSTR Wow_InfoValueForKey(LPCSTR str, LPCSTR key, LPCSTR fallback) {
    static char value[2][MAX_PATHLEN];
    static int valueindex = 0;
    char pkey[64];
    LPCSTR s = str;
    char *o;

    if (!s || !key || !*key)
        return fallback;

    valueindex ^= 1;
    if (*s == '\\')
        s++;
    while (1) {
        o = pkey;
        while (*s != '\\') {
            if (!*s)
                return fallback;
            *o++ = *s++;
        }
        *o = 0;
        s++;

        o = value[valueindex];
        while (*s != '\\' && *s)
            *o++ = *s++;
        *o = 0;

        if (!strcasecmp(key, pkey))
            return value[valueindex];

        if (!*s)
            break;
        s++;
    }
    return fallback;
}

/* Read selected character data from the single userinfo-style cvar set by the
   UI.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCvars(char *race, size_t race_sz, char *sex, size_t sex_sz, DWORD *class_out, DWORD *appearance_out) {
    LPCSTR val;

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    *class_out = WOW_CLASS_WARRIOR;
    *appearance_out = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);

    val = gi.CvarString(WOW_CVAR_PLAYERINFO, "");
    if (val[0]) {
        LPCSTR v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) *class_out = (DWORD)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) *appearance_out = (DWORD)strtoul(v, NULL, 10);
    }
}

/* Map selection happens before LoadMap, but remains authored by the server playercreateinfo table. */
static DWORD Wow_SelectedPlayerCreateMap(void) {
    char race[64], sex[64];
    DWORD class_id, appearance;

    if (!gi.CvarString(WOW_CVAR_PLAYERINFO, "")[0])
        return ~0u;
    Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
    return Wow_PlayerCreateMap(race, class_id);
}

/* Read the selected character's race/sex from the CS_GENERAL configstring for
   server-authored UI (unit-frame portrait).  Fallback matches Wow_InitPlayer. */
void Wow_GetPlayerRaceSex(char *race, size_t race_sz, char *sex, size_t sex_sz) {
    LPCSTR val = gi.GetConfigstring(CS_GENERAL + WOW_CS_PLAYERINFO);

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    if (val && val[0]) {
        LPCSTR v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
    }
}

/* Read selected character data from the single CS_GENERAL configstring set by
   Wow_Init.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCS(char *race, size_t race_sz, char *sex, size_t sex_sz, DWORD *class_out, DWORD *appearance_out) {
    LPCSTR val;

    snprintf(race, race_sz, "Orc");
    snprintf(sex, sex_sz, "Male");
    *class_out = WOW_CLASS_WARRIOR;
    *appearance_out = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);

    val = gi.GetConfigstring(CS_GENERAL + WOW_CS_PLAYERINFO);
    if (val && val[0]) {
        LPCSTR v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) snprintf(race, race_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) snprintf(sex, sex_sz, "%s", v);
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) *class_out = (DWORD)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) *appearance_out = (DWORD)strtoul(v, NULL, 10);
    }
}

/* Server-authored UI resolves class-sensitive quest text from playerinfo. */
DWORD Wow_GetPlayerClass(void) {
    char race[64], sex[64]; DWORD class_id, appearance;
    Wow_ReadSelectedCharFromCS(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
    return class_id;
}

static void Wow_InitPlayer(LPEDICT ent, VECTOR2 spawn_origin, LONG spawn_location) {
    LPPLAYER ps;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    FLOAT height = Wow_TerrainHeight(spawn_origin.x, spawn_origin.y);
    char race[64], sex[64];
    DWORD class_id, appearance;
    char model_path[MAX_PATHLEN * 2];

    /* Read selected character from CS_GENERAL configstrings (set by Wow_Init from cvars). */
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
    ent->s.origin = (VECTOR3){ spawn_origin.x, spawn_origin.y, height };
    ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
    ent->s.angle = (FLOAT)DEG2RAD(wow_move.yaw);
    ent->s.scale = 1.0f;
    ent->s.radius = 1.0f;
    ent->s.flags = EF_GROUND_ANCHOR;
    ent->idle = Wow_AIIdle;
    ent->move = NULL;
    ent->attack = Wow_AIAttack;
    ent->pain = Wow_AIPain;
    Wow_SetStandMove(ent);

    ps = &ent->client->ps;
    memset(ps, 0, sizeof(*ps));
    ps->number = 0;
    ps->start_location = spawn_location;
    snprintf(wow_clients[0].name, sizeof(wow_clients[0].name), "%s", "Thrall");
    {
        wowHudIcon_t const *actions = (class_id == WOW_CLASS_MAGE)
            ? wow_actions_mage : wow_actions_warrior;
        memcpy(wow_clients[0].inventory, wow_start_inventory, sizeof(wow_start_inventory));
        memcpy(wow_clients[0].actions, actions, WOW_UI_ACTION_SLOTS * sizeof(actions[0]));
        Wow_EntityLocal(ent)->copper = 1234; /* starting copper balance */
        fprintf(stderr, "WoW: action bar initialized for class %u\n", (unsigned)class_id);
    }
#ifdef WOW
    ps->origin = spawn_origin;
    ps->viewangles = (VECTOR3){ Wow_ViewPitch(wow_move.pitch), wow_move.yaw, 0.0f };
    ps->viewquat = Quaternion_fromEuler(&MAKE(VECTOR3, wow_move.pitch, 0.0f, wow_move.yaw), ROTATE_ZYX);
    ps->fov = 45;
    ps->distance = wow_move.distance;
#else
    ps->origin = spawn_origin;
    ps->viewquat = Quaternion_fromEuler(&MAKE(VECTOR3, 326.0f, 0.0f, 0.0f), ROTATE_ZYX);
    ps->fov = 54;
    ps->distance = 250.0f;
#endif
    ps->client_ui_state = CLIENT_UI_LOADING;
    ps->name = wow_clients[0].name;
    ps->texts[PLAYERTEXT_MAP_TITLE] = wow_loading_title;
    ps->texts[PLAYERTEXT_MAP_PREVIEW] = wow_loading_texture;
    Wow_UpdatePlayerHud(ent);
}

static void Wow_Init(void) {
    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    memset(wow_clients, 0, sizeof(wow_clients));

    globals.edicts = wow_edicts;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = WOW_MAX_CLIENTS;
    globals.num_edicts = WOW_MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);
}

static void Wow_Shutdown(void) {
    G_FreeModels();
    globals.edicts = NULL;
    globals.num_edicts = 0;
}

static bool Wow_SpawnEntities(void);

static bool Wow_LoadMap(LPCSTR mapFilename) {
    /* "preview" pseudo-map: no collision, no spawns, black background.
     * The renderer logs a missing WDT and draws nothing for the world.
     * Use as: make run-wow-preview  then type "quest <id>" in-game. */
    if (!strcmp(mapFilename, "preview")) {
        if (gi.ClearWorld) gi.ClearWorld();
        gi.configstring(CS_GENERAL + WOW_CS_PLAYERINFO,
            "\\race\\Human\\sex\\Male\\class\\2\\appearance\\0");
        Wow_SelectLoadingScreen("preview");
        Wow_InitPlayer(&wow_edicts[0], (VECTOR2){0, 0}, -1);
        return true;
    }
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    if (gi.ApplyLobbySettings) {
        gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    }
    if (gi.ClearWorld) {
        gi.ClearWorld();
    }
    return Wow_SpawnEntities();
}


static void Wow_ThinkUnit(LPEDICT ent) {
    wowEntityLocal_t *el = Wow_EntityLocal(ent);
    if (el && el->slow_timer > 0)
        el->slow_timer = el->slow_timer > FRAMETIME ? el->slow_timer - FRAMETIME : 0;
    Wow_RunCreatureFrame(ent);
}
static void Wow_ThinkProjectile(LPEDICT ent) { Wow_RunProjectile(ent); }
static void Wow_ThinkDynamicObject(LPEDICT ent) { Wow_RunDynamicObjectFrame(ent); }

/* Build the WDT path for a numeric map ID by scanning Map.dbc field 1 (directory).
 * Returns true and fills out on success; false when the DBC is absent or the ID
 * is not present.  Callers must provide a buffer of at least MAX_PATHLEN bytes. */
static BOOL Wow_WdtPathForMapId(DWORD map_id, LPSTR out, DWORD out_size) {
    LPBYTE data; DWORD size = 0; stbDbc_t h; BOOL found = false;
    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\Map.dbc", &size) : NULL;
    if (!Stb_DbcValid(data, size, &h) || h.fields < 2 || h.record_size < sizeof(wowMapDbc_t))
        { SAFE_DELETE(data, gi.MemFree); return false; }
    BYTE const *recs = Stb_DbcRecords(data), *strs = Stb_DbcStrings(data, &h);
    FOR_LOOP(i, h.records) {
        wowMapDbc_t const *m = (wowMapDbc_t const *)(recs + i * h.record_size);
        if (m->id != map_id) continue;
        LPCSTR dir = Stb_DbcString(strs, h.string_size, m->directory_offset);
        if (dir && *dir) { snprintf(out, out_size, "World\\Maps\\%s\\%s.wdt", dir, dir); found = true; }
        break;
    }
    gi.MemFree(data);
    return found;
}

/* Load AreaTrigger.dbc records for the current map into wow_area_trigs[].
 * Called at end of Wow_SpawnEntities so triggers are ready for RunFrame. */
static void Wow_LoadAreaTriggers(void) {
    LPBYTE data; DWORD size = 0, map_id; stbDbc_t h;
    wow_area_trig_count = 0;
    map_id = CM_WowGetMapId();
    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\AreaTrigger.dbc", &size) : NULL;
    /* AreaTrigger.dbc: 10 uint32/float fields, no string block — record_size == 40. */
    if (!Stb_DbcValid(data, size, &h) || h.fields != 10 || h.record_size != sizeof(WOWAREATRIG))
        { SAFE_DELETE(data, gi.MemFree); return; }
    BYTE const *base = Stb_DbcRecords(data);
    FOR_LOOP(i, h.records) {
        WOWAREATRIG const *t = (WOWAREATRIG const *)(base + i * h.record_size);
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
static void Wow_CheckAreaTriggers(LPEDICT ent) {
    char wdt[MAX_PATHLEN];
    if (wow_pending_teleport.pending || !wow_area_trig_count) return;
    FOR_LOOP(i, wow_area_trig_count) {
        LPCWOWAREATRIG t = &wow_area_trigs[i];
        LPCWOWAREATRIGTELEPORT dest = Wow_AreaTrigTeleportById(t->id);
        FLOAT dx = ent->s.origin.x - t->x, dy = ent->s.origin.y - t->y, dz = ent->s.origin.z - t->z;
        if (!dest) continue;
        if (t->radius > 0.0f) {
            if (dx*dx + dy*dy + dz*dz > t->radius * t->radius) continue;
        } else {
            FLOAT co = cosf(t->box_orientation), so = sinf(t->box_orientation);
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
static DWORD Wow_AnySpawnIndexForMap(DWORD map_id) {
    FOR_LOOP(i, Wow_SpawnCount()) {
        LPCWOWSPAWNPOINT sp = Wow_SpawnByIndex(i);
        if (sp && sp->map == map_id) return i;
    }
    return ~0u;
}

static bool Wow_SpawnEntities(void) {
    LPCMAPINFO mapinfo = CM_GetMapInfo();
    char race[64], sex[64];
    DWORD class_id, appearance, spawn_index;
    LONG spawn_location = -1;
    VECTOR2 spawn_origin = { 0.0f, 0.0f };
    char buf[MAX_PATHLEN];

    /* Read race before spawn selection so the player starts in their race's
       home zone (e.g. Orcs in Valley of Trials, not Northshire). */
    Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);

    if (wow_pending_teleport.pending) {
        /* Cross-map teleport: destination was saved by Wow_CheckAreaTriggers or warp command. */
        spawn_origin = (VECTOR2){ wow_pending_teleport.x, wow_pending_teleport.y };
        fprintf(stderr, "WoW: pending teleport → map=%u (%.1f %.1f)\n",
                (unsigned)CM_WowGetMapId(), spawn_origin.x, spawn_origin.y);
    } else {
        spawn_index = Wow_SelectSpawnPoint(race, class_id);
        if (spawn_index == ~0u) {
            DWORD map_id = CM_WowGetMapId();
            if (Wow_HasSpawnForMap(map_id)) {
                /* Race/class has no spawn on this map but other races do.
                 * Fall back to any available spawn — a deferred +warp will
                 * reposition the player; don't reject the whole map load. */
                DWORD fb = Wow_AnySpawnIndexForMap(map_id);
                fprintf(stderr, "WoW: race=%s class=%u has no spawn on map=%u; using fallback\n",
                        race, (unsigned)class_id, (unsigned)map_id);
                if (fb == ~0u) return false;
                LPCVECTOR3 fsp = Wow_GetSpawnPos(fb);
                if (fsp) { spawn_origin = (VECTOR2){ fsp->x, fsp->y }; spawn_location = (LONG)fb; }
            } else {
            /* No playercreateinfo for ANY race on this map — it's a dungeon/instance.
             * Fall back to the areatrigger_teleport destination for this map. */
            LPCWOWAREATRIGTELEPORT at = Wow_AreaTrigSpawnForMap(map_id);
            if (at) {
                wow_pending_teleport = (wowPendingTeleport_t){ true,
                    at->target_x, at->target_y, at->target_z, at->target_orientation };
                spawn_origin = (VECTOR2){ at->target_x, at->target_y };
                fprintf(stderr, "WoW: dungeon map=%u; using areatrigger spawn '%s'\n",
                        (unsigned)map_id, at->name);
            } else {
                fprintf(stderr, "WoW: no spawn for map=%u (no playercreateinfo, no areatrigger)\n",
                        (unsigned)map_id);
                return false;
            }
            } /* end else-dungeon */
        } else {
            LPCVECTOR3 sp = Wow_GetSpawnPos(spawn_index);
            if (sp) {
                spawn_origin = (VECTOR2){ sp->x, sp->y };
                spawn_location = (LONG)spawn_index;
                fprintf(stderr, "WoW: spawn race=%s at (%.1f %.1f)\n", race, sp->x, sp->y);
            }
        }
    }
    Wow_SelectLoadingScreen(mapinfo ? mapinfo->mapName : NULL);
    /* Re-populate the playerinfo configstring from cvars after SV_Map's
       memset cleared all configstrings (same pattern as Q3: game module
       re-sets configstrings after the server wipes them on map load). */
    snprintf(buf, sizeof(buf), "\\race\\%s\\sex\\%s\\class\\%u\\appearance\\%u", race, sex, (unsigned)class_id, (unsigned)appearance);
    gi.configstring(CS_GENERAL + WOW_CS_PLAYERINFO, buf);
    wow_move.flags = 0;
    wow_move.yaw = 0.0f;
    wow_move.pitch = 328.0f;
    wow_move.distance = 8.5f;
    wow_spawns_this_frame = 0;
    Wow_InitPlayer(&wow_edicts[0], spawn_origin, spawn_location);
    /* Apply authoritative z and orientation from pending teleport AFTER InitPlayer so
     * the SQL z overrides the terrain-height fallback used for dungeon interiors. */
    if (wow_pending_teleport.pending) {
        LPEDICT p = &wow_edicts[0];
        p->s.origin.z = wow_pending_teleport.z;
        p->s.angle    = wow_pending_teleport.orientation;
        wow_pending_teleport.pending = false;
    }
    globals.num_edicts = WOW_MAX_CLIENTS;
    Wow_SpawnAmbientCreatures(&spawn_origin);
    /* Initial world population is intentionally split into budgets so the
     * imported quest anchors do not starve ambient creatures or vice versa. */
    wow_spawns_this_frame = 0;
    Wow_SpawnQuestLocations(&spawn_origin);
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

static void Wow_RunFrame(void) {
    LPEDICT ent = &wow_edicts[0];
    VECTOR2 forward;
    VECTOR2 right;
    VECTOR2 dir = { 0.0f, 0.0f };
    FLOAT len;
    BOOL moving;
    BOOL locked;

    wow_spawns_this_frame = 0;

    if (!ent->inuse || !ent->client) {
        return;
    }

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
    ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
    if (moving) {
        FLOAT step = WOW_WALK_SPEED * ((FLOAT)FRAMETIME / 1000.0f) / len;
        ent->s.origin.x += dir.x * step;
        ent->s.origin.y += dir.y * step;
    }
    /* WMO floors, unlike ADT terrain, can sit above the outdoor ground inside buildings. */
    ent->s.origin.z = Wow_FloorHeight(ent->s.origin.x, ent->s.origin.y, ent->s.origin.z);
    Wow_CheckAreaTriggers(ent); /* check dungeon/zone portals after position is settled */
    /* Run spell cast state machine before entity lock check.
     * Cast animation plays via Wow_AdvanceEntityFrame; cooldowns tick down. */
    {
        wowEntityLocal_t *cl = Wow_EntityLocal(ent);
        if (cl && cl->gcd_time > 0)
            cl->gcd_time -= cl->gcd_time > FRAMETIME ? FRAMETIME : cl->gcd_time;
    }
    BOOL casting = Wow_RunSpellCast(ent);
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
    if (!locked && !moving && Wow_EntityAffectingCombat(ent)) {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);
        LPEDICT enemy = local->enemy;
        if (enemy) {
            VECTOR2 delta = Vector2_sub(&enemy->s.origin2, &ent->s.origin2);
            FLOAT dist = Vector2_len(&delta);
            if (dist > WOW_MELEE_RANGE) {
                FLOAT step = MIN(WOW_WALK_SPEED * ((FLOAT)FRAMETIME / 1000.0f), dist - WOW_MELEE_RANGE);
                ent->s.origin.x += delta.x * step / dist;
                ent->s.origin.y += delta.y * step / dist;
                ent->s.origin2 = (VECTOR2){ ent->s.origin.x, ent->s.origin.y };
                moving = true;
            }
        }
    }
    if (!locked && Wow_EntityAffectingCombat(ent)) {
        ent->attack(ent);
        /* If the attack started, treat as locked so the Run animation below
         * doesn't overwrite the swing. */
        {
            wowEntityLocal_t *l = Wow_EntityLocal(ent);
            if (l && (l->attack_damage_time > 0 || l->attack_backswing_time > 0))
                locked = true;
        }
    }
    if (locked) {
        Wow_UpdateCamera(ent);
    } else if (moving
        ? Wow_SetDirectionalMove(ent, wow_move.flags)
        : (Wow_EntityAffectingCombat(ent)
            ? Wow_SetCombatReadyAnimation(ent)
            : Wow_SetStandMove(ent))) {
        ent->s.angle = (FLOAT)DEG2RAD(wow_move.yaw);
        Wow_MovePlayerFrame(ent);
        Wow_UpdateCamera(ent);
    } else {
        ent->s.angle = (FLOAT)DEG2RAD(wow_move.yaw);
        Wow_UpdateCamera(ent);
    }
    /* Regen mana every frame: WOW_MANA_REGEN_PER_SEC / (1000/FRAMETIME) per tick. */
    {
        wowEntityLocal_t *pl = Wow_EntityLocal(ent);
        if (pl && pl->mana < WOW_MANA_MAX) {
            /* Use integer accumulation scaled by FRAMETIME to avoid per-frame float drift. */
            static DWORD mana_accum = 0;
            mana_accum += (DWORD)(WOW_MANA_REGEN_PER_SEC * FRAMETIME);
            if (mana_accum >= 1000) {
                DWORD ticks = mana_accum / 1000;
                mana_accum %= 1000;
                pl->mana = MIN(pl->mana + ticks, WOW_MANA_MAX);
            }
        }
    }
    Wow_UpdatePlayerHud(ent);

process_entities:
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT e = &wow_edicts[i];
        if (e->inuse && e->think)
            e->think(e);
    }
}

static LPCSTR Wow_GetThemeValue(LPCSTR filename) {
    return filename ? filename : "";
}

static BOOL Wow_PlayerIsMoving(void) { return wow_move.flags & BZ_WOW_MOVE_MASK; }

static void Wow_SelectEntity(LPEDICT ent, LPEDICT target) {
    DWORD old = ent->client->ps.selected_entity;
    LPEDICT old_target = old ? Wow_EdictByNumber(old) : NULL;

    if (old_target && old_target != target)
        old_target->selected &= ~(1 << ent->client->ps.number);
    if (target && target != ent && target->inuse) {
        target->selected |= (1 << ent->client->ps.number);
        ent->client->ps.selected_entity = target->s.number;
    } else {
        ent->client->ps.selected_entity = 0;
    }
}

void Wow_QuestAwardKillCredit(LPEDICT attacker, DWORD display_id) {
    wowClient_t *wc;

    if (!attacker || !attacker->client) return;
    wc = (wowClient_t *)attacker->client;
    FOR_LOOP(i, wc->client.ps.quest_count) {
        svQuestEntry_t *qs = &wc->client.ps.quest_log[i];
        LPCWOWQUESTDETAIL detail;
        BOOL all_done;

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
            if (wc->kill_progress[i][j] < detail->kill_objectives[j].required_count)
                wc->kill_progress[i][j]++;
            if (wc->kill_progress[i][j] < detail->kill_objectives[j].required_count)
                all_done = false;
        }
        if (all_done)
            qs->status = SV_QUEST_COMPLETE;
    }
}

/* An accepted predecessor is still in progress; only completion unlocks the chain. */
static BOOL Wow_QuestPrereqMet(wowClient_t *client, DWORD quest_id) {
    svQuestEntry_t *prev = SV_QuestFind(client->client.ps.quest_log, client->client.ps.quest_count, quest_id);
    return !quest_id || (prev && prev->status == SV_QUEST_COMPLETE);
}

static BOOL Wow_AddQuest(wowClient_t *client, DWORD quest_id) {
    LPCWOWQUESTDETAIL detail = Wow_QuestDetail(quest_id);
    if (!detail) return false;
    if (!Wow_QuestPrereqMet(client, detail->prev_quest)) return false;
    return SV_QuestAdd(client->client.ps.quest_log, &client->client.ps.quest_count, SV_MAX_QUEST_LOG, quest_id);
}

/* Resolve one physical quest NPC's repeated queststarter rows to the first
 * quest currently available to this player. The old one-edict-per-row path
 * made overlapping duplicates select an arbitrary later quest. */
static DWORD Wow_QuestForGiver(wowClient_t *client, wowEntityLocal_t const *local) {
    LPCWOWQUESTGIVER giver = NULL;
    DWORD representative = local->quest_id;

    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER cur = Wow_QuestGiver(i);
        if (cur->quest_id != representative) continue;
        if (local->home.x != cur->position.x || local->home.y != cur->position.y) continue;
        giver = cur; break;
    }
    if (!giver) return representative;
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER cur = Wow_QuestGiver(i);
        LPCWOWQUESTDETAIL detail;
        if (!Wow_QuestGiverSame(giver, cur)) continue;
        if (SV_QuestFind(client->client.ps.quest_log, client->client.ps.quest_count, cur->quest_id)) continue;
        detail = Wow_QuestDetail(cur->quest_id);
        if (detail && Wow_QuestPrereqMet(client, detail->prev_quest))
            return cur->quest_id;
    }
    return 0;
}

/* Serialize the complete bounded inbox so reconnects and repeated reward
 * commands converge on the same client-side message state. */
void Wow_SendInbox(LPEDICT ent) {
    wowClient_t *client;
    BYTE payload[2 + WOW_UI_MAX_MESSAGES * (4 + 1 + 1 + 4 + WOW_UI_MESSAGE_TITLE + WOW_UI_MESSAGE_BODY)];
    DWORD cursor = 0;

    if (!ent || !ent->client || !gi.GameCommand) return;
    client = (wowClient_t *)ent->client;
    payload[cursor++] = 1;
    payload[cursor++] = (BYTE)client->message_count;
    FOR_LOOP(i, client->message_count) {
        wowUiMessage_t const *message = &client->messages[i];
        payload[cursor++] = (BYTE)message->message_id;
        payload[cursor++] = (BYTE)(message->message_id >> 8);
        payload[cursor++] = (BYTE)(message->message_id >> 16);
        payload[cursor++] = (BYTE)(message->message_id >> 24);
        payload[cursor++] = message->kind;
        payload[cursor++] = message->flags;
        payload[cursor++] = (BYTE)message->quest_id;
        payload[cursor++] = (BYTE)(message->quest_id >> 8);
        payload[cursor++] = (BYTE)(message->quest_id >> 16);
        payload[cursor++] = (BYTE)(message->quest_id >> 24);
        memcpy(payload + cursor, message->title, WOW_UI_MESSAGE_TITLE); cursor += WOW_UI_MESSAGE_TITLE;
        memcpy(payload + cursor, message->body, WOW_UI_MESSAGE_BODY); cursor += WOW_UI_MESSAGE_BODY;
    }
    gi.GameCommand(ent, "wow_inbox", payload, cursor);
}

static void Wow_CompleteQuest(wowClient_t *client, DWORD quest_id) {
    svQuestEntry_t *state = SV_QuestFind(client->client.ps.quest_log, client->client.ps.quest_count, quest_id);
    LPCWOWQUESTDETAIL detail;
    if (!state || state->status != SV_QUEST_ACTIVE) return;
    detail = Wow_QuestDetail(quest_id);
    if (!detail) return;
    state->status = SV_QUEST_COMPLETE;
    client->client.ps.stats[WOW_STAT_XP] += detail->reward_xp;
    client->client.ps.stats[WOW_STAT_COPPER] += detail->reward_gold;
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

static BOOL Wow_CheatsEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("sv_cheats", "0")) != 0;
}

static void Wow_CheatHelp(void) {
    fprintf(stderr, "WoW: cheats: give all|health [amount]|mana [amount]|gold [amount]|xp [amount]; god; kill\n");
}

static void Wow_GiveCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    DWORD amount;

    if (!Wow_CheatsEnabled()) {
        fprintf(stderr, "WoW: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    if (!local || argc < 2) {
        Wow_CheatHelp();
        return;
    }
    amount = argc >= 3 ? (DWORD)strtoul(argv[2], NULL, 10) : 0;
    if (!strcasecmp(argv[1], "all")) {
        local->health = 100;
        local->mana = WOW_MANA_MAX;
        ent->client->ps.stats[WOW_STAT_XP] = ent->client->ps.stats[WOW_STAT_XP_MAX];
        ent->client->ps.stats[WOW_STAT_COPPER] += 100000;
    } else if (!strcasecmp(argv[1], "health")) {
        local->health = MIN(100, amount ? amount : 100);
    } else if (!strcasecmp(argv[1], "mana")) {
        local->mana = MIN(WOW_MANA_MAX, amount ? amount : WOW_MANA_MAX);
    } else if (!strcasecmp(argv[1], "gold")) {
        ent->client->ps.stats[WOW_STAT_COPPER] += amount;
    } else if (!strcasecmp(argv[1], "xp")) {
        ent->client->ps.stats[WOW_STAT_XP] += amount;
    } else {
        fprintf(stderr, "WoW: unsupported give target '%s'\n", argv[1]);
        Wow_CheatHelp();
        return;
    }
    UI_WriteWowHud(ent);
}

static void Wow_CheatCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);

    if (!Wow_CheatsEnabled()) {
        fprintf(stderr, "WoW: cheats are disabled; set sv_cheats 1\n");
        return;
    }
    if (!local) {
        fprintf(stderr, "WoW: cheat '%s' requires a player entity\n", argv[0]);
        return;
    }
    if (!strcasecmp(argv[0], "god")) {
        local->godmode = !local->godmode;
        fprintf(stderr, "WoW: god %s\n", local->godmode ? "on" : "off");
    } else if (!strcasecmp(argv[0], "kill")) {
        Wow_AIDie(ent, NULL);
    } else {
        (void)argc;
        fprintf(stderr, "WoW: unsupported cheat command '%s'\n", argv[0]);
    }
}

/* Open the loot window for a specific corpse entity.  Snapshots items into the
 * client struct, auto-takes copper, and triggers the player loot animation. */
static void Wow_OpenLootTarget(LPEDICT ent, LPEDICT corpse) {
    wowClient_t *client = (wowClient_t *)ent->client;
    wowEntityLocal_t *player_local = Wow_EntityLocal(ent);
    wowEntityLocal_t *corpse_local = corpse ? Wow_EntityLocal(corpse) : NULL;

    if (!corpse_local || (corpse_local->loot_count == 0 && corpse_local->loot_copper == 0)) return;

    client->loot_target = corpse->s.number;
    memcpy(client->loot_snap, corpse_local->loot_items, sizeof(client->loot_snap));
    client->loot_snap_count = corpse_local->loot_count;

    /* Copper auto-loots on open (classic WoW behaviour). */
    if (corpse_local->loot_copper > 0 && player_local) {
        player_local->copper += corpse_local->loot_copper;
        corpse_local->loot_copper = 0;
    }

    /* Loot animation: plays once then Wow_UpdatePlayerHud resets to Stand. */
    if (player_local) {
        Wow_SetEntityAnimation(ent, "Loot");
        player_local->loot_anim_timer = 1200; /* ms; long enough for bend-down pose */
    }
    UI_WriteWowHud(ent);
}

static void Wow_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    if (argc >= 1 && !strcasecmp(argv[0], "give")) {
        Wow_GiveCommand(ent, argc, argv);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "god") || !strcasecmp(argv[0], "kill"))) {
        Wow_CheatCommand(ent, argc, argv);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        DWORD quest_id = argc >= 2 ? (DWORD)strtoul(argv[1], NULL, 10) : 0;
        LPEDICT selected = ent->client->ps.selected_entity
            ? Wow_EdictByNumber(ent->client->ps.selected_entity) : NULL;
        wowEntityLocal_t *selected_local = selected ? Wow_EntityLocal(selected) : NULL;
        if (!quest_id && selected_local)
            quest_id = Wow_QuestForGiver(client, selected_local);
        if (!quest_id || !Wow_QuestDetail(quest_id)) {
            fprintf(stderr, "WoW: quest UI has no server data for quest %u\n", (unsigned)quest_id);
            return;
        }
        client->quest_id = quest_id;
        client->quest_open = true;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->quest_open = false;
        client->questlog_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_accept")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        DWORD quest_id = argc >= 2 ? (DWORD)strtoul(argv[1], NULL, 10) : client->quest_id;
        if (quest_id) Wow_AddQuest(client, quest_id);
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "quest_complete")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        DWORD quest_id = argc >= 2 ? (DWORD)strtoul(argv[1], NULL, 10) : client->quest_id;
        Wow_CompleteQuest(client, quest_id);
        Wow_SendInbox(ent);
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "questlog")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->questlog_open = !client->questlog_open;
        client->quest_open = false;
        UI_WriteWowHud(ent);
    } else if (argc >= 2 && !strcasecmp(argv[0], "message_read")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        DWORD message_id = (DWORD)strtoul(argv[1], NULL, 10);
        FOR_LOOP(i, client->message_count) {
            if (client->messages[i].message_id != message_id) continue;
            client->messages[i].flags &= (BYTE)~WOW_UI_MESSAGE_UNREAD;
            Wow_SendInbox(ent);
            break;
        }
    } else if (argc >= 1 && !strcasecmp(argv[0], "loot")) {
        /* Open loot window for the nearest corpse within melee+loot range. */
        LPEDICT corpse = Wow_FindNearestCorpse(ent, 10.0f);
        if (corpse) Wow_OpenLootTarget(ent, corpse);
    } else if (argc >= 2 && !strcasecmp(argv[0], "loot_take")) {
        /* Move one item from the loot snapshot into the first empty inventory slot. */
        wowClient_t *client = (wowClient_t *)ent->client;
        DWORD slot = (DWORD)strtoul(argv[1], NULL, 10);
        if (client->loot_target && slot < WOW_MAX_LOOT_ITEMS && client->loot_snap[slot].icon[0]) {
            DWORD inv_slot = WOW_UI_INVENTORY_SLOTS;
            FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
                if (!client->inventory[i].icon[0]) { inv_slot = i; break; }
            if (inv_slot < WOW_UI_INVENTORY_SLOTS) {
                client->inventory[inv_slot] = client->loot_snap[slot];
                /* Sync removal back to corpse entity (keeps corpse state authoritative). */
                LPEDICT corpse = Wow_EdictByNumber(client->loot_target);
                wowEntityLocal_t *cl = corpse ? Wow_EntityLocal(corpse) : NULL;
                if (cl && cl->loot_items[slot].icon[0]) { cl->loot_items[slot].icon[0] = '\0'; cl->loot_count--; }
                Wow_SendPlayerUi(ent);
            }
            client->loot_snap[slot].icon[0] = '\0';
            /* Close window when all items have been taken. */
            BOOL all_gone = true;
            FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) if (client->loot_snap[i].icon[0]) { all_gone = false; break; }
            if (all_gone) client->loot_target = 0;
            UI_WriteWowHud(ent);
        }
    } else if (argc >= 1 && !strcasecmp(argv[0], "loot_close")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->loot_target = 0;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "backpack")) {
        wowClient_t *client = (wowClient_t *)ent->client;
        client->backpack_open = !client->backpack_open;
        UI_WriteWowHud(ent);
    } else if (argc >= 1 && !strcasecmp(argv[0], "respawn")) {
        char race[64], sex[64]; DWORD class_id, appearance;
        Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
        DWORD idx = Wow_SelectSpawnPoint(race, class_id);
        if (idx == ~0u) {
            fprintf(stderr, "WoW: no respawn for race=%s class=%u; using Orc Warrior spawn\n", race, (unsigned)class_id);
            idx = Wow_SelectSpawnPoint("Orc", WOW_CLASS_WARRIOR);
        }
        if (idx != ~0u) Wow_TeleportPlayer(ent, idx);
    } else if (argc >= 2 && !strcasecmp(argv[0], "warp")) {
        /* warp <name>: teleport to a named WorldSafeLoc on the current map,
         * or perform a cross-map teleport via areatrigger_teleport by name.
         * Usable at runtime and from +warp on the command line (forwarded via client). */
        LPCSTR query = argv[1];
        char wdt[MAX_PATHLEN];
        DWORD n = CM_WowGetAllSpawnCount(); BOOL found = false;
        /* First: search WorldSafeLocs on current map (same-map warp). */
        FOR_LOOP(i, n) {
            LPCSTR nm = CM_WowGetSpawnName(i); LPCVECTOR3 pos;
            DWORD qlen = (DWORD)strlen(query), nlen; BOOL match = false; DWORD j;
            if (!nm) continue;
            nlen = (DWORD)strlen(nm);
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
            LPCWOWAREATRIGTELEPORT at = Wow_AreaTrigTeleportByName(query);
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
        wow_move.flags = (DWORD)strtoul(argv[1], NULL, 10);
        wow_move.yaw = (FLOAT)atof(argv[2]);
        wow_move.pitch = Wow_Clamp((FLOAT)atof(argv[3]), WOW_CAMERA_MIN_PITCH, WOW_CAMERA_MAX_PITCH);
        wow_move.distance = Wow_Clamp((FLOAT)atof(argv[4]), WOW_CAMERA_MIN_DISTANCE, WOW_CAMERA_MAX_DISTANCE);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "select"))) {
        Wow_SelectEntity(ent, argc >= 2 ? Wow_EdictByNumber((DWORD)strtoul(argv[1], NULL, 10)) : NULL);
    } else if (argc >= 2 && !strcasecmp(argv[0], "interact")) {
        LPEDICT target = Wow_EdictByNumber((DWORD)strtoul(argv[1], NULL, 10));
        wowEntityLocal_t *target_local = target ? Wow_EntityLocal(target) : NULL;
        Wow_SelectEntity(ent, target && target != ent ? target : NULL);
        if (target && target->think == Wow_RunCorpseFrame) {
            /* Right-clicked a corpse: open loot window. */
            Wow_OpenLootTarget(ent, target);
        } else if (target_local && target_local->quest_id) {
            wowClient_t *client = (wowClient_t *)ent->client;
            DWORD quest_id = Wow_QuestForGiver(client, target_local);
            if (!quest_id || !Wow_QuestDetail(quest_id)) return;
            client->quest_id = quest_id;
            client->quest_open = true;
            UI_WriteWowHud(ent);
        } else {
            wowEntityLocal_t *local = Wow_EntityLocal(ent);
            if (!local || local->dead || !ent->attack) return;
            local->enemy = target && target != ent ? target : NULL;
            ent->attack(ent);
        }
    } else if (argc >= 1 && (!strcasecmp(argv[0], "wow_cycle_target") || !strcasecmp(argv[0], "cycletarget"))) {
        DWORD old = ent->client->ps.selected_entity;
        DWORD start = old > 0 ? old + 1 : WOW_MAX_CLIENTS;
        for (DWORD i = start; i < (DWORD)globals.num_edicts; i++) {
            LPEDICT t = &wow_edicts[i];
            if (t->inuse && t != ent && (t->svflags & SVF_MONSTER) && (t->s.renderfx & RF_HOSTILE)) {
                Wow_SelectEntity(ent, t);
                return;
            }
        }
        for (DWORD i = WOW_MAX_CLIENTS; i < start && i < (DWORD)globals.num_edicts; i++) {
            LPEDICT t = &wow_edicts[i];
            if (t->inuse && t != ent && (t->svflags & SVF_MONSTER) && (t->s.renderfx & RF_HOSTILE)) {
                Wow_SelectEntity(ent, t);
                return;
            }
        }
    } else if (argc >= 1 && (!strcasecmp(argv[0], "attack") || !strcasecmp(argv[0], "wowattack"))) {
        LPEDICT target = argc >= 2
            ? Wow_EdictByNumber((DWORD)strtoul(argv[1], NULL, 10))
            : Wow_FindNearestAttackTarget(ent, WOW_MELEE_RANGE);
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (!ent || !local || local->dead || !ent->attack) {
            return;
        }
        local->enemy = target && target != ent ? target : NULL;
        Wow_SelectEntity(ent, target && target != ent ? target : NULL);
        ent->attack(ent);
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
    } else if (argc >= 2 && !strcasecmp(argv[0], "wow_action")) {
        DWORD slot = (DWORD)strtoul(argv[1], NULL, 10);

        /* Map action bar slots to spell indices.
         * Slots 0-2 = melee, 3 = heal, 4 = fire, 5 = frost. */
        static DWORD const slot_to_spell[] = {
            [0] = WOW_SPELL_ATTACK,
            [1] = WOW_SPELL_ATTACK,
            [2] = WOW_SPELL_ATTACK,
            [3] = WOW_SPELL_HEALING_TOUCH,
            [4] = WOW_SPELL_FIREBOLT,
            [5] = WOW_SPELL_FROSTBOLT,
        };
        if (slot >= sizeof(slot_to_spell) / sizeof(slot_to_spell[0])) return;
        DWORD spell = slot_to_spell[slot];
        if (spell >= wow_spell_count) return;
        wowSpellDef_t const *def = &wow_spells[spell];
        wowEntityLocal_t *cl = Wow_EntityLocal(ent);

        if (!cl || (cl->cast_spell != SPELL_NONE) || cl->cast_release_time || cl->gcd_time > 0) return;

        if (def->cast_time > 0 && Wow_PlayerIsMoving()) {
            fprintf(stderr, "WoW: %s — cannot cast while moving\n", def->name);
            return;
        }

        if (def->mana_cost > cl->mana) {
            fprintf(stderr, "WoW: %s — not enough mana\n", def->name);
            return;
        }

        LPEDICT target = def->range > 0 ? Wow_FindSpellTarget(ent, def->range) : NULL;
        /* For instant melee spells, accept the selected target even when out of
         * range — the auto-chase in Wow_RunFrame closes the gap automatically. */
        if (!def->cast_time && !target && ent->client && ent->client->ps.selected_entity) {
            LPEDICT t = Wow_EdictByNumber(ent->client->ps.selected_entity);
            if (t && t != ent && t->inuse && (t->svflags & SVF_MONSTER))
                target = t;
        }
        if (def->range > 0 && !target) {
            fprintf(stderr, "WoW: %s — no target in range\n", def->name);
            return;
        }

        cl->selected_action_slot = slot;
        if (def->cast_time > 0) {
            Wow_BeginSpellCast(ent, spell, target ? target->s.number : 0);
        } else {
            cl->gcd_time = WOW_GCD_MS;
            if (def->cast) def->cast(ent, target);
        }
    } else if (argc >= 2 && !strcasecmp(argv[0], "window_close")) {
        UI_HideWindow(ent, argv[1]);
    }
}

typedef enum { QUEST_MARKER_NONE = 0, QUEST_MARKER_AVAILABLE, QUEST_MARKER_ACTIVE, QUEST_MARKER_COMPLETE } questMarker_t;

/* Returns the highest-priority marker for this NPC relative to the given player:
 * COMPLETE > ACTIVE > AVAILABLE > NONE. */
static questMarker_t Wow_QuestMarkerForGiver(wowClient_t *client, wowEntityLocal_t const *local) {
    LPCWOWQUESTGIVER giver = NULL;
    questMarker_t best = QUEST_MARKER_NONE;
    if (!local->quest_id) return QUEST_MARKER_NONE;
    if (Wow_QuestForGiver(client, local)) return QUEST_MARKER_AVAILABLE;
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER cur = Wow_QuestGiver(i);
        if (cur->quest_id != local->quest_id) continue;
        if (local->home.x != cur->position.x || local->home.y != cur->position.y) continue;
        giver = cur; break;
    }
    if (!giver) return QUEST_MARKER_NONE;
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER cur = Wow_QuestGiver(i);
        svQuestEntry_t *e;
        if (!Wow_QuestGiverSame(giver, cur)) continue;
        e = SV_QuestFind(client->client.ps.quest_log, client->client.ps.quest_count, cur->quest_id);
        if (!e || e->status == SV_QUEST_REWARDED) continue;
        if (e->status == SV_QUEST_COMPLETE) return QUEST_MARKER_COMPLETE;
        if (e->status == SV_QUEST_ACTIVE) best = QUEST_MARKER_ACTIVE;
    }
    return best;
}

/* Select a quest marker per recipient; entity state is shared between clients until this copy. */
static void Wow_CustomizeEntity(DWORD player, LPCEDICT ent, LPENTITYSTATE state) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    if (!local || !local->quest_id || player >= WOW_MAX_CLIENTS || !state->overhead_sprite) return;
    switch (Wow_QuestMarkerForGiver(&wow_clients[player], local)) {
    case QUEST_MARKER_AVAILABLE:
        state->overhead_sprite = 0;
        state->model2 = local->quest_available_model;
        state->renderfx |= RF_ATTACH_OVERHEAD;
        break;
    case QUEST_MARKER_COMPLETE:  state->overhead_sprite = local->quest_active_sprite | WOW_QUEST_SPRITE_TINT_FLAG; break;
    case QUEST_MARKER_ACTIVE:    state->overhead_sprite = local->quest_active_sprite; break;
    default:                     state->overhead_sprite = 0; break;
    }
}

static void Wow_ClientSetCameraPosition(LPEDICT ent, LPCVECTOR2 position) {
    if (!ent || !ent->client || !position) {
        return;
    }
    ent->client->ps.origin = *position;
}

static void Wow_ClientBegin(LPEDICT ent) {
    if (!ent) {
        return;
    }
    ent->client = &wow_clients[0].client;
    ent->client->ps.client_ui_state = CLIENT_UI_GAME;
    Wow_SendPlayerUi(ent);
    Wow_SendInbox(ent);
    UI_WriteWowHud(ent);
    UI_WriteWelcomeWindow(ent);
}

struct game_export *GetGameAPI(struct game_import *import) {
    gi = *import;
    (void)gi;

    globals.Init = Wow_Init;
    globals.Shutdown = Wow_Shutdown;
    globals.RunFrame = Wow_RunFrame;
    globals.GetThemeValue = Wow_GetThemeValue;
    globals.ClientCommand = Wow_ClientCommand;
    globals.ClientSetCameraPosition = Wow_ClientSetCameraPosition;
    globals.ClientBegin = Wow_ClientBegin;
    globals.CanSeeEntity = NULL;
    globals.CustomizeEntity = Wow_CustomizeEntity;
    globals.PlayerCreateMap = Wow_SelectedPlayerCreateMap;
    globals.LoadMap = Wow_LoadMap;
    globals.GetWorldBounds = CM_GetWorldBounds;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = WOW_MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);

    return &globals;
}
