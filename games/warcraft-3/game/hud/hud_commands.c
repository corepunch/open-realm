/*
 * hud_commands.c — Command buttons, build queue, inventory.
 *
 * Builds FT_COMMANDBUTTON / FT_BUILDQUEUE frames from the server-side
 * unit command set and training queue, then serializes them for the
 * LAYER_COMMANDBAR and LAYER_INFOPANEL layers.
 */

#include "hud_local.h"

DWORD UI_ClassIdFromCode(LPCSTR code) {
    DWORD class_id = 0;

    if (IS_FOURCC(code)) {
        memcpy(&class_id, code, sizeof(class_id));
    }
    return class_id;
}

void UI_FormatTooltip(LPCSTR code, LPCSTR tip, LPCSTR ubertip, FLOAT manacost, LPSTR out, DWORD out_size) {
    DWORD class_id = UI_ClassIdFromCode(code);
    UnitBalance_t const *balance = class_id ? G_UnitBalance(class_id) : NULL;
    DWORD gold_cost = balance ? (DWORD)balance->goldCost : 0;
    DWORD lumber_cost = balance ? (DWORD)balance->lumberCost : 0;
    DWORD food_cost = balance ? (DWORD)balance->foodUsed : 0;
    DWORD mana_cost = (DWORD)(manacost + 0.5f);
    DWORD gold_icon = 0;
    DWORD lumber_icon = 0;
    DWORD mana_icon = 0;
    DWORD supply_icon = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    snprintf(out, out_size, "%s", tip && *tip ? tip : " ");
    if (gold_cost || lumber_cost || mana_cost || food_cost) {
        gold_icon = gi.ImageIndex(Theme_String("ToolTipGoldIcon", "ToolTipGoldIcon"));
        lumber_icon = gi.ImageIndex(Theme_String("ToolTipLumberIcon", "ToolTipLumberIcon"));
        mana_icon = gi.ImageIndex(Theme_String("ToolTipManaIcon", "ToolTipManaIcon"));
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
        if (mana_cost) {
            snprintf(out + strlen(out), out_size - strlen(out), "<Icon,%u> %u   ",
                     (unsigned)mana_icon, (unsigned)mana_cost);
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
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    FLOAT bx = UI_BASE_WIDTH * 0.5f + 0.2365f + (FLOAT)button->x * 0.0434f;
    FLOAT by = UI_BASE_HEIGHT - 0.1131f + (FLOAT)button->y * 0.0434f;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = button->disabled ? (COLOR32){ 128, 128, 128, 255 } : COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(button->art);
    frame.stat = button->active;
    frame.value = button->cooldown;
    frame.hotkey = button->disabled ? 0 : (BYTE)button->hotkey;
    UI_FormatTooltip(button->command, button->tooltip, button->ubertip, button->manacost, tooltip, sizeof(tooltip));
    frame.tooltip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s", button->research ? "research" : "button", button->command);
    frame.onclick = button->disabled ? NULL : onclick;
    UI_SetFrameRect(&frame, bx - 0.0195f, by - 0.0195f, 0.039f, 0.039f);
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

    UI_WriteTextFrame(0.310f, 0.486f, 0.180f, 0.018f,
                      G_UnitProfile(ent->class_id)->name ? G_UnitProfile(ent->class_id)->name : GetClassName(ent->class_id),
                      COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTextFrame(0.371250f, 0.508875f, 0.105f, 0.014f,
                      ent->currentmove && ent->currentmove->think == ai_birth ? "Constructing" : "Training",
                      MAKE(COLOR32, 252, 210, 18, 255), FONT_JUSTIFYCENTER);

    if (!ent->currentmove || ent->currentmove->think != ai_birth) {
        memset(&backdrop, 0, sizeof(backdrop));
        backdrop.flags.type = FT_TEXTURE;
        backdrop.color = COLOR32_WHITE;
        backdrop.tex.index = gi.ImageIndex("BuildQueueBackdrop");
        UI_SetFrameRect(&backdrop, 0.310f, 0.491f, 0.180f, 0.100f);
        UI_WriteProxyFrame(&backdrop, NULL, 0);
    }

    memset(&firstitem, 0, sizeof(firstitem));
    firstitem.flags.type = FT_TEXTURE;
    firstitem.color = COLOR32_WHITE;
    firstitem.tex.index = gi.ImageIndex(queue[0].art);
    UI_SetFrameRect(&firstitem, 0.320f, 0.525f, 0.028f, 0.031f);
    UI_WriteProxyFrame(&firstitem, NULL, 0);

    memset(&buildtimer, 0, sizeof(buildtimer));
    buildtimer.flags.type = FT_SIMPLESTATUSBAR;
    buildtimer.color = MAKE(COLOR32, 160, 0, 160, 255);
    buildtimer.tex.index = gi.ImageIndex("SimpleBuildTimeIndicator");
    buildtimer.tex.index2 = gi.ImageIndex("SimpleBuildTimeIndicatorBorder");
    UI_SetFrameRect(&buildtimer, 0.371250f, 0.524125f, 0.145f, 0.012f);
    UI_WriteProxyFrame(&buildtimer, NULL, 0);

    size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * count;
    buffer = gi.MemAlloc(size);
    memset(buffer, 0, size);
    buildqueue = (uiBuildQueue_t *)buffer;
    buildqueue->firstitem = (USHORT)firstitem.number;
    buildqueue->buildtimer = (USHORT)buildtimer.number;
    buildqueue->itemoffset = 0.0281f;
    buildqueue->numitems = count;
    FOR_LOOP(i, count) {
        buildqueue->items[i].image = (USHORT)gi.ImageIndex(queue[i].art);
        buildqueue->items[i].starttime = queue[i].starttime;
        buildqueue->items[i].endtime = queue[i].endtime;
    }

    memset(&list, 0, sizeof(list));
    list.flags.type = FT_BUILDQUEUE;
    list.color = COLOR32_WHITE;
    UI_SetFrameRect(&list, 0.3195f, 0.566f, 0.020f, 0.0215f);
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
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
