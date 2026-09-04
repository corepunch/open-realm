#include "s_skills.h"

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
        LPCSTR race = target->data.UnitData->race;
        return race && !strcmp(race, STR_UNDEAD);
    }
    return S_SpellIsFriend(caster, target);
}

static void holylight_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT amount = S_SpellData(spell->code, level, 1);

    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    unit_setmove(caster, &move_heal);
    if (S_SpellIsFriend(caster, target))
        S_SpellHeal(target, amount);
    else
        T_Damage(target, caster, (int)(amount * 0.5f));
}

static spell_info_t spell_holylight = {
    .code = MAKEFOURCC('A', 'H', 'h', 'b'),
    .name = "Holy Light",
    .target_type = SPELL_TARGET_UNIT,
    .validate = holylight_validate,
    .execute = holylight_execute,
};

ability_t a_holylight = {
    .cmd = spell_cmd,
    .spell = &spell_holylight,
};
