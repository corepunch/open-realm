/* Warcraft III score-screen modal. The retail ScoreScreen.fdf supplies the
 * root, textures and inherited styles; gameplay owns the frozen numeric rows.
 * The row/tab offsets below are PRESENTATION GUESSES until the actual
 * ScoreScreen.fdf named subframes can be extracted and bound with fdfbindgen.
 * Do not ship a replacement FDF or add score policy to the generic client. */
#include "hud_local.h"

#define WC3_SCORE_TABS 4

static void ScoreScreenPlace(frameDef_t *frame, UIFRAMEPOINT point,
                             frameDef_t *root, UIFRAMEPOINT other,
                             float x, float y, float width, float height) {
    /* The source styles live in GameResultDialog. Drop all their authored
     * anchors before reparenting to ScoreScreenFrame, or the window serializer
     * may emit dangling cross-window relative frame IDs. */
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = true;
    UI_SetPoint(frame, point, root, other, x, y);
    UI_SetSize(frame, width, height);
}

/* Retail art is mandatory: on a missing archive this screen reports failure
 * and the transition is completed instead of trapping the outgoing session. */
void UI_LoadHudScoreScreen(void) {
    frameDef_t *style;
    frameDef_t *button_style;

    if (hud.score_root) return;
    if (!UI_EnsureFDF("UI\\FrameDef\\Glue\\ScoreScreen.fdf")) return;
    hud.score_root = UI_FindFrame("ScoreScreenFrame");
    if (!hud.score_root) {
        BZ_FDF_REPORT_MISSING("ScoreScreenFrame");
        return;
    }
    UI_CenterFrame(hud.score_root);
    UI_SetHidden(hud.score_root, false);
    style = hud.result.GameResultText;
    button_style = hud.result.GameResultContinueButton;
    if (!style || !button_style) return;

    /* The retained FDF palette/fonts come from the in-game result dialog.
     * Text and controls are temporary data presenters, not hardcoded images. */
    hud.score_label = UI_CloneFrameTree(style, hud.score_root);
    if (!hud.score_label) return;
    UI_SetHidden(hud.score_label, false);
    ScoreScreenPlace(hud.score_label, FRAMEPOINT_TOPLEFT, hud.score_root,
                     FRAMEPOINT_TOPLEFT, 0.030f, -0.105f, 0.70f, 0.025f);

    FOR_LOOP(i, MAX_PLAYERS + 1) {
        hud.score_rows[i] = UI_CloneFrameTree(style, hud.score_root);
        if (!hud.score_rows[i]) return;
        UI_SetHidden(hud.score_rows[i], false);
        ScoreScreenPlace(hud.score_rows[i], FRAMEPOINT_TOPLEFT, hud.score_root,
                         FRAMEPOINT_TOPLEFT, 0.030f, -0.140f - i * 0.013f,
                         0.70f, 0.013f);
        hud.score_rows[i]->Font.Size = 0.011f; /* PRESENTATION GUESS */
    }
    FOR_LOOP(i, WC3_SCORE_TABS) {
        frameDef_t *text;
        hud.score_tabs[i] = UI_CloneFrameTree(button_style, hud.score_root);
        if (!hud.score_tabs[i]) return;
        UI_SetHidden(hud.score_tabs[i], false);
        ScoreScreenPlace(hud.score_tabs[i], FRAMEPOINT_TOPLEFT, hud.score_root,
                         FRAMEPOINT_TOPLEFT, 0.030f + i * 0.170f, -0.060f,
                         0.155f, 0.030f);
        UI_SetOnClick(hud.score_tabs[i], "scorescreen_tab %u", (unsigned)i);
        text = UI_FindChildFrame(hud.score_tabs[i], "GameResultContinueButtonText");
        if (text) UI_SetText(text, "%s", ((cstring_t[]){"Overview", "Units", "Heroes", "Resources"})[i]);
    }
    hud.score_continue = UI_CloneFrameTree(button_style, hud.score_root);
    if (!hud.score_continue) return;
    UI_SetHidden(hud.score_continue, false);
    ScoreScreenPlace(hud.score_continue, FRAMEPOINT_BOTTOMRIGHT, hud.score_root,
                     FRAMEPOINT_BOTTOMRIGHT, -0.035f, 0.022f, 0.140f, 0.030f);
    UI_SetOnClick(hud.score_continue, UI_WINDOW_CLOSE_COMMAND_PREFIX "scorescreen_continue");
    {
        frameDef_t *text = UI_FindChildFrame(hud.score_continue, "GameResultContinueButtonText");
        if (text) UI_SetText(text, "Continue");
    }
}

static void ScoreScreenWriteHeader(uint32_t tab) {
    static cstring_t const headings[] = {
        "Player    Total    Units    Heroes    Resources    Tech %",
        "Player    Trained    Kills    Built    Razed    Units score",
        "Player    Hero kills    Items    Mercenaries    Heroes score",
        "Player    Gold    Net gold    Lumber    Max food    Resources score"
    };
    UI_SetText(hud.score_label, "%s", headings[tab]);
}

bool UI_ShowScoreScreen(edict_t *ent, wc3ScoreSnapshot_t const *snapshot, uint32_t tab) {
    if (!ent || !ent->client || !snapshot || tab >= WC3_SCORE_TABS) return false;
    if (!hud.score_root || !hud.score_label || !hud.score_continue) return false;
    FOR_LOOP(i, WC3_SCORE_TABS) if (!hud.score_tabs[i]) return false;
    ScoreScreenWriteHeader(tab);
    FOR_LOOP(i, MAX_PLAYERS + 1) {
        frameDef_t *frame = hud.score_rows[i];
        wc3ScorePlayerSnapshot_t const *p;
        int32_t const *v;
        if (!frame) return false;
        UI_SetHidden(frame, i >= snapshot->count);
        if (i >= snapshot->count) continue;
        p = &snapshot->players[i];
        v = p->values;
        switch (tab) {
        case 0:
            UI_SetText(frame, "%.18s    %d    %d    %d    %d    %d%%", p->name,
                       v[WC3_PLAYER_SCORE_TOTAL], v[WC3_PLAYER_SCORE_UNIT_TOTAL],
                       v[WC3_PLAYER_SCORE_HERO_TOTAL], v[WC3_PLAYER_SCORE_RESOURCE_TOTAL],
                       v[WC3_PLAYER_SCORE_TECH_PERCENT]);
            break;
        case 1:
            UI_SetText(frame, "%.18s    %d    %d    %d    %d    %d", p->name,
                       v[WC3_PLAYER_SCORE_UNITS_TRAINED], v[WC3_PLAYER_SCORE_UNITS_KILLED],
                       v[WC3_PLAYER_SCORE_STRUCT_BUILT], v[WC3_PLAYER_SCORE_STRUCT_RAZED],
                       v[WC3_PLAYER_SCORE_UNIT_TOTAL]);
            break;
        case 2:
            UI_SetText(frame, "%.18s    %d    %d    %d    %d", p->name,
                       v[WC3_PLAYER_SCORE_HEROES_KILLED], v[WC3_PLAYER_SCORE_ITEMS_GAINED],
                       v[WC3_PLAYER_SCORE_MERCS_HIRED], v[WC3_PLAYER_SCORE_HERO_TOTAL]);
            break;
        default:
            UI_SetText(frame, "%.18s    %d    %d    %d    %d    %d", p->name,
                       v[WC3_PLAYER_SCORE_GOLD_MINED_TOTAL], v[WC3_PLAYER_SCORE_GOLD_MINED_UPKEEP],
                       v[WC3_PLAYER_SCORE_LUMBER_TOTAL], v[WC3_PLAYER_SCORE_FOOD_MAXUSED],
                       v[WC3_PLAYER_SCORE_RESOURCE_TOTAL]);
            break;
        }
    }
    /* The modal follows existing client-owned pause and focus arbitration.
     * Tab changes replace the same unique window without a close/reopen gap. */
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, hud.score_root, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_SCORE,
        .class_id = BZ_WC3_WINDOW_SCORE,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_ESCAPE));
    UI_SetCurrentClient(NULL);
    return true;
}
