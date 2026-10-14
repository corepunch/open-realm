/* Retail Warcraft III score screen: FDF controls/models + runtime score rows.
 * Player-row templates do not exist in the extracted FDF inventory, so only
 * their geometry and tint are provisional. No Blizzard artwork is bundled. */
#include "hud_local.h"
#include "score_screen_table.h"

#define WC3_SCORE_TABS 4
/* PRESENTATION GUESSES: approximate row geometry from the retail 4:3
 * screenshot. Verify against retail's runtime row controller. All coordinates
 * below use the same 0.8x0.6 normalized space as ScoreScreen.fdf. */
#define SCORE_ROW_X       0.009f
#define SCORE_ROW_Y      -0.116f
#define SCORE_ROW_W       0.733f
#define SCORE_ROW_H       0.031f
#define SCORE_ROW_STEP    0.034f
#define SCORE_ROW_ICON    0.025f

static void ScoreScreenUseSprite(frameDef_t *frame) {
    frame->Type = FT_SPRITE;
    UI_SetHidden(frame, false);
}

static cstring_t ScoreScreenRaceArt(gameClient_t *viewer);

static color32_t ScoreScreenPlayerColor(uint32_t player_color) {
    /* Same stock player palette used by the in-game multiboard. The subdued
     * translucent banner tint is a PRESENTATION GUESS, not a retail formula. */
    static color32_t const colors[] = {
        {255,3,3,155}, {0,66,255,155}, {28,230,185,155}, {84,0,129,155},
        {255,252,1,155}, {254,138,14,155}, {32,192,0,155}, {229,91,176,155},
        {149,150,151,155}, {126,191,241,155}, {16,98,70,155}, {78,42,4,155},
        {155,0,0,155}, {0,0,195,155}, {0,234,255,155}, {190,0,254,155},
        {235,205,135,155}, {248,164,139,155}, {191,255,128,155}, {220,185,235,155},
        {40,40,40,155}, {235,240,255,155}, {0,120,30,155}, {164,111,51,155},
    };
    return colors[MIN(player_color, (sizeof(colors) / sizeof(colors[0])) - 1)];
}

static cstring_t ScoreScreenPlayerIcon(uint32_t race) {
    /* Stock archive files, not replacement art. Preserve nonstandard race
     * slots without substituting a potentially incorrect icon. */
    switch (race) {
    case 1: return "UI\\Glues\\ScoreScreen\\scorescreen-player-human.blp";
    case 2: return "UI\\Glues\\ScoreScreen\\scorescreen-player-orc.blp";
    case 3: return "UI\\Glues\\ScoreScreen\\scorescreen-player-undead.blp";
    case 4: return "UI\\Glues\\ScoreScreen\\scorescreen-player-nightelf.blp";
    default: return NULL;
    }
}

static frameDef_t *ScoreScreenSpawn(frameDef_t *parent, FRAMETYPE type) {
    frameDef_t *frame = UI_Spawn(type, parent);
    if (frame) UI_SetHidden(frame, false);
    return frame;
}

static void ScoreScreenPlace(frameDef_t *frame, frameDef_t *parent,
                             float x, float y, float w, float h) {
    UI_SetSize(frame, w, h);
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, parent, FRAMEPOINT_TOPLEFT, x, y);
}

/* One immutable viewport row contains its own background, player portrait,
 * name and five individually positioned score cells. Hiding the parent stops
 * serialization of the entire offscreen row (no clipping dependency). */
static bool ScoreScreenMakeRow(uint32_t slot) {
    ScoreScreen_t *score = &hud.score;
    frameDef_t *root = ScoreScreenSpawn(score->ScoreScreenFrame, FT_FRAME);
    frameDef_t *backdrop, *border, *icon, *name;
    if (!root) return false;
    hud.score_rows[slot].root = root;
    ScoreScreenPlace(root, score->ScoreScreenFrame, SCORE_ROW_X,
                     SCORE_ROW_Y - slot * SCORE_ROW_STEP, SCORE_ROW_W, SCORE_ROW_H);

    backdrop = ScoreScreenSpawn(root, FT_TEXTURE);
    border = ScoreScreenSpawn(root, FT_TEXTURE);
    icon = ScoreScreenSpawn(root, FT_TEXTURE);
    name = ScoreScreenSpawn(root, FT_TEXT);
    if (!backdrop || !border || !icon || !name) return false;
    hud.score_rows[slot].backdrop = backdrop;
    hud.score_rows[slot].border = border;
    hud.score_rows[slot].icon = icon;
    hud.score_rows[slot].name = name;
    ScoreScreenPlace(backdrop, root, 0, 0, SCORE_ROW_W, SCORE_ROW_H);
    UI_SetTexture(backdrop, "UI\\Widgets\\EscMenu\\Human\\blank-background.blp", false);
    /* The existing silver banner border is a stock score-screen asset. Its
     * exact retail runtime placement is NOT defined by ScoreScreen.fdf. */
    ScoreScreenPlace(border, root, 0, 0, 0.153f, SCORE_ROW_H);
    UI_SetTexture(border, "UI\\Glues\\ScoreScreen\\scorescreen-silverbannerborder.blp", false);
    ScoreScreenPlace(icon, root, 0.005f, -0.003f, SCORE_ROW_ICON, SCORE_ROW_ICON);
    ScoreScreenPlace(name, root, 0.034f, -0.003f, 0.111f, SCORE_ROW_H-0.005f);
    name->Font = score->ScoreScreen4ColumnHeader[0]->Font;
    name->Font.Size = 0.011f; /* PRESENTATION GUESS */
    name->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
    name->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;

    FOR_LOOP(c, 5) {
        frameDef_t *cell = ScoreScreenSpawn(root, FT_TEXT);
        if (!cell) return false;
        hud.score_rows[slot].cells[c] = cell;
        cell->Font = score->ScoreScreen4ColumnHeader[0]->Font;
        cell->Font.Size = 0.011f; /* PRESENTATION GUESS */
        cell->Font.Justification.Horizontal = FONT_JUSTIFYCENTER;
        cell->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
    }
    /* Retail's Heroes Used column holds a portrait plus level for each
     * currently owned hero, not a number. The stock FDF defines the column
     * but does not supply these per-player runtime frames. */
    FOR_LOOP(h, WC3_SCORE_HERO_PORTRAITS) {
        frameDef_t *portrait = ScoreScreenSpawn(root, FT_TEXTURE);
        frameDef_t *level = ScoreScreenSpawn(root, FT_TEXT);
        if (!portrait || !level) return false;
        hud.score_rows[slot].hero_icons[h] = portrait;
        hud.score_rows[slot].hero_levels[h] = level;
        /* Keep each portrait and its level side-by-side, inside the first
         * column.  Advance by the combined pair width, not portrait width. */
        ScoreScreenPlace(portrait, root, 0.158f + h * 0.025f, -0.003f, 0.018f, 0.021f);
        ScoreScreenPlace(level, root, 0.177f + h * 0.025f, -0.003f, 0.006f, 0.021f);
        level->Font = score->ScoreScreen4ColumnHeader[0]->Font;
        level->Font.Size = 0.009f; /* runtime hero badge: visual matching pending */
        level->Font.Color = MAKE(color32_t, 255, 215, 48, 255);
        level->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
        level->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
        UI_SetHidden(portrait, true);
        UI_SetHidden(level, true);
    }
    hud.score_rows[slot].hero_overflow = ScoreScreenSpawn(root, FT_TEXT);
    if (!hud.score_rows[slot].hero_overflow) return false;
    ScoreScreenPlace(hud.score_rows[slot].hero_overflow, root, 0.258f, -0.003f, 0.020f, 0.024f);
    hud.score_rows[slot].hero_overflow->Font = score->ScoreScreen4ColumnHeader[0]->Font;
    hud.score_rows[slot].hero_overflow->Font.Size = 0.008f;
    hud.score_rows[slot].hero_overflow->Font.Justification.Horizontal = FONT_JUSTIFYCENTER;
    hud.score_rows[slot].hero_overflow->Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
    UI_SetHidden(hud.score_rows[slot].hero_overflow, true);
    return true;
}

void UI_LoadHudScoreScreen(void) {
    ScoreScreen_t *score = &hud.score;
    if (score->ScoreScreenFrame) return;
    if (!ScoreScreen_Load(score)) {
        memset(score, 0, sizeof(*score));
        return;
    }
    UI_SetAllPoints(score->ScoreScreenFrame);
    UI_SetHidden(score->ScoreScreenFrame, false);
    UI_SetHidden(score->ScoreScreenMainPanel, false);
    ScoreScreenUseSprite(score->ScoreScreenBackground);
    ScoreScreenUseSprite(score->ScoreScreenPageTab);
    /* Render the selected-tab MDX after the background and its labels above
     * the sprite; the MDX visibility tracks choose the single active geoset. */
    score->ScoreScreenPageTab->ui_flags |= UIFLAG_SPRITE_OVERLAY;
    FOR_LOOP(i, WC3_SCORE_TABS) {
        frameDef_t *caption = score->ScoreScreenTabText[i];
        caption->ui_flags = (caption->ui_flags & ~UIFLAG_TEXT_RANK_MASK) |
            UIFLAG_TEXT_OVERLAY | (i << UIFLAG_TEXT_RANK_SHIFT);
    }
    score->ScoreScreenTabTextSelected->ui_flags |= UIFLAG_TEXT_OVERLAY;
    ScoreScreenUseSprite(score->ScoreScreenRaceArt);
    ScoreScreenUseSprite(score->ScoreScreenBarTop);
    UI_SetOnClick(score->ScoreScreenOKButton,
                  UI_WINDOW_CLOSE_COMMAND_PREFIX "scorescreen_continue");
    FOR_LOOP(i, WC3_SCORE_TABS)
        UI_SetOnClick(score->ScoreScreenTabButton[i], "scorescreen_tab %u", (unsigned)i);
    UI_SetOnClick(score->ScoreScreenScrollBar, "scorescreen_scroll");
    /* These GLUECHECKBOXes are authored in ScoreScreen.fdf. Client clicks
     * submit only an intent; the server keeps each spectator's view state. */
    FOR_LOOP(c, 4) UI_SetOnClick(score->ScoreScreen4ColumnButton[c],
                                "scorescreen_sort %u", (unsigned)c);
    FOR_LOOP(c, 5) UI_SetOnClick(score->ScoreScreen5ColumnButton[c],
                                "scorescreen_sort %u", (unsigned)c);
    if (score->ScoreScreenTeamsButton)
        UI_SetOnClick(score->ScoreScreenTeamsButton, "scorescreen_teams");
    if (score->ScoreScreenTeamColorButton)
        UI_SetOnClick(score->ScoreScreenTeamColorButton, "scorescreen_allycolors");
    /* Replay recording/saving is not implemented: do not expose a button
     * with a false-success or inert action. Restore it with a real backend. */
    if (score->ScoreScreenSaveReplayButton)
        UI_SetHidden(score->ScoreScreenSaveReplayButton, true);
    if (score->ScoreScreenReplaySavedButton)
        UI_SetHidden(score->ScoreScreenReplaySavedButton, true);

    FOR_LOOP(i, WC3_SCORE_VISIBLE_ROWS) {
        if (!ScoreScreenMakeRow(i)) {
            memset(score, 0, sizeof(*score));
            return;
        }
    }
    if (!score->ScoreScreenElapsedValue) {
        /* Runtime value only. The original static label remains in the FDF;
         * this fallback uses its bottom-left anchor from the agent report. */
        hud.score_elapsed = ScoreScreenSpawn(score->ScoreScreenFrame, FT_TEXT);
        if (!hud.score_elapsed) { memset(score, 0, sizeof(*score)); return; }
        UI_SetSize(hud.score_elapsed, 0.110f, 0.016f);
        UI_SetPoint(hud.score_elapsed, FRAMEPOINT_BOTTOMLEFT,
                    score->ScoreScreenFrame, FRAMEPOINT_BOTTOMLEFT, 0.112f, 0.05f);
        hud.score_elapsed->Font = score->ScoreScreen4ColumnHeader[0]->Font;
        hud.score_elapsed->Font.Size = 0.011f;
        hud.score_elapsed->Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
    }
}

static void ScoreScreenSetHeaders(ScoreScreen_t *score, uint32_t tab,
                                   wc3ScoreView_t const *view) {
    static cstring_t const keys[][5] = {
        { "OVERVIEW_COLUMN0", "OVERVIEW_COLUMN1", "OVERVIEW_COLUMN2", "OVERVIEW_COLUMN3", NULL },
        { "UNIT_COLUMN0", "UNIT_COLUMN1", "UNIT_COLUMN2", "UNIT_COLUMN3", "UNIT_COLUMN4" },
        { "HEROES_COLUMN0", "HEROES_COLUMN1", "HEROES_COLUMN2", "HEROES_COLUMN3", "HEROES_COLUMN4" },
        { "RESOURCES_COLUMN0", "RESOURCES_COLUMN1", "RESOURCES_COLUMN2", "RESOURCES_COLUMN3", "RESOURCES_COLUMN4" },
    };
    bool const wide = tab >= 1;
    UI_SetHidden(score->ScoreScreen4ColumnFrame, wide);
    UI_SetHidden(score->ScoreScreen5ColumnFrame, !wide);
    FOR_LOOP(i, wide ? 5u : 4u) {
        frameDef_t *header = wide ? score->ScoreScreen5ColumnHeader[i] : score->ScoreScreen4ColumnHeader[i];
        cstring_t key = keys[tab][i];
        UI_SetText(header, "%s", key ? UI_GetString(key) : "");
        UI_SetHidden(wide ? score->ScoreScreen5ColumnButton[i]
                          : score->ScoreScreen4ColumnButton[i], !key);
        {
            frameDef_t *button = wide ? score->ScoreScreen5ColumnButton[i]
                                      : score->ScoreScreen4ColumnButton[i];
            button->CheckBox.Checked = view && view->column == i;
        }
    }
}

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
    /* GUESS: retail's art after quit without an explicit outcome. */
    {
        uint32_t idx = race >= 1 && race <= 4 ? race - 1 : 0;
        bool const expansion = atoi(gi.CvarString("fs_expansion", "0")) != 0;
        snprintf(fallback, sizeof(fallback),
            "UI\\Glues\\ScoreScreen\\ScoreScreen-%sVictory%s\\ScoreScreen-%s-Victory%s.mdx",
            races[idx], expansion ? "Expansion" : "", races[idx], expansion ? "Expansion" : "");
    }
    return fallback;
}

static void ScoreScreenSetPage(gameClient_t *viewer, uint32_t tab,
                               wc3ScoreView_t const *view) {
    ScoreScreen_t *score = &hud.score;
    char anim[32];
    frameDef_t *selected = score->ScoreScreenTabTextSelected;
    cstring_t label;

    UI_SetText(score->ScoreScreenBackground, "#!%s",
               tab >= 1 ? "Scorescreen-5Button" : "Scorescreen-4Button");
    snprintf(anim, sizeof(anim), "#!Tab%u", (unsigned)tab + 1);
    UI_SetText(score->ScoreScreenPageTab, "%s", anim);
    {
        cstring_t art = ScoreScreenRaceArt(viewer);
        uint32_t model = UI_LoadModel(art, false);
        score->ScoreScreenRaceArt->Portrait.model = model;
        BZ_WC3_SCORE_LOG("score art select player=%u race=%u expansion=%u path=%s model_index=%u frame=%p size=(%.4f,%.4f)",
            viewer ? (unsigned)viewer->ps.number : UINT32_MAX,
            viewer ? (unsigned)viewer->ps.race : 0,
            (unsigned)(atoi(gi.CvarString("fs_expansion", "0")) != 0),
            art ? art : "(null)", (unsigned)model,
            (void *)score->ScoreScreenRaceArt,
            score->ScoreScreenRaceArt->Width, score->ScoreScreenRaceArt->Height);
    }
    UI_SetText(score->ScoreScreenRaceArt, "#!Stand");
    snprintf(anim, sizeof(anim), "TeamColor%02u Stand", (unsigned)(viewer->ps.color % 12));
    UI_SetText(score->ScoreScreenBarTop, "%s", anim);

    ScoreScreenSetHeaders(score, tab, view);
    if (score->ScoreScreenTeamsButton)
        score->ScoreScreenTeamsButton->CheckBox.Checked = view && view->teams;
    if (score->ScoreScreenTeamColorButton)
        score->ScoreScreenTeamColorButton->CheckBox.Checked = view && view->ally_colors;
    FOR_LOOP(i, WC3_SCORE_TABS) UI_SetHidden(score->ScoreScreenTabText[i], i == tab);
    label = UI_GetString(((cstring_t[]){
        "SCORESCREEN_TAB0", "SCORESCREEN_TAB1", "SCORESCREEN_TAB2", "SCORESCREEN_TAB3"
    })[tab]);
    UI_SetText(selected, "%s", label);
    selected->ui_flags = (selected->ui_flags & ~UIFLAG_TEXT_RANK_MASK) |
        UIFLAG_TEXT_OVERLAY | (tab << UIFLAG_TEXT_RANK_SHIFT);
    memset(&selected->Points, 0, sizeof(selected->Points));
    selected->AnyPointsSet = true;
    UI_SetPoint(selected, FRAMEPOINT_TOP, score->ScoreScreenBackground,
                FRAMEPOINT_TOP,
                ((float[]){ -0.200f, -0.074f, 0.055f, 0.180f })[tab], -0.036f);
    UI_SetHidden(selected, false);
}

static void ScoreScreenFillRow(uint32_t slot, uint32_t tab,
                               wc3ScorePlayerSnapshot_t const *player,
                               int32_t const peaks[5], uint32_t tint_color) {
    static float const cols4[4] = { -0.172f, -0.0273f, 0.1182f, 0.2647f };
    static float const cols5[5] = { -0.1865f, -0.0703f, 0.0462f, 0.1627f, 0.280f };
    frameDef_t *root = hud.score_rows[slot].root;
    uint32_t count = WC3_ScoreColumns(tab);
    float width = count == 5 ? 0.0935f : 0.1225f;
    cstring_t icon = ScoreScreenPlayerIcon(player->race);
    char text[32];

    UI_SetHidden(root, false);
    hud.score_rows[slot].backdrop->Color = ScoreScreenPlayerColor(tint_color);
    UI_SetHidden(hud.score_rows[slot].border, false);
    UI_SetHidden(hud.score_rows[slot].icon, !icon);
    if (icon) UI_SetTexture(hud.score_rows[slot].icon, icon, false);
    UI_SetText(hud.score_rows[slot].name, "%s", player->name);
    /* Every tab reuses these row frames. Hide stale portraits when switching
     * away from Heroes or changing the sorted/scrolled player in this slot. */
    FOR_LOOP(h, WC3_SCORE_HERO_PORTRAITS) {
        bool const visible = tab == 2 && h < player->hero_count &&
            player->heroes[h].art[0];
        frameDef_t *portrait = hud.score_rows[slot].hero_icons[h];
        frameDef_t *badge = hud.score_rows[slot].hero_levels[h];
        UI_SetHidden(portrait, !visible);
        UI_SetHidden(badge, !visible);
        if (visible) {
            UI_SetTexture(portrait, player->heroes[h].art, false);
            UI_SetText(badge, "%u", (unsigned)player->heroes[h].level);
        }
    }
    UI_SetHidden(hud.score_rows[slot].hero_overflow,
                 tab != 2 || player->hero_count <= WC3_SCORE_HERO_PORTRAITS);
    if (tab == 2 && player->hero_count > WC3_SCORE_HERO_PORTRAITS)
        UI_SetText(hud.score_rows[slot].hero_overflow, "+%u",
                   (unsigned)(player->hero_count - WC3_SCORE_HERO_PORTRAITS));
    FOR_LOOP(c, 5) {
        frameDef_t *cell = hud.score_rows[slot].cells[c];
        int32_t value = 0;
        bool percent = false;
        bool const has_value = c < count && WC3_ScoreCellValue(player, tab, c, &value, &percent);
        /* Keep the first Heroes column available to sorting/audits but do
         * not print its numeric count over the hero portraits. */
        UI_SetHidden(cell, c >= count || (tab == 2 && c == 0));
        if (c >= count || (tab == 2 && c == 0)) continue;
        UI_SetSize(cell, width, SCORE_ROW_H - 0.006f);
        /* Column offsets are the *retail FDF header centers*, in a canvas
         * of width 0.8. The row start subtracts its independent left inset. */
        UI_SetPoint(cell, FRAMEPOINT_TOP, root, FRAMEPOINT_TOPLEFT,
                    0.400f + (count == 5 ? cols5[c] : cols4[c]) - SCORE_ROW_X, -0.003f);
        if (!has_value) strlcpy(text, "-", sizeof(text));
        else if (tab == 3 && c == 2) /* Provisional gold/lumber trade direction. */
            snprintf(text, sizeof(text), "%d/%d",
                     player->values[WC3_PLAYER_SCORE_GOLD_GIVEN],
                     player->values[WC3_PLAYER_SCORE_LUMBER_GIVEN]);
        else if (percent) snprintf(text, sizeof(text), "%d%%", value);
        else snprintf(text, sizeof(text), "%d", value);
        UI_SetText(cell, "%s", text);
        /* PRESENTATION GUESS: the highest positive value in a retail column
         * is gold, based on the provided retail screenshot. */
        cell->Font.Color = has_value && value > 0 && value == peaks[c]
            ? MAKE(color32_t, 255, 215, 48, 255) : COLOR32_WHITE;
    }
}

bool UI_ShowScoreScreen(edict_t *ent, wc3ScoreSnapshot_t const *snapshot,
                        uint32_t tab, uint32_t scroll,
                        wc3ScoreView_t const *view) {
    uint32_t max_scroll, start;
    int32_t peaks[5] = { 0 };
    uint8_t order[MAX_PLAYERS];
    ScoreScreen_t *score = &hud.score;
    frameDef_t *elapsed;
    uint32_t seconds;
    if (!ent || !ent->client || !snapshot || tab >= WC3_SCORE_TABS ||
        snapshot->count > MAX_PLAYERS || !score->ScoreScreenFrame) return false;
    max_scroll = WC3_ScoreMaxScroll(snapshot->count);
    start = MIN(scroll, max_scroll);
    WC3_ScoreOrderedPlayers(snapshot, tab, view, order);
    ScoreScreenSetPage(ent->client, tab, view);

    /* One bounded presentation-stage trace, not per-row/cell world dumps. */
    BZ_WC3_SCORE_LOG("score page render tab=%u players=%u first_row=%u",
        (unsigned)tab, (unsigned)snapshot->count, (unsigned)start);

    /* Snapshot peaks are recomputed for each tab and include hidden rows.
     * Scores are never recalculated from current/possibly removed entities. */
    FOR_LOOP(i, snapshot->count) FOR_LOOP(c, WC3_ScoreColumns(tab)) {
        int32_t value;
        if (WC3_ScoreCellValue(&snapshot->players[i], tab, c, &value, NULL))
            peaks[c] = MAX(peaks[c], value);
    }
    FOR_LOOP(i, WC3_SCORE_VISIBLE_ROWS) {
        bool visible = start + i < snapshot->count;
        if (!hud.score_rows[i].root) return false;
        UI_SetHidden(hud.score_rows[i].root, !visible);
        if (visible) {
            wc3ScorePlayerSnapshot_t const *player = &snapshot->players[order[start + i]];
            uint32_t const tint = view && view->ally_colors
                ? WC3_ScoreTeamColor(snapshot, player) : player->color;
            ScoreScreenFillRow(i, tab, player, peaks, tint);
        }
    }
    /* The stock scrollbar is self-owned; the generic client reports its
     * normalized position via its authored OnClick on wheel/thumb input. */
    UI_SetHidden(score->ScoreScreenScrollBar, max_scroll == 0);
    score->ScoreScreenScrollBar->Slider.MinValue = 0.0f;
    score->ScoreScreenScrollBar->Slider.MaxValue = 1.0f;
    score->ScoreScreenScrollBar->Slider.InitialValue = max_scroll
        ? (float)start / (float)max_scroll : 0.0f;

    elapsed = score->ScoreScreenElapsedValue ? score->ScoreScreenElapsedValue : hud.score_elapsed;
    if (elapsed) {
        seconds = snapshot->elapsed_ms / 1000u;
        UI_SetText(elapsed, "%u:%02u:%02u", seconds / 3600u,
                   (seconds / 60u) % 60u, seconds % 60u);
    }
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, score->ScoreScreenFrame, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_SCORE, .class_id = BZ_WC3_WINDOW_SCORE,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_ESCAPE |
                 UI_WINDOW_PRESENTATION));
    UI_SetCurrentClient(NULL);
    return true;
}
