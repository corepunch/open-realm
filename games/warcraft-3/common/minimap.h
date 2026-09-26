#ifndef WC3_MINIMAP_H
#define WC3_MINIMAP_H

#include "common/shared.h"

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

static inline wc3MinimapContact_t wc3_minimap_contact_get(uint16_t flags) {
    return (wc3MinimapContact_t)EFX_GAME_VARIANT_GET(flags);
}

static inline uint16_t wc3_minimap_contact_set(uint16_t flags, wc3MinimapContact_t contact) {
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

#endif
