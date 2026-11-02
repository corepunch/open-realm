#include "s_skills.h"

#define WW_ID_BOWK MAKEFOURCC('B','O','w','k')
#define WW_ID_BINV MAKEFOURCC('B','i','n','v')

/* Name=Wind Walk
 * Ubertip="Allows the Blademaster to become invisible and move faster until it attacks or uses an ability."
 */
static heroabilitystatus_t *wind_walk_status(edict_t *unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, G_UnitStatusSlotCount(unit))
        if (unit->abilstatus[i].level && G_UnitStatusClassCode(unit->abilstatus+i) == WW_ID_BOWK)
            return unit->abilstatus + i;
    return NULL;
}

static bool wind_walk_event_status(abilityCall_t const *call) {
    return call && call->status.slot && call->status.slot->level &&
           G_UnitStatusClassCode(call->status.slot) == WW_ID_BOWK;
}

static void wind_walk_cleanup(edict_t *unit, heroabilitystatus_t *status) {
    if (!unit || !status || !status->level) return;
    uint32_t const code=status->data,rank=status->level,birth=unit->spawn_time;
    uint64_t const epoch=S_SpellPreparationEpoch();
    /*58e110 calls the shared6314d0 inverse before its Wind Walk cleanup.
     * Release may synchronously dispatch Attack recovery callbacks. */
    if(status->invisibility_owned)S_TemporaryInvisibilityRelease(unit,status);
    else if(!S_UnitHasTemporaryInvisibility(unit,status))G_SetEntityHidden(unit,false);
    if(epoch!=S_SpellPreparationEpoch() || !unit->inuse || unit->spawn_time!=birth || G_IsDeferredFree(unit))return;
    S_RemoveMoveSpeedContribution(unit,status);
    if(code)S_SpellStartCooldown(unit,code,rank);
    G_InvalidateUnitInfoPanel(unit);
}

static void wind_walk_end(edict_t *unit) {
    heroabilitystatus_t *status = wind_walk_status(unit);
    if (!status) return;
    unit_removestatus(unit, status, STATUS_REMOVE_SCRIPT);
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
    if (!has_slot || S_SpellDuration(call->item->code, level, G_UnitIsHero(unit)) <= 0.0f) return false;
    return CAbilitySimpleSpell(unit, A_VALIDATE, call) != 0;
}

static void wind_walk_execute(edict_t *unit, abilityitem_t const *spell) {
    uint32_t rank=S_SpellLevel(unit,spell->code);
    /*58ce50 ->59daa0 ->63ce40: DataA is the shared fade, not speed.
     * BOwk is concrete; the first authored BuffID remains presentation. */
    uint32_t const birth=unit->spawn_time;
    uint64_t const epoch=S_SpellPreparationEpoch();
    if(!S_ApplyTypedTemporaryInvisibility(unit,unit,spell->code,rank,
            S_SpellBuffId(spell->code,rank),WW_ID_BOWK,
            S_SpellDuration(spell->code,rank,G_UnitIsHero(unit)),S_SpellData(spell->code,rank,1)))
        fprintf(stderr,"WC3 Wind Walk: failed to create status for unit %u\n",unit->s.number);
    if(epoch!=S_SpellPreparationEpoch() || !unit->inuse || unit->spawn_time!=birth || G_IsDeferredFree(unit))return;
    heroabilitystatus_t *status=wind_walk_status(unit);
    if(status && status->data==spell->code && status->invisibility_owned)
        S_UpdateMoveSpeedContribution(unit,status,S_SpellData(spell->code,rank,2));
}

BZ_ABILITY_PROC(CAbilityWindWalk) {
    heroabilitystatus_t *status;

    switch (msg) {
    case A_CAST_PROFILE:
        if(!call || !call->item || !call->cast_profile)return false;
        S_DefaultSpellCastProfile(ent,call->item->code,call->cast_profile);return true;
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
