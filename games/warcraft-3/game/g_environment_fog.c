#include "g_local.h"
#include "common/stb_slk.h"

/* Read one value from a versioned comma-separated MiscData field. */
static BOOL G_EnvironmentFogListValue(LPCSTR value, DWORD index, LPFLOAT out) {
    char *stop = NULL;
    FLOAT parsed = 0.0f;

    if (!value || !out) return false;
    FOR_LOOP(i, index + 1) {
        while (*value == ' ' || *value == '\t') value++;
        if (!*value) return false;
        parsed = strtof(value, &stop);
        if (!stop || stop == value) return false;
        value = stop;
        while (*value == ' ' || *value == '\t') value++;
        if (i == index) {
            *out = parsed;
            return true;
        }
        if (*value != ',') return false;
        value++;
    }
    return false;
}

/* [DefaultZFog] is a singleton (index 0); [MenuZFog] is versioned. Try the expansion cell, retry the RoC cell. */
static BOOL G_EnvironmentFogVersionedValue(LPCSTR value, DWORD index, DWORD fallback, LPFLOAT out) {
    if (G_EnvironmentFogListValue(value, index, out)) return true;
    return index != fallback && G_EnvironmentFogListValue(value, fallback, out);
}

BOOL G_EnvironmentFogDefault(wc3EnvironmentFogState_t *fog) {
    if (!fog) return false;
    *fog = (wc3EnvironmentFogState_t){ .style = WC3_ENV_FOG_NONE };
    DWORD const version = atoi(gi.CvarString("fs_expansion", "0")) != 0 ? 1u : 0u;
    LPCSTR style_value = Stb_IniCacheFind(&game.config.misc, "DefaultZFog", "Style");
    LPCSTR start_value = Stb_IniCacheFind(&game.config.misc, "DefaultZFog", "Start");
    LPCSTR end_value = Stb_IniCacheFind(&game.config.misc, "DefaultZFog", "End");
    LPCSTR density_value = Stb_IniCacheFind(&game.config.misc, "DefaultZFog", "Density");
    LPCSTR color_value = Stb_IniCacheFind(&game.config.misc, "DefaultZFog", "Color");
    FLOAT style = -1.0f;
    FLOAT alpha = 255.0f, red = 0.0f, green = 0.0f, blue = 0.0f;

    if (!G_EnvironmentFogVersionedValue(style_value, version, 0, &style)) {
        fprintf(stderr, "WC3: DefaultZFog.Style is missing version %u\n", (unsigned)version);
        return false;
    }

    /* FogSettings.parse() maps the authored -1/0/1/2 style through +1 to
     * NONE/LINEAR/EXP/EXP2.  Invalid values are disabled. */
    if (style < -1.0f || style > 2.0f) {
        fprintf(stderr, "WC3: DefaultZFog.Style %.3f is invalid\n", style);
        return false;
    }
    if (!G_EnvironmentFogVersionedValue(start_value, version, 0, &fog->start) ||
        !G_EnvironmentFogVersionedValue(end_value, version, 0, &fog->end) ||
        !G_EnvironmentFogVersionedValue(density_value, version, 0, &fog->density) ||
        !G_EnvironmentFogVersionedValue(color_value, version * 4u + 0u, 0u, &alpha) ||
        !G_EnvironmentFogVersionedValue(color_value, version * 4u + 1u, 1u, &red) ||
        !G_EnvironmentFogVersionedValue(color_value, version * 4u + 2u, 2u, &green) ||
        !G_EnvironmentFogVersionedValue(color_value, version * 4u + 3u, 3u, &blue)) {
        fprintf(stderr, "WC3: DefaultZFog is incomplete for version %u\n", (unsigned)version);
        return false;
    }
    fog->style = (LONG)style + 1;
    (void)alpha; /* viewDef currently carries RGB only. */
    fog->color = (VECTOR3){ red / 255.0f, green / 255.0f, blue / 255.0f };
    return true;
}

/* Publish authoritative fog state through the generic scene-presentation contract. */
void G_EnvironmentFogPublish(void) {
    wc3EnvironmentFogState_t const *fog = &level.environment_fog.active;
    char value[MAX_PATHLEN];

    /* Keep style/density on the wire even though the generic renderer currently
     * renders all enabled styles with its Warsmash-compatible linear path. */
    snprintf(value, sizeof(value), "%d %.9g %.9g %.9g %.9g %.9g %.9g",
             fog->style,
             (double)fog->start, (double)fog->end, (double)fog->density,
             (double)fog->color.x, (double)fog->color.y, (double)fog->color.z);
    gi.configstring(CS_SCENE_FOG, value);
}

/* Load the map's reset target while starting the active scene unfogged. */
void G_EnvironmentFogInitMap(void) {
    level.environment_fog.defaults_valid = G_EnvironmentFogDefault(&level.environment_fog.defaults);
    level.environment_fog.active = (wc3EnvironmentFogState_t){ .style = WC3_ENV_FOG_NONE };
    G_EnvironmentFogPublish();
}

/* Apply one script-authored terrain-fog update and publish it to connected clients. */
void G_EnvironmentFogSet(wc3EnvironmentFogParams_t const *params) {
    wc3EnvironmentFogState_t *fog = &level.environment_fog.active;

    if (!params) return;
    fog->start = params->start;
    fog->end = params->end;
    fog->density = params->density;
    fog->color = params->color;
    if (params->style < -1 || params->style > 2)
        fprintf(stderr, "WC3: SetTerrainFogEx style %d is invalid; disabling scene fog\n", params->style);
    fog->style = params->style >= -1 && params->style <= 2 ? params->style + 1 : WC3_ENV_FOG_NONE;
    G_EnvironmentFogPublish();
}

/* Restore the map-authored DefaultZFog state when the authoritative row exists. */
void G_EnvironmentFogReset(void) {
    if (!level.environment_fog.defaults_valid) {
        fprintf(stderr, "WC3: ResetTerrainFog ignored because DefaultZFog is unavailable\n");
        return;
    }
    level.environment_fog.active = level.environment_fog.defaults;
    G_EnvironmentFogPublish();
}
