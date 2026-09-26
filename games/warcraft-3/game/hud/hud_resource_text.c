#include "../g_local.h"

#define RESOURCE_TEXT_WORLD_Z_OFFSET 10.0f /* world units; current Warsmash TextTag constructor lift */
#define RESOURCE_TEXT_VELOCITY_X 0.0f      /* screen pixels per second; current Warsmash built-in motion */
#define RESOURCE_TEXT_VELOCITY_Y 60.0f     /* screen pixels per second; positive values rise on the client */
#define RESOURCE_TEXT_FONT_SCALE 500.0f    /* px per WC3 UI-height unit; Warsmash uses TextHeight * 0.5 */

/* These values are only fallbacks when the active Warcraft data omits the
 * corresponding Misc fields. Stock data remains authoritative when present. */
typedef struct {
    cstring_t name;
    color32_t fallback_color;
    float fallback_lifetime;   /* seconds */
    float fallback_fade_start; /* seconds */
    float fallback_height;     /* WC3 UI units */
} resourceTextStyle_t;

static uint8_t resource_text_byte(int value) {
    return (uint8_t)MAX(0, MIN(255, value));
}

/* Warcraft Misc *TextColor fields are authored as alpha, red, green, blue. */
static color32_t resource_text_color(cstring_t field, color32_t fallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", field);
    int a, r, g, b;

    if (!value || sscanf(value, "%d,%d,%d,%d", &a, &r, &g, &b) != 4)
        return fallback;
    return MAKE(color32_t,
        resource_text_byte(r), resource_text_byte(g),
        resource_text_byte(b), resource_text_byte(a));
}

static float resource_text_float(cstring_t field, float fallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", field);
    return value && *value ? (float)atof(value) : fallback;
}

static uint32_t resource_text_color_bits(color32_t color) {
    return (uint32_t)color.r | ((uint32_t)color.g << 8) |
           ((uint32_t)color.b << 16) | ((uint32_t)color.a << 24);
}

static bool resource_text_style(uint32_t resource_state, resourceTextStyle_t *style) {
    if (!style) return false;
    switch (resource_state) {
        case PLAYERSTATE_RESOURCE_GOLD:
            *style = MAKE(resourceTextStyle_t,
                .name = "Gold",
                .fallback_color = MAKE(color32_t, 255, 220, 0, 255),
                .fallback_lifetime = 2.0f,
                .fallback_fade_start = 1.0f,
                .fallback_height = 0.024f);
            return true;
        case PLAYERSTATE_RESOURCE_LUMBER:
            *style = MAKE(resourceTextStyle_t,
                .name = "Lumber",
                .fallback_color = MAKE(color32_t, 0, 200, 80, 255),
                .fallback_lifetime = 2.0f,
                .fallback_fade_start = 1.0f,
                .fallback_height = 0.024f);
            return true;
        default:
            return false;
    }
}

void G_ResourceGainEvent(edict_t *source, uint32_t resource_state, int32_t amount) {
    resourceTextStyle_t style;
    char field[64], text[32];
    vec3_t origin;
    color32_t color;
    float lifetime, fade_start, height;
    uint32_t color_bits, lifetime_ms, fade_start_ms, font_size;
    int32_t font;

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
    lifetime_ms = (uint32_t)(lifetime * 1000.0f + 0.5f);
    fade_start_ms = (uint32_t)(fade_start * 1000.0f + 0.5f);
    font_size = (uint32_t)MAX(1.0f, height * RESOURCE_TEXT_FONT_SCALE + 0.5f);
    font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), font_size);
    if (font <= 0 || font >= MAX_FONTSTYLES) return;

    snprintf(text, sizeof(text), "+%d", (int)amount);
    origin = source->s.origin;
    origin.z += RESOURCE_TEXT_WORLD_Z_OFFSET;
    color_bits = resource_text_color_bits(color);

    gi.Write(PF_BYTE, &(int32_t){ svc_temp_entity });
    gi.Write(PF_BYTE, &(int32_t){ TE_FLOATING_TEXT });
    gi.Write(PF_POSITION, &origin);
    gi.Write(PF_STRING, text);
    gi.Write(PF_LONG, &(int32_t){ (int32_t)color_bits });
    gi.Write(PF_SHORT, &font);
    gi.Write(PF_LONG, &(int32_t){ (int32_t)lifetime_ms });
    gi.Write(PF_LONG, &(int32_t){ (int32_t)fade_start_ms });
    gi.Write(PF_FLOAT, &(float){ RESOURCE_TEXT_VELOCITY_X });
    gi.Write(PF_FLOAT, &(float){ RESOURCE_TEXT_VELOCITY_Y });

    /* Current Warsmash accepts a player index for resource tags but drops it
     * before rendering, so this parity path intentionally has no owner filter. */
    gi.multicast(&origin, MULTICAST_ALL);
}
