#ifndef g_local_h
#define g_local_h

#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

#include "common/common.h"
#include "common/weather.h"
#include "games/warcraft-3/common/terrain.h"
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
#define MAX_EVENT_QUEUE 1024
#define MAX_MESSAGE_SUBSCRIBERS 8 // callbacks; bounded because messages are synchronous and game-local
#define MAX_UNIT_SELECT_SOUNDS 6 // sounds; largest UnitAckSounds *What variant list in ROC/TFT data
#define BZ_STRINGIFY_INNER(value) #value
#define BZ_STRINGIFY(value) BZ_STRINGIFY_INNER(value)
#define MAX_ENTITIES MAX_GAME_ENTITIES
#define MAX_REGION_SIZE 16
#define MAX_REGIONS 2048 // fixed region data slots; generation tokens let retired slots be reused safely
#define REGION_TOKEN_SLOT_BITS 13 // 2048 slots plus a two-bit tag; upper uintptr_t bits carry a generation
#define EVENT_TOKEN_SLOT_BITS 12 // 1024 slots plus a two-bit tag; upper bits carry a generation
#define REGION_HANDLE_ID_GENERATION_BITS 17 // keeps region GetHandleId values unique in a positive 28-bit range
#define EVENT_HANDLE_ID_GENERATION_BITS 18 // keeps region-event GetHandleId values unique in a positive 28-bit range
#define REGION_HANDLE_GENERATION_MAX ((1u << REGION_HANDLE_ID_GENERATION_BITS) - 1)
#define EVENT_HANDLE_GENERATION_MAX ((1u << EVENT_HANDLE_ID_GENERATION_BITS) - 1)
#define REGION_HANDLE_ID_BASE 0x10000000u
#define REGION_EVENT_HANDLE_ID_BASE 0x20000000u
#define MAX_INVENTORY 6
#define ITEM_PICKUP_RANGE 150.0f /* world units; classic contextual-pickup reach */
#ifdef WC3_DEBUG_TIMERDIALOG
#define WC3_TIMERDIALOG_LOG(...) fprintf(stderr, "WC3_TIMERDIALOG " __VA_ARGS__)
#else
#define WC3_TIMERDIALOG_LOG(...) ((void)0)
#endif

typedef enum {
    WC3_MAP_GAME_DATA_SET_DEFAULT = 0,
    WC3_MAP_GAME_DATA_SET_CUSTOM = 1,
    WC3_MAP_GAME_DATA_SET_MELEE = 2,
} wc3MapGameDataSet_t;

typedef struct {
    LPCMAPINFO info;
    uint32_t version;
    string_t out;
    uint32_t size;
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
    gi.Write(PF_LONG, &(int32_t){0}); \
    gi.Write(PF_SHORT, &(int32_t){0}); \
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
KNOWN_AS(gmultiboard_s, MULTIBOARD);
KNOWN_AS(gmultiboarditem_s, MULTIBOARDITEM);
KNOWN_AS(gtexttag_s, TEXTTAG);
KNOWN_AS(ghashtable_s, HASHTABLE);
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
    uint32_t unit_id;
    int32_t *gold, *lumber, *food;
} buildingUpgradeCostParams_t;

typedef struct {
    LPGAMECLIENT client;
    LPEDICT producer;
    uint32_t unit_id;
    string_t reason;
    uint32_t reason_size;
} buildingUpgradeCommandParams_t;

typedef enum {
    PLACE_OK,
    PLACE_INVALID_BUILDING,
    PLACE_TERRAIN_BLOCKED,
    PLACE_UNIT_BLOCKED,
    PLACE_REQUIRED_PATHING_MISSING,
    PLACE_REQUIRES_BLIGHT,
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
    uint32_t id;
    int32_t researched;
    int32_t in_progress;
    int32_t max_allowed; /* -1 = unlimited/default */
} playerTechState_t;

typedef struct {
    bool (*on_entity_selected)(LPEDICT, LPEDICT);
    bool (*on_location_selected)(LPEDICT, LPCVECTOR2);
    void (*cmdbutton)(LPEDICT, uint32_t);
    void (*refresh)(LPEDICT);
    uint32_t ability_code;
    bool supports_order_queue; /* active target mode accepts Shift chaining */
    bool order_queued;         /* transient modifier for the current target callback */
    bool order_queue_chained;  /* successful Shift target keeps this mode armed until Shift release */
    bool ability_off;          /* command-card separate-off variant selected for this dispatch */
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
    AI_CORPSE_UNRAISABLE = 1 << 7, /* corpse lifecycle; sacrifice or temporary summon cannot be raised */
    AI_CORPSE_NO_DECAY = 1 << 8, /* corpse lifecycle; remove after death animation instead of corpse window */
    AI_CORPSE_RESERVED = 1 << 9, /* corpse lifecycle; an active consuming ability owns this corpse */
    AI_CORPSE_IN_CARGO = 1 << 10, /* corpse lifecycle; stored in a Meat Wagon cargo slot */
    AI_PROJECTILE_FIXED_TARGET = 1 << 11, /* projectile flies to channel.origin snapshot rather than homing */
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

/* Warcraft common.j targetflag bits used by UnitWeapons ua1g/ua2g. */
enum {
    WC3_TARGET_FLAG_NONE       = 1u,
    WC3_TARGET_FLAG_GROUND     = 2u,
    WC3_TARGET_FLAG_AIR        = 4u,
    WC3_TARGET_FLAG_STRUCTURE  = 8u,
    WC3_TARGET_FLAG_WARD       = 16u,
    WC3_TARGET_FLAG_ITEM       = 32u,
    WC3_TARGET_FLAG_TREE       = 64u,
    WC3_TARGET_FLAG_WALL       = 128u,
    WC3_TARGET_FLAG_DEBRIS     = 256u,
    WC3_TARGET_FLAG_DECORATION = 512u,
    WC3_TARGET_FLAG_BRIDGE     = 1024u,
};

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

    /* Shop sell ids retain retail common.j numbers (269/271/286/288). */
    EVENT_PLAYER_UNIT_SELL = 269,
    EVENT_PLAYER_UNIT_SELL_ITEM = 271,
    EVENT_UNIT_SELL = 286,
    EVENT_UNIT_SELL_ITEM = 288,

    /* Player-unit damaged mirrors EVENT_UNIT_DAMAGED (52) at retail id 308. */
    EVENT_PLAYER_UNIT_DAMAGED = 308,

    EVENT_UNIT_IN_RANGE = 92,
} EVENTTYPE;

/* struct uiFrameDef_s is defined in common/stb_fdf.h (shared with UI module) */

struct gregion_s {
    BOX2 rects[MAX_REGION_SIZE];
    uint32_t num_rects;
    uint8_t inuse;
    uint32_t generation;
    uint8_t exhausted;
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
    float target_distance;
    float far_z;
    float near_z;
//    float angle_of_attack;
    float fov;      /* vertical field of view in degrees */
//    float roll;
//    float rotations;
    float z_offset;
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
    char name[WC3_MUSIC_NAME_MAX];
    wc3MusicSource_t source;
    bool random;
    int32_t index;
    int32_t position_ms;
    int32_t fade_ms;
    uint32_t played_mask;
    bool paused;
    uint32_t session_id;
    bool valid;
} wc3MusicRestore_t;

typedef struct {
    char map_name[WC3_MUSIC_NAME_MAX];
    bool map_random;
    int32_t map_index;
    uint32_t map_session_id;

    char current_name[WC3_MUSIC_NAME_MAX];
    wc3MusicSource_t current_source;
    bool current_random;
    int32_t current_index;
    int32_t current_position_ms;
    int32_t current_fade_ms;
    uint32_t current_played_mask;
    bool paused;
    uint32_t current_session_id;
    uint32_t session_serial;

    wc3MusicRestore_t thematic_restore;

    int32_t volume;
    int32_t thematic_volume;
} wc3MusicState_t;

struct client_s {
    PLAYER ps;
    bool connected; /* ClientBegin completed for this reserved player edict. */
    bool commands_dirty; /* authoritative command availability changed; rebuild after simulation */
    bool selection_dirty; /* JASS selection changed; synchronize once after simulation */
    bool presentation_dirty; /* dialogue/interface/selected-portrait state changed; flush svc_layout after simulation */
    struct {
        uint32_t race_pref, controller;
        uint8_t tax[MAX_PLAYERS][PLAYERSTATE_LUMBER_GATHERED + 1];
        float handicap, handicap_xp;
        bool race_selectable, on_score_screen;
        bool removed;
        uint8_t pending_game_result; /* 0 = none, PLAYER_GAME_RESULT_* + 1 while fallback UI is deferred */
        uint32_t pending_game_result_event; /* level.events.read must reach this write ordinal before fallback UI */
        char name[MAX_PATHLEN];
        uint32_t disabled_abilities[64]; /* SetPlayerAbilityAvailable(false) rawcodes */
        uint32_t disabled_ability_count;
    } jass;
    playerTechState_t tech[MAX_PLAYER_TECH_STATE];
    char playerTextStorage[PLAYERTEXT_COUNT][PLAYER_TEXT_BACKUP][512];
    uint32_t playerTextCursor[PLAYERTEXT_COUNT];
    LPCMAPPLAYER mapplayer;
    uint32_t ping;
    bool no_control, no_ui;
    /* Presentation class the client's window settled on (ui_canvas command); gates widescreen console
     * chrome. Runtime state: the client reports it again before begin. */
    UICANVASCLASS canvas;
    bool cheat_instant_build; /* developer cheat: owner construction/training/research completes on next work tick */
    bool cheat_instant_kill; /* developer cheat: owner damage lethally hits units/buildings/destructables */
    uint32_t modal_flags;
    bool quest_dialog_open;
    uint32_t quest_until; /* FlashQuestDialogButton deadline in simulation milliseconds. */
    menu_t menu;
    struct clientCamera_s {
        CAMERASETUP state;
        CAMERASETUP old_state;
        float target_height;
        uint32_t start_time;
        uint32_t end_time;
        VECTOR2 quick_position; /* SetCameraQuickPosition spacebar target; does not move the camera */
        bool quick_position_set;
        LPEDICT target_controller;
        VECTOR2 target_offset;
        bool target_inherit_orientation;
    } camera;
    /* Info-panel cache. For single units entity/xp track static presentation;
     * HP/mana are retained for save-layout compatibility because live portrait
     * values now use player-state bindings. With entity==0, hp caches the
     * non-single selection count (-1 denotes the building queue panel). */
    struct {
        uint32_t entity;
        int32_t hp;
        int32_t mana;
        int32_t xp;     /* hero experience, so the XP/attribute display updates live */
    } infopanel;
    /* Last resource values reflected in the resource bar, so the server only
     * re-sends LAYER_CONSOLE when a displayed value or tooltip income rate changes. */
    struct {
        int32_t gold;
        int32_t lumber;
        int32_t food_used;
        int32_t food_cap;
        int32_t gold_rate;
        int32_t lumber_rate;
        uint32_t quest_until;
        UICANVASCLASS canvas; /* class the console chrome was last authored for */
    } resourcebar;
    /* Persistent Hero/idle-worker HUD is rebuilt only after gameplay marks it
     * dirty. last_idle_worker is the cycling cursor, not a per-frame cache. */
    struct {
        bool dirty;
        uint32_t last_idle_worker;
    } shortcuts;
    LPEDICT rally_indicator;
    struct {
        VECTOR2 position;
        uint32_t end_time;        /* game time (ms), 0 = inactive */
        char text[1024];
    } message;
    struct {
        char entries[WC3_MESSAGE_LOG_MAX_ENTRIES][WC3_MESSAGE_LOG_ENTRY_SIZE];
        uint32_t first;
        uint32_t count;
    } message_log;
    wc3MusicState_t music; /* client-local Warcraft music semantics; synced on ClientBegin */
    uint32_t cinematic_end_time;       /* game time (ms) when current SetCinematicScene expires, 0 = none */
    uint32_t cinematic_voice_end_time; /* game time (ms) when Portrait Talk becomes Portrait, 0 = not talking */
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
    UNIT_ORDER_TARGET_BUILD,
} unitOrderTargetType_t;

typedef struct {
    char order[UNIT_ORDER_NAME_SIZE];
    unitOrderTargetType_t target_type;
    VECTOR2 point;
    /* Entity-target orders identify their gameplay target here. Queued Build
     * orders instead identify their owner-only Construction Site Indicator so
     * queue teardown can remove presentation without storing process pointers. */
    uint32_t target_number;
    uint32_t target_spawn_time;
    uint32_t issuer_player;
    uint32_t order_id; /* rawcode payload for delayed orders such as construction */
    float group_speed;
} unitOrder_t;

typedef struct {
    unitOrder_t entries[MAX_UNIT_ORDER_QUEUE];
    uint32_t head;
    uint32_t count;
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
typedef struct heroabilitystatus_s heroabilitystatus_t;

/* A resolved use of a shared procedure. Rawcode belongs to the authored ability, not its behavior. */
typedef struct {
    uint32_t code;
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
    A_TARGET_ORDER,     /* Target-owned interaction: handle call->target_order for an order aimed at this unit. */
    A_ORDER_ACCEPTED,   /* Accepted non-queued order that may not install a new move; call->order identifies it. */
    A_UPDATE,           /* Unit frame: update persistent behavior owned by this procedure. */
    A_UNIT_INIT,        /* Spawn/type rebind: initialize behavior from the unit's authored data. */
    A_IDLE,             /* Stand AI: return true after starting an innate idle behavior. */
    A_MOVE_LEAVE,       /* Before replacing a distinct move: release the old behavior's state. */
    A_DAMAGED,          /* Positive post-mitigation damage, before combat response. */
    A_PROJECTILE_HIT,   /* Projectile impact: let owned abilities react before damage. */
    A_UNIT_REMOVE,      /* Before freeing the edict: release behavior-owned resources. */
    A_NO_ACQUIRE,       /* Target query: return true to suppress automatic enemy acquisition. */
    A_CANCEL,           /* Explicit cancellation: return to the unit's ordinary idle behavior. */
    A_DEATH,            /* unit_die: ability-owned death behavior on the dying unit. */
    A_QUEUE_VALIDATE,   /* Train scheduler: queued item may progress this tick; return validity. Payload: call->queue.{producer,item}. */
    A_QUEUE_COMPLETE,   /* Train completion after placement: owner consumes inputs and activates the result. Payload: call->queue. */
    A_QUEUE_CANCEL,     /* Queue cancellation: owner runs its inverse path before the item is freed. Payload: call->queue. */
    A_QUEUE_ORDER_START,  /* Player FIFO: owning order ability starts call->queued_order when it reaches the head. */
    A_QUEUE_ORDER_CANCEL, /* Player FIFO: owning order ability releases queued presentation/state from call->queued_order. */
    A_STATUS_REFRESH,   /* Status set changed: owner reconciles derived state with remaining statuses. Payload: call->status of one remaining status. */
    A_STATUS_REMOVE,    /* Status expiring/dispelled: owner runs its inverse while the slot is still valid. Sent before the slot is wiped. Payload: call->status of the expiring slot. */
    A_STATUS_TICK,      /* Active status scheduler; owner advances its saved next_tick before applying damage. */
    A_STATUS_DEATH,     /* Victim died with this status active; independent of the victim's learned abilities. */
} abilityMsg_t;

#define BZ_ABILITY_PROC(NAME) intptr_t NAME(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call)

typedef intptr_t (*abilityProc_t)(LPEDICT ent, abilityMsg_t msg, abilityCall_t const *call);

struct ability_call_s {
    abilityitem_t const *item;
    union {
        spellTarget_t const *target;
        LPEDICT client;
        LPEDICT projectile;
        cstring_t order;
        struct { LPEDICT issuer; cstring_t order; } target_order; /* A_TARGET_ORDER */
        cstring_t classname;
        uint32_t level;
        bool enabled;
        struct { LPEDICT producer; LPEDICT item; } queue; /* A_QUEUE_*: owning producer and queued item. */
        unitOrder_t const *queued_order; /* A_QUEUE_ORDER_*: entry being started or discarded from the player FIFO. */
        struct { heroabilitystatus_t *slot; uint32_t ability; } status; /* A_STATUS_*: status slot (valid during REMOVE) and origin ability rawcode. */
    };
};

struct ability_s {
    cstring_t classname;
    abilityProc_t proc;
    uint32_t flags;
    spellTargetType_t target_type;
    cstring_t const *orders;
};

typedef struct {
    cstring_t animation;
    void (*think)(LPEDICT);
    void (*endfunc)(LPEDICT);
    abilityProc_t proc;
    /* Optional authoritative duration for presentation-only moves such as
     * Warcraft corpse decay. M_MoveFrame maps the selected model sequence
     * across this duration instead of assuming one model frame per ms tick. */
    float (*animation_duration)(LPCEDICT);
} umove_t;

typedef struct {
    attackType_t type;
    weaponType_t weapon;
    VECTOR3 origin;
    uint32_t damageBase;
    uint32_t numberOfDice;
    uint32_t sidesPerDie;
    /* Warsmash keeps permanent range changes separate from temporary green/red
     * attack bonuses. damageBase includes permanentDamageBonus; rolls add
     * temporaryDamageBonus after the dice. */
    float permanentDamageBonus;
    float temporaryDamageBonus;
    float damagePoint;
    float cooldown;
    float range;
    uint32_t targetsAllowed; /* WC3 targetflag bitmask (ua1g/ua2g) */
    /* Splash (area-of-effect) attack: full/medium/small radii and the damage
     * factors applied in the medium and small rings. */
    float areaFull;
    float areaMedium;
    float areaSmall;
    float factorMedium;
    float factorSmall;
    uint32_t maxTargets;   /* bounce: max chained targets (utc1) */
    float damageLoss;   /* bounce: fractional damage lost per bounce (udl1) */
    struct {
        uint32_t model;
        float arc;
        float speed;
    } projectile;
} unitAttack_t;

typedef struct {
    float value;
    float max_value;
} EDICTSTAT;
typedef EDICTSTAT edictStat_s;

typedef struct edictAbilities_s {
    uint32_t added[MAX_ABILITIES];
    uint32_t added_count;
    uint32_t removed[MAX_ABILITIES];
    uint32_t removed_count;
    uint32_t permanent[MAX_ABILITIES];
    uint32_t permanent_count;
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
    uint32_t edict_spawn_time;
    bool edict_spawn_tracked;
    LPEDICT source;
    uint32_t source_spawn_time;
    bool source_spawn_tracked;
    int32_t value; /* scalar JASS callback payload (for example spell/research rawcode) */
    VECTOR2 point;
    bool has_point;
    LPEVENT responseTo;
} GAMEEVENT;

typedef struct {
    LPEDICT edict;
    EVENTTYPE type;
    LPEDICT source;
    int32_t value;
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
    uint32_t actor;
    uint32_t target;
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
    uint32_t class_id;
    VECTOR2 origin;
} gitem_t;

#define MAX_GROUP_SIZE 256 // entities; Warcraft III group enumeration cap used by JASS group handles
#define JASS_GROUP_INITIAL_CAPACITY 64 // handle pointer slots; grows dynamically while group objects stay at stable addresses
#define MAX_TRIGGERS 4096 // handles; bounds deterministic per-map trigger registry slots
#define MAX_TIMERS 1024 // handles; bounds deterministic per-map timer registry slots
#define MAX_TIMERDIALOGS 64 // handles; bounds map-lifetime timer-dialog registry slots
#define MAX_LEADERBOARDS 32 // handles; fixed save-stable leaderboard registry
#define MAX_LEADERBOARD_ITEMS 24 // rows; covers classic player/campaign boards
#define MAX_MULTIBOARDS 16 // handles; DotA/scoreboard boards stay well under this
#define MAX_MULTIBOARD_ROWS 24 // rows; covers DotA player list plus header rows
#define MAX_MULTIBOARD_COLS 12 // columns; covers KDA/gold/item scoreboard layouts
#define MAX_MULTIBOARD_CELLS (MAX_MULTIBOARD_ROWS * MAX_MULTIBOARD_COLS) // cells; flat row-major storage
#define MAX_MULTIBOARD_VALUE 96 // chars; scoreboard cell text, not TRIGSTR blobs
#define MAX_MULTIBOARD_ITEMS 256 // views; MultiboardGetItem refcounted cell handles
#define MAX_TEXTTAGS 100 // handles; retail-ish floating-text pool
#define MAX_HASHTABLES 256 // handles; DotA uses many tables but not thousands; overflow logs to stderr
#define MAX_HASHTABLE_ENTRIES 65536 // hard cap per table; grow from a small capacity
#define MAX_HASHTABLE_TYPE 24 // chars; longest JASS handle type name + NUL for nested HT_HANDLE slots
#define HASHTABLE_HANDLE_ID_BASE 0x100000u // keep allocated GetHandleId values out of low edict range
#define MAX_GAMECACHE_ENTRIES 256 // mission/key slots per campaign cache blob
#define MAX_GAMECACHE_STRING 256 // chars; shared string cap for gamecache and hashtable string slots
#define WC3_LAYER_TIMERDIALOG LAYER_GAME_0
#define WC3_LAYER_LEADERBOARD LAYER_GAME_1
#define MAX_EVENTS 1024 // handlers; region-event tokens allow safe reuse of retired handler slots
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
    uint32_t handle_id; // runtime ordinal in level.groups; rebuilt from slot position on load
    bool inuse;
    LPEDICT units[MAX_GROUP_SIZE];
    uint32_t num_units;
} ggroup_t;

typedef struct {
    bool inuse;
    bool enabled;
    uint32_t handle_id;
    uint32_t effect_id;
    BOX2 bounds;
} gweather_t;

typedef gweather_t *LPGWEATHER;
typedef gweather_t const *LPCGWEATHER;

typedef struct GLIGHTNING {
    bool inuse;
    LIGHTNINGEFFECT state;
    LPEDICT source_entity;
    uint32_t source_spawn_time;
    LPEDICT target_entity;
    uint32_t target_spawn_time;
    float script_color[4];
} GLIGHTNING;
typedef GLIGHTNING *LPGLIGHTNING;
typedef GLIGHTNING const *LPCGLIGHTNING;

typedef struct LIGHTNINGADDPARAMS {
    uint32_t effect_id;
    LPCVECTOR3 source, target;
    COLOR32 color;
    uint32_t duration_ms;
} LIGHTNINGADDPARAMS;
typedef LIGHTNINGADDPARAMS *LPLIGHTNINGADDPARAMS;
typedef LIGHTNINGADDPARAMS const *LPCLIGHTNINGADDPARAMS;

typedef struct ABILITYLIGHTNINGPARAMS {
    uint32_t ability_id, index;
    LPCEDICT source, target;
    uint32_t duration_ms;
} ABILITYLIGHTNINGPARAMS;
typedef ABILITYLIGHTNINGPARAMS *LPABILITYLIGHTNINGPARAMS;
typedef ABILITYLIGHTNINGPARAMS const *LPCABILITYLIGHTNINGPARAMS;

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
    bool disabled;
};

struct gtimer_s {
    struct jass_function const *handler;
    uint32_t duration, remaining;
    uint32_t generation;
    bool periodic, paused, running;
};

struct gtimerdialog_s {
    LPGTIMER timer;
    bool inuse;
    bool title_set;
    bool title_color_set;
    bool time_color_set;
    uint32_t visible_clients;
    COLOR32 title_color;
    COLOR32 time_color;
    char title[MAX_TRIGSTR_LENGTH];
};

struct gleaderboarditem_s {
    char label[MAX_TRIGSTR_LENGTH];
    int32_t value;
    int32_t player; /* player number, -1 = no player */
    bool show_label, show_value, show_icon;
    bool label_color_set, value_color_set;
    COLOR32 label_color, value_color;
};

struct gleaderboard_s {
    bool inuse;
    uint32_t displayed_clients;
    bool show_label, show_names, show_values, show_icons;
    bool label_color_set, value_color_set;
    COLOR32 label_color, value_color;
    int32_t size_by_item_count;
    uint32_t item_count;
    char label[MAX_TRIGSTR_LENGTH];
    struct gleaderboarditem_s items[MAX_LEADERBOARD_ITEMS];
};

struct gmultiboardcell_s {
    char value[MAX_MULTIBOARD_VALUE];
    char icon[MAX_PATHLEN];
    float width;
    bool show_value, show_icon;
    bool value_color_set;
    COLOR32 value_color;
};

struct gmultiboard_s {
    bool inuse;
    uint32_t displayed_clients;
    uint32_t minimized_clients;
    uint32_t rows, cols;
    char title[MAX_TRIGSTR_LENGTH];
    struct gmultiboardcell_s cells[MAX_MULTIBOARD_CELLS];
};

/* Refcounted view into one multiboard cell; ReleaseItem frees the view, not the cell. */
struct gmultiboarditem_s {
    bool inuse;
    uint32_t refs;
    int32_t board; /* registry index; -1 when the board was destroyed */
    int32_t row, col;
};

struct gtexttag_s {
    bool inuse;
    uint32_t visible_clients;
    bool permanent;
    float height, height_offset;
    float x, y;
    float xvel, yvel;
    float age, lifespan, fadepoint;
    COLOR32 color;
    LPEDICT unit; /* SetTextTagPosUnit anchor; NULL when unset */
    char text[MAX_MULTIBOARD_VALUE];
};

typedef enum {
    HT_INTEGER = 1,
    HT_REAL,
    HT_BOOLEAN,
    HT_STRING,
    HT_HANDLE,
} hashtableSlotType_t;

typedef struct {
    int32_t parent, child;
    hashtableSlotType_t type;
    char handle_type[MAX_HASHTABLE_TYPE]; /* HT_HANDLE only; SaveUnitHandle vs SaveItemHandle */
    union {
        int32_t integer;
        float real;
        bool boolean;
        handle_t handle;
        char string[MAX_GAMECACHE_STRING];
    } value;
} hashtableEntry_t;

struct ghashtable_s {
    bool inuse;
    uint32_t num_entries, capacity; /* capacity is runtime only; entries pointer is not in the level schema */
    hashtableEntry_t *entries;
};

struct gquestitem_s {
    string_t description;
    bool completed;
    bool inuse;
};

struct gquest_s {
    string_t title;
    string_t description;
    string_t iconPath;
    QUESTITEM items[MAX_QUESTITEMS];
    uint32_t num_items;
    bool discovered;
    bool required;
    bool completed;
    bool failed;
    bool enabled;
    bool inuse;
};

/* Quest rows are present in the journal only while both server visibility gates are enabled. */
static inline bool QuestIsVisible(LPCQUEST quest) { return quest && quest->enabled && quest->discovered; }

typedef struct {
    struct { float day, night; } sight_radius;
    float acquisition_range;
    uint32_t flags;
} unitbalance_t;

#define UNIT_BALANCE_BUILDING 0x1 // bit; immutable building classification; used by hot AI/FOW paths
#define UNIT_BALANCE_PERMANENT_INVISIBLE 0x2 // bit; cached Apiv classification for hot per-viewer FOW checks
#define WC3_UNIT_TYPE_STRUCTURE 2 // handle value; Warcraft structure type; used by IsUnitType
#define WC3_UNIT_TYPE_POLYMORPHED 22 // handle value; Warcraft Polymorphed type; used by IsUnitType
#define WC3_UNIT_STATE_LIFE 0 // handle value; UNIT_STATE_LIFE
#define WC3_ORDER_ID_POLYMORPH 852074 // order ID; Warcraft Polymorph command; used by order dispatch

typedef struct {
    uint32_t code;
    uint32_t level;
} heroability_t;

typedef enum {
    GAMECACHE_INTEGER = 1,
    GAMECACHE_REAL,
    GAMECACHE_BOOLEAN,
    GAMECACHE_UNIT,
    GAMECACHE_STRING,
} gameCacheValueType_t;

typedef struct {
    uint32_t item_id;
    uint32_t charges;
} gameCacheItem_t;

#define WC3_UNIT_COLOR_OVERRIDE_FLAG 0x80000000u // bit; distinguishes explicit PLAYER_COLOR_RED from the zero/default owner-color state
#define WC3_UNIT_COLOR_VALUE_MASK 0x0000001fu // five-bit playercolor payload; effect_flags reserves zero for "no published override"
#define WC3_PLAYER_COLOR_LIGHT_GRAY 8 // playercolor index; canonical Neutral Passive presentation color

typedef struct {
    uint32_t class_id;
    doodadHero_t hero;
    heroability_t abilities[MAX_HERO_ABILITIES];
    EDICTSTAT health;
    EDICTSTAT mana;
    uint32_t unit_color;
    gameCacheItem_t inventory[MAX_INVENTORY];
} gameCacheUnit_t;

typedef struct {
    UINAME mission;
    UINAME key;
    gameCacheValueType_t type;
    union {
        int32_t integer;
        float real;
        bool boolean;
        char string[MAX_GAMECACHE_STRING];
        gameCacheUnit_t unit;
    } value;
} gameCacheEntry_t;

typedef struct {
    PATHSTR campaign;
    uint32_t num_entries;
    bool dirty;
    gameCacheEntry_t entries[MAX_GAMECACHE_ENTRIES];
} gameCache_t;

typedef enum {
    HERO_SKILL_ABSENT,
    HERO_SKILL_NO_POINTS,
    HERO_SKILL_LEVEL_LOCKED,
    HERO_SKILL_AVAILABLE,
    HERO_SKILL_MAXED
} heroSkillState_t;

typedef struct heroabilitystatus_s {
    uint32_t code;
    uint32_t level;
    uint32_t timestamp;
    uint32_t duration_ms; /* milliseconds; original timed-status duration, 0 for persistent state */
    uint32_t data; /* applying ability rawcode for lifecycle dispatch; legacy Anti-Magic Shell absorption payload */
    LPEDICT source; /* applying entity; F_EDICT fixup, checked against source_spawn_time before use */
    uint32_t source_spawn_time, rank, next_tick; /* source incarnation, applying ability rank, next pulse in milliseconds */
} heroabilitystatus_t;

typedef struct {
    uint32_t code;       /* normalized AbilityData.code rawcode; zero means unused slot */
    uint32_t start_time; /* authoritative game time in milliseconds */
    uint32_t end_time;   /* authoritative game time in milliseconds */
} abilityCooldown_t;

typedef struct {
    uint32_t start_time;
    uint32_t end_time;
} abilityCooldownWindow_t;

typedef struct edictShopStockItem_s {
    uint32_t id;
    int32_t current;
    int32_t maximum;
    uint32_t delay_start;
    uint32_t delay_end;
} edictShopStockItem_t;

typedef struct edictStock_s {
    uint32_t item_slots, unit_slots;
    bool items_initialized;
    uint32_t item_count;
    edictShopStockItem_t items[MAX_SHOP_STOCK];
    bool units_initialized;
    uint32_t unit_count;
    edictShopStockItem_t units[MAX_SHOP_STOCK];
} edictStock_t;

typedef struct {
    LPGAMECLIENT client;
    LPEDICT shop;
    gameCommandButton_t *buttons;
    uint8_t max_buttons;
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

typedef struct edictArtillery_s {
    uint32_t attack_type, area_targets, targets_allowed;
    float area_full, area_medium, area_small, factor_medium, factor_small;
} edictArtillery_t;

struct edict_s {
    entityState_t s;
    LPGAMECLIENT client;
    pathTex_t *pathtex;
    float collision;
    BOX2 bounds;
    uint32_t svflags;
    uint32_t selected;
    uint32_t areanum;
    LINK area;
    bool inuse;
    BOX2 areabounds;

    // keep above in sync with server.h
    uint32_t class_id;
    uint32_t variation;
    uint32_t build_project;
    LPEDICT build_preview; /* translucent Construction Site Indicator for an accepted build order */
    bool rally_indicator;
    struct edictConstruction_s {
        bool active;
        bool paused;
        constructionType_t type;
        LPEDICT primary_builder; /* Human Repair owner; only meaningful for Human construction */
        LPEDICT worker;          /* Orc/Night Elf internal worker; Undead summoner while casting */
        uint32_t worker_spawn_time; /* validates worker pointer across remove/reuse */
        bool worker_inside;
        bool consumes_worker;
        bool restore_invulnerable;
        bool restore_paused;
        bool restore_hidden;
        uint32_t worker_release_time; /* Undead summon animation release time; 0 for other strategies */
        float progress;
        bool paid;
        uint32_t payer;
        int32_t gold, lumber;
    } construction;
    bool training; /* spawned in a production queue but not yet completed */
    bool training_food_wait_notified; /* one-shot Nofood feedback for the active queue head */
    struct {
        uint32_t upgrade;     /* research rawcode on queue edicts; target unit type on in-place upgrades */
        int32_t level;        /* 1-based level being researched */
        int32_t gold, lumber; /* exact charged cost, retained for cancellation */
        float duration;    /* seconds */
        float progress;    /* seconds elapsed for the active queue head */
    } research;
    struct edictRally_s {
        rallyTargetType_t type;
        VECTOR2 point;
        LPEDICT entity;
        uint32_t entity_spawn_time;
    } rally;
    struct {
        int32_t used; /* food currently accounted to s.player; queue-head reservations live here */
        int32_t made; /* food capacity currently accounted to s.player */
    } food;
    struct {
        uint32_t ability;
        bool primary;
        float gold_accum;
        float lumber_accum;
    } buildwork;
    /* Hero revival state lives on the persistent Hero edict. While reviving,
     * queue_next links the Hero into a producer's ordinary production chain
     * without borrowing hero->build, which may have independent gameplay use. */
    struct edictRevival_s {
        bool awaiting;
        bool reviving;
        LPEDICT producer;
        LPEDICT queue_next;
        uint32_t player;
        int32_t gold, lumber;
        float progress;
    } revival;
    /* A sacrifice queue item is the hidden result unit.  Keep the consumed
     * worker relationship on that item so cancellation/save-load do not need
     * Sacrificial-Pit-specific state in generic unit AI. */
    struct edictSacrifice_s {
        bool active;
        LPEDICT worker;
        uint32_t worker_spawn_time;
        bool restore_paused;
        bool restore_hidden;
    } sacrifice;
    struct edictUnsummon_s {
        LPEDICT target;
        uint32_t target_spawn_time;
        uint32_t ability, level;
        bool approaching, starting;
        float removed_health;
        int32_t gold_paid, lumber_paid;
    } unsummon;
    uint32_t spawn_time;
    uint32_t summon_ability; /* ability rawcode that created this summoned unit; 0 for ordinary units */
    uint32_t permanent_invisibility_reveal_until; /* Apiv: visible until this server-time deadline after spawn/attack/cast */
    uint32_t harvested_lumber;
    uint32_t harvested_gold;
    struct edictMilitia_s {
        uint32_t ability;          /* Amil alias that supplied Data A/B and duration */
        uint32_t normal_type;      /* Data A: worker form retained across the timed morph */
        uint32_t militia_type;     /* Data B: alternate combat form */
        LPEDICT partner;        /* Hall being approached for militia/militiaoff */
        uint32_t partner_spawn_time;
        uint8_t previous_resource; /* returnResource_t remembered for explicit Back to Work */
        bool active;            /* unit has completed the Peasant -> Militia morph */
        bool returning;         /* current pairing order is militiaoff */
    } militia;
    struct edictPolymorph_s {
        uint32_t ability;          /* Aply-derived ability that owns the active morph */
        uint32_t buff;             /* configured timed buff; stock Sorceress uses Bply */
        uint32_t form_type;        /* first authored Ply2/Ply3/Ply4/Ply5 unit rawcode */
        uint32_t original_model;   /* presentation state restored when the buff ends */
        float original_scale;
        float original_move_speed;
        bool active;
    } polymorph;
    struct edictRaven_s {
        float fly_height; /* authored Raven Form height applied after the forward morph clip */
        float rise_start;
        float rise_duration;
        ravenRiseState_t rise_state;
    } raven;
    struct edictBlightGrowth_s {
        uint32_t ability;      /* concrete Abli-derived alias owning this state */
        float radius;       /* current expanded radius */
        uint32_t next_update;  /* next authored expansion deadline */
    } blight_growth;
    struct edictEnsnare_s {
        float adjust; /* DataA Air Unit Lower Duration (seconds); 0 snaps */
        float height; /* DataB land start, or authored moveHeight while rising */
        uint32_t start;  /* G_Time() when current land/rise phase began */
        ensnareHeightState_t phase;
    } ensnare;
    uint32_t heatmap2;
    VECTOR2 heatmap2_origin;  /* target position when heatmap2 was last built */
    uint32_t heatmap2_time;      /* level.time when heatmap2 was last built */
    float heatmap2_radius;    /* mover collision radius used for heatmap2 */
    uint32_t peonsinside;
    uint32_t aiflags;
    uint32_t damage;
    uint32_t projectile_attack_type; /* basic missile attack type captured at launch */
    /* Impact behavior captured by fixed-point artillery shots. */
    edictArtillery_t artillery;
    uint32_t resources;
    uint32_t freetime;
    struct edictGoldMine_s {
        LPEDICT mine;
        uint32_t mine_spawn_time;
        bool restore_invulnerable;
    } goldmine;
    /* Racial mine overlays keep the original Agld unit as the sole finite
     * gold reservoir. Haunted/Entangled mines own presentation/income only. */
    struct edictMineOverlay_s {
        LPEDICT parent;
        uint32_t parent_spawn_time;
        uint32_t income_time;
        uint32_t active_interval_index;
    } mineoverlay;
    /* Acolyte harvesting is a visible fixed-slot relationship rather than the
     * conventional hidden-inside/carry/return Gold Mine state above. */
    struct edictAcolyteMine_s {
        LPEDICT mine;
        uint32_t mine_spawn_time;
        int32_t slot;
    } acolyte_mine;
    LPEDICT inventory[MAX_INVENTORY];
    struct edictItem_s {
        LPEDICT carrier;
        int32_t inventory_slot;
        bool in_world;
        uint32_t charges;
        uint32_t drop_id;        /* SetItemDropID unit rawcode metadata */
        int32_t user_data;       /* SetItemUserData script scratch */
        bool pawnable_set;    /* SetItemPawnable overrode ItemData.pawnable */
        bool pawnable;        /* effective pawnable when pawnable_set */
    } item;
    struct edictDestructable_s {
        bool initialized;

        /* Set only for destructables originating from war3map.doo. */
        bool map_placed;

        /*
         * During generated map initialization, CreateDestructable() binds named
         * gg_dest_* handles back to these already-created map instances.
         * One preplaced instance may be claimed only once.
         */
        bool script_bound;

        bool dead;
        bool blighted; /* one-way destructable presentation state */
        bool pathing_active;
        bool placement_solid;
        bool loot_processed;

        uint32_t editor_id;
        uint32_t item_table;

        pathTex_t *alive_pathtex;
        pathTex_t *death_pathtex;
        float alive_collision;

        ARRAY(droppableItemSet_t const, drop_sets);
    } destructable;
    struct edictCargo_s {
        LPEDICT units[MAX_CARGO];
        uint32_t count;
    } cargo;
    LPEDICT ground_next;
    edictStock_t stock;
    float velocity;
    doodadHero_t hero;
    uint32_t hero_shortcut_alert_until; /* transient server clock deadline for the owning player's Hero-button damage pulse */
    heroability_t heroabilities[MAX_HERO_ABILITIES];
    heroabilitystatus_t abilstatus[MAX_UNIT_STATUSES];
    abilityCooldown_t abilitycooldowns[MAX_UNIT_COOLDOWNS];
    edictAbilities_s abilities;
    uint32_t autocast_code; /* one selected autocast ability; zero means disabled */
    struct edictAvatar_s {
        uint32_t level;
        float armor, health;
        int32_t damage;
    } avatar;
    bool invulnerable;  // unit cannot take damage when true
    bool paused;        // unit AI and movement suspended when true
    bool stunned;       // unit AI and movement suspended by timed status
    bool no_pathing;    // pathfinding disabled when true
    bool timed_life_paused; /* UnitPauseTimedLife: freeze BTLF expiry while set */
    uint32_t script_unit_types; /* UnitAddType/UnitRemoveType bitmask; bit N = UNIT_TYPE N */
    struct edictSleep_s {
        bool can_sleep; /* mutable natural/night sleep eligibility; seeded from UnitData.canSleep */
        bool sleeping;  /* natural creep sleep only; intentionally excludes spell-induced BUsL */
    } sleep;
    struct edictChannel_s {
        uint32_t code;     // ability code being channeled (0 = none)
        uint32_t serial;   // cast identity; old thinkers cannot continue or cancel a replacement cast
        uint32_t owner_spawn_time; // thinker copy of caster identity; rejects reused owner slots
        uint32_t target_spawn_time; // thinker copy of target identity; rejects reused target slots
        VECTOR2 origin; // position when channel started (movement cancels channel)
    } channel;
    uint32_t unit_color;   // WC3_UNIT_COLOR_OVERRIDE_FLAG | playercolor; zero uses owner color
    int32_t user_data;     /* SetUnitUserData script scratch; no gameplay consumer reads it yet */
    bool uses_alt_icon; /* UnitSetUsesAltIcon presentation flag; no minimap consumer reads it yet */
    VECTOR2 old_origin;
    unitOrderQueue_t order_queue;
    struct edictWaygate_s {
        VECTOR2 destination;
        bool destination_set;
        bool active;
    } waygate;
    struct edictMovement_s {
        VECTOR2 last_origin;
        float last_distance;
        uint32_t blocked_frames;
        uint32_t flow_generation; /* active static-route field selected this tick */
        bool flow_goal_reached; /* mover occupies the route's adjusted goal cell */
        bool flow_unreachable;  /* field exists but current cell has no route */
        bool flow_direct;       /* static path from mover to requested goal is clear */
        bool displacement_active; /* temporary construction exit is being walked */
        VECTOR2 displacement_target;
        VECTOR2 flow_fallback_target; /* last unreachable fallback request */
        VECTOR2 flow_fallback_approach; /* temporary reachable waypoint; target remains authoritative */
        float flow_fallback_radius;
        uint32_t flow_fallback_time;
        uint32_t waygate_target_spawn_time; /* guards the target edict while explicitly approaching a Way Gate */
        LPEDICT waygate_target; /* authoritative gate target owned by CAbilityWarp */
        LPEDICT waygate_goal; /* CAbilityWarp-owned approach waypoint/entity */
        LPEDICT flow_fallback_goal;
        moveFallbackState_t flow_fallback_state;
        ROUTEPATH path; /* persistent WC3 accelerator state shared with other server games */
        float group_speed;  // slowest member's speed for a group move (0 = no cap), keeps the group together
        float heading;      // avoidance-resolved heading chosen this tick by unit_changeangle; movement follows it
        VECTOR2 worker_avoid_origin; /* start of the active resource-worker avoidance corridor */
        float worker_avoid_heading;  /* direct corridor heading captured when local blocking begins */
        uint32_t worker_avoid_blocked_frames; /* consecutive blocked decisions before queue escape */
        bool worker_avoid_active;    /* resource-worker corridor is constraining lateral sidesteps */
        LPEDICT attackmove_waypoint;  // resume attack-move after a combat detour
        LPEDICT patrol_a, patrol_b, patrol_target;
        LPEDICT follow_target;        // persistent unit-target Move/Smart goal; resumed after combat
        bool holding_position;
    } movement;
    EDICTSTAT health;
    EDICTSTAT mana;
    MOVETYPE movetype;
    bool projectile_reflected; /* basic attack missile has already been returned by Defend */
    TARGTYPE targtype;
    LPEDICT goalentity;
    LPEDICT item_drop; /* inventory item owned by an active point-drop behavior */
    LPEDICT combatentity;
    LPEDICT secondarygoal;
    LPEDICT owner;
    LPEDICT build;
    LPCANIMATION animation;
    float animation_speed; /* JASS SetUnitTimeScale multiplier for the simulation animation clock */
    bool animation_override; /* JASS presentation animation may advance while gameplay is paused */
    /* Warcraft Required Animation Names (UnitProfile.animProps/uani) plus
     * AddUnitAnimationProperties mutations. The request is retained separately
     * so a property change can reselect the same logical animation family. */
    char animation_request[WC3_ANIMATION_REQUEST_SIZE];
    char animation_props[WC3_ANIMATION_PROPERTIES_SIZE];
    unitbalance_t runtime;
    COLOR32 vertex_color;
    bool vertex_color_set;
    bool vertex_color_override_set;
    umove_t *currentmove;
    unitRace_t race;
    float wait;
    UNITINFO unitinfo;
    unitAttack_t attack1;
    unitAttack_t attack2;
    uint32_t defense_type;   /* WC3 defType index: small/medium/large/fort/normal/hero/divine/none */
    float armor_value;    /* computed armor ('realdef', incl. hero AGI/modifiers) */
    float permanent_armor_bonus; /* research/permanent modifiers preserved across hero recompute */
    float temporary_armor_bonus; /* item/temporary modifiers preserved across hero recompute */
    float permanent_health_bonus; /* research/permanent maximum-health modifiers preserved across hero recompute */
    float temporary_health_bonus; /* temporary maximum-health modifiers restored on expiration */
    float temporary_mana_bonus; /* item/temporary maximum-mana modifiers preserved across hero recompute */
    float mana_regen_bonus; /* research/permanent mana regeneration modifiers */
    struct {
        uint16_t select[MAX_UNIT_SELECT_SOUNDS];
        uint8_t num_select;
        uint16_t yes[MAX_UNIT_SELECT_SOUNDS];   /* order confirmation ("Yes" sounds) */
        uint8_t num_yes;
        uint16_t ready[MAX_UNIT_SELECT_SOUNDS]; /* training completion ("Ready" sounds) */
        uint8_t num_ready;
        uint16_t chop[3]; uint8_t num_chop;        /* weapon-vs-wood impact variants */
        int pending;
        int owner_pending;                  /* owner-only one-shot queued for next snapshot */
        int world_pending;                  /* unfiltered world one-shot queued for next snapshot */
        uint8_t world_pending_event;
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
typedef struct edictSacrifice_s edictSacrifice_s;
typedef struct edictUnsummon_s edictUnsummon_s;
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
    uint32_t max_clients;
    uint32_t num_abilities;
    LPGAMECLIENT clients;
    struct {
        stbIniCache_t theme;
        stbIniCache_t map_skin;
        stbIniCache_t misc;
    } config;
    /* W3I gameDataSet selects a Warsmash-style Custom_V0/V1 or
     * Melee_V0/V1 sheet-data overlay for the active map. */
    char data_prefix[32];
    struct {
        float attackHalfAngle;
        float maxCollisionRadius;
        float decayTime;
        float boneDecayTime;
        float dissipateTime;
        float structureDecayTime;
        float bulletDeathTime;
        float closeEnoughRange;
        float dawnTimeGameHours;
        float duskTimeGameHours;
        float gameDayHours;
        float gameDayLength;
        float buildingAngle;
        float rootAngle;
        /* Unit-target Move/Smart follows use WC3 Misc distances, not attack
         * acquisition range. war3mapMisc.txt may override either value. */
        float followRange;
        float structureFollowRange;
        /* Combat constants are sourced from Units\MiscGame.txt (and
         * war3mapMisc.txt overrides) rather than baked into attack code. */
        float defenseArmor;
        float strAttackBonus;
        float agiDefenseBonus;
        float agiAttackSpeedBonus;
        float damageBonus[8][8];
        bool defendDeflection; /* Misc.DefendDeflection: permits Defend/Elune projectile returns */
        bool combatConstantsLoaded;
        int32_t foodCeiling;
        uint32_t upkeepUsageCount;
        uint32_t upkeepGoldTaxCount;
        uint32_t upkeepLumberTaxCount;
        float upkeepUsage[MAX_UPKEEP_TIERS];
        float upkeepGoldTax[MAX_UPKEEP_TIERS];
        float upkeepLumberTax[MAX_UPKEEP_TIERS];
    } constants;
};

struct gevent_s {
    LPEDICT subject;
    uint32_t subject_spawn_time;
    bool subject_spawn_tracked;
    EVENTTYPE type;
    LPTRIGGER trigger;
    LPGTIMER timer;
    struct jass_function const *filter;
    handle_t region;
    float range;
    uint32_t state;
    uint32_t limitop;
    float limitval;
    cstring_t variable;
    bool inuse;
    uint32_t handle_generation;
    uint8_t generation_exhausted;
};

typedef struct {
    uint32_t texture;
    BLEND_MODE blendmode;
    TEXMAP_FLAGS texmapflags;
    struct {
        BOX2 uv;
        COLOR32 color;
        uint32_t time;
    } start, end;
    bool displayed;
} CINEFILTER;

typedef struct {
    EVENT handlers[MAX_EVENTS];
    GAMEEVENT queue[MAX_EVENT_QUEUE];
    uint32_t write, read;
} LEVELEVENTS;
enum {
    WC3_FOG_STATE_MASKED = 1,  /* JASS FOG_OF_WAR_MASKED: unexplored */
    WC3_FOG_STATE_FOGGED = 2,  /* JASS FOG_OF_WAR_FOGGED: explored without current sight */
    WC3_FOG_STATE_VISIBLE = 4, /* JASS FOG_OF_WAR_VISIBLE: explored with current sight */
};
typedef struct {
    uint32_t player;
    uint32_t state;
    bool shared;
} FOGWRITE;
typedef FOGWRITE *LPFOGWRITE;
typedef FOGWRITE const *LPCFOGWRITE;
typedef struct {
    uint8_t *visible;
    uint8_t *explored;
    uint8_t *visible_rows;
    uint8_t *dirty_visible_rows;
    uint8_t *dirty_explored_rows;
#ifdef WC3_FOW_PACKED_MASK
    uint16_t *packed_visible;
    uint16_t *packed_explored;
    uint32_t packed_stride;
#endif
    bool client_connected;
} fowPlayerGrid_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    BOX2 bounds;
    uint8_t *blocked;
    uint32_t num_blocked;
    ARRAY(uint32_t, rim_cells);
    fowPlayerGrid_t players[MAX_PLAYERS];
} fowGrid_t;

#define BLIGHT_SWEEP_INTERVAL 100 // frames; resync cadence for undelivered rows; used by background sweep
#define BLIGHT_SWEEP_BYTES 512 // bytes; caps one sweep band payload; used by background resync

typedef struct {
    uint32_t width, height;
    BOX2 bounds;
    uint8_t *cells; /* mutable current Blight, one byte per 32-unit pathing cell */
    uint32_t *dirty_rows; /* one client bit per row; changed rows are sent once per client */
    uint32_t sweep_row[MAX_PLAYERS]; /* per-client background resync cursor; next row to sweep */
} blightGrid_t;

/* A fog modifier continuously applies one of the three JASS fog states while started. */
typedef struct fogmodifier_s {
    uint32_t player;
    uint32_t state;             /* WC3_FOG_STATE_* */
    bool is_rect;
    BOX2 rect;               /* used when is_rect */
    VECTOR2 center;          /* used when !is_rect */
    float radius;            /* used when !is_rect */
    bool use_shared_vision;
    bool started;
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
    uint32_t desired;
    botCaptainState_t state;
} botCaptain_t;

typedef struct {
    int32_t command, data;
} botCommand_t;

typedef struct {
    uint32_t class_id;
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
    bool stage_valid;
    ARRAY(botCommand_t, commands);
    ARRAY(LPEDICT, harvesters);
    ARRAY(botGuardPost_t, guards);
    botMode_t mode, pending_mode;
    uint32_t flags;
    int32_t replacement_count;
    bool paused, stop_requested, restart_requested;
    char script[MAX_PATHLEN], pending_script[MAX_PATHLEN];
} bot_t;

typedef struct {
    int32_t hour;
    int32_t minute;
    int32_t ticks_remaining;
    bool active;
    bool initialized;
} FALSE_TIMEOFDAY;

typedef struct {
    float elapsed;
    float pending;
    bool pending_valid;
    bool suspended;
    FALSE_TIMEOFDAY false_time;
} TIMEOFDAY;

typedef enum {
    WC3_ENV_FOG_NONE = 0,
    WC3_ENV_FOG_LINEAR,
    WC3_ENV_FOG_EXPONENTIAL_1,
    WC3_ENV_FOG_EXPONENTIAL_2,
} wc3EnvironmentFogStyle_t;

typedef struct {
    int32_t style;
    float start;
    float end;
    float density;
    VECTOR3 color;
} wc3EnvironmentFogState_t;

typedef struct {
    wc3EnvironmentFogState_t active;
    wc3EnvironmentFogState_t defaults;
    bool defaults_valid;
} wc3EnvironmentFog_t;

typedef struct {
    int32_t style;
    float start, end, density;
    VECTOR3 color;
} wc3EnvironmentFogParams_t;

struct level_locals {
    LPJASS vm;
    ggroup_t **groups;
    uint32_t num_groups;
    uint32_t group_capacity;
    uint32_t first_free_group;
    TRIGGER triggers[MAX_TRIGGERS];
    uint32_t num_triggers;
    GTIMER timers[MAX_TIMERS];
    uint32_t num_timers;
    TIMERDIALOG timer_dialogs[MAX_TIMERDIALOGS];
    LEADERBOARD leaderboards[MAX_LEADERBOARDS];
    int32_t player_leaderboards[MAX_PLAYERS]; /* registry index, -1 = none */
    uint32_t leaderboard_dirty_clients;
    MULTIBOARD multiboards[MAX_MULTIBOARDS];
    MULTIBOARDITEM multiboard_items[MAX_MULTIBOARD_ITEMS];
    TEXTTAG texttags[MAX_TEXTTAGS];
    HASHTABLE hashtables[MAX_HASHTABLES];
    REGION regions[MAX_REGIONS];
    uint32_t num_regions;
    /* Multiboard HUD presentation is deferred; dirty bits reserved for a later svc/layout path. */
    uint32_t multiboard_dirty_clients;
    uint32_t timer_dialog_dirty_clients; /* transient: clients whose timer layer must be resent */
    int32_t timer_dialog_last_index[MAX_CLIENTS]; /* transient player-number cache */
    int32_t timer_dialog_last_seconds[MAX_CLIENTS]; /* transient formatted-value cache */
    gweather_t weather_effects[MAX_WEATHER_EFFECTS];
    uint32_t next_weather_id;
    GLIGHTNING lightning_effects[MAX_LIGHTNING_EFFECTS];
    uint32_t next_lightning_id;
    bot_t bots[MAX_PLAYERS];
    LPCMAPINFO mapinfo;
    PATHSTR map_path;
    struct {
        char name[MAX_PATHLEN], description[MAX_TRIGSTR_LENGTH];
        uint32_t teams, players, game_types, game_type, map_flags;
        uint32_t placement, speed, difficulty, default_difficulty, resource_density, creature_density;
        uint32_t forced_start_locations;
        struct {
            uint32_t count;
            struct { int32_t location; uint32_t priority; } slots[MAX_START_PRIO];
        } start_prio[MAX_PLAYERS];
    } setup;
    LEVELEVENTS events;
    GAMEMESSAGES messages;
    LPEDICT ground_surfaces;
    struct {
        uint32_t item_slots, unit_slots;
    } stock;
    struct {
        uint32_t base, cursor, count;
    } waypoints;
    QUEST quests[MAX_QUESTS];
    uint16_t alliances[MAX_PLAYERS][MAX_PLAYERS];
    fowGrid_t fow;
    blightGrid_t blight;
    CINEFILTER cinefilter;
    uint32_t framenum;
    uint32_t time;
    bool script_paused;
    bool quest_paused;
    bool modal_paused;
    TIMEOFDAY timeofday;
    wc3EnvironmentFog_t environment_fog;
    BOX2 camera_bounds; /* map-global camera target rectangle; W3I default, SetCameraBounds may replace it */
    bool started;
    bool scriptsConfigured;
    bool scriptsStarted;
    bool cinematic_debug_result_window; /* per-map debug latch for result-window tracing */
    bool campaign_select_on_end; /* ForceCampaignSelectScreen defers campaign selection until EndGame */
};

#define FOR_EACH_EVENT(property) \
for (uint32_t event_index = 0; event_index < MAX_EVENTS; ++event_index) \
    for (LPEVENT property = &level.events.handlers[event_index]; property; property = NULL) \
        if (property->inuse)

#define FOR_EACH_QUEST(property) \
for (uint32_t quest_index = 0; quest_index < MAX_QUESTS; ++quest_index) \
    for (LPQUEST property = &level.quests[quest_index]; property; property = NULL) \
        if (property->inuse)

#define FOR_EACH_QUESTITEM(quest, property) \
for (uint32_t questitem_index = 0; questitem_index < MAX_QUESTITEMS; ++questitem_index) \
    for (__typeof__((quest)->items[0]) *property = &(quest)->items[questitem_index]; property; property = NULL) \
        if (property->inuse)

typedef struct {
    cstring_t id;
    size_t row_offset;
    size_t field_offset;
    bzFieldType_t type;
} unitMeta_t;

#define UITRIGGER_T_DEFINED
typedef struct {
    cstring_t name;
    void (*callback)(LPEDICT, LPCFRAMEDEF);
} uiTrigger_t;

// g_main.c
LPPLAYER G_GetPlayerByNumber(uint32_t);
void G_InitJassHost(void);
LPEDICT G_GetPlayerEntityByNumber(uint32_t);
LPGAMECLIENT G_GetPlayerClientByNumber(uint32_t);
void G_SetClientConnected(LPEDICT player, bool connected);
void G_ResetStartingResourceCheat(void);
void G_DisableStartingResourceCheatForLoadedGame(void);
void G_ApplyStartingResourceCheat(void);
bool G_PlayerInstantBuild(uint32_t player);
bool G_PlayerInstantKill(uint32_t player);
bool G_RemovePlayerWithResult(uint32_t player_num, uint32_t game_result);
bool G_GameResultDebugEnabled(void);
void G_GameResultDebug(cstring_t format, ...);
bool G_IsSinglePlayer(void);
void G_RequestEndGame(bool do_score_screen);
void G_RequestQuitGame(void);
void G_RequestChangeLevel(cstring_t map, bool do_score_screen);
void G_RequestRestartGame(bool do_score_screen);
void G_RequestLoadGameMenu(void);
void G_RequestLoadGameNamed(cstring_t name);
void G_RequestCampaignSelect(void);
void G_CampaignProgressResetRuntime(void);
bool G_CampaignProgressSetTutorialCleared(bool cleared);
bool G_CampaignProgressSetCampaignAvailable(int32_t campaign, bool available);
bool G_CampaignProgressSetMissionAvailable(int32_t campaign, int32_t mission, bool available);
void G_SetScriptPaused(bool paused);
void G_SetClientModal(LPEDICT player, uint32_t modal, bool open);
void G_SetQuestDialogOpen(LPEDICT player, bool open);
TARGTYPE G_GetTargetType(cstring_t);
uint32_t G_TargetFlagForType(TARGTYPE);
cstring_t G_LevelString(cstring_t);
cstring_t G_MapString(LPCMAPINFO info, cstring_t name);
cstring_t G_UnitName(uint32_t);
float G_Cinefade(void);
bool G_SkipCutscene(void);
VECTOR2 G_ClampCameraPosition(LPGAMECLIENT client, LPCVECTOR2 position);
VECTOR3 G_MakeServerOrigin(float x, float y, float z_offset);
void G_SetCameraBounds(float const bounds[8]);
void G_ClearCameraTarget(LPGAMECLIENT client, cstring_t func);
void G_SetPlayerText(LPGAMECLIENT, PLAYERTEXT, cstring_t);
void G_SetAllStockSlots(bool, int32_t);
void G_SetStockSlots(LPEDICT, bool, int32_t);
void G_InitStockSlots(LPEDICT);
bool G_AddItemStock(LPEDICT, uint32_t, int32_t, int32_t);
void G_RemoveItemStock(LPEDICT, uint32_t);
void G_AddItemStockAll(uint32_t, int32_t, int32_t);
void G_RemoveItemStockAll(uint32_t);
bool G_AddUnitStock(LPEDICT, uint32_t, int32_t, int32_t);
void G_RemoveUnitStock(LPEDICT, uint32_t);
void G_AddUnitStockAll(uint32_t, int32_t, int32_t);
void G_RemoveUnitStockAll(uint32_t);
GAMEEVENT *G_PublishEvent(LPEDICT, EVENTTYPE);
static inline bool G_IsDeathEvent(EVENTTYPE type) { return type == EVENT_UNIT_DEATH || type == EVENT_PLAYER_UNIT_DEATH; }
bool G_HasPendingDeathEvent(LPCEDICT);
void G_PublishEventResponse(LPEDICT, EVENTTYPE, LPEVENT);
GAMEEVENT *G_PublishEventWithSource(LPEDICT, EVENTTYPE, LPEDICT);
GAMEEVENT *G_PublishEventWithValue(LPEDICT, EVENTTYPE, LPEDICT, int32_t);
GAMEEVENT *G_PublishEventWithPoint(gameEventPointParams_t const *params);
void G_PublishSummonEvents(LPEDICT summoner, LPEDICT summoned);
void G_PublishChangeOwnerEvents(LPEDICT unit, uint32_t old_player);
bool G_SubscribeMessage(gameMsgFn, void *);
void G_UnsubscribeMessage(gameMsgFn, void *);
void G_PublishMessage(LPEDICT, GAMEMSGTYPE, LPEDICT);

// g_bot.c
bool G_BotStart(LPPLAYER, cstring_t, botMode_t);
void G_BotStop(uint32_t);
void G_BotRequestStop(uint32_t);
void G_BotShutdown(void);
void G_BotPause(uint32_t, bool);
void G_BotRunFrame(void);
bool G_BotUnitAlive(LPEDICT);
LPEDICT G_BotTown(LPPLAYER, int32_t);
LPEDICT G_BotTownMine(LPPLAYER, int32_t);
int32_t G_BotTownWithMine(LPPLAYER);
uint32_t G_BotMinesOwned(LPPLAYER);
uint32_t G_BotGoldOwned(LPPLAYER);
bool G_BotProduce(LPPLAYER, int32_t, uint32_t, int32_t);
void G_BotStopGathering(LPPLAYER);
void G_BotClearHarvest(LPPLAYER);
void G_BotHarvest(LPPLAYER, int32_t, int32_t, bool);
void G_BotCreateCaptains(LPPLAYER);
void G_BotInitAssault(LPPLAYER);
uint32_t G_BotIgnoredUnits(LPPLAYER, uint32_t);
bool G_BotCaptainInCombat(LPPLAYER, bool);
bool G_BotAddAssault(LPPLAYER, int32_t, uint32_t);
uint32_t G_BotCaptainGroupSize(LPPLAYER);
bool G_BotCaptainIsFull(LPPLAYER);
int32_t G_BotCaptainReadiness(LPPLAYER, bool);
bool G_BotAddDefenders(LPPLAYER, int32_t, uint32_t);
void G_BotAddGuardPost(LPPLAYER, uint32_t, float, float);
void G_BotFillGuardPosts(LPPLAYER);
void G_BotReturnGuardPosts(LPPLAYER);
bool G_BotPushCommand(LPPLAYER, int32_t, int32_t);
uint32_t G_BotCommandsWaiting(LPPLAYER);
int32_t G_BotLastCommand(LPPLAYER);
int32_t G_BotLastData(LPPLAYER);
void G_BotPopCommand(LPPLAYER);
void G_BotSetCaptainHome(LPPLAYER, int32_t, float, float);
void G_BotSetStagePoint(LPPLAYER, float, float);
bool G_BotSuicideUnits(LPPLAYER, int32_t, uint32_t, int32_t);
bool G_BotSuicidePlayer(LPPLAYER, uint32_t, bool);
bool G_BotMergeUnits(LPPLAYER, int32_t, uint32_t, uint32_t, uint32_t);

// g_blight.c
void G_BlightInit(void);
void G_BlightShutdown(void);
bool G_IsPointBlighted(LPCVECTOR2 point);
void G_SetBlightPoint(LPCVECTOR2 point, bool add);
void G_SetBlightRadius(LPCVECTOR2 point, float radius, bool add);
void G_SetBlightRect(LPCBOX2 rect, bool add);
void G_BlightInitializeDestructable(LPEDICT ent);
void G_BlightUpdateDestructables(LPCBOX2 region);
void G_BlightMarkDestructable(LPEDICT ent);
uint32_t G_GetBlightStateSize(void);
bool G_GetBlightState(uint8_t * out, uint32_t size);
bool G_SetBlightState(uint8_t const *data, uint32_t size);

// g_fow.c
void G_FowInit(void);
void G_FowShutdown(void);
void G_FowConnectPlayer(uint32_t player);
void G_FowUpdate(void);
void G_FowMarkBlockersDirty(void);
void G_FowSendDeltas(void);
void G_FowSendFull(LPEDICT ent);
bool G_FowPlayerCanSeeEntity(uint32_t player, LPCEDICT ent);
bool G_FowPlayerCanHoverEntity(uint32_t player, LPCEDICT ent);
bool G_FowPlayersShareVision(uint32_t viewer, uint32_t owner);
bool S_UnitIsDetectedByPlayer(LPCEDICT unit, uint32_t player);
bool S_UnitIsInvisibleToPlayer(LPCEDICT unit, uint32_t player);
bool S_UnitUsesInvisibilityRenderFlag(LPCEDICT unit);
bool S_PermanentInvisibilityActive(LPCEDICT unit);
void S_PermanentInvisibilityInitialize(LPEDICT unit);
void S_PermanentInvisibilityReveal(LPEDICT unit);
void G_FowSetStateRect(LPCFOGWRITE fog, LPCBOX2 box);
void G_FowSetStateRadius(LPCFOGWRITE fog, LPCVECTOR2 center, float radius);
void G_FogModifierStart(LPFOGMODIFIER mod);
void G_FogModifierStop(LPFOGMODIFIER mod);
uint32_t G_FowWorldToCellX(float x);
uint32_t G_FowWorldToCellY(float y);
float G_GetTimeOfDay(void);
void G_SetTimeOfDay(float value);
void G_SuspendTimeOfDay(bool suspended);
void G_SetFalseTimeOfDay(int32_t hour, int32_t minute, float duration);
bool G_IsFalseTimeOfDay(void);
void G_UpdateTimeOfDay(void);
bool G_IsNight(void);
#ifdef WC3_DEBUG_CAMERA_TRACE
void G_CameraTraceSnapshotForClient(LPGAMECLIENT, cstring_t);
void G_CameraTraceSnapshot(cstring_t);
#endif

// g_environment_fog.c
bool G_EnvironmentFogDefault(wc3EnvironmentFogState_t *fog); /* exposed: tests parse singleton DefaultZFog under both editions */
void G_EnvironmentFogInitMap(void);
void G_EnvironmentFogSet(wc3EnvironmentFogParams_t const *params);
void G_EnvironmentFogReset(void);
void G_EnvironmentFogPublish(void);

// skills/s_creep_sleep.c — JASS natural-sleep interface
bool G_UnitCanSleep(LPCEDICT);
bool G_UnitIsSleeping(LPCEDICT);
void G_UnitSetCanSleep(LPEDICT, bool);
void G_UnitWakeUp(LPEDICT);

// g_spawn.c
bool WriteGame(cstring_t filename);
bool ReadGame(cstring_t filename);
LPEDICT G_Spawn(void);
void SP_CallSpawn(LPEDICT);
void G_BindEntityData(LPEDICT);
void G_BindEntityRuntime(LPEDICT);
void G_SpawnEntities(void);
#ifdef BZ_TESTS
bool G_TestMapObjectCreatedByMapScript(uint32_t id);
#endif
bool SP_FindEmptySpaceAround(LPEDICT, uint32_t, LPVECTOR2, float *);
bool G_FindUnitUnstuckPosition(LPEDICT unit, LPCVECTOR2 requested, LPVECTOR2 out);
bool SP_FindUnitExitPosition(LPEDICT producer, LPEDICT unit, LPVECTOR2 out, float *angle);
LPEDICT SP_SpawnAtLocation(uint32_t, uint32_t, LPCVECTOR2);
LPEDICT SP_SpawnAtLocationNoBirth(uint32_t, uint32_t, LPCVECTOR2);
LPEDICT G_CreateBuildPreview(LPEDICT builder, uint32_t building_id, LPCVECTOR2 location);
void G_ClearBuildPreview(LPEDICT builder);
LPEDICT G_CreateDestructable(uint32_t class_id, float x, float y, float z, float facing, float scale, uint32_t variation);
LPEDICT G_CreateDeadDestructable(uint32_t class_id, float x, float y, float z, float facing, float scale, uint32_t variation);
bool G_IsDestructable(LPCEDICT ent);
void SP_monster_tree(LPEDICT);
void tree_stand(LPEDICT);
void tree_birth(LPEDICT);
void tree_pain(LPEDICT);

// g_save.c
bool WriteGame(cstring_t filename);
bool ReadGame(cstring_t filename);
bool G_SaveJassHandle(cstring_t type, handle_t value, uint32_t *id);
handle_t G_LoadJassHandle(cstring_t type, uint32_t id);
ggroup_t *G_AllocJassGroup(void);
bool G_EnsureJassGroupSlots(uint32_t count);
bool G_JassGroupValid(ggroup_t const *group);
bool G_JassGroupIndex(ggroup_t const *group, uint32_t *index);
ggroup_t *G_JassGroupByIndex(uint32_t index);
bool G_QuestValid(QUEST const *quest);
bool G_QuestItemValid(QUESTITEM const *item);
void G_FreeJassGroup(ggroup_t *group);
void G_ClearJassGroupRegistry(void);
void G_ClearRegionRegistry(void);
LPREGION G_RegionFromHandle(handle_t);
handle_t G_RegionHandle(uint32_t);
bool G_RegionHandleParts(handle_t, uint32_t *, uint32_t *);
LPEVENT G_EventFromHandle(handle_t);
handle_t G_EventHandle(LPEVENT);
bool G_EventHandleParts(handle_t, uint32_t *, uint32_t *);
bool G_JassGroupDebugEnabled(void);
void G_ResetJassGroupDebug(void);
void G_SetJassGroupDebugCreator(ggroup_t *group, cstring_t creator);
void G_SetJassGroupDebugContext(ggroup_t *group, cstring_t creator, cstring_t chain, int32_t trigger_ordinal);
cstring_t G_GetJassGroupDebugCreator(ggroup_t const *group);
cstring_t G_GetJassGroupDebugChain(ggroup_t const *group);
int32_t G_GetJassGroupDebugTrigger(ggroup_t const *group);
void G_DumpJassGroupDebug(cstring_t failing_creator, cstring_t failing_chain, int32_t failing_trigger);
LPGWEATHER G_WeatherAdd(LPCBOX2 bounds, uint32_t effect_id, bool enabled);
void G_WeatherEnable(LPGWEATHER effect, bool enabled);
void G_WeatherRemove(LPGWEATHER effect);
void G_WeatherInitMap(void);
uint32_t G_WriteClientDatagram(LPEDICT ent, uint8_t * data, uint32_t size);
void G_BlightMarkClientFull(LPEDICT ent);
bool G_BlightDatagramPending(LPEDICT ent);
uint32_t G_BlightWriteDatagram(LPEDICT ent, uint8_t * data, uint32_t size);
LPTRIGGER G_AllocJassTrigger(void);
LPGTIMER G_AllocJassTimer(void);
LPTIMERDIALOG G_AllocTimerDialog(LPGTIMER timer);
void G_FreeTimerDialog(LPTIMERDIALOG dialog);
void G_SetTimerDialogVisible(LPTIMERDIALOG dialog, LPPLAYER player, bool visible);
bool G_IsTimerDialogVisible(LPCTIMERDIALOG dialog, LPCPLAYER player);
void G_MarkTimerDialogDirty(LPCTIMERDIALOG dialog);
void G_UpdateTimerDialogs(void);
void G_FormatTimerDialogValue(LPCGTIMER timer, string_t out, size_t out_size);
LPLEADERBOARD G_AllocLeaderboard(void);
void G_FreeLeaderboard(LPLEADERBOARD board);
void G_MarkLeaderboardDirty(LPCLEADERBOARD board);
void G_SetLeaderboardDisplayed(LPLEADERBOARD board, LPPLAYER player, bool displayed);
bool G_IsLeaderboardDisplayed(LPCLEADERBOARD board, LPCPLAYER player);
void G_UpdateLeaderboards(void);
LPLEADERBOARD G_PlayerLeaderboard(uint32_t player);
void G_SetPlayerLeaderboard(uint32_t player, LPLEADERBOARD board);
LPMULTIBOARD G_AllocMultiboard(void);
void G_FreeMultiboard(LPMULTIBOARD board);
void G_SetMultiboardDisplayed(LPMULTIBOARD board, LPPLAYER player, bool displayed);
bool G_IsMultiboardDisplayed(LPCMULTIBOARD board, LPCPLAYER player);
void G_SetMultiboardMinimized(LPMULTIBOARD board, LPPLAYER player, bool minimized);
bool G_IsMultiboardMinimized(LPCMULTIBOARD board, LPCPLAYER player);
void G_MarkMultiboardDirty(LPCMULTIBOARD board);
void G_MultiboardSetRowCount(LPMULTIBOARD board, int32_t count);
void G_MultiboardSetColumnCount(LPMULTIBOARD board, int32_t count);
struct gmultiboardcell_s *G_MultiboardCell(LPMULTIBOARD board, int32_t row, int32_t col);
LPMULTIBOARDITEM G_MultiboardGetItem(LPMULTIBOARD board, int32_t row, int32_t col);
void G_MultiboardReleaseItem(LPMULTIBOARDITEM item);
LPMULTIBOARD G_MultiboardItemBoard(LPCMULTIBOARDITEM item);
LPTEXTTAG G_AllocTextTag(void);
void G_FreeTextTag(LPTEXTTAG tag);
void G_SetTextTagVisible(LPTEXTTAG tag, LPPLAYER player, bool visible);
bool G_IsTextTagVisible(LPCTEXTTAG tag, LPCPLAYER player);
LPHASHTABLE G_AllocHashtable(void);
void G_FreeHashtable(LPHASHTABLE table);
void G_ClearHashtableRegistry(void);
bool G_HashtableIndex(LPCHASHTABLE table, uint32_t *index);
bool G_HashtableReserve(LPHASHTABLE table, uint32_t need);
void G_ClearSaveRegistries(void);
bool G_GetSaveMap(cstring_t filename, string_t map, uint32_t map_size);
void G_HeroSaveLoadAuditFrame(void);
void G_FormatHeroSaveSnap(LPCEDICT hero, string_t out, uint32_t out_size);
void G_RunTimers(void);
void G_StartProjectilePresentation(LPEDICT ent);
void G_TimerStart(LPGTIMER timer, uint32_t timeout, bool periodic, struct jass_function const *handler);
void G_TimerPause(LPGTIMER timer);
void G_TimerResume(LPGTIMER timer);
void G_TimerDestroy(LPGTIMER timer);
bool G_TimerCoroutineValid(handle_t timer, uint32_t generation);
uint32_t G_TimerRemaining(LPCGTIMER timer);

LPEDICT Waypoint_add(LPCVECTOR2);
void G_InitWaypoints(void);
void M_CheckGround (LPEDICT);
void G_RegisterGroundSurface(LPEDICT);
void G_UnregisterGroundSurface(LPEDICT);
void G_ClearGroundSurfaces(void);
void monster_start(LPEDICT);
void monster_think(LPEDICT);

// g_model.c
void         G_NormalizeModelFilename(cstring_t authored, string_t out, size_t out_size);
int          G_RegisterModel(cstring_t filename);
LPCANIMATION G_GetAnimation(uint32_t modelindex, cstring_t animname);
LPCANIMATION G_SelectAnimationForProperties(LPCANIMATION animations, uint32_t count, cstring_t animname, cstring_t properties);
LPCANIMATION G_SelectAnimationVariantForProperties(LPCANIMATION animations, uint32_t count, cstring_t animname, cstring_t properties, bool randomize);
LPCANIMATION G_GetAnimationForProperties(uint32_t modelindex, cstring_t animname, cstring_t properties);
LPCANIMATION G_GetAnimationVariant(uint32_t modelindex, cstring_t animname, bool randomize);
bool         G_AnimationHasPrimary(LPCANIMATION animation, cstring_t primary);
LPCANIMATION G_GetUnitAnimation(LPEDICT unit, cstring_t animname);
void         G_SetUnitAnimation(LPEDICT unit, cstring_t animname);
void         G_ResetUnitAnimationProperties(LPEDICT unit);
void         G_AddUnitAnimationProperties(LPEDICT unit, cstring_t properties, bool add);
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
bool unit_affectingcombat(LPEDICT);
void unit_updatestatuses(LPEDICT);
void unit_expirestatus(LPEDICT, heroabilitystatus_t *);
heroabilitystatus_t *unit_findstatus(LPEDICT, uint32_t);
void unit_statusdeath(LPEDICT);
void incinerate_explode_think(LPEDICT);
void monsoon_think(LPEDICT);
void unit_refreshstatusflags(LPEDICT);

// skills/s_move.c — locomotion shared by Move, Follow, Attack, Build and Harvest
void unit_moveindirection(LPEDICT);
void unit_moveindirection_ignore_units(LPEDICT);
bool unit_snap_to_point_ignore_units(LPEDICT, LPCVECTOR2);
void unit_changeangle(LPEDICT);
void unit_changeangle_worker(LPEDICT);
void unit_changeangle_interaction_ignore_units(LPEDICT);
bool unit_changeangle_towards_point_ignore_units(LPEDICT, LPCVECTOR2);
void unit_changeangle_towards_point(LPEDICT, LPCVECTOR2);
void unit_changeangle_towards_point_worker(LPEDICT, LPCVECTOR2);
void unit_changeangle_for_radius(LPEDICT, float);
void unit_changeangle_for_radius_worker(LPEDICT, float);
bool M_MoveIsValid(LPEDICT self, LPCVECTOR2 pos);
bool M_CheckAttack(LPEDICT);
bool unit_is_walking(LPCEDICT);
void unit_setanimation(LPEDICT, cstring_t);
void unit_setmove(LPEDICT, umove_t *);
void M_MoveFrame(LPEDICT);
float M_DistanceToGoal(LPEDICT);
float unit_movedistance(LPEDICT);
uint32_t M_RefreshHeatmap(LPEDICT, float);
uint32_t M_RefreshHeatmapForMover(LPCEDICT, LPEDICT, float);
uint8_t M_UnitStaticPathingFlags(LPCEDICT);
bool M_IsDead(LPCEDICT);
void SP_SpawnUnit(LPEDICT);
uint32_t unit_spawn_aiflags(uint32_t);
bool SP_TrainUnit(LPEDICT, uint32_t);
bool player_pay(LPPLAYER, uint32_t);

// g_food.c
bool G_FoodLimitsEnabled(void);
int32_t G_GetEffectiveFoodCap(LPGAMECLIENT client);
uint32_t G_GetPlayerUpkeepTier(LPGAMECLIENT client);
int32_t G_GetUpkeepGoldRateForTier(uint32_t tier);
int32_t G_GetUpkeepLumberRateForTier(uint32_t tier);
bool G_PlayerHasFoodFor(LPGAMECLIENT client, int32_t food_cost);
bool G_ReserveTrainingFood(LPEDICT unit);
void G_SetUnitFoodUsed(LPEDICT unit, int32_t amount);
void G_SetUnitFoodMade(LPEDICT unit, int32_t amount);
void G_ActivateUnitFood(LPEDICT unit);
void G_ClearUnitFood(LPEDICT unit);
void G_ClearTrainingQueueFood(LPEDICT producer);
bool G_CancelTrainingQueueItem(LPEDICT producer, uint32_t index, bool refund);
void G_CancelTrainingQueue(LPEDICT producer, bool refund);
bool G_QueueSacrifice(LPEDICT producer, LPEDICT worker, uint32_t result_id);
void G_SetUnitPlayer(LPEDICT unit, uint32_t player);
uint32_t G_GetUnitTeamColor(LPCEDICT unit);
void G_SetEntityTeamColor(LPENTITYSTATE state, uint32_t color);
void G_SetUnitTeamColor(LPEDICT unit, uint32_t color);
void G_InheritUnitTeamColor(LPEDICT entity, LPCEDICT source);
void G_InitializeUnitTeamColor(LPEDICT unit);
void G_InitializeUnitVertexColor(LPEDICT unit);
void G_ApplyMapUnitTeamColor(LPEDICT unit, LPCDOODAD placement);
void G_ChangePlayerTeamColor(LPPLAYER player, uint32_t previous_color, uint32_t new_color);
bool G_GetUnitColorOverride(LPCEDICT unit, uint32_t * color);
void G_SetUnitColorOverride(LPEDICT unit, uint32_t color);
void G_ClearUnitColorOverride(LPEDICT unit);
void G_RecomputePlayerUpkeep(LPGAMECLIENT client);
int32_t G_ApplyResourceIncome(LPPLAYER player, uint32_t resource_state, int32_t gross_amount);
int32_t G_CreditResourceIncome(LPPLAYER player, LPEDICT source, uint32_t resource_state, int32_t gross_amount);
bool G_UnitCanReviveHeroes(LPCEDICT altar);
bool G_HeroCanBeRevivedAt(LPCEDICT altar, LPCEDICT hero);

// skills/s_rally.c
bool G_UnitHasRally(LPCEDICT producer);
void G_ResetRallyTarget(LPEDICT producer);
bool G_SetRallyPoint(LPEDICT producer, LPCVECTOR2 point);
bool G_SetRallyEntity(LPEDICT producer, LPEDICT target);
rallyTargetType_t G_ResolveRallyTarget(LPEDICT producer, LPVECTOR2 point, LPEDICT *target);
bool G_ApplyRallyOrder(LPEDICT producer, LPEDICT produced);
void G_InvalidateRallyTarget(LPEDICT target);
void G_UpdateRallyIndicator(LPGAMECLIENT client);

uint32_t G_HeroReviveGoldCost(LPCEDICT hero);
uint32_t G_HeroReviveLumberCost(LPCEDICT hero);
float G_HeroReviveTime(LPCEDICT hero);
bool G_QueueHeroRevive(LPEDICT altar, LPEDICT hero);
bool G_CancelHeroRevive(LPEDICT altar, LPEDICT hero);
void G_CancelHeroRevives(LPEDICT altar);
uint8_t compress_stat(EDICTSTAT const *);
uint32_t G_LoadShadowTexture(cstring_t, bool);

// g_pathing.c
pathTex_t *LoadTGA(uint8_t const*, size_t);
pathTex_t *M_LoadPathTex(cstring_t filename);

// g_move.c
bool SV_CloseEnough(LPEDICT, LPCEDICT, float);

// g_phys.c
void G_RunEntity(LPEDICT);
void G_SetHealth(LPEDICT, float);
void G_AddHealth(LPEDICT, float);
void G_ApplyPermanentMaxHealthBonus(LPEDICT, float);
void G_ApplyTemporaryMaxHealthBonus(LPEDICT, float);
void G_ApplyTemporaryMaxManaBonus(LPEDICT, float);
void G_ApplyPermanentArmorBonus(LPEDICT, float);
void G_ApplyTemporaryArmorBonus(LPEDICT, float);
void G_ApplyPermanentAttackDamageBonus(LPEDICT, float);
void G_ApplyTemporaryAttackDamageBonus(LPEDICT, float);
void S_EnableAbility(LPEDICT, uint32_t);
void S_DisableAbility(LPEDICT, uint32_t);
void S_RefreshAbilityLevel(LPEDICT, ability_t const *);
bool S_UnitPolymorphed(LPCEDICT unit);
BZ_ABILITY_PROC(CAbilityOnFireHuman);
void G_ApplyUnitAbilityTraits(LPEDICT);
void G_SolveCollisions(void);
bool M_CheckCollision(LPCVECTOR2, float);
void G_PushEntity(LPEDICT ent, float distance, LPCVECTOR2 direction);
void G_PushEntity3(LPEDICT ent, float distance, LPCVECTOR3 direction);
bool G_ClosestStaticPathablePointInRectForRadiusFlags(LPCVECTOR2 location, LPCBOX2 bounds,
                                                      float radius, uint8_t blocked_flags, LPVECTOR2 out);

// g_abilities.c
void S_RunAbilityUpdates(LPEDICT);
bool S_UnitAbilityEvent(LPEDICT, abilityMsg_t);
bool S_UnitAbilityOrderAccepted(LPEDICT, cstring_t);
bool S_UnitQueuedOrderEvent(LPEDICT, unitOrder_t const *, abilityMsg_t);
bool S_UnitTargetAbilityOrder(LPEDICT, LPEDICT, cstring_t);
bool S_UnitProjectileHit(LPEDICT);
ability_t const *FindAbilityByOrder(cstring_t);
ability_t const *FindAbilityByClassname(cstring_t);
ability_t const *FindAbilityForCommand(cstring_t);
abilityitem_t S_AbilityItem(uint32_t code);
bool S_AbilityHasCommand(ability_t const *ability);
void S_AbilityCommand(LPEDICT clent, ability_t const *ability);
ability_t const *GetAbilityByIndex(uint32_t);
uint32_t FindAbilityIndex(cstring_t);
void InitAbilities(void);
#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void);
#endif
bool G_UnitAutocastIsOn(LPEDICT ent, uint32_t code);
bool G_SetUnitAutocast(LPEDICT ent, uint32_t code, bool enabled);
bool G_TryUnitAutocast(LPEDICT ent);

// g_metadata.c
cstring_t FindConfigValue(cstring_t, cstring_t);
cstring_t GetClassName(uint32_t);

// g_effects.c
cstring_t G_AbilityEffectArt(uint32_t ability_id, wc3EffectType_t type, uint32_t index);
LPEDICT G_SpawnModelEffect(cstring_t model, LPCVECTOR2 point, LPEDICT target, cstring_t attach_point, bool temporary);
LPEDICT G_SpawnAbilityEffectAtPoint(uint32_t ability_id, wc3EffectType_t type, uint32_t index, LPCVECTOR2 point, bool temporary);
LPEDICT G_SpawnAbilityEffectTarget(uint32_t ability_id, wc3EffectType_t type, uint32_t index, LPEDICT target, cstring_t attach_point, bool temporary);
void G_DestroyEffect(LPEDICT effect);
uint32_t G_AbilityLightningId(uint32_t ability_id, uint32_t index);
LPGLIGHTNING G_LightningAdd(LPCLIGHTNINGADDPARAMS params);
bool G_LightningValid(LPCGLIGHTNING effect);
void G_LightningAttach(LPGLIGHTNING effect, LPCEDICT source, LPCEDICT target);
void G_LightningUpdateAttached(LPGLIGHTNING effect);
void G_LightningMove(LPGLIGHTNING effect, LPCVECTOR3 source, LPCVECTOR3 target);
void G_LightningColor(LPGLIGHTNING effect, COLOR32 color);
void G_LightningScriptColor(LPGLIGHTNING effect, COLOR32 color, float const * precise);
void G_LightningRemove(LPGLIGHTNING effect);
LPGLIGHTNING G_SpawnAbilityLightning(LPCABILITYLIGHTNINGPARAMS params);
LPEDICT G_SpawnOwnedAbilityEffectAtPoint(LPEDICT owner, uint32_t ability_id, wc3EffectType_t type, uint32_t index, LPCVECTOR2 point);
void G_DestroyOwnedEffects(LPEDICT owner);
void G_EffectThink(LPEDICT);
void G_EffectValidateTarget(LPEDICT);

// hud/hud_resource_text.c
void G_ResourceGainEvent(LPEDICT source, uint32_t resource_state, int32_t amount);

// hud/hud_unit.c
uint8_t G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, uint8_t max_buttons);
bool G_BuildCommandButton(LPEDICT ent, cstring_t code, bool research, uint32_t level, gameCommandButton_t *button);
bool G_BuildAllEnabled(void);
bool G_WorkerCanBuild(LPEDICT worker, uint32_t building_id);
bool G_ProducerCanTrain(LPEDICT producer, uint32_t unit_id);
bool G_ProducerCanResearch(LPEDICT producer, uint32_t upgrade_id);
bool G_ProducerCanUpgrade(LPEDICT producer, uint32_t unit_id);
bool G_BuildingUpgradeActive(LPCEDICT building);
bool G_BuildingIsUnsummoning(LPCEDICT building);
void G_GetBuildingUpgradeCosts(buildingUpgradeCostParams_t const *params);
buildCommandState_t G_GetBuildCommandState(LPGAMECLIENT client, LPEDICT worker, uint32_t building_id, string_t reason, uint32_t reason_size);
buildCommandState_t G_GetTrainCommandState(LPGAMECLIENT client, LPEDICT producer, uint32_t unit_id, string_t reason, uint32_t reason_size);
buildCommandState_t G_GetResearchCommandState(LPGAMECLIENT client, LPEDICT producer, uint32_t upgrade_id, int32_t *next_level, string_t reason, uint32_t reason_size);
buildCommandState_t G_GetBuildingUpgradeCommandState(buildingUpgradeCommandParams_t const *params);
int32_t G_UpgradeGoldCost(uint32_t upgrade_id, int32_t level_value);
int32_t G_UpgradeLumberCost(uint32_t upgrade_id, int32_t level_value);
float G_UpgradeResearchTime(uint32_t upgrade_id, int32_t level_value);
bool G_QueueResearch(LPEDICT producer, uint32_t upgrade_id);
bool G_StartBuildingUpgrade(LPEDICT building, uint32_t unit_id);
bool G_CancelBuildingUpgrade(LPEDICT building);
void G_StopBuildingUpgrade(LPEDICT building, bool refund);
void G_RunBuildingUpgradeFrame(LPEDICT building);
void G_UpdateBuildingUpgradeAnimation(LPEDICT building);
void G_ApplyPlayerUpgradesToUnit(LPEDICT unit);
bool G_UnitAbilityResearchAvailable(LPCEDICT unit, uint32_t ability_id);
uint32_t G_GetUnitUpgradeForClass(LPCEDICT unit, cstring_t wanted_class);
bool G_ChargeBuilding(LPGAMECLIENT client, uint32_t building_id);
void G_RefundBuilding(LPGAMECLIENT client, uint32_t building_id);
void G_SnapBuildingPoint(uint32_t building_id, LPVECTOR2 point);
void G_GetBuildPlacementPathingFlags(uint32_t building_id, uint8_t * prevented, uint8_t * required);
buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, uint32_t building_id, LPCVECTOR2 requested, LPVECTOR2 snapped);
bool G_DisplaceBuildOccupants(LPEDICT builder, LPEDICT building);
bool G_ExecuteBuildOrder(LPEDICT builder, uint32_t building_id, LPCVECTOR2 location);
bool G_IssueBuildOrder(LPEDICT builder, uint32_t building_id, LPCVECTOR2 location);
bool G_IssueUnitBuildOrder(LPEDICT builder, uint32_t building_id, LPCVECTOR2 location, bool queue, uint32_t issuer_player);
bool G_FindBuildOnTarget(uint32_t building_id, LPCVECTOR2 point, LPEDICT *out);
float G_BuildApproachDistance(uint32_t building_id);
bool G_StartHumanConstruction(LPEDICT builder, LPEDICT building);
bool G_StartOrcConstruction(LPEDICT builder, LPEDICT building);
bool G_StartUndeadConstruction(LPEDICT builder, LPEDICT building);
bool G_StartNightElfConstruction(LPEDICT builder, LPEDICT building);
bool G_StartNightElfOverlayConstruction(LPEDICT building);
void G_RunConstructionFrame(LPEDICT building);
void G_UpdateConstructionAnimation(LPEDICT building);
void G_StopConstruction(LPEDICT building);
bool G_CancelStructureConstruction(LPEDICT building);
void G_CompleteConstruction(LPEDICT building);
bool G_UnitHasHumanRepair(LPEDICT ent);
bool S_OrderRepair(LPEDICT ent, LPEDICT target, uint32_t preferred);
bool S_SetRepairAutocast(LPEDICT ent, bool enabled);
bool S_RepairSmart(LPEDICT ent, LPEDICT target);
void S_CancelRepair(LPEDICT ent);
void G_SetPlayerTechMaxAllowed(LPGAMECLIENT client, uint32_t techid, int32_t maximum);
int32_t G_GetPlayerTechMaxAllowed(LPGAMECLIENT client, uint32_t techid);
void G_SetPlayerTechResearched(LPGAMECLIENT client, uint32_t techid, int32_t level_value);
void G_AddPlayerTechResearched(LPGAMECLIENT client, uint32_t techid, int32_t levels);
int32_t G_GetPlayerTechResearchedLevel(LPGAMECLIENT client, uint32_t techid);
float G_UnitUpgradeEffectBonus(LPCEDICT unit, uint32_t effect);
#define ID_UPGRADE_EFFECT_MAX_MANA MAKEFOURCC('r', 'm', 'n', 'x')
int32_t G_GetPlayerTechInProgress(LPGAMECLIENT client, uint32_t techid);
void G_AddPlayerTechInProgress(LPGAMECLIENT client, uint32_t techid, int32_t levels);
int32_t G_GetPlayerTechCountValue(LPGAMECLIENT client, uint32_t techid);
void G_InvalidateCommands(LPGAMECLIENT client);
bool G_BuildInventoryItem(LPEDICT ent, LPEDICT item, uint8_t slot, gameInventoryItem_t *out);
uint8_t G_GetInventory(LPEDICT ent, gameInventoryItem_t *items, uint8_t max_items);
uint8_t G_GetBuildQueue(LPEDICT ent, gameQueueItem_t *queue, uint8_t max_queue);

// g_ai.c
LPEDICT G_GetMainSelectedUnit(LPGAMECLIENT);
void Get_Commands_f(LPEDICT);
void CMD_CancelCommand(LPEDICT ent);
bool G_ClearBuildPlacementMode(LPEDICT clent);
bool G_CancelBuildPlacement(LPEDICT clent);
bool build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);
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
bool G_UnitIsIdleWorker(LPCEDICT ent);
bool G_UnitShowsIdleWorkerShortcut(LPGAMECLIENT client, LPCEDICT ent);
bool G_UnitShowsHeroShortcut(LPGAMECLIENT client, LPCEDICT ent);
LPEDICT G_GetNextIdleWorker(LPGAMECLIENT client, uint32_t after);
void G_InvalidateUnitShortcuts(LPGAMECLIENT client);
void G_InvalidateAllUnitShortcuts(void);
void G_InvalidateUnitShortcutsForUnit(LPEDICT ent);
void G_AlertHeroShortcutDamage(LPEDICT ent);
void G_ActivateHeroButton(LPEDICT clent, uint32_t number);
void G_ActivateHeroKey(LPEDICT clent, uint32_t slot);
void G_ActivateIdleWorkerShortcut(LPEDICT clent, uint32_t hinted_number);
void G_UpdateClientUnitShortcuts(void);
void UI_WriteUnitShortcutLayer(LPEDICT ent);
void UI_AddCancelButton(LPEDICT);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_AddCommandButton(cstring_t);
void UI_AddCommandButtonExtended(cstring_t code, bool research, uint32_t level);
void UI_WriteTooltipFrame(void);
void UI_SetCurrentClient(LPGAMECLIENT client);
void UI_ShowInterface(LPEDICT, bool, float);
void UI_ShowText(LPEDICT, LPCVECTOR2, cstring_t, float);
void UI_ShowTransientText(LPEDICT, LPCVECTOR2, cstring_t, float);
void UI_RecordTransmissionMessage(LPEDICT);
void UI_ClearTextMessages(LPEDICT);
void UI_InvalidateDialoguePresentation(LPEDICT);
void UI_WriteDialoguePresentation(LPEDICT);
cstring_t GetBuildCommand(unitRace_t);
void UI_RenderRoute(LPEDICT, cstring_t);
void UI_ShowMainMenu(LPEDICT);
void UI_ShowGameMenuEndGame(LPEDICT);
void UI_ShowGameMenuConfirmExit(LPEDICT);
void UI_ShowGameMenuSave(LPEDICT);
void UI_ShowGameMenuLoad(LPEDICT);
void UI_ShowRealmSelect(LPEDICT, bool);
void UI_ShowSinglePlayerMenu(LPEDICT);
void UI_ShowMultiplayerMenu(LPEDICT);
void UI_ShowMultiplayerCreateMenu(LPEDICT);
void UI_ShowMultiplayerGameSetupMenu(LPEDICT, uint32_t);
void UI_ShowGameInterface(LPEDICT);
void UI_WriteHoverLayout(LPEDICT);
void UI_WriteCinematicLayer(LPEDICT);
void UI_ShowMapSelectMenu(LPEDICT, cstring_t);
void UI_ShowMultiplayerCreateMapInfo(LPEDICT);
void UI_ClearCreateGameSlots(void);
void UI_AddCreateGameSlot(uint32_t, cstring_t, cstring_t, cstring_t, uint32_t);

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
void UI_ParseFDF(cstring_t);
void UI_ParseFDF_Buffer(cstring_t, string_t);
void UI_SetAllPoints(LPFRAMEDEF);
void UI_SetParent(LPFRAMEDEF, LPCFRAMEDEF);
void UI_SetText(LPFRAMEDEF, cstring_t, ...);
void UI_SetOnClick(LPFRAMEDEF, cstring_t, ...);
void UI_SetTextPointer(LPFRAMEDEF, cstring_t);
void UI_SetSize(LPFRAMEDEF, float, float);
void UI_SetTexture(LPFRAMEDEF, cstring_t, bool);
void UI_SetTexture2(LPFRAMEDEF, cstring_t, bool);
#ifdef BZ_TESTS
void UI_TestResetInfoPanelIconCache(void);
cstring_t UI_TestResolveTypedInfoPanelIcon(cstring_t prefix, cstring_t type, bool has_upgrade);
uint16_t UI_TestSelectedTimedStatusStat(LPGAMECLIENT client, LPEDICT selected);
#endif
void UI_WriteLayout(LPEDICT, LPCFRAMEDEF, uint32_t);
void UI_WriteStart(uint32_t);
void UI_ClearLayer(LPEDICT, uint32_t);
void UI_ShowGameResult(LPEDICT, uint32_t);
void UI_FlushPendingGameResults(void);
void UI_HideGameResult(LPEDICT);
void UI_ShowQuests(LPEDICT);
void UI_HideQuests(LPEDICT);
void UI_ShowAllies(LPEDICT);
void UI_AlliesToggle(LPEDICT, uint32_t, PLAYERALLIANCE);
void UI_AlliesToggleVictory(LPEDICT);
void UI_AlliesAccept(LPEDICT);
void UI_AlliesCancel(LPEDICT);
void UI_ShowLog(LPEDICT);
void UI_WriteWithTriggers(LPEDICT, LPCFRAMEDEF, uint32_t, uiTrigger_t const *);
void UI_SetPoint(LPFRAMEDEF, UIFRAMEPOINT, LPCFRAMEDEF, UIFRAMEPOINT, float, float);
void UI_InitFrame(LPFRAMEDEF, FRAMETYPE);
void UI_SetHidden(LPFRAMEDEF, bool);
void UI_InheritFrom(LPFRAMEDEF, cstring_t);
uint32_t UI_FindFrameNumber(cstring_t);
uint32_t UI_LoadTexture(cstring_t, bool);
cstring_t UI_GetString(cstring_t);
LPFRAMEDEF UI_Spawn(FRAMETYPE, LPFRAMEDEF);
LPFRAMEDEF UI_FindFrame(cstring_t);
LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF, cstring_t);
LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF, cstring_t);
LPFRAMEDEF UI_FindChildFrameType(LPFRAMEDEF, FRAMETYPE);

cstring_t Theme_String(cstring_t, cstring_t);
cstring_t Theme_PlayerString(LPGAMECLIENT, cstring_t, cstring_t);
float Theme_Float(cstring_t, cstring_t);

// ui_write.c
void UI_WriteFrame(LPCFRAMEDEF);
void UI_WriteFrameValue(LPCFRAMEDEF, float);
uint32_t UI_GetWrittenFrameNumber(LPCFRAMEDEF);
void UI_WriteFrameWithChildren(LPCFRAMEDEF, LPCFRAMEDEF);
void UI_WriteFrameWithChildrenWithTriggers(LPEDICT, LPCFRAMEDEF, LPCFRAMEDEF, uiTrigger_t const *);
bool UI_BuildFrameForWrite(LPCFRAMEDEF frame,
                           LPUIFRAME out,
                           uint8_t * typedata,
                           uint32_t typedata_max,
                           string_t textbuf,
                           uint32_t textbuf_max);

// g_metadata.c
cstring_t UnitMetaString(LPEDICT, uint32_t);
int32_t UnitMetaInteger(LPEDICT, uint32_t);
bool UnitMetaBoolean(LPEDICT, uint32_t);
float UnitMetaReal(LPEDICT, uint32_t);

void InitUnitData(void);
void ShutdownUnitData(void);
void G_SetMapUnitOverrides(LPCMAPINFO);
void G_SetMapAbilityOverrides(LPCMAPINFO);
bool G_IsReignOfChaosMap(LPCMAPINFO);
uint32_t G_MapGameDataSet(LPCMAPINFO);
void G_MapGameDataPrefix(wc3MapGameDataPrefixParams_t const *params);
#ifdef BZ_TESTS
typedef struct { cstring_t text; void *rows; uint32_t count; } slkTestData_t;
bool G_SLKStoreOptional(cstring_t);
slkTestData_t *G_SetSLKRows(cstring_t, slkTestData_t *);
slkTestData_t *G_SetProfileRows(slkTestData_t *);
#endif
void G_RegisterSelectSounds(LPEDICT, cstring_t);
void G_RegisterGlobalSounds(void);  /* register world sounds (tree fall, etc.) at map init */
void G_ResetSoundPresentationState(void);
soundPolicy_t const *G_SoundIndexPolicy(int index);
void G_PlaySound(LPCVECTOR3 origin, LPEDICT ent, int channel, int index, float volume, float attenuation, float timeofs);
float G_SoundIndexVolume(int);
uint32_t G_SoundIndexDuration(int);
int G_UISoundIndex(cstring_t);
void G_PlayUISoundForPlayer(LPEDICT, cstring_t);
int G_AbilityEffectSoundIndex(uint32_t ability_id, bool looped);
void G_PlayAbilityEffectSound(uint32_t ability_id, LPCVECTOR2 point);
uint32_t G_UnitAckSoundVariantCount(cstring_t label, cstring_t suffix);
int G_UnitAckSoundVariantIndex(cstring_t label, cstring_t suffix, uint32_t variant);
uint32_t G_UnitCombatSoundVariantCount(cstring_t key);
int G_UnitCombatSoundVariantIndex(cstring_t key, uint32_t variant);
bool G_SoundLabelDescriptor(cstring_t alias, string_t path, size_t path_size, int *sound_index, float *volume);
void G_PlayCombatImpactSound(LPEDICT attacker, LPEDICT target);
void G_SetConstructionLoopSound(LPEDICT building, bool active);

typedef struct {
    float volume;
    VECTOR3 origin;
    LPEDICT emitter;
    bool positioned;
} jassSoundPlayback_t;


/* Client-owned background music presentation.  The game resolves Warcraft
 * skin/Music.SLK data per recipient and emits reliable svc_music commands. */
void G_MusicResetState(void);
void G_MusicSyncClient(LPGAMECLIENT client);
void G_MusicSetMap(cstring_t music_name, bool random, int32_t index);
void G_MusicClearMap(void);
void G_MusicPlay(cstring_t music_name, int32_t start_ms, int32_t fade_ms);
void G_MusicStop(bool fade_out);
void G_MusicResume(void);
void G_MusicPlayThematic(cstring_t music_name, int32_t start_ms);
bool G_MusicAcceptFinished(LPGAMECLIENT client, uint32_t session_id);
void G_MusicTrackSelected(LPGAMECLIENT client, uint32_t session_id, int32_t index, int32_t position_ms, uint32_t played_mask);
void G_MusicThematicSnapshot(LPGAMECLIENT client, uint32_t thematic_session_id, uint32_t restore_session_id,
                             int32_t index, int32_t position_ms, uint32_t played_mask);
void G_MusicMapTransitionFinished(LPGAMECLIENT client);
void G_MusicExplicitFinished(LPGAMECLIENT client);
void G_MusicThematicFinished(LPGAMECLIENT client);
void G_MusicEndThematic(void);
void G_MusicSetVolume(int32_t volume);
void G_MusicSetPosition(int32_t millisecs);
void G_MusicSetThematicVolume(int32_t volume);
void G_MusicSetThematicPosition(int32_t millisecs);
int32_t G_AudioDurationFromMemory(cstring_t filename, uint8_t const *data, uint32_t size);
int32_t G_SoundFileDuration(cstring_t filename);
void G_JassSoundRuntimeInit(handle_t sound);
void G_JassSoundSetVolume(handle_t sound, float volume);
void G_JassSoundSetPosition(handle_t sound, LPCVECTOR3 position);
void G_JassSoundAttach(handle_t sound, LPEDICT unit);
void G_JassSoundPlayback(handle_t sound, jassSoundPlayback_t *playback);
void G_SendPointConfirmation(LPEDICT, LPCVECTOR2, bool attack);
void G_QueueReadySound(LPEDICT);
void G_QueueOwnerSoundAlias(LPEDICT, cstring_t);
void G_QueueOwnerUISound(LPEDICT, cstring_t);
void G_SendMinimapPing(LPGAMECLIENT, LPCVECTOR2, float, COLOR32, uint32_t);
void G_SendOwnerMinimapAlert(LPEDICT);
COLOR32 G_SmartTargetIndicatorColor(uint32_t, LPCEDICT);
void G_SendWidgetIndicator(LPEDICT, COLOR32, LPPLAYER);
void G_ShowCommandErrorKey(LPEDICT, cstring_t, cstring_t);
void G_ShowCommandErrorText(LPEDICT, cstring_t);
extern int g_treeFallSounds[3];     /* Sound\Destructibles\TreeFall{1,2,3}.wav configstring indices */
extern uint8_t g_numTreeFallSounds;

// g_command.c
int32_t G_CompareSelectionOrder(LPCEDICT, LPCEDICT);
uint32_t G_GetOrderedSelectedUnits(LPGAMECLIENT, LPEDICT *, uint32_t);
void G_SelectEntity(LPGAMECLIENT, LPEDICT);
void G_DeselectEntity(LPGAMECLIENT, LPEDICT);
bool G_IsEntitySelected(LPGAMECLIENT, LPEDICT);
bool G_FocusSelectedUnit(LPGAMECLIENT, LPEDICT);
bool G_CycleSelectionSubgroup(LPGAMECLIENT);
void G_ResetSelectionFocus(LPGAMECLIENT);
bool G_UnitCanBeSelected(LPGAMECLIENT, LPCEDICT);
bool G_UnitCanControl(LPGAMECLIENT, LPCEDICT);
selectionRelation_t G_SelectionRelation(uint32_t viewer, LPCEDICT ent);
LPEDICT G_GetMainControllableUnit(LPGAMECLIENT);
void G_UpdateClientSelections(void);
void G_SyncClientSelection(LPGAMECLIENT);
void G_ResetSelectionSoundState(void);
void G_ClearUnitResponses(LPCEDICT);
uint32_t G_UnitResponseRequest(LPCEDICT, int);
void G_AcceptSoundVariant(int, uint32_t);
bool G_SoundVariantIsLast(int, uint32_t);
bool G_QueueUnitResponseSound(LPEDICT, int);
bool G_UnitResponseTalking(LPCEDICT);
void G_UpdateUnitResponsePresentation(void);
void G_QueueSelectionSound(LPEDICT, bool);
void G_QueueAttackOrderSound(LPEDICT);
void G_ClientCommand(LPEDICT, uint32_t, cstring_t[]);
bool G_CheatsEnabled(void);
void G_ClientSetCameraPosition(LPEDICT, LPCVECTOR2);

//  s_skills.c
float AB_Data(cstring_t, uint32_t, uint32_t);
uint32_t GetAbilityIndex(abilityProc_t);
void G_ResetHeroPassiveCaches(void);

// g_combat.c
int G_AttackDamage(LPEDICT, LPEDICT, int);
int G_AttackDamageWithType(LPEDICT, LPEDICT, int, uint32_t);
void T_Damage(LPEDICT, LPEDICT, int);

// g_utils.c
void G_FreeEdict(LPEDICT);
void G_DeferFreeEdict(LPEDICT);
bool G_IsDeferredFree(LPCEDICT);
void G_RunDeferredFrees(void);
void G_ResetDeferredFrees(void);
LPEVENT G_MakeEvent(EVENTTYPE);
void G_SetEventSubject(LPEVENT, LPEDICT);
void G_SetPlayerEventSubject(LPEVENT, LPEDICT);
bool G_EventSubjectIsCurrent(LPEVENT);
void G_UnitPositionChanged(LPEDICT, LPCVECTOR2);
void G_JassVariableChanged(cstring_t, float, float);
bool G_LimitMatches(uint32_t, float, float);
LPQUEST G_MakeQuest(void);
bool G_RegionContains(LPCREGION, LPCVECTOR2);
void G_RemoveQuest(LPQUEST);
void G_InitPlayerAlliances(LPCMAPINFO);
void G_SetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE, bool);
bool G_GetPlayerAlliance(LPCPLAYER, LPCPLAYER, PLAYERALLIANCE);
bool G_PlayerTreatsPlayerAsAlly(uint32_t, uint32_t);

// m_unit.c
bool unit_issueorder(LPEDICT, cstring_t, LPCVECTOR2);
bool unit_issueimmediateorder(LPEDICT, cstring_t);
bool unit_issuetargetorder(LPEDICT, cstring_t, LPEDICT);
bool G_TransformUnitType(LPEDICT, uint32_t);
bool G_IssueUnitPointOrder(LPEDICT, cstring_t, LPCVECTOR2, bool, uint32_t, float);
bool G_IssueUnitTargetOrder(LPEDICT, cstring_t, LPEDICT, bool, uint32_t);
bool G_QueueUnitOrder(LPEDICT, cstring_t, unitOrderTargetType_t, LPCVECTOR2, LPEDICT, uint32_t, float, uint32_t);
bool G_UnitHasActiveOrder(LPCEDICT);
void G_PublishIssuedPointOrder(LPEDICT, uint32_t, LPCVECTOR2, uint32_t, cstring_t);
void G_PublishIssuedImmediateOrder(LPEDICT, uint32_t, uint32_t, cstring_t);
uint32_t G_GetIssuedOrderId(LPCEDICT);
bool G_GetIssuedOrderPoint(LPCEDICT, LPVECTOR2);
uint32_t G_OrderId(cstring_t);
cstring_t G_OrderId2String(uint32_t);
bool G_UnitStartNextQueuedOrder(LPEDICT);
void G_ClearUnitOrderQueue(LPEDICT);
uint32_t G_UnitQueuedOrderCount(LPCEDICT);
void unit_birth(LPEDICT);
void unit_die(LPEDICT, LPEDICT);
void unit_begin_decay(LPEDICT);
void G_RestartCorpseBoneDecayAfterCargo(LPEDICT);
LPEDICT unit_create(uint32_t, uint32_t, LPCVECTOR2, float);
LPEDICT unit_createorfind(uint32_t, uint32_t, LPCVECTOR2, float);
bool unit_additemtoslot(LPEDICT, LPEDICT, uint32_t);
bool unit_additem(LPEDICT, LPEDICT);
void unit_addstatus(LPEDICT, cstring_t, uint32_t);
void unit_addtimedstatus(LPEDICT, cstring_t, uint32_t, float);
uint32_t G_UnitStatusLevel(LPCEDICT, uint32_t);
bool unit_statusshowstimedbar(uint32_t);
float unit_statusremainingfraction(heroabilitystatus_t const *);
heroabilitystatus_t const *unit_findtimedbarstatus(LPCEDICT);
void unit_learnability(LPEDICT, uint32_t);
uint32_t G_UnitAbilityLevel(LPCEDICT ent, uint32_t abilcode);
uint32_t G_UnitSetAbilityLevel(LPEDICT ent, uint32_t abilcode, int32_t level);
void G_SetPlayerAbilityAvailable(LPGAMECLIENT client, uint32_t abilid, bool avail);
bool G_IsPlayerAbilityAvailable(LPCGAMECLIENT client, uint32_t abilid);
cstring_t G_ObjectName(uint32_t objectId);
extern LPEDICT eventsolditem;
extern LPEDICT eventsoldunit;
bool G_HeroHasCandidateSkill(LPCEDICT ent, uint32_t abilcode);
void G_HeroInitializeProgression(LPEDICT ent);
uint32_t G_HeroSkillRequiredLevel(LPEDICT ent, uint32_t abilcode);
heroSkillState_t G_HeroSkillState(LPEDICT ent, uint32_t abilcode, uint32_t *next_level, uint32_t *required_level);
bool G_HeroLearnSkill(LPEDICT ent, uint32_t abilcode);
bool G_HeroModifySkillPoints(LPEDICT ent, int32_t delta);

void G_GameCacheInit(gameCache_t *cache, cstring_t campaign);
bool G_GameCacheSave(gameCache_t *cache);
void G_GameCacheFlush(gameCache_t *cache);
void G_GameCacheFlushMission(gameCache_t *cache, cstring_t mission);
void G_GameCacheFlushEntry(gameCache_t *cache, cstring_t mission, cstring_t key, gameCacheValueType_t type);
bool G_GameCacheStoreInteger(gameCache_t *cache, cstring_t mission, cstring_t key, int32_t value);
bool G_GameCacheStoreReal(gameCache_t *cache, cstring_t mission, cstring_t key, float value);
bool G_GameCacheStoreBoolean(gameCache_t *cache, cstring_t mission, cstring_t key, bool value);
bool G_GameCacheStoreString(gameCache_t *cache, cstring_t mission, cstring_t key, cstring_t value);
bool G_GameCacheStoreUnit(gameCache_t *cache, cstring_t mission, cstring_t key, LPCEDICT unit);
bool G_GameCacheHave(gameCache_t const *cache, cstring_t mission, cstring_t key, gameCacheValueType_t type);
int32_t G_GameCacheGetInteger(gameCache_t const *cache, cstring_t mission, cstring_t key);
float G_GameCacheGetReal(gameCache_t const *cache, cstring_t mission, cstring_t key);
bool G_GameCacheGetBoolean(gameCache_t const *cache, cstring_t mission, cstring_t key);
cstring_t G_GameCacheGetString(gameCache_t const *cache, cstring_t mission, cstring_t key);
LPEDICT G_GameCacheRestoreUnit(gameCache_t const *cache, cstring_t mission, cstring_t key,
                              uint32_t player, LPCVECTOR2 location, float facing);

void G_RecomputeHeroStats(LPEDICT);
uint32_t G_MaxHeroLevel(void);
uint32_t G_HeroXPForLevel(uint32_t level);
uint32_t G_HeroLevelForXP(uint32_t xp);
void G_HeroApplyLevel(LPEDICT, uint32_t level);
void G_HeroSetXP(LPEDICT, uint32_t xp);
void G_GrantKillXP(LPEDICT victim, LPEDICT killer);
void G_ReviveHero(LPEDICT, float x, float y);
bool G_UnitIsRaisableCorpse(LPCEDICT);
bool G_UnitIsRaisableStoredCorpse(LPCEDICT);
void G_ReviveCorpse(LPEDICT, float life_fraction);
bool G_UnitIsHero(LPCEDICT ent);
float G_UnitArmorValue(LPCEDICT ent);
bool S_SpellCooldownReady(LPEDICT caster, uint32_t code);
float S_SpellCooldownRemaining(LPEDICT caster, uint32_t code);
float S_SpellCooldownLength(LPEDICT caster, uint32_t code);
bool S_SpellCooldownWindow(LPEDICT caster, uint32_t code, abilityCooldownWindow_t *window);
float S_SpellCooldownFraction(LPEDICT caster, uint32_t code, uint32_t level);
void S_SpellStartCooldownDuration(LPEDICT caster, uint32_t code, float seconds);
void S_SpellStartCooldown(LPEDICT caster, uint32_t code, uint32_t level);
void S_SpellEndCooldown(LPEDICT caster, uint32_t code);
void S_SpellResetCooldowns(LPEDICT caster);
cstring_t S_SpellString(uint32_t code, cstring_t field, uint32_t level);

void order_attack(LPEDICT, LPEDICT);
bool S_OrderAttack(LPEDICT self, LPEDICT target);
bool S_AttackCanTarget(LPCEDICT attacker, LPCEDICT target);
bool S_UnitAttackSlotEnabled(LPCEDICT attacker, uint32_t slot);
bool S_AttackCanAutoAcquire(LPCEDICT attacker, LPCEDICT target);
void order_move(LPEDICT, LPEDICT);
bool move_is_active_order_walk(LPCEDICT);
void move_start_displacement(LPEDICT, LPCVECTOR2);
void move_cancel_displacement(LPEDICT);
bool move_displacement_active(LPCEDICT);
bool move_displacement_reached(LPEDICT);
void order_stop(LPEDICT);
void order_attackmove(LPEDICT, LPEDICT);
void order_patrol(LPEDICT, LPEDICT);
void order_patrol_resume(LPEDICT);
void order_follow(LPEDICT, LPEDICT);
void order_follow_resume(LPEDICT);
extern umove_t holdpos_move_stand;
extern umove_t holdpos_move_stand_ready;
void unit_stand(LPEDICT);
bool G_ActorHasSkill(LPCEDICT, cstring_t);
bool G_ActorAddSkill(LPEDICT, uint32_t);
bool G_ActorRemoveSkill(LPEDICT, uint32_t);
bool G_ActorSetSkillPermanent(LPEDICT, uint32_t, bool);
bool G_ActorSkillPermanent(LPEDICT, uint32_t);
void G_FreeActorSkills(LPEDICT);
bool S_GoldMineIsMine(LPCEDICT);
bool S_GoldMineIsOverlay(LPCEDICT);
bool S_UnitTypeIsGoldMine(uint32_t);
bool S_UnitTypeReturnsGold(uint32_t);
uint32_t S_GoldMineMaximumGold(LPCEDICT);
float S_GoldMineMiningDuration(LPCEDICT);
uint32_t S_GoldMineCapacity(LPCEDICT);
bool S_GoldMineCanHarvest(LPCEDICT);
bool S_GoldMineWorkerIsInside(LPCEDICT);
bool S_MilitiaTargetOrder(LPEDICT, cstring_t, LPEDICT);
void S_CancelMilitiaPairing(LPEDICT);
void S_MilitiaExpire(LPEDICT);
bool S_StatusIsEnsnare(uint32_t);
void S_GoldMineInitUnit(LPEDICT);
void S_GoldMineReleaseWorker(LPEDICT);
bool S_MineOverlayBind(LPEDICT, LPEDICT);
void S_MineOverlayBindPreplaced(void);
void S_MineOverlayRelease(LPEDICT);
LPEDICT S_CreateBlightedGoldmine(uint32_t, LPCVECTOR2, float);
void S_GoldMineSetResourceAmount(LPEDICT, uint32_t);
bool S_AcolyteHarvestOrder(LPEDICT, LPEDICT);
void S_AcolyteHarvestRelease(LPEDICT);
bool S_AcolyteHarvestIsActive(LPCEDICT);
void S_EntangledMineTick(LPEDICT);
bool S_HarvestCanLumber(LPCEDICT);
bool S_HarvestCanGold(LPCEDICT);
void harvest_start(LPEDICT, LPEDICT);
void harvest_gold_start(LPEDICT, LPEDICT);
bool harvest_gold_order(LPEDICT, LPEDICT);
bool harvest_auto_start_gold(LPEDICT);
bool harvest_auto_start_lumber(LPEDICT);
bool harvest_lumber_return_to(LPEDICT, LPEDICT);
bool harvest_gold_return_to(LPEDICT, LPEDICT);
void cargo_drop_all(LPEDICT);
void S_CargoInitUnit(LPEDICT);
bool S_CargoTryLoad(LPEDICT, LPEDICT);
bool S_CorpseCargoTryLoad(LPEDICT, LPEDICT);
bool S_CargoOrderBoard(LPEDICT, LPEDICT);
bool S_CargoAttacksEnabled(LPCEDICT);
LPEDICT S_CargoTransportForUnit(LPCEDICT);
void S_CargoReleaseUnit(LPEDICT);
bool S_CargoIsBurrow(LPEDICT);
bool S_CargoIsCorpseHolder(LPEDICT);
bool S_CorpseCargoIsStored(LPCEDICT);
bool S_CorpseCargoPosition(LPCEDICT, LPVECTOR2);
uint32_t S_CargoCapacity(LPEDICT);
LPEDICT S_CargoUnitAt(LPCEDICT, uint32_t);
bool S_CargoUnloadAt(LPEDICT, uint32_t);
bool S_CargoBeginUnloadAll(LPEDICT);
void S_CargoStandDown(LPEDICT);
bool S_WaygateIsGate(LPCEDICT);
bool S_WaygateIsActive(LPCEDICT);
bool S_WaygateGetDestination(LPCEDICT, LPVECTOR2);
void S_WaygateSetDestination(LPEDICT, LPCVECTOR2);
void S_WaygateSetActive(LPEDICT, bool);
void blight_mine_think(LPEDICT);
void blizzard_think(LPEDICT);
void flame_strike_tick(LPEDICT);
void siphon_mana_think(LPEDICT);
void rain_of_fire_think(LPEDICT);
void starfall_think(LPEDICT);
void death_and_decay_think(LPEDICT);
void tranquility_think(LPEDICT);
void earthquake_think(LPEDICT);
void far_sight_think(LPEDICT);
void chain_lightning_think(LPEDICT);
void unsummon_think(LPEDICT);
void whirlwind_think(LPEDICT);
void volcano_think(LPEDICT);
void pocket_factory_think(LPEDICT);
void stasis_trap_think(LPEDICT);
void rain_of_chaos_think(LPEDICT);
void inferno_think(LPEDICT);
void mass_teleport_think(LPEDICT);
void divine_shield_think(LPEDICT);
void dark_portal_think(LPEDICT);
void exhume_think(LPEDICT);
void graveyard_think(LPEDICT);
void corpse_cargo_approach_think(LPEDICT);
void cannibalize_approach_think(LPEDICT);
void healing_spray_think(LPEDICT);
void cannibalize_think(LPEDICT);
void possession_two_think(LPEDICT);
void lsh_think(LPEDICT);
bool move_selectlocation(LPEDICT, LPCVECTOR2);
bool move_should_arrive(LPEDICT, float);
bool move_is_blocked(LPEDICT, float, float);
bool move_is_settled_near_goal(LPEDICT, float, float);
bool move_is_terminal_hold(LPCEDICT);
void move_reset_progress(LPEDICT);
LPEDICT G_FindNearestEnemy(LPEDICT, float);
float G_AcquisitionRange(LPCEDICT);
float G_FollowStopRange(LPCEDICT follower, LPCEDICT target);
bool G_ShouldAcquireThisFrame(LPCEDICT);

// p_jass.c
LPJASS jass_newstate(void);
void jass_close(LPJASS);
bool jass_dofile(LPJASS, cstring_t);
bool jass_dofilenative(LPJASS, cstring_t);
void jass_callbyname(LPJASS, cstring_t, bool);
cstring_t jass_functionname(struct jass_function const *);
void jass_executetrigger(LPJASS, LPTRIGGER, LPEDICT);
bool jass_dobuffer(LPJASS, string_t);
void jass_runevents(LPJASS);

// g_events.c
void G_RunEntities(void);
void G_RunEvents(void);
void G_DrainPausedResultEvents(void);

// g_items.c
void SP_SpawnItem(LPEDICT);
bool G_IsItem(LPCEDICT item);
uint32_t G_InventoryCapacity(LPCEDICT unit);
bool G_InventoryCanUseItems(LPCEDICT unit);
bool G_InventoryCanGetItems(LPCEDICT unit);
bool G_InventoryCanDropItems(LPCEDICT unit);
void G_DropInventoryOnDeath(LPEDICT unit);
bool G_UnitHasInventory(LPEDICT unit);
uint32_t G_ItemCharges(LPCEDICT item);
void G_SetItemCharges(LPEDICT item, uint32_t charges);
void G_ConsumeItemCharge(LPEDICT item);
cstring_t G_ItemAbilityList(LPCEDICT item);
int32_t G_FindFreeInventorySlot(LPCEDICT unit);
bool G_CanPickupItem(LPEDICT unit, LPEDICT item);
bool G_AddItemToSlot(LPEDICT unit, LPEDICT item, uint32_t slot);
bool G_AddItemToSlotInternal(LPEDICT unit, LPEDICT item, uint32_t slot, bool publish_event);
bool G_PickupItem(LPEDICT unit, LPEDICT item);
bool G_OrderPickupItem(LPEDICT unit, LPEDICT item);
bool G_DropItemAt(LPEDICT unit, uint32_t slot, LPCVECTOR2 position);
bool G_DropItem(LPEDICT unit, uint32_t slot);
bool G_OrderDropItemAt(LPEDICT unit, LPEDICT item, LPCVECTOR2 position);
void G_RemoveItem(LPEDICT item);
void G_UseItem(LPEDICT unit, uint32_t slot);
uint32_t G_ItemTypeFromClass(cstring_t cls);

// g_stock.c / neutral shops
bool G_IsItemShop(LPCEDICT shop);
bool G_IsUnitShop(LPCEDICT shop);
bool G_CanUseItemShop(LPGAMECLIENT client, LPCEDICT shop);
bool G_CanUseUnitShop(LPGAMECLIENT client, LPCEDICT shop);
float G_ShopActivationRadius(LPCEDICT shop);
LPEDICT G_FindShopPatron(LPGAMECLIENT client, LPEDICT shop);
LPEDICT G_FindUnitShopPatron(LPGAMECLIENT client, LPEDICT shop);
uint8_t G_GetShopItemButtons(shopItemButtonsParams_t *params);
uint8_t G_GetShopUnitButtons(shopItemButtonsParams_t *params);
uint8_t G_GetShopButtons(shopItemButtonsParams_t *params);
bool G_ShopSellsItem(LPEDICT shop, uint32_t item_id);
bool G_ShopSellsUnit(LPEDICT shop, uint32_t unit_id);
bool G_ShopPurchaseItem(LPEDICT clent, LPEDICT shop, uint32_t item_id);
bool G_ShopPurchaseUnit(LPEDICT clent, LPEDICT shop, uint32_t unit_id);
bool G_ShopPawnItem(shopPawnItemParams_t *params);

// g_destructable.c
void G_SetDestructableScriptBinding(bool enabled);
void G_ActivateScriptedDestructable(LPEDICT ent,
                                    float x,
                                    float y,
                                    float z,
                                    float facing,
                                    float scale,
                                    uint32_t variation);
bool G_IsDestructable(LPCEDICT ent);
bool G_DestructableIsAttackable(LPCEDICT ent);
bool G_DestructableIsWalkable(LPCEDICT ent);
bool G_DestructableCanBeAttackedBy(LPCEDICT attacker, LPCEDICT target);
bool G_DestructableAcceptsSmartAttack(LPCEDICT attacker, LPCEDICT target);
void G_InitializeDestructablePlacement(LPEDICT ent, LPCDOODAD placement);
bool G_DestructableApplyDamage(LPEDICT ent, LPEDICT attacker, float damage);
bool G_KillDestructable(LPEDICT ent, LPEDICT killer);
bool G_SetDestructableDeadState(LPEDICT ent, bool process_death);
bool G_RemoveDestructable(LPEDICT ent);
bool G_SetDestructableLife(LPEDICT ent, float life);
bool G_RestoreDestructable(LPEDICT ent, float life, bool birth);
uint32_t G_SelectDropItem(droppableItem_t const *entries, uint32_t count, uint32_t roll);
uint32_t G_SelectRandomTableItem(mapRandomItem_t const *entries, uint32_t count, uint32_t roll);
mapRandomItemTable_t const *G_FindRandomItemTable(uint32_t table_number);
void G_SpawnDestructableLoot(LPEDICT ent);
void G_DestructableStartDeathAnimation(LPEDICT ent);
void G_DestructableStartAliveAnimation(LPEDICT ent, bool birth);

bool G_IsDoodad(LPCEDICT ent);
void G_DoodadAnimationEnd(LPEDICT ent);
bool G_DoodadSetAnimation(LPEDICT ent, cstring_t anim_name, bool random_animation);
typedef struct {
    float x, y, radius;
    uint32_t doodad_id;
    bool nearest_only, random_animation;
    cstring_t anim_name;
} doodadAnimationRadiusParams_t;
uint32_t G_SetDoodadAnimationRadius(doodadAnimationRadiusParams_t const *params);
uint32_t G_SetDoodadAnimationRect(LPCBOX2 rect, uint32_t doodad_id,
                               cstring_t anim_name, bool random_animation);
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
static inline uint32_t G_Time(void) { return level.time; }

extern unitMeta_t const UnitsMetaData[];

#endif
