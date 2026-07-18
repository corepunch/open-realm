#include "s_skills.h"

static LPCSTR holylight_target_art;

void holylight_done(LPEDICT self);

static umove_t move_heal = { "stand channel", ai_idle, holylight_done, &a_holylight };

void holylight_done(LPEDICT self) {
    self->stand(self);
}

static BOOL holylight_selecttarget(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, MAKEFOURCC('A', 'H', 'h', 'b'));
    DWORD level = S_SpellLevel(caster, code);
    FLOAT amount = S_SpellData(code, level, 1);
    FLOAT range = S_SpellRange(code, level);
    LPCSTR race;

    if (target == caster) {
        return false;
    }
    if (!S_SpellIsAliveTarget(target)) {
        return false;
    }
    if (!S_SpellTargetInRange(caster, target, range)) {
        return false;
    }
    if (!S_SpellAllowsTarget(code, caster, target)) {
        return false;
    }
    if (S_SpellIsEnemy(caster, target)) {
        race = UnitStringField(UnitsMetaData, target->class_id, "urac");
        if (!race || strcmp(race, STR_UNDEAD)) {
            return false;
        }
    }
    if (!S_SpellCooldownReady(caster, code)) {
        return false;
    }
    if (!S_SpellSpendMana(caster, code, level)) {
        return false;
    }

    S_SpellSpawnTargetArt(target, holylight_target_art);
    S_SpellStartCooldown(caster, code, level);
    unit_setmove(caster, &move_heal);
    if (S_SpellIsFriend(caster, target)) {
        S_SpellHeal(target, amount);
    } else {
        T_Damage(target, caster, (int)(amount * 0.5f));
    }
    return true;
}

void holylight_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = holylight_selecttarget;
}

void SP_ability_holylight(LPCSTR classname, ability_t *self) {
    holylight_target_art = FindConfigValue(classname, STR_TARGET_ART);
}

ability_t a_holylight = {
    .cmd = holylight_command,
    .init = SP_ability_holylight,
};
