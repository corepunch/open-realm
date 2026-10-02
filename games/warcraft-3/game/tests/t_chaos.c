#ifdef BZ_TESTS
#include "../skills/s_skills.h"
#include "shared/test.h"

/* Runtime skill APIs are the public UnitAdd/RemoveAbility producers. */
static void chaos_ability_lifecycle(bool cancel, bool same_type) {
    reset_entities(); setup_test_world();
    char const *columns[]={"UnitID","UnitID1"};
    FOR_LOOP(schema,2) {
        char slk[512];
        snprintf(slk,sizeof(slk),"ID;PWXL;N;EBB;Y2;X5\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"levels\"\nC;X4;K\"%s\"\nC;X5;K\"DataA1\"\n"
            "C;Y2;X1;K\"Sca1\"\nC;X2;K\"Acha\"\nC;X3;K1\nC;X4;K\"hRTE\"\nC;X5;K37\nE\n",columns[schema]);
        slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
        float radius=59.25f;
        unitModification_t unit_mod={.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius};
        unitData_t unit_row={.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC('h','C','h','s'),
            .numbeOfModifications=1,.modifications=&unit_mod};
        unitModification_t base_mods[]={
            {.modID=MAKEFOURCC('a','r','e','q'),.type=mod_string,.data="Roch"},
            {.modID=MAKEFOURCC('a','r','q','a'),.type=mod_string,.data="2"},
        };
        unitData_t original={.originalUnitID=MAKEFOURCC('S','c','a','1'),.numbeOfModifications=2,.modifications=base_mods};
        unitModification_t custom_mods[]={
            {.modID=MAKEFOURCC('C','h','a','1'),.type=mod_string,.level=1,.data=same_type ? "hRTE" : "hChs"},
            {.modID=MAKEFOURCC('a','r','e','q'),.type=mod_string,.data=""},
        };
        unitData_t abilities[]={
            {.originalUnitID=original.originalUnitID,.newUnitID=MAKEFOURCC('A','C','h','s'),.numbeOfModifications=1,.modifications=custom_mods},
            {.originalUnitID=original.originalUnitID,.newUnitID=MAKEFOURCC('A','C','h','c'),.numbeOfModifications=2,.modifications=custom_mods},
        };
        mapInfo_t info={.num_userCreatedUnits=1,.userCreatedUnits=&unit_row,
            .num_originalAbilities=1,.originalAbilities=&original,.num_userCreatedAbilities=2,.userCreatedAbilities=abilities};
        mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;
        G_SetMapUnitOverrides(&info);G_SetMapAbilityOverrides(&info);
        uint32_t code=abilities[0].newUnitID;
        T_STREQ(G_AbilityRequirementField(code,false),"Roch");T_STREQ(G_AbilityRequirementField(code,true),"2");
        T_STREQ(G_AbilityRequirementField(abilities[1].newUnitID,false),"");
        T_EQ(G_AbilityLevel(code,1)->unitID,same_type ? unit_row.originalUnitID : unit_row.newUnitID);
        T_FEQ(G_AbilityLevel(code,1)->data[0].number,37,0);
        edict_t *unit=alloc_test_unit(unit_row.originalUnitID,0,0);unit->think=monster_think;
        uint32_t identity=unit->s.number,spawn=unit->spawn_time;
        gameClient_t *client=G_GetPlayerClientByNumber(unit->s.player);T_NOT_NULL(client);
        G_SetPlayerTechResearched(client,MAKEFOURCC('R','o','c','h'),0);
        T_ASSERT(G_ActorAddSkill(unit,code));T_EQ(unit->class_id,unit_row.originalUnitID);
        G_RunEntities();T_EQ(unit->class_id,unit_row.originalUnitID);
        G_SetPlayerTechResearched(client,MAKEFOURCC('R','o','c','h'),1);
        G_RunEntities();T_EQ(unit->class_id,unit_row.originalUnitID);
        if(cancel)T_ASSERT(G_ActorRemoveSkill(unit,code));
        G_SetPlayerTechResearched(client,MAKEFOURCC('R','o','c','h'),2);
        G_RunEntities();
        T_EQ(unit->class_id,cancel || same_type ? unit_row.originalUnitID : unit_row.newUnitID);
        T_EQ(unit->s.number,identity);T_EQ(unit->spawn_time,spawn);
        T_EQ(G_UnitAbilityLevel(unit,code),0);
        if(!cancel && !same_type)T_FEQ(unit->collision,radius,0);
        T_ASSERT(G_ActorAddSkill(unit,abilities[1].newUnitID));
        G_SetPlayerTechResearched(client,MAKEFOURCC('R','o','c','h'),0);
        G_RunEntities();T_EQ(unit->class_id,same_type ? unit_row.originalUnitID : unit_row.newUnitID);
        T_EQ(G_UnitAbilityLevel(unit,abilities[1].newUnitID),0);
        reset_entities();G_SetMapAbilityOverrides(NULL);G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

TEST(wc3_chaos, deferred_morph_inherits_requirements_and_consumes_the_ability) {chaos_ability_lifecycle(false,false);}
TEST(wc3_chaos, removing_pending_ability_cancels_the_morph) {chaos_ability_lifecycle(true,false);}
TEST(wc3_chaos, same_type_morph_still_consumes_the_ability) {chaos_ability_lifecycle(false,true);}
#endif
