/* Payoff382 Metamorphosis: production archive/INI path and retail-proven availability. */
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
typedef struct { handle_t archive,bytes;stbIniCache_t saved;char prefix[sizeof(game.data_prefix)]; } m382Config_t;
static m382Config_t m382_config(unsigned setting) {
    m382Config_t scope={.saved=game.config.misc};uint32_t size=0;
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
static void m382_restore_config(m382Config_t *scope) {
    Stb_IniCacheFree(&game.config.misc);game.config.misc=scope->saved;
    gi.SetPriorityArchive(NULL);SFileCloseArchive(scope->archive);gi.MemFree(scope->bytes);
    strlcpy(game.data_prefix,scope->prefix,sizeof(game.data_prefix));
}
static void m382_install_presentation(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"DataA1\"\nC;X4;K\"UnitID1\"\n"
        "C;X1;Y2;K\"AEme\"\nC;X2;K\"AEme\"\n"
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

static void m382_restore_presentation(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

static void m382_advance(float deadline) {
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

/* Payoff382: independently captured concrete AEme6fb6fa20 v41c ->54c640; both transitions are
 * deferred .375 and alternate lifetime is2.5. Initial alternate attachment
 * schedules lifetime synchronously. Native62dff0 also creates source-linked
 * BEme; this regression scopes passive availability and primary lifetime,
 * without claiming health bonus or buff-publication parity.
 * Reuse Morph374's real nested MPQs; validate their literal metadata above. */
static slkTestData_t *m382_data(unsigned schema) {
    char text[1300];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"BuffID1\"\n"
        "C;X1;Y2;K\"AEme\"\nC;X2;K\"AEme\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K2.5\nC;X7;K1\nC;X8;K\"BEme\"\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1");
    slkTestData_t *data=parse_slk_string(text);T_NOT_NULL(data);G_SetSLKRows("AbilityData",data);
    abilityLevel_t const *loaded=G_AbilityLevel(FS_SLKKey("AEme"),1);
    T_EQ(loaded->data[0].id,FS_SLKKey("hpea"));T_EQ(loaded->unitID,FS_SLKKey("hfoo"));
    T_STREQ(loaded->buffID,"BEme");T_FEQ(loaded->data[1].number,1,.000001f);T_FEQ(loaded->dur,.375f,.000001f);T_FEQ(loaded->heroDur,2.5f,.000001f);
    return data;
}
static edict_t *m382_unit(bool alternate) {
    edict_t *unit=alloc_test_unit(FS_SLKKey(alternate ? "hfoo" : "hpea"),64,64);
    unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};return unit;
}
static void m382_passive(edict_t *unit) {
    T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Apiv")));
    T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Apiv"),true));
}
static void m382_save(edict_t **unit) {
    uint32_t slot=*unit-g_edicts;cstring_t path=Test_TempPath("morph382-inprocess.bin");
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);*unit=g_edicts+slot;
}
/* Literal native3c/40: ordinary configured BEme1/1; initial-alt BEme0/0.
 * Missing zero-valued sparse owners are implicit zeros until a producer needs them. */
static void m382_expect_counts(edict_t *unit,uint32_t code,int execution,int orders,bool require_owner) {
    unitAbilityDisable_t *owner=NULL;
    for(unitAbilityDisable_t *s=unit->ability_disables;s;s=s->next)if(s->linked && s->code==code){owner=s;break;}
    if(require_owner)T_NOT_NULL(owner);
    T_EQ(owner ? owner->depth : 0,execution);T_EQ(owner ? owner->order_depth : 0,orders);
}
TEST(wc3_unit,morph382_real_misc_metamorphosis_timed_availability) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<3;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_data(schema);
        uint32_t code=FS_SLKKey("AEme"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m382_unit(mode==2);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),mode==2 ? 1 : 0);
        T_EQ(unit->ability_exclusive_depth,mode==2 && held ? 1 : 0);
        float reverse_start=mode==2 ? 3.5f : 4.875f;
        float reverse_done=reverse_start+.375f;
        if(mode==2) {
            T_ASSERT(unit->morph && unit->morph->expiration.active);
            if(unit->morph)T_FEQ(unit->morph->expiration.deadline.time,reverse_start,.000001f);
        }
        if(mode==0){m382_passive(unit);m382_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2) {
            T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));
            T_EQ(unit->class_id,FS_SLKKey("hpea"));
            T_ASSERT(unit->morph && unit->morph->transition.active);
            T_EQ(unit->ability_exclusive_depth,0);
            if(mode==0)T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
            m382_advance(2.374f);
            T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->ability_exclusive_depth,0);
            m382_advance(2.375f);
        }
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),1);T_EQ(unit->ability_exclusive_depth,held ? 1 : 0);
        T_ASSERT(unit->morph && unit->morph->expiration.active);
        if(unit->morph)T_FEQ(unit->morph->expiration.deadline.time,reverse_start,.000001f);
        if(mode!=0){m382_advance(2.5f);m382_passive(unit);}
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->permanent_invisibility_fade.request.active,!held && mode!=0);
        T_EQ(S_PermanentInvisibilityActive(unit),!held && mode==0);
        T_EQ(S_AbilityExecutionDisabled(unit,code),held && mode==2);
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        m382_expect_counts(unit,FS_SLKKey("BEme"),held && mode!=2 ? 1 : 0,held && mode!=2 ? 1 : 0,held);
        m382_advance(3.25f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(saved==1){m382_save(&unit);m382_expect_counts(unit,FS_SLKKey("BEme"),held && mode!=2 ? 1 : 0,held && mode!=2 ? 1 : 0,held);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),1);T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);}
        m382_advance(reverse_start-.001f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(unit->movement.repulse.disable_depth,0);
        m382_advance(reverse_start);
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(unit->movement.repulse.disable_depth,1);
        T_ASSERT(unit->morph && unit->morph->transition.active);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(saved==2){m382_advance(reverse_start+.125f);m382_save(&unit);}
        m382_advance(reverse_done-.001f);T_EQ(unit->class_id,FS_SLKKey("hfoo"));
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        if(held)T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        m382_advance(reverse_done);T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        T_EQ(unit->movement.repulse.disable_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_EQ(unit->ability_exclusive_depth,0);
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,reverse_done,.000001f);}
        else T_ASSERT(S_PermanentInvisibilityActive(unit));
        m382_advance(reverse_done+.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m382_advance(reverse_done+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}

static abilityProc_t m382_original_proc;
static unsigned m382_forced_completions;
static bool m382_forced_alternate,m382_forced_lifetime;
static float m382_forced_deadline;
void S_ReplaceAbilityProcedure(ability_t const *,abilityProc_t);
static intptr_t m382_observe_completion(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    intptr_t result=m382_original_proc(unit,msg,call);
    if(msg==A_MORPH_COMPLETE && !call->enabled) {
        m382_forced_completions++;
        m382_forced_alternate=unit->class_id==FS_SLKKey("hfoo");
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),1);
        m382_forced_lifetime=unit->morph && unit->morph->expiration.active;
        if(unit->morph)m382_forced_deadline=unit->morph->expiration.deadline.time;
    }
    return result;
}

TEST(wc3_unit,morph382_metamorphosis_detach_completes_forward_timer_then_inverts) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned pending=0;pending<2;pending++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_data(schema);
        uint32_t code=FS_SLKKey("AEme"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m382_unit(false);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        m382_passive(unit);m382_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));
        m382_advance(pending ? 2.125f : 2.5f);
        T_EQ(unit->class_id,FS_SLKKey(pending ? "hpea" : "hfoo"));
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),pending ? 0 : 1);
        T_ASSERT(unit->morph);if(unit->morph)T_EQ(unit->morph->transition.active,pending);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),!pending && held);
        if(!pending && unit->morph){T_ASSERT(unit->morph->expiration.active);T_FEQ(unit->morph->expiration.deadline.time,4.875f,.000001f);}
        if(saved)m382_save(&unit);
        float detached=level.pathing_clock.time;
        abilityitem_t item=S_AbilityItem(code);T_NOT_NULL(item.ability);
        if(item.ability) {
            m382_original_proc=item.ability->proc;
            m382_forced_completions=0;m382_forced_alternate=m382_forced_lifetime=false;m382_forced_deadline=0;
            S_ReplaceAbilityProcedure(item.ability,m382_observe_completion);
            T_ASSERT(G_ActorRemoveSkill(unit,code));
            S_ReplaceAbilityProcedure(item.ability,m382_original_proc);
            T_EQ(m382_forced_completions,pending ? 1 : 0);
            if(pending){T_ASSERT(m382_forced_alternate);T_ASSERT(m382_forced_lifetime);T_FEQ(m382_forced_deadline,detached+2.5f,.000001f);}
        }
        T_ASSERT(!G_ActorHasAbilityCode(unit,code));T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->permanent_invisibility_fade.request.active,held);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(held)T_FEQ(unit->permanent_invisibility_fade.origin.time,detached,.000001f);
        m382_advance(detached+.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m382_advance(detached+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        m382_advance(detached+3);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}
bool run_test_jass(cstring_t);
extern void (*test_preload_marker)(cstring_t);
static void (*m382_previous_marker)(cstring_t);
static unsigned m382_public_markers;
static int m382_public_before,m382_public_accepted,m382_public_after,m382_public_source;
static void m382_public_marker(cstring_t value) {
    int before,accepted,after,source;
    if(sscanf(value,"P382PUBLIC before=%d accepted=%d after=%d source=%d",&before,&accepted,&after,&source)==4) {
        m382_public_markers++;m382_public_before=before;m382_public_accepted=accepted;
        m382_public_after=after;m382_public_source=source;
    } else if(m382_previous_marker)m382_previous_marker(value);
}
static void m382_remove_public_buff_named(edict_t *unit,cstring_t source,cstring_t buff) {
    unit->user_data=70382;m382_public_markers=0;
    m382_public_before=m382_public_accepted=m382_public_after=m382_public_source=-1;
    m382_previous_marker=test_preload_marker;test_preload_marker=m382_public_marker;
    char script[4096];snprintf(script,sizeof(script),
        "globals\ninteger matched=0\nendglobals\n"
        "function RemoveOwnedBuff takes nothing returns nothing\nlocal unit u=GetEnumUnit()\n"
        "local integer before=0\nlocal integer accepted=0\n"
        "if GetUnitUserData(u)==70382 then\nset matched=matched+1\n"
        "set before=GetUnitAbilityLevel(u,'%s')\n"
        "if UnitRemoveAbility(u,'%s') then\nset accepted=1\nendif\n"
        "call Preload(\"P382PUBLIC before=\"+I2S(before)+\" accepted=\"+I2S(accepted)+\" after=\"+I2S(GetUnitAbilityLevel(u,'%s'))+\" source=\"+I2S(GetUnitAbilityLevel(u,'%s')))\n"
        "endif\nset u=null\nendfunction\n"
        "function main takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(0),null)\ncall ForGroup(g,function RemoveOwnedBuff)\n"
        "call BJassAssert(matched==1,\"exact owned target enumerated\")\ncall DestroyGroup(g)\nset g=null\nendfunction\n",buff,buff,buff,source);
    bool loaded=run_test_jass(script);
    test_preload_marker=m382_previous_marker;unit->user_data=0;
    T_ASSERT(loaded);T_EQ(m382_public_markers,1);
    T_EQ(m382_public_before,1);T_EQ(m382_public_accepted,1);
    T_EQ(m382_public_after,0);T_EQ(m382_public_source,1);
}
static void m382_remove_public_buff(edict_t *unit){m382_remove_public_buff_named(unit,"AEme","BEme");}
/* Concrete647a00 temporarily sets80000: public BEme removal is an immediate
 * inverse, with no new transition timer. Natural expiry retains normal Dur. */
TEST(wc3_unit,morph382_public_buff_removal_restores_base_before_return) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_data(schema);
        uint32_t code=FS_SLKKey("AEme"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m382_unit(mode==2);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(mode==0){m382_passive(unit);m382_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2){T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.375f);}
        if(mode!=0){m382_advance(2.5f);m382_passive(unit);}
        m382_advance(2.625f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),1);
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        m382_expect_counts(unit,FS_SLKKey("BEme"),held && mode!=2 ? 1 : 0,held && mode!=2 ? 1 : 0,held);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->movement.repulse.disable_depth,0);T_ASSERT(unit->morph && unit->morph->expiration.active);
        if(saved)m382_save(&unit);
        float removed=level.pathing_clock.time;
        m382_remove_public_buff(unit);
        T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);T_EQ(G_UnitAbilityLevel(unit,code),1);
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->permanent_invisibility_fade.request.active,held);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(held)T_FEQ(unit->permanent_invisibility_fade.origin.time,removed,.000001f);
        m382_advance(removed+.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m382_advance(removed+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        m382_advance(removed+3);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}
static slkTestData_t *m382_alias_data(unsigned schema) {
    char text[1300];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"BuffID1\"\n"
        "C;X1;Y2;K\"AM82\"\nC;X2;K\"AEme\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K2.5\nC;X7;K1\nC;X8;K\"Bblo\"\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1");
    slkTestData_t *data=parse_slk_string(text);T_NOT_NULL(data);G_SetSLKRows("AbilityData",data);
    abilityLevel_t const *loaded=G_AbilityLevel(FS_SLKKey("AM82"),1);
    T_EQ(loaded->data[0].id,FS_SLKKey("hpea"));T_EQ(loaded->unitID,FS_SLKKey("hfoo"));
    T_STREQ(loaded->buffID,"Bblo");T_FEQ(loaded->data[1].number,1,.000001f);T_FEQ(loaded->dur,.375f,.000001f);T_FEQ(loaded->heroDur,2.5f,.000001f);
    return data;
}
/* NativeAM82/Bblo: BuffData codeBblo does not choose the concrete buff class. */
TEST(wc3_unit,morph382_authored_alias_retains_concrete_buff_class) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<3;mode++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_alias_data(schema);
        slkTestData_t *buff_rows=parse_slk_string("ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X1;Y2;K\"Bblo\"\nC;X2;K\"Bblo\"\nE\n");
        T_NOT_NULL(buff_rows);slkTestData_t *old_buffs=G_SetSLKRows("AbilityBuffData",buff_rows);
        T_EQ(G_AbilityBuffData(FS_SLKKey("Bblo"))->code,FS_SLKKey("Bblo"));
        abilityitem_t source_item=S_AbilityItem(FS_SLKKey("AM82"));T_NOT_NULL(source_item.ability);
        if(source_item.ability)T_STREQ(source_item.ability->classname,"AEme");
        uint32_t code=FS_SLKKey("AM82"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m382_unit(mode==2);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(mode==0){m382_passive(unit);m382_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2){T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.375f);}
        if(mode!=0){m382_advance(2.5f);m382_passive(unit);}
        m382_advance(2.625f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("Bblo")),1);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        m382_expect_counts(unit,FS_SLKKey("Bblo"),held && mode!=2 ? 1 : 0,held && mode!=2 ? 1 : 0,held);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),held);
        T_EQ(unit->movement.repulse.disable_depth,0);T_ASSERT(unit->morph && unit->morph->expiration.active);
        if(saved)m382_save(&unit);
        float removed=level.pathing_clock.time;
        m382_remove_public_buff_named(unit,"AM82","Bblo");
        T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("Bblo")),0);T_EQ(G_UnitAbilityLevel(unit,code),1);
        m382_expect_counts(unit,code,held && mode==2 ? 1 : 0,0,held);
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_EQ(unit->permanent_invisibility_fade.request.active,held);
        T_EQ(S_PermanentInvisibilityActive(unit),!held);
        if(held)T_FEQ(unit->permanent_invisibility_fade.origin.time,removed,.000001f);
        m382_advance(removed+.624f);T_EQ(S_PermanentInvisibilityActive(unit),!held);
        m382_advance(removed+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        m382_advance(removed+3);T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!unit->morph || (!unit->morph->transition.active && !unit->morph->expiration.active));
        T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("Bblo")),0);T_EQ(unit->ability_exclusive_depth,0);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();G_SetSLKRows("AbilityBuffData",old_buffs);free_slk_rows(buff_rows);m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}

/* Native382p: first clock48 suspension freezes privateBEme and common d01db.
 * Public removal whileheld inverts synchronously but leaves the captured form
 * remainder: on resume its later expiry forwards again from the base form. */
TEST(wc3_unit,morph382_pause_preserves_private_and_common_lifetimes) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned initial=0;initial<2;initial++)for(unsigned remove=0;remove<2;remove++)
    for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_data(schema);
        uint32_t code=FS_SLKKey("AEme"),buff=FS_SLKKey("BEme"),apiv=FS_SLKKey("Apiv");bool held=setting==2;
        edict_t *unit=m382_unit(initial);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        m382_passive(unit);m382_advance(1.75f);
        if(!initial)T_ASSERT(S_PermanentInvisibilityActive(unit));
        level.pathing_clock.time=2;
        if(!initial){T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.375f);}
        m382_advance(2.5f);T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,buff),1);
        S_SetUnitPaused(unit,true);T_ASSERT(unit->paused);
        m382_advance(2.625f);
        if(remove){m382_remove_public_buff(unit);T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(G_UnitAbilityLevel(unit,buff),0);T_ASSERT(unit->paused);T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);}
        if(saved)m382_save(&unit);
        m382_advance(6);
        T_ASSERT(unit->paused);T_EQ(unit->class_id,FS_SLKKey(remove ? "hpea" : "hfoo"));
        T_EQ(G_UnitAbilityLevel(unit,buff),remove ? 0 : 1);T_EQ(G_UnitAbilityLevel(unit,code),1);
        T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(!S_PermanentInvisibilityActive(unit));
        S_SetUnitPaused(unit,false);T_ASSERT(!unit->paused);
        /* ScriptUnpause retires its physical owner at the scheduled boundary. */
        m382_advance(unit->movement.pause_deadline.time);S_RunMoveTimers();
        float expiry=initial ? 7 : 8.375f,complete=expiry+.375f;
        m382_advance(expiry-.001f);
        T_EQ(unit->class_id,FS_SLKKey(remove ? "hpea" : "hfoo"));T_EQ(G_UnitAbilityLevel(unit,buff),remove ? 0 : 1);
        m382_advance(expiry);T_ASSERT(unit->morph && unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,1);T_EQ(G_UnitAbilityLevel(unit,buff),0);
        m382_advance(complete-.001f);T_EQ(unit->class_id,FS_SLKKey(remove ? "hpea" : "hfoo"));
        m382_advance(complete);T_EQ(unit->class_id,FS_SLKKey(remove ? "hfoo" : "hpea"));
        T_EQ(G_UnitAbilityLevel(unit,buff),remove ? 1 : 0);T_EQ(unit->movement.repulse.disable_depth,0);
        T_EQ(unit->ability_exclusive_depth,remove && held ? 1 : 0);
        T_EQ(S_AbilityExecutionDisabled(unit,apiv),remove && held);
        if(!remove && held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,complete,.000001f);}
        m382_advance(complete+.625f);T_EQ(S_PermanentInvisibilityActive(unit),!(remove && held));
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_EQ(G_UnitAbilityLevel(unit,buff),0);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}

static slkTestData_t *m382_sleep_data(unsigned schema) {
    char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"BuffID1\"\n"
        "C;X1;Y2;K\"AEme\"\nC;X2;K\"AEme\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K2.5\nC;X7;K1\nC;X8;K\"BEme\"\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\n"
        "C;X1;Y4;K\"AUsl\"\nC;X2;K\"AUsl\"\nC;X3;K2\nC;X5;K6\nC;X6;K6\nC;X8;K\"BUsl,BUsp,BUst\"\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1");
    slkTestData_t *data=parse_slk_string(text);T_NOT_NULL(data);G_SetSLKRows("AbilityData",data);
    abilityLevel_t const *loaded=G_AbilityLevel(FS_SLKKey("AUsl"),1);
    T_STREQ(loaded->buffID,"BUsl,BUsp,BUst");T_FEQ(loaded->dur,6,.000001f);T_FEQ(loaded->heroDur,6,.000001f);T_FEQ(loaded->data[0].number,2,.000001f);
    return data;
}
/* Native382W: physicalSleep Unit54 does not own BEme's Ability48 clock.
 * PublicUnpause resumes private/common expiry while BUsl remains attached.
 * The realAUsl producer owns both independent main and initial guard statuses. */
TEST(wc3_unit,morph382_sleep_physical_owner_does_not_hold_private_expiry) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned mode=0;mode<4;mode++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_sleep_data(schema);
        uint32_t code=FS_SLKKey("AEme"),buff=FS_SLKKey("BEme"),apiv=FS_SLKKey("Apiv");
        uint32_t sleep=FS_SLKKey("BUsl"),guard=FS_SLKKey("BUsp");bool held=setting==2;
        edict_t *unit=m382_unit(false),*caster=m382_unit(false);
        G_SetUnitPlayer(caster,2);caster->invulnerable=true;
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        T_ASSERT(G_ActorAddSkill(caster,FS_SLKKey("AUsl")));
        m382_passive(unit);m382_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.5f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,buff),1);
        abilityitem_t item=S_AbilityItem(FS_SLKKey("AUsl"));
        spellTarget_t target={.type=SPELL_TARGET_UNIT,.entity=unit};
        abilityCall_t call={.item=&item,.target=&target};
        S_AbilityMessage(caster,A_EXECUTE,&call);
        T_NOT_NULL(unit_findstatus(unit,sleep));T_NOT_NULL(unit_findstatus(unit,guard));
        T_EQ(unit->movement.internal_suspend_depth,2);T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);
        m382_advance(2.625f);
        if(mode!=3)S_SetUnitPaused(unit,true);
        if(mode==2){m382_advance(3);m382_remove_public_buff_named(unit,"AEme","BUsl");}
        if(saved)m382_save(&unit);
        if(mode!=3) {
            m382_advance(6);T_ASSERT(unit->paused);T_EQ(G_UnitAbilityLevel(unit,buff),1);
            T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_NOT_NULL(unit_findstatus(unit,guard));
            S_SetUnitPaused(unit,false);T_ASSERT(!unit->paused);
            T_ASSERT(unit->movement.internal_suspend_depth>0);
            if(mode!=2)T_NOT_NULL(unit_findstatus(unit,sleep));
            if(mode==1){m382_advance(6.5f);m382_remove_public_buff_named(unit,"AEme","BUsl");}
        }
        float expiry=mode==3 ? 4.875f : 8.25f,complete=expiry+.375f;
        m382_advance(expiry-.001f);T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,buff),1);
        m382_advance(expiry);T_EQ(G_UnitAbilityLevel(unit,buff),0);T_ASSERT(unit->morph && unit->morph->transition.active);
        if(mode==0 || mode==3){T_NOT_NULL(unit_findstatus(unit,sleep));T_ASSERT(unit->movement.internal_suspend_depth>0);T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);}
        m382_advance(complete);T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(G_UnitAbilityLevel(unit,buff),0);
        T_EQ(unit->ability_exclusive_depth,0);T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        if(mode==0 || mode==3){T_NOT_NULL(unit_findstatus(unit,sleep));T_ASSERT(unit->movement.internal_suspend_depth>0);T_EQ(unit->current_order_id,MOVE_ORDER_SUSPENDED);}
        if(held){T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,complete,.000001f);}
        m382_advance(complete+.625f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(G_ActorRemoveSkill(unit,apiv));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}

static slkTestData_t *m382_raw_alias_modifier_data(unsigned schema,cstring_t raw_buff) {
    char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
        "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"BuffID1\"\n"
        "C;X1;Y2;K\"AM82\"\nC;X2;K\"AEme\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K2.5\nC;X7;K1\nC;X8;K\"%s\"\n"
        "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\n"
        "C;X1;Y4;K\"Ablo\"\nC;X2;K\"Ablo\"\nC;X3;K0.4\nC;X5;K6\nC;X6;K6\nC;X7;K0.25\nC;X8;K\"Bblo\"\nE\n",schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1",raw_buff);
    slkTestData_t *data=parse_slk_string(text);T_NOT_NULL(data);G_SetSLKRows("AbilityData",data);
    T_STREQ(G_AbilityLevel(FS_SLKKey("AM82"),1)->buffID,raw_buff);
    T_FEQ(G_AbilityLevel(FS_SLKKey("Ablo"),1)->data[0].number,.4f,.000001f);
    T_FEQ(G_AbilityLevel(FS_SLKKey("Ablo"),1)->data[1].number,.25f,.000001f);
    return data;
}
static slkTestData_t *m382_alias_modifier_data(unsigned schema) {return m382_raw_alias_modifier_data(schema,"Bblo");}
/* A realBloodlust positivecontrol keeps ordinaryclass0 behavior. AEme's
 * authoredBblo is publiclylevel1 but its concreteBEme cannot own Bloodlust
 * numeric effects merely because the raw public identity matches. */
TEST(wc3_unit,morph382_authored_bblo_class_has_no_bloodlust_bonus) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_alias_modifier_data(schema);
        uint32_t code=FS_SLKKey("AM82"),bblo=FS_SLKKey("Bblo");
        edict_t *positive=m382_unit(false);
        T_ASSERT(G_ActorAddSkill(positive,FS_SLKKey("Ablo")));
        abilityitem_t item=S_AbilityItem(FS_SLKKey("Ablo"));
        spellTarget_t target={.type=SPELL_TARGET_UNIT,.entity=positive};
        abilityCall_t call={.item=&item,.target=&target};
        S_AbilityMessage(positive,A_EXECUTE,&call);
        T_EQ(G_UnitAbilityLevel(positive,bblo),1);
        T_FEQ(S_BloodlustAttackBonus(positive),.4f,.000001f);T_FEQ(S_BloodlustMoveBonus(positive),.25f,.000001f);
        unitStatusQuery_t positive_query;G_BeginUnitStatusQuery(positive,&positive_query);
        T_FEQ(S_BloodlustAttackBonus(positive),.4f,.000001f);T_FEQ(S_BloodlustMoveBonus(positive),.25f,.000001f);
        G_EndUnitStatusQuery(&positive_query);
        heroabilitystatus_t *ordinary=unit_findstatus(positive,bblo);T_NOT_NULL(ordinary);
        if(ordinary)unit_removestatus(positive,ordinary,STATUS_REMOVE_SCRIPT);
        T_EQ(G_UnitAbilityLevel(positive,bblo),0);
        edict_t *unit=m382_unit(false);
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        m382_passive(unit);m382_advance(1.75f);level.pathing_clock.time=2;
        T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.5f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,bblo),1);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        if(saved)m382_save(&unit);
        T_EQ(G_UnitAbilityLevel(unit,bblo),1);
        T_FEQ(S_BloodlustAttackBonus(unit),0,.000001f);T_FEQ(S_BloodlustMoveBonus(unit),0,.000001f);
        unitStatusQuery_t typed_query;G_BeginUnitStatusQuery(unit,&typed_query);
        T_EQ(G_UnitAbilityLevel(unit,bblo),1);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        T_FEQ(S_BloodlustAttackBonus(unit),0,.000001f);T_FEQ(S_BloodlustMoveBonus(unit),0,.000001f);
        G_EndUnitStatusQuery(&typed_query);
        T_FEQ(unit_status_modifier_total(unit,WC3_STATUS_MOD_ATTACK_SPEED_PERCENT),0,.000001f);
        T_FEQ(unit_status_modifier_total(unit,WC3_STATUS_MOD_MOVE_SPEED_PERCENT),0,.000001f);
        m382_remove_public_buff_named(unit,"AM82","Bblo");T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(G_ActorRemoveSkill(unit,FS_SLKKey("Apiv")));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}
/* A concrete BEme timer is an owner of pooled status storage. Releasing that
 * storage must retire its heap entry while another actual producer survives. */
TEST(wc3_unit,metamorphosis382_pool_release_retires_primary_timer) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)for(unsigned release=0;release<2;release++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_data(schema);uint32_t code=FS_SLKKey("AEme"),buff=FS_SLKKey("BEme");
        edict_t *first=m382_unit(false),*second=m382_unit(false);
        T_ASSERT(G_ActorAddSkill(first,code));T_ASSERT(G_ActorSetSkillPermanent(first,code,true));
        T_ASSERT(G_ActorAddSkill(second,code));T_ASSERT(G_ActorSetSkillPermanent(second,code,true));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(first,"metamorphosis"));m382_advance(2.5f);
        T_EQ(G_UnitAbilityLevel(first,buff),1);
        level.pathing_clock.time=3;T_ASSERT(unit_issueimmediateorder(second,"metamorphosis"));m382_advance(3.5f);
        T_EQ(G_UnitAbilityLevel(second,buff),1);
        abilityitem_t item=S_AbilityItem(buff);abilityTimerRequest_t before={0},after={0};
        abilityCall_t call={.primary_timer=&before};
        T_ASSERT(item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        T_FEQ(before.deadline.time,4.885f,.000001f);
        if(release)G_PoolsReleaseEdict(first);else G_FreeUnitStatus(first);
        T_NULL(first->abilstatus);
        call.primary_timer=&after;
        T_ASSERT(item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        T_FEQ(after.deadline.time,5.885f,.000001f);T_ASSERT(after.sequence>before.sequence);
        S_ResetAbilityTimers();T_ASSERT(!item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        S_RebuildAbilityTimers();T_ASSERT(item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        T_FEQ(after.deadline.time,5.885f,.000001f);
        uint32_t sequence=after.sequence,epoch=after.deadline.epoch;
        S_RebaseAbilityPrimaryTimers(1);
        T_ASSERT(item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        T_FEQ(after.deadline.time,4.885f,.000001f);T_EQ(after.deadline.epoch,epoch+1);T_EQ(after.sequence,sequence);
        reset_entities();T_ASSERT(!item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        S_RebuildAbilityTimers();T_ASSERT(!item.ability->proc(NULL,A_PRIMARY_TIMER_NEXT,&call));
        m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}
/* Typed BEme may publicly spell Amov without owning a Move procedure.
 * Real Bloodlust must not acquire a physical Move contribution for that alias. */
TEST(wc3_unit,morph382_authored_amov_does_not_supply_move_owner) {
    for(unsigned setting=1;setting<3;setting++)for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<2;saved++) {
        reset_entities();setup_test_world();m382Config_t config=m382_config(setting);
        slkTestData_t *a,*oa,*ui,*oui,*p,*op;
        m382_install_presentation(&a,&oa,&ui,&oui,&p,&op);
        slkTestData_t *data=m382_raw_alias_modifier_data(schema,"Amov");
        uint32_t code=FS_SLKKey("AM82"),move=FS_SLKKey("Amov"),bblo=FS_SLKKey("Bblo");
        edict_t *caster=m382_unit(false),*positive=m382_unit(false),*unit=m382_unit(false);
        T_ASSERT(G_ActorAddSkill(caster,FS_SLKKey("Ablo")));T_ASSERT(G_ActorHasAbilityCode(positive,move));
        abilityitem_t item=S_AbilityItem(FS_SLKKey("Ablo"));
        spellTarget_t target={.type=SPELL_TARGET_UNIT,.entity=positive};abilityCall_t call={.item=&item,.target=&target};
        S_AbilityMessage(caster,A_EXECUTE,&call);
        heroabilitystatus_t *normal=unit_findstatus(positive,bblo);T_NOT_NULL(normal);
        if(normal){T_EQ(normal->move_speed_applied,1);T_FEQ(normal->move_speed_delta,.25f,.000001f);}
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"metamorphosis"));m382_advance(2.5f);
        T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(G_UnitAbilityLevel(unit,move),1);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        T_ASSERT(G_ActorRemoveSkill(unit,move));T_ASSERT(!G_ActorHasAbilityCode(unit,move));
        if(saved)m382_save(&unit);
        T_ASSERT(!G_ActorHasAbilityCode(unit,move));T_EQ(G_UnitAbilityLevel(unit,move),1);
        target.entity=unit;S_AbilityMessage(caster,A_EXECUTE,&call);
        heroabilitystatus_t *status=unit_findstatus(unit,bblo);T_NOT_NULL(status);
        if(status){T_EQ(status->move_speed_applied,0);T_FEQ(status->move_speed_delta,0,.000001f);}
        T_FEQ(unit->movement.speed_factor_initialized ? unit->movement.speed_factor : 1,1,.000001f);
        unitStatusQuery_t query;G_BeginUnitStatusQuery(unit,&query);
        T_EQ(G_UnitAbilityLevel(unit,move),1);T_EQ(G_UnitAbilityLevel(unit,FS_SLKKey("BEme")),0);
        G_EndUnitStatusQuery(&query);
        m382_remove_public_buff_named(unit,"AM82","Amov");T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(G_ActorRemoveSkill(unit,code));
        reset_entities();m382_restore_presentation(a,oa,ui,oui,p,op);free_slk_rows(data);m382_restore_config(&config);
    }
}
#endif
