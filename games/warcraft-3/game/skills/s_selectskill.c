#include "s_skills.h"

static void selectskill_menu_selected(LPEDICT clent, DWORD classname) {
    LPEDICT ent = G_GetMainSelectedUnit(clent->client);
    DWORD abilcode = classname;

    G_HeroLearnSkill(ent, abilcode);
    Get_Commands_f(clent);
}

void ui_selectskill(LPGAMECLIENT client) {
    LPEDICT ent = G_GetMainSelectedUnit(client);
    LPCSTR abils;

    if (!ent || !G_UnitIsHero(ent) || !ent->data.UnitAbilities) {
        return;
    }
    abils = ent->data.UnitAbilities->heroAbilList;
    if (!abils) {
        return;
    }
    PARSE_LIST(abils, abil, parse_segment) {
        DWORD abilcode = 0;
        DWORD next_level = 0;
        DWORD required_level = 0;
        heroSkillState_t state;
        gameCommandButton_t button;

        if (strlen(abil) != 4) {
            continue;
        }
        memcpy(&abilcode, abil, sizeof(abilcode));
        state = G_HeroSkillState(ent, abilcode, &next_level, &required_level);
        if (state == HERO_SKILL_ABSENT || state == HERO_SKILL_MAXED) {
            continue;
        }
        if (!G_BuildCommandButton(ent, abil, true, next_level, &button)) {
            continue;
        }
        button.number = next_level;
        if (state != HERO_SKILL_AVAILABLE) {
            button.disabled = 1;
        }
        if (state == HERO_SKILL_LEVEL_LOCKED) {
            size_t const used = strlen(button.ubertip);
            snprintf(button.ubertip + used, sizeof(button.ubertip) - used,
                     "%s|cffffcc00Requires: Level %u|r",
                     used ? "|n" : "", (unsigned)required_level);
        }
        UI_WriteCommandButtonFrame(&button);
    }
    UI_AddCommandButton(STR_CmdCancel);
    UI_WriteTooltipFrame();
}

void selectskill_command(LPEDICT edict) {
    UI_WRITE_LAYER(edict, ui_selectskill, LAYER_COMMANDBAR);
    edict->client->menu.cmdbutton = selectskill_menu_selected;
    edict->client->menu.refresh = selectskill_command;
}

ability_t a_selectskill = {
    .cmd = selectskill_command,
};
