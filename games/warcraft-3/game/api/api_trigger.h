static BOOL QuestPeonStageDebugEnabled(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

static LONG QuestPeonStageTriggerOrdinal(LPTRIGGER trigger) {
    return trigger ? (LONG)(trigger - level.triggers) : -1L;
}

static BOOL SubgroupDebugTrigger(LPTRIGGER trigger) {
    LONG ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 208 && ordinal <= 213;
}

static BOOL TutorialFlowDebugTrigger(LPTRIGGER trigger) {
    LONG ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 120 && ordinal <= 165;
}

static void TutorialFlowDebugLogRegistration(LPTRIGGER trigger, EVENTTYPE type,
                                             LPEDICT subject, LPCSTR registration) {
    if (!QuestPeonStageDebugEnabled() || !TutorialFlowDebugTrigger(trigger)) return;
    fprintf(stderr,
            "WC3_TUTORIAL_FLOW register trigger=%ld via=%s event=%u subject=%ld disabled=%d\n",
            (long)QuestPeonStageTriggerOrdinal(trigger),
            registration ? registration : "unknown", (unsigned)type,
            subject ? (long)(subject - globals.edicts) : -1L,
            trigger ? (int)trigger->disabled : -1);
}

static void SubgroupDebugLogRegistration(LPTRIGGER trigger, EVENTTYPE type,
                                         LPEDICT subject, LPCSTR registration) {
    if (!QuestPeonStageDebugEnabled() || !SubgroupDebugTrigger(trigger)) return;
    fprintf(stderr,
            "WC3_SUBGROUP register trigger=%ld via=%s event=%u subject=%ld disabled=%d\n",
            (long)QuestPeonStageTriggerOrdinal(trigger),
            registration ? registration : "unknown", (unsigned)type,
            subject ? (long)(subject - globals.edicts) : -1L,
            trigger ? (int)trigger->disabled : -1);
}

static BOOL QuestPeonStageTrigger(LPTRIGGER trigger) {
    LONG ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 95 && ordinal <= 106;
}

static void QuestPeonStageLogRegistration(LPTRIGGER trigger, EVENTTYPE type,
                                          LPEDICT subject, LPCSTR registration) {
    SubgroupDebugLogRegistration(trigger, type, subject, registration);
    TutorialFlowDebugLogRegistration(trigger, type, subject, registration);
    if (!QuestPeonStageDebugEnabled() || !QuestPeonStageTrigger(trigger)) return;
    fprintf(stderr,
            "WC3_QUEST_PEON register trigger=%ld via=%s event=%u subject=%ld disabled=%d\n",
            (long)QuestPeonStageTriggerOrdinal(trigger),
            registration ? registration : "unknown", (unsigned)type,
            subject ? (long)(subject - globals.edicts) : -1L,
            trigger ? (int)trigger->disabled : -1);
}

DWORD CreateTrigger(LPJASS j) {
    LPTRIGGER trigger = G_AllocJassTrigger();
    if (!trigger) { jass_rterror(j, "CreateTrigger: trigger registry is full"); return 0; }
    return jass_pushlighthandle(j, trigger, "trigger");
}
DWORD DestroyTrigger(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    return 0;
}
DWORD ResetTrigger(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    return 0;
}
DWORD EnableTrigger(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON state trigger=%ld op=enable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP state trigger=%ld op=enable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW state trigger=%ld op=enable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)",
                (int)whichTrigger->disabled);
    }
    whichTrigger->disabled = false;
    return 0;
}
DWORD DisableTrigger(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON state trigger=%ld op=disable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP state trigger=%ld op=disable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW state trigger=%ld op=disable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)",
                (int)whichTrigger->disabled);
    }
    whichTrigger->disabled = true;
    return 0;
}
DWORD IsTriggerEnabled(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    BOOL enabled = !whichTrigger->disabled;
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        char chain[256];
        jass_formatcallchain(j, chain, sizeof(chain));
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW query trigger=%ld native=IsTriggerEnabled caller=\"%s\" disabled=%d result=%d chain=\"%s\"\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)",
                (int)whichTrigger->disabled, (int)enabled, chain);
    }
    return jass_pushboolean(j, enabled);
}
DWORD TriggerWaitOnSleeps(LPJASS j) {
    /* TODO: Store the per-trigger wait-on-sleeps flag once coroutine suspension exposes that state. */
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    BOOL flag = jass_checkboolean(j, 2);
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW wait-on-sleeps trigger=%ld flag=%d caller=\"%s\" implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger), (int)flag,
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return 0;
}
DWORD IsTriggerWaitOnSleeps(LPJASS j) {
    /* TODO: Return the stored per-trigger wait-on-sleeps flag once coroutine suspension exposes that state. */
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW is-wait-on-sleeps trigger=%ld caller=\"%s\" result=0 implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return jass_pushboolean(j, 0);
}
DWORD GetTriggeringTrigger(LPJASS j) {
    LPCJASSCONTEXT ctx = jass_getcontext(j);
    if (ctx && QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(ctx->trigger)) {
        char chain[256];
        jass_formatcallchain(j, chain, sizeof(chain));
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW context trigger=%ld native=GetTriggeringTrigger caller=\"%s\" chain=\"%s\"\n",
                (long)QuestPeonStageTriggerOrdinal(ctx->trigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)", chain);
    }
    return jass_pushlighthandle(j, ctx ? ctx->trigger : NULL, "trigger");
}
DWORD GetTriggerEventId(LPJASS j) {
    return jass_pushnullhandle(j, "eventid");
}
DWORD GetTriggerEvalCount(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    return jass_pushinteger(j, 0);
}
DWORD GetTriggerExecCount(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    return jass_pushinteger(j, 0);
}
/* Registrations own their subject/filter/limit data. Dispatch installs event
 * response context before conditions and actions, then restores it for nested
 * triggers; state-limit events fire on the qualifying transition. */
DWORD TriggerRegisterVariableEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPCSTR varName = jass_checkstring(j, 2);
    LPSTR variable;
    LPDWORD opcode = jass_checkhandle(j, 3, "limitop");
    FLOAT limitval = jass_checknumber(j, 4);
    LPEVENT evt;
    if (!whichTrigger || !varName || !opcode) return jass_pushnullhandle(j, "event");
    evt = G_MakeEvent(EVENT_GAME_VARIABLE_LIMIT);
    if (!evt) return jass_pushnullhandle(j, "event");
    variable = gi.MemAlloc(strlen(varName) + 1); strcpy(variable, varName);
    evt->trigger = whichTrigger; evt->variable = variable; evt->limitop = *opcode; evt->limitval = limitval;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_VARIABLE_LIMIT, NULL, "variable");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterTimerEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    FLOAT timeout = jass_checknumber(j, 2);
    BOOL periodic = jass_checkboolean(j, 3);
    LPGTIMER timer;
    LPEVENT evt;
    if (!whichTrigger || !(timer = G_AllocJassTimer())) return jass_pushnullhandle(j, "event");
    G_TimerStart(timer, (DWORD)(MAX(0.0f, timeout) * 1000.0f), periodic, NULL);
    evt = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED); evt->trigger = whichTrigger; evt->timer = timer;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_TIMER_EXPIRED, NULL, "timer");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterTimerExpireEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPGTIMER timer = jass_checkhandle(j, 2, "timer");
    LPEVENT evt;
    if (!whichTrigger || !timer) return jass_pushnullhandle(j, "event");
    evt = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED); evt->trigger = whichTrigger; evt->timer = timer;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_TIMER_EXPIRED, NULL, "timer-expire");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterGameStateEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPDWORD whichState = jass_checkhandle(j, 2, "gamestate");
    LPDWORD opcode = jass_checkhandle(j, 3, "limitop");
    FLOAT limitval = jass_checknumber(j, 4);
    LPEVENT evt = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    evt->trigger = whichTrigger;
    evt->state = whichState ? *whichState : 0;
    evt->limitop = opcode ? *opcode : 0;
    evt->limitval = limitval;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_STATE_LIMIT, NULL, "game-state");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterDialogEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichDialog = jass_checkhandle(j, 2, "dialog");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterDialogButtonEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichButton = jass_checkhandle(j, 2, "button");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterGameEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichGameEvent = jass_checkhandle(j, 2, "gameevent");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterEnterRegion(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPREGION whichRegion = jass_checkhandle(j, 2, "region");
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    LPEVENT evt = G_MakeEvent(EVENT_GAME_ENTER_REGION);
    evt->trigger = whichTrigger;
    evt->region = *whichRegion;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_ENTER_REGION, NULL, "enter-region");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD GetTriggeringRegion(LPJASS j) {
    return jass_pushnullhandle(j, "region");
}
DWORD TriggerRegisterLeaveRegion(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichRegion = jass_checkhandle(j, 2, "region");
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterTrackableHitEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE t = jass_checkhandle(j, 2, "trackable");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterTrackableTrackEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE t = jass_checkhandle(j, 2, "trackable");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterPlayerEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    EVENTTYPE *whichPlayerEvent = jass_checkhandle(j, 3, "playerevent");
    LPEVENT evt = G_MakeEvent(*whichPlayerEvent);
    evt->subject = PLAYER_ENT(whichPlayer);
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, *whichPlayerEvent, evt->subject, "player");
    if (*whichPlayerEvent == EVENT_PLAYER_VICTORY || *whichPlayerEvent == EVENT_PLAYER_DEFEAT) {
        G_GameResultDebug("register player event type=%s player=%u trigger=%p subject_ent=%ld",
            *whichPlayerEvent == EVENT_PLAYER_VICTORY ? "VICTORY" : "DEFEAT",
            whichPlayer ? (unsigned)PLAYER_NUM(whichPlayer) : 0u,
            (void *)whichTrigger, evt->subject ? (long)evt->subject->s.number : -1L);
    }
    return jass_pushlighthandle(j, evt, "event");
}
DWORD GetTriggerPlayer(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->playerState, "player");
}
DWORD TriggerRegisterPlayerUnitEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    EVENTTYPE *whichPlayerUnitEvent = jass_checkhandle(j, 3, "playerunitevent");
    //HANDLE filter = jass_checkhandle(j, 4, "boolexpr");
    LPEVENT evt = G_MakeEvent(*whichPlayerUnitEvent);
    evt->subject = PLAYER_ENT(whichPlayer);
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, *whichPlayerUnitEvent, evt->subject, "player-unit");
    if (WC3_TUTORIAL_DEBUG_ENABLED() &&
        (*whichPlayerUnitEvent == EVENT_PLAYER_UNIT_CONSTRUCT_START ||
         *whichPlayerUnitEvent == EVENT_PLAYER_UNIT_CONSTRUCT_FINISH)) {
        fprintf(stderr,
                "WC3_QUEST_BUILD register via=player-unit event=%u trigger=%ld player=%d subject=%ld disabled=%d\n",
                (unsigned)*whichPlayerUnitEvent,
                whichTrigger ? (long)(whichTrigger - level.triggers) : -1L,
                whichPlayer ? (int)PLAYER_NUM(whichPlayer) : -1,
                evt->subject ? (long)(evt->subject - globals.edicts) : -1L,
                whichTrigger ? (int)whichTrigger->disabled : -1);
    }
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterPlayerAllianceChange(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE whichAlliance = jass_checkhandle(j, 3, "alliancetype");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterPlayerStateEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE whichState = jass_checkhandle(j, 3, "playerstate");
    //HANDLE opcode = jass_checkhandle(j, 4, "limitop");
    //FLOAT limitval = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterPlayerChatEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //LPCSTR chatMessageToDetect = jass_checkstring(j, 3);
    //BOOL exactMatchOnly = jass_checkboolean(j, 4);
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterDeathEvent(LPJASS j) {
    /* Fire whichTrigger when whichWidget dies.  "widget" is the base type of
     * unit/destructable/item, and a unit handle resolves to its edict; the
     * engine publishes EVENT_UNIT_DEATH for both units (m_unit.c) and trees
     * (m_tree.c), so registering on that type matches the same way
     * TriggerRegisterUnitEvent does.  G_ExecuteEvent's default case matches on
     * (subject, type), so this fires exactly when the registered widget dies. */
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPEDICT whichWidget = jass_checkhandle(j, 2, "widget");
    if (!whichTrigger || !whichWidget) return jass_pushnullhandle(j, "event");
    LPEVENT evt = G_MakeEvent(EVENT_UNIT_DEATH);
    evt->subject = whichWidget;
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_UNIT_DEATH, evt->subject, "death");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterUnitStateEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichUnit = jass_checkhandle(j, 2, "unit");
    //HANDLE whichState = jass_checkhandle(j, 3, "unitstate");
    //HANDLE opcode = jass_checkhandle(j, 4, "limitop");
    //FLOAT limitval = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterUnitEvent(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    EVENTTYPE *whichEvent = jass_checkhandle(j, 3, "unitevent");
    if (!whichTrigger || !whichUnit || !whichEvent) {
        return jass_pushnullhandle(j, "event");
    }
    LPEVENT evt = G_MakeEvent(*whichEvent);
    evt->subject = whichUnit;
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, *whichEvent, evt->subject, "unit");
    if (WC3_TUTORIAL_DEBUG_ENABLED() &&
        *whichEvent == EVENT_UNIT_CONSTRUCT_FINISH) {
        fprintf(stderr,
                "WC3_QUEST_BUILD register via=unit event=%u trigger=%ld unit=%ld id=%.4s disabled=%d\n",
                (unsigned)*whichEvent,
                whichTrigger ? (long)(whichTrigger - level.triggers) : -1L,
                whichUnit ? (long)(whichUnit - globals.edicts) : -1L,
                whichUnit ? (LPCSTR)&whichUnit->class_id : "----",
                whichTrigger ? (int)whichTrigger->disabled : -1);
    }
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerRegisterFilterUnitEvent(LPJASS j) {
    //LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    //HANDLE whichUnit = jass_checkhandle(j, 2, "unit");
    //HANDLE whichEvent = jass_checkhandle(j, 3, "unitevent");
    //HANDLE filter = jass_checkhandle(j, 4, "boolexpr");
    return jass_pushnullhandle(j, "event");
}
DWORD TriggerRegisterUnitInRange(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    FLOAT range = jass_checknumber(j, 3);
//    HANDLE filter = jass_checkhandle(j, 4, "boolexpr");
    if (!whichTrigger || !whichUnit) {
        return jass_pushnullhandle(j, "event");
    }
    LPEVENT evt = G_MakeEvent(EVENT_UNIT_IN_RANGE);
    evt->subject = whichUnit;
    evt->trigger = whichTrigger;
    evt->range = range;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_UNIT_IN_RANGE, evt->subject, "unit-in-range");
    return jass_pushlighthandle(j, evt, "event");
}
DWORD TriggerAddCondition(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    TRIGGERCONDITION *condition = gi.MemAlloc(sizeof(TRIGGERCONDITION));
    condition->expr = jass_checkhandle(j, 2, "boolexpr");
    ADD_TO_LIST(condition, whichTrigger->conditions);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCSTR func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCSTR func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        LPCSTR func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    return jass_pushlighthandle(j, condition, "triggercondition");
}
DWORD TriggerRemoveCondition(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    TRIGGERCONDITION *whichCondition = jass_checkhandle(j, 2, "triggercondition");
    REMOVE_FROM_LIST(TRIGGERCONDITION, whichCondition, whichTrigger->conditions, gi.MemFree);
    return 0;
}
DWORD TriggerClearConditions(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    DELETE_LIST(TRIGGERCONDITION, whichTrigger->conditions, gi.MemFree);
    return 0;
}
DWORD TriggerAddAction(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    TRIGGERACTION *action = gi.MemAlloc(sizeof(TRIGGERACTION));
    action->func = jass_checkcode(j, 2);
    ADD_TO_LIST(action, whichTrigger->actions);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCSTR func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCSTR func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        LPCSTR func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    return jass_pushlighthandle(j, action, "triggeraction");
}
DWORD TriggerRemoveAction(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    TRIGGERACTION *whichAction = jass_checkhandle(j, 2, "triggeraction");
    REMOVE_FROM_LIST(TRIGGERACTION, whichAction, whichTrigger->actions, gi.MemFree);
    return 0;
}
DWORD TriggerClearActions(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    DELETE_LIST(TRIGGERACTION, whichTrigger->actions, gi.MemFree);
    return 0;
}
DWORD TriggerSleepAction(LPJASS j) {
    FLOAT timeout = jass_checknumber(j, 1);
    LPCJASSCONTEXT ctx = jass_getcontext(j);
    if (G_SkipCutscene()) {
        timeout = MIN(timeout, 0.001f);
    }
    if (QuestPeonStageDebugEnabled() && ctx && TutorialFlowDebugTrigger(ctx->trigger)) {
        char chain[256];
        jass_formatcallchain(j, chain, sizeof(chain));
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW sleep trigger=%ld native=TriggerSleepAction msec=%lu chain=\"%s\"\n",
                (long)QuestPeonStageTriggerOrdinal(ctx->trigger),
                (unsigned long)(timeout * 1000.0f), chain);
    }
    jass_sleep(j, (DWORD)(timeout * 1000.0f));
    return 0;
}
DWORD TriggerWaitForSound(LPJASS j) {
    gsound_t *s = jass_checkhandle(j, 1, "sound");
    FLOAT offset = jass_checknumber(j, 2);
    LPCJASSCONTEXT ctx = jass_getcontext(j);
    DWORD wait_msec = G_SkipCutscene() ? 1 : s->duration + (DWORD)(offset * 1000.0f);
    if (QuestPeonStageDebugEnabled() && ctx && TutorialFlowDebugTrigger(ctx->trigger)) {
        char chain[256];
        jass_formatcallchain(j, chain, sizeof(chain));
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW sleep trigger=%ld native=TriggerWaitForSound sound_msec=%lu offset=%.3f wait_msec=%lu chain=\"%s\"\n",
                (long)QuestPeonStageTriggerOrdinal(ctx->trigger),
                (unsigned long)s->duration, offset, (unsigned long)wait_msec, chain);
    }
    jass_sleep(j, wait_msec);
    return 0;
}
DWORD TriggerEvaluate(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPCJASSCONTEXT context = jass_getcontext(j);
    BOOL result = jass_evaluatetrigger(j, whichTrigger, context ? context->unit : NULL);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON direct trigger=%ld op=evaluate caller=\"%s\" disabled=%d result=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled, result);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP direct trigger=%ld op=evaluate caller=\"%s\" disabled=%d result=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled, result);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW direct trigger=%ld op=evaluate caller=\"%s\" disabled=%d result=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)",
                (int)whichTrigger->disabled, result);
    }
    return jass_pushboolean(j, result);
}
DWORD TriggerExecute(LPJASS j) {
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    LPCJASSCONTEXT context = jass_getcontext(j);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON direct trigger=%ld op=execute caller=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        LPCJASSCONTEXT ctx = jass_getcontext(j);
        LPCSTR caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP direct trigger=%ld op=execute caller=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW direct trigger=%ld op=execute caller=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)",
                (int)whichTrigger->disabled);
    }
    jass_executetrigger(j, whichTrigger, context ? context->unit : NULL);
    return 0;
}
DWORD TriggerExecuteWait(LPJASS j) {
    /* TODO: Execute and yield until the trigger finishes once the coroutine scheduler supports waits. */
    LPTRIGGER whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW direct trigger=%ld op=execute-wait caller=\"%s\" implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return 0;
}
DWORD GetTriggerUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetTriggerWidget(LPJASS j) {
    /* The widget whose event fired this trigger.  Units/destructables/items are
     * all edicts, and the dispatcher passes the subject edict in as the context
     * unit (jass_executetrigger), so a death-registered trigger sees the dying
     * destructable here — e.g. SaveDyingWidget -> WidgetDropItem loot drops. */
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "widget");
}

DWORD GetTriggerDestructable(LPJASS j) {
    return jass_pushlighthandle(
        j,
        jass_getcontext(j)->unit,
        "destructable");
}
