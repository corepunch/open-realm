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
    quest_row_template = UI_FindFrame("QuestListItem");
    quest_item_template = UI_FindFrame("QuestItemListItem");
    if (!quest_row_template) BZ_FDF_REPORT_MISSING("QuestListItem");
    if (!quest_item_template) BZ_FDF_REPORT_MISSING("QuestItemListItem");
    return quest_row_template && quest_item_template;
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
        LPFRAMEDEF row_frame, button, title;
        LPFRAMEDEF selected_highlight, completed_highlight, failed_highlight, complete;
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
        authored_selection = selected_highlight != NULL;

        UI_SetHidden(selected_highlight, quest != selected);
        UI_SetHidden(failed_highlight, !quest->failed);
        UI_SetHidden(completed_highlight, !quest->completed || quest->failed);
        UI_SetHidden(complete, !quest->completed || quest->failed);

        snprintf(text, sizeof(text), "%s%s",
                 !authored_selection && quest == selected ? "> " : "",
                 UI_LevelStringSafe(quest->title));
        snprintf(command, sizeof(command), "quest %u", (unsigned)UI_QuestIndex(quest));

        UI_SetText(title, "%s", text);
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
        row++;
    }
    HideUnusedRows(quest_item_rows, quest_item_row_count, row);
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    LPCSTR subtitle;

    if (!ent || !ent->client || !QuestIsVisibleMember(quest)) return;
    if (!QuestsEnsureLoaded()) return;

    UI_SetText(qd.QuestTitleValue, "%s", UI_LevelStringSafe(quest->title));
    qd.QuestTitleValue->Font.Color = MAKE(COLOR32, 252, 210, 18, 255);
    UI_SetTextPointer(qd.QuestDisplay, UI_LevelStringSafe(quest->description));

    subtitle = level.mapinfo ? level.mapinfo->loadingScreenTitle : NULL;
    if (subtitle && *subtitle) {
        UI_SetText(qd.QuestSubtitleValue, "%s", subtitle);
    } else {
        UI_SetText(qd.QuestSubtitleValue, " ");
    }

    /* Keep the authored OK label/skin; only bind the action. */
    UI_SetOnClick(qd.QuestAcceptButton, "hidequests");

    PopulateQuestList(qd.QuestMainContainer, true, quest);
    PopulateQuestList(qd.QuestOptionalContainer, false, quest);
    PopulateQuestItems(qd.QuestItemListContainer, quest);

    ent->client->quest_dialog.selected = (LPQUEST)quest;
    ent->client->quest_dialog.open = true;
    if (ent->client->connected)
        UI_WriteLayout(ent, qd.QuestDialog, LAYER_QUESTDIALOG);
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
    if (!ent || !ent->client) return;
    ent->client->quest_dialog.open = false;
    ent->client->quest_dialog.selected = NULL;
    if (ent->client->connected) UI_ClearLayer(ent, LAYER_QUESTDIALOG);
}
