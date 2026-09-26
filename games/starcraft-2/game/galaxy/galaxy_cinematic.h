/* galaxy_cinematic.h — cinematic natives */
static uint32_t sc2_CinematicMode(jass_t * j) {
    bool  enable = jass_checkboolean(j, 2);
    float dur    = jass_checknumber(j, 3);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CinematicMode: enable=%d dur=%.1f\n", enable, dur);
#endif
    if (sc2_galaxy_on_cinematic) sc2_galaxy_on_cinematic(enable, dur);
    return jass_pushnull(j);
}

static uint32_t sc2_CinematicFade(jass_t * j) {
    bool  fadein = jass_checkboolean(j, 1);
    float dur    = jass_checknumber(j, 2);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CinematicFade: fadein=%d dur=%.1f\n", fadein, dur);
#endif
    if (sc2_galaxy_on_fade) sc2_galaxy_on_fade(fadein ? 0.0f : 1.0f, dur);
    return jass_pushnull(j);
}

static uint32_t sc2_CinematicOverlay(jass_t * j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_CinematicDataRun(jass_t * j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_CinematicDataStop(jass_t * j) { (void)j; return jass_pushnull(j); }
