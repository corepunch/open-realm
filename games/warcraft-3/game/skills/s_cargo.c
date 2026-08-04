#include "s_skills.h"

static FLOAT cargo_capacity_value;

/* ---- Cargo Hold (Acar): passive ability on transport units ---------------- */

static BOOL cargo_has_capacity(LPEDICT transport, DWORD needed) {
    return (transport->cargo_count + needed) <= transport->cargo_capacity;
}

static void cargo_add_unit(LPEDICT transport, LPEDICT unit) {
    if (transport->cargo_count >= MAX_CARGO) {
        return;
    }
    transport->cargo_units[transport->cargo_count++] = unit;
    unit->s.renderfx |= RF_HIDDEN;
    unit->invulnerable = true;
}

static void cargo_drop_unit(LPEDICT transport, DWORD index) {
    if (index >= transport->cargo_count) {
        return;
    }
    LPEDICT unit = transport->cargo_units[index];
    /* Shift remaining units down. */
    for (DWORD i = index; i < transport->cargo_count - 1; i++) {
        transport->cargo_units[i] = transport->cargo_units[i + 1];
    }
    transport->cargo_count--;
    transport->cargo_units[transport->cargo_count] = NULL;
    unit->s.renderfx &= ~RF_HIDDEN;
    unit->invulnerable = false;
    unit->s.origin2 = transport->s.origin2;
}

static void cargo_drop_all(LPEDICT transport) {
    while (transport->cargo_count > 0) {
        cargo_drop_unit(transport, transport->cargo_count - 1);
    }
}

static void SP_ability_cargo_hold(LPCSTR classname, ability_t *self) {
    cargo_capacity_value = AB_Number(classname, "DataA1");
}

ability_t a_cargo_hold = {
    .init = SP_ability_cargo_hold,
};

/* ---- Load (Aloa): load a unit into a transport -------------------------- */

static BOOL load_selecttarget(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);

    if (!caster || !target || target == caster) {
        return false;
    }
    if (target->s.player != caster->s.player) {
        return false;
    }
    if (!G_ActorHasSkill(caster, "Acar")) {
        return false;
    }
    if (caster->cargo_count >= (DWORD)cargo_capacity_value) {
        return false;
    }
    if (target->s.renderfx & RF_HIDDEN) {
        return false;
    }
    cargo_add_unit(caster, target);
    return true;
}

static void load_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = load_selecttarget;
}

ability_t a_load = {
    .cmd = load_command,
};

/* ---- Drop (Adro): drop cargo at a point --------------------------------- */

static BOOL drop_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);

    if (!caster || !point || caster->cargo_count == 0) {
        return false;
    }
    cargo_drop_unit(caster, caster->cargo_count - 1);
    return true;
}

static void drop_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = drop_selectlocation;
}

ability_t a_drop = {
    .cmd = drop_command,
};

/* ---- Drop Instant (Adri): instant drop ---------------------------------- */
ability_t a_drop_instant = {
    .cmd = drop_command,
};

/* ---- Cargo Hold Burrow (Abun): Orc burrow variant ----------------------- */
ability_t a_cargo_hold_burrow = {
    .init = SP_ability_cargo_hold,
};

/* ---- Cargo Hold Entangled Mine (Aenc): NE entangled mine cargo ----------- */
ability_t a_cargo_hold_entangled_mine = {
    .init = SP_ability_cargo_hold,
};

/* ---- Stand Down (Astd): reverse burrow ---------------------------------- */
static void stand_down_command(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    if (!caster) {
        return;
    }
    caster->invulnerable = false;
    caster->s.renderfx &= ~RF_HIDDEN;
    if (caster->stand) {
        caster->stand(caster);
    }
}

ability_t a_stand_down = {
    .cmd = stand_down_command,
};
