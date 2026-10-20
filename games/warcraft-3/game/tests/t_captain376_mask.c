#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "common/stb_slk.h"
TEST(wc3_slk,captain376_weapon_masks_retain_original_tokens) {
    static struct {cstring_t token;uint32_t mask;} const cases[]={
        {"air",0x4u},
        {"aliv",0x1000000u},
        {"alive",0x1000000u},
        {"alli",0x4000u},
        {"allies",0x4000u},
        {"ally",0x4000u},
        {"ancient",0x80000000u},
        {"bridge",0x400u},
        {"dead",0x2000000u},
        {"deco",0x200u},
        {"decoration",0x200u},
        {"debr",0x100u},
        {"debris",0x100u},
        {"enem",0x10000u},
        {"enemies",0x10000u},
        {"enemy",0x10000u},
        {"grou",0x2u},
        {"ground",0x2u},
        {"frie",0x6000u},
        {"friend",0x6000u},
        {"hero",0x400000u},
        {"invu",0x200000u},
        {"invulnerable",0x200000u},
        {"item",0x20u},
        {"mech",0x8000000u},
        {"mechanical",0x8000000u},
        {"neut",0x8000u},
        {"neutral",0x8000u},
        {"none",0x1u},
        {"nonancient",0x40000000u},
        {"nonh",0x800000u},
        {"nonhero",0x800000u},
        {"nonsapper",0x10000000u},
        {"nots",0x1e000u},
        {"notself",0x1e000u},
        {"orga",0x4000000u},
        {"organic",0x4000000u},
        {"play",0x2000u},
        {"player",0x2000u},
        {"sapper",0x20000000u},
        {"self",0x1000u},
        {"stru",0x8u},
        {"structure",0x8u},
        {"tote",0x10u},
        {"totem",0x10u},
        {"tree",0x40u},
        {"vuln",0x100000u},
        {"vulnerable",0x100000u},
        {"wall",0x80u},
        {"ward",0x10u},
        {"_",0xfdd1f00eu},
    };
    FOR_LOOP(i,sizeof(cases)/sizeof(cases[0])) {
        char text[1024];
        snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;Y1;X1;K\"unitWeapID\"\nC;Y1;X2;K\"targs1\"\nC;Y1;X3;K\"targs2\"\nC;Y2;X1;K\"hfoo\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"ground,hero,enemy,vulnerable\"\nE\n",cases[i].token);
        slkTestData_t *rows=parse_slk_string(text),*saved=G_SetSLKRows("UnitWeapons",rows);
        T_EQ(g_UnitWeaponsCount,1);T_EQ((uint32_t)g_UnitWeapons[0].attack1.targetsAllowed,cases[i].mask);
        T_EQ((uint32_t)g_UnitWeapons[0].attack2.targetsAllowed,0x00410002u|0x00100000u);
        G_SetSLKRows("UnitWeapons",saved);free_slk_rows(rows);
    }
}
#endif
