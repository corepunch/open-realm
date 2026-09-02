#include "s_skills.h"

/* ---- Immolation (AEim): toggle AoE damage around caster ------------------ */

static void immolation_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD code = spell->code;

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (caster->abilstatus[i].level && caster->abilstatus[i].code == code) {
            memset(&caster->abilstatus[i], 0, sizeof(caster->abilstatus[i]));
            G_InvalidateUnitInfoPanel(caster);
            return;
        }
    }
    unit_addstatus(caster, "Biml", 1);
}

static spell_info_t spell_immolation = {
    .code = MAKEFOURCC('A', 'E', 'i', 'm'),
    .name = "Immolation",
    .target_type = SPELL_TARGET_NONE,
    .flags = SPELL_TOGGLE,
    .execute = immolation_execute,
};

ability_t a_immolation = {
    .cmd = spell_cmd,
    .spell = &spell_immolation,
};

/* ---- Cold Arrows (AHca): autocast attack modifier with slow -------------- */

static void cold_arrows_execute(LPEDICT caster, spellTarget_t st, spell_info_t const *spell) {
    DWORD code = MAKEFOURCC('c', 'o', 'l', 'd');

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (caster->abilstatus[i].level && caster->abilstatus[i].code == code) {
            memset(&caster->abilstatus[i], 0, sizeof(caster->abilstatus[i]));
            G_InvalidateUnitInfoPanel(caster);
            return;
        }
    }
    unit_addstatus(caster, "cold", 1);
}

static spell_info_t spell_cold_arrows = {
    .code = MAKEFOURCC('A', 'H', 'c', 'a'),
    .name = "Cold Arrows",
    .target_type = SPELL_TARGET_NONE,
    .flags = SPELL_TOGGLE | SPELL_AUTOCAST,
    .execute = cold_arrows_execute,
};

ability_t a_cold_arrows = {
    .cmd = spell_cmd,
    .spell = &spell_cold_arrows,
};

/* Phoenix Fire, Invulnerable: passive abilities with no command handler.
 * Zero-initialized; the engine never calls cmd for passives. */
ability_t a_phoenix_fire = {0};
ability_t a_invulnerable = {0};

/* Harvest Lumber and Couple Instant: non-spell abilities with stub command handlers. */
static void stub_cancel_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
}

ability_t a_harvest_lumber = {
    .cmd = stub_cancel_command,
};


ability_t a_couple_instant = {
    .cmd = stub_cancel_command,
};
