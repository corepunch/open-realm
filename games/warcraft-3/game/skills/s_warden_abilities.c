#include "s_skills.h"

/* Night Elf Warden (Maiev) hero abilities: Blink, Fan of Knives, Shadow Strike.
 * Implemented on the existing spell framework (see s_thunderbolt.c / s_area_spell.c
 * for the point/target patterns). */

#define ID_BLINK        MAKEFOURCC('A', 'E', 'b', 'l')
#define ID_FAN_OF_KNIVES MAKEFOURCC('A', 'E', 'f', 'k')
#define ID_SHADOW_STRIKE MAKEFOURCC('A', 'E', 's', 'h')
#define ID_STUN_BUFF    "Bsta"

/* ---- Blink (AEbl): instant teleport to a target point within range -------- */

static BOOL blink_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_BLINK);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);

    if (!caster || !point) {
        return false;
    }
    if (range > 0 && Vector2_distance(&caster->s.origin2, point) > range) {
        return false;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return false;
    }
    S_SpellStartCooldown(caster, code, level);

    /* Effect at the departure point, then teleport. */
    S_SpellSpawnTargetArt(caster, S_SpellString(code, "Specialart", level));
    caster->s.origin2 = *point;
    caster->s.origin.x = point->x;
    caster->s.origin.y = point->y;
    gi.LinkEntity(caster);
    S_SpellSpawnTargetArt(caster, S_SpellString(code, "Areaeffectart", level));
    S_SpellCursorSplat(clent, 0.0f);
    return true;
}

static void blink_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = blink_selectlocation;
}

/* ---- Fan of Knives (AEfk): instant area damage centred on the caster ------ */

static void fanofknives_command(LPEDICT clent) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_FAN_OF_KNIVES);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT radius = S_SpellNumber(code, "Area", level);
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(code, level, 1));

    if (!caster) {
        return;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return;
    }
    S_SpellStartCooldown(caster, code, level);
    if (radius <= 0.0f) radius = 350.0f;

    FILTER_EDICTS(target, target->inuse && target != caster) {
        if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target)) {
            continue;
        }
        if (Vector2_distance(&target->s.origin2, &caster->s.origin2) > radius) {
            continue;
        }
        T_Damage(target, caster, damage);
    }
}

/* ---- Shadow Strike (AEsh): single-target nuke + stun ---------------------- */

static BOOL shadowstrike_selecttarget(LPEDICT clent, LPEDICT target) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_SHADOW_STRIKE);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT range = S_SpellRange(code, level);

    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(caster, target) ||
        !S_SpellAllowsTarget(code, caster, target)) {
        return false;
    }
    if (!S_SpellTargetInRange(caster, target, range)) {
        return false;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return false;
    }
    S_SpellStartCooldown(caster, code, level);

    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(code, level, 1));
    T_Damage(target, caster, damage);
    if (!M_IsDead(target)) {
        FLOAT duration = S_SpellDuration(code, level, UNIT_LEVEL(target->class_id) >= 5);
        if (duration > 0) {
            unit_addtimedstatus(target, ID_STUN_BUFF, 1, duration);
        }
    }
    return true;
}

static void shadowstrike_command(LPEDICT clent) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = shadowstrike_selecttarget;
}

/* ---- Registration -------------------------------------------------------- */

static void SP_ability_noop(LPCSTR classname, ability_t *self) {
    (void)classname;
    (void)self;
}

ability_t a_blink = {
    .init = SP_ability_noop,
    .cmd = blink_command,
};

ability_t a_fan_of_knives = {
    .init = SP_ability_noop,
    .cmd = fanofknives_command,
};

ability_t a_shadow_strike = {
    .init = SP_ability_noop,
    .cmd = shadowstrike_command,
};
