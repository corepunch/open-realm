#ifndef G_WOW_LOCAL_H
#define G_WOW_LOCAL_H

#include "server/server.h"
#include "server/sv_quest.h"
#include "common/wow_ui_shared.h"
#include "common/ui_constants.h"
#include "common/stb_dbc.h"

typedef struct WOWWEAPON {
    DWORD entry;
    LPCSTR name;
    DWORD subclass;
    DWORD display_id;
    DWORD inventory_type;
    DWORD item_level;
    DWORD required_level;
    FLOAT damage_min;
    FLOAT damage_max;
    DWORD damage_type;
    DWORD delay;
} WOWWEAPON;

typedef const WOWWEAPON *LPCWOWWEAPON;

LPCWOWWEAPON Wow_WeaponByEntry(DWORD entry);
DWORD Wow_RollWeaponDamage(DWORD entry);

#define WOW_CREATURE_MODEL_COUNT 4

typedef struct WOWCREATUREMODEL {
    DWORD index;
    DWORD display_id;
    FLOAT display_scale;
    FLOAT probability;
    LONG verified_build;
} WOWCREATUREMODEL;

typedef const WOWCREATUREMODEL *LPCWOWCREATUREMODEL;

/* File-shaped AzerothCore creature_template row plus every creature_template_model
 * variant. The generated table deliberately retains fields not consumed yet. */
typedef struct WOWCREATURE {
    DWORD entry;
    DWORD difficulty_entry[3];
    DWORD kill_credit[2];
    LPCSTR name;
    LPCSTR subname;
    LPCSTR icon_name;
    DWORD gossip_menu_id;
    DWORD min_level;
    DWORD max_level;
    LONG expansion;
    DWORD faction;
    DWORD npc_flags;
    FLOAT speed_walk;
    FLOAT speed_run;
    FLOAT speed_swim;
    FLOAT speed_flight;
    FLOAT detection_range;
    DWORD rank;
    LONG damage_school;
    FLOAT damage_modifier;
    DWORD base_attack_time;
    DWORD range_attack_time;
    FLOAT base_variance;
    FLOAT range_variance;
    DWORD unit_class;
    DWORD unit_flags;
    DWORD unit_flags2;
    DWORD dynamic_flags;
    LONG family;
    DWORD type;
    DWORD type_flags;
    DWORD loot_id;
    DWORD pickpocket_loot_id;
    DWORD skin_loot_id;
    DWORD pet_spell_data_id;
    DWORD vehicle_id;
    DWORD min_gold;
    DWORD max_gold;
    LPCSTR ai_name;
    DWORD movement_type;
    FLOAT hover_height;
    FLOAT health_modifier;
    FLOAT mana_modifier;
    FLOAT armor_modifier;
    FLOAT experience_modifier;
    DWORD racial_leader;
    DWORD movement_id;
    DWORD regen_health;
    LONG creature_immunities_id;
    DWORD flags_extra;
    LPCSTR script_name;
    LONG verified_build;
    WOWCREATUREMODEL models[WOW_CREATURE_MODEL_COUNT];
    DWORD model_count;
} WOWCREATURE;

typedef const WOWCREATURE *LPCWOWCREATURE;

DWORD Wow_CreatureCount(void);
LPCWOWCREATURE Wow_CreatureByEntry(DWORD entry);

typedef struct {
    DWORD quest_id;
    DWORD creature_entry;
    DWORD display_id;
    VECTOR3 position;
    FLOAT orientation;
} WOWQUESTGIVER;

/* Race/class -> spawn point, generated from serverdata/playercreateinfo.csv. */
typedef struct {
    DWORD race;
    DWORD cls;
    DWORD map;
    FLOAT x, y, z;
    FLOAT facing;
} WOWSPAWNPOINT;
typedef const WOWSPAWNPOINT *LPCWOWSPAWNPOINT;

/* AreaTrigger.dbc record — 10 fields, no strings, WoW 1.12. Struct matches
 * the raw 40-byte DBC record layout so records can be cast without decoding. */
typedef struct {
    DWORD id;
    DWORD map_id;
    FLOAT x, y, z;
    FLOAT radius;               /* > 0 → sphere trigger; == 0 → use box fields */
    FLOAT box_x, box_y, box_z; /* half-extents in local axes */
    FLOAT box_orientation;     /* radians; rotates XY into box-local frame */
} WOWAREATRIG;
typedef const WOWAREATRIG *LPCWOWAREATRIG;

/* areatrigger_teleport.csv record — cross-map destination for a trigger.
 * Generated into build/generated/g_areatrigger_teleport.c. */
typedef struct {
    DWORD id;
    LPCSTR name;               /* descriptive label, used for warp-by-name */
    DWORD target_map;
    FLOAT target_x, target_y, target_z;
    FLOAT target_orientation;
} WOWAREATRIGTELEPORT;
typedef const WOWAREATRIGTELEPORT *LPCWOWAREATRIGTELEPORT;

typedef struct {
    DWORD quest_id;
    VECTOR2 position;
} WOWQUESTOBJECTIVE;

#define WOW_QUEST_MAX_OBJECTIVE_TEXT 4
#define WOW_QUEST_MAX_REWARD_ITEMS   2
#define WOW_QUEST_MAX_KILL_OBJECTIVES 4

typedef struct {
    DWORD display_id;       /* CreatureDisplayInfo.dbc ID to kill */
    DWORD required_count;
} WOWQUESTKILLOBJECTIVE;

typedef struct {
    DWORD quest_id;
    LPCSTR title;
    LPCSTR description;
    LPCSTR objectives_text;
    LPCSTR reward_text;
    DWORD reward_xp;
    DWORD reward_gold;
    DWORD reward_items[WOW_QUEST_MAX_REWARD_ITEMS];
    DWORD prev_quest;
    DWORD min_level;
    WOWQUESTKILLOBJECTIVE kill_objectives[WOW_QUEST_MAX_KILL_OBJECTIVES];
    DWORD kill_objective_count;
} WOWQUESTDETAIL;

typedef const WOWQUESTGIVER *LPCWOWQUESTGIVER;
typedef const WOWQUESTOBJECTIVE *LPCWOWQUESTOBJECTIVE;
typedef const WOWQUESTDETAIL *LPCWOWQUESTDETAIL;

/* Queststarter SQL repeats one physical NPC for every quest it can offer. */
static BOOL Wow_QuestGiverSame(LPCWOWQUESTGIVER a, LPCWOWQUESTGIVER b) {
    return a->creature_entry == b->creature_entry && !memcmp(&a->position, &b->position, sizeof(a->position));
}

#define WOW_QUEST_OBJECTIVE_ANCHOR  0x51504F49
#define WOW_QUEST_SPRITE_TINT_FLAG  (1u << 15) // bit[15] of entityState_t.overhead_sprite → render with yellow tint

DWORD Wow_QuestGiverCount(void);
LPCWOWQUESTGIVER Wow_QuestGiver(DWORD index);
DWORD Wow_QuestObjectiveCount(void);
LPCWOWQUESTOBJECTIVE Wow_QuestObjective(DWORD index);
LPCWOWQUESTDETAIL Wow_QuestDetail(DWORD quest_id);

/* Ambient creature display IDs used by both m_creature.c and the loot table. */
#define WOW_CREATURE_DISPLAY_WOLF   161 // CreatureDisplayInfo.dbc; Timber Wolf family
#define WOW_CREATURE_DISPLAY_BOAR   193 // Stonetusk Boar; Durotar starting zone
#define WOW_CREATURE_DISPLAY_KOBOLD 163 // Kobold Vermin; kobold family
#define WOW_CREATURE_DISPLAY_MURLOC 188 // Murloc; coastal murloc variant

#define WOW_MAX_CLIENTS 1
#define WOW_MAX_EDICTS 128
#define BZ_WOW_MOVE_MASK (WOW_MOVE_FORWARD | WOW_MOVE_BACK | WOW_MOVE_LEFT | WOW_MOVE_RIGHT)
#define WOW_PLAYER_MODEL "Character\\Orc\\Male\\OrcMale.m2"
#define WOW_PLAYER_WEAPON_MODEL "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01.m2"
#define WOW_CLASS_WARRIOR 1
#define WOW_CLASS_PALADIN 2 // ChrClasses.dbc ID; Human reference class; used by quest-text substitution tests
#define WOW_CLASS_MAGE    8
#define WOW_START_WEAPON_ENTRY 37

/* CS_GENERAL slot used to pass selected character data from UI to game module.
   Set by the UI via a single userinfo-style cvar before map load, read by
   Wow_Init.  Format: \race\Human\sex\Male\class\1\appearance\12345 */
#define WOW_CS_PLAYERINFO 0
#define WOW_CS_NPC_NAME_FIRST 1 // CS_GENERAL slot; first server-authored NPC name used by world labels
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
    DWORD spell_dbc_id;  /* Spell.dbc ID for DBC-driven visual resolution */
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

/* HUD icon slot: icon path, display name, stack count.  Used for inventory,
 * action bar slots, and corpse loot slots. */
typedef struct {
    char icon[256];
    char name[64];
    DWORD count;
} wowHudIcon_t;

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
    DWORD weapon_entry;
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
    /* Game-object fields (think == Wow_RunGameObjectFrame). */
    DWORD go_entry;
    DWORD go_type;
    DWORD go_state;   /* 0=ready, 1=active, 2=destroyed */
    BOOL  go_interactive;
    DWORD go_display_id;
    DWORD quest_id;
    DWORD quest_available_model;  /* model index for yellow "!" (TalkToMe.m2) */
    DWORD quest_active_sprite;    /* image index for grey/yellow "?" (ActiveQuestIcon.blp) */
    /* Loot fields — valid on any entity (rolled at death, consumed on pickup). */
#define WOW_MAX_LOOT_ITEMS 6
    wowHudIcon_t loot_items[WOW_MAX_LOOT_ITEMS];
    DWORD loot_count;    /* active slots (icon[0]!=0 entries) */
    DWORD loot_copper;   /* copper coins rolled at death */
    DWORD loot_anim_timer; /* ms remaining for player loot animation */
    /* Corpse fields (think == Wow_RunCorpseFrame). */
    DWORD corpse_owner;
    DWORD corpse_timer;
    /* Dynamic-object fields (think == Wow_RunDynamicObjectFrame). */
    DWORD dyn_spell_id;
    DWORD dyn_caster;
    DWORD dyn_radius;
    DWORD dyn_duration;
    BOOL godmode;
    DWORD copper;        /* player copper balance (written to WOW_STAT_COPPER each frame) */
} wowEntityLocal_t;

typedef struct {
    struct client_s client;
    UINAME name;
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    wowHudIcon_t actions[WOW_UI_ACTION_SLOTS];
    BOOL quest_open;
    DWORD quest_id;
    DWORD kill_progress[SV_MAX_QUEST_LOG][WOW_QUEST_MAX_KILL_OBJECTIVES];
    DWORD questlog_open;
    wowUiMessage_t messages[WOW_UI_MAX_MESSAGES];
    DWORD message_count;
    /* Loot window state: snapshot taken on loot open, consumed by loot_take. */
    DWORD loot_target;                          /* entity# of open corpse (0=closed) */
    wowHudIcon_t loot_snap[WOW_MAX_LOOT_ITEMS]; /* item snapshot at open time */
    DWORD loot_snap_count;                      /* non-zero slots remaining */
    /* Backpack window toggle. */
    BOOL backpack_open;
    /* Damage flash overlay: brief text near health/target frames (server-side timers). */
    DWORD incoming_damage;      /* last incoming hit amount */
    DWORD incoming_dmg_timer;   /* ms remaining to display */
    DWORD outgoing_damage;      /* last outgoing hit amount */
    DWORD outgoing_dmg_timer;   /* ms remaining to display */
} wowClient_t;

typedef struct WOWDOODADDEF {
    DWORD name_id, unique_id;
    FLOAT position[3], rotation[3];
    WORD scale, flags;
} WOWDOODADDEF;
typedef struct WOWDOODADDEF *LPWOWDOODADDEF;
typedef const struct WOWDOODADDEF *LPCWOWDOODADDEF;

extern struct game_import gi;
extern struct game_export globals;
extern edict_t wow_edicts[WOW_MAX_EDICTS];
extern wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
extern wowClient_t wow_clients[WOW_MAX_CLIENTS];

/* Game adapts gi.* onto stb_dbc.h's shared cache I/O table (see common/stb_dbc.h). */
static inline void *G_DbcRead(LPCSTR filename, DWORD *size) {
    DWORD s = 0;
    void *data = gi.ReadFile ? gi.ReadFile(filename, &s) : NULL;
    if (size) *size = s;
    return data;
}
static inline void G_DbcFreeFile(void *p) { gi.MemFree(p); }
static inline void *G_DbcAlloc(size_t n) { return gi.MemAlloc ? gi.MemAlloc((long)n) : NULL; }
static inline void G_DbcFreeMem(void *p) { gi.MemFree(p); }

/* Shared game I/O table (defined in g_wow.c) for stb_dbc.h's cache. */
extern stbDbcIO_t const g_dbc_io;

int          G_RegisterModel(LPCSTR filename);
LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
FLOAT        G_GetAttachmentZ(DWORD modelindex, int aid);
void         G_FreeModels(void);

FLOAT Wow_Clamp(FLOAT value, FLOAT min_value, FLOAT max_value);
FLOAT Wow_TerrainHeight(FLOAT x, FLOAT y);
FLOAT Wow_FloorHeight(FLOAT x, FLOAT y, FLOAT z);
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
void Wow_ApplyDamage(LPEDICT target, LPEDICT attacker, DWORD damage);
BOOL Wow_AIAdvanceLockedFrame(LPEDICT ent);
BOOL Wow_EntityAffectingCombat(LPEDICT ent);
BOOL Wow_SetStandMove(LPEDICT ent);
BOOL Wow_SetRunMove(LPEDICT ent);
BOOL Wow_SetWalkMove(LPEDICT ent);
BOOL Wow_SetDirectionalMove(LPEDICT ent, DWORD flags);
BOOL Wow_SetCombatReadyAnimation(LPEDICT ent);
void Wow_AIRunFrame(LPEDICT ent);
void Wow_SpawnAmbientCreatures(LPCVECTOR2 origin);
void Wow_SpawnQuestLocations(LPCVECTOR2 origin);
void Wow_RunCreatureFrame(LPEDICT ent);
void Wow_SpawnGameObjects(LPCVECTOR2 origin);
void WowGo_SetDoodadTransform(LPCWOWDOODADDEF def, LPENTITYSTATE state);
void Wow_RunGameObjectFrame(LPEDICT ent);
void Wow_RunCorpseFrame(LPEDICT ent);
void Wow_RunDynamicObjectFrame(LPEDICT ent);
LPEDICT Wow_SpawnDynamicObject(DWORD spell_id, LPCVECTOR2 origin, DWORD duration);
LPEDICT Wow_SpawnCorpse(LPEDICT dead_entity);
LPCSTR Wow_CachedCreatureName(DWORD display_id);
DWORD Wow_CachedCreatureType(DWORD display_id);
DWORD Wow_CachedCreatureFamily(DWORD display_id);
DWORD Wow_CachedCreatureRank(DWORD display_id);
void UI_WriteWowHud(LPEDICT ent);
void UI_WriteWelcomeWindow(LPEDICT ent);
void UI_HideWindow(LPEDICT ent, LPCSTR window_id);
void Wow_GetPlayerRaceSex(char *race, size_t race_sz, char *sex, size_t sex_sz);
DWORD Wow_GetPlayerClass(void);
void Wow_QuestAwardKillCredit(LPEDICT attacker, DWORD display_id);
void Wow_SendInbox(LPEDICT ent);

/* Ability/projectile system */
DWORD      Wow_FireboltModel(void);
DWORD      Wow_FrostboltModel(void);
DWORD      Wow_FireboltImpactModel(void);
DWORD      Wow_FrostboltImpactModel(void);
DWORD      Wow_SpellMissileModel(DWORD spell_dbc_id);
DWORD      Wow_SpellImpactModel(DWORD spell_dbc_id);
void       Wow_RunProjectile(LPEDICT ent);
void       Wow_FireFirebolt(LPEDICT caster, LPEDICT target);
void       Wow_FireFrostbolt(LPEDICT caster, LPEDICT target);
void       Wow_HealingTouch(LPEDICT caster);
LPEDICT    Wow_FindSpellTarget(LPEDICT ent, FLOAT range);

/* g_playercreateinfo.c — generated from serverdata/playercreateinfo.csv */
DWORD           Wow_SpawnCount(void);
LPCWOWSPAWNPOINT Wow_SpawnByIndex(DWORD index);
DWORD           Wow_SelectSpawnPoint(LPCSTR race, DWORD class_id);
DWORD           Wow_PlayerCreateMap(LPCSTR race, DWORD class_id);
LPCVECTOR3      Wow_GetSpawnPos(DWORD idx);
BOOL            Wow_HasSpawnForMap(DWORD map_id); /* true if ANY race spawns on map_id */
/* g_wow.c — loot system */
void   Wow_RollLoot(LPEDICT ent);
LPEDICT Wow_FindNearestCorpse(LPEDICT ent, FLOAT range);
/* g_areatrigger_teleport.c — generated from serverdata/areatrigger_teleport.csv */
DWORD                 Wow_AreaTrigTeleportCount(void);
LPCWOWAREATRIGTELEPORT Wow_AreaTrigTeleportById(DWORD id);
LPCWOWAREATRIGTELEPORT Wow_AreaTrigTeleportByName(LPCSTR query); /* case-insensitive substr */
LPCWOWAREATRIGTELEPORT Wow_AreaTrigSpawnForMap(DWORD map_id);   /* first entry targeting map */
/* g_spawn.c — teleport entity to a spawn point or arbitrary position */
void            Wow_TeleportPlayer(LPEDICT ent, DWORD spawn_index);
void            Wow_TeleportPlayerToPos(LPEDICT ent, FLOAT x, FLOAT y, FLOAT z, FLOAT orientation);

#endif
