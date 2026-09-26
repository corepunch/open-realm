static bool QuestPeonStageDebugEnabled(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

static int32_t QuestPeonStageTriggerOrdinal(trigger_t * trigger) {
    return trigger ? (int32_t)(trigger - level.triggers) : -1L;
}

static bool SubgroupDebugTrigger(trigger_t * trigger) {
    int32_t ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 208 && ordinal <= 213;
}

static bool TutorialFlowDebugTrigger(trigger_t * trigger) {
    int32_t ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 120 && ordinal <= 165;
}

static void TutorialFlowDebugLogRegistration(trigger_t * trigger, EVENTTYPE type,
                                             edict_t * subject, cstring_t registration) {
    if (!QuestPeonStageDebugEnabled() || !TutorialFlowDebugTrigger(trigger)) return;
    fprintf(stderr,
            "WC3_TUTORIAL_FLOW register trigger=%ld via=%s event=%u subject=%ld disabled=%d\n",
            (long)QuestPeonStageTriggerOrdinal(trigger),
            registration ? registration : "unknown", (unsigned)type,
            subject ? (long)(subject - globals.edicts) : -1L,
            trigger ? (int)trigger->disabled : -1);
}

static void SubgroupDebugLogRegistration(trigger_t * trigger, EVENTTYPE type,
                                         edict_t * subject, cstring_t registration) {
    if (!QuestPeonStageDebugEnabled() || !SubgroupDebugTrigger(trigger)) return;
    fprintf(stderr,
            "WC3_SUBGROUP register trigger=%ld via=%s event=%u subject=%ld disabled=%d\n",
            (long)QuestPeonStageTriggerOrdinal(trigger),
            registration ? registration : "unknown", (unsigned)type,
            subject ? (long)(subject - globals.edicts) : -1L,
            trigger ? (int)trigger->disabled : -1);
}

static bool QuestPeonStageTrigger(trigger_t * trigger) {
    int32_t ordinal = QuestPeonStageTriggerOrdinal(trigger);
    return ordinal >= 95 && ordinal <= 106;
}

static void QuestPeonStageLogRegistration(trigger_t * trigger, EVENTTYPE type,
                                          edict_t * subject, cstring_t registration) {
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

uint32_t CreateTrigger(jass_t * j) {
    trigger_t * trigger = G_AllocJassTrigger();
    if (!trigger) { jass_rterror(j, "CreateTrigger: trigger registry is full"); return 0; }
    return jass_pushlighthandle(j, trigger, "trigger");
}
uint32_t DestroyTrigger(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    return 0;
}
uint32_t ResetTrigger(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    return 0;
}
uint32_t EnableTrigger(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON state trigger=%ld op=enable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
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
uint32_t DisableTrigger(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON state trigger=%ld op=disable caller=\"%s\" was_disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
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
uint32_t IsTriggerEnabled(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    bool enabled = !whichTrigger->disabled;
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
uint32_t TriggerWaitOnSleeps(jass_t * j) {
    /* TODO: Store the per-trigger wait-on-sleeps flag once coroutine suspension exposes that state. */
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    bool flag = jass_checkboolean(j, 2);
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW wait-on-sleeps trigger=%ld flag=%d caller=\"%s\" implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger), (int)flag,
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return 0;
}
uint32_t IsTriggerWaitOnSleeps(jass_t * j) {
    /* TODO: Return the stored per-trigger wait-on-sleeps flag once coroutine suspension exposes that state. */
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW is-wait-on-sleeps trigger=%ld caller=\"%s\" result=0 implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return jass_pushboolean(j, 0);
}
uint32_t GetTriggeringTrigger(jass_t * j) {
    jassContext_t const * ctx = jass_getcontext(j);
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
uint32_t GetTriggerEventId(jass_t * j) {
    return jass_pushnullhandle(j, "eventid");
}
uint32_t GetTriggerEvalCount(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    return jass_pushinteger(j, 0);
}
uint32_t GetTriggerExecCount(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    return jass_pushinteger(j, 0);
}
/* Registrations own their subject/filter/limit data. Dispatch installs event
 * response context before conditions and actions, then restores it for nested
 * triggers; state-limit events fire on the qualifying transition. */
uint32_t TriggerRegisterVariableEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    cstring_t varName = jass_checkstring(j, 2);
    string_t variable;
    uint32_t * opcode = jass_checkhandle(j, 3, "limitop");
    float limitval = jass_checknumber(j, 4);
    event_t * evt;
    if (!whichTrigger || !varName || !opcode) return jass_pushnullhandle(j, "event");
    evt = G_MakeEvent(EVENT_GAME_VARIABLE_LIMIT);
    if (!evt) return jass_pushnullhandle(j, "event");
    variable = gi.MemAlloc(strlen(varName) + 1); strcpy(variable, varName);
    evt->trigger = whichTrigger; evt->variable = variable; evt->limitop = *opcode; evt->limitval = limitval;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_VARIABLE_LIMIT, NULL, "variable");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterTimerEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    float timeout = jass_checknumber(j, 2);
    bool periodic = jass_checkboolean(j, 3);
    gtimer_t * timer;
    event_t * evt;
    if (!whichTrigger || !(timer = G_AllocJassTimer())) return jass_pushnullhandle(j, "event");
    G_TimerStart(timer, (uint32_t)(MAX(0.0f, timeout) * 1000.0f), periodic, NULL);
    evt = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED); evt->trigger = whichTrigger; evt->timer = timer;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_TIMER_EXPIRED, NULL, "timer");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterTimerExpireEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gtimer_t * timer = jass_checkhandle(j, 2, "timer");
    event_t * evt;
    if (!whichTrigger || !timer) return jass_pushnullhandle(j, "event");
    evt = G_MakeEvent(EVENT_GAME_TIMER_EXPIRED); evt->trigger = whichTrigger; evt->timer = timer;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_TIMER_EXPIRED, NULL, "timer-expire");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterGameStateEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    uint32_t * whichState = jass_checkhandle(j, 2, "gamestate");
    uint32_t * opcode = jass_checkhandle(j, 3, "limitop");
    float limitval = jass_checknumber(j, 4);
    event_t * evt = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    evt->trigger = whichTrigger;
    evt->state = whichState ? *whichState : 0;
    evt->limitop = opcode ? *opcode : 0;
    evt->limitval = limitval;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_STATE_LIMIT, NULL, "game-state");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterDialogEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t whichDialog = jass_checkhandle(j, 2, "dialog");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterDialogButtonEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t whichButton = jass_checkhandle(j, 2, "button");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterGameEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t whichGameEvent = jass_checkhandle(j, 2, "gameevent");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterEnterRegion(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    handle_t region = jass_checkhandle(j, 2, "region");
    region_t * whichRegion = G_RegionFromHandle(region);
    jassFunc_t const * filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichTrigger || !whichRegion || !whichRegion->inuse) return jass_pushnullhandle(j, "event");
    event_t * evt = G_MakeEvent(EVENT_GAME_ENTER_REGION);
    if (!evt) return jass_pushnullhandle(j, "event");
    evt->trigger = whichTrigger;
    evt->filter = filter;
    evt->region = region;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_ENTER_REGION, NULL, "enter-region");
    return jass_pushlighthandle(j, G_EventHandle(evt), "event");
}
uint32_t GetTriggeringRegion(jass_t * j) {
    jassContext_t const * context = jass_getcontext(j);
    handle_t region = context ? context->region : NULL;
    return region ? jass_pushlighthandle(j, region, "region") : jass_pushnullhandle(j, "region");
}
uint32_t TriggerRegisterLeaveRegion(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    handle_t region = jass_checkhandle(j, 2, "region");
    region_t * whichRegion = G_RegionFromHandle(region);
    jassFunc_t const * filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichTrigger || !whichRegion || !whichRegion->inuse) return jass_pushnullhandle(j, "event");
    event_t * evt = G_MakeEvent(EVENT_GAME_LEAVE_REGION);
    if (!evt) return jass_pushnullhandle(j, "event");
    evt->trigger = whichTrigger;
    evt->filter = filter;
    evt->region = region;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_LEAVE_REGION, NULL, "leave-region");
    return jass_pushlighthandle(j, G_EventHandle(evt), "event");
}
uint32_t TriggerRegisterTrackableHitEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t t = jass_checkhandle(j, 2, "trackable");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterTrackableTrackEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t t = jass_checkhandle(j, 2, "trackable");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterPlayerEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    player_t * whichPlayer = jass_checkhandle(j, 2, "player");
    EVENTTYPE *whichPlayerEvent = jass_checkhandle(j, 3, "playerevent");
    event_t * evt = G_MakeEvent(*whichPlayerEvent);
    G_SetPlayerEventSubject(evt, PLAYER_ENT(whichPlayer));
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
uint32_t GetTriggerPlayer(jass_t * j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->playerState, "player");
}
uint32_t TriggerRegisterPlayerUnitEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    player_t * whichPlayer = jass_checkhandle(j, 2, "player");
    EVENTTYPE *whichPlayerUnitEvent = jass_checkhandle(j, 3, "playerunitevent");
    //handle_t filter = jass_checkhandle(j, 4, "boolexpr");
    event_t * evt = G_MakeEvent(*whichPlayerUnitEvent);
    G_SetPlayerEventSubject(evt, PLAYER_ENT(whichPlayer));
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
uint32_t TriggerRegisterPlayerAllianceChange(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //player_t * whichPlayer = jass_checkhandle(j, 2, "player");
    //handle_t whichAlliance = jass_checkhandle(j, 3, "alliancetype");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterPlayerStateEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //player_t * whichPlayer = jass_checkhandle(j, 2, "player");
    //handle_t whichState = jass_checkhandle(j, 3, "playerstate");
    //handle_t opcode = jass_checkhandle(j, 4, "limitop");
    //float limitval = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterPlayerChatEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //player_t * whichPlayer = jass_checkhandle(j, 2, "player");
    //cstring_t chatMessageToDetect = jass_checkstring(j, 3);
    //bool exactMatchOnly = jass_checkboolean(j, 4);
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterDeathEvent(jass_t * j) {
    /* Fire whichTrigger when whichWidget dies.  "widget" is the base type of
     * unit/destructable/item, and a unit handle resolves to its edict; the
     * engine publishes EVENT_UNIT_DEATH for both units (m_unit.c) and trees
     * (m_tree.c), so registering on that type matches the same way
     * TriggerRegisterUnitEvent does.  G_ExecuteEvent's default case matches on
     * (subject, type), so this fires exactly when the registered widget dies. */
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    edict_t * whichWidget = jass_checkhandle(j, 2, "widget");
    if (!whichTrigger || !whichWidget) return jass_pushnullhandle(j, "event");
    event_t * evt = G_MakeEvent(EVENT_UNIT_DEATH);
    G_SetEventSubject(evt, whichWidget);
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_UNIT_DEATH, evt->subject, "death");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterUnitStateEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    edict_t * whichUnit = jass_checkhandle(j, 2, "unit");
    uint32_t * whichState = jass_checkhandle(j, 3, "unitstate");
    uint32_t * opcode = jass_checkhandle(j, 4, "limitop");
    float limitval = jass_checknumber(j, 5);
    if (!whichTrigger || !whichUnit || !whichState || !opcode)
        return jass_pushnullhandle(j, "event");
    if (*whichState != UNIT_STATE_LIFE) {
        fprintf(stderr, "WC3 TriggerRegisterUnitStateEvent: unsupported state %u\n", (unsigned)*whichState);
        return jass_pushnullhandle(j, "event");
    }
    if (*opcode > WC3_LIMITOP_NOT_EQUAL) {
        fprintf(stderr, "WC3 TriggerRegisterUnitStateEvent: unsupported limit operator %u\n", (unsigned)*opcode);
        return jass_pushnullhandle(j, "event");
    }
    event_t * evt = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    if (!evt) return jass_pushnullhandle(j, "event");
    evt->trigger = whichTrigger;
    G_SetEventSubject(evt, whichUnit);
    evt->state = *whichState;
    evt->limitop = *opcode;
    evt->limitval = limitval;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_GAME_STATE_LIMIT, whichUnit, "unit-state");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterUnitEvent(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    edict_t * whichUnit = jass_checkhandle(j, 2, "unit");
    EVENTTYPE *whichEvent = jass_checkhandle(j, 3, "unitevent");
    if (!whichTrigger || !whichUnit || !whichEvent) {
        return jass_pushnullhandle(j, "event");
    }
    event_t * evt = G_MakeEvent(*whichEvent);
    G_SetEventSubject(evt, whichUnit);
    evt->trigger = whichTrigger;
    QuestPeonStageLogRegistration(whichTrigger, *whichEvent, evt->subject, "unit");
    if (WC3_TUTORIAL_DEBUG_ENABLED() &&
        *whichEvent == EVENT_UNIT_CONSTRUCT_FINISH) {
        fprintf(stderr,
                "WC3_QUEST_BUILD register via=unit event=%u trigger=%ld unit=%ld id=%.4s disabled=%d\n",
                (unsigned)*whichEvent,
                whichTrigger ? (long)(whichTrigger - level.triggers) : -1L,
                whichUnit ? (long)(whichUnit - globals.edicts) : -1L,
                whichUnit ? (cstring_t)&whichUnit->class_id : "----",
                whichTrigger ? (int)whichTrigger->disabled : -1);
    }
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerRegisterFilterUnitEvent(jass_t * j) {
    //trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    //handle_t whichUnit = jass_checkhandle(j, 2, "unit");
    //handle_t whichEvent = jass_checkhandle(j, 3, "unitevent");
    //handle_t filter = jass_checkhandle(j, 4, "boolexpr");
    return jass_pushnullhandle(j, "event");
}
uint32_t TriggerRegisterUnitInRange(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    edict_t * whichUnit = jass_checkhandle(j, 2, "unit");
    float range = jass_checknumber(j, 3);
//    handle_t filter = jass_checkhandle(j, 4, "boolexpr");
    if (!whichTrigger || !whichUnit) {
        return jass_pushnullhandle(j, "event");
    }
    event_t * evt = G_MakeEvent(EVENT_UNIT_IN_RANGE);
    G_SetEventSubject(evt, whichUnit);
    evt->trigger = whichTrigger;
    evt->range = range;
    QuestPeonStageLogRegistration(whichTrigger, EVENT_UNIT_IN_RANGE, evt->subject, "unit-in-range");
    return jass_pushlighthandle(j, evt, "event");
}
uint32_t TriggerAddCondition(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gTriggerCondition_t *condition = gi.MemAlloc(sizeof(gTriggerCondition_t));
    condition->expr = jass_checkhandle(j, 2, "boolexpr");
    ADD_TO_LIST(condition, whichTrigger->conditions);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        cstring_t func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        cstring_t func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        cstring_t func = condition->expr ? jass_functionname(condition->expr) : NULL;
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW definition trigger=%ld add=condition func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    return jass_pushlighthandle(j, condition, "triggercondition");
}
uint32_t TriggerRemoveCondition(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gTriggerCondition_t *whichCondition = jass_checkhandle(j, 2, "triggercondition");
    REMOVE_FROM_LIST(gTriggerCondition_t, whichCondition, whichTrigger->conditions, gi.MemFree);
    return 0;
}
uint32_t TriggerClearConditions(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    DELETE_LIST(gTriggerCondition_t, whichTrigger->conditions, gi.MemFree);
    return 0;
}
uint32_t TriggerAddAction(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gTriggerAction_t *action = gi.MemAlloc(sizeof(gTriggerAction_t));
    action->func = jass_checkcode(j, 2);
    ADD_TO_LIST(action, whichTrigger->actions);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        cstring_t func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        cstring_t func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_SUBGROUP definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        cstring_t func = action->func ? jass_functionname(action->func) : NULL;
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW definition trigger=%ld add=action func=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                func ? func : "(anonymous)", (int)whichTrigger->disabled);
    }
    return jass_pushlighthandle(j, action, "triggeraction");
}
uint32_t TriggerRemoveAction(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gTriggerAction_t *whichAction = jass_checkhandle(j, 2, "triggeraction");
    REMOVE_FROM_LIST(gTriggerAction_t, whichAction, whichTrigger->actions, gi.MemFree);
    return 0;
}
uint32_t TriggerClearActions(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    gTriggerAction_t *actions = whichTrigger->actions;
    whichTrigger->actions = NULL;
    DELETE_LIST(gTriggerAction_t, actions, gi.MemFree);
    return 0;
}
uint32_t TriggerSleepAction(jass_t * j) {
    float timeout = jass_checknumber(j, 1);
    jassContext_t const * ctx = jass_getcontext(j);
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
    jass_sleep(j, (uint32_t)(timeout * 1000.0f));
    return 0;
}
uint32_t TriggerWaitForSound(jass_t * j) {
    gsound_t *s = jass_checkhandle(j, 1, "sound");
    float offset = jass_checknumber(j, 2);
    jassContext_t const * ctx = jass_getcontext(j);
    uint32_t wait_msec = G_SkipCutscene() ? 1 : s->duration + (uint32_t)(offset * 1000.0f);
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
uint32_t TriggerEvaluate(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    jassContext_t const * context = jass_getcontext(j);
    bool result = jass_evaluatetrigger(j, whichTrigger, context ? context->unit : NULL);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON direct trigger=%ld op=evaluate caller=\"%s\" disabled=%d result=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled, result);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
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
uint32_t TriggerExecute(jass_t * j) {
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    jassContext_t const * context = jass_getcontext(j);
    if (QuestPeonStageDebugEnabled() && QuestPeonStageTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
        fprintf(stderr,
                "WC3_QUEST_PEON direct trigger=%ld op=execute caller=\"%s\" disabled=%d\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                caller ? caller : "(native/root)", (int)whichTrigger->disabled);
    }
    if (QuestPeonStageDebugEnabled() && SubgroupDebugTrigger(whichTrigger)) {
        jassContext_t const * ctx = jass_getcontext(j);
        cstring_t caller = ctx ? jass_functionname(ctx->func) : NULL;
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
uint32_t TriggerExecuteWait(jass_t * j) {
    /* TODO: Execute and yield until the trigger finishes once the coroutine scheduler supports waits. */
    trigger_t * whichTrigger = jass_checkhandle(j, 1, "trigger");
    if (QuestPeonStageDebugEnabled() && TutorialFlowDebugTrigger(whichTrigger)) {
        fprintf(stderr,
                "WC3_TUTORIAL_FLOW direct trigger=%ld op=execute-wait caller=\"%s\" implementation=stub\n",
                (long)QuestPeonStageTriggerOrdinal(whichTrigger),
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(native/root)");
    }
    return 0;
}
uint32_t GetTriggerUnit(jass_t * j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
uint32_t GetTriggerWidget(jass_t * j) {
    /* The widget whose event fired this trigger.  Units/destructables/items are
     * all edicts, and the dispatcher passes the subject edict in as the context
     * unit (jass_executetrigger), so a death-registered trigger sees the dying
     * destructable here — e.g. SaveDyingWidget -> WidgetDropItem loot drops. */
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "widget");
}

uint32_t GetTriggerDestructable(jass_t * j) {
    return jass_pushlighthandle(
        j,
        jass_getcontext(j)->unit,
        "destructable");
}
