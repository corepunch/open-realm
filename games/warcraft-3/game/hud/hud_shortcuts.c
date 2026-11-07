/*
 * hud_shortcuts.c -- Persistent Warcraft III Hero and idle-worker controls.
 */
#include "hud_local.h"

static uint32_t UI_WriteShortcutRoot(void) {
    uiFrame_t frame;
    uint32_t number = ui_next_frame_number;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLEFRAME;
    frame.flagsvalue |= UIFLAG_EXTEND_WIDESCREEN_X;
    UI_SetFrameRect(&frame, 0.0f, 0.0f, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    UI_WriteProxyFrame(&frame, NULL, 0);
    return number;
}

static void UI_SetShortcutRect(uiFrame_t *frame, uint32_t parent,
                               float x, float y, float w, float h) {
    frame->parent = parent;
    UI_SetFrameRect(frame, x, y, w, h);
    frame->points.x[FPP_MIN].relativeTo = UI_PARENT;
    frame->points.y[FPP_MIN].relativeTo = UI_PARENT;
}

static void UI_WriteShortcutNumber(uint32_t parent, float x, float y, float w, float h, uint32_t number) {
    uiFrame_t frame;
    uiLabel_t label;
    char text[16];

    if (!number) return;
    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    snprintf(text, sizeof(text), "%u", (unsigned)number);
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYRIGHT;
    label.textaligny = FONT_JUSTIFYBOTTOM;
    UI_SetShortcutRect(&frame, parent, x + 0.001f, y + 0.001f, w - 0.002f, h - 0.002f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteUnitShortcutButton(uint32_t parent, float x, float y, float size, edict_t const *unit,
                                       cstring_t command, cstring_t tooltip, bool damage_alert,
                                       bool gamepad_status_icon) {
    uiFrame_t frame;
    cstring_t art;

    if (!unit || !unit->data.UnitProfile || !(art = unit->data.UnitProfile->art) || !*art) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    /* Dead Heroes stay in the roster so their button can still locate the
     * corpse. Dim the portrait while dead and restore it on revival. */
    frame.color = G_UnitIsHero(unit) && M_IsDead(unit)
        ? MAKE(color32_t, 128, 128, 128, 255) : COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(art);
    frame.onclick = command;
    frame.tooltip = tooltip;
    if (gamepad_status_icon) frame.flagsvalue |= UIFLAG_GAMEPAD_STATUS_ICON;
    if (damage_alert && unit->unit_state->shortcut_alert_until > G_Time()) {
        frame.flagsvalue |= UIFLAG_ALERT_RED_PULSE;
        frame.value = (float)unit->unit_state->shortcut_alert_until;
    }
    UI_SetShortcutRect(&frame, parent, x, y, size, size);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Bars bind to the client's HUD-only vital cache, independent of world
 * snapshots. Keep the art and tint identical to multiselect status bars. */
static void UI_WriteHeroShortcutBar(uint32_t parent, float x, float y, edict_t const *hero, uint32_t vital) {
    uiFrame_t frame = {0};

    /* Match the hover bars: a black backing gives the colored fill a clear
     * border and keeps the empty portion legible over the world. */
    frame.flags.type = FT_TEXTURE;
    frame.tex.index = gi.ImageIndex("Textures\\Black32.blp");
    frame.color = MAKE(color32_t, 0, 0, 0, 220);
    UI_SetShortcutRect(&frame, parent, x - 0.001f, y - 0.001f, 0.0360f, 0.0060f);
    UI_WriteProxyFrame(&frame, NULL, 0);

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR;
    frame.flagsvalue |= UIFLAG_ENTITY_STATUSBAR;
    if (vital == ENT_HEALTH) frame.flagsvalue |= UIFLAG_HEALTH_GRADIENT;
    frame.stat = vital;
    frame.value = (float)hero->s.number;
    frame.tex.index = gi.ImageIndex(vital == ENT_HEALTH ? "SimpleHpBarConsole" : "SimpleManaBarConsole");
    frame.color = vital == ENT_HEALTH ? MAKE(color32_t, 0, 255, 0, 255) : MAKE(color32_t, 0, 255, 255, 255);
    UI_SetShortcutRect(&frame, parent, x, y, 0.0340f, 0.0040f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteUnitShortcutLayer(edict_t *clent) {
    gameClient_t *client;
    edict_t *next_idle = NULL;
    edict_t *wrap_idle = NULL;
    edict_t * *heroes;
    uint32_t hero_count = 0;
    uint32_t idle_count = 0;
    uint32_t shortcut_root;
    char command[64];
    char tooltip[128];

    if (!clent || !(client = clent->client)) return;

    heroes = gi.MemAlloc(MAX(1u, globals.num_edicts) * sizeof(*heroes));
    UI_SetCurrentClient(client);
    UI_WriteStart(LAYER_UNIT_SHORTCUTS);
    shortcut_root = UI_WriteShortcutRoot();

    /* One entity pass per dirty rebuild: collect Hero buttons while also
     * counting workers and choosing the next cycle target. Hero emission is
     * deferred so the roster can use the exact same stable ordering as the
     * multiselect status panel. */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *unit = &globals.edicts[i];

        if (G_UnitShowsHeroShortcut(client, unit)) {
            uint32_t insert = hero_count++;

            heroes[insert] = unit;
            while (insert > 0 && G_CompareSelectionOrder(heroes[insert], heroes[insert - 1]) < 0) {
                edict_t *swap = heroes[insert - 1];
                heroes[insert - 1] = heroes[insert];
                heroes[insert] = swap;
                insert--;
            }
        }

        if (G_UnitShowsIdleWorkerShortcut(client, unit)) {
            idle_count++;
            if (!wrap_idle) wrap_idle = unit;
            if (!next_idle && i > client->shortcuts.last_idle_worker) next_idle = unit;
        }
    }

    /* Replace the whole cached roster on an invalidation, not on each tick.
     * The game sends small vital deltas from these entries independently of
     * world-visibility snapshots; shared control never grants extra vision. */
    G_ClearUnitShortcutVitals(client);
    if (hero_count) client->shortcuts.vitals = gi.MemAlloc(hero_count * sizeof(*client->shortcuts.vitals));
    /* Keep the buttons and their existing interactions available if only the
     * optional HUD cache allocation fails. The bars stay unbound until the
     * next dirty rebuild can create a cache. */
    client->shortcuts.vital_count = client->shortcuts.vitals ? hero_count : 0;
    FOR_LOOP(hero_slot, hero_count) {
        edict_t *unit = heroes[hero_slot];
        uint32_t const number = (uint32_t)(unit - globals.edicts);
        float const y = HUD_HERO_SHORTCUT_TOP_Y + hero_slot * 0.0460f;
        float const x = HUD_HERO_SHORTCUT_EDGE_X;
        bool const owned = unit->s.player == client->ps.number;
        cstring_t name = unit->data.UnitProfile && unit->data.UnitProfile->name
            ? G_LevelString(unit->data.UnitProfile->name) : "Hero";

        if (client->shortcuts.vitals)
            client->shortcuts.vitals[hero_slot] = (unitShortcutVital_t){ .entity = number, .spawn_time = unit->spawn_time };
        snprintf(command, sizeof(command), "herobutton %u", (unsigned)number);
        snprintf(tooltip, sizeof(tooltip), "Select %s", name && *name ? name : "Hero");
        UI_WriteUnitShortcutButton(shortcut_root, x, y, 0.0340f, unit, command, tooltip, owned, true);
        UI_WriteShortcutNumber(shortcut_root, x, y, 0.0340f, 0.0340f, unit->unit_state->hero.skillpoints);
        /* Two passive bars always exist; a zero-max-mana Hero hides its mana
         * bar at render time, so a later acquired mana pool needs no rebuild. */
        UI_WriteHeroShortcutBar(shortcut_root, x, y + 0.0350f, unit, ENT_HEALTH);
        UI_WriteHeroShortcutBar(shortcut_root, x, y + 0.0400f, unit, ENT_MANA);
    }

    if (!next_idle) next_idle = wrap_idle;
    if (idle_count && next_idle) {
        uint32_t number = (uint32_t)(next_idle - globals.edicts);
        snprintf(command, sizeof(command), "idleworker %u", (unsigned)number);
        UI_WriteUnitShortcutButton(shortcut_root, WC3_HUD_IDLE_WORKER_X, WC3_HUD_IDLE_WORKER_Y, WC3_HUD_IDLE_WORKER_SIZE,
                                   next_idle, command, "Select Idle Worker", false, false);
        UI_WriteShortcutNumber(shortcut_root, WC3_HUD_IDLE_WORKER_X, WC3_HUD_IDLE_WORKER_Y,
                               WC3_HUD_IDLE_WORKER_SIZE, WC3_HUD_IDLE_WORKER_SIZE, idle_count);
    }

    UI_WriteEnd(clent);
    UI_SetCurrentClient(NULL);
    gi.MemFree(heroes);
}
