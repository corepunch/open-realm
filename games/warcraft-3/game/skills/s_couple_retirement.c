#include "s_skills.h"

/* Acoi's 4ca180 retires world presence before creating the result. Life-zero
 * and death notifications follow only after successful creation. These inputs
 * keep their original identity until the authored UnitData.death deadline. */
static bool couple_input_current(edict_t const *unit,uint32_t spawn) {
    return unit && unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit);
}

bool S_RetireCoupleInputWorld(edict_t *unit) {
    if(!unit || !unit->inuse || G_IsDeferredFree(unit) || M_IsDead(unit))return false;
    uint32_t const spawn=unit->spawn_time;
    if(!unit->couple_instant)unit->couple_instant=G_AllocCoupleInstant();
    if(!unit->couple_instant)return false;
    if(unit->couple_instant->retiring)return true;
    /* Publish absence before synchronous TargetLost. This is a merge-owned
     * lifetime state, independent of reversible ShowUnit or invisibility. */
    unit->couple_instant->retiring=true;
    G_SetEntityHidden(unit,true);
    G_UnpublishMoveSpatialObject(unit);
    if(unit->s.flags&EF_FOW_BLOCKER)G_FowMarkBlockersDirty();
    G_InvalidateUnitShortcutsForUnit(unit);
    G_InvalidateCommands(G_GetPlayerClientByNumber(unit->s.player));
    S_UnitTargetLost(unit);
    if(!couple_input_current(unit,spawn))return false;
    unit_removebuffs(unit,true,true,false,false,false,true,false);
    return couple_input_current(unit,spawn) && unit->couple_instant &&
        unit->couple_instant->retiring && !M_IsDead(unit);
}

void S_CompleteCoupleInputRetirement(edict_t *unit) {
    if(!unit || !unit->inuse || G_IsDeferredFree(unit) || M_IsDead(unit) ||
       !unit->couple_instant || !unit->couple_instant->retiring)return;
    uint32_t const spawn=unit->spawn_time;
    wc3Clock_t deadline=G_TimerQueryClock(NULL);
    float const delay=unit->data.UnitData ? unit->data.UnitData->death : 0.0f;
    deadline.time=wc3_add(deadline.time,MAX(0.0f,delay));

    G_SetHealth(unit,0.0f);
    if(!couple_input_current(unit,spawn))return;
    unit->svflags|=SVF_DEADMONSTER;
    G_DispatchTargetSubscriptions(unit,A_DEATH);
    if(!couple_input_current(unit,spawn))return;
    /* The dying Hippo's own Acoi inverse stops its still-live Archer. Keep
     * that retained relation until this delivery, before public Hippo death. */
    S_UnitAbilityEvent(unit,A_DEATH);
    if(!couple_input_current(unit,spawn))return;

    G_ClearActiveUnitOrder(unit);
    if(!couple_input_current(unit,spawn))return;
    G_ClearUnitOrderQueue(unit);
    if(!couple_input_current(unit,spawn))return;
    unit->current_order_id=0;
    S_SpellReleasePointApproach(unit);
    if(!couple_input_current(unit,spawn))return;
    if(G_ActorHasAbilityCode(unit,MAKEFOURCC('A','m','o','v')))
        S_StopUnitMovementWithRecovery(unit);
    if(!couple_input_current(unit,spawn))return;
    M_SetMove(unit,NULL);
    if(!couple_input_current(unit,spawn))return;
    G_ClearUnitFood(unit);
    G_SetEntitySelectionMask(unit,0);
    unit->s.flags|=EF_NOT_SELECTABLE;
    G_InvalidateUnitShortcutsForUnit(unit);
    G_UnpublishMoveSpatialObject(unit);

    /* This dispatcher is synchronous; false selects normal player/unit
     * discovery, not deferred event delivery. No combat rewards or corpse. */
    G_DispatchUnitEventFamilies(&(gameEventPointParams_t){
        .edict=unit,.type=EVENT_PLAYER_UNIT_DEATH,.source=NULL},EVENT_UNIT_DEATH,false);
    if(!couple_input_current(unit,spawn))return;
    /* 69bef0 reads raw-type UnitData.death; hidden originals omit flesh/bone
     * decay. Existing serialized release ownership stores this absolute clock. */
    G_QueueEdictReleaseDeadline(unit,&deadline);
}
