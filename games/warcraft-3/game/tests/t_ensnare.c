#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AENS MAKEFOURCC('A', 'e', 'n', 's') // rawcode; stock Raider Ensnare
#define BZ_ANEN MAKEFOURCC('A', 'N', 'e', 'n') // rawcode; TFT Naga Ensnare alias
#define BZ_BENS MAKEFOURCC('B', 'e', 'n', 's') // rawcode; abstract / ROC Ensnare buff
#define BZ_BENA MAKEFOURCC('B', 'e', 'n', 'a') // rawcode; TFT EnsnareAir buff
#define BZ_BENG MAKEFOURCC('B', 'e', 'n', 'g') // rawcode; TFT EnsnareGround buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock Dur/Cost so tests cannot pass on hardcoded retail 12/0. */
#define ENS_SLK \
    "ID;PWXL;N;EBB;Y2;X10\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\n" \
    "C;Y2;X1;K\"Aens\"\nC;Y2;X2;K\"Aens\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,air,enemy,neutral\"\nC;Y2;X5;K\"17\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"7\"\nC;Y2;X9;K\"3\"\n" \
    "C;Y2;X10;K\"Bena,Beng\"\nE\n"

#define ENS_BENS_SLK \
    "ID;PWXL;N;EBB;Y2;X10\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\n" \
    "C;Y2;X1;K\"Aens\"\nC;Y2;X2;K\"Aens\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,air,enemy,neutral\"\nC;Y2;X5;K\"17\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"7\"\nC;Y2;X9;K\"3\"\n" \
    "C;Y2;X10;K\"Bens\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, ground, flyer;
    UnitData_t flyer_data;
} ENSFIX;

/* Fill *fix in place so flyer_data's address is the caller's, not a returned copy. */
static void ens_setup(ENSFIX *fix, LPCSTR slk) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->caster = alloc_test_unit(MAKEFOURCC('o','r','a','i'), 0, 0);
    fix->ground = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    fix->flyer = alloc_test_unit(MAKEFOURCC('h','g','r','y'), 160, 0);
    fix->caster->s.player = 0; fix->ground->s.player = fix->flyer->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->ground->svflags |= SVF_MONSTER; fix->flyer->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->ground->targtype = TARG_GROUND;
    fix->flyer->targtype = TARG_AIR;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AENS, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 100;
    fix->ground->health.value = fix->ground->health.max_value = 500;
    fix->flyer->health.value = fix->flyer->health.max_value = 500;
    memset(&fix->flyer_data, 0, sizeof(fix->flyer_data));
    fix->flyer_data.moveTypeName = "fly";
    fix->flyer_data.moveHeight = 180.0f;
    fix->flyer->data.UnitData = &fix->flyer_data;
    fix->flyer->aiflags |= AI_FLYING;
    fix->flyer->unitinfo.FlyHeight = 180.0f;
    fix->flyer->s.origin.z = 180.0f;
    fix->ground->stand = unit_stand; unit_stand(fix->ground);
    fix->flyer->stand = unit_stand; unit_stand(fix->flyer);
}

static void ens_done(ENSFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, ensnare_aliases_share_procedure) {
    T_EQ(S_AbilityItem(BZ_AENS).ability->proc, CAbilityEnsnare);
    T_EQ(S_AbilityItem(BZ_ANEN).ability->proc, CAbilityEnsnare);
}

/* Ground Bens keeps the order_move early-return lock used with BEer. */
TEST(wc3_spell, ensnare_ground_bens_blocks_move) {
    ENSFIX fix; ens_setup(&fix, ENS_BENS_SLK);
    LPEDICT wp;

    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.ground));
    T_EQ(G_UnitStatusLevel(fix.ground, BZ_BENS), 1);
    T_ASSERT(S_UnitIsEnsnared(fix.ground));
    T_ASSERT(!(fix.ground->aiflags & AI_FLYING));
    T_FEQ(fix.ground->unitinfo.FlyHeight, 0, 0.001f);

    wp = Waypoint_add(&(VECTOR2){200, 0});
    fix.ground->goalentity = NULL;
    order_move(fix.ground, wp);
    T_ASSERT(fix.ground->goalentity != wp);
    ens_done(fix);
}

/* Flying targets take Bena, lose AI_FLYING, and land on the support surface. */
TEST(wc3_spell, ensnare_flyer_lands_and_locks) {
    ENSFIX fix; ens_setup(&fix, ENS_SLK);
    LPEDICT wp;

    T_ASSERT(fix.flyer->aiflags & AI_FLYING);
    T_FEQ(fix.flyer->unitinfo.FlyHeight, 180, 0.001f);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.flyer));
    T_EQ(G_UnitStatusLevel(fix.flyer, BZ_BENA), 1);
    T_EQ(G_UnitStatusLevel(fix.flyer, BZ_BENG), 0);
    T_ASSERT(S_UnitIsEnsnared(fix.flyer));
    T_ASSERT(!(fix.flyer->aiflags & AI_FLYING));
    T_FEQ(fix.flyer->unitinfo.FlyHeight, 0, 0.001f);
    T_ASSERT(fix.flyer->s.origin.z < 179.0f);

    wp = Waypoint_add(&(VECTOR2){240, 0});
    fix.flyer->goalentity = NULL;
    order_move(fix.flyer, wp);
    T_ASSERT(fix.flyer->goalentity != wp);
    ens_done(fix);
}

/* Ground TFT targets take Beng and are never given AI_FLYING. */
TEST(wc3_spell, ensnare_ground_gets_beng_not_flying) {
    ENSFIX fix; ens_setup(&fix, ENS_SLK);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.ground));
    T_EQ(G_UnitStatusLevel(fix.ground, BZ_BENG), 1);
    T_EQ(G_UnitStatusLevel(fix.ground, BZ_BENA), 0);
    T_ASSERT(!(fix.ground->aiflags & AI_FLYING));
    T_FEQ(fix.ground->unitinfo.FlyHeight, 0, 0.001f);
    ens_done(fix);
}

/* Expiry restores authored flyer flags and moveHeight through status refresh. */
TEST(wc3_spell, ensnare_expiry_restores_flyer) {
    ENSFIX fix; ens_setup(&fix, ENS_SLK);
    LPEDICT wp;

    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.flyer));
    T_ASSERT(!(fix.flyer->aiflags & AI_FLYING));
    T_FEQ(fix.flyer->unitinfo.FlyHeight, 0, 0.001f);

    level.time += 7000; unit_updatestatuses(fix.flyer);
    T_ASSERT(!S_UnitIsEnsnared(fix.flyer));
    T_ASSERT(fix.flyer->aiflags & AI_FLYING);
    T_FEQ(fix.flyer->unitinfo.FlyHeight, 180, 0.001f);

    wp = Waypoint_add(&(VECTOR2){280, 0});
    order_move(fix.flyer, wp);
    T_ASSERT(fix.flyer->goalentity == wp);
    ens_done(fix);
}

/* ROC empty BuffID still applies Bens; recast refreshes the same lock. */
TEST(wc3_spell, ensnare_roc_empty_buffid_and_recast) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"Aens\"\nC;Y2;X2;K\"Aens\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"ground,air,enemy,neutral\"\nC;Y2;X5;K\"17\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"7\"\nC;Y2;X8;K\"3\"\nE\n";
    ENSFIX fix; ens_setup(&fix, slk);
    LPEDICT wp;

    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.ground));
    T_EQ(G_UnitStatusLevel(fix.ground, BZ_BENS), 1);
    fix.caster->mana.value = 100;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AENS, fix.ground));
    T_EQ(G_UnitStatusLevel(fix.ground, BZ_BENS), 1);
    wp = Waypoint_add(&(VECTOR2){300, 0});
    order_move(fix.ground, wp);
    T_ASSERT(fix.ground->goalentity != wp);
    ens_done(fix);
}

#endif
