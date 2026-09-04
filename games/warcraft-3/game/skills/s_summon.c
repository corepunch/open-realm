#include "s_skills.h"

#define ID_TIMED_LIFE "BTLF"
#define ID_WATER_ELEMENTAL MAKEFOURCC('A', 'H', 'w', 'e')
#define ID_FERAL_SPIRIT MAKEFOURCC('A', 'O', 's', 'f')

static void summon_unit(LPEDICT caster, DWORD unit_id, DWORD index, DWORD count, FLOAT duration) {
    VECTOR2 loc;
    FLOAT angle;
    LPEDICT summon;

    if (!caster || !unit_id)
        return;

    angle = count > 0 ? (2.0f * (FLOAT)M_PI * (FLOAT)index) / (FLOAT)count : 0.0f;
    loc = caster->s.origin2;
    loc.x += cosf(angle) * MAX(64.0f, caster->collision + 32.0f);
    loc.y += sinf(angle) * MAX(64.0f, caster->collision + 32.0f);
    SP_FindEmptySpaceAround(caster, unit_id, &loc, &angle);

    summon = SP_SpawnAtLocation(unit_id, caster->s.player, &loc);
    if (!summon)
        return;
    summon->owner = caster;
    G_ActivateUnitFood(summon);
    if (summon->stand)
        summon->stand(summon);
    if (duration > 0)
        unit_addtimedstatus(summon, ID_TIMED_LIFE, 1, duration);
    G_PublishSummonEvents(caster, summon);
}

static void summon_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD unit_id = S_SpellUnitId(spell->code, level);
    DWORD count = (DWORD)S_SpellData(spell->code, level, 2);
    FLOAT duration = S_SpellDuration(spell->code, level, false);

    if (!caster || !unit_id) return;
    if (count == 0) count = 1;
    FOR_LOOP(i, count)
        summon_unit(caster, unit_id, i, count, duration);
}

static spell_info_t spell_water_elemental = {
    .code = ID_WATER_ELEMENTAL,
    .name = "Water Elemental",
    .target_type = SPELL_TARGET_NONE,
    .execute = summon_execute,
};

static spell_info_t spell_feral_spirit = {
    .code = ID_FERAL_SPIRIT,
    .name = "Feral Spirit",
    .target_type = SPELL_TARGET_NONE,
    .execute = summon_execute,
};

ability_t a_water_elemental = {
    .cmd = spell_cmd,
    .spell = &spell_water_elemental,
};

ability_t a_feral_spirit = {
    .cmd = spell_cmd,
    .spell = &spell_feral_spirit,
};
