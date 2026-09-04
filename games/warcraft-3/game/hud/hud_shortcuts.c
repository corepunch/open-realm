/*
 * hud_shortcuts.c -- Persistent Warcraft III Hero and idle-worker controls.
 */
#include "hud_local.h"

#define HERO_SHORTCUT_X       0.0060f
#define HERO_SHORTCUT_Y       0.0350f
#define HERO_SHORTCUT_SIZE    0.0340f
#define HERO_SHORTCUT_GAP     0.0030f
#define IDLE_WORKER_X         0.0080f
#define IDLE_WORKER_Y         0.4145f
#define IDLE_WORKER_SIZE      0.0340f

static void UI_WriteShortcutNumber(FLOAT x, FLOAT y, FLOAT w, FLOAT h, DWORD number) {
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
    UI_SetFrameRect(&frame, x + 0.001f, y + 0.001f, w - 0.002f, h - 0.002f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteUnitShortcutButton(FLOAT x, FLOAT y, FLOAT size, LPCEDICT unit,
                                       LPCSTR command, LPCSTR tooltip) {
    uiFrame_t frame;
    LPCSTR art;

    if (!unit || !unit->data.UnitProfile || !(art = unit->data.UnitProfile->art) || !*art) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(art);
    frame.onclick = command;
    frame.tooltip = tooltip;
    UI_SetFrameRect(&frame, x, y, size, size);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteUnitShortcutLayer(LPEDICT clent) {
    LPGAMECLIENT client;
    LPEDICT next_idle = NULL;
    LPEDICT wrap_idle = NULL;
    DWORD hero_slot = 0;
    DWORD idle_count = 0;
    char command[64];
    char tooltip[128];

    if (!clent || !(client = clent->client)) return;

    UI_SetCurrentClient(client);
    UI_WriteStart(LAYER_UNIT_SHORTCUTS);

    /* One entity pass per dirty rebuild: emit Hero buttons while also counting
     * workers and choosing the next cycle target. */
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT unit = &globals.edicts[i];

        if (G_UnitShowsHeroShortcut(client, unit)) {
            DWORD number = (DWORD)(unit - globals.edicts);
            LPCSTR name = unit->data.UnitProfile && unit->data.UnitProfile->name
                ? G_LevelString(unit->data.UnitProfile->name) : "Hero";

            snprintf(command, sizeof(command), "herobutton %u", (unsigned)number);
            snprintf(tooltip, sizeof(tooltip), "Select %s", name && *name ? name : "Hero");
            UI_WriteUnitShortcutButton(HERO_SHORTCUT_X,
                                       HERO_SHORTCUT_Y + hero_slot * (HERO_SHORTCUT_SIZE + HERO_SHORTCUT_GAP),
                                       HERO_SHORTCUT_SIZE, unit, command, tooltip);
            hero_slot++;
        }

        if (G_UnitShowsIdleWorkerShortcut(client, unit)) {
            idle_count++;
            if (!wrap_idle) wrap_idle = unit;
            if (!next_idle && i > client->shortcuts.last_idle_worker) next_idle = unit;
        }
    }
    if (!next_idle) next_idle = wrap_idle;
    if (idle_count && next_idle) {
        DWORD number = (DWORD)(next_idle - globals.edicts);
        snprintf(command, sizeof(command), "idleworker %u", (unsigned)number);
        UI_WriteUnitShortcutButton(IDLE_WORKER_X, IDLE_WORKER_Y, IDLE_WORKER_SIZE,
                                   next_idle, command, "Select Idle Worker");
        UI_WriteShortcutNumber(IDLE_WORKER_X, IDLE_WORKER_Y,
                               IDLE_WORKER_SIZE, IDLE_WORKER_SIZE, idle_count);
    }

    UI_WriteEnd(clent);
    UI_SetCurrentClient(NULL);
}
