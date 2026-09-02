/*
 * hud_quests.c — Quest dialog UI.
 *
 * Draws the quest log overlay: backdrop, title, clickable quest list,
 * per-quest objective detail, and close button on LAYER_QUESTDIALOG.
 */

#include "hud_local.h"
#include "hud_utils.h"
#include "../generated/quest_dialog.h"

static QuestDialog_t qd;
static LPFRAMEDEF quest_row_template, quest_item_template;
static BOOL quests_loaded;

/* QuestDialog.fdf owns both repeated row schemas in addition to the dialog root. */
static BOOL QuestsEnsureLoaded(void) {
    if (quests_loaded) return qd.QuestDialog && quest_row_template && quest_item_template;
    quests_loaded = true;
    if (!QuestDialog_Load(&qd)) return false;
    quest_row_template = UI_FindFrame("QuestListItem");
    quest_item_template = UI_FindFrame("QuestItemListItem");
    if (!quest_row_template) BZ_FDF_REPORT_MISSING("QuestListItem");
    if (!quest_item_template) BZ_FDF_REPORT_MISSING("QuestItemListItem");
    return quest_row_template && quest_item_template;
}

DWORD UI_QuestIndex(LPCQUEST quest) {
    DWORD index = 0;
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q == quest) return index;
        index++;
    }
    return index;
}

static void PopulateQuestList(LPFRAMEDEF container, BOOL required, LPCQUEST selected) {
    if (!container) return;
    DWORD row = 0;
    FOR_EACH_LIST(QUEST, quest, level.quests) {
        char text[256];
        char command[64];
        COLOR32 color;

        if (quest->required != required) continue;
        if (quest->discovered) {
            snprintf(text, sizeof(text), "%s%s",
                     quest == selected ? "> " : "  ",
                     UI_LevelStringSafe(quest->title));
        } else {
            snprintf(text, sizeof(text), "%sUndiscovered Quest", quest == selected ? "> " : "  ");
        }
        color = quest == selected ? MAKE(COLOR32, 252, 210, 18, 255) : COLOR32_WHITE;
        snprintf(command, sizeof(command), "quest %u", (unsigned)UI_QuestIndex(quest));

        LPFRAMEDEF row_frame = UI_CloneStackedRow(quest_row_template, container, row);
        LPFRAMEDEF button = row_frame ? UI_FindChildFrame(row_frame, "QuestListItemButton") : NULL;
        LPFRAMEDEF title = row_frame ? UI_FindChildFrame(row_frame, "QuestListItemTitle") : NULL;
        if (!row_frame) {
            fprintf(stderr, "WC3 HUD: failed to clone QuestListItem row %u\n", (unsigned)row);
            return;
        }
        if (!button || !title) {
            BZ_FDF_REPORT_MISSING(!button ? "QuestListItemButton" : "QuestListItemTitle");
            return;
        }
        UI_SetText(title, "%s", text);
        UI_SetOnClick(button, "%s", command);
        title->Font.Color = color;
        row++;
    }
}

static void PopulateQuestItems(LPFRAMEDEF container, LPCQUEST quest) {
    if (!container || !quest) return;
    DWORD row = 0;
    FOR_EACH_LIST(QUESTITEM, item, quest->items) {
        char text[512];
        snprintf(text, sizeof(text), "%s %s",
                 item->completed ? "- |cff80ff80" : "-",
                 UI_LevelStringSafe(item->description));

        LPFRAMEDEF item_frame = UI_CloneStackedRow(quest_item_template, container, row);
        LPFRAMEDEF title = item_frame ? UI_FindChildFrame(item_frame, "QuestItemListItemTitle") : NULL;
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
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    char title[256];
    LPCSTR subtitle;

    if (!ent || !quest) return;

    if (!QuestsEnsureLoaded()) return;

    snprintf(title, sizeof(title), "%s",
             quest->discovered ? UI_LevelStringSafe(quest->title) : "Undiscovered Quest");
    UI_SetText(qd.QuestTitleValue, "%s", title);
    qd.QuestTitleValue->Font.Color = MAKE(COLOR32, 252, 210, 18, 255);

    subtitle = level.mapinfo ? level.mapinfo->loadingScreenTitle : NULL;
    if (subtitle && *subtitle) {
        UI_SetText(qd.QuestSubtitleValue, "%s", subtitle);
    } else {
        UI_SetText(qd.QuestSubtitleValue, " ");
    }

    UI_SetText(qd.QuestAcceptButtonText, "X");
    UI_SetOnClick(qd.QuestAcceptButton, "hidequests");

    PopulateQuestList(qd.QuestMainContainer, true, quest);
    PopulateQuestList(qd.QuestOptionalContainer, false, quest);

    if (quest->discovered) {
        PopulateQuestItems(qd.QuestItemListContainer, quest);
    }

    UI_WriteLayout(ent, qd.QuestDialog, LAYER_QUESTDIALOG);
}

void UI_ShowQuests(LPEDICT ent) {
    LPCQUEST quest = NULL;

    if (!ent) return;
    FOR_EACH_LIST(QUEST, q, level.quests) {
        if (q->required && q->discovered) { quest = q; break; }
    }
    if (!quest) {
        FOR_EACH_LIST(QUEST, q, level.quests) {
            if (q->discovered) { quest = q; break; }
        }
    }
    UI_ShowQuest(ent, quest);
    G_SetQuestDialogOpen(ent, true);
}

void UI_HideQuests(LPEDICT ent) {
    if (!ent) return;
    UI_WriteStart(LAYER_QUESTDIALOG);
    UI_WriteEnd(ent);
    G_SetQuestDialogOpen(ent, false);
}
