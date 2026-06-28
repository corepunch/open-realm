/*
 * ui_screen.h — Screen controller interface.
 *
 * Each UI screen (main menu, single player menu, etc.) is a self-contained
 * module with lifecycle callbacks and input handlers.
 */

#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#include "ui_local.h"

typedef struct uiScreen_s {
    LPCSTR name;
    BOOL (*load)(void);
    void (*init)(void);
    void (*shutdown)(void);
    void (*refresh)(int msec);
    void (*draw)(void);
    void (*key_event)(int key, BOOL down);
    void (*update_unit_ui)(DWORD num_units, uiUnitData_t *units);  /* Phase 8: receive unit UI data */
} uiScreen_t;

/* Screen implementations */
extern uiScreen_t mainMenuScreen;
extern uiScreen_t singlePlayerMenuScreen;
extern uiScreen_t optionsMenuScreen;
extern uiScreen_t creditsMenuScreen;
extern uiScreen_t mapSelectScreen;
extern uiScreen_t lanJoinScreen;
extern uiScreen_t lanCreateScreen;
extern uiScreen_t gameSetupScreen;
extern uiScreen_t quitConfirmScreen;

LPCSTR LAN_SelectedMapPath(void);
LPCSTR LAN_SelectedMapName(void);
DWORD LAN_SelectedGameSpeed(void);

void UI_ShowMainMenu(void);
void UI_ShowSinglePlayerMenu(void);
void UI_ShowOptionsMenu(void);
void UI_ShowCreditsMenu(void);
void UI_ShowLanCreateMenu(void);
void UI_ShowLanBrowserMenu(void);
void UI_ShowGameSetupMenu(void);

void MainMenu_ShowMainPanel(void);
void MainMenu_ShowRealmSelect(void);
void MainMenu_ShowQuitConfirm(void);
void MainMenu_ShowDisconnected(void);

void OptionsMenu_ShowGameplay(void);
void OptionsMenu_ShowVideo(void);
void OptionsMenu_ShowSound(void);
void OptionsMenu_ShowKeys(void);
void OptionsMenu_Apply(void);

void SinglePlayerMenu_ShowMain(void);
void SinglePlayerMenu_ShowCampaign(void);
void SinglePlayerMenu_LaunchCampaign(LPCSTR name);
void SinglePlayerMenu_LaunchCampaignIndex(DWORD index);

void LAN_ShowCreate(void);
void LAN_ShowBrowser(void);
void LAN_RefreshMaps(void);
void LAN_SelectMapIndex(DWORD index);
void LAN_StartSelectedMap(void);
void LAN_JoinSelectedGame(void);
void LAN_ApplyPlayerName(void);

BOOL GameSetup_StartGame(void);
void GameSetup_LoadMap(LPCSTR map_path);
void GameSetup_UpdateLobbySetup(lobbyState_t const *state);
void GameSetup_AddChatMessage(LPCSTR text, BOOL own);
void GameSetup_SetSlotType(DWORD slot, DWORD value);
void GameSetup_SetSlotRace(DWORD slot, DWORD value);
void GameSetup_CycleSlotTeam(DWORD slot);
void GameSetup_CycleSlotColor(DWORD slot);

#endif /* UI_SCREEN_H */
