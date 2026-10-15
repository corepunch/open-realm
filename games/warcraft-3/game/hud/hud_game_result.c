/*
 * hud_game_result.c — temporary native victory / defeat fallback dialog.
 *
 * Warcraft normally lets Blizzard.j build custom/melee result dialogs from the
 * generic ScriptDialog natives. Those natives are not complete yet, so this
 * FDF-backed overlay remains a compatibility fallback. RemovePlayer queues it;
 * UI_FlushPendingGameResults emits it after queued result events. Cinematic UI
 * normally defers the fallback, except when a script pause has frozen the
 * scheduler for the stock single-player result dialog path.
 */

#include "hud_local.h"

/* The stock GameResult dialog is authored as a standalone DIALOG, while the
 * rest of OpenRealm's in-game menus are sent through the client window path.
 * Reuse the already-loaded Esc-menu frame art so victory/defeat has the same
 * race-skinned panel background, pinned to the authored dialog bounds. */
static void GameResultPrepareWindow(void) {
    frameDef_t *dialog = hud.result.GameResultDialog;
    frameDef_t *backdrop = hud.result.GameResultBackdrop;
    frameDef_t *menu_backdrop = hud.menu.EscMenuBackdrop;

    if (!dialog || !backdrop || backdrop->Type != FT_BACKDROP) return;
    if (menu_backdrop && menu_backdrop->Type == FT_BACKDROP)
        backdrop->Backdrop = menu_backdrop->Backdrop;

    memset(&backdrop->Points, 0, sizeof(backdrop->Points));
    backdrop->AnyPointsSet = true;
    UI_SetPoint(backdrop, FRAMEPOINT_TOPLEFT, dialog, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(backdrop, FRAMEPOINT_BOTTOMRIGHT, dialog, FRAMEPOINT_BOTTOMRIGHT,
                0.0f, 0.0f);
    UI_SetHidden(backdrop, false);
    UI_CenterFrame(dialog);
}

/* The stock victory fallback leaves Quit below the nominal dialog rectangle.
 * Keep the window itself unchanged and place Quit directly below Continue,
 * matching the compact vertical button stack used by the other game menus. */
static bool GameResultCreateReduceButton(void) {
    frameDef_t *dialog = hud.result.GameResultDialog;
    frameDef_t *template = hud.result.GameResultRestartButton;
    frameDef_t *button;

    if (!dialog || !template) return false;
    button = UI_FindChildFrame(dialog, "GameResultReduceDifficultyButton");
    if (!button) button = UI_CloneFrameTree(template, dialog);
    if (!button) return false;
    snprintf(button->Name, sizeof(button->Name), "GameResultReduceDifficultyButton");
    hud.game_result_reduce_button = button;
    UI_SetEnabled(button, true);
    hud.game_result_reduce_text = UI_FindChildFrame(button, "GameResultRestartButtonText");
    if (!hud.game_result_reduce_text) return false;
    snprintf(hud.game_result_reduce_text->Name, sizeof(hud.game_result_reduce_text->Name),
             "GameResultReduceDifficultyButtonText");
    UI_SetOnClick(button, UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_reduce_difficulty");
    UI_SetHidden(button, true);
    return true;
}

/* An additional defeat action uses the stock Restart button's FDF styling;
 * the game data doesn't expose a separate Reduce Difficulty control in the
 * fallback frame. The authored ScriptDialog remains authoritative when present. */
static void GameResultPositionDefeatButtons(bool can_reduce) {
    frameDef_t *dialog = hud.result.GameResultDialog;
    frameDef_t *quit = hud.result.GameResultQuitButton;
    frameDef_t *restart = hud.result.GameResultRestartButton;
    frameDef_t *reduce = hud.game_result_reduce_button;

    if (!dialog || !quit || !restart) return;
    UI_SetSize(dialog, dialog->Width, hud.game_result_original_height +
        (can_reduce && reduce ? MAX(reduce->Height, 0.031f) + 0.008f : 0.0f));
    if (can_reduce && reduce) {
        memset(&reduce->Points, 0, sizeof(reduce->Points));
        reduce->AnyPointsSet = true;
        UI_SetPoint(reduce, FRAMEPOINT_TOP, restart, FRAMEPOINT_BOTTOM, 0.0f, -0.008f);
    }
    memset(&quit->Points, 0, sizeof(quit->Points));
    quit->AnyPointsSet = true;
    UI_SetPoint(quit, FRAMEPOINT_TOP, can_reduce && reduce ? reduce : restart,
                FRAMEPOINT_BOTTOM, 0.0f, -0.008f);
    UI_CenterFrame(dialog);
}

static void GameResultPositionVictoryQuit(void) {
    frameDef_t *quit = hud.result.GameResultQuitButton;
    frameDef_t *continue_button = hud.result.GameResultContinueButton;

    if (!quit || !continue_button) return;
    memset(&quit->Points, 0, sizeof(quit->Points));
    quit->AnyPointsSet = true;
    UI_SetPoint(quit, FRAMEPOINT_TOP, continue_button, FRAMEPOINT_BOTTOM,
                0.0f, -0.008f);
}

void UI_LoadHudGameResult(void) {
    bool global_strings;
    bool dialog_loaded;

    if (hud.result.GameResultDialog) return;
    global_strings = UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf");
    dialog_loaded = GameResultDialog_Load(&hud.result);
    if (dialog_loaded) {
        GameResultPrepareWindow();
        GameResultCreateReduceButton();
    }
    (void)global_strings; /* Used only by the optional score diagnostics below. */
    BZ_WC3_SCORE_LOG("hud load global_strings=%u dialog_loaded=%u root=%p text=%p continue=%p restart=%p quit=%p",
        (unsigned)global_strings, (unsigned)dialog_loaded,
        (void *)hud.result.GameResultDialog, (void *)hud.result.GameResultText,
        (void *)hud.result.GameResultContinueButton, (void *)hud.result.GameResultRestartButton,
        (void *)hud.result.GameResultQuitButton);
}

static cstring_t GameResultString(cstring_t key, cstring_t fallback) {
    cstring_t value = UI_GetString(key);
    return value && *value && strcmp(value, key) ? value : fallback;
}

/* This fallback exposes only result actions the current engine can execute.
 * Full Warcraft result policy belongs to Blizzard.j + ScriptDialog. */
void UI_ShowGameResult(edict_t *ent, uint32_t result) {
    bool victory, single_player, can_reduce;

    BZ_WC3_SCORE_LOG("hud show enter ent=%p number=%ld client=%p result=%u",
        (void *)ent, ent ? (long)ent->s.number : -1L,
        ent ? (void *)ent->client : NULL, (unsigned)result);
    if (!ent || !ent->client || result > 1) {
        BZ_WC3_SCORE_LOG("hud show abort reason=invalid_args");
        return;
    }
    victory = result == 0;
    single_player = G_IsSinglePlayer();
    can_reduce = !victory && single_player &&
        (level.setup.difficulty == 1 || level.setup.difficulty == 2);

    if (!hud.result.GameResultDialog) {
        BZ_WC3_SCORE_LOG("hud show abort reason=missing_GameResultDialog_FDF");
        return;
    }

    UI_SetText(hud.result.GameResultText, "%s", GameResultString(
        victory ? "GAMEOVER_VICTORY_MSG" : "GAMEOVER_DEFEAT_MSG",
        victory ? "Victory!" : "Defeat!"));

    UI_SetHidden(hud.result.GameResultContinueButton, !victory && !single_player);
    UI_SetText(hud.result.GameResultContinueButtonText, "%s", GameResultString(
        victory
            ? (single_player ? "GAMEOVER_CONTINUE" : "GAMEOVER_CONTINUE_GAME")
            : "GAMEOVER_LOAD",
        victory ? (single_player ? "Continue" : "Continue Game") : "Load"));
    UI_SetOnClick(hud.result.GameResultContinueButton,
        !victory && single_player
            ? UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_load"
            : UI_WINDOW_CLOSE_COMMAND_PREFIX "hidegameresult");

    UI_SetHidden(hud.result.GameResultRestartButton, victory || !single_player);
    UI_SetText(hud.result.GameResultRestartButtonText, "%s",
        GameResultString("GAMEOVER_RESTART", "Restart"));
    UI_SetOnClick(hud.result.GameResultRestartButton,
        UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_restart");

    if (hud.game_result_reduce_button && hud.game_result_reduce_text) {
        UI_SetText(hud.game_result_reduce_text, "%s", GameResultString(
            "GAMEOVER_REDUCE_DIFFICULTY", "Reduce Difficulty"));
        UI_SetHidden(hud.game_result_reduce_button, !can_reduce);
    }

    UI_SetHidden(hud.result.GameResultQuitButton, false);
    UI_SetText(hud.result.GameResultQuitButtonText, "%s", GameResultString(
        single_player ? "GAMEOVER_QUIT_MISSION" : "GAMEOVER_QUIT_GAME",
        single_player ? "Quit Mission" : "Quit Game"));
    UI_SetOnClick(hud.result.GameResultQuitButton,
        UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_quit");
    if (victory) {
        UI_SetSize(hud.result.GameResultDialog, hud.result.GameResultDialog->Width,
                   hud.game_result_original_height);
        GameResultPositionVictoryQuit();
        UI_CenterFrame(hud.result.GameResultDialog);
    } else {
        GameResultPositionDefeatButtons(can_reduce);
    }

    BZ_WC3_SCORE_LOG("hud show write window=%08x ent=%u connected=%u victory=%u single_player=%u uiflags=0x%08x",
        (unsigned)BZ_WC3_WINDOW_RESULT, (unsigned)ent->s.number,
        (unsigned)ent->client->connected, (unsigned)victory, (unsigned)single_player,
        (unsigned)ent->client->ps.uiflags);
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, hud.result.GameResultDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_RESULT,
        .class_id = BZ_WC3_WINDOW_RESULT,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE));
    UI_SetCurrentClient(NULL);
    BZ_WC3_SCORE_LOG("hud show write complete window=%08x ent=%u",
        (unsigned)BZ_WC3_WINDOW_RESULT, (unsigned)ent->s.number);
}

void UI_FlushPendingGameResults(void) {
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        edict_t *ent;
        uint32_t result;

        if (!client->jass.pending_game_result) continue;

        if (!client->connected ||
            level.events.read < client->jass.pending_game_result_event ||
            (client->ps.client_ui_state == CLIENT_UI_CINEMATIC && !level.script_paused)) {
            continue;
        }

        if (client->ps.client_ui_state == CLIENT_UI_CINEMATIC && level.script_paused) {
            BZ_WC3_SCORE_LOG("flush cinematic override client_index=%u player=%u reason=script_paused_result_dialog",
                (unsigned)i, (unsigned)client->ps.number);
        }

        result = (uint32_t)client->jass.pending_game_result - 1;
        client->jass.pending_game_result = 0;
        client->jass.pending_game_result_event = 0;
        ent = G_GetPlayerEntityByNumber(client->ps.number);
        BZ_WC3_SCORE_LOG("flush ready client_index=%u player=%u result=%u ent=%p ent_number=%ld",
            (unsigned)i, (unsigned)client->ps.number, (unsigned)result,
            (void *)ent, ent ? (long)ent->s.number : -1L);
        if (ent) {
            UI_ShowGameResult(ent, result);
        } else {
            BZ_WC3_SCORE_LOG("flush drop player=%u reason=no_player_edict", (unsigned)client->ps.number);
        }
    }
}

void UI_HideGameResult(edict_t *ent) {
    if (!ent) return;
    BZ_WC3_SCORE_LOG("hud hide ent=%u", (unsigned)ent->s.number);
    /* svc_window result buttons close the client-owned window before forwarding
     * their server command.  Retain the legacy layer clear so upgraded clients
     * cannot keep a stale pre-window result layout around. */
    UI_ClearLayer(ent, LAYER_GAME_RESULT);
}

#ifdef BZ_TESTS
#include "shared/test.h"

/* The native fallback must reuse the authored FDF template, not create an
 * unrelated unstyled button or require another project-owned layout. */
TEST(wc3_game_result, reduce_button_clones_restart_style_and_extends_defeat_rows) {
    frameDef_t *dialog, *restart, *restart_text, *quit;
    float base_height;

    UI_ResetHud();
    dialog = UI_Spawn(FT_DIALOG, NULL);
    restart = UI_Spawn(FT_GLUETEXTBUTTON, dialog);
    restart_text = UI_Spawn(FT_TEXT, restart);
    quit = UI_Spawn(FT_GLUETEXTBUTTON, dialog);
    T_NOT_NULL(dialog);
    T_NOT_NULL(restart);
    T_NOT_NULL(restart_text);
    T_NOT_NULL(quit);
    if (!dialog || !restart || !restart_text || !quit) return;
    snprintf(restart_text->Name, sizeof(restart_text->Name), "GameResultRestartButtonText");
    UI_SetSize(dialog, 0.3f, 0.2f);
    UI_SetSize(restart, 0.15f, 0.03f);
    hud.result.GameResultDialog = dialog;
    hud.result.GameResultRestartButton = restart;
    hud.result.GameResultQuitButton = quit;
    T_ASSERT(GameResultCreateReduceButton());
    T_NOT_NULL(hud.game_result_reduce_button);
    T_NOT_NULL(hud.game_result_reduce_text);
    T_ASSERT(hud.game_result_reduce_button != restart);
    T_STREQ(hud.game_result_reduce_button->OnClick,
             UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_reduce_difficulty");
    T_ASSERT(hud.game_result_reduce_button->hidden);

    base_height = hud.game_result_original_height;
    GameResultPositionDefeatButtons(true);
    T_ASSERT(dialog->Height > base_height);
    GameResultPositionDefeatButtons(false);
    T_FEQ(dialog->Height, base_height, 0.0001f);
    UI_ResetHud();
}
#endif
