#ifndef api_ai_h
#define api_ai_h

/* common.ai counts queued and constructing units toward desired totals; Done excludes both incomplete states. */
static LONG AIUnitCount(LPPLAYER player, DWORD unitid, BOOL done) {
    LONG count = 0;
    if (!player || !unitid) return 0;
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && ent->class_id == unitid &&
                         ent->s.player == PLAYER_NUM(player) && !(ent->svflags & SVF_DEADMONSTER)) {
        if (!done || (!ent->construction.active && !ent->training)) count++;
    }
    return count;
}

DWORD GetAiPlayer(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? (LONG)PLAYER_NUM(player) : -1);
}

DWORD GetUnitCount(LPJASS j) {
    return jass_pushinteger(j, AIUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), false));
}

DWORD GetPlayerUnitTypeCount(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, AIUnitCount(player, jass_checkinteger(j, 2), false));
}

DWORD GetUnitCountDone(LPJASS j) {
    return jass_pushinteger(j, AIUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), true));
}

DWORD GetUnitGoldCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->goldCost));
}

DWORD GetUnitWoodCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->lumberCost));
}

DWORD GetUpgradeLevel(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), jass_checkinteger(j, 1)) : 0);
}

DWORD UnitAlive(LPJASS j) {
    LPEDICT unit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_AIUnitAlive(unit));
}

DWORD StartThread(LPJASS j) {
    LPCJASSFUNC func = jass_checkcode(j, 1);
    JASSCONTEXT context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}

DWORD Sleep(LPJASS j) {
    FLOAT seconds = jass_checknumber(j, 1);
    jass_sleep(j, (DWORD)(MAX(0, seconds) * 1000));
    return 0;
}

#endif /* api_ai_h */
