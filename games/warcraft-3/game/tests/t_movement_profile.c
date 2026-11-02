#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"

void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

/* Retail MOVE-01.1 witness: hP27 base237/173..389, nP27 base220/211..233.
 * The asset-free form clones the fixture Footman model; only its authored
 * movement profile is compared with the native Sheep-form capture. */
TEST(wc3_movement, profile271_polymorph_keeps_authored_bounds_setters_and_move) {
    FOR_LOOP(schema,2) FOR_LOOP(saved,2) FOR_LOOP(expired,2) {
        reset_entities();setup_test_world();
        mapInfo_t const *prior=level.mapinfo;mapInfo_t info=*prior;
        info.fileFormat=schema ? 24 : 18;
        int32_t values[]={237,173,389,220,211,233};
        unitModification_t mods[6];
        uint32_t fields[]={MAKEFOURCC('u','m','v','s'),MAKEFOURCC('u','m','i','s'),MAKEFOURCC('u','m','a','s')};
        FOR_LOOP(i,6)mods[i]=(unitModification_t){.modID=fields[i%3],.type=mod_int,.data=values+i};
        unitData_t types[]={
            {.originalUnitID=MAKEFOURCC('h','f','o','o'),.newUnitID=MAKEFOURCC('h','P','2','7'),.numbeOfModifications=3,.modifications=mods},
            {.originalUnitID=MAKEFOURCC('h','f','o','o'),.newUnitID=MAKEFOURCC('n','P','2','7'),.numbeOfModifications=3,.modifications=mods+3}
        };
        info.num_userCreatedUnits=2;info.userCreatedUnits=types;
        level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        char slk[2048];cstring_t data_a=schema ? "DataA1" : "Data11",data_b=schema ? "DataB1" : "Data12";
        snprintf(slk,sizeof(slk),"ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\n"
            "C;X3;K\"levels\"\nC;X4;K\"targs\"\nC;X5;K\"Cost1\"\nC;X6;K\"Cool1\"\n"
            "C;X7;K\"Rng1\"\nC;X8;K\"Dur1\"\nC;X9;K\"%s\"\nC;X10;K\"%s\"\n%s"
            "C;Y2;X1;K\"Aply\"\nC;X2;K\"Aply\"\nC;X3;K1\nC;X4;K\"ground,enemy\"\n"
            "C;X5;K0\nC;X6;K0\nC;X7;K1000\nC;X8;K60\nC;X9;K10\nC;X10;K\"nP27\"\n%sE\n",
            data_a,data_b,
            "C;X11;K\"BuffID1\"\n",schema ? "C;X11;K\"Bply\"\n" : "");
        size_t end=strlen(slk)-2;
        snprintf(slk+end,sizeof(slk)-end,
            "C;Y3;X1;K\"Aslo\"\nC;X2;K\"Aslo\"\nC;X3;K1\nC;X4;K\"ground,enemy\"\n"
            "C;X5;K0\nC;X6;K0\nC;X7;K1000\nC;X8;K60\nC;X9;K0.6\nC;X10;K0.25\n%sE\n",
            "C;X11;K\"Bslo\"\n");
        slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
        T_EQ(S_SpellDataId(MAKEFOURCC('A','p','l','y'),1,2),MAKEFOURCC('n','P','2','7'));
        T_ASSERT(run_test_jass("globals\nunit actor271\nunit caster271\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set actor271=CreateUnit(Player(1),'hP27',512,512,0)\n"
            "set caster271=CreateUnit(Player(0),'hfoo',256,512,0)\n"
            "call UnitAddAbility(caster271,'Aply')\ncall SetUnitMoveSpeed(actor271,300)\nendfunction\n"
            "function high271 takes nothing returns nothing\ncall SetUnitMoveSpeed(actor271,1000)\nendfunction\n"
            "function low271 takes nothing returns nothing\ncall SetUnitMoveSpeed(actor271,1)\nendfunction\n"
            "function middle271 takes nothing returns nothing\ncall SetUnitMoveSpeed(actor271,300)\n"
            "call IssuePointOrder(actor271,\"move\",1600,512)\nendfunction\n"
            "function remove271 takes nothing returns nothing\ncall UnitRemoveBuffs(actor271,false,true)\nendfunction\n"));
        edict_t *unit=NULL,*caster=NULL;
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','P','2','7'))unit=ent;
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o'))caster=ent;
        T_NOT_NULL(unit);T_NOT_NULL(caster);
        if(!unit || !caster)return;
        unit->stand=unit_stand;unit->think=monster_think;unit->movetype=MOVETYPE_STEP;
        unit_stand(unit);level.show_map_cheat=true;
        T_FEQ(S_UnitMoveSpeed(unit),300,0);T_FEQ(S_UnitDefaultMoveSpeed(unit),237,0);
        spellTarget_t target={.type=SPELL_TARGET_UNIT,.entity=unit};
        abilityitem_t ability=S_AbilityItem(MAKEFOURCC('A','p','l','y'));
        abilityCall_t call={.item=&ability,.target=&target};
        T_ASSERT(S_AbilityMessage(caster,A_EXECUTE,&call));T_ASSERT(S_UnitPolymorphed(unit));
        T_EQ(unit->movement.group_isolation,1);
        T_ASSERT(S_AbilityMessage(caster,A_EXECUTE,&call));
        T_EQ(unit->movement.group_isolation,1);
        T_FEQ(unit->unitinfo.MoveSpeed,283,0);T_FEQ(S_UnitMoveSpeed(unit),233,0);
        T_FEQ(S_UnitDefaultMoveSpeed(unit),237,0);
        jass_callbyname(level.vm,"high271",false);T_FEQ(S_UnitMoveSpeed(unit),233,0);
        jass_callbyname(level.vm,"low271",false);T_FEQ(S_UnitMoveSpeed(unit),211,0);
        jass_callbyname(level.vm,"middle271",false);T_FEQ(S_UnitMoveSpeed(unit),233,0);
        T_EQ(unit->current_order_id,G_OrderId("move"));T_NOT_NULL(unit->goalentity);
        uint32_t number=unit->s.number;float x=unit->s.origin2.x;
        if(saved) {
            cstring_t path=Test_TempPath("wc3-profile271.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
            unit=g_edicts+number;
            T_ASSERT(S_UnitPolymorphed(unit));T_FEQ(S_UnitMoveSpeed(unit),233,0);
            T_EQ(unit->current_order_id,G_OrderId("move"));
        }
        abilityitem_t slow_item=S_AbilityItem(MAKEFOURCC('A','s','l','o'));
        target.entity=unit;
        abilityCall_t slow_call={.item=&slow_item,.target=&target};
        caster=NULL;FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o'))caster=ent;
        T_ASSERT(S_AbilityMessage(caster,A_EXECUTE,&slow_call));
        T_EQ(unit->movement.group_isolation,2);
        if(saved) {
            cstring_t path=Test_TempPath("wc3-slow318-overlap.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+number;
            T_EQ(unit->movement.group_isolation,2);
        }
        if(!expired) {
            heroabilitystatus_t *slow=unit_findstatus(unit,MAKEFOURCC('B','s','l','o'));
            T_NOT_NULL(slow);if(slow)unit_removestatus(unit,slow,STATUS_REMOVE_SCRIPT);
            T_EQ(unit->movement.group_isolation,1);
        }
        if (expired) {
            heroabilitystatus_t *status=unit_findstatus(unit,MAKEFOURCC('B','p','l','y'));
            T_NOT_NULL(status);
            level.time+=1;status->timestamp=G_Time();unit_updatestatuses(unit);
        } else {
            T_EQ(unit_removebuffs(unit,true,false,false,false,true,true,false),0);
            T_ASSERT(S_UnitPolymorphed(unit));
            jass_callbyname(level.vm,"remove271",false);
        }
        if(expired) {
            T_EQ(unit->movement.group_isolation,1);
            heroabilitystatus_t *slow=unit_findstatus(unit,MAKEFOURCC('B','s','l','o'));
            T_NOT_NULL(slow);if(slow)unit_removestatus(unit,slow,STATUS_REMOVE_SCRIPT);
        }
        T_EQ(unit->movement.group_isolation,0);
        T_ASSERT(!S_UnitPolymorphed(unit));T_FEQ(unit->unitinfo.MoveSpeed,317,0);
        T_FEQ(S_UnitMoveSpeed(unit),317,0);T_FEQ(S_UnitDefaultMoveSpeed(unit),237,0);
        T_EQ(unit->current_order_id,G_OrderId("move"));T_NOT_NULL(unit->goalentity);
        level.scheduled_think=true;
        FOR_LOOP(i,12) {
            wc3_clock_advance(&level.pathing_clock,.03f,0);
            S_PublishMovement(unit);S_BeginAbilityOwnerUpdates();S_RunAbilityOwnerUpdates();
        }
        level.scheduled_think=false;T_ASSERT(unit->s.origin2.x>x);
        reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=prior;
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}
#endif
