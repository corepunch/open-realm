/*
 * hud_commands.c — Command buttons, build queue, inventory.
 *
 * Builds FT_COMMANDBUTTON / FT_BUILDQUEUE frames from the server-side
 * unit command set and training queue, then serializes them for the
 * LAYER_COMMANDBAR and LAYER_INFOPANEL layers.
 */

#include "hud_local.h"

uint32_t UI_ClassIdFromCode(cstring_t code) {
    uint32_t class_id = 0;

    if (IS_FOURCC(code)) {
        memcpy(&class_id, code, sizeof(class_id));
    }
    return class_id;
}

static void UI_FormatTooltipLevel(cstring_t code, cstring_t tip, cstring_t ubertip, float manacost, int32_t level,
                                   LPCEDICT producer, bool building_upgrade, string_t out, uint32_t out_size) {
    uint32_t class_id = UI_ClassIdFromCode(code);
    UnitBalance_t const *balance = class_id ? G_UnitBalance(class_id) : NULL;
    ItemData_t const *item = class_id ? G_ItemData(class_id) : NULL;
    UpgradeData_t const *upgrade = class_id ? G_UpgradeData(class_id) : NULL;
    uint32_t gold_cost = balance ? (uint32_t)MAX(0, balance->goldCost) : 0;
    uint32_t lumber_cost = balance ? (uint32_t)MAX(0, balance->lumberCost) : 0;
    uint32_t food_cost = balance ? (uint32_t)MAX(0, balance->foodUsed) : 0;

    /* Item command buttons use ItemData.slk costs rather than UnitBalance.
     * This is primarily consumed by neutral shops but also keeps generic item
     * command presentation data-driven for custom maps. */
    if (item && item->id == class_id) {
        gold_cost = (uint32_t)MAX(0, item->goldcost);
        lumber_cost = (uint32_t)MAX(0, item->lumbercost);
        food_cost = 0;
    }

    if (building_upgrade && producer && class_id) {
        int32_t gold = 0, lumber = 0, food = 0;
        G_GetBuildingUpgradeCosts(&(buildingUpgradeCostParams_t){
            .building = producer, .unit_id = class_id, .gold = &gold, .lumber = &lumber, .food = &food });
        gold_cost = (uint32_t)MAX(0, gold);
        lumber_cost = (uint32_t)MAX(0, lumber);
        food_cost = (uint32_t)MAX(0, food);
    } else if (upgrade && upgrade->id == class_id && ui_current_client) {
        int32_t const level_value = level > 0 ? level : G_GetPlayerTechResearchedLevel(ui_current_client, class_id) + 1;
        gold_cost = (uint32_t)G_UpgradeGoldCost(class_id, level_value);
        lumber_cost = (uint32_t)G_UpgradeLumberCost(class_id, level_value);
        food_cost = 0;
    }
    uint32_t mana_cost = (uint32_t)(manacost + 0.5f);
    uint32_t gold_icon = 0;
    uint32_t lumber_icon = 0;
    uint32_t mana_icon = 0;
    uint32_t supply_icon = 0;

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

void UI_FormatTooltip(cstring_t code, cstring_t tip, cstring_t ubertip, float manacost, string_t out, uint32_t out_size) {
    UI_FormatTooltipLevel(code, tip, ubertip, manacost, 0, NULL, false, out, out_size);
}

static void UI_FormatCommandTooltip(gameCommandButton_t const *button, string_t out, uint32_t out_size) {
    LPEDICT producer = ui_current_client ? G_GetMainSelectedUnit(ui_current_client) : NULL;
    UI_FormatTooltipLevel(button->command, button->tooltip, button->ubertip, button->manacost,
                          button->research ? (int32_t)button->level : 0, producer,
                          button->building_upgrade != 0, out, out_size);
}

static void UI_WriteCommandButtonNumber(float x, float y, float w, float h, uint32_t number) {
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

/* Autocast sparkle has no FDF frame; anchor the authored model to the command button. */
static void UI_WriteAutocastIndicator(gameCommandButton_t const *button, uint32_t parent) {
    cstring_t model;
    uiFrame_t frame = { .flags.type = FT_SPRITE, .color = COLOR32_WHITE, .text = "Stand" };
    if (!button->alternate_active || !parent) return;
    model = Theme_PlayerString(ui_current_client, "CommandButtonAutocast", NULL);
    if (!model || !*model) {
        fprintf(stderr, "WC3: missing CommandButtonAutocast skin model\n");
        return;
    }
    frame.tex.index = gi.ModelIndex(model);
    if (!frame.tex.index) {
        fprintf(stderr, "WC3: unable to register autocast indicator %s\n", model);
        return;
    }
    frame.flagsvalue |= UIFLAG_SPRITE_OVERLAY;
    UI_SetFramePoint(&frame.points.x[FPP_MIN], FPP_MIN, UI_PARENT, 0, false);
    UI_SetFramePoint(&frame.points.y[FPP_MIN], FPP_MAX, UI_PARENT, 0, true);
    UI_WriteProxyFrameToParent(&frame, NULL, 0, parent);
}

/* Disabled icons use the skin's authored DIS artwork, not a tint of the enabled icon. */
static uint32_t UI_CommandButtonImage(gameCommandButton_t const *button) {
    cstring_t prefix, base;
    PATHSTR path;

    if (!button->disabled) return gi.ImageIndex(button->art);
    prefix = Theme_PlayerString(ui_current_client, "CommandButtonDisabledArtPath", NULL);
    if (!prefix || !*prefix) {
        fprintf(stderr, "UI_CommandButtonImage: missing CommandButtonDisabledArtPath for %s\n", button->art);
        return 0;
    }
    base = strrchr(button->art, '\\');
    if (snprintf(path, sizeof(path), "%sDIS%s", prefix, base ? base + 1 : button->art) >= sizeof(path)) {
        fprintf(stderr, "UI_CommandButtonImage: disabled art path too long: %sDIS%s\n", prefix, base ? base + 1 : button->art);
        return 0;
    }
    return gi.ImageIndex(path);
}

void UI_WriteCommandButtonFrame(gameCommandButton_t const *button) {
    uiFrame_t frame;
    uiCommandButton_t state;
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    float const x = 0.6175f + (float)button->x * 0.0434f;
    float const y = 0.4660f + (float)button->y * 0.0440f;
    memset(&frame, 0, sizeof(frame));
    memset(&state, 0, sizeof(state));
    frame.flags.type = FT_COMMANDBUTTON;
    frame.color = COLOR32_WHITE;
    frame.tex.index = UI_CommandButtonImage(button);
    frame.stat = button->active;
    frame.value = button->cooldown;
    state.radialStartTime = button->cooldown_start_time;
    state.radialEndTime = button->cooldown_end_time;
    if (state.radialEndTime != state.radialStartTime) frame.flagsvalue |= UIFLAG_RADIAL_SHADE;
    frame.hotkey = button->disabled ? 0 : (uint8_t)button->hotkey;
    if (button->alternate_active) frame.flagsvalue |= UIFLAG_ALTERNATE_ACTIVE;
    UI_FormatCommandTooltip(button, tooltip, sizeof(tooltip));
    frame.tooltip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s",
             button->building_upgrade ? "upgrade" : (button->research ? "research" : "button"),
             button->command);
    frame.onclick = button->disabled ? NULL : onclick;
    frame.text = button->disabled || !button->alternate[0] ? NULL : button->alternate;
    UI_SetFrameRect(&frame, x, y, 0.039f, 0.039f);
    UI_WriteProxyFrame(&frame, &state, sizeof(state));
    UI_WriteAutocastIndicator(button, frame.number);
    UI_WriteCommandButtonNumber(x, y, 0.039f, 0.039f, button->number);
}

void UI_WriteCommandButton(cstring_t code, bool research, uint32_t level) {
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
    uint8_t count = G_GetBuildQueue(ent, queue, MAX_BUILD_QUEUE);
    uint32_t size;
    uint32_t buildtimer_number;
    uint8_t * buffer;
    uiBuildQueue_t *buildqueue;
    uiFrame_t firstitem;
    uiFrame_t buildtimer;
    uiFrame_t list;
    bool const constructing = ent && ent->currentmove && ent->currentmove->think == ai_birth;
    bool const upgrading = G_BuildingUpgradeActive(ent);
    bool const unsummoning = G_BuildingIsUnsummoning(ent);
    bool const hide_queue_slots = constructing || upgrading;
    uint8_t const visible_count = hide_queue_slots ? 1 : count;
    float const active_x = 0.320546875f;
    float const active_y = 0.526875000f;
    float const active_size = 0.026718750f;
    float const waiting_x = 0.319140625f;
    float const waiting_y = 0.562734375f;
    float const waiting_size = 0.020390625f;
    float const waiting_step = 0.028125000f;

    if (!count) return;

    /* The building name, action label, queue backdrop, and progress-bar
     * geometry come from retail SimpleInfoPanel.fdf.  The runtime queue owns
     * only icon contents, timings, click targets, and the positions of the
     * repeated queue icons that Warcraft creates in code. */
    buildtimer_number = UI_WriteBuildingQueueShell(
        ent, constructing ? "CONSTRUCTING" : upgrading ? "UPGRADING" :
             (ent && ent->build && ent->build->research.upgrade != 0 ? "RESEARCHING" : "TRAINING"),
        !hide_queue_slots);
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

    /* FT_BUILDQUEUE also owns the client-side progress update for the active
     * item. Keep emitting it while construction/upgrades hide only the
     * authored queue backdrop and repeated waiting slots. */
    size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * visible_count;
    buffer = gi.MemAlloc(size);
    memset(buffer, 0, size);
    buildqueue = (uiBuildQueue_t *)buffer;
    buildqueue->firstitem = (uint16_t)firstitem.number;
    buildqueue->buildtimer = (uint16_t)buildtimer_number;
    buildqueue->itemoffset = waiting_step;
    buildqueue->numitems = visible_count;
    FOR_LOOP(i, visible_count) {
        buildqueue->items[i].image = (uint16_t)gi.ImageIndex(queue[i].art);
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
    if (!unsummoning && (constructing || upgrading || (ent->build && ent->build->training))) {
        uint32_t const cancel_count = (constructing || upgrading) ? 1 : count;
        FOR_LOOP(i, cancel_count) {
            uiFrame_t cancel;
            char onclick[64];
            float x, y, w, h;

            memset(&cancel, 0, sizeof(cancel));
            cancel.flags.type = FT_SIMPLEFRAME;
            if (constructing || upgrading)
                snprintf(onclick, sizeof(onclick), "button %s", STR_CmdCancelBuild);
            else
                snprintf(onclick, sizeof(onclick), "canceltrain %u", (unsigned)i);
            cancel.onclick = onclick;
            if (i == 0) {
                x = active_x; y = active_y; w = active_size; h = active_size;
            } else {
                x = waiting_x + (float)(i - 1) * waiting_step;
                y = waiting_y; w = waiting_size; h = waiting_size;
            }
            UI_SetFrameRect(&cancel, x, y, w, h);
            UI_WriteProxyFrame(&cancel, NULL, 0);
        }
    }
}

void UI_AddCommandButtonExtended(cstring_t code, bool research, uint32_t level) {
    UI_WriteCommandButton(code, research, level);
}

void UI_AddCommandButton(cstring_t code) {
    UI_AddCommandButtonExtended(code, false, 0);
}

void UI_AddCancelButton(LPEDICT ent) {
    UI_SetCurrentClient(ent ? ent->client : NULL);
    UI_WriteStart(LAYER_COMMANDBAR);
    UI_AddCommandButton(STR_CmdCancel);
    UI_WriteEnd(ent);
    UI_SetCurrentClient(NULL);
}
