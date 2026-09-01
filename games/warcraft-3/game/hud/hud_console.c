/*
 * hud_console.c — ConsoleUI backdrop, minimap, resource bar.
 *
 * Draws the static chrome of the in-game HUD: the textured console
 * frame at top/bottom, the minimap viewport rect, and the gold/lumber/
 * supply/upkeep resource bar.
 *
 * Blizzard's templates supply the skin; C code handles the final
 * composition and the minimap extension.
 */

#include "hud_local.h"
#include "../generated/console_ui.h"
#include "../generated/resource_bar.h"
#include "../generated/upper_button_bar.h"

static ConsoleUI_t console_ui;
static ResourceBar_t res;
static UpperButtonBar_t upper;
static BOOL hud_console_loaded;

static void ConsoleEnsureLoaded(void) {
    if (hud_console_loaded) return;
    hud_console_loaded = true;
    ConsoleUI_Load(&console_ui);
    UI_SetAllPoints(console_ui.ConsoleUI);
    ResourceBar_Load(&res);
    UI_SetParent(res.ResourceBarFrame, console_ui.ConsoleUI);
    UI_SetPoint(res.ResourceBarFrame, FRAMEPOINT_TOPRIGHT, console_ui.ConsoleUI, FRAMEPOINT_TOPRIGHT, 0.0f, 0.0f);
    if (UpperButtonBar_Load(&upper)) {
        /* UpperButtonBar.fdf is a separate authored root.  Attach it to the
         * serialized ConsoleUI tree and bind the mouse actions here; keyboard
         * mapping is intentionally handled elsewhere. */
        UI_SetParent(upper.UpperButtonBarFrame, console_ui.ConsoleUI);
        UI_SetOnClick(upper.UpperButtonBarQuestsButton, "quests");
        UI_SetOnClick(upper.UpperButtonBarChatButton, "log");
    }
    res.ResourceBarGoldText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    res.ResourceBarLumberText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    res.ResourceBarSupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
}

void UI_WriteMinimapFrame(void) {
    uiFrame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;
    frame.tex.coord[1] = 0xff;
    frame.tex.coord[3] = 0xff;
    UI_SetFrameRect(&frame, 0.0070f, 0.4525f, 0.1395f, 0.1395f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteConsoleBackdrop(LPGAMECLIENT client, LONG food_used, LONG food_cap) {
    DWORD upkeep_tier;
    LPCSTR upkeep_text;
    COLOR32 upkeep_color;

    ConsoleEnsureLoaded();
    if (!hud_console_loaded) return;

    upkeep_tier = G_GetPlayerUpkeepTier(client);
    upkeep_text = upkeep_tier > 1 ? "High Upkeep" : upkeep_tier == 1 ? "Low Upkeep" : "No Upkeep";
    upkeep_color = upkeep_tier > 1 ? MAKE(COLOR32, 255, 64, 64, 255)
                 : upkeep_tier == 1 ? MAKE(COLOR32, 255, 200, 64, 255)
                                    : MAKE(COLOR32, 96, 255, 96, 255);
    UI_SetText(res.ResourceBarUpkeepText, "%s", upkeep_text);
    res.ResourceBarUpkeepText->Font.Color = upkeep_color;
    res.ResourceBarSupplyText->Font.Color = food_used > food_cap
        ? MAKE(COLOR32, 255, 64, 64, 255)
        : COLOR32_WHITE;

    UI_WriteFrameWithChildren(console_ui.ConsoleUI, NULL);
}
