/*
 * hud_timer_dialog.c — Warcraft III TimerDialog presentation.
 *
 * Timer state and JASS handle lifetime live in g_timer.c.  This file owns only
 * the stock TimerDialog.fdf presentation and the dedicated layout layer used to
 * refresh its title/value without resending unrelated HUD panels.
 */

#include "hud_local.h"

#define BZ_WC3_TIMER_DIALOG_EDGE_INSET 0.006f // normalized UI units; keeps measured title/value inside the backdrop
#define BZ_WC3_TIMER_DIALOG_MIN_WIDTH  0.060f // normalized UI units; preserves a readable stock timer strip

typedef struct {
    LPFRAMEDEF frame;
    uiFramePointPos_t point;
    LPCFRAMEDEF relative;
    uiFramePointPos_t target;
    FLOAT offset;
} timerDialogPointParams_t;

static void TimerDialogSetHorizontalPoint(timerDialogPointParams_t const *params) {
    if (!params || !params->frame) return;
    memset(&params->frame->Points.x, 0, sizeof(params->frame->Points.x));
    params->frame->Points.x[params->point].used = true;
    params->frame->Points.x[params->point].relativeTo = params->relative;
    params->frame->Points.x[params->point].targetPos = params->target;
    params->frame->Points.x[params->point].offset = params->offset;
    params->frame->Width = 0.0f;
    params->frame->AnyPointsSet = true;
}

static DWORD TimerDialogMeasureFont(void) {
    LPFRAMEDEF title = hud.timer_dialog.TimerDialogTitle;
    LPFRAMEDEF value = hud.timer_dialog.TimerDialogValue;
    LPFRAMEDEF measure = title;

    DWORD font;

    if (!measure || (value && value->Font.Size > measure->Font.Size)) measure = value;
    font = measure ? UI_LiveFont(measure->Font.Index) : 0;
    return font ? font : gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
}

static LPTIMERDIALOG UI_VisibleTimerDialog(DWORD player_num) {
    if (player_num >= MAX_CLIENTS) return NULL;
    FOR_LOOP(i, MAX_TIMERDIALOGS) {
        LPTIMERDIALOG dialog = &level.timer_dialogs[i];
        if (dialog->inuse && (dialog->visible_clients & (1u << player_num))) return dialog;
    }
    return NULL;
}

FLOAT UI_TimerDialogLeaderboardOffset(DWORD client_num) {
    if (!UI_VisibleTimerDialog(client_num) || !hud.timer_dialog.TimerDialog) return 0.0f;
    return hud.timer_dialog.TimerDialog->Height + BZ_WC3_HUD_TIMER_DIALOG_STACK_GAP;
}

void UI_LoadHudTimerDialogs(void) {
    timerDialogPointParams_t title_params = {
        .frame = hud.timer_dialog.TimerDialogTitle, .point = FPP_MIN,
        .relative = hud.timer_dialog.TimerDialog, .target = FPP_MIN,
        .offset = BZ_WC3_TIMER_DIALOG_EDGE_INSET,
    };
    timerDialogPointParams_t value_params = {
        .frame = hud.timer_dialog.TimerDialogValue, .point = FPP_MAX,
        .relative = hud.timer_dialog.TimerDialog, .target = FPP_MAX,
        .offset = -BZ_WC3_TIMER_DIALOG_EDGE_INSET,
    };

    if (!TimerDialog_Load(&hud.timer_dialog)) {
        fprintf(stderr, "WC3 HUD: missing TimerDialog.fdf\n");
        return;
    }

    /* Match the Hero shortcut layer's full-screen horizontal canvas so the
     * mirrored timer stays against the actual right edge on widescreen too. */
    memset(&hud.timer_dialog_anchor, 0, sizeof(hud.timer_dialog_anchor));
    hud.timer_dialog_anchor.Type = FT_SIMPLEFRAME;
    hud.timer_dialog_anchor.ui_flags |= UIFLAG_EXTEND_WIDESCREEN_X;
    UI_SetSize(&hud.timer_dialog_anchor, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    UI_SetPoint(&hud.timer_dialog_anchor,
                FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);

    /* Align the timer with the first Hero shortcut vertically, mirrored to the
     * opposite screen edge. */
    if (hud.timer_dialog.TimerDialog) {
        memset(&hud.timer_dialog.TimerDialog->Points, 0,
               sizeof(hud.timer_dialog.TimerDialog->Points));
        hud.timer_dialog.TimerDialog->AnyPointsSet = false;
        UI_SetPoint(hud.timer_dialog.TimerDialog,
                    FRAMEPOINT_TOPRIGHT, &hud.timer_dialog_anchor, FRAMEPOINT_TOPRIGHT,
                    -HUD_HERO_SHORTCUT_EDGE_X, -HUD_HERO_SHORTCUT_TOP_Y);
    }

    /* The retail timer is a compact strip. Let the client measure the actual
     * proportional title/time glyphs, while the two labels stay pinned to
     * opposite edges of the resulting strip. */
    if (hud.timer_dialog.TimerDialogBackdrop && hud.timer_dialog.TimerDialog) {
        memset(&hud.timer_dialog.TimerDialogBackdrop->Points, 0,
               sizeof(hud.timer_dialog.TimerDialogBackdrop->Points));
        hud.timer_dialog.TimerDialogBackdrop->AnyPointsSet = false;
        UI_SetPoint(hud.timer_dialog.TimerDialogBackdrop,
                    FRAMEPOINT_TOPLEFT, hud.timer_dialog.TimerDialog, FRAMEPOINT_TOPLEFT,
                    0.0f, 0.0f);
        UI_SetPoint(hud.timer_dialog.TimerDialogBackdrop,
                    FRAMEPOINT_BOTTOMRIGHT, hud.timer_dialog.TimerDialog, FRAMEPOINT_BOTTOMRIGHT,
                    0.0f, 0.0f);
    }
    title_params.frame = hud.timer_dialog.TimerDialogTitle;
    title_params.relative = hud.timer_dialog.TimerDialog;
    value_params.frame = hud.timer_dialog.TimerDialogValue;
    value_params.relative = hud.timer_dialog.TimerDialog;
    TimerDialogSetHorizontalPoint(&title_params);
    TimerDialogSetHorizontalPoint(&value_params);

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
    char measure[MAX_TRIGSTR_LENGTH + sizeof(value) + 8];
    uiSizeToTextParams_t size_params;
    DWORD player_num;

    if (!ent || !ent->client) return;
    player_num = ent->client->ps.number;
    dialog = UI_VisibleTimerDialog(player_num);
    if (!dialog || !hud.timer_dialog.TimerDialog || !hud.timer_dialog.TimerDialogTitle ||
        !hud.timer_dialog.TimerDialogValue) {
        WC3_TIMERDIALOG_LOG("hud clear player=%u client_edict=%u dialog=%d frame=%d title=%d value=%d\n",
                            (unsigned)player_num, (unsigned)ent->s.number, dialog != NULL,
                            hud.timer_dialog.TimerDialog != NULL,
                            hud.timer_dialog.TimerDialogTitle != NULL,
                            hud.timer_dialog.TimerDialogValue != NULL);
        UI_ClearLayer(ent, WC3_LAYER_TIMERDIALOG);
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
    snprintf(measure, sizeof(measure), "%s    %s",
             title && *title ? title : " ", value);
    size_params = (uiSizeToTextParams_t){
        .frame = hud.timer_dialog.TimerDialog, .parent = &hud.timer_dialog_anchor,
        .measure_text = measure, .font = TimerDialogMeasureFont(),
        .padding_x = BZ_WC3_TIMER_DIALOG_EDGE_INSET, .min_width = BZ_WC3_TIMER_DIALOG_MIN_WIDTH,
    };

    UI_SetCurrentClient(ent->client);
    UI_WriteStart(WC3_LAYER_TIMERDIALOG);
    UI_WriteFrame(&hud.timer_dialog_anchor);
    UI_WriteFrameWithChildrenSizedToText(&size_params);
    UI_WriteEnd(ent);
    if ((G_TimerRemaining(dialog->timer) / 1000u) % 30u == 0)
        WC3_TIMERDIALOG_LOG("hud write player=%u client_edict=%u remaining_ms=%u title=\"%s\" value=%s layer=%d\n",
                            (unsigned)player_num, (unsigned)ent->s.number,
                            (unsigned)G_TimerRemaining(dialog->timer),
                            title && *title ? title : " ", value, WC3_LAYER_TIMERDIALOG);
    UI_SetCurrentClient(NULL);
}
