#ifndef WC3_MINIMAP_RENDER_H
#define WC3_MINIMAP_RENDER_H

#include "games/warcraft-3/common/minimap.h"
#include "common/mapinfo.h"
#include "common/stb_slk.h"

/* World overlays use the centered, aspect-preserving area of WC3's square
 * minimap texture. */
static inline RECT WC3_MinimapContentRect(LPCRECT frame, LPCVECTOR2 map_size) {
    RECT content = frame ? *frame : (RECT){ 0 };
    FLOAT world_size;
    if (!frame || !map_size || map_size->x <= 0.0f || map_size->y <= 0.0f) return content;
    world_size = MAX(map_size->x, map_size->y);
    content.w = frame->w * (map_size->x / world_size);
    content.h = frame->h * (map_size->y / world_size);
    content.x = frame->x + (frame->w - content.w) * 0.5f;
    content.y = frame->y + (frame->h - content.h) * 0.5f;
    return content;
}

/* Capture-calibrated sizes are UI-canvas units, not pixels. */
static inline VECTOR2 wc3_minimap_marker_size(wc3MinimapContact_t contact) {
    FLOAT size;
    switch (contact) {
    case WC3_MINIMAP_CONTACT_UNIT: size = 0.002f; break;
    case WC3_MINIMAP_CONTACT_BUILDING: size = 0.005f; break;
    case WC3_MINIMAP_CONTACT_HERO: size = 0.014f; break;
    case WC3_MINIMAP_CONTACT_GOLD_MINE:
    case WC3_MINIMAP_CONTACT_GOLD_ENTANGLED:
    case WC3_MINIMAP_CONTACT_GOLD_HAUNTED:
    case WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING: size = 0.0105f; break;
    default: size = 0.0f; break;
    }
    return MAKE(VECTOR2, size, size);
}

static inline RECT wc3_minimap_marker_rect(VECTOR2 const *point, wc3MinimapContact_t contact) {
    VECTOR2 const size = wc3_minimap_marker_size(contact);
    return point ? MAKE(RECT, point->x - size.x * 0.5f, point->y - size.y * 0.5f, size.x, size.y) : (RECT){ 0 };
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
    BOOL hostile;
} wc3MinimapColorParams_t;

/* Selection's EF_NEUTRAL includes passive allies. Minimap relationship colors
 * classify only neutral player slots as black, enemies as red, and other owners
 * as teal. */
static inline wc3MinimapColorKind_t wc3_minimap_ordinary_color_kind(wc3MinimapColorParams_t const *p) {
    if (!p) return WC3_MINIMAP_COLOR_TEAM;
    if (p->owner == p->viewer) return WC3_MINIMAP_COLOR_SELF_WHITE;
    if (p->filter < WC3_MINIMAP_ALLY_COLOR_MINIMAP) return WC3_MINIMAP_COLOR_TEAM;
    if (p->owner >= PLAYER_NEUTRAL_AGGRESSIVE && p->owner < MAX_PLAYERS)
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

static inline LPCSTR wc3_minimap_skin_texture_path(
    stbIniCache_t const *theme, stbIniCache_t const *map_skin, LPCSTR key)
{
    LPCSTR value;
    if (!key || !*key) return NULL;
    value = map_skin ? Stb_IniCacheFind(map_skin, "CustomSkin", key) : NULL;
    if (value) return value;
    return theme ? Stb_IniCacheFind(theme, "Default", key) : NULL;
}

typedef struct {
    wc3MinimapContact_t contact;
    const char *key, *path;
    BOOL map_override;
} wc3MinimapSpecialAsset_t;

typedef void *(*wc3MinimapTextureLoadFn)(void *context, LPCSTR path);

static inline void *wc3_minimap_register_special_asset(
    wc3MinimapSpecialAsset_t const *asset, void *placeholder, void *context,
    wc3MinimapTextureLoadFn load_pinned, wc3MinimapTextureLoadFn load_map_scoped)
{
    wc3MinimapTextureLoadFn const loader = asset && asset->map_override ? load_map_scoped : load_pinned;
    if (!asset || !asset->path || !*asset->path) return placeholder;
    return loader ? loader(context, asset->path) : placeholder;
}

static inline DWORD wc3_minimap_special_assets(
    stbIniCache_t const *theme, stbIniCache_t const *map_skin,
    wc3MinimapSpecialAsset_t *assets, DWORD capacity)
{
    DWORD count = 0;
    for (wc3MinimapContact_t contact = WC3_MINIMAP_CONTACT_HERO;
         contact <= WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING; contact++) {
        LPCSTR const key = wc3_minimap_skin_key(contact);
        LPCSTR const override = key && map_skin ? Stb_IniCacheFind(map_skin, "CustomSkin", key) : NULL;
        if (assets && count < capacity) assets[count] = (wc3MinimapSpecialAsset_t){
            contact, key, wc3_minimap_skin_texture_path(theme, map_skin, key), override && *override
        };
        count++;
    }
    return count;
}

#endif
