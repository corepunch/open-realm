/*
 * hud_commands.c — Command buttons, build queue, inventory.
 *
 * Builds FT_COMMANDBUTTON / FT_BUILDQUEUE frames from the server-side
 * unit command set and training queue, then serializes them for the
 * LAYER_COMMANDBAR and LAYER_INFOPANEL layers.
 */

#include "hud_local.h"

RECT UI_CommandButtonRect(BYTE x, BYTE y) {
    return MAKE(RECT,
                COMMAND_BUTTON_CENTER_X(x) - COMMAND_BUTTON_SIZE * 0.5f,
                COMMAND_BUTTON_CENTER_Y(y) - COMMAND_BUTTON_SIZE * 0.5f,
                COMMAND_BUTTON_SIZE,
                COMMAND_BUTTON_SIZE);
}

RECT UI_InventoryButtonRect(BYTE slot) {
    return MAKE(RECT,
                INVENTORY_BUTTON_CENTER_X(slot % 2) - INVENTORY_BUTTON_SIZE * 0.5f,
                INVENTORY_BUTTON_CENTER_Y(slot / 2) - INVENTORY_BUTTON_SIZE * 0.5f,
                INVENTORY_BUTTON_SIZE,
                INVENTORY_BUTTON_SIZE);
}

DWORD UI_ClassIdFromCode(LPCSTR code) {
    DWORD class_id = 0;

    if (IS_FOURCC(code)) {
        memcpy(&class_id, code, sizeof(class_id));
    }
    return class_id;
}

void UI_FormatTooltip(LPCSTR code, LPCSTR tip, LPCSTR ubertip, LPSTR out, DWORD out_size) {
    DWORD class_id = UI_ClassIdFromCode(code);
    DWORD gold_cost = class_id ? UNIT_GOLD_COST(class_id) : 0;
    DWORD lumber_cost = class_id ? UNIT_LUMBER_COST(class_id) : 0;
    DWORD food_cost = class_id ? UNIT_FOOD_USED(class_id) : 0;
    DWORD gold_icon = 0;
    DWORD lumber_icon = 0;
    DWORD supply_icon = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    snprintf(out, out_size, "%s", tip && *tip ? tip : " ");
    if (gold_cost || lumber_cost || food_cost) {
        gold_icon = gi.ImageIndex(Theme_String("ToolTipGoldIcon", "ToolTipGoldIcon"));
        lumber_icon = gi.ImageIndex(Theme_String("ToolTipLumberIcon", "ToolTipLumberIcon"));
        supply_icon = gi.ImageIndex(Theme_String("ToolTipSupplyIcon", "ToolTipSupplyIcon"));
        snprintf(out + strlen(out), out_size - strlen(out), "|n");
        if (gold_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)gold_icon, (unsigned)gold_cost);
        }
        if (lumber_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)lumber_icon, (unsigned)lumber_cost);
        }
        if (food_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)supply_icon, (unsigned)food_cost);
        }
    }
    if (ubertip && *ubertip) {
        snprintf(out + strlen(out), out_size - strlen(out), "|n%s", ubertip);
    }
}

void UI_WriteCommandButtonFrame(gameCommandButton_t const *button) {
    uiFrame_t frame;
    RECT rect;
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    rect = UI_CommandButtonRect(button->x, button->y);
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(button->art);
    frame.stat = button->active;
    frame.value = button->cooldown;
    UI_FormatTooltip(button->command, button->tooltip, button->ubertip, tooltip, sizeof(tooltip));
    frame.tooltip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s", button->research ? "research" : "button", button->command);
    frame.onclick = onclick;
    UI_SetFrameRect(&frame, rect.x, rect.y, rect.w, rect.h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteCommandButton(LPCSTR code, BOOL research, DWORD level) {
    gameCommandButton_t buttons[1];
    LPEDICT ent = G_GetMainSelectedUnit(ui_current_client);

    if (!ent || !code || !*code) {
        return;
    }
    if (!G_BuildCommandButton(ent, code, research, level, buttons)) {
        return;
    }

    UI_WriteCommandButtonFrame(buttons);
}

void UI_WriteBuildQueue(LPEDICT ent) {
    gameQueueItem_t queue[MAX_BUILD_QUEUE];
    BYTE count = G_GetBuildQueue(ent, queue, MAX_BUILD_QUEUE);
    DWORD size;
    LPBYTE buffer;
    uiBuildQueue_t *buildqueue;
    uiFrame_t backdrop;
    uiFrame_t firstitem;
    uiFrame_t buildtimer;
    uiFrame_t list;

    if (!count) {
        return;
    }

    UI_WriteTextFrame(INFO_PANEL_X, INFO_PANEL_Y, INFO_PANEL_W, 0.018f,
                      UNIT_NAME(ent->class_id) ? UNIT_NAME(ent->class_id) : GetClassName(ent->class_id),
                      COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTextFrame(BUILDQUEUE_ACTION_X, BUILDQUEUE_ACTION_Y, BUILDQUEUE_ACTION_W, BUILDQUEUE_ACTION_H,
                      ent->currentmove && ent->currentmove->think == ai_birth ? "Constructing" : "Training",
                      MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYCENTER);

    if (!ent->currentmove || ent->currentmove->think != ai_birth) {
        memset(&backdrop, 0, sizeof(backdrop));
        backdrop.flags.type = FT_TEXTURE;
        backdrop.color = COLOR32_WHITE;
        backdrop.tex.index = gi.ImageIndex("BuildQueueBackdrop");
        UI_SetFrameRect(&backdrop, BUILDQUEUE_BACKDROP_X, BUILDQUEUE_BACKDROP_Y,
                        BUILDQUEUE_BACKDROP_W, BUILDQUEUE_BACKDROP_H);
        UI_WriteProxyFrame(&backdrop, NULL, 0);
    }

    memset(&firstitem, 0, sizeof(firstitem));
    firstitem.flags.type = FT_TEXTURE;
    firstitem.color = COLOR32_WHITE;
    firstitem.tex.index = gi.ImageIndex(queue[0].art);
    UI_SetFrameRect(&firstitem, BUILDQUEUE_FIRST_X, BUILDQUEUE_FIRST_Y, BUILDQUEUE_FIRST_W, BUILDQUEUE_FIRST_H);
    UI_WriteProxyFrame(&firstitem, NULL, 0);

    memset(&buildtimer, 0, sizeof(buildtimer));
    buildtimer.flags.type = FT_SIMPLESTATUSBAR;
    buildtimer.color = MAKE(COLOR32, 160, 0, 160, 255);
    buildtimer.tex.index = gi.ImageIndex("SimpleBuildTimeIndicator");
    buildtimer.tex.index2 = gi.ImageIndex("SimpleBuildTimeIndicatorBorder");
    UI_SetFrameRect(&buildtimer, BUILDQUEUE_TIMER_X, BUILDQUEUE_TIMER_Y, BUILDQUEUE_TIMER_W, BUILDQUEUE_TIMER_H);
    UI_WriteProxyFrame(&buildtimer, NULL, 0);

    size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * count;
    buffer = gi.MemAlloc(size);
    memset(buffer, 0, size);
    buildqueue = (uiBuildQueue_t *)buffer;
    buildqueue->firstitem = (USHORT)firstitem.number;
    buildqueue->buildtimer = (USHORT)buildtimer.number;
    buildqueue->itemoffset = BUILDQUEUE_OFFSET;
    buildqueue->numitems = count;
    FOR_LOOP(i, count) {
        buildqueue->items[i].image = (USHORT)gi.ImageIndex(queue[i].art);
        buildqueue->items[i].starttime = queue[i].starttime;
        buildqueue->items[i].endtime = queue[i].endtime;
    }

    memset(&list, 0, sizeof(list));
    list.flags.type = FT_BUILDQUEUE;
    list.color = COLOR32_WHITE;
    UI_SetFrameRect(&list, BUILDQUEUE_LIST_X, BUILDQUEUE_LIST_Y, BUILDQUEUE_ITEM_W, BUILDQUEUE_ITEM_H);
    UI_WriteProxyFrame(&list, buffer, size);
    gi.MemFree(buffer);
}

void UI_AddCommandButtonExtended(LPCSTR code, BOOL research, DWORD level) {
    UI_WriteCommandButton(code, research, level);
}

void UI_AddCommandButton(LPCSTR code) {
    UI_AddCommandButtonExtended(code, false, 0);
}

void UI_AddCancelButton(LPEDICT ent) {
    UI_SetCurrentClient(ent ? ent->client : NULL);
    UI_WriteStart(LAYER_COMMANDBAR);
    UI_AddCommandButton(STR_CmdCancel);
    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
    UI_SetCurrentClient(NULL);
}
