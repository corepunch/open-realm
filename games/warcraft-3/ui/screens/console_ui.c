/*
 * ui/screens/console_ui.c — In-game HUD screen controller.
 *
 * Replaces the server-authored svc_layout HUD (g_ui_stubs.c) with
 * client-side FDF-driven rendering. Loads Blizzard's ConsoleUI FDF files
 * and populates frame content from playerState_t stats and svc_unit_ui data.
 *
 * The console UI consists of:
 * - ConsoleUI.fdf — top/bottom decorative art bars
 * - ResourceBar.fdf — gold/lumber/supply/upkeep text
 * - UpperButtonBar.fdf — Quests/Menu/Allies/Chat buttons
 * - InfoPanelUnitDetail.fdf — unit selection info
 * - InfoPanelBuildingDetail.fdf — building selection info
 * - InfoPanelItemDetail.fdf — item selection info
 * - SimpleInfoPanel.fdf — simplified variants
 *
 * Procedural panels (command card, inventory, portrait, minimap) are spawned
 * programmatically but anchored to FDF-defined container frames.
 */

#include "../ui_local.h"
#include "../ui_screen.h"
#include "../generated/console_ui.h"
#include "../generated/resource_bar.h"
#include "../generated/upper_button_bar.h"
#include "../generated/info_panel_unit_detail.h"
#include "../generated/info_panel_building_detail.h"
#include "../generated/info_panel_item_detail.h"
#include "../generated/simple_info_panel.h"

/* FDF frame references */
static ConsoleUI_t console_ui;
static ResourceBar_t resource_bar;
static UpperButtonBar_t upper_button_bar;
static InfoPanelUnitDetail_t info_unit;
static InfoPanelBuilding_t info_building;
static InfoPanelItem_t info_item;
static SimpleInfoPanel_t info_simple;

/* Current unit data (updated from svc_unit_ui) */
static DWORD console_num_units = 0;
static uiUnitData_t console_units[MAX_SELECTED_ENTITIES];

/* Player state cache */
static DWORD cached_gold = 0;
static DWORD cached_lumber = 0;
static DWORD cached_food_used = 0;
static DWORD cached_food_made = 0;

/* -------------------------------------------------------------------------- */
/* FDF loading                                                                 */
/* -------------------------------------------------------------------------- */
static BOOL ConsoleUI_LoadScreen(void) {
    BOOL ok = true;
    ok = ConsoleUI_Load(&console_ui) && ok;
    ok = ResourceBar_Load(&resource_bar) && ok;
    ok = UpperButtonBar_Load(&upper_button_bar) && ok;
    ok = InfoPanelUnitDetail_Load(&info_unit) && ok;
    ok = InfoPanelBuilding_Load(&info_building) && ok;
    ok = InfoPanelItem_Load(&info_item) && ok;
    ok = SimpleInfoPanel_Load(&info_simple) && ok;

    /* Load additional FDFs needed by the HUD */
    UI_EnsureFDF("UI\\FrameDef\\UI\\InfoPanelTemplates.fdf");

    return ok;
}

/* -------------------------------------------------------------------------- */
/* Frame initialization                                                        */
/* -------------------------------------------------------------------------- */
static void ConsoleUI_InitFrames(void) {
    /* Upper button bar — wire click commands */
    if (upper_button_bar.UpperButtonBarQuestsButton) {
        UI_SetOnClick(upper_button_bar.UpperButtonBarQuestsButton, "questlog");
    }
    if (upper_button_bar.UpperButtonBarMenuButton) {
        UI_SetOnClick(upper_button_bar.UpperButtonBarMenuButton, "togglemenu");
    }
    if (upper_button_bar.UpperButtonBarAlliesButton) {
        UI_SetOnClick(upper_button_bar.UpperButtonBarAlliesButton, "alliances");
    }
    if (upper_button_bar.UpperButtonBarChatButton) {
        UI_SetOnClick(upper_button_bar.UpperButtonBarChatButton, "chat");
    }

    /* Initially hide info panels (shown when units are selected) */
    if (info_unit.InfoPanelUnitDetail) {
        UI_SetHidden(info_unit.InfoPanelUnitDetail, true);
    }
    if (info_building.InfoPanelBuildingDetail) {
        UI_SetHidden(info_building.InfoPanelBuildingDetail, true);
    }
    if (info_item.InfoPanelItemDetail) {
        UI_SetHidden(info_item.InfoPanelItemDetail, true);
    }
}

/* -------------------------------------------------------------------------- */
/* Player state update                                                         */
/* -------------------------------------------------------------------------- */
static void ConsoleUI_UpdatePlayerState(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    if (!ps) {
        return;
    }

    cached_gold = ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    cached_lumber = ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    cached_food_used = ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    cached_food_made = ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP];

    /* Update resource bar text */
    if (resource_bar.ResourceBarGoldText) {
        UI_SetText(resource_bar.ResourceBarGoldText, "%d", cached_gold);
    }
    if (resource_bar.ResourceBarLumberText) {
        UI_SetText(resource_bar.ResourceBarLumberText, "%d", cached_lumber);
    }
    if (resource_bar.ResourceBarSupplyText) {
        UI_SetText(resource_bar.ResourceBarSupplyText, "%d/%d", cached_food_used, cached_food_made);
    }
}

/* -------------------------------------------------------------------------- */
/* Unit selection update                                                       */
/* -------------------------------------------------------------------------- */
static void ConsoleUI_UpdateSelection(void) {
    if (console_num_units == 0) {
        /* No selection — hide all info panels */
        if (info_unit.InfoPanelUnitDetail) {
            UI_SetHidden(info_unit.InfoPanelUnitDetail, true);
        }
        if (info_building.InfoPanelBuildingDetail) {
            UI_SetHidden(info_building.InfoPanelBuildingDetail, true);
        }
        if (info_item.InfoPanelItemDetail) {
            UI_SetHidden(info_item.InfoPanelItemDetail, true);
        }
        return;
    }

    uiUnitData_t const *unit = &console_units[0];

    if (unit->is_building) {
        /* Building selection */
        if (info_building.InfoPanelBuildingDetail) {
            UI_SetHidden(info_building.InfoPanelBuildingDetail, false);
        }
        if (info_unit.InfoPanelUnitDetail) {
            UI_SetHidden(info_unit.InfoPanelUnitDetail, true);
        }
        if (info_item.InfoPanelItemDetail) {
            UI_SetHidden(info_item.InfoPanelItemDetail, true);
        }

        if (info_building.BuildingNameValue) {
            UI_SetText(info_building.BuildingNameValue, "%s", unit->name);
        }
        if (info_building.BuildingDescriptionValue) {
            UI_SetText(info_building.BuildingDescriptionValue, "%s", unit->class_text);
        }
        if (info_building.BuildingDefenseValue) {
            UI_SetText(info_building.BuildingDefenseValue, "%d", unit->armor);
        }
        if (info_building.BuildingSupplyValue) {
            UI_SetText(info_building.BuildingSupplyValue, "%d", unit->food_made);
        }
    } else {
        /* Unit selection */
        if (info_unit.InfoPanelUnitDetail) {
            UI_SetHidden(info_unit.InfoPanelUnitDetail, false);
        }
        if (info_building.InfoPanelBuildingDetail) {
            UI_SetHidden(info_building.InfoPanelBuildingDetail, true);
        }
        if (info_item.InfoPanelItemDetail) {
            UI_SetHidden(info_item.InfoPanelItemDetail, true);
        }

        if (info_unit.NameValue) {
            UI_SetText(info_unit.NameValue, "%s", unit->name);
        }
        if (info_unit.ClassValue) {
            UI_SetText(info_unit.ClassValue, "%s", unit->class_text);
        }
        if (info_unit.DefenseValue) {
            UI_SetText(info_unit.DefenseValue, "%d", unit->armor);
        }
        if (info_unit.AttackValue1) {
            UI_SetText(info_unit.AttackValue1, "%d - %d", unit->damage_min, unit->damage_max);
        }
        if (info_unit.SpeedValue) {
            /* Speed is not in uiUnitData_t yet — leave as template default */
        }

        /* Hero attributes */
        if (info_unit.IconValue1) {
            UI_SetText(info_unit.IconValue1, "%d", unit->hero_strength);
        }
        if (info_unit.IconValue2) {
            UI_SetText(info_unit.IconValue2, "%d", unit->hero_agility);
        }
        if (info_unit.IconValue3) {
            UI_SetText(info_unit.IconValue3, "%d", unit->hero_intelligence);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Screen lifecycle                                                            */
/* -------------------------------------------------------------------------- */
static void ConsoleUI_Init(void) {
    uiimport.Printf("ConsoleUI_Init\n");
    ConsoleUI_InitFrames();
}

static void ConsoleUI_Shutdown(void) {
    uiimport.Printf("ConsoleUI_Shutdown\n");
    console_num_units = 0;
}

static void ConsoleUI_Refresh(int msec) {
    (void)msec;
    ConsoleUI_UpdatePlayerState();
    ConsoleUI_UpdateSelection();
}

static void ConsoleUI_Draw(void) {
    /* Draw HUD frame tree */
    LPCFRAMEDEF roots[8];
    DWORD num_roots = 0;

    if (console_ui.ConsoleUI) {
        roots[num_roots++] = console_ui.ConsoleUI;
    }
    if (resource_bar.ResourceBarFrame) {
        roots[num_roots++] = resource_bar.ResourceBarFrame;
    }
    if (upper_button_bar.UpperButtonBarFrame) {
        roots[num_roots++] = upper_button_bar.UpperButtonBarFrame;
    }
    if (info_unit.InfoPanelUnitDetail && !info_unit.InfoPanelUnitDetail->hidden) {
        roots[num_roots++] = info_unit.InfoPanelUnitDetail;
    }
    if (info_building.InfoPanelBuildingDetail && !info_building.InfoPanelBuildingDetail->hidden) {
        roots[num_roots++] = info_building.InfoPanelBuildingDetail;
    }
    if (info_item.InfoPanelItemDetail && !info_item.InfoPanelItemDetail->hidden) {
        roots[num_roots++] = info_item.InfoPanelItemDetail;
    }

    if (num_roots > 0) {
        UI_DrawFrames(roots, num_roots);
    }
}

static void ConsoleUI_KeyEvent(int key, BOOL down) {
    (void)key;
    (void)down;
}

static void ConsoleUI_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    console_num_units = num_units < MAX_SELECTED_ENTITIES ? num_units : MAX_SELECTED_ENTITIES;
    if (console_num_units > 0 && units) {
        memcpy(console_units, units, console_num_units * sizeof(uiUnitData_t));
    }
}

/* -------------------------------------------------------------------------- */
/* Screen definition                                                           */
/* -------------------------------------------------------------------------- */
uiScreen_t consoleUIScreen = {
    .name = "console_ui",
    .load = ConsoleUI_LoadScreen,
    .init = ConsoleUI_Init,
    .shutdown = ConsoleUI_Shutdown,
    .refresh = ConsoleUI_Refresh,
    .draw = ConsoleUI_Draw,
    .key_event = ConsoleUI_KeyEvent,
    .update_unit_ui = ConsoleUI_UpdateUnitUI,
};
