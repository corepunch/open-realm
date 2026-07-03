#ifndef hud_local_h
#define hud_local_h

#include "../g_local.h"

/* HUD font sizes */
#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8
#define HUD_TITLE_FONT_SIZE 12

/* Layout constants */
#include "common/ui_constants.h"

#define INFO_PANEL_X 0.310f
#define INFO_PANEL_Y 0.486f
#define INFO_PANEL_W 0.180f
#define INFO_PANEL_H 0.105f
#define BUILDQUEUE_OFFSET 0.0281f
#define BUILDQUEUE_BACKDROP_X INFO_PANEL_X
#define BUILDQUEUE_BACKDROP_Y (INFO_PANEL_Y + INFO_PANEL_H - 0.1000f)
#define BUILDQUEUE_BACKDROP_W INFO_PANEL_W
#define BUILDQUEUE_BACKDROP_H 0.1000f
#define BUILDQUEUE_FIRST_X (INFO_PANEL_X + 0.0100f)
#define BUILDQUEUE_FIRST_Y (INFO_PANEL_Y + 0.0390f)
#define BUILDQUEUE_FIRST_W 0.0280f
#define BUILDQUEUE_FIRST_H 0.0310f
#define BUILDQUEUE_LIST_X (INFO_PANEL_X + 0.0095f)
#define BUILDQUEUE_LIST_Y (INFO_PANEL_Y + 0.0800f)
#define BUILDQUEUE_ITEM_W 0.0200f
#define BUILDQUEUE_ITEM_H 0.0215f
#define BUILDQUEUE_TIMER_X (INFO_PANEL_X + 0.061250f)
#define BUILDQUEUE_TIMER_Y (INFO_PANEL_Y + 0.038125f)
#define BUILDQUEUE_TIMER_W 0.1450f
#define BUILDQUEUE_TIMER_H 0.0120f
#define BUILDQUEUE_ACTION_X (INFO_PANEL_X + 0.061250f)
#define BUILDQUEUE_ACTION_Y (INFO_PANEL_Y + 0.022875f)
#define BUILDQUEUE_ACTION_W 0.1050f
#define BUILDQUEUE_ACTION_H 0.0140f
#define PORTRAIT_X 0.215f
#define PORTRAIT_Y 0.490f
#define PORTRAIT_SIZE 0.080f
#define COMMAND_BUTTON_SIZE 0.039f
#define COMMAND_BUTTON_CENTER_X(x) (UI_BASE_WIDTH * 0.5f + 0.2365f + (FLOAT)(x) * 0.0434f)
#define COMMAND_BUTTON_CENTER_Y(y) (UI_BASE_HEIGHT - (0.1131f - (FLOAT)(y) * 0.0434f))
#define INVENTORY_BUTTON_SIZE 0.033f
#define INVENTORY_BUTTON_CENTER_X(x) (UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(x) * 0.0394f)
#define INVENTORY_BUTTON_CENTER_Y(y) (UI_BASE_HEIGHT - (0.0971f - (FLOAT)(y) * 0.0384f))
#define MULTISELECT_X 0.314f
#define MULTISELECT_Y 0.500f
#define MULTISELECT_SIZE 0.025f
#define TOOLTIP_X 0.5800f
#define TOOLTIP_Y 0.3400f
#define TOOLTIP_W 0.2200f
#define TOOLTIP_H 0.1000f
#define QUEST_X 0.1500f
#define QUEST_Y 0.0700f
#define QUEST_W 0.5000f
#define QUEST_H 0.4050f
#define QUEST_LIST_X (QUEST_X + 0.0200f)
#define QUEST_LIST_Y (QUEST_Y + 0.0950f)
#define QUEST_LIST_W 0.1800f
#define QUEST_ROW_H 0.0180f
#define QUEST_DETAIL_X (QUEST_X + 0.2250f)
#define QUEST_DETAIL_Y (QUEST_Y + 0.0950f)
#define QUEST_DETAIL_W 0.2500f
#define QUEST_MESSAGE_X 0.0500f
#define QUEST_MESSAGE_Y 0.3000f
#define QUEST_MESSAGE_W 0.3000f
#define QUEST_MESSAGE_H 0.1450f

/* Frame-write primitives (hud_write.c) */
extern DWORD ui_next_frame_number;
extern LPGAMECLIENT ui_current_client;

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
void UI_ResetFrameWriteList(void);

/* Theme (hud_write.c) */
LPCSTR Theme_String(LPCSTR key, LPCSTR def);
FLOAT Theme_Float(LPCSTR key, LPCSTR def);

/* Console (hud_console.c) */
void UI_WriteConsoleBackdrop(LONG);
void UI_WriteMinimapFrame(void);

/* Command buttons (hud_commands.c) */
RECT UI_CommandButtonRect(BYTE x, BYTE y);
RECT UI_InventoryButtonRect(BYTE slot);
void UI_WriteCommandButton(LPCSTR code, BOOL research, DWORD level);
void UI_WriteCommandButtonFrame(gameCommandButton_t const *button);
void UI_FormatTooltip(LPCSTR code, LPCSTR tip, LPCSTR ubertip, LPSTR out, DWORD out_size);
DWORD UI_ClassIdFromCode(LPCSTR code);
void UI_WriteBuildQueue(LPEDICT ent);
void UI_AddCancelButton(LPEDICT ent);
void UI_AddCommandButton(LPCSTR code);
void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level);

/* Info panel (hud_infopanel.c) */
void UI_WriteSingleInfo(LPEDICT ent);
void UI_WriteMultiselect(LPEDICT *ents, DWORD count);
void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count);
void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count);

/* Quests (hud_quests.c) */
DWORD UI_QuestIndex(LPCQUEST quest);
void UI_ShowQuest(LPEDICT ent, LPCQUEST quest);
void UI_ShowQuests(LPEDICT ent);
void UI_HideQuests(LPEDICT ent);

/* Cinematic / interface (hud_cinematic.c) */
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration);
void UI_ShowGameInterface(LPEDICT ent);
void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration);
void UI_WriteCinematicLayer(LPEDICT ent);
void UI_ClearLayer(LPEDICT ent, DWORD layer);

#endif /* hud_local_h */
