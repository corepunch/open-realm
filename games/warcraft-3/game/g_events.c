#include "g_local.h"

BOOL jass_calltrigger(LPJASS j, LPTRIGGER trigger, LPEDICT unit);

static void G_ExecuteEvent(GAMEEVENT *evt) {
    LPEDICT subject = evt->edict;
    FOR_EACH_LIST(EVENT, e, level.events.handlers) {
        switch (e->type) {
            case EVENT_GAME_VICTORY:
                break;
            case EVENT_GAME_END_LEVEL:
                break;
            case EVENT_GAME_VARIABLE_LIMIT:
                break;
            case EVENT_GAME_STATE_LIMIT:
                break;
            case EVENT_GAME_TIMER_EXPIRED:
                break;
            case EVENT_GAME_ENTER_REGION:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject);
                }
                break;
            case EVENT_GAME_LEAVE_REGION:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject);
                }
                break;
            case EVENT_UNIT_IN_RANGE:
                if (evt->responseTo == e) {
                    jass_calltrigger(level.vm, e->trigger, subject);
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
                if (e->type == evt->type && subject &&
                    (e->subject == subject ||
                     e->subject == G_GetPlayerEntityByNumber(subject->s.player))) {
                    jass_calltrigger(level.vm, e->trigger, subject);
                }
                break;
        }
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
        ent->old_origin = ent->s.origin2;
        /* Clear one-shot event fields so each event fires for exactly one frame. */
        ent->s.event = EV_NONE;
        ent->s.sound = 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        G_RunEntity(globals.edicts+i);
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts+i;
        if (!memcmp(&ent->old_origin, &ent->s.origin2, sizeof(VECTOR2)))
            continue;
        G_TouchTriggers(ent);
    }
}

void G_RunEvents(void) {
    for (LEVELEVENTS *e = &level.events; e->read < e->write; e->read++) {
        GAMEEVENT *evt = &e->queue[e->read % MAX_EVENT_QUEUE];
        G_ExecuteEvent(evt);
    }
}
