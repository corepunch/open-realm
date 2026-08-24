/*
 * hud_console.c — ConsoleUI backdrop, minimap, resource bar.
 *
 * Draws the static chrome of the in-game HUD: the textured console
 * frame at top/bottom, the minimap viewport rect, and the gold/lumber/
 * supply/upkeep resource bar.
 *
 * Blizzard's templates supply the skin; ConsoleHud.fdf owns their final
 * composition and the OpenWarcraft MINIMAP extension.
 */

#include "hud_local.h"
#include "../generated/console_ui.h"
#include "../generated/resource_bar.h"

static ConsoleUI_t console_ui;
static ResourceBar_t res;
static BOOL hud_console_loaded;

static void ConsoleEnsureLoaded(void) {
    if (hud_console_loaded) return;
    hud_console_loaded = ConsoleUI_Load(&console_ui) && ResourceBar_Load(&res) &&
        UI_EnsureFDF("UI\\FrameDef\\OpenWarcraft3\\ConsoleHud.fdf") &&
        ConsoleUI_Bind(&console_ui, UI_FindFrame("OpenWarcraftConsoleUI")) &&
        ResourceBar_Bind(&res, UI_FindFrame("OpenWarcraftResourceBar"));
    if (!hud_console_loaded) return;
    res.ResourceBarGoldText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    res.ResourceBarLumberText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    res.ResourceBarSupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
}

void UI_WriteConsoleBackdrop(LONG food_used) {
    LPCSTR upkeep_text;
    COLOR32 upkeep_color;

    ConsoleEnsureLoaded();
    if (!hud_console_loaded) return;

    upkeep_text = food_used > 80 ? "Heavy Upkeep" : food_used > 50 ? "Low Upkeep" : "No Upkeep";
    upkeep_color = food_used > 80 ? MAKE(COLOR32, 255, 64, 64, 255)
                 : food_used > 50 ? MAKE(COLOR32, 255, 200, 64, 255)
                                  : MAKE(COLOR32, 96, 255, 96, 255);
    UI_SetText(res.ResourceBarUpkeepText, "%s", upkeep_text);
    res.ResourceBarUpkeepText->Font.Color = upkeep_color;

    UI_WriteFrameWithChildren(console_ui.ConsoleUI, NULL);
}
