#include "g_local.h"

BOOL jass_calltriggerevent(LPJASS j, LPTRIGGER trigger, GAMEEVENT const *event);

BOOL G_LimitMatches(DWORD op, FLOAT value, FLOAT limit) {
    switch (op) {
        case WC3_LIMITOP_LESS_THAN: return value < limit;
        case WC3_LIMITOP_LESS_THAN_OR_EQUAL: return value <= limit;
        case WC3_LIMITOP_EQUAL: return value == limit;
        case WC3_LIMITOP_GREATER_THAN_OR_EQUAL: return value >= limit;
        case WC3_LIMITOP_GREATER_THAN: return value > limit;
        case WC3_LIMITOP_NOT_EQUAL: return value != limit;
        default: return false;
    }
}

void G_JassVariableChanged(LPCSTR name, FLOAT before, FLOAT after) {
    FOR_EACH_EVENT(evt) {
        if (evt->type == EVENT_GAME_VARIABLE_LIMIT && evt->variable && !strcmp(evt->variable, name) &&
            !G_LimitMatches(evt->limitop, before, evt->limitval) && G_LimitMatches(evt->limitop, after, evt->limitval))
            G_PublishEvent(NULL, EVENT_GAME_VARIABLE_LIMIT)->responseTo = evt;
    }
}

/* One authoritative terminal-result transition shared by JASS RemovePlayer
 * and developer cheats.  Keep campaign/result presentation downstream of the
 * normal EVENT_PLAYER_VICTORY / EVENT_PLAYER_DEFEAT pipeline. */
BOOL G_RemovePlayerWithResult(DWORD player_num, DWORD game_result) {
    LPGAMECLIENT client;
    LPEDICT pent;

    if (player_num >= game.max_clients || game_result > 3) {
        G_GameResultDebug("RemovePlayer ignored reason=invalid_args player=%u result=%u",
            (unsigned)player_num, (unsigned)game_result);
        return false;
    }

    client = G_GetPlayerClientByNumber(player_num);
    G_GameResultDebug("RemovePlayer resolved player=%u client_index=%ld connected=%u removed=%u ui=%u",
        (unsigned)player_num,
        client ? (long)(client - game.clients) : -1L,
        client ? (unsigned)client->connected : 0u,
        client ? (unsigned)client->jass.removed : 0u,
        client ? (unsigned)client->ps.client_ui_state : 0u);

    if (!client) {
        G_GameResultDebug("RemovePlayer ignored player=%u reason=no_client", (unsigned)player_num);
        return false;
    }
    if (client->jass.removed) {
        G_GameResultDebug("RemovePlayer ignored player=%u reason=already_removed stored_result=%u",
            (unsigned)player_num, (unsigned)client->ps.stats[PLAYERSTATE_GAME_RESULT]);
        return false;
    }

    client->ps.stats[PLAYERSTATE_GAME_RESULT] = (USHORT)game_result;
    client->jass.removed = true;
    client->jass.pending_game_result = 0;
    client->jass.pending_game_result_event = level.events.read;
    G_BotRequestStop(player_num);

    pent = G_GetPlayerEntityByNumber(player_num);
    G_GameResultDebug("RemovePlayer state player=%u result=%u pent=%p ent=%ld inuse=%u owner=%u",
        (unsigned)player_num, (unsigned)game_result, (void *)pent,
        pent ? (long)pent->s.number : -1L,
        pent ? (unsigned)pent->inuse : 0u,
        pent ? (unsigned)pent->s.player : 0u);
    if (!pent) {
        G_GameResultDebug("RemovePlayer player=%u result=%u has no player edict; no result event/UI queued",
            (unsigned)player_num, (unsigned)game_result);
        return true;
    }

    if (game_result == 0) {
        G_PublishEvent(pent, EVENT_PLAYER_VICTORY);
        client->jass.pending_game_result = 1;
        client->jass.pending_game_result_event = level.events.write;
        G_GameResultDebug("RemovePlayer queued VICTORY player=%u wait_event=%u events=%u/%u",
            (unsigned)player_num, (unsigned)client->jass.pending_game_result_event,
            (unsigned)level.events.read, (unsigned)level.events.write);
    } else if (game_result == 1) {
        G_PublishEvent(pent, EVENT_PLAYER_DEFEAT);
        client->jass.pending_game_result = 2;
        client->jass.pending_game_result_event = level.events.write;
        G_GameResultDebug("RemovePlayer queued DEFEAT player=%u wait_event=%u events=%u/%u",
            (unsigned)player_num, (unsigned)client->jass.pending_game_result_event,
            (unsigned)level.events.read, (unsigned)level.events.write);
    } else {
        G_GameResultDebug("RemovePlayer player=%u result=%u records removal only; no victory/defeat UI",
            (unsigned)player_num, (unsigned)game_result);
    }
    return true;
}

static void G_ExecuteEvent(GAMEEVENT *evt) {
    LPEDICT subject = evt->edict;
    BOOL result_event = evt->type == EVENT_PLAYER_VICTORY || evt->type == EVENT_PLAYER_DEFEAT;
    DWORD matching_handlers = 0, invoked_handlers = 0;

    if (result_event) {
        G_GameResultDebug("execute event type=%s subject_ent=%ld owner=%u",
            evt->type == EVENT_PLAYER_VICTORY ? "VICTORY" : "DEFEAT",
            subject ? (long)subject->s.number : -1L,
            subject ? (unsigned)subject->s.player : 0u);
    }

    FOR_EACH_EVENT(e) {
        switch (e->type) {
            case EVENT_GAME_VICTORY:
                break;
            case EVENT_GAME_END_LEVEL:
                break;
            case EVENT_GAME_VARIABLE_LIMIT:
                if (evt->responseTo == e) jass_calltriggerevent(level.vm, e->trigger, evt);
                break;
            case EVENT_GAME_STATE_LIMIT:
                if (evt->responseTo == e) {
                    jass_calltriggerevent(level.vm, e->trigger, evt);
                }
                break;
            case EVENT_GAME_TIMER_EXPIRED:
                break;
            case EVENT_GAME_ENTER_REGION:
                if (evt->responseTo == e) {
                    jass_calltriggerevent(level.vm, e->trigger, evt);
                }
                break;
            case EVENT_GAME_LEAVE_REGION:
                if (evt->responseTo == e) {
                    jass_calltriggerevent(level.vm, e->trigger, evt);
                }
                break;
            case EVENT_UNIT_IN_RANGE:
                if (evt->responseTo == e) {
                    jass_calltriggerevent(level.vm, e->trigger, evt);
                }
                break;
            case EVENT_GAME_TRACKABLE_HIT:
                break;
            case EVENT_GAME_TRACKABLE_TRACK:
                break;
            case EVENT_GAME_SHOW_SKILL:
                break;
            case EVENT_GAME_BUILD_SUBMENU:
                break;
            default:
                /* Two subject conventions share this path:
                 *  - widget/unit events (e.g. EVENT_UNIT_DEATH): the handler's
                 *    subject is a specific unit, matched directly.
                 *  - player-unit events (EVENT_PLAYER_UNIT_*): registered via
                 *    TriggerRegisterPlayerUnitEvent with subject = the player's
                 *    edict; they fire for ANY of that player's units, so match
                 *    the dying/triggering unit's owner against the handler's
                 *    player.  Either way the triggering unit is passed as the
                 *    context unit so GetTriggerUnit()/GetDyingUnit() resolve to
                 *    it (e.g. Naga_Victory_Check counts the dying naga). */
                if (e->type == evt->type) {
                    BOOL direct = subject && e->subject == subject;
                    BOOL owner_match = subject &&
                        e->subject == G_GetPlayerEntityByNumber(subject->s.player);
                    LONG quest_trigger_ordinal = e->trigger
                        ? (LONG)(e->trigger - level.triggers) : -1L;
                    BOOL quest_peon_stage = gi.CvarString &&
                        WC3_TUTORIAL_DEBUG_ENABLED() &&
                        quest_trigger_ordinal >= 95 && quest_trigger_ordinal <= 106;
                    BOOL subgroup_stage = gi.CvarString &&
                        WC3_TUTORIAL_DEBUG_ENABLED() &&
                        quest_trigger_ordinal >= 208 && quest_trigger_ordinal <= 213;
                    BOOL quest_build_event = gi.CvarString &&
                        WC3_TUTORIAL_DEBUG_ENABLED() &&
                        (evt->type == EVENT_PLAYER_UNIT_CONSTRUCT_START ||
                         evt->type == EVENT_PLAYER_UNIT_CONSTRUCT_FINISH ||
                         evt->type == EVENT_UNIT_CONSTRUCT_FINISH);
                    if (quest_build_event) {
                        fprintf(stderr,
                                "WC3_QUEST_BUILD dispatch event=%u trigger=%ld building=%ld id=%.4s owner=%u handler_subject=%ld direct=%d owner_match=%d match=%d disabled=%d\n",
                                (unsigned)evt->type, (long)quest_trigger_ordinal,
                                subject ? (long)(subject - globals.edicts) : -1L,
                                subject ? (LPCSTR)&subject->class_id : "----",
                                subject ? (unsigned)subject->s.player : 0u,
                                e->subject ? (long)(e->subject - globals.edicts) : -1L,
                                direct, owner_match, direct || owner_match,
                                e->trigger ? (int)e->trigger->disabled : -1);
                    }
                    if (subgroup_stage) {
                        fprintf(stderr,
                                "WC3_SUBGROUP dispatch event=%u trigger=%ld subject=%ld id=%.4s owner=%u source=%ld source_id=%.4s handler_subject=%ld direct=%d owner_match=%d match=%d disabled=%d\n",
                                (unsigned)evt->type, (long)quest_trigger_ordinal,
                                subject ? (long)(subject - globals.edicts) : -1L,
                                subject ? (LPCSTR)&subject->class_id : "----",
                                subject ? (unsigned)subject->s.player : 0u,
                                evt->source ? (long)(evt->source - globals.edicts) : -1L,
                                evt->source ? (LPCSTR)&evt->source->class_id : "----",
                                e->subject ? (long)(e->subject - globals.edicts) : -1L,
                                direct, owner_match, direct || owner_match,
                                e->trigger ? (int)e->trigger->disabled : -1);
                    }
                    if (quest_peon_stage) {
                        fprintf(stderr,
                                "WC3_QUEST_PEON dispatch event=%u trigger=%ld unit=%ld id=%.4s owner=%u source=%ld source_id=%.4s handler_subject=%ld direct=%d owner_match=%d match=%d disabled=%d\n",
                                (unsigned)evt->type, (long)quest_trigger_ordinal,
                                subject ? (long)(subject - globals.edicts) : -1L,
                                subject ? (LPCSTR)&subject->class_id : "----",
                                subject ? (unsigned)subject->s.player : 0u,
                                evt->source ? (long)(evt->source - globals.edicts) : -1L,
                                evt->source ? (LPCSTR)&evt->source->class_id : "----",
                                e->subject ? (long)(e->subject - globals.edicts) : -1L,
                                direct, owner_match, direct || owner_match,
                                e->trigger ? (int)e->trigger->disabled : -1);
                    }
                    if (result_event) {
                        matching_handlers++;
                        G_GameResultDebug("event handler candidate trigger=%p handler_subject=%ld direct=%u owner_match=%u",
                            (void *)e->trigger,
                            e->subject ? (long)e->subject->s.number : -1L,
                            (unsigned)direct, (unsigned)owner_match);
                    }
                    if (direct || owner_match) {
                        BOOL queued = jass_calltriggerevent(level.vm, e->trigger, evt);
                        if (quest_build_event) {
                            fprintf(stderr,
                                    "WC3_QUEST_BUILD dispatch-result event=%u trigger=%ld queued=%d disabled=%d\n",
                                    (unsigned)evt->type, (long)quest_trigger_ordinal, queued,
                                    e->trigger ? (int)e->trigger->disabled : -1);
                        }
                        if (subgroup_stage) {
                            fprintf(stderr,
                                    "WC3_SUBGROUP dispatch-result event=%u trigger=%ld queued=%d disabled=%d\n",
                                    (unsigned)evt->type, (long)quest_trigger_ordinal, queued,
                                    e->trigger ? (int)e->trigger->disabled : -1);
                        }
                        if (quest_peon_stage) {
                            fprintf(stderr,
                                    "WC3_QUEST_PEON dispatch-result event=%u trigger=%ld queued=%d disabled=%d\n",
                                    (unsigned)evt->type, (long)quest_trigger_ordinal,
                                    queued, e->trigger ? (int)e->trigger->disabled : -1);
                        }
                        if (result_event) {
                            invoked_handlers++;
                            G_GameResultDebug("event handler dispatch trigger=%p queued=%u",
                                (void *)e->trigger, (unsigned)queued);
                        }
                    }
                }
                break;
        }
    }
    if (result_event) {
        G_GameResultDebug("execute event complete type=%s matching_handlers=%u invoked_handlers=%u",
            evt->type == EVENT_PLAYER_VICTORY ? "VICTORY" : "DEFEAT",
            (unsigned)matching_handlers, (unsigned)invoked_handlers);
    }
}

static void G_TouchTriggers(LPEDICT ent) {
    FOR_EACH_EVENT(evt) {
        switch (evt->type) {
            case EVENT_GAME_ENTER_REGION:
                if (G_RegionContains(&evt->region, &ent->s.origin2) &&
                    !G_RegionContains(&evt->region, &ent->old_origin))
                {
                    G_PublishEvent(ent, evt->type)->responseTo = evt;
                }
                break;
            case EVENT_GAME_LEAVE_REGION:
                if (!G_RegionContains(&evt->region, &ent->s.origin2) &&
                    G_RegionContains(&evt->region, &ent->old_origin))
                {
                    G_PublishEvent(ent, evt->type)->responseTo = evt;
                }
                break;
            case EVENT_UNIT_IN_RANGE:
                if (ent != evt->subject &&
                    Vector2_distance(&((LPEDICT)evt->subject)->old_origin, &ent->old_origin) > evt->range &&
                    Vector2_distance(&((LPEDICT)evt->subject)->s.origin2, &ent->s.origin2) <= evt->range)
                {
                    GAMEEVENT *e = G_PublishEvent(ent, evt->type);
                    e->edict = ent;
                    e->responseTo = evt;
                }
                break;
            default:
                break;
        }
    }
}

void G_RunEntities(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts+i;
        if (!ent->inuse) continue; /* freed edicts are memset and never re-sent; skip the per-frame clear */
        ent->old_origin = ent->s.origin2;
        if (ent->sound.pending) {
            gi.Sound(ent, CHAN_VOICE | CHAN_OWNER | CHAN_RELIABLE, ent->sound.pending, 1.0f, 0.0f, 0.0f);
            ent->sound.pending = 0;
        }
        if (ent->sound.owner_pending) {
            gi.Sound(ent, CHAN_VOICE | CHAN_OWNER | CHAN_RELIABLE, ent->sound.owner_pending, 1.0f, 0.0f, 0.0f);
            ent->sound.owner_pending = 0;
        }
        if (ent->sound.world_pending) {
            gi.Sound(ent, CHAN_VOICE, ent->sound.world_pending, 1.0f, 1.0f, 0.0f);
            ent->sound.world_pending = 0;
            ent->sound.world_pending_event = EV_NONE;
        }
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts+i;
        if (!ent->inuse) continue;
        G_RunEntity(ent);
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts+i;
        if (!ent->inuse) continue;
        if (!memcmp(&ent->old_origin, &ent->s.origin2, sizeof(VECTOR2)))
            continue;
        G_TouchTriggers(ent);
    }
}

void G_RunEvents(void) {
    for (LEVELEVENTS *e = &level.events; e->read < e->write; e->read++) {
        GAMEEVENT *evt = &e->queue[e->read % MAX_EVENT_QUEUE];
        if (evt->type == EVENT_PLAYER_VICTORY || evt->type == EVENT_PLAYER_DEFEAT) {
            G_GameResultDebug("run event ordinal=%u/%u type=%s",
                (unsigned)(e->read + 1), (unsigned)e->write,
                evt->type == EVENT_PLAYER_VICTORY ? "VICTORY" : "DEFEAT");
        }
        G_ExecuteEvent(evt);
    }
}

/* Warcraft's stock CustomVictoryDialogBJ/CustomDefeatDialogBJ pauses a
 * single-player game after RemovePlayer().  If RemovePlayer ran from a JASS
 * action queued by this frame's first event pass, its result event was
 * published too late for that pass and the server scheduler will stop before
 * the next frame.  Drain only result events that are actively blocking a
 * pending result handoff; repeat for chained player removals, bounded by the
 * number of player slots. */
void G_DrainPausedResultEvents(void) {
    DWORD passes = 0;

    if (!level.script_paused || !level.vm) return;

    while (passes++ < MAX_PLAYERS) {
        BOOL waiting = false;

        FOR_LOOP(i, game.max_clients) {
            LPGAMECLIENT client = game.clients + i;
            if (client->jass.pending_game_result &&
                level.events.read < client->jass.pending_game_result_event) {
                waiting = true;
                break;
            }
        }
        if (!waiting) return;

        G_GameResultDebug("frame drain paused result events pass=%u events=%u/%u",
            (unsigned)passes, (unsigned)level.events.read, (unsigned)level.events.write);
        G_RunEvents();
        jass_runevents(level.vm);
    }

    G_GameResultDebug("frame drain paused result events stopped after %u passes events=%u/%u",
        (unsigned)MAX_PLAYERS, (unsigned)level.events.read, (unsigned)level.events.write);
}
