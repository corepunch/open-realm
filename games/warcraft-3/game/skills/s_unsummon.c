#include "s_skills.h"

#define ID_UNSUMMON_BUFF MAKEFOURCC('B','u','n','s')

#ifdef WC3_DEBUG_UNSUMMON
static int unsummon_debug_level(void) {
    LPCSTR value = gi.CvarString("wc3_unsummon_debug", "1");
    return value ? atoi(value) : 0;
}
#define UNSUMMON_LOG(LEVEL, ...) do { \
    if (unsummon_debug_level() >= (LEVEL)) \
        fprintf(stderr, "WC3_UNSUMMON " __VA_ARGS__); \
} while (0)
#else
#define UNSUMMON_LOG(...) ((void)0)
#endif

void S_UnsummonDebugCast(LPEDICT caster, DWORD code, LPEDICT target, LPCSTR stage, LPCSTR reason) {
    if (code != MAKEFOURCC('A','u','n','s')) return;
    UNSUMMON_LOG(1, "cast stage=%s reason=%s caster=%ld caster-id=%.4s target=%ld target-id=%.4s "
        "caster-state=(inuse=%d dead=%d player=%u mana=%.1f build=%ld goal=%ld channel=%.4s) "
        "target-state=(inuse=%d dead=%d svflags=0x%x player=%u health=%.1f/%.1f targtype=%d buns=%d) "
        "ability-level=%u range=%.1f distance=%.1f canpay=%d cooldown=%d\n",
        stage ? stage : "?", reason ? reason : "?", caster ? (long)(caster - g_edicts) : -1L,
        caster ? (LPCSTR)&caster->class_id : "----", target ? (long)(target - g_edicts) : -1L,
        target ? (LPCSTR)&target->class_id : "----", caster ? caster->inuse : 0,
        caster ? M_IsDead(caster) : 0, caster ? caster->s.player : 0, caster ? caster->mana.value : 0.0f,
        caster && caster->build ? (long)(caster->build - g_edicts) : -1L,
        caster && caster->goalentity ? (long)(caster->goalentity - g_edicts) : -1L,
        caster ? (LPCSTR)&caster->channel.code : "----", target ? target->inuse : 0,
        target ? M_IsDead(target) : 0, target ? target->svflags : 0, target ? target->s.player : 0,
        target ? target->health.value : 0.0f, target ? target->health.max_value : 0.0f,
        target ? target->targtype : 0, target ? G_UnitStatusLevel(target, ID_UNSUMMON_BUFF) : 0,
        caster ? G_UnitAbilityLevel(caster, code) : 0, caster ? S_SpellRange(code, S_SpellLevel(caster, code)) : 0.0f,
        caster && target ? Vector2_distance(&caster->s.origin2, &target->s.origin2) : 0.0f,
        caster ? S_SpellCanPay(caster, code, S_SpellLevel(caster, code)) : 0,
        caster ? S_SpellCooldownReady(caster, code) : 0);
}

static void unsummon_remove_status(LPEDICT building) {
    if (!building) return;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (building->abilstatus[i].level && building->abilstatus[i].code == ID_UNSUMMON_BUFF)
            memset(building->abilstatus + i, 0, sizeof(building->abilstatus[i]));
    }
    G_InvalidateUnitInfoPanel(building);
}

static void unsummon_add_status(LPEDICT building) {
    if (!building || G_UnitStatusLevel(building, ID_UNSUMMON_BUFF)) return;
    unit_addstatus(building, "Buns", 1);
}

static BOOL unsummon_in_range(LPEDICT worker, LPEDICT building) {
    FLOAT footprint;

    if (!worker || !building) return false;
    footprint = CM_DistanceToPathingFootprint(building, &worker->s.origin2);
    if (footprint < FLT_MAX) return footprint <= worker->collision;
    return Vector2_distance(&worker->s.origin2, &building->s.origin2) <=
        worker->collision + building->collision;
}

static BOOL unsummon_prepare_approach(LPEDICT worker, LPEDICT building) {
    VECTOR2 approach;
    FLOAT footprint;
    FLOAT const route_band = worker ?
        worker->collision + CM_PathCellWorldSize() * 1.41421356237f : 0.0f;

    if (!worker || !building) return false;
    if (CM_FindApproachPointToFootprintForRadius(
            building, &worker->s.origin2, route_band, worker->collision, &approach) &&
        CM_DistanceToPathingFootprint(building, &approach) <= worker->collision) {
        worker->goalentity = Waypoint_add(&approach);
        move_reset_progress(worker);
        UNSUMMON_LOG(1, "approach goal worker=%ld building=%ld goal=%ld point=(%.1f,%.1f) created=%d\n",
            (long)(worker - g_edicts), (long)(building - g_edicts),
            worker->goalentity ? (long)(worker->goalentity - g_edicts) : -1L,
        approach.x, approach.y, worker->goalentity != NULL);
        return worker->goalentity != NULL;
    }
    /* Static path cells are 32 units wide on retail maps.  A 16-unit Acolyte
     * radius rounds to one whole cell in the generic query, rejecting the
     * legal edge cell beside a completed footprint.  The inner query selects
     * the nearest legal ring; the exact footprint check keeps the route from
     * stopping at a merely nearby grid cell. */
    if (CM_FindInnerApproachPointToFootprintForRadius(
            building, &worker->s.origin2, worker->collision, 0.0f, &approach) &&
        (footprint = CM_DistanceToPathingFootprint(building, &approach)) <= worker->collision) {
        worker->goalentity = Waypoint_add(&approach);
        move_reset_progress(worker);
        UNSUMMON_LOG(1, "approach goal-inner worker=%ld building=%ld goal=%ld point=(%.1f,%.1f) footprint=%.1f created=%d\n",
            (long)(worker - g_edicts), (long)(building - g_edicts),
            worker->goalentity ? (long)(worker->goalentity - g_edicts) : -1L,
            approach.x, approach.y, footprint, worker->goalentity != NULL);
        return worker->goalentity != NULL;
    }
    if (!building->pathtex) {
        worker->goalentity = building;
        move_reset_progress(worker);
        UNSUMMON_LOG(1, "approach goal-fallback worker=%ld building=%ld reason=no-pathtex\n",
            (long)(worker - g_edicts), (long)(building - g_edicts));
        return true;
    }
    UNSUMMON_LOG(1, "approach-fail worker=%ld building=%ld reason=no-footprint-point\n",
        (long)(worker - g_edicts), (long)(building - g_edicts));
    return false;
}

static BOOL unsummon_target_valid(LPEDICT worker, LPEDICT building) {
    return worker && building && building->inuse &&
        building->spawn_time == worker->unsummon.target_spawn_time &&
        S_SpellIsAliveTarget(building) && building->s.player == worker->s.player &&
        G_UnitIsBuilding(building->class_id);
}

static BOOL unsummon_thinker_target_valid(LPEDICT thinker, LPEDICT building) {
    return thinker && building && building->inuse &&
        building->spawn_time == thinker->channel.target_spawn_time &&
        S_SpellIsAliveTarget(building) && building->s.player == thinker->s.player &&
        G_UnitIsBuilding(building->class_id);
}

static void unsummon_cancel_approach(LPEDICT worker) {
    if (!worker) return;
    worker->unsummon.target = NULL;
    worker->unsummon.target_spawn_time = 0;
    worker->unsummon.ability = worker->unsummon.level = 0;
    worker->unsummon.approaching = worker->unsummon.starting = false;
    if (worker->goalentity) worker->goalentity = NULL;
    move_reset_progress(worker);
}

static void ai_unsummon_walk(LPEDICT worker);
static umove_t unsummon_move_walk = { "walk", ai_unsummon_walk, NULL, CAbilityUnsummon };
static umove_t unsummon_move_channel = { "stand channel", ai_idle, NULL, CAbilityUnsummon };

/* Owned living structure only; S_SpellAllowsTarget ignores structure/player tokens. */
static BOOL unsummon_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT building = st.entity;
    (void)spell;
    if (!caster) { UNSUMMON_LOG(1, "validate-fail reason=no-caster\n"); return false; }
    if (!building) { UNSUMMON_LOG(1, "validate-fail reason=no-target caster=%ld\n", (long)(caster - g_edicts)); return false; }
    if (!S_SpellIsAliveTarget(building)) {
        UNSUMMON_LOG(1, "validate-fail reason=target-not-alive target=%ld\n", (long)(building - g_edicts));
        return false;
    }
    if (building->s.player != caster->s.player) {
        UNSUMMON_LOG(1, "validate-fail reason=target-owner caster=%u target=%u\n", caster->s.player, building->s.player);
        return false;
    }
    if (!G_UnitIsBuilding(building->class_id)) {
        UNSUMMON_LOG(1, "validate-fail reason=target-not-building target=%ld id=%.4s\n",
            (long)(building - g_edicts), (LPCSTR)&building->class_id);
        return false;
    }
    if (G_UnitStatusLevel(building, ID_UNSUMMON_BUFF)) {
        UNSUMMON_LOG(1, "validate-fail reason=already-unsummoning target=%ld\n", (long)(building - g_edicts));
        return false;
    }
    UNSUMMON_LOG(2, "validate-ok caster=%ld target=%ld\n", (long)(caster - g_edicts), (long)(building - g_edicts));
    return true;
}

static void unsummon_credit(LPEDICT thinker, LPEDICT building, FLOAT removed_health) {
    UnitBalance_t const *bal;
    LPGAMECLIENT client;
    FLOAT rate, fraction;
    LONG gold_total, lumber_total, gold, lumber;

    if (!thinker || !building || removed_health <= 0.0f || building->health.max_value <= 0.0f) return;
    bal = building->data.UnitBalance;
    if (!bal) bal = G_UnitBalance(building->class_id);
    if (!bal) return;

    /* Track demolition attributable to Unsummon rather than the building's
     * current HP.  Enemy damage therefore reduces the eventual refund, while
     * cumulative totals avoid losing the last resource to per-tick float
     * rounding. */
    thinker->unsummon.removed_health += removed_health;
    rate = MAX(0.0f, S_SpellData(thinker->class_id, thinker->resources, 1));
    fraction = MIN(1.0f, thinker->unsummon.removed_health / building->health.max_value);
    gold_total = (LONG)floorf(MAX(0, bal->goldCost) * rate * fraction + 0.0001f);
    lumber_total = (LONG)floorf(MAX(0, bal->lumberCost) * rate * fraction + 0.0001f);
    gold = MAX(0, gold_total - thinker->unsummon.gold_paid);
    lumber = MAX(0, lumber_total - thinker->unsummon.lumber_paid);
    thinker->unsummon.gold_paid = gold_total;
    thinker->unsummon.lumber_paid = lumber_total;
    if (gold <= 0 && lumber <= 0) return;

    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number == building->s.player) {
        LONG value;
        value = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] + gold;
        client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(value, USHRT_MAX);
        value = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] + lumber;
        client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(value, USHRT_MAX);
        G_RefreshResourceBar(G_GetPlayerEntityByNumber(building->s.player));
    }
}

void unsummon_think(LPEDICT thinker) {
    LPEDICT caster = thinker ? thinker->owner : NULL;
    LPEDICT building = thinker ? thinker->unsummon.target : NULL;
    FLOAT damage, removed;

    if (!thinker) return;
    if (thinker->unsummon.approaching) return;
    if (!unsummon_thinker_target_valid(thinker, building) || M_IsDead(building)) {
        if (building && building->inuse && building->spawn_time == thinker->channel.target_spawn_time)
            unsummon_remove_status(building);
        S_SpellEndChannel(thinker);
        return;
    }

    damage = MAX(0.0f, S_SpellData(thinker->class_id, thinker->resources, 2)) * ((FLOAT)FRAMETIME / 1000.0f);
    if (damage <= 0.0f) {
        unsummon_remove_status(building);
        S_SpellEndChannel(thinker);
        return;
    }
    removed = MIN(building->health.value, damage);
    unsummon_credit(thinker, building, removed);
    G_AddHealth(building, -removed);
    if (building->health.value <= 0.0f) {
        unsummon_remove_status(building);
        unit_die(building, caster && caster->inuse ? caster : NULL);
        S_SpellEndChannel(thinker);
    }
}

static void unsummon_start(LPEDICT worker, LPEDICT thinker) {
    LPEDICT building = worker ? worker->unsummon.target : NULL;

    if (!worker || !thinker || !unsummon_target_valid(worker, building)) {
        UNSUMMON_LOG(1, "start-fail worker=%ld thinker=%ld building=%ld reason=target-invalid\n",
            worker ? (long)(worker - g_edicts) : -1L,
            thinker ? (long)(thinker - g_edicts) : -1L,
            building ? (long)(building - g_edicts) : -1L);
        if (worker) S_SpellCancelChannel(worker);
        return;
    }
    UNSUMMON_LOG(1, "start worker=%ld thinker=%ld building=%ld origin=(%.1f,%.1f)\n",
        (long)(worker - g_edicts), (long)(thinker - g_edicts), (long)(building - g_edicts),
        worker->s.origin2.x, worker->s.origin2.y);
    worker->unsummon.starting = true;
    worker->unsummon.approaching = false;
    thinker->unsummon.approaching = false;
    worker->channel.origin = worker->s.origin2;
    worker->goalentity = NULL;
    unit_setmove(worker, &unsummon_move_channel);
    worker->unsummon.starting = false;
    unsummon_add_status(building);
    G_SpawnAbilityEffectTarget(thinker->class_id, WC3_EFFECT_TARGET, 0, building, NULL, true);
}

static void ai_unsummon_walk(LPEDICT worker) {
    LPEDICT building = worker ? worker->unsummon.target : NULL;
    FLOAT distance, footprint, step;
    BOOL in_range, ready, blocked;
    LPEDICT thinker = NULL;

    if (!worker || !worker->unsummon.approaching || !unsummon_target_valid(worker, building)) {
        UNSUMMON_LOG(1, "walk-cancel worker=%ld building=%ld reason=state-invalid\n",
            worker ? (long)(worker - g_edicts) : -1L, building ? (long)(building - g_edicts) : -1L);
        if (worker && worker->channel.code) S_SpellCancelChannel(worker);
        return;
    }
    in_range = unsummon_in_range(worker, building);
    distance = M_DistanceToGoal(worker);
    step = unit_movedistance(worker);
    footprint = CM_DistanceToPathingFootprint(building, &worker->s.origin2);
    ready = in_range || (footprint < FLT_MAX &&
        footprint <= worker->collision + CM_PathCellWorldSize() * 1.41421356237f);
    UNSUMMON_LOG(2, "walk worker=%ld building=%ld origin=(%.1f,%.1f) goal=%ld distance=%.1f step=%.1f footprint=%.1f inrange=%d ready=%d flow=(reached=%d unreachable=%d)\n",
        (long)(worker - g_edicts), (long)(building - g_edicts), worker->s.origin2.x, worker->s.origin2.y,
        worker->goalentity ? (long)(worker->goalentity - g_edicts) : -1L, distance, step, footprint, in_range, ready,
        worker->movement.flow_goal_reached, worker->movement.flow_unreachable);
    if (ready) {
        if (!in_range) UNSUMMON_LOG(1, "walk-arrive worker=%ld building=%ld reason=pathing-cell-clearance footprint=%.1f step=%.1f\n",
            (long)(worker - g_edicts), (long)(building - g_edicts), footprint, step);
        FILTER_EDICTS(ent, ent->inuse && ent->think == unsummon_think &&
            ent->owner == worker && ent->class_id == worker->unsummon.ability) {
            thinker = ent;
            break;
        }
        if (thinker) unsummon_start(worker, thinker);
        else UNSUMMON_LOG(1, "walk-wait worker=%ld building=%ld reason=thinker-not-found ability=%.4s\n",
            (long)(worker - g_edicts), (long)(building - g_edicts), (LPCSTR)&worker->unsummon.ability);
        return;
    }
    if (!worker->goalentity && !unsummon_prepare_approach(worker, building)) {
        UNSUMMON_LOG(1, "walk-cancel worker=%ld building=%ld reason=approach-unavailable\n",
            (long)(worker - g_edicts), (long)(building - g_edicts));
        S_SpellCancelChannel(worker);
        return;
    }
    blocked = move_is_blocked(worker, distance, step);
    if (blocked || worker->movement.flow_unreachable ||
        (worker->movement.flow_goal_reached && !unsummon_in_range(worker, building))) {
        UNSUMMON_LOG(1, "walk-cancel worker=%ld building=%ld reason=route-failure blocked=%d unreachable=%d reached=%d\n",
            (long)(worker - g_edicts), (long)(building - g_edicts), blocked,
            worker->movement.flow_unreachable, worker->movement.flow_goal_reached);
        S_SpellCancelChannel(worker);
        return;
    }
    unit_changeangle_for_radius_worker(worker, worker->collision);
    unit_moveindirection(worker);
    UNSUMMON_LOG(2, "walk-moved worker=%ld building=%ld origin=(%.1f,%.1f)\n",
        (long)(worker - g_edicts), (long)(building - g_edicts), worker->s.origin2.x, worker->s.origin2.y);
}

static void unsummon_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT thinker;
    DWORD level;

    if (!unsummon_validate(caster, st, spell)) return;
    level = S_SpellLevel(caster, spell->code);
    if (S_SpellData(spell->code, level, 2) <= 0.0f) {
        UNSUMMON_LOG(1, "execute-fail caster=%ld target=%ld reason=no-damage-data level=%u\n",
            (long)(caster - g_edicts), (long)(st.entity - g_edicts), level);
        S_SpellCancelChannel(caster);
        return;
    }
    thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->goalentity = st.entity;
    thinker->channel.target_spawn_time = st.entity->spawn_time;
    thinker->s.player = st.entity->s.player;
    thinker->resources = level;
    thinker->think = unsummon_think;
    thinker->unsummon.target = st.entity;
    thinker->unsummon.target_spawn_time = st.entity->spawn_time;
    thinker->unsummon.ability = spell->code;
    thinker->unsummon.level = level;
    thinker->unsummon.approaching = true;
    caster->unsummon.target = st.entity;
    caster->unsummon.target_spawn_time = st.entity->spawn_time;
    caster->unsummon.ability = spell->code;
    caster->unsummon.level = level;
    caster->unsummon.approaching = true;
    UNSUMMON_LOG(1, "execute caster=%ld target=%ld origin=(%.1f,%.1f) target-origin=(%.1f,%.1f) inrange=%d collision=%.1f\n",
        (long)(caster - g_edicts), (long)(st.entity - g_edicts), caster->s.origin2.x, caster->s.origin2.y,
        st.entity->s.origin2.x, st.entity->s.origin2.y, unsummon_in_range(caster, st.entity), caster->collision);
    if (unsummon_in_range(caster, st.entity)) {
        UNSUMMON_LOG(1, "execute-branch caster=%ld target=%ld branch=immediate-start\n",
            (long)(caster - g_edicts), (long)(st.entity - g_edicts));
        unsummon_start(caster, thinker);
    } else if (unsummon_prepare_approach(caster, st.entity)) {
        UNSUMMON_LOG(1, "execute-branch caster=%ld target=%ld branch=approach goal=%ld\n",
            (long)(caster - g_edicts), (long)(st.entity - g_edicts),
            caster->goalentity ? (long)(caster->goalentity - g_edicts) : -1L);
        caster->unsummon.starting = true;
        unit_setmove(caster, &unsummon_move_walk);
        caster->unsummon.starting = false;
    } else {
        UNSUMMON_LOG(1, "execute-fail caster=%ld target=%ld reason=approach-unavailable\n",
            (long)(caster - g_edicts), (long)(st.entity - g_edicts));
        S_SpellCancelChannel(caster);
    }
}

static void unsummon_cancel_owned(LPEDICT caster, DWORD code) {
    if (!caster || !code) return;
    UNSUMMON_LOG(1, "cancel caster=%ld ability=%.4s state=(approaching=%d starting=%d channel=%.4s target=%ld)\n",
        (long)(caster - g_edicts), (LPCSTR)&code, caster->unsummon.approaching, caster->unsummon.starting,
        (LPCSTR)&caster->channel.code, caster->unsummon.target ? (long)(caster->unsummon.target - g_edicts) : -1L);
    for (DWORD i = 1; i < globals.num_edicts; i++) {
        LPEDICT thinker = g_edicts + i;
        if (!thinker->inuse || thinker->think != unsummon_think || thinker->owner != caster ||
            thinker->class_id != code) continue;
        if (!thinker->unsummon.approaching) {
            UNSUMMON_LOG(1, "cancel-ignore caster=%ld thinker=%ld reason=demolition-owned-by-thinker\n",
                (long)(caster - g_edicts), (long)(thinker - g_edicts));
            continue;
        }
        UNSUMMON_LOG(1, "cancel-approach caster=%ld thinker=%ld target=%ld\n",
            (long)(caster - g_edicts), (long)(thinker - g_edicts),
            thinker->unsummon.target ? (long)(thinker->unsummon.target - g_edicts) : -1L);
        if (thinker->goalentity && thinker->goalentity->inuse &&
            thinker->goalentity->spawn_time == thinker->channel.target_spawn_time)
            unsummon_remove_status(thinker->goalentity);
        thinker->unsummon.target = NULL;
        thinker->unsummon.approaching = false;
        thinker->goalentity = NULL;
        if (thinker->owner == caster) unsummon_cancel_approach(caster);
    }
}

BZ_ABILITY_PROC(CAbilityUnsummon) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    switch (msg) {
    case A_VALIDATE:
        return unsummon_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE:
        unsummon_execute(ent, target, call ? call->item : NULL);
        return true;
    case A_CANCEL:
        unsummon_cancel_owned(ent, call && call->item ? call->item->code : MAKEFOURCC('A','u','n','s'));
        return true;
    case A_MOVE_LEAVE:
        if (ent && ent->unsummon.starting) return true;
        if (ent && ent->channel.code) S_SpellCancelChannel(ent);
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
