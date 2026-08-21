/*
 * hud_game_result.c — Victory / Defeat result dialog.
 *
 * Shows the GameResultDialog FDF overlay to the player whose game has ended.
 * Called by RemovePlayer() for both single-player and multiplayer.
 */

#include "hud_local.h"
#include "../generated/game_result_dialog.h"

static GameResultDialog_t grd;
static BOOL game_result_loaded;

static void GameResultEnsureLoaded(void) {
    if (game_result_loaded) return;
    game_result_loaded = true;
    GameResultDialog_Load(&grd);
}

/* UI_ShowGameResult — send the victory/defeat dialog to a single client.
 * No-ops gracefully when the FDF is not loaded (e.g., test environment). */
void UI_ShowGameResult(LPEDICT ent, BOOL victory) {
    if (!ent) return;
    GameResultEnsureLoaded();
    if (!grd.GameResultDialog) return; /* FDF unavailable — UI_WriteLayout would crash on NULL root */
    UI_SetText(grd.GameResultText, "%s", victory ? "Victory!" : "Defeat!");
    UI_SetText(grd.GameResultContinueButtonText, "Continue");
    UI_SetOnClick(grd.GameResultContinueButton, "hidegameresult");
    UI_SetText(grd.GameResultRestartButtonText, "Restart");
    UI_SetOnClick(grd.GameResultRestartButton, "gameresult_restart");
    UI_SetText(grd.GameResultQuitButtonText, "Quit");
    UI_SetOnClick(grd.GameResultQuitButton, "gameresult_quit");
    UI_WriteLayout(ent, grd.GameResultDialog, LAYER_GAME_RESULT);
}

/* UI_HideGameResult — clear the game result layer for a client. */
void UI_HideGameResult(LPEDICT ent) {
    if (!ent) return;
    UI_ClearLayer(ent, LAYER_GAME_RESULT);
}
