/*
 * hud_infopanel.c — Info panel, multiselect, and per-frame update stubs.
 *
 * Builds the single-unit info panel (name, level, damage, armor, hero
 * attributes, XP bar, HP/mana), the multi-select grid, and the
 * build-queue overlay.  Also contains the stubbed entry points that
 * console_ui.c now handles client-side.
 */

#include "hud_local.h"
#include "../generated/info_panel_unit_detail.h"
#include "../generated/info_panel_building_detail.h"

static InfoPanelUnitDetail_t unit_panel;
static InfoPanelBuildingDetail_t building_panel;
static FRAMEDEF bottom_panel;
static BOOL infopanel_loaded;

#define INVENTORY_CHARGE_FONT_SIZE 10

static void InfoPanelEnsureLoaded(void) {
    if (infopanel_loaded) return;
    infopanel_loaded = true;
    InfoPanelUnitDetail_Load(&unit_panel);
    InfoPanelBuildingDetail_Load(&building_panel);
    UI_InitFrame(&bottom_panel, FT_SIMPLEFRAME);
    UI_SetSize(&bottom_panel, 0.180f, 0.120f);
    /* UI_SetPoint Y uses WC3 FDF convention: negative = downward from TOPLEFT.
     * UI_CopyFrameBase encodes the raw float; the client negates it on decode.
     * So to place the panel at top-origin y=0.480, pass -(0.480). */
    UI_SetPoint(&bottom_panel, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.310f, -(UI_BASE_HEIGHT - 0.120f));
}

void UI_WriteSingleInfo(LPEDICT ent) {
    char buffer[128];
    UnitBalance_t const *balance = ent->UnitBalance;
    UnitWeapons_t const *weapons = ent->UnitWeapons;
    LPCSTR name = G_UnitProfile(ent->class_id)->properNames;
    LPCSTR unit_name = G_UnitProfile(ent->class_id)->name;
    BOOL const is_hero = balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0;
    DWORD level = is_hero && ent->hero.level > 0 ? ent->hero.level
                                                 : MAX(1, balance->level);
    LONG dice = ent->attack1.numberOfDice;
    LONG min_damage = dice ? (LONG)(ent->attack1.damageBase + dice) : 0;
    LONG max_damage = dice ? (LONG)(ent->attack1.damageBase + dice * ent->attack1.sidesPerDie) : 0;
    LONG dice2 = weapons->attack2.damageDice;
    BOOL has_attack2 = UI_HasSecondAttack(dice2);

    if (!name || !*name) {
        name = unit_name ? unit_name : GetClassName(ent->class_id);
    }

    InfoPanelEnsureLoaded();

    UI_SetText(unit_panel.NameValue, "%s", name);
    snprintf(buffer, sizeof(buffer), "Level %lu %s", (unsigned long)level, unit_name ? unit_name : "");
    UI_SetText(unit_panel.ClassValue, "%s", buffer);

    UI_SetText(unit_panel.AttackLabel1, "Damage:");
    snprintf(buffer, sizeof(buffer), "%d - %d", (int)min_damage, (int)max_damage);
    UI_SetText(unit_panel.AttackValue1, "%s", buffer);

    UI_SetText(unit_panel.AttackLabel2, "Damage:");
    snprintf(buffer, sizeof(buffer), "%d - %d",
             (int)(weapons->attack2.damageBase + dice2),
             (int)(weapons->attack2.damageBase + dice2 * weapons->attack2.damageSides));
    UI_SetText(unit_panel.AttackValue2, "%s", buffer);
    UI_SetHidden(unit_panel.AttackLabel2, !has_attack2);
    UI_SetHidden(unit_panel.AttackValue2, !has_attack2);

    UI_SetText(unit_panel.DefenseLabel, "Armor:");
    snprintf(buffer, sizeof(buffer), "%d", (int)(ent->armor_value + 0.5f));
    UI_SetText(unit_panel.DefenseValue, "%s", buffer);

    UI_SetText(unit_panel.SpeedTitle, "Speed:");
    UI_SetText(unit_panel.SpeedValue, "%d", (int)(ent->unitinfo.MoveSpeed + 0.5f));
    UI_SetText(unit_panel.RangeTitle1, "Range:");
    UI_SetText(unit_panel.RangeValue1, "%d", (int)(ent->attack1.range + 0.5f));
    UI_SetText(unit_panel.RangeTitle2, "Range:");
    UI_SetText(unit_panel.RangeValue2, "%d", (int)(weapons->attack2.range + 0.5f));
    UI_SetHidden(unit_panel.RangeTitle2, !has_attack2);
    UI_SetHidden(unit_panel.RangeValue2, !has_attack2);

    if (is_hero) {
        LPCSTR const prim = balance->primaryAttribute;
        struct { LPCSTR tag, code; DWORD val; } attrs[3] = {
            { "Str:", "STR", ent->hero.str },
            { "Agi:", "AGI", ent->hero.agi },
            { "Int:", "INT", ent->hero.intel },
        };
        LPFRAMEDEF icon_values[3] = { unit_panel.IconValue1, unit_panel.IconValue2, unit_panel.IconValue3 };

        FOR_LOOP(a, 3) {
            BOOL const isprim = prim && !strcmp(prim, attrs[a].code);
            UI_SetText(icon_values[a], "%lu", (unsigned long)attrs[a].val);
            icon_values[a]->Font.Color = isprim ? MAKE(COLOR32, 120, 230, 120, 255) : COLOR32_WHITE;
        }

        DWORD const need = G_HeroXPForLevel(level + 1);
        DWORD const have = G_HeroXPForLevel(level);
        if (need > have) {
            snprintf(buffer, sizeof(buffer), "XP: %lu / %lu",
                     (unsigned long)(ent->hero.xp - (ent->hero.xp < have ? ent->hero.xp : have)),
                     (unsigned long)(need - have));
            UI_SetText(unit_panel.IconValue4, "%s", buffer);
            unit_panel.IconValue4->Font.Color = MAKE(COLOR32, 200, 200, 200, 255);
        }
    }

    /* InfoPanelUnitDetail uses SetAllPoints and must fill the old bottom-center panel, not the whole scene. */
    UI_WriteFrame(&bottom_panel);
    UI_WriteFrameWithChildren(unit_panel.InfoPanelUnitDetail, &bottom_panel);
}

void UI_WriteMultiselect(LPEDICT *ents, DWORD count) {
    if (count > 12) count = 12;
    DWORD size = sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t) * count;
    LPBYTE buffer = gi.MemAlloc(size);
    uiMultiselect_t *multi = (uiMultiselect_t *)buffer;
    uiFrame_t frame;

    memset(buffer, 0, size);
    multi->hp_bar = gi.ImageIndex(Theme_String("SimpleHpBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
    multi->mana_bar = gi.ImageIndex(Theme_String("SimpleManaBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
    multi->offset = MAKE(VECTOR2, 0.031f, 0.050f);
    multi->numcolumns = 6;
    multi->numitems = count;
    FOR_LOOP(i, count) {
        multi->items[i].entity = ents[i]->s.number;
        multi->items[i].image = gi.ImageIndex(FindConfigValue(GetClassName(ents[i]->class_id), STR_ART));
    }

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MULTISELECT;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, 0.314f, 0.500f, 0.025f, 0.025f);
    UI_WriteProxyFrame(&frame, buffer, size);
    gi.MemFree(buffer);
}

void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count) {
    if (!ent->client) return;
    if (count == 1 && !selected[0]->build) {
        ent->client->infopanel.entity = selected[0]->s.number;
        ent->client->infopanel.hp = (LONG)(selected[0]->health.value + 0.5f);
        ent->client->infopanel.mana = (LONG)(selected[0]->mana.value + 0.5f);
        ent->client->infopanel.xp = (LONG)selected[0]->hero.xp;
    } else {
        ent->client->infopanel.entity = 0;
    }
}

void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count) {
    UI_WriteStart(LAYER_INFOPANEL);
    if (count == 1) {
        if (selected[0]->build) {
            UI_WriteBuildQueue(selected[0]);
        } else {
            UI_WriteSingleInfo(selected[0]);
        }
    } else if (count > 1) {
        UI_WriteMultiselect(selected, count);
    }
    UI_WriteEnd(ent);
    UI_SeedInfoPanelCache(ent, selected, count);
}

static DWORD SelectedUnits(LPGAMECLIENT client, LPEDICT *out, DWORD max_out) {
    DWORD count = 0;
    FOR_SELECTED_UNITS(client, ent) {
        if (count < max_out) out[count] = ent;
        count++;
    }
    return MIN(count, max_out);
}

void Get_Commands_f(LPEDICT ent) {
    LPEDICT selected = ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;
    gameCommandButton_t buttons[12];
    BYTE count;

    if (!ent || !ent->client) return;
    ent->client->commands_dirty = false;
    if (!selected) {
        UI_ClearLayer(ent, LAYER_COMMANDBAR);
        return;
    }
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));

    UI_WriteStart(LAYER_COMMANDBAR);
    count = G_GetCommandButtons(selected, buttons, 12);
    FOR_LOOP(i, count) {
        UI_WriteCommandButtonFrame(&buttons[i]);
    }
    if (count) UI_WriteTooltipFrame();
    UI_WriteEnd(ent);
}

static void WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;
    if (!ent || !ent->s.model) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    UI_SetFrameRect(&frame, 0.215f, 0.486f, 0.080f, 0.080f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void WriteInventoryCharge(FLOAT x, FLOAT y, FLOAT w, FLOAT h, DWORD charges) {
    uiFrame_t frame;
    uiLabel_t label;
    char text[16];

    if (!charges) return;
    memset(&frame, 0, sizeof(frame)); memset(&label, 0, sizeof(label));
    snprintf(text, sizeof(text), "%u", (unsigned)charges);
    frame.flags.type = FT_STRING; frame.text = text; frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", INVENTORY_CHARGE_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYRIGHT; label.textaligny = FONT_JUSTIFYBOTTOM;
    UI_SetFrameRect(&frame, x + 0.001f, y + 0.001f, w - 0.002f, h - 0.002f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* WC3's classic inventory cover has no usable ROC FDF definition, so construct
 * the native frame directly while keeping its texture race-selected from war3skins. */
static void WriteInventoryCover(LPEDICT player) {
    static FRAMEDEF frame;
    LPCSTR art = Theme_PlayerString(player ? player->client : NULL, "ConsoleInventoryCoverTexture", NULL);

    if (!art || !*art) {
        fprintf(stderr, "WriteInventoryCover: missing ConsoleInventoryCoverTexture for player skin\n");
        return;
    }
    UI_InitFrame(&frame, FT_TEXTURE);
    UI_SetSize(&frame, 0.128f, 0.175f);
    /* Native WC3 anchor is BOTTOMRIGHT at (0.600, 0.000) in bottom-left coordinates.
     * Relative to OpenRealm's top-left scene origin that is x=0.600, y=0.600. */
    UI_SetPoint(&frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_TOPLEFT, 0.600f, -0.600f);
    frame.AlphaMode = BLEND_MODE_ALPHAKEY;
    frame.Texture.TexCoord.min.y = 0.380859375f;
    frame.Texture.Image = gi.ImageIndex(art);
    UI_WriteFrame(&frame);
}

static void WriteInventoryNoCapacitySlot(BYTE slot, LPCSTR art) {
    FLOAT bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(slot % 2) * 0.0394f;
    FLOAT by = UI_BASE_HEIGHT - 0.0971f + (FLOAT)(slot / 2) * 0.0384f;
    UI_WriteTextureFrame(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, art);
}

static void WriteInventory(LPEDICT player, LPEDICT ent) {
    gameInventoryItem_t items[MAX_INVENTORY];
    DWORD capacity = G_InventoryCapacity(ent);
    BYTE count;

    if (!capacity) { WriteInventoryCover(player); return; }
    if (capacity < MAX_INVENTORY) {
        LPCSTR art = Theme_PlayerString(player ? player->client : NULL, "ConsoleInventoryNoCapacity", NULL);
        if (!art || !*art) {
            fprintf(stderr, "WriteInventory: missing ConsoleInventoryNoCapacity for player skin\n");
            return;
        }
        for (DWORD slot = capacity; slot < MAX_INVENTORY; slot++) WriteInventoryNoCapacitySlot((BYTE)slot, art);
    }

    count = G_GetInventory(ent, items, MAX_INVENTORY);
    FOR_LOOP(i, count) {
        FLOAT bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(items[i].slot % 2) * 0.0394f;
        FLOAT by = UI_BASE_HEIGHT - 0.0971f + (FLOAT)(items[i].slot / 2) * 0.0384f;
        uiFrame_t frame;
        char onclick[128];
        char tooltip[1024];
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_COMMANDBUTTON;
        frame.color = COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(items[i].art);
        UI_FormatTooltip("", items[i].tooltip, items[i].ubertip, 0, tooltip, sizeof(tooltip));
        frame.tooltip = tooltip;
        snprintf(onclick, sizeof(onclick), "inventory %u", (unsigned)items[i].slot);
        frame.onclick = onclick;
        UI_SetFrameRect(&frame, bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f);
        UI_WriteProxyFrame(&frame, NULL, 0);
        WriteInventoryCharge(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, items[i].charges);
    }
    if (count) UI_WriteTooltipFrame();
}

static void UI_SendInventoryLayer(LPEDICT ent, LPEDICT *selected, DWORD count) {
    UI_WriteStart(LAYER_INVENTORY);
    if (count == 1) WriteInventory(ent, selected[0]);
    UI_WriteEnd(ent);
}

void G_RefreshInventoryLayer(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    UI_SendInventoryLayer(ent, selected, count);
}

void Get_Portrait_f(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);

    UI_WriteStart(LAYER_PORTRAIT);
    if (count == 1) WritePortraitFrame(selected[0]);
    UI_WriteEnd(ent);

    UI_SendInfoPanel(ent, selected, count);
    UI_SendInventoryLayer(ent, selected, count);
}

/* Re-send LAYER_INFOPANEL only when HP, mana, or XP of the selected unit changed. */
void G_RefreshInfoPanel(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;
    LONG hp, mana;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    if (count != 1 || selected[0]->build) {
        ent->client->infopanel.entity = 0;
        return;
    }
    hp = (LONG)(selected[0]->health.value + 0.5f);
    mana = (LONG)(selected[0]->mana.value + 0.5f);
    if (selected[0]->s.number == ent->client->infopanel.entity &&
        hp == ent->client->infopanel.hp &&
        mana == ent->client->infopanel.mana &&
        (LONG)selected[0]->hero.xp == ent->client->infopanel.xp) {
        return;
    }
    UI_SendInfoPanel(ent, selected, count);
}

/* Once per server frame, keep every player's info panel in sync. */
void G_UpdateClientInfoPanels(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshInfoPanel(ent);
    }
}

/* Re-send LAYER_CONSOLE only when a resource value changed. */
void G_RefreshResourceBar(LPEDICT ent) {
    LPPLAYER ps;
    LONG gold, lumber, food_u, food_c;

    if (!ent || !ent->client) return;
    ps     = &ent->client->ps;
    gold   = (LONG)ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    lumber = (LONG)ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    food_u = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    food_c = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_CAP];

    if (gold   == ent->client->resourcebar.gold   &&
        lumber == ent->client->resourcebar.lumber  &&
        food_u == ent->client->resourcebar.food_used &&
        food_c == ent->client->resourcebar.food_cap)
        return;

    UI_WriteStart(LAYER_CONSOLE);
    UI_WriteConsoleBackdrop(food_u);
    UI_WriteMinimapFrame();
    UI_WriteEnd(ent);

    ent->client->resourcebar.gold      = gold;
    ent->client->resourcebar.lumber    = lumber;
    ent->client->resourcebar.food_used = food_u;
    ent->client->resourcebar.food_cap  = food_c;
}

/* Once per server frame, keep every player's resource bar in sync. */
void G_UpdateClientResourceBars(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshResourceBar(ent);
    }
}
