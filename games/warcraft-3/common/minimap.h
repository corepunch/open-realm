#ifndef WC3_MINIMAP_H
#define WC3_MINIMAP_H

#include "common/shared.h"
#include "common/mapinfo.h"
#include "common/stb_slk.h"

/* Warcraft draws the authored minimap texture across the full square frame,
 * but projects world-space content through a centred aspect-preserving area.
 * This matches Warsmash's minimapFilledArea contract for rectangular maps. */
static inline RECT WC3_MinimapContentRect(LPCRECT frame, LPCVECTOR2 map_size) {
    RECT content = frame ? *frame : (RECT){ 0 };
    FLOAT world_size;

    if (!frame || !map_size || map_size->x <= 0.0f || map_size->y <= 0.0f) {
        return content;
    }

    world_size = MAX(map_size->x, map_size->y);
    content.w = frame->w * (map_size->x / world_size);
    content.h = frame->h * (map_size->y / world_size);
    content.x = frame->x + (frame->w - content.w) * 0.5f;
    content.y = frame->y + (frame->h - content.h) * 0.5f;
    return content;
}

/* entityState_t.effect_flags bits 13-15 are deliberately generic. WC3 owns
 * their minimap-contact interpretation on both sides of the game/renderer
 * boundary; shared client code transports the value without decoding it. */
typedef enum {
    WC3_MINIMAP_CONTACT_NONE = 0,
    WC3_MINIMAP_CONTACT_UNIT,
    WC3_MINIMAP_CONTACT_BUILDING,
    WC3_MINIMAP_CONTACT_HERO,
    WC3_MINIMAP_CONTACT_GOLD_MINE,
    WC3_MINIMAP_CONTACT_GOLD_ENTANGLED,
    WC3_MINIMAP_CONTACT_GOLD_HAUNTED,
    WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING,
} wc3MinimapContact_t;

_Static_assert(WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING <= 7,
               "WC3 automatic minimap contacts must fit the generic three-bit presentation variant");

static inline wc3MinimapContact_t wc3_minimap_contact_get(USHORT flags) {
    return (wc3MinimapContact_t)EFX_GAME_VARIANT_GET(flags);
}

static inline USHORT wc3_minimap_contact_set(USHORT flags, wc3MinimapContact_t contact) {
    return EFX_GAME_VARIANT_SET(flags, contact);
}

/* WC3 assigns the generic game-owned local presentation variant to the
 * minimap ally-colour filter. Shared/client code copies the opaque value only. */
enum { WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR = UI_PLAYERSTAT_GAME_VARIANT };

typedef enum {
    WC3_MINIMAP_ALLY_COLOR_PLAYERS = 0,
    WC3_MINIMAP_ALLY_COLOR_MINIMAP = 1,
    WC3_MINIMAP_ALLY_COLOR_WORLD = 2,
} wc3MinimapAllyColorMode_t;

/* Capture-calibrated sizes are UI-canvas units, not pixels. Keep the values
 * next to the classification switch so raw reference pixels cannot be passed
 * directly to R_DrawImage again. */
static inline VECTOR2 wc3_minimap_marker_size(wc3MinimapContact_t contact) {
    FLOAT size;
    switch (contact) {
    case WC3_MINIMAP_CONTACT_UNIT: size = 0.002f; break;
    case WC3_MINIMAP_CONTACT_BUILDING: size = 0.005f; break;
    case WC3_MINIMAP_CONTACT_HERO: size = 0.014f; break;
    case WC3_MINIMAP_CONTACT_GOLD_MINE:
    case WC3_MINIMAP_CONTACT_GOLD_ENTANGLED:
    case WC3_MINIMAP_CONTACT_GOLD_HAUNTED:
    case WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING:
        size = 0.0105f;
        break;
    default:
        size = 0.0f;
        break;
    }
    return MAKE(VECTOR2, size, size);
}

typedef enum {
    WC3_MINIMAP_COLOR_SELF_WHITE,
    WC3_MINIMAP_COLOR_TEAM,
    WC3_MINIMAP_COLOR_ALLY_TEAL,
    WC3_MINIMAP_COLOR_ENEMY_RED,
    WC3_MINIMAP_COLOR_NEUTRAL_BLACK,
} wc3MinimapColorKind_t;

typedef struct {
    DWORD owner, viewer, filter;
    BOOL hostile, neutral;
} wc3MinimapColorParams_t;

/* Ordinary-contact colour policy is local presentation state. In player-colour
 * mode the viewer is still white; in ally-colour modes other contacts collapse
 * to ally/enemy/neutral relationship colours. */
static inline wc3MinimapColorKind_t wc3_minimap_ordinary_color_kind(wc3MinimapColorParams_t const *p) {
    if (!p) return WC3_MINIMAP_COLOR_TEAM;
    if (p->owner == p->viewer) return WC3_MINIMAP_COLOR_SELF_WHITE;
    if (p->filter < WC3_MINIMAP_ALLY_COLOR_MINIMAP) return WC3_MINIMAP_COLOR_TEAM;
    if (p->neutral || (p->owner >= PLAYER_NEUTRAL_AGGRESSIVE && p->owner < MAX_PLAYERS))
        return WC3_MINIMAP_COLOR_NEUTRAL_BLACK;
    if (p->hostile) return WC3_MINIMAP_COLOR_ENEMY_RED;
    return WC3_MINIMAP_COLOR_ALLY_TEAL;
}

static inline LPCSTR wc3_minimap_skin_key(wc3MinimapContact_t contact) {
    switch (contact) {
    case WC3_MINIMAP_CONTACT_HERO: return "MinimapHeroTexture";
    case WC3_MINIMAP_CONTACT_GOLD_MINE: return "MinimapResourceTexture";
    case WC3_MINIMAP_CONTACT_GOLD_ENTANGLED: return "MinimapEntangledResourceTexture";
    case WC3_MINIMAP_CONTACT_GOLD_HAUNTED: return "MinimapHauntedResourceTexture";
    case WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING: return "MinimapNeutralTexture";
    default: return NULL;
    }
}

/* Minimap special textures are Game Interface fields. Map CustomSkin overrides
 * the stock Default field. Keeping this lookup pure lets game-side tests cover
 * precedence without coupling the shared renderer to WC3 names. */
static inline LPCSTR wc3_minimap_skin_texture_path(
    stbIniCache_t const *theme, stbIniCache_t const *map_skin, LPCSTR key)
{
    LPCSTR value;
    if (!key || !*key) return NULL;
    value = map_skin ? Stb_IniCacheFind(map_skin, "CustomSkin", key) : NULL;
    if (value) return value;
    return theme ? Stb_IniCacheFind(theme, "Default", key) : NULL;
}

#endif
