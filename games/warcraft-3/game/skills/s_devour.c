/* Devour (Adev/ACdv): physical swallow, off-world victim and digest lifecycle.
 * A save-serialized identity thinker owns exactly one victim. It is independent
 * of ordinary Cargo Hold and of spell channels: the caster may fight and move
 * while digestion continues. */
#include "s_skills.h"

#define DEVOUR_TICK_MS 1000 // milliseconds; Dev2 is authored in hit points per second
#define ID_DEVOUR MAKEFOURCC('A','d','e','v')
#define ID_CREEP_DEVOUR MAKEFOURCC('A','C','d','v')

/* Owner and target generation are captured by S_SpellIdentityThinker. */
static bool devour_link(edict_t const *thinker) {
    edict_t *caster = S_SpellChannelOwner(thinker);
    edict_t *victim = S_SpellChannelTarget(thinker);
    return thinker && thinker->think == devour_think && caster && victim &&
        (caster->aiflags & AI_DEVOURING) && (victim->aiflags & AI_DEVOURED);
}

static edict_t *devour_find(edict_t const *unit, bool by_owner) {
    if (!unit) return NULL;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *thinker = g_edicts + i;
        if (!thinker->inuse || thinker->think != devour_think || !devour_link(thinker)) continue;
        if (by_owner ? S_SpellChannelOwner(thinker) == unit : S_SpellChannelTarget(thinker) == unit)
            return thinker;
    }
    return NULL;
}

bool S_DevourHasVictim(edict_t const *caster) {
    return caster && (caster->aiflags & AI_DEVOURING) && devour_find(caster, true);
}

/* The target is still a real edict and keeps owner, health and identity. Free
 * the thinker first so removing or killing either unit cannot release twice. */
static void devour_stop(edict_t *thinker, bool release) {
    edict_t *caster = S_SpellChannelOwner(thinker);
    edict_t *victim = S_SpellChannelTarget(thinker);
    if (caster) caster->aiflags &= ~AI_DEVOURING;
    if (victim) {
        victim->aiflags &= ~AI_DEVOURED;
        G_SetUnitFlag(victim, UNIT_FLAG_PAUSED, false);
        if (release && !M_IsDead(victim)) {
            vec2_t point, position;
            point = caster ? caster->s.origin2 : victim->s.origin2;
            if (!G_FindUnitUnstuckPosition(victim, &point, &position)) position = point;
            victim->s.origin2 = position;
            /* The swallowing caster may fly (dragons); the victim is ground-only. */
            victim->s.origin.z = CM_GetHeightAtPoint(position.x, position.y);
            victim->s.renderfx &= ~RF_HIDDEN;
            victim->svflags &= ~SVF_NOCLIENT;
            victim->s.flags &= ~EF_NOT_SELECTABLE;
            gi.LinkEntity(victim);
            if (victim->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        } else {
            /* A fully digested victim must not leave an invisible, raisable
             * corpse behind after its normal death/event lifecycle. */
            victim->aiflags |= AI_CORPSE_NO_DECAY | AI_CORPSE_UNRAISABLE;
        }
        G_InvalidateUnitShortcutsForUnit(victim);
    }
    if (thinker->inuse) G_FreeEdict(thinker);
    if (caster) G_InvalidateCommands(G_GetPlayerClientByNumber(caster->s.player));
}

/* Called synchronously BEFORE the unit's death/removal mutates its identity.
 * The caster ejects a living victim; an explicitly killed/removed victim just
 * retires its digest thinker (it must never reappear on later caster death). */
void S_DevourUnitExit(edict_t *unit) {
    edict_t *thinker;
    if (!unit || !(unit->aiflags & (AI_DEVOURING | AI_DEVOURED))) return;
    if ((unit->aiflags & AI_DEVOURING) && (thinker = devour_find(unit, true)))
        devour_stop(thinker, true);
    if ((unit->aiflags & AI_DEVOURED) && (thinker = devour_find(unit, false)))
        devour_stop(thinker, false);
}

/* Damage is a universal digestion drain, not an attack or another cast. The
 * ordinary attack/ability damage path intentionally cannot reach off-world
 * victims; apply Dev2 directly and issue normal unit death attribution when
 * their life reaches zero. */
void devour_think(edict_t *thinker) {
    edict_t *caster, *victim;
    float damage;
    uint32_t const now = G_Time();

    if (!thinker || !thinker->inuse) return;
    caster = S_SpellChannelOwner(thinker);
    victim = S_SpellChannelTarget(thinker);
    if (!devour_link(thinker) || M_IsDead(caster)) {
        devour_stop(thinker, victim && !M_IsDead(victim));
        return;
    }
    if ((int32_t)(now - thinker->freetime) < 0) return;
    /* Preserve the interval under late scheduling without catching up with
     * thousands of ticks in one frame after a pause or time jump. */
    thinker->freetime = now + DEVOUR_TICK_MS;
    damage = MAX(0.0f, thinker->wait); /* Dev2, stored on thinker for save/load */
    if (damage <= 0.0f) return;
    if (victim->health.value > damage) {
        G_AddHealth(victim, -damage);
        return;
    }
    G_SetHealth(victim, 0.0f);
    /* The death hook retires this thinker and keeps the victim concealed. */
    unit_die(victim, caster);
}

static bool devour_valid(edict_t *caster, edict_t *victim, uint32_t code) {
    float max_creep;
    uint32_t level;
    if (!caster || !victim || caster == victim || !S_SpellIsAliveTarget(caster) ||
        !S_SpellIsAliveTarget(victim) || G_UnitIsHero(victim) ||
        G_UnitIsStructure(victim) || G_UnitTargetType(victim) != TARG_GROUND ||
        victim->targtype == TARG_MECHANICAL || G_UnitHasFlag(victim, UNIT_FLAG_INVULNERABLE) ||
        unit_hasstatusstate(victim, WC3_STATUS_STATE_INVULNERABLE) ||
        (victim->s.renderfx & RF_HIDDEN) ||
        (victim->aiflags & (AI_DEVOURED | AI_SOUL_TRAPPED)) ||
        G_UnitHasFlag(victim, UNIT_FLAG_PAUSED) || S_CargoTransportForUnit(victim) ||
        S_DevourHasVictim(caster) || (caster->aiflags & AI_DEVOURING) ||
        !S_SpellIsEnemy(caster, victim) ||
        G_UnitAbilityLevel(victim, ID_DEVOUR) || G_UnitAbilityLevel(victim, ID_CREEP_DEVOUR) ||
        !S_SpellAllowsTarget(code, caster, victim)) return false;
    level = S_SpellLevel(caster, code);
    max_creep = S_SpellData(code, level, 1); /* Dev1: maximum neutral creep level */
    if (victim->s.player == PLAYER_NEUTRAL_AGGRESSIVE && max_creep > 0.0f &&
        (!victim->data.UnitBalance || victim->data.UnitBalance->level > max_creep)) return false;
    return true;
}

static bool devour_start(edict_t *caster, edict_t *victim, uint32_t code) {
    edict_t *thinker;
    if (!devour_valid(caster, victim, code) ||
        !S_SpellTargetInRange(caster, victim, S_SpellRange(code, S_SpellLevel(caster, code)))) return false;
    thinker = S_SpellIdentityThinker(caster, code, victim);
    if (!thinker) return false;
    thinker->think = devour_think;
    thinker->freetime = G_Time() + DEVOUR_TICK_MS;
    thinker->wait = MAX(0.0f, S_SpellData(code, S_SpellLevel(caster, code), 2)); /* Dev2: HP/s */
    S_SpellCancelChannel(victim);
    G_ClearActiveUnitOrder(victim);
    G_ClearUnitOrderQueue(victim);
    victim->goalentity = victim->combatentity = NULL;
    if (victim->orders) victim->orders->resource_goal = NULL;
    unit_stand(victim);
    caster->aiflags |= AI_DEVOURING;
    victim->aiflags |= AI_DEVOURED;
    G_SetUnitFlag(victim, UNIT_FLAG_PAUSED, true);
    victim->s.renderfx |= RF_HIDDEN;
    victim->svflags |= SVF_NOCLIENT;
    victim->s.flags |= EF_NOT_SELECTABLE;
    if (victim->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.UnlinkEntity(victim);
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (!client->connected || !(victim->selected & (1u << client->ps.number))) continue;
        G_DeselectEntity(client, victim);
        G_SyncClientSelection(client);
    }
    victim->selected = 0;
    G_InvalidateUnitShortcutsForUnit(victim);
    G_InvalidateCommands(G_GetPlayerClientByNumber(caster->s.player));
    return true;
}

BZ_ABILITY_PROC(CAbilityDevour) {
    if (!call || !call->item) return false;
    switch (msg) {
    case A_VALIDATE:
        return call->target && call->target->type == SPELL_TARGET_UNIT &&
            devour_valid(ent, call->target->entity, call->item->code);
    case A_EXECUTE:
        return call->target && call->target->type == SPELL_TARGET_UNIT &&
            devour_start(ent, call->target->entity, call->item->code);
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
