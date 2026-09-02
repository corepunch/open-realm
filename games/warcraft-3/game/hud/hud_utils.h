#ifndef hud_utils_h
#define hud_utils_h

/* Keep generated FDF frames and later proxy frames in one monotonically increasing wire namespace. */
static DWORD UI_NextProxyFrameNumber(DWORD next, DWORD written) { return MAX(next, written + 1); }
static BOOL UI_HasSecondAttack(UnitWeapons_t const *weapons) {
    if (!weapons || !(weapons->attacksEnabled & 0x2)) return false;
    return weapons->attack2.damageDice > 0 && weapons->attack2.showUI;
}

/* Warcraft exposes two parallel status-icon skin families. Units whose type
 * has a matching weapon/armor upgrade use the normal infocard artwork; units
 * without that upgrade class use the corresponding Neutral artwork. Keep the
 * authored attack/defense type in the key here: Warsmash only falls Spells
 * back to Magic when the Spells skin field itself is absent. */
static void UI_InfoPanelIconSkinKey(LPCSTR prefix, LPCSTR type, BOOL has_upgrade,
                                    LPSTR out, DWORD out_size) {
    char code[32];
    size_t length;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!prefix || !*prefix) return;
    if (!type || !*type)
        type = !strcasecmp(prefix, "Armor") ? "Small" : "Unknown";
    if (!strcasecmp(prefix, "Armor") && !strcasecmp(type, "heavy")) type = "Large";
    if (!strcasecmp(prefix, "Damage") && !strcasecmp(type, "seige")) type = "Siege";
    length = MIN(strlen(type), sizeof(code) - 1);
    memcpy(code, type, length);
    code[length] = '\0';
    code[0] = (char)toupper((unsigned char)code[0]);
    snprintf(out, out_size, "InfoPanelIcon%s%s%s", prefix, code, has_upgrade ? "" : "Neutral");
}
static void UI_SetPortraitFrameModel(LPFRAMEDEF frame, DWORD model) {
    frame->Type = FT_PORTRAIT;
    frame->Portrait.model = model;
}

/* Dynamic lists repeat authored row geometry; only the row index is runtime data. */
static LPFRAMEDEF UI_CloneStackedRow(LPCFRAMEDEF tmpl, LPFRAMEDEF parent, DWORD row) {
    LPFRAMEDEF frame = UI_CloneFrameTree(tmpl, parent);
    if (frame) UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, parent, FRAMEPOINT_TOPLEFT, 0.0f, -(FLOAT)row * frame->Height);
    return frame;
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
