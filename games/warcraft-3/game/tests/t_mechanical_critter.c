#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);
bool run_test_jass(cstring_t);

/* Preserve fixture expectations: these are independent, explicitly authored
 * test rows. Their UI order differs from their rawcode/data-table order. */
TEST(wc3_items, mechanical201_item_cast_retains_latent_category_through_save_and_inverse) {
    reset_entities(); setup_test_world();
    edict_t *caster=alloc_test_unit(MAKEFOURCC('H','p','a','l'),512,512);
    caster->s.player=0; caster->s.angle=0; caster->stand=unit_stand;
    caster->health.value=caster->health.max_value=100; caster->svflags|=SVF_MONSTER;
    g_edicts[0].client=game.clients;
    game.clients[0].connected=true; game.clients[0].ps.number=0;
    G_SelectEntity(g_edicts[0].client,caster);
    UnitUI_t ui[6]; UnitData_t data[6]; UnitBalance_t balance[6];
    uint32_t ids[]={MAKEFOURCC('h','f','o','o'),MAKEFOURCC('h','r','i','f'),MAKEFOURCC('h','g','r','y'),
        MAKEFOURCC('h','p','e','a'),MAKEFOURCC('h','m','t','m'),MAKEFOURCC('H','m','k','g')};
    FOR_LOOP(i,6) {
        ui[i]=*G_UnitUI(ids[0]); ui[i].id=ids[i]; ui[i].tilesets="*";
        data[i]=*G_UnitData(ids[0]); data[i].id=ids[i]; data[i].race="critters";
        balance[i]=*G_UnitBalance(ids[0]); balance[i].id=ids[i];
        balance[i].goldCost=balance[i].lumberCost=0; balance[i].speed=0;
        balance[i].repulse=1; balance[i].repulseGroup=3; balance[i].repulsePrio=2;
        balance[i].repulseParam=0; balance[i].tilesets="*";
    }
    data[0].race="human"; balance[3].goldCost=1; data[4].moveTypeName="fly";
    ui[5].tilesets=balance[5].tilesets="L";
    mapInfo_t info=*level.mapinfo; info.mainGroundType='X'; level.mapinfo=&info;
    UnitUI_t tmp=ui[1]; ui[1]=ui[2]; ui[2]=tmp;
    ItemData_t itemrow=*G_ItemData(MAKEFOURCC('s','p','r','o'));
    itemrow.id=MAKEFOURCC('m','c','r','i'); itemrow.abilList="Amec"; itemrow.usable=true;
    itemrow.perishable=true; itemrow.uses=1;
    cstring_t names[]={"UnitUI","UnitData","UnitBalance","ItemData"};
    slkTestData_t rows[]={{.rows=ui,.count=6},{.rows=data,.count=6},{.rows=balance,.count=6},{.rows=&itemrow,.count=1}};
    slkTestData_t *old[4]; FOR_LOOP(i,4)old[i]=G_SetSLKRows(names[i],rows+i);
    char const slk[]="ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"levels\"\n"
        "C;X4;K\"DataA1\"\nC;X5;K\"Area1\"\nC;X6;K\"Dur1\"\nC;X7;K\"DataC1\"\nC;X8;K\"DataD1\"\n"
        "C;Y2;X1;K\"Amec\"\nC;X2;K\"Amec\"\nC;X3;K1\nC;X4;K1\nC;X5;K200\nC;X6;K0\n"
        "C;Y3;X1;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K1\nC;X4;K6\nC;X7;K1\nC;X8;K1\nE\n";
    slkTestData_t *abilities=parse_slk_string(slk), *oldabil=G_SetSLKRows("AbilityData",abilities);
    wc3_random_reseed(level.purpose_random,731);
    wc3Random_t expected=level.purpose_random[33], others[BZ_WC3_RANDOM_STREAMS];
    memcpy(others,level.purpose_random,sizeof(others));
    wc3_random_range(&expected,2); /* Native item attachment prepares the displayed choice. */
    uint32_t selected=wc3_random_range(&expected,2);
    edict_t *item=alloc_test_unit(itemrow.id,512,512);
    SP_SpawnItem(item);
    T_ASSERT(G_InventoryCanUseItems(caster));
    T_STREQ(G_ItemAbilityList(item),"Amec");
    T_ASSERT(S_AbilityItem(MAKEFOURCC('A','m','e','c')).ability);
    T_ASSERT(G_AddItemToSlot(caster,item,0));
    G_UseItem(caster,0);
    edict_t *critter=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->summon_ability==MAKEFOURCC('A','m','e','c'))critter=ent;
    T_NOT_NULL(critter);
    T_EQ(level.purpose_random[33].sum,expected.sum); T_EQ(level.purpose_random[33].index,expected.index);
    FOR_LOOP(i,BZ_WC3_RANDOM_STREAMS)if(i!=33) {
        T_EQ(level.purpose_random[i].sum,others[i].sum); T_EQ(level.purpose_random[i].index,others[i].index);
    }
    if(critter) {
        T_EQ(critter->class_id,ui[1+selected].id);
        T_EQ(wc3_float_bits(critter->s.origin2.x),wc3_float_bits(711.75f));
        T_EQ(wc3_float_bits(critter->s.origin2.y),wc3_float_bits(512.f));
        T_EQ(G_UnitStatusLevel(critter,MAKEFOURCC('B','m','e','c')),1);
        T_EQ(critter->movement.repulse.state.packed & 0xffff0000u,0x20300000u);
        G_SetUnitPlayer(critter,1);
        T_EQ(critter->movement.repulse.state.packed & 0xffff0000u,0x2f300000u);
        cstring_t file=Test_TempPath("mechanical201-latent-category.bin");
        T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file)); remove(file);
        T_EQ(critter->movement.repulse.state.packed & 0xffff0000u,0x2f300000u);
        /* Real public removal must deliver the inverse although Bmec is a
         * status, not one of the victim's learned abilities. */
        T_ASSERT(run_test_jass("globals\ngroup g\nendglobals\nfunction inverse takes nothing returns nothing\n"
            "if GetUnitAbilityLevel(GetEnumUnit(),'Bmec')>0 then\n"
            "call BJassAssert(UnitRemoveAbility(GetEnumUnit(),'Bmec'),\"remove Mechanical Critter buff\")\nendif\nendfunction\n"
            "function main takes nothing returns nothing\nset g=CreateGroup()\ncall GroupEnumUnitsOfPlayer(g,Player(1),null)\n"
            "call ForGroup(g,function inverse)\ncall DestroyGroup(g)\nendfunction\n"));
        T_EQ(G_UnitStatusLevel(critter,MAKEFOURCC('B','m','e','c')),0);
        T_EQ(critter->movement.repulse.state.packed & 0xffff0000u,0x2f300000u);
        G_SetUnitPlayer(critter,2);
        T_EQ(critter->movement.repulse.state.packed & 0xffff0000u,0x22300000u);
    }
    reset_entities(); setup_test_world();
    FOR_LOOP(i,4) {slkTestData_t *replaced=G_SetSLKRows(names[i],old[i]);free(replaced);free(old[i]);}
    slkTestData_t *replaced=G_SetSLKRows("AbilityData",oldabil);free(replaced);free(oldabil);free_slk_rows(abilities);
}
#endif
