#ifndef hud_utils_h
#define hud_utils_h

/* Missing authored text is represented by an empty, bounded HUD string. */
static void UI_CopyString(string_t out, uint32_t size, cstring_t text) {
    if (out && size) snprintf(out, size, "%s", text ? text : "");
}

/* ROC rows are label,sequence,model; TFT prepends a numeric expansion category (even for ROC campaigns). */
static inline bool UI_ParseLoadingRow(cstring_t row, uint32_t * sequence, string_t model) {
    int offset = 0;
    *sequence = 0; model[0] = 0;
    if (!row) return false;
    sscanf(row, "%*u,%n", &offset);
    /* The old fixed column indices read TFT's sequence as a filename; 255 bounds the PATHSTR output. */
    return sscanf(row + offset, "%*[^,],%u,%255[^,\r\n]", sequence, model) == 2;
}

/* Keep generated FDF frames and later proxy frames in one monotonically increasing wire namespace. */
static uint32_t UI_NextProxyFrameNumber(uint32_t next, uint32_t written) { return MAX(next, written + 1); }
static bool UI_HasSecondAttack(UnitWeapons_t const *weapons) {
    if (!weapons || !(weapons->attacksEnabled & 0x2)) return false;
    return weapons->attack2.damageDice > 0 && weapons->attack2.showUI;
}

/* Warcraft exposes two parallel status-icon skin families. Units whose type
 * has a matching weapon/armor upgrade use the normal infocard artwork; units
 * without that upgrade class use the corresponding Neutral artwork. Keep the
 * authored attack/defense type in the key here: Warsmash only falls Spells
 * back to Magic when the Spells skin field itself is absent. */
static void UI_InfoPanelIconSkinKey(cstring_t prefix, cstring_t type, bool has_upgrade,
                                    string_t out, uint32_t out_size) {
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
static void UI_SetPortraitFrameModel(LPFRAMEDEF frame, uint32_t model) {
    frame->Type = FT_PORTRAIT;
    frame->Portrait.model = model;
}

/* Dynamic lists repeat authored row geometry; only the row index is runtime data. */
static LPFRAMEDEF UI_CloneStackedRow(LPCFRAMEDEF tmpl, LPFRAMEDEF parent, uint32_t row) {
    LPFRAMEDEF frame = UI_CloneFrameTree(tmpl, parent);
    if (frame) UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, parent, FRAMEPOINT_TOPLEFT, 0.0f, -(float)row * frame->Height);
    return frame;
}

/* Correct stale war3skins attribute paths before they enter the image configstring table. */
static cstring_t UI_ResolveTextureAlias(cstring_t path) {
    static struct { cstring_t from, to; } const aliases[] = {
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
