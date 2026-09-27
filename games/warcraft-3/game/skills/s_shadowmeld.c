#include "s_skills.h"

#define ID_ASHM MAKEFOURCC('A', 's', 'h', 'm')
#define ID_AHID MAKEFOURCC('A', 'h', 'i', 'd')
/* Standard retail Shadow Meld uses a 1.5 second fade. Keep this as a
 * compatibility constant for now; custom/variant abilities can author a
 * different fade, so this must ultimately come from the resolved ability data. */
#define WC3_SHADOWMELD_FADE_MS 1500u

/* Retail Shadow Meld is conditional invisibility rather than an RF_HIDDEN
 * lifecycle. Keeping the source as explicit unit state lets the existing
 * player-relative invisibility query decide owner/detector visibility without
 * colliding with RF_HIDDEN uses such as cargo, mines, training, or revival. */
bool S_ShadowMeldActive(edict_t const *unit) {
    return unit && unit->inuse && unit->shadowmeld.active;
}

void S_ShadowMeldBreak(edict_t *unit) {
    if (!unit) return;
    unit->shadowmeld.fade_start = 0;
    unit->shadowmeld.fading = false;
    unit->shadowmeld.active = false;
    unit->shadowmeld.hide_order_active = false;
}

static bool shadowmeld_has_standard(edict_t const *unit) {
    return unit && G_UnitAbilityLevel(unit, ID_ASHM) != 0;
}

static bool shadowmeld_has_akama(edict_t const *unit) {
    return unit && G_UnitAbilityLevel(unit, ID_AHID) != 0;
}

static bool shadowmeld_has_ability(edict_t const *unit) {
    return shadowmeld_has_standard(unit) || shadowmeld_has_akama(unit);
}

static bool shadowmeld_stationary(edict_t const *unit) {
    if (!unit || M_IsDead((edict_t *)unit) || unit->training || unit->construction.active ||
        (unit->svflags & SVF_NOCLIENT) || S_GoldMineWorkerIsInside((edict_t *)unit))
        return false;
    /* Ordinary idle and Hold Position stand moves do not own an ability proc.
     * Movement, Attack, Patrol, Harvest, Repair and other active behaviors do. */
    return !unit->currentmove || !unit->currentmove->proc;
}

static bool shadowmeld_eligible(edict_t const *unit) {
    if (!G_IsNight() || !shadowmeld_stationary(unit)) return false;
    return shadowmeld_has_ability(unit);
}

static void shadowmeld_update(edict_t *unit) {
    uint32_t now;
    bool eligible;

    if (!unit || !unit->inuse) return;
    eligible = shadowmeld_eligible(unit);
    if (!eligible) {
        unit->shadowmeld.fade_start = 0;
        unit->shadowmeld.fading = false;
        unit->shadowmeld.active = false;
        return;
    }
    if (unit->shadowmeld.active) return;

    now = G_Time();
    if (!unit->shadowmeld.fading) {
        unit->shadowmeld.fade_start = now;
        unit->shadowmeld.fading = true;
        return;
    }
    if ((uint32_t)(now - unit->shadowmeld.fade_start) >= WC3_SHADOWMELD_FADE_MS) {
        unit->shadowmeld.fading = false;
        unit->shadowmeld.active = true;
    }
}

/* Both stock Shadow Meld (Ashm) and Shadow Meld (Akama) (Ahid) own the
 * passive/update state and expose Warcraft's no-target "ambush" Hide order.
 * They stay separate handlers because retail ability metadata treats them as
 * distinct effect classes.  The Akama variant additionally disables automatic
 * enemy acquisition even when the player has not explicitly issued Hide. */
static intptr_t shadowmeld_common(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call, bool akama) {
    switch (msg) {
    case A_UPDATE:
        shadowmeld_update(ent);
        return ent && shadowmeld_has_ability(ent);
    case A_NO_ACQUIRE:
        return ent && (ent->shadowmeld.hide_order_active || (akama && shadowmeld_has_akama(ent)));
    case A_NO_RETALIATE:
        return ent && ent->shadowmeld.hide_order_active;
    case A_MOVE_LEAVE:
        if (ent && (ent->shadowmeld.active || ent->shadowmeld.fading || ent->shadowmeld.hide_order_active)) {
            S_ShadowMeldBreak(ent);
            return true;
        }
        return false;
    case A_ORDER_ACCEPTED:
        if (ent && call && call->order && strcmp(call->order, "ambush")) {
            S_ShadowMeldBreak(ent);
        }
        return false;
    case A_DISABLE:
    case A_DEATH:
    case A_UNIT_REMOVE:
        S_ShadowMeldBreak(ent);
        return true;
    case A_UNIT_INIT:
        if (ent && !shadowmeld_has_ability(ent)) S_ShadowMeldBreak(ent);
        return false;
    case A_VALIDATE:
        return ent && G_IsNight();
    case A_EXECUTE:
        if (!ent || !G_IsNight()) return false;
        order_stop(ent);
        ent->shadowmeld.hide_order_active = true;
        if (!ent->shadowmeld.active) {
            ent->shadowmeld.fade_start = 0;
            ent->shadowmeld.fading = false;
        }
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

BZ_ABILITY_PROC(CAbilityShadowMeld) {
    return shadowmeld_common(ent, msg, call, false);
}

/* Ahid is the distinct Shadow Meld (Akama) effect class.  Keep its permanent
 * no-auto-acquire policy local to that class; do not collapse it into Ashm.
 * The current classic data baseline remains night-only. */
BZ_ABILITY_PROC(CAbilityShadowMeldAkama) {
    return shadowmeld_common(ent, msg, call, true);
}

/* Compatibility name retained for callers/tests written against the first
 * dedicated Hide implementation.  Stock command registration now lives on
 * each Shadow Meld effect class itself. */
BZ_ABILITY_PROC(CAbilityHide) {
    return shadowmeld_common(ent, msg, call, false);
}
