#include "s_skills.h"
#include "../g_entity_set.h"

#define ID_ENDURANCE MAKEFOURCC('A','O','a','e')

typedef struct { edict_t *unit; uint32_t spawn; } enduranceSource_t;
static enduranceSource_t endurance_sources[MAX_ENTITIES];
static uint32_t endurance_generation;
static entitySet_t endurance_members, endurance_changed;
#ifdef BZ_TESTS
static uint32_t test_endurance_discovery_visits;
#endif
static bool endurance_dirty=true, endurance_pending;

void S_InvalidateEnduranceSources(void) { endurance_dirty=true; }

/* Local ownership edits retain all other prepared providers. */
void S_MarkEnduranceSource(edict_t const *unit) {
    uintptr_t offset = (uintptr_t)unit - (uintptr_t)g_edicts;
    if (!unit || !g_edicts || offset >= sizeof(*unit) * MAX_ENTITIES || offset % sizeof(*unit)) {
        S_InvalidateEnduranceSources();
        return;
    }
    entity_set_put(&endurance_changed, offset / sizeof(*unit), true);
    endurance_pending = true;
}

static void endurance_discover(uint32_t index) {
    edict_t *unit = g_edicts + index;
    bool present = unit->inuse && G_UnitAbilityLevel(unit, ID_ENDURANCE);
#ifdef BZ_TESTS
    test_endurance_discovery_visits++;
#endif
    entity_set_put(&endurance_members, index, present);
    if (present) endurance_sources[index] = (enduranceSource_t){unit, unit->spawn_time};
}

/* Ordered bitsets preserve multiplication order without an O(N) discovery or
 * ordered-array insertion for every newly created provider. */
static void endurance_prepare(void) {
    uint32_t generation = G_AbilityDataGeneration();
    if (!endurance_dirty && !endurance_pending && endurance_generation == generation) return;
    if (endurance_dirty || endurance_generation != generation) {
        memset(&endurance_members, 0, sizeof(endurance_members));
        FOR_LOOP(i, globals.num_edicts) endurance_discover(i);
        memset(&endurance_changed, 0, sizeof(endurance_changed));
    } else {
        for (uint32_t i = entity_set_next(&endurance_changed, 0); i < MAX_ENTITIES;
             i = entity_set_next(&endurance_changed, i + 1)) {
            entity_set_put(&endurance_changed, i, false);
            endurance_discover(i);
        }
    }
    endurance_generation = generation;
    endurance_dirty = endurance_pending = false;
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
    for (uint32_t i = entity_set_next(&endurance_members, 0); i < MAX_ENTITIES;
         i = entity_set_next(&endurance_members, i + 1)) {
        abilityLevel_t const *row=endurance_level(endurance_sources+i,unit);
        if(row)speed*=1.0f+row->data[0].number*0.01f;
    }
    return speed;
}

float S_ApplyEnduranceAttackBonus(edict_t *unit,float bonus) {
    if(!S_AuraUnitActive(unit))return bonus;
    endurance_prepare();
    for (uint32_t i = entity_set_next(&endurance_members, 0); i < MAX_ENTITIES;
         i = entity_set_next(&endurance_members, i + 1)) {
        abilityLevel_t const *row=endurance_level(endurance_sources+i,unit);
        if(row)bonus+=row->data[1].number*0.01f;
    }
    return bonus;
}

BZ_ABILITY_PROC(CAbilityEnduranceAura) {
    if(msg==A_ENABLE || msg==A_DISABLE || msg==A_UNIT_INIT || msg==A_UNIT_TYPE_CHANGED)
        S_MarkEnduranceSource(ent);
    return CAbilityPassive(ent,msg,call);
}

#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
TEST(wc3_ability_dispatch, endurance_local_discovery_preserves_ordered_multiplication) {
    reset_entities(); setup_test_world();
    AbilityData_t ability = {.id = ID_ENDURANCE, .code = ID_ENDURANCE};
    ability.level[0].area = 500; ability.level[0].data[0].number = .17f;
    ability.level[1] = ability.level[0]; ability.level[1].data[0].number = .23f;
    slkTestData_t rows = {.rows = &ability, .count = 1};
    slkTestData_t *old = G_SetSLKRows("AbilityData", &rows);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    FOR_LOOP(i, 4096) alloc_test_unit(MAKEFOURCC('h','f','o','o'), 2000, 2000);
    T_FEQ(S_ApplyEnduranceMoveSpeed(target, 123.45f), 123.45f, 0);
    test_endurance_discovery_visits = 0;
    edict_t *source[64];
    FOR_LOOP(i, 64) {
        source[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
        source[i]->s.player = 0;
        T_ASSERT(G_ActorAddSkill(source[i], ID_ENDURANCE));
        G_UnitSetAbilityLevel(source[i], ID_ENDURANCE, 1 + i % 2);
        S_ApplyEnduranceMoveSpeed(target, 123.45f);
    }
    T_EQ(test_endurance_discovery_visits, 64);
    float ordered = S_ApplyEnduranceMoveSpeed(target, 123.45f);
    S_InvalidateEnduranceSources();
    T_FEQ(S_ApplyEnduranceMoveSpeed(target, 123.45f), ordered, 0);
    FOR_LOOP(i, 64) {
        G_FreeEdict(source[i]);
        float changed = S_ApplyEnduranceMoveSpeed(target, 123.45f);
        S_InvalidateEnduranceSources();
        T_FEQ(S_ApplyEnduranceMoveSpeed(target, 123.45f), changed, 0);
    }
    T_FEQ(S_ApplyEnduranceMoveSpeed(target, 123.45f), 123.45f, 0);
    G_SetSLKRows("AbilityData", old); free(old);
    reset_entities(); setup_test_world();
}
#endif
