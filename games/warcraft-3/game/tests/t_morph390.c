/* Payoff390 Stone Form: production archive/INI path and retail-proven availability. */
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
typedef struct { handle_t archive,bytes;stbIniCache_t saved;char prefix[sizeof(game.data_prefix)]; } m390Config_t;
static m390Config_t m390_config(unsigned setting) {
    m390Config_t scope={.saved=game.config.misc};uint32_t size=0;
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
static void m390_restore_config(m390Config_t *scope) {
    Stb_IniCacheFree(&game.config.misc);game.config.misc=scope->saved;
    gi.SetPriorityArchive(NULL);SFileCloseArchive(scope->archive);gi.MemFree(scope->bytes);
    strlcpy(game.data_prefix,scope->prefix,sizeof(game.data_prefix));
}
static void m390_install_presentation(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"DataA1\"\nC;X4;K\"UnitID1\"\n"
        "C;X1;Y2;K\"Astn\"\nC;X2;K\"Astn\"\n"
        "C;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nE\n";
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
        "C;Y3;X2;K\"\"\n"
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

static void m390_restore_presentation(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

static void m390_advance(float deadline) {
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

/* Payoff390: concrete StoneForm primary6fafeb3c v41c ->54c640. The forward
 * order lands (deferred rebind after Dur+DataC=.875); the reverse order takes
 * off and rebinds immediately, then562c50 ->54ff50 ->+404 releases before the
 * .375 separation transition. Initial alternate attachment acquires synchronously.
 * Reuse Morph374's real nested MPQs; validate their literal metadata above. */
typedef struct { slkTestData_t *data;mapInfo_t const *saved_map;mapInfo_t info;unitModification_t mod;unitData_t flying; } m390Data_t;
static void m390_data(m390Data_t *scope,unsigned schema) {
    char text[1400];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"%s\"\n"
        "C;X1;Y2;K\"Ms90\"\nC;X2;K\"Astn\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\nC;X7;K7\nC;X8;K0.5\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1",schema ? "Data13" : "DataC1");
    scope->data=parse_slk_string(text);T_NOT_NULL(scope->data);G_SetSLKRows("AbilityData",scope->data);
    abilityLevel_t const *loaded=G_AbilityLevel(FS_SLKKey("Ms90"),1);
    T_EQ(loaded->data[0].id,FS_SLKKey("hpea"));T_EQ(loaded->unitID,FS_SLKKey("hfoo"));
    T_FEQ(loaded->data[1].number,7,.000001f);T_FEQ(loaded->data[2].number,.5f,.000001f);
    T_FEQ(loaded->dur,.375f,.000001f);T_FEQ(loaded->heroDur,0,.000001f);
    /* Stock Gargoyle: flying base, ground stone form. */
    scope->saved_map=level.mapinfo;scope->info=*scope->saved_map;
    scope->mod=(unitModification_t){.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"};
    scope->flying=(unitData_t){.originalUnitID=FS_SLKKey("hpea"),.numbeOfModifications=1,.modifications=&scope->mod};
    scope->info.num_originalUnits=1;scope->info.originalUnits=&scope->flying;
    level.mapinfo=&scope->info;G_SetMapUnitOverrides(&scope->info);
}
static void m390_restore_data(m390Data_t *scope) {
    level.mapinfo=scope->saved_map;G_SetMapUnitOverrides(scope->saved_map);free_slk_rows(scope->data);
}
static edict_t *m390_unit(bool alternate) {
    edict_t *unit=alloc_test_unit(FS_SLKKey(alternate ? "hfoo" : "hpea"),64,64);
    unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
    if(!alternate)unit->aiflags|=AI_FLYING;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};return unit;
}
static void m390_passive(edict_t *unit) {
    T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Apiv")));
    T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Apiv"),true));
}
static void m390_save(edict_t **unit) {
    uint32_t slot=*unit-g_edicts;cstring_t path=Test_TempPath("morph390-inprocess.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);*unit=g_edicts+slot;
}
TEST(wc3_unit,morph390_real_misc_stone_form_takeoff_releases_at_rebind) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<3;saved++) {
        reset_entities();setup_test_world();m390Config_t config=m390_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m390_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        m390Data_t data;m390_data(&data,schema);
        uint32_t code=FS_SLKKey("Ms90"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m390_unit(mode==2);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        /* Native initial-alt acquisition is already complete at AddAbility return. */
        T_EQ(unit->ability_exclusive_depth,mode==2 && held ? 1 : 0);
        if(mode==0){m390_passive(unit);m390_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2) {
            T_ASSERT(unit_issueimmediateorder(unit,"stoneform"));
            T_EQ(unit->class_id,FS_SLKKey("hpea"));
            T_ASSERT(unit->morph && unit->morph->transition.active);
            T_EQ(unit->ability_exclusive_depth,0);
            if(mode==0)T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
            m390_advance(2.874f);
            T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->ability_exclusive_depth,0);
            if(mode==0)T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
            m390_advance(2.875f);
        }
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(unit->ability_exclusive_depth,held ? 1 : 0);
        if(mode!=0){m390_advance(3);m390_passive(unit);}
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->permanent_invisibility_fade.request.active,!held && mode!=0);
        T_EQ(S_PermanentInvisibilityActive(unit),!held && mode==0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,code));
        m390_advance(3.75f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(saved==1){m390_save(&unit);T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);}
        /* Takeoff rebinds to the base before the transition: release is immediate. */
        level.pathing_clock.time=4;T_ASSERT(unit_issueimmediateorder(unit,"unstoneform"));
        T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(unit->morph && unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,1);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_EQ(unit->ability_exclusive_depth,0);
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,4,.000001f);}
        else T_ASSERT(S_PermanentInvisibilityActive(unit));
        if(saved==2){m390_advance(4.125f);m390_save(&unit);}
        m390_advance(4.374f);T_ASSERT(unit->morph && unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,1);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m390_advance(4.375f);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_EQ(unit->movement.repulse.disable_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_EQ(unit->ability_exclusive_depth,0);
        m390_advance(4.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m390_advance(4.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m390_restore_presentation(a,oa,ui,oui,p,op);m390_restore_data(&data);m390_restore_config(&config);
    }
}
/* Payoff390d: StoneForm+a4 6f500230 calls common540e30 before its own clock
 * cancel. Pending landing detach completes the forward rebind, restores the base
 * while held, then releases and restarts the passive fade. */
TEST(wc3_unit,morph390_stone_detach_restores_base_before_configured_passive_fade) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned pending=0;pending<2;pending++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m390Config_t config=m390_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m390_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        m390Data_t data;m390_data(&data,schema);
        uint32_t code=FS_SLKKey("Ms90"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m390_unit(false);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        m390_passive(unit);m390_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"stoneform"));
        m390_advance(pending ? 2.125f : 3.0f);
        T_EQ(unit->class_id,FS_SLKKey(pending ? "hpea" : "hfoo"));
        T_ASSERT(unit->morph);if(unit->morph)T_EQ(unit->morph->transition.active,pending);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),!pending && held);
        if(saved)m390_save(&unit);
        float detached=level.pathing_clock.time;
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!G_ActorHasAbilityCode(unit,code));
        T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
        T_ASSERT(!unit->morph || !unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->permanent_invisibility_fade.request.active,held);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(held)T_FEQ(unit->permanent_invisibility_fade.origin.time,detached,.000001f);
        m390_advance(detached+.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m390_advance(detached+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m390_restore_presentation(a,oa,ui,oui,p,op);m390_restore_data(&data);m390_restore_config(&config);
    }
}
#endif
