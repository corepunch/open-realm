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
static char wow_loading_texture[MAX_PATHLEN] = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
static char wow_loading_title[128] = "World of Warcraft";
enum {
    WOW_PLAYER_EQUIPMENT_UPPER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_LOWER_BODY = 1,
    WOW_PLAYER_EQUIPMENT_HANDS = 1,
    WOW_PLAYER_EQUIPMENT_FEET = 1
};
static struct {
    DWORD flags;
    FLOAT yaw;
    FLOAT pitch;
    FLOAT distance;
} wow_move = {
    .pitch = 328.0f,
    .distance = 8.5f,
};

static wowHudIcon_t const wow_start_inventory[WOW_UI_INVENTORY_SLOTS] = {
    { "Interface\\Icons\\INV_Misc_Bag_08.blp", "Backpack", 1 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", "Short Blade", 1 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp", "Food", 5 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp", "Healing Touch", 1 },
    { "Interface\\Icons\\Ability_Warrior_BattleShout.blp", "Battle Shout", 1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp", "Coin", 12 },
};

static wowHudIcon_t const wow_start_actions[WOW_UI_ACTION_SLOTS] = {
    { "Interface\\Icons\\Ability_Warrior_Cleave.blp", "Attack", 1 },
    { "Interface\\Icons\\Ability_Warrior_Charge.blp", "Charge", 1 },
    { "Interface\\Icons\\Ability_Warrior_BattleShout.blp", "Battle Shout", 1 },
    { "Interface\\Icons\\Spell_Nature_HealingTouch.blp", "Healing Touch", 1 },
    { "Interface\\Icons\\Spell_Fire_FireBolt02.blp", "Firebolt", 1 },
    { "Interface\\Icons\\Spell_Frost_FrostBolt02.blp", "Frostbolt", 1 },
    { "Interface\\Icons\\INV_Weapon_ShortBlade_05.blp", "Short Blade", 1 },
    { "Interface\\Icons\\INV_Misc_Food_24.blp", "Food", 5 },
    { "Interface\\Icons\\INV_Potion_51.blp", "Healing Potion", 2 },
    { "Interface\\Icons\\INV_Misc_Bag_08.blp", "Backpack", 1 },
    { "Interface\\Icons\\Spell_Holy_MagicalSentry.blp", "Sentry", 1 },
    { "Interface\\Icons\\INV_Misc_Coin_01.blp", "Coin", 12 },
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
    ps->stats[WOW_STAT_POWER] = 42;
    ps->stats[WOW_STAT_POWER_MAX] = 100;
    ps->stats[WOW_STAT_LEVEL] = 1;
    ps->stats[WOW_STAT_XP] = 120;
    ps->stats[WOW_STAT_XP_MAX] = 400;
    ps->stats[WOW_STAT_COPPER] = 1234;
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
    gi.Write(PF_BYTE, &(LONG){ slot < 10 ? '1' + (LONG)slot : 0 });
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

static LPEDICT Wow_FindNearestAttackTarget(LPEDICT ent) {
    LPEDICT best = NULL;
    FLOAT best_dist2 = 6.0f * 6.0f;

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
   Returns pointer to value in str, or fallback if key not found. */
static LPCSTR Wow_InfoValueForKey(LPCSTR str, LPCSTR key, LPCSTR fallback) {
    char keybuf[64];
    LPCSTR k, v, next;
    size_t klen;

    if (!str || !key || !*key)
        return fallback;
    snprintf(keybuf, sizeof(keybuf), "\\%s\\", key);
    klen = strlen(keybuf);
    for (k = str; *k; k = next) {
        if (*k == '\\') k++;
        next = strchr(k, '\\');
        if (!next) next = k + strlen(k);
        if (!strncmp(k, keybuf + 1, klen - 1) && *(k + klen - 1) == '\\') {
            v = k + klen;
            return v;
        }
    }
    return fallback;
}

/* Read selected character data from the single userinfo-style cvar set by the
   UI.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCvars(LPCSTR *race_out, LPCSTR *sex_out, DWORD *class_out, DWORD *appearance_out) {
    LPCSTR race = "Orc";
    LPCSTR sex = "Male";
    DWORD class_id = WOW_CLASS_WARRIOR;
    DWORD appearance = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);
    LPCSTR val;

    val = gi.CvarString(WOW_CVAR_PLAYERINFO, "");
    if (val[0]) {
        LPCSTR v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) race = v;
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) sex = v;
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) class_id = (DWORD)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) appearance = (DWORD)strtoul(v, NULL, 10);
    }

    *race_out = race;
    *sex_out = sex;
    *class_out = class_id;
    *appearance_out = appearance;
}

/* Read selected character data from the single CS_GENERAL configstring set by
   Wow_Init.  Fallbacks to OrcMale Warrior when no character was selected. */
static void Wow_ReadSelectedCharFromCS(LPCSTR *race_out, LPCSTR *sex_out, DWORD *class_out, DWORD *appearance_out) {
    LPCSTR race = "Orc";
    LPCSTR sex = "Male";
    DWORD class_id = WOW_CLASS_WARRIOR;
    DWORD appearance = Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0);
    LPCSTR val;

    val = gi.GetConfigstring(CS_GENERAL + WOW_CS_PLAYERINFO);
    if (val && val[0]) {
        LPCSTR v;
        v = Wow_InfoValueForKey(val, "race", "");
        if (v[0]) race = v;
        v = Wow_InfoValueForKey(val, "sex", "");
        if (v[0]) sex = v;
        v = Wow_InfoValueForKey(val, "class", "");
        if (v[0]) class_id = (DWORD)atoi(v);
        v = Wow_InfoValueForKey(val, "appearance", "");
        if (v[0]) appearance = (DWORD)strtoul(v, NULL, 10);
    }

    *race_out = race;
    *sex_out = sex;
    *class_out = class_id;
    *appearance_out = appearance;
}

static void Wow_InitPlayer(LPEDICT ent) {
    LPPLAYER ps;
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    FLOAT height = Wow_TerrainHeight(wow_spawn_origin.x, wow_spawn_origin.y);
    LPCSTR race, sex;
    DWORD class_id, appearance;
    char model_path[MAX_PATHLEN];

    /* Read selected character from CS_GENERAL configstrings (set by Wow_Init from cvars). */
    Wow_ReadSelectedCharFromCS(&race, &sex, &class_id, &appearance);

    memset(ent, 0, sizeof(*ent));
    if (local) {
        memset(local, 0, sizeof(*local));
        local->kind = WOW_ENTITY_PLAYER;
        local->home = wow_spawn_origin;
        local->yaw = wow_move.yaw;
        local->health = 100;
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
    ent->run = NULL;
    ent->attack = Wow_AIAttack;
    ent->pain = Wow_AIPain;
    Wow_SetStandMove(ent);

    ps = &ent->client->ps;
    memset(ps, 0, sizeof(*ps));
    ps->number = 0;
    ps->start_location = wow_spawn_location;
    snprintf(wow_clients[0].name, sizeof(wow_clients[0].name), "%s", "Thrall");
    memcpy(wow_clients[0].inventory, wow_start_inventory, sizeof(wow_start_inventory));
    memcpy(wow_clients[0].actions, wow_start_actions, sizeof(wow_start_actions));
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
    ps->client_ui_state = CLIENT_UI_GAME;
    ps->name = wow_clients[0].name;
    ps->texts[PLAYERTEXT_MAP_TITLE] = wow_loading_title;
    ps->texts[PLAYERTEXT_MAP_PREVIEW] = wow_loading_texture;
    Wow_UpdatePlayerHud(ent);
}

static void Wow_Init(void) {
    LPCSTR race, sex;
    DWORD class_id, appearance;
    char buf[MAX_PATHLEN];

    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    memset(wow_clients, 0, sizeof(wow_clients));

    globals.edicts = wow_edicts;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = WOW_MAX_CLIENTS;
    globals.num_edicts = WOW_MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);

    /* Copy selected character from cvars into a single CS_GENERAL configstring
       so the client can also read them for rendering. */
    Wow_ReadSelectedCharFromCvars(&race, &sex, &class_id, &appearance);
    snprintf(buf, sizeof(buf), "\\race\\%s\\sex\\%s\\class\\%u\\appearance\\%u",
             race, sex, (unsigned)class_id, (unsigned)appearance);
    gi.configstring(CS_GENERAL + WOW_CS_PLAYERINFO, buf);

    Wow_InitPlayer(&wow_edicts[0]);
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
    wow_move.flags = 0;
    wow_move.yaw = 0.0f;
    wow_move.pitch = 328.0f;
    wow_move.distance = 8.5f;
    Wow_InitPlayer(&wow_edicts[0]);
    globals.num_edicts = WOW_MAX_CLIENTS;
    Wow_SpawnAmbientCreatures(&wow_spawn_origin);
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
    locked = Wow_AIAdvanceLockedFrame(ent);
    if (locked) {
        Wow_UpdateCamera(ent);
    } else if (moving
        ? Wow_SetRunMove(ent)
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
    Wow_UpdatePlayerHud(ent);

    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        if (wow_edicts[i].inuse) {
            Wow_RunCreatureFrame(&wow_edicts[i]);
        }
    }
}

static LPCSTR Wow_GetThemeValue(LPCSTR filename) {
    return filename ? filename : "";
}

static void Wow_ClientCommand(LPEDICT ent, DWORD argc, LPCSTR argv[]) {
    if (argc >= 5 && (!strcasecmp(argv[0], "move") || !strcasecmp(argv[0], "wowmove"))) {
        wow_move.flags = (DWORD)strtoul(argv[1], NULL, 10);
        wow_move.yaw = (FLOAT)atof(argv[2]);
        wow_move.pitch = Wow_Clamp((FLOAT)atof(argv[3]), WOW_CAMERA_MIN_PITCH, WOW_CAMERA_MAX_PITCH);
        wow_move.distance = Wow_Clamp((FLOAT)atof(argv[4]), WOW_CAMERA_MIN_DISTANCE, WOW_CAMERA_MAX_DISTANCE);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "attack") || !strcasecmp(argv[0], "wowattack"))) {
        LPEDICT target = argc >= 2
            ? Wow_EdictByNumber((DWORD)strtoul(argv[1], NULL, 10))
            : Wow_FindNearestAttackTarget(ent);
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (!ent || !local || local->dead || !ent->attack) {
            return;
        }
        local->enemy = target && target != ent ? target : NULL;
        ent->attack(ent);
    } else if (argc >= 1 && (!strcasecmp(argv[0], "stopattack") || !strcasecmp(argv[0], "wowstopattack"))) {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        if (local) {
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
    Wow_SendPlayerUi(ent);
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
