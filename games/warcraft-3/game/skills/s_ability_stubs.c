#include "s_skills.h"

static void stub_cancel_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
}

static void immolation_command(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = MAKEFOURCC('B', 'i', 'm', 'l');

    if (!caster) {
        return;
    }
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (caster->abilstatus[i].level && caster->abilstatus[i].code == code) {
            memset(&caster->abilstatus[i], 0, sizeof(caster->abilstatus[i]));
            return;
        }
    }
    unit_addstatus(caster, "Biml", 1);
}

ability_t a_immolation = {
    .cmd = immolation_command,
};

ability_t a_phoenix_fire = {0};
ability_t a_cold_arrows = {0};
ability_t a_invulnerable = {0};

ability_t a_harvest_lumber = {
    .cmd = stub_cancel_command,
};

/* Aren/Arst share the same repair logic as Arep. */
ability_t a_repair_generic = {
    .init = SP_ability_repair,
    .cmd = stub_cancel_command,
};

ability_t a_couple_instant = {
    .cmd = stub_cancel_command,
};
