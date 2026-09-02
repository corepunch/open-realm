/*
 * hud_quests.c — Quest dialog UI.
 *
 * Draws the quest log overlay: backdrop, title, clickable quest list,
 * per-quest description/objective detail, and close button on
 * LAYER_QUESTDIALOG.
 */

#include "hud_local.h"
#include "hud_utils.h"
#include "../generated/quest_dialog.h"

static QuestDialog_t qd;
static LPFRAMEDEF quest_row_template, quest_item_template;
static LPFRAMEDEF required_rows[MAX_UI_CLASSES], optional_rows[MAX_UI_CLASSES];
static LPFRAMEDEF quest_item_rows[MAX_UI_CLASSES];
static DWORD required_row_count, optional_row_count, quest_item_row_count;
static BOOL quests_loaded;

/* QuestDialog.fdf owns both repeated row schemas in addition to the dialog root. */
static BOOL QuestsEnsureLoaded(void) {
    if (quests_loaded) return qd.QuestDialog && quest_row_template && quest_item_template;
    quests_loaded = true;
    memset(required_rows, 0, sizeof(required_rows));
    memset(optional_rows, 0, sizeof(optional_rows));
    memset(quest_item_rows, 0, sizeof(quest_item_rows));
    required_row_count = optional_row_count = quest_item_row_count = 0;
    if (!QuestDialog_Load(&qd)) return false;
    UI_CenterFrame(qd.QuestDialog);
    quest_row_template = UI_FindFrame("QuestListItem");
    quest_item_template = UI_FindFrame("QuestItemListItem");
    if (!quest_row_template) BZ_FDF_REPORT_MISSING("QuestListItem");
    if (!quest_item_template) BZ_FDF_REPORT_MISSING("QuestItemListItem");
    return quest_row_template && quest_item_template;
}


static BOOL QuestDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_quest_debug", "0")) != 0;
}

static void QuestDebugQuoted(LPSTR out, DWORD out_size, LPCSTR text) {
    DWORD used = 0;

    if (!out || !out_size) return;
    if (!text) text = "";
    for (; *text && used + 1 < out_size; text++) {
        LPCSTR escaped = NULL;
        switch (*text) {
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            case '"': escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            default: break;
        }
        if (escaped) {
            while (*escaped && used + 1 < out_size) out[used++] = *escaped++;
        } else {
            out[used++] = *text;
        }
    }
    out[used] = '\0';
}

static void QuestDebugText(DWORD quest_index, LPCSTR field, LPCSTR raw, LPCSTR resolved) {
    char raw_text[768], resolved_text[768];

    if (!QuestDebugEnabled()) return;
    QuestDebugQuoted(raw_text, sizeof(raw_text), raw);
    QuestDebugQuoted(resolved_text, sizeof(resolved_text), resolved);
    fprintf(stderr, "WC3_QUEST_TEXT quest=%u field=%s raw=\"%s\" resolved=\"%s\"\n",
            (unsigned)quest_index, field ? field : "?", raw_text, resolved_text);
}

static BOOL QuestIsVisible(LPCQUEST quest) {
    return quest && quest->enabled && quest->discovered;
}

static BOOL QuestIsVisibleMember(LPCQUEST quest) {
    if (!quest) return false;
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q == quest) return QuestIsVisible(q);
    }
    return false;
}

DWORD UI_QuestIndex(LPCQUEST quest) {
    DWORD index = 0;
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q == quest) return index;
        index++;
    }
    return index;
}


static void ResetRowsForParent(LPFRAMEDEF *rows, DWORD *count, LPFRAMEDEF parent) {
    if (!rows || !count || !*count) return;
    if (!rows[0] || !rows[0]->inuse || rows[0]->Parent != parent) {
        memset(rows, 0, sizeof(LPFRAMEDEF) * MAX_UI_CLASSES);
        *count = 0;
    }
}

static LPFRAMEDEF QuestRowAt(LPFRAMEDEF *rows, DWORD *count, LPFRAMEDEF container, DWORD row,
                             LPCFRAMEDEF row_template) {
    LPFRAMEDEF frame;

    if (!rows || !count || !container || !row_template || row >= MAX_UI_CLASSES) return NULL;
    frame = rows[row];
    if (!frame || !frame->inuse || frame->Parent != container) {
        frame = UI_CloneStackedRow(row_template, container, row);
        rows[row] = frame;
    } else {
        UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT,
                    0.0f, -(FLOAT)row * frame->Height);
    }
    if (!frame) return NULL;
    UI_SetHidden(frame, false);
    if (*count <= row) *count = row + 1;
    return frame;
}

static void HideUnusedRows(LPFRAMEDEF *rows, DWORD count, DWORD used) {
    for (DWORD i = used; i < count; i++) {
        if (rows[i] && rows[i]->inuse) UI_SetHidden(rows[i], true);
    }
}

static void PopulateQuestList(LPFRAMEDEF container, BOOL required, LPCQUEST selected) {
    LPFRAMEDEF *rows = required ? required_rows : optional_rows;
    DWORD *row_count = required ? &required_row_count : &optional_row_count;
    DWORD row = 0;

    if (!container) return;
    ResetRowsForParent(rows, row_count, container);
    FOR_EACH_LIST(QUEST, quest, level.quests) {
        char text[256];
        char command[64];
        LPFRAMEDEF row_frame, button, title, icon_container;
        LPFRAMEDEF selected_highlight, completed_highlight, failed_highlight, complete;
        LPCSTR title_text, icon_path;
        DWORD quest_index;
        BOOL authored_selection;

        if (!QuestIsVisible(quest) || quest->required != required) continue;

        row_frame = QuestRowAt(rows, row_count, container, row, quest_row_template);
        button = row_frame ? UI_FindChildFrame(row_frame, "QuestListItemButton") : NULL;
        title = row_frame ? UI_FindChildFrame(row_frame, "QuestListItemTitle") : NULL;
        if (!row_frame) {
            fprintf(stderr, "WC3 HUD: failed to clone QuestListItem row %u\n", (unsigned)row);
            return;
        }
        if (!button || !title) {
            BZ_FDF_REPORT_MISSING(!button ? "QuestListItemButton" : "QuestListItemTitle");
            return;
        }

        selected_highlight = UI_FindChildFrame(row_frame, "QuestListItemSelectedHighlight");
        completed_highlight = UI_FindChildFrame(row_frame, "QuestListItemCompletedHighlight");
        failed_highlight = UI_FindChildFrame(row_frame, "QuestListItemFailedHighlight");
        complete = UI_FindChildFrame(row_frame, "QuestListItemComplete");
        icon_container = UI_FindChildFrame(row_frame, "QuestListItemIconContainer");
        authored_selection = selected_highlight != NULL;

        /* The retail selected highlight is authored with SetAllPoints on the
         * clickable quest button. Reassert that relation after cloning so the
         * selected art uses the same full-width rectangle as mouse-over art. */
        if (selected_highlight) {
            memset(&selected_highlight->Points, 0, sizeof(selected_highlight->Points));
            selected_highlight->AnyPointsSet = true;
            UI_SetPoint(selected_highlight, FRAMEPOINT_TOPLEFT,
                        button, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
            UI_SetPoint(selected_highlight, FRAMEPOINT_BOTTOMRIGHT,
                        button, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
        }

        UI_SetHidden(selected_highlight, quest != selected);
        UI_SetHidden(failed_highlight, !quest->failed);
        UI_SetHidden(completed_highlight, !quest->completed || quest->failed);
        UI_SetHidden(complete, !quest->completed || quest->failed);

        quest_index = UI_QuestIndex(quest);
        title_text = UI_LevelStringSafe(quest->title);
        icon_path = quest->iconPath && *quest->iconPath ? G_LevelString(quest->iconPath) : NULL;
        snprintf(text, sizeof(text), "%s%s",
                 !authored_selection && quest == selected ? "> " : "", title_text);
        snprintf(command, sizeof(command), "quest %u", (unsigned)quest_index);

        UI_SetText(title, "%s", text);
        if (icon_container && icon_container->Type == FT_BACKDROP) {
            icon_container->Backdrop.Background = icon_path && *icon_path
                ? UI_LoadTexture(icon_path, false) : 0;
        }
        QuestDebugText(quest_index, "list_title", quest->title, title_text);
        if (QuestDebugEnabled()) {
            char icon_raw[512], icon_resolved[512];
            QuestDebugQuoted(icon_raw, sizeof(icon_raw), quest->iconPath);
            QuestDebugQuoted(icon_resolved, sizeof(icon_resolved), icon_path);
            fprintf(stderr,
                    "WC3_QUEST_STATE quest=%u required=%d discovered=%d enabled=%d completed=%d failed=%d "
                    "iconRaw=\"%s\" iconResolved=\"%s\" iconFrame=%s iconImage=%u\n",
                    (unsigned)quest_index, quest->required, quest->discovered, quest->enabled,
                    quest->completed, quest->failed, icon_raw, icon_resolved,
                    icon_container ? "yes" : "no",
                    (unsigned)(icon_container && icon_container->Type == FT_BACKDROP
                        ? icon_container->Backdrop.Background : 0));
        }
        UI_SetOnClick(button, "%s", command);
        title->Font.Color = !authored_selection && quest == selected
            ? MAKE(COLOR32, 252, 210, 18, 255)
            : COLOR32_WHITE;
        row++;
    }
    HideUnusedRows(rows, *row_count, row);
}

static void PopulateQuestItems(LPFRAMEDEF container, LPCQUEST quest) {
    DWORD row = 0;

    if (!container || !quest) return;
    ResetRowsForParent(quest_item_rows, &quest_item_row_count, container);
    FOR_EACH_LIST(QUESTITEM, item, quest->items) {
        char text[512];
        LPFRAMEDEF item_frame, title;

        snprintf(text, sizeof(text), "%s %s",
                 item->completed ? "- |cff80ff80" : "-",
                 UI_LevelStringSafe(item->description));

        item_frame = QuestRowAt(quest_item_rows, &quest_item_row_count,
                                container, row, quest_item_template);
        title = item_frame ? UI_FindChildFrame(item_frame, "QuestItemListItemTitle") : NULL;
        if (!item_frame) {
            fprintf(stderr, "WC3 HUD: failed to clone QuestItemListItem row %u\n", (unsigned)row);
            return;
        }
        if (!title) {
            BZ_FDF_REPORT_MISSING("QuestItemListItemTitle");
            return;
        }
        UI_SetText(title, "%s", text);
        title->Font.Color = COLOR32_WHITE;
        if (QuestDebugEnabled()) {
            char field[64];
            snprintf(field, sizeof(field), "item[%u]%s", (unsigned)row, item->completed ? ".completed" : "");
            QuestDebugText(UI_QuestIndex(quest), field, item->description, UI_LevelStringSafe(item->description));
        }
        row++;
    }
    HideUnusedRows(quest_item_rows, quest_item_row_count, row);
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    LPCSTR title, description, subtitle;
    BOOL was_open;

    if (!ent || !ent->client || !QuestIsVisibleMember(quest)) return;
    UI_SetCurrentClient(ent->client);
    if (!QuestsEnsureLoaded()) {
        UI_SetCurrentClient(NULL);
        return;
    }

    title = UI_LevelStringSafe(quest->title);
    description = UI_LevelStringSafe(quest->description);
    UI_SetText(qd.QuestTitleValue, "%s", title);
    qd.QuestTitleValue->Font.Color = MAKE(COLOR32, 252, 210, 18, 255);
    UI_SetTextPointer(qd.QuestDisplay, description);
    if (qd.QuestDetailsTitle)
        UI_SetText(qd.QuestDetailsTitle, "%s", UI_GetString("QUESTDESCRIPTION"));

    subtitle = level.mapinfo ? UI_LevelStringSafe(level.mapinfo->loadingScreenTitle) : NULL;
    if (subtitle && *subtitle) {
        UI_SetText(qd.QuestSubtitleValue, "%s", subtitle);
    } else {
        UI_SetText(qd.QuestSubtitleValue, " ");
        subtitle = " ";
    }

    QuestDebugText(UI_QuestIndex(quest), "title", quest->title, title);
    QuestDebugText(UI_QuestIndex(quest), "description", quest->description, description);
    QuestDebugText(UI_QuestIndex(quest), "subtitle",
                   level.mapinfo ? level.mapinfo->loadingScreenTitle : NULL, subtitle);
    if (QuestDebugEnabled()) {
        QuestDebugText(UI_QuestIndex(quest), "required_heading",
                       qd.QuestMainTitle ? qd.QuestMainTitle->Text : NULL,
                       qd.QuestMainTitle ? qd.QuestMainTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "optional_heading",
                       qd.QuestOptionalTitle ? qd.QuestOptionalTitle->Text : NULL,
                       qd.QuestOptionalTitle ? qd.QuestOptionalTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "details_heading",
                       qd.QuestDetailsTitle ? qd.QuestDetailsTitle->Text : NULL,
                       qd.QuestDetailsTitle ? qd.QuestDetailsTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "done_button",
                       qd.QuestAcceptButtonText ? qd.QuestAcceptButtonText->Text : NULL,
                       qd.QuestAcceptButtonText ? qd.QuestAcceptButtonText->Text : NULL);
    }

    /* Keep the authored OK/Done label and race-skinned button art; only bind
     * the action. */
    UI_SetOnClick(qd.QuestAcceptButton, "hidequests");

    PopulateQuestList(qd.QuestMainContainer, true, quest);
    PopulateQuestList(qd.QuestOptionalContainer, false, quest);
    PopulateQuestItems(qd.QuestItemListContainer, quest);

    was_open = ent->client->quest_dialog.open;
    ent->client->quest_dialog.selected = (LPQUEST)quest;
    ent->client->quest_dialog.open = true;
    if (ent->client->connected)
        UI_WriteModalLayout(ent, qd.QuestDialog, LAYER_QUESTDIALOG);
    UI_SetCurrentClient(NULL);
    if (!was_open) UI_ModalStateChanged(ent);
}

void UI_ShowQuests(LPEDICT ent) {
    LPCQUEST quest = NULL;

    if (!ent || !ent->client) return;
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q->required && QuestIsVisible(q)) { quest = q; break; }
    }
    if (!quest) {
        FOR_EACH_LIST(QUEST, q, level.quests) {
            if (QuestIsVisible(q)) { quest = q; break; }
        }
    }
    if (quest) UI_ShowQuest(ent, quest);
    else UI_HideQuests(ent);
}

void UI_RefreshQuests(LPEDICT ent) {
    if (!ent || !ent->client || !ent->client->quest_dialog.open) return;
    if (QuestIsVisibleMember(ent->client->quest_dialog.selected))
        UI_ShowQuest(ent, ent->client->quest_dialog.selected);
    else
        UI_ShowQuests(ent);
}

void UI_HideQuests(LPEDICT ent) {
    BOOL was_open;

    if (!ent || !ent->client) return;
    was_open = ent->client->quest_dialog.open;
    ent->client->quest_dialog.open = false;
    ent->client->quest_dialog.selected = NULL;
    if (ent->client->connected) UI_ClearLayer(ent, LAYER_QUESTDIALOG);
    if (was_open) UI_ModalStateChanged(ent);
}
