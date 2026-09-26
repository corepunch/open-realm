uint32_t SetCineFilterTexture(jass_t *j) {
    cstring_t filename = jass_checkstring(j, 1);
    level.cinefilter.texture = UI_LoadTexture(filename, true);
    return 0;
}
uint32_t SetCineFilterBlendMode(jass_t *j) {
    BLEND_MODE *whichMode = jass_checkhandle(j, 1, "blendmode");
    level.cinefilter.blendmode = *whichMode;
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
