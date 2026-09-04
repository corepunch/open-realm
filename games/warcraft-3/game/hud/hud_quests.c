/*
 * hud_quests.c — Quest dialog UI.
 *
 * Draws the quest log overlay: backdrop, title, clickable quest list,
 * per-quest description/objective detail, and close button on
 * LAYER_QUESTDIALOG.
 */

#include "hud_local.h"
#include "hud_utils.h"

/* QuestDialog.fdf owns both repeated row schemas in addition to the dialog root. */
void UI_LoadHudQuests(void) {
    if (hud.quest.QuestDialog) return;
    if (!QuestDialog_Load(&hud.quest)) return;
    UI_CenterFrame(hud.quest.QuestDialog);
    hud.quest_row = UI_FindFrame("QuestListItem");
    hud.quest_item = UI_FindFrame("QuestItemListItem");
    if (!hud.quest_row) BZ_FDF_REPORT_MISSING("QuestListItem");
    if (!hud.quest_item) BZ_FDF_REPORT_MISSING("QuestItemListItem");
}


static BOOL QuestDebugEnabled(void) {
    return atoi(gi.CvarString("wc3_quest_debug", "0")) != 0;
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

/* Retail lists enabled undiscovered quests as placeholders but only lets the
 * player open discovered quests. */
static BOOL QuestIsListVisible(LPCQUEST quest) {
    return quest && quest->enabled;
}

static BOOL QuestIsVisibleMember(LPCQUEST quest) {
    if (!quest) return false;
    FOR_EACH_QUEST(q) {
        if (q == quest) return QuestIsVisible(q);
    }
    return false;
}

DWORD UI_QuestIndex(LPCQUEST quest) {
    DWORD index = 0;
    FOR_EACH_QUEST(q) {
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
    /* QuestListItemButton is anchored to the row's trailing edge; give the
     * row the authored list width so that anchor resolves to the container. */
    frame->Width = container->Width;
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
    LPFRAMEDEF *rows = required ? hud.required_rows : hud.optional_rows;
    DWORD *row_count = required ? &hud.required_row_count : &hud.optional_row_count;
    DWORD row = 0;

    if (!container) return;
    ResetRowsForParent(rows, row_count, container);
    FOR_EACH_QUEST(quest) {
        char text[256];
        char command[64];
        LPFRAMEDEF row_frame, button, title, icon_container;
        LPFRAMEDEF selected_highlight, completed_highlight, failed_highlight, complete;
        LPCSTR title_text, icon_path;
        DWORD quest_index;
        BOOL authored_selection;

        if (!QuestIsListVisible(quest) || quest->required != required) continue;

        row_frame = QuestRowAt(rows, row_count, container, row, hud.quest_row);
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
         * clickable quest button (QuestListItemButton).  Reassert that relation
         * after cloning so the selected art covers exactly the button area.
         * The button is anchor-sized (TOPLEFT→icon TOPRIGHT, BOTTOMRIGHT→row
         * BOTTOMRIGHT) so it does not include the icon container. */
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
        if (complete && quest->completed && !quest->failed)
            UI_SetText(complete, "(%s)", UI_GetString("QUESTCOMPLETED"));

        quest_index = UI_QuestIndex(quest);
        title_text = quest->discovered ? UI_LevelStringSafe(quest->title) : UI_GetString("UNDISCOVERED_QUEST");
        icon_path = quest->discovered && quest->iconPath && *quest->iconPath ? G_LevelString(quest->iconPath) : NULL;
        snprintf(text, sizeof(text), "%s%s", !authored_selection && quest == selected ? "> " : "", title_text);
        snprintf(command, sizeof(command), "quest %u", (unsigned)quest_index);

        UI_SetText(title, "%s", text);
        if (icon_container && icon_container->Type == FT_BACKDROP) {
            icon_container->Backdrop.Background = icon_path && *icon_path
                ? UI_LoadTexture(icon_path, false) : UI_LoadTexture("UndiscoveredQuestIcon", true);
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
        title->Font.Color = quest->completed ? title->Font.DisabledColor :
            (!authored_selection && quest == selected ? MAKE(COLOR32, 252, 210, 18, 255) : COLOR32_WHITE);
        row++;
    }
    HideUnusedRows(rows, *row_count, row);
}

static void PopulateQuestItems(LPFRAMEDEF container, LPCQUEST quest) {
    DWORD row = 0;

    if (!container || !quest) return;
    ResetRowsForParent(hud.quest_item_rows, &hud.quest_item_row_count, container);
    FOR_EACH_QUESTITEM(quest, item) {
        char text[512];
        LPFRAMEDEF item_frame, title;

        snprintf(text, sizeof(text), "%s %s",
                 item->completed ? "- |cff80ff80" : "-",
                 UI_LevelStringSafe(item->description));

        item_frame = QuestRowAt(hud.quest_item_rows, &hud.quest_item_row_count,
                                container, row, hud.quest_item);
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
    HideUnusedRows(hud.quest_item_rows, hud.quest_item_row_count, row);
}

void UI_ShowQuest(LPEDICT ent, LPCQUEST quest) {
    LPCSTR title, description, subtitle;

    if (!ent || !ent->client || !QuestIsVisibleMember(quest)) return;
    UI_SetCurrentClient(ent->client);
    if (!hud.quest.QuestDialog || !hud.quest_row || !hud.quest_item) {
        UI_SetCurrentClient(NULL);
        return;
    }

    title = UI_LevelStringSafe(quest->title);
    description = UI_LevelStringSafe(quest->description);
    UI_SetText(hud.quest.QuestTitleValue, "%s", title);
    hud.quest.QuestTitleValue->Font.Color = MAKE(COLOR32, 252, 210, 18, 255);
    UI_SetTextPointer(hud.quest.QuestDisplay, description);
    if (hud.quest.QuestDetailsTitle)
        UI_SetText(hud.quest.QuestDetailsTitle, "%s", UI_GetString("QUESTDESCRIPTION"));

    subtitle = level.mapinfo ? UI_LevelStringSafe(level.mapinfo->loadingScreenTitle) : NULL;
    if (subtitle && *subtitle) {
        UI_SetText(hud.quest.QuestSubtitleValue, "%s", subtitle);
    } else {
        UI_SetText(hud.quest.QuestSubtitleValue, " ");
        subtitle = " ";
    }

    QuestDebugText(UI_QuestIndex(quest), "title", quest->title, title);
    QuestDebugText(UI_QuestIndex(quest), "description", quest->description, description);
    QuestDebugText(UI_QuestIndex(quest), "subtitle",
                   level.mapinfo ? level.mapinfo->loadingScreenTitle : NULL, subtitle);
    if (QuestDebugEnabled()) {
        QuestDebugText(UI_QuestIndex(quest), "required_heading",
                       hud.quest.QuestMainTitle ? hud.quest.QuestMainTitle->Text : NULL,
                       hud.quest.QuestMainTitle ? hud.quest.QuestMainTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "optional_heading",
                       hud.quest.QuestOptionalTitle ? hud.quest.QuestOptionalTitle->Text : NULL,
                       hud.quest.QuestOptionalTitle ? hud.quest.QuestOptionalTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "details_heading",
                       hud.quest.QuestDetailsTitle ? hud.quest.QuestDetailsTitle->Text : NULL,
                       hud.quest.QuestDetailsTitle ? hud.quest.QuestDetailsTitle->Text : NULL);
        QuestDebugText(UI_QuestIndex(quest), "done_button",
                       hud.quest.QuestAcceptButtonText ? hud.quest.QuestAcceptButtonText->Text : NULL,
                       hud.quest.QuestAcceptButtonText ? hud.quest.QuestAcceptButtonText->Text : NULL);
    }

    /* Done is presentation-only: let the client close this window without a round trip. */
    UI_SetOnClick(hud.quest.QuestAcceptButton, UI_WINDOW_CLOSE_NOTIFY_ACTION);

    PopulateQuestList(hud.quest.QuestMainContainer, true, quest);
    PopulateQuestList(hud.quest.QuestOptionalContainer, false, quest);
    PopulateQuestItems(hud.quest.QuestItemListContainer, quest);

    if (ent->client->connected)
        UI_WriteWindow(ent, hud.quest.QuestDialog, &MAKE(uiWindowDef_t,
            .id = BZ_WC3_WINDOW_QUEST, .class_id = BZ_WC3_WINDOW_QUEST,
            .flags = UI_WINDOW_MOVABLE | UI_WINDOW_MODAL | UI_WINDOW_UNIQUE));
    UI_SetCurrentClient(NULL);
}

void UI_ShowQuests(LPEDICT ent) {
    LPCQUEST quest = NULL;

    if (!ent || !ent->client) return;
    FOR_EACH_QUEST(q) {
        if (q->required && QuestIsVisible(q)) { quest = q; break; }
    }
    if (!quest) {
        FOR_EACH_QUEST(q) {
            if (QuestIsVisible(q)) { quest = q; break; }
        }
    }
    if (quest) {
        UI_ShowQuest(ent, quest);
    }
}
