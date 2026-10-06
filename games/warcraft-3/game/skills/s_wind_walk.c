#include "s_skills.h"

#define WW_ID_BOWK MAKEFOURCC('B','O','w','k')
#define WW_ID_BINV MAKEFOURCC('B','i','n','v')

/* Name=Wind Walk
 * Ubertip="Allows the Blademaster to become invisible and move faster until it attacks or uses an ability."
 */
static heroabilitystatus_t *wind_walk_status(edict_t *unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit))
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == WW_ID_BOWK)
            return unit->abilstatus + i;
    return NULL;
}

static bool wind_walk_event_status(abilityCall_t const *call) {
    return call && call->status.slot && call->status.slot->level &&
           call->status.slot->code == WW_ID_BOWK;
}

static void wind_walk_cleanup(edict_t *unit, heroabilitystatus_t const *status) {
    if (!unit || !status || !status->level) return;
    if (!status->data) {
        fprintf(stderr, "WC3 Wind Walk: status on unit %u has no applying ability rawcode\n",
                unit->s.number);
        if (!S_UnitHasTemporaryInvisibility(unit, status)) G_SetEntityHidden(unit,false);
        return;
    }
    if (!S_UnitHasTemporaryInvisibility(unit, status)) G_SetEntityHidden(unit,false);
    S_SpellStartCooldown(unit, status->data, status->level);
    G_InvalidateUnitInfoPanel(unit);
}

static void wind_walk_end(edict_t *unit) {
    heroabilitystatus_t *status = wind_walk_status(unit);
    heroabilitystatus_t saved;
    if (!status) return;
    saved = *status;
    memset(status, 0, sizeof(*status));
    wind_walk_cleanup(unit, &saved);
}

static bool wind_walk_validate(edict_t *unit, abilityCall_t const *call) {
    bool has_slot = wind_walk_status(unit) != NULL;
    uint32_t level;

    if (!unit || !call || !call->item) return false;
    if (!unit->abilstatus) has_slot = true;
    if (!has_slot) {
        FOR_LOOP(i, G_UnitStatusSlotCount(unit))
            if (!unit->abilstatus[i].level) { has_slot = true; break; }
    }
    level = S_SpellLevel(unit, call->item->code);
    if (!has_slot || S_SpellDuration(call->item->code, level, true) <= 0.0f) return false;
    return CAbilitySimpleSpell(unit, A_VALIDATE, call) != 0;
}

static void wind_walk_execute(edict_t *unit, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(unit, spell->code);
    heroabilitystatus_t *status;

    status = S_SpellApplyTimedStatus(unit, "BOwk", level,
                                     S_SpellDuration(spell->code, level, true));
    if (!status) {
        fprintf(stderr, "WC3 Wind Walk: failed to create status for unit %u\n", unit->s.number);
        return;
    }
    status->data = spell->code;
    G_SetEntityHidden(unit,true);
}

BZ_ABILITY_PROC(CAbilityWindWalk) {
    heroabilitystatus_t *status;

    switch (msg) {
    case A_VALIDATE:
        return wind_walk_validate(ent, call);
    case A_EXECUTE:
        if (!ent || !call || !call->item) return false;
        wind_walk_execute(ent, call->item);
        return true;
    case A_STATUS_REMOVE:
        if (!wind_walk_event_status(call)) return false;
        wind_walk_cleanup(ent, call->status.slot);
        return true;
    case A_MOVE_COLLISION_QUERY:
        return wind_walk_event_status(call);
    case A_ATTACK_DAMAGE_BONUS:
        if (!wind_walk_event_status(call)) return 0;
        status = call->status.slot;
        if (!status) return 0;
        if (!status->data) {
            fprintf(stderr, "WC3 Wind Walk: status on unit %u has no applying ability rawcode\n",
                    ent->s.number);
            return 0;
        }
        return (intptr_t)(int)S_SpellData(status->data, status->level, 3);
    case A_ATTACK_LANDED:
        if (wind_walk_event_status(call)) wind_walk_end(ent);
        return true;
    case A_SPELL_COMMIT:
        if (!wind_walk_event_status(call)) return false;
        if (call && call->item && call->item->ability && call->item->ability->proc == CAbilityWindWalk) return true;
        wind_walk_end(ent);
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
