#include "g_local.h"
#include "jass/jass.h"

static playerAI_t *G_PlayerAIState(DWORD player) {
    return player < MAX_PLAYERS ? &level.player_ai[player] : NULL;
}

/* AI script paths are normally basenames; preserve an explicit archive path when a map supplies one. */
static BOOL G_PlayerAIScriptPath(LPCSTR script, LPSTR path, size_t size) {
    int len;
    if (!script || !*script) return false;
    len = strchr(script, '\\') || strchr(script, '/') ? snprintf(path, size, "%s", script) :
        snprintf(path, size, "Scripts\\%s", script);
    return len >= 0 && (size_t)len < size;
}

void G_PlayerAIStop(DWORD player) {
    playerAI_t *ai = G_PlayerAIState(player);
    if (!ai || !ai->vm) return;
    jass_close(ai->vm);
    memset(ai, 0, sizeof(*ai));
}

/* Removal can originate inside the player's AI coroutine, so teardown waits until that resume returns. */
void G_PlayerAIRequestStop(DWORD player) {
    playerAI_t *ai = G_PlayerAIState(player);
    if (ai && ai->vm) { ai->stop_requested = true; jass_haltevents(ai->vm); }
}

void G_PlayerAIShutdown(void) {
    FOR_LOOP(player, MAX_PLAYERS) G_PlayerAIStop(player);
}

/* Each AI gets a private JASS root because common.ai stores all policy state in globals. */
BOOL G_PlayerAIStart(LPPLAYER player, LPCSTR script, playerAIMode_t mode) {
    playerAI_t *ai;
    char path[MAX_PATHLEN];
    DWORD playernum;

    if (!player || !G_PlayerAIScriptPath(script, path, sizeof(path))) {
        fprintf(stderr, "WC3 AI: invalid player or script\n");
        return false;
    }
    playernum = PLAYER_NUM(player);
    ai = G_PlayerAIState(playernum);
    if (!ai) {
        fprintf(stderr, "WC3 AI: player %u is out of range\n", playernum);
        return false;
    }
    if (ai->vm && jass_isrunning(ai->vm)) {
        ai->restart_requested = true;
        ai->pending_mode = mode;
        strlcpy(ai->pending_script, path, sizeof(ai->pending_script));
        jass_haltevents(ai->vm);
        return true;
    }

    G_PlayerAIStop(playernum);
    /* AI VMs can start before map spawning, which previously left the shared JASS allocator unset. */
    G_InitJassHost();
    ai->vm = jass_newstate();
    ai->player = player;
    ai->mode = mode;
    strlcpy(ai->script, path, sizeof(ai->script));
    if (!jass_dofile(ai->vm, "Scripts\\common.j") || !jass_dofile(ai->vm, "Scripts\\common.ai") ||
        !jass_dofile(ai->vm, path)) {
        fprintf(stderr, "WC3 AI: player %u could not load %s\n", playernum, path);
        G_PlayerAIStop(playernum);
        return false;
    }
    if (!jass_startcoroutinebynameforplayer(ai->vm, "main", player)) {
        fprintf(stderr, "WC3 AI: player %u script %s has no main\n", playernum, path);
        G_PlayerAIStop(playernum);
        return false;
    }
    fprintf(stderr, "WC3 AI: player %u started %s\n", playernum, path);
    return true;
}

void G_PlayerAIPause(DWORD player, BOOL paused) {
    playerAI_t *ai = G_PlayerAIState(player);
    if (ai && ai->vm) ai->paused = paused;
}

void G_PlayerAIRunFrame(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        playerAI_t *ai = level.player_ai + player;
        if (!ai->vm) continue;
        if (ai->stop_requested) { G_PlayerAIStop(player); continue; }
        if (ai->paused) continue;
        jass_runevents(ai->vm);
        if (ai->stop_requested) { G_PlayerAIStop(player); continue; }
        if (ai->restart_requested) {
            LPPLAYER owner = ai->player;
            playerAIMode_t mode = ai->pending_mode;
            char script[MAX_PATHLEN];
            strlcpy(script, ai->pending_script, sizeof(script));
            G_PlayerAIStop(player);
            G_PlayerAIStart(owner, script, mode);
            continue;
        }
        if (!jass_rterror_pending(ai->vm)) continue;
        fprintf(stderr, "WC3 AI: player %u script %s stopped: %s\n", player, ai->script,
            jass_rterror_message(ai->vm));
        G_PlayerAIStop(player);
    }
}
