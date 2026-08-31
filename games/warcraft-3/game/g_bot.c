#include "g_local.h"
#include "jass/jass.h"
#include "skills/s_skills.h"

#define BOT_GUARD_RETURN_RANGE 64.0f // world units; avoid resetting movement for guards already standing near their post

static bot_t *G_BotState(DWORD player) {
    return player < MAX_PLAYERS ? &level.bots[player] : NULL;
}

static void G_BotClearCaptains(bot_t *bot) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        if (bot->captains[i].units) gi.MemFree(bot->captains[i].units);
        memset(bot->captains + i, 0, sizeof(bot->captains[i]));
    }
}

/* KillUnit changes life immediately while ordinary death also carries SVF_DEADMONSTER. */
BOOL G_BotUnitAlive(LPEDICT unit) {
    return unit && unit->inuse && unit->health.value > 0 && !(unit->svflags & SVF_DEADMONSTER);
}

/* Stop only active gather orders; carried resources remain available for an explicit return order. */
void G_BotStopGathering(LPPLAYER player) {
    if (!player) return;
    FILTER_EDICTS(unit, unit->inuse && unit->s.player == PLAYER_NUM(player) && unit->currentmove &&
        (unit->currentmove->ability == &a_harvest || unit->currentmove->ability == &a_goldmine ||
         unit->currentmove->ability == &a_wisp_harvest)) {
        S_GoldMineReleaseWorker(unit);
        order_stop(unit);
    }
}

static BOOL G_BotHarvesterReserved(bot_t *bot, LPEDICT unit) {
    FOR_EACH_ARRAY(LPEDICT, assigned, bot->harvesters) if (*assigned == unit) return true;
    return false;
}

static void G_BotReserveHarvester(bot_t *bot, LPEDICT unit) {
    DWORD count = ARRAY_COUNT(bot->harvesters);
    LPEDICT *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, bot->harvesters, count * sizeof(*units));
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = units; ARRAY_COUNT(bot->harvesters) = count + 1; bot->harvesters[count] = unit;
}

void G_BotClearHarvest(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    if (bot->harvesters) gi.MemFree(bot->harvesters);
    bot->harvesters = NULL; ARRAY_COUNT(bot->harvesters) = 0;
}

/* Town IDs enumerate owned gold drop-offs in spawn order, matching the expansion index used by common.ai. */
static LPEDICT G_BotHarvestTown(LPPLAYER player, LONG town) {
    edict_t probe = {0};
    if (!player || town < 0) return NULL;
    probe.s.player = PLAYER_NUM(player);
    FILTER_EDICTS(ent, S_CanReturnResourceAt(&probe, ent, RETURN_RESOURCE_GOLD))
        if (!town--) return ent;
    return NULL;
}

static LPEDICT G_BotHarvestTarget(LPEDICT town, returnResource_t resource) {
    LPEDICT best = NULL;
    FLOAT best_dist = 0;
    FILTER_EDICTS(ent, resource == RETURN_RESOURCE_GOLD ? S_GoldMineCanHarvest(ent) :
        ent->inuse && ent->targtype == TARG_TREE && !M_IsDead(ent)) {
        FLOAT dist = Vector2_distance(&town->s.origin2, &ent->s.origin2);
        if (!best || dist < best_dist) { best = ent; best_dist = dist; }
    }
    return best;
}

/* A ClearHarvestAI pass assigns each eligible worker once across all town/resource requests. */
void G_BotHarvest(LPPLAYER player, LONG town_id, LONG peons, BOOL gold) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    returnResource_t resource = gold ? RETURN_RESOURCE_GOLD : RETURN_RESOURCE_LUMBER;
    LPEDICT town, target;
    if (!bot || peons <= 0 || !(town = G_BotHarvestTown(player, town_id)) || !(target = G_BotHarvestTarget(town, resource))) return;
    while (peons-- > 0) {
        LPEDICT best = NULL;
        FLOAT best_dist = 0;
        FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && unit->UnitAbilities &&
            G_ActorHasSkill(unit, "Ahar") && !G_BotHarvesterReserved(bot, unit)) {
            FLOAT dist = Vector2_distance(&town->s.origin2, &unit->s.origin2);
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
void G_BotCreateCaptains(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    G_BotClearCaptains(bot);
}

/* Captain members remain in TownCount, so common.ai adds this count when requesting their replacements. */
DWORD G_BotIgnoredUnits(LPPLAYER player, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    DWORD count = 0;
    if (!bot) return 0;
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(LPEDICT, unit, bot->captains[i].units)
        if (G_BotUnitAlive(*unit) && (*unit)->s.player == PLAYER_NUM(player) && (*unit)->class_id == class_id) count++;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards)
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == class_id) count++;
    return count;
}

/* Combat belongs to members, not formation state; validating each target also clears stale combat links. */
BOOL G_BotCaptainInCombat(LPPLAYER player, BOOL attack) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return false;
    captain = bot->captains + (attack ? BOT_CAPTAIN_ATTACK : BOT_CAPTAIN_DEFENSE);
    FOR_EACH_ARRAY(LPEDICT, unit, captain->units)
        if (G_BotUnitAlive(*unit) && unit_affectingcombat(*unit)) return true;
    return false;
}

static BOOL G_BotCaptainHasUnit(bot_t *bot, LPEDICT unit) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) FOR_EACH_ARRAY(LPEDICT, member, bot->captains[i].units)
        if (*member == unit) return true;
    return false;
}

/* Script formation retries rebuild only the assault roster; the defense captain remains independent. */
void G_BotInitAssault(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    if (captain->units) gi.MemFree(captain->units);
    memset(captain, 0, sizeof(*captain)); captain->state = BOT_CAPTAIN_FORMING;
}

static void G_BotCaptainAdd(botCaptain_t *captain, LPEDICT unit) {
    DWORD count = ARRAY_COUNT(captain->units);
    LPEDICT *units = gi.MemAlloc((count + 1) * sizeof(*units));
    if (count) memcpy(units, captain->units, count * sizeof(*units));
    if (captain->units) gi.MemFree(captain->units);
    captain->units = units; ARRAY_COUNT(captain->units) = count + 1; captain->units[count] = unit;
}

/* Production is requested by common.ai; roster fills never steal units assigned to the other captain. */
static BOOL G_BotCaptainFill(LPPLAYER player, botCaptainType_t type, LONG qty, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    LONG have = 0;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    captain = bot->captains + type;
    FOR_EACH_ARRAY(LPEDICT, unit, captain->units)
        if (G_BotUnitAlive(*unit) && (*unit)->class_id == class_id) have++;
    FILTER_EDICTS(unit, have < qty && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && !unit->construction.active && !unit->training && !G_BotCaptainHasUnit(bot, unit)) {
        G_BotCaptainAdd(captain, unit); have++;
    }
    return have >= qty;
}

BOOL G_BotAddAssault(LPPLAYER player, LONG qty, DWORD class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (bot && qty > 0 && class_id) bot->captains[BOT_CAPTAIN_ATTACK].desired += qty;
    return G_BotCaptainFill(player, BOT_CAPTAIN_ATTACK, qty, class_id);
}

DWORD G_BotCaptainGroupSize(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    DWORD count = 0;
    if (!bot) return 0;
    FOR_EACH_ARRAY(LPEDICT, unit, bot->captains[BOT_CAPTAIN_ATTACK].units)
        if (G_BotUnitAlive(*unit)) count++;
    return count;
}

BOOL G_BotCaptainIsFull(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && G_BotCaptainGroupSize(player) >= bot->captains[BOT_CAPTAIN_ATTACK].desired;
}

BOOL G_BotAddDefenders(LPPLAYER player, LONG qty, DWORD class_id) {
    return G_BotCaptainFill(player, BOT_CAPTAIN_DEFENSE, qty, class_id);
}

void G_BotAddGuardPost(LPPLAYER player, DWORD class_id, FLOAT x, FLOAT y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botGuardPost_t *guards;
    DWORD count;
    if (!bot || !class_id) return;
    count = ARRAY_COUNT(bot->guards); guards = gi.MemAlloc((count + 1) * sizeof(*guards));
    if (count) memcpy(guards, bot->guards, count * sizeof(*guards));
    if (bot->guards) gi.MemFree(bot->guards);
    bot->guards = guards; ARRAY_COUNT(bot->guards) = count + 1;
    bot->guards[count] = MAKE(botGuardPost_t, class_id, MAKE(VECTOR2, x, y), NULL);
}

static BOOL G_BotGuardHasUnit(bot_t *bot, LPEDICT unit) {
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) if (post->unit == unit) return true;
    return false;
}

/* Guard posts reserve ordinary completed units independently from the two captain rosters. */
void G_BotFillGuardPosts(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (G_BotUnitAlive(post->unit) && post->unit->s.player == PLAYER_NUM(player) && post->unit->class_id == post->class_id) continue;
        post->unit = NULL;
        FILTER_EDICTS(unit, !post->unit && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
            unit->class_id == post->class_id && !unit->construction.active && !unit->training &&
            !G_BotCaptainHasUnit(bot, unit) && !G_BotGuardHasUnit(bot, unit)) post->unit = unit;
    }
}

/* A fighting guard keeps its combat target; an idle guard outside its post radius walks home. */
void G_BotReturnGuardPosts(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_EACH_ARRAY(botGuardPost_t, post, bot->guards) {
        if (!G_BotUnitAlive(post->unit)) { post->unit = NULL; continue; }
        if (!unit_affectingcombat(post->unit) && Vector2_distance(&post->unit->s.origin2, &post->origin) > BOT_GUARD_RETURN_RANGE)
            order_move(post->unit, Waypoint_add(&post->origin));
    }
}

/* CommandAI is a per-player stack: GetLast* observes the newest command until PopLastCommand removes it. */
BOOL G_BotPushCommand(LPPLAYER player, LONG command, LONG data) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCommand_t *commands;
    DWORD count;
    if (!bot) return false;
    count = ARRAY_COUNT(bot->commands);
    commands = gi.MemAlloc((count + 1) * sizeof(*commands));
    if (count) memcpy(commands, bot->commands, count * sizeof(*commands));
    if (bot->commands) gi.MemFree(bot->commands);
    bot->commands = commands; ARRAY_COUNT(bot->commands) = count + 1;
    bot->commands[count] = MAKE(botCommand_t, command, data);
    return true;
}

DWORD G_BotCommandsWaiting(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot ? ARRAY_COUNT(bot->commands) : 0;
}

LONG G_BotLastCommand(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].command : 0;
}

LONG G_BotLastData(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && ARRAY_COUNT(bot->commands) ? bot->commands[ARRAY_COUNT(bot->commands) - 1].data : 0;
}

void G_BotPopCommand(LPPLAYER player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (bot && ARRAY_COUNT(bot->commands)) ARRAY_COUNT(bot->commands)--;
}

/* AI script paths are normally basenames; preserve an explicit archive path when a map supplies one. */
static BOOL G_BotScriptPath(LPCSTR script, LPSTR path, size_t size) {
    int len;
    if (!script || !*script) return false;
    len = strchr(script, '\\') || strchr(script, '/') ? snprintf(path, size, "%s", script) :
        snprintf(path, size, "Scripts\\%s", script);
    return len >= 0 && (size_t)len < size;
}

void G_BotStop(DWORD player) {
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
void G_BotRequestStop(DWORD player) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) { bot->stop_requested = true; jass_haltevents(bot->vm); }
}

void G_BotShutdown(void) {
    FOR_LOOP(player, MAX_PLAYERS) G_BotStop(player);
}

/* Each bot gets a private JASS root because common.ai stores all policy state in globals. */
BOOL G_BotStart(LPPLAYER player, LPCSTR script, botMode_t mode) {
    bot_t *bot;
    char path[MAX_PATHLEN];
    DWORD playernum;

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

void G_BotPause(DWORD player, BOOL paused) {
    bot_t *bot = G_BotState(player);
    if (bot && bot->vm) bot->paused = paused;
}

void G_BotRunFrame(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        bot_t *bot = level.bots + player;
        if (!bot->vm) continue;
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->paused) continue;
        jass_runevents(bot->vm);
        if (bot->stop_requested) { G_BotStop(player); continue; }
        if (bot->restart_requested) {
            LPPLAYER owner = bot->player;
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
