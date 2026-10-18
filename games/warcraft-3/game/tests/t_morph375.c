/* Payoff375 Robo-Goblin: production archive/INI path and retail-proven availability. */
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
typedef struct { handle_t archive,bytes;stbIniCache_t saved;char prefix[sizeof(game.data_prefix)]; } m375Config_t;
static m375Config_t m375_config(unsigned setting) {
    m375Config_t scope={.saved=game.config.misc};uint32_t size=0;
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
static void m375_restore_config(m375Config_t *scope) {
    Stb_IniCacheFree(&game.config.misc);game.config.misc=scope->saved;
    gi.SetPriorityArchive(NULL);SFileCloseArchive(scope->archive);gi.MemFree(scope->bytes);
    strlcpy(game.data_prefix,scope->prefix,sizeof(game.data_prefix));
}
static void m375_install_presentation(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"DataA1\"\nC;X4;K\"UnitID1\"\n"
        "C;X1;Y2;K\"ANrg\"\nC;X2;K\"ANrg\"\n"
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

static void m375_restore_presentation(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

static void m375_advance(float deadline) {
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

/* Payoff375: concrete RoboGoblin v41c ->54c640; both directions are
 * deferred .375 transitions. Initial alternate attachment is synchronous.
 * Reuse Morph374's real nested MPQs; validate their literal metadata above. */
static slkTestData_t *m375_data(unsigned schema) {
    char text[1300];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\n"
        "C;X1;Y2;K\"Ms75\"\nC;X2;K\"ANrg\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\nC;X7;K1\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1");
    slkTestData_t *data=parse_slk_string(text);T_NOT_NULL(data);G_SetSLKRows("AbilityData",data);
    abilityLevel_t const *loaded=G_AbilityLevel(FS_SLKKey("Ms75"),1);
    T_EQ(loaded->data[0].id,FS_SLKKey("hpea"));T_EQ(loaded->unitID,FS_SLKKey("hfoo"));
    T_FEQ(loaded->data[1].number,1,.000001f);T_FEQ(loaded->dur,.375f,.000001f);T_FEQ(loaded->heroDur,0,.000001f);
    return data;
}
static edict_t *m375_unit(bool alternate) {
    edict_t *unit=alloc_test_unit(FS_SLKKey(alternate ? "hfoo" : "hpea"),64,64);
    unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};return unit;
}
static void m375_passive(edict_t *unit) {
    T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Apiv")));
    T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Apiv"),true));
}
static void m375_save(edict_t **unit) {
    uint32_t slot=*unit-g_edicts;cstring_t path=Test_TempPath("morph375-inprocess.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);*unit=g_edicts+slot;
}
TEST(wc3_unit,morph375_real_misc_archive_controls_robo_goblin_availability) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<3;saved++) {
        reset_entities();setup_test_world();m375Config_t config=m375_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m375_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m375_data(schema);
        uint32_t code=FS_SLKKey("Ms75"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m375_unit(mode==2);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        /* Native initial-alt acquisition is already complete at AddAbility return. */
        T_EQ(unit->ability_exclusive_depth,mode==2 && held ? 1 : 0);
        if(mode==0){m375_passive(unit);m375_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2) {
            T_ASSERT(unit_issueimmediateorder(unit,"robogoblin"));
            T_EQ(unit->class_id,FS_SLKKey("hpea"));
            T_ASSERT(unit->morph && unit->morph->transition.active);
            T_EQ(unit->ability_exclusive_depth,0);
            if(mode==0)T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
            m375_advance(2.374f);
            T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->ability_exclusive_depth,0);
            if(mode==0)T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
            m375_advance(2.375f);
        }
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(unit->ability_exclusive_depth,held ? 1 : 0);
        if(mode!=0){m375_advance(2.5f);m375_passive(unit);}
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->permanent_invisibility_fade.request.active,!held && mode!=0);
        T_EQ(S_PermanentInvisibilityActive(unit),!held && mode==0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,code));
        m375_advance(3.25f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(saved==1){m375_save(&unit);T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);}
        level.pathing_clock.time=3.5f;T_ASSERT(unit_issueimmediateorder(unit,"unrobogoblin"));
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(saved==2){m375_advance(3.625f);m375_save(&unit);}
        m375_advance(3.874f);T_EQ(unit->class_id,FS_SLKKey("hfoo"));
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(held)T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        m375_advance(3.875f);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_EQ(unit->ability_exclusive_depth,0);
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,3.875f,.000001f);}
        else T_ASSERT(S_PermanentInvisibilityActive(unit));
        m375_advance(4.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m375_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m375_restore_config(&config);
    }
}
/* Payoff375d: concrete RoboGoblin+a4 routes both pending and active detach
 * through540e30; configured inverse restarts Apiv only after base restoration. */
TEST(wc3_unit,morph375_robo_detach_restores_base_before_configured_passive_fade) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned pending=0;pending<2;pending++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m375Config_t config=m375_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m375_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m375_data(schema);
        uint32_t code=FS_SLKKey("Ms75"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m375_unit(false);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        m375_passive(unit);m375_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"robogoblin"));
        m375_advance(pending ? 2.125f : 2.5f);
        T_EQ(unit->class_id,FS_SLKKey(pending ? "hpea" : "hfoo"));
        T_EQ(unit->morph->transition.active,pending);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),!pending && held);
        if(saved)m375_save(&unit);
        float detached=level.pathing_clock.time;
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!G_ActorHasAbilityCode(unit,code));
        T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
        T_ASSERT(!unit->morph || !unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->permanent_invisibility_fade.request.active,held);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(held)T_FEQ(unit->permanent_invisibility_fade.origin.time,detached,.000001f);
        m375_advance(detached+.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m375_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m375_restore_config(&config);
    }
}
#endif
