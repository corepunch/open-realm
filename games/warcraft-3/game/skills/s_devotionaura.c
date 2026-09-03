#include "s_skills.h"

#define ID_DEVOTION_AURA "AHad"

static void devotionaura_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    (void)st;
    unit_addstatus(caster, ID_DEVOTION_AURA, 1);

    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, caster, NULL, false);
}

static spell_info_t spell_devotionaura = {
    .code = MAKEFOURCC('A', 'H', 'a', 'd'),
    .name = "Devotion Aura",
    .target_type = SPELL_TARGET_NONE,
    .execute = devotionaura_execute,
};

ability_t a_devotionaura = {
    .cmd = spell_cmd,
    .spell = &spell_devotionaura,
};
