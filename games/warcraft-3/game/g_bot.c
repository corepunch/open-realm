#include "g_local.h"
#include "jass/jass.h"
#include "skills/s_skills.h"
#include <stdarg.h>

#define BOT_GUARD_RETURN_RANGE 82.006f // world units; avoid resetting movement for guards already standing near their post
#define BOT_BUILD_GRID 32.0f // world units; WC3 structures snap to this placement-cell interval
#define BOT_BUILD_SEARCH_RINGS 32 // 32-unit grid rings; searches 1024 world units around a town for legal placement
#define BOT_INJURED_HEALTH_FRACTION 0.5f // retail AI Editor: units below 50% life are treated as injured
#define BOT_TOWERED_BASE_RADIUS 1024.0f // world units; BZ_COMPAT_GUESS: exact retail IsTowered base-proximity radius is unknown
#define BOT_TOWERED_GUARD_RADIUS 1024.0f // world units; BZ_COMPAT_GUESS: exact retail IsTowered tower-proximity radius is unknown
#define BOT_MEGA_DEFENDER_RADIUS 1200.0f // world units; BZ_COMPAT_GUESS: exact retail mega-target protection radius is unknown
#define BOT_CREEP_CAMP_RADIUS 600.0f // world units; BZ_COMPAT_GUESS: exact retail creep-camp grouping radius is unknown
#define BOT_ENEMY_BASE_SEARCH_DELAY_MS 1000u // milliseconds; BZ_COMPAT_GUESS: exact retail asynchronous discovery latency is unknown
#define BOT_DEFAULT_REPLACEMENT_COUNT 3
#define BOT_GROUP_FLEE_ENEMY_RADIUS 1200.0f // world units; BZ_COMPAT_GUESS: exact retail local-force comparison radius is unknown
#define BOT_GROUP_FLEE_POWER_RATIO 1.5f // BZ_COMPAT_GUESS: exact retail disadvantage threshold is unknown
#define BOT_GROUP_FLEE_PERSIST_MS 2500u // milliseconds; BZ_COMPAT_GUESS: exact retail losing-battle persistence is unknown
#define BOT_GROUP_FLEE_HOME_RADIUS 128.0f // world units; BZ_COMPAT_GUESS: exact captain home-arrival tolerance is unknown
#define BOT_INDIVIDUAL_FLEE_SCAN_MS 250u // BZ_COMPAT_GUESS: retail reevaluation cadence is unknown
#define BOT_INDIVIDUAL_FLEE_HEALTH_FRACTION 0.30f // BZ_COMPAT_GUESS: retail damage threshold is unknown
#define BOT_INDIVIDUAL_FLEE_DANGER_RADIUS 800.0f // BZ_COMPAT_GUESS: hostile proximity radius is unknown
#define BOT_HERO_ITEM_SCAN_MS 1000u // BZ_COMPAT_GUESS: retail pickup cadence is unknown
#define BOT_HERO_ITEM_RADIUS 600.0f // BZ_COMPAT_GUESS: retail ground-item radius is unknown
#define BOT_HERO_BUY_SCAN_MS 2000u // BZ_COMPAT_GUESS: retail shop-purchase cadence is unknown
#define BOT_DEFEND_PLAYER_SCAN_MS 500u // BZ_COMPAT_GUESS: retail defense response cadence is unknown

#ifdef WC3_TRACE_AI
#define BOT_TRACE_WAIT_INTERVAL_MS 10000u
#define BOT_TRACE_ASSAULT_MOVE_INTERVAL_MS 10000u
#define BOT_TRACE_ASSAULT_MOVE_EPSILON 8.0f

typedef struct botTraceAssaultMove_s {
    edict_t *unit;
    uint32_t player, last_report;
    vec2_t last_position, start_position;
    bool terminal_reported;
} botTraceAssaultMove_t;

static botTraceAssaultMove_t bot_trace_assault_moves[MAX_ENTITIES];

typedef struct botTraceWait_s {
    struct botTraceWait_s *next;
    handle_t coroutine;
    uint32_t yielded_at, last_report, wake_time;
    bool waiting;
    char initial_function[96], function[96], callchain[512];
} botTraceWait_t;

static botTraceWait_t *bot_trace_waits[MAX_PLAYERS];
#endif

static bot_t *G_BotState(uint32_t player) {
    return player < MAX_PLAYERS ? &level.bots[player] : NULL;
}

#ifdef WC3_TRACE_AI
static int32_t G_BotTracePlayer(jass_t *vm) {
    jass_t *root;
    if (!vm) return -1;
    root = jass_getroot(vm);
    FOR_LOOP(player, MAX_PLAYERS)
        if (level.bots[player].vm == root) return (int32_t)player;
    return -1;
}
#endif

void G_BotTrace(uint32_t player, jass_t *vm, cstring_t event, cstring_t format, ...) {
#ifdef WC3_TRACE_AI
    bot_t *bot = G_BotState(player);
    char detail[512] = "", callchain[512] = "(unavailable)";
    cstring_t function = "(engine)";
    va_list args;

    if (!bot || !bot->vm || (vm && jass_getroot(vm) != bot->vm)) return;
    if (format) {
        va_start(args, format);
        vsnprintf(detail, sizeof(detail), format, args);
        va_end(args);
    }
    if (vm) {
        cstring_t current = jass_currentfunctionname(vm);
        function = current && *current ? current : "(unknown)";
        jass_formatcallchain(vm, callchain, sizeof(callchain));
        if (!callchain[0]) strlcpy(callchain, "(empty)", sizeof(callchain));
    }
    fprintf(stderr,
            "WC3_AI_TRACE time=%u player=%u script=\"%s\" event=%s function=\"%s\" callchain=\"%s\"%s%s\n",
            (unsigned)G_Time(), player, bot->script, event ? event : "event", function,
            callchain, detail[0] ? " " : "", detail);
#else
    (void)player; (void)vm; (void)event; (void)format;
#endif
}

void G_BotTraceAssaultUnit(edict_t *unit, cstring_t event, cstring_t format, ...) {
#ifdef WC3_TRACE_AI
    botTraceAssaultMove_t *trace;
    char detail[512];
    va_list args;
    if (!unit || unit->s.number >= MAX_ENTITIES) return;
    trace = bot_trace_assault_moves + unit->s.number;
    if (trace->unit != unit || trace->player >= MAX_PLAYERS) return;
    detail[0] = '\0';
    if (format) {
        va_start(args, format);
        vsnprintf(detail, sizeof(detail), format, args);
        va_end(args);
    }
    G_BotTrace(trace->player, NULL, event, "%s", detail);
#else
    (void)unit; (void)event; (void)format;
#endif
}

void G_BotTraceCoroutine(jass_t *vm, handle_t coroutine, cstring_t function,
                         cstring_t phase, uint32_t now, uint32_t wake_time,
                         bool yielded, bool done) {
#ifdef WC3_TRACE_AI
    int32_t player = G_BotTracePlayer(vm);
    botTraceWait_t **link, *wait;
    if (player < 0 || !coroutine) return;
    link = bot_trace_waits + player;
    while (*link && (*link)->coroutine != coroutine) link = &(*link)->next;
    wait = *link;

    if (phase && !strcmp(phase, "resume")) {
        char callchain[512];
        uint32_t slept_ms = wait && wait->waiting && now >= wait->yielded_at ? now - wait->yielded_at : 0;
        uint32_t requested_ms = wait && wait->waiting ? wait->wake_time - wait->yielded_at : 0;
        if (!wait) {
            wait = gi.MemAlloc(sizeof(*wait));
            if (!wait) return;
            memset(wait, 0, sizeof(*wait));
            wait->coroutine = coroutine;
            wait->next = bot_trace_waits[player];
            bot_trace_waits[player] = wait;
        }
        if (!wait->initial_function[0])
            strlcpy(wait->initial_function, function && *function ? function : "(unknown)",
                    sizeof(wait->initial_function));
        strlcpy(wait->function, function && *function ? function : "(unknown)", sizeof(wait->function));
        jass_formatcallchain(vm, callchain, sizeof(callchain));
        strlcpy(wait->callchain, callchain[0] ? callchain : "(empty)", sizeof(wait->callchain));
        if (requested_ms)
            fprintf(stderr,
                    "WC3_AI_TRACE time=%u player=%d script=\"%s\" event=coroutine_resumed function=\"%s\" callchain=\"%s\" requested_ms=%u actual_ms=%u deadline=%u\n",
                    (unsigned)now, player, level.bots[player].script, wait->function,
                    wait->callchain, requested_ms, slept_ms, (unsigned)wait->wake_time);
        wait->waiting = false;
        return;
    }
    if (phase && !strcmp(phase, "yield") && yielded && !done) {
        char callchain[512];
        if (!wait) {
            wait = gi.MemAlloc(sizeof(*wait));
            if (!wait) return;
            memset(wait, 0, sizeof(*wait));
            wait->coroutine = coroutine;
            wait->next = bot_trace_waits[player];
            bot_trace_waits[player] = wait;
        }
        wait->waiting = true;
        wait->yielded_at = wait->last_report = now;
        wait->wake_time = wake_time;
        strlcpy(wait->function, function && *function ? function : "(unknown)", sizeof(wait->function));
        jass_formatcallchain(vm, callchain, sizeof(callchain));
        strlcpy(wait->callchain, callchain[0] ? callchain : "(empty)", sizeof(wait->callchain));
        fprintf(stderr,
                "WC3_AI_TRACE time=%u player=%d script=\"%s\" event=coroutine_yield function=\"%s\" callchain=\"%s\" requested_ms=%u deadline=%u\n",
                (unsigned)now, player, level.bots[player].script, wait->function,
                wait->callchain, (unsigned)(wake_time - now), (unsigned)wake_time);
        return;
    }
    if (done || (phase && !strcmp(phase, "done"))) {
        if (wait && !strcmp(wait->initial_function, "main"))
            fprintf(stderr,
                    "WC3_AI_TRACE time=%u player=%d script=\"%s\" event=script_main_done function=\"%s\" callchain=\"%s\"\n",
                    (unsigned)now, player, level.bots[player].script, wait->function, wait->callchain);
        if (wait) { *link = wait->next; gi.MemFree(wait); }
    } else if (!yielded || (phase && !strcmp(phase, "return"))) {
        if (wait) { *link = wait->next; gi.MemFree(wait); }
    }
#else
    (void)vm; (void)coroutine; (void)function; (void)phase;
    (void)now; (void)wake_time; (void)yielded; (void)done;
#endif
}

void G_BotTraceWaits(uint32_t player) {
#ifdef WC3_TRACE_AI
    bot_t *bot = G_BotState(player);
    uint32_t now = G_Time();
    if (!bot || !bot->vm || bot->paused) return;
    for (botTraceWait_t *wait = bot_trace_waits[player]; wait; wait = wait->next) {
        uint32_t remaining;
        if (!wait->waiting) continue;
        if ((uint32_t)(now - wait->last_report) < BOT_TRACE_WAIT_INTERVAL_MS) continue;
        wait->last_report = now;
        remaining = wait->wake_time > now ? wait->wake_time - now : 0;
        fprintf(stderr,
                "WC3_AI_TRACE time=%u player=%u script=\"%s\" event=waiting coroutine=%p function=\"%s\" callchain=\"%s\" waited_ms=%u wake_in_ms=%u\n",
                (unsigned)now, player, bot->script, wait->coroutine, wait->function, wait->callchain,
                (unsigned)(now - wait->yielded_at), (unsigned)remaining);
    }
#else
    (void)player;
#endif
}

static void G_BotTraceClearWaits(uint32_t player) {
#ifdef WC3_TRACE_AI
    botTraceWait_t *wait = bot_trace_waits[player];
    while (wait) {
        botTraceWait_t *next = wait->next;
        gi.MemFree(wait);
        wait = next;
    }
    bot_trace_waits[player] = NULL;
#else
    (void)player;
#endif
}

static bool G_BotBuildSiteReachable(edict_t *, vec2_t const *);
static bool G_BotIsHostile(player_t *, edict_t *);

static void G_BotClearCaptains(bot_t *bot) {
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        if (bot->captains[i].units) gi.MemFree(bot->captains[i].units);
        if (bot->captains[i].routes) gi.MemFree(bot->captains[i].routes);
        memset(bot->captains + i, 0, sizeof(bot->captains[i]));
    }
}

/* KillUnit changes life immediately while ordinary death also carries SVF_DEADMONSTER. */
bool G_BotUnitAlive(edict_t *unit) {
    return unit && unit->inuse && unit->health.value > 0 && !(unit->svflags & SVF_DEADMONSTER);
}

/* common.ai uses TownThreatened as a global "is anything I own under attack?" gate
 * before launching or refreshing an offensive wave. Despite the native name, retail
 * documentation says any owned unit or building qualifies. BZ_COMPAT_GUESS: retail
 * does not expose the exact threatened-state latch lifetime, so use authoritative active
 * attack behavior instead of inventing a town radius or recent-damage timeout: a hostile
 * live attacker must currently be executing CAbilityAttack against a live owned unit. */
bool G_BotTownThreatened(player_t *player) {
    uint32_t owner;
    if (!player || (owner = PLAYER_NUM(player)) >= MAX_PLAYERS) return false;
    FILTER_EDICTS(attacker, G_BotUnitAlive(attacker) && attacker->currentmove &&
        attacker->currentmove->proc == CAbilityAttack && G_BotUnitAlive(attacker->goalentity) &&
        ((attacker->goalentity->svflags & SVF_MONSTER) || G_UnitIsStructure(attacker->goalentity)) &&
        attacker->goalentity->s.player == owner &&
        S_SpellIsEnemy(attacker, attacker->goalentity))
        return true;
    return false;
}

/* Retail-facing AI documentation says IsTowered(target) requires the target to be near a
 * base and guarded by a tower, while the tower's authored acquisition/attack ranges do
 * not affect the result. Custom towers are allowed, so classify by authoritative unit
 * properties rather than stock rawcodes. BZ_COMPAT_GUESS: Blizzard does not publish the
 * two internal proximity radii; keep both constants local to this query for replacement
 * after direct native capture. */
static bool G_BotTowerDefenseBuilding(edict_t *unit, uint32_t owner) {
    return G_BotUnitAlive(unit) && unit->s.player == owner && G_UnitIsBuilding(unit->class_id) &&
        ((unit->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 0)) ||
         (unit->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 1)));
}

bool G_BotIsTowered(player_t *player, edict_t *target) {
    player_t *defender;
    edict_t *town;
    uint32_t owner;
    bool near_base = false;

    (void)player; /* Query is about the target's defenses; caller identity does not classify the tower. */
    if (!G_BotUnitAlive(target) || target->s.player >= MAX_PLAYERS) return false;
    owner = target->s.player;
    defender = &game.clients[owner].ps;

    for (int32_t town_id = 0; (town = G_BotTown(defender, town_id)); town_id++) {
        if (Vector2_distance(&town->s.origin2, &target->s.origin2) <= BOT_TOWERED_BASE_RADIUS) {
            near_base = true;
            break;
        }
    }
    if (!near_base) return false;

    FILTER_EDICTS(tower, G_BotTowerDefenseBuilding(tower, owner) && tower != target &&
        Vector2_distance(&tower->s.origin2, &target->s.origin2) <= BOT_TOWERED_GUARD_RADIUS)
        return true;
    return false;
}

/* Stock melee AI gives GetMegaTarget priority over ordinary expansion/base discovery,
 * and contemporary analysis describes it as exploiting an enemy main base left
 * unprotected while defenders are elsewhere. SetWatchMegaTargets gates the engine-side
 * watch. Keep the vulnerable-base heuristic isolated here: retail does not publish the
 * exact defender radius, defender weighting, or arbitration when several mains qualify. */
static bool G_BotMegaCombatDefender(edict_t *unit, uint32_t owner, edict_t *hall) {
    if (!G_BotUnitAlive(unit) || unit == hall || unit->s.player != owner || G_UnitIsBuilding(unit->class_id)) return false;
    /* Workers remaining at an economy do not by themselves make the main base a defended
     * military position. Ahar is the stock/custom worker harvest command shared by melee workers. */
    if (G_ActorHasSkill(unit, "Ahar")) return false;
    return (unit->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 0)) ||
           (unit->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 1));
}

edict_t *G_BotGetMegaTarget(player_t *player) {
    bot_t *bot;
    edict_t *home, *best = NULL;
    float best_dist = 0.0f;
    uint32_t caller;

    if (!player || (caller = PLAYER_NUM(player)) >= MAX_PLAYERS) return NULL;
    bot = G_BotState(caller);
    if (!bot || !(bot->flags & BOT_WATCH_MEGA)) return NULL;
    home = G_BotTown(player, 0);

    FOR_LOOP(i, MAX_PLAYERS) {
        player_t *enemy;
        edict_t *hall;
        bool defended = false;
        float dist;

        if (i == caller) continue;
        enemy = &game.clients[i].ps;
        hall = G_BotTown(enemy, 0);
        if (!G_BotUnitAlive(hall) || !G_BotIsHostile(player, hall)) continue;
        if (G_BotIsTowered(player, hall)) continue;

        FILTER_EDICTS(unit, G_BotMegaCombatDefender(unit, i, hall) &&
            Vector2_distance(&unit->s.origin2, &hall->s.origin2) <= BOT_MEGA_DEFENDER_RADIUS) {
            defended = true;
            break;
        }
        if (defended) continue;

        /* BZ_COMPAT_GUESS: when several enemy mains are simultaneously vulnerable,
         * retail's arbitration is unknown. Prefer the one nearest our primary town;
         * without a town, stable player-slot order wins. */
        if (!home) return hall;
        dist = Vector2_distance(&home->s.origin2, &hall->s.origin2);
        if (!best || dist < best_dist) { best = hall; best_dist = dist; }
    }
    return best;
}

/* GetCreepCamp returns a representative unit from the nearest Neutral Hostile camp whose
 * total creep level lies in [min_power,max_power]. Retail-facing documentation defines camp
 * power as the sum of UnitBalance levels and optionally excludes camps containing flyers.
 * BZ_COMPAT_GUESS: Blizzard does not expose camp membership geometry; group Neutral Hostile
 * units by connected proximity using BOT_CREEP_CAMP_RADIUS, then choose the qualifying camp
 * nearest the caller's primary town (or first live owned unit when no town exists). */
static bool G_BotCreepFlyer(edict_t const *unit) {
    cstring_t move = unit && unit->data.UnitData ? unit->data.UnitData->moveTypeName : NULL;
    return move && !strcmp(move, "fly");
}

static bool G_BotCreepCandidate(edict_t *unit) {
    return G_BotUnitAlive(unit) && unit->s.player == PLAYER_NEUTRAL_AGGRESSIVE &&
        (unit->svflags & SVF_MONSTER) && !G_UnitIsBuilding(unit->class_id);
}

static edict_t *G_BotCreepSearchOrigin(player_t *player) {
    edict_t *town = G_BotTown(player, 0);
    if (town) return town;
    if (!player) return NULL;
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) && (unit->svflags & SVF_MONSTER))
        return unit;
    return NULL;
}

edict_t *G_BotGetCreepCamp(player_t *player, int32_t min_power, int32_t max_power, bool flyers_ok) {
    edict_t *origin, *best = NULL;
    float best_dist = 0.0f;
    uint32_t count, *queue;
    uint8_t *visited;

    if (!player || min_power > max_power) return NULL;
    origin = G_BotCreepSearchOrigin(player);
    if (!origin) return NULL;

    count = globals.num_edicts;
    visited = gi.MemAlloc(count ? count : 1);
    queue = gi.MemAlloc((count ? count : 1) * sizeof(*queue));
    memset(visited, 0, count ? count : 1);

    for (uint32_t seed_index = 0; seed_index < count; seed_index++) {
        edict_t *seed = globals.edicts + seed_index;
        uint32_t head = 0, tail = 0;
        int32_t power = 0;
        bool has_flyer = false;
        edict_t *representative = NULL;
        float representative_dist = 0.0f;

        if (visited[seed_index] || !G_BotCreepCandidate(seed)) continue;
        visited[seed_index] = 1;
        queue[tail++] = seed_index;

        while (head < tail) {
            edict_t *creep = globals.edicts + queue[head++];
            int32_t level = creep->data.UnitBalance ? creep->data.UnitBalance->level : 0;
            float dist = Vector2_distance(&origin->s.origin2, &creep->s.origin2);
            power += level;
            has_flyer |= G_BotCreepFlyer(creep);
            if (!representative || dist < representative_dist) { representative = creep; representative_dist = dist; }

            for (uint32_t other_index = 0; other_index < count; other_index++) {
                edict_t *other;
                if (visited[other_index]) continue;
                other = globals.edicts + other_index;
                if (!G_BotCreepCandidate(other)) continue;
                if (Vector2_distance(&creep->s.origin2, &other->s.origin2) > BOT_CREEP_CAMP_RADIUS) continue;
                visited[other_index] = 1;
                queue[tail++] = other_index;
            }
        }

        if (!representative || power < min_power || power > max_power || (!flyers_ok && has_flyer)) continue;
        if (!best || representative_dist < best_dist) { best = representative; best_dist = representative_dist; }
    }

    gi.MemFree(queue);
    gi.MemFree(visited);
    return best;
}

/* PurchaseZeppelin is a common.ai convenience action. Retail-facing
 * documentation requires an owned Hero near a Goblin Laboratory. Reuse the
 * neutral-unit shop path so authored stock, activation range, resources, food,
 * exit placement, and restock timing remain authoritative. */
void G_BotPurchaseZeppelin(player_t *player) {
    uint32_t const zeppelin = MAKEFOURCC('n','z','e','p');
    uint32_t player_num;
    gameClient_t *client;
    edict_t *clent;
    edict_t *best_shop = NULL;
    float best_distance = 0.0f;

    if (!player || (player_num = PLAYER_NUM(player)) >= MAX_PLAYERS) return;
    client = G_GetPlayerClientByNumber(player_num);
    clent = G_GetPlayerEntityByNumber(player_num);
    if (!client || !clent || !clent->client) return;

    FILTER_EDICTS(shop, shop->inuse && shop->s.player == PLAYER_NEUTRAL_PASSIVE &&
                         G_ShopSellsUnit(shop, zeppelin)) {
        edict_t *nearest_hero = NULL;
        float nearest_distance = 0.0f;
        float radius = G_ShopActivationRadius(shop);

        FILTER_EDICTS(hero, G_BotUnitAlive(hero) && hero->s.player == player_num && G_UnitIsHero(hero)) {
            float distance = Vector2_distance(&shop->s.origin2, &hero->s.origin2);
            float reach = radius + MAX(0.0f, shop->collision) + MAX(0.0f, hero->collision);
            if (distance > reach) continue;
            if (!nearest_hero || distance < nearest_distance ||
                (distance == nearest_distance && hero->s.number < nearest_hero->s.number)) {
                nearest_hero = hero;
                nearest_distance = distance;
            }
        }
        if (!nearest_hero) continue;

        if (!best_shop || nearest_distance < best_distance ||
            (nearest_distance == best_distance && shop->s.number < best_shop->s.number)) {
            best_shop = shop;
            best_distance = nearest_distance;
        }
    }

    if (best_shop) G_ShopPurchaseUnit(clent, best_shop, zeppelin);
}

/* Start/Wait/Get form one asynchronous discovery contract in common.ai. Retail-facing
 * documentation requires Start before Get, while stock scripts poll Wait once per second.
 * BZ_COMPAT_GUESS: retail does not expose the exact discovery latency or internal path/search
 * algorithm. OpenRealm uses a one-second asynchronous latch and chooses the nearest live hostile
 * AI town hall to the caller's primary town (or first live owned unit when no town exists). */
static edict_t *G_BotFindEnemyBase(player_t *player) {
    edict_t *origin = NULL, *best = NULL;
    float best_dist = 0.0f;
    uint32_t caller;
    if (!player || (caller = PLAYER_NUM(player)) >= MAX_PLAYERS) return NULL;
    origin = G_BotTown(player, 0);
    if (!origin) FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == caller) { origin = unit; break; }

    FOR_LOOP(i, MAX_PLAYERS) {
        player_t *enemy;
        edict_t *hall;
        if (i == caller) continue;
        enemy = &game.clients[i].ps;
        for (int32_t town_id = 0; (hall = G_BotTown(enemy, town_id)); town_id++) {
            float dist;
            if (!G_BotUnitAlive(hall) || !G_BotIsHostile(player, hall)) continue;
            if (!origin) return hall;
            dist = Vector2_distance(&origin->s.origin2, &hall->s.origin2);
            if (!best || dist < best_dist || (dist == best_dist && hall->s.number < best->s.number)) {
                best = hall; best_dist = dist;
            }
        }
    }
    return best;
}

void G_BotStartGetEnemyBase(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    bot->enemy_base_target = NULL;
    bot->enemy_base_search_active = true;
    bot->enemy_base_ready_time = G_Time() + BOT_ENEMY_BASE_SEARCH_DELAY_MS;
}

bool G_BotWaitGetEnemyBase(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot || !bot->enemy_base_search_active) return false;
    if (G_Time() < bot->enemy_base_ready_time) return true;
    bot->enemy_base_target = G_BotFindEnemyBase(player);
    bot->enemy_base_search_active = false;
    return false;
}

edict_t *G_BotGetEnemyBase(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot || bot->enemy_base_search_active || !G_BotUnitAlive(bot->enemy_base_target) ||
        !G_BotIsHostile(player, bot->enemy_base_target)) {
        if (bot && !bot->enemy_base_search_active) bot->enemy_base_target = NULL;
        return NULL;
    }
    return bot->enemy_base_target;
}

/* Retail common.ai asks GetEnemyExpansion before asynchronous enemy-main
 * discovery. Map "enemy expansion base" to hostile town IDs after the primary
 * town (1+), using the existing AI town model. BZ_COMPAT_GUESS: when several
 * hostile expansions exist, prefer the one nearest the caller's primary town;
 * without a caller town, stable player/town enumeration wins. */
edict_t *G_BotGetEnemyExpansion(player_t *player) {
    edict_t *home, *best = NULL;
    float best_dist = 0.0f;
    uint32_t caller;

    if (!player || (caller = PLAYER_NUM(player)) >= MAX_PLAYERS) return NULL;
    home = G_BotTown(player, 0);

    FOR_LOOP(i, MAX_PLAYERS) {
        player_t *enemy;
        edict_t *hall;

        if (i == caller) continue;
        enemy = &game.clients[i].ps;
        for (int32_t town_id = 1; (hall = G_BotTown(enemy, town_id)); town_id++) {
            float dist;
            if (!G_BotUnitAlive(hall) || !G_BotIsHostile(player, hall)) continue;
            if (!home) return hall;
            dist = Vector2_distance(&home->s.origin2, &hall->s.origin2);
            if (!best || dist < best_dist ||
                (dist == best_dist && hall->s.number < best->s.number)) {
                best = hall;
                best_dist = dist;
            }
        }
    }
    return best;
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
        if (!done || (!ent->construction && !ent->training)) count++;
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
                        !town->construction) {
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
        !worker->construction && !worker->training && !worker->build_project &&
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
        worker->construction || worker->training || worker->build_project ||
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
        !worker->construction && !worker->training && !worker->build_project &&
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
    int32_t requested = qty;
    uint32_t made = 0;
    cstring_t stop_reason = "request_satisfied";
    if (!player || qty <= 0 || !class_id) return false;
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce request player=%u qty=%d id=%.4s town=%d\n",
        PLAYER_NUM(player), qty, (cstring_t)&class_id, town_id);
#endif
    while (qty-- > 0) {
        if (G_UnitIsBuilding(class_id)) {
            if (!G_BotBuildNearTown(player, class_id, town_id)) {
                stop_reason = "no_worker_or_build_site";
                break;
            }
            made++;
            stop_reason = "serialized_building_placement";
            break; /* common.ai retries deficits; one pending footprint at a time prevents overlapping reservations. */
        } else {
            edict_t *producer = NULL;
            uint32_t producer_queue = MAX_BUILD_QUEUE;
            float producer_dist = 0.0f;
            edict_t *town = town_id < 0 ? G_BotTown(player, 0) : G_BotTown(player, town_id);
            /* Prefer empty queues. Once every eligible producer is occupied,
             * balance new orders onto the producer with the most free slots. */
            FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
                !ent->construction && !ent->training &&
                G_BotUnitAtTown(player, ent, town_id) &&
                G_GetTrainCommandState(G_GetPlayerClientByNumber(ent->s.player), ent, class_id, NULL, 0) ==
                    BUILD_COMMAND_AVAILABLE) {
                uint32_t queue_count = G_ProductionQueueCount(ent);
                float dist = town ? Vector2_distance(&town->s.origin2, &ent->s.origin2) : 0.0f;
                if (queue_count >= MAX_BUILD_QUEUE) continue;
                if (!producer || queue_count < producer_queue ||
                    (queue_count == producer_queue && dist < producer_dist)) {
                    producer = ent;
                    producer_queue = queue_count;
                    producer_dist = dist;
                }
            }
            if (!producer) {
                stop_reason = "no_eligible_producer_with_queue_space";
                break;
            }
            if (!SP_TrainUnit(producer, class_id)) {
                stop_reason = "train_command_rejected";
                break;
            }
        }
        made++;
    }
#ifdef WC3_TRACE_AI
    if (made < (uint32_t)requested) {
        if (G_UnitIsBuilding(class_id)) {
            G_BOT_TRACE(PLAYER_NUM(player), NULL, "production_shortfall",
                       "requested=%d started=%u unit=%.4s town=%d stop=%s",
                       requested, made, (cstring_t)&class_id, town_id, stop_reason);
        } else {
            int32_t owned_producers = 0, busy_producers = 0, wrong_town = 0;
            int32_t train_absent = 0, train_hidden = 0, train_disabled = 0;
            int32_t train_unaffordable = 0, queue_available = 0, queue_full = 0;
            FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
                G_UnitIsBuilding(ent->class_id)) {
                owned_producers++;
                if (ent->construction || ent->training) {
                    busy_producers++;
                } else if (!G_BotUnitAtTown(player, ent, town_id)) {
                    wrong_town++;
                } else {
                    switch (G_GetTrainCommandState(G_GetPlayerClientByNumber(ent->s.player),
                                                   ent, class_id, NULL, 0)) {
                        case BUILD_COMMAND_ABSENT: train_absent++; break;
                        case BUILD_COMMAND_HIDDEN: train_hidden++; break;
                        case BUILD_COMMAND_DISABLED: train_disabled++; break;
                        case BUILD_COMMAND_UNAFFORDABLE: train_unaffordable++; break;
                        case BUILD_COMMAND_AVAILABLE:
                            if (G_ProductionQueueCount(ent) >= MAX_BUILD_QUEUE) queue_full++;
                            else queue_available++;
                            break;
                    }
                }
            }
            G_BOT_TRACE(PLAYER_NUM(player), NULL, "production_shortfall",
                       "requested=%d started=%u unit=%.4s town=%d stop=%s producers=%d busy=%d wrong_town=%d absent=%d hidden=%d disabled=%d unaffordable=%d queue_slots=%d queue_full=%d",
                       requested, made, (cstring_t)&class_id, town_id, stop_reason,
                       owned_producers, busy_producers, wrong_town, train_absent,
                       train_hidden, train_disabled, train_unaffordable,
                       queue_available, queue_full);
        }
    }
#endif
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI produce result id=%.4s made=%u\n", (cstring_t)&class_id, made);
#endif
#ifndef WC3_TRACE_AI
    (void)requested;
    (void)stop_reason;
#endif
    return made > 0;
}

/* SetUpgrade is a one-shot AI request. common.ai handles resource planning and retries;
 * the engine selects any completed owned producer whose ordinary research command is available. */
bool G_BotUpgrade(player_t *player, uint32_t upgrade_id) {
    gameClient_t *client;
    if (!player || !upgrade_id || !(client = PLAYER_CLIENT(player))) return false;
    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == PLAYER_NUM(player) &&
        !ent->construction && !ent->training &&
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
            !unit->construction && !unit->build_project &&
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

/* BZ_COMPAT_GUESS: retail exposes the group-flee policy and CaptainRetreating state,
 * but not the internal battle-strength formula. Use health-weighted unit level as a
 * deterministic local combat-power proxy; keep it private to the flee evaluator so
 * direct retail capture can replace the heuristic without changing captain state. */
static float G_BotRetreatUnitPower(edict_t *unit) {
    float health_fraction = 1.0f;
    float level = 1.0f;
    if (!G_BotUnitAlive(unit)) return 0.0f;
    if (unit->data.UnitBalance) level = (float)MAX(1, unit->data.UnitBalance->level);
    if (unit->health.max_value > 0.0f)
        health_fraction = MIN(1.0f, MAX(0.0f, unit->health.value / unit->health.max_value));
    return level * health_fraction;
}

static bool G_BotRetreatEnemyCombatant(edict_t *unit) {
    if (!G_BotUnitAlive(unit) || !(unit->svflags & SVF_MONSTER)) return false;
    return (unit->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 0)) ||
           (unit->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(unit, 1));
}

static bool G_BotRetreatEnemyNearCaptain(botCaptain_t const *captain, edict_t *enemy) {
    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        if (G_BotUnitAlive(*member) &&
            Vector2_distance(&(*member)->s.origin2, &enemy->s.origin2) <= BOT_GROUP_FLEE_ENEMY_RADIUS)
            return true;
    return false;
}

static void G_BotCaptainBeginRetreat(botCaptain_t *captain) {
    edict_t *waypoint = NULL;
    if (!captain) return;
    captain->state = BOT_CAPTAIN_RETREATING;
    captain->goal = captain->home;
    captain->disadvantage_active = false;
    captain->disadvantage_since = 0;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        bool already_returning;
        if (!G_BotUnitAlive(*member)) continue;
        unit_leavecombat(*member);
        already_returning = (*member)->currentmove && (*member)->currentmove->proc == CAbilityMove &&
            (*member)->goalentity && (*member)->goalentity->inuse &&
            Vector2_distance(&(*member)->goalentity->s.origin2, &captain->home) <= 1.0f;
        if (already_returning) continue;
        if (!waypoint) waypoint = Waypoint_add(&captain->home);
        order_move(*member, waypoint);
    }
}

static void G_BotCaptainUpdateRetreat(botCaptain_t *captain) {
    edict_t *waypoint = NULL;
    bool any = false, all_home = true;
    if (!captain) return;

    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        edict_t *unit = *member;
        if (!G_BotUnitAlive(unit)) continue;
        any = true;
        unit_leavecombat(unit);
        if (Vector2_distance(&unit->s.origin2, &captain->home) <= BOT_GROUP_FLEE_HOME_RADIUS) continue;
        all_home = false;
        if (!unit->currentmove || unit->currentmove->proc != CAbilityMove ||
            !unit->goalentity || !unit->goalentity->inuse ||
            Vector2_distance(&unit->goalentity->s.origin2, &captain->home) > 1.0f) {
            unit_leavecombat(unit);
            if (!waypoint) waypoint = Waypoint_add(&captain->home);
            order_move(unit, waypoint);
        }
    }

    if (!any || all_home) {
        captain->state = BOT_CAPTAIN_IDLE;
        captain->goal = captain->home;
        captain->disadvantage_active = false;
        captain->disadvantage_since = 0;
    }
}

/* SetGroupsFlee is engine policy, while stock common.ai only observes the result through
 * CaptainRetreating(). BZ_COMPAT_GUESS: compare nearby hostile combat power against the
 * active assault captain and require the disadvantage to persist before retreating.
 * The threshold, neighborhood, persistence, and home tolerance are isolated constants. */
void G_BotUpdateGroupFlee(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    float friendly_power = 0.0f, enemy_power = 0.0f;
    uint32_t now;
    if (!bot) return;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;

    if (captain->state == BOT_CAPTAIN_RETREATING) {
        G_BotCaptainUpdateRetreat(captain);
        return;
    }
    if (!(bot->flags & BOT_GROUPS_FLEE) || captain->state != BOT_CAPTAIN_ACTIVE ||
        !G_BotCaptainInCombat(player, true)) {
        captain->disadvantage_active = false;
        captain->disadvantage_since = 0;
        return;
    }

    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        friendly_power += G_BotRetreatUnitPower(*member);
    FILTER_EDICTS(enemy, G_BotRetreatEnemyCombatant(enemy) && G_BotIsHostile(player, enemy) &&
        G_BotRetreatEnemyNearCaptain(captain, enemy))
        enemy_power += G_BotRetreatUnitPower(enemy);

    if (friendly_power <= 0.0f || enemy_power <= friendly_power * BOT_GROUP_FLEE_POWER_RATIO) {
        captain->disadvantage_active = false;
        captain->disadvantage_since = 0;
        return;
    }

    now = G_Time();
    if (!captain->disadvantage_active) {
        captain->disadvantage_active = true;
        captain->disadvantage_since = now;
        return;
    }
    if ((uint32_t)(now - captain->disadvantage_since) < BOT_GROUP_FLEE_PERSIST_MS) return;
    G_BotCaptainBeginRetreat(captain);
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
    if (captain->state == BOT_CAPTAIN_RETREATING) return;
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
    if (captain->routes) gi.MemFree(captain->routes);
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

/* Persistent defender requests are totals by type: repeated AddDefenders calls reconcile
 * the same desired count instead of consuming additional units. */
static bool G_BotCaptainFill(player_t *player, botCaptainType_t type, int32_t qty, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    int32_t have = 0;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    captain = bot->captains + type;
    FOR_EACH_ARRAY(edict_t *, unit, captain->units)
        if (G_BotUnitAlive(*unit) && (*unit)->class_id == class_id) have++;
    FILTER_EDICTS(unit, have < qty && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && !unit->construction && !unit->training && !G_BotCaptainHasUnit(bot, unit)) {
        G_BotCaptainAdd(captain, unit); have++;
    }
    return have >= qty;
}

/* AddAssault is additive per common.ai harass entry, not a per-type total. SetAssaultGroup
 * intentionally allows duplicate entries (Interleave helpers can emit them), and FormGroup
 * calls AddAssault once for each entry. Each call therefore consumes up to qty additional
 * eligible units of that type while returning whether the whole entry was satisfied. */
static bool G_BotCaptainTakeAssault(player_t *player, int32_t qty, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    int32_t added = 0;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    captain = bot->captains + BOT_CAPTAIN_ATTACK;
    FILTER_EDICTS(unit, added < qty && G_BotUnitAlive(unit) && unit->s.player == PLAYER_NUM(player) &&
        unit->class_id == class_id && !unit->construction && !unit->training && !G_BotCaptainHasUnit(bot, unit)) {
        G_BotCaptainAdd(captain, unit); added++;
    }
    return added >= qty;
}

bool G_BotAddAssault(player_t *player, int32_t qty, uint32_t class_id) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool ready;
    if (bot && qty > 0 && class_id) bot->captains[BOT_CAPTAIN_ATTACK].desired += qty;
    ready = G_BotCaptainTakeAssault(player, qty, class_id);
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

/* CaptainRetreating is a query over the assault captain's engine-owned state.
 * Stock common.ai uses it to stop waiting on an attack wave once the engine has
 * begun a group retreat. Do not infer or initiate retreat here: SetGroupsFlee's
 * retail disadvantage/losing-battle transition remains separate behavior work. */
bool G_BotCaptainRetreating(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    return bot && bot->captains[BOT_CAPTAIN_ATTACK].state == BOT_CAPTAIN_RETREATING;
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
             * captain-home point. Until that selector exists, return to the main
             * town, falling back to the captain home when no town exists. */
            if (town || captain->home.x != 0.0f || captain->home.y != 0.0f)
                order_move(unit, Waypoint_add(town ? &town->s.origin2 : &captain->home));
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
            unit->class_id == post->class_id && !unit->construction && !unit->training &&
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

/* BZ_COMPAT_GUESS: TeleportCaptain relocates the *attack captain's logical*
 * position without teleporting its member units, changing its home or
 * destroying existing attack targets.  Retail in-flight effects are unknown. */
void G_BotTeleportCaptain(player_t *player, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot || !isfinite(x) || !isfinite(y)) return;
    botCaptain_t *captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    captain->position = MAKE(vec2_t, x, y);
    captain->position_valid = true;
}

/* Choose a reachable per-unit waypoint for AI orders without changing authored movement masks.
 * BZ_COMPAT_GUESS: partial destinations can finish an order short of the common goal. */
static vec2_t G_BotCaptainRoutePoint(bot_t const *bot, edict_t const *unit,
                                   vec2_t const *goal, bool *partial) {
    vec2_t reachable;
    *partial = false;
    if (bot && unit && (bot->flags & BOT_AMPHIBIOUS) && !(bot->flags & BOT_DISABLE_PATHING) &&
        CM_ClosestReachablePointForRadiusFlags(&unit->s.origin2, goal, unit->collision,
                                                M_UnitStaticPathingFlags(unit), &reachable)) {
        *partial = Vector2_distance(&reachable, goal) > BOT_GROUP_FLEE_HOME_RADIUS;
        return reachable;
    }
    if (bot && unit && (bot->flags & BOT_AMPHIBIOUS) &&
        !(bot->flags & BOT_DISABLE_PATHING)) {
        /* BZ_COMPAT_GUESS: no navigable candidate; complete the captain's
         * order at the member's existing reachable position rather than
         * leaving common.ai waiting forever for unreachable water. */
        *partial = true;
        return unit->s.origin2;
    }
    return *goal;
}

/* A captain owns one authored objective but each member may receive a
 * different reachable destination. These routes are transient AI runtime
 * state, not an edict or game-save contract. Replacing an order frees the
 * previous route map; no stale member pointer is ever dereferenced. */
static void G_BotCaptainClearRoutes(botCaptain_t *captain) {
    if (captain->routes) gi.MemFree(captain->routes);
    captain->routes = NULL;
    ARRAY_COUNT(captain->routes) = 0;
}

static void G_BotCaptainPrepareRoutes(botCaptain_t *captain) {
    uint32_t count = ARRAY_COUNT(captain->units);
    G_BotCaptainClearRoutes(captain);
    if (!count) return;
    captain->routes = gi.MemAlloc(count * sizeof(*captain->routes));
    memset(captain->routes, 0, count * sizeof(*captain->routes));
    ARRAY_COUNT(captain->routes) = count;
}

static void G_BotCaptainRecordRoute(botCaptain_t *captain, uint32_t index,
                                   edict_t *unit, vec2_t destination, bool partial) {
    if (index >= ARRAY_COUNT(captain->routes)) return;
    captain->routes[index] = MAKE(botCaptainRoute_t,
        .unit = unit, .destination = destination, .partial = partial);
}

static vec2_t const *G_BotCaptainMemberDestination(botCaptain_t const *captain,
                                                    edict_t const *unit, vec2_t const *default_goal) {
    FOR_EACH_ARRAY(botCaptainRoute_t, route, captain->routes)
        if (route->unit == unit) return &route->destination;
    return default_goal;
}

void G_BotCaptainAttack(player_t *player, float x, float y) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    bool any = false;
    if (!bot || !isfinite(x) || !isfinite(y)) return;
    captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    captain->goal = MAKE(vec2_t, x, y);
    captain->position_valid = false;
    G_BotCaptainPrepareRoutes(captain);
    FOR_LOOP(i, ARRAY_COUNT(captain->units)) {
        edict_t *member = captain->units[i];
        vec2_t destination;
        bool partial;
        if (!G_BotUnitAlive(member)) continue;
        destination = G_BotCaptainRoutePoint(bot, member, &captain->goal, &partial);
        G_BotCaptainRecordRoute(captain, i, member, destination, partial);
        order_attackmove(member, Waypoint_add(&destination));
        any = true;
    }
    if (any) captain->state = BOT_CAPTAIN_ACTIVE;
}

void G_BotCaptainGoHome(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    if (!bot) return;
    captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    captain->goal = captain->home;
    captain->position_valid = false;
    G_BotCaptainPrepareRoutes(captain);
    FOR_LOOP(i, ARRAY_COUNT(captain->units)) {
        edict_t *member = captain->units[i];
        vec2_t destination;
        bool partial;
        if (!G_BotUnitAlive(member)) continue;
        destination = G_BotCaptainRoutePoint(bot, member, &captain->home, &partial);
        G_BotCaptainRecordRoute(captain, i, member, destination, partial);
        order_move(member, Waypoint_add(&destination));
    }
    captain->state = BOT_CAPTAIN_ACTIVE;
}

/* BZ_COMPAT_GUESS: partial-route arrival means each living member reached
 * its issued reachable waypoint. This reports order completion, not actual
 * access to an unreachable water-separated captain objective. The attack
 * captain's original goal remains unchanged for scripts/next orders. */
bool G_BotCaptainAtGoal(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool has_unit = false;
    botCaptain_t *captain;
    if (!bot) return false;
    captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    if (captain->position_valid)
        return Vector2_distance(&captain->position, &captain->goal) <= BOT_GROUP_FLEE_HOME_RADIUS;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        vec2_t const *destination;
        if (!G_BotUnitAlive(*member)) continue;
        has_unit = true;
        destination = G_BotCaptainMemberDestination(captain, *member, &captain->goal);
        if (Vector2_distance(&(*member)->s.origin2, destination) > BOT_GROUP_FLEE_HOME_RADIUS) return false;
    }
    return has_unit;
}

bool G_BotCaptainIsHome(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool has_unit = false;
    if (!bot) return false;
    botCaptain_t *captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    if (captain->position_valid)
        return Vector2_distance(&captain->position, &captain->home) <= BOT_GROUP_FLEE_HOME_RADIUS;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        if (!G_BotUnitAlive(*member)) continue;
        has_unit = true;
        vec2_t const *destination = &captain->home;
        if (Vector2_distance(&captain->goal, &captain->home) <= 0.01f)
            destination = G_BotCaptainMemberDestination(captain, *member, &captain->home);
        if (Vector2_distance(&(*member)->s.origin2, destination) > BOT_GROUP_FLEE_HOME_RADIUS) return false;
    }
    if (has_unit) return true;
    return false;
}

void G_BotClearCaptainTargets(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    botCaptain_t *captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    /* BZ_COMPAT_GUESS: clear logical targets only; existing member orders
     * keep running, so do not claim the captain is idle while they fight. */
    G_BotCaptainClearRoutes(captain);
    captain->position_valid = false;
    captain->goal = captain->home;
}

void G_BotResetCaptainLocs(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        bot->captains[i].position = bot->captains[i].home;
        bot->captains[i].position_valid = true;
        bot->captains[i].goal = bot->captains[i].home;
        G_BotCaptainClearRoutes(&bot->captains[i]);
    }
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

/* Assault targeting stays inside ordinary combat validity: visible, live
 * units and buildings, so waves never issue rejected attacks at hidden units,
 * items, waypoints, corpses, or the attackers themselves. A negative target
 * suicides against any hostile owner; otherwise only the named player's
 * forces qualify. */
static edict_t *G_BotAssaultTarget(player_t *player, edict_t *self, int32_t target) {
    edict_t *best = NULL;
    float best_dist = 0;
    if (!player || !G_BotUnitAlive(self)) return NULL;
    FILTER_EDICTS(ent, ent != self && ent->inuse && !(ent->svflags & (SVF_DEADMONSTER | SVF_NOCLIENT)) &&
        ent->health.value > 0 && ((ent->svflags & SVF_MONSTER) || G_UnitIsStructure(ent)) &&
        !S_UnitIsHiddenFromPlayer(ent, PLAYER_NUM(player)) &&
        (target >= 0 ? ent->s.player == (uint32_t)target : G_BotIsHostile(player, ent))) {
        float dist = Vector2_distance(&self->s.origin2, &ent->s.origin2);
        if (!best || dist < best_dist) { best = ent; best_dist = dist; }
    }
    return best;
}

/* Send one assault member at the enemy, falling back to an attack-move toward
 * the staged point so waves keep moving when no target is visible yet. */
static void G_BotOrderAssaultMember(bot_t *bot, edict_t *unit, int32_t target) {
    if (!bot || !unit) return;
    edict_t *enemy = bot ? G_BotAssaultTarget(bot->player, unit, target) : NULL;
#ifdef WC3_TRACE_AI
    botTraceAssaultMove_t *trace = unit && unit->s.number < MAX_ENTITIES ? bot_trace_assault_moves + unit->s.number : NULL;
#endif
    if (enemy) {
        order_attack(unit, enemy);
#ifdef WC3_TRACE_AI
        if (trace) {
            trace->unit = unit;
            trace->player = PLAYER_NUM(bot->player);
            trace->last_report = G_Time();
            trace->last_position = trace->start_position = unit->s.origin2;
            trace->terminal_reported = false;
            G_BotTraceAssaultUnit(unit, "assault_order_result",
                "target=%u target_inuse=%d target_alive=%d target_deadflag=%d target_health=%.1f target_type=%d target_flag=0x%x target_cycloned=%d target_hidden=%d can_target=%d attacker_cycloned=%d mine_worker_inside=%d attacker_attack1_type=%d attack1_mask=0x%x attacker_attack2_type=%d attack2_mask=0x%x installed=%d goal_matches=%d attack_move=%d attack1_enabled=%d attack2_enabled=%d",
                enemy->s.number, enemy->inuse, G_BotUnitAlive(enemy), !!(enemy->svflags & SVF_DEADMONSTER),
                enemy->health.value, G_UnitTargetType(enemy), G_TargetFlagForType(G_UnitTargetType(enemy)),
                S_UnitIsCycloned(enemy), S_UnitIsHiddenFromPlayer(enemy, PLAYER_NUM(bot->player)),
                S_AttackCanTarget(unit, enemy),
                S_UnitIsCycloned(unit), S_GoldMineWorkerIsInside(unit),
                unit->attack1.type, unit->attack1.targetsAllowed,
                unit->attack2.type, unit->attack2.targetsAllowed,
                unit->goalentity == enemy && unit->currentmove && unit->currentmove->proc == CAbilityAttack,
                unit->goalentity == enemy, unit->currentmove && unit->currentmove->proc == CAbilityAttack,
                S_UnitAttackSlotEnabled(unit, 0), S_UnitAttackSlotEnabled(unit, 1));
        }
#endif
        G_BOT_TRACE(PLAYER_NUM(bot->player), NULL, "assault_member_order",
                    "unit=%u type=%c%c%c%c mode=attack target=%u target_type=%c%c%c%c from=(%.1f,%.1f) to=(%.1f,%.1f)",
                    unit->s.number, (char)(unit->class_id & 255), (char)((unit->class_id >> 8) & 255),
                    (char)((unit->class_id >> 16) & 255), (char)((unit->class_id >> 24) & 255),
                    enemy->s.number, (char)(enemy->class_id & 255), (char)((enemy->class_id >> 8) & 255),
                    (char)((enemy->class_id >> 16) & 255), (char)((enemy->class_id >> 24) & 255),
                    unit->s.origin2.x, unit->s.origin2.y, enemy->s.origin2.x, enemy->s.origin2.y);
        return;
    }
    if (bot && bot->stage_valid) {
        order_attackmove(unit, Waypoint_add(&bot->stage));
#ifdef WC3_TRACE_AI
        if (trace) {
            trace->unit = unit;
            trace->player = PLAYER_NUM(bot->player);
            trace->last_report = G_Time();
            trace->last_position = trace->start_position = unit->s.origin2;
            trace->terminal_reported = false;
        }
#endif
        G_BOT_TRACE(PLAYER_NUM(bot->player), NULL, "assault_member_order",
                    "unit=%u type=%c%c%c%c mode=attackmove target_player=%d from=(%.1f,%.1f) stage=(%.1f,%.1f)",
                    unit->s.number, (char)(unit->class_id & 255), (char)((unit->class_id >> 8) & 255),
                    (char)((unit->class_id >> 16) & 255), (char)((unit->class_id >> 24) & 255), target,
                    unit->s.origin2.x, unit->s.origin2.y, bot->stage.x, bot->stage.y);
    } else {
        G_BOT_TRACE(PLAYER_NUM(bot->player), NULL, "assault_member_no_target",
                    "unit=%u type=%c%c%c%c target_player=%d stage_valid=%d",
                    unit->s.number, (char)(unit->class_id & 255), (char)((unit->class_id >> 8) & 255),
                    (char)((unit->class_id >> 16) & 255), (char)((unit->class_id >> 24) & 255),
                    target, bot ? bot->stage_valid : 0);
    }
}

#ifdef WC3_TRACE_AI
static void G_BotTraceAssaultMovement(bot_t *bot) {
    botCaptain_t *captain = bot->captains + BOT_CAPTAIN_ATTACK;
    if (captain->state != BOT_CAPTAIN_ACTIVE) return;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        edict_t *unit = *member;
        botTraceAssaultMove_t *trace;
        uint32_t now;
        float interval_distance, total_distance;
        edict_t *goal;
        if (!unit || unit->s.number >= MAX_ENTITIES) continue;
        trace = bot_trace_assault_moves + unit->s.number;
        if (trace->unit != unit || trace->player != PLAYER_NUM(bot->player)) continue;
        if (!G_BotUnitAlive(unit) && trace->terminal_reported) continue;
        now = G_Time();
        if (now - trace->last_report < BOT_TRACE_ASSAULT_MOVE_INTERVAL_MS) continue;
        interval_distance = Vector2_distance(&trace->last_position, &unit->s.origin2);
        total_distance = Vector2_distance(&trace->start_position, &unit->s.origin2);
        goal = unit->goalentity;
        G_BOT_TRACE(PLAYER_NUM(bot->player), NULL, "assault_member_movement",
                    "unit=%u type=%c%c%c%c alive=%d moved_interval=%d interval_distance=%.1f total_distance=%.1f position=(%.1f,%.1f) goal=%u goal_type=%c%c%c%c goal_position=(%.1f,%.1f)",
                    unit->s.number, (char)(unit->class_id & 255), (char)((unit->class_id >> 8) & 255),
                    (char)((unit->class_id >> 16) & 255), (char)((unit->class_id >> 24) & 255),
                    G_BotUnitAlive(unit), interval_distance >= BOT_TRACE_ASSAULT_MOVE_EPSILON,
                    interval_distance, total_distance, unit->s.origin2.x, unit->s.origin2.y,
                    goal && goal->inuse ? goal->s.number : UINT32_MAX,
                    goal && goal->inuse ? (char)(goal->class_id & 255) : '-',
                    goal && goal->inuse ? (char)((goal->class_id >> 8) & 255) : '-',
                    goal && goal->inuse ? (char)((goal->class_id >> 16) & 255) : '-',
                    goal && goal->inuse ? (char)((goal->class_id >> 24) & 255) : '-',
                    goal && goal->inuse ? goal->s.origin2.x : 0.0f,
                    goal && goal->inuse ? goal->s.origin2.y : 0.0f);
        trace->last_report = now;
        trace->last_position = unit->s.origin2;
        trace->terminal_reported = !G_BotUnitAlive(unit);
    }
}
#endif

/* SuicideUnit/SuicideUnitEx share one backend: fill the assault roster through
 * the ordinary AddAssault path, then send the requested type at the enemy.
 * Retail common.ai declares both natives void; the boolean reports roster
 * acceptance for tests, mirroring AddAssault. */
bool G_BotSuicideUnits(player_t *player, int32_t qty, uint32_t class_id, int32_t target) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    bool accepted;
    if (!bot || qty <= 0 || !class_id) return qty <= 0;
    if (bot->captains[BOT_CAPTAIN_ATTACK].state == BOT_CAPTAIN_RETREATING) return false;
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
    if (captain->state == BOT_CAPTAIN_RETREATING) return false;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) if (G_BotUnitAlive(*member)) { any = true; break; }
    if (!any) return false;
    if (check_full && !G_BotCaptainIsFull(player)) return false;
    FOR_EACH_ARRAY(edict_t *, member, captain->units)
        if (G_BotUnitAlive(*member)) G_BotOrderAssaultMember(bot, *member, (int32_t)target);
    captain->state = BOT_CAPTAIN_ACTIVE;
    if (bot->stage_valid) captain->goal = bot->stage;
    return true;
}

/* BZ_COMPAT_GUESS: VsUnits targets enemy units only; VsPlayer also
 * targets buildings. Select ONE visible enemy as captain objective, rather
 * than repeatedly overwriting the shared goal with each soldier's target.
 * G_FowPlayerCanHoverEntity requires CURRENT visibility for structures as
 * well as units (CanSeeEntity deliberately includes explored buildings). */
static void G_BotCaptainVsTarget(player_t *player, player_t *enemy, bool units_only) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    edict_t *best = NULL;
    float best_distance = 0.0f;
    bool any = false;
    if (!bot || !enemy) return;
    captain = &bot->captains[BOT_CAPTAIN_ATTACK];
    FILTER_EDICTS(candidate, G_BotUnitAlive(candidate) &&
        candidate->s.player == PLAYER_NUM(enemy) &&
        ((candidate->svflags & SVF_MONSTER) || G_UnitIsStructure(candidate)) &&
        (!units_only || !G_UnitIsStructure(candidate)) &&
        !(candidate->svflags & SVF_NOCLIENT) &&
        !S_UnitIsHiddenFromPlayer(candidate, PLAYER_NUM(player)) &&
        G_FowPlayerCanHoverEntity(PLAYER_NUM(player), candidate)) {
        FOR_EACH_ARRAY(edict_t *, member, captain->units) {
            float distance;
            if (!G_BotUnitAlive(*member)) continue;
            distance = Vector2_distance(&(*member)->s.origin2, &candidate->s.origin2);
            if (!best || distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        }
    }
    if (!best) return; /* Retain previous orders/goal when no valid target is visible. */
    G_BotCaptainClearRoutes(captain);
    captain->position_valid = false;
    captain->goal = best->s.origin2;
    FOR_EACH_ARRAY(edict_t *, member, captain->units) {
        if (!G_BotUnitAlive(*member)) continue;
        order_attack(*member, best);
        any = true;
    }
    if (any) captain->state = BOT_CAPTAIN_ACTIVE;
}

void G_BotCaptainVsPlayer(player_t *player, player_t *enemy) {
    G_BotCaptainVsTarget(player, enemy, false);
}

void G_BotCaptainVsUnits(player_t *player, player_t *enemy) {
    G_BotCaptainVsTarget(player, enemy, true);
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
        ent->class_id == make && !ent->construction && !ent->training) have++;
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

/* Keep the macro temporary distinct from caller aliases; alias = alias self-initialized it. */
#define TRY_CONVERSION_ABILITY(alias_) do { \
        uint32_t const code = (alias_); \
        abilityitem_t const item = S_AbilityItem(code); \
        uint32_t const level = G_UnitAbilityLevel(unit, code); \
        uint32_t const target = level ? S_SpellUnitId(code, level) : 0; \
        if (level && item.ability && item.ability->proc == CAbilityMetamorphosis && \
            item.ability->target_type == SPELL_TARGET_NONE && target && target != unit->class_id) { \
            if (target_type) *target_type = target; \
            return code; \
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
        ent->class_id == target_type && !ent->construction && !ent->training) have++;
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

void G_BotRefreshPeonsRepair(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    if (!bot) return;
    bot->repair_policy_dirty = true;
    G_BotApplyRepairPolicy(bot);
}

/* SetUnitsFlee/SetHeroesFlee expose retail engine policy rather than script orders.
 * BZ_COMPAT_GUESS: use <30% life while a hostile is nearby and return to the primary
 * town until retail threshold, radius and destination policy are recovered. */
static bool G_BotIndividualFleeCandidate(edict_t *unit, bool hero_policy) {
    bool is_hero;
    if (!G_BotUnitAlive(unit) || !(unit->svflags & SVF_MONSTER) ||
        G_UnitIsBuilding(unit->class_id) || unit->health.max_value <= 0.0f) return false;
    is_hero = G_UnitIsHero(unit);
    if (is_hero != hero_policy) return false;
    if (!is_hero && (G_ActorHasSkill(unit, "Ahar") ||
        (unit->attack1.type == ATK_NONE && unit->attack2.type == ATK_NONE))) return false;
    return unit->health.value / unit->health.max_value < BOT_INDIVIDUAL_FLEE_HEALTH_FRACTION;
}

static bool G_BotHostileNear(player_t *player, edict_t *unit) {
    FILTER_EDICTS(enemy, G_BotUnitAlive(enemy) && (enemy->svflags & SVF_MONSTER) &&
        G_BotIsHostile(player, enemy) &&
        Vector2_distance(&unit->s.origin2, &enemy->s.origin2) <= BOT_INDIVIDUAL_FLEE_DANGER_RADIUS)
        return true;
    return false;
}

void G_BotUpdateIndividualFlee(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    edict_t *home;
    uint32_t now;
    if (!bot || !(bot->flags & (BOT_UNITS_FLEE | BOT_HEROES_FLEE))) return;
    now = G_Time();
    if ((uint32_t)(now - bot->flee_policy_last_scan) < BOT_INDIVIDUAL_FLEE_SCAN_MS) return;
    bot->flee_policy_last_scan = now;
    home = G_BotTown(player, 0);
    if (!G_BotUnitAlive(home)) return;

    FILTER_EDICTS(unit, unit->s.player == PLAYER_NUM(player) &&
        (((bot->flags & BOT_HEROES_FLEE) && G_BotIndividualFleeCandidate(unit, true)) ||
         ((bot->flags & BOT_UNITS_FLEE) && G_BotIndividualFleeCandidate(unit, false)))) {
        if (!G_BotHostileNear(player, unit)) continue;
        if (unit->currentmove && unit->currentmove->proc == CAbilityMove && unit->goalentity &&
            unit->goalentity->inuse &&
            Vector2_distance(&unit->goalentity->s.origin2, &home->s.origin2) <= 1.0f) continue;
        unit_leavecombat(unit);
        order_move(unit, Waypoint_add(&home->s.origin2));
    }
}

static edict_t *G_BotNearestGroundItem(edict_t *hero) {
    edict_t *best = NULL;
    int32_t best_priority = INT32_MIN;
    float best_distance = 0.0f;
    FILTER_EDICTS(item, G_IsItem(item) && item->item->in_world) {
        ItemData_t const *data = G_ItemData(item->class_id);
        float distance = Vector2_distance(&hero->s.origin2, &item->s.origin2);
        if (!data || distance > BOT_HERO_ITEM_RADIUS || !G_CanPickupItem(hero, item)) continue;
        if (!best || data->prio > best_priority ||
            (data->prio == best_priority && distance < best_distance) ||
            (data->prio == best_priority && distance == best_distance && item->s.number < best->s.number)) {
            best = item; best_priority = data->prio; best_distance = distance;
        }
    }
    return best;
}

static bool G_BotHeroNearShop(edict_t *hero, edict_t *shop) {
    float reach = G_ShopActivationRadius(shop) + MAX(0.0f, shop->collision) + MAX(0.0f, hero->collision);
    return Vector2_distance(&hero->s.origin2, &shop->s.origin2) <= reach;
}

static bool G_BotBuyBestShopItem(player_t *player, edict_t *hero) {
    edict_t *clent = G_GetPlayerEntityByNumber(PLAYER_NUM(player));
    gameClient_t *client = clent ? clent->client : NULL;
    edict_t *best_shop = NULL;
    uint32_t best_item = 0;
    int32_t best_priority = INT32_MIN;
    float best_distance = 0.0f;
    if (!client || G_FindFreeInventorySlot(hero) < 0) return false;

    FILTER_EDICTS(shop, shop->inuse && G_CanUseItemShop(client, shop) && G_BotHeroNearShop(hero, shop)) {
        if (G_FindShopPatron(client, shop) != hero) continue;
        FOR_LOOP(i, G_UpdateShopItemStock(shop)) {
            uint32_t item_id = shop->stock->items[i].id;
            ItemData_t const *data = G_ItemData(item_id);
            float distance;
            if (shop->stock->items[i].current <= 0 || !data || !data->file ||
                !G_ShopItemRequirementsSatisfied(client, item_id, NULL, 0)) continue;
            if (client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < (uint32_t)MAX(0, data->goldcost) ||
                client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] < (uint32_t)MAX(0, data->lumbercost)) continue;
            distance = Vector2_distance(&hero->s.origin2, &shop->s.origin2);
            if (!best_shop || data->prio > best_priority ||
                (data->prio == best_priority && distance < best_distance) ||
                (data->prio == best_priority && distance == best_distance && item_id < best_item)) {
                best_shop = shop; best_item = item_id; best_priority = data->prio; best_distance = distance;
            }
        }
    }
    return best_shop && best_item && G_ShopPurchaseItem(clent, best_shop, best_item);
}

/* Item policies do not interrupt combat. BZ_COMPAT_GUESS: scans use 600 world units for
 * ground items and two seconds for purchases; ItemData.prio ranks candidates. */
void G_BotUpdateHeroItems(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    uint32_t now;
    bool buy_due;
    if (!bot || !(bot->flags & (BOT_HEROES_TAKE_ITEM | BOT_HEROES_BUY_ITEMS))) return;
    now = G_Time();
    if ((uint32_t)(now - bot->item_policy_last_scan) < BOT_HERO_ITEM_SCAN_MS) return;
    bot->item_policy_last_scan = now;
    buy_due = (bot->flags & BOT_HEROES_BUY_ITEMS) &&
        (uint32_t)(now - bot->hero_buy_last_scan) >= BOT_HERO_BUY_SCAN_MS;
    FILTER_EDICTS(hero, G_BotUnitAlive(hero) && hero->s.player == PLAYER_NUM(player) && G_UnitIsHero(hero)) {
        edict_t *item;
        if (unit_affectingcombat(hero)) continue;
        if ((bot->flags & BOT_HEROES_TAKE_ITEM) && G_FindFreeInventorySlot(hero) >= 0 &&
            (item = G_BotNearestGroundItem(hero))) {
            if (hero->currentmove && hero->currentmove->proc == CAbilityInventory && hero->goalentity == item)
                continue;
            if (G_OrderPickupItem(hero, item)) continue;
        }
        if (buy_due && G_BotBuyBestShopItem(player, hero)) break;
    }
    if (buy_due) bot->hero_buy_last_scan = now;
}

static edict_t *G_BotAlliedThreat(player_t *player) {
    uint32_t self = PLAYER_NUM(player);
    FILTER_EDICTS(attacker, G_BotUnitAlive(attacker) && attacker->currentmove &&
        attacker->currentmove->proc == CAbilityAttack && G_BotUnitAlive(attacker->goalentity) &&
        attacker->goalentity->s.player < MAX_PLAYERS) {
        uint32_t ally = attacker->goalentity->s.player;
        player_t *ally_player;
        if (ally == self || ally == attacker->s.player) continue;
        ally_player = G_GetPlayerByNumber(ally);
        if (!ally_player || !G_GetPlayerAlliance(player, ally_player, ALLIANCE_PASSIVE) ||
            !G_GetPlayerAlliance(ally_player, player, ALLIANCE_PASSIVE)) continue;
        if (!G_BotIsHostile(player, attacker) || !G_BotIsHostile(ally_player, attacker)) continue;
        return attacker->goalentity;
    }
    return NULL;
}

/* BZ_COMPAT_GUESS: redirect only the defense captain to the first actively attacked,
 * mutually allied player's position, then return it to its authored home. */
void G_BotUpdateDefendPlayer(player_t *player) {
    bot_t *bot = player ? G_BotState(PLAYER_NUM(player)) : NULL;
    botCaptain_t *captain;
    edict_t *threat;
    uint32_t now;
    if (!bot) return;
    now = G_Time();
    if ((uint32_t)(now - bot->defend_policy_last_scan) < BOT_DEFEND_PLAYER_SCAN_MS) return;
    bot->defend_policy_last_scan = now;
    captain = bot->captains + BOT_CAPTAIN_DEFENSE;
    threat = (bot->flags & BOT_DEFEND_PLAYER) ? G_BotAlliedThreat(player) : NULL;
    if (threat) {
        bool any = false;
        edict_t *waypoint = Waypoint_add(&threat->s.origin2);
        FOR_EACH_ARRAY(edict_t *, member, captain->units) if (G_BotUnitAlive(*member)) {
            any = true; order_attackmove(*member, waypoint);
        }
        if (any) {
            captain->goal = threat->s.origin2;
            captain->state = BOT_CAPTAIN_ACTIVE;
            bot->defend_player_active = true;
        }
        return;
    }
    if (bot->defend_player_active) {
        edict_t *waypoint = Waypoint_add(&captain->home);
        FOR_EACH_ARRAY(edict_t *, member, captain->units)
            if (G_BotUnitAlive(*member)) { unit_leavecombat(*member); order_move(*member, waypoint); }
        captain->goal = captain->home;
        captain->state = BOT_CAPTAIN_IDLE;
        bot->defend_player_active = false;
    }
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
    G_BotTraceClearWaits(player);
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
    G_BOT_TRACE(playernum, NULL, "script_main_started", "entry=main");
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
        G_BotUpdateGroupFlee(bot->player);
        jass_runevents(bot->vm);
#ifdef WC3_TRACE_AI
        G_BotTraceAssaultMovement(bot);
#endif
        G_BOT_TRACE_WAITS(player);
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
        if (jass_rterror_pending(bot->vm)) {
            fprintf(stderr, "WC3 AI: player %u script %s stopped: %s\n", player, bot->script,
                jass_rterror_message(bot->vm));
            G_BotStop(player);
            continue;
        }
        G_BotUpdateDefendPlayer(bot->player);
        G_BotUpdateIndividualFlee(bot->player);
        G_BotUpdateHeroItems(bot->player);
    }
}
