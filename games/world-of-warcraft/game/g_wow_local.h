#ifndef G_WOW_LOCAL_H
#define G_WOW_LOCAL_H

#include "server/server.h"
#include "common/wow_ui_shared.h"
#include "common/ui_constants.h"

#define WOW_MAX_CLIENTS 1
#define WOW_MAX_EDICTS 128
#define BZ_WOW_MOVE_MASK (WOW_MOVE_FORWARD | WOW_MOVE_BACK | WOW_MOVE_LEFT | WOW_MOVE_RIGHT)
#define WOW_PLAYER_MODEL "Character\\Orc\\Male\\OrcMale.m2"
#define WOW_PLAYER_WEAPON_MODEL "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01.m2"
#define WOW_CLASS_WARRIOR 1
#define WOW_CLASS_MAGE    8

/* CS_GENERAL slot used to pass selected character data from UI to game module.
   Set by the UI via a single userinfo-style cvar before map load, read by
   Wow_Init.  Format: \race\Human\sex\Male\class\1\appearance\12345 */
#define WOW_CS_PLAYERINFO 0
#define WOW_MOVE_FORWARD 1
#define WOW_MOVE_BACK 2
#define WOW_MOVE_LEFT 4
#define WOW_MOVE_RIGHT 8
#define WOW_WALK_SPEED 7.0f
#define WOW_MELEE_RANGE 5.0f
#define WOW_CAMERA_MIN_PITCH 305.0f
#define WOW_CAMERA_MAX_PITCH 355.0f
#define WOW_CAMERA_MIN_DISTANCE 5.5f
#define WOW_CAMERA_MAX_DISTANCE 25.0f

/* Spell definition table: Q2 g_items.c pattern — data-driven, function pointers per spell.
   Spell indices double as the cast_spell value while a spell is being channeled. */
typedef struct wowSpellDef_s {
    LPCSTR name;
    void (*cast)(LPEDICT caster, LPEDICT target);
    DWORD cast_time;     /* ms, 0 = instant */
    DWORD mana_cost;
    FLOAT range;         /* 0 = melee range / self */
    LPCSTR cast_anim;    /* animation during cast channel */
    LPCSTR ready_anim;   /* animation while waiting for cast */
} wowSpellDef_t;

extern wowSpellDef_t const wow_spells[];
extern DWORD const wow_spell_count;

#define WOW_SPELL_ATTACK        0
#define WOW_SPELL_FIREBOLT      1
#define WOW_SPELL_FROSTBOLT     2
#define WOW_SPELL_HEALING_TOUCH 3

#define SPELL_NONE ((DWORD)-1)  /* sentinel: no spell is casting */

typedef struct wowMove_s {
    LPCSTR animation;
    void (*think)(LPEDICT ent);
    void (*endfunc)(LPEDICT ent);
} wowMove_t, *LPWOWMOVE;

/* Per-frame entity spawn budget (reset each frame) */
extern DWORD wow_spawns_this_frame;

/* Per-entity game state.  Entity behaviour is driven entirely by the edict's
 * think function pointer (Quake2 style); there is no type/kind tag. */
typedef struct {
    DWORD display_id;
    LPCANIMATION animation;
    LPWOWMOVE currentmove;
    VECTOR2 home;
    FLOAT yaw;
    FLOAT patrol_radius;
    FLOAT patrol_phase;
    FLOAT walk_speed;
    DWORD health;
    DWORD mana;
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
    DWORD slow_timer;   /* ms remaining on movement-slow debuff (Frostbolt) */
    LPEDICT enemy;
    /* Cast state (SpellCast) — WoW-format cast time system */
    DWORD cast_spell;        /* spell id being cast (0 = idle) */
    DWORD cast_duration;     /* total cast duration (ms) */
    DWORD cast_remaining;    /* ms remaining until cast completes */
    DWORD cast_target;       /* entity number of target */
    VECTOR2 cast_origin;     /* XY position when cast began (movement cancels) */
    DWORD cast_release_time; /* ms remaining in the post-launch release animation */
    DWORD gcd_time;          /* ms remaining on global cooldown */
    DWORD selected_action_slot;  /* highlighted action bar slot (0-11, 255=none) */
    /* Projectile fields (valid when think == Wow_RunProjectile) */
    DWORD projectile_target;
    DWORD projectile_caster;
    FLOAT projectile_speed;
    DWORD projectile_damage;
    FLOAT projectile_yaw;
    FLOAT projectile_pitch;
    /* GameObject fields (kind == WOW_ENTITY_GAMEOBJECT) */
    DWORD go_entry;
    DWORD go_type;
    DWORD go_state;   /* 0=ready, 1=active, 2=destroyed */
    BOOL  go_interactive;
    DWORD go_display_id;
    /* Corpse fields (kind == WOW_ENTITY_CORPSE) */
    DWORD corpse_owner;
    DWORD corpse_timer;
    /* DynamicObject fields (kind == WOW_ENTITY_DYNAMICOBJECT) */
    DWORD dyn_spell_id;
    DWORD dyn_caster;
    DWORD dyn_radius;
    DWORD dyn_duration;
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
FLOAT        G_GetAttachmentZ(DWORD modelindex, int aid);
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
void Wow_FaceTarget(LPEDICT ent, LPEDICT target);
void Wow_AIAttack(LPEDICT ent);
void Wow_AIPain(LPEDICT ent);
void Wow_AIDie(LPEDICT ent, LPEDICT attacker);
BOOL Wow_AIAdvanceLockedFrame(LPEDICT ent);
BOOL Wow_EntityAffectingCombat(LPEDICT ent);
BOOL Wow_SetStandMove(LPEDICT ent);
BOOL Wow_SetRunMove(LPEDICT ent);
BOOL Wow_SetWalkMove(LPEDICT ent);
BOOL Wow_SetDirectionalMove(LPEDICT ent, DWORD flags);
BOOL Wow_SetCombatReadyAnimation(LPEDICT ent);
void Wow_AIRunFrame(LPEDICT ent);
void Wow_SpawnAmbientCreatures(LPCVECTOR2 origin);
void Wow_RunCreatureFrame(LPEDICT ent);
void Wow_SpawnGameObjects(LPCVECTOR2 origin);
void Wow_RunGameObjectFrame(LPEDICT ent);
void Wow_RunCorpseFrame(LPEDICT ent);
void Wow_RunDynamicObjectFrame(LPEDICT ent);
LPEDICT Wow_SpawnDynamicObject(DWORD spell_id, LPCVECTOR2 origin, DWORD duration);
LPCSTR Wow_CachedCreatureName(DWORD display_id);
DWORD Wow_CachedCreatureType(DWORD display_id);
DWORD Wow_CachedCreatureFamily(DWORD display_id);
DWORD Wow_CachedCreatureRank(DWORD display_id);
void UI_WriteWowHud(LPEDICT ent);

/* Ability/projectile system */
DWORD      Wow_FireboltModel(void);
DWORD      Wow_FrostboltModel(void);
void       Wow_RunProjectile(LPEDICT ent);
void       Wow_FireFirebolt(LPEDICT caster, LPEDICT target);
void       Wow_FireFrostbolt(LPEDICT caster, LPEDICT target);
void       Wow_HealingTouch(LPEDICT caster);
LPEDICT    Wow_FindSpellTarget(LPEDICT ent, FLOAT range);

#endif
