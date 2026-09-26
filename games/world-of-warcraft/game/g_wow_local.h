#ifndef G_WOW_LOCAL_H
#define G_WOW_LOCAL_H

#include "server/server.h"
#include "server/sv_quest.h"
#include "common/wow_ui_shared.h"
#include "common/ui_constants.h"
#include "common/stb_dbc.h"

void UI_WriteLoadingLayout(edict_t * ent);
extern char wow_loading_texture[MAX_PATHLEN];
extern char wow_loading_title[128];

typedef struct wowWeapon_s {
    uint32_t entry;
    cstring_t name;
    uint32_t subclass;
    uint32_t display_id;
    uint32_t inventory_type;
    uint32_t item_level;
    uint32_t required_level;
    float damage_min;
    float damage_max;
    uint32_t damage_type;
    uint32_t delay;
} wowWeapon_t;



wowWeapon_t const * Wow_WeaponByEntry(uint32_t entry);
uint32_t Wow_RollWeaponDamage(uint32_t entry);

#define WOW_CREATURE_MODEL_COUNT 4

typedef struct wowCreatureModel_s {
    uint32_t index;
    uint32_t display_id;
    float display_scale;
    float probability;
    int32_t verified_build;
} wowCreatureModel_t;



/* File-shaped AzerothCore creature_template row plus every creature_template_model
 * variant. The generated table deliberately retains fields not consumed yet. */
typedef struct wowCreature_s {
    uint32_t entry;
    uint32_t difficulty_entry[3];
    uint32_t kill_credit[2];
    cstring_t name;
    cstring_t subname;
    cstring_t icon_name;
    uint32_t gossip_menu_id;
    uint32_t min_level;
    uint32_t max_level;
    int32_t expansion;
    uint32_t faction;
    uint32_t npc_flags;
    float speed_walk;
    float speed_run;
    float speed_swim;
    float speed_flight;
    float detection_range;
    uint32_t rank;
    int32_t damage_school;
    float damage_modifier;
    uint32_t base_attack_time;
    uint32_t range_attack_time;
    float base_variance;
    float range_variance;
    uint32_t unit_class;
    uint32_t unit_flags;
    uint32_t unit_flags2;
    uint32_t dynamic_flags;
    int32_t family;
    uint32_t type;
    uint32_t type_flags;
    uint32_t loot_id;
    uint32_t pickpocket_loot_id;
    uint32_t skin_loot_id;
    uint32_t pet_spell_data_id;
    uint32_t vehicle_id;
    uint32_t min_gold;
    uint32_t max_gold;
    cstring_t ai_name;
    uint32_t movement_type;
    float hover_height;
    float health_modifier;
    float mana_modifier;
    float armor_modifier;
    float experience_modifier;
    uint32_t racial_leader;
    uint32_t movement_id;
    uint32_t regen_health;
    int32_t creature_immunities_id;
    uint32_t flags_extra;
    cstring_t script_name;
    int32_t verified_build;
    wowCreatureModel_t models[WOW_CREATURE_MODEL_COUNT];
    uint32_t model_count;
} wowCreature_t;



uint32_t Wow_CreatureCount(void);
wowCreature_t const * Wow_CreatureByEntry(uint32_t entry);

typedef struct {
    uint32_t quest_id;
    uint32_t creature_entry;
    uint32_t display_id;
    vector3_t position;
    float orientation;
} wowQuestGiver_t;

/* Race/class -> spawn point, generated from serverdata/playercreateinfo.csv. */
typedef struct {
    uint32_t race;
    uint32_t cls;
    uint32_t map;
    float x, y, z;
    float facing;
} wowSpawnPoint_t;


/* AreaTrigger.dbc record — 10 fields, no strings, WoW 1.12. Struct matches
 * the raw 40-byte DBC record layout so records can be cast without decoding. */
typedef struct {
    uint32_t id;
    uint32_t map_id;
    float x, y, z;
    float radius;               /* > 0 → sphere trigger; == 0 → use box fields */
    float box_x, box_y, box_z; /* half-extents in local axes */
    float box_orientation;     /* radians; rotates XY into box-local frame */
} wowAreatrig_t;


/* areatrigger_teleport.csv record — cross-map destination for a trigger.
 * Generated into build/generated/g_areatrigger_teleport.c. */
typedef struct {
    uint32_t id;
    cstring_t name;               /* descriptive label, used for warp-by-name */
    uint32_t target_map;
    float target_x, target_y, target_z;
    float target_orientation;
} wowAreatrigTeleport_t;


typedef struct {
    uint32_t quest_id;
    vector2_t position;
} wowQuestObjective_t;

#define WOW_QUEST_MAX_OBJECTIVE_TEXT 4
#define WOW_QUEST_MAX_REWARD_ITEMS   2
#define WOW_QUEST_MAX_KILL_OBJECTIVES 4

typedef struct {
    uint32_t display_id;       /* CreatureDisplayInfo.dbc ID to kill */
    uint32_t required_count;
} wowQuestKillObjective_t;

typedef struct {
    uint32_t quest_id;
    cstring_t title;
    cstring_t description;
    cstring_t objectives_text;
    cstring_t reward_text;
    uint32_t reward_xp;
    uint32_t reward_gold;
    uint32_t reward_items[WOW_QUEST_MAX_REWARD_ITEMS];
    uint32_t prev_quest;
    uint32_t min_level;
    wowQuestKillObjective_t kill_objectives[WOW_QUEST_MAX_KILL_OBJECTIVES];
    uint32_t kill_objective_count;
} wowQuestDetail_t;





/* Queststarter SQL repeats one physical NPC for every quest it can offer. */
static bool Wow_QuestGiverSame(wowQuestGiver_t const * a, wowQuestGiver_t const * b) {
    return a->creature_entry == b->creature_entry && !memcmp(&a->position, &b->position, sizeof(a->position));
}

#define WOW_QUEST_OBJECTIVE_ANCHOR  0x51504F49
#define WOW_QUEST_GIVER_GROUP_NONE  0xFFFFFFFFU // generated-index sentinel; no physical giver matches the representative row

uint32_t Wow_QuestGiverCount(void);
wowQuestGiver_t const * Wow_QuestGiver(uint32_t index);
uint32_t Wow_QuestGiverGroup(uint32_t quest_id, vector2_t const * position);
uint32_t Wow_QuestGiverGroupCount(uint32_t group);
wowQuestGiver_t const * Wow_QuestGiverInGroup(uint32_t group, uint32_t index);
uint32_t Wow_QuestObjectiveCount(void);
wowQuestObjective_t const * Wow_QuestObjective(uint32_t index);
wowQuestDetail_t const * Wow_QuestDetail(uint32_t quest_id);

/* Ambient creature display IDs used by both m_creature.c and the loot table. */
#define WOW_CREATURE_DISPLAY_WOLF   161 // CreatureDisplayInfo.dbc; Timber Wolf family
#define WOW_CREATURE_DISPLAY_BOAR   193 // Stonetusk Boar; Durotar starting zone
#define WOW_CREATURE_DISPLAY_KOBOLD 163 // Kobold Vermin; kobold family
#define WOW_CREATURE_DISPLAY_MURLOC 188 // Murloc; coastal murloc variant

#define WOW_MAX_EDICTS 128
#define BZ_WOW_MOVE_MASK (WOW_MOVE_FORWARD | WOW_MOVE_BACK | WOW_MOVE_LEFT | WOW_MOVE_RIGHT)
#define WOW_PLAYER_MODEL "Character\\Orc\\Male\\OrcMale.m2"
#define WOW_PLAYER_WEAPON_MODEL "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01.m2"
#define WOW_CLASS_WARRIOR 1
#define WOW_CLASS_PALADIN 2 // ChrClasses.dbc ID; Human reference class; used by quest-text substitution tests
#define WOW_CLASS_MAGE    8
#define WOW_START_WEAPON_ENTRY 37

/* CS_PLAYERSKINS + client number payload set from the selected-character cvar
   before map load. Format: \race\Human\sex\Male\class\1\appearance\12345 */
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
    cstring_t name;
    void (*cast)(edict_t * caster, edict_t * target);
    uint32_t cast_time;     /* ms, 0 = instant */
    uint32_t mana_cost;
    float range;         /* 0 = melee range / self */
    cstring_t cast_anim;    /* animation during cast channel */
    cstring_t ready_anim;   /* animation while waiting for cast */
    uint32_t spell_dbc_id;  /* Spell.dbc ID for DBC-driven visual resolution */
} wowSpellDef_t;

extern wowSpellDef_t const wow_spells[];
extern uint32_t const wow_spell_count;

#define WOW_SPELL_ATTACK        0
#define WOW_SPELL_FIREBOLT      1
#define WOW_SPELL_FROSTBOLT     2
#define WOW_SPELL_HEALING_TOUCH 3

#define SPELL_NONE ((uint32_t)-1)  /* sentinel: no spell is casting */

typedef struct wowMove_s {
    cstring_t animation;
    void (*think)(edict_t * ent);
    void (*endfunc)(edict_t * ent);
} wowMove_t;

/* Per-frame entity spawn budget (reset each frame) */
extern uint32_t wow_spawns_this_frame;

/* HUD icon slot: icon path, display name, stack count.  Used for inventory,
 * action bar slots, and corpse loot slots. */
typedef struct {
    char icon[256];
    char name[64];
    uint32_t count;
} wowHudIcon_t;

/* Per-entity game state.  Entity behaviour is driven entirely by function
 * pointers (Quake2 style); there is no type/kind tag. */
typedef struct {
    uint32_t display_id;
    animation_t const * animation;
    wowmove_t * currentmove;
    vector2_t home;
    float yaw;
    float patrol_radius;
    float patrol_phase;
    float walk_speed;
    uint32_t health;
    uint32_t mana;
    uint32_t attack_damage_point;
    uint32_t attack_backswing;
    uint32_t attack_time;
    uint32_t attack_damage_time;
    uint32_t attack_backswing_time;
    uint32_t pain_time;
    uint32_t death_time;
    bool attack_damage_done;
    uint32_t weapon_entry;
    bool dead;
    bool hostile;
    uint32_t slow_timer;   /* ms remaining on movement-slow debuff (Frostbolt) */
    edict_t * enemy;
    /* Cast state (SpellCast) — WoW-format cast time system */
    uint32_t cast_spell;        /* spell id being cast (0 = idle) */
    uint32_t cast_duration;     /* total cast duration (ms) */
    uint32_t cast_remaining;    /* ms remaining until cast completes */
    uint32_t cast_target;       /* entity number of target */
    vector2_t cast_origin;     /* XY position when cast began (movement cancels) */
    uint32_t cast_release_time; /* ms remaining in the post-launch release animation */
    uint32_t gcd_time;          /* ms remaining on global cooldown */
    uint32_t selected_action_slot;  /* highlighted action bar slot (0-11, 255=none) */
    /* Projectile fields (valid when think == Wow_RunProjectile) */
    uint32_t projectile_target;
    uint32_t projectile_caster;
    float projectile_speed;
    uint32_t projectile_damage;
    float projectile_yaw;
    float projectile_pitch;
    /* Game-object fields (think == Wow_RunGameObjectFrame). */
    uint32_t go_entry;
    uint32_t go_type;
    uint32_t go_state;   /* 0=ready, 1=active, 2=destroyed */
    bool  go_interactive;
    uint32_t go_display_id;
    uint32_t quest_id;
    uint32_t quest_available_model;  /* model index for yellow "!" (TalkToMe.m2) */
    /* Loot fields — valid on any entity (rolled at death, consumed on pickup). */
#define WOW_MAX_LOOT_ITEMS 6
    wowHudIcon_t loot_items[WOW_MAX_LOOT_ITEMS];
    uint32_t loot_count;    /* active slots (icon[0]!=0 entries) */
    uint32_t loot_copper;   /* copper coins rolled at death */
    uint32_t loot_anim_timer; /* ms remaining for player loot animation */
    /* Corpse fields (think == Wow_RunCorpseFrame). */
    uint32_t corpse_owner;
    uint32_t corpse_timer;
    /* Dynamic-object fields (think == Wow_RunDynamicObjectFrame). */
    uint32_t dyn_spell_id;
    uint32_t dyn_caster;
    uint32_t dyn_radius;
    uint32_t dyn_duration;
    bool godmode;
    uint32_t copper;        /* player copper balance (written to WOW_STAT_COPPER each frame) */
    void (*think)(edict_t *);
    void (*idle)(edict_t *);
    void (*move)(edict_t *);
    void (*attack)(edict_t *);
    void (*pain)(edict_t *);
} wowEntityLocal_t;

typedef struct {
    struct client_s client;
    UINAME name;
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    wowHudIcon_t actions[WOW_UI_ACTION_SLOTS];
    uint32_t selected_entity;  /* entity this player has targeted (server-side only, not synced to client) */
    bool quest_open;
    uint32_t quest_id;
    svQuestEntry_t quest_log[SV_MAX_QUEST_LOG];
    uint32_t quest_count;
    uint32_t kill_progress[SV_MAX_QUEST_LOG][WOW_QUEST_MAX_KILL_OBJECTIVES];
    uint32_t questlog_open;
    wowUiMessage_t messages[WOW_UI_MAX_MESSAGES];
    uint32_t message_count;
    uint32_t message_open_id;
    /* Loot window state: snapshot taken on loot open, consumed by loot_take. */
    uint32_t loot_target;                          /* entity# of open corpse (0=closed) */
    wowHudIcon_t loot_snap[WOW_MAX_LOOT_ITEMS]; /* item snapshot at open time */
    uint32_t loot_snap_count;                      /* non-zero slots remaining */
    /* Backpack window toggle. */
    bool backpack_open;
    /* Damage flash overlay: brief text near health/target frames (server-side timers). */
    uint32_t incoming_damage;      /* last incoming hit amount */
    uint32_t incoming_dmg_timer;   /* ms remaining to display */
    uint32_t outgoing_damage;      /* last outgoing hit amount */
    uint32_t outgoing_dmg_timer;   /* ms remaining to display */
} wowClient_t;

typedef struct wowDoodadDef_s {
    uint32_t name_id, unique_id;
    float position[3], rotation[3];
    uint16_t scale, flags;
} wowDoodadDef_t;



extern struct game_import gi;
extern struct game_export globals;
extern edict_t wow_edicts[WOW_MAX_EDICTS];
extern wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];
extern wowClient_t wow_clients[MAX_CLIENTS];

/* Game adapts gi.* onto stb_dbc.h's shared cache I/O table (see common/stb_dbc.h). */
static inline void *G_DbcRead(cstring_t filename, uint32_t *size) {
    uint32_t s = 0;
    void *data = gi.ReadFile(filename, &s);
    if (size) *size = s;
    return data;
}
static inline void G_DbcFreeFile(void *p) { gi.MemFree(p); }
static inline void *G_DbcAlloc(size_t n) { return gi.MemAlloc((long)n); }
static inline void G_DbcFreeMem(void *p) { gi.MemFree(p); }

/* Shared game I/O table (defined in g_wow.c) for stb_dbc.h's cache. */
extern stbDbcIO_t const g_dbc_io;

int          G_RegisterModel(cstring_t filename);
animation_t const * G_GetAnimation(uint32_t modelindex, cstring_t animname);
float        G_GetAttachmentZ(uint32_t modelindex, int aid);
void         G_FreeModels(void);

float Wow_Clamp(float value, float min_value, float max_value);
float Wow_TerrainHeight(float x, float y);
float Wow_FloorHeight(float x, float y, float z);
bool Wow_TerrainMoveWalkable(vector3_t const * from, vector3_t const * to, float terrain);
uint32_t Wow_EntityIndex(edict_t const * ent);
wowEntityLocal_t *Wow_EntityLocal(edict_t const * ent);
animation_t const * Wow_SetEntityAnimation(edict_t * ent, cstring_t animation_name);
bool Wow_SetEntityMove(edict_t * ent, wowmove_t * move);
bool Wow_SetEntityMoveFirstAnimation(edict_t * ent, wowmove_t * move, cstring_t const *animation_names);
void Wow_AdvanceEntityFrame(edict_t * ent);
edict_t * Wow_Spawn(void);
void Wow_AIIdle(edict_t * ent);
void Wow_AIMove(edict_t * ent);
void Wow_FaceTarget(edict_t * ent, edict_t * target);
void Wow_AIAttack(edict_t * ent);
void Wow_AIPain(edict_t * ent);
void Wow_AIDie(edict_t * ent, edict_t * attacker);
void Wow_ApplyDamage(edict_t * target, edict_t * attacker, uint32_t damage);
bool Wow_AIAdvanceLockedFrame(edict_t * ent);
bool Wow_EntityAffectingCombat(edict_t * ent);
bool Wow_SetStandMove(edict_t * ent);
bool Wow_SetRunMove(edict_t * ent);
bool Wow_SetWalkMove(edict_t * ent);
bool Wow_SetDirectionalMove(edict_t * ent, uint32_t flags);
bool Wow_SetCombatReadyAnimation(edict_t * ent);
void Wow_AIRunFrame(edict_t * ent);
void Wow_SpawnAmbientCreatures(vector2_t const * origin);
void Wow_SpawnQuestLocations(vector2_t const * origin);
void Wow_RunCreatureFrame(edict_t * ent);
void Wow_SpawnGameObjects(vector2_t const * origin);
void WowGo_SetDoodadTransform(wowDoodadDef_t const * def, entityState_t * state);
void Wow_RunGameObjectFrame(edict_t * ent);
void Wow_RunCorpseFrame(edict_t * ent);
void Wow_RunDynamicObjectFrame(edict_t * ent);
edict_t * Wow_SpawnDynamicObject(uint32_t spell_id, vector2_t const * origin, uint32_t duration);
edict_t * Wow_SpawnCorpse(edict_t * dead_entity);
cstring_t Wow_CachedCreatureName(uint32_t display_id);
uint32_t Wow_CachedCreatureType(uint32_t display_id);
uint32_t Wow_CachedCreatureFamily(uint32_t display_id);
uint32_t Wow_CachedCreatureRank(uint32_t display_id);
void UI_WriteWowHud(edict_t * ent);
void UI_WriteWowHover(edict_t * ent);
void UI_WriteWelcomeWindow(edict_t * ent);
void Wow_GetPlayerRaceSex(char *race, size_t race_sz, char *sex, size_t sex_sz);
uint32_t Wow_GetPlayerClass(void);
void Wow_QuestAwardKillCredit(edict_t * attacker, uint32_t display_id);

/* Ability/projectile system */
uint32_t      Wow_FireboltModel(void);
uint32_t      Wow_FrostboltModel(void);
uint32_t      Wow_FireboltImpactModel(void);
uint32_t      Wow_FrostboltImpactModel(void);
uint32_t      Wow_SpellMissileModel(uint32_t spell_dbc_id);
uint32_t      Wow_SpellImpactModel(uint32_t spell_dbc_id);
void       Wow_RunProjectile(edict_t * ent);
void       Wow_FireFirebolt(edict_t * caster, edict_t * target);
void       Wow_FireFrostbolt(edict_t * caster, edict_t * target);
void       Wow_HealingTouch(edict_t * caster);
edict_t *    Wow_FindSpellTarget(edict_t * ent, float range);

/* g_playercreateinfo.c — generated from serverdata/playercreateinfo.csv */
uint32_t           Wow_SpawnCount(void);
wowSpawnPoint_t const * Wow_SpawnByIndex(uint32_t index);
uint32_t           Wow_SelectSpawnPoint(cstring_t race, uint32_t class_id);
uint32_t           Wow_PlayerCreateMap(cstring_t race, uint32_t class_id);
vector3_t const *      Wow_GetSpawnPos(uint32_t idx);
bool            Wow_HasSpawnForMap(uint32_t map_id); /* true if ANY race spawns on map_id */
/* g_wow.c — loot system */
void   Wow_RollLoot(edict_t * ent);
edict_t * Wow_FindNearestCorpse(edict_t * ent, float range);
/* g_areatrigger_teleport.c — generated from serverdata/areatrigger_teleport.csv */
uint32_t                 Wow_AreaTrigTeleportCount(void);
wowAreatrigTeleport_t const * Wow_AreaTrigTeleportById(uint32_t id);
wowAreatrigTeleport_t const * Wow_AreaTrigTeleportByName(cstring_t query); /* case-insensitive substr */
wowAreatrigTeleport_t const * Wow_AreaTrigSpawnForMap(uint32_t map_id);   /* first entry targeting map */
/* g_spawn.c — teleport entity to a spawn point or arbitrary position */
void            Wow_TeleportPlayer(edict_t * ent, uint32_t spawn_index);
void            Wow_TeleportPlayerToPos(edict_t * ent, float x, float y, float z, float orientation);

#endif
