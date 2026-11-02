#include "s_skills.h"

/* Derived, ordered membership: no timer tick walks unrelated map entities.
 * Live iteration admits later slots scheduled by an earlier callback, exactly
 * as the original ascending edict scan did. The scalar deadline remains saved
 * on the unit; this index carries no authoritative state. */
static entitySet_t chaos_timers;
#ifdef BZ_TESTS
static unsigned chaos_timer_visits;
#endif

static bool chaos_pending(edict_t const *unit) {
    return unit->inuse && unit->chaos.code && unit->chaos.phase && unit->chaos.phase!=3;
}

static void chaos_track(edict_t const *unit) {
    uintptr_t index=((uintptr_t)unit-(uintptr_t)g_edicts)/sizeof(*unit);
    if(g_edicts && index<MAX_ENTITIES)
        entity_set_put(&chaos_timers,index,chaos_pending(unit));
}

/* Public addition first delivers the enabled message, then original4d8dc0
 * schedules commit with scalar3c23d70a. Both stages retain the primary clock. */
static void chaos_schedule(edict_t *unit, uint32_t code, uint32_t phase) {
    unit->chaos.code=code;unit->chaos.phase=phase;
    unit->chaos.deadline=level.pathing_clock;
    /* Original4d8dc0 requests scalar3c23d70a. Observed delivery is two
     * primary advances, whose truncated sum can differ from one .01 add.
     * TODO NUM-02.9: recover general timer deadline arithmetic; this retained
     * cursor covers the public periodic Chaos producers, not arbitrary timers. */
    FOR_LOOP(i,2) wc3_clock_advance(&unit->chaos.deadline,wc3_float(0x3ba3d70a),0);
    chaos_track(unit);
}

static bool chaos_due(edict_t const *unit) {
    wc3Clock_t const *now=&level.pathing_clock,*due=&unit->chaos.deadline;
    return now->epoch==due->epoch ? now->time>=due->time : (int32_t)(now->epoch-due->epoch)>0;
}
BZ_ABILITY_PROC(CAbilityChaos) {
    if (msg == A_UNIT_TYPE_UPDATE && ent) return 0;
    if (msg == A_UNIT_TYPE_UPDATE) return S_UnitTypeHasAbilityCode(call->unit_type, MAKEFOURCC('A','c','h','a')) ? UNIT_UPDATE_RUN : UNIT_UPDATE_SKIP;
    abilityAliasRef_t ability;
    uint32_t target;

    if ((msg==A_TIMERS_RESET || msg==A_TIMERS_REBUILD) && ent) return 0;
    if (msg==A_TIMERS_RESET) {
        chaos_timers=(entitySet_t){0};return true;
    }
    if (msg==A_TIMERS_REBUILD) {
        chaos_timers=(entitySet_t){0};
        FOR_LOOP(i,globals.num_edicts) chaos_track(g_edicts+i);
        return true;
    }
    if (msg==A_PRIMARY_TIMER) {
        for(uint32_t i=entity_set_next(&chaos_timers,0);i<globals.num_edicts;i=entity_set_next(&chaos_timers,i+1)) {
            edict_t *unit=g_edicts+i;
#ifdef BZ_TESTS
            chaos_timer_visits++;
#endif
            if(chaos_pending(unit)) CAbilityChaos(unit,A_UPDATE,NULL);
            /* Removal/reuse may clear the edict without an ability-disable
             * notification. Retire that stale membership on this visit. */
            chaos_track(unit);
        }
        return true;
    }
    if (ent && msg==A_ENABLE && call && call->item) {
        chaos_schedule(ent,call->item->code,1);return true;
    }
    if (ent && msg==A_DISABLE && call && call->item && call->item->code==ent->chaos.code) {
        ent->chaos=(typeof(ent->chaos)){0};chaos_track(ent);return true;
    }
    if (ent && msg==A_REQUIREMENTS_CHANGED && ent->chaos.phase==3) {
        ability=S_ResolveAbilityAlias(ent,MAKEFOURCC('A','c','h','a'));
        if (ability.alias && G_UnitAbilityResearchAvailable(ent,ability.alias) && G_AbilityRequirementsSatisfied(ent,ability.alias)) {
            /* Original80261 is synchronous with the research publisher, then
             *4d8d40 queues the same independent ten-ms commit timer. */
            chaos_schedule(ent,ability.alias,2);return true;
        }
        return false;
    }
    if (msg != A_UPDATE || !ent || !ent->inuse || M_IsDead(ent))
        return CAbilityPassive(ent, msg, call);
    ability = S_ResolveAbilityAlias(ent, MAKEFOURCC('A','c','h','a'));
    if (!ability.alias) return false;
    if (ent->chaos.code!=ability.alias || !ent->chaos.phase) {
        chaos_schedule(ent,ability.alias,1);return false;
    }
    if (ent->chaos.phase==3) {
        if (!G_UnitAbilityResearchAvailable(ent,ability.alias) || !G_AbilityRequirementsSatisfied(ent,ability.alias))return false;
        chaos_schedule(ent,ability.alias,1);return false;
    }
    if (!chaos_due(ent))return false;
    if (ent->chaos.phase==1) {
        if (!G_UnitAbilityResearchAvailable(ent,ability.alias) || !G_AbilityRequirementsSatisfied(ent,ability.alias)) {ent->chaos.phase=3;chaos_track(ent);return false;}
        chaos_schedule(ent,ability.alias,2);return false;
    }
    target = G_AbilityLevel(ability.alias, ability.level)->unitID;
    if (!target) {
        fprintf(stderr, "Chaos: missing UnitID for %.4s on unit %u\n", (cstring_t)&ability.alias, ent->s.number);
        return false;
    }
    if (ent->class_id == target) { G_ActorRemoveSkill(ent, ability.alias); return true; }
    if (!G_TransformUnitType(ent, target)) {
        fprintf(stderr, "Chaos: cannot transform unit %u from %.4s to %.4s for %.4s\n",
                ent->s.number, (cstring_t)&ent->class_id, (cstring_t)&target, (cstring_t)&ability.alias);
        return false;
    }
    G_ActorRemoveSkill(ent, ability.alias);
    return true;
}
