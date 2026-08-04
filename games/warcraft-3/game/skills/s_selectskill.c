#include "s_skills.h"

static void selectskill_menu_selected(LPEDICT clent, DWORD classname) {
    LPEDICT ent = G_GetMainSelectedUnit(clent->client);
    DWORD abilcode = classname;

    if (!ent || !G_UnitIsHero(ent)) {
        return;
    }
    if (ent->hero.skillpoints <= 0) {
        return;
    }
    unit_learnability(ent, abilcode);
    ent->hero.skillpoints--;
    Get_Commands_f(clent);
}

void ui_selectskill(LPGAMECLIENT client) {
    LPEDICT ent = G_GetMainSelectedUnit(client);
    LPCSTR abils = UNIT_ABILITIES_HERO(ent->class_id);
    if (!abils)
        return;
    PARSE_LIST(abils, abil, parse_segment) {
        UI_AddCommandButtonExtended(abil, true, 1);
    }
    UI_AddCommandButton(STR_CmdCancel);
    UI_WriteTooltipFrame();
}

void selectskill_command(LPEDICT edict) {
    UI_WRITE_LAYER(edict, ui_selectskill, LAYER_COMMANDBAR);
    edict->client->menu.cmdbutton = selectskill_menu_selected;
}

ability_t a_selectskill = {
    .cmd = selectskill_command,
};
