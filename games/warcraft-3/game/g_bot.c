#include "g_local.h"
#include "jass/jass.h"

static bot_t *G_BotState(DWORD player) {
    return player < MAX_PLAYERS ? &level.bots[player] : NULL;
}

/* KillUnit changes life immediately while ordinary death also carries SVF_DEADMONSTER. */
BOOL G_BotUnitAlive(LPEDICT unit) {
    return unit && unit->inuse && unit->health.value > 0 && !(unit->svflags & SVF_DEADMONSTER);
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
    if (!bot || !bot->vm) return;
    jass_close(bot->vm);
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
    if (!jass_dofile(bot->vm, "Scripts\\common.j") || !jass_dofile(bot->vm, "Scripts\\common.ai") ||
        !jass_dofile(bot->vm, path)) {
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
