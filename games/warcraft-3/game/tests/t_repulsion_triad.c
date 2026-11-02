#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_records.h"
#include "retail_repulsion_triad.h"

extern void reset_entities(void), setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t,float,float);
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);

static edict_t *triad_members[27];static unsigned triad_group,triad_pair_cursor[9];
static void triad_pair_check(edict_t const *source,edict_t const *candidate,wc3RepulsePair_t const *pair,
    wc3Repulse_t const *before,wc3Random_t random_before) {
    T_ASSERT(triad_pair_cursor[triad_group]<triad_pair_offsets[triad_group+1]);
    if(triad_pair_cursor[triad_group]>=triad_pair_offsets[triad_group+1])return;
    uint32_t const *row=triad_pairs[triad_pair_cursor[triad_group]++];
    T_EQ(source,triad_members[row[0]]);T_EQ(candidate,triad_members[row[1]]);
    uint32_t actual[]={wc3_float_bits(pair->source[0]),wc3_float_bits(pair->source[1]),
        wc3_float_bits(pair->other[0]),wc3_float_bits(pair->other[1]),
        wc3_float_bits(before->vector[0]),wc3_float_bits(before->vector[1]),
        wc3_float_bits(source->movement.repulse.state.vector[0]),wc3_float_bits(source->movement.repulse.state.vector[1]),
        random_before.sum,random_before.index,level.pathing_random.sum,level.pathing_random.index};
    FOR_LOOP(i,12)T_EQ(actual[i],row[i+2]);
}

/* Drive every complete idle update from the nine repeated/control retail
 * clusters, including cross-cluster no-contribution neighbors. The owner schedule is an input here; neighbors, admission,
 * displacement, occupancy and cooldown are the production implementation. */
TEST(wc3_repulsion_triad, complete_nine_cluster_visits_match_retail_words) {
    {
        reset_entities();setup_test_world();
        uint8_t cells[128*128]={0};
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});
        CM_SetupTestPathmap(128,128,cells);
        UnitBalance_t balance[27]; UnitData_t data={.moveTypeName="foot"};edict_t *units[27];
        FOR_LOOP(i,27) {
            typeof(triad_units[0]) const *input=triad_units+i;
            balance[i]=(UnitBalance_t){.speed=220,.repulse=input->enabled,
                .repulseParam=input->selector,.repulsePrio=input->rank};
            edict_t *unit=units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
            unit->data.UnitBalance=balance+i;unit->data.UnitData=&data;
            unit->s.player=input->owner;unit->collision=input->radius;
            unit->svflags|=SVF_MONSTER;unit->s.model=1;unit->stand=unit_stand;
            unit->movement.pose_valid=true;
            unit->movement.fine_pose=(vec2_t){wc3_float(input->x),wc3_float(input->y)};
            unit->s.origin2=unit->movement.pose_world=(vec2_t){wc3_mul(unit->movement.fine_pose.x,32),wc3_mul(unit->movement.fine_pose.y,32)};
            CAbilityMove(unit,A_UNIT_INIT,NULL);unit_stand(unit);gi.LinkEntity(unit);G_PublishMoveSpatialObject(unit);
        }
        level.pathing_random=(wc3Random_t){.sum=4273436052u,.index=209508436u};
        memcpy(triad_members,units,sizeof(units));
        FOR_LOOP(group,9)triad_pair_cursor[group]=triad_pair_offsets[group];
        move_test_repulse_pair=triad_pair_check;
        FOR_LOOP(visit,sizeof(triad_schedule)/sizeof(*triad_schedule)) {
            unsigned i=triad_schedule[visit];
            typeof(triad_visits[0]) const *row=triad_visits+i;
            unsigned group=triad_group=row->unit/3;
            edict_t *unit=units[row->unit];
            move_repulse_update(unit);
            uint32_t actual[]={wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
                wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),
                unit->movement.repulse.state.packed};
            uint32_t expected[]={row->x,row->y,row->vx,row->vy,row->word};
            bool match=!memcmp(actual,expected,sizeof(actual));
            T_ASSERT(match);
            if(!match) {fprintf(stderr,"Triad group%u visit%u unit%u: %08x/%08x %08x/%08x %08x/%08x %08x/%08x %08x/%08x\n",group,i,row->unit,
                actual[0],expected[0],actual[1],expected[1],actual[2],expected[2],actual[3],expected[3],actual[4],expected[4]);break;}
            wc3FineBox_t box=G_GetMoveSpatialObject(unit-g_edicts)->box;
            T_EQ(box.min.y,row->y0);T_EQ(box.min.x,row->x0);T_EQ(box.max.y,row->y1);T_EQ(box.max.x,row->x1);
            T_EQ(level.pathing_random.sum,4273436052u);T_EQ(level.pathing_random.index,209508436u);
        }
        FOR_LOOP(group,9)T_EQ(triad_pair_cursor[group],triad_pair_offsets[group+1]);
        move_test_repulse_pair=NULL;
    }
    reset_entities();setup_test_world();
}
#endif
