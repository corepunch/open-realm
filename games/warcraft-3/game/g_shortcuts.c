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

static BOOL G_ShortcutIsControlledMonster(LPGAMECLIENT client, LPCEDICT ent) {
    return client && ent && ent->inuse && (ent->svflags & SVF_MONSTER) &&
        G_UnitCanControl(client, ent);
}

static BOOL G_UnitHasWorkerShortcutCapability(LPCEDICT ent) {
    LPCSTR builds;

    if (!ent || !ent->UnitProfile) return false;
    builds = ent->UnitProfile->builds;
    /* Standard workers expose a construction list. Ahar additionally covers
     * harvest-capable custom workers without a build menu. This avoids
     * hard-coding race/unit rawcodes while keeping combat-only resource
     * gatherers out unless their data explicitly makes them builders. */
    return (builds && *builds) || G_ActorHasSkill((LPEDICT)ent, "Ahar");
}

BOOL G_UnitShowsHeroShortcut(LPGAMECLIENT client, LPCEDICT ent) {
    return G_ShortcutIsControlledMonster(client, ent) && ent->UnitBalance &&
        ent->UnitUI && !ent->training && !ent->UnitUI->hideHeroBar &&
        G_UnitIsHero(ent);
}

BOOL G_UnitIsIdleWorker(LPCEDICT ent) {
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !ent->UnitBalance || ent->training || G_UnitIsBuilding(ent->class_id) ||
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
    if ((!ent->UnitBalance || !G_UnitIsHero(ent)) &&
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

static BOOL G_SoleSelectedUnitIs(LPGAMECLIENT client, LPCEDICT target) {
    DWORD bit;
    DWORD selected = 0;
    LPCEDICT only = NULL;

    if (!client || !target) return false;
    bit = 1u << client->ps.number;
    FILTER_EDICTS(ent, ent->inuse && (ent->selected & bit)) {
        if (!G_IsEntitySelected(client, ent)) continue;
        selected++;
        only = ent;
        if (selected > 1) return false;
    }
    return selected == 1 && only == target;
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

void G_ActivateHeroButton(LPEDICT clent, DWORD number) {
    LPEDICT hero;

    if (!clent || !clent->client || number >= globals.num_edicts) return;
    hero = &globals.edicts[number];
    if (!G_UnitShowsHeroShortcut(clent->client, hero)) return;

    /* Mouse Hero shortcuts are direct navigation controls: select when the
     * Hero is alive/selectable, and always center on its current position. */
    G_SelectShortcutUnit(clent, hero);
    G_CenterShortcutUnit(clent, hero);
}

void G_ActivateHeroKey(LPEDICT clent, DWORD slot) {
    LPEDICT hero;

    if (!clent || !clent->client) return;
    hero = G_GetHeroShortcut(clent->client, slot);
    if (!hero) return;

    /* Classic function-key semantics: first press selects; pressing the same
     * Hero shortcut while it is the sole selection centers the camera. Dead
     * Heroes cannot be selected, so the key remains useful as camera focus. */
    if (G_SoleSelectedUnitIs(clent->client, hero) || !G_UnitCanBeSelected(clent->client, hero)) {
        G_CenterShortcutUnit(clent, hero);
    } else {
        G_SelectShortcutUnit(clent, hero);
    }
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
