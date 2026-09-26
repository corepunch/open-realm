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
#include "../generated/esc_menu_save_game_panel.h"
#include "../generated/map_list_box.h"
#include "../generated/chat_dialog.h"
#include "../generated/alliance_dialog.h"
#include "../generated/game_result_dialog.h"
#include "../generated/cinematic_panel.h"
#include "../generated/loading_screen.h"
#include "../generated/timer_dialog.h"
#include "../generated/leader_board.h"

/* HUD font sizes */
#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8
#define HUD_TITLE_FONT_SIZE 12

/* Persistent top-edge HUD controls share these authored screen offsets. */
#define HUD_HERO_SHORTCUT_EDGE_X 0.0060f
#define HUD_HERO_SHORTCUT_TOP_Y  0.0350f
#define BZ_WC3_HUD_TIMER_DIALOG_STACK_GAP 0.0040f // normalized UI units; separates a leaderboard from a visible timer
#define WC3_MESSAGE_LOG_TEXT_SIZE \
    (WC3_MESSAGE_LOG_MAX_ENTRIES * (WC3_MESSAGE_LOG_ENTRY_SIZE + 4) + 1)
#define HUD_CONSOLE_WIDE_MAX 8 // frames; retail ConsoleUI.fdf authors four widescreen tiles; headroom for custom skins
#define HUD_DEFERRED_IMAGES 8 // symbolic skin keys registered at write time, not parse time; class-gated chrome only
#define HUD_DEFERRED_IMAGE_BASE MAX_IMAGES // handle base; deferred handles never alias a live CS_IMAGES slot

typedef struct {
    bool resolved;
    PATHSTR texture;
} infoPanelIconCache_t;

typedef struct {
    frameDef_t const *frame;
    frameDef_t const *parent;
    cstring_t measure_text;
    uint32_t font;
    float padding_x;
    float min_width;
} uiSizeToTextParams_t;

/* Process-lifetime HUD bindings. memset(&hud, 0, sizeof(hud)) on map load. */
typedef struct {
    LoadingScreen_t loading;
    ConsoleUI_t console;
    frameDef_t *console_wide[HUD_CONSOLE_WIDE_MAX]; /* ConsoleTexture05/06 children; written for wide clients only */
    uint32_t console_wide_count;
    PATHSTR deferred_key[HUD_DEFERRED_IMAGES]; /* symbolic keys behind HUD_DEFERRED_IMAGE_BASE handles */
    ResourceBar_t res;
    UpperButtonBar_t upper;
    UINAME upper_cmds[4];
    InfoPanelUnitDetail_t unit;
    InfoPanelBuildingDetail_t building;
    SimpleInfoPanel_t simple;
    FRAMEDEF bottom, attack1, attack2, armor, hero, food, gold;
    FRAMEDEF buff_label, buff_icon[MAX_UNIT_STATUSES], buff_tex[MAX_UNIT_STATUSES];
    frameDef_t *attack2_icon, *attack2_icon_backdrop, *attack2_icon_level, *attack2_icon_label, *attack2_icon_value;
    infoPanelIconCache_t icon_cache[2][8][2];
    QuestDialog_t quest;
    frameDef_t *quest_row, *quest_item;
    frameDef_t *required_rows[MAX_UI_CLASSES], *optional_rows[MAX_UI_CLASSES], *quest_item_rows[MAX_UI_CLASSES];
    uint32_t required_row_count, optional_row_count, quest_item_row_count;
    LogDialog_t log;
    char log_text[WC3_MESSAGE_LOG_TEXT_SIZE];
    EscMenuMainPanelGame_t menu;
    EscMenuSaveGamePanel_t save_menu;
    MapListBox_t save_list_art;
    FRAMEDEF save_list;
    AllianceDialog_t allies;
    GameResultDialog_t result;
    CinematicPanel_t cinematic;
    TimerDialog_t timer_dialog;
    LeaderBoard_t leaderboard;
    FRAMEDEF leaderboard_anchor;
    color32_t leaderboard_default_title_color;
    color32_t leaderboard_default_item_color;
    FRAMEDEF timer_dialog_anchor;
    char timer_dialog_default_title[MAX_TRIGSTR_LENGTH];
    color32_t timer_dialog_default_title_color;
    color32_t timer_dialog_default_time_color;
    FRAMEDEF msg_root, msg_text;
    PATHSTR image_key[MAX_IMAGES];
    PATHSTR image_name[MAX_IMAGES];
    bool image_decorated[MAX_IMAGES];
    PATHSTR font_spec[MAX_FONTSTYLES];
} hud_t;

extern hud_t hud;

/* Frame-write primitives (hud_write.c) */
extern uint32_t ui_next_frame_number;
extern gameClient_t *ui_current_client;
extern bool ui_window_writing;

void UI_SetCurrentClient(gameClient_t *client);
void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, uint32_t relative, float offset, bool y_axis);
void UI_SetFrameRect(uiFrame_t *frame, float x, float y, float w, float h);
void UI_WriteProxyFrame(uiFrame_t *frame, handle_t data, uint32_t data_size);
void UI_WriteProxyFrameToParent(uiFrame_t *frame, handle_t data, uint32_t data_size, uint32_t parent);
void UI_SetFramePointRelative(uiFramePoint_t *point, uiFramePointPos_t target, uint32_t relative, float offset, bool y_axis);
void UI_WriteTextFrame(float x, float y, float w, float h, cstring_t text, color32_t color, uiFontJustificationH_t align);
void UI_WriteTextureFrame(float x, float y, float w, float h, cstring_t art);
void UI_WriteTextFrameSized(float x, float y, float w, float h, cstring_t text, color32_t color, uiFontJustificationH_t align, uint32_t font_size);
void UI_WriteCommandTextFrame(float x, float y, float w, float h, cstring_t text, cstring_t command, color32_t color, uiFontJustificationH_t align, uint32_t font_size);
void UI_WriteBackdropFrame(float x, float y, float w, float h, cstring_t background, cstring_t edge);
void UI_WriteTextAreaFrame(float x, float y, float w, float h, cstring_t text, color32_t color, uint32_t font_size, float inset);
void UI_WriteTooltipFrame(void);
void UI_AppendMessageText(string_t out, uint32_t out_size, cstring_t text);
cstring_t UI_FormatMessageText(cstring_t text);
cstring_t UI_LevelStringSafe(cstring_t text);
void UI_WriteStart(uint32_t layer);
void UI_WriteEnd(edict_t *ent);
void UI_WriteWindow(edict_t *ent, frameDef_t const *root, uiWindowDef_t const *def);
void UI_WriteWindowStart(uiWindowDef_t const *def);
void UI_WriteWindowEnd(edict_t *ent);
uint32_t UI_WindowTextOffset(cstring_t text);
void UI_ResetFrameWriteList(void);
void UI_CenterFrame(frameDef_t *frame);
uint32_t UI_LiveImage(uint32_t image);
cstring_t UI_ImageKey(uint32_t image);
bool UI_IsWideChromeKey(cstring_t key);
cstring_t UI_ThemeImagePath(cstring_t key);
uint32_t UI_LiveFont(uint32_t font);
void UI_ResetHud(void);
void UI_LoadHud(void);
void UI_LoadHudLoading(void);
void UI_WriteLoadingLayout(edict_t *ent, mapInfo_t const *info);
void UI_LoadHudConsole(void);
void UI_LoadHudInfoPanel(void);
void UI_LoadHudQuests(void);
void UI_LoadHudLog(void);
void UI_LoadHudMenu(void);
void UI_LoadHudAllies(void);
void UI_LoadHudGameResult(void);
void UI_LoadHudCinematic(void);
void UI_LoadHudMessage(void);
void UI_LoadHudTimerDialogs(void);
void UI_WriteTimerDialogs(edict_t *ent);
float UI_TimerDialogLeaderboardOffset(uint32_t client_num);
void UI_LoadHudLeaderboards(void);
void UI_WriteLeaderboard(edict_t *ent);
void UI_WriteFrameValue(frameDef_t const *frame, float value);
void UI_WriteFrameWithChildrenSizedToText(uiSizeToTextParams_t const *params);
uint32_t UI_GetWrittenFrameNumber(frameDef_t const *frame);

/* Theme (hud_write.c) */
cstring_t Theme_String(cstring_t key, cstring_t def);
cstring_t Theme_PlayerString(gameClient_t *client, cstring_t key, cstring_t def);
float Theme_Float(cstring_t key, cstring_t def);

/* Console (hud_console.c) */
void UI_WriteConsoleBackdrop(gameClient_t *, int32_t, int32_t);
void UI_WriteMinimapFrame(void);

/* World hover (hud_hover.c) */
void UI_WriteHoverLayout(edict_t *ent);

/* Command buttons (hud_commands.c) */
void UI_WriteCommandButton(cstring_t code, bool research, uint32_t level);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_FormatTooltip(cstring_t code, cstring_t tip, cstring_t ubertip, float manacost, string_t out, uint32_t out_size);
uint32_t UI_ClassIdFromCode(cstring_t code);
void UI_WriteBuildQueue(edict_t *ent);
void UI_AddCancelButton(edict_t *ent);
void UI_AddCommandButton(cstring_t code);
void UI_AddCommandButtonExtended(cstring_t code, bool research, uint32_t level);

/* Info panel (hud_infopanel.c) */
void UI_WriteSingleInfo(edict_t *ent, gameClient_t *viewer);
uint32_t UI_WriteBuildingQueueShell(edict_t *ent, cstring_t action_key, bool show_queue_slots);
void UI_WriteMultiselect(edict_t * *ents, uint32_t count, gameClient_t *viewer);
void UI_SeedInfoPanelCache(edict_t *ent, edict_t * *selected, uint32_t count);
void UI_SendInfoPanel(edict_t *ent, edict_t * *selected, uint32_t count);
void UI_WriteSelectedPortraitLayer(edict_t *ent);
#ifdef BZ_TESTS
bool UI_TestUsesBuildingQueuePanel(gameClient_t *viewer, edict_t *unit);
#endif

/* Quests (hud_quests.c) */
uint32_t UI_QuestIndex(quest_t const *quest);
void UI_ShowQuest(edict_t *ent, quest_t const *quest);
void UI_ShowQuests(edict_t *ent);

/* Message log (hud_log.c) */
void UI_MessageLogAppend(edict_t *ent, cstring_t text);
void UI_ShowLog(edict_t *ent);
void UI_ShowAllies(edict_t *ent);
void UI_AlliesToggle(edict_t *ent, uint32_t target, PLAYERALLIANCE type);
void UI_AlliesToggleVictory(edict_t *ent);
void UI_AlliesAccept(edict_t *ent);
void UI_AlliesCancel(edict_t *ent);
void UI_ShowMainMenu(edict_t *ent);
void UI_ShowGameMenuEndGame(edict_t *ent);
void UI_ShowGameMenuConfirmExit(edict_t *ent);
void UI_ShowGameMenuSave(edict_t *ent);
void UI_ShowGameMenuLoad(edict_t *ent);

/* Game result dialog (hud_game_result.c) */
void UI_ShowGameResult(edict_t *ent, uint32_t result);
void UI_HideGameResult(edict_t *ent);

/* Cinematic / interface (hud_cinematic.c) */
void UI_ShowInterface(edict_t *ent, bool flag, float duration);
void UI_ShowGameInterface(edict_t *ent);
void UI_ShowText(edict_t *ent, vector2_t const *pos, cstring_t text, float duration);
void UI_ShowTransientText(edict_t *ent, vector2_t const *pos, cstring_t text, float duration);
void UI_RecordTransmissionMessage(edict_t *ent);
void UI_ClearTextMessages(edict_t *ent);
void UI_InvalidateDialoguePresentation(edict_t *ent);
void UI_WriteDialoguePresentation(edict_t *ent);
void UI_WriteCinematicLayer(edict_t *ent);
void UI_ClearLayer(edict_t *ent, uint32_t layer);

#endif /* hud_local_h */
