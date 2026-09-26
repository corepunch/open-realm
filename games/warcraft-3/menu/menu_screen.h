/*
 * menu_screen.h — Screen controller interface.
 *
 * Each UI screen (main menu, single player menu, etc.) is a self-contained
 * module with lifecycle callbacks and input handlers.
 */

#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#include "menu_local.h"

typedef struct uiScreen_s {
    cstring_t name;
    glueDest_t glue;
    cstring_t const *left; // Native FDF subtree names; remaining frames belong to the right side.
    bool (*load)(void);
    void (*init)(void);
    void (*shutdown)(void);
    void (*refresh)(int msec);
    void (*draw)(void);
    void (*key_event)(int key, bool down);
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

cstring_t LAN_SelectedMapPath(void);
cstring_t LAN_SelectedMapName(void);
uint32_t LAN_SelectedGameSpeed(void);

void M_ShowMainMenu(void);
void M_ShowSinglePlayerMenu(void);
void M_ShowOptionsMenu(void);
void M_ShowCreditsMenu(void);
void M_ShowLanCreateMenu(void);
void M_ShowLanBrowserMenu(void);
void M_ShowGameSetupMenu(void);

void MainMenu_ShowMainPanel(void);
void MainMenu_ShowRealmSelect(void);
void MainMenu_ShowQuitConfirm(void);
void MainMenu_ShowDisconnected(void);
void MainMenu_BeginEditionSwitch(void);

void OptionsMenu_ShowGameplay(void);
void OptionsMenu_ShowVideo(void);
void OptionsMenu_ShowSound(void);
void OptionsMenu_ShowKeys(void);
void OptionsMenu_Apply(void);

void SinglePlayerMenu_ShowMain(void);
void SinglePlayerMenu_ShowCampaign(void);
void SinglePlayerMenu_BackCampaign(void);
void SinglePlayerMenu_LaunchCampaign(cstring_t name);
void SinglePlayerMenu_LaunchCampaignIndex(uint32_t index);
void SinglePlayerMenu_LaunchMissionIndex(uint32_t index);
void SinglePlayerMenu_SetDifficulty(uint32_t difficulty);

void LAN_ShowCreate(void);
void LAN_ShowSinglePlayerCreate(void);
void LAN_ShowBrowser(void);
void LAN_RefreshMaps(void);
void LAN_SelectMapIndex(uint32_t index);
void LAN_StartSelectedMap(void);
void LAN_JoinSelectedGame(void);
void LAN_ApplyPlayerName(void);
bool LAN_IsSinglePlayerCreate(void);

bool GameSetup_StartGame(void);
void GameSetup_LoadMap(cstring_t map_path);
void GameSetup_UpdateLobbySetup(lobbyState_t const *state);
void GameSetup_AddChatMessage(cstring_t text, bool own);
void GameSetup_SetSlotType(uint32_t slot, uint32_t value);
void GameSetup_SetSlotRace(uint32_t slot, uint32_t value);
void GameSetup_CycleSlotTeam(uint32_t slot);
void GameSetup_CycleSlotColor(uint32_t slot);

#endif /* UI_SCREEN_H */
