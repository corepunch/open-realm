#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);

/* Frozen304 native scenes2/3: every attached owner's3c/40 becomes0/0
 * before RemoveUnit returns, despite the already-pending removal lifetime. */
TEST(wc3_movement,pause379_removal_releases_every_scripted_owner_before_return_and_save) {
    FOR_LOOP(saved,2) {
        reset_entities();setup_test_world();
        uint8_t cells[64*64]={0};
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        T_ASSERT(run_test_jass("globals\nunit u=null\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set u=CreateUnit(Player(0),'hfoo',656,656,0)\n"
            "call SetUnitAcquireRange(u,0)\ncall PauseUnit(u,true)\nendfunction\n"
            "function issue takes nothing returns nothing\ncall RemoveUnit(u)\nendfunction\n"));
        edict_t *unit=NULL;
        FILTER_EDICTS(e,e->inuse && e->class_id==MAKEFOURCC('h','f','o','o'))unit=e;
        T_NOT_NULL(unit);if(!unit)continue;
        uint32_t number=unit-g_edicts,owners=0;bool move=false;
        T_EQ(unit->ability_pause_depth,1);
        for(unitAbilityDisable_t *state=unit->ability_disables;state;state=state->next) {
            owners++;move|=state->code==MAKEFOURCC('A','m','o','v');
            T_ASSERT(state->linked);T_ASSERT(state->pause_owned);
            T_EQ(state->depth,1);T_EQ(state->order_depth,1);
        }
        T_ASSERT(owners>1);T_ASSERT(move);
        if(saved) {
            cstring_t file=Test_TempPath("pause379-paused.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
        }
        jass_callbyname(level.vm,"issue",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(G_IsDeferredFree(unit));T_ASSERT(!M_IsDead(unit));
        T_EQ(unit->ability_pause_depth,0);T_EQ(unit->current_order_id,0);
        uint32_t after=0;
        for(unitAbilityDisable_t *state=unit->ability_disables;state;state=state->next) {
            after++;T_ASSERT(!state->pause_owned);T_EQ(state->depth,0);T_EQ(state->order_depth,0);
        }
        T_EQ(after,owners);
        if(saved) {
            cstring_t file=Test_TempPath("pause379-removed.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+number;
            T_ASSERT(G_IsDeferredFree(unit));T_EQ(unit->ability_pause_depth,0);
            for(unitAbilityDisable_t *state=unit->ability_disables;state;state=state->next) {
                T_ASSERT(!state->pause_owned);T_EQ(state->depth,0);T_EQ(state->order_depth,0);
            }
        }
        reset_entities();
    }
    setup_test_world();
}

/* Engine lifetime protection for the resumed-owner callback: the inverse
 * holds prefetched records but must never continue against another incarnation.
 * This synthetic callback is not claimed as an additional public retail trace. */
static abilityProc_t pause379_original;
static edict_t *pause379_source,*pause379_replacement;
static uint32_t pause379_callbacks;
static bool pause379_reuse;
static intptr_t pause379_owner(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    if(msg==A_UNIT_EVENT_MASK)return UNIT_MESSAGE_SUBSCRIPTIONS(A_UNIT_SUSPEND,A_UNIT_RESUME);
    if(msg==A_UNIT_RESUME && unit==pause379_source) {
        pause379_source=NULL;pause379_callbacks++;
        vec2_t position=unit->s.origin2;G_FreeEdict(unit);
        if(pause379_reuse) {
            /* G_Spawn quarantines freed storage for1000ms. Use the same real
             * allocator lifecycle as the existing unit-release reuse test. */
            level.time+=2000;
            pause379_replacement=unit_create(0,MAKEFOURCC('h','f','o','o'),&position,0);
        }
        return true;
    }
    return pause379_original(unit,msg,call);
}
TEST(wc3_movement,pause379_owner_inverse_keeps_callback_lifetime_and_incarnation_guards) {
    FOR_LOOP(reuse,2) {
        reset_entities();setup_test_world();
        edict_t *unit=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){656,656},0);
        T_NOT_NULL(unit);if(!unit)continue;
        S_AcquireScriptedAbilityDisables(unit);T_EQ(unit->ability_pause_depth,1);
        T_NOT_NULL(unit->ability_disables);if(!unit->ability_disables)continue;
        abilityitem_t item=S_AbilityItem(unit->ability_disables->code);T_NOT_NULL(item.ability);
        if(!item.ability)continue;
        pause379_original=item.ability->proc;pause379_source=unit;pause379_replacement=NULL;
        pause379_callbacks=0;pause379_reuse=reuse;
        uint32_t birth=unit->spawn_time;
        S_ReplaceAbilityProcedure(item.ability,pause379_owner);
        S_ReleaseScriptedAbilityDisables(unit);
        S_ReplaceAbilityProcedure(item.ability,pause379_original);
        T_EQ(pause379_callbacks,1);
        if(reuse) {
            T_NOT_NULL(pause379_replacement);
            T_ASSERT(pause379_replacement==unit);
            T_ASSERT(unit->spawn_time!=birth);T_ASSERT(unit->inuse);T_ASSERT(!M_IsDead(unit));
            T_EQ(unit->ability_pause_depth,0);
            for(unitAbilityDisable_t *state=unit->ability_disables;state;state=state->next) {
                T_ASSERT(!state->pause_owned);T_EQ(state->depth,0);T_EQ(state->order_depth,0);
            }
        } else T_ASSERT(!unit->inuse);
        pause379_source=NULL;pause379_replacement=NULL;
    }
    reset_entities();setup_test_world();
}
#endif
