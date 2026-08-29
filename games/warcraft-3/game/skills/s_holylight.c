#include "s_skills.h"

static LPCSTR holylight_target_art;

void holylight_done(LPEDICT self);

static umove_t move_heal = { "stand channel", ai_idle, holylight_done, &a_holylight };

void holylight_done(LPEDICT self) {
    self->stand(self);
}

/* Holy Light has unique target validation: friendlies are healed, undead
 * enemies take half-damage.  Self-target and non-undead enemies are rejected. */
static BOOL holylight_validate(LPEDICT caster, spellTarget_t st) {
    LPEDICT target = st.entity;

    if (target == caster) return false;
    if (!S_SpellIsAliveTarget(target)) return false;
    if (S_SpellIsEnemy(caster, target)) {
        LPCSTR race = target->UnitData->race;
        return race && !strcmp(race, STR_UNDEAD);
    }
    return S_SpellIsFriend(caster, target);
}

static void holylight_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT amount = S_SpellData(spell->code, level, 1);

    S_SpellSpawnTargetArt(target, holylight_target_art);
    unit_setmove(caster, &move_heal);
    if (S_SpellIsFriend(caster, target))
        S_SpellHeal(target, amount);
    else
        T_Damage(target, caster, (int)(amount * 0.5f));
}

static void SP_ability_holylight(LPCSTR classname, ability_t *self) {
    holylight_target_art = FindConfigValue(classname, STR_TARGET_ART);
}

static spell_info_t spell_holylight = {
    .code = MAKEFOURCC('A', 'H', 'h', 'b'),
    .name = "Holy Light",
    .target_type = SPELL_TARGET_UNIT,
    .validate = holylight_validate,
    .execute = holylight_execute,
};

ability_t a_holylight = {
    .init = SP_ability_holylight,
    .cmd = spell_cmd,
    .spell = &spell_holylight,
};
