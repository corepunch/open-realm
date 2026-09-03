/*
 * hud_log.c — Single-player Warcraft III Message Log dialog.
 *
 * DisplayText* presentation remains transient in hud_cinematic.c.  This file
 * owns the separate bounded history shown by the stock LogDialog.fdf frames.
 */

#include "hud_local.h"

void UI_LoadHudLog(void) {
    if (hud.log.LogDialog) return;
    if (!LogDialog_Load(&hud.log)) return;
    UI_CenterFrame(hud.log.LogDialog);
    UI_SetOnClick(hud.log.LogOkButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
}

void UI_MessageLogAppend(LPEDICT ent, LPCSTR text) {
    LPGAMECLIENT client;
    DWORD index;

    if (!ent || !(client = ent->client) || !text || !*text) return;

    if (client->message_log.count < WC3_MESSAGE_LOG_MAX_ENTRIES) {
        index = (client->message_log.first + client->message_log.count) % WC3_MESSAGE_LOG_MAX_ENTRIES;
        client->message_log.count++;
    } else {
        index = client->message_log.first;
        client->message_log.first = (client->message_log.first + 1) % WC3_MESSAGE_LOG_MAX_ENTRIES;
    }
    snprintf(client->message_log.entries[index], WC3_MESSAGE_LOG_ENTRY_SIZE, "%s", text);
}

static LPCSTR MessageLogText(LPGAMECLIENT client) {
    size_t used = 0;

    hud.log_text[0] = '\0';
    if (!client) return hud.log_text;

    for (DWORD i = 0; i < client->message_log.count; i++) {
        DWORD index = (client->message_log.first + i) % WC3_MESSAGE_LOG_MAX_ENTRIES;
        LPCSTR separator = i ? "|n|n" : "";
        int written = snprintf(hud.log_text + used, sizeof(hud.log_text) - used,
                               "%s%s", separator, client->message_log.entries[index]);
        if (written < 0) break;
        if ((size_t)written >= sizeof(hud.log_text) - used) {
            used = sizeof(hud.log_text) - 1;
            break;
        }
        used += (size_t)written;
    }
    hud.log_text[used] = '\0';
    return hud.log_text;
}

static void UI_WriteLogWindow(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || !(client = ent->client) || !client->connected) return;
    UI_SetCurrentClient(client);
    if (!hud.log.LogDialog) {
        UI_SetCurrentClient(NULL);
        return;
    }

    UI_SetTextPointer(hud.log.LogArea, MessageLogText(client));
    UI_SetOnClick(hud.log.LogOkButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);
    UI_WriteWindow(ent, hud.log.LogDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_LOG, .class_id = BZ_WC3_WINDOW_LOG,
        .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}

void UI_ShowLog(LPEDICT ent) {
    if (!ent || !ent->client) return;
    UI_WriteLogWindow(ent);
}
