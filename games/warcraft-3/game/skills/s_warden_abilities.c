#include "s_skills.h"

/* Night Elf Warden (Maiev) hero abilities: Blink, Fan of Knives, Shadow Strike.
 * Implemented on the existing spell framework (see s_thunderbolt.c / s_area_spell.c
 * for the point/target patterns). */

#define ID_BLINK        MAKEFOURCC('A', 'E', 'b', 'l')
#define ID_FAN_OF_KNIVES MAKEFOURCC('A', 'E', 'f', 'k')
#define ID_SHADOW_STRIKE MAKEFOURCC('A', 'E', 's', 'h')

/* ---- Blink (AEbl): instant teleport to a target point within range -------- */

static BOOL blink_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    LPEDICT caster = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_BLINK);
    DWORD level = S_SpellLevel(caster, code);
    FLOAT maxrange = S_SpellData(code, level, 1); /* DataA = Maximum Range */
    FLOAT minrange = S_SpellData(code, level, 2); /* DataB = Minimum Range */
    FLOAT dist;

    if (!caster || !point) {
        return false;
    }
    /* Blink range is bounded by the ability's DataA/DataB (Rng1 is the cast
     * range, set huge for this self-target point spell — using it gave an
     * unlimited blink). */
    dist = Vector2_distance(&caster->s.origin2, point);
    if (maxrange > 0 && dist > maxrange) {
        return false;
    }
    if (minrange > 0 && dist < minrange) {
        return false;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return false;
    }
    S_SpellStartCooldown(caster, code, level);

    /* Effect at the departure point, then teleport.  Snap the destination to
     * the nearest free space for the caster's collision radius so the blink
     * never lands on top of another unit (WC3 resolves blink to walkable
     * ground); move-time collision then keeps her from overlapping. */
    S_SpellSpawnTargetArt(caster, S_SpellString(code, "Specialart", level));
    VECTOR2 dest = *point;
    CM_ClosestPathablePointForRadius(point, caster->collision, &dest);
    caster->s.origin2 = dest;
    caster->s.origin.x = dest.x;
    caster->s.origin.y = dest.y;
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
    FLOAT damage = MAX(1.0f, S_SpellData(code, level, 1));   /* DataA = Damage Per Target */
    FLOAT maxtotal = S_SpellData(code, level, 2);            /* DataB = Maximum Total Damage */
    DWORD ntargets = 0;

    if (!caster) {
        return;
    }
    if (!S_SpellCooldownReady(caster, code) || !S_SpellSpendMana(caster, code, level)) {
        return;
    }
    S_SpellStartCooldown(caster, code, level);
    if (radius <= 0.0f) radius = 400.0f;

#define FOK_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsEnemy(caster, t) && \
                     S_SpellAllowsTarget(code, caster, t) &&                     \
                     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)

    /* Count targets first so the Maximum Total Damage cap can scale the
     * per-target damage when it would otherwise be exceeded. */
    FILTER_EDICTS(target, FOK_HITS(target)) {
        ntargets++;
    }
    if (maxtotal > 0.0f && ntargets > 0 && damage * (FLOAT)ntargets > maxtotal) {
        damage = MAX(1.0f, maxtotal / (FLOAT)ntargets);
    }
    FILTER_EDICTS(target, FOK_HITS(target)) {
        T_Damage(target, caster, (DWORD)damage);
    }
#undef FOK_HITS
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

    /* Initial Damage is DataE (DataA is the per-second Decaying Damage of the
     * poison, ~10).  Reading DataA here made Shadow Strike hit for 10 instead
     * of 75. */
    DWORD damage = (DWORD)MAX(1.0f, S_SpellData(code, level, 5)); /* DataE = Initial Damage */
    T_Damage(target, caster, damage);
    /* TODO(1:1): Shadow Strike also applies, via the BEsh buff for Dur1 (~15s),
     * a movement slow (DataB = Movement Speed Factor) and a decaying poison DoT
     * (DataA over the duration, falloff DataD = Decay Power).  The status system
     * currently models only stun + timed-life, so the slow/DoT need a movement-
     * speed modifier and a periodic-damage tick before they can be done 1:1; the
     * previous code applied an inert "Bsta" status (only "Bstu" is read as a
     * stun) that did nothing, so it is removed rather than left misleading. */
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
