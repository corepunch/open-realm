#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANCH MAKEFOURCC('A', 'N', 'c', 'h')
#define BZ_ACSK MAKEFOURCC('A', 'C', 's', 'k')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(cstring_t text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock mana, range, and level limit keep validation data-driven. */
static cstring_t const charm_slk =
    "ID;PWXL;N;EBB;Y2;X8\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\n"
    "C;Y2;X1;K\"ANch\"\nC;Y2;X2;K\"ANch\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero,organic\"\n"
    "C;Y2;X5;K\"37\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"700\"\n"
    "C;Y2;X8;K\"5\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *target;
    UnitBalance_t target_balance;
} charmFix_t;

static void charm_setup(charmFix_t *fix, uint32_t target_type) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(charm_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->target_balance = MAKE(UnitBalance_t, .maxHealth = 500, .level = 3);
    fix->caster = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 0, 0);
    fix->target = alloc_test_unit(target_type, 96, 0);
    /* Live spawned units have a stand callback; Charm's order cleanup uses it. */
    fix->caster->stand = fix->target->stand = unit_stand;
    fix->caster->s.player = 0; fix->target->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->target->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->target->targtype = TARG_GROUND;
    fix->caster->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_ANCH, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->target->health.value = fix->target->health.max_value = 500;
    fix->target->data.UnitBalance = &fix->target_balance;
}

static void charm_done(charmFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

TEST(wc3_spell, charm_rejects_dead_hero_resistant_and_unbalanced_targets) {
    charmFix_t fix;
    float mana;

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    mana = fix.caster->mana.value;
    fix.target->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('H','p','a','l'));
    mana = fix.caster->mana.value;
    fix.target_balance.strength = 1;
    T_ASSERT(G_UnitIsHero(fix.target));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_AGGRESSIVE].playerType = kPlayerTypeNeutral;
    fix.target_balance.level = 6;
    mana = fix.caster->mana.value;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_ACSK, .level = 1);
    mana = fix.caster->mana.value;
    T_ASSERT(S_UnitIsResistant(fix.target));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->data.UnitBalance = NULL;
    mana = fix.caster->mana.value;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);
}


/* Charm DataA is a neutral-creep cap, not a universal unit level cap. A
 * player-owned rank-9 ordinary unit remains eligible, even though its level
 * exceeds the custom DataA=5 limit. The real spell pipeline must commit mana
 * exactly once and emit the authored temporary overhead presentation. */
TEST(wc3_spell, charm_higher_level_player_unit_uses_transient_target_art) {
    charmFix_t fix;
    uint32_t model;
    edict_t *visual = NULL;
    uint32_t visual_count = 0;

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target_balance.level = 9;
    fix.target->s.radius = 28.0f;
    T_STREQ(G_AbilityEffectArt(BZ_ANCH, WC3_EFFECT_TARGET, 0),
             "TestUI\\Models\\anim_oneshot.mdx");
    model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    T_ASSERT(model != 0);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_EQ(fix.target->s.player, 0u);
    T_ASSERT(fix.target->owner == fix.caster);
    T_FEQ(fix.caster->mana.value, 163.0f, 0.001f);
    FILTER_EDICTS(effect, effect->goalentity == fix.target && effect->s.model == model) {
        visual = effect;
        visual_count++;
        T_ASSERT(effect->s.flags & EF_NOT_SELECTABLE);
        T_ASSERT(effect->movetype == MOVETYPE_LINK);
        T_EQ(effect->damage, fix.target->spawn_time);
        T_ASSERT(effect->s.origin.z > fix.target->s.origin.z);
        T_ASSERT(!(effect->s.flags & EF_NAMED_ATTACHMENT));
    }
    T_EQ(visual_count, 1u);
    T_NOT_NULL(visual);
    T_NULL(unit_findstatus(fix.target, MAKEFOURCC('B','N','c','h')));
    if (visual) {
        /* Temporary effect finishes one model sequence; it is not an
         * endless 'Stand' despite the stock model's looping flag. */
        FOR_LOOP(i, 100) {
            if (!visual->inuse) break;
            M_MoveFrame(visual);
        }
        T_ASSERT(!visual->inuse);
    }
    T_EQ(fix.target->s.player, 0u);
    charm_done(&fix);
}

TEST(wc3_spell, charm_neutral_level_limit_and_player_owner_order_cleanup) {
    charmFix_t fix;
    vec2_t pending = { 192.0f, 0.0f };

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_AGGRESSIVE].playerType = kPlayerTypeNeutral;
    fix.target_balance.level = 5;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_EQ(fix.target->s.player, 0u);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->combatentity = fix.caster;
    T_ASSERT(G_QueueUnitOrder(fix.target, "move", UNIT_ORDER_TARGET_POINT,
                              &pending, NULL, 1, 0.0f, G_OrderId("move")));
    T_EQ(G_UnitQueuedOrderCount(fix.target), 1u);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_EQ(fix.target->s.player, 0u);
    T_EQ(G_UnitQueuedOrderCount(fix.target), 0u);
    T_NULL(fix.target->combatentity);
    T_NULL(fix.target->goalentity);
    charm_done(&fix);
}

TEST(wc3_spell, charm_rejects_ineligible_targets_without_spending_or_creating_art) {
    charmFix_t fix;
    uint32_t model, count = 0;
    float mana;

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    mana = fix.caster->mana.value;
    fix.target->targtype = TARG_MECHANICAL;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.target->targtype = TARG_GROUND;
    T_NOT_NULL(unit_applystatus(fix.target, &(status_application_t){
        .buff = "Zimm", .level = 1, .duration = 30.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    }));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    FILTER_EDICTS(effect, effect->s.model == model && effect->goalentity == fix.target) count++;
    T_EQ(count, 0u);
    T_EQ(fix.target->s.player, 1u);
    charm_done(&fix);
}

#endif
