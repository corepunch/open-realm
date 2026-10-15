#pragma once

#include <stdint.h>

/* Classic (RoC/TFT) and Reforged player-colour RGB palettes.
 * Numeric colour IDs stay unchanged. This palette affects UI tints only;
 * replaceable MDX TeamColorXX textures are loaded from the game data. */
#ifndef WC3_REFORGED_COLORS
#define WC3_REFORGED_COLORS 1
#endif

#define WC3_CLASSIC_COLOR_COUNT 12u
#define WC3_REFORGED_COLOR_COUNT 24u

static inline uint32_t WC3_PlayerColorRGB(uint32_t index) {
#if WC3_REFORGED_COLORS
    static uint32_t const rgb[WC3_REFORGED_COLOR_COUNT] = {
        0xF80402u,         0x0041F8u,         0x1AE4B5u,         0x52007Du,
        0xF8F900u,         0xF8880Du,         0x20BD00u,         0xDD5AAAu,
        0x909492u,         0x7ABDEAu,         0x106145u,         0x4D2A05u,
        0x980000u,         0x0000BDu,         0x00E8F8u,         0xB800F8u,
        0xE5CA82u,         0xF0A288u,         0xBAFC7Du,         0xD5B6E5u,
        0x4D4E52u,         0xE5EDF8u,         0x00761Du,         0xA06E32u,
    };
    return rgb[index % WC3_REFORGED_COLOR_COUNT];
#else
    static uint32_t const rgb[WC3_CLASSIC_COLOR_COUNT] = {
        0xFF0303u,         0x0042FFu,         0x1CE6B9u,         0x540081u,
        0xFFFC01u,         0xFEBA0Eu,         0x20C000u,         0xE55BB0u,
        0x959697u,         0x7EBFF1u,         0x106246u,         0x4E2A04u,
    };
    return rgb[index % WC3_CLASSIC_COLOR_COUNT];
#endif
}
