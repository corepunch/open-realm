#include "s_skills.h"

#define ID_ENDURANCE MAKEFOURCC('A','O','a','e')

typedef struct { edict_t *unit; uint32_t spawn; } enduranceSource_t;
static enduranceSource_t endurance_sources[MAX_ENTITIES];
static uint32_t endurance_count, endurance_generation;
static bool endurance_dirty=true;

void S_InvalidateEnduranceSources(void) { endurance_dirty=true; }

/* Discovery follows ability ownership changes, not speed queries. Keep the
 * original edict order; every consumer rechecks live rank and eligibility. */
static void endurance_prepare(void) {
    uint32_t generation=G_AbilityDataGeneration();
    if(!endurance_dirty && endurance_generation==generation)return;
    endurance_count=0;
    FILTER_EDICTS(unit,unit->inuse) {
        if(G_UnitAbilityLevel(unit,ID_ENDURANCE))
            endurance_sources[endurance_count++]=(enduranceSource_t){unit,unit->spawn_time};
    }
    endurance_generation=generation;endurance_dirty=false;
}

static abilityLevel_t const *endurance_level(enduranceSource_t const *source,edict_t const *target) {
    edict_t *unit=source->unit;
    if(!unit->inuse || unit->spawn_time!=source->spawn || !S_AuraUnitActive(unit) ||
        !S_SpellIsFriend(unit,(edict_t *)target))return NULL;
    uint32_t rank=G_UnitAbilityLevel(unit,ID_ENDURANCE);
    if(!rank)return NULL;
    abilityLevel_t const *row=G_AbilityLevel(ID_ENDURANCE,rank);
    return Vector2_distance(&unit->s.origin2,&target->s.origin2)<=row->area ? row : NULL;
}

float S_ApplyEnduranceMoveSpeed(edict_t *unit,float speed) {
    if(!S_AuraUnitActive(unit))return speed;
    endurance_prepare();
    FOR_LOOP(i,endurance_count) {
        abilityLevel_t const *row=endurance_level(endurance_sources+i,unit);
        if(row)speed*=1.0f+row->data[0].number*0.01f;
    }
    return speed;
}

float S_ApplyEnduranceAttackBonus(edict_t *unit,float bonus) {
    if(!S_AuraUnitActive(unit))return bonus;
    endurance_prepare();
    FOR_LOOP(i,endurance_count) {
        abilityLevel_t const *row=endurance_level(endurance_sources+i,unit);
        if(row)bonus+=row->data[1].number*0.01f;
    }
    return bonus;
}

BZ_ABILITY_PROC(CAbilityEnduranceAura) {
    if(msg==A_ENABLE || msg==A_DISABLE || msg==A_UNIT_INIT || msg==A_UNIT_TYPE_CHANGED)
        S_InvalidateEnduranceSources();
    return CAbilityPassive(ent,msg,call);
}
