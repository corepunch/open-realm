#ifndef g_local_h
#define g_local_h

#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include "common/common.h"
#include "common/stb_fdf.h"
#include "common/stb_slk.h"
#include "server/game.h"
#include "g_shared.h"
#include "g_unitrow.h"
#include "jass/jlex.h"

#define EDICTFIELD(x, type) { #x, FOFS(edict_s, x)-(HANDLE)NULL, type }

#define SAFE_CALL(FUNC, ...) if (FUNC) FUNC(__VA_ARGS__)
#define ABILITY(NAME) void M_##NAME(LPEDICT ent, LPEDICT target)
#define SEL_SCALE 72
#define MAX_BUILD_QUEUE 7
#define MAX_EVENT_QUEUE 256
#define MAX_MESSAGE_SUBSCRIBERS 8 // callbacks; bounded because messages are synchronous and game-local
#define MAX_UNIT_SELECT_SOUNDS 6 // sounds; largest UnitAckSounds *What variant list in ROC/TFT data
#define MAX_ENTITIES MAX_GAME_ENTITIES
#define MAX_REGION_SIZE 16
#define MAX_INVENTORY 6
#define ITEM_PICKUP_RANGE 150.0f /* world units; classic contextual-pickup reach */
#define MAX_CARGO 8
#define MAX_HERO_ABILITIES 4
#define MAX_UNIT_STATUSES 8
#define PLAYER_TEXT_BACKUP 16
#define PLAYER_TEXT_MASK (PLAYER_TEXT_BACKUP - 1)
#define MAX_START_PRIO 16 // slots; one possible priority entry per WC3 player start location
#define MAX_PLAYER_TECH_STATE 128

#define FILTER_EDICTS(ENT, CONDITION) \
for (LPEDICT ENT = globals.edicts; \
ENT - globals.edicts < globals.num_edicts; \
ENT++) if (CONDITION)

#define PLAYER_NUM(PLAYER) (PLAYER->number)
#define PLAYER_ENT(PLAYER) G_GetPlayerEntityByNumber(PLAYER_NUM(PLAYER))
#define PLAYER_CLIENT(PLAYER) G_GetPlayerClientByNumber(PLAYER_NUM(PLAYER))

#define UI_CHILD_VALUE(NAME, PARENT, VALUE, ...) \
LPFRAMEDEF NAME = UI_FindChildFrame(PARENT, #NAME); \
if (NAME) { \
    UI_Set##VALUE(NAME, __VA_ARGS__); \
} else { \
    fprintf(stderr, #NAME " not found");\
}

#define UI_WRITE_LAYER(ent, BuildUI, layer, ...) \
    UI_SetCurrentClient(ent->client); \
    UI_WriteStart(layer); \
    BuildUI(ent->client, ##__VA_ARGS__); \
    gi.Write(PF_LONG, &(LONG){0}); \
    gi.Write(PF_SHORT, &(LONG){0}); \
    gi.unicast(ent); \
    UI_SetCurrentClient(NULL);


#define FOR_SELECTED_UNITS(CLIENT, ENT) \
FILTER_EDICTS(ENT, G_IsEntitySelected(CLIENT, ENT))

KNOWN_AS(jass_s, JASS);
KNOWN_AS(gcamerasetup_s, CAMERASETUP);
KNOWN_AS(gregion_s, REGION);
KNOWN_AS(gevent_s, EVENT);
KNOWN_AS(gtrigger_s, TRIGGER);
KNOWN_AS(gquest_s, QUEST);
KNOWN_AS(gquestitem_s, QUESTITEM);

typedef enum {
    BUILD_COMMAND_ABSENT,
    BUILD_COMMAND_HIDDEN,
    BUILD_COMMAND_DISABLED,
    BUILD_COMMAND_AVAILABLE,
} buildCommandState_t;

typedef enum {
    PLACE_OK,
    PLACE_INVALID_BUILDING,
    PLACE_TERRAIN_BLOCKED,
    PLACE_UNIT_BLOCKED,
    PLACE_REQUIRED_PATHING_MISSING,
    PLACE_OUT_OF_BOUNDS,
    PLACE_REQUIRED_PARENT_MISSING,
} buildPlacementResult_t;

typedef struct {
    DWORD id;
    LONG researched;
    LONG max_allowed; /* -1 = unlimited/default */
} playerTechState_t;

typedef struct {
    BOOL (*on_entity_selected)(LPEDICT, LPEDICT);
    BOOL (*on_location_selected)(LPEDICT, LPCVECTOR2);
    void (*cmdbutton)(LPEDICT, DWORD);
    DWORD ability_code;
} menu_t;

enum {
    AI_HOLD_FRAME = 1 << 0,
    AI_FLYING     = 1 << 1,  /* air-layer unit (movetp "fly"): ignores ground collision */
    AI_IMMOBILE   = 1 << 2,  /* fixed unit: may act, but never translates or changes facing */
};

typedef enum {
    F_INT,
    F_FLOAT,
    F_LSTRING,            // string on disk, pointer in memory, TAG_LEVEL
    F_GSTRING,            // string on disk, pointer in memory, TAG_GAME
    F_VECTOR,
    F_ANGLEHACK,
    F_EDICT,            // index on disk, pointer in memory
    F_ITEM,                // index on disk, pointer in memory
    F_CLIENT,            // index on disk, pointer in memory
    F_FUNCTION,
    F_MMOVE,
    F_IGNORE
} fieldtype_t;

typedef struct {
    LPCSTR name;
    DWORD ofs;
    fieldtype_t type;
    DWORD flags;
} field_t;

typedef enum {
    ATK_NONE,
    ATK_NORMAL,
    ATK_PIERCE,
    ATK_SIEGE,
    ATK_SPELLS,
    ATK_CHAOS,
    ATK_MAGIC,
    ATK_HERO,
} attackType_t;

typedef enum {
    WPN_NONE,
    WPN_NORMAL,
    WPN_INSTANT,
    WPN_ARTILLERY,
    WPN_ALINE,
    WPN_MISSILE,
    WPN_MSPLASH,
    WPN_MBOUNCE,
    WPN_MLINE,
} weaponType_t;


typedef enum {
    ALLIANCE_PASSIVE = 0,
    ALLIANCE_HELP_REQUEST = 1,
    ALLIANCE_HELP_RESPONSE = 2,
    ALLIANCE_SHARED_XP = 3,
    ALLIANCE_SHARED_SPELLS = 4,
    ALLIANCE_SHARED_VISION = 5,
    ALLIANCE_SHARED_CONTROL = 6,
    ALLIANCE_SHARED_ADVANCED_CONTROL = 7,
    ALLIANCE_RESCUABLE = 8,
    ALLIANCE_SHARED_VISION_FORCED = 9,
} PLAYERALLIANCE;

typedef enum {
    TARG_NONE,
    TARG_AIR,
    TARG_ALIVE,
    TARG_ALLIES,
    TARG_DEAD,
    TARG_DEBRIS,
    TARG_ENEMIES,
    TARG_GROUND,
    TARG_HERO,
    TARG_INVULNERABLE,
    TARG_ITEM,
    TARG_MECHANICAL,
    TARG_NEUTRAL,
    TARG_NONHERO,
    TARG_NONSAPPER,
    TARG_NOTSELF,
    TARG_ORGANIC,
    TARG_PLAYERUNITS,
    TARG_SAPPER,
    TARG_SELF,
    TARG_STRUCTURE,
    TARG_TERRAIN,
    TARG_TREE,
    TARG_VULNERABLE,
    TARG_WALL,
    TARG_WARD,
    TARG_ANCIENT,
    TARG_NONANCIENT,
    TARG_FRIEND,
    TARG_BRIDGE,
    TARG_DECORATION,
} TARGTYPE;

typedef enum {
    MOVETYPE_NONE,            // never moves
    MOVETYPE_NOCLIP,          // origin and angles change with no interaction
    MOVETYPE_PUSH,            // no clip to world, push on box contact
    MOVETYPE_STOP,            // no clip to world, stops on box contact
    MOVETYPE_WALK,            // gravity
    MOVETYPE_STEP,            // gravity, special edge handling
    MOVETYPE_FLY,
    MOVETYPE_TOSS,            // gravity
    MOVETYPE_FLYMISSILE,      // extra size to monsters
    MOVETYPE_LINK,
    MOVETYPE_BOUNCE
} MOVETYPE;

typedef enum {
    EVENT_GAME_VICTORY = 0,
    EVENT_GAME_END_LEVEL = 1,
    EVENT_GAME_VARIABLE_LIMIT = 2,
    EVENT_GAME_STATE_LIMIT = 3,
    EVENT_GAME_TIMER_EXPIRED = 4,
    EVENT_GAME_ENTER_REGION = 5,
    EVENT_GAME_LEAVE_REGION = 6,
    EVENT_GAME_TRACKABLE_HIT = 7,
    EVENT_GAME_TRACKABLE_TRACK = 8,
    EVENT_GAME_SHOW_SKILL = 9,
    EVENT_GAME_BUILD_SUBMENU = 10,
    EVENT_PLAYER_STATE_LIMIT = 11,
    EVENT_PLAYER_ALLIANCE_CHANGED = 12,
    EVENT_PLAYER_DEFEAT = 13,
    EVENT_PLAYER_VICTORY = 14,
    EVENT_PLAYER_LEAVE = 15,
    EVENT_PLAYER_CHAT = 16,
    EVENT_PLAYER_END_CINEMATIC = 17,
    EVENT_PLAYER_UNIT_ATTACKED = 18,
    EVENT_PLAYER_UNIT_RESCUED = 19,
    EVENT_PLAYER_UNIT_DEATH = 20,
    EVENT_PLAYER_UNIT_DECAY = 21,
    EVENT_PLAYER_UNIT_DETECTED = 22,
    EVENT_PLAYER_UNIT_HIDDEN = 23,
    EVENT_PLAYER_UNIT_SELECTED = 24,
    EVENT_PLAYER_UNIT_DESELECTED = 25,
    EVENT_PLAYER_UNIT_CONSTRUCT_START = 26,
    EVENT_PLAYER_UNIT_CONSTRUCT_CANCEL = 27,
    EVENT_PLAYER_UNIT_CONSTRUCT_FINISH = 28,
    EVENT_PLAYER_UNIT_UPGRADE_START = 29,
    EVENT_PLAYER_UNIT_UPGRADE_CANCEL = 30,
    EVENT_PLAYER_UNIT_UPGRADE_FINISH = 31,
    EVENT_PLAYER_UNIT_TRAIN_START = 32,
    EVENT_PLAYER_UNIT_TRAIN_CANCEL = 33,
    EVENT_PLAYER_UNIT_TRAIN_FINISH = 34,
    EVENT_PLAYER_UNIT_RESEARCH_START = 35,
    EVENT_PLAYER_UNIT_RESEARCH_CANCEL = 36,
    EVENT_PLAYER_UNIT_RESEARCH_FINISH = 37,
    EVENT_PLAYER_UNIT_ISSUED_ORDER = 38,
    EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER = 39,
    EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER = 40,
    EVENT_PLAYER_UNIT_ISSUED_UNIT_ORDER = 40,    // for compat
    EVENT_PLAYER_HERO_LEVEL = 41,
    EVENT_PLAYER_HERO_SKILL = 42,
    EVENT_PLAYER_HERO_REVIVABLE = 43,
    EVENT_PLAYER_HERO_REVIVE_START = 44,
    EVENT_PLAYER_HERO_REVIVE_CANCEL = 45,
    EVENT_PLAYER_HERO_REVIVE_FINISH = 46,
    EVENT_PLAYER_UNIT_SUMMON = 47,
    EVENT_PLAYER_UNIT_DROP_ITEM = 48,
    EVENT_PLAYER_UNIT_PICKUP_ITEM = 49,
    EVENT_PLAYER_UNIT_USE_ITEM = 50,
    EVENT_PLAYER_UNIT_LOADED = 51,
    EVENT_UNIT_DAMAGED = 52,
    EVENT_UNIT_DEATH = 53,
    EVENT_UNIT_DECAY = 54,
    EVENT_UNIT_DETECTED = 55,
    EVENT_UNIT_HIDDEN = 56,
    EVENT_UNIT_SELECTED = 57,
    EVENT_UNIT_DESELECTED = 58,
    EVENT_UNIT_STATE_LIMIT = 59,
    EVENT_UNIT_ACQUIRED_TARGET = 60,
    EVENT_UNIT_TARGET_IN_RANGE = 61,
    EVENT_UNIT_ATTACKED = 62,
    EVENT_UNIT_RESCUED = 63,
    EVENT_UNIT_CONSTRUCT_CANCEL = 64,
    EVENT_UNIT_CONSTRUCT_FINISH = 65,
    EVENT_UNIT_UPGRADE_START = 66,
    EVENT_UNIT_UPGRADE_CANCEL = 67,
    EVENT_UNIT_UPGRADE_FINISH = 68,
    EVENT_UNIT_TRAIN_START = 69,
    EVENT_UNIT_TRAIN_CANCEL = 70,
    EVENT_UNIT_TRAIN_FINISH = 71,
    EVENT_UNIT_RESEARCH_START = 72,
    EVENT_UNIT_RESEARCH_CANCEL = 73,
    EVENT_UNIT_RESEARCH_FINISH = 74,
    EVENT_UNIT_ISSUED_ORDER = 75,
    EVENT_UNIT_ISSUED_POINT_ORDER = 76,
    EVENT_UNIT_ISSUED_TARGET_ORDER = 77,
    EVENT_UNIT_HERO_LEVEL = 78,
    EVENT_UNIT_HERO_SKILL = 79,
    EVENT_UNIT_HERO_REVIVABLE = 80,
    EVENT_UNIT_HERO_REVIVE_START = 81,
    EVENT_UNIT_HERO_REVIVE_CANCEL = 82,
    EVENT_UNIT_HERO_REVIVE_FINISH = 83,
    EVENT_UNIT_SUMMON = 84,
    EVENT_UNIT_DROP_ITEM = 85,
    EVENT_UNIT_PICKUP_ITEM = 86,
    EVENT_UNIT_USE_ITEM = 87,
    EVENT_UNIT_LOADED = 88,
    EVENT_WIDGET_DEATH = 89,
    EVENT_DIALOG_BUTTON_CLICK = 90,
    EVENT_DIALOG_CLICK = 91,
    
    EVENT_UNIT_IN_RANGE,
} EVENTTYPE;

/* struct uiFrameDef_s is defined in common/stb_fdf.h (shared with UI module) */

struct gregion_s {
    BOX2 rects[MAX_REGION_SIZE];
    DWORD num_rects;
};

struct gcamerasetup_s {
    FLOAT target_distance;
    FLOAT far_z;
//    FLOAT angle_of_attack;
    FLOAT fov;      /* vertical field of view in degrees */
//    FLOAT roll;
//    FLOAT rotations;
    FLOAT z_offset;
    VECTOR3 viewangles;
    VECTOR2 position;
};

struct client_s {
    PLAYER ps;
    BOOL connected; /* ClientBegin completed for this reserved player edict. */
    struct {
        DWORD race_pref, controller;
        BYTE tax[MAX_PLAYERS][PLAYERSTATE_LUMBER_GATHERED + 1];
        FLOAT handicap, handicap_xp;
        BOOL race_selectable, on_score_screen;
        char name[MAX_PATHLEN];
    } jass;
    playerTechState_t tech[MAX_PLAYER_TECH_STATE];
    char playerTextStorage[PLAYERTEXT_COUNT][PLAYER_TEXT_BACKUP][512];
    DWORD playerTextCursor[PLAYERTEXT_COUNT];
    LPCMAPPLAYER mapplayer;
    DWORD ping;
    BOOL no_control;
    menu_t menu;
    struct {
        CAMERASETUP state;
        CAMERASETUP old_state;
        DWORD start_time;
        DWORD end_time;
        LPEDICT target_controller;
        VECTOR2 target_offset;
    } camera;
    /* Last HP/mana figures reflected in the single-unit info panel, so the
     * server only re-sends LAYER_INFOPANEL when a displayed value changes. */
    struct {
        DWORD entity;
        LONG hp;
        LONG mana;
        LONG xp;     /* hero experience, so the XP/attribute display updates live */
    } infopanel;
    /* Last resource values reflected in the resource bar, so the server only
     * re-sends LAYER_CONSOLE when gold/lumber/food changes. */
    struct {
        LONG gold;
        LONG lumber;
        LONG food_used;
        LONG food_cap;
    } resourcebar;
    DWORD cinematic_end_time;  /* game time (ms) when current SetCinematicScene expires, 0 = none */
};

typedef struct {
    LPCSTR animation;
    void (*think)(LPEDICT);
    void (*endfunc)(LPEDICT);
    struct ability_s *ability;
} umove_t;

#define ABILITY_PASSIVE  (1 << 0)
#define ABILITY_TOGGLE   (1 << 1)
#define ABILITY_CHANNEL  (1 << 2)

/* Spell target types: maps to WarSmash's unit-target / point-target / no-target
 * base classes.  SPELL_TARGET_UNIT_OR_POINT allows either (e.g. Carrion Swarm). */
typedef enum {
    SPELL_TARGET_NONE,
    SPELL_TARGET_UNIT,
    SPELL_TARGET_POINT,
    SPELL_TARGET_UNIT_OR_POINT,
} spellTargetType_t;

typedef struct spell_target_s {
    spellTargetType_t type;
    union {
        LPEDICT entity;
        VECTOR2 point;
    };
} spellTarget_t;

/* Spell execution flags — mirrors the Quake2-style ability_t flags but scoped to
 * the unified spell pipeline. */
#define SPELL_CHANNEL    (1 << 0)  /* caster locked in place; movement cancels */
#define SPELL_TOGGLE     (1 << 1)  /* toggle on/off like Immolation */
#define SPELL_AUTOCAST   (1 << 2)  /* right-click toggles autocast (Cold Arrows) */
#define SPELL_NO_SMART   (1 << 3)  /* skip smart-click auto-target (Charm) */

typedef struct spell_info_s {
    DWORD code;                    /* ability FourCC (must match abilitylist entry) */
    LPCSTR name;                   /* debug / log identifier */
    spellTargetType_t target_type;
    DWORD flags;
    BOOL (*validate)(LPEDICT caster, spellTarget_t target);  /* extra validation before mana/cooldown spend */
    void (*execute)(LPEDICT caster, spellTarget_t target, struct spell_info_s const *spell);
} spell_info_t;

typedef struct ability_s {
    void (*init)(LPCSTR, struct ability_s *);
    void (*cmd)(LPEDICT);
    DWORD flags;
    struct spell_info_s *spell;    /* non-NULL for spells using the unified pipeline */
} ability_t;

typedef struct {
    attackType_t type;
    weaponType_t weapon;
    VECTOR3 origin;
    DWORD damageBase;
    DWORD numberOfDice;
    DWORD sidesPerDie;
    FLOAT damagePoint;
    FLOAT cooldown;
    FLOAT range;
    /* Splash (area-of-effect) attack: full/medium/small radii and the damage
     * factors applied in the medium and small rings. */
    FLOAT areaFull;
    FLOAT areaMedium;
    FLOAT areaSmall;
    FLOAT factorMedium;
    FLOAT factorSmall;
    DWORD maxTargets;   /* bounce: max chained targets (utc1) */
    FLOAT damageLoss;   /* bounce: fractional damage lost per bounce (udl1) */
    struct {
        DWORD model;
        FLOAT arc;
        FLOAT speed;
    } projectile;
} unitAttack_t;

typedef struct {
    FLOAT value;
    FLOAT max_value;
} EDICTSTAT;

typedef struct {
    float MoveSpeed;
    float FlyHeight;
//    float FlyRate;
    float TurnSpeed;
    float PropWindow;
    float AcquireRange;
} UNITINFO;

typedef struct {
    EVENTTYPE type;
    LPEDICT edict;
    LPEDICT source;
    LPEVENT responseTo;
} GAMEEVENT;

typedef enum {
    GAME_MSG_HARVEST_MOVE_GOLD,
    GAME_MSG_HARVEST_ENTER_MINE,
    GAME_MSG_HARVEST_RETURN_GOLD,
    GAME_MSG_HARVEST_DEPOSIT_GOLD,
    GAME_MSG_HARVEST_RESUME_GOLD,
    GAME_MSG_HARVEST_MOVE_LUMBER,
    GAME_MSG_HARVEST_START_CHOP,
    GAME_MSG_HARVEST_CHOP,
    GAME_MSG_HARVEST_TREE_FELLED,
    GAME_MSG_HARVEST_RETURN_LUMBER,
    GAME_MSG_HARVEST_DEPOSIT_LUMBER,
    GAME_MSG_HARVEST_RESUME_LUMBER,
} GAMEMSGTYPE;

typedef struct {
    GAMEMSGTYPE type;
    DWORD actor;
    DWORD target;
} GAMEMSG;
typedef GAMEMSG const *LPCGAMEMSG;
typedef void (*gameMsgFn)(LPCGAMEMSG, void *);

typedef struct {
    gameMsgFn fn;
    void *ctx;
} GAMEMSGSUB;

typedef struct {
    GAMEMSGSUB subs[MAX_MESSAGE_SUBSCRIBERS];
} GAMEMESSAGES;

typedef struct {
    DWORD class_id;
    VECTOR2 origin;
} gitem_t;

struct gquestitem_s {
    LPSTR description;
    LPQUESTITEM next;
    BOOL completed;
};

struct gquest_s {
    LPSTR title;
    LPSTR description;
    LPSTR iconPath;
    LPQUESTITEM items;
    LPQUEST next;
    BOOL discovered;
    BOOL required;
    BOOL completed;
    BOOL failed;
    BOOL enabled;
};

typedef struct {
    struct { FLOAT day, night; } sight_radius;
    FLOAT acquisition_range;
    DWORD flags;
} unitbalance_t;

#define UNIT_BALANCE_BUILDING 0x1 // bit; immutable building classification; used by hot AI/FOW paths

typedef struct {
    DWORD code;
    DWORD level;
} heroability_t;

typedef struct {
    DWORD code;
    DWORD level;
    DWORD timestamp;
} heroabilitystatus_t;

struct edict_s {
    entityState_t s;
    LPGAMECLIENT client;
    pathTex_t *pathtex;
    FLOAT collision;
    BOX2 bounds;
    DWORD svflags;
    DWORD selected;
    DWORD areanum;
    LINK area;
    BOOL inuse;
    BOX2 areabounds;

    // keep above in sync with server.h
    DWORD class_id;
    DWORD variation;
    DWORD build_project;
    struct {
        BOOL active;
        BOOL paused;
        LPEDICT primary_builder;
        FLOAT progress;
    } construction;
    struct {
        BOOL primary;
        FLOAT gold_accum;
        FLOAT lumber_accum;
    } buildwork;
    DWORD spawn_time;
    DWORD harvested_lumber;
    DWORD harvested_gold;
    DWORD heatmap2;
    VECTOR2 heatmap2_origin;  /* target position when heatmap2 was last built */
    DWORD heatmap2_time;      /* level.time when heatmap2 was last built */
    FLOAT heatmap2_radius;    /* mover collision radius used for heatmap2 */
    DWORD peonsinside;
    DWORD aiflags;
    DWORD damage;
    DWORD resources;
    DWORD freetime;
    struct {
        LPEDICT mine;
        DWORD mine_spawn_time;
        BOOL restore_invulnerable;
    } goldmine;
    LPEDICT inventory[MAX_INVENTORY];
    struct {
        LPEDICT carrier;
        LONG inventory_slot;
        BOOL in_world;
        DWORD charges;
    } item;
    struct {
        BOOL initialized;

        /* Set only for destructables originating from war3map.doo. */
        BOOL map_placed;

        /*
         * During generated map initialization, CreateDestructable() binds named
         * gg_dest_* handles back to these already-created map instances.
         * One preplaced instance may be claimed only once.
         */
        BOOL script_bound;

        BOOL dead;
        BOOL pathing_active;
        BOOL placement_solid;
        BOOL loot_processed;

        DWORD editor_id;
        DWORD item_table;

        pathTex_t *alive_pathtex;
        pathTex_t *death_pathtex;
        FLOAT alive_collision;

        ARRAY(droppableItemSet_t const, drop_sets);
    } destructable;
    struct {
        LPEDICT units[MAX_CARGO];
        DWORD count;
    } cargo;
    FLOAT velocity;
    doodadHero_t hero;
    heroability_t heroabilities[MAX_HERO_ABILITIES];
    heroabilitystatus_t abilstatus[MAX_UNIT_STATUSES];
    BOOL invulnerable;  // unit cannot take damage when true
    BOOL paused;        // unit AI and movement suspended when true
    BOOL stunned;       // unit AI and movement suspended by timed status
    BOOL no_pathing;    // pathfinding disabled when true
    struct {
        DWORD code;     // ability code being channeled (0 = none)
        VECTOR2 origin; // position when channel started (movement cancels channel)
    } channel;
    DWORD unit_color;   // explicit per-unit color override (0 = use owner color)
    VECTOR2 old_origin;
    struct {
        VECTOR2 last_origin;
        FLOAT last_distance;
        DWORD blocked_frames;
        DWORD flow_generation; /* active static-route field selected this tick */
        BOOL flow_goal_reached; /* mover occupies the route's adjusted goal cell */
        BOOL flow_unreachable;  /* field exists but current cell has no route */
        BOOL flow_direct;       /* static path from mover to requested goal is clear */
        FLOAT group_speed;  // slowest member's speed for a group move (0 = no cap), keeps the group together
        FLOAT heading;      // avoidance-resolved heading chosen this tick by unit_changeangle; movement follows it
        LPEDICT attackmove_waypoint;  // resume attack-move after a combat detour
        LPEDICT patrol_a, patrol_b, patrol_target;
        BOOL holding_position;
    } movement;
    EDICTSTAT health;
    EDICTSTAT mana;
    MOVETYPE movetype;
    TARGTYPE targtype;
    LPEDICT goalentity;
    LPEDICT combatentity;
    LPEDICT secondarygoal;
    LPEDICT owner;
    LPEDICT build;
    LPCANIMATION animation;
    unitbalance_t runtime;
    umove_t *currentmove;
    unitRace_t race;
    FLOAT wait;
    UNITINFO unitinfo;
    unitAttack_t attack1;
    unitAttack_t attack2;
    DWORD defense_type;   /* WC3 defType index: small/medium/large/fort/normal/hero/divine/none */
    FLOAT armor_value;    /* computed armor ('realdef', incl. hero AGI) for damage reduction */
    struct {
        BYTE select[MAX_UNIT_SELECT_SOUNDS];
        BYTE num_select;
        BYTE yes[MAX_UNIT_SELECT_SOUNDS];   /* order confirmation ("Yes" sounds) */
        BYTE num_yes;
        BYTE chop[3]; BYTE num_chop;        /* weapon-vs-wood impact variants */
        BYTE pending;
        int attack, death;
    } sound;

    void (*stand)(LPEDICT);
    void (*birth)(LPEDICT);
    void (*prethink)(LPEDICT);
    void (*think)(LPEDICT);
    void (*die)(LPEDICT, LPEDICT);
    void (*idle)(LPEDICT);
    void (*move)(LPEDICT);
    void (*run)(LPEDICT);
    void (*attack)(LPEDICT);
    void (*pain)(LPEDICT);

    UnitProfile_t const *UnitProfile;
    UnitBalance_t const *UnitBalance;
    UnitData_t const *UnitData;
    UnitUI_t const *UnitUI;
    UnitWeapons_t const *UnitWeapons;
    UnitAbilities_t const *UnitAbilities;
    Doodads_t const *Doodads;
    ItemData_t const *ItemData;
    DestructableData_t const *DestructableData;
};

/* An entity that should be ignored by collision and physics: dead, hidden, or
 * not a live model.  Shared by g_phys.c (M_CheckCollision) and g_ai.c
 * (collision-aware movement). */
#define IS_HOLLOW(ent) ((ent->svflags & SVF_DEADMONSTER) || (ent->s.renderfx & RF_HIDDEN) || !ent->s.model || !ent->inuse)

struct game_locals {
    DWORD max_clients;
    DWORD num_abilities;
    LPGAMECLIENT clients;
    struct {
        stbIniCache_t theme;
        stbIniCache_t misc;
    } config;
    struct {
        FLOAT attackHalfAngle;
        FLOAT maxCollisionRadius;
        FLOAT decayTime;
        FLOAT boneDecayTime;
        FLOAT dissipateTime;
        FLOAT structureDecayTime;
        FLOAT bulletDeathTime;
        FLOAT closeEnoughRange;
        FLOAT dawnTimeGameHours;
        FLOAT duskTimeGameHours;
        FLOAT gameDayHours;
        FLOAT gameDayLength;
        FLOAT buildingAngle;
        FLOAT rootAngle;
    } constants;
};

struct gevent_s {
    LPEVENT next;
    LPEDICT subject;
    EVENTTYPE type;
    LPTRIGGER trigger;
    REGION region;
    FLOAT range;
};

typedef struct {
    DWORD texture;
    BLEND_MODE blendmode;
    TEXMAP_FLAGS texmapflags;
    struct {
        BOX2 uv;
        COLOR32 color;
        DWORD time;
    } start, end;
    BOOL displayed;
} CINEFILTER;

typedef struct {
    LPEVENT handlers;
    GAMEEVENT queue[MAX_EVENT_QUEUE];
    DWORD write, read;
} LEVELEVENTS;

typedef struct {
    BYTE *visible;
    BYTE *explored;
    BYTE *visible_rows;
    BYTE *dirty_visible_rows;
    BYTE *dirty_explored_rows;
    BOOL client_connected;
} fowPlayerGrid_t;

typedef struct {
    DWORD width;
    DWORD height;
    BOX2 bounds;
    BYTE *blocked;
    DWORD num_blocked;
    ARRAY(DWORD, rim_cells);
    fowPlayerGrid_t players[MAX_PLAYERS];
} fowGrid_t;

/* A fog modifier reveals (or masks) a region for a player while started.
 * FOG_OF_WAR_VISIBLE (state 4) reveals the region so units inside it become
 * visible; cinematics use these to show staged units in unexplored areas. */
typedef struct fogmodifier_s {
    DWORD player;
    DWORD state;             /* fogstate value: 4 = FOG_OF_WAR_VISIBLE */
    BOOL is_rect;
    BOX2 rect;               /* used when is_rect */
    VECTOR2 center;          /* used when !is_rect */
    FLOAT radius;            /* used when !is_rect */
    BOOL use_shared_vision;
    BOOL started;
} FOGMODIFIER, *LPFOGMODIFIER;
typedef FOGMODIFIER const *LPCFOGMODIFIER;

struct level_locals {
    LPJASS vm;
    LPCMAPINFO mapinfo;
    struct {
        char name[MAX_PATHLEN], description[MAX_TRIGSTR_LENGTH];
        DWORD teams, players, game_types, game_type, map_flags;
        DWORD placement, speed, difficulty, resource_density, creature_density;
        DWORD forced_start_locations;
        struct {
            DWORD count;
            struct { LONG location; DWORD priority; } slots[MAX_START_PRIO];
        } start_prio[MAX_PLAYERS];
    } setup;
    LEVELEVENTS events;
    GAMEMESSAGES messages;
    LPQUEST quests;
    USHORT alliances[MAX_PLAYERS][MAX_PLAYERS];
    fowGrid_t fow;
    CINEFILTER cinefilter;
    DWORD framenum;
    DWORD time;
    BOOL started;
    BOOL scriptsStarted;
};

typedef struct {
    LPCSTR id;
    size_t row_offset;
    size_t field_offset;
    bzFieldType_t type;
} unitMeta_t;

#define UITRIGGER_T_DEFINED
typedef struct {
    LPCSTR name;
    void (*callback)(LPEDICT, LPCFRAMEDEF);
} uiTrigger_t;

// g_main.c
LPPLAYER G_GetPlayerByNumber(DWORD);
LPEDICT G_GetPlayerEntityByNumber(DWORD);
LPGAMECLIENT G_GetPlayerClientByNumber(DWORD);
void G_SetClientConnected(LPEDICT player, BOOL connected);
TARGTYPE G_GetTargetType(LPCSTR);
LPCSTR G_LevelString(LPCSTR);
FLOAT G_Cinefade(void);
BOOL G_SkipCutscene(void);
VECTOR2 G_ClampCameraPosition(LPGAMECLIENT client, LPCVECTOR2 position);
void G_SetClientCameraBounds(LPGAMECLIENT client, FLOAT const bounds[8]);
void G_ClearCameraTarget(LPGAMECLIENT client, LPCSTR func);
void G_SetPlayerText(LPGAMECLIENT, PLAYERTEXT, LPCSTR);
GAMEEVENT *G_PublishEvent(LPEDICT, EVENTTYPE);
GAMEEVENT *G_PublishEventWithSource(LPEDICT, EVENTTYPE, LPEDICT);
BOOL G_SubscribeMessage(gameMsgFn, void *);
void G_UnsubscribeMessage(gameMsgFn, void *);
void G_PublishMessage(LPEDICT, GAMEMSGTYPE, LPEDICT);

// g_fow.c
void G_FowInit(void);
void G_FowShutdown(void);
void G_FowConnectPlayer(DWORD player);
void G_FowUpdate(void);
void G_FowMarkBlockersDirty(void);
void G_FowSendDeltas(void);
void G_FowSendFull(LPEDICT ent);
BOOL G_FowPlayerCanSeeEntity(DWORD player, LPCEDICT ent);
BOOL G_FowPlayerCanHoverEntity(DWORD player, LPCEDICT ent);
void G_FogModifierStart(LPFOGMODIFIER mod);
void G_FogModifierStop(LPFOGMODIFIER mod);
DWORD G_FowWorldToCellX(FLOAT x);
DWORD G_FowWorldToCellY(FLOAT y);
BOOL G_IsNight(void);

// g_spawn.c
LPEDICT G_Spawn(void);
void SP_CallSpawn(LPEDICT);
void G_BindEntityData(LPEDICT);
void G_SpawnEntities(void);
BOOL SP_FindEmptySpaceAround(LPEDICT, DWORD, LPVECTOR2, FLOAT *);
BOOL SP_FindUnitExitPosition(LPEDICT producer, LPEDICT unit, LPVECTOR2 out, FLOAT *angle);
LPEDICT SP_SpawnAtLocation(DWORD, DWORD, LPCVECTOR2);
LPEDICT G_CreateDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation);
LPEDICT G_CreateDeadDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation);
BOOL G_IsDestructable(LPCEDICT ent);
void SP_monster_tree(LPEDICT);

LPEDICT Waypoint_add(LPCVECTOR2);
void M_CheckGround (LPEDICT);
void monster_start(LPEDICT);
void monster_think(LPEDICT);

// g_model.c
int          G_RegisterModel(LPCSTR filename);
LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
void         G_FreeModels(void);

// g_ai.c
void ai_birth(LPEDICT);
void ai_stand(LPEDICT);
void ai_pain(LPEDICT);
void ai_idle(LPEDICT);
void unit_runwait(LPEDICT, void (*callback)(LPEDICT ));
void unit_stand(LPEDICT);
void unit_entercombat(LPEDICT, LPEDICT);
void unit_leavecombat(LPEDICT);
BOOL unit_affectingcombat(LPEDICT);
void unit_updatestatuses(LPEDICT);

// g_monster.c
void unit_moveindirection(LPEDICT);
void unit_changeangle(LPEDICT);
void unit_changeangle_towards_point(LPEDICT, LPCVECTOR2);
void unit_changeangle_for_radius(LPEDICT, FLOAT);
BOOL M_MoveIsValid(LPEDICT self, LPCVECTOR2 pos);
BOOL M_CheckAttack(LPEDICT);
BOOL unit_is_walking(LPCEDICT);
void unit_setanimation(LPEDICT, LPCSTR);
void unit_setmove(LPEDICT, umove_t *);
void M_MoveFrame(LPEDICT);
FLOAT M_DistanceToGoal(LPEDICT);
FLOAT unit_movedistance(LPEDICT);
DWORD M_RefreshHeatmap(LPEDICT, FLOAT);
BOOL M_IsDead(LPEDICT);
void SP_SpawnUnit(LPEDICT);
DWORD unit_spawn_aiflags(DWORD);
void SP_TrainUnit(LPEDICT, DWORD);
BOOL player_pay(LPPLAYER, DWORD);
BYTE compress_stat(EDICTSTAT const *);
DWORD G_LoadShadowTexture(LPCSTR, BOOL);

// g_pathing.c
pathTex_t *LoadTGA(BYTE const*, size_t);
pathTex_t *M_LoadPathTex(LPCSTR filename);

// g_move.c
BOOL SV_CloseEnough(LPEDICT, LPCEDICT, FLOAT);

// g_phys.c
void G_RunEntity(LPEDICT);
void G_SolveCollisions(void);
BOOL M_CheckCollision(LPCVECTOR2, FLOAT);
void G_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 direction);
void G_PushEntity3(LPEDICT ent, FLOAT distance, LPCVECTOR3 direction);

// g_abilities.c
ability_t const *FindAbilityByClassname(LPCSTR);
ability_t const *FindAbilityForCommand(LPCSTR);
ability_t const *GetAbilityByIndex(DWORD);
DWORD FindAbilityIndex(LPCSTR);
void InitAbilities(void);
void SetAbilityNames(void);

// g_metadata.c
LPCSTR FindConfigValue(LPCSTR, LPCSTR);
LPCSTR GetClassName(DWORD);

// g_unit_ui.c (Phase 8)
BYTE G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons);
BOOL G_BuildCommandButton(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, gameCommandButton_t *button);
BOOL G_BuildAllEnabled(void);
BOOL G_WorkerCanBuild(LPEDICT worker, DWORD building_id);
buildCommandState_t G_GetBuildCommandState(LPGAMECLIENT client, LPEDICT worker, DWORD building_id, LPSTR reason, DWORD reason_size);
BOOL G_ChargeBuilding(LPGAMECLIENT client, DWORD building_id);
void G_RefundBuilding(LPGAMECLIENT client, DWORD building_id);
void G_SnapBuildingPoint(DWORD building_id, LPVECTOR2 point);
buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, DWORD building_id, LPCVECTOR2 requested, LPVECTOR2 snapped);
FLOAT G_BuildApproachDistance(DWORD building_id);
BOOL G_StartHumanConstruction(LPEDICT builder, LPEDICT building);
void G_CompleteConstruction(LPEDICT building);
BOOL G_UnitHasHumanRepair(LPEDICT ent);
void G_SetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid, LONG maximum);
LONG G_GetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid);
void G_SetPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG level_value);
void G_AddPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG levels);
LONG G_GetPlayerTechResearchedLevel(LPGAMECLIENT client, DWORD techid);
LONG G_GetPlayerTechCountValue(LPGAMECLIENT client, DWORD techid);
BOOL G_BuildInventoryItem(LPEDICT ent, LPEDICT item, BYTE slot, gameInventoryItem_t *out);
BYTE G_GetInventory(LPEDICT ent, gameInventoryItem_t *items, BYTE max_items);
BYTE G_GetBuildQueue(LPEDICT ent, gameQueueItem_t *queue, BYTE max_queue);

// g_ai.c
LPEDICT G_GetMainSelectedUnit(LPGAMECLIENT);
void Get_Commands_f(LPEDICT);
void CMD_CancelCommand(LPEDICT ent);
BOOL G_CancelBuildPlacement(LPEDICT clent);
void Get_Portrait_f(LPEDICT);
void G_RefreshInventoryLayer(LPEDICT);
void G_RefreshInfoPanel(LPEDICT);
void G_UpdateClientInfoPanels(void);
void G_RefreshResourceBar(LPEDICT);
void G_UpdateClientResourceBars(void);
void UI_AddCancelButton(LPEDICT);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_AddCommandButton(LPCSTR);
void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level);
void UI_WriteTooltipFrame(void);
void UI_SetCurrentClient(LPGAMECLIENT client);
void UI_ShowInterface(LPEDICT, BOOL, FLOAT);
void UI_ShowText(LPEDICT, LPCVECTOR2, LPCSTR, FLOAT);
LPCSTR GetBuildCommand(unitRace_t);
void UI_RenderRoute(LPEDICT, LPCSTR);
void UI_ShowMainMenu(LPEDICT);
void UI_ShowRealmSelect(LPEDICT, BOOL);
void UI_ShowSinglePlayerMenu(LPEDICT);
void UI_ShowMultiplayerMenu(LPEDICT);
void UI_ShowMultiplayerCreateMenu(LPEDICT);
void UI_ShowMultiplayerGameSetupMenu(LPEDICT, DWORD);
void UI_ShowGameInterface(LPEDICT);
void UI_WriteCinematicLayer(LPEDICT);
void UI_ShowMapSelectMenu(LPEDICT, LPCSTR);
void UI_ShowMultiplayerCreateMapInfo(LPEDICT);
void UI_ClearCreateGameSlots(void);
void UI_AddCreateGameSlot(DWORD, LPCSTR, LPCSTR, LPCSTR, DWORD);

// p_fdf.c
void UI_PrintClasses(void);
void UI_ClearTemplates(void);
void UI_ParseFDF(LPCSTR);
void UI_ParseFDF_Buffer(LPCSTR, LPSTR);
void UI_SetAllPoints(LPFRAMEDEF);
void UI_SetParent(LPFRAMEDEF, LPCFRAMEDEF);
void UI_SetText(LPFRAMEDEF, LPCSTR, ...);
void UI_SetOnClick(LPFRAMEDEF, LPCSTR, ...);
void UI_SetTextPointer(LPFRAMEDEF, LPCSTR);
void UI_SetSize(LPFRAMEDEF, FLOAT, FLOAT);
void UI_SetTexture(LPFRAMEDEF, LPCSTR, BOOL);
void UI_SetTexture2(LPFRAMEDEF, LPCSTR, BOOL);
void UI_WriteLayout(LPEDICT, LPCFRAMEDEF, DWORD);
void UI_WriteStart(DWORD);
void UI_ClearLayer(LPEDICT, DWORD);
void UI_ShowGameResult(LPEDICT, BOOL);
void UI_HideGameResult(LPEDICT);
void UI_ShowQuests(LPEDICT);
void UI_HideQuests(LPEDICT);
void UI_WriteWithTriggers(LPEDICT, LPCFRAMEDEF, DWORD, uiTrigger_t const *);
void UI_SetPoint(LPFRAMEDEF, UIFRAMEPOINT, LPCFRAMEDEF, UIFRAMEPOINT, FLOAT, FLOAT);
void UI_InitFrame(LPFRAMEDEF, FRAMETYPE);
void UI_SetHidden(LPFRAMEDEF, BOOL);
void UI_InheritFrom(LPFRAMEDEF, LPCSTR);
DWORD UI_FindFrameNumber(LPCSTR);
DWORD UI_LoadTexture(LPCSTR, BOOL);
LPCSTR UI_GetString(LPCSTR);
LPFRAMEDEF UI_Spawn(FRAMETYPE, LPFRAMEDEF);
LPFRAMEDEF UI_FindFrame(LPCSTR);
LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF, LPCSTR);
LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF, LPCSTR);

LPCSTR Theme_String(LPCSTR, LPCSTR);
FLOAT Theme_Float(LPCSTR, LPCSTR);

// ui_write.c
void UI_WriteFrame(LPCFRAMEDEF);
void UI_WriteFrameWithChildren(LPCFRAMEDEF, LPCFRAMEDEF);
void UI_WriteFrameWithChildrenWithTriggers(LPEDICT, LPCFRAMEDEF, LPCFRAMEDEF, uiTrigger_t const *);
BOOL UI_BuildFrameForWrite(LPCFRAMEDEF frame,
                           LPUIFRAME out,
                           LPBYTE typedata,
                           DWORD typedata_max,
                           LPSTR textbuf,
                           DWORD textbuf_max);

// g_metadata.c
LPCSTR UnitMetaString(LPEDICT, DWORD);
LONG UnitMetaInteger(LPEDICT, DWORD);
BOOL UnitMetaBoolean(LPEDICT, DWORD);
FLOAT UnitMetaReal(LPEDICT, DWORD);

void InitUnitData(void);
void ShutdownUnitData(void);
#ifdef BZ_TESTS
typedef struct { LPCSTR text; void *rows; DWORD count; } slkTestData_t;
slkTestData_t *G_SetSLKRows(LPCSTR, slkTestData_t *);
slkTestData_t *G_SetProfileRows(slkTestData_t *);
#endif
void G_RegisterSelectSounds(LPEDICT, LPCSTR);
void G_RegisterGlobalSounds(void);  /* register world sounds (tree fall, etc.) at map init */
extern int g_treeFallSounds[3];     /* Sound\Destructibles\TreeFall{1,2,3}.wav configstring indices */
extern BYTE g_numTreeFallSounds;

// g_command.c
void G_SelectEntity(LPGAMECLIENT, LPEDICT);
void G_DeselectEntity(LPGAMECLIENT, LPEDICT);
BOOL G_IsEntitySelected(LPGAMECLIENT, LPEDICT);
void G_QueueSelectionSound(LPEDICT);
void G_ClientCommand(LPEDICT, DWORD, LPCSTR[]);
void G_ClientSetCameraPosition(LPEDICT, LPCVECTOR2);

//  s_skills.c
FLOAT AB_Data(LPCSTR, DWORD, DWORD);
DWORD GetAbilityIndex(ability_t const *);

// g_combat.c
void T_Damage(LPEDICT, LPEDICT, int);

// g_utils.c
void G_FreeEdict(LPEDICT);
LPEVENT G_MakeEvent(EVENTTYPE);
LPQUEST G_MakeQuest(void);
BOOL G_RegionContains(LPCREGION, LPCVECTOR2);
void G_RemoveQuest(LPQUEST);
void G_SetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE, BOOL);
BOOL G_GetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE);

// m_unit.c
BOOL unit_issueorder(LPEDICT, LPCSTR, LPCVECTOR2);
BOOL unit_issueimmediateorder(LPEDICT, LPCSTR);
BOOL unit_issuetargetorder(LPEDICT, LPCSTR, LPEDICT);
LPEDICT unit_createorfind(DWORD, DWORD, LPCVECTOR2, FLOAT);
BOOL unit_additemtoslot(LPEDICT, LPEDICT, DWORD);
BOOL unit_additem(LPEDICT, LPEDICT);
void unit_addstatus(LPEDICT, LPCSTR, DWORD);
void unit_addtimedstatus(LPEDICT, LPCSTR, DWORD, FLOAT);
void unit_learnability(LPEDICT, DWORD);
void G_RecomputeHeroStats(LPEDICT);
DWORD G_MaxHeroLevel(void);
DWORD G_HeroXPForLevel(DWORD level);
DWORD G_HeroLevelForXP(DWORD xp);
void G_HeroApplyLevel(LPEDICT, DWORD level);
void G_HeroSetXP(LPEDICT, DWORD xp);
void G_GrantKillXP(LPEDICT victim, LPEDICT killer);
void G_ReviveHero(LPEDICT, FLOAT x, FLOAT y);
BOOL G_UnitIsHero(LPCEDICT ent);
BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code);
LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level);

void order_attack(LPEDICT, LPEDICT);
void order_move(LPEDICT, LPEDICT);
void order_stop(LPEDICT);
void order_attackmove(LPEDICT, LPEDICT);
void order_patrol(LPEDICT, LPEDICT);
void order_patrol_resume(LPEDICT);
extern umove_t holdpos_move_stand;
extern umove_t holdpos_move_stand_ready;
void unit_stand(LPEDICT);
BOOL G_ActorHasSkill(LPEDICT, LPCSTR);
BOOL S_GoldMineIsMine(LPCEDICT);
DWORD S_GoldMineMaximumGold(LPCEDICT);
FLOAT S_GoldMineMiningDuration(LPCEDICT);
DWORD S_GoldMineCapacity(LPCEDICT);
BOOL S_GoldMineCanHarvest(LPCEDICT);
BOOL S_GoldMineWorkerIsInside(LPCEDICT);
void S_GoldMineInitUnit(LPEDICT);
void S_GoldMineReleaseWorker(LPEDICT);
void harvest_start(LPEDICT, LPEDICT);
void harvest_gold_start(LPEDICT, LPEDICT);
BOOL harvest_lumber_return_to(LPEDICT, LPEDICT);
BOOL harvest_gold_return_to(LPEDICT, LPEDICT);
void cargo_drop_all(LPEDICT);
void blight_mine_think(LPEDICT);
BOOL move_selectlocation(LPEDICT, LPCVECTOR2);
BOOL move_should_arrive(LPEDICT, FLOAT);
BOOL move_is_blocked(LPEDICT, FLOAT, FLOAT);
void move_reset_progress(LPEDICT);
LPEDICT G_FindNearestEnemy(LPEDICT, FLOAT);
FLOAT G_AcquisitionRange(LPCEDICT);
BOOL G_ShouldAcquireThisFrame(LPCEDICT);

// p_jass.c
LPJASS jass_newstate(void);
void jass_close(LPJASS);
BOOL jass_dofile(LPJASS, LPCSTR);
BOOL jass_dofilenative(LPJASS, LPCSTR);
void jass_callbyname(LPJASS, LPCSTR, BOOL);
BOOL jass_dobuffer(LPJASS, LPSTR);
void jass_runevents(LPJASS);

// g_events.c
void G_RunEntities(void);
void G_RunEvents(void);

// g_items.c
void SP_SpawnItem(LPEDICT);
BOOL G_IsItem(LPCEDICT item);
DWORD G_InventoryCapacity(LPCEDICT unit);
BOOL G_UnitHasInventory(LPEDICT unit);
DWORD G_ItemCharges(LPCEDICT item);
void G_SetItemCharges(LPEDICT item, DWORD charges);
LONG G_FindFreeInventorySlot(LPCEDICT unit);
BOOL G_CanPickupItem(LPEDICT unit, LPEDICT item);
BOOL G_AddItemToSlot(LPEDICT unit, LPEDICT item, DWORD slot);
BOOL G_PickupItem(LPEDICT unit, LPEDICT item);
BOOL G_OrderPickupItem(LPEDICT unit, LPEDICT item);
BOOL G_DropItemAt(LPEDICT unit, DWORD slot, LPCVECTOR2 position);
BOOL G_DropItem(LPEDICT unit, DWORD slot);
void G_RemoveItem(LPEDICT item);
void G_UseItem(LPEDICT unit, DWORD slot);
DWORD G_ItemTypeFromClass(LPCSTR cls);

// g_destructable.c
void G_SetDestructableScriptBinding(BOOL enabled);
void G_ActivateScriptedDestructable(LPEDICT ent,
                                    FLOAT x,
                                    FLOAT y,
                                    FLOAT z,
                                    FLOAT facing,
                                    FLOAT scale,
                                    DWORD variation);
BOOL G_IsDestructable(LPCEDICT ent);
BOOL G_DestructableIsAttackable(LPCEDICT ent);
void G_InitializeDestructablePlacement(LPEDICT ent, LPCDOODAD placement);
BOOL G_DestructableApplyDamage(LPEDICT ent, LPEDICT attacker, FLOAT damage);
BOOL G_KillDestructable(LPEDICT ent, LPEDICT killer);
BOOL G_SetDestructableDeadState(LPEDICT ent, BOOL process_death);
BOOL G_RemoveDestructable(LPEDICT ent);
BOOL G_SetDestructableLife(LPEDICT ent, FLOAT life);
BOOL G_RestoreDestructable(LPEDICT ent, FLOAT life, BOOL birth);
DWORD G_SelectDropItem(droppableItem_t const *entries, DWORD count, DWORD roll);
DWORD G_SelectRandomTableItem(mapRandomItem_t const *entries, DWORD count, DWORD roll);
mapRandomItemTable_t const *G_FindRandomItemTable(DWORD table_number);
void G_SpawnDestructableLoot(LPEDICT ent);
void G_DestructableStartDeathAnimation(LPEDICT ent);
void G_DestructableStartAliveAnimation(LPEDICT ent, BOOL birth);
void tree_die(LPEDICT ent, LPEDICT attacker);

// ui_init
void UI_Init(void);

// globals
extern struct game_locals game;
extern struct game_export globals;
extern struct game_import gi;
extern struct level_locals level;
extern struct edict_s *g_edicts;

extern unitMeta_t const UnitsMetaData[];

#endif
