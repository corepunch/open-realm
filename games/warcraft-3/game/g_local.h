#ifndef g_local_h
#define g_local_h

#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include "common/common.h"
#include "common/weather.h"
#include "common/stb_fdf.h"
#include "common/stb_slk.h"
#include "server/game.h"
#include "server/routing.h"
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
#define BZ_STRINGIFY_INNER(value) #value
#define BZ_STRINGIFY(value) BZ_STRINGIFY_INNER(value)
#define MAX_ENTITIES MAX_GAME_ENTITIES
#define MAX_REGION_SIZE 16
#define MAX_INVENTORY 6
#define ITEM_PICKUP_RANGE 150.0f /* world units; classic contextual-pickup reach */

typedef enum {
    WC3_MAP_GAME_DATA_SET_DEFAULT = 0,
    WC3_MAP_GAME_DATA_SET_CUSTOM = 1,
    WC3_MAP_GAME_DATA_SET_MELEE = 2,
} wc3MapGameDataSet_t;

typedef struct {
    LPCMAPINFO info;
    DWORD version;
    LPSTR out;
    DWORD size;
} wc3MapGameDataPrefixParams_t;

#define ITEM_DROP_RANGE 150.0f   /* world units; point-drop reach before the carrier must move */
#define MAX_SHOP_STOCK 24 // entries; exceeds Blizzard.j's default 11 item slots; bounds persisted shop merchandise
#define MAX_CARGO 8
#define MAX_HERO_ABILITIES 4
#define MAX_ABILITIES 16 // slots; extra ability codes granted or stripped at runtime
#define MAX_UNIT_COOLDOWNS 16 // independent per-unit ability cooldown records; does not consume buff/status capacity
#define MAX_UNIT_STATUSES 8
#define PLAYER_TEXT_BACKUP 16
#define PLAYER_TEXT_MASK (PLAYER_TEXT_BACKUP - 1)
#define MAX_START_PRIO 16 // slots; one possible priority entry per WC3 player start location
#define MAX_PLAYER_TECH_STATE 256 // slots; NightElfX02 scripts 137 distinct techs for one player, exceeding the former 128; game-local only, not a network contract

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

struct jass_function;
KNOWN_AS(jass_s, JASS);
KNOWN_AS(gcamerasetup_s, CAMERASETUP);
KNOWN_AS(gregion_s, REGION);
KNOWN_AS(gevent_s, EVENT);
KNOWN_AS(gtrigger_s, TRIGGER);
KNOWN_AS(gtimer_s, GTIMER);
KNOWN_AS(gtimerdialog_s, TIMERDIALOG);
KNOWN_AS(gleaderboard_s, LEADERBOARD);
KNOWN_AS(gquest_s, QUEST);
KNOWN_AS(gquestitem_s, QUESTITEM);

typedef enum {
    BUILD_COMMAND_ABSENT,
    BUILD_COMMAND_HIDDEN,
    BUILD_COMMAND_DISABLED,      /* visible but inert: unmet prerequisite */
    BUILD_COMMAND_UNAFFORDABLE,  /* visible/clickable: report resource shortage */
    BUILD_COMMAND_AVAILABLE,
} buildCommandState_t;

typedef struct {
    LPCEDICT building;
    DWORD unit_id;
    LONG *gold, *lumber, *food;
} buildingUpgradeCostParams_t;

typedef struct {
    LPGAMECLIENT client;
    LPEDICT producer;
    DWORD unit_id;
    LPSTR reason;
    DWORD reason_size;
} buildingUpgradeCommandParams_t;

typedef enum {
    PLACE_OK,
    PLACE_INVALID_BUILDING,
    PLACE_TERRAIN_BLOCKED,
    PLACE_UNIT_BLOCKED,
    PLACE_REQUIRED_PATHING_MISSING,
    PLACE_TOO_CLOSE_TO_GOLD_MINE,
    PLACE_OUT_OF_BOUNDS,
    PLACE_REQUIRED_PARENT_MISSING,
} buildPlacementResult_t;

typedef enum {
    CONSTRUCTION_NONE,
    CONSTRUCTION_HUMAN,
    CONSTRUCTION_ORC,
    CONSTRUCTION_UNDEAD,
    CONSTRUCTION_NIGHTELF,
} constructionType_t;

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
    LPEDICT dragged_item;      /* transient inventory item carried by the cursor for a drop order */
} menu_t;
typedef menu_t clientMenu_s;

enum {
    AI_HOLD_FRAME = 1 << 0,
    AI_FLYING     = 1 << 1,  /* air-layer unit (movetp "fly"): ignores ground collision */
    AI_IMMOBILE   = 1 << 2,  /* fixed unit: may act, but never translates or changes facing */
    AI_AUTOCAST_REPAIR = 1 << 3, /* persisted Repair-family autocast toggle */
    AI_AUTOCAST_ACTIVE = 1 << 4, /* fast unit-wide marker: some autocast ability is enabled */
    AI_ILLUSION    = 1 << 5,  /* summoned copy created by illusion abilities */
    AI_SLEEPING    = 1 << 6,  /* neutral creep is dormant; wakes on enemy proximity */
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

    /* Later Warcraft III spell lifecycle event ids retain their retail numeric
     * values so ConvertPlayerUnitEvent/ConvertUnitEvent handles compare exactly
     * with the constants authored by common.j.  Only SPELL_EFFECT is currently
     * published; the surrounding values are reserved for future lifecycle work. */
    EVENT_PLAYER_UNIT_SPELL_CHANNEL = 272,
    EVENT_PLAYER_UNIT_SPELL_CAST = 273,
    EVENT_PLAYER_UNIT_SPELL_EFFECT = 274,
    EVENT_PLAYER_UNIT_SPELL_FINISH = 275,
    EVENT_PLAYER_UNIT_SPELL_ENDCAST = 276,
    EVENT_UNIT_SPELL_CHANNEL = 289,
    EVENT_UNIT_SPELL_CAST = 290,
    EVENT_UNIT_SPELL_EFFECT = 291,
    EVENT_UNIT_SPELL_FINISH = 292,
    EVENT_UNIT_SPELL_ENDCAST = 293,

    /* Ownership-change ids retain their retail common.j numbers (270/287) so
     * TriggerRegisterPlayerUnitEvent/TriggerRegisterUnitEvent handles match.
     * The published value carries the previous owner + 1 (zero stays reserved
     * for "no change context", which keeps death/research/spell callbacks that
     * share a trigger observing null from GetChangingUnit). */
    EVENT_PLAYER_UNIT_CHANGE_OWNER = 270,
    EVENT_UNIT_CHANGE_OWNER = 287,

    EVENT_UNIT_IN_RANGE = 92,
} EVENTTYPE;

/* struct uiFrameDef_s is defined in common/stb_fdf.h (shared with UI module) */

struct gregion_s {
    BOX2 rects[MAX_REGION_SIZE];
    DWORD num_rects;
};

typedef enum {
    RAVEN_RISE_NONE,
    RAVEN_RISE_AFTER_MORPH,
    RAVEN_RISE_ACTIVE,
} ravenRiseState_t;

typedef enum {
    ENSNARE_HEIGHT_NONE,
    ENSNARE_HEIGHT_LAND,
    ENSNARE_HEIGHT_RISE,
} ensnareHeightState_t;

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
#define WC3_MUSIC_NAME_MAX 2048 // bytes; resolved later per recipient through war3skins/Music.SLK

typedef enum {
    WC3_MUSIC_SOURCE_NONE = 0,
    WC3_MUSIC_SOURCE_MAP,
    WC3_MUSIC_SOURCE_EXPLICIT,
    WC3_MUSIC_SOURCE_THEMATIC,
} wc3MusicSource_t;

typedef struct {
    char map_name[WC3_MUSIC_NAME_MAX];
    BOOL map_random;
    LONG map_index;

    char current_name[WC3_MUSIC_NAME_MAX];
    wc3MusicSource_t current_source;
    BOOL current_random;
    LONG current_index;
    LONG current_position_ms;
    LONG current_fade_ms;
    BOOL paused;

    LONG volume;
    LONG thematic_volume;
} wc3MusicState_t;

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
    BOOL cheat_instant_build; /* developer cheat: owner construction/training/research completes on next work tick */
    BOOL cheat_instant_kill; /* developer cheat: owner damage lethally hits units/buildings/destructables */
    DWORD modal_flags;
    BOOL quest_dialog_open;
    DWORD quest_until; /* FlashQuestDialogButton deadline in simulation milliseconds. */
    menu_t menu;
    struct clientCamera_s {
        CAMERASETUP state;
        CAMERASETUP old_state;
        FLOAT target_height;
        DWORD start_time;
        DWORD end_time;
        VECTOR2 quick_position; /* SetCameraQuickPosition spacebar target; does not move the camera */
        BOOL quick_position_set;
        LPEDICT target_controller;
        VECTOR2 target_offset;
        BOOL target_inherit_orientation;
    } camera;
    /* Info-panel cache. For single units entity/xp track static presentation;
     * HP/mana are retained for save-layout compatibility because live portrait
     * values now use player-state bindings. With entity==0, hp caches the
     * non-single selection count (-1 denotes the building queue panel). */
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
        DWORD quest_until;
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
    wc3MusicState_t music; /* client-local Warcraft music semantics; synced on ClientBegin */
    DWORD cinematic_end_time;       /* game time (ms) when current SetCinematicScene expires, 0 = none */
    DWORD cinematic_voice_end_time; /* game time (ms) when Portrait Talk becomes Portrait, 0 = not talking */
};

/* Player-issued WC3 Shift orders are simulation state, separate from the
 * training/research queue. Targets are retained by edict number + spawn_time
 * so a recycled slot cannot silently retarget an old queued command. */
#define MAX_UNIT_ORDER_QUEUE 16
#define UNIT_ORDER_NAME_SIZE 20 // bytes; fits the 17-byte longest stock order name plus NUL; bounds queued order strings

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

/* Independent policies consumed by ability command and cast dispatch. */
#define AB_PASSIVE      (1u << 0)  // bit 0; passive command policy; used in ability flags
#define AB_TOGGLE       (1u << 1)  // bit 1; reversible on/off action; used in ability flags
#define AB_CHANNEL      (1u << 2)  // bit 2; channel lifecycle policy; used in ability flags
#define AB_AUTOCAST     (1u << 3)  // bit 3; independent automatic activation policy; used in ability flags
#define AB_SPELL        (1u << 4)  // bit 4; shared casting path; selects generic command and effect dispatch
#define AB_NO_SMART     (1u << 5)  // bit 5; excludes Smart target acquisition; used in ability flags
#define AB_COMMAND      (1u << 6)  // bit 6; bespoke command procedure; exposes a command-card action
#define AB_UPDATE       (1u << 7)  // bit 7; persistent behavior procedure; receives per-unit update messages
#define AB_ITEM         (1u << 8)  // bit 8; inventory behavior procedure; receives item-use messages
#define AB_INNATE       (1u << 9)  // bit 9; unit-data behavior; receives lifecycle messages without a command-card slot
#define AB_SEPARATE_OFF (1u << 16) // bit 16; preserves the existing explicit off-button policy; used in ability flags

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

typedef enum {
    WC3_EFFECT_EFFECT = 0,
    WC3_EFFECT_TARGET = 1,
    WC3_EFFECT_CASTER = 2,
    WC3_EFFECT_SPECIAL = 3,
    WC3_EFFECT_AREA_EFFECT = 4,
    WC3_EFFECT_MISSILE = 5,
    WC3_EFFECT_LIGHTNING = 6,
} wc3EffectType_t;

typedef struct ability_s ability_t;
typedef struct ability_call_s abilityCall_t;

/* A resolved use of a shared procedure. Rawcode belongs to the authored ability, not its behavior. */
typedef struct {
    DWORD code;
    ability_t const *ability;
} abilityitem_t;

typedef enum {
    A_INIT,             /* InitAbilities: initialize shared data from call->classname. */
    A_COMMAND,          /* Command card: begin the ability through call->client; return handled. */
    A_TOGGLE_ON,        /* Command card: return whether ent currently uses its alternate/off button. */
    A_VALIDATE,         /* Spell pipeline: validate call->target before spending resources; return allowed. */
    A_EXECUTE,          /* Spell pipeline: apply the effect to call->target; return whether it executed. */
    A_ITEM_USE,         /* Inventory click: apply an immediate item effect; return success for charge use. */
    A_ITEM_ADD,         /* Inventory pickup: apply this item's authored passive modifier. */
    A_ITEM_REMOVE,      /* Inventory removal: undo this item's authored passive modifier. */
    A_AUTOCAST_ON,      /* Autocast/UI query: return whether autocast is enabled on ent. */
    A_AUTOCAST_SET,     /* Autocast command: set ent's state from call->enabled. */
    A_AUTOCAST_ACQUIRE, /* Unit scheduler: acquire a target and issue an autocast; return whether issued. */
    A_ENABLE,           /* UnitAddAbility: notify the procedure that this ability was added to ent. */
    A_DISABLE,          /* UnitRemoveAbility: notify the procedure that this ability was removed from ent. */
    A_LEVEL,            /* Level refresh: return ent's current behavior-specific ability level. */
    A_LEVEL_CHANGED,    /* Level refresh: apply the new call->level to behavior-owned state. */
    A_ORDER,            /* Immediate-order dispatch: handle call->order; return whether it was accepted. */
    A_UPDATE,           /* Unit frame: update persistent behavior owned by this procedure. */
    A_UNIT_INIT,        /* Spawn/type rebind: initialize behavior from the unit's authored data. */
    A_IDLE,             /* Stand AI: return true after starting an innate idle behavior. */
    A_MOVE_LEAVE,       /* Before replacing a distinct move: release the old behavior's state. */
    A_DAMAGED,          /* Positive post-mitigation damage, before combat response. */
    A_UNIT_REMOVE,      /* Before freeing the edict: release behavior-owned resources. */
    A_NO_ACQUIRE,       /* Target query: return true to suppress automatic enemy acquisition. */
    A_CANCEL,           /* Explicit cancellation: return to the unit's ordinary idle behavior. */
    A_DEATH,            /* unit_die: ability-owned death behavior on the dying unit. */
} abilityMsg_t;

#define BZ_ABILITY_PROC(NAME) intptr_t NAME(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call)

typedef intptr_t (*abilityProc_t)(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);

struct ability_call_s {
    abilityitem_t const *item;
    union {
        spellTarget_t const *target;
        LPEDICT client;
        LPCSTR order;
        LPCSTR classname;
        DWORD level;
        BOOL enabled;
    };
};

struct ability_s {
    LPCSTR classname;
    abilityProc_t proc;
    DWORD flags;
    spellTargetType_t target_type;
    LPCSTR const *orders;
};

typedef struct {
    LPCSTR animation;
    void (*think)(LPEDICT);
    void (*endfunc)(LPEDICT);
    abilityProc_t proc;
} umove_t;

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
    DWORD targetsAllowed; /* WC3 targetflag bitmask (ua1g/ua2g) */
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
    LONG value; /* scalar JASS callback payload (for example spell/research rawcode) */
    VECTOR2 point;
    BOOL has_point;
    LPEVENT responseTo;
} GAMEEVENT;

typedef struct {
    LPEDICT edict;
    EVENTTYPE type;
    LPEDICT source;
    LONG value;
    LPCVECTOR2 point;
} gameEventPointParams_t;

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
#define JASS_GROUP_INITIAL_CAPACITY 64 // handle pointer slots; grows dynamically while group objects stay at stable addresses
#define MAX_TRIGGERS 4096 // handles; bounds deterministic per-map trigger registry slots
#define MAX_TIMERS 1024 // handles; bounds deterministic per-map timer registry slots
#define MAX_TIMERDIALOGS 64 // handles; bounds map-lifetime timer-dialog registry slots
#define MAX_LEADERBOARDS 32 // handles; fixed save-stable leaderboard registry
#define MAX_LEADERBOARD_ITEMS 24 // rows; covers classic player/campaign boards
#define WC3_LAYER_TIMERDIALOG LAYER_GAME_0
#define WC3_LAYER_LEADERBOARD LAYER_GAME_1
#define MAX_EVENTS 1024 // handlers; fixed event slots preserve stable pointers across removal
#define MAX_QUESTS 256 // quests; fixed quest slots preserve stable pointers across removal
#define MAX_QUESTITEMS 16 // items per quest; matches the practical quest objective display capacity
#define MAX_WAYPOINTS 256 // entities; fixed g_edicts ring used by point-target movement
#define WC3_PLAYERSTATE_NO_CREEP_SLEEP 25 // common.j playerstate; prevents Neutral Hostile from entering natural night sleep

#ifdef WC3_DEBUG_TUTORIAL_FLOW
#define WC3_TUTORIAL_DEBUG_ENABLED() (gi.CvarString && atoi(gi.CvarString("wc3_quest_debug", "0")) != 0)
#else
#define WC3_TUTORIAL_DEBUG_ENABLED() false
#endif

typedef struct {
    DWORD handle_id; // runtime ordinal in level.groups; rebuilt from slot position on load
    BOOL inuse;
    LPEDICT units[MAX_GROUP_SIZE];
    DWORD num_units;
} ggroup_t;

typedef struct {
    BOOL inuse;
    BOOL enabled;
    DWORD handle_id;
    DWORD effect_id;
    BOX2 bounds;
} gweather_t;

typedef gweather_t *LPGWEATHER;
typedef gweather_t const *LPCGWEATHER;

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

struct gtimerdialog_s {
    LPGTIMER timer;
    BOOL inuse;
    BOOL title_set;
    BOOL title_color_set;
    BOOL time_color_set;
    DWORD visible_clients;
    COLOR32 title_color;
    COLOR32 time_color;
    char title[MAX_TRIGSTR_LENGTH];
};

struct gleaderboarditem_s {
    char label[MAX_TRIGSTR_LENGTH];
    LONG value;
    LONG player; /* player number, -1 = no player */
    BOOL show_label, show_value, show_icon;
    BOOL label_color_set, value_color_set;
    COLOR32 label_color, value_color;
};

struct gleaderboard_s {
    BOOL inuse;
    DWORD displayed_clients;
    BOOL show_label, show_names, show_values, show_icons;
    BOOL label_color_set, value_color_set;
    COLOR32 label_color, value_color;
    LONG size_by_item_count;
    DWORD item_count;
    char label[MAX_TRIGSTR_LENGTH];
    struct gleaderboarditem_s items[MAX_LEADERBOARD_ITEMS];
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
static inline BOOL QuestIsVisible(LPCQUEST quest) { return quest && quest->enabled && quest->discovered; }

typedef struct {
    struct { FLOAT day, night; } sight_radius;
    FLOAT acquisition_range;
    DWORD flags;
} unitbalance_t;

#define UNIT_BALANCE_BUILDING 0x1 // bit; immutable building classification; used by hot AI/FOW paths
#define WC3_UNIT_TYPE_STRUCTURE 2 // handle value; Warcraft structure type; used by IsUnitType
#define WC3_UNIT_TYPE_POLYMORPHED 22 // handle value; Warcraft Polymorphed type; used by IsUnitType
#define WC3_ORDER_ID_POLYMORPH 852074 // order ID; Warcraft Polymorph command; used by order dispatch

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
    DWORD data; /* ability-owned payload; Anti-Magic Shell remaining absorption, 0 otherwise */
} heroabilitystatus_t;

typedef struct {
    DWORD code;       /* normalized AbilityData.code rawcode; zero means unused slot */
    DWORD start_time; /* authoritative game time in milliseconds */
    DWORD end_time;   /* authoritative game time in milliseconds */
} abilityCooldown_t;

typedef struct {
    DWORD start_time;
    DWORD end_time;
} abilityCooldownWindow_t;

typedef struct edictShopStockItem_s {
    DWORD id;
    LONG current;
    DWORD delay_start;
    DWORD delay_end;
} edictShopStockItem_t;

typedef struct edictStock_s {
    DWORD item_slots, unit_slots;
    BOOL items_initialized;
    DWORD item_count;
    edictShopStockItem_t items[MAX_SHOP_STOCK];
} edictStock_t;

typedef struct {
    LPGAMECLIENT client;
    LPEDICT shop;
    gameCommandButton_t *buttons;
    BYTE max_buttons;
} shopItemButtonsParams_t;

typedef struct {
    LPEDICT clent;
    LPEDICT shop;
    LPEDICT carrier;
    LPEDICT item;
} shopPawnItemParams_t;

#define WC3_ANIMATION_REQUEST_SIZE 80
#define WC3_ANIMATION_PROPERTIES_SIZE 128

typedef enum {
    MOVE_FALLBACK_NONE,
    MOVE_FALLBACK_RETRY,
    MOVE_FALLBACK_APPLIED,
} moveFallbackState_t;

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
        constructionType_t type;
        LPEDICT primary_builder; /* Human Repair owner; only meaningful for Human construction */
        LPEDICT worker;          /* Orc/Night Elf internal worker; Undead summoner while casting */
        DWORD worker_spawn_time; /* validates worker pointer across remove/reuse */
        BOOL worker_inside;
        BOOL consumes_worker;
        BOOL restore_invulnerable;
        BOOL restore_paused;
        BOOL restore_hidden;
        DWORD worker_release_time; /* Undead summon animation release time; 0 for other strategies */
        FLOAT progress;
        BOOL paid;
        DWORD payer;
        LONG gold, lumber;
    } construction;
    BOOL training; /* spawned in a production queue but not yet completed */
    BOOL training_food_wait_notified; /* one-shot Nofood feedback for the active queue head */
    struct {
        DWORD upgrade;     /* research rawcode on queue edicts; target unit type on in-place upgrades */
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
    DWORD summon_ability; /* ability rawcode that created this summoned unit; 0 for ordinary units */
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
    struct edictPolymorph_s {
        DWORD ability;          /* Aply-derived ability that owns the active morph */
        DWORD buff;             /* configured timed buff; stock Sorceress uses Bply */
        DWORD form_type;        /* first authored Ply2/Ply3/Ply4/Ply5 unit rawcode */
        DWORD original_model;   /* presentation state restored when the buff ends */
        FLOAT original_scale;
        FLOAT original_move_speed;
        BOOL active;
    } polymorph;
    struct edictRaven_s {
        FLOAT fly_height; /* authored Raven Form height applied after the forward morph clip */
        FLOAT rise_start;
        FLOAT rise_duration;
        ravenRiseState_t rise_state;
    } raven;
    struct edictEnsnare_s {
        FLOAT adjust; /* DataA Air Unit Lower Duration (seconds); 0 snaps */
        FLOAT height; /* DataB land start, or authored moveHeight while rising */
        DWORD start;  /* G_Time() when current land/rise phase began */
        ensnareHeightState_t phase;
    } ensnare;
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
    /* Racial mine overlays keep the original Agld unit as the sole finite
     * gold reservoir. Haunted/Entangled mines own presentation/income only. */
    struct edictMineOverlay_s {
        LPEDICT parent;
        DWORD parent_spawn_time;
        DWORD income_time;
        DWORD active_interval_index;
    } mineoverlay;
    /* Acolyte harvesting is a visible fixed-slot relationship rather than the
     * conventional hidden-inside/carry/return Gold Mine state above. */
    struct edictAcolyteMine_s {
        LPEDICT mine;
        DWORD mine_spawn_time;
        LONG slot;
    } acolyte_mine;
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
    edictStock_t stock;
    FLOAT velocity;
    doodadHero_t hero;
    DWORD hero_shortcut_alert_until; /* transient server clock deadline for the owning player's Hero-button damage pulse */
    heroability_t heroabilities[MAX_HERO_ABILITIES];
    heroabilitystatus_t abilstatus[MAX_UNIT_STATUSES];
    abilityCooldown_t abilitycooldowns[MAX_UNIT_COOLDOWNS];
    edictAbilities_s abilities;
    DWORD autocast_code; /* one selected autocast ability; zero means disabled */
    struct edictAvatar_s {
        DWORD level;
        FLOAT armor, health;
        LONG damage;
    } avatar;
    BOOL invulnerable;  // unit cannot take damage when true
    BOOL paused;        // unit AI and movement suspended when true
    BOOL stunned;       // unit AI and movement suspended by timed status
    BOOL no_pathing;    // pathfinding disabled when true
    struct edictSleep_s {
        BOOL can_sleep; /* mutable natural/night sleep eligibility; seeded from UnitData.canSleep */
        BOOL sleeping;  /* natural creep sleep only; intentionally excludes spell-induced BUsL */
    } sleep;
    struct edictChannel_s {
        DWORD code;     // ability code being channeled (0 = none)
        DWORD serial;   // cast identity; old thinkers cannot continue or cancel a replacement cast
        DWORD owner_spawn_time; // thinker copy of caster identity; rejects reused owner slots
        DWORD target_spawn_time; // thinker copy of target identity; rejects reused target slots
        VECTOR2 origin; // position when channel started (movement cancels channel)
    } channel;
    DWORD unit_color;   // explicit per-unit color override (0 = use owner color)
    LONG user_data;     /* SetUnitUserData script scratch; no gameplay consumer reads it yet */
    BOOL uses_alt_icon; /* UnitSetUsesAltIcon presentation flag; no minimap consumer reads it yet */
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
        VECTOR2 flow_fallback_target; /* last unreachable fallback request */
        FLOAT flow_fallback_radius;
        DWORD flow_fallback_time;
        LPEDICT flow_fallback_goal;
        moveFallbackState_t flow_fallback_state;
        ROUTEPATH path; /* persistent WC3 accelerator state shared with other server games */
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
    LPEDICT item_drop; /* inventory item owned by an active point-drop behavior */
    LPEDICT combatentity;
    LPEDICT secondarygoal;
    LPEDICT owner;
    LPEDICT build;
    LPCANIMATION animation;
    FLOAT animation_speed; /* JASS SetUnitTimeScale multiplier for the simulation animation clock */
    BOOL animation_override; /* JASS presentation animation may advance while gameplay is paused */
    /* Warcraft Required Animation Names (UnitProfile.animProps/uani) plus
     * AddUnitAnimationProperties mutations. The request is retained separately
     * so a property change can reselect the same logical animation family. */
    char animation_request[WC3_ANIMATION_REQUEST_SIZE];
    char animation_props[WC3_ANIMATION_PROPERTIES_SIZE];
    unitbalance_t runtime;
    COLOR32 vertex_color;
    BOOL vertex_color_set;
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
    FLOAT temporary_health_bonus; /* temporary maximum-health modifiers restored on expiration */
    FLOAT mana_regen_bonus; /* research/permanent mana regeneration modifiers */
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
typedef struct edictMineOverlay_s edictMineOverlay_s;
typedef struct edictAcolyteMine_s edictAcolyteMine_s;
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
    /* W3I gameDataSet selects a Warsmash-style Custom_V0/V1 or
     * Melee_V0/V1 sheet-data overlay for the active map. */
    char data_prefix[32];
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
        /* Unit-target Move/Smart follows use WC3 Misc distances, not attack
         * acquisition range. war3mapMisc.txt may override either value. */
        FLOAT followRange;
        FLOAT structureFollowRange;
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
    LPCSTR variable;
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
    VECTOR2 stage; /* SetStagePoint staging area; assault fallback when no enemy target is visible */
    BOOL stage_valid;
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
    LONG hour;
    LONG minute;
    LONG ticks_remaining;
    BOOL active;
    BOOL initialized;
} FALSE_TIMEOFDAY;

typedef struct {
    FLOAT elapsed;
    FLOAT pending;
    BOOL pending_valid;
    BOOL suspended;
    FALSE_TIMEOFDAY false_time;
} TIMEOFDAY;

typedef enum {
    WC3_ENV_FOG_NONE = 0,
    WC3_ENV_FOG_LINEAR,
    WC3_ENV_FOG_EXPONENTIAL_1,
    WC3_ENV_FOG_EXPONENTIAL_2,
} wc3EnvironmentFogStyle_t;

typedef struct {
    LONG style;
    FLOAT start;
    FLOAT end;
    FLOAT density;
    VECTOR3 color;
} wc3EnvironmentFogState_t;

typedef struct {
    wc3EnvironmentFogState_t active;
    wc3EnvironmentFogState_t defaults;
    BOOL defaults_valid;
} wc3EnvironmentFog_t;

typedef struct {
    LONG style;
    FLOAT start, end, density;
    VECTOR3 color;
} wc3EnvironmentFogParams_t;

struct level_locals {
    LPJASS vm;
    ggroup_t **groups;
    DWORD num_groups;
    DWORD group_capacity;
    DWORD first_free_group;
    TRIGGER triggers[MAX_TRIGGERS];
    DWORD num_triggers;
    GTIMER timers[MAX_TIMERS];
    DWORD num_timers;
    TIMERDIALOG timer_dialogs[MAX_TIMERDIALOGS];
    LEADERBOARD leaderboards[MAX_LEADERBOARDS];
    LONG player_leaderboards[MAX_PLAYERS]; /* registry index, -1 = none */
    DWORD leaderboard_dirty_clients;
    DWORD timer_dialog_dirty_clients; /* transient: clients whose timer layer must be resent */
    LONG timer_dialog_last_index[MAX_CLIENTS]; /* transient active-slot cache */
    LONG timer_dialog_last_seconds[MAX_CLIENTS]; /* transient formatted-value cache */
    gweather_t weather_effects[MAX_WEATHER_EFFECTS];
    DWORD next_weather_id;
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
    wc3EnvironmentFog_t environment_fog;
    BOX2 camera_bounds; /* map-global camera target rectangle; W3I default, SetCameraBounds may replace it */
    BOOL started;
    BOOL scriptsStarted;
    BOOL cinematic_debug_result_window; /* per-map debug latch for result-window tracing */
    BOOL campaign_select_on_end; /* ForceCampaignSelectScreen defers campaign selection until EndGame */
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
void G_ResetStartingResourceCheat(void);
void G_DisableStartingResourceCheatForLoadedGame(void);
void G_ApplyStartingResourceCheat(void);
BOOL G_PlayerInstantBuild(DWORD player);
BOOL G_PlayerInstantKill(DWORD player);
BOOL G_RemovePlayerWithResult(DWORD player_num, DWORD game_result);
BOOL G_GameResultDebugEnabled(void);
void G_GameResultDebug(LPCSTR format, ...);
BOOL G_IsSinglePlayer(void);
void G_RequestEndGame(BOOL do_score_screen);
void G_RequestQuitGame(void);
void G_RequestChangeLevel(LPCSTR map, BOOL do_score_screen);
void G_RequestRestartGame(BOOL do_score_screen);
void G_RequestLoadGameMenu(void);
void G_RequestLoadGameNamed(LPCSTR name);
void G_RequestCampaignSelect(void);
void G_CampaignProgressResetRuntime(void);
BOOL G_CampaignProgressSetTutorialCleared(BOOL cleared);
BOOL G_CampaignProgressSetCampaignAvailable(LONG campaign, BOOL available);
BOOL G_CampaignProgressSetMissionAvailable(LONG campaign, LONG mission, BOOL available);
void G_SetScriptPaused(BOOL paused);
void G_SetClientModal(LPEDICT player, DWORD modal, BOOL open);
void G_SetQuestDialogOpen(LPEDICT player, BOOL open);
TARGTYPE G_GetTargetType(LPCSTR);
LPCSTR G_LevelString(LPCSTR);
LPCSTR G_MapString(LPCMAPINFO info, LPCSTR name);
LPCSTR G_UnitName(DWORD);
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
GAMEEVENT *G_PublishEventWithValue(LPEDICT, EVENTTYPE, LPEDICT, LONG);
GAMEEVENT *G_PublishEventWithPoint(gameEventPointParams_t const *params);
void G_PublishSummonEvents(LPEDICT summoner, LPEDICT summoned);
void G_PublishChangeOwnerEvents(LPEDICT unit, DWORD old_player);
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
void G_BotSetCaptainHome(LPPLAYER, LONG, FLOAT, FLOAT);
void G_BotSetStagePoint(LPPLAYER, FLOAT, FLOAT);
BOOL G_BotSuicideUnits(LPPLAYER, LONG, DWORD, LONG);
BOOL G_BotSuicidePlayer(LPPLAYER, DWORD, BOOL);
BOOL G_BotMergeUnits(LPPLAYER, LONG, DWORD, DWORD, DWORD);

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
void G_SetFalseTimeOfDay(LONG hour, LONG minute, FLOAT duration);
BOOL G_IsFalseTimeOfDay(void);
void G_UpdateTimeOfDay(void);
BOOL G_IsNight(void);
#ifdef WC3_DEBUG_CAMERA_TRACE
void G_CameraTraceSnapshotForClient(LPGAMECLIENT, LPCSTR);
void G_CameraTraceSnapshot(LPCSTR);
#endif

// g_environment_fog.c
BOOL G_EnvironmentFogDefault(wc3EnvironmentFogState_t *fog); /* exposed: tests parse singleton DefaultZFog under both editions */
void G_EnvironmentFogInitMap(void);
void G_EnvironmentFogSet(wc3EnvironmentFogParams_t const *params);
void G_EnvironmentFogReset(void);
void G_EnvironmentFogPublish(void);

// skills/s_creep_sleep.c — JASS natural-sleep interface
BOOL G_UnitCanSleep(LPCEDICT);
BOOL G_UnitIsSleeping(LPCEDICT);
void G_UnitSetCanSleep(LPEDICT, BOOL);
void G_UnitWakeUp(LPEDICT);

// g_spawn.c
BOOL WriteGame(LPCSTR filename);
BOOL ReadGame(LPCSTR filename);
LPEDICT G_Spawn(void);
void SP_CallSpawn(LPEDICT);
void G_BindEntityData(LPEDICT);
void G_BindEntityRuntime(LPEDICT);
void G_SpawnEntities(void);
#ifdef BZ_TESTS
BOOL G_TestMapObjectCreatedByMapScript(DWORD id);
#endif
BOOL SP_FindEmptySpaceAround(LPEDICT, DWORD, LPVECTOR2, FLOAT *);
BOOL G_FindUnitUnstuckPosition(LPEDICT unit, LPCVECTOR2 requested, LPVECTOR2 out);
BOOL SP_FindUnitExitPosition(LPEDICT producer, LPEDICT unit, LPVECTOR2 out, FLOAT *angle);
LPEDICT SP_SpawnAtLocation(DWORD, DWORD, LPCVECTOR2);
LPEDICT SP_SpawnAtLocationNoBirth(DWORD, DWORD, LPCVECTOR2);
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
BOOL G_EnsureJassGroupSlots(DWORD count);
BOOL G_JassGroupValid(ggroup_t const *group);
BOOL G_JassGroupIndex(ggroup_t const *group, DWORD *index);
ggroup_t *G_JassGroupByIndex(DWORD index);
BOOL G_QuestValid(QUEST const *quest);
BOOL G_QuestItemValid(QUESTITEM const *item);
void G_FreeJassGroup(ggroup_t *group);
void G_ClearJassGroupRegistry(void);
BOOL G_JassGroupDebugEnabled(void);
void G_ResetJassGroupDebug(void);
void G_SetJassGroupDebugCreator(ggroup_t *group, LPCSTR creator);
void G_SetJassGroupDebugContext(ggroup_t *group, LPCSTR creator, LPCSTR chain, LONG trigger_ordinal);
LPCSTR G_GetJassGroupDebugCreator(ggroup_t const *group);
LPCSTR G_GetJassGroupDebugChain(ggroup_t const *group);
LONG G_GetJassGroupDebugTrigger(ggroup_t const *group);
void G_DumpJassGroupDebug(LPCSTR failing_creator, LPCSTR failing_chain, LONG failing_trigger);
LPGWEATHER G_WeatherAdd(LPCBOX2 bounds, DWORD effect_id, BOOL enabled);
void G_WeatherEnable(LPGWEATHER effect, BOOL enabled);
void G_WeatherRemove(LPGWEATHER effect);
void G_WeatherInitMap(void);
DWORD G_WriteClientDatagram(LPEDICT ent, LPBYTE data, DWORD size);
LPTRIGGER G_AllocJassTrigger(void);
LPGTIMER G_AllocJassTimer(void);
LPTIMERDIALOG G_AllocTimerDialog(LPGTIMER timer);
void G_FreeTimerDialog(LPTIMERDIALOG dialog);
void G_SetTimerDialogVisible(LPTIMERDIALOG dialog, LPPLAYER player, BOOL visible);
BOOL G_IsTimerDialogVisible(LPCTIMERDIALOG dialog, LPCPLAYER player);
void G_MarkTimerDialogDirty(LPCTIMERDIALOG dialog);
void G_UpdateTimerDialogs(void);
void G_FormatTimerDialogValue(LPCGTIMER timer, LPSTR out, size_t out_size);
LPLEADERBOARD G_AllocLeaderboard(void);
void G_FreeLeaderboard(LPLEADERBOARD board);
void G_MarkLeaderboardDirty(LPCLEADERBOARD board);
void G_SetLeaderboardDisplayed(LPLEADERBOARD board, LPPLAYER player, BOOL displayed);
BOOL G_IsLeaderboardDisplayed(LPCLEADERBOARD board, LPCPLAYER player);
void G_UpdateLeaderboards(void);
LPLEADERBOARD G_PlayerLeaderboard(DWORD player);
void G_SetPlayerLeaderboard(DWORD player, LPLEADERBOARD board);
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
void         G_NormalizeModelFilename(LPCSTR authored, LPSTR out, size_t out_size);
int          G_RegisterModel(LPCSTR filename);
LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname);
LPCANIMATION G_SelectAnimationForProperties(LPCANIMATION animations, DWORD count, LPCSTR animname, LPCSTR properties);
LPCANIMATION G_GetAnimationForProperties(DWORD modelindex, LPCSTR animname, LPCSTR properties);
LPCANIMATION G_GetAnimationVariant(DWORD modelindex, LPCSTR animname, BOOL randomize);
BOOL         G_AnimationHasPrimary(LPCANIMATION animation, LPCSTR primary);
LPCANIMATION G_GetUnitAnimation(LPEDICT unit, LPCSTR animname);
void         G_SetUnitAnimation(LPEDICT unit, LPCSTR animname);
void         G_ResetUnitAnimationProperties(LPEDICT unit);
void         G_AddUnitAnimationProperties(LPEDICT unit, LPCSTR properties, BOOL add);
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
void unit_expirestatus(LPEDICT, heroabilitystatus_t *);
void unit_refreshstatusflags(LPEDICT);

// skills/s_move.c — locomotion shared by Move, Follow, Attack, Build and Harvest
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
DWORD M_RefreshHeatmapForMover(LPCEDICT, LPEDICT, FLOAT);
BYTE M_UnitStaticPathingFlags(LPCEDICT);
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
void G_SetHealth(LPEDICT, FLOAT);
void G_AddHealth(LPEDICT, FLOAT);
void S_EnableAbility(LPEDICT, DWORD);
void S_DisableAbility(LPEDICT, DWORD);
void S_RefreshAbilityLevel(LPEDICT, ability_t const *);
BOOL S_UnitPolymorphed(LPCEDICT unit);
BZ_ABILITY_PROC(CAbilityOnFireHuman);
void G_ApplyUnitAbilityTraits(LPEDICT);
void G_SolveCollisions(void);
BOOL M_CheckCollision(LPCVECTOR2, FLOAT);
void G_PushEntity(LPEDICT ent, FLOAT distance, LPCVECTOR2 direction);
void G_PushEntity3(LPEDICT ent, FLOAT distance, LPCVECTOR3 direction);

// g_abilities.c
void S_RunAbilityUpdates(LPEDICT);
BOOL S_UnitAbilityEvent(LPEDICT, abilityMsg_t);
ability_t const *FindAbilityByOrder(LPCSTR);
ability_t const *FindAbilityByClassname(LPCSTR);
ability_t const *FindAbilityForCommand(LPCSTR);
abilityitem_t S_AbilityItem(DWORD code);
BOOL S_AbilityHasCommand(ability_t const *ability);
void S_AbilityCommand(LPEDICT clent, ability_t const *ability);
ability_t const *GetAbilityByIndex(DWORD);
DWORD FindAbilityIndex(LPCSTR);
void InitAbilities(void);
#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void);
#endif
BOOL G_UnitAutocastIsOn(LPEDICT ent, DWORD code);
BOOL G_SetUnitAutocast(LPEDICT ent, DWORD code, BOOL enabled);
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

// hud/hud_resource_text.c
void G_ResourceGainEvent(LPEDICT source, DWORD resource_state, LONG amount);

// hud/hud_unit.c
BYTE G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons);
BOOL G_BuildCommandButton(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, gameCommandButton_t *button);
BOOL G_BuildAllEnabled(void);
BOOL G_WorkerCanBuild(LPEDICT worker, DWORD building_id);
BOOL G_ProducerCanTrain(LPEDICT producer, DWORD unit_id);
BOOL G_ProducerCanResearch(LPEDICT producer, DWORD upgrade_id);
BOOL G_ProducerCanUpgrade(LPEDICT producer, DWORD unit_id);
BOOL G_BuildingUpgradeActive(LPCEDICT building);
void G_GetBuildingUpgradeCosts(buildingUpgradeCostParams_t const *params);
buildCommandState_t G_GetBuildCommandState(LPGAMECLIENT client, LPEDICT worker, DWORD building_id, LPSTR reason, DWORD reason_size);
buildCommandState_t G_GetTrainCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD unit_id, LPSTR reason, DWORD reason_size);
buildCommandState_t G_GetResearchCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD upgrade_id, LONG *next_level, LPSTR reason, DWORD reason_size);
buildCommandState_t G_GetBuildingUpgradeCommandState(buildingUpgradeCommandParams_t const *params);
LONG G_UpgradeGoldCost(DWORD upgrade_id, LONG level_value);
LONG G_UpgradeLumberCost(DWORD upgrade_id, LONG level_value);
FLOAT G_UpgradeResearchTime(DWORD upgrade_id, LONG level_value);
BOOL G_QueueResearch(LPEDICT producer, DWORD upgrade_id);
BOOL G_StartBuildingUpgrade(LPEDICT building, DWORD unit_id);
BOOL G_CancelBuildingUpgrade(LPEDICT building);
void G_StopBuildingUpgrade(LPEDICT building, BOOL refund);
void G_RunBuildingUpgradeFrame(LPEDICT building);
void G_UpdateBuildingUpgradeAnimation(LPEDICT building);
void G_ApplyPlayerUpgradesToUnit(LPEDICT unit);
DWORD G_GetUnitUpgradeForClass(LPCEDICT unit, LPCSTR wanted_class);
BOOL G_ChargeBuilding(LPGAMECLIENT client, DWORD building_id);
void G_RefundBuilding(LPGAMECLIENT client, DWORD building_id);
void G_SnapBuildingPoint(DWORD building_id, LPVECTOR2 point);
void G_GetBuildPlacementPathingFlags(DWORD building_id, LPBYTE prevented, LPBYTE required);
buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, DWORD building_id, LPCVECTOR2 requested, LPVECTOR2 snapped);
BOOL G_DisplaceBuildOccupants(LPEDICT builder, LPEDICT building);
BOOL G_IssueBuildOrder(LPEDICT builder, DWORD building_id, LPCVECTOR2 location);
BOOL G_FindBuildOnTarget(DWORD building_id, LPCVECTOR2 point, LPEDICT *out);
FLOAT G_BuildApproachDistance(DWORD building_id);
BOOL G_StartHumanConstruction(LPEDICT builder, LPEDICT building);
BOOL G_StartOrcConstruction(LPEDICT builder, LPEDICT building);
BOOL G_StartUndeadConstruction(LPEDICT builder, LPEDICT building);
BOOL G_StartNightElfConstruction(LPEDICT builder, LPEDICT building);
BOOL G_StartNightElfOverlayConstruction(LPEDICT building);
void G_RunConstructionFrame(LPEDICT building);
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
BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);
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
void G_AlertHeroShortcutDamage(LPEDICT ent);
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
void UI_RecordTransmissionMessage(LPEDICT);
void UI_ClearTextMessages(LPEDICT);
void UI_InvalidateDialoguePresentation(LPEDICT);
void UI_WriteDialoguePresentation(LPEDICT);
LPCSTR GetBuildCommand(unitRace_t);
void UI_RenderRoute(LPEDICT, LPCSTR);
void UI_ShowMainMenu(LPEDICT);
void UI_ShowGameMenuEndGame(LPEDICT);
void UI_ShowGameMenuConfirmExit(LPEDICT);
void UI_ShowGameMenuSave(LPEDICT);
void UI_ShowGameMenuLoad(LPEDICT);
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
void UI_LoadHudLoading(void);
void UI_LoadHudTimerDialogs(void);
void UI_WriteTimerDialogs(LPEDICT ent);
void UI_LoadHudLeaderboards(void);
void UI_WriteLeaderboard(LPEDICT ent);
void UI_WriteLoadingLayout(LPEDICT ent, LPCMAPINFO info);
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
LPFRAMEDEF UI_FindChildFrameType(LPFRAMEDEF, FRAMETYPE);

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
void G_SetMapUnitOverrides(LPCMAPINFO);
BOOL G_IsReignOfChaosMap(LPCMAPINFO);
DWORD G_MapGameDataSet(LPCMAPINFO);
void G_MapGameDataPrefix(wc3MapGameDataPrefixParams_t const *params);
#ifdef BZ_TESTS
typedef struct { LPCSTR text; void *rows; DWORD count; } slkTestData_t;
BOOL G_SLKStoreOptional(LPCSTR);
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

/* Client-owned background music presentation.  The game resolves Warcraft
 * skin/Music.SLK data per recipient and emits reliable svc_music commands. */
void G_MusicResetState(void);
void G_MusicSyncClient(LPGAMECLIENT client);
void G_MusicSetMap(LPCSTR music_name, BOOL random, LONG index);
void G_MusicClearMap(void);
void G_MusicPlay(LPCSTR music_name, LONG start_ms, LONG fade_ms);
void G_MusicStop(BOOL fade_out);
void G_MusicResume(void);
void G_MusicPlayThematic(LPCSTR music_name, LONG start_ms);
void G_MusicEndThematic(void);
void G_MusicSetVolume(LONG volume);
void G_MusicSetPosition(LONG millisecs);
void G_MusicSetThematicVolume(LONG volume);
void G_MusicSetThematicPosition(LONG millisecs);
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
COLOR32 G_SmartTargetIndicatorColor(DWORD, LPCEDICT);
void G_SendWidgetIndicator(LPEDICT, COLOR32, LPPLAYER);
void G_ShowCommandErrorKey(LPEDICT, LPCSTR, LPCSTR);
void G_ShowCommandErrorText(LPEDICT, LPCSTR);
extern int g_treeFallSounds[3];     /* Sound\Destructibles\TreeFall{1,2,3}.wav configstring indices */
extern BYTE g_numTreeFallSounds;

// g_command.c
LONG G_CompareSelectionOrder(LPCEDICT, LPCEDICT);
DWORD G_GetOrderedSelectedUnits(LPGAMECLIENT, LPEDICT *, DWORD);
void G_SelectEntity(LPGAMECLIENT, LPEDICT);
void G_DeselectEntity(LPGAMECLIENT, LPEDICT);
BOOL G_IsEntitySelected(LPGAMECLIENT, LPEDICT);
BOOL G_FocusSelectedUnit(LPGAMECLIENT, LPEDICT);
BOOL G_CycleSelectionSubgroup(LPGAMECLIENT);
void G_ResetSelectionFocus(LPGAMECLIENT);
BOOL G_UnitCanBeSelected(LPGAMECLIENT, LPCEDICT);
BOOL G_UnitCanControl(LPGAMECLIENT, LPCEDICT);
selectionRelation_t G_SelectionRelation(DWORD viewer, LPCEDICT ent);
LPEDICT G_GetMainControllableUnit(LPGAMECLIENT);
void G_UpdateClientSelections(void);
void G_SyncClientSelection(LPGAMECLIENT);
void G_QueueSelectionSound(LPEDICT);
void G_ClientCommand(LPEDICT, DWORD, LPCSTR[]);
BOOL G_CheatsEnabled(void);
void G_ClientSetCameraPosition(LPEDICT, LPCVECTOR2);

//  s_skills.c
FLOAT AB_Data(LPCSTR, DWORD, DWORD);
DWORD GetAbilityIndex(abilityProc_t);

// g_combat.c
int G_AttackDamage(LPEDICT, LPEDICT, int);
void T_Damage(LPEDICT, LPEDICT, int);

// g_utils.c
void G_FreeEdict(LPEDICT);
void G_DeferFreeEdict(LPEDICT);
BOOL G_IsDeferredFree(LPCEDICT);
void G_RunDeferredFrees(void);
void G_ResetDeferredFrees(void);
LPEVENT G_MakeEvent(EVENTTYPE);
void G_JassVariableChanged(LPCSTR, FLOAT, FLOAT);
BOOL G_LimitMatches(DWORD, FLOAT, FLOAT);
LPQUEST G_MakeQuest(void);
BOOL G_RegionContains(LPCREGION, LPCVECTOR2);
void G_RemoveQuest(LPQUEST);
void G_InitPlayerAlliances(LPCMAPINFO);
void G_SetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE, BOOL);
BOOL G_GetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE);
BOOL G_PlayerTreatsPlayerAsAlly(DWORD, DWORD);

// m_unit.c
BOOL unit_issueorder(LPEDICT, LPCSTR, LPCVECTOR2);
BOOL unit_issueimmediateorder(LPEDICT, LPCSTR);
BOOL unit_issuetargetorder(LPEDICT, LPCSTR, LPEDICT);
BOOL G_TransformUnitType(LPEDICT, DWORD);
BOOL G_IssueUnitPointOrder(LPEDICT, LPCSTR, LPCVECTOR2, BOOL, DWORD, FLOAT);
BOOL G_IssueUnitTargetOrder(LPEDICT, LPCSTR, LPEDICT, BOOL, DWORD);
void G_PublishIssuedPointOrder(LPEDICT, DWORD, LPCVECTOR2, DWORD, LPCSTR);
DWORD G_GetIssuedOrderId(LPCEDICT);
BOOL G_GetIssuedOrderPoint(LPCEDICT, LPVECTOR2);
DWORD G_OrderId(LPCSTR);
LPCSTR G_OrderId2String(DWORD);
BOOL G_UnitStartNextQueuedOrder(LPEDICT);
void G_ClearUnitOrderQueue(LPEDICT);
DWORD G_UnitQueuedOrderCount(LPCEDICT);
void unit_birth(LPEDICT);
void unit_die(LPEDICT, LPEDICT);
LPEDICT unit_create(DWORD, DWORD, LPCVECTOR2, FLOAT);
LPEDICT unit_createorfind(DWORD, DWORD, LPCVECTOR2, FLOAT);
BOOL unit_additemtoslot(LPEDICT, LPEDICT, DWORD);
BOOL unit_additem(LPEDICT, LPEDICT);
void unit_addstatus(LPEDICT, LPCSTR, DWORD);
void unit_addtimedstatus(LPEDICT, LPCSTR, DWORD, FLOAT);
DWORD G_UnitStatusLevel(LPCEDICT, DWORD);
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
BOOL G_HeroModifySkillPoints(LPEDICT ent, LONG delta);

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
void G_ReviveCorpse(LPEDICT, FLOAT life_fraction);
BOOL G_UnitIsHero(LPCEDICT ent);
FLOAT G_UnitArmorValue(LPCEDICT ent);
BOOL S_SpellCooldownReady(LPEDICT caster, DWORD code);
FLOAT S_SpellCooldownRemaining(LPEDICT caster, DWORD code);
FLOAT S_SpellCooldownLength(LPEDICT caster, DWORD code);
BOOL S_SpellCooldownWindow(LPEDICT caster, DWORD code, abilityCooldownWindow_t *window);
FLOAT S_SpellCooldownFraction(LPEDICT caster, DWORD code, DWORD level);
void S_SpellStartCooldownDuration(LPEDICT caster, DWORD code, FLOAT seconds);
void S_SpellStartCooldown(LPEDICT caster, DWORD code, DWORD level);
void S_SpellEndCooldown(LPEDICT caster, DWORD code);
void S_SpellResetCooldowns(LPEDICT caster);
LPCSTR S_SpellString(DWORD code, LPCSTR field, DWORD level);

void order_attack(LPEDICT, LPEDICT);
BOOL S_OrderAttack(LPEDICT self, LPEDICT target);
void order_move(LPEDICT, LPEDICT);
BOOL move_is_active_order_walk(LPCEDICT);
void order_stop(LPEDICT);
void order_attackmove(LPEDICT, LPEDICT);
void order_patrol(LPEDICT, LPEDICT);
void order_patrol_resume(LPEDICT);
void order_follow(LPEDICT, LPEDICT);
void order_follow_resume(LPEDICT);
extern umove_t holdpos_move_stand;
extern umove_t holdpos_move_stand_ready;
void unit_stand(LPEDICT);
BOOL G_ActorHasSkill(LPCEDICT, LPCSTR);
BOOL G_ActorAddSkill(LPEDICT, DWORD);
BOOL G_ActorRemoveSkill(LPEDICT, DWORD);
BOOL G_ActorSetSkillPermanent(LPEDICT, DWORD, BOOL);
BOOL G_ActorSkillPermanent(LPEDICT, DWORD);
void G_FreeActorSkills(LPEDICT);
BOOL S_GoldMineIsMine(LPCEDICT);
BOOL S_GoldMineIsOverlay(LPCEDICT);
BOOL S_UnitTypeIsGoldMine(DWORD);
BOOL S_UnitTypeReturnsGold(DWORD);
DWORD S_GoldMineMaximumGold(LPCEDICT);
FLOAT S_GoldMineMiningDuration(LPCEDICT);
DWORD S_GoldMineCapacity(LPCEDICT);
BOOL S_GoldMineCanHarvest(LPCEDICT);
BOOL S_GoldMineWorkerIsInside(LPCEDICT);
BOOL S_MilitiaTargetOrder(LPEDICT, LPCSTR, LPEDICT);
void S_CancelMilitiaPairing(LPEDICT);
void S_MilitiaExpire(LPEDICT);
BOOL S_StatusIsEnsnare(DWORD);
void S_EnsnareStatusExpired(LPEDICT, heroabilitystatus_t const *);
void S_GoldMineInitUnit(LPEDICT);
void S_GoldMineReleaseWorker(LPEDICT);
BOOL S_MineOverlayBind(LPEDICT, LPEDICT);
void S_MineOverlayBindPreplaced(void);
void S_MineOverlayRelease(LPEDICT);
LPEDICT S_CreateBlightedGoldmine(DWORD, LPCVECTOR2, FLOAT);
void S_GoldMineSetResourceAmount(LPEDICT, DWORD);
BOOL S_AcolyteHarvestOrder(LPEDICT, LPEDICT);
void S_AcolyteHarvestRelease(LPEDICT);
BOOL S_AcolyteHarvestIsActive(LPCEDICT);
void S_EntangledMineTick(LPEDICT);
BOOL S_HarvestCanLumber(LPCEDICT);
BOOL S_HarvestCanGold(LPCEDICT);
void harvest_start(LPEDICT, LPEDICT);
void harvest_gold_start(LPEDICT, LPEDICT);
BOOL harvest_gold_order(LPEDICT, LPEDICT);
BOOL harvest_auto_start_gold(LPEDICT);
BOOL harvest_auto_start_lumber(LPEDICT);
BOOL harvest_lumber_return_to(LPEDICT, LPEDICT);
BOOL harvest_gold_return_to(LPEDICT, LPEDICT);
void cargo_drop_all(LPEDICT);
void S_CargoInitUnit(LPEDICT);
BOOL S_CargoTryLoad(LPEDICT, LPEDICT);
BOOL S_CargoOrderBoard(LPEDICT, LPEDICT);
BOOL S_CargoAttacksEnabled(LPCEDICT);
LPEDICT S_CargoTransportForUnit(LPCEDICT);
void S_CargoReleaseUnit(LPEDICT);
BOOL S_CargoIsBurrow(LPEDICT);
DWORD S_CargoCapacity(LPEDICT);
LPEDICT S_CargoUnitAt(LPCEDICT, DWORD);
BOOL S_CargoUnloadAt(LPEDICT, DWORD);
void S_CargoStandDown(LPEDICT);
void blight_mine_think(LPEDICT);
void blizzard_think(LPEDICT);
void flame_strike_tick(LPEDICT);
void siphon_mana_think(LPEDICT);
void rain_of_fire_think(LPEDICT);
void starfall_think(LPEDICT);
void death_and_decay_think(LPEDICT);
void tranquility_think(LPEDICT);
void earthquake_think(LPEDICT);
void whirlwind_think(LPEDICT);
void volcano_think(LPEDICT);
void pocket_factory_think(LPEDICT);
void stasis_trap_think(LPEDICT);
void rain_of_chaos_think(LPEDICT);
void inferno_think(LPEDICT);
void dark_portal_think(LPEDICT);
void exhume_think(LPEDICT);
void healing_spray_think(LPEDICT);
void cannibalize_think(LPEDICT);
void possession_two_think(LPEDICT);
void lsh_think(LPEDICT);
BOOL move_selectlocation(LPEDICT, LPCVECTOR2);
BOOL move_should_arrive(LPEDICT, FLOAT);
BOOL move_is_blocked(LPEDICT, FLOAT, FLOAT);
BOOL move_is_settled_near_goal(LPEDICT, FLOAT, FLOAT);
BOOL move_is_terminal_hold(LPCEDICT);
void move_reset_progress(LPEDICT);
LPEDICT G_FindNearestEnemy(LPEDICT, FLOAT);
FLOAT G_AcquisitionRange(LPCEDICT);
FLOAT G_FollowStopRange(LPCEDICT follower, LPCEDICT target);
BOOL G_ShouldAcquireThisFrame(LPCEDICT);

// p_jass.c
LPJASS jass_newstate(void);
void jass_close(LPJASS);
BOOL jass_dofile(LPJASS, LPCSTR);
BOOL jass_dofilenative(LPJASS, LPCSTR);
void jass_callbyname(LPJASS, LPCSTR, BOOL);
LPCSTR jass_functionname(struct jass_function const *);
void jass_executetrigger(LPJASS, LPTRIGGER, LPEDICT);
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
BOOL G_OrderDropItemAt(LPEDICT unit, LPEDICT item, LPCVECTOR2 position);
void G_RemoveItem(LPEDICT item);
void G_UseItem(LPEDICT unit, DWORD slot);
DWORD G_ItemTypeFromClass(LPCSTR cls);

// g_stock.c / neutral shops
BOOL G_IsItemShop(LPCEDICT shop);
BOOL G_CanUseItemShop(LPGAMECLIENT client, LPCEDICT shop);
FLOAT G_ShopActivationRadius(LPCEDICT shop);
LPEDICT G_FindShopPatron(LPGAMECLIENT client, LPEDICT shop);
BYTE G_GetShopItemButtons(shopItemButtonsParams_t *params);
BOOL G_ShopPurchaseItem(LPEDICT clent, LPEDICT shop, DWORD item_id);
BOOL G_ShopPawnItem(shopPawnItemParams_t *params);

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
BOOL G_DestructableIsWalkable(LPCEDICT ent);
BOOL G_DestructableCanBeAttackedBy(LPCEDICT attacker, LPCEDICT target);
BOOL G_DestructableAcceptsSmartAttack(LPCEDICT attacker, LPCEDICT target);
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

BOOL G_IsDoodad(LPCEDICT ent);
void G_DoodadAnimationEnd(LPEDICT ent);
typedef struct {
    FLOAT x, y, radius;
    DWORD doodad_id;
    BOOL nearest_only, random_animation;
    LPCSTR anim_name;
} doodadAnimationRadiusParams_t;
DWORD G_SetDoodadAnimationRadius(doodadAnimationRadiusParams_t const *params);
DWORD G_SetDoodadAnimationRect(LPCBOX2 rect, DWORD doodad_id,
                               LPCSTR anim_name, BOOL random_animation);
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
