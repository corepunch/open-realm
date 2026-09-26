#include <float.h>

#include "s_skills.h"

#define BZ_AMEL MAKEFOURCC('A','m','e','l')
#define BZ_AMED MAKEFOURCC('A','m','e','d')
#define BZ_AMTC MAKEFOURCC('A','m','t','c')

static void cargo_unload_all(edict_t * transport);
static umove_t cargo_move_unload = { "stand", cargo_unload_all, NULL, CAbilityCargoDrop };

/* Cargo abilities are data-driven per holder. Do not cache one global
 * capacity: Acar/Abun/Aenc and custom aliases can coexist in one map. */
static uint32_t cargo_actor_ability_alias(edict_t * ent, uint32_t base_code) {
    char alias[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t code = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&code, token, 4);
            if (G_AbilityCode(code) == base_code) return code;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const code = ent->abilities.added[i];
        if (!code) continue;
        memcpy(alias, &code, 4);
        if (G_ActorHasSkill(ent, alias) && G_AbilityCode(code) == base_code) return code;
    }
    return 0;
}

static uint32_t cargo_living_hold_alias(edict_t * transport) {
    static uint32_t const bases[] = {
        MAKEFOURCC('A','b','u','n'),
        MAKEFOURCC('A','c','a','r'),
        MAKEFOURCC('A','e','n','c'),
    };

    FOR_LOOP(i, sizeof(bases) / sizeof(bases[0])) {
        uint32_t const alias = cargo_actor_ability_alias(transport, bases[i]);
        if (alias) return alias;
    }
    return 0;
}

static uint32_t cargo_hold_alias(edict_t * transport) {
    uint32_t alias = cargo_living_hold_alias(transport);
    if (alias) return alias;
    return cargo_actor_ability_alias(transport, BZ_AMTC);
}

uint32_t S_CargoCapacity(edict_t * transport) {
    uint32_t const alias = cargo_hold_alias(transport);
    float authored;

    if (!alias) return 0;
    authored = G_AbilityLevel(alias, 1)->data[0].number;
    if (authored <= 0.0f) return 0;
    return MIN((uint32_t)authored, (uint32_t)MAX_CARGO);
}

static bool cargo_has_capacity(edict_t * transport, uint32_t needed) {
    uint32_t const capacity = S_CargoCapacity(transport);
    return capacity > 0 && transport->cargo.count + needed <= capacity &&
           transport->cargo.count + needed <= MAX_CARGO;
}

bool S_CargoIsBurrow(edict_t * transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','u','n')) != 0;
}

/* Cargo Hold is a passive capability row; load/drop commands own interaction. */
BZ_ABILITY_PROC(CAbilityCargoHold) {
    return CAbilityPassive(ent, msg, call);
}

bool S_CargoIsCorpseHolder(edict_t * transport) {
    return cargo_actor_ability_alias(transport, BZ_AMTC) != 0;
}

bool S_CorpseCargoIsStored(edict_t const * unit) {
    return unit && (unit->aiflags & AI_CORPSE_IN_CARGO) != 0;
}

/* Stored corpse edicts keep their identity and decay state, but corpse-fed
 * abilities treat them as physically present at their current holder.  Do not
 * rely on the hidden edict's stale pre-load origin after the Wagon moves. */
bool S_CorpseCargoPosition(edict_t const * corpse, vector2_t * out) {
    edict_t * transport;

    if (!corpse || !out) return false;
    *out = corpse->s.origin2;
    if (!S_CorpseCargoIsStored(corpse)) return true;
    transport = S_CargoTransportForUnit(corpse);
    if (!transport || !transport->inuse) return false;
    *out = transport->s.origin2;
    return true;
}

/* Identify Entangled Mines so their cargo count can drive the authored model animation. */
static bool cargo_is_entangled_mine(edict_t * transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','e','g','m')) != 0;
}

/* Map the occupied Wisp count to the Required Animation Name used by the mine model. */
static cstring_t cargo_count_animation_tag(uint32_t count) {
    static cstring_t const tags[] = { NULL, "first", "second", "third", "fourth", "fifth" };
    return count < sizeof(tags) / sizeof(tags[0]) ? tags[count] : NULL;
}

/* Replace the previous cargo-count animation tag after a Wisp enters or leaves. */
static void cargo_update_entangled_animation(edict_t * transport, uint32_t old_count) {
    cstring_t old_tag, new_tag;

    if (!transport || !cargo_is_entangled_mine(transport)) return;
    old_tag = cargo_count_animation_tag(old_count);
    new_tag = cargo_count_animation_tag(transport->cargo.count);
    if (old_tag) G_AddUnitAnimationProperties(transport, old_tag, false);
    if (new_tag) G_AddUnitAnimationProperties(transport, new_tag, true);
}

bool S_CargoAttacksEnabled(edict_t const * ent) {
    if (!ent) return false;
    if (!S_CargoIsBurrow((edict_t *)ent)) return true;
    return ent->cargo.count > 0;
}

static void cargo_update_burrow_attacks(edict_t * transport) {
    UnitWeapons_t const *weapons;
    float divisor;

    if (!transport || !S_CargoIsBurrow(transport) || transport->cargo.count == 0) return;
    weapons = G_UnitWeapons(transport->class_id);
    divisor = (float)(1u << MIN(transport->cargo.count, 30u));
    if (weapons->attack1.cooldown > 0.0f)
        transport->attack1.cooldown = weapons->attack1.cooldown / divisor;
    if (weapons->attack2.cooldown > 0.0f)
        transport->attack2.cooldown = weapons->attack2.cooldown / divisor;
}

void S_CargoInitUnit(edict_t * unit) {
    if (!unit) return;
    /* Empty Burrows retain authored weapon data for HUD/upgrades but combat
     * gates attacks through S_CargoAttacksEnabled(). */
    if (unit->cargo.count > 0) cargo_update_burrow_attacks(unit);
    if (cargo_is_entangled_mine(unit) && unit->cargo.count > 0)
        cargo_update_entangled_animation(unit, 0);
}

static void cargo_add_unit(edict_t * transport, edict_t * unit) {
    uint32_t old_count;

    if (!transport || !unit || !cargo_has_capacity(transport, 1)) return;
    old_count = transport->cargo.count;
    transport->cargo.units[transport->cargo.count++] = unit;
    G_ClearUnitOrderQueue(unit);
    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    unit_stand(unit);
    unit->s.renderfx |= RF_HIDDEN;
    unit->paused = true;
    G_InvalidateUnitShortcutsForUnit(unit);
    cargo_update_burrow_attacks(transport);
    cargo_update_entangled_animation(transport, old_count);
    G_InvalidateUnitInfoPanel(transport);
    /* The selected-unit portrait is serialized on its own layer. Cargo
     * transitions replace only the stat subsection, so explicitly redraw the
     * holder portrait when occupancy changes instead of letting a stale/empty
     * portrait layer survive the info-panel update. */
    G_InvalidateUnitPortrait(transport);
    G_InvalidateCommands(G_GetPlayerClientByNumber(transport->s.player));
}

static void cargo_place_unloaded_unit(edict_t * transport, edict_t * unit) {
    vector2_t position;

    if (!transport || !unit) return;
    if (!G_FindUnitUnstuckPosition(unit, &transport->s.origin2, &position))
        position = transport->s.origin2;
    unit->s.origin.x = position.x;
    unit->s.origin.y = position.y;
    gi.LinkEntity(unit);
}

static edict_t * cargo_drop_unit(edict_t * transport, uint32_t index) {
    edict_t * unit;
    uint32_t old_count;

    if (!transport || index >= transport->cargo.count) return NULL;
    old_count = transport->cargo.count;
    unit = transport->cargo.units[index];
    for (uint32_t i = index; i < transport->cargo.count - 1; i++)
        transport->cargo.units[i] = transport->cargo.units[i + 1];
    transport->cargo.count--;
    transport->cargo.units[transport->cargo.count] = NULL;
    if (!unit) return NULL;

    {
        bool const was_corpse = S_CorpseCargoIsStored(unit);
        cargo_place_unloaded_unit(transport, unit);
        unit->s.renderfx &= ~RF_HIDDEN;
        unit->paused = false;
        unit->aiflags &= ~AI_CORPSE_IN_CARGO;
        if (was_corpse) G_RestartCorpseBoneDecayAfterCargo(unit);
    }
    G_InvalidateUnitShortcutsForUnit(unit);
    cargo_update_burrow_attacks(transport);
    cargo_update_entangled_animation(transport, old_count);
    G_InvalidateUnitInfoPanel(transport);
    /* The selected-unit portrait is serialized on its own layer. Cargo
     * transitions replace only the stat subsection, so explicitly redraw the
     * holder portrait when occupancy changes instead of letting a stale/empty
     * portrait layer survive the info-panel update. */
    G_InvalidateUnitPortrait(transport);
    G_InvalidateCommands(G_GetPlayerClientByNumber(transport->s.player));
    return unit;
}

edict_t * S_CargoUnitAt(edict_t const * transport, uint32_t index) {
    if (!transport || index >= transport->cargo.count) return NULL;
    return transport->cargo.units[index];
}

bool S_CargoUnloadAt(edict_t * transport, uint32_t index) {
    return cargo_drop_unit(transport, index) != NULL;
}

void cargo_drop_all(edict_t * transport) {
    while (transport && transport->cargo.count > 0)
        cargo_drop_unit(transport, transport->cargo.count - 1);
}

static uint32_t cargo_unload_interval_ms(edict_t * transport) {
    uint32_t const alias = cargo_hold_alias(transport);
    abilityLevel_t const *level = alias ? G_AbilityLevel(alias, 1) : NULL;
    float const seconds = level ? MAX(0.0f, level->dur) : 0.0f;

    /* Warsmash's Unload All behavior spaces passengers by Cargo Hold Dur.
     * A zero-duration custom hold still advances at most once per simulation
     * frame instead of collapsing the whole sequence into one tick. */
    return (uint32_t)MAX((float)FRAMETIME, seconds * 1000.0f);
}

/* Unloading is the active order, so Stop/Move/death replace it and the
 * normal monster scheduler suspends it during pause/stun. An independent
 * thinker used to keep ejecting passengers after the order was cancelled. */
static void cargo_unload_all(edict_t * transport) {
    if (M_IsDead(transport)) return;
    if (transport->cargo.count == 0) { unit_stand(transport); return; }
    if (G_Time() < transport->freetime) return;
    S_CargoUnloadAt(transport, 0);
    if (transport->cargo.count == 0) unit_stand(transport);
    else transport->freetime = G_Time() + cargo_unload_interval_ms(transport);
}

bool S_CargoBeginUnloadAll(edict_t * transport) {
    if (!transport || !transport->inuse || !transport->cargo.count || M_IsDead(transport) ||
        transport->paused || transport->stunned || !cargo_living_hold_alias(transport)) return false;
    if (transport->currentmove == &cargo_move_unload) return true;
    order_stop(transport);
    unit_setmove(transport, &cargo_move_unload);
    transport->freetime = 0;
    cargo_unload_all(transport);
    return true;
}

edict_t * S_CargoTransportForUnit(edict_t const * unit) {
    if (!unit) return NULL;
    FILTER_EDICTS(transport, transport->inuse && transport->cargo.count > 0) {
        FOR_LOOP(i, transport->cargo.count) {
            if (transport->cargo.units[i] == unit) return transport;
        }
    }
    return NULL;
}

/* Release a worker from its transport before the worker edict is removed or retasked. */
void S_CargoReleaseUnit(edict_t * unit) {
    edict_t * transport;

    if (!unit || !(transport = S_CargoTransportForUnit(unit))) return;
    FOR_LOOP(i, transport->cargo.count) {
        if (transport->cargo.units[i] == unit) {
            cargo_drop_unit(transport, i);
            return;
        }
    }
}

/* ---- Load (Aloa): load a unit into a transport -------------------------- */

static bool cargo_load_type_allowed(edict_t * transport, edict_t * target) {
    uint32_t const load_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','l','o','a'));
    uint32_t const battle_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
    uint32_t allowed = load_alias ? G_AbilityLevel(load_alias, 1)->unitID : 0;

    /* Orc Burrows use Battle Stations' Allowed Unit Type even when they do not
     * expose a separate Aloa object on the building. */
    if (!allowed && battle_alias) allowed = G_AbilityLevel(battle_alias, 1)->unitID;
    return !allowed || target->class_id == allowed;
}

static bool cargo_load_target_allowed(edict_t * transport, edict_t * target) {
    uint32_t const hold_alias = cargo_hold_alias(transport);
    return !hold_alias || S_SpellAllowsTarget(hold_alias, transport, target);
}

static float cargo_load_range(edict_t * transport) {
    uint32_t const hold_alias = cargo_hold_alias(transport);
    return hold_alias ? MAX(0.0f, G_AbilityLevel(hold_alias, 1)->range) : 0.0f;
}

static bool cargo_target_in_range(edict_t * transport, edict_t * target) {
    float const range = cargo_load_range(transport);
    float footprint;

    if (!transport || !target) return false;
    if ((transport->s.flags & EF_BUILDING) && transport->pathtex) {
        footprint = CM_DistanceToPathingFootprint(transport, &target->s.origin2);
        if (footprint < FLT_MAX) return footprint <= target->collision + range;
    }
    return Vector2_distance(&transport->s.origin2, &target->s.origin2) <=
           range + transport->collision + target->collision;
}

static bool corpse_cargo_target_valid(edict_t * transport, edict_t * target) {
    uint32_t alias;

    if (!transport || !target || !S_CargoIsCorpseHolder(transport) ||
        M_IsDead(transport) || !G_UnitIsRaisableCorpse(target) ||
        S_CorpseCargoIsStored(target) || (target->s.renderfx & RF_HIDDEN) ||
        S_CargoTransportForUnit(target) || !cargo_has_capacity(transport, 1)) return false;
    alias = cargo_actor_ability_alias(transport, BZ_AMEL);
    return alias && S_SpellAllowsCorpseTarget(alias, transport, target);
}

bool S_CargoTryLoad(edict_t * transport, edict_t * target) {
    if (!transport || !target || target == transport || M_IsDead(transport) || M_IsDead(target)) return false;
    if (target->s.player != transport->s.player) return false;
    /* Amtc is the Meat Wagon corpse hold, not a normal transport hold.  Keep
     * living-unit Load/Smart boarding on Acar/Abun/Aenc so a Wagon can never
     * accept a live unit merely because its corpse hold has free slots. */
    if (!cargo_living_hold_alias(transport) || !cargo_has_capacity(transport, 1)) return false;
    if (S_CargoTransportForUnit(target) || (target->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, target)) return false;
    if (!cargo_load_target_allowed(transport, target)) return false;
    if (!cargo_target_in_range(transport, target)) return false;
    cargo_add_unit(transport, target);
    return S_CargoTransportForUnit(target) == transport;
}

bool S_CorpseCargoTryLoad(edict_t * transport, edict_t * target) {
    umove_t *move;
    float wait;

    if (!corpse_cargo_target_valid(transport, target) || !cargo_target_in_range(transport, target)) return false;
    move = target->currentmove;
    wait = target->wait;
    cargo_add_unit(transport, target);
    target->currentmove = move;
    target->wait = wait;
    target->aiflags |= AI_CORPSE_IN_CARGO;
    return S_CargoTransportForUnit(target) == transport;
}

static edict_t * corpse_cargo_nearest(edict_t * transport, float max_distance) {
    edict_t * nearest = NULL;
    float best = FLT_MAX;

    if (!transport) return NULL;
    FILTER_EDICTS(corpse, corpse != transport && corpse_cargo_target_valid(transport, corpse)) {
        float const distance = Vector2_distance(&transport->s.origin2, &corpse->s.origin2);
        if (max_distance > 0.0f && distance > max_distance + transport->collision + corpse->collision) continue;
        if (distance < best) { nearest = corpse; best = distance; }
    }
    return nearest;
}

static void corpse_cargo_approach_cancel(edict_t * thinker) {
    edict_t * transport = thinker ? thinker->owner : NULL;
    if (transport && transport->inuse && transport->goalentity == thinker->goalentity &&
        move_is_active_order_walk(transport)) unit_stand(transport);
    if (thinker) G_FreeEdict(thinker);
}

void corpse_cargo_approach_think(edict_t * thinker) {
    edict_t * transport = thinker ? thinker->owner : NULL;
    edict_t * corpse = thinker ? thinker->goalentity : NULL;

    if (!thinker || !transport || !transport->inuse || M_IsDead(transport) ||
        !corpse || !corpse->inuse || corpse->spawn_time != thinker->channel.target_spawn_time ||
        !corpse_cargo_target_valid(transport, corpse)) {
        corpse_cargo_approach_cancel(thinker);
        return;
    }
    if (transport->goalentity != corpse || !move_is_active_order_walk(transport)) {
        G_FreeEdict(thinker);
        return;
    }
    if (cargo_target_in_range(transport, corpse)) {
        unit_stand(transport);
        G_FreeEdict(thinker);
        S_CorpseCargoTryLoad(transport, corpse);
        return;
    }
    if (transport->movement.flow_unreachable || transport->movement.flow_goal_reached) {
        corpse_cargo_approach_cancel(thinker);
        return;
    }
}

static bool corpse_cargo_start(edict_t * transport, edict_t * corpse, uint32_t code) {
    edict_t * thinker;

    if (!transport || !corpse || !corpse_cargo_target_valid(transport, corpse)) return false;
    if (cargo_target_in_range(transport, corpse)) return S_CorpseCargoTryLoad(transport, corpse);
    order_move(transport, corpse);
    if (transport->goalentity != corpse || !move_is_active_order_walk(transport)) return false;
    thinker = G_Spawn();
    if (!thinker) return false;
    thinker->owner = transport;
    thinker->goalentity = corpse;
    thinker->class_id = code;
    thinker->channel.target_spawn_time = corpse->spawn_time;
    thinker->think = corpse_cargo_approach_think;
    return true;
}

static bool corpse_cargo_command(edict_t * clent) {
    edict_t * transport, *corpse;
    uint32_t code;

    if (!clent || !clent->client || !(transport = G_GetMainSelectedUnit(clent->client)) ||
        !S_CargoIsCorpseHolder(transport) || !(corpse = corpse_cargo_nearest(transport, 0.0f))) return false;
    code = clent->client->menu.ability_code;
    return corpse_cargo_start(transport, corpse, code ? code : BZ_AMEL);
}

static bool corpse_cargo_autocast_acquire(edict_t * transport, uint32_t code) {
    float radius;
    edict_t * corpse;

    if (!transport || M_IsDead(transport) || transport->cargo.count >= S_CargoCapacity(transport)) return false;
    radius = G_AcquisitionRange(transport);
    if (radius <= 0.0f) return false;
    corpse = corpse_cargo_nearest(transport, radius);
    return corpse && corpse_cargo_start(transport, corpse, code);
}

static bool load_selecttarget(edict_t * clent, edict_t * target) {
    edict_t * caster = G_GetMainSelectedUnit(clent->client);
    if (clent->client->menu.ability_code == BZ_AMEL)
        return S_CorpseCargoTryLoad(caster, target);
    return S_CargoTryLoad(caster, target);
}

BZ_ABILITY_PROC(CAbilityCargoLoad) {
    uint32_t const code = call && call->item ? call->item->code : 0;
    bool const corpse_load = code && G_AbilityCode(code) == BZ_AMEL;

    switch (msg) {
    case A_COMMAND: {
        edict_t * clent = call && call->client ? call->client : ent;
        if (corpse_load) {
            if (clent && clent->client) {
                clent->client->menu.on_entity_selected = NULL;
                clent->client->menu.on_location_selected = NULL;
            }
            return corpse_cargo_command(clent);
        }
        if (!clent || !clent->client) return false;
        UI_AddCancelButton(clent);
        clent->client->menu.on_entity_selected = load_selecttarget;
        return true;
    }
    case A_AUTOCAST_ON:
        return corpse_load && ent && ent->autocast_code == code;
    case A_AUTOCAST_SET:
        return corpse_load;
    case A_AUTOCAST_ACQUIRE:
        return corpse_load && corpse_cargo_autocast_acquire(ent, code);
    default:
        return false;
    }
}

/* ---- Battle Stations (Abtl): call nearby allowed units into cargo -------- */

static uint32_t battlestations_alias(edict_t * transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
}

static bool cargo_board_target_valid(edict_t * unit, edict_t * transport) {
    if (!unit || !transport || unit == transport || M_IsDead(unit) || M_IsDead(transport)) return false;
    if (unit->paused || transport->paused || unit->s.player != transport->s.player) return false;
    if (!cargo_living_hold_alias(transport) || !cargo_has_capacity(transport, 1)) return false;
    if (S_CargoTransportForUnit(unit) || (unit->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, unit)) return false;
    return cargo_load_target_allowed(transport, unit);
}

static bool cargo_prepare_board_approach(edict_t * unit, edict_t * transport) {
    vector2_t approach;
    float const interaction_range = unit->collision + cargo_load_range(transport);

    if (!unit || !transport) return false;
    if ((transport->s.flags & EF_BUILDING) && transport->pathtex &&
        CM_FindApproachPointToFootprintForRadius(transport, &unit->s.origin2,
                                                 interaction_range, unit->collision, &approach)) {
        unit->goalentity = Waypoint_add(&approach);
    } else {
        unit->goalentity = transport;
    }
    if (!unit->goalentity) return false;
    unit->secondarygoal = transport;
    move_reset_progress(unit);
    return true;
}

static void cargo_board_cancel(edict_t * unit) {
    if (!unit) return;
    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    move_reset_progress(unit);
    unit_stand(unit);
}

static void ai_cargo_board_walk(edict_t * unit) {
    edict_t * transport = unit ? unit->secondarygoal : NULL;
    float distance, step;

    if (!cargo_board_target_valid(unit, transport)) {
        cargo_board_cancel(unit);
        return;
    }
    if (cargo_target_in_range(transport, unit)) {
        if (!S_CargoTryLoad(transport, unit)) cargo_board_cancel(unit);
        return;
    }
    if (!unit->goalentity || !unit->goalentity->inuse) {
        if (!cargo_prepare_board_approach(unit, transport)) {
            cargo_board_cancel(unit);
            return;
        }
    }
    distance = M_DistanceToGoal(unit);
    step = unit_movedistance(unit);
    if (move_is_blocked(unit, distance, step) || unit->movement.flow_unreachable) {
        cargo_board_cancel(unit);
        return;
    }
    unit_changeangle_for_radius_worker(unit, unit->collision);
    if (unit->movement.flow_goal_reached && !cargo_target_in_range(transport, unit)) {
        cargo_board_cancel(unit);
        return;
    }
    unit_moveindirection(unit);
}

static umove_t battlestations_move_walk = { "walk", ai_cargo_board_walk, NULL, CAbilityBattlestations };

bool S_CargoOrderBoard(edict_t * unit, edict_t * transport) {
    if (!cargo_board_target_valid(unit, transport)) return false;
    if (cargo_target_in_range(transport, unit)) return S_CargoTryLoad(transport, unit);
    G_ClearUnitOrderQueue(unit);
    unit->movement.follow_target = NULL;
    unit->movement.attackmove_waypoint = NULL;
    unit->movement.patrol_a = NULL;
    unit->movement.patrol_b = NULL;
    unit->movement.patrol_target = NULL;
    unit->movement.holding_position = false;
    if (!cargo_prepare_board_approach(unit, transport)) return false;
    unit_setmove(unit, &battlestations_move_walk);
    return true;
}

static bool battlestations_busy_allowed(uint32_t alias) {
    return alias && G_AbilityLevel(alias, 1)->data[0].number != 0.0f;
}

static bool battlestations_candidate(edict_t * transport, edict_t * unit, uint32_t alias, uint32_t allowed_type, float area) {
    if (!cargo_board_target_valid(unit, transport)) return false;
    if (allowed_type && unit->class_id != allowed_type) return false;
    if (!S_SpellAllowsTarget(alias, transport, unit)) return false;
    if (!battlestations_busy_allowed(alias) && unit->currentmove && unit->currentmove->proc) return false;
    return Vector2_distance(&transport->s.origin2, &unit->s.origin2) <= area;
}

static uint32_t battlestations_collect(edict_t * transport, edict_t * *out, uint32_t max_count) {
    uint32_t const alias = battlestations_alias(transport);
    uint32_t const allowed_type = alias ? G_AbilityLevel(alias, 1)->unitID : 0;
    float const area = alias ? MAX(0.0f, G_AbilityLevel(alias, 1)->area) : 0.0f;
    float distances[MAX_CARGO];
    uint32_t count = 0;

    if (!alias || !out || !max_count) return 0;
    FILTER_EDICTS(unit, unit != transport && (unit->svflags & SVF_MONSTER)) {
        float distance;
        uint32_t slot;
        if (!battlestations_candidate(transport, unit, alias, allowed_type, area)) continue;
        distance = Vector2_distance(&transport->s.origin2, &unit->s.origin2);
        slot = count;
        while (slot > 0 && distances[slot - 1] > distance) {
            if (slot < max_count) {
                distances[slot] = distances[slot - 1];
                out[slot] = out[slot - 1];
            }
            slot--;
        }
        if (slot < max_count) {
            distances[slot] = distance;
            out[slot] = unit;
            if (count < max_count) count++;
        }
    }
    return count;
}

BZ_COMMAND_PROC(AbilityBattlestations) {
    edict_t * transport = G_GetMainSelectedUnit(clent->client);
    edict_t * candidates[MAX_CARGO];
    uint32_t capacity, free_slots, count;

    if (!transport || !S_CargoIsBurrow(transport)) return;
    capacity = S_CargoCapacity(transport);
    if (capacity <= transport->cargo.count) return;
    free_slots = MIN(capacity - transport->cargo.count, (uint32_t)MAX_CARGO);
    count = battlestations_collect(transport, candidates, free_slots);
    FOR_LOOP(i, count) S_CargoOrderBoard(candidates[i], transport);
    Get_Commands_f(clent);
}

/* ---- Drop (Adro): drop cargo at a point --------------------------------- */

static bool drop_selectlocation(edict_t * clent, vector2_t const * point) {
    edict_t * caster = G_GetMainSelectedUnit(clent->client);
    (void)point;

    if (!caster || caster->cargo.count == 0) return false;
    if (clent->client->menu.ability_code == BZ_AMED) {
        bool dropped = false;
        while (caster->cargo.count > 0 &&
               S_CorpseCargoIsStored(S_CargoUnitAt(caster, caster->cargo.count - 1)))
            dropped |= S_CargoUnloadAt(caster, caster->cargo.count - 1);
        return dropped;
    }
    return S_CargoBeginUnloadAll(caster);
}

static void drop_command(edict_t * clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = drop_selectlocation;
}

BZ_COMMAND_PROC(AbilityCargoDrop) { drop_command(clent); }

/* ---- Drop Instant (Adri): unload every occupant immediately ------------- */
BZ_COMMAND_PROC(AbilityCargoDropInstant) {
    edict_t * caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || caster->cargo.count == 0) return;
    /* Retire timed unloading before a new passenger can board this frame. */
    order_stop(caster);
    cargo_drop_all(caster);
    Get_Commands_f(clent);
}

/* ---- Stand Down (Astd): stop combat, then unload all Burrow occupants --- */
void S_CargoStandDown(edict_t * caster) {
    if (!caster || !S_CargoIsBurrow(caster)) return;

    /* Stand Down is also the authoritative exit from the occupied Burrow
     * combat state.  Disable the live attack/order before cargo removal so an
     * in-progress repeating attack cannot keep driving after the Peons leave.
     * Reuse normal Stop semantics so attack-move/patrol/follow state and queued
     * orders are retired consistently with the command-card Stop button. */
    order_stop(caster);
    caster->goalentity = NULL;
    cargo_drop_all(caster);
}

BZ_COMMAND_PROC(AbilityStandDown) {
    edict_t * caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !S_CargoIsBurrow(caster)) return;
    S_CargoStandDown(caster);
    Get_Commands_f(clent);
}
