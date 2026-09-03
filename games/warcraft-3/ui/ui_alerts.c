#include <string.h>

#include "ui_local.h"
#include "games/warcraft-3/common/alerts.h"

#define WC3_DEFAULT_MINIMAP_INDICATOR "UI\\Minimap\\Minimap-Ping.mdl"

typedef struct {
    BOOL active;
    wc3MinimapPing_t ping;
    DWORD start_time;
    DWORD model_index;
} wc3ActiveMinimapPing_t;

static wc3ActiveMinimapPing_t active_pings[WC3_ACTIVE_MINIMAP_PINGS];
static VECTOR2 recent_alerts[WC3_RECENT_ALERT_COUNT];
static DWORD recent_alert_count;
static DWORD recent_alert_cursor;

void UI_AlertsClear(void) {
    memset(active_pings, 0, sizeof(active_pings));
    memset(recent_alerts, 0, sizeof(recent_alerts));
    recent_alert_count = 0;
    recent_alert_cursor = 0;
}

static void UI_AlertsRemember(LPCVECTOR2 position) {
    DWORD move;

    if (!position) return;
    move = MIN(recent_alert_count, WC3_RECENT_ALERT_COUNT - 1);
    if (move) {
        memmove(&recent_alerts[1], &recent_alerts[0], move * sizeof(recent_alerts[0]));
    }
    recent_alerts[0] = *position;
    recent_alert_count = MIN(recent_alert_count + 1, WC3_RECENT_ALERT_COUNT);
    recent_alert_cursor = 0;
}

static void UI_AlertsAddPing(wc3MinimapPing_t const *ping) {
    DWORD slot = WC3_ACTIVE_MINIMAP_PINGS;
    DWORD oldest = 0;
    DWORD oldest_age = 0;
    DWORD now = UI_GameTime();

    if (!ping || ping->duration <= 0.0f) return;
    FOR_LOOP(i, WC3_ACTIVE_MINIMAP_PINGS) {
        if (!active_pings[i].active) {
            slot = i;
            break;
        }
        DWORD age = now - active_pings[i].start_time;
        if (slot == WC3_ACTIVE_MINIMAP_PINGS && age >= oldest_age) {
            oldest = i;
            oldest_age = age;
        }
    }
    if (slot == WC3_ACTIVE_MINIMAP_PINGS) slot = oldest;

    active_pings[slot].active = true;
    active_pings[slot].ping = *ping;
    active_pings[slot].ping.model[MAX_PATHLEN - 1] = '\0';
    active_pings[slot].start_time = now;
    active_pings[slot].model_index = UI_LoadModel(
        active_pings[slot].ping.model[0] ? active_pings[slot].ping.model : WC3_DEFAULT_MINIMAP_INDICATOR,
        false);

    if (ping->flags & WC3_MINIMAP_PING_REMEMBER) {
        UI_AlertsRemember(&ping->position);
    }
}

void UI_AlertsGameCommand(LPCSTR command, void const *data, DWORD size) {
    if (!command) return;
    if (!strcmp(command, "minimap_ping")) {
        wc3MinimapPing_t ping;
        if (!data || size != sizeof(ping)) return;
        memcpy(&ping, data, sizeof(ping));
        ping.model[MAX_PATHLEN - 1] = '\0';
        UI_AlertsAddPing(&ping);
    }
}

DWORD UI_AlertsGameplayKeyEvent(int key, DWORD modifiers, BOOL repeat, LPVECTOR2 camera_position) {
    (void)modifiers;
    if (key != ' ') return 0;
    if (repeat) return UI_GAMEKEY_HANDLED;

    if (recent_alert_count) {
        if (recent_alert_cursor >= recent_alert_count) recent_alert_cursor = 0;
        if (camera_position) *camera_position = recent_alerts[recent_alert_cursor];
        recent_alert_cursor++;
        if (recent_alert_cursor >= recent_alert_count) recent_alert_cursor = 0;
        return UI_GAMEKEY_HANDLED | UI_GAMEKEY_CAMERA_POSITION;
    }
    if (uiimport.ServerCommand) uiimport.ServerCommand("quickcamera");
    return UI_GAMEKEY_HANDLED;
}

void UI_AlertsDraw(void) {
    LPRENDERER renderer;
    DWORD now;

    if (!uiimport.GetRenderer) return;
    renderer = uiimport.GetRenderer();
    if (!renderer || !renderer->WorldToMinimap || !renderer->DrawSprite) return;
    now = UI_GameTime();

    FOR_LOOP(i, WC3_ACTIVE_MINIMAP_PINGS) {
        wc3ActiveMinimapPing_t *active = &active_pings[i];
        DWORD duration_ms;
        VECTOR2 screen;
        LPCMODEL model;

        if (!active->active) continue;
        duration_ms = (DWORD)MAX(1.0f, active->ping.duration * 1000.0f);
        if (now - active->start_time >= duration_ms) {
            active->active = false;
            continue;
        }
        if (!renderer->WorldToMinimap(&active->ping.position, &screen)) continue;
        model = UI_GetModel(active->model_index);
        if (!model) continue;

        /* HACK: svc_layout cannot express a transient authored MDX at a
         * client-local world->minimap projection. Keep the exception isolated
         * in the game UI overlay instead of teaching the shared HUD about WC3. */
        renderer->DrawSprite(model, "Stand", screen.x, screen.y);
    }
}
