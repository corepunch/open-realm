#include "g_local.h"

#define RESOURCE_TEXT_WORLD_Z_OFFSET 10.0f /* world units; current Warsmash TextTag constructor lift */
#define RESOURCE_TEXT_VELOCITY_X 0.0f      /* screen pixels per second; current Warsmash built-in motion */
#define RESOURCE_TEXT_VELOCITY_Y 60.0f     /* screen pixels per second; positive values rise on the client */
#define RESOURCE_TEXT_FONT_SCALE 500.0f    /* px per WC3 UI-height unit; Warsmash uses TextHeight * 0.5 */

/* These values are only fallbacks when the active Warcraft data omits the
 * corresponding Misc fields. Stock data remains authoritative when present. */
typedef struct {
    LPCSTR name;
    COLOR32 fallback_color;
    FLOAT fallback_lifetime;   /* seconds */
    FLOAT fallback_fade_start; /* seconds */
    FLOAT fallback_height;     /* WC3 UI units */
} resourceTextStyle_t;

static BYTE resource_text_byte(int value) {
    return (BYTE)MAX(0, MIN(255, value));
}

/* Warcraft Misc *TextColor fields are authored as alpha, red, green, blue. */
static COLOR32 resource_text_color(LPCSTR field, COLOR32 fallback) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", field);
    int a, r, g, b;

    if (!value || sscanf(value, "%d,%d,%d,%d", &a, &r, &g, &b) != 4)
        return fallback;
    return MAKE(COLOR32,
        resource_text_byte(r), resource_text_byte(g),
        resource_text_byte(b), resource_text_byte(a));
}

static FLOAT resource_text_float(LPCSTR field, FLOAT fallback) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", field);
    return value && *value ? (FLOAT)atof(value) : fallback;
}

static DWORD resource_text_color_bits(COLOR32 color) {
    return (DWORD)color.r | ((DWORD)color.g << 8) |
           ((DWORD)color.b << 16) | ((DWORD)color.a << 24);
}

static BOOL resource_text_style(DWORD resource_state, resourceTextStyle_t *style) {
    if (!style) return false;
    switch (resource_state) {
        case PLAYERSTATE_RESOURCE_GOLD:
            *style = MAKE(resourceTextStyle_t,
                .name = "Gold",
                .fallback_color = MAKE(COLOR32, 255, 220, 0, 255),
                .fallback_lifetime = 2.0f,
                .fallback_fade_start = 1.0f,
                .fallback_height = 0.024f);
            return true;
        case PLAYERSTATE_RESOURCE_LUMBER:
            *style = MAKE(resourceTextStyle_t,
                .name = "Lumber",
                .fallback_color = MAKE(COLOR32, 0, 200, 80, 255),
                .fallback_lifetime = 2.0f,
                .fallback_fade_start = 1.0f,
                .fallback_height = 0.024f);
            return true;
        default:
            return false;
    }
}

void G_ResourceGainEvent(LPEDICT source, DWORD resource_state, LONG amount) {
    resourceTextStyle_t style;
    char field[64], text[32];
    VECTOR3 origin;
    COLOR32 color;
    FLOAT lifetime, fade_start, height;
    DWORD color_bits, lifetime_ms, fade_start_ms, font_size;
    LONG font;

    if (!source || amount <= 0 || !resource_text_style(resource_state, &style)) return;
    if (!gi.Write || !gi.multicast || !gi.FontIndex) return;

    snprintf(field, sizeof(field), "%sTextColor", style.name);
    color = resource_text_color(field, style.fallback_color);
    snprintf(field, sizeof(field), "%sTextLifetime", style.name);
    lifetime = resource_text_float(field, style.fallback_lifetime);
    snprintf(field, sizeof(field), "%sTextFadeStart", style.name);
    fade_start = resource_text_float(field, style.fallback_fade_start);
    snprintf(field, sizeof(field), "%sTextHeight", style.name);
    height = resource_text_float(field, style.fallback_height);

    if (lifetime <= 0.0f || height <= 0.0f) return;
    fade_start = MAX(0.0f, MIN(fade_start, lifetime));
    lifetime_ms = (DWORD)(lifetime * 1000.0f + 0.5f);
    fade_start_ms = (DWORD)(fade_start * 1000.0f + 0.5f);
    font_size = (DWORD)MAX(1.0f, height * RESOURCE_TEXT_FONT_SCALE + 0.5f);
    font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), font_size);
    if (font <= 0 || font >= MAX_FONTSTYLES) return;

    snprintf(text, sizeof(text), "+%d", (int)amount);
    origin = source->s.origin;
    origin.z += RESOURCE_TEXT_WORLD_Z_OFFSET;
    color_bits = resource_text_color_bits(color);

    gi.Write(PF_BYTE, &(LONG){ svc_temp_entity });
    gi.Write(PF_BYTE, &(LONG){ TE_FLOATING_TEXT });
    gi.Write(PF_POSITION, &origin);
    gi.Write(PF_STRING, text);
    gi.Write(PF_LONG, &(LONG){ (LONG)color_bits });
    gi.Write(PF_SHORT, &font);
    gi.Write(PF_LONG, &(LONG){ (LONG)lifetime_ms });
    gi.Write(PF_LONG, &(LONG){ (LONG)fade_start_ms });
    gi.Write(PF_FLOAT, &(FLOAT){ RESOURCE_TEXT_VELOCITY_X });
    gi.Write(PF_FLOAT, &(FLOAT){ RESOURCE_TEXT_VELOCITY_Y });

    /* Current Warsmash accepts a player index for resource tags but drops it
     * before rendering, so this parity path intentionally has no owner filter. */
    gi.multicast(&origin, MULTICAST_ALL);
}
