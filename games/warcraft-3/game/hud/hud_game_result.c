/*
 * hud_game_result.c — Victory / Defeat result dialog.
 *
 * Shows the GameResultDialog FDF overlay to the player whose game has ended.
 * Called by RemovePlayer() for both single-player and multiplayer.
 */

#include "hud_local.h"

void UI_LoadHudGameResult(void) {
    if (hud.result.GameResultDialog) return;
    GameResultDialog_Load(&hud.result);
}

/* UI_ShowGameResult — send the victory/defeat dialog to a single client.
 * No-ops gracefully when the FDF is not loaded (e.g., test environment). */
void UI_ShowGameResult(LPEDICT ent, BOOL victory) {
    if (!ent) return;
    if (!hud.result.GameResultDialog) return; /* FDF unavailable — UI_WriteLayout would crash on NULL root */
    UI_SetText(hud.result.GameResultText, "%s", victory ? "Victory!" : "Defeat!");
    UI_SetText(hud.result.GameResultContinueButtonText, "Continue");
    UI_SetOnClick(hud.result.GameResultContinueButton, "hidegameresult");
    UI_SetText(hud.result.GameResultRestartButtonText, "Restart");
    UI_SetOnClick(hud.result.GameResultRestartButton, "gameresult_restart");
    UI_SetText(hud.result.GameResultQuitButtonText, "Quit");
    UI_SetOnClick(hud.result.GameResultQuitButton, "gameresult_quit");
    UI_WriteLayout(ent, hud.result.GameResultDialog, LAYER_GAME_RESULT);
}

/* UI_HideGameResult — clear the game result layer for a client. */
void UI_HideGameResult(LPEDICT ent) {
    if (!ent) return;
    UI_ClearLayer(ent, LAYER_GAME_RESULT);
}
