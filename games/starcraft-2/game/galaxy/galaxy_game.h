/* galaxy_game.h — string, text, color, game, AI, and data-table natives */
static uint32_t sc2_StringExternal(jass_t * j)     { cstring_t s = jass_checkstring(j,1); return jass_pushstring(j, s ? s : ""); }
/* Galaxy uses a null result to terminate whitespace-delimited word iteration. */
static uint32_t sc2_StringWord(jass_t * j) {
    cstring_t str = jass_checkstring(j, 1), word;
    int32_t index = jass_checkinteger(j, 2);
    if (!str || index < 1) return jass_pushnull(j);
    while (*str) {
        while (*str && isspace((unsigned char)*str)) str++;
        if (!*str) break;
        word = str;
        while (*str && !isspace((unsigned char)*str)) str++;
        if (!--index) return jass_pushstringlen(j, word, (uint32_t)(str - word));
    }
    return jass_pushnull(j);
}
/* Galaxy text is represented as a VM string; an integer placeholder corrupted objective-name concatenation. */
static uint32_t sc2_IntToText(jass_t * j) {
    char text[32];
    snprintf(text, sizeof(text), "%ld", (long)jass_checkinteger(j, 1));
    return jass_pushstring(j, text);
}
/* Galaxy color components are fixed-point percentages; integer byte reads aborted cinematic fades. */
static uint32_t sc2_color(jass_t * j, uint32_t count) {
    uint32_t packed = count == 3 ? 0xff000000u : 0;
    FOR_LOOP(i, count) {
        float value = jass_checknumber(j, i + 1);
        uint32_t byte = (uint32_t)lroundf(MAX(0.0f, MIN(100.0f, value)) * 255.0f / 100.0f);
        packed |= byte << (i < 3 ? 16 - i * 8 : 24);
    }
    return jass_pushinteger(j, (int32_t)packed);
}
static uint32_t sc2_Color(jass_t * j) { return sc2_color(j, 3); }
static uint32_t sc2_ColorWithAlpha(jass_t * j) { return sc2_color(j, 4); }
static uint32_t sc2_GameTimeOfDayPause(jass_t * j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameTimeOfDaySet(jass_t * j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameSetBackground(jass_t * j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameSetLighting(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_DifficultyEnabled(jass_t * j)  { return jass_pushboolean(j, false); }
static uint32_t sc2_DifficultyName(jass_t * j)     { return jass_pushstring(j, ""); }
static uint32_t sc2_DifficultyNameCampaign(jass_t * j) { return jass_pushstring(j, ""); }
static uint32_t sc2_AITimePause(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AIDisableAllScouting(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_CampaignMode(jass_t * j)                  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_DataTableSetString(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_DataTableValueExists(jass_t * j)          { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_FixedToInt(jass_t * j)  { return jass_pushinteger(j, (int32_t)jass_checknumber(j, 1)); }
static uint32_t sc2_FixedToString(jass_t * j) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.4g", (double)jass_checknumber(j, 1));
    return jass_pushstring(j, buf);
}
/* natives.galaxy declares one integer argument; reading a second boolean aborted campaign credit formatting. */
static uint32_t sc2_FormatNumber(jass_t * j) {
    int32_t v   = jass_checkinteger(j, 1);
    char raw[32], out[48];
    snprintf(raw, sizeof(raw), "%ld", (long)v);
    /* Insert thousands separators. */
    int32_t len = (int32_t)strlen(raw), start = (v < 0) ? 1 : 0;
    int32_t digits = len - start, wr = 0;
    if (start) out[wr++] = '-';
    for (int32_t i = 0; i < digits; i++) {
        if (i > 0 && (digits - i) % 3 == 0) out[wr++] = ',';
        out[wr++] = raw[start + i];
    }
    out[wr] = '\0';
    return jass_pushstring(j, out);
}
static uint32_t sc2_GameCheatAllow(jass_t * j)                { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameGetSpeedValue(jass_t * j)             { (void)j; return jass_pushnumber(j, 1.0f); }
static uint32_t sc2_GameIsDebugOptionSet(jass_t * j)          { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_GameIsTestMap(jass_t * j)                 { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_GameIsTransitionMap(jass_t * j)           { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_GameMapIsBlizzard(jass_t * j)             { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_GamePauseAllCharges(jass_t * j)           { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameSetSeedLocked(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameSetSpeedLocked(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_GameSetSpeedValue(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_IntToFixed(jass_t * j) { return jass_pushnumber(j, (float)jass_checkinteger(j, 1)); }
static uint32_t sc2_IntToString(jass_t * j) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", (long)jass_checkinteger(j, 1));
    return jass_pushstring(j, buf);
}
/* Galaxy uses 1-based, inclusive string indices. */
static uint32_t sc2_StringSub(jass_t * j) {
    cstring_t src = jass_checkstring(j, 1);
    int32_t start = jass_checkinteger(j, 2) - 1;
    int32_t end   = jass_checkinteger(j, 3);
    if (!src) return jass_pushstring(j, "");
    int32_t len = (int32_t)strlen(src);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return jass_pushstring(j, "");
    return jass_pushstringlen(j, src + start, (uint32_t)(end - start));
}
/* NativeLib passes maxCount before caseSens; reading slot four as bool aborted animation setup. */
static uint32_t sc2_StringReplaceWord(jass_t * j) {
    cstring_t src = jass_checkstring(j, 1), find = jass_checkstring(j, 2), repl = jass_checkstring(j, 3);
    int32_t limit = jass_checkinteger(j, 4);
    bool cs = jass_checkboolean(j, 5);
    if (!src || !find || !*find) return jass_pushstring(j, src ? src : "");
    size_t len = strlen(src), flen = strlen(find), rlen = repl ? strlen(repl) : 0, count = len / flen;
    /* NativeLib uses zero for all replacements; natives.galaxy also defines c_stringReplaceAll as -1. */
    if (limit > 0) count = MIN(count, (size_t)limit);
    size_t size = len + 1 + (rlen > flen ? (rlen - flen) * count : 0);
    string_t out = jass_alloc((long)size), dst = out;
    if (!out) jass_rterror(j, "StringReplaceWord: allocation failed");
    while (*src) {
        bool match = count && !(cs ? strncmp(src, find, flen) : strncasecmp(src, find, flen));
        if (match) {
            if (rlen) memcpy(dst, repl, rlen);
            dst += rlen; src += flen; count--;
        } else
            *dst++ = *src++;
    }
    *dst = '\0';
    uint32_t ret = jass_pushstring(j, out);
    jass_free(out);
    return ret;
}
/* text and string are the same underlying type in Galaxy; just forward the string. */
static uint32_t sc2_StringToText(jass_t * j) {
    cstring_t s = jass_checkstring(j, 1);
    return jass_pushstring(j, s ? s : "");
}
static uint32_t sc2_TextCase(jass_t * j) {
    cstring_t src  = jass_checkstring(j, 1);
    bool upper  = jass_checkboolean(j, 2);
    if (!src) return jass_pushstring(j, "");
    char buf[512]; int32_t i = 0;
    for (; src[i] && i < (int32_t)sizeof(buf) - 1; i++)
        buf[i] = (char)(upper ? toupper((unsigned char)src[i]) : tolower((unsigned char)src[i]));
    buf[i] = '\0';
    return jass_pushstring(j, buf);
}
