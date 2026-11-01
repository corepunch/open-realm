#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

extern void reset_entities(void), setup_test_world(void);
edict_t *alloc_test_unit(uint32_t,float,float);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);

/* Exercise real inventory admission and resource consumption. Native opaque
 * global agent IDs are not asserted: slot-order observations are item choice,
 * mana and charge changes, including the production registry save section. */
typedef struct {
    mapInfo_t info; mapInfo_t const *old_info;
    slkTestData_t *rows,*old_rows;
    edict_t *unit,*first,*second;
} preparatoryFixture_t;
static ItemData_t const preparatory_first_data={.abilList="Zpm1",.uses=3,.usable=true,.droppable=true};
static ItemData_t const preparatory_second_data={.abilList="Zpm2",.uses=3,.usable=true,.droppable=true};

static edict_t *PreparatoryItem(uint32_t code,ItemData_t const *data) {
    edict_t *item=alloc_test_unit(code,32,0);
    item->targtype=TARG_ITEM;item->movetype=MOVETYPE_NONE;
    item->data.ItemData=data;if(!item->item)item->item=G_AllocItem();
    item->item->in_world=true;item->item->inventory_slot=-1;item->item->charges=3;
    gi.LinkEntity(item);return item;
}
static void PreparatoryBegin(preparatoryFixture_t *f) {
    reset_entities();setup_test_world();memset(f,0,sizeof(*f));
    f->old_info=level.mapinfo;f->info=*level.mapinfo;
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    f->info.fileFormat=tft ? 25 : 24;level.mapinfo=&f->info;
    char text[2400];snprintf(text,sizeof(text),
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"levels\"\n"
        "C;X4;K\"%s\"\nC;X5;K\"%s\"\nC;X6;K\"%s\"\nC;X7;K\"%s\"\nC;X8;K\"%s\"\nC;X9;K\"Cost1\"\nC;X10;K\"Cool1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K1\nC;X4;K6\nC;X5;K0\nC;X6;K1\nC;X7;K1\nC;X8;K1\n"
        "C;Y3;X1;K\"Zpm1\"\nC;X2;K\"AIma\"\nC;X3;K1\nC;X4;K80\nC;X9;K0\nC;X10;K0\n"
        "C;Y4;X1;K\"Zpm2\"\nC;X2;K\"AIma\"\nC;X3;K1\nC;X4;K120\nC;X9;K0\nC;X10;K0\n"
        "C;Y5;X1;K\"Zcst\"\nC;X2;K\"Ainf\"\nC;X3;K1\nC;X9;K50\nE\n",
        tft?"DataA1":"Data11",tft?"DataB1":"Data12",tft?"DataC1":"Data13",
        tft?"DataD1":"Data14",tft?"DataE1":"Data15");
    f->rows=parse_slk_string(text);f->old_rows=G_SetSLKRows("AbilityData",f->rows);
    static UnitAbilities_t abilities={.abilList="AInv,Amov",.heroAbilList=""};
    f->unit=alloc_test_unit(MAKEFOURCC('H','p','a','l'),0,0);
    f->unit->data.UnitAbilities=&abilities;f->unit->s.player=0;
    f->unit->health.value=f->unit->health.max_value=100;
    f->unit->mana.max_value=200;f->unit->mana.value=0;
    f->unit->aiflags|=AI_TOWN_OWNED;f->unit->movetype=MOVETYPE_STEP;
    f->unit->collision=16;f->unit->unitinfo.MoveSpeed=270;f->unit->unitinfo.TurnSpeed=1;
    f->unit->stand=unit_stand;unit_stand(f->unit);gi.LinkEntity(f->unit);
    f->first=PreparatoryItem(MAKEFOURCC('Z','i','m','1'),&preparatory_first_data);
    f->second=PreparatoryItem(MAKEFOURCC('Z','i','m','2'),&preparatory_second_data);
    T_ASSERT(G_AddItemToSlotScripted(f->unit,f->first,5));
    T_ASSERT(G_AddItemToSlotScripted(f->unit,f->second,0));
    vec2_t point={512,0};
    T_ASSERT(G_IssueUnitPointOrder(f->unit,"move",&point,false,0,0));
    T_ASSERT(f->unit->current_order_id!=0);
    T_FEQ(S_PreparatoryManaPotential(f->unit),200,0);
}
static void PreparatoryEnd(preparatoryFixture_t *f) {
    reset_entities();G_SetSLKRows("AbilityData",f->old_rows);free_slk_rows(f->rows);
    level.mapinfo=f->old_info;
}
static FILE *PreparatorySave(void) {
    FILE *file=tmpfile();T_NOT_NULL(file);
    if(file)T_ASSERT(S_WritePreparationItems(file));
    return file;
}
static void PreparatoryLoad(FILE *file) {
    T_ASSERT(file!=NULL);if(!file)return;
    S_ResetSpellPreparation();rewind(file);T_ASSERT(S_ReadPreparationItems(file));
}
static void PreparatoryConsume(preparatoryFixture_t *f,bool first) {
    f->unit->mana.value=0;
    uint32_t a=f->first->item->charges,b=f->second->item->charges;
    T_ASSERT(S_PrepareSpellMana(f->unit,MAKEFOURCC('Z','c','s','t'),1));
    T_FEQ(f->unit->mana.value,first?80:120,0);
    T_EQ(f->first->item->charges,a-(first?1u:0u));
    T_EQ(f->second->item->charges,b-(first?0u:1u));
    T_ASSERT(!S_SpellPreparationPending());
}

TEST(wc3_preparatory_items, real_attachments_choose_identity_not_inventory_and_replay_saved_reuse) {
    preparatoryFixture_t f;PreparatoryBegin(&f);
    FILE *held=PreparatorySave();
    PreparatoryConsume(&f,false); /* second identity is in slot0, not slot5. */
    G_SetItemCharges(f.first,3);G_SetItemCharges(f.second,3);
    PreparatoryLoad(held);PreparatoryConsume(&f,false);if(held)fclose(held);
    G_SetItemCharges(f.first,3);G_SetItemCharges(f.second,3);
    vec2_t point={32,0};
    T_ASSERT(G_DropItemAtScripted(f.unit,5,&point));
    T_ASSERT(G_DropItemAtScripted(f.unit,0,&point));
    FILE *free_slots=PreparatorySave();
    for(unsigned replay=0;replay<2;replay++) {
        if(replay)PreparatoryLoad(free_slots);
        T_ASSERT(G_AddItemToSlotScripted(f.unit,f.first,0));
        T_ASSERT(G_AddItemToSlotScripted(f.unit,f.second,5));
        /* LIFO reuse gives first the higher freed identity; newest attachment
         * and reverse inventory order would both incorrectly choose second. */
        PreparatoryConsume(&f,true);
        T_ASSERT(G_DropItemAtScripted(f.unit,0,&point));
        T_ASSERT(G_DropItemAtScripted(f.unit,5,&point));
        G_SetItemCharges(f.first,3);G_SetItemCharges(f.second,3);
    }
    if (free_slots) fclose(free_slots);
    PreparatoryEnd(&f);
}

TEST(wc3_preparatory_items, corrupt_registry_rejects_missing_attachment_reference_and_free_cycle) {
    preparatoryFixture_t f;PreparatoryBegin(&f);
    FILE *empty=tmpfile();T_NOT_NULL(empty);
    if(empty) {
        uint32_t header[3]={0,0,1};T_EQ(fwrite(header,sizeof(header),1,empty),1u);
        rewind(empty);T_ASSERT(!S_ReadPreparationItems(empty));fclose(empty);
        T_FEQ(S_PreparatoryManaPotential(f.unit),200,0);
    }
    FILE *invalid=PreparatorySave();
    if(invalid) {
        /* First serialized row's item index is one-based. */
        uint32_t bad=globals.num_edicts+1;
        T_EQ(fseek(invalid,3*sizeof(uint32_t),SEEK_SET),0);
        T_EQ(fwrite(&bad,sizeof(bad),1,invalid),1u);rewind(invalid);
        T_ASSERT(!S_ReadPreparationItems(invalid));fclose(invalid);
        T_FEQ(S_PreparatoryManaPotential(f.unit),200,0);
    }
    vec2_t point={32,0};T_ASSERT(G_DropItemAtScripted(f.unit,5,&point));
    FILE *cycle=PreparatorySave();
    if(cycle) {
        uint32_t header[3];rewind(cycle);T_EQ(fread(header,sizeof(header),1,cycle),1u);
        T_ASSERT(header[1]>0);
        /* Saved rows contain eight scalar words; next_free is the last. */
        long next_free=(long)(3+(header[1]-1)*8+7)*sizeof(uint32_t);
        T_EQ(fseek(cycle,next_free,SEEK_SET),0);
        T_EQ(fwrite(&header[1],sizeof(uint32_t),1,cycle),1u);rewind(cycle);
        T_ASSERT(!S_ReadPreparationItems(cycle));fclose(cycle);
        T_FEQ(S_PreparatoryManaPotential(f.unit),120,0);
        PreparatoryConsume(&f,false);
    }
    PreparatoryEnd(&f);
}
#endif
