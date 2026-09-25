#ifdef WC3_SC2API

#include "sc2api_raw.h"
#include "sc2api_map.h"

static wc3Sc2Alliance_t sc2api_alliance(DWORD player, LPCEDICT ent) {
    DWORD owner;

    if (!ent) return WC3_SC2_ALLIANCE_ENEMY;
    owner = ent->s.player;
    if (owner == player) return WC3_SC2_ALLIANCE_SELF;
    if (owner == PLAYER_NEUTRAL_PASSIVE) return WC3_SC2_ALLIANCE_NEUTRAL;
    if (player < MAX_PLAYERS && owner < MAX_PLAYERS && G_PlayerTreatsPlayerAsAlly(player, owner)) {
        return WC3_SC2_ALLIANCE_ALLY;
    }
    return WC3_SC2_ALLIANCE_ENEMY;
}

static wc3Sc2CloakState_t sc2api_cloak_state(DWORD player, LPCEDICT ent,
                                              wc3Sc2Alliance_t alliance,
                                              BOOL visible) {
    if (!visible) return WC3_SC2_CLOAK_UNKNOWN;
    if (!S_UnitUsesInvisibilityRenderFlag(ent)) return WC3_SC2_NOT_CLOAKED;
    if (alliance == WC3_SC2_ALLIANCE_SELF || alliance == WC3_SC2_ALLIANCE_ALLY) {
        return WC3_SC2_CLOAKED_ALLIED;
    }
    if (!S_UnitIsInvisibleToPlayer(ent, player)) return WC3_SC2_CLOAKED_DETECTED;
    return WC3_SC2_CLOAKED;
}


static DWORD sc2api_weapon_upgrade(LPCEDICT ent) {
    static LPCSTR const classes[] = { "melee", "ranged", "artillery" };
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0])) {
        DWORD const upgrade = G_GetUnitUpgradeForClass(ent, classes[i]);
        if (upgrade) return upgrade;
    }
    return 0;
}

static DWORD sc2api_ability_code_for_order(DWORD order_id) {
    FOR_LOOP(i, G_OrderDefinitionCount()) {
        DWORD id = 0, ability_code = 0;
        if (G_OrderDefinitionInfo(i, &id, NULL, &ability_code) && id == order_id) return ability_code;
    }
    return 0;
}

static FLOAT sc2api_build_progress(LPCEDICT ent) {
    FLOAT duration;

    if (!ent || !ent->construction.active || !ent->data.UnitBalance ||
        ent->data.UnitBalance->buildTime <= 0) {
        return 1.0f;
    }
    duration = (FLOAT)ent->data.UnitBalance->buildTime * 1000.0f;
    return MAX(0.0f, MIN(1.0f, ent->construction.progress / duration));
}

static BOOL sc2api_append_order(wc3Sc2RawUnit_t *out, unitOrder_t const *order) {
    DWORD ability_id;
    DWORD index;

    if (!out || !order || !order->order[0] || out->order_count >= MAX_UNIT_ORDER_QUEUE + 1) return false;
    ability_id = G_OrderId(order->order);
    if (!ability_id) return false;
    index = out->order_count;
    out->orders[index].ability_id = ability_id;
    if (order->target_type == UNIT_ORDER_TARGET_POINT) {
        out->orders[index].has_point = true;
        out->orders[index].point = WC3_SC2API_WorldToBoardPoint(&order->point);
    } else if (order->target_type == UNIT_ORDER_TARGET_ENTITY) {
        LPEDICT target;
        if (order->target_number >= globals.num_edicts) return false;
        target = globals.edicts + order->target_number;
        if (!target->inuse || target->spawn_time != order->target_spawn_time || M_IsDead(target)) return false;
        out->orders[index].has_target_unit = true;
        out->orders[index].target_unit_tag = WC3_SC2API_UnitTag(target);
    }
    out->order_count++;
    return true;
}

BOOL WC3_SC2API_FillRawUnit(DWORD player, LPCEDICT ent, wc3Sc2RawUnit_t *out) {
    entityState_t state;
    BOOL visible;
    DWORD cargo_capacity;
    LPGAMECLIENT owner_client;
    BOOL const destructable = ent && G_IsDestructable(ent);

    if (!out || !ent || !ent->inuse || !WC3_SC2API_PlayerClient(player) ||
        (!(ent->svflags & SVF_MONSTER) && !destructable) ||
        (ent->svflags & SVF_DEADMONSTER) || ent->health.value <= 0.0f ||
        !G_FowPlayerCanSeeEntity(player, ent)) {
        return false;
    }

    state = ent->s;
    if (globals.CustomizeEntity) globals.CustomizeEntity(player, ent, &state);
    if (state.renderfx & RF_HIDDEN) return false;

    visible = G_FowPlayerCanHoverEntity(player, ent);
    memset(out, 0, sizeof(*out));
    out->display_type = visible ? WC3_SC2_DISPLAY_VISIBLE : WC3_SC2_DISPLAY_SNAPSHOT;
    out->alliance = destructable ? WC3_SC2_ALLIANCE_NEUTRAL : sc2api_alliance(player, ent);
    out->tag = WC3_SC2API_UnitTag(ent);
    out->unit_type = destructable ? WC3_SC2API_DestructableTypeId(ent->class_id) : ent->class_id;
    out->owner = destructable ? 0 : (ent->s.player < MAX_PLAYERS ? (LONG)ent->s.player + 1 : 0);
    {
        VECTOR2 const board = WC3_SC2API_WorldToBoardPoint(&state.origin2);
        out->pos = (VECTOR3){ board.x, board.y, WC3_SC2API_WorldToBoardHeight(state.origin.z) };
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

    /* Blizzard raw.proto does not populate live combat/economy fields for a
     * Snapshot. Redact them rather than leaking current fogged building state. */
    if (!visible) return true;

    out->has_live_data = true;
    out->health = ent->health.value;
    out->health_max = ent->health.max_value;
    if (destructable) return true;
    out->energy = ent->mana.value;
    out->energy_max = ent->mana.max_value;
    if ((state.flags & EF_RESOURCE_SOURCE) && ent->resources != UINT_MAX) {
        out->vespene_contents = (LONG)MIN(ent->resources, 0x7fffffffu);
    }
    out->is_flying = (ent->aiflags & AI_FLYING) != 0;
    out->is_active = (state.flags & EF_BUILDING) &&
        (ent->build != NULL || G_BuildingUpgradeActive((LPEDICT)ent));

    owner_client = ent->s.player < MAX_PLAYERS ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    if (owner_client && out->alliance != WC3_SC2_ALLIANCE_ENEMY) {
        DWORD const weapon_upgrade = sc2api_weapon_upgrade(ent);
        DWORD const armor_upgrade = G_GetUnitUpgradeForClass(ent, "armor");
        if (weapon_upgrade) {
            out->has_attack_upgrade_level = true;
            out->attack_upgrade_level = G_GetPlayerTechResearchedLevel(owner_client, weapon_upgrade);
        }
        if (armor_upgrade) {
            out->has_armor_upgrade_level = true;
            out->armor_upgrade_level = G_GetPlayerTechResearchedLevel(owner_client, armor_upgrade);
        }
    }

    /* Resolve the same authored WC3 buff rawcode used by the normal status
     * panel, then expose that stable uint32 id as SC2 Raw Unit.buff_ids. Only
     * publish live visible state; snapshots return before this point. */
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        DWORD const buff = G_UnitStatusBuffCode(&ent->abilstatus[i]);
        BOOL duplicate = false;

        if (!buff ||
            (ent->abilstatus[i].timestamp && ent->abilstatus[i].timestamp <= G_Time()) ||
            out->buff_count >= MAX_UNIT_STATUSES) continue;
        FOR_LOOP(j, out->buff_count) if (out->buff_ids[j] == buff) { duplicate = true; break; }
        if (!duplicate) out->buff_ids[out->buff_count++] = buff;
    }

    /* raw.proto marks orders/passenger/cargo state as unavailable for enemy
     * units. Friendly transport contents are authoritative and already bounded
     * by MAX_CARGO, so expose the same PassengerUnit fields SC2 provides. */
    if (out->alliance != WC3_SC2_ALLIANCE_ENEMY) {
        VECTOR2 rally_point;
        LPEDICT rally_entity = NULL;
        rallyTargetType_t const rally_type = G_ResolveRallyTarget((LPEDICT)ent, &rally_point, &rally_entity);
        if (rally_type != RALLY_TARGET_NONE) {
            wc3Sc2RallyTarget_t *rally = &out->rally_targets[out->rally_target_count++];
            VECTOR2 const board = WC3_SC2API_WorldToBoardPoint(&rally_point);
            rally->point = (VECTOR3){ board.x, board.y, 0.0f };
            if ((rally_type == RALLY_TARGET_SELF || rally_type == RALLY_TARGET_ENTITY) && rally_entity) {
                rally->has_tag = true;
                rally->tag = WC3_SC2API_UnitTag(rally_entity);
            }
        }

        cargo_capacity = S_CargoCapacity((LPEDICT)ent);
        if (cargo_capacity) {
            out->cargo_space_taken = (LONG)ent->cargo.count;
            out->cargo_space_max = (LONG)cargo_capacity;
            FOR_LOOP(i, MIN(ent->cargo.count, (DWORD)MAX_CARGO)) {
                LPEDICT passenger = ent->cargo.units[i];
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
        if (G_GetActiveUnitOrder(ent, &order)) sc2api_append_order(out, &order);
        FOR_LOOP(i, G_UnitQueuedOrderCount(ent)) {
            if (!G_GetQueuedUnitOrder(ent, i, &order)) continue;
            sc2api_append_order(out, &order);
        }
    }
    return true;
}

DWORD WC3_SC2API_BuildRawUnits(DWORD player, wc3Sc2RawUnit_t *out, DWORD max_out) {
    DWORD count = 0;

    if (!WC3_SC2API_PlayerClient(player) || (!out && max_out)) return 0;
    for (DWORD i = 0; i < globals.num_edicts && count < max_out; i++) {
        wc3Sc2RawUnit_t unit;
        if (!WC3_SC2API_FillRawUnit(player, &g_edicts[i], &unit)) continue;
        out[count++] = unit;
    }
    return count;
}

static wc3Sc2ActionResult_t sc2api_unit_cost_failure(LPGAMECLIENT client, DWORD unit_type) {
    UnitBalance_t const *balance = G_UnitBalance(unit_type);

    if (!client || !balance || balance->id != unit_type) return WC3_SC2_ACTION_NOT_SUPPORTED;
    if (MAX(0, balance->goldCost) > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
        return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
    if (MAX(0, balance->lumberCost) > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
        return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
    if (!G_PlayerHasFoodFor(client, MAX(0, balance->foodUsed)))
        return WC3_SC2_ACTION_NOT_ENOUGH_FOOD;
    return WC3_SC2_ACTION_ERROR;
}

static wc3Sc2ActionResult_t sc2api_build_state_result(LPGAMECLIENT client, DWORD unit_type,
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

static wc3Sc2ActionResult_t sc2api_try_production_command(LPGAMECLIENT client, LPEDICT unit,
                                                           wc3Sc2RawUnitCommand_t const *command,
                                                           BOOL *handled) {
    DWORD const id = command->ability_id;
    buildCommandState_t state;

    *handled = false;
    if (command->target_type == WC3_SC2_TARGET_POINT && G_WorkerCanBuild(unit, id)) {
        VECTOR2 world = WC3_SC2API_BoardToWorldPoint(&command->target_point);
        buildPlacementResult_t placement;
        VECTOR2 snapped;

        *handled = true;
        if (command->queue_command) return WC3_SC2_ACTION_CANT_QUEUE;
        state = G_GetBuildCommandState(client, unit, id, NULL, 0);
        if (state != BUILD_COMMAND_AVAILABLE) return sc2api_build_state_result(client, id, state);
        placement = G_EvaluateBuildPlacement(unit, id, &world, &snapped);
        if (placement != PLACE_OK) return sc2api_placement_result(placement);
        return G_IssueBuildOrder(unit, id, &world) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (command->target_type != WC3_SC2_TARGET_NONE) return WC3_SC2_ACTION_SUCCESS;

    if (G_ProducerCanTrain(unit, id)) {
        *handled = true;
        state = G_GetTrainCommandState(client, unit, id, NULL, 0);
        if (state != BUILD_COMMAND_AVAILABLE) return sc2api_build_state_result(client, id, state);
        return SP_TrainUnit(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (G_ProducerCanResearch(unit, id)) {
        LONG next_level = 0;
        LONG gold, lumber;

        *handled = true;
        state = G_GetResearchCommandState(client, unit, id, &next_level, NULL, 0);
        if (state == BUILD_COMMAND_ABSENT) return WC3_SC2_ACTION_NOT_SUPPORTED;
        if (state == BUILD_COMMAND_DISABLED || state == BUILD_COMMAND_HIDDEN)
            return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        if (state == BUILD_COMMAND_UNAFFORDABLE) {
            gold = G_UpgradeGoldCost(id, next_level);
            lumber = G_UpgradeLumberCost(id, next_level);
            if (gold > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
                return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
            if (lumber > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
                return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
            return WC3_SC2_ACTION_ERROR;
        }
        return G_QueueResearch(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    if (G_ProducerCanUpgrade(unit, id)) {
        buildingUpgradeCommandParams_t params = {
            .client = client, .producer = unit, .unit_id = id,
        };
        LONG gold = 0, lumber = 0, food = 0;

        *handled = true;
        if (command->queue_command) return WC3_SC2_ACTION_CANT_QUEUE;
        state = G_GetBuildingUpgradeCommandState(&params);
        if (state == BUILD_COMMAND_ABSENT) return WC3_SC2_ACTION_NOT_SUPPORTED;
        if (state == BUILD_COMMAND_DISABLED || state == BUILD_COMMAND_HIDDEN)
            return WC3_SC2_ACTION_BUILD_TECH_REQUIREMENTS;
        if (state == BUILD_COMMAND_UNAFFORDABLE) {
            G_GetBuildingUpgradeCosts(&(buildingUpgradeCostParams_t){
                .building = unit, .unit_id = id, .gold = &gold, .lumber = &lumber, .food = &food,
            });
            if (gold > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD])
                return WC3_SC2_ACTION_NOT_ENOUGH_VESPENE;
            if (lumber > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER])
                return WC3_SC2_ACTION_NOT_ENOUGH_MINERALS;
            if (!G_PlayerHasFoodFor(client, food)) return WC3_SC2_ACTION_NOT_ENOUGH_FOOD;
            return WC3_SC2_ACTION_ERROR;
        }
        return G_StartBuildingUpgrade(unit, id) ? WC3_SC2_ACTION_SUCCESS : WC3_SC2_ACTION_ERROR;
    }
    return WC3_SC2_ACTION_SUCCESS;
}

wc3Sc2ActionResult_t WC3_SC2API_IssueRawUnitCommand(DWORD player,
                                                     wc3Sc2RawUnitCommand_t const *command) {
    LPGAMECLIENT client;
    LPEDICT unit;
    LPEDICT target = NULL;
    LPCSTR order;
    BOOL accepted = false;
    BOOL handled = false;
    wc3Sc2ActionResult_t production_result;

    if (!command || !(client = WC3_SC2API_PlayerClient(player))) return WC3_SC2_ACTION_ERROR;
    unit = WC3_SC2API_ResolveUnitTag(command->unit_tag);
    if (!unit) return WC3_SC2_ACTION_CANT_TARGET;
    if (!G_UnitCanControl(client, unit)) return WC3_SC2_ACTION_CANT_CONTROL_UNIT;

    /* WC3 production buttons naturally use unit/upgrade rawcodes rather than
     * generic order ids. UnitTypeData/UpgradeData expose those same rawcodes as
     * SC2 ability ids, so route them through the authoritative WC3 production
     * APIs before falling back to ordinary immediate/point/unit orders. */
    production_result = sc2api_try_production_command(client, unit, command, &handled);
    if (handled) return production_result;

    order = G_OrderId2String(command->ability_id);
    if (!order || !*order) return WC3_SC2_ACTION_NOT_SUPPORTED;

    switch (command->target_type) {
        case WC3_SC2_TARGET_NONE:
            if (command->queue_command) return WC3_SC2_ACTION_CANT_QUEUE;
            accepted = unit_issueimmediateorder(unit, order);
            break;
        case WC3_SC2_TARGET_POINT: {
            VECTOR2 const world_point = WC3_SC2API_BoardToWorldPoint(&command->target_point);
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

wc3Sc2ActionResult_t WC3_SC2API_ToggleRawAutocast(DWORD player, DWORD ability_id,
                                                   uint64_t const *unit_tags, DWORD unit_count) {
    LPGAMECLIENT client = WC3_SC2API_PlayerClient(player);
    DWORD const ability_code = sc2api_ability_code_for_order(ability_id);
    abilityitem_t const item = ability_code ? S_AbilityItem(ability_code) : (abilityitem_t){ 0 };
    wc3Sc2ActionResult_t aggregate = WC3_SC2_ACTION_SUCCESS;

    if (!client || !unit_tags || !unit_count || !item.ability || !(item.ability->flags & AB_AUTOCAST))
        return WC3_SC2_ACTION_NOT_SUPPORTED;
    FOR_LOOP(i, unit_count) {
        LPEDICT unit = WC3_SC2API_ResolveUnitTag(unit_tags[i]);
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
