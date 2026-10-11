/* Reads the alpha depth from the image header. Authored .tga names usually
 * ship as .blp, matching the renderer's lookup fallback. */
bool G_ImageHeaderHasAlpha(uint8_t const *data, uint32_t size) {
    if (!data) return false;
    if (size >= 12 && !memcmp(data, "BLP1", 4)) return data[8] | data[9] | data[10] | data[11];
    if (size >= 10 && !memcmp(data, "BLP2", 4)) return data[9] != 0;
    if (size >= 18 && (data[2] == 2 || data[2] == 10)) return (data[17] & 0x0F) != 0; /* TGA */
    return false;
}

static bool G_ImageFileHasAlpha(cstring_t filename) {
    PATHSTR path;
    uint32_t size = 0;
    cstring_t ext = strrchr(filename, '.');
    handle_t data = gi.ReadFile(filename, &size);
    bool alpha;

    if (!data && ext && !strcasecmp(ext, ".tga")) {
        snprintf(path, sizeof(path), "%.*s.blp", (int)(ext - filename), filename);
        data = gi.ReadFile(path, &size);
    }
    alpha = G_ImageHeaderHasAlpha(data, size);
    if (data) gi.MemFree(data);
    return alpha;
}

uint32_t SetCineFilterTexture(jass_t *j) {
    cstring_t filename = jass_checkstring(j, 1);
    level.cinefilter.texture = (uint32_t)gi.ImageIndex(filename);
    level.cinefilter.masked = G_ImageFileHasAlpha(filename);
    return 0;
}
/* JASS blendmode constants have no ADDALPHA entry, so MODULATE and MODULATE_2X
 * sit one below the engine BLEND_MODE values. */
BLEND_MODE G_BlendModeFromJass(uint32_t mode) {
    static BLEND_MODE const modes[] = {
        BLEND_MODE_NONE, BLEND_MODE_ALPHAKEY, BLEND_MODE_BLEND,
        BLEND_MODE_ADD, BLEND_MODE_MODULATE, BLEND_MODE_MODULATE_2X,
    };
    return mode < sizeof(modes) / sizeof(*modes) ? modes[mode] : BLEND_MODE_BLEND;
}
uint32_t SetCineFilterBlendMode(jass_t *j) {
    uint32_t *whichMode = jass_checkhandle(j, 1, "blendmode");
    level.cinefilter.blendmode = G_BlendModeFromJass(*whichMode);
    return 0;
}
uint32_t SetCineFilterTexMapFlags(jass_t *j) {
    TEXMAP_FLAGS *whichFlags = jass_checkhandle(j, 1, "texmapflags");
    level.cinefilter.texmapflags = *whichFlags;
    return 0;
}
uint32_t SetCineFilterStartUV(jass_t *j) {
    level.cinefilter.start.uv =
    MAKE(box2_t,
         .min = {
             jass_checknumber(j, 1),
             jass_checknumber(j, 2)
         },
         .max = {
             jass_checknumber(j, 3),
             jass_checknumber(j, 4)
         });
    return 0;
}
uint32_t SetCineFilterEndUV(jass_t *j) {
    level.cinefilter.end.uv =
    MAKE(box2_t,
         .min = {
             jass_checknumber(j, 1),
             jass_checknumber(j, 2)
         },
         .max = {
             jass_checknumber(j, 3),
             jass_checknumber(j, 4)
         });
    return 0;
}
uint32_t SetCineFilterStartColor(jass_t *j) {
    level.cinefilter.start.color = 
    MAKE(color32_t,
         .r = jass_checkinteger(j, 1),
         .g = jass_checkinteger(j, 2),
         .b = jass_checkinteger(j, 3),
         .a = jass_checkinteger(j, 4));
    return 0;
}
uint32_t SetCineFilterEndColor(jass_t *j) {
    level.cinefilter.end.color =
    MAKE(color32_t,
         .r = jass_checkinteger(j, 1),
         .g = jass_checkinteger(j, 2),
         .b = jass_checkinteger(j, 3),
         .a = jass_checkinteger(j, 4));
    return 0;
}
uint32_t SetCineFilterDuration(jass_t *j) {
    float duration = jass_checknumber(j, 1);
    if (G_SkipCutscene()) {
        duration = 0;
    }
    level.cinefilter.start.time = G_Time();
    level.cinefilter.end.time = G_Time() + duration * 1000;
    return 0;
}
uint32_t DisplayCineFilter(jass_t *j) {
    level.cinefilter.displayed = jass_checkboolean(j, 1);
    if (G_SkipCutscene()) {
        level.cinefilter.displayed = false;
    }
    return 0;
}
uint32_t IsCineFilterDisplayed(jass_t *j) {
    return jass_pushboolean(j, level.cinefilter.displayed);
}
