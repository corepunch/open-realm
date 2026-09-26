#include "s_skills.h"

static void selectskill_menu_selected(edict_t * clent, uint32_t classname) {
    edict_t * ent = G_GetMainSelectedUnit(clent->client);
    uint32_t abilcode = classname;

    G_HeroLearnSkill(ent, abilcode);
    Get_Commands_f(clent);
}

void ui_selectskill(gameClient_t * client) {
    edict_t * ent = G_GetMainSelectedUnit(client);
    cstring_t abils;

    if (!ent || !G_UnitIsHero(ent) || !ent->data.UnitAbilities) {
        return;
    }
    abils = ent->data.UnitAbilities->heroAbilList;
    if (!abils) {
        return;
    }
    PARSE_LIST(abils, abil, parse_segment) {
        uint32_t abilcode = 0;
        uint32_t next_level = 0;
        uint32_t required_level = 0;
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

BZ_COMMAND_PROC(AbilitySelectSkill) {
    UI_WRITE_LAYER(clent, ui_selectskill, LAYER_COMMANDBAR);
    clent->client->menu.cmdbutton = selectskill_menu_selected;
    clent->client->menu.refresh = AbilitySelectSkill_Command;
}
