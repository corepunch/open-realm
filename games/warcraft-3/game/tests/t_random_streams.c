#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "fixtures/retail_purpose_random_172.h"

extern void reset_entities(void);
extern void setup_test_world(void);
extern bool run_test_jass(char const *);

static void streams172_check(wc3Random_t const *expected) {
    FOR_LOOP(i,45) {
        T_EQ(level.purpose_random[i].sum,expected[i].sum);
        T_EQ(level.purpose_random[i].index,expected[i].index);
    }
}
static uint32_t streams172_result(unsigned i) {
    uint32_t value;memcpy(&value,&level.hashtables[0].entries[i].value,sizeof(value));return value;
}
TEST(wc3_random_streams, startup_seeds_every_purpose_independently_of_race_draws) {
    reset_entities();setup_test_world();
    FOR_LOOP(k,sizeof(retail_streams172)/sizeof(*retail_streams172)) {
        retailStreams172_t const *s=retail_streams172+k;
        FOR_LOOP(p,12)game.clients[p].jass.race_pref=1;
        level.setup.map_flags=0;level.setup.random_seed=s->seed;
        G_InitMapRandom();streams172_check(s->streams);
        FOR_LOOP(p,12)game.clients[p].jass.race_pref=32;
        G_InitMapRandom();streams172_check(s->streams);
    }
    FOR_LOOP(p,12)game.clients[p].jass.race_pref=1;
    level.setup.map_flags=0x8000u;G_InitMapRandom();streams172_check(retail_streams172[0].streams);
}
TEST(wc3_random_streams, purpose_helpers_match_original_results_and_positions) {
    FOR_LOOP(i,sizeof(retail_draws172)/sizeof(*retail_draws172)) {
        retailDraw172_t const *s=retail_draws172+i;
        memcpy(level.purpose_random,retail_streams172[0].streams,sizeof(level.purpose_random));
        level.purpose_random[s->index]=s->before;
        wc3Random_t owner=level.pathing_random;
        uint32_t result=s->real ? wc3_float_bits(wc3_random_unit(level.purpose_random+s->index)) :
            wc3_random_range(level.purpose_random+s->index,s->span);
        T_EQ(result,s->result);
        T_EQ(level.purpose_random[s->index].sum,s->after.sum);
        T_EQ(level.purpose_random[s->index].index,s->after.index);
        FOR_LOOP(k,45)if(k!=s->index) {
            T_EQ(level.purpose_random[k].sum,retail_streams172[0].streams[k].sum);
            T_EQ(level.purpose_random[k].index,retail_streams172[0].streams[k].index);
        }
        T_EQ(level.pathing_random.sum,owner.sum);T_EQ(level.pathing_random.index,owner.index);
    }
    /* Unlike GetRandomInt's equal-bounds shortcut, the indexed helper burns
     * exactly one word even for singleton or empty numeric spans. */
    FOR_LOOP(span,2) {
        wc3Random_t state=retail_streams172[0].streams[2],expected=state;
        wc3_random_next(&expected);T_EQ(wc3_random_range(&state,span),0u);
        T_EQ(state.sum,expected.sum);T_EQ(state.index,expected.index);
    }
}
TEST(wc3_random_streams, public_reseed_executes_original_tail_without_libc_side_effect) {
    reset_entities();setup_test_world();
    FOR_LOOP(k,sizeof(retail_reseed172)/sizeof(*retail_reseed172)) {
        retailReseed172_t const *s=retail_reseed172+k;
        char script[160];
        snprintf(script,sizeof(script),"function main takes nothing returns nothing\ncall SetRandomSeed(%d)\nendfunction\n",(int32_t)s->seed);
        srand(98251);int next=rand();srand(98251);
        T_ASSERT(run_test_jass(script));T_EQ(rand(),next);
        T_EQ(level.pathing_random.sum,s->owner.sum);T_EQ(level.pathing_random.index,s->owner.index);
        streams172_check(s->streams);
    }
}
TEST(wc3_random_streams, all_purposes_and_owner_resume_after_save) {
    reset_entities();setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\ncall SetRandomSeed(12345)\nendfunction\n"));
    wc3Random_t saved[45];uint32_t next[45];
    FOR_LOOP(i,45)FOR_LOOP(n,i+1)wc3_random_next(level.purpose_random+i);
    memcpy(saved,level.purpose_random,sizeof(saved));wc3Random_t owner=level.pathing_random;
    cstring_t file="/tmp/openrealm-purpose172.bin";T_ASSERT(WriteGame(file));
    FOR_LOOP(i,45)next[i]=wc3_random_next(level.purpose_random+i);
    wc3_random_next(&level.pathing_random);
    T_ASSERT(ReadGame(file));remove(file);streams172_check(saved);
    T_EQ(level.pathing_random.sum,owner.sum);T_EQ(level.pathing_random.index,owner.index);
    FOR_LOOP(i,45)T_EQ(wc3_random_next(level.purpose_random+i),next[i]);
    reset_entities();setup_test_world();
}
TEST(wc3_random_streams, public_item_choices_only_advance_their_owned_stream) {
    reset_entities();setup_test_world();G_ClearHashtableRegistry();
    memcpy(level.purpose_random,retail_streams172[0].streams,sizeof(level.purpose_random));
    wc3Random_t owner=level.pathing_random;
    uint32_t count,candidates=0;ItemData_t const *items=G_ItemDataRows(&count);
    FOR_LOOP(i,count)if(items[i].pickRandom && items[i].level==1)candidates++;
    T_EQ(candidates,2u);
    T_ASSERT(run_test_jass("globals\nhashtable result\nendglobals\n"
        "function next takes nothing returns nothing\n"
        "call SaveInteger(result,0,0,ChooseRandomItem(1))\n"
        "call SaveInteger(result,0,1,ChooseRandomItemEx(ITEM_TYPE_PERMANENT,1))\n"
        "call BJassAssert(ChooseRandomItem(999)==0,\"empty pool\")\nendfunction\n"
        "function main takes nothing returns nothing\nset result=InitHashtable()\ncall next()\nendfunction\n"));
    T_EQ(level.hashtables[0].num_entries,2u);
    FOR_LOOP(i,2)T_EQ(streams172_result(i),MAKEFOURCC('r','d','e','2'));
    T_EQ(level.purpose_random[35].sum,2356487499u);T_EQ(level.purpose_random[35].index,1615352040u);
    FOR_LOOP(i,45)if(i!=35) {
        T_EQ(level.purpose_random[i].sum,retail_streams172[0].streams[i].sum);
        T_EQ(level.purpose_random[i].index,retail_streams172[0].streams[i].index);
    }
    T_EQ(level.pathing_random.sum,owner.sum);T_EQ(level.pathing_random.index,owner.index);
    cstring_t file="/tmp/openrealm-item-purpose172.bin";T_ASSERT(WriteGame(file));
    jass_callbyname(level.vm,"next",false);
    uint32_t expected[2];FOR_LOOP(i,2)expected[i]=streams172_result(i);
    wc3Random_t after=level.purpose_random[35];
    T_ASSERT(ReadGame(file));remove(file);jass_callbyname(level.vm,"next",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    FOR_LOOP(i,2)T_EQ(streams172_result(i),expected[i]);
    T_EQ(level.purpose_random[35].sum,after.sum);T_EQ(level.purpose_random[35].index,after.index);
    reset_entities();setup_test_world();G_ClearHashtableRegistry();
}
#endif
