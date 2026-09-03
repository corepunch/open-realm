#ifndef hud_local_h
#define hud_local_h

#include "../g_local.h"
#include "common/ui_constants.h"
#include "../generated/console_ui.h"
#include "../generated/resource_bar.h"
#include "../generated/upper_button_bar.h"
#include "../generated/info_panel_unit_detail.h"
#include "../generated/info_panel_building_detail.h"
#include "../generated/simple_info_panel.h"
#include "../generated/quest_dialog.h"
#include "../generated/log_dialog.h"
#include "../generated/esc_menu_main_panel.h"
#include "../generated/alliance_dialog.h"
#include "../generated/game_result_dialog.h"
#include "../generated/cinematic_panel.h"

/* HUD font sizes */
#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8
#define HUD_TITLE_FONT_SIZE 12
#define WC3_MESSAGE_LOG_TEXT_SIZE \
    (WC3_MESSAGE_LOG_MAX_ENTRIES * (WC3_MESSAGE_LOG_ENTRY_SIZE + 4) + 1)

typedef struct {
    BOOL resolved;
    PATHSTR texture;
} infoPanelIconCache_t;

/* Process-lifetime HUD bindings. memset(&hud, 0, sizeof(hud)) on map load. */
typedef struct {
    ConsoleUI_t console;
    ResourceBar_t res;
    UpperButtonBar_t upper;
    UINAME upper_cmds[4];
    InfoPanelUnitDetail_t unit;
    InfoPanelBuildingDetail_t building;
    SimpleInfoPanel_t simple;
    FRAMEDEF bottom, attack1, attack2, armor, hero, food, gold;
    FRAMEDEF buff_label, buff_icon[MAX_UNIT_STATUSES], buff_tex[MAX_UNIT_STATUSES];
    LPFRAMEDEF attack2_icon, attack2_icon_backdrop, attack2_icon_level, attack2_icon_label, attack2_icon_value;
    infoPanelIconCache_t icon_cache[2][8][2];
    QuestDialog_t quest;
    LPFRAMEDEF quest_row, quest_item;
    LPFRAMEDEF required_rows[MAX_UI_CLASSES], optional_rows[MAX_UI_CLASSES], quest_item_rows[MAX_UI_CLASSES];
    DWORD required_row_count, optional_row_count, quest_item_row_count;
    LogDialog_t log;
    char log_text[WC3_MESSAGE_LOG_TEXT_SIZE];
    EscMenuMainPanelGame_t menu;
    AllianceDialog_t allies;
    GameResultDialog_t result;
    CinematicPanel_t cinematic;
    FRAMEDEF msg_root, msg_text;
    PATHSTR image_key[MAX_IMAGES];
    PATHSTR image_name[MAX_IMAGES];
    BOOL image_decorated[MAX_IMAGES];
    PATHSTR font_spec[MAX_FONTSTYLES];
} hud_t;

extern hud_t hud;

/* Frame-write primitives (hud_write.c) */
extern DWORD ui_next_frame_number;
extern LPGAMECLIENT ui_current_client;
extern BOOL ui_window_writing;

void UI_SetCurrentClient(LPGAMECLIENT client);
void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis);
void UI_SetFrameRect(LPUIFRAME frame, FLOAT x, FLOAT y, FLOAT w, FLOAT h);
void UI_WriteProxyFrame(LPUIFRAME frame, HANDLE data, DWORD data_size);
void UI_WriteProxyFrameToParent(LPUIFRAME frame, HANDLE data, DWORD data_size, DWORD parent);
void UI_SetFramePointRelative(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis);
void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color, uiFontJustificationH_t align);
void UI_WriteTextureFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR art);
void UI_WriteTextFrameSized(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color, uiFontJustificationH_t align, DWORD font_size);
void UI_WriteCommandTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, LPCSTR command, COLOR32 color, uiFontJustificationH_t align, DWORD font_size);
void UI_WriteBackdropFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR background, LPCSTR edge);
void UI_WriteTextAreaFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color, DWORD font_size, FLOAT inset);
void UI_WriteTooltipFrame(void);
void UI_AppendMessageText(LPSTR out, DWORD out_size, LPCSTR text);
LPCSTR UI_FormatMessageText(LPCSTR text);
LPCSTR UI_LevelStringSafe(LPCSTR text);
void UI_WriteStart(DWORD layer);
void UI_WriteEnd(LPEDICT ent);
void UI_WriteWindow(LPEDICT ent, LPCFRAMEDEF root, uiWindowDef_t const *def);
void UI_WriteWindowStart(uiWindowDef_t const *def);
void UI_WriteWindowEnd(LPEDICT ent);
DWORD UI_WindowTextOffset(LPCSTR text);
void UI_ResetFrameWriteList(void);
void UI_CenterFrame(LPFRAMEDEF frame);
DWORD UI_LiveImage(DWORD image);
DWORD UI_LiveFont(DWORD font);
void UI_ResetHud(void);
void UI_LoadHud(void);
void UI_LoadHudConsole(void);
void UI_LoadHudInfoPanel(void);
void UI_LoadHudQuests(void);
void UI_LoadHudLog(void);
void UI_LoadHudMenu(void);
void UI_LoadHudAllies(void);
void UI_LoadHudGameResult(void);
void UI_LoadHudCinematic(void);
void UI_LoadHudMessage(void);
void UI_WriteFrameValue(LPCFRAMEDEF frame, FLOAT value);
DWORD UI_GetWrittenFrameNumber(LPCFRAMEDEF frame);

/* Theme (hud_write.c) */
LPCSTR Theme_String(LPCSTR key, LPCSTR def);
LPCSTR Theme_PlayerString(LPGAMECLIENT client, LPCSTR key, LPCSTR def);
FLOAT Theme_Float(LPCSTR key, LPCSTR def);

/* Console (hud_console.c) */
void UI_WriteConsoleBackdrop(LPGAMECLIENT, LONG, LONG);
void UI_WriteMinimapFrame(void);

/* World hover (hud_hover.c) */
void UI_WriteHoverLayout(LPEDICT ent);

/* Command buttons (hud_commands.c) */
void UI_WriteCommandButton(LPCSTR code, BOOL research, DWORD level);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_FormatTooltip(LPCSTR code, LPCSTR tip, LPCSTR ubertip, FLOAT manacost, LPSTR out, DWORD out_size);
DWORD UI_ClassIdFromCode(LPCSTR code);
void UI_WriteBuildQueue(LPEDICT ent);
void UI_AddCancelButton(LPEDICT ent);
void UI_AddCommandButton(LPCSTR code);
void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level);

/* Info panel (hud_infopanel.c) */
void UI_WriteSingleInfo(LPEDICT ent);
DWORD UI_WriteBuildingQueueShell(LPEDICT ent, LPCSTR action_key);
void UI_WriteMultiselect(LPEDICT *ents, DWORD count);
void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count);
void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count);
void UI_WriteSelectedPortraitLayer(LPEDICT ent);

/* Quests (hud_quests.c) */
DWORD UI_QuestIndex(LPCQUEST quest);
void UI_ShowQuest(LPEDICT ent, LPCQUEST quest);
void UI_ShowQuests(LPEDICT ent);

/* Message log (hud_log.c) */
void UI_MessageLogAppend(LPEDICT ent, LPCSTR text);
void UI_ShowLog(LPEDICT ent);
void UI_ShowAllies(LPEDICT ent);
void UI_AlliesToggle(LPEDICT ent, DWORD target, PLAYERALLIANCE type);
void UI_AlliesToggleVictory(LPEDICT ent);
void UI_AlliesAccept(LPEDICT ent);
void UI_AlliesCancel(LPEDICT ent);
void UI_ShowMainMenu(LPEDICT ent);
void UI_ShowGameMenuEndGame(LPEDICT ent);
void UI_ShowGameMenuConfirmExit(LPEDICT ent);

/* Game result dialog (hud_game_result.c) */
void UI_ShowGameResult(LPEDICT ent, DWORD result);
void UI_HideGameResult(LPEDICT ent);

/* Cinematic / interface (hud_cinematic.c) */
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration);
void UI_ShowGameInterface(LPEDICT ent);
void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration);
void UI_ShowTransientText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration);
void UI_ClearTextMessages(LPEDICT ent);
void UI_InvalidateDialoguePresentation(LPEDICT ent);
void UI_WriteDialoguePresentation(LPEDICT ent);
void UI_WriteCinematicLayer(LPEDICT ent);
void UI_ClearLayer(LPEDICT ent, DWORD layer);

#endif /* hud_local_h */
