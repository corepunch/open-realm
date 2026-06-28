/*
 * hud_console.c — ConsoleUI backdrop, minimap, resource bar.
 *
 * Draws the static chrome of the in-game HUD: the textured console
 * frame at top/bottom, the minimap viewport rect, and the gold/lumber/
 * supply/upkeep resource bar.
 *
 * ConsoleUI and ResourceBar use FDF-defined layouts from Blizzard's
 * MPQ archives instead of hardcoded coordinates.  The minimap stays
 * manual because FT_MINIMAP has no FDF equivalent.
 */

#include "hud_local.h"
#include "../generated/console_ui.h"
#include "../generated/resource_bar.h"

static ConsoleUI_t console_ui;
static ResourceBar_t res;
static BOOL hud_console_loaded;

static void ConsoleEnsureLoaded(void) {
    if (hud_console_loaded) return;
    hud_console_loaded = true;
    ConsoleUI_Load(&console_ui);
    ResourceBar_Load(&res);
    res.ResourceBarGoldText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    res.ResourceBarLumberText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    res.ResourceBarSupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
}

void UI_WriteConsoleBackdrop(void) {
    ConsoleEnsureLoaded();
    UI_WriteFrameWithChildren(console_ui.ConsoleUI, NULL);
}

void UI_WriteMinimapFrame(void) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, 0.0070f, 0.4525f, 0.1395f, 0.1395f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteResourceBar(LONG food_used) {
    LPCSTR upkeep_text;
    COLOR32 upkeep_color;

    ConsoleEnsureLoaded();

    upkeep_text = food_used > 80 ? "Heavy Upkeep" : food_used > 50 ? "Low Upkeep" : "No Upkeep";
    upkeep_color = food_used > 80 ? MAKE(COLOR32, 255, 64, 64, 255)
                 : food_used > 50 ? MAKE(COLOR32, 255, 200, 64, 255)
                                  : MAKE(COLOR32, 96, 255, 96, 255);
    UI_SetText(res.ResourceBarUpkeepText, "%s", upkeep_text);
    res.ResourceBarUpkeepText->Font.Color = upkeep_color;

    UI_WriteFrameWithChildren(res.ResourceBarFrame, NULL);
}
