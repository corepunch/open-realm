#ifndef SC2_MINIMAP_H
#define SC2_MINIMAP_H
#include "common/shared.h"
enum { SC2_MINIMAP_HIDE_TERRAIN=1, SC2_MINIMAP_ALLIANCE_COLORS=2 };
enum { SC2_MINIMAP_NONE, SC2_MINIMAP_SELF, SC2_MINIMAP_ALLY, SC2_MINIMAP_ENEMY, SC2_MINIMAP_NEUTRAL };
static inline color32_t SC2_MinimapContactColor(uint32_t contact) {
    static color32_t const colors[]={ {0,0,0,0}, {255,255,255,255}, {32,220,96,255}, {255,48,48,255}, {200,200,32,255} };
    return contact<(sizeof(colors)/sizeof(*colors)) ? colors[contact] : colors[0];
}
#endif
