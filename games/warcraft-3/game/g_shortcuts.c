/*
 * g_shortcuts.c -- Warcraft III persistent Hero / idle-worker shortcuts.
 *
 * The roster is deliberately not polled every frame. Gameplay transitions
 * mark a player's shortcut layer dirty; the next frame rebuilds that layer
 * once. User activations may scan entities because they are discrete input
 * events rather than simulation hot paths.
 */
#include "g_local.h"

#define WC3_HERO_FUNCTION_KEYS 7
#define WC3_HERO_BUTTON_DOUBLE_CLICK_MS 500

typedef struct {
    DWORD entity;
    DWORD time;
} heroShortcutClick_t;

static heroShortcutClick_t hero_shortcut_clicks[MAX_CLIENTS];

static BOOL G_ShortcutIsControlledMonster(LPGAMECLIENT client, LPCEDICT ent) {
    return client && ent && ent->inuse && (ent->svflags & SVF_MONSTER) &&
        G_UnitCanControl(client, ent);
}

static BOOL G_UnitHasWorkerShortcutCapability(LPCEDICT ent) {
    LPCSTR builds;

    if (!ent || !ent->data.UnitProfile) return false;
    builds = ent->data.UnitProfile->builds;
    /* Standard workers expose a construction list. Ahar additionally covers
     * harvest-capable custom workers without a build menu. This avoids
     * hard-coding race/unit rawcodes while keeping combat-only resource
     * gatherers out unless their data explicitly makes them builders. */
    return (builds && *builds) || G_ActorHasSkill((LPEDICT)ent, "Ahar");
}

BOOL G_UnitShowsHeroShortcut(LPGAMECLIENT client, LPCEDICT ent) {
    return G_ShortcutIsControlledMonster(client, ent) && ent->data.UnitBalance &&
        ent->data.UnitUI && !ent->training && !ent->data.UnitUI->hideHeroBar &&
        !(ent->aiflags & AI_ILLUSION) && G_UnitIsHero(ent);
}

BOOL G_UnitIsIdleWorker(LPCEDICT ent) {
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !ent->data.UnitBalance || ent->training || G_UnitIsBuilding(ent->class_id) ||
        M_IsDead(ent) || (ent->s.renderfx & RF_HIDDEN) ||
        S_GoldMineWorkerIsInside(ent) || ent->movement.holding_position ||
        !ent->currentmove || ent->currentmove->ability || !ent->currentmove->animation ||
        strcmp(ent->currentmove->animation, "stand")) {
        return false;
    }
    return G_UnitHasWorkerShortcutCapability(ent);
}

BOOL G_UnitShowsIdleWorkerShortcut(LPGAMECLIENT client, LPCEDICT ent) {
    return G_ShortcutIsControlledMonster(client, ent) && G_UnitIsIdleWorker(ent);
}

void G_InvalidateUnitShortcuts(LPGAMECLIENT client) {
    if (client) client->shortcuts.dirty = true;
}

void G_InvalidateAllUnitShortcuts(void) {
    FOR_LOOP(i, game.max_clients) G_InvalidateUnitShortcuts(game.clients + i);
}

void G_InvalidateUnitShortcutsForUnit(LPEDICT ent) {
    /* This hook is also called from generic entity destruction paths. Keep it
     * cheap for projectiles, effects, destructables, and ordinary units so
     * they cannot trigger an unnecessary full shortcut-roster rebuild. */
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) return;
    if ((!ent->data.UnitBalance || !G_UnitIsHero(ent)) &&
        !G_UnitHasWorkerShortcutCapability(ent)) return;

    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (G_UnitCanControl(client, ent)) G_InvalidateUnitShortcuts(client);
    }
}

LPEDICT G_GetNextIdleWorker(LPGAMECLIENT client, DWORD after) {
    DWORD count = globals.num_edicts;

    if (!client || count <= 1) return NULL;
    if (after >= count) after = 0;

    for (DWORD i = after + 1; i < count; i++) {
        LPEDICT ent = &globals.edicts[i];
        if (G_UnitShowsIdleWorkerShortcut(client, ent)) return ent;
    }
    for (DWORD i = 1; i <= after && i < count; i++) {
        LPEDICT ent = &globals.edicts[i];
        if (G_UnitShowsIdleWorkerShortcut(client, ent)) return ent;
    }
    return NULL;
}

static LPEDICT G_GetHeroShortcut(LPGAMECLIENT client, DWORD slot) {
    DWORD found = 0;

    if (!client || slot >= WC3_HERO_FUNCTION_KEYS) return NULL;
    FILTER_EDICTS(ent, G_UnitShowsHeroShortcut(client, ent)) {
        if (found++ == slot) return ent;
    }
    return NULL;
}

static void G_SendShortcutSelection(LPEDICT clent, LPEDICT target) {
    DWORD number;

    if (!clent || !clent->client || !target || !target->inuse ||
        !clent->client->connected) return;
    number = (DWORD)(target - globals.edicts);
    gi.GameCommand(clent, "select", &number, sizeof(number));
}

static BOOL G_SelectShortcutUnit(LPEDICT clent, LPEDICT target) {
    LPGAMECLIENT client;
    DWORD bit;

    if (!clent || !(client = clent->client) ||
        !G_UnitCanControl(client, target) || !G_UnitCanBeSelected(client, target)) {
        return false;
    }

    bit = 1u << client->ps.number;
    FILTER_EDICTS(ent, ent->inuse && (ent->selected & bit)) {
        G_DeselectEntity(client, ent);
    }
    G_SelectEntity(client, target);
    G_QueueSelectionSound(target);
    G_SendShortcutSelection(clent, target);
    if (client->connected) {
        Get_Portrait_f(clent);
        Get_Commands_f(clent);
    }
    return true;
}

static void G_CenterShortcutUnit(LPEDICT clent, LPCEDICT target) {
    if (!clent || !clent->client || !target || !target->inuse ||
        !G_UnitCanControl(clent->client, target)) return;
    G_ClientSetCameraPosition(clent, &target->s.origin2);
}

static void G_ActivateHeroShortcut(LPEDICT clent, LPEDICT hero) {
    LONG client_index;
    heroShortcutClick_t *click;
    DWORD number;
    DWORD now;
    BOOL double_click;

    if (!clent || !clent->client || !hero || !G_UnitShowsHeroShortcut(clent->client, hero)) return;
    client_index = (LONG)(clent->client - game.clients);
    if (client_index < 0 || client_index >= game.max_clients || client_index >= MAX_CLIENTS) return;

    number = (DWORD)(hero - globals.edicts);
    click = &hero_shortcut_clicks[client_index];
    now = G_Time();
    double_click = click->entity == number && (DWORD)(now - click->time) < WC3_HERO_BUTTON_DOUBLE_CLICK_MS;
    click->entity = number;
    click->time = now;

    /* Hero HUD buttons and F1-F7 share one same-Hero double-activation rule:
     * an isolated activation selects only; a second activation within 500 ms
     * centers the camera. Being already selected does not turn a later single
     * activation into an implicit camera jump. */
    if (double_click)
        G_CenterShortcutUnit(clent, hero);
    else
        G_SelectShortcutUnit(clent, hero);
}

void G_ActivateHeroButton(LPEDICT clent, DWORD number) {
    if (!clent || !clent->client || number >= globals.num_edicts) return;
    G_ActivateHeroShortcut(clent, &globals.edicts[number]);
}

void G_ActivateHeroKey(LPEDICT clent, DWORD slot) {
    LPEDICT hero;

    if (!clent || !clent->client) return;
    hero = G_GetHeroShortcut(clent->client, slot);
    if (!hero) return;
    G_ActivateHeroShortcut(clent, hero);
}

void G_ActivateIdleWorkerShortcut(LPEDICT clent, DWORD hinted_number) {
    LPGAMECLIENT client;
    LPEDICT worker = NULL;
    DWORD number;

    if (!clent || !(client = clent->client)) return;

    /* The HUD embeds its precomputed next worker as a hint. Never reuse the
     * worker selected by the previous activation; rapid repeated clicks must
     * still advance even before the dirty layer has crossed the network. */
    if (hinted_number > 0 && hinted_number < globals.num_edicts &&
        hinted_number != client->shortcuts.last_idle_worker) {
        LPEDICT hinted = &globals.edicts[hinted_number];
        if (G_UnitShowsIdleWorkerShortcut(client, hinted)) worker = hinted;
    }
    if (!worker) worker = G_GetNextIdleWorker(client, client->shortcuts.last_idle_worker);
    if (!worker) {
        client->shortcuts.last_idle_worker = 0;
        G_InvalidateUnitShortcuts(client);
        return;
    }

    number = (DWORD)(worker - globals.edicts);
    if (!G_SelectShortcutUnit(clent, worker)) return;
    G_CenterShortcutUnit(clent, worker);
    client->shortcuts.last_idle_worker = number;
    G_InvalidateUnitShortcuts(client);
}

void G_UpdateClientUnitShortcuts(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT clent;

        if (!client->shortcuts.dirty || !client->connected) continue;
        clent = G_GetPlayerEntityByNumber(client->ps.number);
        if (!clent || clent->client != client) continue;
        client->shortcuts.dirty = false;
        UI_WriteUnitShortcutLayer(clent);
    }
}
