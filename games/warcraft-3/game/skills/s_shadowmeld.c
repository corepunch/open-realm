#include "s_skills.h"

#define ID_ASHM MAKEFOURCC('A', 's', 'h', 'm')
#define ID_AHID MAKEFOURCC('A', 'h', 'i', 'd')
/* Presentation approximation: friendly/shared-vision viewers retain a ghosted
 * model at 35% opacity. Retail's exact final alpha and interpolation curve have
 * not been recovered; keep these presentation constants isolated from gameplay. */
#define WC3_SHADOWMELD_FRIENDLY_ALPHA 0.35f

static uint32_t shadowmeld_fade_ms(edict_t const *unit);

static bool shadowmeld_has_standard(edict_t const *unit) {
    return unit && G_UnitAbilityLevel(unit, ID_ASHM) != 0;
}

static bool shadowmeld_has_akama(edict_t const *unit) {
    return unit && G_UnitAbilityLevel(unit, ID_AHID) != 0;
}

static bool shadowmeld_has_ability(edict_t const *unit) {
    return shadowmeld_has_standard(unit) || shadowmeld_has_akama(unit);
}

static bool shadowmeld_ability_available(edict_t const *unit, bool akama) {
    uint32_t code = akama ? ID_AHID : ID_ASHM;
    return unit && G_UnitAbilityLevel(unit, code) != 0 && G_IsUnitAbilityAvailable(unit, code);
}

static bool shadowmeld_any_ability_available(edict_t const *unit) {
    return shadowmeld_ability_available(unit, false) || shadowmeld_ability_available(unit, true);
}

/* Retail Shadow Meld is conditional invisibility rather than an RF_HIDDEN
 * lifecycle. Keeping the source as explicit unit state lets the existing
 * player-relative invisibility query decide owner/detector visibility without
 * colliding with RF_HIDDEN uses such as cargo, mines, training, or revival. */
bool S_ShadowMeldActive(edict_t const *unit) {
    return unit && unit->inuse && unit->shadowmeld && unit->shadowmeld->active &&
           shadowmeld_any_ability_available(unit);
}

/* Presentation-only opacity for a viewer that is allowed to perceive the unit.
 * Smoothstep gives a gentle ease-in/ease-out fade instead of a visibly linear
 * alpha ramp. Gameplay still switches invisibility only when the 1.5s fade
 * completes; hostile/detector visibility remains owned by the shared query. */
float S_ShadowMeldPresentationAlpha(edict_t const *unit) {
    float t, eased;
    uint32_t elapsed, fade_ms;

    if (!unit || !unit->inuse || !unit->shadowmeld || !shadowmeld_any_ability_available(unit)) return 1.0f;
    if (unit->shadowmeld->active) return WC3_SHADOWMELD_FRIENDLY_ALPHA;
    if (!unit->shadowmeld->fading) return 1.0f;

    fade_ms = shadowmeld_fade_ms(unit);
    if (!fade_ms) return 1.0f;
    elapsed = G_Time() - unit->shadowmeld->fade_start;
    t = MIN(1.0f, MAX(0.0f, (float)elapsed / (float)fade_ms));
    eased = t * t * (3.0f - 2.0f * t);
    return 1.0f - (1.0f - WC3_SHADOWMELD_FRIENDLY_ALPHA) * eased;
}

static void shadowmeld_set_hide_order(edict_t *unit, bool active) {
    gameClient_t *client;

    assert(unit && unit->shadowmeld);
    if (unit->shadowmeld->hide_order_active == active) return;
    unit->shadowmeld->hide_order_active = active;
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (client) G_InvalidateCommands(client);
}

void S_ShadowMeldBreak(edict_t *unit) {
    if (!unit || !unit->shadowmeld) return;
    unit->shadowmeld->fade_start = 0;
    unit->shadowmeld->fading = false;
    unit->shadowmeld->active = false;
    shadowmeld_set_hide_order(unit, false);
}

static uint32_t shadowmeld_fade_ms(edict_t const *unit) {
    static uint32_t bad_code, bad_level;
    uint32_t code = shadowmeld_has_standard(unit) ? ID_ASHM :
                    shadowmeld_has_akama(unit) ? ID_AHID : 0;
    uint32_t level = code ? G_UnitAbilityLevel(unit, code) : 0;
    float duration = code && level ? S_SpellData(code, level, 1) : 0.0f; /* Shm1 / DataA */

    if (!(duration > 0.0f) || !isfinite(duration)) {
        if (code && (bad_code != code || bad_level != level)) {
            fprintf(stderr, "WC3 Shadow Meld: invalid Shm1 duration for %08x level %u\n",
                    (unsigned)code, (unsigned)level);
            bad_code = code; bad_level = level;
        }
        return 0;
    }
    return (uint32_t)(duration * 1000.0f + 0.5f);
}

static bool shadowmeld_stationary(edict_t const *unit) {
    if (!unit || M_IsDead((edict_t *)unit) || unit->training || unit->construction ||
        (unit->svflags & SVF_NOCLIENT) || S_GoldMineWorkerIsInside((edict_t *)unit))
        return false;
    /* Ordinary idle and Hold Position stand moves do not own an ability proc.
     * Movement, Attack, Patrol, Harvest, Repair and other active behaviors do. */
    return !unit->currentmove || !unit->currentmove->proc;
}

static bool shadowmeld_eligible(edict_t const *unit) {
    if (!G_IsNight() || !shadowmeld_stationary(unit)) return false;
    return shadowmeld_any_ability_available(unit);
}

static void shadowmeld_update(edict_t *unit) {
    uint32_t now, fade_ms;
    bool eligible;

    if (!unit || !unit->inuse) return;
    if (!unit->shadowmeld && !shadowmeld_has_ability(unit)) return;
    if (!unit->shadowmeld) unit->shadowmeld = G_AllocShadowMeld();
    assert(unit->shadowmeld);
    if (!shadowmeld_any_ability_available(unit)) {
        S_ShadowMeldBreak(unit);
        return;
    }
    fade_ms = shadowmeld_fade_ms(unit);
    eligible = fade_ms && shadowmeld_eligible(unit);
    if (!eligible) {
        unit->shadowmeld->fade_start = 0;
        unit->shadowmeld->fading = false;
        unit->shadowmeld->active = false;
        return;
    }
    if (unit->shadowmeld->active) return;

    now = G_Time();
    if (!unit->shadowmeld->fading) {
        unit->shadowmeld->fade_start = now;
        unit->shadowmeld->fading = true;
        return;
    }
    if ((uint32_t)(now - unit->shadowmeld->fade_start) >= fade_ms) {
        unit->shadowmeld->fading = false;
        unit->shadowmeld->active = true;
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
        if (shadowmeld_ability_available(ent, akama)) shadowmeld_update(ent);
        return ent && shadowmeld_has_ability(ent);
    case A_NO_ACQUIRE:
        return ent && shadowmeld_ability_available(ent, akama) &&
               ((ent->shadowmeld && ent->shadowmeld->hide_order_active) || (akama && shadowmeld_has_akama(ent)));
    case A_NO_RETALIATE:
        return ent && shadowmeld_ability_available(ent, akama) && ent->shadowmeld && ent->shadowmeld->hide_order_active;
    case A_TOGGLE_ON:
        return ent && shadowmeld_ability_available(ent, akama) && ent->shadowmeld && ent->shadowmeld->hide_order_active;
    case A_MOVE_LEAVE:
        if (ent && ent->shadowmeld && (ent->shadowmeld->active || ent->shadowmeld->fading || ent->shadowmeld->hide_order_active)) {
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
        return ent && G_IsNight() && shadowmeld_ability_available(ent, akama);
    case A_EXECUTE:
        if (!ent || !G_IsNight() || !shadowmeld_ability_available(ent, akama)) return false;
        order_stop_cleanup(ent);
        if (!ent->shadowmeld) ent->shadowmeld = G_AllocShadowMeld();
        assert(ent->shadowmeld);
        shadowmeld_set_hide_order(ent, true);
        if (!ent->shadowmeld->active) {
            ent->shadowmeld->fade_start = 0;
            ent->shadowmeld->fading = false;
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
