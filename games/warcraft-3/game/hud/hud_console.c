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

void UI_LoadHudConsole(void) {
    if (hud.console.ConsoleUI) return;
    if (!ConsoleUI_Load(&hud.console)) return;
    UI_SetAllPoints(hud.console.ConsoleUI);
    ResourceBar_Load(&hud.res);
    UI_SetParent(hud.res.ResourceBarFrame, hud.console.ConsoleUI);
    UI_SetPoint(hud.res.ResourceBarFrame, FRAMEPOINT_TOPRIGHT, hud.console.ConsoleUI, FRAMEPOINT_TOPRIGHT, 0.0f, 0.0f);
    if (UpperButtonBar_Load(&hud.upper)) {
        /* UpperButtonBar.fdf is a separate authored root. Attach it to the
         * serialized ConsoleUI tree and bind the same server actions used by
         * the default F9-F12 bindings in share/config.cfg. */
        UI_SetParent(hud.upper.UpperButtonBarFrame, hud.console.ConsoleUI);
        UI_SetOnClick(hud.upper.UpperButtonBarQuestsButton, "quests");
        UI_SetOnClick(hud.upper.UpperButtonBarMenuButton, "menu");
        UI_SetOnClick(hud.upper.UpperButtonBarAlliesButton, "allies");
        UI_SetOnClick(hud.upper.UpperButtonBarChatButton, "log");
        snprintf(hud.upper_cmds[0], sizeof(hud.upper_cmds[0]), "%s", hud.upper.UpperButtonBarQuestsButton->OnClick);
        snprintf(hud.upper_cmds[1], sizeof(hud.upper_cmds[1]), "%s", hud.upper.UpperButtonBarMenuButton->OnClick);
        snprintf(hud.upper_cmds[2], sizeof(hud.upper_cmds[2]), "%s", hud.upper.UpperButtonBarAlliesButton->OnClick);
        snprintf(hud.upper_cmds[3], sizeof(hud.upper_cmds[3]), "%s", hud.upper.UpperButtonBarChatButton->OnClick);
    }
    UI_SetSize(hud.res.ResourceBarGoldText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarGoldText->Height);
    UI_SetSize(hud.res.ResourceBarLumberText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarLumberText->Height);
    UI_SetSize(hud.res.ResourceBarSupplyText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarSupplyText->Height);
    hud.res.ResourceBarGoldText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    hud.res.ResourceBarLumberText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    hud.res.ResourceBarSupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
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

    /* Keep symbolic DecorateFileNames keys in the payload; the local WC3 UI
     * resolves them for the recipient's race when the image configstring loads. */
    UI_SetCurrentClient(client);
    if (!hud.console.ConsoleUI) {
        UI_SetCurrentClient(NULL);
        return;
    }

    if (hud.upper.UpperButtonBarFrame) {
        LPFRAMEDEF buttons[] = {
            hud.upper.UpperButtonBarQuestsButton,
            hud.upper.UpperButtonBarMenuButton,
            hud.upper.UpperButtonBarAlliesButton,
            hud.upper.UpperButtonBarChatButton,
        };
        FOR_LOOP(i, 4) UI_SetOnClick(buttons[i], "%s", hud.upper_cmds[i]);
    }

    upkeep_tier = G_GetPlayerUpkeepTier(client);
    upkeep_text = upkeep_tier > 1 ? "High Upkeep" : upkeep_tier == 1 ? "Low Upkeep" : "No Upkeep";
    upkeep_color = upkeep_tier > 1 ? MAKE(COLOR32, 255, 64, 64, 255)
                 : upkeep_tier == 1 ? MAKE(COLOR32, 255, 200, 64, 255)
                                    : MAKE(COLOR32, 96, 255, 96, 255);
    UI_SetText(hud.res.ResourceBarUpkeepText, "%s", upkeep_text);
    hud.res.ResourceBarUpkeepText->Font.Color = upkeep_color;
    hud.res.ResourceBarSupplyText->Font.Color = food_used > food_cap
        ? MAKE(COLOR32, 255, 64, 64, 255)
        : COLOR32_WHITE;

    UI_WriteFrameWithChildren(hud.console.ConsoleUI, NULL);
    UI_SetCurrentClient(NULL);
}
