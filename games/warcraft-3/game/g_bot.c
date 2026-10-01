#include "g_local.h"
#include "jass/jass.h"
#include "skills/s_skills.h"

#define BOT_GUARD_RETURN_RANGE 82.006f // world units; avoid resetting movement for guards already standing near their post
#define BOT_BUILD_GRID 32.0f // world units; WC3 structures snap to this placement-cell interval
#define BOT_BUILD_SEARCH_RINGS 32 // 32-unit grid rings; searches 1024 world units around a town for legal placement
#define BOT_INJURED_HEALTH_FRACTION 0.5f // retail AI Editor: units below 50% life are treated as injured
#define BOT_DEFAULT_REPLACEMENT_COUNT 3

static bot_t *G_BotState(uint32_t player) {
    return player < MAX_PLAYERS ? &level.bots[player] : NULL;
}

static bool G_BotBuildSiteReachable(edict_t *, vec2_t const *);
static bool G_BotIsHostile(player_t *, edict_t *);

static void G_BotClearCaptains(bot_t *bot) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        if (bot->captains[i].units) gi.MemFree(bot->captains[i].units);
        memset(bot->captains + i, 0, sizeof(bot->captains[i]));
    }
}

/* KillUnit changes life immediately while ordinary death also carries SVF_DEADMONSTER. */
bool G_BotUnitAlive(edict_t *unit) {
    return unit && unit->inuse && unit->health.value > 0 && !(unit->svflags & SVF_DEADMONSTER);
}

/* common.ai uses this as a shared assault rendezvous, not as an order primitive.
 * Publish the same target into each mutually-passive ally's bot slot so a later
 * GetAllianceTarget observes the common value. Publishing NULL clears that shared
 * value for the current alliance, matching the stock join-ally-force consume path. */
void G_BotSetAllianceTarget(player_t *player, edict_t *target) {
    uint32_t owner;
    if (!player) return;
    owner = PLAYER_NUM(player);
    if (owner >= MAX_PLAYERS) return;

    FOR_LOOP(i, MAX_PLAYERS) {
        player_t *other = &game.clients[i].ps;
        if (i != owner && (!G_GetPlayerAlliance(player, other, ALLIANCE_PASSIVE) ||
                           !G_GetPlayerAlliance(other, player, ALLIANCE_PASSIVE))) continue;
        level.bots[i].alliance_target = target;
    }
}

edict_t *G_BotGetAllianceTarget(player_t *player) {
    bot_t *bot;
    if (!player || PLAYER_NUM(player) >= MAX_PLAYERS) return NULL;
    bot = G_BotState(PLAYER_NUM(player));
    if (!bot || !G_BotUnitAlive(bot->alliance_target)) {
        if (bot) bot->alliance_target = NULL;
        return NULL;
    }
    return bot->alliance_target;
}

/* Stop only active gather orders; carried resources remain available for an explicit return order. */
void G_BotStopGathering(player_t *player) {
    if (!player) return;
    FILTER_EDICTS(unit, unit->inuse && unit->s.player == PLAYER_NUM(player) && unit->currentmove &&
        (unit->currentmove->proc == CAbilityHarvest || unit->currentmove->proc == CAbilityGoldMine ||
         unit->currentmove->proc == CAbilityWispHarvest)) {
        S_GoldMineReleaseWorker(unit);
        order_stop_cleanup(unit);
    }
}

static bool G_BotHarvesterReserved(bot_t *bot, edict_t *unit) {
    FOR_EACH_ARRAY(edict_t *, assigned, bot->harvesters) if (*assigned == unit) return true;
    return false;
}

static void G_BotReserveHarvester(bot_t *bot, edict_t *unit) {
    uint32_t count = ARRAY_COUNT(bot->harvesters);
    edict_t * *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, bot->harvesters, count * sizeof(*units));
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = units; ARRAY_COUNT(bot->harvesters) = count + 1; bot->harvesters[count] = unit;
}

void G_BotClearHarvest(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = NULL; ARRAY_COUNT(bot->harvesters) = 0;
}

/* Town IDs enumerate owned gold drop-offs in spawn order, matching the expansion index used by common.ai. */
edict_t *G_BotTown(player_t *player, int32_t town) {
    edict_t probe = {0};
    if (!player || town < 0) return NULL;
    probe.s.player = PLAYER_NUM(player);
    FILTER_EDICTS(ent, S_CanReturnResourceAt(&probe, ent, RETURN_RESOURCE_GOLD))
        if (!town--) return ent;
    return NULL;
}

static bool G_BotUnitAtTown(player_t *, edict_t *, int32_t);

int32_t G_BotTownUnitCount(player_t *player, uint32_t class_id, int32_t town_id, bool done) {
    edict_t *town = G_BotTown(player, town_id);
    int32_t count = 0;
    if (!player || !town || !class_id) return 0;
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && ent->class_id == class_id &&
                         ent->s.player == PLAYER_NUM(player) && !(ent->svflags & SVF_DEADMONSTER) &&
                         G_BotUnitAtTown(player, ent, town_id)) {
        if (!done || (!ent->construction.active && !ent->training)) count++;
    }
    if (!done) FILTER_EDICTS(builder, G_BotUnitAlive(builder) && builder->s.player == PLAYER_NUM(player) &&
                                      builder->build_project == class_id && G_BotUnitAtTown(player, builder, town_id)) count++;
    return count;
}

static edict_t *G_BotMineOwner(player_t *player, edict_t *mine) {
    edict_t *best = NULL;
    float best_dist = 0;
    edict_t probe = {0};
    if (!player || !mine) return NULL;
    probe.s.player = PLAYER_NUM(player);
    FILTER_EDICTS(town, S_CanReturnResourceAt(&probe, town, RETURN_RESOURCE_GOLD)) {
        float dist = Vector2_distance(&town->s.origin2, &mine->s.origin2);
        if (!best || dist < best_dist) { best = town; best_dist = dist; }
    }
    return best;
}

static edict_t *G_BotHarvestTarget(player_t *player, edict_t *town, returnResource_t resource) {
    edict_t *best = NULL;
    float best_dist = 0;
    FILTER_EDICTS(ent, resource == RETURN_RESOURCE_GOLD ? S_GoldMineCanHarvest(ent) :
        ent->inuse && ent->targtype == TARG_TREE && !M_IsDead(ent)) {
        float dist = Vector2_distance(&town->s.origin2, &ent->s.origin2);
        if (resource == RETURN_RESOURCE_GOLD && G_BotMineOwner(player, ent) != town) continue;
        if (!best || dist < best_dist) { best = ent; best_dist = dist; }
    }
    return best;
}

edict_t *G_BotTownMine(player_t *player, int32_t town) {
    edict_t *hall = G_BotTown(player, town);
    return hall ? G_BotHarvestTarget(player, hall, RETURN_RESOURCE_GOLD) : NULL;
}

int32_t G_BotTownWithMine(player_t *player) {
    for (int32_t town = 0; G_BotTown(player, town); town++)
        if (G_BotTownMine(player, town)) return town;
    return -1;
}

edict_t *G_BotExpansionMine(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botExpansion_t *expansion = bot ? &bot->expansion : NULL;
    edict_t *mine;
    if (!expansion || !expansion->valid || expansion->entity_number >= globals.num_edicts) return NULL;
    mine = globals.edicts + expansion->entity_number;
    if (!mine->inuse || mine->spawn_time != expansion->spawn_time || !S_GoldMineCanHarvest(mine)) {
        expansion->valid = false;
        return NULL;
    }
    return mine;
}

static edict_t *G_BotNearestTownToMine(edict_t *mine) {
    edict_t *best = NULL;
    float best_dist = 0;
    if (!mine) return NULL;
    FILTER_EDICTS(town, G_BotUnitAlive(town) && S_UnitTypeReturnsGold(town->class_id) &&
                        !town->construction.active) {
        float dist = Vector2_distance(&town->s.origin2, &mine->s.origin2);
        if (!best || dist < best_dist || (dist == best_dist && town->s.number < best->s.number)) {
            best = town; best_dist = dist;
        }
    }
    return best;
}

static bool G_BotMineAlreadyTowned(player_t *player, edict_t *mine) {
    edict_t *town;
    if (!player || !mine) return true;
    town = G_BotNearestTownToMine(mine);
    return town && town->s.player == PLAYER_NUM(player);
}

static bool G_BotMineClaimedByOther(edict_t *mine, player_t *player) {
    edict_t *town = G_BotNearestTownToMine(mine);
    return town && town->s.player != PLAYER_NUM(player);
}

int32_t G_BotNextExpansion(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    edict_t *main_town = G_BotTown(player, 0), *best = NULL;
    float best_dist = 0;
    if (!bot || !main_town) { if (bot) bot->expansion.valid = false; return -1; }
    if (G_BotExpansionMine(player)) {
        edict_t *builder = NULL;
        if (!bot->expansion.build_accepted) return 0;
        FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
                            unit->build_project == bot->expansion.hall_id) { builder = unit; break; }
        if (builder) return 0;
        bot->expansion.valid = false;
        bot->expansion.build_accepted = false;
        bot->expansion.hall_id = 0;
    }
    /* BZ_COMPAT_GUESS: rank viable unclaimed mines by distance to the first owned town hall. */
    FILTER_EDICTS(mine, S_GoldMineCanHarvest(mine) && !G_BotMineAlreadyTowned(player, mine) &&
                        !G_BotMineClaimedByOther(mine, player)) {
        float dist = Vector2_distance(&main_town->s.origin2, &mine->s.origin2);
        if (!best || dist < best_dist || (dist == best_dist && mine->s.number < best->s.number)) {
            best = mine; best_dist = dist;
        }
    }
    if (!best) { bot->expansion.valid = false; return -1; }
    bot->expansion.entity_number = best->s.number;
    bot->expansion.spawn_time = best->spawn_time;
    bot->expansion.position = best->s.origin2;
    bot->expansion.hall_id = 0;
    bot->expansion.valid = true;
    bot->expansion.build_accepted = false;
    return 0;
}

vec2_t G_BotExpansionPosition(player_t *player) {
    edict_t *mine = G_BotExpansionMine(player);
    return mine ? mine->s.origin2 : MAKE(vec2_t, 0, 0);
}

edict_t *G_BotExpansionFoe(player_t *player) {
    edict_t *mine = G_BotExpansionMine(player), *best = NULL;
    float best_dist = 0;
    if (!mine || !player) return NULL;
    /* BZ_COMPAT_GUESS: a 1200-unit site radius and nearest hostile live unit/building identify a contested expansion. */
    FILTER_EDICTS(ent, ent != mine && ent->inuse && ent->health.value > 0 &&
        !(ent->svflags & (SVF_DEADMONSTER | SVF_NOCLIENT)) &&
        ((ent->svflags & SVF_MONSTER) || G_UnitIsStructure(ent)) && G_BotIsHostile(player, ent)) {
        float dist = Vector2_distance(&mine->s.origin2, &ent->s.origin2);
        if (dist > 1200.0f) continue;
        if (!best || dist < best_dist || (dist == best_dist && ent->s.number < best->s.number)) {
            best = ent; best_dist = dist;
        }
    }
    return best;
}

edict_t *G_BotExpansionPeon(player_t *player) {
    edict_t *mine = G_BotExpansionMine(player), *best = NULL;
    float best_dist = 0;
    if (!mine || !player) return NULL;
    FILTER_EDICTS(worker, G_BotUnitAlive(worker) && worker->s.player == PLAYER_NUM(player) &&
        !worker->construction.active && !worker->training && !worker->build_project &&
        !S_GoldMineWorkerIsInside(worker) && !G_BuildingUpgradeActive(worker) &&
        G_ActorHasSkill(worker, "Ahar") && worker->data.UnitProfile && worker->data.UnitProfile->builds) {
        float dist = Vector2_distance(&mine->s.origin2, &worker->s.origin2);
        if (!best || dist < best_dist || (dist == best_dist && worker->s.number < best->s.number)) {
            best = worker; best_dist = dist;
        }
    }
    return best;
}

bool G_BotSetExpansion(player_t *player, edict_t *worker, uint32_t hall_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    edict_t *mine = G_BotExpansionMine(player);
    UnitBalance_t const *balance = hall_id ? G_UnitBalance(hall_id) : NULL;
    UnitProfile_t const *profile = hall_id ? G_UnitProfile(hall_id) : NULL;
    vec2_t center;
    if (!bot || !mine || !worker || !G_BotUnitAlive(worker) || worker->s.player != PLAYER_NUM(player) ||
        !balance || !profile || !G_UnitIsBuilding(hall_id) || !G_WorkerCanBuild(worker, hall_id) ||
        !worker->data.UnitProfile || !G_ActorHasSkill(worker, "Ahar") ||
        worker->construction.active || worker->training || worker->build_project ||
        S_GoldMineWorkerIsInside(worker) || G_BuildingUpgradeActive(worker) ||
        player->stats[PLAYERSTATE_RESOURCE_GOLD] < balance->goldCost ||
        player->stats[PLAYERSTATE_RESOURCE_LUMBER] < balance->lumberCost) return false;
    center = bot->expansion.position;
    /* Respect the normal placement rule that keeps resource-return buildings at least 512 units from a mine.
     * BZ_COMPAT_GUESS: search concentric 32-unit cells starting one cell beyond that authored runtime threshold. */
    for (int32_t ring = (int32_t)(WC3_GOLD_MINE_MIN_DISTANCE / BOT_BUILD_GRID) + 1; ring <= 24; ring++) {
        for (int32_t x = -ring; x <= ring; x++) for (int32_t y = -ring; y <= ring; y++) {
            vec2_t point;
            if (abs(x) != ring && abs(y) != ring) continue;
            point = MAKE(vec2_t, center.x + x * BOT_BUILD_GRID, center.y + y * BOT_BUILD_GRID);
            if (!G_BotBuildSiteReachable(worker, &point)) continue;
            if (G_IssueBuildOrder(worker, hall_id, &point)) {
                bot->town_spot_valid = false;
                bot->expansion.hall_id = hall_id;
                bot->expansion.build_accepted = true;
                return true;
            }
        }
    }
    return false;
}

uint32_t G_BotMinesOwned(player_t *player) {
    uint32_t count = 0;
    for (int32_t town = 0; G_BotTown(player, town); town++)
        if (G_BotTownMine(player, town)) count++;
    return count;
}

uint32_t G_BotGoldOwned(player_t *player) {
    uint32_t gold = 0;
    for (int32_t town = 0; G_BotTown(player, town); town++) {
        edict_t *mine = G_BotTownMine(player, town);
        if (mine) gold += mine->resources;
    }
    return gold;
}

static bool G_BotUnitAtTown(player_t *player, edict_t *unit, int32_t town_id) {
    edict_t *town, *nearest = NULL, *candidate;
    float best_dist = 0;
    if (town_id < 0) return true;
    town = G_BotTown(player, town_id);
    if (!town) return false;
    for (int32_t index = 0; (candidate = G_BotTown(player, index)); index++) {
        float dist = Vector2_distance(&candidate->s.origin2, &unit->s.origin2);
        if (!nearest || dist < best_dist) { nearest = candidate; best_dist = dist; }
    }
    return nearest == town;
}

static bool G_BotBuildSiteReachable(edict_t *worker, vec2_t const *point) {
    return worker && point && CM_LineIsWalkableForRadius(&worker->s.origin2, point, MAX(0.0f, worker->collision));
}

static bool G_BotBuildNearTown(player_t *player, uint32_t class_id, int32_t town_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    edict_t *town = G_BotTown(player, town_id < 0 ? 0 : town_id);
    vec2_t const *center;
    if (!bot || !town) return false;
    center = bot->town_spot_valid ? &bot->town_spot : &town->s.origin2;
    /* Pending footprints are not baked yet, so serialize them to keep later orders from invalidating earlier placement. */
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && unit->build_project)
        return false;
    FILTER_EDICTS(worker, G_BotUnitAlive(worker) && worker->s.player == PLAYER_NUM(player) &&
        !worker->construction.active && !worker->training && !worker->build_project &&
        (!worker->currentmove || worker->currentmove->proc != CAbilityRepair) && G_WorkerCanBuild(worker, class_id)) {
        for (int32_t ring = 1; ring <= BOT_BUILD_SEARCH_RINGS; ring++) {
            for (int32_t x = -ring; x <= ring; x++) for (int32_t y = -ring; y <= ring; y++) {
                vec2_t point;
                if (abs(x) != ring && abs(y) != ring) continue;
                point = MAKE(vec2_t, center->x + x * BOT_BUILD_GRID,
                             center->y + y * BOT_BUILD_GRID);
                if (!G_BotBuildSiteReachable(worker, &point)) continue;
                if (G_IssueBuildOrder(worker, class_id, &point)) return true;
            }
        }
    }
    return false;
}

/* common.ai has already bounded qty by resources; each accepted action still performs authoritative checks/payment. */
bool G_BotProduce(player_t *player, int32_t qty, uint32_t class_id, int32_t town_id) {
    uint32_t made = 0;
    if (!player || qty <= 0 || !class_id) return false;
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce request player=%u qty=%d id=%.4s town=%d\n",
        PLAYER_NUM(player), qty, (cstring_t)&class_id, town_id);
#endif
    while (qty-- > 0) {
        if (G_UnitIsBuilding(class_id)) {
            if (!G_BotBuildNearTown(player, class_id, town_id)) break;
            made++;
            break; /* common.ai retries deficits; one pending footprint at a time prevents overlapping reservations. */
        } else {
            edict_t *producer = NULL;
            FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
                !ent->construction.active && !ent->training && G_BotUnitAtTown(player, ent, town_id) &&
                G_GetTrainCommandState(G_GetPlayerClientByNumber(ent->s.player), ent, class_id, NULL, 0) ==
                    BUILD_COMMAND_AVAILABLE) { producer = ent; break; }
            if (!producer || !SP_TrainUnit(producer, class_id)) break;
        }
        made++;
    }
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce result id=%.4s made=%u\n", (cstring_t)&class_id, made);
#endif
    return made > 0;
}

/* SetUpgrade is a one-shot AI request. common.ai handles resource planning and retries;
 * the engine selects any completed owned producer whose ordinary research command is available. */
bool G_BotUpgrade(player_t *player, uint32_t upgrade_id) {
    gameClient_t *client;
    if (!player || !upgrade_id || !(client = PLAYER_CLIENT(player))) return false;
    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
        !ent->construction.active && !ent->training &&
        G_GetResearchCommandState(client, ent, upgrade_id, NULL, NULL, 0) == BUILD_COMMAND_AVAILABLE)
        if (G_QueueResearch(ent, upgrade_id)) return true;
    return false;
}

static bool G_BotHarvesting(edict_t *unit, returnResource_t resource) {
    abilityProc_t proc = resource == RETURN_RESOURCE_GOLD ? CAbilityGoldMine : CAbilityHarvest;
    return unit->currentmove && unit->currentmove->proc == proc;
}

/* A ClearHarvestAI pass preserves active jobs, then assigns each remaining worker once. */
void G_BotHarvest(player_t *player, int32_t town_id, int32_t peons, bool gold) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    returnResource_t resource = gold ? RETURN_RESOURCE_GOLD : RETURN_RESOURCE_LUMBER;
    edict_t *town, *target;
    if (!bot || peons <= 0 || !(town = G_BotTown(player, town_id)) ||
        !(target = G_BotHarvestTarget(player, town, resource))) return;
    FILTER_EDICTS(unit, peons > 0 && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        G_BotHarvesting(unit, resource) && !G_BotHarvesterReserved(bot, unit)) {
        G_BotReserveHarvester(bot, unit); peons--;
    }
    while (peons-- > 0) {
        edict_t *best = NULL;
        float best_dist = 0;
        /* Preserve accepted construction orders; harvest reassignment used to strand their pending footprints. */
        FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && !unit->training &&
            !unit->construction.active && !unit->build_project &&
            (!unit->currentmove || (unit->currentmove->proc != CAbilityGoldMine &&
             unit->currentmove->proc != CAbilityHarvest && unit->currentmove->proc != CAbilityRepair)) && unit->data.UnitAbilities &&
            G_ActorHasSkill(unit, "Ahar") && !G_BotHarvesterReserved(bot, unit)) {
            float dist = Vector2_distance(&town->s.origin2, &unit->s.origin2);
            if (!best || dist < best_dist) { best = unit; best_dist = dist; }
        }
        if (!best) return;
        G_BotReserveHarvester(bot, best);
        if (best->harvested_gold) harvest_gold_return_to(best, town);
        else if (best->harvested_lumber) harvest_lumber_return_to(best, town);
        else if (resource == RETURN_RESOURCE_GOLD) harvest_gold_start(best, target);
        else harvest_start(best, target);
    }
}

/* Blizzard AI owns one assault and one defense captain; recreation drops all prior membership and orders. */
void G_BotCreateCaptains(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    G_BotClearCaptains(bot);
}

/* Captain members remain in TownCount, so common.ai adds this count when requesting their replacements. */
uint32_t G_BotIgnoredUnits(player_t *player, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    uint32_t count = 0;
    if (!bot) return 0;
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(edict_t *, unit, bot->captains[i].units)
        if (G_BotUnitAlive(*unit) && (*unit)->s.player == PLAYER_NUM(player) && (*unit)->class_id == class_id) count++;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards)
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == class_id) count++;
    return count;
}

/* Combat belongs to members, not formation state; validating each target also clears stale combat links. */
bool G_BotCaptainInCombat(player_t *player, bool attack) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return false;
    captain = bot->captains + (attack ? BOT_CAPTAIN_ATTACK : BOT_CAPTAIN_DEFENSE);
    FOR_EACH_ARRAY(edict_t *, unit, captain->units)
        if (G_BotUnitAlive(*unit) && unit_affectingcombat(*unit)) return true;
    return false;
}

/* common.ai repeatedly calls AttackMoveKill while its selected target lives.
 * Issue an attack-move for the current assault captain toward the target's
 * current position; the script's three-second loop refreshes moving targets.
 * This deliberately does not implement the lower-confidence retail minimap
 * signal or post-kill return-home behavior. */
void G_BotAttackMoveKill(player_t *player, edict_t *target) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    edict_t *waypoint;
    bool any = false;

    if (!bot || !G_BotUnitAlive(target)) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        if (G_BotUnitAlive(*member)) { any = true; break; }
    if (!any) return;

    captain->goal = target->s.origin2;
    captain->state = BOT_CAPTAIN_ACTIVE;
    waypoint = Waypoint_add(&captain->goal);
    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        if (G_BotUnitAlive(*member)) order_attackmove(*member, waypoint);
}

static bool G_BotCaptainHasUnit(bot_t *bot, edict_t *unit) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(edict_t *, member, bot->captains[i].units)
        if (*member == unit) return true;
    return false;
}

/* Script formation retries rebuild only the assault roster; the defense captain remains independent. */
void G_BotInitAssault(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    if (captain->units) gi.MemFree(captain->units);
    memset(captain, 0, sizeof(*captain)); captain->state = BOT_CAPTAIN_FORMING;
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI assault init player=%u\n", PLAYER_NUM(player));
#endif
}

static void G_BotCaptainAdd(botCaptain_t *captain, edict_t *unit) {
    uint32_t count = ARRAY_COUNT(captain->units);
    edict_t * *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, captain->units, count * sizeof(*units));
    if (captain->units) gi.MemFree(captain->units);
    captain->units = units; ARRAY_COUNT(captain->units) = count + 1; captain->units[count] = unit;
}

/* Production is requested by common.ai; roster fills never steal units assigned to the other captain. */
static bool G_BotCaptainFill(player_t *player, botCaptainType_t type, int32_t qty, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    int32_t have = 0;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    captain = bot->captains + type;
    FOR_EACH_ARRAY(edict_t *, unit, captain->units)
        if (G_BotUnitAlive(*unit) && (*unit)->class_id == class_id) have++;
    FILTER_EDICTS(unit, have < qty && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && !unit->construction.active && !unit->training && !G_BotCaptainHasUnit(bot, unit)) {
        G_BotCaptainAdd(captain, unit); have++;
    }
    return have >= qty;
}

bool G_BotAddAssault(player_t *player, int32_t qty, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool ready;
    if (bot && qty > 0 && class_id) bot->captains[BOT_CAPTAIN_ATTACK].desired += qty;
    ready = G_BotCaptainFill(player, BOT_CAPTAIN_ATTACK, qty, class_id);
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI assault add player=%u qty=%d id=%.4s ready=%d size=%u desired=%d\n",
        player ? PLAYER_NUM(player) : MAX_PLAYERS, qty, (cstring_t)&class_id, ready,
        G_BotCaptainGroupSize(player), bot ? bot->captains[BOT_CAPTAIN_ATTACK].desired : 0);
#endif
    return ready;
}

uint32_t G_BotCaptainGroupSize(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    uint32_t count = 0;
    if (!bot) return 0;
    FOR_EACH_ARRAY(edict_t *, unit, bot->captains[BOT_CAPTAIN_ATTACK].units)
        if (G_BotUnitAlive(*unit)) count++;
    return count;
}

bool G_BotCaptainIsFull(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && G_BotCaptainGroupSize(player) >= bot->captains[BOT_CAPTAIN_ATTACK].desired;
}

static bool G_BotUnitInjured(edict_t const *unit) {
    return unit && unit->health.max_value > 0.0f &&
        unit->health.value < unit->health.max_value * BOT_INJURED_HEALTH_FRACTION;
}

/* RemoveInjuries operates on the assault captain before a new melee wave is formed.
 * Retail AI Editor documentation defines injured as below 50% life and describes
 * these units as being sent home (or to a healing fountain). OpenRealm does not yet
 * have a recovered healing-site chooser, so use the player's primary gold-dropoff
 * as the deterministic retreat point and keep that destination policy isolated here. */
void G_BotRemoveInjuries(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    edict_t *town;
    uint32_t write = 0;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    town = G_BotTown(player, 0);
    FOR_LOOP(read, ARRAY_COUNT(captain->units)) {
        edict_t *unit = captain->units[read];
        if (!G_BotUnitAlive(unit)) continue;
        if (G_BotUnitInjured(unit)) {
            /* BZ_COMPAT_GUESS: retail may prefer a nearby Fountain of Health or a
             * captain-home point. Until that selector exists, returning to the main
             * town preserves the documented "send injured units back to base" rule. */
            if (town) order_move(unit, Waypoint_add(&town->s.origin2));
            continue;
        }
        captain->units[write++] = unit;
    }
    ARRAY_COUNT(captain->units) = write;
}

static bool G_BotUnitSiege(edict_t const *unit) {
    if (!unit) return false;
    /* BZ_COMPAT_GUESS: common.ai exposes RemoveSiege but Blizzard does not document
     * the native's internal classifier. Stock WC3 siege engines author their combat
     * profile with the siege attack type, so use either active attack slot's authored
     * ATK_SIEGE value rather than a hard-coded unit rawcode list. */
    return unit->attack1.type == ATK_SIEGE || unit->attack2.type == ATK_SIEGE;
}

/* InitMeleeGroup calls RemoveSiege before building the next assault specification.
 * Keep this native limited to attack-captain roster cleanup: siege units remain live,
 * owned world entities and may be managed separately by the AI's artillery policy. */
void G_BotRemoveSiege(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    uint32_t write = 0;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    FOR_LOOP(read, ARRAY_COUNT(captain->units)) {
        edict_t *unit = captain->units[read];
        if (!G_BotUnitAlive(unit) || G_BotUnitSiege(unit)) continue;
        captain->units[write++] = unit;
    }
    ARRAY_COUNT(captain->units) = write;
}

/* Blizzard scores heroes and ordinary units separately so one healthy category cannot hide the other's losses. */
int32_t G_BotCaptainReadiness(player_t *player, bool mana) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    float cur[2] = {0}, max[2] = {0};
    if (!bot) return 100;
    FOR_EACH_ARRAY(edict_t *, unit, bot->captains[BOT_CAPTAIN_ATTACK].units) {
        uint32_t hero;
        if (!G_BotUnitAlive(*unit)) continue;
        hero = G_UnitIsHero(*unit) ? 1 : 0;
        cur[hero] += mana ? (*unit)->mana.value : (*unit)->health.value;
        max[hero] += mana ? (*unit)->mana.max_value : (*unit)->health.max_value;
    }
    /* The original fixed-real divider defines equal operands, including 0/0, as 1.0. */
    FOR_LOOP(i, 2) cur[i] = cur[i] == max[i] ? 100.0f : cur[i] * 100.0f / max[i];
    return (int32_t)MIN(cur[0], cur[1]);
}

bool G_BotAddDefenders(player_t *player, int32_t qty, uint32_t class_id) {
    return G_BotCaptainFill(player, BOT_CAPTAIN_DEFENSE, qty, class_id);
}

void G_BotAddGuardPost(player_t *player, uint32_t class_id, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botGuardPost_t *guards;
    uint32_t count;
    if (!bot || !class_id) return;
    count = ARRAY_COUNT(bot->guards); guards = gi.MemAlloc((count + 1) * sizeof(*guards));
    if (count) memcpy(guards, bot->guards, count * sizeof(*guards));
    if (bot->guards) gi.MemFree(bot->guards);
    bot->guards = guards; ARRAY_COUNT(bot->guards) = count + 1;
    bot->guards[count] = MAKE(botGuardPost_t, .class_id = class_id, .origin = MAKE(vec2_t, x, y),
                                   .replacements_used = 0);
}

static bool G_BotGuardHasUnit(bot_t *bot, edict_t *unit) {
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) if (post->unit == unit) return true;
    return false;
}

static bool G_BotGuardReplacementTraining(player_t *player, uint32_t class_id) {
    if (!player || !class_id) return false;
    FILTER_EDICTS(unit, unit->inuse && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && unit->training) return true;
    return false;
}

/* Guard posts reserve ordinary completed units independently from the two captain rosters.
 * If a vacant post has exhausted the spare pool, FillGuardPosts requests one ordinary trained
 * replacement at a time and consumes the per-post replacement budget only after queue acceptance. */
void G_BotFillGuardPosts(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == post->class_id) continue;
        post->unit = NULL;
        FILTER_EDICTS(unit, !post->unit && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
            unit->class_id == post->class_id && !unit->construction.active && !unit->training &&
            !G_BotCaptainHasUnit(bot, unit) && !G_BotGuardHasUnit(bot, unit)) post->unit = unit;
        if (post->unit) { post->replacement_pending = false; continue; }
        if (post->replacement_pending) {
            if (G_BotGuardReplacementTraining(player, post->class_id)) continue;
            post->replacement_pending = false;
        }
        if (post->replacements_used < bot->replacement_count && G_BotProduce(player, 1, post->class_id, -1)) {
            post->replacement_pending = true;
            post->replacements_used++;
        }
    }
}

/* A fighting guard keeps its combat target; an idle guard outside its post radius walks home. */
void G_BotReturnGuardPosts(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (!G_BotUnitAlive(post->unit)) { post->unit = NULL; continue; }
        /* Retail trains Hero guard replacements but does not send Heroes back to the authored guard point. */
        if (G_UnitIsHero(post->unit)) continue;
        if (!unit_affectingcombat(post->unit) && Vector2_distance(&post->unit->s.origin2, &post->origin) > BOT_GUARD_RETURN_RANGE)
            order_move(post->unit, Waypoint_add(&post->origin));
    }
}

/* common.ai captain selectors are script constants: ATTACK_CAPTAIN=1, DEFENSE_CAPTAIN=2, BOTH_CAPTAINS=3. */
void G_BotSetCaptainHome(player_t *player, int32_t which, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    vec2_t home;
    if (!bot) return;
    home = MAKE(vec2_t, x, y);
    if (which == 1 || which == 3) bot->captains[BOT_CAPTAIN_ATTACK].home = home;
    if (which == 2 || which == 3) bot->captains[BOT_CAPTAIN_DEFENSE].home = home;
}

void G_BotSetStagePoint(player_t *player, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    bot->stage = MAKE(vec2_t, x, y); bot->stage_valid = true;
}

void G_BotShiftTownSpot(player_t *player, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    bot->town_spot = MAKE(vec2_t, x, y);
    bot->town_spot_valid = true;
}

static bool G_BotIsHostile(player_t *player, edict_t *ent) {
    player_t *owner;
    if (!player || !ent) return false;
    if (ent->s.player == PLAYER_NUM(player)) return false;
    owner = G_GetPlayerByNumber(ent->s.player);
    return !G_GetPlayerAlliance(player, owner, ALLIANCE_PASSIVE);
}

/* Assault targeting stays inside ordinary combat validity: live units and
 * buildings, so waves never order attacks on items, waypoints, corpses, or
 * the attackers themselves. A negative target suicides against any hostile
 * owner; otherwise only the named player's forces qualify. */
static edict_t *G_BotAssaultTarget(player_t *player, edict_t *self, int32_t target) {
    edict_t *best = NULL;
    float best_dist = 0;
    if (!player || !G_BotUnitAlive(self)) return NULL;
    FILTER_EDICTS(ent, ent != self && ent->inuse && !(ent->svflags & (SVF_DEADMONSTER | SVF_NOCLIENT)) &&
        ent->health.value > 0 && ((ent->svflags & SVF_MONSTER) || G_UnitIsStructure(ent)) &&
        (target >= 0 ? ent->s.player == (uint32_t)target : G_BotIsHostile(player, ent))) {
        float dist = Vector2_distance(&self->s.origin2, &ent->s.origin2);
        if (!best || dist < best_dist) { best = ent; best_dist = dist; }
    }
    return best;
}

/* Send one assault member at the enemy, falling back to an attack-move toward
 * the staged point so waves keep moving when no target is visible yet. */
static void G_BotOrderAssaultMember(bot_t *bot, edict_t *unit, int32_t target) {
    edict_t *enemy = bot ? G_BotAssaultTarget(bot->player, unit, target) : NULL;
    if (enemy) { order_attack(unit, enemy); return; }
    if (bot && bot->stage_valid) order_attackmove(unit, Waypoint_add(&bot->stage));
}

/* SuicideUnit/SuicideUnitEx share one backend: fill the assault roster through
 * the ordinary AddAssault path, then send the requested type at the enemy.
 * Retail common.ai declares both natives void; the boolean reports roster
 * acceptance for tests, mirroring AddAssault. */
bool G_BotSuicideUnits(player_t *player, int32_t qty, uint32_t class_id, int32_t target) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool accepted;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    accepted = G_BotAddAssault(player, qty, class_id);
    FOR_EACH_ARRAY(edict_t *, member, bot->captains[BOT_CAPTAIN_ATTACK].units) {
        edict_t *unit = *member;
        if (G_BotUnitAlive(unit) && unit->class_id == class_id) G_BotOrderAssaultMember(bot, unit, target);
    }
    return accepted;
}

/* SuicidePlayer launches the formed assault captain at the named player and
 * reports whether the wave left. check_full holds the wave until the roster
 * reaches its requested size; without it an under-strength captain still
 * attacks so campaign scripts never stall on a missing full house. */
bool G_BotSuicidePlayer(player_t *player, uint32_t target, bool check_full) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    bool any = false;
    if (!bot) return false;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) if (G_BotUnitAlive(*member)) { any = true; break; }
    if (!any) return false;
    if (check_full && !G_BotCaptainIsFull(player)) return false;
    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        if (G_BotUnitAlive(*member)) G_BotOrderAssaultMember(bot, *member, (int32_t)target);
    captain->state = BOT_CAPTAIN_ACTIVE;
    if (bot->stage_valid) captain->goal = bot->stage;
    return true;
}

/* MergeUnits reports whether the requested fused count already stands as live,
 * completed, owned units. Automatic a+b->make merge orders are not implemented
 * yet, so a shortfall returns false and production (SetBuildUnit/Conversions)
 * remains responsible for supplying the fused type. */
bool G_BotMergeUnits(player_t *player, int32_t qty, uint32_t a, uint32_t b, uint32_t make) {
    int32_t have = 0;
    (void)a; (void)b;
    if (!player || qty <= 0 || !make) return qty <= 0;
    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
        ent->class_id == make && !ent->construction.active && !ent->training) have++;
    return have >= qty;
}

/* Find the no-target morph ability that turns this source unit into its authored
 * alternate unit. Stock common.ai uses ConvertUnits for Obsidian Statue ->
 * Destroyer, whose Aave ability is represented by the shared metamorphosis
 * handler. Keep lookup data-driven so custom source units can use the same AI
 * primitive without hard-coding uobs/Aave. */
static uint32_t G_BotConversionAbility(edict_t *unit, uint32_t *target_type) {
    if (target_type) *target_type = 0;
    if (!unit) return 0;

#define TRY_CONVERSION_ABILITY(alias_) do { \
        uint32_t const alias = (alias_); \
        abilityitem_t const item = S_AbilityItem(alias); \
        uint32_t const level = G_UnitAbilityLevel(unit, alias); \
        uint32_t const target = level ? S_SpellUnitId(alias, level) : 0; \
        if (level && item.ability && item.ability->proc == CAbilityMetamorphosis && \
            item.ability->target_type == SPELL_TARGET_NONE && target && target != unit->class_id) { \
            if (target_type) *target_type = target; \
            return alias; \
        } \
    } while (0)

    if (unit->data.UnitAbilities && unit->data.UnitAbilities->abilList) {
        PARSE_LIST(unit->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t alias = 0;
            if (strlen(token) != 4) continue;
            memcpy(&alias, token, 4);
            TRY_CONVERSION_ABILITY(alias);
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(unit->abilities.added)) {
        uint32_t const alias = unit->abilities.added[i];
        if (alias) TRY_CONVERSION_ABILITY(alias);
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        uint32_t const alias = unit->heroabilities[i].code;
        if (alias) TRY_CONVERSION_ABILITY(alias);
    }
#undef TRY_CONVERSION_ABILITY
    return 0;
}

/* Stock common.ai calls ConvertUnits(desire, OBS_STATUE) while satisfying a
 * desired Destroyer count. BZ_COMPAT_GUESS: interpret qty as the desired final
 * count of the authored conversion target, not simply the number of source
 * units to click. That avoids over-converting when some Destroyers already
 * exist and matches the surrounding Conversions(desire, unitid) helper.
 * Conversion itself uses the ordinary spell path so ability validation,
 * cooldown/mana, events, and in-place handle-preserving morph logic remain
 * authoritative. */
bool G_BotConvertUnits(player_t *player, int32_t qty, uint32_t source_type) {
    uint32_t player_num, ability = 0, target_type = 0;
    int32_t have = 0, needed;

    if (!player || qty <= 0 || !source_type) return qty <= 0;
    player_num = PLAYER_NUM(player);

    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == player_num && ent->class_id == source_type) {
        ability = G_BotConversionAbility(ent, &target_type);
        if (ability && target_type) break;
    }
    if (!ability || !target_type) return false;

    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == player_num &&
        ent->class_id == target_type && !ent->construction.active && !ent->training) have++;
    if (have >= qty) return true;
    needed = qty - have;

    FILTER_EDICTS(ent, needed > 0 && G_BotUnitAlive(ent) && ent->s.player == player_num &&
        ent->class_id == source_type) {
        uint32_t target = 0;
        uint32_t code = G_BotConversionAbility(ent, &target);
        if (!code || target != target_type) continue;
        if (S_CastNoTargetSpell(ent, code)) needed--;
    }

    return needed <= 0;
}

/* CommandAI is a per-player stack: GetLast* observes the newest command until PopLastCommand removes it. */
bool G_BotPushCommand(player_t *player, int32_t command, int32_t data) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCommand_t *commands;
    uint32_t count;
    if (!bot) return false;
    count = ARRAY_COUNT(bot->commands);
    commands = gi.MemAlloc((count + 1) * sizeof(*commands));
    if (count) memcpy(commands, bot->commands, count * sizeof(*commands));
    if (bot->commands) gi.MemFree(bot->commands);
    bot->commands = commands; ARRAY_COUNT(bot->commands) = count + 1;
    bot->commands[count] = MAKE(botCommand_t, command, data);
    return true;
}

uint32_t G_BotCommandsWaiting(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot ? ARRAY_COUNT(bot->commands) : 0;
}

int32_t G_BotLastCommand(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].command : 0;
}

int32_t G_BotLastData(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].data : 0;
}

void G_BotPopCommand(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (bot && ARRAY_COUNT(bot->commands)) ARRAY_COUNT(bot->commands)--;
}

static void G_BotApplyRepairToUnit(bot_t *bot, edict_t *unit) {
    bool enabled, active;
    if (!bot || !bot->player || !unit || !G_BotUnitAlive(unit) || unit->s.player != PLAYER_NUM(bot->player)) return;
    enabled = (bot->flags & BOT_PEONS_REPAIR) != 0;
    active = (unit->aiflags & AI_AUTOCAST_REPAIR) != 0;
    if (active != enabled && (enabled || active)) (void)S_SetRepairAutocast(unit, enabled);
}

static void G_BotApplyRepairPolicy(bot_t *bot) {
    if (!bot || !bot->player || !bot->repair_policy_dirty) return;
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(bot->player))
        G_BotApplyRepairToUnit(bot, unit);
    bot->repair_policy_dirty = false;
}

static void G_BotHeroChooseSkill(bot_t *bot, edict_t *hero) {
    int32_t skill = 0;
    if (!bot || !bot->vm || !bot->hero_levels || !hero || !G_BotUnitAlive(hero) ||
        hero->s.player != PLAYER_NUM(bot->player) || !G_UnitIsHero(hero) || !hero->hero.skillpoints) return;
    bot->hero_id = hero->class_id;
    bot->hero_level = hero->hero.level;
    if (jass_evaluateplayerinteger(bot->vm, bot->hero_levels, bot->player, &skill) && skill > 0)
        (void)G_HeroLearnSkill(hero, (uint32_t)skill);
    bot->hero_id = 0;
    bot->hero_level = 0;
}

void G_BotHeroLevelUp(edict_t *hero) {
    bot_t *bot = hero && hero->s.player < MAX_PLAYERS ? G_BotState(hero->s.player) : NULL;
    G_BotHeroChooseSkill(bot, hero);
}

void G_BotUnitReady(edict_t *unit) {
    bot_t *bot = unit && unit->s.player < MAX_PLAYERS ? G_BotState(unit->s.player) : NULL;
    G_BotApplyRepairToUnit(bot, unit);
    if (G_UnitIsHero(unit)) G_BotHeroChooseSkill(bot, unit);
}

/* AI script paths are normally basenames; preserve an explicit archive path when a map supplies one. */
static bool G_BotScriptPath(cstring_t script, string_t path, size_t size) {
    int len;
    if (!script || !*script) return false;
    len = strchr(script, '\\') || strchr(script, '/') ? snprintf(path, size, "%s", script) :
        snprintf(path, size, "Scripts\\%s", script);
    return len >= 0 && (size_t)len < size;
}

void G_BotStop(uint32_t player) {
    bot_t *bot = G_BotState(player);
    if (!bot) return;
    if (bot->vm) jass_close(bot->vm);
    G_BotClearCaptains(bot);
    if (bot->commands) gi.MemFree(bot->commands);
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    if (bot->guards) gi.MemFree(bot->guards);
    memset(bot, 0, sizeof(*bot));
}

/* Removal can originate inside the player's AI coroutine, so teardown waits until that resume returns. */
void G_BotRequestStop(uint32_t player) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) { bot->stop_requested = true; jass_haltevents(bot->vm); }
}

void G_BotShutdown(void) {
    FOR_LOOP(player, MAX_PLAYERS) G_BotStop(player);
}

/* Each bot gets a private JASS root because common.ai stores all policy state in globals. */
bool G_BotStart(player_t *player, cstring_t script, botMode_t mode) {
    bot_t *bot;
    char path[MAX_PATHLEN];
    uint32_t playernum;

    if (!player || !G_BotScriptPath(script, path, sizeof(path))) {
        fprintf(stderr, "WC3 AI: invalid player or script\n");
        return false;
    }
    playernum = PLAYER_NUM(player);
    bot = G_BotState(playernum);
    if (!bot) {
        fprintf(stderr, "WC3 AI: player %u is out of range\n", playernum);
        return false;
    }
    if (bot->vm && jass_isrunning(bot->vm)) {
        bot->restart_requested = true;
        bot->pending_mode = mode;
        strlcpy(bot->pending_script, path, sizeof(bot->pending_script));
        jass_haltevents(bot->vm);
        return true;
    }

    G_BotStop(playernum);
    /* AI VMs can start before map spawning, which previously left the shared JASS allocator unset. */
    G_InitJassHost();
    bot->vm = jass_newstate();
    bot->player = player;
    bot->mode = mode;
    bot->replacement_count = BOT_DEFAULT_REPLACEMENT_COUNT;
    strlcpy(bot->script, path, sizeof(bot->script));
    if (!jass_dofile(bot->vm, "Scripts\\common.j")) {
        fprintf(stderr, "WC3 AI: player %u could not load Scripts\\common.j\n", playernum);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_dofile(bot->vm, "Scripts\\common.ai")) {
        fprintf(stderr, "WC3 AI: player %u could not load Scripts\\common.ai\n", playernum);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_dofile(bot->vm, path)) {
        fprintf(stderr, "WC3 AI: player %u could not load %s\n", playernum, path);
        G_BotStop(playernum);
        return false;
    }
    if (!jass_startcoroutinebynameforplayer(bot->vm, "main", player)) {
        fprintf(stderr, "WC3 AI: player %u script %s has no main\n", playernum, path);
        G_BotStop(playernum);
        return false;
    }
    fprintf(stderr, "WC3 AI: player %u started %s\n", playernum, path);
    return true;
}

void G_BotPause(uint32_t player, bool paused) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) bot->paused = paused;
}

void G_BotRunFrame(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        bot_t *bot = level.bots + player;
        if (!bot->vm) continue;
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->paused) continue;
        G_BotApplyRepairPolicy(bot);
        jass_runevents(bot->vm);
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->restart_requested) {
            player_t *owner = bot->player;
            botMode_t mode = bot->pending_mode;
            char script[MAX_PATHLEN];
            strlcpy(script, bot->pending_script, sizeof(script));
            G_BotStop(player);
            G_BotStart(owner, script, mode);
            continue;
        }
        if (!jass_rterror_pending(bot->vm)) continue;
        fprintf(stderr, "WC3 AI: player %u script %s stopped: %s\n", player, bot->script,
            jass_rterror_message(bot->vm));
        G_BotStop(player);
    }
}
