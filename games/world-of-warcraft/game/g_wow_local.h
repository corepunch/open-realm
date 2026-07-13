#ifndef G_WOW_LOCAL_H
#define G_WOW_LOCAL_H

#include "server/server.h"
#include "common/wow_ui_shared.h"
#include "common/ui_constants.h"

#define WOW_MAX_CLIENTS 1
#define WOW_MAX_EDICTS 128
#define WOW_PLAYER_MODEL "Character\\Orc\\Male\\OrcMale.m2"
#define WOW_PLAYER_WEAPON_MODEL "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01.m2"
#define WOW_CLASS_WARRIOR 1

/* CS_GENERAL slot used to pass selected character data from UI to game module.
   Set by the UI via a single userinfo-style cvar before map load, read by
   Wow_Init.  Format: \race\Human\sex\Male\class\1\appearance\12345 */
#define WOW_CS_PLAYERINFO 0
#define WOW_MOVE_FORWARD 1
#define WOW_MOVE_BACK 2
#define WOW_MOVE_LEFT 4
#define WOW_MOVE_RIGHT 8
#define WOW_WALK_SPEED 7.0f
#define WOW_CAMERA_MIN_PITCH 300.0f
#define WOW_CAMERA_MAX_PITCH 350.0f
#define WOW_CAMERA_MIN_DISTANCE 3.0f
#define WOW_CAMERA_MAX_DISTANCE 35.0f

typedef enum {
    WOW_ENTITY_NONE,
    WOW_ENTITY_PLAYER,
    WOW_ENTITY_CREATURE,
} wowEntityKind_t;

typedef struct wowMove_s {
    LPCSTR animation;
    void (*think)(LPEDICT ent);
    void (*endfunc)(LPEDICT ent);
} wowMove_t, *LPWOWMOVE;

typedef struct {
    wowEntityKind_t kind;
    DWORD display_id;
    LPCANIMATION animation;
    LPWOWMOVE currentmove;
    VECTOR2 home;
    FLOAT yaw;
    FLOAT patrol_radius;
    FLOAT patrol_phase;
    FLOAT walk_speed;
    DWORD health;
    DWORD attack_damage_point;
    DWORD attack_backswing;
    DWORD attack_time;
    DWORD attack_damage_time;
    DWORD attack_backswing_time;
    DWORD pain_time;
    DWORD death_time;
    BOOL attack_damage_done;
    BOOL dead;
    BOOL hostile;
    LPEDICT enemy;
} wowEntityLocal_t;

typedef struct {
    char icon[256];
    char name[64];
    DWORD count;
} wowHudIcon_t;

typedef struct {
    struct client_s client;
    UINAME name;
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    wowHudIcon_t actions[WOW_UI_ACTION_SLOTS];
} wowClient_t;

extern struct game_import gi;
extern struct game_export globals;
extern edict_t wow_edicts[WOW_MAX_EDICTS];
extern wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
extern wowClient_t wow_clients[WOW_MAX_CLIENTS];

int          G_RegisterModel(LPCSTR filename);
LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);

FLOAT Wow_Clamp(FLOAT value, FLOAT min_value, FLOAT max_value);
DWORD Wow_Read32(BYTE const *p);
FLOAT Wow_ReadFloat(BYTE const *p);
LPCSTR Wow_DbcString(BYTE const *string_block, DWORD string_size, DWORD offset);
BOOL Wow_ValidDbc(BYTE const *data,
                  DWORD size,
                  DWORD *records,
                  DWORD *fields,
                  DWORD *record_size,
                  DWORD *string_size);
BOOL Wow_FindDbcRecord(LPCSTR filename,
                       DWORD wanted_id,
                       LPBYTE *data_out,
                       DWORD *fields_out,
                       DWORD *record_size_out,
                       BYTE const **record_out,
                       BYTE const **strings_out,
                       DWORD *string_size_out);
FLOAT Wow_TerrainHeight(FLOAT x, FLOAT y);
DWORD Wow_EntityIndex(LPCEDICT ent);
wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent);
LPCANIMATION Wow_SetEntityAnimation(LPEDICT ent, LPCSTR animation_name);
BOOL Wow_SetEntityMove(LPEDICT ent, LPWOWMOVE move);
BOOL Wow_SetEntityMoveFirstAnimation(LPEDICT ent, LPWOWMOVE move, LPCSTR const *animation_names);
void Wow_AdvanceEntityFrame(LPEDICT ent);
LPEDICT Wow_Spawn(void);
void Wow_AIIdle(LPEDICT ent);
void Wow_AIMove(LPEDICT ent);
void Wow_AIAttack(LPEDICT ent);
void Wow_AIPain(LPEDICT ent);
void Wow_AIDie(LPEDICT ent, LPEDICT attacker);
BOOL Wow_AIAdvanceLockedFrame(LPEDICT ent);
BOOL Wow_EntityAffectingCombat(LPEDICT ent);
BOOL Wow_SetStandMove(LPEDICT ent);
BOOL Wow_SetRunMove(LPEDICT ent);
BOOL Wow_SetWalkMove(LPEDICT ent);
BOOL Wow_SetCombatReadyAnimation(LPEDICT ent);
void Wow_AIRunFrame(LPEDICT ent);
void Wow_SpawnAmbientCreatures(LPCVECTOR2 origin);
void Wow_RunCreatureFrame(LPEDICT ent);
void UI_WriteWowHud(LPEDICT ent);

#endif
