#ifndef hud_local_h
#define hud_local_h

#include "../g_local.h"
#include "common/ui_constants.h"

/* HUD font sizes */
#define HUD_FONT_SIZE 10
#define HUD_SMALL_FONT_SIZE 8
#define HUD_TITLE_FONT_SIZE 12

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
LPCSTR Theme_PlayerString(LPGAMECLIENT client, LPCSTR key, LPCSTR def);
FLOAT Theme_Float(LPCSTR key, LPCSTR def);

/* Console (hud_console.c) */
void UI_WriteConsoleBackdrop(LONG);
void UI_WriteMinimapFrame(void);

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
void UI_WriteMultiselect(LPEDICT *ents, DWORD count);
void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count);
void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count);

/* Quests (hud_quests.c) */
DWORD UI_QuestIndex(LPCQUEST quest);
void UI_ShowQuest(LPEDICT ent, LPCQUEST quest);
void UI_ShowQuests(LPEDICT ent);
void UI_HideQuests(LPEDICT ent);

/* Game result dialog (hud_game_result.c) */
void UI_ShowGameResult(LPEDICT ent, BOOL victory);
void UI_HideGameResult(LPEDICT ent);

/* Cinematic / interface (hud_cinematic.c) */
void UI_ShowInterface(LPEDICT ent, BOOL flag, FLOAT duration);
void UI_ShowGameInterface(LPEDICT ent);
void UI_ShowText(LPEDICT ent, LPCVECTOR2 pos, LPCSTR text, FLOAT duration);
void UI_WriteCinematicLayer(LPEDICT ent);
void UI_ClearLayer(LPEDICT ent, DWORD layer);

#endif /* hud_local_h */
