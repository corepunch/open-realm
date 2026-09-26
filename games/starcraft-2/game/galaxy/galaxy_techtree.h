/* galaxy_techtree.h — tech tree, victory panel, loop, and preload natives */

/* IntLoopBegin(tag, start, end): tag is a small integer slot (0-based, compiler-assigned).
 * IntLoopStep returns the new current so the script can assign it to its loop variable. */
#define SC2_MAX_INT_LOOPS 32
typedef struct { int32_t cur, end; bool active; } SC2IntLoop;
static SC2IntLoop sc2_int_loops[SC2_MAX_INT_LOOPS];

static uint32_t sc2_IntLoopBegin(LPJASS j) {
    int32_t tag   = jass_checkinteger(j, 1);
    int32_t start = jass_checkinteger(j, 2);
    int32_t end   = jass_checkinteger(j, 3);
    if (tag >= 0 && tag < SC2_MAX_INT_LOOPS)
        sc2_int_loops[tag] = (SC2IntLoop){ start, end, true };
    return jass_pushnull(j);
}
static uint32_t sc2_IntLoopDone(LPJASS j) {
    int32_t tag = jass_checkinteger(j, 1);
    if (tag >= 0 && tag < SC2_MAX_INT_LOOPS && sc2_int_loops[tag].active)
        return jass_pushboolean(j, sc2_int_loops[tag].cur > sc2_int_loops[tag].end);
    return jass_pushboolean(j, true);
}
static uint32_t sc2_IntLoopStep(LPJASS j) {
    int32_t tag = jass_checkinteger(j, 1);
    if (tag >= 0 && tag < SC2_MAX_INT_LOOPS && sc2_int_loops[tag].active)
        return jass_pushinteger(j, ++sc2_int_loops[tag].cur);
    return jass_pushinteger(j, 0);
}
static uint32_t sc2_IntLoopEnd(LPJASS j) {
    int32_t tag = jass_checkinteger(j, 1);
    if (tag >= 0 && tag < SC2_MAX_INT_LOOPS) sc2_int_loops[tag].active = false;
    return jass_pushnull(j);
}

static uint32_t sc2_TechTreeUpgradeAddLevel(LPJASS j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_VictoryPanelAddAchievement(LPJASS j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_VictoryPanelAddCustomStatisticLine(LPJASS j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_VictoryPanelAddTrackedStatistic(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadAsset(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadImage(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadModel(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadMovie(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadObject(LPJASS j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadScene(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadScript(LPJASS j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PreloadSound(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TechTreeAbilityAllow(LPJASS j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TechTreeAbilityIsAllowed(LPJASS j)      { (void)j; return jass_pushboolean(j, true); }
static uint32_t sc2_TechTreeRestrictionsEnable(LPJASS j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TechTreeUnitHelp(LPJASS j)              { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TechTreeUnitHelpDefault(LPJASS j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_TechTreeUpgradeCount(LPJASS j)          { (void)j; return jass_pushinteger(j, 0); }
