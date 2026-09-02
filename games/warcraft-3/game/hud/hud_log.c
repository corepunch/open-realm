/*
 * hud_log.c — Single-player Warcraft III Message Log dialog.
 *
 * DisplayText* presentation remains transient in hud_cinematic.c.  This file
 * owns the separate bounded history shown by the stock LogDialog.fdf frames.
 */

#include "hud_local.h"
#include "../generated/log_dialog.h"

#define WC3_MESSAGE_LOG_TEXT_SIZE \
    (WC3_MESSAGE_LOG_MAX_ENTRIES * (WC3_MESSAGE_LOG_ENTRY_SIZE + 4) + 1)

static LogDialog_t log_dialog;
static BOOL log_loaded;
static char log_text[WC3_MESSAGE_LOG_TEXT_SIZE];

static BOOL LogEnsureLoaded(void) {
    if (log_loaded) return log_dialog.LogDialog && log_dialog.LogArea && log_dialog.LogOkButton;
    log_loaded = true;
    if (!LogDialog_Load(&log_dialog)) return false;
    UI_CenterFrame(log_dialog.LogDialog);
    UI_SetOnClick(log_dialog.LogOkButton, UI_WINDOW_CLOSE_ACTION);
    return true;
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

    log_text[0] = '\0';
    if (!client) return log_text;

    for (DWORD i = 0; i < client->message_log.count; i++) {
        DWORD index = (client->message_log.first + i) % WC3_MESSAGE_LOG_MAX_ENTRIES;
        LPCSTR separator = i ? "|n|n" : "";
        int written = snprintf(log_text + used, sizeof(log_text) - used,
                               "%s%s", separator, client->message_log.entries[index]);
        if (written < 0) break;
        if ((size_t)written >= sizeof(log_text) - used) {
            used = sizeof(log_text) - 1;
            break;
        }
        used += (size_t)written;
    }
    log_text[used] = '\0';
    return log_text;
}

static void UI_WriteLogWindow(LPEDICT ent) {
    LPGAMECLIENT client;

    if (!ent || !(client = ent->client) || !client->connected) return;
    UI_SetCurrentClient(client);
    if (!LogEnsureLoaded()) {
        UI_SetCurrentClient(NULL);
        return;
    }

    UI_SetTextPointer(log_dialog.LogArea, MessageLogText(client));
    UI_SetOnClick(log_dialog.LogOkButton, UI_WINDOW_CLOSE_ACTION);
    UI_WriteWindow(ent, log_dialog.LogDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_LOG, .class_id = BZ_WC3_WINDOW_LOG,
        .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}

void UI_ShowLog(LPEDICT ent) {
    if (!ent || !ent->client) return;
    UI_WriteLogWindow(ent);
}
