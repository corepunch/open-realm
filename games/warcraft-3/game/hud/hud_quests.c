/*
 * hud_quests.c — Quest dialog UI.
 *
 * Draws the quest log overlay: backdrop, title, clickable quest list,
 * per-quest objective detail, and close button on LAYER_QUESTDIALOG.
 */

#include "hud_local.h"
#include "../generated/quest_dialog.h"

static QuestDialog_t qd;
static BOOL quests_loaded;

static void QuestsEnsureLoaded(void) {
    if (quests_loaded) return;
    quests_loaded = true;
    QuestDialog_Load(&qd);
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

        LPFRAMEDEF row_frame = UI_Spawn(FT_TEXTBUTTON, container);
        if (!row_frame) continue;
        UI_SetPoint(row_frame, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT, 0.0f, -(FLOAT)row * QUEST_ROW_H);
        UI_SetSize(row_frame, QUEST_LIST_W, QUEST_ROW_H);
        UI_SetText(row_frame, "%s", text);
        UI_SetOnClick(row_frame, "%s", command);
        row_frame->Font.Color = color;
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

        LPFRAMEDEF item_frame = UI_Spawn(FT_STRING, container);
        if (!item_frame) continue;
        UI_SetPoint(item_frame, FRAMEPOINT_TOPLEFT, container, FRAMEPOINT_TOPLEFT, 0.0f, -(FLOAT)row * QUEST_ROW_H);
        UI_SetSize(item_frame, QUEST_DETAIL_W, QUEST_ROW_H);
        UI_SetText(item_frame, "%s", text);
        item_frame->Font.Color = COLOR32_WHITE;
        row++;
    }
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    char title[256];
    LPCSTR subtitle;

    if (!ent || !quest) return;

    QuestsEnsureLoaded();

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
}

void UI_HideQuests(LPEDICT ent) {
    if (!ent) return;
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_QUESTDIALOG});
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}
