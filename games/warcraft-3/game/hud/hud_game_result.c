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

static DWORD game_result_last_defer_log[MAX_PLAYERS];

/* The stock GameResult dialog is authored as a standalone DIALOG, while the
 * rest of OpenRealm's in-game menus are sent through the client window path.
 * Reuse the already-loaded Esc-menu frame art so victory/defeat has the same
 * race-skinned panel background, pinned to the authored dialog bounds. */
static void GameResultPrepareWindow(void) {
    LPFRAMEDEF dialog = hud.result.GameResultDialog;
    LPFRAMEDEF backdrop = hud.result.GameResultBackdrop;
    LPFRAMEDEF menu_backdrop = hud.menu.EscMenuBackdrop;

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
static void GameResultPositionVictoryQuit(void) {
    LPFRAMEDEF quit = hud.result.GameResultQuitButton;
    LPFRAMEDEF continue_button = hud.result.GameResultContinueButton;

    if (!quit || !continue_button) return;
    memset(&quit->Points, 0, sizeof(quit->Points));
    quit->AnyPointsSet = true;
    UI_SetPoint(quit, FRAMEPOINT_TOP, continue_button, FRAMEPOINT_BOTTOM,
                0.0f, -0.008f);
}

void UI_LoadHudGameResult(void) {
    BOOL global_strings;
    BOOL dialog_loaded;

    if (hud.result.GameResultDialog) return;
    global_strings = UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf");
    dialog_loaded = GameResultDialog_Load(&hud.result);
    if (dialog_loaded) GameResultPrepareWindow();
    G_GameResultDebug("hud load global_strings=%u dialog_loaded=%u root=%p text=%p continue=%p restart=%p quit=%p",
        (unsigned)global_strings, (unsigned)dialog_loaded,
        (void *)hud.result.GameResultDialog, (void *)hud.result.GameResultText,
        (void *)hud.result.GameResultContinueButton, (void *)hud.result.GameResultRestartButton,
        (void *)hud.result.GameResultQuitButton);
}

static LPCSTR GameResultString(LPCSTR key, LPCSTR fallback) {
    LPCSTR value = UI_GetString(key);
    return value && *value && strcmp(value, key) ? value : fallback;
}

/* This fallback exposes only result actions the current engine can execute.
 * Full Warcraft result policy belongs to Blizzard.j + ScriptDialog. */
void UI_ShowGameResult(LPEDICT ent, DWORD result) {
    BOOL victory, single_player;

    G_GameResultDebug("hud show enter ent=%p number=%ld client=%p result=%u",
        (void *)ent, ent ? (long)ent->s.number : -1L,
        ent ? (void *)ent->client : NULL, (unsigned)result);
    if (!ent || !ent->client || result > 1) {
        G_GameResultDebug("hud show abort reason=invalid_args");
        return;
    }
    victory = result == 0;
    single_player = G_IsSinglePlayer();

    if (!hud.result.GameResultDialog) {
        G_GameResultDebug("hud show abort reason=missing_GameResultDialog_FDF");
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

    UI_SetHidden(hud.result.GameResultQuitButton, false);
    UI_SetText(hud.result.GameResultQuitButtonText, "%s", GameResultString(
        single_player ? "GAMEOVER_QUIT_MISSION" : "GAMEOVER_QUIT_GAME",
        single_player ? "Quit Mission" : "Quit Game"));
    UI_SetOnClick(hud.result.GameResultQuitButton,
        UI_WINDOW_CLOSE_COMMAND_PREFIX "gameresult_quit");
    if (victory) GameResultPositionVictoryQuit();

    G_GameResultDebug("hud show write window=%08x ent=%u connected=%u victory=%u single_player=%u uiflags=0x%08x",
        (unsigned)BZ_WC3_WINDOW_RESULT, (unsigned)ent->s.number,
        (unsigned)ent->client->connected, (unsigned)victory, (unsigned)single_player,
        (unsigned)ent->client->ps.uiflags);
    UI_SetCurrentClient(ent->client);
    UI_WriteWindow(ent, hud.result.GameResultDialog, &MAKE(uiWindowDef_t,
        .id = BZ_WC3_WINDOW_RESULT,
        .class_id = BZ_WC3_WINDOW_RESULT,
        .flags = UI_WINDOW_MODAL | UI_WINDOW_UNIQUE | UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE));
    UI_SetCurrentClient(NULL);
    G_GameResultDebug("hud show write complete window=%08x ent=%u",
        (unsigned)BZ_WC3_WINDOW_RESULT, (unsigned)ent->s.number);
}

void UI_FlushPendingGameResults(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT ent;
        DWORD result;
        DWORD now;

        if (!client->jass.pending_game_result) {
            if (i < MAX_PLAYERS) game_result_last_defer_log[i] = 0;
            continue;
        }

        now = G_Time();
        if (!client->connected ||
            level.events.read < client->jass.pending_game_result_event ||
            (client->ps.client_ui_state == CLIENT_UI_CINEMATIC && !level.script_paused)) {
            if (G_GameResultDebugEnabled() && i < MAX_PLAYERS &&
                (!game_result_last_defer_log[i] || now - game_result_last_defer_log[i] >= 1000)) {
                game_result_last_defer_log[i] = now;
                G_GameResultDebug("flush defer client_index=%u player=%u pending=%u connected=%u ui=%u events=%u/%u wait_event=%u reason=%s",
                    (unsigned)i, (unsigned)client->ps.number,
                    (unsigned)client->jass.pending_game_result,
                    (unsigned)client->connected,
                    (unsigned)client->ps.client_ui_state,
                    (unsigned)level.events.read, (unsigned)level.events.write,
                    (unsigned)client->jass.pending_game_result_event,
                    !client->connected ? "disconnected" :
                    (level.events.read < client->jass.pending_game_result_event ? "event_queue" : "cinematic"));
            }
            continue;
        }

        if (client->ps.client_ui_state == CLIENT_UI_CINEMATIC && level.script_paused) {
            G_GameResultDebug("flush cinematic override client_index=%u player=%u reason=script_paused_result_dialog",
                (unsigned)i, (unsigned)client->ps.number);
        }

        result = (DWORD)client->jass.pending_game_result - 1;
        client->jass.pending_game_result = 0;
        client->jass.pending_game_result_event = 0;
        if (i < MAX_PLAYERS) game_result_last_defer_log[i] = 0;
        ent = G_GetPlayerEntityByNumber(client->ps.number);
        G_GameResultDebug("flush ready client_index=%u player=%u result=%u ent=%p ent_number=%ld",
            (unsigned)i, (unsigned)client->ps.number, (unsigned)result,
            (void *)ent, ent ? (long)ent->s.number : -1L);
        if (ent) {
            UI_ShowGameResult(ent, result);
        } else {
            G_GameResultDebug("flush drop player=%u reason=no_player_edict", (unsigned)client->ps.number);
        }
    }
}

void UI_HideGameResult(LPEDICT ent) {
    if (!ent) return;
    G_GameResultDebug("hud hide ent=%u", (unsigned)ent->s.number);
    /* svc_window result buttons close the client-owned window before forwarding
     * their server command.  Retain the legacy layer clear so upgraded clients
     * cannot keep a stale pre-window result layout around. */
    UI_ClearLayer(ent, LAYER_GAME_RESULT);
}
