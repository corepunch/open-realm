#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "common/stb_slk.h"
#include "jass/jass.h"
#include <unistd.h>
extern void setup_test_world(void);
extern bool run_test_jass(cstring_t);

/* Original218580/218ea0 accept only these nine public handle values. */
static bool c376_mutable_type(int type) {
    return type==9 || type==10 || (type>=14 && type<=20);
}
TEST(wc3_captain376_types, rejects_nonclassification_type_mutations) {
    setup_test_world();
    for(int type=-1;type<=32;type++) {
        if(c376_mutable_type(type))continue;
        for(int remove=0;remove<2;remove++) {
            char text[1024];
            snprintf(text,sizeof(text),
                "function main takes nothing returns nothing\n"
                "local unit u=CreateUnit(Player(0),'hfoo',0,0,0)\n"
                "call BJassAssert(u!=null,\"constructed unit\")\n"
                "call BJassAssert(not Unit%sType(u,ConvertUnitType(%d)),\"unsupported type %d\")\n"
                "endfunction\n",remove?"Remove":"Add",type,type);
            T_ASSERT(run_test_jass(text));
        }
    }
}
TEST(wc3_captain376_types, public_supported_classification_roundtrip) {
    static int const types[]={9,10,14,15,16,17,18,19,20};
    setup_test_world();
    FOR_LOOP(i,sizeof(types)/sizeof(types[0])) {
        char text[1400];
        snprintf(text,sizeof(text),
            "function main takes nothing returns nothing\n"
            "local unit u=CreateUnit(Player(0),'hfoo',0,0,0)\n"
            "call BJassAssert(u!=null,\"constructed unit\")\n"
            "call BJassAssert(UnitAddType(u,ConvertUnitType(%d)),\"accepted add\")\n"
            "call BJassAssert(IsUnitType(u,ConvertUnitType(%d)),\"added class visible\")\n"
            "call BJassAssert(UnitRemoveType(u,ConvertUnitType(%d)),\"accepted remove\")\n"
            "call BJassAssert(not IsUnitType(u,ConvertUnitType(%d)),\"removed class absent\")\n"
            "endfunction\n",types[i],types[i],types[i],types[i]);
        T_ASSERT(run_test_jass(text));
    }
}
TEST(wc3_captain376_types, public_remove_clears_authored_classification) {
    static int const types[]={15,17,19};
    static char const *const names[]={"mechanical","sapper","ancient"};
    setup_test_world();
    FOR_LOOP(i,3) {
        char slk[1024];
        snprintf(slk,sizeof(slk),
            "ID;PWXL;N;E\nC;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"type\"\n"
            "C;Y1;X3;K\"realHP\"\nC;Y1;X4;K\"spd\"\n"
            "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K420\nC;Y2;X4;K270\nE\n",names[i]);
        slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("UnitBalance",rows);
        T_EQ(g_UnitBalanceCount,1);T_STREQ(G_UnitBalance(MAKEFOURCC('h','f','o','o'))->type,names[i]);
        for(int remove=0;remove<2;remove++) {
            char text[1200];
            snprintf(text,sizeof(text),
                "function main takes nothing returns nothing\n"
                "local unit u=CreateUnit(Player(0),'hfoo',0,0,0)\n"
                "call BJassAssert(u!=null,\"constructed authored unit\")\n"
                "%s"
                "call BJassAssert(%sIsUnitType(u,ConvertUnitType(%d)),\"authored class %s %s\")\n"
                "endfunction\n",
                remove ? (types[i]==15 ? "call UnitRemoveType(u,ConvertUnitType(15))\n" :
                          types[i]==17 ? "call UnitRemoveType(u,ConvertUnitType(17))\n" :
                                        "call UnitRemoveType(u,ConvertUnitType(19))\n") : "",
                remove?"not ":"",types[i],names[i],remove?"removed":"present");
            T_ASSERT(run_test_jass(text));
        }
        G_SetSLKRows("UnitBalance",old);free_slk_rows(rows);
    }
}
/* The existing production rebind is shared by concrete same-handle morphs.
 * Table rebinding on load must preserve the saved live classification. */
TEST(wc3_captain376_types, classification_persists_save_and_rebind_replaces_it) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\nunit classSubject\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set classSubject=CreateUnit(Player(0),'hfoo',0,0,0)\n"
        "call BJassAssert(classSubject!=null,\"constructed unit\")\n"
        "call BJassAssert(UnitAddType(classSubject,ConvertUnitType(15)),\"add mechanical\")\n"
        "endfunction\n"
        "function verifyAdded takes nothing returns nothing\n"
        "call BJassAssert(IsUnitType(classSubject,ConvertUnitType(15)),\"saved mutable mechanical\")\n"
        "endfunction\n"
        "function verifyRebound takes nothing returns nothing\n"
        "call BJassAssert(GetUnitTypeId(classSubject)=='hpea',\"same handle rebound\")\n"
        "call BJassAssert(not IsUnitType(classSubject,ConvertUnitType(15)),\"rebind replaces old class\")\n"
        "endfunction\n"));
    char path[128];snprintf(path,sizeof(path),"/tmp/wc3-c376-class-%ld.sav",(long)getpid());
    T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
    jass_callbyname(level.vm,"verifyAdded",false);T_ASSERT(!jass_rterror_pending(level.vm));
    edict_t *subject=NULL;
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].class_id==MAKEFOURCC('h','f','o','o')){subject=g_edicts+i;break;}
    T_NOT_NULL(subject);if(!subject)return;
    T_ASSERT(G_TransformUnitType(subject,MAKEFOURCC('h','p','e','a')));
    jass_callbyname(level.vm,"verifyRebound",false);T_ASSERT(!jass_rterror_pending(level.vm));
}
TEST(wc3_captain376_types, public_remove_summoned_does_not_remove_timed_life) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u=CreateUnit(Player(0),'hfoo',0,0,0)\n"
        "call BJassAssert(u!=null,\"constructed unit\")\n"
        "call UnitApplyTimedLife(u,'BTLF',30)\n"
        "call BJassAssert(GetUnitAbilityLevel(u,'BTLF')==1,\"real timed life attached\")\n"
        "call BJassAssert(IsUnitType(u,ConvertUnitType(10)),\"timed life marks summoned\")\n"
        "call BJassAssert(UnitRemoveType(u,ConvertUnitType(10)),\"remove summoned accepted\")\n"
        "call BJassAssert(not IsUnitType(u,ConvertUnitType(10)),\"live summoned bit removed\")\n"
        "call BJassAssert(GetUnitAbilityLevel(u,'BTLF')==1,\"classification removal preserves timer\")\n"
        "endfunction\n"));
}
TEST(wc3_captain376_types, same_type_rebind_keeps_live_mutations) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\nunit classSubject\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set classSubject=CreateUnit(Player(0),'hfoo',0,0,0)\n"
        "call BJassAssert(classSubject!=null,\"constructed unit\")\n"
        "call BJassAssert(UnitAddType(classSubject,ConvertUnitType(15)),\"add mechanical\")\n"
        "endfunction\n"
        "function verifyUnchanged takes nothing returns nothing\n"
        "call BJassAssert(IsUnitType(classSubject,ConvertUnitType(15)),\"same type preserves class\")\n"
        "endfunction\n"));
    edict_t *subject=NULL;
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].class_id==MAKEFOURCC('h','f','o','o')){subject=g_edicts+i;break;}
    T_NOT_NULL(subject);if(!subject)return;
    T_ASSERT(G_TransformUnitType(subject,subject->class_id));
    jass_callbyname(level.vm,"verifyUnchanged",false);T_ASSERT(!jass_rterror_pending(level.vm));
}
#endif
