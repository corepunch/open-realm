/* Retail Warcraft III score-screen presentation. The original FDF and MDX/BLP
 * remain in mounted Warcraft game data: this module binds their named frames
 * and emits generic UI windows. Patch 2 replaces the temporary score rows with
 * retail player banners and individually aligned numeric cells. */
#include "hud_local.h"

#define WC3_SCORE_TABS 4

/* Keep the temporary text data rows scoped to the score root. The named FDF
 * controls, columns, buttons, model layers and their coordinates are NEVER
 * replaced with result-dialog clones. Row geometry is a PRESENTATION GUESS
 * until the dynamic retail row implementation in Patch 2. */
static void ScoreScreenPlaceRow(frameDef_t *frame, frameDef_t *root, uint32_t row) {
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = true;
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, root, FRAMEPOINT_TOPLEFT,
                0.140f, -0.137f - row * 0.019f);
    UI_SetSize(frame, 0.610f, 0.018f);
}

/* The retail FDF uses MODEL layers containing 2-D score-screen meshes. The
 * client sprite renderer preserves their native screen geometry and named MDX
 * animations; the portrait renderer would frame them as a 3-D talking head. */
static void ScoreScreenUseSprite(frameDef_t *frame) {
    frame->Type = FT_SPRITE;
    UI_SetHidden(frame, false);
}

void UI_LoadHudScoreScreen(void) {
    ScoreScreen_t *score = &hud.score;
    if (score->ScoreScreenFrame) return;
    if (!ScoreScreen_Load(score)) {
        memset(score, 0, sizeof(*score));
        return;
    }
    /* This root is authored with SetAllPoints, not as a centered in-game
     * dialog. UI_CenterFrame used to erase its full-screen anchors. */
    UI_SetAllPoints(score->ScoreScreenFrame);
    UI_SetHidden(score->ScoreScreenFrame, false);
    UI_SetHidden(score->ScoreScreenMainPanel, false);
    ScoreScreenUseSprite(score->ScoreScreenBackground);
    ScoreScreenUseSprite(score->ScoreScreenPageTab);
    ScoreScreenUseSprite(score->ScoreScreenRaceArt);
    ScoreScreenUseSprite(score->ScoreScreenBarTop);
    UI_SetOnClick(score->ScoreScreenOKButton,
                  UI_WINDOW_CLOSE_COMMAND_PREFIX "scorescreen_continue");
    FOR_LOOP(i, WC3_SCORE_TABS)
        UI_SetOnClick(score->ScoreScreenTabButton[i], "scorescreen_tab %u", (unsigned)i);

    /* Runtime-generated player cells do not exist in retail ScoreScreen.fdf.
     * Keep a modest legacy text presenter until Patch 2 supplies their actual
     * template-free scoreboard geometry, scrolling and team art. */
    FOR_LOOP(i, MAX_PLAYERS + 1) {
        hud.score_rows[i] = UI_Spawn(FT_TEXT, score->ScoreScreenFrame);
        if (!hud.score_rows[i]) {
            memset(score, 0, sizeof(*score));
            return;
        }
        UI_InheritFrom(hud.score_rows[i], "ScoreScreenColumnHeaderTemplate");
        ScoreScreenPlaceRow(hud.score_rows[i], score->ScoreScreenFrame, i);
        hud.score_rows[i]->Font.Size = 0.010f; /* PRESENTATION GUESS, Patch 2 */
        UI_SetHidden(hud.score_rows[i], false);
    }
}

static void ScoreScreenSetHeaders(ScoreScreen_t *score, uint32_t tab) {
    static cstring_t const keys[][5] = {
        { "OVERVIEW_COLUMN0", "OVERVIEW_COLUMN1", "OVERVIEW_COLUMN2", "OVERVIEW_COLUMN3", NULL },
        { "UNIT_COLUMN0", "UNIT_COLUMN1", NULL, NULL, NULL },
        { "HEROES_COLUMN0", "HEROES_COLUMN1", "HEROES_COLUMN2", "HEROES_COLUMN3", "HEROES_COLUMN4" },
        { "RESOURCES_COLUMN0", "RESOURCES_COLUMN1", "RESOURCES_COLUMN2", "RESOURCES_COLUMN3", "RESOURCES_COLUMN4" },
    };
    bool const wide = tab >= 2;
    UI_SetHidden(score->ScoreScreen4ColumnFrame, wide);
    UI_SetHidden(score->ScoreScreen5ColumnFrame, !wide);
    FOR_LOOP(i, wide ? 5u : 4u) {
        frameDef_t *header = wide ? score->ScoreScreen5ColumnHeader[i] : score->ScoreScreen4ColumnHeader[i];
        cstring_t key = keys[tab][i];
        /* FDF header text frames are children of the checkbox controls. */
        UI_SetText(header, "%s", key ? UI_GetString(key) : "");
    }
}

/* Versioned skin keys select RoC/TFT race-specific artwork. The defeat asset
 * is shared across races. Missing skin metadata falls back to the original
 * asset naming family, not to a newly-authored OpenRealm image. */
static cstring_t ScoreScreenRaceArt(gameClient_t *viewer) {
    static cstring_t const races[] = { "Human", "Orc", "Undead", "NightElf" };
    static PATHSTR fallback;
    cstring_t from_skin;
    uint32_t const race = viewer ? viewer->ps.race : 0;
    bool const defeated = viewer && viewer->jass.removed &&
        viewer->ps.stats[PLAYERSTATE_GAME_RESULT] == 1;

    if (defeated) return "UI\\Glues\\ScoreScreen\\ScoreScreen-Defeat\\ScoreScreen-Defeat.mdx";
    from_skin = Theme_PlayerString(viewer, "ScoreScreenVictory", NULL);
    if (from_skin && *from_skin) return from_skin;
    /* GUESS: if the mission is quit without RemovePlayer(VICTORY/DEFEAT),
     * retail's choice of outcome art is unknown. Default to victory/race
     * art rather than claiming a determined game result. */
    {
        uint32_t idx = race >= 1 && race <= 4 ? race - 1 : 0;
        bool const expansion = atoi(gi.CvarString("fs_expansion", "0")) != 0;
        snprintf(fallback, sizeof(fallback),
            "UI\\Glues\\ScoreScreen\\ScoreScreen-%sVictory%s\\ScoreScreen-%s-Victory%s.mdx",
            races[idx], expansion ? "Expansion" : "", races[idx], expansion ? "Expansion" : "");
    }
    return fallback;
}

static void ScoreScreenSetPage(gameClient_t *viewer, uint32_t tab) {
    ScoreScreen_t *score = &hud.score;
    char anim[32];
    frameDef_t *selected = score->ScoreScreenTabTextSelected;
    cstring_t label;

    /* Background exposes the authentic 4/5-column MDX sequences. */
    UI_SetText(score->ScoreScreenBackground, "%s",
               tab >= 2 ? "Scorescreen-5Button" : "Scorescreen-4Button");
    snprintf(anim, sizeof(anim), "Tab%u", (unsigned)tab + 1);
    UI_SetText(score->ScoreScreenPageTab, "%s", anim);
    score->ScoreScreenRaceArt->Portrait.model = UI_LoadModel(ScoreScreenRaceArt(viewer), false);
    UI_SetText(score->ScoreScreenRaceArt, "Stand");
    snprintf(anim, sizeof(anim), "TeamColor%02u Stand",
             (unsigned)(viewer->ps.color % 12));
    UI_SetText(score->ScoreScreenBarTop, "%s", anim);

    ScoreScreenSetHeaders(score, tab);
    FOR_LOOP(i, WC3_SCORE_TABS) UI_SetHidden(score->ScoreScreenTabText[i], i == tab);
    label = UI_GetString(((cstring_t[]){
        "SCORESCREEN_TAB0", "SCORESCREEN_TAB1", "SCORESCREEN_TAB2", "SCORESCREEN_TAB3"
    })[tab]);
    UI_SetText(selected, "%s", label);
    /* Retail defines the selected tab's font separately; its anchor follows
     * the active tab's FDF position rather than a manually spaced row. */
    memset(&selected->Points, 0, sizeof(selected->Points));
    selected->AnyPointsSet = true;
    /* These are the tab positions authored by ScoreScreen.fdf, not pixel
     * guesses. Anchor to an earlier-written background so the wire format
     * can resolve the relative frame without a forward-reference. */
    UI_SetPoint(selected, FRAMEPOINT_TOP, score->ScoreScreenBackground,
                FRAMEPOINT_TOP,
                ((float[]){ -0.200f, -0.074f, 0.055f, 0.180f })[tab], -0.036f);
    UI_SetHidden(selected, false);
}

bool UI_ShowScoreScreen(edict_t *ent, wc3ScoreSnapshot_t const *snapshot, uint32_t tab) {
    if (!ent || !ent->client || !snapshot || tab >= WC3_SCORE_TABS) return false;
    if (!hud.score.ScoreScreenFrame) return false;
    ScoreScreenSetPage(ent->client, tab);
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
    /* One generic exclusive presentation suppresses the gameplay world, HUD
     * and stale result dialog without changing persistent player UI flags.
     * Switching tabs refreshes the same unique modal. */
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, hud.score.ScoreScreenFrame, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_SCORE,
        .class_id = BZ_WC3_WINDOW_SCORE,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_ESCAPE |
                 UI_WINDOW_PRESENTATION));
    UI_SetCurrentClient(NULL);
    return true;
}
