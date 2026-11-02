/* CommonMorph374: production archive/INI path and retail-proven availability. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
edict_t *alloc_test_unit(uint32_t,float,float);
void reset_entities(void);
void setup_test_world(void);
void unit_stand(edict_t *);
void unit_die(edict_t *,edict_t *);
bool unit_issueimmediateorder(edict_t *,cstring_t);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);
extern cstring_t miscdata_files[];

/* Same priority-archive and production file list used by map initialization. */
typedef struct { handle_t archive,bytes;stbIniCache_t saved;char prefix[sizeof(game.data_prefix)]; } m374Config_t;
static m374Config_t m374_config(unsigned setting) {
    m374Config_t scope={.saved=game.config.misc};uint32_t size=0;
    char const *names[]={"Absent","Zero","One"};char path[80];
    strlcpy(scope.prefix,game.data_prefix,sizeof(scope.prefix));game.data_prefix[0]=0;
    snprintf(path,sizeof(path),"Maps\\Morph374%s.w3x",names[setting]);
    scope.bytes=gi.ReadFile(path,&size);T_NOT_NULL(scope.bytes);
    T_ASSERT(SFileOpenArchiveFromMemory(scope.bytes,size,0,&scope.archive));
    gi.SetPriorityArchive(scope.archive);game.config.misc=(stbIniCache_t){0};
    T_ASSERT(Stb_IniCacheLoadFiles(&game.config.misc,miscdata_files));
    T_STREQ(Stb_IniCacheFind(&game.config.misc,"Misc","Morph374Fixture"),names[setting]);
    cstring_t value=Stb_IniCacheFind(&game.config.misc,"Misc","MorphAlternateDisable");
    if(setting==0)T_NULL(value);
    else T_STREQ(value,setting==2 ? "1" : "0");
    return scope;
}
static void m374_restore_config(m374Config_t *scope) {
    Stb_IniCacheFree(&game.config.misc);game.config.misc=scope->saved;
    gi.SetPriorityArchive(NULL);SFileCloseArchive(scope->archive);gi.MemFree(scope->bytes);
    strlcpy(game.data_prefix,scope->prefix,sizeof(game.data_prefix));
}
static void m374_install_presentation(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Amrf\"\n"
        "C;Y2;X2;K\"Amrf\"\n"
        "C;Y2;X3;K\"hpea\"\n"
        "C;Y2;X4;K\"hfoo\"\n"
        "C;Y3;X1;K\"Arav\"\n"
        "C;Y3;X2;K\"Arav\"\n"
        "C;Y3;X3;K\"edot\"\n"
        "C;Y3;X4;K\"edtm\"\n"
        "C;Y4;X1;K\"Astn\"\n"
        "C;Y4;X2;K\"Astn\"\n"
        "C;Y4;X3;K\"hpea\"\n"
        "C;Y4;X4;K\"hfoo\"\n"
        "E\n";
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\n"
        "C;Y1;X4;K\"scale\"\n"
        "C;Y1;X5;K\"red\"\n"
        "C;Y1;X6;K\"green\"\n"
        "C;Y1;X7;K\"blue\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\n"
        "C;Y2;X4;K1\n"
        "C;Y2;X5;K224\n"
        "C;Y2;X6;K232\n"
        "C;Y2;X7;K255\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\n"
        "C;Y3;X4;K1\n"
        "E\n";
    static cstring_t const profile_slk =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"unitID\"\n"
        "C;Y1;X2;K\"animProps\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"\"\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"alternateex\"\n"
        "E\n";

    *ability_rows = parse_slk_string(ability_slk);
    *ui_rows = parse_slk_string(ui_slk);
    *profile_rows = parse_slk_string(profile_slk);
    T_NOT_NULL(*ability_rows); T_NOT_NULL(*ui_rows); T_NOT_NULL(*profile_rows);
    *old_ability = G_SetSLKRows("AbilityData", *ability_rows);
    *old_ui = G_SetSLKRows("UnitUI", *ui_rows);
    *old_profile = G_SetProfileRows(*profile_rows);
    T_NOT_NULL(*old_ability); T_NOT_NULL(*old_ui); T_NOT_NULL(*old_profile);
}

static void m374_restore_presentation(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

static void m374_advance(float deadline) {
    abilityTimerRequest_t request;
    unsigned fired=0;
    while(S_NextAbilityPrimaryTimer(&request) && request.deadline.time<=deadline) {
        T_ASSERT(++fired<128);if(fired>=128)break;
        level.pathing_clock=request.deadline;
        abilityCall_t call={.primary_timer=&request};
        request.proc(NULL,A_PRIMARY_TIMER_FIRE,&call);
    }
    level.pathing_clock.time=deadline;
}

/* Actual alternate movement policy is required for the takeoff/landing frontier. */
static mapInfo_t const *m374_flight(bool flying) {
    static mapInfo_t info;
    static unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"};
    static unitData_t alt={.originalUnitID=MAKEFOURCC('h','f','o','o'),.numbeOfModifications=1,.modifications=&mod};
    mapInfo_t const *old=level.mapinfo;info=*old;
    info.num_originalUnits=flying ? 1 : 0;info.originalUnits=flying ? &alt : NULL;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);return old;
}
static slkTestData_t *m374_data(unsigned schema,unsigned family) {
    char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\n"
        "C;X1;Y2;K\"Ms74\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\nC;X7;K1\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",
        schema ? "Data11" : "DataA1",schema ? "Data13" : "DataC1",family ? "Arav" : "Abrf");
    slkTestData_t *data=parse_slk_string(text);G_SetSLKRows("AbilityData",data);
    T_FEQ(G_AbilityLevel(FS_SLKKey("Ms74"),1)->data[2].number,1,.000001f);
    return data;
}
static edict_t *m374_unit(bool alternate) {
    edict_t *unit=alloc_test_unit(FS_SLKKey(alternate ? "hfoo" : "hpea"),64,64);
    unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};return unit;
}
static void m374_passive(edict_t *unit) {
    T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Apiv")));
    T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Apiv"),true));
}
static void m374_save(edict_t **unit) {
    uint32_t slot=*unit-g_edicts;cstring_t path=Test_TempPath("morph374-inprocess.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);*unit=g_edicts+slot;
}

TEST(wc3_unit,morph374_real_misc_archive_controls_bear_and_takeoff_availability) {
    for(unsigned setting=0;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned family=0;family<2;family++)for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<3;saved++) {
        reset_entities();setup_test_world();m374Config_t config=m374_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m374_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m374_data(schema,family);mapInfo_t const *oldmap=m374_flight(family);
        uint32_t code=FS_SLKKey("Ms74"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m374_unit(mode==2);
        /* Initially alternate attachment must not consume negative transient depths. */
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(mode==0){m374_passive(unit);m374_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2) {
            T_ASSERT(unit_issueimmediateorder(unit,family ? "ravenform" : "bearform"));
            T_ASSERT(unit->morph && unit->morph->transition.active);
            if(mode==0)T_EQ(S_AbilityExecutionDisabled(unit,apiv),held && family);
            m374_advance(2.374f);
            if(mode==0)T_EQ(S_AbilityExecutionDisabled(unit,apiv),held && family);
            m374_advance(2.375f);
        }
        if(mode!=0){T_ASSERT(!S_PermanentInvisibilityActive(unit));m374_passive(unit);}
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->permanent_invisibility_fade.request.active,!held && mode!=0);
        if(!held && mode==0)T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,code));
        T_EQ(unit->ability_exclusive_depth,held ? 1 : 0);
        if(saved==1){m374_save(&unit);T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);}
        level.pathing_clock.time=3;T_ASSERT(unit_issueimmediateorder(unit,family ? "unravenform" : "unbearform"));
        float end=family ? 4.375f : 3.375f;
        if(saved==2){m374_advance(3.125f);m374_save(&unit);}
        m374_advance(end-.001f);T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(held)T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        m374_advance(end);T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->ability_exclusive_depth,0);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,end,.000001f);}
        m374_advance(end+.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();level.mapinfo=oldmap;G_SetMapUnitOverrides(oldmap);
        m374_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m374_restore_config(&config);
    }
}

TEST(wc3_unit,morph374_detach_releases_only_its_configured_owner) {
    for(unsigned setting=0;setting<3;setting++)for(unsigned family=0;family<2;family++)
    for(unsigned phase=0;phase<4;phase++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m374Config_t config=m374_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m374_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m374_data(0,family);mapInfo_t const *oldmap=m374_flight(family);
        uint32_t code=FS_SLKKey("Ms74"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m374_unit(phase==3);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));m374_passive(unit);
        if(phase!=3){m374_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));level.pathing_clock.time=2;
            T_ASSERT(unit_issueimmediateorder(unit,family ? "ravenform" : "bearform"));}
        m374_advance(phase==1 ? 2.125f : 2.375f);
        if(phase==2){level.pathing_clock.time=2.5f;T_ASSERT(unit_issueimmediateorder(unit,family ? "unravenform" : "unbearform"));m374_advance(2.625f);}
        if(phase!=1)T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(saved)m374_save(&unit);
        float detached=level.pathing_clock.time;
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!unit->morph || !unit->morph->transition.active);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_EQ(unit->ability_exclusive_depth,0);
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,detached,.000001f);}
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));m374_passive(unit);
        m374_advance(detached+.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        reset_entities();level.mapinfo=oldmap;G_SetMapUnitOverrides(oldmap);
        m374_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m374_restore_config(&config);
    }
}
/* Rebind refresh consumes only an existing contribution. A passive introduced
 * by the destination type must begin and retain its own authored fade. */
TEST(wc3_unit,morph374_destination_only_authored_passive_keeps_its_fade) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned pending=0;pending<2;pending++) {
        reset_entities();setup_test_world();m374Config_t config=m374_config(1);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m374_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m374_data(schema,0);
        mapInfo_t const *oldmap=level.mapinfo;mapInfo_t info=*oldmap;
        unitModification_t mod={.modID=MAKEFOURCC('u','a','b','i'),.type=mod_string,.data="Apiv"};
        unitData_t alternate={.originalUnitID=MAKEFOURCC('h','f','o','o'),.numbeOfModifications=1,.modifications=&mod};
        info.num_originalUnits=1;info.originalUnits=&alternate;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        edict_t *unit=m374_unit(false);
        T_ASSERT(!G_ActorHasAbilityCode(unit,FS_SLKKey("Apiv")));
        T_ASSERT(G_TransformUnitType(unit,FS_SLKKey("hfoo")));
        T_ASSERT(G_ActorHasAbilityCode(unit,FS_SLKKey("Apiv")));
        T_ASSERT(unit->permanent_invisibility_fade.request.active);
        T_ASSERT(!S_PermanentInvisibilityActive(unit));
        T_FEQ(unit->permanent_invisibility_fade.origin.time,1,.000001f);
        if(!pending){m374_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        T_ASSERT(G_TransformUnitType(unit,FS_SLKKey("hpea")));
        T_ASSERT(!G_ActorHasAbilityCode(unit,FS_SLKKey("Apiv")));
        T_ASSERT(!S_PermanentInvisibilityActive(unit));
        T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        m374_advance(2.75f);T_ASSERT(!S_PermanentInvisibilityActive(unit));
        reset_entities();level.mapinfo=oldmap;G_SetMapUnitOverrides(oldmap);
        m374_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m374_restore_config(&config);
    }
}
#endif
