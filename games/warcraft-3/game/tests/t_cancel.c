#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_cancel208.h"
#include "retail_cancel208_scene.h"

extern void (*test_preload_marker)(cstring_t);

static edict_t *cancel208_units[8];
static unsigned cancel208_cursor[8];
static bool cancel208_mismatch;
static uint32_t const (*const cancel208_rows[8])[5]={cancel208_visual0,cancel208_visual1,
    cancel208_visual2,cancel208_visual3,cancel208_visual4,cancel208_visual5,cancel208_visual6,cancel208_visual7};
static unsigned const cancel208_counts[8]={sizeof(cancel208_visual0)/sizeof(*cancel208_visual0),
    sizeof(cancel208_visual1)/sizeof(*cancel208_visual1),sizeof(cancel208_visual2)/sizeof(*cancel208_visual2),
    sizeof(cancel208_visual3)/sizeof(*cancel208_visual3),sizeof(cancel208_visual4)/sizeof(*cancel208_visual4),
    sizeof(cancel208_visual5)/sizeof(*cancel208_visual5),sizeof(cancel208_visual6)/sizeof(*cancel208_visual6),
    sizeof(cancel208_visual7)/sizeof(*cancel208_visual7)};

static void cancel208_visual(edict_t *unit,float facing,float speed) {
    unsigned role=8;FOR_LOOP(i,8)if(unit==cancel208_units[i])role=i;
    T_ASSERT(role<8);if(role>=8)return;
    unsigned index=cancel208_cursor[role]++;
    T_ASSERT(index<cancel208_counts[role]);if(index>=cancel208_counts[role]) {cancel208_mismatch=true;return;}
    uint32_t actual[]={wc3_float_bits(unit->s.angle),wc3_float_bits(facing),wc3_float_bits(speed),
        wc3_float_bits(unit->movement.visual_facing),wc3_float_bits(unit->movement.visual_speed)};
    FOR_LOOP(i,5) {
        T_EQ(actual[i],cancel208_rows[role][index][i]);
        if(actual[i]!=cancel208_rows[role][index][i]) {
            fprintf(stderr,"cancel208 visual role=%u row=%u field=%u actual=%08x expected=%08x\n",
                role,index,i,actual[i],cancel208_rows[role][index][i]);cancel208_mismatch=true;
        }
    }
}

static void cancel208_snapshot(uint32_t const expected[8][8]) {
    FOR_LOOP(role,8) {
        edict_t const *unit=cancel208_units[role];
        uint32_t actual[]={wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
            wc3_float_bits(wc3_div(unit->movement.velocity.x,32)),wc3_float_bits(wc3_div(unit->movement.velocity.y,32)),
            wc3_float_bits(unit->s.angle),wc3_float_bits(unit->movement.visual_facing),
            wc3_float_bits(unit->movement.visual_speed),unit->movement.group_id!=0};
        FOR_LOOP(i,8) {
            T_EQ(actual[i],expected[role][i]);
            if(actual[i]!=expected[role][i]) {
                fprintf(stderr,"cancel208 snapshot time=%u role=%u field=%u actual=%08x expected=%08x\n",
                    level.time,role,i,actual[i],expected[role][i]);cancel208_mismatch=true;
            }
        }
    }
}

static void cancel208_marker(cstring_t value) {
    unsigned tick=0;
    if(sscanf(value,"C208 tick=%u",&tick)!=1)return;
    if(strstr(value," label=sample ")) {
        T_ASSERT(tick>=1 && tick<=80);
        if(tick>=1 && tick<=80)cancel208_snapshot(cancel208_samples+(tick-1)*8);
    }
}

TEST(wc3_cancel, public_stop_and_replacement_match_turning_and_saved_suffix) {
    reset_entities();setup_test_world();
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    float radius=31,speed=270,old_min=game.constants.minUnitSpeed,old_max=game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;
    unitModification_t mods[]={{.modID=MAKEFOURCC('u','c','o','l'),.type=mod_real,.data=&radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed}};
    unitData_t custom={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','F','0','0'),
        .numbeOfModifications=2,.modifications=mods};
    mapInfo_t info={.num_userCreatedUnits=1,.userCreatedUnits=&custom};mapInfo_t const *oldinfo=level.mapinfo;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    uint8_t cells[64*64]={0};CM_SetupTestPathmap(64,64,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=level.pathing_phase=0;level.pathing_due=false;level.pathing_counter=1024;
    T_ASSERT(run_test_jass(cancel208_scene));
    unsigned count=0;FILTER_EDICTS(ent,ent->inuse && ent->class_id==custom.newUnitID)if(count<8)cancel208_units[count++]=ent;
    T_EQ(count,8);cancel208_mismatch=false;memset(cancel208_cursor,0,sizeof(cancel208_cursor));
    test_preload_marker=cancel208_marker;
    move_test_visual_commit=cancel208_visual;level.started=level.scriptsConfigured=level.scriptsStarted=true;
    cstring_t file=Test_TempPath("wc3-cancel208.bin");unsigned saved[8]={0};
    FOR_LOOP(pass,2) {
        if(pass) {T_ASSERT(ReadGame(file));memcpy(cancel208_cursor,saved,sizeof(saved));}
        while(level.time<8000 && !cancel208_mismatch) {
            level.time+=5;globals.RunFrame();
            if(level.time==600) {
                cancel208_snapshot(cancel208_after_cancel_angular);
                FOR_LOOP(role,4) {
                    T_EQ(cancel208_units[role]->current_order_id,0);
                    T_ASSERT(!move_unit_group(cancel208_units[role]));
                    T_ASSERT(!cancel208_units[role]->movement.fine_queued);
                    T_ASSERT(!cancel208_units[role]->movement.fine_route.adaptive_admission.queued);
                }
            }
            if(!pass && level.time==700) {memcpy(saved,cancel208_cursor,sizeof(saved));T_ASSERT(WriteGame(file));}
        }
        if(!cancel208_mismatch)cancel208_snapshot(cancel208_complete);
        FOR_LOOP(i,8) {
            T_EQ(cancel208_cursor[i],cancel208_counts[i]);
            if(cancel208_cursor[i]!=cancel208_counts[i])fprintf(stderr,"cancel208 visual count role=%u actual=%u expected=%u\n",
                i,cancel208_cursor[i],cancel208_counts[i]);
        }
        if(cancel208_mismatch)break;
    }
    test_preload_marker=NULL;move_test_visual_commit=NULL;level.started=false;remove(file);
    reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=oldinfo;
    game.constants.minUnitSpeed=old_min;game.constants.maxUnitSpeed=old_max;
}

/* Queue exhaustion is a supplied engine precondition, as in the original
 * 96-unit witness. Exercise public replacement and save/reclamation, rather
 * than reproducing the allocator or scheduler in the test. */
TEST(wc3_cancel, waiting_and_active_replacements_retain_survivor_fifo_and_storage) {
    bool policy=level.move_fine_responsive;
    FOR_LOOP(phase,4) FOR_LOOP(replace,2) {
        reset_entities();setup_test_world();level.move_fine_responsive=false;
        FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
        uint8_t cells[64*64]={0};CM_SetupTestPathmap(64,64,cells);
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
        level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;
        level.time=level.pathing_msec=level.pathing_phase=0;level.pathing_due=false;
        T_ASSERT(run_test_jass("globals\nunit array u\nendglobals\n"
            "function main takes nothing returns nothing\nlocal integer i=0\n"
            "loop\nexitwhen i==4\nset u[i]=CreateUnit(Player(i/3),'hRTE',128,128+128*i,0)\n"
            "call SetUnitAcquireRange(u[i],0)\ncall IssuePointOrder(u[i],\"move\",1024,128+128*i)\n"
            "set i=i+1\nendloop\nendfunction\n"));
        if(phase==0){level.move_fine_budgets[0].work=1101;level.move_fine_budgets[0].countdown=1;}
        if(phase==1){level.move_coarse_budgets[0][2].work=901;level.move_coarse_budgets[0][2].countdown=2;}
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        while(level.time<(phase>=2?350:35)){level.time+=5;globals.RunFrame();}
        edict_t *unit=NULL,*other=NULL;
        FILTER_EDICTS(e,e->inuse && e->class_id==MAKEFOURCC('h','R','T','E')) {
            if(e->s.player==1)other=e;else if(!unit)unit=e;
        }
        if(phase==0)unit=level.move_fine_budgets[0].head;
        if(phase==1)unit=(edict_t *)((char *)level.move_coarse_budgets[0][2].head-
            offsetof(edict_t,movement.fine_route.adaptive_admission));
        T_NOT_NULL(unit);T_NOT_NULL(other);if(!unit||!other)continue;
        if(phase==3) {
            T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            S_SetUnitFacingTimed(unit,270,1.2);T_EQ(unit->current_order_id,0);
            T_ASSERT(move_unit_group(unit)->turning);
        }
        uint32_t id=unit->movement.group_id,other_id=other->movement.group_id;
        moveGroup_t *old=move_unit_group(unit);T_NOT_NULL(old);if(!old)continue;
        vec2_t *storage=unit->movement.fine_route.points,*adaptive=unit->movement.fine_route.adaptive_points;
        edict_t *fine_next=unit->movement.fine_next,*fine_tail=level.move_fine_budgets[0].tail;
        moveCoarseRequest_t *coarse_next=unit->movement.fine_route.adaptive_admission.next;
        moveCoarseRequest_t *coarse_tail=level.move_coarse_budgets[0][2].tail;
        uint32_t work=phase==0?level.move_fine_budgets[0].work:level.move_coarse_budgets[0][2].work;
        if(phase==0)T_EQ(level.move_fine_budgets[0].count,3);
        if(phase==1)T_EQ(level.move_coarse_budgets[0][2].count,3);
        if(phase==2){T_ASSERT(storage);T_ASSERT(unit->movement.velocity.x!=0);}
        if(phase!=3) {
            T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1024,1024},true,0,0));
            T_EQ(unit->order_queue.count,1);
        }
        vec2_t point=unit->s.origin2;
        if(replace)T_ASSERT(unit_issueorder(unit,"move",&point));
        else T_ASSERT(unit_issueimmediateorder(unit,"stop"));
        T_EQ(unit->order_queue.count,0);T_EQ(unit->current_order_id,replace?G_OrderId("move"):0);
        T_EQ(unit->movement.velocity.x,0);T_EQ(unit->movement.velocity.y,0);
        T_EQ(unit->movement.fine_route.points,storage);T_EQ(unit->movement.fine_route.adaptive_points,adaptive);
        T_EQ(unit->movement.fine_route.count,0);T_EQ(unit->movement.fine_route.adaptive_count,0);
        T_EQ(unit->movement.fine_route.index,UINT32_MAX);T_EQ(unit->movement.fine_route.adaptive_index,UINT32_MAX);
        T_ASSERT(!unit->movement.fine_queued);T_ASSERT(!unit->movement.fine_route.adaptive_admission.queued);
        T_ASSERT(!unit->movement.path.valid);T_EQ(other->movement.group_id,other_id);
        T_ASSERT(old->inuse);T_EQ(old->count,0);
        if(phase==0){T_EQ(level.move_fine_budgets[0].count,2);T_EQ(level.move_fine_budgets[0].head,fine_next);
            T_EQ(level.move_fine_budgets[0].tail,fine_tail);T_EQ(level.move_fine_budgets[0].work,work);}
        if(phase==1){T_EQ(level.move_coarse_budgets[0][2].count,2);T_EQ(level.move_coarse_budgets[0][2].head,coarse_next);
            T_EQ(level.move_coarse_budgets[0][2].tail,coarse_tail);T_EQ(level.move_coarse_budgets[0][2].work,work);}
        T_ASSERT(S_ValidateMoveCoarseRequests());
        cstring_t file=Test_TempPath("wc3-cancel208-wait.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
        moveGroup_t *loaded=NULL;FOR_LOOP(i,ARRAY_COUNT(level.move_groups))
            if(level.move_groups[i]->inuse && level.move_groups[i]->id==id)loaded=level.move_groups[i];
        T_NOT_NULL(loaded);if(loaded)T_EQ(loaded->count,0);
        T_EQ(other->movement.group_id,other_id);T_ASSERT(S_ValidateMoveCoarseRequests());
        uint32_t until=level.time+35;while(level.time<until){level.time+=5;globals.RunFrame();}
        T_ASSERT(!loaded || !loaded->inuse);
        if(loaded){T_NULL(loaded->route.points);T_NULL(loaded->route.adaptive_points);T_NULL(loaded->route.group_points);}
        if(replace)T_NE(unit->movement.group_id,id);
        else {T_EQ(unit->movement.group_id,0);T_EQ(unit->current_order_id,0);}
        remove(file);level.started=false;
    }
    reset_entities();setup_test_world();level.move_fine_responsive=policy;
}
#endif
