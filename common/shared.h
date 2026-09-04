#ifndef shared_h
#define shared_h

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>

#include "../shared/shared.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
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
#define BZ_CLAMP_U8(x) ((BYTE)MIN(255, MAX(0, (int)(x)))) // clamp a numeric expression to a 0..255 byte

#define BYTE2FLOAT(x) ((x)/255.f)

#define IS_FOURCC(STRING) (STRING && strlen(STRING) == 4)

#define MAKE(TYPE,...)(TYPE){__VA_ARGS__}

#define COLOR32_WHITE MAKE(COLOR32,255,255,255,255)
#define COLOR32_BLACK MAKE(COLOR32,0,0,0,255)

#ifndef __cplusplus
  #include <stdbool.h>
#endif

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
        BZ_FIELD_FOURCC,  /* 4-char text → DWORD via memcpy (same as MAKEFOURCC on LE) */
} bzFieldType_t;

#define KNOWN_AS(STRUCT, TYPE) \
typedef struct STRUCT TYPE; \
typedef struct STRUCT *LP##TYPE; \
typedef struct STRUCT const *LPC##TYPE;

#define FOR_LOOP(property, max) \
for (DWORD property = 0, end = max; property < end; ++property)

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
#define ARRAY(type, name) type *name; DWORD name##_count
#define ARRAY_COUNT(name) (name##_count)
#define IS_ARRAY_EMPTY(name) (!(name) || !ARRAY_COUNT(name))
#define FOR_EACH_ARRAY(type, property, name) \
for (type *property = name; property - name < ARRAY_COUNT(name); property++)

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) ((int)(char)(ch0) | ((int)(char)(ch1) << 8) | ((int)(char)(ch2) << 16) | ((int)(char)(ch3) << 24))
#endif

#define FOFS(type, x) (HANDLE)&(((struct type *)NULL)->x)

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
for (LPCSTR ITEM = PARSEFUNC(&parser); ITEM; ITEM = PARSEFUNC(&parser))

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
#define EFX_SLOT_MASK 0x1f00 // bits 8-12; authored attachment slots used by model effects
#define EFX_SLOT_SHIFT 8 // bits; low bit of the packed attachment-slot mask; used by the renderer

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
#define MAX_MODELS 256
#define MAX_FONTSTYLES 256
#define MAX_SOUNDS 512 // units carry many sounds (what/yes/attack/death per unit type); a single map peaks above 256
#define MAX_IMAGES 256
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

// Typedefs for ANSI C
typedef unsigned char  BYTE;
typedef unsigned char  BOOL;
typedef unsigned short USHORT;
typedef int            LONG;
typedef short          SHORT;
typedef unsigned int   DWORD;
typedef unsigned short WORD;
typedef unsigned long  DWORD_PTR;
typedef long           LONG_PTR;
typedef long           INT_PTR;
typedef long long      LONGLONG;
typedef unsigned long long ULONGLONG;
typedef float          FLOAT;
typedef void         * HANDLE;
typedef void         * LPOVERLAPPED; // Unsupported on Linux and Mac
typedef char           TCHAR;
typedef unsigned int   LCID;
typedef LONG         * PLONG;
typedef DWORD        * LPDWORD;
typedef BYTE         * LPBYTE;
typedef FLOAT        * LPFLOAT;
typedef const char   * LPCTSTR;
typedef const char   * LPCSTR;
typedef char         * LPTSTR;
typedef char         * LPSTR;
typedef FLOAT const  * LPCFLOAT;
typedef char           PATHSTR[MAX_PATHLEN];
typedef void const   * LPCVOID;
typedef struct color { FLOAT r, g, b, a; } color_t;
typedef struct color32 { BYTE r, g, b, a; } color32_t;
typedef struct bounds { FLOAT min, max; } bounds_t;
typedef struct edges { FLOAT left, top, right, bottom; } edges_t;
typedef struct transform2 { VECTOR2 translation, scale; FLOAT rotation; } transform2_t;
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

/* Transient minimap attention-marker flags. */
#define MINIMAP_PING_REMEMBER      0x01 // bit; add position to recent-alert history; used by svc_minimap_ping
#define MINIMAP_PING_EXTRA_EFFECTS 0x02 // bit; draw an additional pulse; used by PingMinimapEx
#define MINIMAP_PING_DURATION_MAX 4294967.0f // seconds; DWORD millisecond clock ceiling; bounds packet lifetime

/* Sound channels follow Quake 2; high bits select server delivery policy. */
#define CHAN_AUTO       0x00 // units; automatic channel selection; used for generic sounds
#define CHAN_WEAPON     0x01 // units; weapon channel; used for attack sounds
#define CHAN_VOICE      0x02 // units; voice channel; used for acknowledgements and death sounds
#define CHAN_ITEM       0x03 // units; item channel; reserved for item sounds
#define CHAN_BODY       0x04 // units; body channel; used for impact and movement sounds
#define CHAN_NO_PHS_ADD 0x08 // units; sends beyond normal PHS; used for global world sounds
#define CHAN_RELIABLE   0x10 // units; sends through the reliable client message; used for critical sounds
#define CHAN_OWNER      0x20 // units; unicasts to the source entity owner; used for local acknowledgements

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
    ENT_UNUSED,
    ENT_STAT_COUNT,
};

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
    PLAYERTEXT_MAP_TITLE,
    PLAYERTEXT_MAP_SUGGESTED_PLAYERS,
    PLAYERTEXT_MAP_SIZE,
    PLAYERTEXT_MAP_TILESET,
    PLAYERTEXT_MAP_DESCRIPTION,
    PLAYERTEXT_MAP_PREVIEW,
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
} UILAYOUTLAYER;

typedef enum {
    UI_WINDOW_OPEN,
} uiWindowOp_t;

#define UI_WINDOW_MOVABLE (1u << 0) // flag bit; permits client-local pointer dragging; used by server-authored windows
#define UI_WINDOW_MODAL   (1u << 1) // flag bit; blocks input outside the topmost modal window; used by confirmation-style windows
#define UI_WINDOW_UNIQUE  (1u << 2) // flag bit; keeps one instance per class; used by singleton inventory and journal windows
#define UI_WINDOW_NO_PAUSE (1u << 3) // flag bit; modal input capture without acquiring the client-owned simulation pause
#define UI_WINDOW_CLOSE_ACTION "close_window" // client action; closes the owning window without a server command
#define UI_WINDOW_CLOSE_NOTIFY_ACTION "close_window_notify" // client action; closes locally and notifies server of modal release
#define UI_WINDOW_CLOSE_COMMAND_PREFIX "close_window_command " // client action prefix; forwards suffix then closes the owning window
#define UI_WINDOW_DISCONNECT_ACTION "disconnect_game" // client action; leaves the current server/map and returns to the front-end
#define UI_WINDOW_QUIT_ACTION "quit_application" // client action; exits the application after an explicit local click

typedef struct {
    DWORD id, class_id, flags;
} uiWindowDef_t;

typedef enum {
    UI_PLAYERSTAT_SELECTION_HEALTH = 18,
    UI_PLAYERSTAT_SELECTION_MAX_HEALTH,
    UI_PLAYERSTAT_SELECTION_MANA,
    UI_PLAYERSTAT_SELECTION_MAX_MANA,
} UIPLAYERSTAT;

typedef enum {
    UI_STAT_SELECTION_HEALTH_TEXT = 251,
    UI_STAT_SELECTION_MANA_TEXT,
    UI_STAT_CONTEXT_NAME,
    UI_STAT_CONTEXT_HEALTH,
    UI_STAT_CONTEXT_MANA,
} UIFRAMESTAT;

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
    DWORD quest_id;
    svQuestStatus_t status;
} svQuestEntry_t;

struct playerState_s {
    DWORD number;                   // client slot index
    QUATERNION viewquat;            // canonical 3D view orientation sent to the renderer
    VECTOR3 viewangles;             // euler pitch/yaw for WoW orbit camera math; only transmitted #ifdef WOW (can't round-trip euler from quat losslessly)
    VECTOR2 origin;                 // 2D camera focus point; XY only because all games here are isometric/orbit, not first-person
#ifdef WC3
    BOX2 camera_bounds;             // current per-player camera target bounds; initialized from W3I, mutable by SetCameraBounds
#endif
    FLOAT distance;                 // camera distance from origin for orbit/isometric view
    DWORD fov;                      // vertical FOV in degrees; transmitted as NFT_BYTE so BYTE would suffice
    DWORD rdflags;                  // refdef flags (underwater tint, etc.)
    DWORD uiflags;                  // per-widget HUD visibility bits, set server-side via FDF/svc_layout pipeline
    DWORD client_ui_state;          // coarse UI mode: CLIENT_UI_LOADING/GAME/CINEMATIC; state machine, not a bitfield like uiflags
    DWORD cinematic_portrait;       // WC3 only: model index of transmission talking-head portrait (set by JASS TransmissionFromUnitWithNameBJ), 0 = none
    DWORD team;                     // alliance group (1-based, 0 = none); NOT the same as color — multiple colors can share a team
    DWORD color;                    // cosmetic color slot (0 = red, 1 = blue, …); drives minimap dot and skin lookup, NOT the same as team
    DWORD race;                     // map player race (playerRace_t), used to resolve WC3 race UI skins
    LPSTR name;                     // player display name from mapplayer or JASS script; NOTE: LPSTR but no caller mutates through this — LPCSTR would be correct
    LONG  start_location;           // start location index for JASS GetStartLocationX/Y (-1 = none)
    FLOAT cinefade;                 // full-screen fade alpha [0,1]; collapsed from Q2's blend[4] since no game here uses tinted overlays
    USHORT stats[MAX_STATS];        // fast-update integer stats; USHORT (vs Q3's int) to halve wire size
    LPCSTR texts[MAX_STATS];        // string stats parallel to stats[], for UI text fields
};

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

static inline BOOL entity_name_slot_empty(LPCSTR slot) { return !*slot || (BYTE)*slot == ENT_NAME_SEPARATOR; }

static inline BOOL entity_name_slot_equals(LPCSTR slot, LPCSTR name) {
    size_t slot_len = 0, name_len = MIN(strlen(name), ENT_NAME_SLOT_SIZE - 1);
    while (slot_len < ENT_NAME_SLOT_SIZE - 1 && slot[slot_len] && (BYTE)slot[slot_len] != ENT_NAME_SEPARATOR) slot_len++;
    return slot_len == name_len && !memcmp(slot, name, name_len);
}

static inline void entity_name_pool_prepare(LPSTR pool, LPCSTR current) {
    memset(pool, ENT_NAME_SEPARATOR, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS);
    pool[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1] = '\0';
    if (current && *current)
        memcpy(pool, current, strnlen(current, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1));
}

static inline void entity_name_slot_store(LPSTR pool, DWORD sub, LPCSTR name) {
    LPSTR slot = pool + sub * ENT_NAME_SLOT_SIZE;
    size_t len = MIN(strlen(name), ENT_NAME_SLOT_SIZE - 1);
    memset(slot, ENT_NAME_SEPARATOR, ENT_NAME_SLOT_SIZE);
    memcpy(slot, name, len);
    pool[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1] = '\0';
}

static inline void entity_name_pool_decode(LPSTR pool) {
    FOR_LOOP(i, ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS - 1)
        if ((BYTE)pool[i] == ENT_NAME_SEPARATOR) pool[i] = '\0';
}

typedef struct entityState_s {
    DWORD number; // edict index
    DWORD class_id;
    union {
        VECTOR3 origin;
        struct { VECTOR2 origin2; FLOAT z; };
    };
    FLOAT angle;
#ifdef WOW
    VECTOR3 rotation;
#endif
    FLOAT scale;
    FLOAT radius;
    BYTE stats[ENT_STAT_COUNT];
    BYTE player;
    BYTE model;
    BYTE model2;
    BYTE effect;
    USHORT effect_flags; /* EFX_* presentation contract for effect/splat effects */
    USHORT image;
    USHORT name;        /* packed name: 0=none; see ENT_NAME_SLOT_SIZE/ENT_NAMES_PER_CS */
    USHORT sound;
    DWORD frame;
    BYTE event;
    USHORT flags;
    BYTE renderfx;
    BYTE ability;
    USHORT pathing_width;   /* authored cursor/building pathing texture width in 32-unit cells */
    USHORT pathing_height;  /* authored cursor/building pathing texture height in 32-unit cells */
    DWORD splat;
#ifdef WOW
    DWORD appearance;
    DWORD equipment;
#endif
#ifndef USE_SHADOWMAPS
    DWORD shadow;
    DWORD shadow_rect;
#endif
} entityState_t;

_Static_assert(MAX_CLIENTS     <= 256,  "entityState_t.player is BYTE — bump to USHORT if MAX_CLIENTS exceeds 255");
_Static_assert(MAX_MODELS      <= 256,  "entityState_t.model/model2 are BYTE — bump to USHORT if MAX_MODELS exceeds 255");
_Static_assert(MAX_SOUNDS      <= 65535, "entityState_t.sound is USHORT — bump to DWORD if MAX_SOUNDS exceeds 65534");
_Static_assert(MAX_CONFIGSTRINGS <= 65536, "entityState_t.image is USHORT — bump to DWORD if MAX_CONFIGSTRINGS exceeds 65535");
_Static_assert(ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS == MAX_PATHLEN, "packed name configstring must exactly fill one PATHSTR");
_Static_assert(CS_MAX_NAMES / ENT_NAMES_PER_CS <= MAX_GENERAL,       "name pool requires more CS_GENERAL slots than MAX_GENERAL provides");
_Static_assert(CS_MAX_NAMES <= 65535,                                 "entityState_t.name is USHORT; packed index is 1-based so max is 65535");

#ifdef WOW
typedef struct wowAppearance_s {
    BYTE skinColorID;
    BYTE faceID;
    BYTE hairStyleID;
    BYTE hairColorID;
    BYTE facialHairStyleID;
    BYTE classID;
    BYTE flags;
} wowAppearance_t;

typedef struct wowEquipment_s {
    BYTE upperBodyItem;
    BYTE lowerBodyItem;
    BYTE handItem;
    BYTE footItem;
} wowEquipment_t;

static inline DWORD Wow_PackAppearance(BYTE skinColorID,
                                       BYTE faceID,
                                       BYTE hairStyleID,
                                       BYTE hairColorID,
                                       BYTE facialHairStyleID,
                                       BYTE classID,
                                       BYTE flags) {
    return ((DWORD)(skinColorID & 0x1f)) |
           ((DWORD)(faceID & 0x0f) << 5) |
           ((DWORD)(hairStyleID & 0x1f) << 10) |
           ((DWORD)(hairColorID & 0x0f) << 15) |
           ((DWORD)(facialHairStyleID & 0x0f) << 19) |
           ((DWORD)(facialHairStyleID & 0x10) << 5) |
           ((DWORD)(classID & 0x0f) << 23) |
           ((DWORD)(flags & 0x1f) << 27);
}

static inline wowAppearance_t Wow_UnpackAppearance(DWORD appearance) {
    wowAppearance_t unpacked = {
        .skinColorID = (BYTE)(appearance & 0x1f),
        .faceID = (BYTE)((appearance >> 5) & 0x0f),
        .hairStyleID = (BYTE)((appearance >> 10) & 0x1f),
        .hairColorID = (BYTE)((appearance >> 15) & 0x0f),
        /* Classic face IDs stop at 14, so its spare fifth bit carries facial-feature ID bit 4. */
        .facialHairStyleID = (BYTE)(((appearance >> 19) & 0x0f) | ((appearance >> 5) & 0x10)),
        .classID = (BYTE)((appearance >> 23) & 0x0f),
        .flags = (BYTE)((appearance >> 27) & 0x1f),
    };
    return unpacked;
}

static inline DWORD Wow_PackEquipment(BYTE upperBodyItem,
                                      BYTE lowerBodyItem,
                                      BYTE handItem,
                                      BYTE footItem) {
    return ((DWORD)upperBodyItem) |
           ((DWORD)lowerBodyItem << 8) |
           ((DWORD)handItem << 16) |
           ((DWORD)footItem << 24);
}

static inline wowEquipment_t Wow_UnpackEquipment(DWORD equipment) {
    wowEquipment_t unpacked = {
        .upperBodyItem = (BYTE)(equipment & 0xff),
        .lowerBodyItem = (BYTE)((equipment >> 8) & 0xff),
        .handItem = (BYTE)((equipment >> 16) & 0xff),
        .footItem = (BYTE)((equipment >> 24) & 0xff),
    };
    return unpacked;
}
#endif

#define SHADOW_RECT_STEP 4.0f

static inline BYTE ShadowPackRectComponent(FLOAT value) {
    if (value <= 0) {
        return 0;
    }
    DWORD packed = (DWORD)((value + SHADOW_RECT_STEP * 0.5f) / SHADOW_RECT_STEP);
    if (packed > 0xff) {
        packed = 0xff;
    }
    return (BYTE)packed;
}

static inline FLOAT ShadowUnpackRectComponent(BYTE packed) {
    return (FLOAT)packed * SHADOW_RECT_STEP;
}

static inline DWORD ShadowPackRect(FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    return (DWORD)ShadowPackRectComponent(x) |
           ((DWORD)ShadowPackRectComponent(y) << 8) |
           ((DWORD)ShadowPackRectComponent(w) << 16) |
           ((DWORD)ShadowPackRectComponent(h) << 24);
}

static inline void ShadowUnpackRect(DWORD packed, LPFLOAT x, LPFLOAT y, LPFLOAT w, LPFLOAT h) {
    if (x) *x = ShadowUnpackRectComponent((BYTE)(packed & 0xff));
    if (y) *y = ShadowUnpackRectComponent((BYTE)((packed >> 8) & 0xff));
    if (w) *w = ShadowUnpackRectComponent((BYTE)((packed >> 16) & 0xff));
    if (h) *h = ShadowUnpackRectComponent((BYTE)((packed >> 24) & 0xff));
}

typedef struct animation_s {
    char name[80];
    DWORD interval[2];
    FLOAT movespeed;     // movement speed of the entity while playing this animation
    DWORD flags;      // &1: non looping
    FLOAT rarity;
    DWORD syncpoint;
    FLOAT radius;
    VECTOR3 min;
    VECTOR3 max;
    DWORD damage_point;
} animation_t;

typedef struct {
    DWORD width;
    DWORD height;
} size2_t;

typedef enum {
    TE_GUNSHOT,
    TE_BLOOD,
    TE_MOVE_CONFIRMATION,
    TE_MISSILE,
    TE_FIREBOLT_IMPACT,    /* WoW: fire explosion — payload: POSITION, model SHORT */
    TE_FROSTBOLT_IMPACT,   /* WoW: frost burst — payload: POSITION, model SHORT */
    TE_ATTACK_CONFIRMATION,
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
} FRAMETYPE;

#define UIFLAG_SIZE_TO_CONTENT   (1 << 10) // flag bit; derives a composite frame's size from rendered content; used by FT_NAMETAG
#define UIFLAG_ALTERNATE_ACTIVE (1 << 11) // flag bit; secondary command state is active (for example an autocast toggle)

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
    FLOAT fov, znear, zfar, aspect;
    UIMODELPROJECTION projection;
} UIMODEL;
typedef struct UIMODEL *LPUIMODEL;
typedef const struct UIMODEL *LPCUIMODEL;

typedef struct uiFrame_s {
    DWORD number;
    DWORD parent;
    COLOR32 color;
    struct { uiFramePoints_t x, y; } points;
    struct { FLOAT width, height; } size;
    struct {
        USHORT index;
        USHORT index2;
        BYTE coord[4];  // also used as animation start timestamp
    } tex;
    union {
        struct {
            FRAMETYPE type: 8;
            BYTE alphaMode: 2;
        } flags;
        DWORD flagsvalue;
    };
    struct {
        HANDLE data;
        DWORD size;
    } buffer;
    DWORD textLength;
    DWORD stat;
    LPCSTR text; /* type-specific text; FT_COMMANDBUTTON uses this for its optional secondary click command */
    LPCSTR tooltip;
    LPCSTR onclick;
    FLOAT value;
    BYTE hotkey;
} uiFrame_t;

typedef USHORT RESOURCE;

typedef struct {
    USHORT image;
    DWORD starttime;
    DWORD endtime;
} uiBuildQueueItem_t;

typedef struct {
    USHORT image;
    USHORT entity;
} uiMultiselectItem_t;

typedef struct {
    USHORT firstitem;
    USHORT buildtimer;
    FLOAT itemoffset;
    USHORT numitems;
    uiBuildQueueItem_t items[];
} uiBuildQueue_t;

typedef struct {
    DWORD message_id;
    RESOURCE image;
    RESOURCE title_font;
    RESOURCE body_font;
    BYTE flags;
} uiMessageQueue_t;

#define UI_MESSAGE_UNREAD (1u << 0) // flag bit; server marks an unread message; used by FT_MESSAGE_QUEUE
#define UI_MESSAGE_OPEN   (1u << 1) // flag bit; server marks the message panel; used by FT_MESSAGE_QUEUE

typedef struct {
    RESOURCE hp_bar;
    RESOURCE mana_bar;
    VECTOR2 offset;
    USHORT numcolumns;
    USHORT numitems;
    uiMultiselectItem_t items[];
} uiMultiselect_t;

typedef struct {
    uiFontJustificationH_t textalignx: 4;
    uiFontJustificationV_t textaligny: 4;
    SHORT offsetx;
    SHORT offsety;
    RESOURCE font;
} uiLabel_t;

typedef struct {
    RESOURCE font;
    FLOAT inset;
} uiTextArea_t;

typedef struct {
    RESOURCE alphaFile;
    BLEND_MODE alphaMode;
} uiHighlight_t;

typedef struct {
    RESOURCE texture;
    RESOURCE font;
    BYTE texcoord[4];
    COLOR32 fontcolor;
} uiSimpleButtonState_t;

typedef struct {
    uiSimpleButtonState_t normal;
    uiSimpleButtonState_t pushed;
    uiSimpleButtonState_t disabled;
    uiSimpleButtonState_t highlight;
} uiSimpleButton_t;

typedef struct {
    SHORT CornerFlags;
    FLOAT CornerSize;
    FLOAT BackgroundSize;
    FLOAT BackgroundInsets[4];// 0.01 0.01 0.01 0.01,
    RESOURCE EdgeFile;//  "EscMenuBorder",
    RESOURCE Background;
    BOOL TileBackground:1;
    BOOL BlendAll:1;
    BOOL Mirrored:1;
} uiBackdrop_t;

/* Optional buffer for FT_TEXTURE frames that need float-precision UV or flip.
 * When present, SCR_LayoutDrawTexture uses these values instead of tex.coord. */
typedef struct {
    FLOAT l, r, t, b;   /* UV as float [0,1]; l>r or t>b = flipped axis */
    COLOR32 color;
    BLEND_MODE alphamode;
} uiTextureUV_t;

typedef struct {
    uiBackdrop_t background;
    RESOURCE font;
    FLOAT borderSize;
    COLOR32 textColor;
    COLOR32 cursorColor;
    DWORD maxChars;
} uiEditBox_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
    FLOAT border;
    FLOAT itemHeight;
    SHORT selectedIndex;
    UINAME id;
    UINAME fetchCommand;
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
    BYTE texcoord[4];
} uiScrollBarImage_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
} uiTooltip_t;

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
    FLOAT padding_x;
    FLOAT padding_y;
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
    LPSTR tok;
    LPCSTR str;
    bool reading_string;
    bool error;
    bool comma_space;
    bool equals_space;
    char token[TOKEN_LEN];
} parser_t;

typedef struct {
    DWORD itemID;
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
    DWORD level;        // (set to 1 for non hero units and items)
    DWORD str;          // strength attribute
    DWORD agi;          // agility attribute
    DWORD intel;        // intelligence attribute
    DWORD xp;           // accumulated experience points
    BOOL  suspend_xp;   // when true, XP gains are suspended
    DWORD skillpoints;  // available skill points for hero ability learning
} doodadHero_t;

typedef struct {
    DWORD slot;
    DWORD itemID;
} inventoryItem_t;

typedef struct {
    DWORD abilityID;
    DWORD active;
    DWORD level;
} modifiedAbility_t;

typedef struct {
    WORD width;
    WORD height;
    COLOR32 map[];
} pathTex_t;

struct Doodad {
    DWORD doodID;
    DWORD variation;
    VECTOR3 position;
    FLOAT angle;
    VECTOR3 scale;
    BYTE flags;
    DWORD player;
    BYTE treeLife; // integer stored in %, 100% is 0x64, 170% is 0xAA for example
    BYTE unknown1;
    BYTE unknown2;
    DWORD hitPoints; // (-1 = use default)
    DWORD manaPoints; // (-1 = use default, 0 = unit doesn't have mana)
    DWORD droppedItemSetPtr;
    DWORD num_droppedItemSets;
    DWORD goldAmount; // (default = 12500)
    FLOAT targetAcquisition; // (-1 = normal, -2 = camp)
    doodadHero_t hero;
    DWORD num_inventoryItems;
    DWORD num_modifiedAbilities;
    droppableItemSet_t *droppableItemSets;
    inventoryItem_t *inventoryItems;
    modifiedAbility_t *modifiedAbilities;
    DWORD randomUnitFlag; // "r" (for uDNR units and iDNR items)
    DWORD levelOfRandomItem; //    byte[3]: level of the random unit/item,-1 = any (this is actually interpreted as a 24-bit number)
    //    byte: item class of the random item, 0 = any, 1 = permanent ... (this is 0 for units)
    DWORD randomUnitGroupNumber; //    DWORD: unit group number (which group from the global table)
    DWORD randomUnitPositionNumber; //    DWORD: position number (which column of this group)
    DWORD num_diffAvailUnits;
    droppableItem_t *diffAvailUnits;
    COLOR32 color;
    DWORD waygate;
    DWORD unitID;
    struct Doodad *next;
};

typedef struct particle_s {
    struct particle_s *next;
    struct texture const *texture;
    VECTOR3 org;
    VECTOR3 vel;
    VECTOR3 accel;
    COLOR32 color[3];
    BYTE size[3];
    BYTE midtime;
    BYTE columns;
    BYTE rows;
    BYTE blend_mode;
    FLOAT size_value_scale;
    FLOAT size_time_scale;
    FLOAT time;
    FLOAT lifespan;
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
    BOOL visible;
    BOOL occupied;
    DWORD client;
    DWORD map_player;
    lobbySlotType_t type;
    playerRace_t race;
    DWORD team;
    DWORD color;
    UINAME name;
} lobbySlot_t;

typedef struct lobbyState_s {
    BOOL active;
    PATHSTR map_path;
    UINAME map_name;
    DWORD game_speed;
    DWORD slot_count;
    DWORD revision;
    DWORD local_slot;
    lobbySlot_t slots[MAX_PLAYERS];
} lobbyState_t;

//#define NULL 0

#endif
