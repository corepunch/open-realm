/*
 * hud_shortcuts.c -- Persistent Warcraft III Hero and idle-worker controls.
 */
#include "hud_local.h"

#define HERO_SHORTCUT_SIZE    0.0340f
#define HERO_SHORTCUT_GAP     0.0030f
#define IDLE_WORKER_X         0.0080f
#define IDLE_WORKER_Y         0.4145f
#define IDLE_WORKER_SIZE      0.0340f

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
                                       cstring_t command, cstring_t tooltip, bool damage_alert) {
    uiFrame_t frame;
    cstring_t art;

    if (!unit || !unit->data.UnitProfile || !(art = unit->data.UnitProfile->art) || !*art) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(art);
    frame.onclick = command;
    frame.tooltip = tooltip;
    if (damage_alert && unit->hero_shortcut_alert_until > G_Time()) {
        frame.flagsvalue |= UIFLAG_ALERT_RED_PULSE;
        frame.value = (float)unit->hero_shortcut_alert_until;
    }
    UI_SetShortcutRect(&frame, parent, x, y, size, size);
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

    FOR_LOOP(hero_slot, hero_count) {
        edict_t *unit = heroes[hero_slot];
        uint32_t number = (uint32_t)(unit - globals.edicts);
        cstring_t name = unit->data.UnitProfile && unit->data.UnitProfile->name
            ? G_LevelString(unit->data.UnitProfile->name) : "Hero";

        snprintf(command, sizeof(command), "herobutton %u", (unsigned)number);
        snprintf(tooltip, sizeof(tooltip), "Select %s", name && *name ? name : "Hero");
        UI_WriteUnitShortcutButton(shortcut_root, HUD_HERO_SHORTCUT_EDGE_X,
                                   HUD_HERO_SHORTCUT_TOP_Y + hero_slot * (HERO_SHORTCUT_SIZE + HERO_SHORTCUT_GAP),
                                   HERO_SHORTCUT_SIZE, unit, command, tooltip, unit->s.player == client->ps.number);
        UI_WriteShortcutNumber(shortcut_root, HUD_HERO_SHORTCUT_EDGE_X,
                               HUD_HERO_SHORTCUT_TOP_Y + hero_slot * (HERO_SHORTCUT_SIZE + HERO_SHORTCUT_GAP),
                               HERO_SHORTCUT_SIZE, HERO_SHORTCUT_SIZE, unit->hero.skillpoints);
    }

    if (!next_idle) next_idle = wrap_idle;
    if (idle_count && next_idle) {
        uint32_t number = (uint32_t)(next_idle - globals.edicts);
        snprintf(command, sizeof(command), "idleworker %u", (unsigned)number);
        UI_WriteUnitShortcutButton(shortcut_root, IDLE_WORKER_X, IDLE_WORKER_Y, IDLE_WORKER_SIZE,
                                   next_idle, command, "Select Idle Worker", false);
        UI_WriteShortcutNumber(shortcut_root, IDLE_WORKER_X, IDLE_WORKER_Y,
                               IDLE_WORKER_SIZE, IDLE_WORKER_SIZE, idle_count);
    }

    UI_WriteEnd(clent);
    UI_SetCurrentClient(NULL);
    gi.MemFree(heroes);
}
