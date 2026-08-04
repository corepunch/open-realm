#include "s_skills.h"

#define ID_DEVOTION_AURA "AHad"

static LPCSTR devotionaura_target_art;

static umove_t aura_move_stand = { "stand", ai_idle, NULL };

static void devotionaura_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    (void)st;
    (void)spell;
    unit_addstatus(caster, ID_DEVOTION_AURA, 1);

    LPEDICT effect = G_Spawn();
    effect->s.origin = caster->s.origin;
    effect->s.angle = caster->s.angle;
    effect->s.model = G_RegisterModel(devotionaura_target_art);
    effect->goalentity = caster;
    effect->movetype = MOVETYPE_LINK;
    effect->think = M_MoveFrame;
    unit_setmove(effect, &aura_move_stand);
}

static void SP_ability_devotionaura(LPCSTR classname, ability_t *self) {
    devotionaura_target_art = FindConfigValue(classname, STR_TARGET_ART);
}

static spell_info_t spell_devotionaura = {
    .code = MAKEFOURCC('A', 'H', 'a', 'd'),
    .name = "Devotion Aura",
    .target_type = SPELL_TARGET_NONE,
    .execute = devotionaura_execute,
};

ability_t a_devotionaura = {
    .init = SP_ability_devotionaura,
    .cmd = spell_cmd,
    .spell = &spell_devotionaura,
};
