#include "s_skills.h"

#define ID_UNSUMMON_BUFF MAKEFOURCC('B','u','n','s')

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

static void unsummon_end_effect(LPEDICT thinker) {
    if (!thinker) return;
    FILTER_EDICTS(effect, effect->inuse && effect->owner == thinker &&
                  effect->goalentity == thinker->unsummon.target &&
                  (effect->s.flags & EF_NOT_SELECTABLE)) {
        effect->owner = NULL;
        G_DestroyEffect(effect);
    }
}

static bool unsummon_in_range(LPEDICT worker, LPEDICT building) {
    float footprint;

    if (!worker || !building) return false;
    footprint = CM_DistanceToPathingFootprint(building, &worker->s.origin2);
    if (footprint < FLT_MAX) return footprint <= worker->collision;
    return Vector2_distance(&worker->s.origin2, &building->s.origin2) <=
        worker->collision + building->collision;
}

static bool unsummon_prepare_approach(LPEDICT worker, LPEDICT building) {
    VECTOR2 approach;
    float footprint;
    float const route_band = worker ?
        worker->collision + CM_PathCellWorldSize() * 1.41421356237f : 0.0f;

    if (!worker || !building) return false;
    if (CM_FindApproachPointToFootprintForRadius(
            building, &worker->s.origin2, route_band, worker->collision, &approach) &&
        CM_DistanceToPathingFootprint(building, &approach) <= worker->collision) {
        worker->goalentity = Waypoint_add(&approach);
        move_reset_progress(worker);
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
        return worker->goalentity != NULL;
    }
    if (!building->pathtex) {
        worker->goalentity = building;
        move_reset_progress(worker);
        return true;
    }
    return false;
}

static bool unsummon_target_valid(LPEDICT worker, LPEDICT building) {
    return worker && building && building->inuse &&
        building->spawn_time == worker->unsummon.target_spawn_time &&
        S_SpellIsAliveTarget(building) && building->s.player == worker->s.player &&
        G_UnitIsBuilding(building->class_id);
}

static bool unsummon_thinker_target_valid(LPEDICT thinker, LPEDICT building) {
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
static bool unsummon_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT building = st.entity;
    (void)spell;
    if (!caster || !building || !S_SpellIsAliveTarget(building) ||
        building->s.player != caster->s.player || !G_UnitIsBuilding(building->class_id) ||
        G_UnitStatusLevel(building, ID_UNSUMMON_BUFF)) return false;
    /* Retail rejects incomplete structures before mana spend; the old path
     * treated every allied building as a valid Unsummon target. */
    if (building->construction.active) {
        G_ShowCommandErrorKey(G_GetPlayerEntityByNumber(caster->s.player),
                              "UnderConstruction", "That building is currently under construction.");
        return false;
    }
    return true;
}

static void unsummon_credit(LPEDICT thinker, LPEDICT building, float removed_health) {
    UnitBalance_t const *bal;
    LPGAMECLIENT client;
    float rate, fraction;
    int32_t gold_total, lumber_total, gold, lumber;

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
    gold_total = (int32_t)floorf(MAX(0, bal->goldCost) * rate * fraction + 0.0001f);
    lumber_total = (int32_t)floorf(MAX(0, bal->lumberCost) * rate * fraction + 0.0001f);
    gold = MAX(0, gold_total - thinker->unsummon.gold_paid);
    lumber = MAX(0, lumber_total - thinker->unsummon.lumber_paid);
    thinker->unsummon.gold_paid = gold_total;
    thinker->unsummon.lumber_paid = lumber_total;
    if (gold <= 0 && lumber <= 0) return;

    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number == building->s.player) {
        int32_t value;
        value = (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] + gold;
        client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (uint16_t)MIN(value, USHRT_MAX);
        value = (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] + lumber;
        client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (uint16_t)MIN(value, USHRT_MAX);
        G_RefreshResourceBar(G_GetPlayerEntityByNumber(building->s.player));
    }
}

void unsummon_think(LPEDICT thinker) {
    LPEDICT caster = thinker ? thinker->owner : NULL;
    LPEDICT building = thinker ? thinker->unsummon.target : NULL;
    float damage, removed;

    if (!thinker) return;
    if (thinker->unsummon.approaching) return;
    if (!unsummon_thinker_target_valid(thinker, building) || M_IsDead(building)) {
        if (building && building->inuse && building->spawn_time == thinker->channel.target_spawn_time)
            unsummon_remove_status(building);
        unsummon_end_effect(thinker);
        S_SpellEndChannel(thinker);
        return;
    }

    damage = MAX(0.0f, S_SpellData(thinker->class_id, thinker->resources, 2)) * ((float)FRAMETIME / 1000.0f);
    if (damage <= 0.0f) {
        unsummon_remove_status(building);
        unsummon_end_effect(thinker);
        S_SpellEndChannel(thinker);
        return;
    }
    removed = MIN(building->health.value, damage);
    unsummon_credit(thinker, building, removed);
    G_AddHealth(building, -removed);
    if (building->health.value <= 0.0f) {
        unsummon_remove_status(building);
        unsummon_end_effect(thinker);
        unit_die(building, caster && caster->inuse ? caster : NULL);
        S_SpellEndChannel(thinker);
    }
}

static void unsummon_start(LPEDICT worker, LPEDICT thinker) {
    LPEDICT building = worker ? worker->unsummon.target : NULL;

    if (!worker || !thinker || !unsummon_target_valid(worker, building)) {
        if (worker) S_SpellCancelChannel(worker);
        return;
    }
    worker->unsummon.starting = true;
    worker->unsummon.approaching = false;
    thinker->unsummon.approaching = false;
    worker->channel.origin = worker->s.origin2;
    worker->goalentity = NULL;
    unit_setmove(worker, &unsummon_move_channel);
    worker->unsummon.starting = false;
    unsummon_add_status(building);
    {
        LPEDICT effect = G_SpawnAbilityEffectTarget(ID_UNSUMMON_BUFF, WC3_EFFECT_TARGET, 0, building, NULL, false);
        if (effect) effect->owner = thinker;
    }
}

static void ai_unsummon_walk(LPEDICT worker) {
    LPEDICT building = worker ? worker->unsummon.target : NULL;
    float distance, footprint, step;
    bool in_range, ready, blocked;
    LPEDICT thinker = NULL;

    if (!worker || !worker->unsummon.approaching || !unsummon_target_valid(worker, building)) {
        if (worker && worker->channel.code) S_SpellCancelChannel(worker);
        return;
    }
    in_range = unsummon_in_range(worker, building);
    distance = M_DistanceToGoal(worker);
    step = unit_movedistance(worker);
    footprint = CM_DistanceToPathingFootprint(building, &worker->s.origin2);
    ready = in_range || (footprint < FLT_MAX &&
        footprint <= worker->collision + CM_PathCellWorldSize() * 1.41421356237f);
    if (ready) {
        /* An older persistent demolition may still belong to this worker; only
         * the thinker created by this approach order may start here. */
        FILTER_EDICTS(ent, ent->inuse && ent->think == unsummon_think &&
            ent->owner == worker && ent->class_id == worker->unsummon.ability &&
            ent->channel.serial == worker->channel.serial &&
            ent->unsummon.target == building &&
            ent->channel.target_spawn_time == building->spawn_time) {
            thinker = ent;
            break;
        }
        if (thinker) unsummon_start(worker, thinker);
        return;
    }
    if (!worker->goalentity && !unsummon_prepare_approach(worker, building)) {
        S_SpellCancelChannel(worker);
        return;
    }
    blocked = move_is_blocked(worker, distance, step);
    if (blocked || worker->movement.flow_unreachable ||
        (worker->movement.flow_goal_reached && !unsummon_in_range(worker, building))) {
        S_SpellCancelChannel(worker);
        return;
    }
    unit_changeangle_for_radius_worker(worker, worker->collision);
    unit_moveindirection(worker);
}

static void unsummon_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT thinker;
    uint32_t level;

    if (!unsummon_validate(caster, st, spell)) return;
    level = S_SpellLevel(caster, spell->code);
    if (S_SpellData(spell->code, level, 2) <= 0.0f) {
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
    if (unsummon_in_range(caster, st.entity)) {
        unsummon_start(caster, thinker);
    } else if (unsummon_prepare_approach(caster, st.entity)) {
        caster->unsummon.starting = true;
        unit_setmove(caster, &unsummon_move_walk);
        caster->unsummon.starting = false;
    } else {
        S_SpellCancelChannel(caster);
    }
}

static void unsummon_cancel_owned(LPEDICT caster, uint32_t code) {
    if (!caster || !code) return;
    for (uint32_t i = 1; i < globals.num_edicts; i++) {
        LPEDICT thinker = g_edicts + i;
        if (!thinker->inuse || thinker->think != unsummon_think || thinker->owner != caster ||
            thinker->class_id != code) continue;
        if (!thinker->unsummon.approaching) {
            continue;
        }
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
