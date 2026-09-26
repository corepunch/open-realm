/* Player state is level-owned and shared with resource HUD publication. */
sc2PlayerState_t sc2_players[32];
static int sc2_player_index(jass_t *j, int arg) {
    int32_t p = jass_checkinteger(j, arg);
    if (p < 0 || p >= 32) { jass_rterror(j, "Player index outside [0,31]"); return 0; }
    return (int)p;
}
static int sc2_checked_index(jass_t *j, int arg, int count) {
    int32_t n = jass_checkinteger(j, arg);
    if (n < 0 || n >= count) { jass_rterror(j, "Galaxy property index out of range"); return 0; }
    return (int)n;
}
static uint32_t sc2_PlayerGetState(jass_t *j) { return jass_pushboolean(j, (sc2_players[sc2_player_index(j,1)].states & (1u << sc2_checked_index(j,2,9))) != 0); }
static uint32_t sc2_PlayerSetState(jass_t *j) {
    sc2PlayerState_t *p = &sc2_players[sc2_player_index(j,1)]; uint32_t bit = 1u << sc2_checked_index(j,2,9);
    if (jass_checkboolean(j,3)) p->states |= bit; else p->states &= ~bit;
    return 0;
}
static uint32_t sc2_PlayerGetAlliance(jass_t *j) {
    int p = sc2_player_index(j,1), a = sc2_checked_index(j,2,16), other = sc2_player_index(j,3);
    return jass_pushboolean(j, p == other || (sc2_players[p].alliances[other] & (1u << a)));
}
static uint32_t sc2_PlayerSetAlliance(jass_t *j) {
    int p = sc2_player_index(j,1), a = sc2_checked_index(j,2,16), other = sc2_player_index(j,3);
    uint32_t bit = 1u << a;
    bool on = jass_checkboolean(j,4), was = (sc2_players[p].alliances[other] & bit) != 0;
    if (on) sc2_players[p].alliances[other] |= bit; else sc2_players[p].alliances[other] &= ~bit;
    if (on != was)
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_ALLIANCE, .player = p, .ival = a, .ival2 = other });
    return 0;
}
static uint32_t sc2_PlayerDifficulty(jass_t *j) { return jass_pushinteger(j, sc2_players[sc2_player_index(j,1)].difficulty); }
static uint32_t sc2_PlayerSetDifficulty(jass_t *j) { sc2_players[sc2_player_index(j,1)].difficulty = jass_checkinteger(j,2); return 0; }
static uint32_t sc2_PlayerType(jass_t *j) { return jass_pushinteger(j, sc2_players[sc2_player_index(j,1)].type); }
static uint32_t sc2_player_modify(jass_t *j, bool integral) {
    int p = sc2_player_index(j,1), prop = sc2_checked_index(j,2,16), op = sc2_checked_index(j,3,3);
    float v = integral ? jass_checkinteger(j,4) : jass_checknumber(j,4);
    float *value = &sc2_players[p].properties[prop];
    float old = *value;
    *value = op == 0 ? v : *value + (op == 1 ? v : -v);
    if (*value != old)
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_PLAYER_PROP, .player = p, .ival = prop, .amount = *value });
    return 0;
}
static uint32_t sc2_PlayerModifyPropertyInt(jass_t *j) { return sc2_player_modify(j,true); }
static uint32_t sc2_PlayerModifyPropertyFixed(jass_t *j) { return sc2_player_modify(j,false); }
static uint32_t sc2_PlayerGetPropertyInt(jass_t *j) { return jass_pushinteger(j,(int32_t)sc2_players[sc2_player_index(j,1)].properties[sc2_checked_index(j,2,16)]); }
static uint32_t sc2_PlayerGetPropertyFixed(jass_t *j) { return jass_pushnumber(j,sc2_players[sc2_player_index(j,1)].properties[sc2_checked_index(j,2,16)]); }

static sc2GGroup_t *sc2_player_group(jass_t *j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"playergroup");
    return h > 0 && h < sc2_player_group_n ? &sc2_player_groups[h] : NULL;
}
static uint32_t sc2_player_group_result(jass_t *j, sc2GGroup_t const *source) {
    int32_t h = sc2_group_new(j, sc2_player_groups, &sc2_player_group_n, source);
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"playergroup");
}
static uint32_t sc2_PlayerGroupEmpty(jass_t *j) { return sc2_player_group_result(j,NULL); }
static uint32_t sc2_PlayerGroupCopy(jass_t *j) { return sc2_player_group_result(j,sc2_player_group(j)); }
static uint32_t sc2_PlayerGroupClear(jass_t *j) { sc2GGroup_t *g = sc2_player_group(j); if (g) g->count = 0; return 0; }
static uint32_t sc2_PlayerGroupAdd(jass_t *j) { sc2GGroup_t *g = sc2_player_group(j); int p = sc2_player_index(j,2); if (g) sc2_group_append(j,g,p); return 0; }
static uint32_t sc2_PlayerGroupRemove(jass_t *j) { sc2GGroup_t *g = sc2_player_group(j); int p = sc2_player_index(j,2); if (g) sc2_group_remove(g,p); return 0; }
static uint32_t sc2_PlayerGroupCount(jass_t *j) { sc2GGroup_t *g = sc2_player_group(j); return jass_pushinteger(j,g ? g->count : 0); }
static uint32_t sc2_PlayerGroupPlayer(jass_t *j) {
    sc2GGroup_t *g = sc2_player_group(j); int32_t n = jass_checkinteger(j,2)-1;
    return jass_pushinteger(j,g && n >= 0 && n < g->count ? g->items[n] : -1);
}
static uint32_t sc2_PlayerGroupHasPlayer(jass_t *j) {
    sc2GGroup_t *g = sc2_player_group(j); int p = sc2_player_index(j,2);
    if (g) for (int i = 0; i < g->count; i++) if (g->items[i] == p) return jass_pushboolean(j,true);
    return jass_pushboolean(j,false);
}
static uint32_t sc2_player_group_select(jass_t *j, int mode) {
    int32_t h = sc2_group_new(j,sc2_player_groups,&sc2_player_group_n,NULL);
    int type = mode == 3 ? sc2_checked_index(j,1,3) : 2;
    int p = mode == 2 ? sc2_player_index(j,1) : mode == 3 ? sc2_player_index(j,2) : -1;
    for (int i = 0; i < 32; i++) {
        bool ally = p >= 0 && (p == i || (sc2_players[p].alliances[i] & 1));
        if ((mode == 1 && !sc2_players[i].active) || (mode == 2 && p != i)) continue;
        if (mode == 3 && ((type == 0 && !ally) || (type == 1 && ally))) continue;
        sc2_group_append(j,&sc2_player_groups[h],i);
    }
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"playergroup");
}
static uint32_t sc2_PlayerGroupAll(jass_t *j) { return sc2_player_group_select(j,0); }
static uint32_t sc2_PlayerGroupActive(jass_t *j) { return sc2_player_group_select(j,1); }
static uint32_t sc2_PlayerGroupSingle(jass_t *j) { return sc2_player_group_select(j,2); }
static uint32_t sc2_PlayerGroupAlliance(jass_t *j) { return sc2_player_group_select(j,3); }
static uint32_t sc2_PlayerGroupLoopBegin(jass_t *j) { sc2_loop_begin(j,1,sc2_player_group(j)); return 0; }
static uint32_t sc2_PlayerGroupLoopEnd(jass_t *j) { sc2_loop_end(j,1); return 0; }
static uint32_t sc2_PlayerGroupLoopStep(jass_t *j) { sc2GLoop_t *l = sc2_loop_current(j,1); if (l) l->index++; return 0; }
static uint32_t sc2_PlayerGroupLoopDone(jass_t *j) { sc2GLoop_t *l = sc2_loop_current(j,1); return jass_pushboolean(j,!l || l->index >= l->snapshot.count); }
static uint32_t sc2_PlayerGroupLoopCurrent(jass_t *j) { sc2GLoop_t *l = sc2_loop_current(j,1); return jass_pushinteger(j,l && l->index < l->snapshot.count ? l->snapshot.items[l->index] : -1); }

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
