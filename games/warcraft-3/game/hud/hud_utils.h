#ifndef hud_utils_h
#define hud_utils_h

/* Keep generated FDF frames and later proxy frames in one monotonically increasing wire namespace. */
static DWORD UI_NextProxyFrameNumber(DWORD next, DWORD written) { return MAX(next, written + 1); }
static BOOL UI_HasSecondAttack(DWORD dice) { return dice > 0; }
static void UI_SetPortraitFrameModel(LPFRAMEDEF frame, DWORD model) {
    frame->Type = FT_PORTRAIT;
    frame->Portrait.model = model;
}

/* Correct stale war3skins attribute paths before they enter the image configstring table. */
static LPCSTR UI_ResolveTextureAlias(LPCSTR path) {
    static struct { LPCSTR from, to; } const aliases[] = {
        { "HeroStrengthIcon", "UI\\Widgets\\Console\\Human\\infocard-heroattributes-str.blp" },
        { "HeroAgilityIcon", "UI\\Widgets\\Console\\Human\\infocard-heroattributes-agi.blp" },
        { "HeroIntelligenceIcon", "UI\\Widgets\\Console\\Human\\infocard-heroattributes-int.blp" },
        { "UI\\Widgets\\Console\\Human\\human-attribute-str.blp",
          "UI\\Widgets\\Console\\Human\\infocard-heroattributes-str.blp" },
        { "UI\\Widgets\\Console\\Human\\human-attribute-agi.blp",
          "UI\\Widgets\\Console\\Human\\infocard-heroattributes-agi.blp" },
        { "UI\\Widgets\\Console\\Human\\human-attribute-int.blp",
          "UI\\Widgets\\Console\\Human\\infocard-heroattributes-int.blp" },
    };
    FOR_LOOP(i, sizeof(aliases) / sizeof(aliases[0]))
        if (!strcasecmp(path, aliases[i].from)) return aliases[i].to;
    return path;
}

#endif
