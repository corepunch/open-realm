#include "g_wow_local.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>

struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
wowClient_t wow_clients[WOW_MAX_CLIENTS];
static VECTOR2 wow_spawn_origin = { 0.0f, 0.0f };
static LONG wow_spawn_location = -1;
/* Configstring model indices for spell impact visuals; set during map load. */
static int wow_firebolt_impact_model = 0;
static int wow_frostbolt_impact_model = 0;
static char wow_loading_texture[MAX_PATHLEN] = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
static char wow_loading_title[128] = "World of Warcraft";
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
    .pitch = 328.0f,
    .distance = 8.5f,
};

#define WOW_MAX_SPAWNS_PER_FRAME 64
/* Per-frame spawn budget (declared extern in g_wow_local.h). */
DWORD wow_spawns_this_frame = 0;

static wowHudIcon_t const wow_start_inventory[WOW_UI_INVENTORY_SLOTS] = {
    { "Interface\\Icons\\INV_Misc_Bag_08.blp", "Backpack", 1 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", "Short Blade", 1 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp", "Food", 5 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp", "Healing Touch", 1 },
    { "Interface\\Icons\\Ability_Warrior_BattleShout.blp", "Battle Shout", 1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp", "Coin", 12 },
};

/* Warrior: melee abilities in slots 0-2, healing in 3, no ranged spells. */
static wowHudIcon_t const wow_actions_warrior[WOW_UI_ACTION_SLOTS] = {
    { "Interface\\Icons\\Ability_Warrior_Cleave.blp",      "Attack",        1 },
    { "Interface\\Icons\\Ability_Warrior_Charge.blp",      "Charge",        1 },
    { "Interface\\Icons\\Ability_Warrior_BattleShout.blp", "Battle Shout",  1 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp",   "Healing Touch", 1 },
    { "",                                                   "",              0 },
    { "",                                                   "",              0 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp",    "Short Blade",   1 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp",            "Food",          5 },
    { "Interface\\Icons\\INV_Potion_51.blp",               "Healing Potion",2 },
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",             "Backpack",      1 },
    { "",                                                   "",              0 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp",            "Coin",          12 },
};

/* Mage prototype: healing plus fire/frost spells in slots 3-5. */
static wowHudIcon_t const wow_actions_mage[WOW_UI_ACTION_SLOTS] = {
    { "Interface\\Icons\\Ability_Warrior_Cleave.blp",      "Attack",        1 },
    { "",                                                   "",              0 },
    { "",                                                   "",              0 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp",   "Healing Touch", 1 },
    { "Interface\\Icons\\Spell_Fire_FireBolt02.blp",       "Fireball",      1 },
    { "Interface\\Icons\\Spell_Frost_FrostBolt02.blp",     "Frostbolt",     1 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp",    "Short Blade",   1 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp",            "Food",          5 },
    { "Interface\\Icons\\INV_Potion_51.blp",               "Healing Potion",2 },
    { "Interface\\Icons\\INV_Misc_Bag_08.blp",             "Backpack",      1 },
    { "Interface\\Icons\\Spell_Holy_MagicalSentry.blp",    "Sentry",        1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp",            "Coin",          12 },
};

#define WOW_MISSING_ANIMATION_LOG_SLOTS 128

typedef struct {
    DWORD model;
    char name[64];
} wowMissingAnimationLog_t;

typedef struct {
    DWORD id;
    DWORD unused;
    DWORD path_offset;
} wowLoadingScreenDbc_t;

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
            fprintf(stderr,
                    "WoW missing animation: entity=%u model=%u animation=%s%s\n",
                    (unsigned)ent->s.number,
                    (unsigned)model,
                    animation_name,
                    invalid_interval ? " invalid-interval" : "");
            return;
        }
    }

    fprintf(stderr,
            "WoW missing animation: entity=%u model=%u animation=%s%s\n",
            (unsigned)ent->s.number,
            (unsigned)model,
            animation_name,
            invalid_interval ? " invalid-interval" : "");
}

FLOAT Wow_Clamp(FLOAT value, FLOAT min_value, FLOAT max_value) {
    return MAX(min_value, MIN(value, max_value));
}

DWORD Wow_Read32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

FLOAT Wow_ReadFloat(BYTE const *p) {
    FLOAT value;
    memcpy(&value, p, sizeof(value));
    return value;
}

LPCSTR Wow_DbcString(BYTE const *string_block, DWORD string_size, DWORD offset) {
    if (offset >= string_size) {
        return NULL;
    }
    return (LPCSTR)(string_block + offset);
}

BOOL Wow_ValidDbc(BYTE const *data,
                  DWORD size,
                  DWORD *records,
                  DWORD *fields,
                  DWORD *record_size,
                  DWORD *string_size) {
    if (!data || size <= 20 || memcmp(data, "WDBC", 4) != 0) {
        return false;
    }

    *records = Wow_Read32(data + 4);
    *fields = Wow_Read32(data + 8);
    *record_size = Wow_Read32(data + 12);
    *string_size = Wow_Read32(data + 16);

    if (*fields == 0 || *record_size < *fields * sizeof(DWORD) ||
        20 + *records * *record_size + *string_size > size) {
        return false;
    }
    return true;
}

BOOL Wow_FindDbcRecord(LPCSTR filename,
                       DWORD wanted_id,
                       LPBYTE *data_out,
                       DWORD *fields_out,
                       DWORD *record_size_out,
                       BYTE const **record_out,
                       BYTE const **strings_out,
                       DWORD *string_size_out) {
    LPBYTE data;
    DWORD size = 0;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *records_base;

    if (!filename || !data_out || !fields_out || !record_size_out ||
        !record_out || !strings_out || !string_size_out) {
        return false;
    }

    data = gi.ReadFile ? gi.ReadFile(filename, &size) : NULL;
    if (!Wow_ValidDbc(data, size, &records, &fields, &record_size, &string_size) ||
        fields < 1 || record_size < fields * sizeof(DWORD)) {
        SAFE_DELETE(data, gi.MemFree);
        return false;
    }

    records_base = data + 20;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        DWORD id = Wow_Read32(record);

        if (id == wanted_id) {
            *data_out = data;
            *fields_out = fields;
            *record_size_out = record_size;
            *record_out = record;
            *strings_out = records_base + records * record_size;
            *string_size_out = string_size;
            return true;
        }
    }

    gi.MemFree(data);
    return false;
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

static BOOL Wow_ResolveLoadingScreenById(DWORD loading_screen_id, LPSTR out, DWORD out_size) {
    LPBYTE data;
    DWORD size = 0;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *records_base;
    BYTE const *strings_base;

    if (!out || out_size == 0) {
        return false;
    }

    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\LoadingScreens.dbc", &size) : NULL;
    if (!Wow_ValidDbc(data, size, &records, &fields, &record_size, &string_size) ||
        fields < 3 || record_size < sizeof(wowLoadingScreenDbc_t)) {
        SAFE_DELETE(data, gi.MemFree);
        return false;
    }

    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        wowLoadingScreenDbc_t const *loading_screen = (wowLoadingScreenDbc_t const *)record;

        if (loading_screen->id == loading_screen_id) {
            LPCSTR path = Wow_DbcString(strings_base, string_size, loading_screen->path_offset);

            if (path && *path) {
                snprintf(out, out_size, "%s", path);
                gi.MemFree(data);
                return true;
            }
            break;
        }
    }

    gi.MemFree(data);
    return false;
}

static void Wow_SelectLoadingScreen(LPCSTR map_path) {
    LPBYTE data;
    DWORD size = 0;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *records_base;
    BYTE const *strings_base;
    char map_name[128] = { 0 };

    snprintf(wow_loading_texture,
             sizeof(wow_loading_texture),
             "%s",
             "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp");
    snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", "World of Warcraft");

    if (!map_path || !*map_path) {
        return;
    }

    Wow_MapNameFromPath(map_path, map_name, sizeof(map_name));
    if (!map_name[0]) {
        return;
    }

    data = gi.ReadFile ? gi.ReadFile("DBFilesClient\\Map.dbc", &size) : NULL;
    if (!Wow_ValidDbc(data, size, &records, &fields, &record_size, &string_size) ||
        fields < 4 || record_size < sizeof(wowMapDbc_t)) {
        SAFE_DELETE(data, gi.MemFree);
        return;
    }

    records_base = data + 20;
    strings_base = records_base + records * record_size;
    FOR_LOOP(record_index, records) {
        BYTE const *record = records_base + record_index * record_size;
        wowMapDbc_t const *map = (wowMapDbc_t const *)record;
        LPCSTR map_dir = Wow_DbcString(strings_base, string_size, map->directory_offset);

        if (!map_dir || strcasecmp(map_dir, map_name)) {
            continue;
        }

        DWORD loading_screen_id = Wow_Read32(record + (fields - 1) * sizeof(DWORD));
        LPCSTR map_title = Wow_DbcString(strings_base, string_size, map->title_offset);

        if (map_title && *map_title) {
            snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", map_title);
        } else {
            snprintf(wow_loading_title, sizeof(wow_loading_title), "%s", map_name);
        }

        if (!Wow_ResolveLoadingScreenById(loading_screen_id,
                                          wow_loading_texture,
                                          sizeof(wow_loading_texture))) {
            snprintf(wow_loading_texture,
                     sizeof(wow_loading_texture),
                     "%s",
                     "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp");
        }

        if (gi.error) {
            gi.error("Wow_SelectLoadingScreen: map=%s title=%s loadingId=%u texture=%s\n",
                     map_name,
                     wow_loading_title,
                     (unsigned)loading_screen_id,
                     wow_loading_texture);
        }
        gi.MemFree(data);
        return;
    }

    gi.MemFree(data);
}

FLOAT Wow_TerrainHeight(FLOAT x, FLOAT y) {
    return CM_GetHeightAtPoint(x, y);
}

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
    [WOW_SPELL_ATTACK]        = { "Attack",        Wow_SpellAttack,           0,    0,  5.0f, NULL,                NULL },
    [WOW_SPELL_FIREBOLT]      = { "Fireball",      Wow_SpellFireball,      1500,   10, 30.0f, "SpellCastDirected", "ReadySpellDirected" },
    [WOW_SPELL_FROSTBOLT]     = { "Frostbolt",     Wow_SpellFrostbolt,     2500,   15, 30.0f, "SpellCastDirected", "ReadySpellDirected" },
    [WOW_SPELL_HEALING_TOUCH] = { "Healing Touch", Wow_SpellHealingTouch,      0,   15,  0.0f, NULL,                NULL },
};
DWORD const wow_spell_count = sizeof(wow_spells) / sizeof(wow_spells[0]);

/* SPELL_NONE / SPELL_FIREBOLT etc. defined in g_wow_local.h */

DWORD Wow_FireboltModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
        /* WoW stores spell models flat under Spells\ — not in per-spell
         * subdirectories.  Verify each path exists in the MPQ before using it. */
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
            /* Hit the target — apply damage and optional slow debuff. */
            wowEntityLocal_t *target_local = Wow_EntityLocal(target);
            if (target_local && !target_local->dead) {
                if (target_local->health <= local->projectile_damage) {
                    Wow_AIDie(target, ent);
                } else {
                    target_local->health -= local->projectile_damage;
                    if (target->pain) target->pain(target);
                }
                /* slow_timer on the projectile encodes the debuff duration to apply. */
                if (local->slow_timer > 0)
                    target_local->slow_timer = MAX(target_local->slow_timer, local->slow_timer);
            }
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
        VECTOR2 delta = Vector2_sub(&(VECTOR2){ target->s.origin.x, target->s.origin.y },
                                    &(VECTOR2){ caster->s.origin.x, caster->s.origin.y });
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
    caster_local->enemy = target;
}

DWORD Wow_FrostboltModel(void) {
    static DWORD model = 0;
    static BOOL resolved = false;
    if (!resolved) {
        resolved = true;
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
        VECTOR2 delta = Vector2_sub(&(VECTOR2){ target->s.origin.x, target->s.origin.y },
                                    &(VECTOR2){ caster->s.origin.x, caster->s.origin.y });
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
    caster_local->enemy = target;
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
    ps->stats[WOW_STAT_COPPER] = 1234;
    /* Cast progress: remaining ms and total ms for client-side cast bar */
    ps->stats[WOW_STAT_CAST_PROGRESS] = (USHORT)(local->cast_spell != SPELL_NONE ? local->cast_remaining : 0);
    ps->stats[WOW_STAT_CAST_MAX] = (USHORT)(local->cast_spell != SPELL_NONE ? local->cast_duration : 0);
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

static void Wow_InitPlayer(LPEDICT ent) {
    LPPLAYER ps;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    FLOAT height = Wow_TerrainHeight(wow_spawn_origin.x, wow_spawn_origin.y);
    char race[64], sex[64];
    DWORD class_id, appearance;
    char model_path[MAX_PATHLEN * 2];

    /* Read selected character from CS_GENERAL configstrings (set by Wow_Init from cvars). */
    Wow_ReadSelectedCharFromCS(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);

    memset(ent, 0, sizeof(*ent));
    if (local) {
        memset(local, 0, sizeof(*local));
        local->cast_spell = SPELL_NONE;
        local->hostile = false;
        local->home = wow_spawn_origin;
        local->yaw = wow_move.yaw;
        local->health = 100;
        local->mana = WOW_MANA_MAX;
        local->attack_damage_point = 250;
        local->attack_backswing = 450;
    }
    ent->client = &wow_clients[0].client;
    ent->inuse = true;
    ent->s.number = 0;
    snprintf(model_path, sizeof(model_path), "Character\\%s\\%s\\%s%s.m2", race, sex, race, sex);
    ent->s.model = G_RegisterModel(model_path);
    ent->s.model2 = G_RegisterModel(WOW_PLAYER_WEAPON_MODEL);
    ent->s.appearance = appearance;
    ent->s.equipment = Wow_PackEquipment(WOW_PLAYER_EQUIPMENT_UPPER_BODY,
                                         WOW_PLAYER_EQUIPMENT_LOWER_BODY,
                                         WOW_PLAYER_EQUIPMENT_HANDS,
                                         WOW_PLAYER_EQUIPMENT_FEET);
    ent->s.origin = (VECTOR3){ wow_spawn_origin.x, wow_spawn_origin.y, height };
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
    ps->start_location = wow_spawn_location;
    snprintf(wow_clients[0].name, sizeof(wow_clients[0].name), "%s", "Thrall");
    {
        wowHudIcon_t const *actions = (class_id == WOW_CLASS_MAGE)
            ? wow_actions_mage : wow_actions_warrior;
        memcpy(wow_clients[0].inventory, wow_start_inventory, sizeof(wow_start_inventory));
        memcpy(wow_clients[0].actions, actions, WOW_UI_ACTION_SLOTS * sizeof(actions[0]));
        fprintf(stderr, "WoW: action bar initialized for class %u\n", (unsigned)class_id);
    }
#ifdef WOW
    ps->origin = wow_spawn_origin;
    ps->viewangles = (VECTOR3){ Wow_ViewPitch(wow_move.pitch), wow_move.yaw, 0.0f };
    ps->viewquat = Quaternion_fromEuler(&MAKE(VECTOR3, wow_move.pitch, 0.0f, wow_move.yaw), ROTATE_ZYX);
    ps->fov = 45;
    ps->distance = wow_move.distance;
#else
    ps->origin = wow_spawn_origin;
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

static void Wow_SpawnEntities(void);

static bool Wow_LoadMap(LPCSTR mapFilename) {
    if (!CM_LoadMap(mapFilename)) {
        return false;
    }
    if (gi.ApplyLobbySettings) {
        gi.ApplyLobbySettings((LPMAPINFO)CM_GetMapInfo());
    }
    if (gi.ClearWorld) {
        gi.ClearWorld();
    }
    Wow_SpawnEntities();
    return true;
}

static FLOAT Wow_PlayersRangeFromSpawn(LPCVECTOR2 spot, LPEDICT skip) {
    FLOAT best_dist2 = 999999999.0f;

    FOR_LOOP(i, WOW_MAX_CLIENTS) {
        LPEDICT ent = &wow_edicts[i];
        VECTOR2 delta;
        FLOAT dist2;

        if (ent == skip || !ent->inuse || !ent->client)
            continue;
        delta = Vector2_sub(spot, &ent->s.origin2);
        dist2 = delta.x * delta.x + delta.y * delta.y;
        if (dist2 < best_dist2)
            best_dist2 = dist2;
    }
    return best_dist2;
}

static DWORD Wow_CountSpawnPlayers(LPEDICT skip) {
    DWORD count = 0;

    FOR_LOOP(i, WOW_MAX_CLIENTS) {
        LPEDICT ent = &wow_edicts[i];

        if (ent != skip && ent->inuse && ent->client)
            count++;
    }
    return count;
}

static DWORD Wow_SelectRandomSpawnPoint(LPCMAPINFO mapinfo, LPEDICT ent) {
    DWORD count = 0;
    DWORD player_count;
    DWORD avoid1 = MAX_PLAYERS;
    DWORD avoid2 = MAX_PLAYERS;
    FLOAT range1 = 999999999.0f;
    FLOAT range2 = 999999999.0f;
    DWORD selection;

    if (!mapinfo)
        return MAX_PLAYERS;

    player_count = Wow_CountSpawnPlayers(ent);
    FOR_LOOP(i, MAX_PLAYERS) {
        FLOAT range;

        if (!mapinfo->players[i].used)
            continue;
        count++;
        if (player_count == 0)
            continue;
        range = Wow_PlayersRangeFromSpawn(&mapinfo->players[i].startingPosition, ent);
        if (avoid1 == MAX_PLAYERS || range < range1) {
            range2 = range1;
            avoid2 = avoid1;
            range1 = range;
            avoid1 = i;
        } else if (avoid2 == MAX_PLAYERS || range < range2) {
            range2 = range;
            avoid2 = i;
        }
    }
    if (count == 0)
        return MAX_PLAYERS;
    if (count <= 2 || player_count == 0) {
        avoid1 = MAX_PLAYERS;
        avoid2 = MAX_PLAYERS;
    } else {
        count -= 2;
    }
    selection = (DWORD)(rand() % count);
    FOR_LOOP(i, MAX_PLAYERS) {
        if (!mapinfo->players[i].used || i == avoid1 || i == avoid2)
            continue;
        if (selection-- == 0)
            return i;
    }
    return MAX_PLAYERS;
}

static void Wow_ThinkUnit(LPEDICT ent) {
    wowEntityLocal_t *el = Wow_EntityLocal(ent);
    if (el && el->slow_timer > 0)
        el->slow_timer = el->slow_timer > FRAMETIME ? el->slow_timer - FRAMETIME : 0;
    Wow_RunCreatureFrame(ent);
}
static void Wow_ThinkProjectile(LPEDICT ent) { Wow_RunProjectile(ent); }
static void Wow_ThinkDynamicObject(LPEDICT ent) { Wow_RunDynamicObjectFrame(ent); }

static void Wow_SpawnEntities(void) {
    LPCMAPINFO mapinfo = CM_GetMapInfo();
    DWORD spawn_location = Wow_SelectRandomSpawnPoint(mapinfo, &wow_edicts[0]);

    wow_spawn_location = -1;
    if (mapinfo && spawn_location < MAX_PLAYERS) {
        wow_spawn_origin = mapinfo->players[spawn_location].startingPosition;
        wow_spawn_location = (LONG)spawn_location;
    } else if (mapinfo && mapinfo->players[0].used) {
        wow_spawn_origin = mapinfo->players[0].startingPosition;
        wow_spawn_location = 0;
    }
    Wow_SelectLoadingScreen(mapinfo ? mapinfo->mapName : NULL);
    /* Re-populate the playerinfo configstring from cvars after SV_Map's
       memset cleared all configstrings (same pattern as Q3: game module
       re-sets configstrings after the server wipes them on map load). */
    {
        char race[64], sex[64];
        DWORD class_id, appearance;
        char buf[MAX_PATHLEN];
        Wow_ReadSelectedCharFromCvars(race, sizeof(race), sex, sizeof(sex), &class_id, &appearance);
        snprintf(buf, sizeof(buf), "\\race\\%s\\sex\\%s\\class\\%u\\appearance\\%u",
                 race, sex, (unsigned)class_id, (unsigned)appearance);
        gi.configstring(CS_GENERAL + WOW_CS_PLAYERINFO, buf);
    }
    wow_move.flags = 0;
    wow_move.yaw = 0.0f;
    wow_move.pitch = 328.0f;
    wow_move.distance = 8.5f;
    wow_spawns_this_frame = 0;
    Wow_InitPlayer(&wow_edicts[0]);
    globals.num_edicts = WOW_MAX_CLIENTS;
    Wow_SpawnAmbientCreatures(&wow_spawn_origin);
    Wow_SpawnGameObjects(&wow_spawn_origin);
    /* Register spell impact models so the client can load them before the first
     * svc_temp_entity arrives.  Try several known WoW MPQ paths in preference order. */
    {
        static LPCSTR const fire_paths[] = {
            "Spells\\FireBolt_ImpactDD_Med_Chest.m2",
            "Spells\\Fire_ImpactDD_Med_Chest.m2",
            NULL
        };
        static LPCSTR const frost_paths[] = {
            "Spells\\Ice_ImpactDD_Med_Chest.m2",
            "Spells\\Ice_ImpactDD_Low_Chest.m2",
            NULL
        };
        wow_firebolt_impact_model = 0;
        for (LPCSTR const *p = fire_paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
            if (buf) { wow_firebolt_impact_model = gi.ModelIndex(*p); gi.MemFree(buf); break; }
        }
        wow_frostbolt_impact_model = 0;
        for (LPCSTR const *p = frost_paths; *p; p++) {
            DWORD sz;
            HANDLE buf = gi.ReadFile ? gi.ReadFile(*p, &sz) : NULL;
            if (buf) { wow_frostbolt_impact_model = gi.ModelIndex(*p); gi.MemFree(buf); break; }
        }
        fprintf(stderr, "WoW: impact models — fire=%d frost=%d\n",
                wow_firebolt_impact_model, wow_frostbolt_impact_model);
    }
    fprintf(stderr, "WoW doodads: static ADT doodads are renderer-owned and not synced as entities\n");
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
    ent->s.origin.z = Wow_TerrainHeight(ent->s.origin.x, ent->s.origin.y);
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

static void Wow_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    if (argc >= 5 && (!strcasecmp(argv[0], "move") || !strcasecmp(argv[0], "wowmove"))) {
        wow_move.flags = (DWORD)strtoul(argv[1], NULL, 10);
        wow_move.yaw = (FLOAT)atof(argv[2]);
        wow_move.pitch = Wow_Clamp((FLOAT)atof(argv[3]), WOW_CAMERA_MIN_PITCH, WOW_CAMERA_MAX_PITCH);
        wow_move.distance = Wow_Clamp((FLOAT)atof(argv[4]), WOW_CAMERA_MIN_DISTANCE, WOW_CAMERA_MAX_DISTANCE);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "select"))) {
        LPEDICT target = argc >= 2
            ? Wow_EdictByNumber((DWORD)strtoul(argv[1], NULL, 10))
            : NULL;

        if (target && target != ent && target->inuse) {
            ent->client->ps.selected_entity = target->s.number;
        } else {
            ent->client->ps.selected_entity = 0;
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
        ent->client->ps.selected_entity = target && target != ent ? target->s.number : 0;
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
        if (def->range > 0 && !target) {
            fprintf(stderr, "WoW: %s — no target in range\n", def->name);
            return;
        }

        if (def->cast_time > 0) {
            Wow_BeginSpellCast(ent, spell, target ? target->s.number : 0);
        } else {
            cl->gcd_time = WOW_GCD_MS;
            if (def->cast) def->cast(ent, target);
        }
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
    UI_WriteWowHud(ent);
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
    globals.LoadMap = Wow_LoadMap;
    globals.GetWorldBounds = CM_GetWorldBounds;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = WOW_MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);

    return &globals;
}
