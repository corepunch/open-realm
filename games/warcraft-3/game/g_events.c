#include "g_local.h"

BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit, LPEDICT source);

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

    FOR_EACH_LIST(EVENT, e, level.events.handlers) {
        switch (e->type) {
            case EVENT_GAME_VICTORY:
                break;
            case EVENT_GAME_END_LEVEL:
                break;
            case EVENT_GAME_VARIABLE_LIMIT:
                break;
            case EVENT_GAME_STATE_LIMIT:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, NULL, NULL);
                }
                break;
            case EVENT_GAME_TIMER_EXPIRED:
                break;
            case EVENT_GAME_ENTER_REGION:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject, evt->source);
                }
                break;
            case EVENT_GAME_LEAVE_REGION:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject, evt->source);
                }
                break;
            case EVENT_UNIT_IN_RANGE:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject, evt->source);
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
                    if (result_event) {
                        matching_handlers++;
                        G_GameResultDebug("event handler candidate trigger=%p handler_subject=%ld direct=%u owner_match=%u",
                            (void *)e->trigger,
                            e->subject ? (long)e->subject->s.number : -1L,
                            (unsigned)direct, (unsigned)owner_match);
                    }
                    if (direct || owner_match) {
                        BOOL queued = jass_calltrigger(level.vm, e->trigger, subject, evt->source);
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
    FOR_EACH_LIST(EVENT, evt, level.events.handlers) {
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
