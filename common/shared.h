#ifndef shared_h
#define shared_h

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "../shared/shared.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
static inline char const *bz_strcasestr(char const *haystack, char const *needle) {
    size_t needle_len = strlen(needle);
    if (!needle_len) return haystack;
    for (; *haystack; haystack++)
        if (!strncasecmp(haystack, needle, needle_len)) return haystack;
    return NULL;
}
#define strcasestr bz_strcasestr
#endif

/* Windows and older Linux C libraries lack BSD strlcpy/strlcat. */
static inline size_t bz_strlcpy(char *destination, const char *source, size_t size) {
    size_t const source_length = strlen(source);
    if (size > 0) {
        size_t const copy_length = source_length < size - 1 ? source_length : size - 1;
        memcpy(destination, source, copy_length);
        destination[copy_length] = '\0';
    }
    return source_length;
}

static inline size_t bz_strlcat(char *destination, const char *source, size_t size) {
    size_t destination_length = 0, source_length = strlen(source);
    while (destination_length < size && destination[destination_length]) destination_length++;
    if (destination_length == size) return size + source_length;
    size_t const available = size - destination_length - 1;
    size_t const copy_length = source_length < available ? source_length : available;
    memcpy(destination + destination_length, source, copy_length);
    destination[destination_length + copy_length] = '\0';
    return destination_length + source_length;
}

#if defined(_WIN32) || defined(__linux__)
#define strlcpy bz_strlcpy
#define strlcat bz_strlcat
#endif

#define MAX_PATHLEN 256
#define MAX_PLAYERS 16
#define MAX_SELECTED_ENTITIES 64
#define TOKEN_LEN 1024
#define FRAMETIME 100
#define MAX_LAYOUT_OBJECTS 1024
#define MAX_LIST_FETCH_TEXT 2048
#define MAX_LIST_FETCH_ROWS 32
#define MIN(x, y) (((x)<(y))?(x):(y))
#define MAX(x, y) (((x)>(y))?(x):(y))
#define BZ_CLAMP_U8(x) ((uint8_t)MIN(255, MAX(0, (int)(x)))) // clamp a numeric expression to a 0..255 byte

#define BYTE2FLOAT(x) ((x)/255.f)

#define IS_FOURCC(STRING) (STRING && strlen(STRING) == 4)

#define MAKE(TYPE,...)(TYPE){__VA_ARGS__}

#define COLOR32_WHITE MAKE(COLOR32,255,255,255,255)
#define COLOR32_BLACK MAKE(COLOR32,0,0,0,255)

/* Descriptor grammars share conversion contracts; source readers still own byte/text decoding. */
typedef enum {
        BZ_FIELD_U32,
        BZ_FIELD_FLOAT,
        BZ_FIELD_BOOL,
        BZ_FIELD_CSTR,
        BZ_FIELD_CHAR_ARRAY,
        BZ_FIELD_VEC3,
        BZ_FIELD_COLOR32_ARGB,
        BZ_FIELD_COLOR32_RGBA,
        BZ_FIELD_FOURCC,  /* 4-char text → uint32_t via memcpy (same as MAKEFOURCC on LE) */
} bzFieldType_t;

#define KNOWN_AS(STRUCT, TYPE) \
typedef struct STRUCT TYPE; \
typedef struct STRUCT *LP##TYPE; \
typedef struct STRUCT const *LPC##TYPE;

#define FOR_LOOP(property, max) \
for (uint32_t property = 0, end = max; property < end; ++property)

#define PrintTag(tag) do { (void)(tag); } while(false)

#define FOR_EACH_LIST(type, property, list) \
for (type *property = list, *next = list ? (list)->next : NULL; \
property; \
property = next, next = next ? next->next : NULL)

#define ADD_TO_LIST(VAR, LIST) VAR->next = LIST; LIST = VAR;

#define FOR_EACH(type, property, array, num) \
for (type *property = array; property - array < num; property++)

/* ARRAY(type, name): declare a pointer and its element count as one unit.
   Read the count with ARRAY_COUNT(name), iterate with FOR_EACH_ARRAY
   (or FOR_LOOP(i, ARRAY_COUNT(name)) when the index is needed), and test for
   emptiness with IS_ARRAY_EMPTY(name) — never touch name##_count directly. */
#define ARRAY(type, name) type *name; uint32_t name##_count
#define ARRAY_COUNT(name) (name##_count)
#define IS_ARRAY_EMPTY(name) (!(name) || !ARRAY_COUNT(name))
#define FOR_EACH_ARRAY(type, property, name) \
for (type *property = name; property - name < ARRAY_COUNT(name); property++)

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) ((int)(char)(ch0) | ((int)(char)(ch1) << 8) | ((int)(char)(ch2) << 16) | ((int)(char)(ch3) << 24))
#endif

#define FOFS(type, x) (handle_t)&(((struct type *)NULL)->x)

#define SAFE_DELETE(x, func) if (x) { func(x); (x) = NULL; }

#define DEG2RAD(ANGLE) ((ANGLE) / 180.0 * M_PI)
#define RAD2DEG(ANGLE) ((ANGLE) / M_PI * 180.0)

#define SET_FLAG(VAR, FLAG, VALUE) if (VALUE) { VAR |= FLAG; } else { VAR &= ~FLAG; }

#define PUSH_BACK(TYPE, VAR, LIST) \
if (LIST) { \
    TYPE *last##TYPE = LIST; \
    while (last##TYPE->next) last##TYPE = last##TYPE->next; \
    last##TYPE->next = VAR; \
} else { \
    LIST = VAR; \
}

#define REMOVE_FROM_LIST(TYPE, VAR, LIST, DELETER) \
TYPE **prev = &LIST; \
FOR_EACH_LIST(TYPE, it, LIST) { \
    if (it == VAR) { \
        *prev = it->next; \
        DELETER(it); \
        break; \
    } \
    prev = &it->next; \
}

#define DELETE_LIST(TYPE, LIST, DELETER) \
for (TYPE *it = LIST; it;) { \
    TYPE *next = it->next; \
    DELETER(it); \
    it = next; \
}

#define PARSE_LIST(LIST, ITEM, PARSEFUNC) \
PARSER parser = { .buffer = LIST, .delimiters = "" }; \
for (cstring_t ITEM = PARSEFUNC(&parser); ITEM; ITEM = PARSEFUNC(&parser))

#define TRACE_CALL(FUNC, ...) FUNC(__VA_ARGS__)
#define TRACE(FUNC, ...) \
do { \
    fprintf(stderr, "%s: %s\n", __func__, #FUNC); \
    TRACE_CALL(FUNC, ##__VA_ARGS__); \
} while (0)

#ifdef DIAG_OUTPUT
#define DIAGF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DIAGF(...) ((void)0)
#endif


#define FLAG(NAME, X) NAME = (1 << X)

enum {
    FLAG(RF_SELECTED, 0),
    FLAG(RF_HAS_LUMBER, 1),
    FLAG(RF_HAS_GOLD, 2),
    FLAG(RF_HIDDEN, 3),
    FLAG(RF_NO_UBERSPLAT, 4),
    FLAG(RF_NO_FOGOFWAR, 5),
    FLAG(RF_NO_SHADOW, 6),
    FLAG(RF_ATTACH_OVERHEAD, 7),
    FLAG(RF_NO_LIGHTING, 8),
    FLAG(RF_GROUND_ANCHOR, 9),
    FLAG(RF_FOW_BLOCKER, 10),
    FLAG(RF_PORTRAIT_LIGHTING, 11),
    FLAG(RF_FOW_REVEALER, 12),
    FLAG(RF_HOSTILE, 13),      /* hostile relationship presentation */
    FLAG(RF_HOVERED, 14),
    FLAG(RF_ORTHO_CAMERA, 15), /* HUD model: use orthographic camera (console chrome) */
    FLAG(RF_GROUND_EFFECT, 15),
    FLAG(RF_MOUNTED, 16),      /* riding a mount; overhead name resolves to the mounted attachment */
    FLAG(RF_HAS_QUEST, 17),    /* show overhead "?" sprite */
    FLAG(RF_QUEST_COMPLETE, 18), /* tint "?" sprite yellow */
    FLAG(RF_NOT_SELECTABLE, 19), /* render normally but exclude from world hit/box selection */
    FLAG(RF_NEUTRAL, 20),        /* neutral/passive relationship presentation */
    FLAG(RF_BUILDING, 21),       /* WC3 structure; enables building-only presentation */
    FLAG(RF_GROUND_CONFORM, 22), /* presentation: conform entity Z to authored model ground surfaces */
    FLAG(RF_GROUND_SURFACE, 23), /* presentation: model may provide an authored walkable support surface */
};

enum {
    FLAG(EF_GROUND_ANCHOR, 0),
    FLAG(EF_FOW_BLOCKER, 1),
    FLAG(EF_FOW_REVEALER, 2),
    FLAG(EF_MOUNTED, 3),        /* WoW: entity is riding a mount */
    FLAG(EF_HAS_QUEST, 4),      /* entity has a quest in progress — show "?" sprite */
    FLAG(EF_QUEST_COMPLETE, 5), /* quest is ready to turn in — tint "?" yellow */
    FLAG(EF_HOSTILE, 6),        /* hostile relationship to this snapshot recipient */
    FLAG(EF_NOT_SELECTABLE, 7), /* render entity, but exclude it from world/box selection */
    /* TODO: deliver the authoritative/localized unit name through a dedicated hover-UI
     * response or game command; do not widen entityState_t for this presentation data. */
    FLAG(EF_HOVER_HEALTH, 8),   /* client may expose this entity's health on world hover */
    FLAG(EF_NEUTRAL, 9),        /* neutral/passive relationship to this snapshot recipient */
    FLAG(EF_BUILDING, 10),      /* WC3 structure presentation metadata */
    FLAG(EF_GROUND_CONFORM, 11), /* presentation: conform entity Z to authored model ground surfaces */
    FLAG(EF_GROUND_SURFACE, 12), /* presentation: entity model provides an authored support surface */
    FLAG(EF_RESOURCE_SOURCE, 13), /* resource source presentation metadata */
    FLAG(EF_RESOURCE_RETURN, 14), /* resource-return destination presentation metadata */
    FLAG(EF_HOVER_MANA, 15),      /* client may expose this entity's mana on world hover */
};

enum {
    EFX_MODEL = 1 << 0,
    EFX_SPLAT = 1 << 1,
    EFX_ATTACH_SLOTS = 1 << 2,
    EFX_SLOT_FIRST = 1 << 8,
    EFX_SLOT_SECOND = 1 << 9,
    EFX_SLOT_THIRD = 1 << 10,
    EFX_SLOT_FOURTH = 1 << 11,
    EFX_SLOT_FIFTH = 1 << 12,
};
/* Bits 3-7 carry an optional WC3 player-color override as ordinal + 1.
 * Zero means use entityState_t.player, preserving ownership separately. */
#define EFX_TEAM_COLOR_MASK 0x00f8
#define EFX_TEAM_COLOR_SHIFT 3
#define EFX_SLOT_MASK 0x1f00 // bits 8-12; authored attachment slots used by model effects
#define EFX_SLOT_SHIFT 8 // bits; low bit of the packed attachment-slot mask; used by the renderer
/* Bits 13-15 are an opaque game-owned presentation variant. Shared/client
 * code transports this value without assigning game semantics; the selected
 * game module and renderer must agree on the 0..7 interpretation. */
#define EFX_GAME_VARIANT_MASK 0xe000
#define EFX_GAME_VARIANT_SHIFT 13
#define EFX_GAME_VARIANT_GET(flags) (((flags) & EFX_GAME_VARIANT_MASK) >> EFX_GAME_VARIANT_SHIFT)
#define EFX_GAME_VARIANT_SET(flags, variant) \
    (((flags) & ~EFX_GAME_VARIANT_MASK) | ((((uint16_t)(variant)) << EFX_GAME_VARIANT_SHIFT) & EFX_GAME_VARIANT_MASK))

enum {
    FLAG(RDF_NOFOG, 0),
    FLAG(RDF_NOFOGMASK, 1),
    FLAG(RDF_NOWORLDMODEL, 2),
    FLAG(RDF_NOFRUSTUMCULL, 3),
    FLAG(RDF_NOPARTICLES, 4),
    FLAG(RDF_USE_ENTITY_CAMERA, 5),
};

#define MAX_COMMANDS 12
#define MAX_STATS 32

#define MAX_GAME_ENTITIES 16000
#define MAX_PACKET_ENTITIES 1024 // per-frame packet snapshot budget
#define MAX_CLIENTS 24
#define MAX_MODELS 512 // campaign maps can reference more than 255 distinct models
#define MAX_FONTSTYLES 256
#define MAX_SOUNDS 1024 // campaign maps can reference more than 512 unit and ambient sounds
#define MAX_IMAGES 2048 // UI-heavy games can reference hundreds of distinct command/status textures in one map session
#define MAX_DYNAMIC_IMAGES 32
#define MAX_ITEMS 256
#define MAX_GENERAL (MAX_CLIENTS*2)

enum {
    CS_NAME = 0,
    CS_CDTRACK = 1,
    CS_SKY = 2,
    CS_STATUSBAR = 5,        // display program string
    CS_WORLD = 7,
    CS_MINIMAP = 8,            // alert-ping model path
    CS_TERRAIN_LIGHT_MODEL = 9, // decimal CS_MODELS index; optional world/terrain environment light model
    CS_ENTITY_LIGHT_MODEL = 10, // decimal CS_MODELS index; optional entity environment light model
    CS_ORDER_MARKER = 11, // model path; server-authored point-order confirmation
    CS_ASSET_SCOPE = 12, // archive/directory scope for map-owned media, available before world registration
    /* Generic scene-distance fog: "style start end density r g b".  Style 0 disables;
     * positive styles currently share the renderer's linear start/end path. */
    CS_SCENE_FOG = 13,
    CS_MAXCLIENTS = 30,
    CS_MAPCHECKSUM = 31,        // for catching cheater maps
    CS_MODELS = 32,
    CS_SOUNDS = (CS_MODELS+MAX_MODELS),
    CS_IMAGES = (CS_SOUNDS+MAX_SOUNDS),
    CS_FONTS = (CS_IMAGES+MAX_IMAGES),
    CS_ITEMS = (CS_FONTS+MAX_FONTSTYLES),
    CS_PLAYERSKINS = (CS_ITEMS+MAX_ITEMS),
    CS_GENERAL = (CS_PLAYERSKINS+MAX_CLIENTS),
    MAX_CONFIGSTRINGS = (CS_GENERAL+MAX_GENERAL),
};

#define ID_MDLX MAKEFOURCC('M','D','L','X')
#define ID_43DM MAKEFOURCC('4','3','D','M')
#define ID_MD20 MAKEFOURCC('M','D','2','0')
#define ID_MD21 MAKEFOURCC('M','D','2','1')
#define ID_12DM MAKEFOURCC('1','2','D','M')
#define ID_BLP1 MAKEFOURCC('B','L','P','1')
#define ID_BLP2 MAKEFOURCC('B','L','P','2')
#define ID_DDS  MAKEFOURCC('D','D','S','\40')
#define ID_WDBC MAKEFOURCC('W','D','B','C')

typedef struct m3Model_s m3Model_t;
typedef struct mdxModel_s mdxModel_t;
typedef struct m2Model_s m2Model_t;

typedef char *string_t;
typedef char const *cstring_t;
typedef void *handle_t;
typedef char           PATHSTR[MAX_PATHLEN];
typedef struct color { float r, g, b, a; } color_t;
typedef struct color32 { uint8_t r, g, b, a; } color32_t;
typedef struct bounds { float min, max; } bounds_t;
typedef struct edges { float left, top, right, bottom; } edges_t;
typedef struct transform2 { VECTOR2 translation, scale; float rotation; } transform2_t;
typedef struct transform3 { VECTOR3 translation, rotation, scale; } transform3_t;
typedef char UINAME[80];

KNOWN_AS(SheetLayout, SHEETLAYOUT);
KNOWN_AS(SheetCell, SHEET);
KNOWN_AS(Doodad, DOODAD);
KNOWN_AS(vector3, VECTOR3);
KNOWN_AS(color32, COLOR32);
KNOWN_AS(animation_s, ANIMATION);
KNOWN_AS(uiFrame_s, UIFRAME);
KNOWN_AS(entityState_s, ENTITYSTATE);
KNOWN_AS(mapInfo_s, MAPINFO);
KNOWN_AS(mapPlayer_s, MAPPLAYER);
KNOWN_AS(playerState_s, PLAYER);

typedef enum {
    NO_BOM,
    UTF8_BOM_FOUND,
    UTF16LE_BOM_FOUND,
    UTF16BE_BOM_FOUND,
    INVALID_BOM
} BOMStatus;

typedef enum {
    MULTICAST_ALL,
    MULTICAST_PHS,
    MULTICAST_PVS,
    MULTICAST_ALL_R,
    MULTICAST_PHS_R,
    MULTICAST_PVS_R
} multicast_t;

/* Quake 2-compatible sound packet flags. */
#define SND_VOLUME      0x01 // byte; overrides default volume; used by svc_sound
#define SND_ATTENUATION 0x02 // byte; overrides default attenuation; used by svc_sound
#define SND_POS         0x04 // packed position; identifies an explicit world origin
#define SND_ENT         0x08 // short; entity number plus channel; identifies an entity source
#define SND_OFFSET      0x10 // byte; start delay in milliseconds; used by svc_sound
#define SND_PRIORITY    0x20 // ushort; playback importance, higher values win
#define SND_POLICY      0x40 // explicit admission policy, independent of spatial source

/* Reliable client music-control commands carried by svc_music.  The payload is
 * presentation-only state; game modules choose the command and the client owns
 * playlist lifetime, decoding, seeking and mixing. */
typedef enum {
    MUSIC_CMD_SET_MAP = 1,
    MUSIC_CMD_CLEAR_MAP,
    MUSIC_CMD_PLAY,
    MUSIC_CMD_STOP,
    MUSIC_CMD_RESUME,
    MUSIC_CMD_PLAY_THEMATIC,
    MUSIC_CMD_END_THEMATIC,
    MUSIC_CMD_SET_VOLUME,
    MUSIC_CMD_SET_POSITION,
    MUSIC_CMD_SET_THEMATIC_VOLUME,
    MUSIC_CMD_SET_THEMATIC_POSITION
} musicCommand_t;

/* Transient minimap attention-marker flags. */
#define MINIMAP_PING_REMEMBER      0x01 // bit; add position to recent-alert history; used by svc_minimap_ping
#define MINIMAP_PING_EXTRA_EFFECTS 0x02 // bit; draw an additional pulse; used by PingMinimapEx
#define MINIMAP_PING_DURATION_MAX 4294967.0f // seconds; uint32_t millisecond clock ceiling; bounds packet lifetime

/* Sound channels follow Quake 2; high bits select server delivery policy. */
#define CHAN_AUTO       0x00 // units; automatic channel selection; used for generic sounds
#define CHAN_WEAPON     0x01 // units; weapon channel; used for attack sounds
#define CHAN_VOICE      0x02 // units; voice channel; used for acknowledgements and death sounds
#define CHAN_ITEM       0x03 // units; item channel; reserved for item sounds
#define CHAN_BODY       0x04 // units; body channel; used for impact and movement sounds
#define CHAN_NO_PHS_ADD 0x08 // units; sends beyond normal PHS; used for global world sounds
#define CHAN_RELIABLE   0x10 // units; sends through the reliable client message; used for critical sounds
#define CHAN_OWNER      0x20 // units; unicasts to the source entity owner; used for local acknowledgements

/* Generic one-shot admission contract. Games own channel configuration and flag mapping. */
enum {
    SOUND_CHANNEL_PREEMPT = 4, SOUND_CHANNEL_OLDEST = 8,
    SOUND_LIST_PREEMPT = 16, SOUND_LIST_OLDEST = 32,
    SOUND_NO_DUPLICATES = 64, SOUND_DUPLICATE_PREEMPT = 128,
    SOUND_NO_DUPLICATE_USERS = 1024, SOUND_USER_PREEMPT = 2048,
    SOUND_IGNORE_USER = 32768
};
typedef struct {
    uint32_t priority, user, request;
    uint16_t flags, cooldown_ms;
    uint8_t group, max_channel, max_total, max_duplicates;
} soundPolicy_t;
/* Request zero opts out. Events travel back over the reliable client command stream. */
enum { SOUND_ACCEPTED = 1, SOUND_STARTED, SOUND_ENDED, SOUND_REJECTED };
typedef struct { uint32_t user, request, event; } soundEvent_t;

/* Upper channel bits carry optional generic importance to the sound packet. */
#define CHAN_PRIORITY(value) ((int)((unsigned)(value) << 8))
#define SOUND_PRIORITY(channel) (((unsigned)(channel) >> 8) & 65535)

#define DEFAULT_SOUND_PACKET_VOLUME 1.0f // normalized volume; default when SND_VOLUME is absent
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0f // attenuation scale; default when SND_ATTENUATION is absent

typedef enum {
    BLEND_MODE_NONE,
    BLEND_MODE_ALPHAKEY,
    BLEND_MODE_BLEND,
    BLEND_MODE_ADD,
    BLEND_MODE_ADDALPHA,
    BLEND_MODE_MODULATE,
    BLEND_MODE_MODULATE_2X,
} BLEND_MODE;

typedef enum {
    TEXMAP_FLAG_NONE,
    TEXMAP_FLAG_WRAP_U,
    TEXMAP_FLAG_WRAP_V,
    TEXMAP_FLAG_WRAP_UV,
} TEXMAP_FLAGS;

enum {
    ENT_PLAYER,
    ENT_HEALTH,
    ENT_MANA,
    ENT_CARGO, /* packed low nibble=count, high nibble=capacity for world cargo occupancy UI */
    ENT_STAT_COUNT,
};

#define ENT_CARGO_NIBBLE_MASK 0x0fu
#define ENT_CARGO_CAPACITY_SHIFT 4u

static inline uint8_t EntityCargoPack(uint32_t count, uint32_t capacity) {
    count = MIN(count, ENT_CARGO_NIBBLE_MASK);
    capacity = MIN(capacity, ENT_CARGO_NIBBLE_MASK);
    return (uint8_t)(count | (capacity << ENT_CARGO_CAPACITY_SHIFT));
}

static inline uint32_t EntityCargoCount(uint8_t packed) {
    return packed & ENT_CARGO_NIBBLE_MASK;
}

static inline uint32_t EntityCargoCapacity(uint8_t packed) {
    return (packed >> ENT_CARGO_CAPACITY_SHIFT) & ENT_CARGO_NIBBLE_MASK;
}

typedef enum {
    PLAYERSTATE_GAME_RESULT = 0,
    PLAYERSTATE_RESOURCE_GOLD = 1,
    PLAYERSTATE_RESOURCE_LUMBER = 2,
    PLAYERSTATE_RESOURCE_HERO_TOKENS = 3,
    PLAYERSTATE_RESOURCE_FOOD_CAP = 4,
    PLAYERSTATE_RESOURCE_FOOD_USED = 5,
    PLAYERSTATE_FOOD_CAP_CEILING = 6,
    PLAYERSTATE_GIVES_BOUNTY = 7,
    PLAYERSTATE_ALLIED_VICTORY = 8,
    PLAYERSTATE_PLACED = 9,
    PLAYERSTATE_OBSERVER_ON_DEATH = 10,
    PLAYERSTATE_OBSERVER = 11,
    PLAYERSTATE_UNFOLLOWABLE = 12,
    PLAYERSTATE_GOLD_UPKEEP_RATE = 13,
    PLAYERSTATE_LUMBER_UPKEEP_RATE = 14,
    PLAYERSTATE_GOLD_GATHERED = 15,
    PLAYERSTATE_LUMBER_GATHERED = 16,
} PLAYERSTATE;

typedef enum {
    PLAYERTEXT_SPEAKER,
    PLAYERTEXT_DIALOGUE,
    PLAYERTEXT_COUNT,
} PLAYERTEXT;

typedef enum {
    LAYER_BACKGROUND,
    LAYER_PORTRAIT,
    LAYER_CINEMATIC,
    LAYER_CONSOLE,
    LAYER_COMMANDBAR,
    LAYER_INFOPANEL,
    LAYER_INVENTORY,
    LAYER_MESSAGE,
    LAYER_QUESTDIALOG,
    LAYER_GAME_RESULT,
    LAYER_WORLD_HOVER,
    LAYER_UNIT_SHORTCUTS,
    LAYER_LOADING,
    LAYER_GAME_0,
    LAYER_GAME_1,
} UILAYOUTLAYER;

typedef enum {
    UI_WINDOW_OPEN,
} uiWindowOp_t;

#define UI_WINDOW_MOVABLE (1u << 0) // flag bit; permits client-local pointer dragging; used by server-authored windows
#define UI_WINDOW_MODAL   (1u << 1) // flag bit; blocks input outside the topmost modal window; used by confirmation-style windows
#define UI_WINDOW_UNIQUE  (1u << 2) // flag bit; keeps one instance per class; used by singleton inventory and journal windows
#define UI_WINDOW_NO_PAUSE (1u << 3) // flag bit; modal input capture without acquiring the client-owned simulation pause
#define UI_WINDOW_NO_ESCAPE (1u << 4) // flag bit; Escape is consumed without dismissing the window; used by mandatory result/decision windows
#define MAX_LAYOUT_LAYERS 16
#define UI_WINDOW_CLOSE_ACTION "close_window" // client action; closes the owning window without a server command
#define UI_WINDOW_CLOSE_NOTIFY_ACTION "close_window_notify" // client action; closes locally and notifies server of modal release
#define UI_WINDOW_CLOSE_COMMAND_PREFIX "close_window_command " // client action prefix; forwards suffix then closes the owning window
#define UI_WINDOW_DISCONNECT_ACTION "disconnect_game" // client action; leaves the current server/map and returns to the front-end
#define UI_WINDOW_QUIT_ACTION "quit_application" // client action; exits the application after an explicit local click

typedef struct {
    uint32_t id, class_id, flags;
} uiWindowDef_t;

typedef enum {
    UI_PLAYERSTAT_ENV_PHASE = 16, /* normalized 0..USHRT_MAX environment/day phase; 0 if unused */
    UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR = 17, /* WC3 presentation-only team-color slot for talking portraits */
    UI_PLAYERSTAT_SELECTION_HEALTH = 18,
    UI_PLAYERSTAT_SELECTION_MAX_HEALTH,
    UI_PLAYERSTAT_SELECTION_MANA,
    UI_PLAYERSTAT_SELECTION_MAX_MANA,
    UI_PLAYERSTAT_SELECTION_TIMED_STATUS, /* 0..USHRT_MAX; selected-unit timed-status remaining fraction */
    UI_PLAYERSTAT_ENV_VARIANT, /* presentation variant for environment-bound UI; 0 is normal */
    UI_PLAYERSTAT_GAME_VARIANT, /* opaque game-owned local presentation variant; shared/client code assigns no semantics */
} UIPLAYERSTAT;

typedef enum {
    /* uiFrame_t.stat is NFT_BYTE on the wire. Keep generic special bindings
     * inside the reserved high-byte range and below 251 so the established
     * selection/context bindings retain their values. */
    UI_STAT_LOADING_PROGRESS = 249, /* client-local normalized resource registration progress */
    UI_STAT_SELECTION_TIMED_STATUS = 250, /* statusbar binding; reads normalized selected-unit timer player stat */
    UI_STAT_SELECTION_HEALTH_TEXT = 251,
    UI_STAT_SELECTION_MANA_TEXT,
    UI_STAT_CONTEXT_NAME,
    UI_STAT_CONTEXT_HEALTH,
    UI_STAT_CONTEXT_MANA,
} UIFRAMESTAT;

_Static_assert(UI_STAT_CONTEXT_MANA <= UINT8_MAX,
               "uiFrame_t.stat bindings must fit the NFT_BYTE wire field");

typedef enum {
    CLIENT_UI_GAME,
    CLIENT_UI_LOADING,
    CLIENT_UI_CINEMATIC,
} CLIENTUISTATE;

#define SV_MAX_QUEST_LOG 16

typedef enum {
    SV_QUEST_NONE = 0,
    SV_QUEST_ACTIVE,
    SV_QUEST_COMPLETE,
    SV_QUEST_REWARDED
} svQuestStatus_t;

typedef struct {
    uint32_t quest_id;
    svQuestStatus_t status;
} svQuestEntry_t;

/* Evaluated scene light. type matches RMODELLIGHTTYPE; 0 is omni, so presence is `valid`. */
typedef struct ENVIRONLIGHT {
    VECTOR3 dir, color, ambient;
    float intensity, ambient_intensity;
    uint32_t type;
    bool valid;
} ENVIRONLIGHT;
typedef struct ENVIRONLIGHT *LPENVIRONLIGHT;
typedef const struct ENVIRONLIGHT *LPCENVIRONLIGHT;

_Static_assert(UI_PLAYERSTAT_ENV_PHASE != UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR,
               "env phase and cinematic portrait color must occupy distinct stats[] slots");
_Static_assert(UI_PLAYERSTAT_GAME_VARIANT < MAX_STATS,
               "presentation stats must fit playerState.stats[]");

/* Controller input is independent of whether the player edict has a visible model. */
#define BZ_INPUT_MAX_MSEC 250 // milliseconds; bounds one controller movement sample after stalls
#define BZ_INPUT_MOVE_MASK 15u // bits; four directional buttons accepted on the wire
#define BZ_BROAD_HEIGHT_SAMPLES 4 // taps per axis; shared terrain blur kernel; used by camera and air-height sampling

typedef enum { BZ_INPUT_FOCUS, BZ_INPUT_VIEW, BZ_INPUT_MOVE } INPUTACTION;
enum {
    BZ_MOVE_FORWARD = 1 << 0,
    BZ_MOVE_BACK = 1 << 1,
    BZ_MOVE_LEFT = 1 << 2,
    BZ_MOVE_RIGHT = 1 << 3,
};
typedef struct INPUTCMD {
    INPUTACTION action;
    union {
        VECTOR2 focus;
        struct { VECTOR3 angles; float distance; } view;
        struct { uint32_t buttons, msec; } move;
    };
} INPUTCMD;
typedef INPUTCMD *LPINPUTCMD;
typedef INPUTCMD const *LPCINPUTCMD;

struct playerState_s {
    uint32_t number;                   // client slot index
    VECTOR3 viewangles;             // Euler degrees, ROTATE_ZYX {pitch, roll, yaw}; client converts to quat and slerps
    VECTOR3 vieworigin;             // server-authored camera look-at in world space (XY focus + composed Z)
    float distance;                 // camera distance from vieworigin for orbit/isometric view
    float znear;                    // near clip; required camera sample, copied like fov
    float zfar;                     // far clip; required camera sample, copied like fov
    float fov;                      // vertical FOV in degrees; transmitted as NFT_FLOAT for cinematic interpolation
    uint32_t rdflags;                  // refdef flags (underwater tint, etc.)
    uint32_t uiflags;                  // per-widget HUD visibility bits, set server-side via FDF/svc_layout pipeline
    uint32_t client_ui_state;          // coarse UI mode: CLIENT_UI_LOADING/GAME/CINEMATIC; state machine, not a bitfield like uiflags
    uint8_t cinematic_portrait;        // model index, 0 = none; packed with team/color/race as one NFT_LONG
    uint8_t team;                      // alliance group (1-based, 0 = none); not the same as color
    uint8_t color;                     // cosmetic color slot (0 = red, 1 = blue, …)
    uint8_t race;                      // playerRace_t
    string_t name;                     // player display name from mapplayer or JASS script; NOTE: string_t but no caller mutates through this — cstring_t would be correct
    int32_t  start_location;           // start location index for JASS GetStartLocationX/Y (-1 = none)
    float cinefade;                 // full-screen fade alpha [0,1]; collapsed from Q2's blend[4] since no game here uses tinted overlays
    uint16_t stats[MAX_STATS];        // fast-update integer stats; uint16_t (vs Q3's int) to halve wire size
    cstring_t texts[PLAYERTEXT_COUNT]; // named player text channels used by server-authored UI
};

_Static_assert(offsetof(PLAYER, cinematic_portrait) % 4 == 0, "NFT_LONG identity pack requires 4-byte alignment");
_Static_assert(offsetof(PLAYER, team) == offsetof(PLAYER, cinematic_portrait) + 1, "team must follow cinematic_portrait");
_Static_assert(offsetof(PLAYER, color) == offsetof(PLAYER, cinematic_portrait) + 2, "color must follow team");
_Static_assert(offsetof(PLAYER, race) == offsetof(PLAYER, cinematic_portrait) + 3, "race must follow color");
_Static_assert(MAX_PLAYERS <= 256, "playerState_t.team is uint8_t");

/* One-shot events embedded in entityState_t.event.
 * The server sets event once; the client fires the sound and resets it.
 * Zero means no event. */
typedef enum {
    EV_NONE = 0,
    EV_ATTACK,       /* unit began an attack swing */
    EV_DEATH,        /* unit died */
    EV_MOVE,         /* footstep / movement sound */
} entity_event_t;

/* Packing layout for entityState_t.name.
 * CS_MAX_NAMES names total, ENT_NAMES_PER_CS per CS_GENERAL slot, ENT_NAME_SLOT_SIZE bytes each.
 * Wire slots use ASCII Unit Separator padding because configstrings cannot carry embedded NULs. The client restores separators
 * to NULs after receipt. Decode: i = name-1; slot = i>>4; sub = i&0xF. */
#define CS_MAX_NAMES        256
#define ENT_NAMES_PER_CS    16  /* names per configstring slot */
#define ENT_NAME_SLOT_SIZE  16  /* bytes per name; ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS == MAX_PATHLEN */
#define ENT_NAME_SEPARATOR  0x1f // ASCII byte; keeps fixed-width name records transmissible through C-string configstrings

static inline bool entity_name_slot_empty(cstring_t slot) { return !*slot || (uint8_t)*slot == ENT_NAME_SEPARATOR; }

static inline bool entity_name_slot_equals(cstring_t slot, cstring_t name) {
    size_t slot_len = 0, name_len = MIN(strlen(name), ENT_NAME_SLOT_SIZE - 1);
    while (slot_len < ENT_NAME_SLOT_SIZE - 1 && slot[slot_len] && (uint8_t)slot[slot_len] != ENT_NAME_SEPARATOR) slot_len++;
    return slot_len == name_len && !memcmp(slot, name, name_len);
}

static inline void entity_name_pool_prepare(string_t pool, cstring_t current) {
    memset(pool, ENT_NAME_SEPARATOR, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS);
    pool[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1] = '\0';
    if (current && *current)
        memcpy(pool, current, strnlen(current, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1));
}

static inline void entity_name_slot_store(string_t pool, uint32_t sub, cstring_t name) {
    string_t slot = pool + sub * ENT_NAME_SLOT_SIZE;
    size_t len = MIN(strlen(name), ENT_NAME_SLOT_SIZE - 1);
    memset(slot, ENT_NAME_SEPARATOR, ENT_NAME_SLOT_SIZE);
    memcpy(slot, name, len);
    pool[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1] = '\0';
}

static inline void entity_name_pool_decode(string_t pool) {
    FOR_LOOP(i, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1)
        if ((uint8_t)pool[i] == ENT_NAME_SEPARATOR) pool[i] = '\0';
}

typedef struct entityState_s {
    uint32_t number; // edict index
    uint32_t class_id;
    union {
        VECTOR3 origin;
        struct { VECTOR2 origin2; float z; };
    };
    float angle; /* Canonical actor heading, radians. */
#ifdef WOW
    VECTOR3 rotation; /* Raw placement Euler degrees; preserve the wire layout and decode in the game pose hook. */
#endif
    float scale;
    float radius;
    float collision;    /* gameplay collision radius when a client preview must mirror occupancy */
    float ground_offset; /* presentation: current altitude above the authoritative support surface (WC3 FlyHeight) */
    uint8_t stats[ENT_STAT_COUNT];
    uint8_t player;
    uint16_t model;
    uint16_t model2;
    uint8_t effect;
    uint16_t effect_flags; /* EFX_* presentation contract for effect/splat effects */
    uint16_t image;
    uint16_t name;        /* packed name: 0=none; see ENT_NAME_SLOT_SIZE/ENT_NAMES_PER_CS */
    uint32_t hover_value;   /* recipient-filtered hover detail; 0 = absent, present values are wire_value - 1 */
    uint16_t sound;
    uint32_t frame;
    uint8_t event;
    uint16_t flags;
    uint8_t renderfx;
    uint8_t ability;
    uint16_t pathing_width;   /* authored cursor/building pathing texture width in 32-unit cells */
    uint16_t pathing_height;  /* authored cursor/building pathing texture height in 32-unit cells */
    uint32_t pathing_preview;  /* low16 ignore entity, bits16..23 prevented, bits24..31 required */
    uint32_t splat;
#ifdef WOW
    uint32_t appearance;
    uint32_t equipment;
#endif
#ifndef USE_SHADOWMAPS
    uint32_t shadow;
    uint32_t shadow_rect;
#endif
} entityState_t;

_Static_assert(MAX_CLIENTS     <= 256,  "entityState_t.player is uint8_t — bump to uint16_t if MAX_CLIENTS exceeds 255");
_Static_assert(MAX_GAME_ENTITIES <= 65535, "entityState_t.pathing_preview reserves 16 bits for the ignored entity number");
_Static_assert(MAX_MODELS      <= 65535, "entityState_t.model/model2 are uint16_t — bump to uint32_t if MAX_MODELS exceeds 65534");
_Static_assert(MAX_SOUNDS      <= 65535, "entityState_t.sound is uint16_t — bump to uint32_t if MAX_SOUNDS exceeds 65534");
_Static_assert(MAX_CONFIGSTRINGS <= 65536, "entityState_t.image is uint16_t — bump to uint32_t if MAX_CONFIGSTRINGS exceeds 65535");
_Static_assert(ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS == MAX_PATHLEN, "packed name configstring must exactly fill one PATHSTR");
_Static_assert(CS_MAX_NAMES / ENT_NAMES_PER_CS <= MAX_GENERAL,       "name pool requires more CS_GENERAL slots than MAX_GENERAL provides");
_Static_assert(CS_MAX_NAMES <= 65535,                                 "entityState_t.name is uint16_t; packed index is 1-based so max is 65535");

static inline uint32_t EntityPathingPreviewPack(uint32_t ignore_entity, uint8_t prevented, uint8_t required) {
    return (ignore_entity & 0xffffu) | ((uint32_t)prevented << 16) | ((uint32_t)required << 24);
}

static inline uint16_t EntityPathingPreviewIgnore(uint32_t preview) {
    return (uint16_t)(preview & 0xffffu);
}

static inline uint8_t EntityPathingPreviewPrevented(uint32_t preview) {
    return (uint8_t)((preview >> 16) & 0xffu);
}

static inline uint8_t EntityPathingPreviewRequired(uint32_t preview) {
    return (uint8_t)((preview >> 24) & 0xffu);
}

#ifdef WOW
typedef struct wowAppearance_s {
    uint8_t skinColorID;
    uint8_t faceID;
    uint8_t hairStyleID;
    uint8_t hairColorID;
    uint8_t facialHairStyleID;
    uint8_t classID;
    uint8_t flags;
} wowAppearance_t;

typedef struct wowEquipment_s {
    uint8_t upperBodyItem;
    uint8_t lowerBodyItem;
    uint8_t handItem;
    uint8_t footItem;
} wowEquipment_t;

static inline uint32_t Wow_PackAppearance(uint8_t skinColorID,
                                       uint8_t faceID,
                                       uint8_t hairStyleID,
                                       uint8_t hairColorID,
                                       uint8_t facialHairStyleID,
                                       uint8_t classID,
                                       uint8_t flags) {
    return ((uint32_t)(skinColorID & 0x1f)) |
           ((uint32_t)(faceID & 0x0f) << 5) |
           ((uint32_t)(hairStyleID & 0x1f) << 10) |
           ((uint32_t)(hairColorID & 0x0f) << 15) |
           ((uint32_t)(facialHairStyleID & 0x0f) << 19) |
           ((uint32_t)(facialHairStyleID & 0x10) << 5) |
           ((uint32_t)(classID & 0x0f) << 23) |
           ((uint32_t)(flags & 0x1f) << 27);
}

static inline wowAppearance_t Wow_UnpackAppearance(uint32_t appearance) {
    wowAppearance_t unpacked = {
        .skinColorID = (uint8_t)(appearance & 0x1f),
        .faceID = (uint8_t)((appearance >> 5) & 0x0f),
        .hairStyleID = (uint8_t)((appearance >> 10) & 0x1f),
        .hairColorID = (uint8_t)((appearance >> 15) & 0x0f),
        /* Classic face IDs stop at 14, so its spare fifth bit carries facial-feature ID bit 4. */
        .facialHairStyleID = (uint8_t)(((appearance >> 19) & 0x0f) | ((appearance >> 5) & 0x10)),
        .classID = (uint8_t)((appearance >> 23) & 0x0f),
        .flags = (uint8_t)((appearance >> 27) & 0x1f),
    };
    return unpacked;
}

static inline uint32_t Wow_PackEquipment(uint8_t upperBodyItem,
                                      uint8_t lowerBodyItem,
                                      uint8_t handItem,
                                      uint8_t footItem) {
    return ((uint32_t)upperBodyItem) |
           ((uint32_t)lowerBodyItem << 8) |
           ((uint32_t)handItem << 16) |
           ((uint32_t)footItem << 24);
}

static inline wowEquipment_t Wow_UnpackEquipment(uint32_t equipment) {
    wowEquipment_t unpacked = {
        .upperBodyItem = (uint8_t)(equipment & 0xff),
        .lowerBodyItem = (uint8_t)((equipment >> 8) & 0xff),
        .handItem = (uint8_t)((equipment >> 16) & 0xff),
        .footItem = (uint8_t)((equipment >> 24) & 0xff),
    };
    return unpacked;
}
#endif

#define SHADOW_RECT_STEP 4.0f

static inline uint8_t ShadowPackRectComponent(float value) {
    if (value <= 0) {
        return 0;
    }
    uint32_t packed = (uint32_t)((value + SHADOW_RECT_STEP * 0.5f) / SHADOW_RECT_STEP);
    if (packed > 0xff) {
        packed = 0xff;
    }
    return (uint8_t)packed;
}

static inline float ShadowUnpackRectComponent(uint8_t packed) {
    return (float)packed * SHADOW_RECT_STEP;
}

static inline uint32_t ShadowPackRect(float x, float y, float w, float h) {
    return (uint32_t)ShadowPackRectComponent(x) |
           ((uint32_t)ShadowPackRectComponent(y) << 8) |
           ((uint32_t)ShadowPackRectComponent(w) << 16) |
           ((uint32_t)ShadowPackRectComponent(h) << 24);
}

static inline void ShadowUnpackRect(uint32_t packed, float * x, float * y, float * w, float * h) {
    if (x) *x = ShadowUnpackRectComponent((uint8_t)(packed & 0xff));
    if (y) *y = ShadowUnpackRectComponent((uint8_t)((packed >> 8) & 0xff));
    if (w) *w = ShadowUnpackRectComponent((uint8_t)((packed >> 16) & 0xff));
    if (h) *h = ShadowUnpackRectComponent((uint8_t)((packed >> 24) & 0xff));
}

typedef struct animation_s {
    char name[80];
    uint32_t interval[2];
    float movespeed;     // movement speed of the entity while playing this animation
    uint32_t flags;      // &1: non looping
    float rarity;
    uint32_t syncpoint;
    float radius;
    VECTOR3 min;
    VECTOR3 max;
    uint32_t damage_point;
} animation_t;

typedef struct {
    uint32_t width;
    uint32_t height;
} size2_t;

/* UI canvas contract: how the authored UI scene maps onto the window (docs/architecture/ui-canvas.md).
 * The client resolves it (common/ui_canvas.h), the renderer projects it, the game authors chrome per class. */
typedef enum {
    UI_CANVAS_STRETCH,       // authored scene fills the window at any aspect; classic WC3 without widescreen chrome
    UI_CANVAS_EXPAND,        // scene widens with the window aspect and the HUD root is the whole scene; SC2 and WoW
    UI_CANVAS_EXPAND_CENTER, // scene widens, the authored 4:3 HUD root stays centered, extension chrome fills the sides
} UICANVASPOLICY;

typedef enum {
    UI_CANVAS_STANDARD,    // no scene area beside the HUD root; the game authors its 4:3 chrome only
    UI_CANVAS_WIDE,        // scene area exists beside the HUD root; the game also authors extension chrome for it
    UI_CANVAS_CLASS_COUNT,
} UICANVASCLASS;

typedef struct {
    rect_t scene;            // full UI scene in authored units; the renderer maps it onto the whole drawable
    rect_t root;             // server-authored HUD root inside the scene; centered under UI_CANVAS_EXPAND_CENTER
    size2_t window;        // logical window size the canvas was resolved from
    UICANVASPOLICY policy; // resolved once per mounted data by the game's client hook
    UICANVASCLASS chrome;  // presentation class reported to the game through the ui_canvas client command
} UICANVAS;
typedef UICANVAS *LPUICANVAS;
typedef UICANVAS const *LPCUICANVAS;

typedef enum {
    TE_GUNSHOT,
    TE_BLOOD,
    TE_MOVE_CONFIRMATION,
    TE_MISSILE,
    TE_FIREBOLT_IMPACT,    /* WoW: fire explosion — payload: POSITION, model int16_t */
    TE_FROSTBOLT_IMPACT,   /* WoW: frost burst — payload: POSITION, model int16_t */
    TE_ATTACK_CONFIRMATION,
    /* Generic server-selected world text. Payload: POSITION, STRING, RGBA int32_t,
     * font int16_t, lifetime int32_t ms, fade-start int32_t ms, velocity float/float px/s. */
    TE_FLOATING_TEXT,
    /* Generic transient entity highlight. Payload: entity int32_t, RGBA int32_t.
     * The client owns the fixed two-flash lifetime and follows the entity while visible. */
    TE_ENTITY_INDICATOR,
} tempEvent_t;

typedef enum {
    FT_NONE,
    FT_BACKDROP,
    FT_BUTTON,
    FT_CHATDISPLAY,
    FT_CHECKBOX,
    FT_CONTROL,
    FT_DIALOG,
    FT_EDITBOX,
    FT_FRAME,
    FT_GLUEBUTTON,
    FT_GLUECHECKBOX,
    FT_GLUEEDITBOX,
    FT_GLUEPOPUPMENU,
    FT_GLUETEXTBUTTON,
    FT_HIGHLIGHT,
    FT_LISTBOX,
    FT_MENU,
    FT_MODEL,
    FT_POPUPMENU,
    FT_SCROLLBAR,
    FT_SIMPLEBUTTON,
    FT_SIMPLECHECKBOX,
    FT_SIMPLEFRAME,
    FT_SIMPLESTATUSBAR,
    FT_SLASHCHATBOX,
    FT_SLIDER,
    FT_SPRITE,
    FT_TEXT,
    FT_TEXTAREA,
    FT_TEXTBUTTON,
    FT_TIMERTEXT,
    FT_TEXTURE,
    FT_STRING,
    FT_LAYER,
    FT_SCREEN,
    FT_COMMANDBUTTON,
    FT_PORTRAIT,
    FT_STRINGLIST,
    // custom types
    FT_BUILDQUEUE,
    FT_MESSAGE_QUEUE,
    FT_MULTISELECT,
    FT_TOOLTIPTEXT,
    FT_MINIMAP,
    FT_NAMETAG,
    FT_LOADING_BAR,
    FT_SEGMENTED_STATUSBAR, /* entity-context packed count/capacity rendered as equal filled/empty slots */
} FRAMETYPE;

#define UIFLAG_RADIAL_SHADE      (1 << 9) // FT_COMMANDBUTTON: uiCommandButton_t carries a client-clock radial timer
#define UIFLAG_SIZE_TO_CONTENT   (1 << 10) // flag bit; uiNameTag_t measured size; ignored on a fully min+max-anchored axis
#define UIFLAG_ALTERNATE_ACTIVE (1 << 11) // flag bit; secondary command state is active (for example an autocast toggle)
#define UIFLAG_SPRITE_STAT_SEQUENCE (1 << 12) // FT_SPRITE: frame.value names a stats[] slot selecting an explicit #N sequence
#define UIFLAG_EXTEND_WIDESCREEN_X (1 << 13) // flag bit; client expands this frame horizontally across the full UI canvas
#define UIFLAG_SPRITE_OVERLAY (1 << 16) // flag bit; draws an authored sprite after the containing layout artwork
#define UIFLAG_MINIMAP_PREVIEW (1 << 15) // flag bit; frame text names a static map preview; excludes fog, camera and input
#define UIFLAG_ALERT_RED_PULSE (1 << 14) // flag bit; command-button art pulses red until frame.value absolute milliseconds; used for transient alerts

typedef enum {
    BACKDROP_TOP_LEFT_CORNER,
    BACKDROP_TOP_EDGE,
    BACKDROP_TOP_RIGHT_CORNER,
    BACKDROP_LEFT_EDGE,
    BACKDROP_CENTER,
    BACKDROP_RIGHT_EDGE,
    BACKDROP_BOTTOM_LEFT_CORNER,
    BACKDROP_BOTTOM_EDGE,
    BACKDROP_BOTTOM_RIGHT_CORNER,
    BACKDROP_SIZE,
} BACKDROPCORNER;

typedef enum {
    FPP_MIN,
    FPP_MID,
    FPP_MAX,
    FPP_COUNT,
} uiFramePointPos_t;

typedef enum {
    FONT_JUSTIFYCENTER,
    FONT_JUSTIFYLEFT,
    FONT_JUSTIFYRIGHT,
} uiFontJustificationH_t;

typedef enum {
    FONT_JUSTIFYMIDDLE,
    FONT_JUSTIFYTOP,
    FONT_JUSTIFYBOTTOM,
} uiFontJustificationV_t;

#define UI_PARENT 255

typedef struct { // serialized as 4 bytes
    uiFramePointPos_t targetPos: 7;
    uint8_t used: 1;
    uint8_t relativeTo: 8;
    int16_t offset: 16;
} uiFramePoint_t;

typedef uiFramePoint_t uiFramePoints_t[FPP_COUNT];

/* Model-frame payload: camera and placement authored by the game's layout, not network entity state. */
typedef enum { UI_MODEL_PERSPECTIVE, UI_MODEL_ORTHOGRAPHIC } UIMODELPROJECTION;
typedef struct UIMODEL {
    VECTOR3 eye, target, pos, scale;
    float fov, znear, zfar, aspect;
    UIMODELPROJECTION projection;
} UIMODEL;
typedef struct UIMODEL *LPUIMODEL;
typedef const struct UIMODEL *LPCUIMODEL;

typedef struct {
    uint32_t radialStartTime; /* absolute server/client clock milliseconds */
    uint32_t radialEndTime;   /* <= start means no radial progress overlay */
} uiCommandButton_t;

typedef struct uiFrame_s {
    uint32_t number;
    uint32_t parent;
    COLOR32 color;
    struct { uiFramePoints_t x, y; } points;
    struct { float width, height; } size;
    struct {
        uint16_t index;
        uint16_t index2;
        uint8_t coord[4];  // also used as animation start timestamp
    } tex;
    union {
        struct {
            FRAMETYPE type: 8;
            uint8_t alphaMode: 2;
        } flags;
        uint32_t flagsvalue;
    };
    struct {
        handle_t data;
        uint32_t size;
    } buffer;
    uint32_t textLength;
    uint32_t stat;
    cstring_t text; /* type-specific text; FT_COMMANDBUTTON uses this for its optional secondary click command */
    cstring_t tooltip;
    cstring_t onclick;
    float value;
    uint8_t hotkey;
} uiFrame_t;

typedef uint16_t RESOURCE;

typedef struct {
    uint16_t image;
    uint32_t starttime;
    uint32_t endtime;
} uiBuildQueueItem_t;

#define UI_MULTISELECT_ITEM_FOCUSED (1u << 0)

typedef struct {
    uint16_t image;
    uint16_t entity;
    uint16_t flags;
} uiMultiselectItem_t;

typedef struct {
    uint16_t firstitem;
    uint16_t buildtimer;
    float itemoffset;
    uint16_t numitems;
    uiBuildQueueItem_t items[];
} uiBuildQueue_t;

typedef struct {
    uint32_t message_id;
    RESOURCE image;
    RESOURCE title_font;
    RESOURCE body_font;
    uint8_t flags;
} uiMessageQueue_t;

#define UI_MESSAGE_UNREAD (1u << 0) // flag bit; server marks an unread message; used by FT_MESSAGE_QUEUE
#define UI_MESSAGE_OPEN   (1u << 1) // flag bit; server marks the message panel; used by FT_MESSAGE_QUEUE

typedef struct {
    RESOURCE hp_bar;
    RESOURCE mana_bar;
    RESOURCE focus_highlight;
    VECTOR2 offset;
    uint16_t numcolumns;
    uint16_t numitems;
    uiMultiselectItem_t items[];
} uiMultiselect_t;

typedef struct {
    uiFontJustificationH_t textalignx: 4;
    uiFontJustificationV_t textaligny: 4;
    int16_t offsetx;
    int16_t offsety;
    RESOURCE font;
} uiLabel_t;

typedef struct {
    RESOURCE font;
    float inset;
} uiTextArea_t;

typedef struct {
    RESOURCE alphaFile;
    BLEND_MODE alphaMode;
} uiHighlight_t;

typedef struct {
    RESOURCE texture;
    RESOURCE font;
    uint8_t texcoord[4];
    COLOR32 fontcolor;
} uiSimpleButtonState_t;

typedef struct {
    uiSimpleButtonState_t normal;
    uiSimpleButtonState_t pushed;
    uiSimpleButtonState_t disabled;
    uiSimpleButtonState_t highlight;
} uiSimpleButton_t;

typedef struct {
    int16_t CornerFlags;
    float CornerSize;
    float BackgroundSize;
    float BackgroundInsets[4];// 0.01 0.01 0.01 0.01,
    RESOURCE EdgeFile;//  "EscMenuBorder",
    RESOURCE Background;
    bool TileBackground:1;
    bool BlendAll:1;
    bool Mirrored:1;
} uiBackdrop_t;

/* Optional buffer for FT_TEXTURE frames that need float-precision UV or flip.
 * When present, SCR_LayoutDrawTexture uses these values instead of tex.coord. */
typedef struct {
    float l, r, t, b;   /* UV as float [0,1]; l>r or t>b = flipped axis */
    COLOR32 color;
    BLEND_MODE alphamode;
} uiTextureUV_t;

typedef struct {
    uiBackdrop_t background;
    RESOURCE font;
    float borderSize;
    COLOR32 textColor;
    COLOR32 cursorColor;
    uint32_t maxChars;
    UINAME id;
} uiEditBox_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
    float border;
    float itemHeight;
    int16_t selectedIndex;
    UINAME id;
    UINAME fetchCommand;
    uint32_t editTarget;
} uiListBox_t;

typedef struct {
    uiBackdrop_t background;
    uiBackdrop_t incButton;
    uiBackdrop_t decButton;
    uiBackdrop_t thumbButton;
} uiScrollBar_t;

/* Compact scrollbar art: one normal state, shared UV crop, and square parts. */
typedef struct {
    RESOURCE image[3];
    uint8_t texcoord[4];
} uiScrollBarImage_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
} uiTooltip_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
    float padding_x;
    float padding_y;
    float min_width;
} uiNameTag_t;

typedef struct {
    uiBackdrop_t normal;
    uiBackdrop_t pushed;
    uiBackdrop_t disabled;
    uiBackdrop_t disabledPushed;
    uiHighlight_t highlight;
    VECTOR2 pushedTextOffset;
} uiGlueTextButton_t;

typedef struct {
    uiBackdrop_t normal;
    uiBackdrop_t pushed;
    uiBackdrop_t disabled;
    uiBackdrop_t disabledPushed;
    uiHighlight_t mouseOver;
    uiHighlight_t checked;
    uiHighlight_t disabledChecked;
} uiCheckBox_t;

_Static_assert(sizeof(uiCheckBox_t) <= 255, "uiCheckBox_t must fit the one-byte UI typed payload");

typedef struct {
    string_t tok;
    cstring_t str;
    bool reading_string;
    bool error;
    bool comma_space;
    bool equals_space;
    char token[TOKEN_LEN];
} parser_t;

typedef struct {
    uint32_t itemID;
    int chanceToDrop;
} droppableItem_t;

typedef struct {
    int num_droppableItems;
    droppableItem_t *droppableItems;
} droppableItemSet_t;

typedef struct {
    int x, y;
} point2_t;

typedef struct {
    uint32_t level;        // (set to 1 for non hero units and items)
    uint32_t str;          // strength attribute
    uint32_t agi;          // agility attribute
    uint32_t intel;        // intelligence attribute
    uint32_t xp;           // accumulated experience points
    bool  suspend_xp;   // when true, XP gains are suspended
    uint32_t skillpoints;  // available skill points for hero ability learning
} doodadHero_t;

typedef struct {
    uint32_t slot;
    uint32_t itemID;
} inventoryItem_t;

typedef struct {
    uint32_t abilityID;
    uint32_t active;
    uint32_t level;
} modifiedAbility_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    COLOR32 map[];
} pathTex_t;

struct Doodad {
    uint32_t doodID;
    uint32_t variation;
    VECTOR3 position;
    float angle;
    VECTOR3 scale;
    uint8_t flags;
    uint32_t player;
    uint8_t treeLife; // integer stored in %, 100% is 0x64, 170% is 0xAA for example
    uint8_t unknown1;
    uint8_t unknown2;
    uint32_t hitPoints; // (-1 = use default)
    uint32_t manaPoints; // (-1 = use default, 0 = unit doesn't have mana)
    uint32_t droppedItemSetPtr;
    uint32_t num_droppedItemSets;
    uint32_t goldAmount; // (default = 12500)
    float targetAcquisition; // (-1 = normal, -2 = camp)
    doodadHero_t hero;
    uint32_t num_inventoryItems;
    uint32_t num_modifiedAbilities;
    droppableItemSet_t *droppableItemSets;
    inventoryItem_t *inventoryItems;
    modifiedAbility_t *modifiedAbilities;
    uint32_t randomUnitFlag; // "r" (for uDNR units and iDNR items)
    uint32_t levelOfRandomItem; //    byte[3]: level of the random unit/item,-1 = any (this is actually interpreted as a 24-bit number)
    //    byte: item class of the random item, 0 = any, 1 = permanent ... (this is 0 for units)
    uint32_t randomUnitGroupNumber; //    uint32_t: unit group number (which group from the global table)
    uint32_t randomUnitPositionNumber; //    uint32_t: position number (which column of this group)
    uint32_t num_diffAvailUnits;
    droppableItem_t *diffAvailUnits;
    uint32_t color; // map placement color index; -1 uses the unit type/owner color
    uint32_t waygate;
    uint32_t unitID;
    struct Doodad *next;
};

typedef struct particle_s {
    struct particle_s *next;
    struct texture const *texture;
    VECTOR3 org;
    VECTOR3 vel;
    VECTOR3 accel;
    VECTOR3 tail;       /* optional world-space trail vector; zero keeps billboard behavior */
    COLOR32 color[3];
    uint8_t size[3];
    uint8_t midtime;
    uint8_t columns;
    uint8_t rows;
    uint8_t blend_mode;
    float size_value_scale;
    float size_time_scale;
    float time;
    float lifespan;
} cparticle_t;

typedef enum {
    kPlayerRaceNone,
    kPlayerRaceHuman,
    kPlayerRaceOrc,
    kPlayerRaceUndead,
    kPlayerRaceNightElf
} playerRace_t;

typedef enum {
    LOBBY_SLOT_OPEN,
    LOBBY_SLOT_HUMAN,
    LOBBY_SLOT_COMPUTER,
    LOBBY_SLOT_CLOSED,
} lobbySlotType_t;

typedef struct lobbySlot_s {
    bool visible;
    bool occupied;
    uint32_t client;
    uint32_t map_player;
    lobbySlotType_t type;
    playerRace_t race;
    uint32_t team;
    uint32_t color;
    UINAME name;
} lobbySlot_t;

typedef struct lobbyState_s {
    bool active;
    PATHSTR map_path;
    UINAME map_name;
    uint32_t game_speed;
    uint32_t slot_count;
    uint32_t revision;
    uint32_t local_slot;
    lobbySlot_t slots[MAX_PLAYERS];
} lobbyState_t;

//#define NULL 0

#endif
