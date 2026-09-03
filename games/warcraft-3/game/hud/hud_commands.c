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
    UpgradeData_t const *upgrade = class_id ? G_UpgradeData(class_id) : NULL;
    DWORD gold_cost = balance ? (DWORD)MAX(0, balance->goldCost) : 0;
    DWORD lumber_cost = balance ? (DWORD)MAX(0, balance->lumberCost) : 0;
    DWORD food_cost = balance ? (DWORD)MAX(0, balance->foodUsed) : 0;

    if (upgrade && upgrade->id == class_id && ui_current_client) {
        LONG const level_value = G_GetPlayerTechResearchedLevel(ui_current_client, class_id) + 1;
        gold_cost = (DWORD)G_UpgradeGoldCost(class_id, level_value);
        lumber_cost = (DWORD)G_UpgradeLumberCost(class_id, level_value);
        food_cost = 0;
    }
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
        gold_icon = gi.ImageIndex("ToolTipGoldIcon");
        lumber_icon = gi.ImageIndex("ToolTipLumberIcon");
        mana_icon = gi.ImageIndex("ToolTipManaIcon");
        supply_icon = gi.ImageIndex("ToolTipSupplyIcon");
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

static void UI_WriteCommandButtonNumber(FLOAT x, FLOAT y, FLOAT w, FLOAT h, DWORD number) {
    uiFrame_t frame;
    uiLabel_t label;
    char text[16];

    if (!number) {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    snprintf(text, sizeof(text), "%u", (unsigned)number);
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYRIGHT;
    label.textaligny = FONT_JUSTIFYBOTTOM;
    UI_SetFrameRect(&frame, x + 0.001f, y + 0.001f, w - 0.002f, h - 0.002f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

void UI_WriteCommandButtonFrame(gameCommandButton_t const *button) {
    uiFrame_t frame;
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    FLOAT const x = 0.6175f + (FLOAT)button->x * 0.0434f;
    FLOAT const y = 0.4660f + (FLOAT)button->y * 0.0440f;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = button->disabled ? (COLOR32){ 128, 128, 128, 255 } : COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(button->art);
    frame.stat = button->active;
    frame.value = button->cooldown;
    frame.hotkey = button->disabled ? 0 : (BYTE)button->hotkey;
    if (button->alternate_active) frame.flagsvalue |= UIFLAG_ALTERNATE_ACTIVE;
    UI_FormatTooltip(button->command, button->tooltip, button->ubertip, button->manacost, tooltip, sizeof(tooltip));
    frame.tooltip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s", button->research ? "research" : "button", button->command);
    frame.onclick = button->disabled ? NULL : onclick;
    frame.text = button->disabled || !button->alternate[0] ? NULL : button->alternate;
    UI_SetFrameRect(&frame, x, y, 0.039f, 0.039f);
    UI_WriteProxyFrame(&frame, NULL, 0);
    UI_WriteCommandButtonNumber(x, y, 0.039f, 0.039f, button->number);
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
    DWORD buildtimer_number;
    LPBYTE buffer;
    uiBuildQueue_t *buildqueue;
    uiFrame_t firstitem;
    uiFrame_t buildtimer;
    uiFrame_t list;
    BOOL const constructing = ent && ent->currentmove && ent->currentmove->think == ai_birth;
    FLOAT const active_x = 0.320546875f;
    FLOAT const active_y = 0.526875000f;
    FLOAT const active_size = 0.026718750f;
    FLOAT const waiting_x = 0.319140625f;
    FLOAT const waiting_y = 0.562734375f;
    FLOAT const waiting_size = 0.020390625f;
    FLOAT const waiting_step = 0.028125000f;

    if (!count) return;

    /* The building name, action label, queue backdrop, and progress-bar
     * geometry come from retail SimpleInfoPanel.fdf.  The runtime queue owns
     * only icon contents, timings, click targets, and the positions of the
     * repeated queue icons that Warcraft creates in code. */
    buildtimer_number = UI_WriteBuildingQueueShell(
        ent, constructing ? "CONSTRUCTING" :
             (ent && ent->build && ent->build->research.upgrade != 0 ? "RESEARCHING" : "TRAINING"));
    if (!buildtimer_number) {
        fprintf(stderr, "UI_WriteBuildQueue: SimpleInfoPanel building shell unavailable; using runtime progress fallback\n");
        memset(&buildtimer, 0, sizeof(buildtimer));
        buildtimer.flags.type = FT_SIMPLESTATUSBAR;
        buildtimer.color = COLOR32_WHITE;
        buildtimer.tex.index = gi.ImageIndex("SimpleBuildTimeIndicator");
        buildtimer.tex.index2 = gi.ImageIndex("SimpleBuildTimeIndicatorBorder");
        UI_SetFrameRect(&buildtimer, 0.371250f, 0.518125f, 0.105380f, 0.010300f);
        UI_WriteProxyFrame(&buildtimer, NULL, 0);
        buildtimer_number = buildtimer.number;
    }

    memset(&firstitem, 0, sizeof(firstitem));
    firstitem.flags.type = FT_TEXTURE;
    firstitem.color = COLOR32_WHITE;
    firstitem.tex.index = gi.ImageIndex(queue[0].art);
    UI_SetFrameRect(&firstitem, active_x, active_y, active_size, active_size);
    UI_WriteProxyFrame(&firstitem, NULL, 0);

    size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * count;
    buffer = gi.MemAlloc(size);
    memset(buffer, 0, size);
    buildqueue = (uiBuildQueue_t *)buffer;
    buildqueue->firstitem = (USHORT)firstitem.number;
    buildqueue->buildtimer = (USHORT)buildtimer_number;
    buildqueue->itemoffset = waiting_step;
    buildqueue->numitems = count;
    FOR_LOOP(i, count) {
        buildqueue->items[i].image = (USHORT)gi.ImageIndex(queue[i].art);
        buildqueue->items[i].starttime = queue[i].starttime;
        buildqueue->items[i].endtime = queue[i].endtime;
    }

    memset(&list, 0, sizeof(list));
    list.flags.type = FT_BUILDQUEUE;
    list.color = COLOR32_WHITE;
    UI_SetFrameRect(&list, waiting_x, waiting_y, waiting_size, waiting_size);
    UI_WriteProxyFrame(&list, buffer, size);
    gi.MemFree(buffer);

    /* Match the repeated icon geometry for cancellation hit targets as well as
     * drawing.  Slot 0 is the larger active item beside the progress bar; the
     * remaining slots are the smaller row along the panel bottom. */
    if (ent->build && ent->build->training) {
        FOR_LOOP(i, count) {
            uiFrame_t cancel;
            char onclick[64];
            FLOAT x, y, w, h;

            memset(&cancel, 0, sizeof(cancel));
            cancel.flags.type = FT_SIMPLEFRAME;
            snprintf(onclick, sizeof(onclick), "canceltrain %u", (unsigned)i);
            cancel.onclick = onclick;
            if (i == 0) {
                x = active_x; y = active_y; w = active_size; h = active_size;
            } else {
                x = waiting_x + (FLOAT)(i - 1) * waiting_step;
                y = waiting_y; w = waiting_size; h = waiting_size;
            }
            UI_SetFrameRect(&cancel, x, y, w, h);
            UI_WriteProxyFrame(&cancel, NULL, 0);
        }
    }
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
