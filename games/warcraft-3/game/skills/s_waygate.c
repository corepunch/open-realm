#include "s_skills.h"
#include "../g_entity_set.h"
#include "../../common/wc3_pathing_gate.h"
#include "../../common/wc3_math.h"
#include "../../common/wc3_pathing_adaptive.h"
#include "../../common/wc3_pathing_coordinates.h"

#define BZ_AWRP MAKEFOURCC('A','w','r','p')
#define BZ_AMOV MAKEFOURCC('A','m','o','v')

/* Derived ownership in edict order. Rebuild once after map/save replacement;
 * queries still read live ability eligibility, activation and destinations. */
static entitySet_t waygate_members;
static bool waygate_members_valid;
#ifdef BZ_TESTS
static uint32_t waygate_edge_visits;
#endif

void S_ResetWaygateCache(void) {
    waygate_members=(entitySet_t){0};
    waygate_members_valid=false;
}

static void waygate_prepare_members(void) {
    if(waygate_members_valid)return;
    FOR_LOOP(i,globals.num_edicts) {
#ifdef BZ_TESTS
        waygate_edge_visits++;
#endif
        edict_t const *ent=g_edicts+i;
        if(ent->inuse && ent->waygate && ent->waygate->edge_id)
            entity_set_put(&waygate_members,i,true);
    }
    waygate_members_valid=true;
}

static void waygate_track(edict_t const *gate,bool present) {
    uintptr_t base=(uintptr_t)g_edicts, address=(uintptr_t)gate;
    if(address<base || address-base>=sizeof(*gate)*globals.num_edicts ||
        (address-base)%sizeof(*gate))return;
    entity_set_put(&waygate_members,(address-base)/sizeof(*gate),present);
}

static uint32_t waygate_next_member(uint32_t from) {
    for(uint32_t i=entity_set_next(&waygate_members,from);i<globals.num_edicts;
        i=entity_set_next(&waygate_members,i+1)) {
#ifdef BZ_TESTS
        waygate_edge_visits++;
#endif
        edict_t const *ent=g_edicts+i;
        if(ent->inuse && ent->waygate && ent->waygate->edge_id)return i;
        entity_set_put(&waygate_members,i,false);
    }
    return globals.num_edicts;
}

/* Way Gate is authored as a passive ability. Resolve aliases rather than
 * hard-coding Awrp so map object-data copies retain their DataA/DataB entry
 * rectangle and normal JASS Waygate* behavior. */
static uint32_t waygate_actor_ability_alias(edict_t const *gate) {
    char alias_name[5] = {0};

    if (!gate) return 0;
    uint32_t count;
    unitAbilityToken_t const *tokens = G_UnitAbilityTokens(gate->data.UnitAbilities, &count);
    FOR_LOOP(i, count)
        if (tokens[i].length == 4 && tokens[i].base == BZ_AWRP &&
            G_ActorHasAbilityCode(gate, tokens[i].code)) return tokens[i].code;
    FOR_LOOP(i, ARRAY_COUNT(gate->abilities.added)) {
        uint32_t const alias = gate->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        if (G_AbilityCode(alias) == BZ_AWRP && G_ActorHasSkill(gate, alias_name))
            return alias;
    }
    return 0;
}

static bool waygate_dimensions(edict_t const *gate, float *width, float *height) {
    uint32_t const alias = waygate_actor_ability_alias(gate);
    abilityLevel_t const *row;

    if (!alias || !width || !height) return false;
    row = G_AbilityLevel(alias, MAX(1u, G_UnitAbilityLevel(gate, alias)));
    if (!row) return false;
    *width = MAX(0.0f, row->data[0].number);  /* Wrp1 / DataA */
    *height = MAX(0.0f, row->data[1].number); /* Wrp2 / DataB */
    return *width > 0.0f && *height > 0.0f;
}

static void waygate_publish_source(edict_t const *gate,uint8_t id) {
    vec2_t half=gate->waygate->source_half;
    box2_t rectangle={{wc3_sub(gate->s.origin2.x,half.x),wc3_sub(gate->s.origin2.y,half.y)},
        {wc3_add(gate->s.origin2.x,half.x),wc3_add(gate->s.origin2.y,half.y)}};
    G_PublishWaygateSource(&rectangle,id);
}

static void waygate_release(edict_t *gate) {
    if(!gate->waygate)return;
    if(gate->waygate->edge_id)waygate_publish_source(gate,0);
    G_FreeWaygate(gate);
    waygate_track(gate,false);
}

/* The saved ability state owns allocation; reconstructing availability avoids
 * a second pool whose restore/free lifetime could disagree with the edicts. */
static void waygate_initialize(edict_t *gate) {
    uint8_t used[BZ_WC3_GATE_RECORDS]={0};
    waygate_prepare_members();
    if (!gate->waygate) gate->waygate=G_AllocWaygate();
    if (gate->waygate->initialized) {
        waygate_track(gate,gate->inuse && gate->waygate->edge_id);
        return;
    }
    for(uint32_t i=waygate_next_member(0);i<globals.num_edicts;i=waygate_next_member(i+1)) {
        edict_t const *ent=g_edicts+i;
        if(ent->inuse && ent->waygate && ent->waygate->initialized && ent->waygate->edge_id)
            used[ent->waygate->edge_id]=1;
    }
    gate->waygate->edge_id=(uint8_t)wc3_gate_allocate(used);
    gate->waygate->initialized=true;
    waygate_track(gate,gate->inuse && gate->waygate->edge_id);
    float width=0,height=0;
    if(!waygate_dimensions(gate,&width,&height))
        fprintf(stderr,"WC3 Waygate: nonpositive authored source dimensions unit=%u\n",gate->s.number);
    gate->waygate->source_half=(vec2_t){wc3_mul(width,.5f),wc3_mul(height,.5f)};
    /* Original setup publishes zero too: exhausted creation can erase overlap. */
    waygate_publish_source(gate,gate->waygate->edge_id);
    if(!gate->waygate->edge_id)
        fprintf(stderr,"WC3 Waygate: native1..255 edge pool exhausted for unit %u\n",gate->s.number);
}

bool S_ValidateWaygateIds(void) {
    uint8_t used[BZ_WC3_GATE_RECORDS]={0};
    FOR_LOOP(i,globals.num_edicts) {
        waygate_t const *gate=g_edicts[i].waygate;
        if(!g_edicts[i].inuse || !gate)continue;
        if(!gate->initialized || (!gate->edge_id && (gate->active || gate->destination_set)) ||
            (gate->edge_id && used[gate->edge_id])) {
            fprintf(stderr,"WC3 Waygate: invalid saved edge ownership unit=%u id=%u\n",i,gate->edge_id);
            return false;
        }
        if(gate->edge_id)used[gate->edge_id]=1;
    }
    return true;
}

bool S_WaygateIsGate(edict_t const *gate) {
    return gate && gate->inuse && !G_IsDeferredFree(gate) && waygate_actor_ability_alias(gate) != 0;
}

bool S_WaygateIsActive(edict_t const *gate) {
    return S_WaygateIsGate(gate) && gate->waygate && gate->waygate->edge_id && gate->waygate->active;
}

/* Ability-owned records are reconstructed for queries; retained routes keep
 * their own exit words through destination mutation and ID reuse. */
void S_WaygateBuildEdges(wc3AccGate_t *edges) {
    waygate_prepare_members();
    memset(edges,0,BZ_WC3_GATE_RECORDS*sizeof(*edges));
    box2_t bounds=CM_GetWorldBounds();
    for(uint32_t i=waygate_next_member(0);i<globals.num_edicts;i=waygate_next_member(i+1)) {
        edict_t const *ent=g_edicts+i;
        waygate_t const *gate=ent->waygate;
        wc3AccGate_t *record=edges+gate->edge_id;
        record->active=S_WaygateIsActive(ent);
        float x=wc3_mul(wc3_grid_coordinate(gate->destination.x,bounds.min.x,32),.5f);
        float y=wc3_mul(wc3_grid_coordinate(gate->destination.y,bounds.min.y,32),.5f);
        record->destination=(wc3FinePoint_t){(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(x))),
            (int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(y)))};
    }
}

bool S_WaygateEdgeIsActive(uint8_t id) {
    if(!id)return false;
    waygate_prepare_members();
    for(uint32_t i=waygate_next_member(0);i<globals.num_edicts;i=waygate_next_member(i+1)) {
        edict_t const *ent=g_edicts+i;
        if(ent->waygate->edge_id==id)return S_WaygateIsActive(ent);
    }
    return false;
}

bool S_WaygateGetDestination(edict_t const *gate, vec2_t *destination) {
    if (!S_WaygateIsGate(gate) || !destination) return false;
    if (!gate->waygate || !gate->waygate->edge_id) { *destination = (vec2_t){0}; return false; }
    *destination = gate->waygate->destination;
    return gate->waygate->destination_set;
}

void S_WaygateSetDestination(edict_t *gate, vec2_t const *destination) {
    if (!S_WaygateIsGate(gate) || !destination) return;
    waygate_initialize(gate);
    if(!gate->waygate->edge_id)return;
    gate->waygate->destination = *destination;
    gate->waygate->destination_set = true;
}

void S_WaygateSetActive(edict_t *gate, bool active) {
    if (!S_WaygateIsGate(gate)) return;
    waygate_initialize(gate);
    gate->waygate->active = active && gate->waygate->edge_id;
    /* Original43b840 updates alternate even when map bridge rejects edge0. */
    G_AddUnitAnimationProperties(gate, "alternate", active);
}

static bool waygate_behavior_active(edict_t const *unit) {
    return unit && (unit->movement.waygate_target || unit->movement.waygate_goal ||
                    unit->movement.waygate_target_spawn_time);
}

/* CAbilityWarp owns only its pointers. In particular, secondarygoal is shared
 * by unrelated movement behaviors and must never be cleared by Way Gate exit. */
static void waygate_clear_order(edict_t *unit) {
    if (!unit) return;
    if (unit->goalentity == unit->movement.waygate_goal)
        S_SetMoveGoal(unit, &unit->goalentity, NULL);
    unit->movement.waygate_target = NULL;
    S_SetMoveGoal(unit, &unit->movement.waygate_goal, NULL);
    unit->movement.waygate_target_spawn_time = 0;
    move_reset_progress(unit);
}

BZ_ABILITY_PROC(CAbilityWarp) {
    switch (msg) {
        case A_UNIT_TYPE_INIT: {
            if (ent || !call) return UNIT_INIT_UNKNOWN;
            uint32_t count;
            unitAbilityToken_t const *tokens = G_UnitAbilityTokens(call->unit_type, &count);
            FOR_LOOP(i, count) if (tokens[i].length == 4 && tokens[i].base == BZ_AWRP) return UNIT_INIT_RUN;
            return UNIT_INIT_SKIP_FALSE;
        }
        case A_UNIT_EVENT_MASK:
            return UNIT_MESSAGE_SUBSCRIPTIONS(A_ENABLE, A_UNIT_INIT, A_DISABLE, A_UNIT_REMOVING,
                A_TARGET_ORDER, A_MOVE_LEAVE, A_ORDER_ACCEPTED, A_UNIT_REMOVE);
        case A_ENABLE:
        case A_UNIT_INIT:
            if(!S_WaygateIsGate(ent))return false;
            waygate_initialize(ent);
            return true;
        case A_DISABLE:
            if(!ent)return false;
            if(waygate_behavior_active(ent))waygate_clear_order(ent);
            waygate_release(ent);
            return true;
        case A_UNIT_REMOVING:
            if(!ent)return false;
            if(waygate_behavior_active(ent))waygate_clear_order(ent);
            /* Original RemoveUnit returns with its allocation retained. Same
             * callback replacement must allocate before deferred cleanup. */
            return true;
        case A_TARGET_ORDER:
            return call && call->target_order.issuer && call->target_order.order &&
                   !strcmp(call->target_order.order, "smart") &&
                   S_IssueWaygateMoveOrder(call->target_order.issuer, ent,
                       &ent->s.origin2, G_OrderId(call->target_order.order));
        case A_MOVE_LEAVE:
            if (!waygate_behavior_active(ent)) return false;
            waygate_clear_order(ent);
            return true;
        case A_ORDER_ACCEPTED:
            if (!waygate_behavior_active(ent)) return false;
            /* Reserved serialized fields never own a physical task now. */
            waygate_clear_order(ent);
            return true;
        case A_UNIT_REMOVE:
            if(!ent)return false;
            if(waygate_behavior_active(ent))waygate_clear_order(ent);
            waygate_release(ent);
            return true;
        default:
            return false;
    }
}
