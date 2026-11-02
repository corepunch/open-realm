#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define DR_SILENCE MAKEFOURCC('A','N','s','i')
#define DR_SILENCE_BUFF MAKEFOURCC('B','N','s','i')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock range, area, duration and hero duration prevent hard-coded
 * retail values from passing. The ordinary ANsi and BNsi art come from
 * NeutralAbilityFunc.txt in the source-controlled test archive. */
static char const silence_slk[] =
    "ID;PWXL;N;EBB;Y2;X11\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"Dur1\"\n"
    "C;Y1;X10;K\"HeroDur1\"\nC;Y1;X11;K\"BuffID1\"\n"
    "C;Y2;X1;K\"ANsi\"\nC;Y2;X2;K\"ANsi\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"air,ground,enemy,organic\"\nC;Y2;X5;K0\nC;Y2;X6;K0\n"
    "C;Y2;X7;K650\nC;Y2;X8;K183\nC;Y2;X9;K13\n"
    "C;Y2;X10;K4\nC;Y2;X11;K\"BNsi\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *hero, *ally, *mechanical, *immune, *far;
    UnitBalance_t hero_balance;
} drSilenceFix_t;

static void dr_silence_setup(drSilenceFix_t *fix) {
    memset(fix, 0, sizeof(*fix));
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(silence_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->caster = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 80, 0);
    fix->hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 210, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    fix->mechanical = alloc_test_unit(MAKEFOURCC('h','m','t','t'), 130, 0);
    fix->immune = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 145, 0);
    fix->far = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 400, 0);
    fix->caster->s.player = fix->ally->s.player = 0;
    fix->enemy->s.player = fix->hero->s.player = fix->mechanical->s.player =
        fix->immune->s.player = fix->far->s.player = 1;
    edict_t *units[] = { fix->caster, fix->enemy, fix->hero, fix->ally,
                         fix->mechanical, fix->immune, fix->far };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->svflags |= SVF_MONSTER;
        units[i]->targtype = TARG_GROUND;
    }
    fix->mechanical->targtype = TARG_MECHANICAL;
    fix->enemy->s.radius = fix->hero->s.radius = 32.0f;
    fix->hero_balance = MAKE(UnitBalance_t, .strength = 20, .level = 1);
    fix->hero->data.UnitBalance = &fix->hero_balance;
    fix->caster->unit_state->ranks[0] = MAKE(heroability_t, .code = DR_SILENCE, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 500;
    T_NOT_NULL(unit_applystatus(fix->immune, &(status_application_t){
        .buff = "Zimm", .level = 1, .duration = 30.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    }));
}

static void dr_silence_done(drSilenceFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

static uint32_t dr_silence_count_art(edict_t *target) {
    uint32_t count = 0;
    FILTER_EDICTS(effect, effect->status_effect_code == DR_SILENCE_BUFF &&
                  effect->goalentity == target && effect->damage == target->spawn_time)
        count++;
    return count;
}

TEST(wc3_spell, dark_ranger_silence_filters_targets_and_uses_authored_durations) {
    drSilenceFix_t fix;
    heroabilitystatus_t *normal, *hero;
    vec2_t point = { 130, 0 };
    dr_silence_setup(&fix);
    T_EQ(S_AbilityItem(DR_SILENCE).ability->proc, CAbilitySilence);
    T_FEQ(S_SpellNumber(DR_SILENCE, ABILITY_NUMBER_AREA, 1), 183, 0.001f);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, DR_SILENCE, &point));
    normal = unit_findstatus(fix.enemy, DR_SILENCE_BUFF);
    hero = unit_findstatus(fix.hero, DR_SILENCE_BUFF);
    T_NOT_NULL(normal);
    T_NOT_NULL(hero);
    if (normal) {
        T_EQ(normal->timestamp, G_Time() + 13000u);
        T_EQ(normal->source_ability, DR_SILENCE);
        T_ASSERT(normal->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
        T_ASSERT(normal->buff_flags & WC3_STATUS_BUFF_NEGATIVE);
        T_ASSERT(normal->buff_flags & WC3_STATUS_BUFF_MAGICAL);
        T_ASSERT(unit_status_can_magic_dispel(normal));
    }
    if (hero) T_EQ(hero->timestamp, G_Time() + 4000u);
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    T_ASSERT(S_UnitIsSilenced(fix.hero));
    T_ASSERT(!S_UnitIsSilenced(fix.ally));
    T_ASSERT(!S_UnitIsSilenced(fix.mechanical));
    T_ASSERT(!S_UnitIsSilenced(fix.immune));
    T_ASSERT(!S_UnitIsSilenced(fix.far));
    dr_silence_done(&fix);
}

TEST(wc3_spell, dark_ranger_silence_area_and_overhead_art_refresh_and_expiry) {
    drSilenceFix_t fix;
    vec2_t point = { 130, 0 };
    uint32_t area_model, target_model, area_count = 0;
    heroabilitystatus_t *status;
    dr_silence_setup(&fix);
    T_STREQ(G_AbilityEffectArt(DR_SILENCE, WC3_EFFECT_EFFECT, 0),
             "TestUI\\Models\\anim_oneshot.mdx");
    T_STREQ(G_AbilityEffectArt(DR_SILENCE_BUFF, WC3_EFFECT_TARGET, 0),
             "TestUI\\Models\\anim_pulse.mdx");
    area_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    target_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    T_ASSERT(area_model > 0 && target_model > 0);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, DR_SILENCE, &point));
    FILTER_EDICTS(effect, effect->s.model == area_model && !effect->goalentity &&
                  Vector2_distance(&effect->s.origin2, &point) < 0.01f) area_count++;
    T_EQ(area_count, 1u);
    T_EQ(dr_silence_count_art(fix.enemy), 1u);
    T_EQ(dr_silence_count_art(fix.hero), 1u);
    FILTER_EDICTS(effect, effect->status_effect_code == DR_SILENCE_BUFF &&
                  effect->goalentity == fix.enemy) {
        T_EQ(effect->s.model, target_model);
        T_ASSERT(!(effect->s.flags & EF_NAMED_ATTACHMENT));
        T_ASSERT(effect->s.origin.z > fix.enemy->s.origin.z);
    }
    /* Refresh must not accumulate status-owned models. */
    T_ASSERT(S_CastPointTargetSpell(fix.caster, DR_SILENCE, &point));
    T_EQ(dr_silence_count_art(fix.enemy), 1u);
    T_EQ(dr_silence_count_art(fix.hero), 1u);
    /* Dispel removes the live presentation tag while the model plays Death. */
    status = unit_findstatus(fix.enemy, DR_SILENCE_BUFF);
    T_NOT_NULL(status);
    if (status) unit_removestatus(fix.enemy, status, STATUS_REMOVE_DISPEL);
    T_EQ(dr_silence_count_art(fix.enemy), 0u);
    T_ASSERT(!S_UnitIsSilenced(fix.enemy));
    level.time += 4001;
    unit_updatestatuses(fix.hero);
    T_EQ(dr_silence_count_art(fix.hero), 0u);
    T_ASSERT(!S_UnitIsSilenced(fix.hero));
    dr_silence_done(&fix);
}

#endif
