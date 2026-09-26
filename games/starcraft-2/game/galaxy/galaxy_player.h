/* galaxy_player.h — player and playergroup natives */
static uint32_t sc2_PlayerGroupAll(jass_t *j)            { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGroupActive(jass_t *j)         { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGroupAdd(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGroupClear(jass_t *j)          { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGroupCopy(jass_t *j)           { return jass_pushnullhandle(j, "playergroup"); }
/* PlayerGroupCount / PlayerGroupPlayer: singleplayer stubs — exactly 1 human player. */
static uint32_t sc2_PlayerGroupCount(jass_t *j) {
    (void)jass_checkhandle(j, 1, "playergroup");
    return jass_pushinteger(j, 1);
}
static uint32_t sc2_PlayerGroupEmpty(jass_t *j)          { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGroupHasPlayer(jass_t *j)      { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerGroupPlayer(jass_t *j) {
    (void)jass_checkhandle(j, 1, "playergroup");
    (void)jass_checkinteger(j, 2);
    return jass_pushinteger(j, 1);
}
static uint32_t sc2_PlayerGroupLoopBegin(jass_t *j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGroupLoopDone(jass_t *j)       { return jass_pushboolean(j, true); }
static uint32_t sc2_PlayerGroupLoopEnd(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGroupLoopStep(jass_t *j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGroupRemove(jass_t *j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGroupSingle(jass_t *j)         { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGroupAlliance(jass_t *j)       { return jass_pushnullhandle(j, "playergroup"); }
static uint32_t sc2_PlayerGetState(jass_t *j)            { return jass_pushinteger(j, 0); }
static uint32_t sc2_PlayerSetState(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGetAlliance(jass_t *j)         { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerSetAlliance(jass_t *j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerDifficulty(jass_t *j)          { return jass_pushinteger(j, 0); }
static uint32_t sc2_PlayerModifyPropertyInt(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerAddChargeRegen(jass_t *j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerAddChargeUsed(jass_t *j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerAddCooldown(jass_t *j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerGetChargeRegen(jass_t *j)      { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_PlayerGetChargeUsed(jass_t *j)       { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_PlayerGetCooldown(jass_t *j)         { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_PlayerScoreValueEnable(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerScoreValueEnableAll(jass_t *j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerScoreValueGetAsFixed(jass_t *j){ return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_PlayerScoreValueGetAsInt(jass_t *j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_PlayerScoreValueSetFromFixed(jass_t *j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerScoreValueSetFromInt(jass_t *j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerPauseAllCharges(jass_t *j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerPauseAllCooldowns(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerBeaconAlert(jass_t *j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerBeaconClearTarget(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerBeaconGetTargetPoint(jass_t *j){ return jass_pushinteger(j, 0); }
static uint32_t sc2_PlayerBeaconGetTargetUnit(jass_t *j) { return jass_pushinteger(j, 0); }
static uint32_t sc2_PlayerBeaconIsAutoCast(jass_t *j)    { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerBeaconIsFromUser(jass_t *j)    { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerBeaconIsSet(jass_t *j)         { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerBeaconSetAutoCast(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerBeaconSetTargetPoint(jass_t *j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerBeaconSetTargetUnit(jass_t *j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerCreateEffectPoint(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerCreateEffectUnit(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PlayerValidateEffectPoint(jass_t *j) { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerValidateEffectUnit(jass_t *j)  { return jass_pushboolean(j, false); }
static uint32_t sc2_PlayerType(jass_t *j)                { (void)j; return jass_pushinteger(j, 0); }
