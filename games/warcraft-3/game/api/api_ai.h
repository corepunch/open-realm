#ifndef api_ai_h
#define api_ai_h

DWORD GetAiPlayer(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? (LONG)PLAYER_NUM(player) : -1);
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
