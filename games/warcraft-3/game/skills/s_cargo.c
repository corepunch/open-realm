#include <float.h>

#include "s_skills.h"

#define BZ_AMEL MAKEFOURCC('A','m','e','l')
#define BZ_AMED MAKEFOURCC('A','m','e','d')
#define BZ_AMTC MAKEFOURCC('A','m','t','c')

/* Cargo abilities are data-driven per holder. Do not cache one global
 * capacity: Acar/Abun/Aenc and custom aliases can coexist in one map. */
static DWORD cargo_actor_ability_alias(LPEDICT ent, DWORD base_code) {
    char alias[5] = {0};

    if (!ent) return 0;
    if (ent->data.UnitAbilities && ent->data.UnitAbilities->abilList) {
        PARSE_LIST(ent->data.UnitAbilities->abilList, token, parse_segment) {
            DWORD code = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(ent, token)) continue;
            memcpy(&code, token, 4);
            if (G_AbilityCode(code) == base_code) return code;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        DWORD const code = ent->abilities.added[i];
        if (!code) continue;
        memcpy(alias, &code, 4);
        if (G_ActorHasSkill(ent, alias) && G_AbilityCode(code) == base_code) return code;
    }
    return 0;
}

static DWORD cargo_hold_alias(LPEDICT transport) {
    static DWORD const bases[] = {
        MAKEFOURCC('A','b','u','n'),
        MAKEFOURCC('A','c','a','r'),
        MAKEFOURCC('A','e','n','c'),
        BZ_AMTC,
    };

    FOR_LOOP(i, sizeof(bases) / sizeof(bases[0])) {
        DWORD const alias = cargo_actor_ability_alias(transport, bases[i]);
        if (alias) return alias;
    }
    return 0;
}

DWORD S_CargoCapacity(LPEDICT transport) {
    DWORD const alias = cargo_hold_alias(transport);
    FLOAT authored;

    if (!alias) return 0;
    authored = G_AbilityLevel(alias, 1)->data[0].number;
    if (authored <= 0.0f) return 0;
    return MIN((DWORD)authored, (DWORD)MAX_CARGO);
}

static BOOL cargo_has_capacity(LPEDICT transport, DWORD needed) {
    DWORD const capacity = S_CargoCapacity(transport);
    return capacity > 0 && transport->cargo.count + needed <= capacity &&
           transport->cargo.count + needed <= MAX_CARGO;
}

BOOL S_CargoIsBurrow(LPEDICT transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','u','n')) != 0;
}

BOOL S_CargoIsCorpseHolder(LPEDICT transport) {
    return cargo_actor_ability_alias(transport, BZ_AMTC) != 0;
}

BOOL S_CorpseCargoIsStored(LPCEDICT unit) {
    return unit && (unit->aiflags & AI_CORPSE_IN_CARGO) != 0;
}

/* Identify Entangled Mines so their cargo count can drive the authored model animation. */
static BOOL cargo_is_entangled_mine(LPEDICT transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','e','g','m')) != 0;
}

/* Map the occupied Wisp count to the Required Animation Name used by the mine model. */
static LPCSTR cargo_count_animation_tag(DWORD count) {
    static LPCSTR const tags[] = { NULL, "first", "second", "third", "fourth", "fifth" };
    return count < sizeof(tags) / sizeof(tags[0]) ? tags[count] : NULL;
}

/* Replace the previous cargo-count animation tag after a Wisp enters or leaves. */
static void cargo_update_entangled_animation(LPEDICT transport, DWORD old_count) {
    LPCSTR old_tag, new_tag;

    if (!transport || !cargo_is_entangled_mine(transport)) return;
    old_tag = cargo_count_animation_tag(old_count);
    new_tag = cargo_count_animation_tag(transport->cargo.count);
    if (old_tag) G_AddUnitAnimationProperties(transport, old_tag, false);
    if (new_tag) G_AddUnitAnimationProperties(transport, new_tag, true);
}

BOOL S_CargoAttacksEnabled(LPCEDICT ent) {
    if (!ent) return false;
    if (!S_CargoIsBurrow((LPEDICT)ent)) return true;
    return ent->cargo.count > 0;
}

static void cargo_update_burrow_attacks(LPEDICT transport) {
    UnitWeapons_t const *weapons;
    FLOAT divisor;

    if (!transport || !S_CargoIsBurrow(transport) || transport->cargo.count == 0) return;
    weapons = G_UnitWeapons(transport->class_id);
    divisor = (FLOAT)(1u << MIN(transport->cargo.count, 30u));
    if (weapons->attack1.cooldown > 0.0f)
        transport->attack1.cooldown = weapons->attack1.cooldown / divisor;
    if (weapons->attack2.cooldown > 0.0f)
        transport->attack2.cooldown = weapons->attack2.cooldown / divisor;
}

void S_CargoInitUnit(LPEDICT unit) {
    if (!unit) return;
    /* Empty Burrows retain authored weapon data for HUD/upgrades but combat
     * gates attacks through S_CargoAttacksEnabled(). */
    if (unit->cargo.count > 0) cargo_update_burrow_attacks(unit);
    if (cargo_is_entangled_mine(unit) && unit->cargo.count > 0)
        cargo_update_entangled_animation(unit, 0);
}

static void cargo_add_unit(LPEDICT transport, LPEDICT unit) {
    DWORD old_count;

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

static void cargo_place_unloaded_unit(LPEDICT transport, LPEDICT unit) {
    VECTOR2 position;

    if (!transport || !unit) return;
    if (!G_FindUnitUnstuckPosition(unit, &transport->s.origin2, &position))
        position = transport->s.origin2;
    unit->s.origin.x = position.x;
    unit->s.origin.y = position.y;
    gi.LinkEntity(unit);
}

static LPEDICT cargo_drop_unit(LPEDICT transport, DWORD index) {
    LPEDICT unit;
    DWORD old_count;

    if (!transport || index >= transport->cargo.count) return NULL;
    old_count = transport->cargo.count;
    unit = transport->cargo.units[index];
    for (DWORD i = index; i < transport->cargo.count - 1; i++)
        transport->cargo.units[i] = transport->cargo.units[i + 1];
    transport->cargo.count--;
    transport->cargo.units[transport->cargo.count] = NULL;
    if (!unit) return NULL;

    cargo_place_unloaded_unit(transport, unit);
    unit->s.renderfx &= ~RF_HIDDEN;
    unit->paused = false;
    unit->aiflags &= ~AI_CORPSE_IN_CARGO;
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

LPEDICT S_CargoUnitAt(LPCEDICT transport, DWORD index) {
    if (!transport || index >= transport->cargo.count) return NULL;
    return transport->cargo.units[index];
}

BOOL S_CargoUnloadAt(LPEDICT transport, DWORD index) {
    return cargo_drop_unit(transport, index) != NULL;
}

void cargo_drop_all(LPEDICT transport) {
    while (transport && transport->cargo.count > 0)
        cargo_drop_unit(transport, transport->cargo.count - 1);
}

LPEDICT S_CargoTransportForUnit(LPCEDICT unit) {
    if (!unit) return NULL;
    FILTER_EDICTS(transport, transport->inuse && transport->cargo.count > 0) {
        FOR_LOOP(i, transport->cargo.count) {
            if (transport->cargo.units[i] == unit) return transport;
        }
    }
    return NULL;
}

/* Release a worker from its transport before the worker edict is removed or retasked. */
void S_CargoReleaseUnit(LPEDICT unit) {
    LPEDICT transport;

    if (!unit || !(transport = S_CargoTransportForUnit(unit))) return;
    FOR_LOOP(i, transport->cargo.count) {
        if (transport->cargo.units[i] == unit) {
            cargo_drop_unit(transport, i);
            return;
        }
    }
}

/* ---- Load (Aloa): load a unit into a transport -------------------------- */

static BOOL cargo_load_type_allowed(LPEDICT transport, LPEDICT target) {
    DWORD const load_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','l','o','a'));
    DWORD const battle_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
    DWORD allowed = load_alias ? G_AbilityLevel(load_alias, 1)->unitID : 0;

    /* Orc Burrows use Battle Stations' Allowed Unit Type even when they do not
     * expose a separate Aloa object on the building. */
    if (!allowed && battle_alias) allowed = G_AbilityLevel(battle_alias, 1)->unitID;
    return !allowed || target->class_id == allowed;
}

static BOOL cargo_load_target_allowed(LPEDICT transport, LPEDICT target) {
    DWORD const hold_alias = cargo_hold_alias(transport);
    return !hold_alias || S_SpellAllowsTarget(hold_alias, transport, target);
}

static FLOAT cargo_load_range(LPEDICT transport) {
    DWORD const hold_alias = cargo_hold_alias(transport);
    return hold_alias ? MAX(0.0f, G_AbilityLevel(hold_alias, 1)->range) : 0.0f;
}

static BOOL cargo_target_in_range(LPEDICT transport, LPEDICT target) {
    FLOAT const range = cargo_load_range(transport);
    FLOAT footprint;

    if (!transport || !target) return false;
    if ((transport->s.flags & EF_BUILDING) && transport->pathtex) {
        footprint = CM_DistanceToPathingFootprint(transport, &target->s.origin2);
        if (footprint < FLT_MAX) return footprint <= target->collision + range;
    }
    return Vector2_distance(&transport->s.origin2, &target->s.origin2) <=
           range + transport->collision + target->collision;
}

BOOL S_CargoTryLoad(LPEDICT transport, LPEDICT target) {
    if (!transport || !target || target == transport || M_IsDead(transport) || M_IsDead(target)) return false;
    if (target->s.player != transport->s.player) return false;
    if (!cargo_hold_alias(transport) || !cargo_has_capacity(transport, 1)) return false;
    if (S_CargoTransportForUnit(target) || (target->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, target)) return false;
    if (!cargo_load_target_allowed(transport, target)) return false;
    if (!cargo_target_in_range(transport, target)) return false;
    cargo_add_unit(transport, target);
    return S_CargoTransportForUnit(target) == transport;
}

BOOL S_CorpseCargoTryLoad(LPEDICT transport, LPEDICT target) {
    DWORD alias;
    umove_t *move;
    FLOAT wait;

    if (!transport || !target || !S_CargoIsCorpseHolder(transport) ||
        M_IsDead(transport) || !G_UnitIsRaisableCorpse(target) ||
        target->s.player != transport->s.player || S_CorpseCargoIsStored(target) ||
        (target->s.renderfx & RF_HIDDEN) || S_CargoTransportForUnit(target) ||
        !cargo_has_capacity(transport, 1)) return false;
    alias = cargo_actor_ability_alias(transport, BZ_AMEL);
    if (!alias || !S_SpellAllowsCorpseTarget(alias, transport, target) ||
        Vector2_distance(&transport->s.origin2, &target->s.origin2) >
        MAX(0.0f, G_AbilityLevel(alias, 1)->range) + transport->collision + target->collision) return false;
    move = target->currentmove;
    wait = target->wait;
    cargo_add_unit(transport, target);
    target->currentmove = move;
    target->wait = wait;
    target->aiflags |= AI_CORPSE_IN_CARGO;
    return S_CargoTransportForUnit(target) == transport;
}

static BOOL load_selecttarget(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    if (clent->client->menu.ability_code == BZ_AMEL)
        return S_CorpseCargoTryLoad(caster, target);
    return S_CargoTryLoad(caster, target);
}

BZ_COMMAND_PROC(AbilityCargoLoad) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = load_selecttarget;
}

/* ---- Battle Stations (Abtl): call nearby allowed units into cargo -------- */

static DWORD battlestations_alias(LPEDICT transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
}

static BOOL cargo_board_target_valid(LPEDICT unit, LPEDICT transport) {
    if (!unit || !transport || unit == transport || M_IsDead(unit) || M_IsDead(transport)) return false;
    if (unit->paused || transport->paused || unit->s.player != transport->s.player) return false;
    if (!cargo_hold_alias(transport) || !cargo_has_capacity(transport, 1)) return false;
    if (S_CargoTransportForUnit(unit) || (unit->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, unit)) return false;
    return cargo_load_target_allowed(transport, unit);
}

static BOOL cargo_prepare_board_approach(LPEDICT unit, LPEDICT transport) {
    VECTOR2 approach;
    FLOAT const interaction_range = unit->collision + cargo_load_range(transport);

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

static void cargo_board_cancel(LPEDICT unit) {
    if (!unit) return;
    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    move_reset_progress(unit);
    unit_stand(unit);
}

static void ai_cargo_board_walk(LPEDICT unit) {
    LPEDICT transport = unit ? unit->secondarygoal : NULL;
    FLOAT distance, step;

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

BOOL S_CargoOrderBoard(LPEDICT unit, LPEDICT transport) {
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

static BOOL battlestations_busy_allowed(DWORD alias) {
    return alias && G_AbilityLevel(alias, 1)->data[0].number != 0.0f;
}

static BOOL battlestations_candidate(LPEDICT transport, LPEDICT unit, DWORD alias, DWORD allowed_type, FLOAT area) {
    if (!cargo_board_target_valid(unit, transport)) return false;
    if (allowed_type && unit->class_id != allowed_type) return false;
    if (!S_SpellAllowsTarget(alias, transport, unit)) return false;
    if (!battlestations_busy_allowed(alias) && unit->currentmove && unit->currentmove->proc) return false;
    return Vector2_distance(&transport->s.origin2, &unit->s.origin2) <= area;
}

static DWORD battlestations_collect(LPEDICT transport, LPEDICT *out, DWORD max_count) {
    DWORD const alias = battlestations_alias(transport);
    DWORD const allowed_type = alias ? G_AbilityLevel(alias, 1)->unitID : 0;
    FLOAT const area = alias ? MAX(0.0f, G_AbilityLevel(alias, 1)->area) : 0.0f;
    FLOAT distances[MAX_CARGO];
    DWORD count = 0;

    if (!alias || !out || !max_count) return 0;
    FILTER_EDICTS(unit, unit != transport && (unit->svflags & SVF_MONSTER)) {
        FLOAT distance;
        DWORD slot;
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
    LPEDICT transport = G_GetMainSelectedUnit(clent->client);
    LPEDICT candidates[MAX_CARGO];
    DWORD capacity, free_slots, count;

    if (!transport || !S_CargoIsBurrow(transport)) return;
    capacity = S_CargoCapacity(transport);
    if (capacity <= transport->cargo.count) return;
    free_slots = MIN(capacity - transport->cargo.count, (DWORD)MAX_CARGO);
    count = battlestations_collect(transport, candidates, free_slots);
    FOR_LOOP(i, count) S_CargoOrderBoard(candidates[i], transport);
    Get_Commands_f(clent);
}

/* ---- Drop (Adro): drop cargo at a point --------------------------------- */

static BOOL drop_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    (void)point;

    if (!caster || caster->cargo.count == 0) return false;
    if (clent->client->menu.ability_code == BZ_AMED) {
        BOOL dropped = false;
        while (caster->cargo.count > 0 &&
               S_CorpseCargoIsStored(S_CargoUnitAt(caster, caster->cargo.count - 1)))
            dropped |= S_CargoUnloadAt(caster, caster->cargo.count - 1);
        return dropped;
    }
    return cargo_drop_unit(caster, caster->cargo.count - 1) != NULL;
}

static void drop_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = drop_selectlocation;
}

BZ_COMMAND_PROC(AbilityCargoDrop) { drop_command(clent); }

/* ---- Drop Instant (Adri): instant drop ---------------------------------- */
BZ_COMMAND_PROC(AbilityCargoDropInstant) { drop_command(clent); }

/* ---- Stand Down (Astd): stop combat, then unload all Burrow occupants --- */
void S_CargoStandDown(LPEDICT caster) {
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
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !S_CargoIsBurrow(caster)) return;
    S_CargoStandDown(caster);
    Get_Commands_f(clent);
}
