#include "s_skills.h"

void selectskill_menu_selected(LPEDICT ent, DWORD building_id) {
    (void)ent; (void)building_id;
}

void selectskill_command(LPEDICT edict) {
    (void)edict;
}

ability_t a_selectskill = {
    .cmd = selectskill_command,
};
