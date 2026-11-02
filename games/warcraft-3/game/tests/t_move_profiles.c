#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t);
edict_t *alloc_test_unit(uint32_t,float,float);
#include "retail_movement_profiles.h"

TEST(wc3_move_profiles, existing_move_owner_is_independent_of_shared_zero_defaults) {
    UnitBalance_t balance={.speed=0};UnitData_t data={.moveTypeName="none"};
    edict_t unit={.data={.UnitBalance=&balance,.UnitData=&data}};
    T_ASSERT(M_UnitMoveDisabled(&unit));
    unit.unitinfo.MoveSpeed=220;T_ASSERT(!M_UnitMoveDisabled(&unit));
    unit.unitinfo.MoveSpeed=0;unit.unitinfo.move_flags=BZ_UNIT_SPEED_SET;
    T_ASSERT(!M_UnitMoveDisabled(&unit));
    T_EQ(M_UnitStaticPathingFlags(&unit),0);
    T_EQ(S_UnitMoveCategory(&unit),0);
}

TEST(wc3_move_profiles, complete_original_authored_parser_and_exact_bit_switch) {
    FOR_LOOP(i,sizeof(retail_profile_names)/sizeof(*retail_profile_names))
        T_EQ(wc3_movement_profile(wc3_movement_parse(retail_profile_names[i].name))->bits,retail_profile_names[i].bits);
    T_EQ(wc3_movement_profile(wc3_movement_parse(NULL))->bits,0);
    FOR_LOOP(i,sizeof(retail_profile_bits)/sizeof(*retail_profile_bits)) {
        wc3MovementProfile_t const *p=wc3_movement_profile_bits(retail_profile_bits[i].bits);
        T_EQ(p->query,retail_profile_bits[i].query);
        T_EQ(p->category,retail_profile_bits[i].category);
        T_EQ(p->path_class,retail_profile_bits[i].path_class);
    }
}

TEST(wc3_move_profiles, authored_categories_reach_live_fine_occupancy) {
    char const *names[]={"foot","horse","fly","hover","float","amph","unbuild","none","","foot"};
    uint8_t const categories[]={0xca,0xca,0,0xca,0xca,0xca,8,0,0,0xca};
    uint8_t const queries[]={2,4,8,0x40,0x80};
    FOR_LOOP(i,sizeof(names)/sizeof(*names)) {
        reset_entities();setup_test_world();
        edict_t *mover=alloc_test_unit(MAKEFOURCC('h','f','o','o'),128,320);
        edict_t *other=alloc_test_unit(MAKEFOURCC('h','f','o','o'),320,320);
        UnitData_t row=*other->data.UnitData;row.moveTypeName=names[i];S_CompileMovementData(&row);other->data.UnitData=&row;
        mover->collision=other->collision=16;other->s.model=1;
        UnitBalance_t balance=*other->data.UnitBalance;
        if(i==sizeof(names)/sizeof(*names)-1) {
            balance.speed=0;other->data.UnitBalance=&balance;
            T_ASSERT(M_UnitMoveDisabled(other));
        }
        if(categories[i]==0 && !strcmp(names[i],"fly"))other->aiflags|=AI_FLYING;
        G_PublishMoveSpatialObject(other);
        FOR_LOOP(q,sizeof(queries)) {
            movePathQuery_t query={.geometry={&mover->s.origin2,&other->s.origin2,16,queries[q]},.mover=mover,.units=true};
            /* Bounds[-1024,1024],32-world cells: other occupies fine(42,42). */
            float fine[]={42,42};
            T_EQ(G_UnitMovePathFinePointIsPathable(&query,fine),(categories[i]&queries[q])==0);
        }
        reset_entities();
    }
    setup_test_world();
}

TEST(wc3_move_profiles, authored_names_publish_original_query_masks_and_flight_layer) {
    static char const *names[]={"foot","horse","fly","hover","float","amph","unbuild",
        "none","","_","FLY","Float","foot,fly","boat","-","FOOT","fLy","Unbuild"};
    static uint8_t const query[]={2,2,4,2,0x40,0x80,0,0,0,0,4,0x40,0,0,0,2,4,0};
    static uint8_t const category[]={0xca,0xca,0,0xca,0xca,0xca,8,0,0,0,0,0xca,0,0,0,0xca,0,8};
    static uint8_t const coarse[]={2,2,4,2,0x40,0x80,2,2,2,2,4,0x40,2,2,2,2,4,2};
    FOR_LOOP(i,sizeof(names)/sizeof(*names)) {
        reset_entities(); setup_test_world();
        mapInfo_t const *saved=level.mapinfo; mapInfo_t info=*saved;
        unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data=(void *)names[i]};
        unitData_t custom={.originalUnitID=MAKEFOURCC('h','f','o','o'),
            .newUnitID=MAKEFOURCC('x','M','0','0'),.numbeOfModifications=1,.modifications=&mod};
        info.num_userCreatedUnits=1; info.userCreatedUnits=&custom;
        level.mapinfo=&info; G_SetMapUnitOverrides(&info);
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
            "call CreateUnit(Player(0),'xM00',320,320,0)\nendfunction\n"));
        edict_t *created=NULL;
        FOR_LOOP(n,globals.num_edicts)if(g_edicts[n].inuse && g_edicts[n].class_id==custom.newUnitID)created=g_edicts+n;
        T_NOT_NULL(created);
        if(created) {
            T_EQ(M_UnitStaticPathingFlags(created),query[i]);
            T_EQ(!!(created->aiflags&AI_FLYING),query[i]==4);
            T_EQ(S_UnitMoveCategory(created),category[i]);
            T_EQ(S_UnitMoveCoarseMask(created),coarse[i]);
            T_ASSERT(!M_UnitMoveDisabled(created));
            T_ASSERT(unit_effective_speed(created)>0);
        }
        reset_entities(); G_SetMapUnitOverrides(NULL); level.mapinfo=saved;
    }
    setup_test_world();
}

TEST(wc3_move_profiles, zero_query_native_units_accept_move_and_translate) {
    char const *names[]={"none","","_","foot,fly","boat","unbuild"};
    FOR_LOOP(i,sizeof(names)/sizeof(*names)) {
        reset_entities();setup_test_world();
        mapInfo_t const *saved=level.mapinfo;mapInfo_t info=*saved;
        unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data=(void *)names[i]};
        unitData_t custom={.originalUnitID=MAKEFOURCC('h','f','o','o'),
            .newUnitID=MAKEFOURCC('x','M','0','0'),.numbeOfModifications=1,.modifications=&mod};
        info.num_userCreatedUnits=1;info.userCreatedUnits=&custom;
        level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
        level.pathing_clock=(wc3Clock_t){0};level.pathing_counter=0x400;
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set mover=CreateUnit(Player(0),'xM00',320,320,0)\n"
            "call IssuePointOrder(mover,\"move\",640,320)\nendfunction\n"));
        edict_t *unit=NULL;
        FOR_LOOP(n,globals.num_edicts)if(g_edicts[n].inuse && g_edicts[n].class_id==custom.newUnitID)unit=g_edicts+n;
        T_NOT_NULL(unit);
        if(unit) {
            float start=unit->s.origin2.x;
            FOR_LOOP(frame,40) {level.time+=30;globals.RunFrame();}
            T_ASSERT(unit->s.origin2.x>start+64);
            T_EQ(M_UnitStaticPathingFlags(unit),0);
            T_EQ(S_UnitMoveCoarseMask(unit),2);
        }
        reset_entities();G_SetMapUnitOverrides(NULL);level.mapinfo=saved;
    }
    setup_test_world();
}
#endif
