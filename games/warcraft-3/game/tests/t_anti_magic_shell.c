#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AMS MAKEFOURCC('A', 'a', 'm', 's') // rawcode; stock duration immunity Anti-Magic Shell
#define BZ_AAM2 MAKEFOURCC('A', 'a', 'm', '2') // rawcode; TFT melee absorption Anti-Magic Shell
#define BZ_ACAM MAKEFOURCC('A', 'C', 'a', 'm') // rawcode; creep Anti-Magic Shell alias
#define BZ_AAMI MAKEFOURCC('A', 'a', 'm', 'i') // rawcode; Instant AMS class / potion cooldownID
#define BZ_AIXS MAKEFOURCC('A', 'I', 'x', 's') // rawcode; item Instant Anti-Magic Shell
#define BZ_BAMS MAKEFOURCC('B', 'a', 'm', 's') // rawcode; targeting/spell-immunity buff
#define BZ_BAM2 MAKEFOURCC('B', 'a', 'm', '2') // rawcode; spell-damage absorption buff
#define BZ_AHTB MAKEFOURCC('A', 'H', 't', 'b') // rawcode; Storm Bolt, used as a hostile spell probe

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old; LPEDICT caster, ally, enemy; } AMSFIX;

static AMSFIX ams_setup(LPCSTR slk, DWORD code) {
    AMSFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('u','n','e','c'), 0, 0);
    fix.ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    fix.enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
    fix.caster->s.player = fix.ally->s.player = 0; fix.enemy->s.player = 1;
    fix.caster->svflags |= SVF_MONSTER; fix.ally->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
    fix.caster->targtype = fix.ally->targtype = fix.enemy->targtype = TARG_GROUND;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 100;
    fix.ally->health.value = fix.ally->health.max_value = 500;
    return fix;
}

static void ams_done(AMSFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

static DWORD ams_remaining(LPCEDICT unit) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BAM2) return unit->abilstatus[i].data;
    return 0;
}

/* Aams with empty DataC applies Bams: spells cannot target or damage the unit until expiry. */
TEST(wc3_spell, anti_magic_shell_bams_blocks_spells_until_expiry) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aams\"\nC;Y2;X2;K\"Aams\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"90\"\nC;Y2;X9;K\"90\"\nC;Y2;X10;K\"Bams,Bam2\"\nE\n";
    AMSFIX fix = ams_setup(slk, BZ_AMS);

    T_EQ(S_AbilityItem(BZ_AMS).ability->proc, CAbilityAntiMagicShell);
    T_ASSERT(S_SpellAllowsTarget(BZ_AMS, fix.caster, fix.enemy));
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AMS, fix.ally));
    T_FEQ(fix.caster->mana.value, 25, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAMS), 1);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 0);
    T_ASSERT(S_UnitSpellImmune(fix.ally));
    T_ASSERT(!S_SpellAllowsTarget(BZ_AHTB, fix.enemy, fix.ally));
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 100)); T_FEQ(fix.ally->health.value, 500, 0.001f);
    S_ResolveAttackHit(fix.enemy, fix.ally, 50); T_FEQ(fix.ally->health.value, 450, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AMS, fix.ally));

    level.time += 90000; unit_updatestatuses(fix.ally);
    T_ASSERT(!S_UnitSpellImmune(fix.ally));
    T_ASSERT(S_SpellDamage(fix.ally, fix.enemy, 100));
    fix.caster->mana.value = 100;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AMS, fix.ally));
    T_ASSERT(S_UnitSpellImmune(fix.ally));
    fix.ally->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AMS, fix.ally));
    ams_done(fix);
}

/* ROC AbilityData omits BuffID; empty DataC must still apply Bams from the documented fallback. */
TEST(wc3_spell, anti_magic_shell_roc_row_without_buffid_still_applies_bams) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"Aams\"\nC;Y2;X2;K\"Aams\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"90\"\nC;Y2;X8;K\"90\"\nE\n";
    AMSFIX fix = ams_setup(slk, BZ_AMS);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AMS, fix.ally));
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAMS), 1);
    T_ASSERT(S_UnitSpellImmune(fix.ally));
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 40));
    T_FEQ(fix.ally->health.value, 500, 0.001f);
    ams_done(fix);
}

/* Aam2 DataC is a spell-damage pool on Bam2. The unit stays targetable; physical hits ignore the shell. */
TEST(wc3_spell, anti_magic_shell_aam2_absorbs_authored_spell_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"Cool1\"\nC;Y1;X12;K\"DataA1\"\n"
        "C;Y2;X1;K\"Aam2\"\nC;Y2;X2;K\"Aams\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground,friend,self\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"90\"\nC;Y2;X8;K\"90\"\nC;Y2;X9;K\"300\"\nC;Y2;X10;K\"Bams,Bam2\"\n"
        "C;Y2;X11;K\"0\"\nC;Y2;X12;K\"0\"\n"
        "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"air,ground,enemy\"\nC;Y3;X5;K\"75\"\nC;Y3;X6;K\"600\"\n"
        "C;Y3;X7;K\"5\"\nC;Y3;X8;K\"3\"\nC;Y3;X9;K\"0\"\nC;Y3;X10;K\"BHtb\"\n"
        "C;Y3;X11;K\"9\"\nC;Y3;X12;K\"100\"\nE\n";
    AMSFIX fix = ams_setup(slk, BZ_AAM2);

    T_EQ(S_AbilityItem(BZ_AAM2).ability->proc, CAbilityAntiMagicShell);
    T_ASSERT(!S_SpellAllowsTarget(BZ_AAM2, fix.caster, fix.enemy));
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AAM2, fix.ally));
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 1);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAMS), 0);
    T_ASSERT(!S_UnitSpellImmune(fix.ally));
    T_EQ(ams_remaining(fix.ally), 300);
    T_ASSERT(S_SpellAllowsTarget(BZ_AHTB, fix.enemy, fix.ally));
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 100));
    T_FEQ(fix.ally->health.value, 500, 0.001f);
    T_EQ(ams_remaining(fix.ally), 200);
    S_ResolveAttackHit(fix.enemy, fix.ally, 50); T_FEQ(fix.ally->health.value, 450, 0.001f);
    T_EQ(ams_remaining(fix.ally), 200);
    T_ASSERT(S_SpellDamage(fix.ally, fix.enemy, 250));
    T_FEQ(fix.ally->health.value, 400, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 0);
    T_EQ(ams_remaining(fix.ally), 0);

    fix.caster->mana.value = 100;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AAM2, fix.ally));
    T_EQ(ams_remaining(fix.ally), 300);
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 40));
    T_EQ(ams_remaining(fix.ally), 260);
    fix.caster->mana.value = 100;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AAM2, fix.ally));
    T_EQ(ams_remaining(fix.ally), 300);
    ams_done(fix);
}

TEST(wc3_spell, anti_magic_shell_aliases_share_procedure) {
    T_EQ(S_AbilityItem(BZ_AMS).ability->proc, CAbilityAntiMagicShell);
    T_EQ(S_AbilityItem(BZ_AAM2).ability->proc, CAbilityAntiMagicShell);
    T_EQ(S_AbilityItem(BZ_ACAM).ability->proc, CAbilityAntiMagicShell);
    T_EQ(S_AbilityItem(BZ_AAMI).ability->proc, CAbilityAntiMagicShellInstant);
    T_EQ(S_AbilityItem(BZ_AIXS).ability->proc, CAbilityAntiMagicShellInstant);
}

/* Item AIxs (code=Aami) reads its own Dur/DataC; empty DataC applies Bams via Instant. */
TEST(wc3_spell, anti_magic_shell_item_aixs_applies_bams_with_authored_duration) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
        "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIxs\"\nC;Y2;X2;K\"Aami\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"30\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"17\"\nC;Y2;X9;K\"17\"\n"
        "C;Y2;X10;K\"Bams,Bam2\"\nC;Y2;X11;K\"10\"\nC;Y2;X12;K\"0\"\nE\n";
    AMSFIX fix = ams_setup(slk, BZ_AIXS);
    T_EQ(S_AbilityItem(BZ_AIXS).ability->proc, CAbilityAntiMagicShellInstant);
    T_FEQ(S_SpellDuration(BZ_AIXS, 1, false), 17, 0.001f);
    T_FEQ(S_SpellData(BZ_AIXS, 1, 2), 10, 0.001f);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AIXS, fix.ally));
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAMS), 1);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 0);
    T_ASSERT(S_UnitSpellImmune(fix.ally));
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 40));
    T_FEQ(fix.ally->health.value, 500, 0.001f);
    level.time += 17000; unit_updatestatuses(fix.ally);
    T_ASSERT(!S_UnitSpellImmune(fix.ally));
    ams_done(fix);
}

/* Non-zero item DataC still drives Bam2 absorb through the shared Instant path. */
TEST(wc3_spell, anti_magic_shell_item_aixs_datac_uses_bam2_absorb) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataB1\"\n"
        "C;Y2;X1;K\"AIxs\"\nC;Y2;X2;K\"Aami\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"17\"\nC;Y2;X8;K\"17\"\nC;Y2;X9;K\"140\"\n"
        "C;Y2;X10;K\"Bams,Bam2\"\nC;Y2;X11;K\"10\"\nE\n";
    AMSFIX fix = ams_setup(slk, BZ_AIXS);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AIXS, fix.ally));
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 1);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAMS), 0);
    T_EQ(ams_remaining(fix.ally), 140);
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 40));
    T_EQ(ams_remaining(fix.ally), 100);
    ams_done(fix);
}

/* Remaining Bam2 absorption lives on abilstatus.data and is part of the raw edict save record. */
TEST(wc3_spell, anti_magic_shell_absorption_survives_save_load) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aam2\"\nC;Y2;X2;K\"Aams\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground,friend,self\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"90\"\nC;Y2;X8;K\"90\"\nC;Y2;X9;K\"300\"\nC;Y2;X10;K\"Bams,Bam2\"\nE\n";
    LPCSTR path = "/tmp/openwarcraft3-ams-save.bin";
    AMSFIX fix = ams_setup(slk, BZ_AAM2);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AAM2, fix.ally));
    T_ASSERT(!S_SpellDamage(fix.ally, fix.enemy, 100));
    T_EQ(ams_remaining(fix.ally), 200);
    T_ASSERT(WriteGame(path));
    memset(fix.ally->abilstatus, 0, sizeof(fix.ally->abilstatus));
    T_EQ(ams_remaining(fix.ally), 0);
    T_ASSERT(ReadGame(path));
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 1);
    T_EQ(ams_remaining(fix.ally), 200);
    T_ASSERT(S_SpellDamage(fix.ally, fix.enemy, 250));
    T_FEQ(fix.ally->health.value, 450, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.ally, BZ_BAM2), 0);
    remove(path); ams_done(fix);
}

#endif
