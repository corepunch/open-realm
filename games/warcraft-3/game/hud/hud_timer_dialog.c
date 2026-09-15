/*
 * hud_timer_dialog.c — Warcraft III TimerDialog presentation.
 *
 * Timer state and JASS handle lifetime live in g_timer.c.  This file owns only
 * the stock TimerDialog.fdf presentation and the dedicated layout layer used to
 * refresh its title/value without resending unrelated HUD panels.
 */

#include "hud_local.h"

static LPTIMERDIALOG UI_VisibleTimerDialog(DWORD client_num) {
    if (client_num >= MAX_CLIENTS) return NULL;
    FOR_LOOP(i, MAX_TIMERDIALOGS) {
        LPTIMERDIALOG dialog = &level.timer_dialogs[i];
        if (dialog->inuse && (dialog->visible_clients & (1u << client_num))) return dialog;
    }
    return NULL;
}

void UI_LoadHudTimerDialogs(void) {
    if (!TimerDialog_Load(&hud.timer_dialog)) return;
    if (hud.timer_dialog.TimerDialogTitle) {
        strlcpy(hud.timer_dialog_default_title,
                hud.timer_dialog.TimerDialogTitle->Text ? hud.timer_dialog.TimerDialogTitle->Text : "",
                sizeof(hud.timer_dialog_default_title));
        hud.timer_dialog_default_title_color = hud.timer_dialog.TimerDialogTitle->Color;
    }
    if (hud.timer_dialog.TimerDialogValue)
        hud.timer_dialog_default_time_color = hud.timer_dialog.TimerDialogValue->Color;
}

void UI_WriteTimerDialogs(LPEDICT ent) {
    LPTIMERDIALOG dialog;
    LPCSTR title;
    char value[32];
    DWORD client_num;

    if (!ent || !ent->client) return;
    client_num = ent->client->ps.number;
    dialog = UI_VisibleTimerDialog(client_num);
    if (!dialog || !hud.timer_dialog.TimerDialog || !hud.timer_dialog.TimerDialogTitle ||
        !hud.timer_dialog.TimerDialogValue) {
        UI_ClearLayer(ent, LAYER_TIMERDIALOG);
        return;
    }

    UI_SetHidden(hud.timer_dialog.TimerDialog, false);
    UI_SetHidden(hud.timer_dialog.TimerDialogTitle, false);
    UI_SetHidden(hud.timer_dialog.TimerDialogValue, false);

    title = dialog->title_set ? dialog->title : hud.timer_dialog_default_title;
    UI_SetTextPointer(hud.timer_dialog.TimerDialogTitle, title && *title ? title : " ");
    hud.timer_dialog.TimerDialogTitle->Color = dialog->title_color_set
        ? dialog->title_color : hud.timer_dialog_default_title_color;
    hud.timer_dialog.TimerDialogValue->Color = dialog->time_color_set
        ? dialog->time_color : hud.timer_dialog_default_time_color;
    G_FormatTimerDialogValue(dialog->timer, value, sizeof(value));
    UI_SetText(hud.timer_dialog.TimerDialogValue, "%s", value);

    UI_SetCurrentClient(ent->client);
    UI_WriteLayout(ent, hud.timer_dialog.TimerDialog, LAYER_TIMERDIALOG);
    UI_SetCurrentClient(NULL);
}
