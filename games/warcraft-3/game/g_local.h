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

#define SAFE_CALL(FUNC, ...) if (FUNC) FUNC(__VA_ARGS__)
#define ABILITY(NAME) void M_##NAME(LPEDICT ent, LPEDICT target)
#define SEL_SCALE 72
#define MAX_BUILD_QUEUE 7
#define MAX_EVENT_QUEUE 256
#define MAX_MESSAGE_SUBSCRIBERS 8 // callbacks; bounded because messages are synchronous and game-local
#define MAX_UNIT_SELECT_SOUNDS 6 // sounds; largest UnitAckSounds *What variant list in ROC/TFT data
#define WC3_PATH_WORK_BUDGET 32768 // queue pops/server frame; completes a 256x256 open field in two 10 Hz ticks
#define BZ_STRINGIFY_INNER(value) #value
#define BZ_STRINGIFY(value) BZ_STRINGIFY_INNER(value)
#define MAX_ENTITIES MAX_GAME_ENTITIES
#define MAX_REGION_SIZE 16
#define MAX_INVENTORY 6
#define ITEM_PICKUP_RANGE 150.0f /* world units; classic contextual-pickup reach */
#define MAX_CARGO 8
#define MAX_HERO_ABILITIES 4
#define MAX_ABILITIES 16 // slots; extra ability codes granted or stripped at runtime
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

#define UI_WRITE_LAYER(ent, BuildUI, layer, ...) do { \
    UI_SetCurrentClient((ent)->client); \
    UI_WriteStart(layer); \
    BuildUI((ent)->client, ##__VA_ARGS__); \
    gi.Write(PF_LONG, &(LONG){0}); \
    gi.Write(PF_SHORT, &(LONG){0}); \
    gi.unicast(ent); \
    UI_SetCurrentClient(NULL); \
} while (0)


#define FOR_SELECTED_UNITS(CLIENT, ENT) \
FILTER_EDICTS(ENT, G_IsEntitySelected(CLIENT, ENT))

#define FOR_CONTROLLABLE_SELECTED_UNITS(CLIENT, ENT) \
FILTER_EDICTS(ENT, G_IsEntitySelected(CLIENT, ENT) && G_UnitCanControl(CLIENT, ENT))

KNOWN_AS(jass_s, JASS);
KNOWN_AS(gcamerasetup_s, CAMERASETUP);
KNOWN_AS(gregion_s, REGION);
KNOWN_AS(gevent_s, EVENT);
KNOWN_AS(gtrigger_s, TRIGGER);
KNOWN_AS(gtimer_s, GTIMER);
KNOWN_AS(gquest_s, QUEST);
KNOWN_AS(gquestitem_s, QUESTITEM);

typedef enum {
    BUILD_COMMAND_ABSENT,
    BUILD_COMMAND_HIDDEN,
    BUILD_COMMAND_DISABLED,      /* visible but inert: unmet prerequisite */
    BUILD_COMMAND_UNAFFORDABLE,  /* visible/clickable: report resource shortage */
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
    LONG in_progress;
    LONG max_allowed; /* -1 = unlimited/default */
} playerTechState_t;

typedef struct {
    BOOL (*on_entity_selected)(LPEDICT, LPEDICT);
    BOOL (*on_location_selected)(LPEDICT, LPCVECTOR2);
    void (*cmdbutton)(LPEDICT, DWORD);
    void (*refresh)(LPEDICT);
    DWORD ability_code;
    BOOL supports_order_queue; /* active target mode accepts Shift chaining */
    BOOL order_queued;         /* transient modifier for the current target callback */
    BOOL ability_off;          /* command-card separate-off variant selected for this dispatch */
} menu_t;
typedef menu_t clientMenu_s;

enum {
    AI_HOLD_FRAME = 1 << 0,
    AI_FLYING     = 1 << 1,  /* air-layer unit (movetp "fly"): ignores ground collision */
    AI_IMMOBILE   = 1 << 2,  /* fixed unit: may act, but never translates or changes facing */
    AI_AUTOCAST_REPAIR = 1 << 3, /* persisted Repair-family autocast toggle */
    AI_AUTOCAST_ACTIVE = 1 << 4, /* fast unit-wide marker: some autocast ability is enabled */
    AI_ILLUSION    = 1 << 5,  /* summoned copy created by illusion abilities */
};

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
    SELECT_RELATION_FRIEND,
    SELECT_RELATION_NEUTRAL,
    SELECT_RELATION_ENEMY,
} selectionRelation_t;

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

enum {
    WC3_GAME_STATE_TIME_OF_DAY = 2,
};

typedef enum {
    WC3_LIMITOP_LESS_THAN = 0,
    WC3_LIMITOP_LESS_THAN_OR_EQUAL = 1,
    WC3_LIMITOP_EQUAL = 2,
    WC3_LIMITOP_GREATER_THAN_OR_EQUAL = 3,
    WC3_LIMITOP_GREATER_THAN = 4,
    WC3_LIMITOP_NOT_EQUAL = 5,
} WC3LIMITOP;

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
    FLOAT near_z;
//    FLOAT angle_of_attack;
    FLOAT fov;      /* vertical field of view in degrees */
//    FLOAT roll;
//    FLOAT rotations;
    FLOAT z_offset;
    VECTOR3 viewangles;
    VECTOR2 position;
};

#define WC3_MESSAGE_LOG_MAX_ENTRIES 128 // entries; bounded per-client message history for the Message Log dialog
#define WC3_MESSAGE_LOG_ENTRY_SIZE 1024 // bytes; maximum stored Message Log entry length

struct client_s {
    PLAYER ps;
    BOOL connected; /* ClientBegin completed for this reserved player edict. */
    BOOL commands_dirty; /* authoritative command availability changed; rebuild after simulation */
    BOOL presentation_dirty; /* dialogue/interface/selected-portrait state changed; flush svc_layout after simulation */
    struct {
        DWORD race_pref, controller;
        BYTE tax[MAX_PLAYERS][PLAYERSTATE_LUMBER_GATHERED + 1];
        FLOAT handicap, handicap_xp;
        BOOL race_selectable, on_score_screen;
        BOOL removed;
        BYTE pending_game_result; /* 0 = none, PLAYER_GAME_RESULT_* + 1 while fallback UI is deferred */
        DWORD pending_game_result_event; /* level.events.read must reach this write ordinal before fallback UI */
        char name[MAX_PATHLEN];
    } jass;
    playerTechState_t tech[MAX_PLAYER_TECH_STATE];
    char playerTextStorage[PLAYERTEXT_COUNT][PLAYER_TEXT_BACKUP][512];
    DWORD playerTextCursor[PLAYERTEXT_COUNT];
    LPCMAPPLAYER mapplayer;
    DWORD ping;
    BOOL no_control, no_ui;
    DWORD modal_flags;
    BOOL quest_dialog_open;
    menu_t menu;
    struct clientCamera_s {
        CAMERASETUP state;
        CAMERASETUP old_state;
        DWORD start_time;
        DWORD end_time;
        VECTOR2 quick_position; /* SetCameraQuickPosition spacebar target; does not move the camera */
        BOOL quick_position_set;
        LPEDICT target_controller;
        VECTOR2 target_offset;
        BOOL target_inherit_orientation;
    } camera;
    /* Single-unit info-panel cache. HP/mana are retained here for save-layout
     * compatibility, but portrait HP/mana now use live player-state bindings. */
    struct {
        DWORD entity;
        LONG hp;
        LONG mana;
        LONG xp;     /* hero experience, so the XP/attribute display updates live */
    } infopanel;
    /* Last resource values reflected in the resource bar, so the server only
     * re-sends LAYER_CONSOLE when a displayed value or tooltip income rate changes. */
    struct {
        LONG gold;
        LONG lumber;
        LONG food_used;
        LONG food_cap;
        LONG gold_rate;
        LONG lumber_rate;
    } resourcebar;
    /* Persistent Hero/idle-worker HUD is rebuilt only after gameplay marks it
     * dirty. last_idle_worker is the cycling cursor, not a per-frame cache. */
    struct {
        BOOL dirty;
        DWORD last_idle_worker;
    } shortcuts;
    LPEDICT rally_indicator;
    struct {
        VECTOR2 position;
        DWORD end_time;        /* game time (ms), 0 = inactive */
        char text[1024];
    } message;
    struct {
        char entries[WC3_MESSAGE_LOG_MAX_ENTRIES][WC3_MESSAGE_LOG_ENTRY_SIZE];
        DWORD first;
        DWORD count;
    } message_log;
    DWORD cinematic_end_time;       /* game time (ms) when current SetCinematicScene expires, 0 = none */
    DWORD cinematic_voice_end_time; /* game time (ms) when Portrait Talk becomes Portrait, 0 = not talking */
};

typedef struct {
    LPCSTR animation;
    void (*think)(LPEDICT);
    void (*endfunc)(LPEDICT);
    struct ability_s *ability;
} umove_t;

/* Player-issued WC3 Shift orders are simulation state, separate from the
 * training/research queue. Targets are retained by edict number + spawn_time
 * so a recycled slot cannot silently retarget an old queued command. */
#define MAX_UNIT_ORDER_QUEUE 16
#define UNIT_ORDER_NAME_SIZE 12

typedef enum {
    UNIT_ORDER_TARGET_NONE,
    UNIT_ORDER_TARGET_POINT,
    UNIT_ORDER_TARGET_ENTITY,
} unitOrderTargetType_t;

typedef struct {
    char order[UNIT_ORDER_NAME_SIZE];
    unitOrderTargetType_t target_type;
    VECTOR2 point;
    DWORD target_number;
    DWORD target_spawn_time;
    DWORD issuer_player;
    FLOAT group_speed;
} unitOrder_t;

typedef struct {
    unitOrder_t entries[MAX_UNIT_ORDER_QUEUE];
    DWORD head;
    DWORD count;
} unitOrderQueue_t;

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

/* Rally state is producer-owned and intentionally separate from the training
 * queue. Zero-initialized RALLY_TARGET_SELF is the Warcraft default: the
 * producer itself is the rally widget until the player chooses another target. */
typedef enum {
    RALLY_TARGET_NONE = -1,
    RALLY_TARGET_SELF = 0,
    RALLY_TARGET_POINT,
    RALLY_TARGET_ENTITY,
} rallyTargetType_t;

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
#define ABILITY_SEPARATE_OFF (1u << 16) /* render an explicit Unart/Untip off button beside the on button */

typedef struct spell_info_s {
    DWORD code;                    /* ability FourCC (must match abilitylist entry) */
    LPCSTR name;                   /* debug / log identifier */
    spellTargetType_t target_type;
    DWORD flags;
    BOOL (*validate)(LPEDICT caster, spellTarget_t target);  /* extra validation before mana/cooldown spend */
    void (*execute)(LPEDICT caster, spellTarget_t target, struct spell_info_s const *spell);
} spell_info_t;


typedef enum {
    WC3_EFFECT_EFFECT = 0,
    WC3_EFFECT_TARGET = 1,
    WC3_EFFECT_CASTER = 2,
    WC3_EFFECT_SPECIAL = 3,
    WC3_EFFECT_AREA_EFFECT = 4,
    WC3_EFFECT_MISSILE = 5,
    WC3_EFFECT_LIGHTNING = 6,
} wc3EffectType_t;

typedef struct ability_s {
    void (*init)(LPCSTR, struct ability_s *);
    void (*cmd)(LPEDICT);
    BOOL (*is_toggle_on)(LPEDICT); /* selects Un* command-card fields when true */
    DWORD flags;
    struct spell_info_s *spell;    /* non-NULL for spells using the unified pipeline */

    /* Keep new dispatch hooks append-only. ability_t is defined in a widely
     * included game header and incremental builds may retain objects compiled
     * against the previous layout; inserting a field above spell changes the
     * offsets of every existing dispatch member. */
    BOOL (*item_use)(LPEDICT); /* synchronous inventory activation; true only when gameplay effect applies */

    /* Optional generic autocast hooks. Keep these append-only for the same ABI
     * reason as item_use above. The unit scheduler owns when to try autocast;
     * each ability owns its toggle state and target acquisition policy. */
    BOOL (*autocast_is_on)(LPEDICT);
    void (*autocast_set)(LPEDICT, BOOL);
    BOOL (*autocast_acquire)(LPEDICT);
} ability_t;

typedef struct {
    attackType_t type;
    weaponType_t weapon;
    VECTOR3 origin;
    DWORD damageBase;
    DWORD numberOfDice;
    DWORD sidesPerDie;
    /* Warsmash keeps permanent range changes separate from temporary green/red
     * attack bonuses. damageBase includes permanentDamageBonus; rolls add
     * temporaryDamageBonus after the dice. */
    FLOAT permanentDamageBonus;
    FLOAT temporaryDamageBonus;
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
typedef EDICTSTAT edictStat_s;

typedef struct edictAbilities_s {
    DWORD added[MAX_ABILITIES];
    DWORD added_count;
    DWORD removed[MAX_ABILITIES];
    DWORD removed_count;
    DWORD permanent[MAX_ABILITIES];
    DWORD permanent_count;
} edictAbilities_s;

typedef struct {
    float MoveSpeed;
    float FlyHeight;
//    float FlyRate;
    float TurnSpeed;
    float PropWindow;
    float AcquireRange;
} UNITINFO;

typedef struct gameevent_s {
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

#define MAX_GROUP_SIZE 256 // entities; Warcraft III group enumeration cap used by JASS group handles
#define MAX_GROUPS 1024 // handles; bounds deterministic per-map group registry slots
#define MAX_TRIGGERS 4096 // handles; bounds deterministic per-map trigger registry slots
#define MAX_TIMERS 1024 // handles; bounds deterministic per-map timer registry slots
#define MAX_EVENTS 1024 // handlers; fixed event slots preserve stable pointers across removal
#define MAX_QUESTS 256 // quests; fixed quest slots preserve stable pointers across removal
#define MAX_QUESTITEMS 16 // items per quest; matches the practical quest objective display capacity
#define MAX_WAYPOINTS 256 // entities; fixed g_edicts ring used by point-target movement
typedef struct {
    LPEDICT units[MAX_GROUP_SIZE];
    DWORD num_units;
} ggroup_t;

typedef struct gtriggeraction_s {
    struct jass_function const *func;
    struct gtriggeraction_s *next;
} TRIGGERACTION;

typedef struct gtriggercondition_s {
    struct jass_function const *expr;
    struct gtriggercondition_s *next;
} TRIGGERCONDITION;

struct gtrigger_s {
    TRIGGERACTION *actions;
    TRIGGERCONDITION *conditions;
    BOOL disabled;
};

struct gtimer_s {
    struct jass_function const *handler;
    DWORD duration, remaining;
    BOOL periodic, paused, running;
};

struct gquestitem_s {
    LPSTR description;
    BOOL completed;
    BOOL inuse;
};

struct gquest_s {
    LPSTR title;
    LPSTR description;
    LPSTR iconPath;
    QUESTITEM items[MAX_QUESTITEMS];
    DWORD num_items;
    BOOL discovered;
    BOOL required;
    BOOL completed;
    BOOL failed;
    BOOL enabled;
    BOOL inuse;
};

/* Quest rows are present in the journal only while both server visibility gates are enabled. */
#define QuestIsVisible(quest) ((quest) && (quest)->enabled && (quest)->discovered)

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

#define MAX_GAMECACHE_ENTRIES 256
#define MAX_GAMECACHE_STRING 256

typedef enum {
    GAMECACHE_INTEGER = 1,
    GAMECACHE_REAL,
    GAMECACHE_BOOLEAN,
    GAMECACHE_UNIT,
    GAMECACHE_STRING,
} gameCacheValueType_t;

typedef struct {
    DWORD item_id;
    DWORD charges;
} gameCacheItem_t;

typedef struct {
    DWORD class_id;
    doodadHero_t hero;
    heroability_t abilities[MAX_HERO_ABILITIES];
    EDICTSTAT health;
    EDICTSTAT mana;
    DWORD unit_color;
    gameCacheItem_t inventory[MAX_INVENTORY];
} gameCacheUnit_t;

typedef struct {
    UINAME mission;
    UINAME key;
    gameCacheValueType_t type;
    union {
        LONG integer;
        FLOAT real;
        BOOL boolean;
        char string[MAX_GAMECACHE_STRING];
        gameCacheUnit_t unit;
    } value;
} gameCacheEntry_t;

typedef struct {
    PATHSTR campaign;
    DWORD num_entries;
    BOOL dirty;
    gameCacheEntry_t entries[MAX_GAMECACHE_ENTRIES];
} gameCache_t;

typedef enum {
    HERO_SKILL_ABSENT,
    HERO_SKILL_NO_POINTS,
    HERO_SKILL_LEVEL_LOCKED,
    HERO_SKILL_AVAILABLE,
    HERO_SKILL_MAXED
} heroSkillState_t;

typedef struct {
    DWORD code;
    DWORD level;
    DWORD timestamp;
    DWORD duration_ms; /* milliseconds; original timed-status duration, 0 for persistent state */
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
    BOOL rally_indicator;
    struct edictConstruction_s {
        BOOL active;
        BOOL paused;
        LPEDICT primary_builder;
        FLOAT progress;
        BOOL paid;
        DWORD payer;
        LONG gold, lumber;
    } construction;
    BOOL training; /* spawned in a production queue but not yet completed */
    BOOL training_food_wait_notified; /* one-shot Nofood feedback for the active queue head */
    struct {
        DWORD upgrade;     /* non-zero on lightweight research queue edicts */
        LONG level;        /* 1-based level being researched */
        LONG gold, lumber; /* exact charged cost, retained for cancellation */
        FLOAT duration;    /* seconds */
        FLOAT progress;    /* seconds elapsed for the active queue head */
    } research;
    struct edictRally_s {
        rallyTargetType_t type;
        VECTOR2 point;
        LPEDICT entity;
        DWORD entity_spawn_time;
    } rally;
    struct {
        LONG used; /* food currently accounted to s.player; queue-head reservations live here */
        LONG made; /* food capacity currently accounted to s.player */
    } food;
    struct {
        DWORD ability;
        BOOL primary;
        FLOAT gold_accum;
        FLOAT lumber_accum;
    } buildwork;
    /* Hero revival state lives on the persistent Hero edict. While reviving,
     * queue_next links the Hero into a producer's ordinary production chain
     * without borrowing hero->build, which may have independent gameplay use. */
    struct edictRevival_s {
        BOOL awaiting;
        BOOL reviving;
        LPEDICT producer;
        LPEDICT queue_next;
        DWORD player;
        LONG gold, lumber;
        FLOAT progress;
    } revival;
    DWORD spawn_time;
    DWORD harvested_lumber;
    DWORD harvested_gold;
    struct edictMilitia_s {
        DWORD ability;          /* Amil alias that supplied Data A/B and duration */
        DWORD normal_type;      /* Data A: worker form retained across the timed morph */
        DWORD militia_type;     /* Data B: alternate combat form */
        LPEDICT partner;        /* Hall being approached for militia/militiaoff */
        DWORD partner_spawn_time;
        BYTE previous_resource; /* returnResource_t remembered for explicit Back to Work */
        BOOL active;            /* unit has completed the Peasant -> Militia morph */
        BOOL returning;         /* current pairing order is militiaoff */
    } militia;
    DWORD heatmap2;
    VECTOR2 heatmap2_origin;  /* target position when heatmap2 was last built */
    DWORD heatmap2_time;      /* level.time when heatmap2 was last built */
    FLOAT heatmap2_radius;    /* mover collision radius used for heatmap2 */
    DWORD peonsinside;
    DWORD aiflags;
    DWORD damage;
    DWORD resources;
    DWORD freetime;
    struct edictGoldMine_s {
        LPEDICT mine;
        DWORD mine_spawn_time;
        BOOL restore_invulnerable;
    } goldmine;
    LPEDICT inventory[MAX_INVENTORY];
    struct edictItem_s {
        LPEDICT carrier;
        LONG inventory_slot;
        BOOL in_world;
        DWORD charges;
    } item;
    struct edictDestructable_s {
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
    struct edictCargo_s {
        LPEDICT units[MAX_CARGO];
        DWORD count;
    } cargo;
    LPEDICT ground_next;
    struct {
        DWORD item_slots, unit_slots;
    } stock;
    FLOAT velocity;
    doodadHero_t hero;
    heroability_t heroabilities[MAX_HERO_ABILITIES];
    heroabilitystatus_t abilstatus[MAX_UNIT_STATUSES];
    edictAbilities_s abilities;
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
    unitOrderQueue_t order_queue;
    struct edictMovement_s {
        VECTOR2 last_origin;
        FLOAT last_distance;
        DWORD blocked_frames;
        DWORD flow_generation; /* active static-route field selected this tick */
        BOOL flow_goal_reached; /* mover occupies the route's adjusted goal cell */
        BOOL flow_unreachable;  /* field exists but current cell has no route */
        BOOL flow_direct;       /* static path from mover to requested goal is clear */
        VECTOR2 path_waypoint, path_target; /* persistent accelerated turn and the destination that produced it */
        FLOAT path_radius;
        BOOL path_valid;
        FLOAT group_speed;  // slowest member's speed for a group move (0 = no cap), keeps the group together
        FLOAT heading;      // avoidance-resolved heading chosen this tick by unit_changeangle; movement follows it
        VECTOR2 worker_avoid_origin; /* start of the active resource-worker avoidance corridor */
        FLOAT worker_avoid_heading;  /* direct corridor heading captured when local blocking begins */
        DWORD worker_avoid_blocked_frames; /* consecutive blocked decisions before queue escape */
        BOOL worker_avoid_active;    /* resource-worker corridor is constraining lateral sidesteps */
        LPEDICT attackmove_waypoint;  // resume attack-move after a combat detour
        LPEDICT patrol_a, patrol_b, patrol_target;
        LPEDICT follow_target;        // persistent unit-target Move/Smart goal; resumed after combat
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
    FLOAT armor_value;    /* computed armor ('realdef', incl. hero AGI/modifiers) */
    FLOAT permanent_armor_bonus; /* research/permanent modifiers preserved across hero recompute */
    FLOAT temporary_armor_bonus; /* item/temporary modifiers preserved across hero recompute */
    struct {
        BYTE select[MAX_UNIT_SELECT_SOUNDS];
        BYTE num_select;
        BYTE yes[MAX_UNIT_SELECT_SOUNDS];   /* order confirmation ("Yes" sounds) */
        BYTE num_yes;
        BYTE ready[MAX_UNIT_SELECT_SOUNDS]; /* training completion ("Ready" sounds) */
        BYTE num_ready;
        BYTE chop[3]; BYTE num_chop;        /* weapon-vs-wood impact variants */
        BYTE pending;
        int owner_pending;                  /* owner-only one-shot queued for next snapshot */
        int world_pending;                  /* unfiltered world one-shot queued for next snapshot */
        BYTE world_pending_event;
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

    struct edictData_s {
        UnitProfile_t const *UnitProfile;
        UnitBalance_t const *UnitBalance;
        UnitData_t const *UnitData;
        UnitUI_t const *UnitUI;
        UnitWeapons_t const *UnitWeapons;
        UnitAbilities_t const *UnitAbilities;
        Doodads_t const *Doodads;
        ItemData_t const *ItemData;
        DestructableData_t const *DestructableData;
    } data;
};

typedef struct edictConstruction_s edictConstruction_s;
typedef struct edictRally_s edictRally_s;
typedef struct edictRevival_s edictRevival_s;
typedef struct edictMilitia_s edictMilitia_s;
typedef struct edictGoldMine_s edictGoldMine_s;
typedef struct edictItem_s edictItem_s;
typedef struct edictDestructable_s edictDestructable_s;
typedef struct edictCargo_s edictCargo_s;
typedef struct edictMovement_s edictMovement_s;
typedef struct edictData_s edictData_s;
typedef struct clientCamera_s clientCamera_s;

/* An entity that should be ignored by collision and physics: dead, hidden, or
 * not a live model.  Shared by g_phys.c (M_CheckCollision) and g_ai.c
 * (collision-aware movement). */
#define IS_HOLLOW(ent) ((ent->svflags & SVF_DEADMONSTER) || (ent->s.renderfx & RF_HIDDEN) || !ent->s.model || !ent->inuse)
#define MAX_UPKEEP_TIERS 10

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
        /* Combat constants are sourced from Units\MiscGame.txt (and
         * war3mapMisc.txt overrides) rather than baked into attack code. */
        FLOAT defenseArmor;
        FLOAT strAttackBonus;
        FLOAT agiDefenseBonus;
        FLOAT agiAttackSpeedBonus;
        FLOAT damageBonus[8][8];
        BOOL combatConstantsLoaded;
        LONG foodCeiling;
        DWORD upkeepUsageCount;
        DWORD upkeepGoldTaxCount;
        DWORD upkeepLumberTaxCount;
        FLOAT upkeepUsage[MAX_UPKEEP_TIERS];
        FLOAT upkeepGoldTax[MAX_UPKEEP_TIERS];
        FLOAT upkeepLumberTax[MAX_UPKEEP_TIERS];
    } constants;
};

struct gevent_s {
    LPEDICT subject;
    EVENTTYPE type;
    LPTRIGGER trigger;
    LPGTIMER timer;
    REGION region;
    FLOAT range;
    DWORD state;
    DWORD limitop;
    FLOAT limitval;
    BOOL inuse;
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
    EVENT handlers[MAX_EVENTS];
    GAMEEVENT queue[MAX_EVENT_QUEUE];
    DWORD write, read;
} LEVELEVENTS;
enum {
    WC3_FOG_STATE_MASKED = 1,  /* JASS FOG_OF_WAR_MASKED: unexplored */
    WC3_FOG_STATE_FOGGED = 2,  /* JASS FOG_OF_WAR_FOGGED: explored without current sight */
    WC3_FOG_STATE_VISIBLE = 4, /* JASS FOG_OF_WAR_VISIBLE: explored with current sight */
};
typedef struct {
    DWORD player;
    DWORD state;
    BOOL shared;
} FOGWRITE;
typedef FOGWRITE *LPFOGWRITE;
typedef FOGWRITE const *LPCFOGWRITE;
typedef struct {
    BYTE *visible;
    BYTE *explored;
    BYTE *visible_rows;
    BYTE *dirty_visible_rows;
    BYTE *dirty_explored_rows;
#ifdef WC3_FOW_PACKED_MASK
    WORD *packed_visible;
    WORD *packed_explored;
    DWORD packed_stride;
#endif
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

/* A fog modifier continuously applies one of the three JASS fog states while started. */
typedef struct fogmodifier_s {
    DWORD player;
    DWORD state;             /* WC3_FOG_STATE_* */
    BOOL is_rect;
    BOX2 rect;               /* used when is_rect */
    VECTOR2 center;          /* used when !is_rect */
    FLOAT radius;            /* used when !is_rect */
    BOOL use_shared_vision;
    BOOL started;
} FOGMODIFIER, *LPFOGMODIFIER;
typedef FOGMODIFIER const *LPCFOGMODIFIER;

typedef enum {
    BOT_NONE,
    BOT_CAMPAIGN,
    BOT_MELEE,
} botMode_t;

typedef enum {
    BOT_CAPTAIN_ATTACK,
    BOT_CAPTAIN_DEFENSE,
    BOT_CAPTAIN_COUNT,
} botCaptainType_t;

typedef enum {
    BOT_CAPTAIN_IDLE,
    BOT_CAPTAIN_FORMING,
    BOT_CAPTAIN_ACTIVE,
    BOT_CAPTAIN_RETREATING,
} botCaptainState_t;

typedef struct {
    ARRAY(LPEDICT, units);
    VECTOR2 home, goal;
    DWORD desired;
    botCaptainState_t state;
} botCaptain_t;

typedef struct {
    LONG command, data;
} botCommand_t;

typedef struct {
    DWORD class_id;
    VECTOR2 origin;
    LPEDICT unit;
} botGuardPost_t;

typedef enum {
    BOT_TARGET_HEROES    = 1 << 0,
    BOT_PEONS_REPAIR     = 1 << 1,
    BOT_HEROES_FLEE      = 1 << 2,
    BOT_WATCH_MEGA       = 1 << 3,
    BOT_IGNORE_INJURED   = 1 << 4,
    BOT_HEROES_TAKE_ITEM = 1 << 5,
    BOT_UNITS_FLEE       = 1 << 6,
    BOT_GROUPS_FLEE      = 1 << 7,
    BOT_SLOW_CHOPPING    = 1 << 8,
    BOT_CAPTAIN_CHANGES  = 1 << 9,
    BOT_SMART_ARTILLERY  = 1 << 10,
    BOT_GROUP_TIMED_LIFE = 1 << 11,
    BOT_NEW_HEROES       = 1 << 12,
    BOT_RANDOM_PATHS     = 1 << 13,
    BOT_DEFEND_PLAYER    = 1 << 14,
    BOT_HEROES_BUY_ITEMS = 1 << 15,
} botFlag_t;

typedef struct {
    LPJASS vm;
    LPPLAYER player;
    struct jass_function const *hero_levels;
    botCaptain_t captains[BOT_CAPTAIN_COUNT];
    ARRAY(botCommand_t, commands);
    ARRAY(LPEDICT, harvesters);
    ARRAY(botGuardPost_t, guards);
    botMode_t mode, pending_mode;
    DWORD flags;
    LONG replacement_count;
    BOOL paused, stop_requested, restart_requested;
    char script[MAX_PATHLEN], pending_script[MAX_PATHLEN];
} bot_t;

typedef struct {
    FLOAT elapsed;
    FLOAT pending;
    BOOL pending_valid;
    BOOL suspended;
} TIMEOFDAY;

struct level_locals {
    LPJASS vm;
    ggroup_t groups[MAX_GROUPS];
    DWORD num_groups;
    TRIGGER triggers[MAX_TRIGGERS];
    DWORD num_triggers;
    GTIMER timers[MAX_TIMERS];
    DWORD num_timers;
    bot_t bots[MAX_PLAYERS];
    LPCMAPINFO mapinfo;
    PATHSTR map_path;
    struct {
        char name[MAX_PATHLEN], description[MAX_TRIGSTR_LENGTH];
        DWORD teams, players, game_types, game_type, map_flags;
        DWORD placement, speed, difficulty, default_difficulty, resource_density, creature_density;
        DWORD forced_start_locations;
        struct {
            DWORD count;
            struct { LONG location; DWORD priority; } slots[MAX_START_PRIO];
        } start_prio[MAX_PLAYERS];
    } setup;
    LEVELEVENTS events;
    GAMEMESSAGES messages;
    LPEDICT ground_surfaces;
    struct {
        DWORD item_slots, unit_slots;
    } stock;
    struct {
        DWORD base, cursor, count;
    } waypoints;
    QUEST quests[MAX_QUESTS];
    USHORT alliances[MAX_PLAYERS][MAX_PLAYERS];
    fowGrid_t fow;
    CINEFILTER cinefilter;
    DWORD framenum;
    DWORD time;
    BOOL script_paused;
    BOOL quest_paused;
    BOOL modal_paused;
    TIMEOFDAY timeofday;
    BOX2 camera_bounds; /* map-global camera target rectangle; W3I default, SetCameraBounds may replace it */
    BOOL started;
    BOOL scriptsStarted;
    BOOL cinematic_debug_result_window; /* per-map debug latch for result-window tracing */
};

#define FOR_EACH_EVENT(property) \
for (DWORD event_index = 0; event_index < MAX_EVENTS; ++event_index) \
    for (LPEVENT property = &level.events.handlers[event_index]; property; property = NULL) \
        if (property->inuse)

#define FOR_EACH_QUEST(property) \
for (DWORD quest_index = 0; quest_index < MAX_QUESTS; ++quest_index) \
    for (LPQUEST property = &level.quests[quest_index]; property; property = NULL) \
        if (property->inuse)

#define FOR_EACH_QUESTITEM(quest, property) \
for (DWORD questitem_index = 0; questitem_index < MAX_QUESTITEMS; ++questitem_index) \
    for (__typeof__((quest)->items[0]) *property = &(quest)->items[questitem_index]; property; property = NULL) \
        if (property->inuse)

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
void G_InitJassHost(void);
LPEDICT G_GetPlayerEntityByNumber(DWORD);
LPGAMECLIENT G_GetPlayerClientByNumber(DWORD);
void G_SetClientConnected(LPEDICT player, BOOL connected);
BOOL G_GameResultDebugEnabled(void);
void G_GameResultDebug(LPCSTR format, ...);
BOOL G_IsSinglePlayer(void);
void G_RequestEndGame(BOOL do_score_screen);
void G_RequestChangeLevel(LPCSTR map, BOOL do_score_screen);
void G_RequestRestartGame(BOOL do_score_screen);
void G_RequestLoadGameMenu(void);
void G_RequestCampaignSelect(void);
void G_SetScriptPaused(BOOL paused);
void G_SetClientModal(LPEDICT player, DWORD modal, BOOL open);
void G_SetQuestDialogOpen(LPEDICT player, BOOL open);
TARGTYPE G_GetTargetType(LPCSTR);
LPCSTR G_LevelString(LPCSTR);
FLOAT G_Cinefade(void);
BOOL G_SkipCutscene(void);
VECTOR2 G_ClampCameraPosition(LPGAMECLIENT client, LPCVECTOR2 position);
VECTOR3 G_MakeServerOrigin(FLOAT x, FLOAT y, FLOAT z_offset);
void G_SetCameraBounds(FLOAT const bounds[8]);
void G_ClearCameraTarget(LPGAMECLIENT client, LPCSTR func);
void G_SetPlayerText(LPGAMECLIENT, PLAYERTEXT, LPCSTR);
void G_SetAllStockSlots(BOOL, LONG);
void G_SetStockSlots(LPEDICT, BOOL, LONG);
void G_InitStockSlots(LPEDICT);
GAMEEVENT *G_PublishEvent(LPEDICT, EVENTTYPE);
GAMEEVENT *G_PublishEventWithSource(LPEDICT, EVENTTYPE, LPEDICT);
void G_PublishSummonEvents(LPEDICT summoner, LPEDICT summoned);
BOOL G_SubscribeMessage(gameMsgFn, void *);
void G_UnsubscribeMessage(gameMsgFn, void *);
void G_PublishMessage(LPEDICT, GAMEMSGTYPE, LPEDICT);

// g_bot.c
BOOL G_BotStart(LPPLAYER, LPCSTR, botMode_t);
void G_BotStop(DWORD);
void G_BotRequestStop(DWORD);
void G_BotShutdown(void);
void G_BotPause(DWORD, BOOL);
void G_BotRunFrame(void);
BOOL G_BotUnitAlive(LPEDICT);
LPEDICT G_BotTown(LPPLAYER, LONG);
LPEDICT G_BotTownMine(LPPLAYER, LONG);
LONG G_BotTownWithMine(LPPLAYER);
DWORD G_BotMinesOwned(LPPLAYER);
DWORD G_BotGoldOwned(LPPLAYER);
BOOL G_BotProduce(LPPLAYER, LONG, DWORD, LONG);
void G_BotStopGathering(LPPLAYER);
void G_BotClearHarvest(LPPLAYER);
void G_BotHarvest(LPPLAYER, LONG, LONG, BOOL);
void G_BotCreateCaptains(LPPLAYER);
void G_BotInitAssault(LPPLAYER);
DWORD G_BotIgnoredUnits(LPPLAYER, DWORD);
BOOL G_BotCaptainInCombat(LPPLAYER, BOOL);
BOOL G_BotAddAssault(LPPLAYER, LONG, DWORD);
DWORD G_BotCaptainGroupSize(LPPLAYER);
BOOL G_BotCaptainIsFull(LPPLAYER);
LONG G_BotCaptainReadiness(LPPLAYER, BOOL);
BOOL G_BotAddDefenders(LPPLAYER, LONG, DWORD);
void G_BotAddGuardPost(LPPLAYER, DWORD, FLOAT, FLOAT);
void G_BotFillGuardPosts(LPPLAYER);
void G_BotReturnGuardPosts(LPPLAYER);
BOOL G_BotPushCommand(LPPLAYER, LONG, LONG);
DWORD G_BotCommandsWaiting(LPPLAYER);
LONG G_BotLastCommand(LPPLAYER);
LONG G_BotLastData(LPPLAYER);
void G_BotPopCommand(LPPLAYER);

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
void G_FowSetStateRect(LPCFOGWRITE fog, LPCBOX2 box);
void G_FowSetStateRadius(LPCFOGWRITE fog, LPCVECTOR2 center, FLOAT radius);
void G_FogModifierStart(LPFOGMODIFIER mod);
void G_FogModifierStop(LPFOGMODIFIER mod);
DWORD G_FowWorldToCellX(FLOAT x);
DWORD G_FowWorldToCellY(FLOAT y);
FLOAT G_GetTimeOfDay(void);
void G_SetTimeOfDay(FLOAT value);
void G_SuspendTimeOfDay(BOOL suspended);
void G_UpdateTimeOfDay(void);
BOOL G_IsNight(void);

// g_spawn.c
BOOL WriteGame(LPCSTR filename);
BOOL ReadGame(LPCSTR filename);
LPEDICT G_Spawn(void);
void SP_CallSpawn(LPEDICT);
void G_BindEntityData(LPEDICT);
void G_BindEntityRuntime(LPEDICT);
void G_SpawnEntities(void);
BOOL SP_FindEmptySpaceAround(LPEDICT, DWORD, LPVECTOR2, FLOAT *);
BOOL SP_FindUnitExitPosition(LPEDICT producer, LPEDICT unit, LPVECTOR2 out, FLOAT *angle);
LPEDICT SP_SpawnAtLocation(DWORD, DWORD, LPCVECTOR2);
LPEDICT G_CreateDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation);
LPEDICT G_CreateDeadDestructable(DWORD class_id, FLOAT x, FLOAT y, FLOAT z, FLOAT facing, FLOAT scale, DWORD variation);
BOOL G_IsDestructable(LPCEDICT ent);
void SP_monster_tree(LPEDICT);
void tree_stand(LPEDICT);
void tree_birth(LPEDICT);
void tree_pain(LPEDICT);

// g_save.c
BOOL WriteGame(LPCSTR filename);
BOOL ReadGame(LPCSTR filename);
BOOL G_SaveJassHandle(LPCSTR type, HANDLE value, DWORD *id);
HANDLE G_LoadJassHandle(LPCSTR type, DWORD id);
ggroup_t *G_AllocJassGroup(void);
LPTRIGGER G_AllocJassTrigger(void);
LPGTIMER G_AllocJassTimer(void);
void G_ClearSaveRegistries(void);
BOOL G_GetSaveMap(LPCSTR filename, LPSTR map, DWORD map_size);
void G_RunTimers(void);
void G_TimerStart(LPGTIMER timer, DWORD timeout, BOOL periodic, struct jass_function const *handler);
void G_TimerPause(LPGTIMER timer);
void G_TimerResume(LPGTIMER timer);
DWORD G_TimerRemaining(LPCGTIMER timer);

LPEDICT Waypoint_add(LPCVECTOR2);
void G_InitWaypoints(void);
void M_CheckGround (LPEDICT);
void G_RegisterGroundSurface(LPEDICT);
void G_UnregisterGroundSurface(LPEDICT);
void G_ClearGroundSurfaces(void);
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
void unit_moveindirection_ignore_units(LPEDICT);
BOOL unit_snap_to_point_ignore_units(LPEDICT, LPCVECTOR2);
void unit_changeangle(LPEDICT);
void unit_changeangle_worker(LPEDICT);
void unit_changeangle_interaction_ignore_units(LPEDICT);
BOOL unit_changeangle_towards_point_ignore_units(LPEDICT, LPCVECTOR2);
void unit_changeangle_towards_point(LPEDICT, LPCVECTOR2);
void unit_changeangle_towards_point_worker(LPEDICT, LPCVECTOR2);
void unit_changeangle_for_radius(LPEDICT, FLOAT);
void unit_changeangle_for_radius_worker(LPEDICT, FLOAT);
BOOL M_MoveIsValid(LPEDICT self, LPCVECTOR2 pos);
BOOL M_CheckAttack(LPEDICT);
BOOL unit_is_walking(LPCEDICT);
void unit_setanimation(LPEDICT, LPCSTR);
void unit_setmove(LPEDICT, umove_t *);
void M_MoveFrame(LPEDICT);
FLOAT M_DistanceToGoal(LPEDICT);
FLOAT unit_movedistance(LPEDICT);
DWORD M_RefreshHeatmap(LPEDICT, FLOAT);
BOOL M_IsDead(LPCEDICT);
void SP_SpawnUnit(LPEDICT);
DWORD unit_spawn_aiflags(DWORD);
BOOL SP_TrainUnit(LPEDICT, DWORD);
BOOL player_pay(LPPLAYER, DWORD);

// g_food.c
BOOL G_FoodLimitsEnabled(void);
LONG G_GetEffectiveFoodCap(LPGAMECLIENT client);
DWORD G_GetPlayerUpkeepTier(LPGAMECLIENT client);
LONG G_GetUpkeepGoldRateForTier(DWORD tier);
LONG G_GetUpkeepLumberRateForTier(DWORD tier);
BOOL G_PlayerHasFoodFor(LPGAMECLIENT client, LONG food_cost);
BOOL G_ReserveTrainingFood(LPEDICT unit);
void G_SetUnitFoodUsed(LPEDICT unit, LONG amount);
void G_SetUnitFoodMade(LPEDICT unit, LONG amount);
void G_ActivateUnitFood(LPEDICT unit);
void G_ClearUnitFood(LPEDICT unit);
void G_ClearTrainingQueueFood(LPEDICT producer);
BOOL G_CancelTrainingQueueItem(LPEDICT producer, DWORD index, BOOL refund);
void G_CancelTrainingQueue(LPEDICT producer, BOOL refund);
void G_SetUnitPlayer(LPEDICT unit, DWORD player);
void G_RecomputePlayerUpkeep(LPGAMECLIENT client);
LONG G_ApplyResourceIncome(LPPLAYER player, DWORD resource_state, LONG gross_amount);
LONG G_CreditResourceIncome(LPPLAYER player, LPEDICT source, DWORD resource_state, LONG gross_amount);
void G_ResourceGainEvent(LPEDICT source, DWORD resource_state, LONG amount);
BOOL G_UnitCanReviveHeroes(LPCEDICT altar);
BOOL G_HeroCanBeRevivedAt(LPCEDICT altar, LPCEDICT hero);

// skills/s_rally.c
BOOL G_UnitHasRally(LPCEDICT producer);
void G_ResetRallyTarget(LPEDICT producer);
BOOL G_SetRallyPoint(LPEDICT producer, LPCVECTOR2 point);
BOOL G_SetRallyEntity(LPEDICT producer, LPEDICT target);
rallyTargetType_t G_ResolveRallyTarget(LPEDICT producer, LPVECTOR2 point, LPEDICT *target);
BOOL G_ApplyRallyOrder(LPEDICT producer, LPEDICT produced);
void G_InvalidateRallyTarget(LPEDICT target);
void G_UpdateRallyIndicator(LPGAMECLIENT client);

DWORD G_HeroReviveGoldCost(LPCEDICT hero);
DWORD G_HeroReviveLumberCost(LPCEDICT hero);
FLOAT G_HeroReviveTime(LPCEDICT hero);
BOOL G_QueueHeroRevive(LPEDICT altar, LPEDICT hero);
BOOL G_CancelHeroRevive(LPEDICT altar, LPEDICT hero);
void G_CancelHeroRevives(LPEDICT altar);
BYTE compress_stat(EDICTSTAT const *);
DWORD G_LoadShadowTexture(LPCSTR, BOOL);

// g_pathing.c
pathTex_t *LoadTGA(BYTE const*, size_t);
pathTex_t *M_LoadPathTex(LPCSTR filename);

// g_move.c
BOOL SV_CloseEnough(LPEDICT, LPCEDICT, FLOAT);

// g_phys.c
void G_RunEntity(LPEDICT);
void G_UpdateOnFire(LPEDICT);
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
#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void);
#endif
BOOL G_UnitAutocastIsOn(LPEDICT ent, ability_t const *ability);
BOOL G_SetUnitAutocast(LPEDICT ent, ability_t const *ability, BOOL enabled);
BOOL G_TryUnitAutocast(LPEDICT ent);

// g_metadata.c
LPCSTR FindConfigValue(LPCSTR, LPCSTR);
LPCSTR GetClassName(DWORD);

// g_effects.c
LPCSTR G_AbilityEffectArt(DWORD ability_id, wc3EffectType_t type, DWORD index);
LPEDICT G_SpawnModelEffect(LPCSTR model, LPCVECTOR2 point, LPEDICT target, LPCSTR attach_point, BOOL temporary);
LPEDICT G_SpawnAbilityEffectAtPoint(DWORD ability_id, wc3EffectType_t type, DWORD index, LPCVECTOR2 point, BOOL temporary);
LPEDICT G_SpawnAbilityEffectTarget(DWORD ability_id, wc3EffectType_t type, DWORD index, LPEDICT target, LPCSTR attach_point, BOOL temporary);
void G_DestroyEffect(LPEDICT effect);
void G_EffectThink(LPEDICT);
void G_EffectValidateTarget(LPEDICT);

// g_unit_ui.c (Phase 8)
BYTE G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons);
BOOL G_BuildCommandButton(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, gameCommandButton_t *button);
BOOL G_BuildAllEnabled(void);
BOOL G_WorkerCanBuild(LPEDICT worker, DWORD building_id);
BOOL G_ProducerCanTrain(LPEDICT producer, DWORD unit_id);
BOOL G_ProducerCanResearch(LPEDICT producer, DWORD upgrade_id);
buildCommandState_t G_GetBuildCommandState(LPGAMECLIENT client, LPEDICT worker, DWORD building_id, LPSTR reason, DWORD reason_size);
buildCommandState_t G_GetTrainCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD unit_id, LPSTR reason, DWORD reason_size);
buildCommandState_t G_GetResearchCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD upgrade_id, LONG *next_level, LPSTR reason, DWORD reason_size);
LONG G_UpgradeGoldCost(DWORD upgrade_id, LONG level_value);
LONG G_UpgradeLumberCost(DWORD upgrade_id, LONG level_value);
FLOAT G_UpgradeResearchTime(DWORD upgrade_id, LONG level_value);
BOOL G_QueueResearch(LPEDICT producer, DWORD upgrade_id);
void G_ApplyPlayerUpgradesToUnit(LPEDICT unit);
DWORD G_GetUnitUpgradeForClass(LPCEDICT unit, LPCSTR wanted_class);
BOOL G_ChargeBuilding(LPGAMECLIENT client, DWORD building_id);
void G_RefundBuilding(LPGAMECLIENT client, DWORD building_id);
void G_SnapBuildingPoint(DWORD building_id, LPVECTOR2 point);
buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, DWORD building_id, LPCVECTOR2 requested, LPVECTOR2 snapped);
BOOL G_IssueBuildOrder(LPEDICT builder, DWORD building_id, LPCVECTOR2 location);
FLOAT G_BuildApproachDistance(DWORD building_id);
BOOL G_StartHumanConstruction(LPEDICT builder, LPEDICT building);
void G_UpdateConstructionAnimation(LPEDICT building);
void G_StopConstruction(LPEDICT building);
BOOL G_CancelStructureConstruction(LPEDICT building);
void G_CompleteConstruction(LPEDICT building);
BOOL G_UnitHasHumanRepair(LPEDICT ent);
BOOL S_OrderRepair(LPEDICT ent, LPEDICT target, DWORD preferred);
BOOL S_SetRepairAutocast(LPEDICT ent, BOOL enabled);
BOOL S_RepairSmart(LPEDICT ent, LPEDICT target);
void S_CancelRepair(LPEDICT ent);
void G_SetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid, LONG maximum);
LONG G_GetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid);
void G_SetPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG level_value);
void G_AddPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG levels);
LONG G_GetPlayerTechResearchedLevel(LPGAMECLIENT client, DWORD techid);
LONG G_GetPlayerTechInProgress(LPGAMECLIENT client, DWORD techid);
void G_AddPlayerTechInProgress(LPGAMECLIENT client, DWORD techid, LONG levels);
LONG G_GetPlayerTechCountValue(LPGAMECLIENT client, DWORD techid);
void G_InvalidateCommands(LPGAMECLIENT client);
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
void G_InvalidateUnitInfoPanel(LPEDICT);
void G_InvalidateUnitPortrait(LPEDICT);
void G_RefreshInfoPanel(LPEDICT);
void G_UpdateClientInfoPanels(void);
void UI_WriteSelectedPortraitLayer(LPEDICT);
void G_RefreshResourceBar(LPEDICT);
void G_AccumulatePlayerFood(LPGAMECLIENT client);
void G_InitClientUIState(LPGAMECLIENT client);
void G_UpdateClientResourceBars(void);
BOOL G_UnitIsIdleWorker(LPCEDICT ent);
BOOL G_UnitShowsIdleWorkerShortcut(LPGAMECLIENT client, LPCEDICT ent);
BOOL G_UnitShowsHeroShortcut(LPGAMECLIENT client, LPCEDICT ent);
LPEDICT G_GetNextIdleWorker(LPGAMECLIENT client, DWORD after);
void G_InvalidateUnitShortcuts(LPGAMECLIENT client);
void G_InvalidateAllUnitShortcuts(void);
void G_InvalidateUnitShortcutsForUnit(LPEDICT ent);
void G_ActivateHeroButton(LPEDICT clent, DWORD number);
void G_ActivateHeroKey(LPEDICT clent, DWORD slot);
void G_ActivateIdleWorkerShortcut(LPEDICT clent, DWORD hinted_number);
void G_UpdateClientUnitShortcuts(void);
void UI_WriteUnitShortcutLayer(LPEDICT ent);
void UI_AddCancelButton(LPEDICT);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_AddCommandButton(LPCSTR);
void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level);
void UI_WriteTooltipFrame(void);
void UI_SetCurrentClient(LPGAMECLIENT client);
void UI_ShowInterface(LPEDICT, BOOL, FLOAT);
void UI_ShowText(LPEDICT, LPCVECTOR2, LPCSTR, FLOAT);
void UI_ShowTransientText(LPEDICT, LPCVECTOR2, LPCSTR, FLOAT);
void UI_ClearTextMessages(LPEDICT);
void UI_InvalidateDialoguePresentation(LPEDICT);
void UI_WriteDialoguePresentation(LPEDICT);
LPCSTR GetBuildCommand(unitRace_t);
void UI_RenderRoute(LPEDICT, LPCSTR);
void UI_ShowMainMenu(LPEDICT);
void UI_ShowGameMenuEndGame(LPEDICT);
void UI_ShowGameMenuConfirmExit(LPEDICT);
void UI_ShowRealmSelect(LPEDICT, BOOL);
void UI_ShowSinglePlayerMenu(LPEDICT);
void UI_ShowMultiplayerMenu(LPEDICT);
void UI_ShowMultiplayerCreateMenu(LPEDICT);
void UI_ShowMultiplayerGameSetupMenu(LPEDICT, DWORD);
void UI_ShowGameInterface(LPEDICT);
void UI_WriteHoverLayout(LPEDICT);
void UI_WriteCinematicLayer(LPEDICT);
void UI_ShowMapSelectMenu(LPEDICT, LPCSTR);
void UI_ShowMultiplayerCreateMapInfo(LPEDICT);
void UI_ClearCreateGameSlots(void);
void UI_AddCreateGameSlot(DWORD, LPCSTR, LPCSTR, LPCSTR, DWORD);

// p_fdf.c
void UI_PrintClasses(void);
void UI_ClearTemplates(void);
void UI_ResetHud(void);
void UI_LoadHud(void);
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
#ifdef BZ_TESTS
void UI_TestResetInfoPanelIconCache(void);
LPCSTR UI_TestResolveTypedInfoPanelIcon(LPCSTR prefix, LPCSTR type, BOOL has_upgrade);
USHORT UI_TestSelectedTimedStatusStat(LPGAMECLIENT client, LPEDICT selected);
#endif
void UI_WriteLayout(LPEDICT, LPCFRAMEDEF, DWORD);
void UI_WriteStart(DWORD);
void UI_ClearLayer(LPEDICT, DWORD);
void UI_ShowGameResult(LPEDICT, DWORD);
void UI_FlushPendingGameResults(void);
void UI_HideGameResult(LPEDICT);
void UI_ShowQuests(LPEDICT);
void UI_HideQuests(LPEDICT);
void UI_ShowAllies(LPEDICT);
void UI_AlliesToggle(LPEDICT, DWORD, PLAYERALLIANCE);
void UI_AlliesToggleVictory(LPEDICT);
void UI_AlliesAccept(LPEDICT);
void UI_AlliesCancel(LPEDICT);
void UI_ShowLog(LPEDICT);
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
LPCSTR Theme_PlayerString(LPGAMECLIENT, LPCSTR, LPCSTR);
FLOAT Theme_Float(LPCSTR, LPCSTR);

// ui_write.c
void UI_WriteFrame(LPCFRAMEDEF);
void UI_WriteFrameValue(LPCFRAMEDEF, FLOAT);
DWORD UI_GetWrittenFrameNumber(LPCFRAMEDEF);
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
void G_PlayUISoundForPlayer(LPEDICT, LPCSTR);

typedef struct {
    FLOAT volume;
    VECTOR3 origin;
    LPEDICT emitter;
    BOOL positioned;
} jassSoundPlayback_t;

void G_JassSoundRuntimeReset(void);
void G_JassSoundRuntimeInit(HANDLE sound);
void G_JassSoundSetVolume(HANDLE sound, FLOAT volume);
void G_JassSoundSetPosition(HANDLE sound, LPCVECTOR3 position);
void G_JassSoundAttach(HANDLE sound, LPEDICT unit);
void G_JassSoundPlayback(HANDLE sound, jassSoundPlayback_t *playback);
void G_SendPointConfirmation(LPEDICT, LPCVECTOR2, BOOL attack);
void G_QueueReadySound(LPEDICT);
void G_QueueOwnerSoundAlias(LPEDICT, LPCSTR);
void G_QueueOwnerUISound(LPEDICT, LPCSTR);
void G_SendMinimapPing(LPGAMECLIENT, LPCVECTOR2, FLOAT, COLOR32, DWORD);
void G_SendOwnerMinimapAlert(LPEDICT);
void G_ShowCommandErrorText(LPEDICT, LPCSTR);
extern int g_treeFallSounds[3];     /* Sound\Destructibles\TreeFall{1,2,3}.wav configstring indices */
extern BYTE g_numTreeFallSounds;

// g_command.c
void G_SelectEntity(LPGAMECLIENT, LPEDICT);
void G_DeselectEntity(LPGAMECLIENT, LPEDICT);
BOOL G_IsEntitySelected(LPGAMECLIENT, LPEDICT);
BOOL G_FocusSelectedUnit(LPGAMECLIENT, LPEDICT);
void G_ResetSelectionFocus(LPGAMECLIENT);
BOOL G_UnitCanBeSelected(LPGAMECLIENT, LPCEDICT);
BOOL G_UnitCanControl(LPGAMECLIENT, LPCEDICT);
selectionRelation_t G_SelectionRelation(DWORD viewer, LPCEDICT ent);
LPEDICT G_GetMainControllableUnit(LPGAMECLIENT);
void G_UpdateClientSelections(void);
void G_QueueSelectionSound(LPEDICT);
void G_ClientCommand(LPEDICT, DWORD, LPCSTR[]);
void G_ClientSetCameraPosition(LPEDICT, LPCVECTOR2);

//  s_skills.c
FLOAT AB_Data(LPCSTR, DWORD, DWORD);
DWORD GetAbilityIndex(ability_t const *);

// g_combat.c
int G_AttackDamage(LPEDICT, LPEDICT, int);
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
BOOL G_IssueUnitPointOrder(LPEDICT, LPCSTR, LPCVECTOR2, BOOL, DWORD, FLOAT);
BOOL G_IssueUnitTargetOrder(LPEDICT, LPCSTR, LPEDICT, BOOL, DWORD);
BOOL G_UnitStartNextQueuedOrder(LPEDICT);
void G_ClearUnitOrderQueue(LPEDICT);
DWORD G_UnitQueuedOrderCount(LPCEDICT);
void unit_birth(LPEDICT);
void unit_die(LPEDICT, LPEDICT);
LPEDICT unit_createorfind(DWORD, DWORD, LPCVECTOR2, FLOAT);
BOOL unit_additemtoslot(LPEDICT, LPEDICT, DWORD);
BOOL unit_additem(LPEDICT, LPEDICT);
void unit_addstatus(LPEDICT, LPCSTR, DWORD);
void unit_addtimedstatus(LPEDICT, LPCSTR, DWORD, FLOAT);
BOOL unit_statusshowstimedbar(DWORD);
FLOAT unit_statusremainingfraction(heroabilitystatus_t const *);
heroabilitystatus_t const *unit_findtimedbarstatus(LPCEDICT);
void unit_learnability(LPEDICT, DWORD);
DWORD G_UnitAbilityLevel(LPCEDICT ent, DWORD abilcode);
BOOL G_HeroHasCandidateSkill(LPCEDICT ent, DWORD abilcode);
void G_HeroInitializeProgression(LPEDICT ent);
DWORD G_HeroSkillRequiredLevel(LPEDICT ent, DWORD abilcode);
heroSkillState_t G_HeroSkillState(LPEDICT ent, DWORD abilcode, DWORD *next_level, DWORD *required_level);
BOOL G_HeroLearnSkill(LPEDICT ent, DWORD abilcode);

void G_GameCacheInit(gameCache_t *cache, LPCSTR campaign);
BOOL G_GameCacheSave(gameCache_t *cache);
void G_GameCacheFlush(gameCache_t *cache);
void G_GameCacheFlushMission(gameCache_t *cache, LPCSTR mission);
void G_GameCacheFlushEntry(gameCache_t *cache, LPCSTR mission, LPCSTR key, gameCacheValueType_t type);
BOOL G_GameCacheStoreInteger(gameCache_t *cache, LPCSTR mission, LPCSTR key, LONG value);
BOOL G_GameCacheStoreReal(gameCache_t *cache, LPCSTR mission, LPCSTR key, FLOAT value);
BOOL G_GameCacheStoreBoolean(gameCache_t *cache, LPCSTR mission, LPCSTR key, BOOL value);
BOOL G_GameCacheStoreString(gameCache_t *cache, LPCSTR mission, LPCSTR key, LPCSTR value);
BOOL G_GameCacheStoreUnit(gameCache_t *cache, LPCSTR mission, LPCSTR key, LPCEDICT unit);
BOOL G_GameCacheHave(gameCache_t const *cache, LPCSTR mission, LPCSTR key, gameCacheValueType_t type);
LONG G_GameCacheGetInteger(gameCache_t const *cache, LPCSTR mission, LPCSTR key);
FLOAT G_GameCacheGetReal(gameCache_t const *cache, LPCSTR mission, LPCSTR key);
BOOL G_GameCacheGetBoolean(gameCache_t const *cache, LPCSTR mission, LPCSTR key);
LPCSTR G_GameCacheGetString(gameCache_t const *cache, LPCSTR mission, LPCSTR key);
LPEDICT G_GameCacheRestoreUnit(gameCache_t const *cache, LPCSTR mission, LPCSTR key,
                              DWORD player, LPCVECTOR2 location, FLOAT facing);

void G_RecomputeHeroStats(LPEDICT);
DWORD G_MaxHeroLevel(void);
DWORD G_HeroXPForLevel(DWORD level);
DWORD G_HeroLevelForXP(DWORD xp);
void G_HeroApplyLevel(LPEDICT, DWORD level);
void G_HeroSetXP(LPEDICT, DWORD xp);
void G_GrantKillXP(LPEDICT victim, LPEDICT killer);
void G_ReviveHero(LPEDICT, FLOAT x, FLOAT y);
BOOL G_UnitIsHero(LPCEDICT ent);
FLOAT G_UnitArmorValue(LPCEDICT ent);
BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code);
LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level);

void order_attack(LPEDICT, LPEDICT);
void order_move(LPEDICT, LPEDICT);
void order_stop(LPEDICT);
void order_attackmove(LPEDICT, LPEDICT);
void order_patrol(LPEDICT, LPEDICT);
void order_patrol_resume(LPEDICT);
void order_follow(LPEDICT, LPEDICT);
void order_follow_resume(LPEDICT);
extern umove_t holdpos_move_stand;
extern umove_t holdpos_move_stand_ready;
void unit_stand(LPEDICT);
BOOL G_ActorHasSkill(LPEDICT, LPCSTR);
BOOL G_ActorAddSkill(LPEDICT, DWORD);
BOOL G_ActorRemoveSkill(LPEDICT, DWORD);
BOOL G_ActorSetSkillPermanent(LPEDICT, DWORD, BOOL);
BOOL G_ActorSkillPermanent(LPEDICT, DWORD);
void G_FreeActorSkills(LPEDICT);
BOOL S_GoldMineIsMine(LPCEDICT);
DWORD S_GoldMineMaximumGold(LPCEDICT);
FLOAT S_GoldMineMiningDuration(LPCEDICT);
DWORD S_GoldMineCapacity(LPCEDICT);
BOOL S_GoldMineCanHarvest(LPCEDICT);
BOOL S_GoldMineWorkerIsInside(LPCEDICT);
BOOL S_MilitiaTargetOrder(LPEDICT, LPCSTR, LPEDICT);
void S_CancelMilitiaPairing(LPEDICT);
void S_MilitiaExpire(LPEDICT);
void S_GoldMineInitUnit(LPEDICT);
void S_GoldMineReleaseWorker(LPEDICT);
void harvest_start(LPEDICT, LPEDICT);
void harvest_gold_start(LPEDICT, LPEDICT);
BOOL harvest_gold_order(LPEDICT, LPEDICT);
BOOL harvest_auto_start_gold(LPEDICT);
BOOL harvest_auto_start_lumber(LPEDICT);
BOOL harvest_lumber_return_to(LPEDICT, LPEDICT);
BOOL harvest_gold_return_to(LPEDICT, LPEDICT);
void cargo_drop_all(LPEDICT);
void blight_mine_think(LPEDICT);
void blizzard_think(LPEDICT);
void flame_strike_tick(LPEDICT);
void siphon_mana_think(LPEDICT);
BOOL move_selectlocation(LPEDICT, LPCVECTOR2);
BOOL move_should_arrive(LPEDICT, FLOAT);
BOOL move_is_blocked(LPEDICT, FLOAT, FLOAT);
BOOL move_is_settled_near_goal(LPEDICT, FLOAT, FLOAT);
BOOL move_is_terminal_hold(LPCEDICT);
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
void G_DrainPausedResultEvents(void);

// g_items.c
void SP_SpawnItem(LPEDICT);
BOOL G_IsItem(LPCEDICT item);
DWORD G_InventoryCapacity(LPCEDICT unit);
BOOL G_UnitHasInventory(LPEDICT unit);
DWORD G_ItemCharges(LPCEDICT item);
void G_SetItemCharges(LPEDICT item, DWORD charges);
void G_ConsumeItemCharge(LPEDICT item);
LPCSTR G_ItemAbilityList(LPCEDICT item);
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

/* Simulation clock reader. Spell-rank parameters named `level` shadow the global in
 * several skill functions, so clock reads go through this instead of `level.time`. */
static inline DWORD G_Time(void) { return level.time; }

extern unitMeta_t const UnitsMetaData[];

#endif
