#include <float.h>

#include "s_skills.h"

#define BZ_AMEL MAKEFOURCC('A','m','e','l')
#define BZ_AMED MAKEFOURCC('A','m','e','d')
#define BZ_AMTC MAKEFOURCC('A','m','t','c')

static void cargo_unload_all(edict_t *transport);
static umove_t cargo_move_unload = { "stand", cargo_unload_all, NULL, CAbilityCargoDrop };
static bool cargo_unload_move_arrive(edict_t *, abilityCall_t const *);
static void cargo_ferry_loaded(edict_t *);
static bool cargo_begin_ferry_board(edict_t *,edict_t *);
static edict_t *cargo_select_ferry(edict_t *,vec2_t const *,edict_t *);
static bool cargo_admit_recovery_board(edict_t *,edict_t *,vec2_t const *);
static umove_t cargo_ferry_board, cargo_ferry_load, cargo_ferry_drop;
static bool cargo_forget_boarder(edict_t *,edict_t *);
enum { CARGO_QUEUE_LOAD = 1 };
static queuedOrderResult_t cargo_queued_load(edict_t *,unitOrder_t const *);

BZ_ABILITY_PROC(CAbilityCargoDrop);

static entitySet_t cargo_holders;
static edict_t *cargo_holder_edicts;
static uint32_t cargo_holder_highwater;
static bool cargo_holders_valid;

void S_InvalidateCargoHolders(void) { cargo_holders_valid = false; }

void S_CargoForgetHolder(edict_t const *ent) {
    uintptr_t offset = (uintptr_t)ent - (uintptr_t)globals.edicts;
    if (offset < sizeof(*ent) * MAX_ENTITIES && !(offset % sizeof(*ent)))
        entity_set_put(&cargo_holders, offset / sizeof(*ent), false);
}

/* Allocation/restoration changes holder discovery; passenger writes and live
 * eligibility do not. Keep empty holders so filling an existing hold is live,
 * and retain original edict order even for duplicate saved references. */
static void cargo_prepare_holders(void) {
    if (cargo_holders_valid && cargo_holder_edicts == globals.edicts &&
        cargo_holder_highwater <= globals.num_edicts) return;
    cargo_holders = (entitySet_t){0};
    FOR_LOOP(i, globals.num_edicts)
        if (globals.edicts[i].cargo) entity_set_put(&cargo_holders, i, true);
    cargo_holder_edicts = globals.edicts;
    cargo_holder_highwater = globals.num_edicts;
    cargo_holders_valid = true;
}

/* Cargo abilities are data-driven per holder. Do not cache one global
 * capacity: Acar/Abun/Aenc and custom aliases can coexist in one map. */
static uint32_t cargo_actor_ability_alias(edict_t *ent, uint32_t base_code) {
    char alias[5] = {0};

    if (!ent) return 0;
    uint32_t count;
    unitAbilityToken_t const *tokens=G_UnitAbilityTokens(ent->data.UnitAbilities,&count);
    FOR_LOOP(i,count)
        if(tokens[i].length==4 && tokens[i].base==base_code &&
           G_ActorHasAbilityCode(ent,tokens[i].code))return tokens[i].code;
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        uint32_t const code = ent->abilities.added[i];
        if (!code) continue;
        memcpy(alias, &code, 4);
        if (G_ActorHasSkill(ent, alias) && G_AbilityCode(code) == base_code) return code;
    }
    return 0;
}

static uint32_t cargo_living_hold_alias(edict_t *transport) {
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

static uint32_t cargo_hold_alias(edict_t *transport) {
    uint32_t alias = cargo_living_hold_alias(transport);
    if (alias) return alias;
    return cargo_actor_ability_alias(transport, BZ_AMTC);
}

uint32_t S_CargoCapacity(edict_t *transport) {
    uint32_t const alias = cargo_hold_alias(transport);
    float authored;

    if (!alias) return 0;
    authored = G_AbilityLevel(alias, 1)->data[0].number;
    if (authored <= 0.0f) return 0;
    return MIN((uint32_t)authored, (uint32_t)MAX_CARGO);
}

static bool cargo_has_capacity(edict_t *transport, uint32_t needed) {
    uint32_t const capacity = S_CargoCapacity(transport);
    uint32_t const count = transport && transport->cargo ? transport->cargo->count : 0;
    return capacity > 0 && count + needed <= capacity && count + needed <= MAX_CARGO;
}

/* Ordinary Cargo Hold consumes authored cargo space. Other holder
 * implementations keep their existing slot rules. Polymorph leaves the
 * original UnitData bound, matching the original-size snapshot in BPly. */
static bool cargo_has_room_for(edict_t *transport,edict_t *target) {
    if(!transport || !target)return false;
    if(!cargo_actor_ability_alias(transport,MAKEFOURCC('A','c','a','r')))
        return cargo_has_capacity(transport,1);
    int32_t needed=target->data.UnitData->cargoSize;
    uint32_t remaining=S_CargoCapacity(transport);
    if(needed<=0 || (uint32_t)needed>remaining ||
       (transport->cargo && transport->cargo->count>=MAX_CARGO))return false;
    if(transport->cargo)FOR_LOOP(i,transport->cargo->count) {
        edict_t *passenger=transport->cargo->units[i];
        int32_t size=passenger->data.UnitData->cargoSize;
        if(size<=0 || (uint32_t)size>remaining)return false;
        remaining-=size;
    }
    return (uint32_t)needed<=remaining;
}

bool S_CargoIsBurrow(edict_t *transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','u','n')) != 0;
}

/* Cargo Hold is a passive capability row; load/drop commands own interaction. */
BZ_ABILITY_PROC(CAbilityCargoHold) {
    return CAbilityPassive(ent, msg, call);
}

bool S_CargoIsCorpseHolder(edict_t *transport) {
    return cargo_actor_ability_alias(transport, BZ_AMTC) != 0;
}

bool S_CorpseCargoIsStored(edict_t const *unit) {
    return unit && (unit->aiflags & AI_CORPSE_IN_CARGO) != 0;
}

/* Stored corpse edicts keep their identity and decay state, but corpse-fed
 * abilities treat them as physically present at their current holder.  Do not
 * rely on the hidden edict's stale pre-load origin after the Wagon moves. */
bool S_CorpseCargoPosition(edict_t const *corpse, vec2_t *out) {
    edict_t *transport;

    if (!corpse || !out) return false;
    *out = corpse->s.origin2;
    if (!S_CorpseCargoIsStored(corpse)) return true;
    transport = S_CargoTransportForUnit(corpse);
    if (!transport || !transport->inuse) return false;
    *out = transport->s.origin2;
    return true;
}

/* Identify Entangled Mines so their cargo count can drive the authored model animation. */
static bool cargo_is_entangled_mine(edict_t *transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','e','g','m')) != 0;
}

/* Map the occupied Wisp count to the Required Animation Name used by the mine model. */
static cstring_t cargo_count_animation_tag(uint32_t count) {
    static cstring_t const tags[] = { NULL, "first", "second", "third", "fourth", "fifth" };
    return count < sizeof(tags) / sizeof(tags[0]) ? tags[count] : NULL;
}

/* Replace the previous cargo-count animation tag after a Wisp enters or leaves. */
static void cargo_update_entangled_animation(edict_t *transport, uint32_t old_count) {
    cstring_t old_tag, new_tag;

    if (!transport || !cargo_is_entangled_mine(transport)) return;
    old_tag = cargo_count_animation_tag(old_count);
    new_tag = cargo_count_animation_tag(transport->cargo->count);
    if (old_tag) G_AddUnitAnimationProperties(transport, old_tag, false);
    if (new_tag) G_AddUnitAnimationProperties(transport, new_tag, true);
}

bool S_CargoAttacksEnabled(edict_t const *ent) {
    if (!ent) return false;
    if (!S_CargoIsBurrow((edict_t *)ent)) return true;
    return ent->cargo && ent->cargo->count > 0;
}

static void cargo_update_burrow_attacks(edict_t *transport) {
    UnitWeapons_t const *weapons;
    float divisor;

    if (!transport || !transport->cargo || !S_CargoIsBurrow(transport) || transport->cargo->count == 0) return;
    weapons = G_UnitWeapons(transport->class_id);
    divisor = (float)(1u << MIN(transport->cargo->count, 30u));
    if (weapons->attack1.cooldown > 0.0f)
        S_AttackProfileWrite(transport, 0)->cooldown = weapons->attack1.cooldown / divisor;
    if (weapons->attack2.cooldown > 0.0f)
        S_AttackProfileWrite(transport, 1)->cooldown = weapons->attack2.cooldown / divisor;
}

void S_CargoInitUnit(edict_t *unit) {
    if (!unit) return;
    if (unit->cargo) S_InvalidateCargoHolders();
    /* Empty Burrows retain authored weapon data for HUD/upgrades but combat
     * gates attacks through S_CargoAttacksEnabled(). */
    if (unit->cargo && unit->cargo->count > 0) cargo_update_burrow_attacks(unit);
    if (cargo_is_entangled_mine(unit) && unit->cargo && unit->cargo->count > 0)
        cargo_update_entangled_animation(unit, 0);
}

static void cargo_add_unit(edict_t *transport, edict_t *unit) {
    uint32_t old_count;

    if (!transport || !unit || !cargo_has_room_for(transport, unit)) return;
    if (!transport->cargo) transport->cargo = G_AllocCargo();
    assert(transport->cargo);
    old_count = transport->cargo->count;
    cargo_forget_boarder(transport,unit);
    transport->cargo->units[transport->cargo->count++] = unit;
    G_ClearUnitOrderQueue(unit);
    S_SetMoveGoal(unit, &unit->goalentity, NULL);
    S_SetMoveGoal(unit, &unit->secondarygoal, NULL);
    unit_stand(unit);
    G_SetEntityHidden(unit,true);
    /* Cargo membership precedes TargetLost, so the observer sees loaded a9
     * rather than ordinary hidden aa before Load returns. */
    S_UnitTargetLost(unit);
    S_SetUnitPauseFlag(unit,true);
    /* Native562030 acquires for the Entangle Cargo task, not every hidden
     * passenger. Retain the owner independently of later holder type changes. */
    if (cargo_actor_ability_alias(transport, MAKEFOURCC('A','e','n','c'))) {
        transport->cargo->separation_suppression[old_count] = true;
        S_AcquireUnitSeparationSuppression(unit);
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
}

static void cargo_place_unloaded_unit(edict_t *transport, edict_t *unit) {
    vec2_t position;

    if (!transport || !unit) return;
    if(transport->cargo && transport->cargo->ferry_pending && transport->cargo->ferry_arrived) {
        position=transport->cargo->ferry_destination;
        G_FindUnitUnstuckPosition(unit,&transport->cargo->ferry_destination,&position);
        S_SetUnitPosition(unit,&position);
        return;
    }
    if (!G_FindUnitUnstuckPosition(unit, &transport->s.origin2, &position))
        position = transport->s.origin2;
    unit->s.origin.x = position.x;
    unit->s.origin.y = position.y;
    gi.LinkEntity(unit);
}

static edict_t *cargo_drop_unit(edict_t *transport, uint32_t index) {
    edict_t *unit;
    uint32_t old_count;

    if (!transport || !transport->cargo || index >= transport->cargo->count) return NULL;
    old_count = transport->cargo->count;
    unit = transport->cargo->units[index];
    bool const separation = transport->cargo->separation_suppression[index];
    for (uint32_t i = index; i < transport->cargo->count - 1; i++) {
        transport->cargo->units[i] = transport->cargo->units[i + 1];
        transport->cargo->separation_suppression[i] = transport->cargo->separation_suppression[i + 1];
    }
    transport->cargo->count--;
    transport->cargo->units[transport->cargo->count] = NULL;
    transport->cargo->separation_suppression[transport->cargo->count] = false;
    if (!unit) return NULL;

    /* The engine's cargo pause is an execution flag, not native PauseUnit.
     * Retire it before native5610e0's release/configuration, which precedes
     * the general placement and visibility inverse. */
    S_SetUnitPauseFlag(unit,false);
    if (separation && unit->movement.repulse.disable_depth > 0)
        S_ReleaseUnitSeparationSuppression(unit);
    {
        bool const was_corpse = S_CorpseCargoIsStored(unit);
        cargo_place_unloaded_unit(transport, unit);
        G_SetEntityHidden(unit,false);
        unit->aiflags &= ~AI_CORPSE_IN_CARGO;
        if (was_corpse) G_RestartCorpseBoneDecayAfterCargo(unit);
    }
    /* Cargo virtual31c replaces the old order after placement/visibility.
     * Dead passengers use virtual320, whose default handler is empty. */
    if(!M_IsDead(unit)) {
        order_stop_cleanup(unit);
        if((unit->aiflags&AI_TOWN_OWNED) && !G_BotRecenterCargoCaptain(unit))
            G_BotDispatchAIStop(unit);
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

edict_t *S_CargoUnitAt(edict_t const *transport, uint32_t index) {
    if (!transport || !transport->cargo || index >= transport->cargo->count) return NULL;
    return transport->cargo->units[index];
}

bool S_CargoUnloadAt(edict_t *transport, uint32_t index) {
    return cargo_drop_unit(transport, index) != NULL;
}

void cargo_drop_all(edict_t *transport) {
    while (transport && transport->cargo && transport->cargo->count > 0)
        cargo_drop_unit(transport, transport->cargo->count - 1);
}

static uint32_t cargo_unload_interval_ms(edict_t *transport) {
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
static void cargo_unload_all(edict_t *transport) {
    if (M_IsDead(transport)) return;
    if (transport->cargo->count == 0) { unit_stand(transport); return; }
    if (G_Time() < transport->freetime) return;
    S_CargoUnloadAt(transport, 0);
    if (transport->cargo->count == 0) unit_stand(transport);
    else transport->freetime = G_Time() + cargo_unload_interval_ms(transport);
}

bool S_CargoBeginUnloadAll(edict_t *transport) {
    if (!transport || !transport->inuse || !transport->cargo || !transport->cargo->count || M_IsDead(transport) ||
        transport->paused || transport->stunned || !cargo_living_hold_alias(transport)) return false;
    if (transport->currentmove == &cargo_move_unload) return true;
    order_stop_cleanup(transport);
    unit_setmove(transport, &cargo_move_unload);
    transport->freetime = 0;
    cargo_unload_all(transport);
    return true;
}

static void cargo_clear_pending_unload(edict_t *transport) {
    if (!transport) return;
    transport->movement.cargo_unload_pending = false;
    transport->movement.cargo_unload_ability = 0;
    S_SetMoveGoal(transport, &transport->movement.cargo_unload_goal, NULL);
    transport->movement.cargo_unload_goal_spawn_time = 0;
}

static bool cargo_begin_unload_at(edict_t *transport, vec2_t const *point, uint32_t ability_code) {
    vec2_t destination;
    edict_t *waypoint;

    if (!transport || !point || !transport->inuse || !transport->cargo || !transport->cargo->count || M_IsDead(transport) ||
        transport->paused || transport->stunned || !cargo_living_hold_alias(transport) || !ability_code) return false;
    destination = *point;
    if (!CM_ClosestPathablePointForRadiusFlags(point, transport->collision,
                                               M_UnitStaticPathingFlags(transport), &destination)) return false;
    cargo_clear_pending_unload(transport);
    waypoint = Waypoint_add(&destination);
    order_move(transport, waypoint);
    if (!move_is_active_order_walk(transport) || transport->goalentity != waypoint) return false;
    transport->movement.cargo_unload_ability = ability_code;
    S_SetMoveGoal(transport, &transport->movement.cargo_unload_goal, transport->goalentity);
    transport->movement.cargo_unload_goal_spawn_time = transport->goalentity->spawn_time;
    transport->movement.cargo_unload_pending = true;
    return true;
}

static bool cargo_unload_move_arrive(edict_t *transport, abilityCall_t const *call) {
    if (!transport || !transport->movement.cargo_unload_pending || !call || !call->item ||
        call->item->code != transport->movement.cargo_unload_ability) return false;
    if (transport->goalentity != transport->movement.cargo_unload_goal ||
        !transport->movement.cargo_unload_goal || !transport->movement.cargo_unload_goal->inuse ||
        transport->movement.cargo_unload_goal->spawn_time != transport->movement.cargo_unload_goal_spawn_time) {
        cargo_clear_pending_unload(transport);
        return false;
    }
    cargo_clear_pending_unload(transport);
    S_CargoBeginUnloadAll(transport);
    return true;
}

#ifdef BZ_TESTS
static uint32_t cargo_holder_visits;
#endif
edict_t *S_CargoTransportForUnit(edict_t const *unit) {
    if (!unit) return NULL;
    cargo_prepare_holders();
    for (uint32_t index = entity_set_next(&cargo_holders, 0); index < globals.num_edicts;
         index = entity_set_next(&cargo_holders, index + 1)) {
#ifdef BZ_TESTS
        cargo_holder_visits++;
#endif
        edict_t *transport = globals.edicts + index;
        if (!transport->inuse || !transport->cargo || !transport->cargo->count) continue;
        FOR_LOOP(i, transport->cargo->count) {
            if (transport->cargo->units[i] == unit) return transport;
        }
    }
    return NULL;
}

#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t, float, float);
TEST(wc3_movement, cargo_holder_queries_skip_unrelated_entities_and_keep_holder_order_live) {
    reset_entities(); setup_test_world();
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *passenger = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    edict_t *outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 192, 0);
    first->cargo = G_AllocCargo(); second->cargo = G_AllocCargo();
    first->cargo->units[0] = second->cargo->units[0] = passenger;
    first->cargo->count = second->cargo->count = 1;
    uint32_t old_count = globals.num_edicts;
    globals.num_edicts += 2048;
    FOR_LOOP(i, 2048) { g_edicts[old_count + i].inuse = true; g_edicts[old_count + i].s.number = old_count + i; }
    T_ASSERT(S_CargoTransportForUnit(passenger) == first);
    cargo_holder_visits = 0;
    FOR_LOOP(i, 128) T_ASSERT(!S_CargoTransportForUnit(outside));
    T_EQ(cargo_holder_visits, 256);
    G_FreeCargo(first);
    T_ASSERT(S_CargoTransportForUnit(passenger) == second);
    first->cargo = G_AllocCargo(); first->cargo->units[0] = passenger; first->cargo->count = 1;
    T_ASSERT(S_CargoTransportForUnit(passenger) == first);
    first->cargo->count = 0;
    T_ASSERT(S_CargoTransportForUnit(passenger) == second);
    second->cargo->units[0] = outside;
    T_ASSERT(!S_CargoTransportForUnit(passenger));
    T_ASSERT(S_CargoTransportForUnit(outside) == second);
    reset_entities(); setup_test_world();
}
#endif

/* Release a worker from its transport before the worker edict is removed or retasked. */
void S_CargoReleaseUnit(edict_t *unit) {
    edict_t *transport;

    if(!unit)return;
    cargo_prepare_holders();
    for(uint32_t i=entity_set_next(&cargo_holders,0);i<globals.num_edicts;
        i=entity_set_next(&cargo_holders,i+1))cargo_forget_boarder(globals.edicts+i,unit);
    if(!(transport=S_CargoTransportForUnit(unit)))return;
    FOR_LOOP(i, transport->cargo->count) {
        if (transport->cargo->units[i] == unit) {
            cargo_drop_unit(transport, i);
            return;
        }
    }
}

/* ---- Load (Aloa): load a unit into a transport -------------------------- */

static bool cargo_load_type_allowed(edict_t *transport, edict_t *target) {
    uint32_t const load_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','l','o','a'));
    uint32_t const battle_alias = cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
    uint32_t allowed = load_alias ? G_AbilityLevel(load_alias, 1)->unitID : 0;

    /* Orc Burrows use Battle Stations' Allowed Unit Type even when they do not
     * expose a separate Aloa object on the building. */
    if (!allowed && battle_alias) allowed = G_AbilityLevel(battle_alias, 1)->unitID;
    return !allowed || target->class_id == allowed;
}

static bool cargo_load_target_allowed(edict_t *transport, edict_t *target) {
    uint32_t const hold_alias = cargo_hold_alias(transport);
    return !hold_alias || S_SpellAllowsTarget(hold_alias, transport, target);
}

static float cargo_load_range(edict_t *transport) {
    uint32_t const hold_alias = cargo_hold_alias(transport);
    return hold_alias ? MAX(0.0f, G_AbilityLevel(hold_alias, 1)->range) : 0.0f;
}

static bool cargo_target_in_range(edict_t *transport, edict_t *target) {
    float const range = cargo_load_range(transport);
    float footprint;

    if (!transport || !target) return false;
    if (G_UnitIsStructure(transport) && transport->pathtex) {
        footprint = CM_DistanceToPathingFootprint(transport, &target->s.origin2);
        if (footprint < FLT_MAX) return footprint <= target->collision + range;
    }
    return Vector2_distance(&transport->s.origin2, &target->s.origin2) <=
           range + transport->collision + target->collision;
}

static bool corpse_cargo_target_valid(edict_t *transport, edict_t *target) {
    uint32_t alias;

    if (!transport || !target || !S_CargoIsCorpseHolder(transport) ||
        M_IsDead(transport) || !G_UnitIsRaisableCorpse(target) ||
        S_CorpseCargoIsStored(target) || (target->s.renderfx & RF_HIDDEN) ||
        S_CargoTransportForUnit(target) || !cargo_has_capacity(transport, 1)) return false;
    alias = cargo_actor_ability_alias(transport, BZ_AMEL);
    return alias && S_SpellAllowsCorpseTarget(alias, transport, target);
}

bool S_CargoTryLoad(edict_t *transport, edict_t *target) {
    if (!transport || !target || target == transport || M_IsDead(transport) || M_IsDead(target)) return false;
    /* Retail permits Wisps to rally to an unfinished Entangled Mine, but the
     * cargo transition itself must wait until its construction is complete. */
    if (cargo_is_entangled_mine(transport) && transport->construction) return false;
    if (target->s.player != transport->s.player) return false;
    /* Amtc is the Meat Wagon corpse hold, not a normal transport hold.  Keep
     * living-unit Load/Smart boarding on Acar/Abun/Aenc so a Wagon can never
     * accept a live unit merely because its corpse hold has free slots. */
    if (!cargo_living_hold_alias(transport) || !cargo_has_room_for(transport, target)) return false;
    if (S_CargoTransportForUnit(target) || (target->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, target)) return false;
    if (!cargo_load_target_allowed(transport, target)) return false;
    if (!cargo_target_in_range(transport, target)) return false;
    cargo_add_unit(transport, target);
    bool loaded=S_CargoTransportForUnit(target)==transport;
    if(loaded)cargo_ferry_loaded(transport);
    return loaded;
}

bool S_CorpseCargoTryLoad(edict_t *transport, edict_t *target) {
    umove_t *move;
    float wait;

    if (!corpse_cargo_target_valid(transport, target) || !cargo_target_in_range(transport, target)) return false;
    move = target->currentmove;
    wait = target->wait;
    cargo_add_unit(transport, target);
    M_SetMove(target,move);
    target->wait = wait;
    target->aiflags |= AI_CORPSE_IN_CARGO;
    return S_CargoTransportForUnit(target) == transport;
}

static edict_t *corpse_cargo_nearest(edict_t *transport, float max_distance) {
    edict_t *nearest = NULL;
    float best = FLT_MAX;

    if (!transport) return NULL;
    FILTER_EDICTS(corpse, corpse != transport && corpse_cargo_target_valid(transport, corpse)) {
        float const distance = Vector2_distance(&transport->s.origin2, &corpse->s.origin2);
        if (max_distance > 0.0f && distance > max_distance + transport->collision + corpse->collision) continue;
        if (distance < best) { nearest = corpse; best = distance; }
    }
    return nearest;
}

static void corpse_cargo_approach_cancel(edict_t *thinker) {
    edict_t *transport = thinker ? thinker->owner : NULL;
    if (transport && transport->inuse && transport->goalentity == thinker->goalentity &&
        move_is_active_order_walk(transport)) unit_stand(transport);
    if (thinker) G_FreeEdict(thinker);
}

void corpse_cargo_approach_think(edict_t *thinker) {
    edict_t *transport = thinker ? thinker->owner : NULL;
    edict_t *corpse = S_SpellChannelTarget(thinker);

    if (!thinker || !transport || !transport->inuse || M_IsDead(transport) ||
        !corpse || !corpse_cargo_target_valid(transport, corpse)) {
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

static bool corpse_cargo_start(edict_t *transport, edict_t *corpse, uint32_t code) {
    edict_t *thinker;

    if (!transport || !corpse || !corpse_cargo_target_valid(transport, corpse)) return false;
    if (cargo_target_in_range(transport, corpse)) return S_CorpseCargoTryLoad(transport, corpse);
    order_move(transport, corpse);
    if (transport->goalentity != corpse || !move_is_active_order_walk(transport)) return false;
    thinker = G_Spawn();
    if (!thinker) return false;
    thinker->owner = transport;
    S_SetMoveGoal(thinker, &thinker->goalentity, corpse);
    thinker->class_id = code;
    if (!thinker->channel) thinker->channel = G_AllocChannel();
    assert(thinker->channel);
    thinker->channel->target_spawn_time = corpse->spawn_time;
    thinker->think = corpse_cargo_approach_think;
    return true;
}

static bool corpse_cargo_command(edict_t *clent) {
    edict_t *transport, *corpse;
    uint32_t code;

    if (!clent || !clent->client || !(transport = G_GetMainSelectedUnit(clent->client)) ||
        !S_CargoIsCorpseHolder(transport) || !(corpse = corpse_cargo_nearest(transport, 0.0f))) return false;
    code = clent->client->menu.ability_code;
    return corpse_cargo_start(transport, corpse, code ? code : BZ_AMEL);
}

static bool corpse_cargo_autocast_acquire(edict_t *transport, uint32_t code) {
    float radius;
    edict_t *corpse;

    if (!transport || M_IsDead(transport) || !cargo_has_capacity(transport, 1)) return false;
    radius = G_AcquisitionRange(transport);
    if (radius <= 0.0f) return false;
    corpse = corpse_cargo_nearest(transport, radius);
    return corpse && corpse_cargo_start(transport, corpse, code);
}

static bool load_selecttarget(edict_t *clent, edict_t *target) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    if (clent->client->menu.ability_code == BZ_AMEL)
        return S_CorpseCargoTryLoad(caster, target);
    return S_CargoTryLoad(caster, target);
}

BZ_ABILITY_PROC(CAbilityCargoLoad) {
    uint32_t const code = call && call->item ? call->item->code : 0;
    bool const corpse_load = code && G_AbilityCode(code) == BZ_AMEL;

    switch (msg) {
    case A_TARGET_REMOVED:
        if(ent && call && ent->currentmove==&cargo_ferry_load && ent->goalentity==call->removed_target) {
            S_EndUnitTargetChase(ent,CAbilityCargoLoad);
            S_SetMoveGoal(ent,&ent->goalentity,NULL);
            unit_stand(ent);
            return true;
        }
        return false;
    case A_QUEUE_ORDER_START:
        return call && call->queued_order ? cargo_queued_load(ent,call->queued_order) : QUEUED_ORDER_UNHANDLED;
    case A_COMMAND: {
        edict_t *clent = call && call->client ? call->client : ent;
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

static uint32_t battlestations_alias(edict_t *transport) {
    return cargo_actor_ability_alias(transport, MAKEFOURCC('A','b','t','l'));
}

static bool cargo_board_target_eligible(edict_t *unit, edict_t *transport) {
    if (!unit || !transport || unit == transport || M_IsDead(unit) || M_IsDead(transport)) return false;
    if (unit->paused || transport->paused || unit->s.player != transport->s.player) return false;
    if (!cargo_living_hold_alias(transport)) return false;
    /* Keep the approach order alive through Entangled Mine construction. The
     * final load attempt is retried by this same movement order on completion. */
    if (S_CargoTransportForUnit(unit) || (unit->s.renderfx & RF_HIDDEN)) return false;
    if (!cargo_load_type_allowed(transport, unit)) return false;
    /* Incomplete Entangled Mines are valid rally destinations. They become
     * loadable on this existing approach order as soon as construction ends. */
    return cargo_load_target_allowed(transport, unit);
}

static bool cargo_board_target_valid(edict_t *unit,edict_t *transport) {
    return cargo_board_target_eligible(unit,transport) && cargo_has_room_for(transport,unit);
}

static bool cargo_prepare_board_approach(edict_t *unit, edict_t *transport) {
    vec2_t approach;
    float const interaction_range = unit->collision + cargo_load_range(transport);

    if (!unit || !transport) return false;
    if (G_UnitIsStructure(transport) && transport->pathtex &&
        CM_FindApproachPointToFootprintForRadius(transport, &unit->s.origin2,
                                                 interaction_range, unit->collision, &approach)) {
        S_SetMoveGoal(unit, &unit->goalentity, Waypoint_add(&approach));
    } else {
        S_SetMoveGoal(unit, &unit->goalentity, transport);
    }
    if (!unit->goalentity) return false;
    S_SetMoveGoal(unit, &unit->secondarygoal, transport);
    move_reset_progress(unit);
    return true;
}

static void cargo_board_cancel(edict_t *unit) {
    if (!unit) return;
    S_SetMoveGoal(unit, &unit->goalentity, NULL);
    S_SetMoveGoal(unit, &unit->secondarygoal, NULL);
    move_reset_progress(unit);
    unit_stand(unit);
}

static umove_t cargo_board_move_walk, cargo_board_move_wait;

static void ai_cargo_board_walk(edict_t *unit) {
    edict_t *transport = unit ? unit->secondarygoal : NULL;
    float distance, step;

    if (!cargo_board_target_valid(unit, transport)) {
        cargo_board_cancel(unit);
        return;
    }
    if (cargo_target_in_range(transport, unit)) {
        if (cargo_is_entangled_mine(transport) && transport->construction) {
            if (unit->goalentity && unit->goalentity != transport &&
                unit->goalentity->class_id == 0)
                G_FreeEdict(unit->goalentity);
            S_SetMoveGoal(unit, &unit->goalentity, transport);
            S_SetMoveGoal(unit, &unit->secondarygoal, transport);
            move_reset_progress(unit);
            /* Keep the boarding owner active while presenting a stand pose.
             * The same behavior retries on the first tick after construction
             * completes, matching rallied-Wisp automatic mine entry. */
            unit_setmove(unit, &cargo_board_move_wait);
            return;
        }
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

static umove_t cargo_board_move_walk = { "walk", ai_cargo_board_walk, NULL, CAbilityBattlestations };
static umove_t cargo_board_move_wait = { "stand", ai_cargo_board_walk, NULL, CAbilityBattlestations };

static bool cargo_naval_pair(edict_t const *unit,edict_t const *transport) {
    uint32_t passenger=S_UnitMovementProfile(unit->data.UnitData)->bits;
    return S_UnitMoveCoarseMask(transport)==CM_PATHING_UNFLOATABLE &&
        (passenger==wc3_movement_profile(UNIT_MOVE_FOOT)->bits ||
         passenger==wc3_movement_profile(UNIT_MOVE_AMPH)->bits);
}
static bool cargo_prepare_shore(edict_t *unit,edict_t *transport) {
    if(!unit->cargo)unit->cargo=G_AllocCargo();
    if(!unit->cargo->shore_valid)unit->cargo->shore_valid=G_FindCargoShorePair(unit,transport,
        &unit->cargo->shore_ground,&unit->cargo->shore_water);
    return unit->cargo->shore_valid;
}

bool S_CargoOrderBoard(edict_t *unit, edict_t *transport) {
    if (!cargo_board_target_valid(unit, transport)) return false;
    /* Native Board chooses its strategy from Cargo Hold and movement, not Load presence. */
    if((transport->aiflags&AI_FLYING) || cargo_naval_pair(unit,transport))
        return cargo_begin_ferry_board(unit,transport);
    if (cargo_target_in_range(transport, unit)) {
        if (cargo_is_entangled_mine(transport) && transport->construction) {
            G_ClearUnitOrderQueue(unit);
            S_SetMoveGoal(unit, &unit->goalentity, transport);
            S_SetMoveGoal(unit, &unit->secondarygoal, transport);
            move_reset_progress(unit);
            unit_setmove(unit, &cargo_board_move_wait);
            return true;
        }
        return S_CargoTryLoad(transport, unit);
    }
    G_ClearUnitOrderQueue(unit);
    S_SetFollowTarget(unit,NULL);
    S_SetMoveGoal(unit, &unit->movement.attackmove_waypoint, NULL);
    S_SetMoveGoal(unit, &unit->movement.patrol_a, NULL);
    S_SetMoveGoal(unit, &unit->movement.patrol_b, NULL);
    S_SetMoveGoal(unit, &unit->movement.patrol_target, NULL);
    unit->movement.holding_position = false;
    if (!cargo_prepare_board_approach(unit, transport)) return false;
    unit_setmove(unit, &cargo_board_move_walk);
    return true;
}

/* The campaign scripts use autoharvestgold for Wisps. Resolve that order to
 * the nearest owned Entangled Mine and keep all boarding checks in Cargo. */
bool S_CargoOrderNearestEntangledMine(edict_t *unit) {
    edict_t *best = NULL;
    float best_distance = FLT_MAX;

    if (!unit) return false;
    FILTER_EDICTS(transport, transport != unit && cargo_is_entangled_mine(transport) &&
                  cargo_board_target_valid(unit, transport)) {
        float const distance = Vector2_distance(&unit->s.origin2, &transport->s.origin2);
        if (distance >= best_distance) continue;
        best = transport;
        best_distance = distance;
    }
    return best && S_CargoOrderBoard(unit, best);
}

static bool battlestations_busy_allowed(uint32_t alias) {
    return alias && G_AbilityLevel(alias, 1)->data[0].number != 0.0f;
}

static bool battlestations_candidate(edict_t *transport, edict_t *unit, uint32_t alias, uint32_t allowed_type, float area) {
    if (!cargo_board_target_valid(unit, transport)) return false;
    if (allowed_type && unit->class_id != allowed_type) return false;
    if (!S_SpellAllowsTarget(alias, transport, unit)) return false;
    if (!battlestations_busy_allowed(alias) && unit->currentmove && unit->currentmove->proc) return false;
    return Vector2_distance(&transport->s.origin2, &unit->s.origin2) <= area;
}

static uint32_t battlestations_collect(edict_t *transport, edict_t * *out, uint32_t max_count) {
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

static void AbilityBattlestations_Command(edict_t *clent);
BZ_ABILITY_PROC(CAbilityBattlestations) {
    if(msg==A_TARGET_REMOVED && ent && call && ent->currentmove==&cargo_ferry_board &&
       ent->goalentity==call->removed_target) {
        /* TargetLost recovery completes before RemoveUnit returns (MapAI).
         * Waiting for Board's next think lets its old physical group advance. */
        edict_t *transport=call->removed_target;
        bool recover=(ent->aiflags&AI_TOWN_OWNED) && transport->cargo &&
            transport->cargo->ferry_pending && !cargo_target_in_range(transport,ent);
        vec2_t destination=recover ? transport->cargo->ferry_destination : (vec2_t){0};
        /* Native selects before retiring Board, while the removed carrier is
         * still discoverable. Selecting that same identity means guard return,
         * even if another, farther carrier exists (5fb4c0). */
        edict_t *replacement=recover ? cargo_select_ferry(ent,&destination,transport) : NULL;
        S_EndUnitTargetChase(ent,CAbilityBattlestations);
        S_SetMoveGoal(ent,&ent->goalentity,NULL);
        unit_stand(ent);
        if(recover) {
            if(replacement && replacement!=transport) {
                cargo_admit_recovery_board(ent,replacement,&destination);
            } else G_BotDispatchAIStop(ent);
        }
        return true;
    }
    if(msg!=A_COMMAND)return false;
    AbilityBattlestations_Command(call && call->client ? call->client : ent);
    return true;
}
static void AbilityBattlestations_Command(edict_t *clent) {
    edict_t *transport = G_GetMainSelectedUnit(clent->client);
    edict_t *candidates[MAX_CARGO];
    uint32_t capacity, free_slots, count;

    if (!transport || !S_CargoIsBurrow(transport)) return;
    if (!transport->cargo) transport->cargo = G_AllocCargo();
    assert(transport->cargo);
    capacity = S_CargoCapacity(transport);
    if (capacity <= transport->cargo->count) return;
    free_slots = MIN(capacity - transport->cargo->count, (uint32_t)MAX_CARGO);
    count = battlestations_collect(transport, candidates, free_slots);
    FOR_LOOP(i, count) S_CargoOrderBoard(candidates[i], transport);
    Get_Commands_f(clent);
}

/* Move603110 ->603d70 ->607950: selection reserves Adro's destination before
 * Board starts. Physical approaches remain owned by the ordinary Move scheduler. */
static edict_t *cargo_next_boarder(edict_t *transport);

/* Native Cargo100 owns ten stable reservation slots, independently of the
 * passenger's Board task and the carrier's current Load. Slot order breaks ties. */
static edict_t *cargo_boarder_at(edict_t *transport,uint32_t slot) {
    if(!transport->cargo)return NULL;
    edict_t *unit=transport->cargo->boarding[slot];
    if(unit && (!unit->inuse || G_IsDeferredFree(unit) ||
                unit->spawn_time!=transport->cargo->boarding_spawn[slot])) {
        transport->cargo->boarding[slot]=NULL;
        unit=NULL;
    }
    return unit;
}
static bool cargo_forget_boarder(edict_t *transport,edict_t *unit) {
    if(!transport || !transport->cargo)return false;
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS)if(transport->cargo->boarding[i]==unit) {
        transport->cargo->boarding[i]=NULL;
        transport->cargo->boarding_spawn[i]=0;
        return true;
    }
    return false;
}

static void cargo_ferry_board_leave(edict_t *unit) {
    edict_t *transport=unit->secondarygoal;
    /* Retire the reservation before callbacks can query this carrier again.
     * The primary goal may already belong to the replacing command. Loading
     * clears secondarygoal before leaving Board, so it keeps the Load effect. */
    S_SetMoveGoal(unit,&unit->secondarygoal,NULL);
    if(unit->cargo)unit->cargo->board_requested=unit->cargo->shore_valid=unit->cargo->shore_arrived=false;
    S_EndUnitTargetChase(unit,CAbilityBattlestations);
    bool reserved=cargo_forget_boarder(transport,unit);
    if(reserved && transport->inuse && !G_IsDeferredFree(transport) && !M_IsDead(transport) &&
       transport->currentmove==&cargo_ferry_load &&
       transport->goalentity==unit) {
        /* Native5a0800 retires this Load and its queued transients now, but
         * guards other reservations against Load's ordinary owner cleanup.
         * The surviving Board requests the refresh on its next think. */
        edict_t *next=cargo_next_boarder(transport);
        uint32_t spawn=transport->spawn_time;
        transport->cargo->board_withdrawal=next!=NULL;
        if(next)next->cargo->board_requested=false;
        order_stop_cleanup(transport);
        if(transport->inuse && transport->spawn_time==spawn && transport->cargo)
            transport->cargo->board_withdrawal=false;
    }
}

/* Pending space is a bounded carrier-local query, not a world/holder scan. */
static int32_t cargo_pending_board_space(edict_t *transport) {
    int32_t space=0;
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS) {
        edict_t *unit=cargo_boarder_at(transport,i);
        if(unit)space+=MAX(0,unit->data.UnitData->cargoSize);
    }
    return space;
}
static void cargo_ferry_load_leave(edict_t *unit) {
    if(unit->cargo)unit->cargo->ferry_pending=unit->cargo->load_failed=unit->cargo->load_point=unit->cargo->load_point_arrived=false;
    S_EndUnitTargetChase(unit,CAbilityCargoLoad);
    /* Removed Load leaves a physical approach without an ability subscriber;
     * its eventual completion must not retire Board (retail MapH). */
    if((unit->cargo && unit->cargo->board_withdrawal) ||
       !cargo_actor_ability_alias(unit,MAKEFOURCC('A','l','o','a')))return;
    /* Load owner replacement withdraws all pending passengers (5a08b0).
     * Clear the reservation before Board's leave hook can call us back. */
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS) {
        edict_t *passenger=cargo_boarder_at(unit,i);
        if(!passenger)continue;
        cargo_forget_boarder(unit,passenger);
        if(passenger->currentmove!=&cargo_ferry_board || passenger->secondarygoal!=unit)continue;
        S_SetMoveGoal(passenger,&passenger->secondarygoal,NULL);
        unit_stand(passenger);
    }
}

static void cargo_ferry_drop_leave(edict_t *unit) {
    if(unit->cargo)unit->cargo->ferry_pending=unit->cargo->ferry_arrived=false;
    S_EndUnitTargetChase(unit,CAbilityCargoDrop);
}

static void cargo_ferry_request_load(edict_t *,edict_t *,bool);

/* Reconsider pending passengers on completion, not on every mover tick.
 * Native5856a0 uses strict squared distance over retained reservations. */
static edict_t *cargo_next_boarder(edict_t *transport) {
    edict_t *best=NULL;
    float nearest=FLT_MAX;
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS) {
        edict_t *unit=cargo_boarder_at(transport,i);
        if(!unit)continue;
        if(!cargo_board_target_valid(unit,transport)) {
            /*5856a0 retires invalid reservations without cancelling Board. */
            cargo_forget_boarder(transport,unit);
            if(unit->secondarygoal==transport)S_SetMoveGoal(unit,&unit->secondarygoal,NULL);
            continue;
        }
        float dx=unit->s.origin2.x-transport->s.origin2.x;
        float dy=unit->s.origin2.y-transport->s.origin2.y;
        float squared=dx*dx+dy*dy;
        if(squared<nearest) {nearest=squared;best=unit;}
    }
    return best;
}

static float cargo_board_distance(edict_t *transport,edict_t *unit) {
    float dx=unit->s.origin2.x-transport->s.origin2.x;
    float dy=unit->s.origin2.y-transport->s.origin2.y;
    return dx*dx+dy*dy;
}
static bool cargo_reserve_boarder(edict_t *transport,edict_t *unit) {
    if(!transport->cargo)transport->cargo=G_AllocCargo();
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS)if(cargo_boarder_at(transport,i)==unit)return true;
    if(!cargo_board_target_valid(unit,transport))return false;
    uint32_t free=S_CargoCapacity(transport),needed=unit->data.UnitData->cargoSize;
    FOR_LOOP(i,transport->cargo->count)free-=transport->cargo->units[i]->data.UnitData->cargoSize;
    uint32_t pending=cargo_pending_board_space(transport);
    if(needed>free-pending) {
        uint32_t removable=0,request=unit->movement.previous_request_id;
        float distance=cargo_board_distance(transport,unit);
        FOR_LOOP(i,MAX_BOARDING_RESERVATIONS) {
            edict_t *other=cargo_boarder_at(transport,i);
            if(other && (other->movement.previous_request_id!=request ||
                         distance<=cargo_board_distance(transport,other)))
                removable+=other->data.UnitData->cargoSize;
        }
        /*5a3bbe/bc0 subtract BOTH quantities; unsigned JA is intentional. */
        if(needed>free-removable-pending)return false;
        do {
            edict_t *victim=NULL;bool different=false;float farthest=0;
            FOR_LOOP(i,MAX_BOARDING_RESERVATIONS) {
                edict_t *other=cargo_boarder_at(transport,i);
                if(!other)continue;
                bool mismatch=other->movement.previous_request_id!=request;
                float squared=cargo_board_distance(transport,other);
                if((!different || mismatch) && (different!=mismatch || squared>=farthest)) {
                    victim=other;different=mismatch;farthest=squared;
                }
            }
            if(!victim)return false;
            pending-=victim->data.UnitData->cargoSize;
            cargo_forget_boarder(transport,victim);
            /* Withdrawal(param3=0) never stops the carrier's current Load. */
            if(victim->currentmove==&cargo_ferry_board && victim->secondarygoal==transport) {
                S_SetMoveGoal(victim,&victim->secondarygoal,NULL);
                unit_stand(victim);
            }
        } while(needed>free-pending);
    }
    FOR_LOOP(i,MAX_BOARDING_RESERVATIONS)if(!cargo_boarder_at(transport,i)) {
        transport->cargo->boarding[i]=unit;
        transport->cargo->boarding_spawn[i]=unit->spawn_time;
        return true;
    }
    return false;
}

static void cargo_ferry_loaded(edict_t *transport) {
    if(transport->currentmove!=&cargo_ferry_load)return;
    edict_t *next=cargo_next_boarder(transport);
    if(next) {cargo_ferry_request_load(transport,next,true);return;}
    bool pending=transport->cargo->ferry_pending;
    vec2_t destination=transport->cargo->ferry_destination;
    unit_stand(transport);
    if(pending)G_IssueUnitPointOrder(transport,"unloadall",&destination,false,transport->s.player,0);
}

/* A removed Load no longer executes its effect, while the retained Move
 * task still reaches its captured endpoint (retail ORDER346 MapH). */
static bool cargo_ferry_has_load(edict_t *transport) {
    uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','l','o','a'));
    /* Player availability rejects new Load admission, but MapI confirms
     * an already-started Load still executes. Removal destroys that owner. */
    return alias!=0;
}

static void cargo_ferry_load_think(edict_t *transport) {
    edict_t *unit=transport->goalentity;
    if(!unit || !unit->inuse || !cargo_board_target_valid(unit,transport)) {
        unit_stand(transport);return;
    }
    if(!cargo_ferry_has_load(transport)) {
        if(!S_UnitTargetChaseActive(transport,CAbilityCargoLoad))unit_stand(transport);
        return;
    }
    /*603110 excludes AI Load from recovery after the physical member retires.
     * MapAN keeps this logical task dormant even when the target moves in range. */
    if(transport->cargo && transport->cargo->load_failed)return;
    if(transport->cargo->load_point) {
        if(transport->cargo->load_point_arrived)return;
        if(!S_UnitTargetChaseActive(transport,CAbilityCargoLoad)) {
            uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','l','o','a'));
            float range=alias ? MAX(0.f,G_AbilityLevel(alias,1)->range) : 0;
            if(!S_BeginUnitPointApproach(transport,&transport->cargo->load_destination,range,
                    CAbilityCargoLoad,S_CargoLoadComplete))unit_stand(transport);
        }
        return;
    }
    /* A synchronous Stop can retire the Load head before its outer task
     * starts. Keep that approach, but it no longer owns a loading effect. */
    if(transport->current_order_id==G_OrderId("load")) {
        if(S_CargoTryLoad(transport,unit))return;
    } else if(!S_UnitTargetChaseActive(transport,CAbilityCargoLoad) && cargo_target_in_range(transport,unit)) {
        unit_stand(transport);return;
    }
    if(!S_UnitTargetChaseActive(transport,CAbilityCargoLoad))
        if(!S_BeginUnitTargetChase(transport,unit,cargo_load_range(transport),CAbilityCargoLoad,S_CargoLoadComplete))
            unit_stand(transport);
}
void S_CargoLoadComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || unit->currentmove!=&cargo_ferry_load)return;
    if(!arrived && unit->current_order_id==G_OrderId("load") && (unit->aiflags&AI_TOWN_OWNED)) {
        unit->cargo->load_failed=true;
        return;
    }
    /*5fb6e0 lets active point-Load execute its arrival continuation even
     * after physical failure. It waits for Board; it does not load at a distance. */
    if(unit->cargo->load_point && unit->current_order_id==G_OrderId("load")) {
        unit->cargo->load_point_arrived=true;
        return;
    }
    if(!arrived || unit->current_order_id!=G_OrderId("load")) {unit_stand(unit);return;}
    cargo_ferry_load_think(unit);
}

static bool cargo_start_load(edict_t *transport,edict_t *unit,bool retain) {
    uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','l','o','a'));
    if(!alias || !G_IsUnitAbilityAvailable(transport,alias) || !cargo_board_target_valid(unit,transport))return false;
    if(!transport->cargo)transport->cargo=G_AllocCargo();
    bool pending=transport->cargo->ferry_pending;
    vec2_t destination=transport->cargo->ferry_destination;
    if(retain && transport->currentmove==&cargo_ferry_load)
        S_EndUnitTargetChase(transport,CAbilityCargoLoad);
    else order_stop_cleanup(transport);
    transport->cargo->ferry_pending=pending;
    transport->cargo->ferry_destination=destination;
    if(!cargo_reserve_boarder(transport,unit))return false;
    transport->cargo->load_failed=false;
    transport->cargo->load_point=cargo_naval_pair(unit,transport) && cargo_prepare_shore(unit,transport);
    transport->cargo->load_point_arrived=false;
    if(transport->cargo->load_point)transport->cargo->load_destination=unit->cargo->shore_water;
    S_SetMoveGoal(transport,&transport->goalentity,unit);
    unit_setmove(transport,&cargo_ferry_load);
    transport->current_order_id=G_OrderId("load");
    /* A point Load needs the passenger's Board continuation to finish the
     * shore transfer. Public Load can start without a preceding Board. Use
     * order dispatch so synchronous script replacement keeps its ownership. */
    if(transport->cargo->load_point &&
       (unit->currentmove!=&cargo_ferry_board || unit->secondarygoal!=transport))
        G_IssueUnitTargetOrder(unit,"board",transport,false,transport->s.player);
    return true;
}

bool S_CargoOrderLoad(edict_t *transport,edict_t *unit) {
    return cargo_start_load(transport,unit,false);
}

/* Board's internal relay publishes the head before Load admission. Public
 * JASS Load still validates first (ORDER346 MapJ versus MapK). */
static void cargo_ferry_request_load(edict_t *transport,edict_t *unit,bool retain) {
    uint32_t spawn=transport->spawn_time,target_spawn=unit->spawn_time;
    bool pending=transport->cargo && transport->cargo->ferry_pending;
    vec2_t destination=transport->cargo ? transport->cargo->ferry_destination : (vec2_t){0};
    /* Internal succession retains reservations (native Cargo800 guard).
     * Public Load replacement deliberately uses the ordinary leave path. */
    if(retain && transport->currentmove==&cargo_ferry_load)
        S_EndUnitTargetChase(transport,CAbilityCargoLoad);
    else order_stop_cleanup(transport);
    if(!transport->cargo)transport->cargo=G_AllocCargo();
    transport->cargo->ferry_pending=pending;
    transport->cargo->ferry_destination=destination;
    transport->current_order_id=G_OrderId("load");
    uint32_t revision=G_UnitMoveRevision(transport);
    G_PublishIssuedTargetOrder(transport,"load",unit,transport->s.player);
    if(!transport->inuse || transport->spawn_time!=spawn || G_IsDeferredFree(transport) || M_IsDead(transport) ||
       (G_UnitHasActiveOrder(transport) && G_UnitMoveRevision(transport)!=revision))return;
    if(!unit->inuse || unit->spawn_time!=target_spawn || G_IsDeferredFree(unit) || M_IsDead(unit)) {
        unit_stand(transport);return;
    }
    uint32_t head=transport->current_order_id;
    if(cargo_start_load(transport,unit,retain))transport->current_order_id=head;
    else unit_stand(transport);
}

/* Transient Load must re-notify even when its target has already boarded.
 * Generic public Load admission would reject before this event. */
static queuedOrderResult_t cargo_queued_load(edict_t *transport,unitOrder_t const *queued) {
    if(queued->owner_context!=CARGO_QUEUE_LOAD || queued->target_type!=UNIT_ORDER_TARGET_ENTITY)
        return QUEUED_ORDER_UNHANDLED;
    edict_t *target=queued->target_number<globals.num_edicts ? globals.edicts+queued->target_number : NULL;
    if(target && target->inuse && !G_IsDeferredFree(target) && target->spawn_time==queued->target_spawn_time)
        cargo_ferry_request_load(transport,target,true);
    else {
        uint32_t spawn=transport->spawn_time,revision=G_UnitMoveRevision(transport);
        transport->current_order_id=G_OrderId("load");
        G_PublishIssuedPointOrder(transport,G_OrderId("load"),&queued->point,queued->issuer_player,"load");
        if(transport->inuse && transport->spawn_time==spawn && !G_IsDeferredFree(transport) && !M_IsDead(transport) &&
           !(G_UnitHasActiveOrder(transport) && G_UnitMoveRevision(transport)!=revision))unit_stand(transport);
    }
    return QUEUED_ORDER_REPLACED;
}

static void cargo_queue_load(edict_t *transport,edict_t *target) {
    if(G_UnitQueuedOrderCount(transport)>=MAX_UNIT_ORDER_QUEUE)return;
    unitOrder_t queued={.order="load",.target_type=UNIT_ORDER_TARGET_ENTITY,
        /*59ec90 retains zero point words for missing-target replay. */
        .target_number=target->s.number,.target_spawn_time=target->spawn_time,
        .target_is_unit=true,.issuer_player=transport->s.player,.order_id=G_OrderId("load"),
        .owner_context=CARGO_QUEUE_LOAD};
    G_PrependUnitOrder(transport,&queued);
}

/*5ff750 owns the d017f continuation used by a carrier without Move.
 * A moving flyer instead owns loading through its accepted Aloa task. */
static void cargo_static_board_arrive(edict_t *unit) {
    edict_t *transport=unit->goalentity;
    if(!transport || !cargo_board_target_eligible(unit,transport)) {unit_stand(unit);return;}
    if(cargo_target_in_range(transport,unit)) {
        if(!S_CargoTryLoad(transport,unit))unit_stand(unit);
        return;
    }
    if(unit->cargo->board_retries++<5)unit->freetime=G_Time()+500;
    else unit_stand(unit);
}

static void cargo_ferry_board_think(edict_t *unit) {
    edict_t *transport=unit->goalentity;
    if(!transport || !transport->inuse || !cargo_board_target_eligible(unit,transport)) {
        unit_stand(unit);return;
    }
    if(unit->cargo->board_failed)return;
    if(unit->cargo->shore_valid) {
        if(!unit->cargo->board_requested) {
            unit->cargo->board_requested=true;
            if(transport->currentmove!=&cargo_ferry_load)cargo_ferry_request_load(transport,unit,false);
            if(unit->currentmove!=&cargo_ferry_board)return;
        }
        if(!unit->cargo->shore_arrived) {
            if(!S_UnitTargetChaseActive(unit,CAbilityBattlestations))
                if(!S_BeginUnitPointApproach(unit,&unit->cargo->shore_ground,500,CAbilityBattlestations,
                        S_CargoBoardComplete))unit_stand(unit);
            return;
        }
    }
    if(unit->cargo->board_static) {
        if(S_UnitTargetChaseActive(unit,CAbilityBattlestations) ||
           G_Time()<unit->freetime)return;
        if(cargo_target_in_range(transport,unit))cargo_static_board_arrive(unit);
        else if(!S_BeginUnitTargetChase(unit,transport,cargo_load_range(transport),
                    CAbilityBattlestations,S_CargoBoardComplete))unit_stand(unit);
        return;
    }
    if(!unit->cargo->board_requested) {
        unit->cargo->board_requested=true;
        if(transport->currentmove!=&cargo_ferry_load)
            cargo_ferry_request_load(transport,unit,false);
        else if(unit!=transport->goalentity && cargo_next_boarder(transport)==unit)
            cargo_queue_load(transport,unit);
        if(unit->currentmove!=&cargo_ferry_board)return;
    }
    /* The carrier's accepted Load owns the cargo effect. Board only moves
     * into range: raw loading here bypassed removed or rejected Load owners. */
    if(cargo_target_in_range(transport,unit))return;
    if(!S_UnitTargetChaseActive(unit,CAbilityBattlestations))
        if(!S_BeginUnitTargetChase(unit,transport,cargo_load_range(transport),CAbilityBattlestations,S_CargoBoardComplete))
            unit_stand(unit);
}
void S_CargoBoardComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || unit->currentmove!=&cargo_ferry_board)return;
    /*603110 excludes every AI Board task, including the initial naval
     * d016c shore approach. Failure must not advance to its next phase. */
    if(!arrived && (unit->aiflags&AI_TOWN_OWNED)) {
        unit->cargo->board_failed=true;return;
    }
    if(unit->cargo->shore_valid && !unit->cargo->shore_arrived) {
        unit->cargo->shore_arrived=true;
        cargo_ferry_board_think(unit);
        return;
    }
    if(unit->cargo->board_static) {
        cargo_static_board_arrive(unit);
        return;
    }
    if(!arrived) {unit_stand(unit);return;}
    cargo_ferry_board_think(unit);
}
static bool cargo_begin_ferry_board(edict_t *unit,edict_t *transport) {
    order_stop_cleanup(unit);
    bool stationary=!G_ActorHasAbilityCode(transport,MAKEFOURCC('A','m','o','v'));
    /*5a3820 admits a no-Move carrier without registering a Load reservation. */
    if(!stationary && !cargo_reserve_boarder(transport,unit)) {unit_stand(unit);return true;}
    if(!unit->cargo)unit->cargo=G_AllocCargo();
    bool naval=!stationary && cargo_naval_pair(unit,transport);
    unit->cargo->shore_valid=unit->cargo->shore_arrived=false;
    if(naval && !cargo_prepare_shore(unit,transport)) {unit_stand(unit);return true;}
    unit->cargo->board_static=stationary || naval;unit->cargo->board_failed=false;
    unit->cargo->board_retries=0;unit->freetime=0;
    unit->cargo->board_requested=false;
    S_SetMoveGoal(unit,&unit->goalentity,transport);
    S_SetMoveGoal(unit,&unit->secondarygoal,transport);
    unit_setmove(unit,&cargo_ferry_board);
    unit->current_order_id=G_OrderId("board");
    return true;
}

static void cargo_ferry_drop_think(edict_t *transport) {
    if(!transport->cargo || !transport->cargo->count || !transport->cargo->ferry_pending) {
        unit_stand(transport);return;
    }
    if(transport->cargo->ferry_arrived) {
        /* Removal retains the in-flight point task, but cannot execute Drop
         * when that task arrives; the passenger remains hidden in cargo. */
        uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','d','r','o'));
        if(!alias || !G_IsUnitAbilityAvailable(transport,alias)) {unit_stand(transport);return;}
        if(G_Time()<transport->freetime)return;
        S_CargoUnloadAt(transport,0);
        if(transport->currentmove!=&cargo_ferry_drop)return;
        if(!transport->cargo->count)unit_stand(transport);
        else transport->freetime=G_Time()+cargo_unload_interval_ms(transport);
        return;
    }
    if(!S_UnitTargetChaseActive(transport,CAbilityCargoDrop)) {
        uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','d','r','o'));
        if(!alias || !G_IsUnitAbilityAvailable(transport,alias) ||
           !S_BeginUnitPointApproach(transport,&transport->cargo->ferry_destination,
               G_AbilityLevel(alias,1)->range,CAbilityCargoDrop,S_CargoDropComplete))unit_stand(transport);
    }
}
void S_CargoDropComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || unit->currentmove!=&cargo_ferry_drop || !unit->cargo->ferry_pending)return;
    if(!arrived) {
        vec2_t destination=unit->cargo->ferry_destination;
        uint32_t spawn=unit->spawn_time;
        unit_stand(unit);
        /*603110 continues its AI failure dispatch after Drop's near/far
         * recovery. No unload effect is authorized by failed completion. */
        if(unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) &&
           (unit->aiflags&AI_TOWN_OWNED) && !S_CargoTryFailedMove(unit,&destination))
            G_BotDispatchAIStop(unit);
        return;
    }
    unit->cargo->ferry_arrived=true;unit->freetime=0;
    cargo_ferry_drop_think(unit);
}
bool S_CargoOrderDrop(edict_t *transport,vec2_t const *point) {
    uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','d','r','o'));
    if(!point || !alias || !G_IsUnitAbilityAvailable(transport,alias) ||
       !transport->cargo || !transport->cargo->count)return false;
    order_stop_cleanup(transport);
    transport->cargo->ferry_destination=*point;
    transport->cargo->ferry_pending=true;transport->cargo->ferry_arrived=false;
    S_SetMoveGoal(transport,&transport->goalentity,Waypoint_add(point));
    unit_setmove(transport,&cargo_ferry_drop);
    transport->current_order_id=G_OrderId("unloadall");
    return true;
}

static umove_t cargo_ferry_board={.animation="walk",.think=cargo_ferry_board_think,
    .proc=CAbilityBattlestations,.sample_pose=S_PublishMovement,.leave=cargo_ferry_board_leave};
static umove_t cargo_ferry_load={.animation="walk",.think=cargo_ferry_load_think,
    .proc=CAbilityCargoLoad,.sample_pose=S_PublishMovement,.leave=cargo_ferry_load_leave};
static umove_t cargo_ferry_drop={.animation="walk",.think=cargo_ferry_drop_think,
    .proc=CAbilityCargoDrop,.sample_pose=S_PublishMovement,.leave=cargo_ferry_drop_leave};

/* Native610710 stops at the first matching command, including a queued one.
 * A later compatible Drop cannot override an earlier incompatible destination. */
static bool cargo_drop_destination_matches(edict_t const *unit,vec2_t const *point) {
    uint32_t command=G_OrderId("unloadall");
    if(unit->current_order_id==command)
        return !unit->goalentity || (unit->goalentity->s.origin2.x==point->x && unit->goalentity->s.origin2.y==point->y);
    unitOrderQueue_t const *queue=&unit->order_queue;
    FOR_LOOP(i,queue->count) {
        unitOrder_t const *order=queue->entries+(queue->head+i)%queue->capacity;
        if((order->order_id ? order->order_id : G_OrderId(order->order))!=command)continue;
        return order->target_type!=UNIT_ORDER_TARGET_POINT || (order->point.x==point->x && order->point.y==point->y);
    }
    return true;
}

/* The target-loss callback runs before the retiring carrier leaves the native
 * ability registry. Normal failed-Move selection excludes deferred removals. */
static edict_t *cargo_select_ferry(edict_t *unit,vec2_t const *point,edict_t *retiring) {
    /*607950 returns an Adro source before capacity and candidate filtering. */
    if(cargo_actor_ability_alias(unit,MAKEFOURCC('A','d','r','o')))return unit;
    if((unit->unit_classification&WC3_UNIT_CLASS_SUMMONED) ||
       (unit->aiflags&AI_ILLUSION))return NULL;
    int32_t needed=unit->data.UnitData->cargoSize;
    if(needed<=0)return NULL;
    edict_t *best=NULL;float distance=FLT_MAX;
    box2_t bounds=CM_GetWorldBounds();float source[2];
    S_PredictUnitFinePointAt(unit,&level.pathing_clock,source);
    source[0]=wc3_world_coordinate(source[0],bounds.min.x,32);
    source[1]=wc3_world_coordinate(source[1],bounds.min.y,32);
    /* This terminal-failure query runs once, not on mover ticks. Sparse cargo
     * holder membership cannot enumerate empty, never-loaded transports. */
    FILTER_EDICTS(transport,transport!=unit && transport->inuse &&
                  (!G_IsDeferredFree(transport) || transport==retiring) &&
                  transport->s.player==unit->s.player && (transport->aiflags&AI_FLYING) && !M_IsDead(transport)) {
        uint32_t alias=cargo_actor_ability_alias(transport,MAKEFOURCC('A','d','r','o'));
        if(!alias || !G_IsUnitAbilityAvailable(transport,alias) ||
           transport->health.max_value>transport->health.value)continue;
        int32_t used=0;
        if(transport->cargo)FOR_LOOP(i,transport->cargo->count)
            used+=MAX(0,transport->cargo->units[i]->data.UnitData->cargoSize);
        if(needed>(int32_t)S_CargoCapacity(transport)-used-cargo_pending_board_space(transport))continue;
        if(!cargo_drop_destination_matches(transport,point))continue;
        float position[2];S_PredictUnitFinePointAt(transport,&level.pathing_clock,position);
        float dx=wc3_sub(wc3_world_coordinate(position[0],bounds.min.x,32),source[0]);
        float dy=wc3_sub(wc3_world_coordinate(position[1],bounds.min.y,32),source[1]);
        float dz=wc3_sub(transport->s.origin.z,unit->s.origin.z);
        float squared=wc3_add(wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy)),wc3_mul(dz,dz));
        if(squared<distance) {best=transport;distance=squared;}
    }
    return best;
}

bool S_CargoTryFailedMove(edict_t *unit,vec2_t const *point) {
    if(!unit || !point || S_UnitPointInMoveRange(unit,point,300))return false;
    vec2_t destination=*point;
    pathAccelParams_t placement={point,NULL,S_MoveCollisionRadius(unit),M_UnitStaticPathingFlags(unit)};
    if(!G_ClosestMovePathPoint(&placement,&destination) || S_UnitPointInMoveRange(unit,&destination,300))return false;
    edict_t *best=cargo_select_ferry(unit,&destination,NULL);
    return cargo_admit_recovery_board(unit,best,&destination);
}

static bool cargo_admit_recovery_board(edict_t *unit,edict_t *transport,vec2_t const *point) {
    if(!transport)return false;
    if(transport==unit) {
        /* The internal AI head is published before self-Board fails admission.
         * Public user Board remains subject to ordinary target validation. */
        uint32_t spawn=unit->spawn_time,revision=G_UnitMoveRevision(unit);
        unit->current_order_id=G_OrderId("board");
        G_PublishIssuedTargetOrder(unit,"board",unit,unit->s.player);
        if(unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) &&
           G_UnitMoveRevision(unit)==revision && unit->current_order_id==G_OrderId("board"))
            unit_stand(unit);
        return true;
    }
    if(!cargo_board_target_valid(unit,transport))return false;
    if(!transport->cargo)transport->cargo=G_AllocCargo();
    transport->cargo->ferry_destination=*point;transport->cargo->ferry_pending=true;
    return G_IssueUnitTargetOrder(unit,"board",transport,false,unit->s.player);
}

/* ---- Drop (Adro): drop cargo at a point --------------------------------- */

static bool drop_selectlocation(edict_t *clent, vec2_t const *point) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);

    if (!caster || !caster->cargo || caster->cargo->count == 0 || !point) return false;
    if (clent->client->menu.ability_code == BZ_AMED) {
        bool dropped = false;
        while (caster->cargo->count > 0 &&
               S_CorpseCargoIsStored(S_CargoUnitAt(caster, caster->cargo->count - 1)))
            dropped |= S_CargoUnloadAt(caster, caster->cargo->count - 1);
        return dropped;
    }
    if(clent->client->menu.ability_code==cargo_actor_ability_alias(caster,MAKEFOURCC('A','d','r','o'))) {
        vec2_t destination=*point;
        if(!CM_ClosestPathablePointForRadiusFlags(point,caster->collision,
                M_UnitStaticPathingFlags(caster),&destination))return false;
        return G_IssueUnitPointOrder(caster,"unloadall",&destination,
            clent->client->menu.order_queued,clent->client->ps.number,0);
    }
    return cargo_begin_unload_at(caster, point, clent->client->menu.ability_code);
}

static void drop_command(edict_t *clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = drop_selectlocation;
}

BZ_ABILITY_PROC(CAbilityCargoDrop) {
    if(msg==A_POINT_ORDER_ADMIT || msg==A_POINT_ORDER) {
        if(!call || !call->point_order.order || strcmp(call->point_order.order,"unloadall"))return ABILITY_ORDER_UNHANDLED;
        uint32_t alias=cargo_actor_ability_alias(ent,MAKEFOURCC('A','d','r','o'));
        if(!ent || !alias || !G_IsUnitAbilityAvailable(ent,alias) || !ent->cargo || !ent->cargo->count)
            return ABILITY_ORDER_REJECTED;
        return msg==A_POINT_ORDER_ADMIT || S_CargoOrderDrop(ent,call->point_order.point) ?
            ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
    }
    if (msg == A_MOVE_ARRIVE && cargo_unload_move_arrive(ent, call)) return true;
    if (msg == A_DISABLE && ent && call && call->item && ent->movement.cargo_unload_pending && call->item->code == ent->movement.cargo_unload_ability) {
        cargo_clear_pending_unload(ent);
        return true;
    }
    if (msg == A_MOVE_LEAVE && ent && ent->movement.cargo_unload_pending) {
        cargo_clear_pending_unload(ent);
        return true;
    }
    if (msg != A_COMMAND) return 0;
    drop_command(call && call->client ? call->client : ent);
    return true;
}

/* ---- Drop Instant (Adri): unload every occupant immediately ------------- */
BZ_COMMAND_PROC(AbilityCargoDropInstant) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !caster->cargo || caster->cargo->count == 0) return;
    /* Retire timed unloading before a new passenger can board this frame. */
    order_stop_cleanup(caster);
    cargo_drop_all(caster);
    Get_Commands_f(clent);
}

/* ---- Stand Down (Astd): stop combat, then unload all Burrow occupants --- */
void S_CargoStandDown(edict_t *caster) {
    if (!caster || !S_CargoIsBurrow(caster)) return;

    /* Stand Down is also the authoritative exit from the occupied Burrow
     * combat state.  Disable the live attack/order before cargo removal so an
     * in-progress repeating attack cannot keep driving after the Peons leave.
     * Reuse normal Stop semantics so attack-move/patrol/follow state and queued
     * orders are retired consistently with the command-card Stop button. */
    order_stop_cleanup(caster);
    S_SetMoveGoal(caster, &caster->goalentity, NULL);
    cargo_drop_all(caster);
}

BZ_COMMAND_PROC(AbilityStandDown) {
    edict_t *caster = G_GetMainSelectedUnit(clent->client);
    if (!caster || !S_CargoIsBurrow(caster)) return;
    S_CargoStandDown(caster);
    Get_Commands_f(clent);
}
