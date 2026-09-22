#include "s_skills.h"

#define ID_SACRIFICE_PIT MAKEFOURCC('A','s','a','c')
#define ID_SACRIFICE_ACOLYTE MAKEFOURCC('A','l','a','m')
#define ID_SHADE MAKEFOURCC('u','s','h','d')

/* Retail exposes the command from both ends: Asac on the Sacrificial Pit
 * targets an Acolyte, while Alam on an Acolyte targets a Sacrificial Pit.
 * Neither ability has object-data fields describing the result type; the
 * stock mechanic always creates a Shade. */
static BOOL sacrifice_pair(LPEDICT caster, LPEDICT target, DWORD ability,
                           LPEDICT *pit, LPEDICT *worker) {
    DWORD const code = G_AbilityCode(ability);

    if (!caster || !target || !S_SpellIsAliveTarget(caster) || !S_SpellIsAliveTarget(target) ||
        caster->s.player != target->s.player) return false;
    if (code == ID_SACRIFICE_PIT) {
        *pit = caster;
        *worker = target;
    } else if (code == ID_SACRIFICE_ACOLYTE) {
        *pit = target;
        *worker = caster;
    } else {
        return false;
    }
    if (!G_UnitIsBuilding((*pit)->class_id) || G_UnitIsBuilding((*worker)->class_id) ||
        G_UnitIsHero(*worker) || ((*worker)->s.renderfx & RF_HIDDEN)) return false;
    /* Counterpart abilities identify the two stock endpoints without coupling
     * the behavior to uaco/usap unit rawcodes. */
    if (!G_UnitAbilityLevel(*pit, ID_SACRIFICE_PIT) ||
        !G_UnitAbilityLevel(*worker, ID_SACRIFICE_ACOLYTE)) return false;
    return true;
}

static BOOL sacrifice_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT pit = NULL, worker = NULL;
    if (!spell || st.type != SPELL_TARGET_UNIT || !st.entity ||
        !sacrifice_pair(caster, st.entity, spell->code, &pit, &worker)) return false;
    /* Warsmash's Sacrifice is a producer queue item and a pit can own only one
     * sacrifice worker.  Do not displace ordinary training/research state. */
    if (pit->build || pit->construction.active || G_BuildingUpgradeActive(pit)) return false;
    return G_UnitBalance(ID_SHADE) != NULL;
}

static void sacrifice_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT pit = NULL, worker = NULL;
    if (!sacrifice_validate(caster, st, spell) ||
        !sacrifice_pair(caster, st.entity, spell->code, &pit, &worker)) return;
    G_QueueSacrifice(pit, worker, ID_SHADE);
}

/* Identity for Train's queue-lifecycle dispatch; policy stays in this file. */
DWORD S_SacrificeAbilityCode(void) { return ID_SACRIFICE_PIT; }

/* The Shade result inherits the consumed worker's food slot, so Train must
 * not reserve additional food while the worker still exists. */
BOOL S_SacrificeSkipsFoodReservation(LPCEDICT item) {
    return item && item->sacrifice.active;
}

/* The queued worker must be the same live edict owned by the same player.
 * Spawn-time comparison rejects edict slots reused after the worker died. */
static BOOL sacrifice_worker_valid(LPCEDICT item) {
    LPCEDICT worker;
    if (!item || !item->sacrifice.active || !(worker = item->sacrifice.worker)) return false;
    return worker->inuse && worker->spawn_time == item->sacrifice.worker_spawn_time &&
        !M_IsDead(worker) && worker->s.player == item->s.player;
}

/* Inverse of the queue-time hide/pause: restore exactly the stashed state. */
static void sacrifice_release_worker(LPEDICT item) {
    LPEDICT worker;
    if (!item || !item->sacrifice.active || !(worker = item->sacrifice.worker)) return;
    if (worker->inuse && worker->spawn_time == item->sacrifice.worker_spawn_time) {
        if (!item->sacrifice.restore_hidden) worker->s.renderfx &= ~RF_HIDDEN;
        worker->paused = item->sacrifice.restore_paused;
        G_InvalidateUnitShortcutsForUnit(worker);
    }
}

/* A_QUEUE_VALIDATE: the queued result may progress only while its worker is intact. */
static BOOL sacrifice_queue_validate(LPEDICT producer, LPEDICT item) {
    (void)producer;
    return sacrifice_worker_valid(item);
}

/* A_QUEUE_COMPLETE: placement already succeeded. Remove the worker first so
 * its food releases, then activate the result without a transient +1. */
static void sacrifice_queue_complete(LPEDICT producer, LPEDICT item) {
    LPEDICT worker = item->sacrifice.worker;
    DWORD const worker_spawn_time = item->sacrifice.worker_spawn_time;
    (void)producer;
    memset(&item->sacrifice, 0, sizeof(item->sacrifice));
    if (worker && worker->inuse && worker->spawn_time == worker_spawn_time)
        G_FreeEdict(worker);
    G_SetUnitFoodUsed(item, item->data.UnitBalance ? item->data.UnitBalance->foodUsed : 0);
}

/* A_QUEUE_CANCEL: restore the worker; Train refunds the result cost itself. */
static void sacrifice_queue_cancel(LPEDICT producer, LPEDICT item) {
    (void)producer;
    sacrifice_release_worker(item);
}

/* Sacrifice queue creation. Generic allocation/payment/queue-link/move/UI stay
 * Train-owned mechanisms; worker hiding and restore-state stashing are
 * Sacrifice policy owned here. */
BOOL G_QueueSacrifice(LPEDICT producer, LPEDICT worker, DWORD result_id) {
    LPPLAYER player;
    LPEDICT result;
    BOOL restore_hidden;

    if (!producer || !worker || !result_id || producer->build || !worker->inuse || M_IsDead(worker) ||
        producer->s.player != worker->s.player) return false;
    player = G_GetPlayerByNumber(producer->s.player);
    if (!player) return false;
    result = SP_SpawnAtLocation(result_id, producer->s.player, &producer->s.origin2);
    if (!result) return false;
    if (!player_pay(player, result_id)) {
        G_FreeEdict(result);
        return false;
    }

    result->training = true;
    result->training_food_wait_notified = false;
    G_SetHealth(result, 0);
    result->s.renderfx |= RF_HIDDEN;
    result->sacrifice.active = true;
    result->sacrifice.worker = worker;
    result->sacrifice.worker_spawn_time = worker->spawn_time;
    result->sacrifice.restore_paused = worker->paused;
    restore_hidden = (worker->s.renderfx & RF_HIDDEN) != 0;
    result->sacrifice.restore_hidden = restore_hidden;
    worker->s.renderfx |= RF_HIDDEN;
    worker->paused = true;
    G_InvalidateUnitShortcutsForUnit(worker);

    unit_add_build_queue(producer, result);
    TrainSetBuildMove(producer);
    G_RefreshTrainingQueue(producer);
    return true;
}

/* Queue-lifecycle ownership for Train's dispatch (A_QUEUE_* messages). The
 * validated spell path owns targeting/cast; this concrete procedure owns the
 * queued worker lifecycle and delegates the rest to the shared spell
 * default, like a TFT override delegates to its parent. */
BZ_ABILITY_PROC(CAbilitySacrifice) {
    LPEDICT producer, item;
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    if (msg == A_QUEUE_VALIDATE || msg == A_QUEUE_COMPLETE || msg == A_QUEUE_CANCEL) {
        if (!call) return false;
        producer = call->queue.producer; item = call->queue.item;
        if (!item || !item->sacrifice.active) return false;
        if (msg == A_QUEUE_VALIDATE) return sacrifice_queue_validate(producer, item);
        if (msg == A_QUEUE_COMPLETE) { sacrifice_queue_complete(producer, item); return true; }
        sacrifice_queue_cancel(producer, item); return true;
    }
    switch (msg) {
    case A_VALIDATE: return sacrifice_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: sacrifice_execute(ent, target, call ? call->item : NULL); return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}
