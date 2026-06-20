#ifndef shared_h
#define shared_h

//#define DEBUG_PATHFINDING

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

#define MAX_PATHLEN 256
#define MAX_PLAYERS 16
#define MAX_SELECTED_ENTITIES 64
#define TOKEN_LEN 1024
#define FRAMETIME 100
#define MAX_LAYOUT_OBJECTS 0xffff
#define MAX_LIST_FETCH_TEXT 2048
#define MAX_LIST_FETCH_ROWS 32
#define MIN(x, y) (((x)<(y))?(x):(y))
#define MAX(x, y) (((x)>(y))?(x):(y))

#define BYTE2FLOAT(x) ((x)/255.f)

#define IS_FOURCC(STRING) (STRING && strlen(STRING) == 4)

#define MAKE(TYPE,...)(TYPE){__VA_ARGS__}

#define COLOR32_WHITE MAKE(COLOR32,255,255,255,255)
#define COLOR32_BLACK MAKE(COLOR32,0,0,0,255)

#ifndef __cplusplus
  #include <stdbool.h>
#endif

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

#define MAKEFOURCC(ch0, ch1, ch2, ch3) ((int)(char)(ch0) | ((int)(char)(ch1) << 8) | ((int)(char)(ch2) << 16) | ((int)(char)(ch3) << 24))

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
};

enum {
    FLAG(EF_GROUND_ANCHOR, 0),
    FLAG(EF_FOW_BLOCKER, 1),
    FLAG(EF_FOW_REVEALER, 2),
};

enum {
    FLAG(RDF_NOFOG, 0),
    FLAG(RDF_NOFOGMASK, 1),
    FLAG(RDF_NOWORLDMODEL, 2),
    FLAG(RDF_NOFRUSTUMCULL, 3),
    FLAG(RDF_USE_ENTITY_CAMERA, 4),
};

#define MAX_COMMANDS 12
#define MAX_STATS 32

#define MAX_PACKET_ENTITIES 256
#define MAX_GAME_ENTITIES 16384
#define MAX_CLIENTS 24
#define MAX_MODELS 256
#define MAX_FONTSTYLES 256
#define MAX_SOUNDS 256
#define MAX_IMAGES 256
#define MAX_DYNAMIC_IMAGES 32
#define MAX_ITEMS 256
#define MAX_GENERAL (MAX_CLIENTS*2)

enum {
    CS_NAME = 0,
    CS_CDTRACK = 1,
    CS_SKY = 2,
    CS_SKYAXIS = 3,        // %f %f %f format
    CS_SKYROTATE = 4,
    CS_STATUSBAR = 5,        // display program string
    CS_HEALTHBAR = 6,
    CS_MANAHBAR = 6,
    CS_WORLD = 7,
    CS_AIRACCEL = 29,        // air acceleration control
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
} UILAYOUTLAYER;

typedef enum {
    CLIENT_UI_GAME,
    CLIENT_UI_LOADING,
    CLIENT_UI_CINEMATIC,
} CLIENTUISTATE;

struct playerState_s {
    DWORD number;
    QUATERNION viewquat;
    VECTOR3 viewangles;
    VECTOR2 origin;
    FLOAT distance;
    DWORD fov;      /* vertical field of view in degrees */
    DWORD rdflags;
    DWORD uiflags;
    DWORD client_ui_state;
    DWORD team;
    DWORD color;    // player color index (0 = red, 1 = blue, … see PLAYER_COLOR_*)
    DWORD race;     // map player race, see playerRace_t
    LPSTR name;     // player display name (set from mapplayer or by script)
    LONG  start_location; // start location index assigned to this player (-1 = none)
    FLOAT cinefade;
    USHORT stats[MAX_STATS];
    LPCSTR texts[MAX_STATS];
};

typedef struct entityState_s {
    DWORD number; // edict index
    DWORD class_id;
    union {
        VECTOR3 origin;
        struct { VECTOR2 origin2; FLOAT z; };
    };
    FLOAT angle;
    VECTOR3 rotation;
    FLOAT scale;
    FLOAT radius;
    BYTE stats[ENT_STAT_COUNT];
    DWORD player;
    DWORD model;
    DWORD model2;
#ifdef WOW
    DWORD appearance;
    DWORD equipment;
#endif
    DWORD image;
    DWORD sound;
    DWORD frame;
    DWORD event;
    USHORT flags;
    BYTE renderfx;
    BYTE ability;
    DWORD splat;
    DWORD shadow;
    DWORD shadow_rect;
} entityState_t;

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
           ((DWORD)(faceID & 0x1f) << 5) |
           ((DWORD)(hairStyleID & 0x1f) << 10) |
           ((DWORD)(hairColorID & 0x0f) << 15) |
           ((DWORD)(facialHairStyleID & 0x0f) << 19) |
           ((DWORD)(classID & 0x0f) << 23) |
           ((DWORD)(flags & 0x1f) << 27);
}

static inline wowAppearance_t Wow_UnpackAppearance(DWORD appearance) {
    wowAppearance_t unpacked = {
        .skinColorID = (BYTE)(appearance & 0x1f),
        .faceID = (BYTE)((appearance >> 5) & 0x1f),
        .hairStyleID = (BYTE)((appearance >> 10) & 0x1f),
        .hairColorID = (BYTE)((appearance >> 15) & 0x0f),
        .facialHairStyleID = (BYTE)((appearance >> 19) & 0x0f),
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
    FT_MULTISELECT,
    FT_TOOLTIPTEXT,
} FRAMETYPE;

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

#define UI_FRAMEPOINT_SCALE 32767.0
#define UI_PARENT 255

typedef struct { // serialized as 4 bytes
    uiFramePointPos_t targetPos: 7;
    uint8_t used: 1;
    uint8_t relativeTo: 8;
    int16_t offset: 16;
} uiFramePoint_t;

typedef uiFramePoint_t uiFramePoints_t[FPP_COUNT];

/* UI interaction flags — shared between engine and game UI */
#define UIFLAG_PRESSED  (1 << 0)
#define UIFLAG_HOVERED  (1 << 1)
#define UIFLAG_CHECKED  (1 << 2)
#define UIFLAG_HIDDEN   (1 << 3)
#define UIFLAG_DISABLED (1 << 4)

/* Base frame struct — engine defines, game extends (edict_s pattern).
 * Game structs embed this as their first member and add fields below
 * a "keep above in sync" comment. Generic code operates on this. */
struct uiBaseFrame_s;
typedef struct uiBaseFrame_s {
    DWORD number;
    FRAMETYPE type;
    void *parent;                   /* game resolves: pointer or index */
    COLOR32 color;
    FLOAT alpha;
    struct { FLOAT width, height; } size;
    BOOL hidden;
    BOOL disabled;
    DWORD image;                    /* primary texture/resource index */
    RECT texcoord;                  /* texture UV rect */
    LPCSTR text;
    COLOR32 text_color;
    struct {                        /* backdrop */
        DWORD bg;
        DWORD edge;
        BOOL tile;
        FLOAT insets[4];
    } backdrop;
    DWORD ui_flags;                 /* UIFLAG_* bitmask */
    void (*on_event)(struct uiBaseFrame_s *frame, FLOAT x, FLOAT y, int button, BOOL down);
    void (*on_draw)(struct uiBaseFrame_s *frame, LPCRECT rect);
} uiBaseFrame_t;

typedef uiBaseFrame_t *LPUIBASEFRAME;
typedef uiBaseFrame_t const *LPCUIBASEFRAME;

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
    LPCSTR text;
    LPCSTR tooltip;
    LPCSTR onclick;
    FLOAT value;
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

typedef struct {
    uiBackdrop_t background;
    uiLabel_t text;
} uiTooltip_t;

typedef struct {
    uiBackdrop_t normal;
    uiBackdrop_t pushed;
    uiBackdrop_t disabled;
    uiBackdrop_t disabledPushed;
    uiHighlight_t highlight;
    VECTOR2 pushedTextOffset;
} uiGlueTextButton_t;

typedef struct sheetField_s {
    LPCSTR name, value;
    struct sheetField_s *next;
} sheetField_t;

typedef struct sheetRow_s {
    LPCSTR name;
    struct sheetField_s *fields;
    struct sheetRow_s *next;
} sheetRow_t;

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
