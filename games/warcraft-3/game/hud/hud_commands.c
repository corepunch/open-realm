/*
 * hud_commands.c — Command buttons, build queue, inventory.
 *
 * Builds FT_COMMANDBUTTON / FT_BUILDQUEUE frames from the server-side
 * unit command set and training queue, then serializes them for the
 * LAYER_COMMANDBAR and LAYER_INFOPANEL layers.
 */

#include "hud_local.h"

static LPFRAMEDEF cmd_frames[12], inv_frames[MAX_INVENTORY];

/* Grid instances retain stable frame numbers while FDF owns origin, size, stride, and columns. */
static LPFRAMEDEF UI_GridFrame(LPFRAMEDEF *items, DWORD count, LPCSTR name, DWORD index) {
    if (index >= count) return NULL;
    if (!items[index]) items[index] = UI_CloneGridItem(UI_HudFrame(name), NULL, index);
    return items[index];
}

LPFRAMEDEF UI_InventoryFrame(BYTE slot) {
    return UI_GridFrame(inv_frames, MAX_INVENTORY, "OpenWarcraftInventoryButton", slot);
}

DWORD UI_ClassIdFromCode(LPCSTR code) {
    DWORD class_id = 0;

    if (IS_FOURCC(code)) {
        memcpy(&class_id, code, sizeof(class_id));
    }
    return class_id;
}

void UI_FormatTooltip(LPCSTR code, LPCSTR tip, LPCSTR ubertip, FLOAT manacost, LPSTR out, DWORD out_size) {
    DWORD class_id = UI_ClassIdFromCode(code);
    DWORD gold_cost = class_id ? UNIT_GOLD_COST(class_id) : 0;
    DWORD lumber_cost = class_id ? UNIT_LUMBER_COST(class_id) : 0;
    DWORD food_cost = class_id ? UNIT_FOOD_USED(class_id) : 0;
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
    LPFRAMEDEF frame;
    char onclick[320];
    char tooltip[1024];

    if (!button) {
        return;
    }
    frame = UI_GridFrame(cmd_frames, 12, "OpenWarcraftCommandButton", button->y * 4 + button->x);
    if (!frame) return;
    frame->Texture.Image = gi.ImageIndex(button->art);
    frame->Stat = button->active;
    frame->Value = button->cooldown;
    frame->Hotkey = (BYTE)button->hotkey;
    UI_FormatTooltip(button->command, button->tooltip, button->ubertip, button->manacost, tooltip, sizeof(tooltip));
    frame->Tip = tooltip;
    snprintf(onclick, sizeof(onclick), "%s %s", button->research ? "research" : "button", button->command);
    snprintf(frame->OnClick, sizeof(frame->OnClick), "%s", onclick);
    UI_WriteFrame(frame);
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
    LPFRAMEDEF root, panel, name, action, backdrop, first, timer, list;

    if (!count) return;
    root = UI_HudFrame("OpenWarcraftInfoPanel");
    panel = UI_HudFrame("OpenWarcraftBuildingDetail");
    name = UI_HudFrame("OpenWarcraftBuildingName");
    action = UI_HudFrame("OpenWarcraftBuildingAction");
    backdrop = UI_HudFrame("OpenWarcraftBuildQueueBackdrop");
    first = UI_HudFrame("OpenWarcraftBuildQueueFirst");
    timer = UI_HudFrame("OpenWarcraftBuildTimeIndicator");
    list = UI_HudFrame("OpenWarcraftBuildQueue");
    if (!root || !panel || !name || !action || !backdrop || !first || !timer || !list) return;

    UI_SetText(name, "%s", UNIT_NAME(ent->class_id) ? UNIT_NAME(ent->class_id) : GetClassName(ent->class_id));
    UI_SetText(action, "%s", ent->currentmove && ent->currentmove->think == ai_birth ? "Constructing" : "Training");
    UI_SetHidden(backdrop, ent->currentmove && ent->currentmove->think == ai_birth);
    first->Texture.Image = gi.ImageIndex(queue[0].art);
    timer->Color = MAKE(COLOR32, 160, 0, 160, 255);
    timer->Texture.Image = gi.ImageIndex("SimpleBuildTimeIndicator");
    timer->Texture.Image2 = gi.ImageIndex("SimpleBuildTimeIndicatorBorder");
    list->BuildQueue.NumQueue = count;
    FOR_LOOP(i, count) {
        list->BuildQueue.Queue[i].image = (USHORT)gi.ImageIndex(queue[i].art);
        list->BuildQueue.Queue[i].starttime = queue[i].starttime;
        list->BuildQueue.Queue[i].endtime = queue[i].endtime;
    }
    UI_WriteFrameWithChildren(root, NULL);
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
