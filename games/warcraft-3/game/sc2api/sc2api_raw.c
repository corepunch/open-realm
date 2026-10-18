#ifdef WC3_SC2API

#include "sc2api_raw.h"
#include "sc2api_map.h"

/* Retain only public, immutable-looking snapshot fields. Current health,
 * ownership, type changes and unseen deaths must not update remembered state.
 * Records are keyed by opaque incarnation, so reusing an edict slot cannot
 * delete an unrelated fog snapshot. The bounded table exists only for viewers
 * that have actually observed buildings/destructibles. */
typedef struct {
    uint64_t tag;
    uint32_t number, spawn_time, unit_type;
    int32_t owner;
    wc3Sc2Alliance_t alliance;
    vec3_t pos;
    vec2_t world;
    float facing, radius;
    bool present;
} sc2FogMemory_t;

static struct {
    sc2FogMemory_t *entries;
    uint32_t count, capacity;
} sc2_fog_memory[MAX_PLAYERS];

void WC3_SC2API_ResetFogMemory(void) {
    FOR_LOOP(i, MAX_PLAYERS) {
        SAFE_DELETE(sc2_fog_memory[i].entries, gi.MemFree);
        sc2_fog_memory[i].count = sc2_fog_memory[i].capacity = 0;
    }
}

uint32_t WC3_SC2API_FogMemoryCount(uint32_t player) {
    return player < MAX_PLAYERS ? sc2_fog_memory[player].count : 0;
}

static void sc2api_snapshot(sc2FogMemory_t const *memory, wc3Sc2RawUnit_t *out) {
    memset(out, 0, sizeof(*out));
    out->display_type = WC3_SC2_DISPLAY_SNAPSHOT;
    out->alliance = memory->alliance;
    out->tag = memory->tag;
    out->unit_type = memory->unit_type;
    out->owner = memory->owner;
    out->pos = memory->pos;
    out->facing = memory->facing;
    out->radius = memory->radius;
    out->cloak = WC3_SC2_CLOAK_UNKNOWN;
}

static sc2FogMemory_t *sc2api_remembered_entity(uint32_t player, edict_t const *ent) {
    if (player >= MAX_PLAYERS || !ent) return NULL;
    for (uint32_t i = sc2_fog_memory[player].count; i; i--) {
        sc2FogMemory_t *memory = sc2_fog_memory[player].entries + i - 1;
        if (memory->tag && memory->number == ent->s.number && memory->spawn_time == ent->spawn_time)
            return memory;
    }
    return NULL;
}

static void sc2api_remember(uint32_t player, edict_t const *ent, wc3Sc2RawUnit_t const *unit) {
    sc2FogMemory_t *memory = NULL;
    uint32_t empty = UINT32_MAX;
    FOR_LOOP(i, sc2_fog_memory[player].count) {
        sc2FogMemory_t *candidate = sc2_fog_memory[player].entries + i;
        if (candidate->tag == unit->tag) { memory = candidate; break; }
        if (!candidate->tag) empty = i;
    }
    if (!memory) {
        if (empty != UINT32_MAX) memory = sc2_fog_memory[player].entries + empty;
        else {
            uint32_t const count = sc2_fog_memory[player].count;
            if (count >= MAX_ENTITIES) return;
            if (count == sc2_fog_memory[player].capacity) {
                uint32_t capacity = MIN(MAX_ENTITIES, MAX(32u, count * 2));
                sc2FogMemory_t *entries = gi.MemAlloc(sizeof(*entries) * capacity);
                if (!entries) return;
                if (count) memcpy(entries, sc2_fog_memory[player].entries, sizeof(*entries) * count);
                SAFE_DELETE(sc2_fog_memory[player].entries, gi.MemFree);
                sc2_fog_memory[player].entries = entries;
                sc2_fog_memory[player].capacity = capacity;
            }
            memory = sc2_fog_memory[player].entries + sc2_fog_memory[player].count++;
        }
    }
    * memory = (sc2FogMemory_t){ .tag = unit->tag, .number = ent->s.number,
        .spawn_time = ent->spawn_time, .unit_type = unit->unit_type, .owner = unit->owner,
        .alliance = unit->alliance, .pos = unit->pos, .world = ent->s.origin2,
        .facing = unit->facing, .radius = unit->radius, .present = true };
}

static wc3Sc2Alliance_t sc2api_alliance(uint32_t player, edict_t const *ent) {
    uint32_t owner;

    if (!ent) return WC3_SC2_ALLIANCE_ENEMY;
    owner = ent->s.player;
    if (owner == player) return WC3_SC2_ALLIANCE_SELF;
    if (owner == PLAYER_NEUTRAL_PASSIVE) return WC3_SC2_ALLIANCE_NEUTRAL;
    if (player < MAX_PLAYERS && owner < MAX_PLAYERS && G_PlayerTreatsPlayerAsAlly(player, owner)) {
        return WC3_SC2_ALLIANCE_ALLY;
    }
    return WC3_SC2_ALLIANCE_ENEMY;
}

static wc3Sc2CloakState_t sc2api_cloak_state(uint32_t player, edict_t const *ent,
                                              wc3Sc2Alliance_t alliance,
                                              bool visible) {
    if (!visible) return WC3_SC2_CLOAK_UNKNOWN;
    if (!S_UnitUsesInvisibilityRenderFlag(ent)) return WC3_SC2_NOT_CLOAKED;
    if (alliance == WC3_SC2_ALLIANCE_SELF || alliance == WC3_SC2_ALLIANCE_ALLY) {
        return WC3_SC2_CLOAKED_ALLIED;
    }
    if (!S_UnitIsInvisibleToPlayer(ent, player)) return WC3_SC2_CLOAKED_DETECTED;
    return WC3_SC2_CLOAKED;
}


static uint32_t sc2api_weapon_upgrade(edict_t const *ent) {
    static cstring_t const classes[] = { "melee", "ranged", "artillery" };
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0])) {
        uint32_t const upgrade = G_GetUnitUpgradeForClass(ent, classes[i]);
        if (upgrade) return upgrade;
    }
    return 0;
}

static uint32_t sc2api_ability_code_for_order(uint32_t order_id) {
    if (G_AbilityData(order_id)->id == order_id && S_AbilityItem(order_id).ability) return order_id;
    FOR_LOOP(i, G_OrderDefinitionCount()) {
        uint32_t id = 0, ability_code = 0;
        if (G_OrderDefinitionInfo(i, &id, NULL, &ability_code) && id == order_id) return ability_code;
    }
    return 0;
}

static float sc2api_build_progress(edict_t const *ent) {
    float duration;

    if (!ent || !ent->construction || !ent->data.UnitBalance ||
        ent->data.UnitBalance->buildTime <= 0) {
        return 1.0f;
    }
    duration = ent->construction->duration_ms > 0.0f ? ent->construction->duration_ms : (float)ent->data.UnitBalance->buildTime * 1000.0f;
    return MAX(0.0f, MIN(1.0f, ent->construction->progress / duration));
}

static bool sc2api_append_order(wc3Sc2RawUnit_t *out, unitOrder_t const *order) {
    uint32_t ability_id;
    uint32_t index;

    if (!out || !order || !order->order[0] || out->order_count >= MAX_UNIT_ORDER_QUEUE + 1) return false;
    ability_id = order->target_type == UNIT_ORDER_TARGET_BUILD ? order->order_id : G_OrderId(order->order);
    if (!ability_id) return false;
    index = out->order_count;
    out->orders[index].ability_id = ability_id;
    if (order->target_type == UNIT_ORDER_TARGET_POINT || order->target_type == UNIT_ORDER_TARGET_BUILD) {
        out->orders[index].has_point = true;
        out->orders[index].point = WC3_SC2API_WorldToBoardPoint(&order->point);
    } else if (order->target_type == UNIT_ORDER_TARGET_ENTITY) {
        edict_t *target;
        if (order->target_number >= globals.num_edicts) return false;
        target = globals.edicts + order->target_number;
        if (!target->inuse || target->spawn_time != order->target_spawn_time || M_IsDead(target)) return false;
        out->orders[index].has_target_unit = true;
        out->orders[index].target_unit_tag = WC3_SC2API_UnitTag(target);
    }
    out->order_count++;
    return true;
}

bool WC3_SC2API_FillRawUnit(uint32_t player, edict_t const *ent, wc3Sc2RawUnit_t *out) {
    entityState_t state;
    bool visible;
    uint32_t cargo_capacity;
    gameClient_t *owner_client;
    bool const destructable = ent && G_IsDestructable(ent);

    if (!out || !ent || !WC3_SC2API_PlayerClient(player)) return false;
    visible = ent->inuse && G_FowPlayerCanHoverEntity(player, ent);
    bool const alive = ent->inuse && ((ent->svflags & SVF_MONSTER) || destructable) &&
        !(ent->svflags & SVF_DEADMONSTER) && !ent->training && ent->health.value > 0.0f;
    sc2FogMemory_t *memory = sc2api_remembered_entity(player, ent);
    if (memory && (!alive || !visible)) {
        if ((visible && ent->inuse) || G_FowPointIsVisible(player, &memory->world)) memory->tag = 0;
        else {
            sc2api_snapshot(memory, out);
            memory->present = true;
            return true;
        }
    }
    if (!alive || !visible || !G_FowPlayerCanSeeEntity(player, ent)) return false;

    state = ent->s;
    if (globals.CustomizeEntity) globals.CustomizeEntity(player, ent, &state);
    if (state.renderfx & RF_HIDDEN) return false;

    visible = G_FowPlayerCanHoverEntity(player, ent);
    memset(out, 0, sizeof(*out));
    out->display_type = visible ? WC3_SC2_DISPLAY_VISIBLE : WC3_SC2_DISPLAY_SNAPSHOT;
    out->alliance = destructable ? WC3_SC2_ALLIANCE_NEUTRAL : sc2api_alliance(player, ent);
    out->tag = WC3_SC2API_UnitTag(ent);
    out->unit_type = destructable ? WC3_SC2API_DestructableTypeId(ent->class_id) : ent->class_id;
    out->owner = destructable ? 0 : (ent->s.player < MAX_PLAYERS ? (int32_t)ent->s.player + 1 : 0);
    {
        vec2_t const board = WC3_SC2API_WorldToBoardPoint(&state.origin2);
        out->pos = (vec3_t){ board.x, board.y, WC3_SC2API_WorldToBoardHeight(state.origin.z) };
    }
    out->facing = state.angle;
    out->radius = WC3_SC2API_WorldToBoardDistance(ent->collision > 0.0f ? ent->collision : state.radius);
    if (visible) {
        out->has_build_progress = true;
        out->build_progress = destructable ? 1.0f : sc2api_build_progress(ent);
    }
    out->cloak = destructable ? (visible ? WC3_SC2_NOT_CLOAKED : WC3_SC2_CLOAK_UNKNOWN)
                              : sc2api_cloak_state(player, ent, out->alliance, visible);
    out->is_selected = (ent->selected & (1u << player)) != 0;
    if (destructable || G_UnitIsStructure(ent)) sc2api_remember(player, ent, out);

    /*Blizzard raw.proto does not populate live combat/economy fields for a
     * Snapshot. Redact them rather than leaking current fogged building state. */
    if (!visible) return true;

    out->has_live_data = true;
    out->health = ent->health.value;
    out->health_max = ent->health.max_value;
    if (destructable) return true;
    out->energy = ent->mana.value;
    out->energy_max = ent->mana.max_value;
    if ((state.flags & EF_RESOURCE_SOURCE) && ent->resources != UINT_MAX) {
        out->vespene_contents = (int32_t)MIN(ent->resources, 0x7fffffffu);
    }
    out->is_flying = (ent->aiflags & AI_FLYING) != 0;
    out->is_active = (state.flags & EF_BUILDING) &&
        (ent->build != NULL || G_BuildingUpgradeActive((edict_t *)ent));

    owner_client = ent->s.player < MAX_PLAYERS ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    if (owner_client && out->alliance != WC3_SC2_ALLIANCE_ENEMY) {
        uint32_t const weapon_upgrade = sc2api_weapon_upgrade(ent);
        uint32_t const armor_upgrade = G_GetUnitUpgradeForClass(ent, "armor");
        if (weapon_upgrade) {
            out->has_attack_upgrade_level = true;
            out->attack_upgrade_level = G_GetPlayerTechResearchedLevel(owner_client, weapon_upgrade);
        }
        if (armor_upgrade) {
            out->has_armor_upgrade_level = true;
            out->armor_upgrade_level = G_GetPlayerTechResearchedLevel(owner_client, armor_upgrade);
        }
    }

    /*Resolve the same authored WC3 buff rawcode used by the normal status
     * panel, then expose that stable uint32 id as SC2 Raw Unit.buff_ids. Only
     * publish live visible state; snapshots return before this point. */
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        uint32_t const buff = G_UnitStatusBuffCode(&ent->abilstatus[i]);
        bool duplicate = false;

        if (!buff ||
            (ent->abilstatus[i].timestamp && ent->abilstatus[i].timestamp <= G_Time()) ||
            out->buff_count >= MAX_UNIT_STATUSES) continue;
        FOR_LOOP(j, out->buff_count) if (out->buff_ids[j] == buff) { duplicate = true; break; }
        if (!duplicate) out->buff_ids[out->buff_count++] = buff;
    }

    /*raw.proto marks orders/passenger/cargo state as unavailable for enemy
     * units. Friendly transport contents are authoritative and already bounded
     * by MAX_CARGO, so expose the same PassengerUnit fields SC2 provides. */
    if (out->alliance != WC3_SC2_ALLIANCE_ENEMY) {
        vec2_t rally_point;
        edict_t *rally_entity = NULL;
        rallyTargetType_t rally_type = RALLY_TARGET_NONE;
        /* Default SELF is omitted. An unseen target's automatic death/reset
         * therefore cannot change the observation from a hidden entity rally
         * to a newly exposed default. Never invoke the mutating resolver for
         * an entity whose current state this player cannot observe. */
        if (ent->rally && ent->rally->type == RALLY_TARGET_POINT)
            rally_type = G_ResolveRallyTarget((edict_t *)ent, &rally_point, &rally_entity);
        else if (ent->rally && ent->rally->type == RALLY_TARGET_ENTITY) {
            edict_t *target = ent->rally->entity;
            if (target && target->inuse && target->spawn_time == ent->rally->entity_spawn_time &&
                G_FowPlayerCanHoverEntity(player, target))
                rally_type = G_ResolveRallyTarget((edict_t *)ent, &rally_point, &rally_entity);
        }
        if (rally_type != RALLY_TARGET_NONE && rally_type != RALLY_TARGET_SELF) {
            wc3Sc2RallyTarget_t *rally = &out->rally_targets[out->rally_target_count++];
            vec2_t const board = WC3_SC2API_WorldToBoardPoint(&rally_point);
            rally->point = (vec3_t){ board.x, board.y, 0.0f };
            if ((rally_type == RALLY_TARGET_SELF || rally_type == RALLY_TARGET_ENTITY) && rally_entity) {
                rally->has_tag = true;
                rally->tag = WC3_SC2API_UnitTag(rally_entity);
            }
        }

        cargo_capacity = S_CargoCapacity((edict_t *)ent);
        if (cargo_capacity) {
            out->cargo_space_taken = (int32_t)(ent->cargo ? ent->cargo->count : 0);
            out->cargo_space_max = (int32_t)cargo_capacity;
            FOR_LOOP(i, MIN((ent->cargo ? ent->cargo->count : 0), (uint32_t)MAX_CARGO)) {
                edict_t *passenger = ent->cargo->units[i];
                wc3Sc2PassengerUnit_t *p;
                if (!passenger || !passenger->inuse) continue;
                p = &out->passengers[out->passenger_count++];
                p->tag = WC3_SC2API_UnitTag(passenger);
                p->health = passenger->health.value;
                p->health_max = passenger->health.max_value;
                p->energy = passenger->mana.value;
                p->energy_max = passenger->mana.max_value;
                p->unit_type = passenger->class_id;
            }
        }
    }

    /* SC2 Unit.orders is the active order followed by queued orders. Do not use
     * Warcraft's trigger-facing "last issued order" state here: Shift commands
     * update that state before they become active. The dedicated active-order
     * snapshot plus the authoritative FIFO preserves execution order. */
    if (out->alliance == WC3_SC2_ALLIANCE_SELF || out->alliance == WC3_SC2_ALLIANCE_ALLY) {
        unitOrder_t order;
        for (uint32_t i = 0; i < G_ProductionQueueCount((edict_t *)ent) && out->order_count < MAX_UNIT_ORDER_QUEUE + 1; i++) {
            uint32_t id;
            float progress;
            if (!G_GetProductionQueueOrder((edict_t *)ent, i, &id, &progress)) continue;
            out->orders[out->order_count].ability_id = id;
            out->orders[out->order_count++].progress = progress;
        }
        if (!out->order_count && G_GetActiveUnitOrder(ent, &order)) sc2api_append_order(out, &order);
        FOR_LOOP(i, G_UnitQueuedOrderCount(ent)) {
            if (!G_GetQueuedUnitOrder(ent, i, &order)) continue;
            sc2api_append_order(out, &order);
        }
    }
    return true;
}

uint32_t WC3_SC2API_BuildRawUnits(uint32_t player, wc3Sc2RawUnit_t *out, uint32_t max_out) {
    uint32_t count = 0;

    if (!WC3_SC2API_PlayerClient(player) || (!out && max_out)) return 0;
    FOR_LOOP(i, sc2_fog_memory[player].count) sc2_fog_memory[player].entries[i].present = false;
    for (uint32_t i = 0; i < globals.num_edicts && count < max_out; i++) {
        wc3Sc2RawUnit_t unit;
        if (!WC3_SC2API_FillRawUnit(player, &g_edicts[i], &unit)) continue;
        out[count++] = unit;
    }
    FOR_LOOP(i, sc2_fog_memory[player].count) {
        sc2FogMemory_t *memory = sc2_fog_memory[player].entries + i;
        if (!memory->tag || memory->present) continue;
        if (G_FowPointIsVisible(player, &memory->world)) { memory->tag = 0; continue; }
        if (count < max_out) sc2api_snapshot(memory, out + count++);
    }
    return count;
}

void WC3_SC2API_CaptureFogMemory(uint32_t player) {
    wc3Sc2RawUnit_t unit;
    FILTER_EDICTS(ent, ent->inuse && (G_IsDestructable(ent) || G_UnitIsStructure(ent))) {
        WC3_SC2API_FillRawUnit(player, ent, &unit);
    }
}

bool WC3_SC2API_PlacementIsVisible(uint32_t player, uint32_t building, vec2_t const *point) {
    vec2_t snapped;
    float radius;
    float const cell = WC3_SC2API_BoardCellWorldSize();
    if (!point || !isfinite(point->x) || !isfinite(point->y)) return false;
    snapped = *point;
    G_SnapBuildingPoint(building, &snapped);
    radius = G_BuildApproachDistance(building);
    for (float y = -radius; y <= radius; y += cell)
        for (float x = -radius; x <= radius; x += cell)
            if (!G_FowPointIsVisible(player, &MAKE(vec2_t, .x = snapped.x + x, .y = snapped.y + y))) return false;
    return G_FowPointIsVisible(player, &snapped);
}

static wc3Sc2ActionResult_t sc2api_unit_cost_failure(gameClient_t *client, uint32_t unit_type) {
    UnitBalance_t const *balance = G_UnitBalance(unit_type);

    if (!client || !balance || balance->id != unit_type) return WC3_SC2_ACTION_NOT_SUPPORTED;
    if (MAX(0, balance->goldCost) > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
        return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
    if (MAX(0, balance->lumberCost) > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
        return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
    if (!G_PlayerHasFoodFor(client, MAX(0, balance->foodUsed)))
        return WC3_SC2_ACTION_NOT_ENOUGH_FOOD;
    return WC3_SC2_ACTION_ERROR;
}

static wc3Sc2ActionResult_t sc2api_build_state_result(gameClient_t *client, uint32_t unit_type,
                                                       buildCommandState_t state) {
    switch (state) {
        case BUILD_COMMAND_AVAILABLE: return WC3_SC2_ACTION_SUCCESS;
        case BUILD_COMMAND_UNAFFORDABLE: return sc2api_unit_cost_failure(client, unit_type);
        case BUILD_COMMAND_DISABLED:
        case BUILD_COMMAND_HIDDEN: return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        default: return WC3_SC2_ACTION_NOT_SUPPORTED;
    }
}

static wc3Sc2ActionResult_t sc2api_placement_result(buildPlacementResult_t result) {
    switch (result) {
        case PLACE_OK: return WC3_SC2_ACTION_SUCCESS;
        case PLACE_OUT_OF_BOUNDS: return WC3_SC2_ACTION_CANT_BUILD_LOCATION_INVALID;
        case PLACE_REQUIRES_BLIGHT:
        case PLACE_REQUIRED_PATHING_MISSING: return WC3_SC2_ACTION_CANT_BUILD_ON_THAT;
        case PLACE_TOO_CLOSE_TO_GOLD_MINE: return WC3_SC2_ACTION_CANT_BUILD_TOO_CLOSE_TO_RESOURCES;
        case PLACE_REQUIRED_PARENT_MISSING: return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        case PLACE_INVALID_BUILDING: return WC3_SC2_ACTION_NOT_SUPPORTED;
        default: return WC3_SC2_ACTION_CANT_FIND_PLACEMENT;
    }
}

static wc3Sc2ActionResult_t sc2api_try_production_command(gameClient_t *client, edict_t *unit,
                                                           wc3Sc2RawUnitCommand_t const *command,
                                                           bool *handled) {
    uint32_t const id = command->ability_id;
    buildCommandState_t state;

    * handled = false;
    uint32_t const issuer = client->ps.number;
    bool const spends = (command->target_type == WC3_SC2_TARGET_POINT && G_WorkerCanBuild(unit, id)) ||
        (command->target_type == WC3_SC2_TARGET_NONE &&
         (G_ProducerCanTrain(unit, id) || G_ProducerCanResearch(unit, id) || G_ProducerCanUpgrade(unit, id)));
    if (spends) {
        if (!G_UnitCanSpendResources(client, unit) || !(client = WC3_SC2API_PlayerClient(unit->s.player))) {
            * handled = true;
            return WC3_SC2_ACTION_CANT_CONTROL_UNIT;
        }
    }
    if (command->target_type == WC3_SC2_TARGET_POINT && G_WorkerCanBuild(unit, id)) {
        vec2_t world = WC3_SC2API_BoardToWorldPoint(&command->target_point);
        buildPlacementResult_t placement;
        vec2_t snapped;

        * handled = true;
        state = G_GetBuildCommandState(client, unit, id, NULL, 0);
        if (state != BUILD_COMMAND_AVAILABLE) return sc2api_build_state_result(client, id, state);
        if (!WC3_SC2API_PlacementIsVisible(issuer, id, &world)) return WC3_SC2_ACTION_CANT_SEE_BUILD_LOCATION;
        placement = G_EvaluateBuildPlacement(unit, id, &world, &snapped);
        if (placement != PLACE_OK) return sc2api_placement_result(placement);
        return G_IssueUnitBuildOrder(unit, id, &world, command->queue_command, issuer)
            ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (command->target_type != WC3_SC2_TARGET_NONE) return WC3_SC2_ACTION_SUCCESS;

    if (G_ProducerCanTrain(unit, id)) {
        * handled = true;
        state = G_GetTrainCommandState(client, unit, id, NULL, 0);
        if (state != BUILD_COMMAND_AVAILABLE) return sc2api_build_state_result(client, id, state);
        return SP_TrainUnit(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (G_ProducerCanResearch(unit, id)) {
        int32_t next_level = 0;
        int32_t gold, lumber;

        * handled = true;
        state = G_GetResearchCommandState(client, unit, id, &next_level, NULL, 0);
        if (state == BUILD_COMMAND_ABSENT) return WC3_SC2_ACTION_NOT_SUPPORTED;
        if (state == BUILD_COMMAND_DISABLED || state == BUILD_COMMAND_HIDDEN)
            return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        if (state == BUILD_COMMAND_UNAFFORDABLE) {
            gold = G_UpgradeGoldCost(id, next_level);
            lumber = G_UpgradeLumberCost(id, next_level);
            if (gold > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
                return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
            if (lumber > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
                return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
            return WC3_SC2_ACTION_ERROR;
        }
        return G_QueueResearch(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (G_ProducerCanUpgrade(unit, id)) {
        buildingUpgradeCommandParams_t params = {
            .client = client, .producer = unit, .unit_id = id,
        };
        int32_t gold = 0, lumber = 0, food = 0;

        * handled = true;
        if (command->queue_command) return WC3_SC2_ACTION_CANT_QUEUE;
        state = G_GetBuildingUpgradeCommandState(&params);
        if (state == BUILD_COMMAND_ABSENT) return WC3_SC2_ACTION_NOT_SUPPORTED;
        if (state == BUILD_COMMAND_DISABLED || state == BUILD_COMMAND_HIDDEN)
            return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        if (state == BUILD_COMMAND_UNAFFORDABLE) {
            G_GetBuildingUpgradeCosts(&(buildingUpgradeCostParams_t){
                .building = unit, .unit_id = id, .gold = &gold, .lumber = &lumber, .food = &food,
            });
            if (gold > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
                return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
            if (lumber > (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
                return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
            if (!G_PlayerHasFoodFor(client, food)) return WC3_SC2_ACTION_NOT_ENOUGH_FOOD;
            return WC3_SC2_ACTION_ERROR;
        }
        return G_StartBuildingUpgrade(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    return WC3_SC2_ACTION_SUCCESS;
}

wc3Sc2ActionResult_t WC3_SC2API_IssueRawUnitCommand(uint32_t player,
                                                     wc3Sc2RawUnitCommand_t const *command) {
    gameClient_t *client;
    edict_t *unit;
    edict_t *target = NULL;
    cstring_t order;
    bool accepted = false;
    bool handled = false;
    wc3Sc2ActionResult_t production_result;

    if (!command || !(client = WC3_SC2API_PlayerClient(player))) return WC3_SC2_ACTION_ERROR;
    unit = WC3_SC2API_ResolveUnitTag(command->unit_tag);
    if (!unit) return WC3_SC2_ACTION_CANT_TARGET;
    if (!G_UnitCanControl(client, unit)) return WC3_SC2_ACTION_CANT_CONTROL_UNIT;

    /*WC3 production buttons naturally use unit/upgrade rawcodes rather than
     * generic order ids. UnitTypeData/UpgradeData expose those same rawcodes as
     * SC2 ability ids, so route them through the authoritative WC3 production
     * APIs before falling back to ordinary immediate/point/unit orders. */
    production_result = sc2api_try_production_command(client, unit, command, &handled);
    if (handled) return production_result;

    order = G_OrderId2String(command->ability_id);
    /* Command-card abilities use their authored rawcode. Resolve the normal
     * registered order rather than interpreting four rawcode bytes as an order
     * name. Alias-aware unit dispatch retains the concrete Warcraft ability. */
    abilityitem_t const item = S_AbilityItem(command->ability_id);
    if (G_AbilityData(command->ability_id)->id == command->ability_id && item.ability) {
        if (!G_UnitAbilityLevel(unit, command->ability_id) || !item.ability->orders ||
            !item.ability->orders[0]) return WC3_SC2_ACTION_NOT_SUPPORTED;
        order = item.ability->orders[0];
    }
    if (!order || !*order) return WC3_SC2_ACTION_NOT_SUPPORTED;
    uint32_t const ability_code = G_AbilityData(command->ability_id)->id == command->ability_id
        ? command->ability_id : sc2api_ability_code_for_order(command->ability_id);
    if (ability_code && (!G_IsUnitAbilityAvailable(unit, ability_code) ||
                         !G_UnitAbilityResearchAvailable(unit, ability_code)))
        return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;

    switch (command->target_type) {
        case WC3_SC2_TARGET_NONE:
            if (command->queue_command) return WC3_SC2_ACTION_CANT_QUEUE;
            accepted = unit_issueimmediateorder(unit, order);
            break;
        case WC3_SC2_TARGET_POINT: {
            if (!isfinite(command->target_point.x) || !isfinite(command->target_point.y)) return WC3_SC2_ACTION_ERROR;
            vec2_t const world_point = WC3_SC2API_BoardToWorldPoint(&command->target_point);
            accepted = G_IssueUnitPointOrder(unit, order, &world_point,
                                             command->queue_command, player, 0.0f);
            break;
        }
        case WC3_SC2_TARGET_UNIT:
            target = WC3_SC2API_ResolveUnitTag(command->target_unit_tag);
            if (!target) return WC3_SC2_ACTION_CANT_TARGET;
            if (!G_FowPlayerCanHoverEntity(player, target)) {
                return WC3_SC2_ACTION_MUST_TARGET_VISIBLE_UNIT;
            }
            accepted = G_IssueUnitTargetOrder(unit, order, target,
                                              command->queue_command, player);
            break;
        default:
            return WC3_SC2_ACTION_NOT_SUPPORTED;
    }
    return accepted ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
}

wc3Sc2ActionResult_t WC3_SC2API_ToggleRawAutocast(uint32_t player, uint32_t ability_id,
                                                   uint64_t const *unit_tags, uint32_t unit_count) {
    gameClient_t *client = WC3_SC2API_PlayerClient(player);
    uint32_t const ability_code = sc2api_ability_code_for_order(ability_id);
    abilityitem_t const item = ability_code ? S_AbilityItem(ability_code) : (abilityitem_t){ 0 };
    wc3Sc2ActionResult_t aggregate = WC3_SC2_ACTION_SUCCESS;

    if (!client || !unit_tags || !unit_count || !item.ability || !(item.ability->flags & AB_AUTOCAST))
        return WC3_SC2_ACTION_NOT_SUPPORTED;
    FOR_LOOP(i, unit_count) {
        edict_t *unit = WC3_SC2API_ResolveUnitTag(unit_tags[i]);
        wc3Sc2ActionResult_t result = WC3_SC2_ACTION_SUCCESS;
        if (!unit) result = WC3_SC2_ACTION_CANT_TARGET;
        else if (!G_UnitCanControl(client, unit)) result = WC3_SC2_ACTION_CANT_CONTROL_UNIT;
        else if (!G_UnitAbilityLevel(unit, ability_code)) result = WC3_SC2_ACTION_NOT_SUPPORTED;
        else if (!G_SetUnitAutocast(unit, ability_code, !G_UnitAutocastIsOn(unit, ability_code)))
            result = WC3_SC2_ACTION_ERROR;
        if (aggregate == WC3_SC2_ACTION_SUCCESS && result != WC3_SC2_ACTION_SUCCESS) aggregate = result;
    }
    return aggregate;
}

#endif
