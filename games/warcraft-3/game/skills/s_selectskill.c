#include "s_skills.h"

void selectskill_menu_selected(LPEDICT ent, DWORD building_id) {
//    entityState_t cursor;
//    FillUnitData(&cursor, building_id, "stand");
//    UI_AddCancelButton(ent);
//    gi.WriteByte(svc_cursor);
//    gi.WriteEntity(&cursor);
//    gi.unicast(ent);
//    ent->client->menu.on_location_selected = build_menu_send_builder;
//    ent->build_project = building_id;
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
}

void selectskill_command(LPEDICT edict) {
    UI_WRITE_LAYER(edict, ui_selectskill, LAYER_COMMANDBAR);
    edict->client->menu.cmdbutton = selectskill_menu_selected;
}

ability_t a_selectskill = {
    .cmd = selectskill_command,
};
