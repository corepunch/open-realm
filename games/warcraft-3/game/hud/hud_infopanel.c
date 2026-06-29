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
static BOOL infopanel_loaded;

static void InfoPanelEnsureLoaded(void) {
    if (infopanel_loaded) return;
    infopanel_loaded = true;
    InfoPanelUnitDetail_Load(&unit_panel);
    InfoPanelBuildingDetail_Load(&building_panel);
}

void UI_WriteSingleInfo(LPEDICT ent) {
    char buffer[128];
    LPCSTR name = UNIT_PROPER_NAMES(ent->class_id);
    LPCSTR unit_name = UNIT_NAME(ent->class_id);
    BOOL const is_hero = UNIT_STRENGTH(ent->class_id) > 0 ||
                         UNIT_AGILITY(ent->class_id) > 0 ||
                         UNIT_INTELLIGENCE(ent->class_id) > 0;
    DWORD level = is_hero && ent->hero.level > 0 ? ent->hero.level
                                                 : MAX(1, UNIT_LEVEL(ent->class_id));
    LONG dice = ent->attack1.numberOfDice;
    LONG min_damage = dice ? (LONG)(ent->attack1.damageBase + dice) : 0;
    LONG max_damage = dice ? (LONG)(ent->attack1.damageBase + dice * ent->attack1.sidesPerDie) : 0;

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

    UI_SetText(unit_panel.DefenseLabel, "Armor:");
    snprintf(buffer, sizeof(buffer), "%d", (int)(ent->armor_value + 0.5f));
    UI_SetText(unit_panel.DefenseValue, "%s", buffer);

    if (is_hero) {
        LPCSTR const prim = UNIT_PRIMARY_ATTRIBUTE(ent->class_id);
        struct { LPCSTR tag, code; DWORD val; } attrs[3] = {
            { "Str:", "STR", ent->hero.str },
            { "Agi:", "AGI", ent->hero.agi },
            { "Int:", "INT", ent->hero.intel },
        };
        LPFRAMEDEF icon_backdrops[3] = { unit_panel.IconBackdrop1, unit_panel.IconBackdrop2, unit_panel.IconBackdrop3 };
        LPFRAMEDEF icon_values[3] = { unit_panel.IconValue1, unit_panel.IconValue2, unit_panel.IconValue3 };
        LPCSTR icon_art[3] = { "HeroStrengthIcon", "HeroAgilityIcon", "HeroIntelligenceIcon" };

        FOR_LOOP(a, 3) {
            BOOL const isprim = prim && !strcmp(prim, attrs[a].code);
            UI_SetText(icon_values[a], "%lu", (unsigned long)attrs[a].val);
            icon_values[a]->Font.Color = isprim ? MAKE(COLOR32, 120, 230, 120, 255) : COLOR32_WHITE;
            if (icon_backdrops[a]) {
                icon_backdrops[a]->Texture.Image = gi.ImageIndex(icon_art[a]);
            }
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

    UI_WriteFrameWithChildren(unit_panel.InfoPanelUnitDetail, NULL);
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
    UI_SetFrameRect(&frame, MULTISELECT_X, MULTISELECT_Y, MULTISELECT_SIZE, MULTISELECT_SIZE);
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
    UI_WriteConsoleAnchor();
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
    UI_WriteEnd(ent);
}

static void WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;
    if (!ent || !ent->s.model) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    UI_SetFrameRect(&frame, PORTRAIT_X, PORTRAIT_Y, PORTRAIT_SIZE, PORTRAIT_SIZE);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static void WriteInventory(LPEDICT ent) {
    gameInventoryItem_t items[MAX_INVENTORY];
    BYTE count = G_GetInventory(ent, items, MAX_INVENTORY);
    FOR_LOOP(i, count) {
        RECT rect = UI_InventoryButtonRect(items[i].slot);
        uiFrame_t frame;
        char onclick[128];
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_COMMANDBUTTON;
        frame.color = COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(items[i].art);
        frame.tooltip = items[i].ubertip[0] ? items[i].ubertip : items[i].tooltip;
        snprintf(onclick, sizeof(onclick), "inventory %u", (unsigned)items[i].slot);
        frame.onclick = onclick;
        UI_SetFrameRect(&frame, rect.x, rect.y, rect.w, rect.h);
        UI_WriteProxyFrame(&frame, NULL, 0);
    }
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

    UI_WriteStart(LAYER_INVENTORY);
    if (count == 1) WriteInventory(selected[0]);
    UI_WriteEnd(ent);
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
    UI_WriteConsoleBackdrop();
    UI_WriteMinimapFrame();
    UI_WriteResourceBar(food_u);
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
