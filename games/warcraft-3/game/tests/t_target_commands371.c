#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
extern void reset_entities(void),setup_test_world(void);

/* Actual native IssuePointOrder(attack) creates its range-zero d016b request
 * before returning, including an occupied destination. Compare public Move's
 * separately established admission and save before either owner can tick. */
TEST(wc3_movement, target371_public_attack_point_retains_click_and_admits_physical_owner_synchronously) {
    FOR_LOOP(occupied,3)FOR_LOOP(saved,2) {
        reset_entities();setup_test_world();
        static uint8_t cells[64*64];memset(cells,0,sizeof(cells));
        if(occupied==2)FOR_LOOP(y,5)FOR_LOOP(x,5)cells[(30+y)*64+46+x]=2;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        level.time=level.pathing_msec=level.pathing_phase=0;
        level.pathing_due=level.pathing_owner_clock_valid=level.timer_clock_valid=false;
        level.pathing_clock=(wc3Clock_t){0,0,300};
        ((mapInfo_t *)level.mapinfo)->players[0].used=true;
        ((mapInfo_t *)level.mapinfo)->players[0].playerType=kPlayerTypeHuman;
        game.clients[0].mapplayer=level.mapinfo->players;
        edict_t *unit=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){512,1024},180);
        unit_stand(unit);S_SetUnitMoveSpeed(unit,270);gi.LinkEntity(unit);
        edict_t *block=NULL;
        if(occupied==1) {
            block=unit_create(0,MAKEFOURCC('h','h','o','u'),&(vec2_t){1536,1024},0);
            S_SetUnitPaused(block,true);gi.LinkEntity(block);
        }
        T_ASSERT(G_IssueUnitPointOrder(unit,"attack",&(vec2_t){1536,1024},false,0,0));
        T_EQ(unit->current_order_id,G_OrderId("attack"));
        T_NOT_NULL(unit->movement.attackmove_waypoint);
        if(unit->movement.attackmove_waypoint) {
            T_FEQ(unit->movement.attackmove_waypoint->s.origin2.x,1536,0);
            T_FEQ(unit->movement.attackmove_waypoint->s.origin2.y,1024,0);
        }
        T_FEQ(unit->s.origin.x,512,0);T_FEQ(unit->s.origin.y,1024,0);
        moveGroup_t *group=move_unit_group(unit);T_NOT_NULL(group);
        if(group) {
            T_EQ(group->receiver,unit);T_EQ(group->owner_ability,GetAbilityIndex(CAbilityAttack)+1);
            T_EQ(group->complete,S_AttackMoveComplete);
            T_EQ(wc3_float_bits(group->members[0].arrival_range),0x3efae148u);
            T_FEQ(group->goal.x,48,0);T_FEQ(group->goal.y,32,0);
        }
        cstring_t path=Test_TempPath("wc3-target371-immediate.bin");
        if(saved) {
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));
            group=move_unit_group(unit);T_NOT_NULL(group);
            T_EQ(unit->current_order_id,G_OrderId("attack"));
            if(group) {
                T_EQ(group->complete,S_AttackMoveComplete);
                T_EQ(wc3_float_bits(group->members[0].arrival_range),0x3efae148u);
                T_FEQ(group->goal.x,48,0);T_FEQ(group->goal.y,32,0);
            }
            remove(path);
        }
        T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        T_EQ(unit->current_order_id,0);T_NULL(move_unit_group(unit));
        if(block)T_ASSERT(block->inuse);
    }
    reset_entities();setup_test_world();
}
#endif
