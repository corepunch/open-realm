DWORD SetCineFilterTexture(LPJASS j) {
    LPCSTR filename = jass_checkstring(j, 1);
    level.cinefilter.texture = UI_LoadTexture(filename, true);
    return 0;
}
DWORD SetCineFilterBlendMode(LPJASS j) {
    BLEND_MODE *whichMode = jass_checkhandle(j, 1, "blendmode");
    level.cinefilter.blendmode = *whichMode;
    return 0;
}
DWORD SetCineFilterTexMapFlags(LPJASS j) {
    TEXMAP_FLAGS *whichFlags = jass_checkhandle(j, 1, "texmapflags");
    level.cinefilter.texmapflags = *whichFlags;
    return 0;
}
DWORD SetCineFilterStartUV(LPJASS j) {
    level.cinefilter.start.uv =
    MAKE(BOX2,
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
DWORD SetCineFilterEndUV(LPJASS j) {
    level.cinefilter.end.uv =
    MAKE(BOX2,
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
DWORD SetCineFilterStartColor(LPJASS j) {
    level.cinefilter.start.color = 
    MAKE(COLOR32,
         .r = jass_checkinteger(j, 1),
         .g = jass_checkinteger(j, 2),
         .b = jass_checkinteger(j, 3),
         .a = jass_checkinteger(j, 4));
    return 0;
}
DWORD SetCineFilterEndColor(LPJASS j) {
    level.cinefilter.end.color =
    MAKE(COLOR32,
         .r = jass_checkinteger(j, 1),
         .g = jass_checkinteger(j, 2),
         .b = jass_checkinteger(j, 3),
         .a = jass_checkinteger(j, 4));
    return 0;
}
DWORD SetCineFilterDuration(LPJASS j) {
    FLOAT duration = jass_checknumber(j, 1);
    if (G_SkipCutscene()) {
        duration = 0;
    }
    level.cinefilter.start.time = G_Time();
    level.cinefilter.end.time = G_Time() + duration * 1000;
    return 0;
}
DWORD DisplayCineFilter(LPJASS j) {
    level.cinefilter.displayed = jass_checkboolean(j, 1);
    if (G_SkipCutscene()) {
        level.cinefilter.displayed = false;
    }
    return 0;
}
DWORD IsCineFilterDisplayed(LPJASS j) {
    return jass_pushboolean(j, level.cinefilter.displayed);
}
