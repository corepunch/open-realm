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
#define WC3_HERO_BUTTON_DOUBLE_CLICK_MS 500 // milliseconds; matches the existing Hero shortcut double-activation window
#define WC3_HERO_DAMAGE_ALERT_MS 3000 // milliseconds; keeps a damaged Hero conspicuous through several red pulses without rebuilding on expiry

typedef struct {
    uint32_t entity;
    uint32_t time;
} heroShortcutClick_t;

static heroShortcutClick_t hero_shortcut_clicks[MAX_CLIENTS];

static bool G_ShortcutIsControlledMonster(gameClient_t *client, edict_t const *ent) {
    return client && ent && ent->inuse && (ent->svflags & SVF_MONSTER) &&
        G_UnitCanControl(client, ent);
}

bool G_UnitIsWorker(edict_t const *ent) {
    cstring_t builds;

    if (!ent || !ent->data.UnitProfile) return false;
    builds = ent->data.UnitProfile->builds;
    /* Standard workers expose a construction list. Ahar additionally covers
     * harvest-capable custom workers without a build menu. This avoids
     * hard-coding race/unit rawcodes while keeping combat-only resource
     * gatherers out unless their data explicitly makes them builders. */
    return (builds && *builds) || G_ActorHasSkill((edict_t *)ent, "Ahar");
}

bool G_UnitShowsHeroShortcut(gameClient_t *client, edict_t const *ent) {
    return G_ShortcutIsControlledMonster(client, ent) && ent->data.UnitBalance &&
        ent->data.UnitUI && !G_UnitHasFlag(ent, UNIT_FLAG_TRAINING) &&
        (!(ent->s.renderfx & RF_HIDDEN) || M_IsDead(ent)) &&
        !ent->data.UnitUI->hideHeroBar && !(ent->aiflags & AI_ILLUSION) &&
        G_UnitIsHero(ent);
}

bool G_UnitIsIdleWorker(edict_t const *ent) {
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !ent->data.UnitBalance || G_UnitHasFlag(ent, UNIT_FLAG_TRAINING) || G_UnitIsStructure(ent) ||
        M_IsDead(ent) || (ent->s.renderfx & RF_HIDDEN) ||
        S_GoldMineWorkerIsInside(ent) || (ent->orders && ent->orders->holding_position) ||
        !ent->currentmove || ent->currentmove->proc || !ent->currentmove->animation ||
        strcmp(ent->currentmove->animation, "stand")) {
        return false;
    }
    return G_UnitIsWorker(ent);
}

bool G_UnitShowsIdleWorkerShortcut(gameClient_t *client, edict_t const *ent) {
    return G_ShortcutIsControlledMonster(client, ent) && G_UnitIsIdleWorker(ent);
}

void G_InvalidateUnitShortcuts(gameClient_t *client) {
    if (client) client->shortcuts.dirty = true;
}

void G_InvalidateAllUnitShortcuts(void) {
    FOR_LOOP(i, game.max_clients) G_InvalidateUnitShortcuts(game.clients + i);
}

void G_InvalidateUnitShortcutsForUnit(edict_t *ent) {
    /* This hook is also called from generic entity destruction paths. Keep it
     * cheap for projectiles, effects, destructables, and ordinary units so
     * they cannot trigger an unnecessary full shortcut-roster rebuild. */
    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER)) return;
    if ((!ent->data.UnitBalance || !G_UnitIsHero(ent)) &&
        !G_UnitIsWorker(ent)) return;

    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (G_UnitCanControl(client, ent)) G_InvalidateUnitShortcuts(client);
    }
}

/* Record one real-damage alert on an owned Hero and rebuild its shortcut once.
 * The client animates until this absolute deadline, so combat does not create per-frame layout traffic. */
void G_AlertHeroShortcutDamage(edict_t *ent) {
    gameClient_t *owner;

    if (!ent || !ent->inuse || !(ent->svflags & SVF_MONSTER) ||
        !ent->data.UnitBalance || !G_UnitIsHero(ent)) return;
    owner = G_GetPlayerClientByNumber(ent->s.player);
    if (!owner || owner->ps.number != ent->s.player || !G_UnitShowsHeroShortcut(owner, ent)) return;

    /* Damage used to leave the persistent Hero button visually unchanged; carry one expiry timestamp in its next layout instead. */
    ent->unit_state->shortcut_alert_until = G_Time() + WC3_HERO_DAMAGE_ALERT_MS;
    G_InvalidateUnitShortcuts(owner);
}

edict_t *G_GetNextIdleWorker(gameClient_t *client, uint32_t after) {
    uint32_t count = globals.num_edicts;

    if (!client || count <= 1) return NULL;
    if (after >= count) after = 0;

    for (uint32_t i = after + 1; i < count; i++) {
        edict_t *ent = &globals.edicts[i];
        if (G_UnitShowsIdleWorkerShortcut(client, ent)) return ent;
    }
    for (uint32_t i = 1; i <= after && i < count; i++) {
        edict_t *ent = &globals.edicts[i];
        if (G_UnitShowsIdleWorkerShortcut(client, ent)) return ent;
    }
    return NULL;
}

static edict_t *G_GetHeroShortcut(gameClient_t *client, uint32_t slot) {
    edict_t *ordered[WC3_HERO_FUNCTION_KEYS] = { 0 };
    uint32_t seen = 0;

    if (!client || slot >= WC3_HERO_FUNCTION_KEYS) return NULL;
    FILTER_EDICTS(ent, G_UnitShowsHeroShortcut(client, ent)) {
        uint32_t insert;

        if (seen < WC3_HERO_FUNCTION_KEYS) {
            insert = seen;
            ordered[insert] = ent;
        } else if (G_CompareSelectionOrder(ent, ordered[WC3_HERO_FUNCTION_KEYS - 1]) < 0) {
            insert = WC3_HERO_FUNCTION_KEYS - 1;
            ordered[insert] = ent;
        } else {
            seen++;
            continue;
        }

        while (insert > 0 && G_CompareSelectionOrder(ordered[insert], ordered[insert - 1]) < 0) {
            edict_t *swap = ordered[insert - 1];
            ordered[insert - 1] = ordered[insert];
            ordered[insert] = swap;
            insert--;
        }
        seen++;
    }

    return slot < MIN(seen, (uint32_t)WC3_HERO_FUNCTION_KEYS) ? ordered[slot] : NULL;
}

static bool G_SelectShortcutUnit(edict_t *clent, edict_t *target) {
    gameClient_t *client;
    uint32_t bit;

    if (!clent || !(client = clent->client) ||
        !G_UnitCanControl(client, target) || !G_UnitCanBeSelected(client, target)) {
        return false;
    }

    bit = 1u << client->ps.number;
    FILTER_EDICTS(ent, ent->inuse && (ent->selected & bit)) {
        G_DeselectEntity(client, ent);
    }
    G_SelectEntity(client, target);
    G_QueueSelectionSound(target, true);
    G_SyncClientSelection(client);
    return true;
}

static void G_CenterShortcutUnit(edict_t *clent, edict_t const *target) {
    if (!clent || !clent->client || !target || !target->inuse ||
        !G_UnitCanControl(clent->client, target)) return;
    G_ClientSetCameraPosition(clent, &target->s.origin2);
}

static void G_ActivateHeroShortcut(edict_t *clent, edict_t *hero) {
    int32_t client_index;
    heroShortcutClick_t *click;
    uint32_t number;
    uint32_t now;
    bool double_click;

    if (!clent || !clent->client || !hero || !G_UnitShowsHeroShortcut(clent->client, hero)) return;
    client_index = (int32_t)(clent->client - game.clients);
    if (client_index < 0 || client_index >= game.max_clients || client_index >= MAX_CLIENTS) return;

    number = (uint32_t)(hero - globals.edicts);
    click = &hero_shortcut_clicks[client_index];
    now = G_Time();
    double_click = click->entity == number && (uint32_t)(now - click->time) < WC3_HERO_BUTTON_DOUBLE_CLICK_MS;
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

void G_ActivateHeroButton(edict_t *clent, uint32_t number) {
    if (!clent || !clent->client || number >= globals.num_edicts) return;
    G_ActivateHeroShortcut(clent, &globals.edicts[number]);
}

void G_ActivateHeroKey(edict_t *clent, uint32_t slot) {
    edict_t *hero;

    if (!clent || !clent->client) return;
    hero = G_GetHeroShortcut(clent->client, slot);
    if (!hero) return;
    G_ActivateHeroShortcut(clent, hero);
}

void G_ActivateIdleWorkerShortcut(edict_t *clent, uint32_t hinted_number) {
    gameClient_t *client;
    edict_t *worker = NULL;
    uint32_t number;

    if (!clent || !(client = clent->client)) return;

    /* The HUD embeds its precomputed next worker as a hint. Never reuse the
     * worker selected by the previous activation; rapid repeated clicks must
     * still advance even before the dirty layer has crossed the network. */
    if (hinted_number > 0 && hinted_number < globals.num_edicts &&
        hinted_number != client->shortcuts.last_idle_worker) {
        edict_t *hinted = &globals.edicts[hinted_number];
        if (G_UnitShowsIdleWorkerShortcut(client, hinted)) worker = hinted;
    }
    if (!worker) worker = G_GetNextIdleWorker(client, client->shortcuts.last_idle_worker);
    if (!worker) {
        client->shortcuts.last_idle_worker = 0;
        G_InvalidateUnitShortcuts(client);
        return;
    }

    number = (uint32_t)(worker - globals.edicts);
    if (!G_SelectShortcutUnit(clent, worker)) return;
    G_CenterShortcutUnit(clent, worker);
    client->shortcuts.last_idle_worker = number;
    G_InvalidateUnitShortcuts(client);
}

/* The roster cache is runtime-only; clearing it on disconnect, load and
 * rebuild prevents a recycled edict from inheriting another Hero's vitals. */
void G_ClearUnitShortcutVitals(gameClient_t *client) {
    if (!client) return;
    if (client->shortcuts.vitals) gi.MemFree(client->shortcuts.vitals);
    client->shortcuts.vitals = NULL;
    client->shortcuts.vital_count = 0;
}

/* Each unicast contains only authorized, normalized HUD stats: never world
 * position, model or vision. Batch changed values instead of serializing the
 * entire Hero layout for every health/mana regeneration tick. */
static void G_WriteShortcutVitalBatch(edict_t *clent, unitShortcutVital_t const *items, uint32_t count, bool clear) {
    gi.Write(PF_BYTE, &(int32_t){svc_ui_entity_stats});
    gi.Write(PF_BYTE, &(int32_t){clear});
    gi.Write(PF_BYTE, &(int32_t){count});
    FOR_LOOP(i, count) {
        gi.Write(PF_SHORT, &(int32_t){items[i].entity});
        gi.Write(PF_BYTE, &(int32_t){items[i].hp});
        gi.Write(PF_BYTE, &(int32_t){items[i].mana});
        gi.Write(PF_BYTE, &(int32_t){items[i].has_mana});
    }
    gi.unicast(clent);
}

/* Only O(shortcut-roster Heroes) work per client per tick. This path is
 * independent of world snapshot admission, including shared control without
 * shared vision. Validate the whole roster before emitting any HUD values. */
void G_SendUnitShortcutVitals(edict_t *clent, bool clear) {
    gameClient_t *client = clent ? clent->client : NULL;
    unitShortcutState_t *shortcuts;
    unitShortcutVital_t batch[BZ_UI_ENTITY_STAT_BATCH];
    uint32_t count = 0;

    if (!client || !client->connected) return;
    shortcuts = &client->shortcuts;
    FOR_LOOP(i, shortcuts->vital_count) {
        unitShortcutVital_t const *entry = shortcuts->vitals + i;
        if (entry->entity >= globals.num_edicts ||
            !G_UnitShowsHeroShortcut(client, globals.edicts + entry->entity) ||
            globals.edicts[entry->entity].spawn_time != entry->spawn_time) {
            /* Free, owner change or eligibility changes invalidate both the
             * shortcuts and their authorized vital channel. Clear immediately
             * so the client cannot display stale data until the next rebuild. */
            G_WriteShortcutVitalBatch(clent, NULL, 0, true);
            G_InvalidateUnitShortcuts(client);
            return;
        }
    }
    FOR_LOOP(i, shortcuts->vital_count) {
        unitShortcutVital_t *entry = shortcuts->vitals + i;
        edict_t const *hero = globals.edicts + entry->entity;
        uint8_t const hp = compress_stat(&hero->health);
        uint8_t const mana = compress_stat(&hero->mana);
        uint8_t const pool = hero->mana.max_value > 0.0f;
        if (!clear && hp == entry->hp && mana == entry->mana && pool == entry->has_mana) continue;
        entry->hp = hp;
        entry->mana = mana;
        entry->has_mana = pool;
        batch[count++] = *entry;
        if (count == BZ_UI_ENTITY_STAT_BATCH) {
            G_WriteShortcutVitalBatch(clent, batch, count, clear);
            count = 0;
            clear = false;
        }
    }
    if (count || clear) G_WriteShortcutVitalBatch(clent, batch, count, clear);
}

void G_UpdateClientUnitShortcuts(void) {
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        edict_t *clent;

        if (!client->connected) continue;
        clent = G_GetPlayerEntityByNumber(client->ps.number);
        if (!clent || clent->client != client) continue;
        if (client->shortcuts.dirty) {
            client->shortcuts.dirty = false;
            UI_WriteUnitShortcutLayer(clent);
            G_SendUnitShortcutVitals(clent, true);
        } else {
            G_SendUnitShortcutVitals(clent, false);
        }
    }
}
