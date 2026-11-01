#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

#define BREW_FIRE_CODE MAKEFOURCC('A','N','b','f')
#define BREW_HAZE_CODE MAKEFOURCC('A','N','d','h')
#define BREW_SPLIT_CODE MAKEFOURCC('A','N','e','f')

static char const brewmaster_presentation_config[] =
    "[ANbf]\nMissileArt=TestUI\\Models\\anim_projectile.mdx\nMissilespeed=1050\n"
    "[BNbf]\nTargetArt=TestUI\\Models\\anim_pulse.mdx\n";

/* Nonstock cone, damage budget and 100% Haze miss chance expose hardcoding.
 * The campaign row retains its own slow/miss values and casting rawcode. */
static char const brewmaster_test_slk[] =
    "ID;PWXL;N;E\nB;X17;Y4;D0\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
    "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\n"
    "C;Y1;X9;K\"DataE1\"\nC;Y1;X10;K\"Area1\"\n"
    "C;Y1;X11;K\"Dur1\"\nC;Y1;X12;K\"HeroDur1\"\n"
    "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"Cost1\"\n"
    "C;Y1;X15;K\"Cool1\"\nC;Y1;X16;K\"Rng1\"\n"
    "C;Y1;X17;K\"UnitID1\"\n"
    "C;Y2;X1;K\"ANbf\"\nC;Y2;X2;K\"ANbf\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"ground,air,structure\"\n"
    "C;Y2;X5;K40\nC;Y2;X6;K65\nC;Y2;X7;K300\n"
    "C;Y2;X8;K200\nC;Y2;X9;K9\nC;Y2;X10;K125\n"
    "C;Y2;X11;K5\nC;Y2;X12;K5\nC;Y2;X13;K\"BNbf\"\n"
    "C;Y3;X1;K\"ANdh\"\nC;Y3;X2;K\"ANdh\"\nC;Y3;X3;K1\n"
    "C;Y3;X4;K\"air,ground,enemy,organic,neutral\"\n"
    "C;Y3;X6;K1\nC;Y3;X7;K0.35\nC;Y3;X10;K200\n"
    "C;Y3;X11;K12\nC;Y3;X12;K5\nC;Y3;X13;K\"BNdh\"\n"
    "C;Y4;X1;K\"Acdh\"\nC;Y4;X2;K\"ANdh\"\nC;Y4;X3;K1\n"
    "C;Y4;X4;K\"air,ground,enemy,organic,neutral\"\n"
    "C;Y4;X6;K0\nC;Y4;X7;K0.5\nC;Y4;X10;K200\n"
    "C;Y4;X11;K12\nC;Y4;X12;K5\nC;Y4;X13;K\"BNdh\"\nE\n";

TEST(wc3_brewmaster, drunken_haze_applies_authored_slow_and_miss_from_cast_alias) {
    slkTestData_t *rows, *old;
    edict_t *target;
    reset_entities(); setup_test_world(); level.time = 1000;
    rows = parse_slk_string(brewmaster_test_slk);
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    target = alloc_test_unit(FS_SLKKey("hfoo"), 100, 0);
    T_NOT_NULL(S_SpellApplyTimedTargetStatus(target, FS_SLKKey("ANdh"), 1, "BNdh", 5.0f));
    T_FEQ(unit_status_modifier_total(target, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.35f, .001f);
    T_ASSERT(S_BrewmasterHazeMiss(target)); /* 100% misses at synthetic ANdh level 1 */
    T_NOT_NULL(S_SpellApplyTimedTargetStatus(target, FS_SLKKey("Acdh"), 1, "BNdh", 5.0f));
    T_FEQ(unit_status_modifier_total(target, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.5f, .001f);
    T_ASSERT(!S_BrewmasterHazeMiss(target)); /* 0% campaign miss chance */
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_brewmaster, breath_of_fire_cone_ignites_haze_and_limits_initial_damage) {
    slkTestData_t *rows, *old;
    edict_t *caster, *front, *side, *behind;
    vec2_t aim = { 300, 0 };
    reset_entities(); setup_test_world(); level.time = 1000;
    rows = parse_slk_string(brewmaster_test_slk);
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster = alloc_test_unit(FS_SLKKey("hfoo"), 0, 0);
    front = alloc_test_unit(FS_SLKKey("hfoo"), 180, 0);
    side = alloc_test_unit(FS_SLKKey("hfoo"), 180, 240);
    behind = alloc_test_unit(FS_SLKKey("hfoo"), -180, 0);
    caster->svflags |= SVF_MONSTER;
    front->svflags |= SVF_MONSTER;
    side->svflags |= SVF_MONSTER;
    behind->svflags |= SVF_MONSTER;
    caster->s.player = 0; front->s.player = side->s.player = behind->s.player = 1;
    caster->targtype = front->targtype = side->targtype = behind->targtype = TARG_GROUND;
    T_NOT_NULL(S_SpellApplyTimedTargetStatus(front, BREW_HAZE_CODE, 1, "BNdh", 5.0f));
    T_ASSERT(S_SpellIsEnemy(caster, front));
    T_ASSERT(S_SpellAllowsAreaTarget(BREW_FIRE_CODE, caster, front));
    T_FEQ(S_SpellData(BREW_FIRE_CODE, 1, 1), 40.0f, .001f);
    T_FEQ(S_SpellData(BREW_FIRE_CODE, 1, 3), 300.0f, .001f);
    float front_before = front->health.value, side_before = side->health.value, behind_before = behind->health.value;
    T_ASSERT(G_TestPushAbilityConfig(brewmaster_presentation_config));
    S_BrewmasterBreathOfFire(caster, &aim, BREW_FIRE_CODE);
    T_FEQ(front->health.value, front_before - 40.0f, .001f);
    T_FEQ(side->health.value, side_before, .001f);
    T_FEQ(behind->health.value, behind_before, .001f);
    T_NOT_NULL(unit_findstatus(front, FS_SLKKey("BNbf")));
    T_NULL(unit_findstatus(side, FS_SLKKey("BNbf")));
    {
        int const missile_model = G_RegisterModel("TestUI\\Models\\anim_projectile.mdx");
        int const fire_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
        edict_t *missile = NULL;
        bool fire_target = false;
        FILTER_EDICTS(ent, ent->inuse && ent->class_id == BREW_FIRE_CODE &&
                      ent->owner == caster && (ent->aiflags & AI_PROJECTILE_FIXED_TARGET)) {
            missile = ent;
            break;
        }
        T_NOT_NULL(missile);
        if (missile) {
            channel_t const *flight = G_GetChannel(missile);
            T_EQ(missile->s.model, missile_model);
            T_EQ(missile->movetype, MOVETYPE_FLYMISSILE);
            T_ASSERT(missile->velocity > 0.0f);
            T_NOT_NULL(flight);
            if (flight) {
                T_FEQ(flight->origin.x, aim.x, .001f);
                T_FEQ(flight->origin.y, aim.y, .001f);
            }
        }
        FILTER_EDICTS(effect, effect->inuse && effect->s.model == fire_model &&
                      effect->goalentity == front) fire_target = true;
        T_ASSERT(fire_target);
    }
    G_TestPopAbilityConfig();
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_brewmaster, storm_elemental_command_card_includes_dispel_magic) {
    static UnitAbilities_t const storm_abilities = { .abilList = "Adsm,ACcy,ACrk,ANwk" };
    static cstring_t const ability_config =
        "[Adsm]\nArt=ReplaceableTextures\\CommandButtons\\BTNDispelMagic.blp\n"
        "Buttonpos=0,2\nTip=Dispel Magic\n"
        "[ACcy]\nArt=ReplaceableTextures\\CommandButtons\\BTNResurrect.blp\n"
        "Buttonpos=1,2\nTip=Cyclone\n"
        "[ACrk]\nArt=ReplaceableTextures\\PassiveButtons\\PASBTNResistantSkin.blp\n"
        "Buttonpos=3,2\nTip=Resistant Skin\n"
        "[ANwk]\nArt=ReplaceableTextures\\CommandButtons\\BTNWindWalkOn.blp\n"
        "Buttonpos=0,2\nTip=Wind Walk\n";
    gameCommandButton_t buttons[16];
    edict_t *storm;
    uint8_t count;
    gameCommandButton_t *dispel = NULL, *wind_walk = NULL;

    reset_entities(); setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    storm = alloc_test_unit(MAKEFOURCC('n','p','n','2'), 0, 0);
    storm->data.UnitAbilities = (UnitAbilities_t *)&storm_abilities;
    count = G_GetCommandButtons(storm, buttons, sizeof(buttons) / sizeof(buttons[0]));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Adsm")) dispel = &buttons[i];
        if (!strcmp(buttons[i].command, "ANwk")) wind_walk = &buttons[i];
    }
    T_NOT_NULL(dispel);
    T_NOT_NULL(wind_walk);
    if (dispel && wind_walk) {
        T_STREQ(dispel->art, "ReplaceableTextures\\CommandButtons\\BTNDispelMagic.blp");
        T_ASSERT(dispel->x != wind_walk->x || dispel->y != wind_walk->y);
    }
    G_TestPopAbilityConfig();
}

TEST(wc3_brewmaster, earth_elemental_command_card_shows_pulverize_indicator) {
    static UnitAbilities_t const earth_abilities = { .abilList = "ACpv,ANta" };
    static cstring_t const ability_config =
        "[ACpv]\nArt=ReplaceableTextures\\PassiveButtons\\PASBTNSeaGiantPulverize.blp\n"
        "Buttonpos=0,2\nTip=Pulverize\n"
        "[ANta]\nArt=ReplaceableTextures\\CommandButtons\\BTNWarStomp.blp\n"
        "Buttonpos=0,2\nTip=Taunt\n";
    gameCommandButton_t buttons[16];
    edict_t *earth;
    uint8_t count;
    bool found = false;
    gameCommandButton_t *pulverize = NULL, *taunt = NULL;

    reset_entities(); setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    earth = alloc_test_unit(MAKEFOURCC('n','p','n','3'), 0, 0);
    earth->data.UnitAbilities = (UnitAbilities_t *)&earth_abilities;
    count = G_GetCommandButtons(earth, buttons, sizeof(buttons) / sizeof(buttons[0]));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "ACpv")) {
            found = true;
            pulverize = &buttons[i];
            T_ASSERT(buttons[i].disabled);
            T_ASSERT(buttons[i].indicator);
            T_STREQ(buttons[i].art, "ReplaceableTextures\\PassiveButtons\\PASBTNSeaGiantPulverize.blp");
        }
        if (!strcmp(buttons[i].command, "ANta")) taunt = &buttons[i];
    }
    T_ASSERT(found);
    T_NOT_NULL(taunt);
    if (pulverize && taunt)
        T_ASSERT(pulverize->x != taunt->x || pulverize->y != taunt->y);
    G_TestPopAbilityConfig();
}

TEST(wc3_brewmaster, ultimate_returns_same_hero_if_one_elemental_survives) {
    edict_t *hero, *survivor, *controller;
    reset_entities(); setup_test_world(); level.time = 1000;
    hero = alloc_test_unit(FS_SLKKey("Hpal"), 100, 100);
    survivor = alloc_test_unit(FS_SLKKey("hfoo"), 190, 120);
    hero->s.player = survivor->s.player = 0;
    survivor->class_id = MAKEFOURCC('n','p','n','2');
    survivor->owner = hero;
    survivor->summon_ability = BREW_SPLIT_CODE;
    controller = S_SpellIdentityThinker(hero, BREW_SPLIT_CODE, NULL);
    T_NOT_NULL(controller);
    if (!controller) return;
    controller->think = brewmaster_split_think;
    controller->freetime = G_Time();
    hero->aiflags |= AI_SOUL_TRAPPED;
    hero->s.renderfx |= RF_HIDDEN;
    hero->svflags |= SVF_NOCLIENT;
    hero->s.flags |= EF_NOT_SELECTABLE;
    G_SetUnitFlag(hero, UNIT_FLAG_PAUSED, true);
    brewmaster_split_think(controller);
    T_ASSERT(!controller->inuse);
    T_ASSERT(hero->inuse && !M_IsDead(hero));
    T_ASSERT(G_UnitIsWorldActive(hero));
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));
    T_ASSERT(!G_UnitHasFlag(hero, UNIT_FLAG_PAUSED));
    T_ASSERT(!survivor->inuse);
    T_FEQ(hero->s.origin2.x, 190.0f, 50.0f); /* may shift to legal nearby cell */
}

TEST(wc3_brewmaster, ultimate_all_elementals_dead_kills_hidden_hero) {
    edict_t *hero, *controller;
    reset_entities(); setup_test_world(); level.time = 1000;
    hero = alloc_test_unit(FS_SLKKey("Hpal"), 100, 100);
    controller = S_SpellIdentityThinker(hero, BREW_SPLIT_CODE, NULL);
    T_NOT_NULL(controller);
    if (!controller) return;
    controller->think = brewmaster_split_think;
    controller->freetime = G_Time() + 40000;
    hero->aiflags |= AI_SOUL_TRAPPED;
    hero->s.renderfx |= RF_HIDDEN;
    hero->svflags |= SVF_NOCLIENT;
    hero->s.flags |= EF_NOT_SELECTABLE;
    brewmaster_split_think(controller);
    T_ASSERT(!controller->inuse);
    T_ASSERT(M_IsDead(hero));
    T_ASSERT(!(hero->aiflags & AI_SOUL_TRAPPED));
}

TEST(wc3_brewmaster, ultimate_controller_remains_linked_after_save_load) {
    cstring_t file = Test_TempPath("wc3-brewmaster-split.bin");
    edict_t *hero, *controller;
    reset_entities(); setup_test_world(); level.time = 1000;
    hero = alloc_test_unit(FS_SLKKey("Hpal"), 100, 100);
    controller = S_SpellIdentityThinker(hero, BREW_SPLIT_CODE, NULL);
    T_NOT_NULL(controller);
    if (!controller) return;
    controller->think = brewmaster_split_think;
    controller->freetime = G_Time() + 45000;
    hero->aiflags |= AI_SOUL_TRAPPED;
    hero->s.renderfx |= RF_HIDDEN;
    hero->svflags |= SVF_NOCLIENT;
    hero->s.flags |= EF_NOT_SELECTABLE;
    T_ASSERT(WriteGame(file));
    controller->think = NULL; controller->owner = NULL;
    hero->aiflags &= ~AI_SOUL_TRAPPED;
    T_ASSERT(ReadGame(file));
    T_ASSERT(controller->think == brewmaster_split_think);
    T_ASSERT(S_SpellChannelOwner(controller) == hero);
    T_EQ(controller->freetime, 46000u);
    T_ASSERT(!G_UnitIsWorldActive(hero));
    remove(file);
}
#endif /* BZ_TESTS */
