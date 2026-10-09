#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_records.h"
#include "retail_repulsion_overlap.h"
extern void reset_entities(void),setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t,float,float);
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);

static edict_t *overlap_members[20];
static unsigned overlap_visit_cursor,overlap_pair_cursor,overlap_draws;
static bool overlap_endpoint_seen;

static void overlap_endpoint_check(edict_t const *unit,float const endpoint[2],bool attempted,bool admitted) {
    typeof(overlap_visits[0]) const *row=overlap_visits+overlap_visit_cursor;
    T_EQ(unit,overlap_members[row->unit-27]);T_ASSERT(!overlap_endpoint_seen);overlap_endpoint_seen=true;
    T_EQ(attempted,row->admission>=0);
    if(attempted) {T_EQ(wc3_float_bits(endpoint[0]),row->endpoint[0]);T_EQ(wc3_float_bits(endpoint[1]),row->endpoint[1]);T_EQ(admitted,row->admission==1);}
}

static void overlap_pair_check(edict_t const *source,edict_t const *candidate,wc3RepulsePair_t const *pair,
    wc3Repulse_t const *before,wc3Random_t random_before) {
    T_ASSERT(overlap_pair_cursor<sizeof(overlap_pairs)/sizeof(*overlap_pairs));
    if(overlap_pair_cursor>=sizeof(overlap_pairs)/sizeof(*overlap_pairs))return;
    uint32_t const *row=overlap_pairs[overlap_pair_cursor++];
    T_EQ(source,overlap_members[row[0]-27]);T_EQ(candidate,overlap_members[row[1]-27]);
    uint32_t actual[]={wc3_float_bits(pair->source[0]),wc3_float_bits(pair->source[1]),
        wc3_float_bits(pair->other[0]),wc3_float_bits(pair->other[1]),
        wc3_float_bits(before->vector[0]),wc3_float_bits(before->vector[1]),
        wc3_float_bits(source->movement.repulse.state.vector[0]),wc3_float_bits(source->movement.repulse.state.vector[1]),
        random_before.sum,random_before.index,level.pathing_random.sum,level.pathing_random.index};
    FOR_LOOP(i,12)T_EQ(actual[i],row[i+2]);
    if(random_before.sum!=level.pathing_random.sum || random_before.index!=level.pathing_random.index)overlap_draws++;
}

static void overlap_visit_check(edict_t *unit,moveRepulseTrace_t const *before) {
    T_ASSERT(overlap_visit_cursor<sizeof(overlap_visits)/sizeof(*overlap_visits));
    if(overlap_visit_cursor>=sizeof(overlap_visits)/sizeof(*overlap_visits))return;
    typeof(overlap_visits[0]) const *row=overlap_visits+overlap_visit_cursor++;
    T_EQ(unit,overlap_members[row->unit-27]);
    uint32_t previous[]={wc3_float_bits(before->point.x),wc3_float_bits(before->point.y),
        wc3_float_bits(before->state.vector[0]),wc3_float_bits(before->state.vector[1]),before->state.packed};
    uint32_t actual[]={wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),unit->movement.repulse.state.packed};
    FOR_LOOP(i,5) {T_EQ(previous[i],row->before[i]);T_EQ(actual[i],row->after[i]);}
    T_EQ(before->owner.sum,row->random_before[0]);T_EQ(before->owner.index,row->random_before[1]);
    T_EQ(level.pathing_random.sum,row->random_after[0]);T_EQ(level.pathing_random.index,row->random_after[1]);
    wc3FineBox_t box=G_GetMoveSpatialObject(unit-g_edicts)->box;
    int rectangle[]={box.min.y,box.min.x,box.max.y,box.max.x};FOR_LOOP(i,4)T_EQ(rectangle[i],row->box[i]);
    T_EQ(overlap_endpoint_seen,row->admission!=-2);overlap_endpoint_seen=false;
}

/* The production owner visits alternating intrusive-list entries; expected
 * identities/order are outputs here, never supplied to the scheduler. */
TEST(wc3_repulsion_overlap, complete_owner_passes_match_retained_overlap_capture) {
    reset_entities();setup_test_world();uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    UnitBalance_t balance[20];UnitData_t data={.moveTypeName="foot"};
    FOR_LOOP(i,20) {
        typeof(overlap_units[0]) const *input=overlap_units+i;
        balance[i]=(UnitBalance_t){.speed=220,.repulse=input->enabled,.repulseParam=input->selector,.repulsePrio=input->rank};
        edict_t *unit=overlap_members[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
        unit->data.UnitBalance=balance+i;unit->data.UnitData=&data;unit->s.player=input->owner;unit->collision=input->radius;
        unit->svflags|=SVF_MONSTER;unit->s.model=1;unit->stand=unit_stand;unit->movement.pose_valid=true;
        unit->movement.fine_pose=(vec2_t){wc3_float(input->x),wc3_float(input->y)};
        unit->s.origin2=unit->movement.pose_world=(vec2_t){wc3_mul(unit->movement.fine_pose.x,32),wc3_mul(unit->movement.fine_pose.y,32)};
        CAbilityMove(unit,A_UNIT_INIT,NULL);unit_stand(unit);gi.LinkEntity(unit);G_PublishMoveSpatialObject(unit);
    }
    level.pathing_random=(wc3Random_t){.sum=4273436052u,.index=209508436u};
    level.repulse_phase=1;overlap_visit_cursor=overlap_pair_cursor=overlap_draws=0;overlap_endpoint_seen=false;
    move_test_repulse=overlap_visit_check;move_test_repulse_pair=overlap_pair_check;move_test_repulse_endpoint=overlap_endpoint_check;
    unsigned end=overlap_visits[sizeof(overlap_visits)/sizeof(*overlap_visits)-1].visit;
    for(unsigned visit=overlap_visits[0].visit;visit<=end;visit++) {
        unsigned first=overlap_visit_cursor;
        CAbilityMove(NULL,A_OWNER_UPDATE,NULL);
        for(unsigned i=first;i<overlap_visit_cursor;i++)T_EQ(overlap_visits[i].visit,visit);
    }
    T_EQ(overlap_visit_cursor,sizeof(overlap_visits)/sizeof(*overlap_visits));
    T_EQ(overlap_pair_cursor,sizeof(overlap_pairs)/sizeof(*overlap_pairs));T_EQ(overlap_draws,1476);
    T_EQ(level.pathing_random.sum,3711717831u);T_EQ(level.pathing_random.index,2080948460u);
    move_test_repulse= NULL;move_test_repulse_pair=NULL;move_test_repulse_endpoint=NULL;
    reset_entities();setup_test_world();
}

TEST(wc3_repulsion_overlap, ascending_retirement_visits_a_linear_number_of_owners) {
    reset_entities();setup_test_world();
    UnitBalance_t balance={.speed=220,.repulse=1};UnitData_t data={.moveTypeName="foot"};edict_t *units[1024];
    FOR_LOOP(i,1024) {
        edict_t *unit=units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
        unit->data.UnitBalance=&balance;unit->data.UnitData=&data;unit->s.model=1;
        CAbilityMove(unit,A_UNIT_INIT,NULL);T_ASSERT(unit->movement.repulse.active);
    }
    move_test_repulse_unlink_visits=0;
    FOR_LOOP(i,1024) {S_SetUnitPaused(units[i],true);T_ASSERT(!units[i]->movement.repulse.active);}
    T_NULL(level.repulse_head);
    fprintf(stderr,"Repulsor retirement: 1024 owners, %llu visits\n",(unsigned long long)move_test_repulse_unlink_visits);
    T_ASSERT(move_test_repulse_unlink_visits<=2*1024);
    reset_entities();setup_test_world();
}
TEST(wc3_repulsion_overlap, saved_owner_slots_rebuild_and_reject_invalid_chains) {
    reset_entities();setup_test_world();edict_t *units[3];
    FOR_LOOP(i,3) {
        edict_t *unit=units[i]=alloc_test_unit(MAKEFOURCC('h','g','r','y'),304+i*8,304);
        unit->s.model=1;unit->svflags|=SVF_MONSTER;unit->collision=8;unit->stand=unit_stand;
        CAbilityMove(unit,A_UNIT_INIT,NULL);unit_stand(unit);gi.LinkEntity(unit);
        T_ASSERT(unit->movement.repulse.active);
    }
    level.repulse_phase=1;
    cstring_t file=Test_TempPath("wc3-repulsion-link-slots.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));T_EQ(level.repulse_phase,1);
    T_EQ(level.repulse_head,units[2]);T_EQ(units[2]->movement.repulse.next,units[1]);
    S_SetUnitPaused(units[1],true);T_EQ(level.repulse_head,units[2]);T_EQ(units[2]->movement.repulse.next,units[0]);
    S_SetUnitPaused(units[1],false);T_EQ(level.repulse_head,units[1]);T_EQ(units[1]->movement.repulse.next,units[2]);
    CAbilityMove(units[2],A_UNIT_REMOVE,NULL);T_EQ(units[1]->movement.repulse.next,units[0]);
    T_ASSERT(S_RestoreMoveRepulsors());
    units[0]->movement.repulse.next=units[0];T_ASSERT(!S_RestoreMoveRepulsors());
    units[0]->movement.repulse.next=NULL;T_ASSERT(S_RestoreMoveRepulsors());
    units[2]->movement.repulse.active=true;T_ASSERT(!S_RestoreMoveRepulsors());
    units[2]->movement.repulse.active=false;T_ASSERT(S_RestoreMoveRepulsors());
    remove(file);reset_entities();setup_test_world();
}
#endif
