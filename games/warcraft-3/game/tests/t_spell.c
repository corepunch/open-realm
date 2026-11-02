#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"
#include "games/warcraft-3/common/wc3_math.h"
#include "fixtures/retail_object_range184.h"

void attack_melee(edict_t *self);
void attack_ranged(edict_t *self);
void whirlwind_think(edict_t *ent);
void morph_end(edict_t *thinker);

uint32_t S_TestHeroAuraAliasResolves(void);
void S_TestResetHeroAuraAliasResolves(void);
bool S_TestAuraDiscoveryMatchesResolver(edict_t *ent);
void S_TestResetAuraClassificationCache(void);
uint32_t S_TestAuraClassResolves(void);
uint32_t S_TestAuraOwnershipChecks(void);
void S_TestResetMoonWellEffectReleaseCalls(void);
uint32_t S_TestMoonWellEffectReleaseCalls(void);
void S_TestResetMoonWellMissingDataWarningCalls(void);
uint32_t S_TestMoonWellMissingDataWarningCalls(void);
uint32_t S_TestMoonWellAlias(edict_t const *);
void S_TestResetMoonWellUpdateCalls(void);
uint32_t S_TestMoonWellUpdateCalls(void);
void S_TestResetDiseaseCloudTargetValidations(void);
uint32_t S_TestDiseaseCloudTargetValidations(void);
void S_TestResetMoonWellClassResolves(void);
uint32_t S_TestMoonWellClassResolves(void);

static pfWriteType_t terrain_deform_capture_types[32];
static int32_t terrain_deform_capture_values[32];
static float terrain_deform_capture_floats[16];
static uint32_t terrain_deform_capture_count, terrain_deform_capture_float_count;
void UI_TestFormatAttackDamageWithStatus(char *, size_t, edict_t const *, int32_t, int32_t, float, float);

static bool spell_cursor_clear_seen;
static bool spell_cursor_splat_seen;
static bool spell_cursor_image_clear;
static float spell_cursor_capture_radius;

static void spell_cursor_capture_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE) {
        spell_cursor_splat_seen = *(int32_t const *)data == svc_cursor_splat;
    } else if (spell_cursor_splat_seen && type == PF_SHORT) {
        spell_cursor_image_clear = *(int32_t const *)data == 0;
    } else if (spell_cursor_splat_seen && type == PF_FLOAT) {
        spell_cursor_capture_radius = *(float const *)data;
        spell_cursor_clear_seen = spell_cursor_image_clear && spell_cursor_capture_radius == 0.0f;
        spell_cursor_splat_seen = false;
    }
}

static char const spell_cursor_test_slk[] =
    "ID;PWXL;N;EBB;Y2;X8\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
    "C;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"DataA1\"\n"
    "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"levels\"\n"
    "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\n"
    "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1000\"\nC;Y2;X6;K\"1000\"\n"
    "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nE\n";

static edict_t *spell_cursor_test_begin(slkTestData_t **rows, slkTestData_t **old) {
    static UnitAbilities_t abilities;
    edict_t *clent, *caster;

    reset_entities(); setup_test_world();
    *rows = parse_slk_string(spell_cursor_test_slk);
    *old = G_SetSLKRows("AbilityData", *rows);
    abilities = (UnitAbilities_t){ .abilList = "AEbl" };
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    caster = alloc_test_unit(MAKEFOURCC('E','w','d','n'), 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = clent->client->ps.number;
    caster->svflags |= SVF_MONSTER;
    caster->stand = unit_stand;
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','b','l'), .level = 1);
    G_SelectEntity(clent->client, caster);
    clent->client->menu.ability_code = MAKEFOURCC('A','E','b','l');
    spell_cursor_clear_seen = false;
    spell_cursor_splat_seen = false;
    spell_cursor_image_clear = false;
    spell_cursor_capture_radius = 0.0f;
    gi.Write = spell_cursor_capture_write;
    G_ClientCommand(clent, 2, (cstring_t[]){ "button", "AEbl" });
    return clent;
}

static void terrain_deform_capture_write(pfWriteType_t type, void const *data) {
    uint32_t slot = terrain_deform_capture_count++;
    if (slot < sizeof(terrain_deform_capture_types) / sizeof(terrain_deform_capture_types[0])) {
        terrain_deform_capture_types[slot] = type;
        if (type == PF_BYTE || type == PF_LONG) terrain_deform_capture_values[slot] = *(int32_t const *)data;
    }
    if (type == PF_FLOAT && terrain_deform_capture_float_count <
        sizeof(terrain_deform_capture_floats) / sizeof(terrain_deform_capture_floats[0]))
        terrain_deform_capture_floats[terrain_deform_capture_float_count++] = *(float const *)data;
}

static void terrain_deform_capture_multicast(vec3_t const *origin, multicast_t to) {
    (void)origin; (void)to;
}

static void spell_panel_test_write(pfWriteType_t type, void const *data) {
    (void)type;
    (void)data;
}

static void spell_panel_test_unicast(edict_t *ent) { (void)ent; }

static char dark_ritual_command_error[64];
static void capture_dark_ritual_command_error(pfWriteType_t type, void const *data) {
    if (type != PF_UIFRAME || !data) return;
    uiFrame_t const *frame = data;
    if (frame->flags.type == FT_STRING && frame->text && frame->textLength < sizeof(dark_ritual_command_error))
        strlcpy(dark_ritual_command_error, frame->text, sizeof(dark_ritual_command_error));
}

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void AbilitySummonMisha_Execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell);
void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(char const *source);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);
void SV_Physics_Toss(edict_t *ent);
static edict_t *make_hero(uint32_t class_id, float hp, float mana, float x, float y);

static intptr_t test_ability_message(edict_t *ent, abilityMsg_t msg, abilityitem_t const *item, spellTarget_t const *target) {
    abilityCall_t call = MAKE(abilityCall_t, .item = item, .target = target);
    return S_AbilityMessage(ent, msg, &call);
}

static intptr_t test_execute_code(edict_t *ent, cstring_t code, spellTarget_t target) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey(code));
    return test_ability_message(ent, A_EXECUTE, &item, &target);
}

TEST(wc3_spell, mana_burn_drains_mana_and_deals_equal_health_damage_at_each_rank) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataA2\"\nC;Y1;X6;K\"DataA3\"\n"
        "C;Y2;X1;K\"AEmb\"\nC;Y2;X2;K\"AEmb\"\nC;Y2;X3;K\"3\"\n"
        "C;Y2;X4;K\"50\"\nC;Y2;X5;K\"100\"\nC;Y2;X6;K\"150\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target;
    uint32_t rank;
    float const amounts[] = { 50.0f, 100.0f, 150.0f };

    old = G_SetSLKRows("AbilityData", rows);
    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 500, 0, 0, 0);
    caster->s.player = 0;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    for (rank = 1; rank <= 3; rank++) {
        caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','m','b'), .level = rank);
        target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
        target->s.player = 1;
        target->svflags |= SVF_MONSTER;
        target->health.value = target->health.max_value = 500.0f;
        target->mana.value = target->mana.max_value = 300.0f;
        T_ASSERT(test_execute_code(caster, "AEmb", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
        if (rank == 1) {
            T_EQ(G_AbilityLightningId(MAKEFOURCC('A','E','m','b'), 0), MAKEFOURCC('M','B','U','R'));
            T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','E','m','b'), WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
            T_ASSERT(level.lightning_effects[0].inuse);
            T_EQ(level.lightning_effects[0].state.effect_id, MAKEFOURCC('M','B','U','R'));
            T_EQ(level.lightning_effects[0].state.end_time, 300);
        }
        T_FEQ(target->mana.value, 300.0f - amounts[rank - 1], 0.001f);
        T_FEQ(target->health.value, 500.0f - amounts[rank - 1], 0.001f);
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, dark_ritual_full_mana_command_shows_error_without_targeting) {
    static UnitAbilities_t const abilities = { .abilList = "AUdr" };
    edict_t *caster, *clent;
    gameClient_t *client = &game.clients[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUdr"));
    abilityCall_t call;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0];
    call = (abilityCall_t){ .item = &item, .client = clent };
    caster = make_hero(MAKEFOURCC('U','l','i','c'), 500, 300, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->mana.value = caster->mana.max_value = 300.0f;
    caster->s.player = client->ps.number = 0;
    clent->client = client;
    G_SetClientConnected(clent, true);
    G_SelectEntity(client, caster);
    dark_ritual_command_error[0] = '\0';
    gi.Write = capture_dark_ritual_command_error;
    gi.unicast = spell_panel_test_unicast;

    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_STREQ(dark_ritual_command_error, "Already at full mana.");
    T_NULL(client->menu.on_entity_selected);
    T_FEQ(caster->mana.value, caster->mana.max_value, 0.001f);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_spell, death_pact_full_health_command_uses_localized_error) {
    static UnitAbilities_t const abilities = { .abilList = "AUdp" };
    edict_t *caster, *clent;
    gameClient_t *client = &game.clients[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUdp"));
    abilityCall_t call;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0];
    call = (abilityCall_t){ .item = &item, .client = clent };
    caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 300, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->health.value = caster->health.max_value;
    caster->s.player = client->ps.number = 0;
    clent->client = client;
    G_SetClientConnected(clent, true);
    G_SelectEntity(client, caster);
    dark_ritual_command_error[0] = '\0';
    gi.Write = capture_dark_ritual_command_error;
    gi.unicast = spell_panel_test_unicast;

    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_STREQ(dark_ritual_command_error, "Hero has full health.");
    T_NULL(client->menu.on_entity_selected);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_spell, dark_ritual_requests_caster_spell_animation) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AUdr\"\nC;Y2;X2;K\"AUdr\"\nC;Y2;X3;K\"0.\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *target;
    abilityitem_t spell;
    spellTarget_t target_arg;

    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('U','l','i','c'), 500, 0, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    caster->s.player = target->s.player = 0;
    target->health.value = target->health.max_value = 100.0f;
    target->die = unit_die;
    spell = S_AbilityItem(FS_SLKKey("AUdr"));
    target_arg = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);

    T_ASSERT(CAbilityDarkRitual(caster, A_EXECUTE, &(abilityCall_t){ .item = &spell, .target = &target_arg }));
    T_STREQ(G_UnitAnimationRequest(caster), "spell");
    T_NOT_NULL(caster->currentmove->endfunc);
    caster->currentmove->endfunc(caster);
    T_ASSERT(strcmp(G_UnitAnimationRequest(caster), "spell") != 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

static const char slk_spell_data[] =
	"ID;PWXL;N;EBB;Y2;X11\n"
	"C;Y1;X1;K\"alias\"\n"
	"C;Y1;X2;K\"code\"\n"
	"C;Y1;X3;K\"targs\"\n"
	"C;Y1;X4;K\"Cost1\"\n"
	"C;Y1;X5;K\"Cool1\"\n"
	"C;Y1;X6;K\"Rng1\"\n"
	"C;Y1;X7;K\"Dur1\"\n"
	"C;Y1;X8;K\"HeroDur1\"\n"
	"C;Y1;X9;K\"DataA1\"\n"
	"C;Y1;X10;K\"DataB1\"\n"
	"C;Y1;X11;K\"Area1\"\n"
	"C;Y2;X1;K\"AHtb\"\n"
	"C;Y2;X2;K\"AHtb\"\n"
	"C;Y2;X3;K\"air,ground,enemy,neutral\"\n"
	"C;Y2;X4;K\"75\"\n"
	"C;Y2;X5;K\"9\"\n"
	"C;Y2;X6;K\"600\"\n"
	"C;Y2;X7;K\"5\"\n"
	"C;Y2;X8;K\"3\"\n"
	"C;Y2;X9;K\"100\"\n"
	"C;Y2;X10;K\"55\"\n"
	"E\n";

static edict_t *make_hero(uint32_t class_id, float hp, float mana, float x, float y) {
	reset_entities();
	setup_test_world();
	edict_t *ent = alloc_test_unit(class_id, x, y);
	ent->health.value = hp;
	ent->health.max_value = hp;
	ent->mana.value = mana;
	ent->mana.max_value = mana;
	ent->svflags |= SVF_MONSTER;
	ent->stand = unit_stand;
	ent->movetype = MOVETYPE_NONE;
	unit_stand(ent);
	return ent;
}

/* Original05b580 outputs, not another implementation of its range formula.
 * Exercise the spell consumer and ensure prediction remains read-only. */
TEST(wc3_spell, range184_matches_original_object_predicate) {
    reset_entities(); setup_test_world();
    edict_t *units[2]={alloc_test_unit(MAKEFOURCC('H','p','a','l'),0,0),
                       alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0)};
    box2_t bounds=CM_GetWorldBounds();
    level.pathing_clock=(wc3Clock_t){.time=1};
    FOR_LOOP(i,sizeof(retail_range184)/sizeof(*retail_range184)) {
        typeof(*retail_range184) *row=retail_range184+i;
        FOR_LOOP(k,2) {
            edict_t *unit=units[k];
            unit->movement.fine_pose=(vec2_t){wc3_float(row->point[k][0]),wc3_float(row->point[k][1])};
            unit->s.origin2=(vec2_t){bounds.min.x+unit->movement.fine_pose.x*32,
                                    bounds.min.y+unit->movement.fine_pose.y*32};
            unit->movement.pose_world=unit->s.origin2;
            unit->movement.pose_valid=true;
            unit->movement.velocity=(vec2_t){wc3_float(row->velocity[k][0])*32,
                                            wc3_float(row->velocity[k][1])*32};
            unit->movement.clock_valid=row->predict;
            unit->movement.pose_clock=(wc3Clock_t){.time=wc3_float(row->old[k])};
            unit->collision=wc3_float(row->radius[k])*32;
        }
        edict_t before[2]={*units[0],*units[1]};
        wc3Clock_t clock=level.pathing_clock;
        T_EQ(S_SpellTargetInRange(units[0],row->same ? units[0] : units[1],wc3_float(row->reach)),row->accepted);
        T_EQ(memcmp(units[0],before,sizeof(*units[0])),0);
        T_EQ(memcmp(units[1],before+1,sizeof(*units[1])),0);
        T_EQ(memcmp(&clock,&level.pathing_clock,sizeof(clock)),0);
    }
    T_ASSERT(!S_SpellTargetInRange(NULL,units[1],100));
    T_ASSERT(!S_SpellTargetInRange(units[0],NULL,100));
    reset_entities(); setup_test_world();
}

/* Holy Bolt consumes authored range and collision edges. Public target
 * orders must not install a walk when the predicted target is already in range.
 * Stop must still cancel an out-of-range pending cast without spending mana. */
TEST(wc3_spell, range184_public_holybolt_uses_edges_prediction_and_stop) {
    FOR_LOOP(schema,2) FOR_LOOP(kind,4) {
        char text[1400];
        snprintf(text,sizeof(text),
            "ID;PWXL;N;EBB;Y2;X7\n"
            "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"%s\"\n"
            "C;Y1;X4;K\"%s\"\nC;Y1;X5;K\"%s\"\nC;Y1;X6;K\"%s\"\nC;Y1;X7;K\"%s\"\n"
            "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend\"\n"
            "C;Y2;X4;K\"13\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"37\"\nE\n",
            schema ? "targs1" : "targs","Cost1","Cool1","Rng1",schema ? "DataA1" : "Data11");
        edict_t *caster=make_hero(MAKEFOURCC('H','p','a','l'),500,200,256,256);
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),kind==0 ? 388 : 420,256);
        slkTestData_t *rows=parse_slk_string(text),*old=G_SetSLKRows("AbilityData",rows);
        UnitAbilities_t abilities={.abilList="AHhb"};
        caster->data.UnitAbilities=&abilities;
        caster->s.player=target->s.player=0;
        caster->collision=target->collision=16;
        target->svflags|=SVF_MONSTER;
        target->targtype=TARG_GROUND;
        target->health.value=100; target->health.max_value=1000;
        caster->unitinfo.MoveSpeed=270;
        if(kind==1 || kind==2) {
            /* Both committed centers remain outside: either independent
             * velocity brings the predicted edge into the configured range. */
            level.pathing_clock=(wc3Clock_t){.time=1};
            edict_t *mover=kind==1 ? caster : target;
            mover->movement.pose_clock=(wc3Clock_t){0};
            mover->movement.clock_valid=true;
            mover->movement.velocity=(vec2_t){kind==1 ? 32 : -32,0};
        }
        T_FEQ(S_SpellRange(MAKEFOURCC('A','H','h','b'),1),100,0);
        T_ASSERT(G_IssueUnitTargetOrder(caster,"holybolt",target,false,0));
        if(kind<3) {
            T_FEQ(target->health.value,137,0);
            T_FEQ(caster->mana.value,187,0);
            T_ASSERT(!move_is_active_order_walk(caster));
        } else {
            T_FEQ(target->health.value,100,0);
            T_FEQ(caster->mana.value,200,0);
            T_ASSERT(move_is_active_order_walk(caster));
            uint32_t slot=globals.num_edicts-1;
            edict_t *thinker=g_edicts+slot;
            T_NULL(thinker->think);
            T_ASSERT(S_UnitTargetApproachReceiver(caster)==thinker);
            T_ASSERT(unit_issueimmediateorder(caster,"stop"));
            if(thinker->inuse && thinker->think)thinker->think(thinker);
            T_ASSERT(!thinker->inuse);
            T_FEQ(target->health.value,100,0);
            T_FEQ(caster->mana.value,200,0);
            T_ASSERT(S_SpellCooldownReady(caster,MAKEFOURCC('A','H','h','b')));
        }
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    reset_entities(); setup_test_world();
}

static moveGroup_t *spell185_group(edict_t const *unit) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t *group=level.move_groups[i];
        if(group->inuse && group->id==unit->movement.group_id)return group;
    }
    return NULL;
}

/* Advance the actual Move owner, rather than calling a retired polling
 * implementation. Legacy point/air fixtures still retain their own thinker. */
static void spell_test_approach_tick(edict_t *caster, edict_t *pending) {
    if(pending->inuse && pending->think) {pending->think(pending);return;}
    FOR_LOOP(i,3) {
        if(!pending->inuse || !caster->inuse)return;
        wc3_clock_advance(&level.pathing_clock,.03f,0);
        level.scheduled_think=true;S_BeginAbilityOwnerUpdates();S_RunAbilityOwnerUpdates();level.scheduled_think=false;
    }
}

/* Public Holy Bolt must submit the authored range to the physical target
 * scheduler, not walk all the way to the target behind a per-frame poll. */
TEST(wc3_spell, approach185_captures_target_range_and_completes_once) {
    FOR_LOOP(schema,2) FOR_LOOP(kind,5) {
        char text[1400];
        snprintf(text,sizeof(text),
            "ID;PWXL;N;EBB;Y2;X7\n"
            "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"%s\"\n"
            "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"%s\"\n"
            "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend\"\n"
            "C;Y2;X4;K\"13\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"37\"\nE\n",
            schema ? "targs1" : "targs",schema ? "DataA1" : "Data11");
        edict_t *caster=make_hero(MAKEFOURCC('H','p','a','l'),500,200,288,288);
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),800,288);
        slkTestData_t *rows=parse_slk_string(text),*old=G_SetSLKRows("AbilityData",rows);
        UnitAbilities_t abilities={.abilList="AHhb"}; caster->data.UnitAbilities=&abilities;
        caster->heroabilities[0]=(heroability_t){.code=MAKEFOURCC('A','H','h','b'),.level=1};
        caster->s.player=target->s.player=0; caster->collision=32; target->collision=31;
        caster->unitinfo.MoveSpeed=270; caster->movetype=MOVETYPE_STEP;
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        target->svflags|=SVF_MONSTER; target->targtype=TARG_GROUND;
        target->health.value=100; target->health.max_value=1000;
        T_ASSERT(!S_UnitHasSpellTask(caster));
        T_ASSERT(G_IssueUnitTargetOrder(caster,"holybolt",target,false,0));
        T_ASSERT(S_UnitHasSpellTask(caster));
        edict_t *pending=g_edicts+globals.num_edicts-1;
        T_ASSERT(pending->inuse); T_NULL(pending->think);
        moveGroup_t *group=spell185_group(caster);
        T_NOT_NULL(group);
        if(group) {
            T_ASSERT(!group->individual); T_ASSERT(group->target==target);
            T_EQ(group->flags&0x1801u,0x1000u);
            T_FEQ(group->members[0].arrival_range,5.09375f,0);
        }
        if(kind==1) {
            T_ASSERT(unit_issueimmediateorder(caster,"stop"));
            T_ASSERT(!S_UnitHasSpellTask(caster));
        }
        if(kind==2) {
            vec2_t point={288,800};T_ASSERT(G_IssueUnitPointOrder(caster,"move",&point,false,0,0));
            T_ASSERT(!S_UnitHasSpellTask(caster));
        }
        if(kind==3) G_FreeEdict(target);
        if(kind==4) {
            uint32_t ci=caster-g_edicts,ti=target-g_edicts,pi=pending-g_edicts;
            FOR_LOOP(tick,5) {level.time+=30;globals.RunFrame();}
            T_ASSERT(WriteGame(Test_TempPath("wc3-spell185-approach.bin")));
            T_ASSERT(ReadGame(Test_TempPath("wc3-spell185-approach.bin")));
            caster=g_edicts+ci;target=g_edicts+ti;pending=g_edicts+pi;
            T_ASSERT(S_UnitHasSpellTask(caster));
            group=spell185_group(caster);T_NOT_NULL(group);
            if(group) {
                T_ASSERT(group->receiver==pending);T_NULL(pending->think);
                T_ASSERT(group->complete==S_SpellTargetApproachComplete);
                T_FEQ(group->members[0].arrival_range,5.09375f,0);
            }
            remove(Test_TempPath("wc3-spell185-approach.bin"));
        }
        float effect=0;
        FOR_LOOP(tick,200) {
            float before=target->health.value;
            level.time+=30; globals.RunFrame();
            if(!pending->inuse) {effect=target->health.value-before;break;}
        }
        T_ASSERT(!pending->inuse);
        if(kind==0 || kind==4) {
            T_FEQ(caster->mana.value,187,0.1f); T_FEQ(effect,37,0.1f);
            T_EQ(caster->movement.group_id,0);
            T_ASSERT(caster->s.origin2.x>600 && caster->s.origin2.x<700);
        } else {
            T_FEQ(caster->mana.value,200,0.1f);
            if(target->inuse)T_FEQ(target->health.value,100,0.1f);
            T_ASSERT(S_SpellCooldownReady(caster,MAKEFOURCC('A','H','h','b')));
        }
        G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
    reset_entities();setup_test_world();
}

/* Two authored rawcodes share Holy Bolt's callbacks but must read their own effect and resource data. */
TEST(wc3_spell, shared_handler_uses_each_requested_rawcode) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\n"
        "C;Y3;X1;K\"A001\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K\"air,ground,friend\"\n"
        "C;Y3;X4;K\"13\"\nC;Y3;X5;K\"7\"\nC;Y3;X6;K\"600\"\nC;Y3;X7;K\"37\"\n"
        "C;Y4;X1;K\"A002\"\nC;Y4;X2;K\"AHhb\"\nC;Y4;X3;K\"air,ground,friend\"\n"
        "C;Y4;X4;K\"23\"\nC;Y4;X5;K\"11\"\nC;Y4;X6;K\"600\"\nC;Y4;X7;K\"89\"\nE\n";
    uint32_t first = MAKEFOURCC('A','0','0','1'), second = MAKEFOURCC('A','0','0','2');
    UnitAbilities_t abilities = { .abilList = "A001,A002" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 1000;
    T_ASSERT(FindAbilityForCommand("A001")->proc == CAbilityHolyBolt);
    T_ASSERT(FindAbilityForCommand("A002")->proc == CAbilityHolyBolt);
    T_ASSERT(S_CastUnitTargetSpell(caster, first, target));
    T_FEQ(target->health.value, 137, 0.001f);
    T_FEQ(caster->mana.value, 187, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, first));
    T_FEQ(S_SpellCooldownLength(caster, first), 7, 0.001f);
    S_SpellEndCooldown(caster, first); /* Existing policy groups cooldowns by base code. */
    InitAbilities();
    T_ASSERT(S_CastUnitTargetSpell(caster, second, target));
    T_FEQ(target->health.value, 226, 0.001f);
    T_FEQ(caster->mana.value, 164, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, second));
    T_FEQ(S_SpellCooldownLength(caster, second), 11, 0.001f);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, wind_walk_delays_authored_cooldown_until_end_and_round_trips_origin) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"17\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"2.75\"\n"
        "C;Y2;X7;K\"2.75\"\nC;Y2;X8;K\"33\"\nC;Y2;X9;K\"77\"\nE\n";
    cstring_t save = Test_TempPath("wind-walk-save.bin");
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    edict_t *target;
    heroabilitystatus_t *status;
    uint32_t number;

    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    T_EQ(S_AbilityItem(MAKEFOURCC('A','O','w','k')).ability->proc, CAbilityWindWalk);
    T_EQ(S_AbilityItem(MAKEFOURCC('A','N','w','k')).ability->proc, CAbilityWindWalk);
    T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','O','w','k')));
    status = unit_findstatus(caster, MAKEFOURCC('B','O','w','k'));
    T_NOT_NULL(status);
    T_EQ(status->duration_ms, 2750);
    T_EQ(status->data, MAKEFOURCC('A','O','w','k'));
    T_ASSERT(S_StatusIsUndispellable(status));
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));

    number = caster->s.number;
    T_ASSERT(WriteGame(save));
    G_EnsureUnitStatusSlots(caster);
    memset(caster->abilstatus, 0, MAX_UNIT_STATUSES * sizeof(*caster->abilstatus));
    T_ASSERT(ReadGame(save));
    caster = g_edicts + number;
    status = unit_findstatus(caster, MAKEFOURCC('B','O','w','k'));
    T_NOT_NULL(status);
    T_EQ(status->data, MAKEFOURCC('A','O','w','k'));
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));

    S_ResolveAttackHit(caster, target, 10);
    T_NULL(unit_findstatus(caster, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(target->health.value < 100.0f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));
    T_FEQ(S_SpellCooldownLength(caster, MAKEFOURCC('A','O','w','k')), 13.0f, 0.001f);

    remove(save);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, wind_walk_and_temporary_invisibility_keep_hidden_until_both_expire) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"2.75\"\n"
        "C;Y2;X7;K\"2.75\"\nC;Y2;X8;K\"77\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *walk_first, *walk_second;
    heroabilitystatus_t *wind, *invis;

    setup_test_world();
    walk_first = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    walk_first->data.UnitAbilities = &abilities;
    walk_first->s.player = 0;
    T_ASSERT(S_CastNoTargetSpell(walk_first, MAKEFOURCC('A','O','w','k')));
    unit_addtimedstatus(walk_first, "Bixx", 1, 10.0f);
    unit_findstatus(walk_first, MAKEFOURCC('B','i','x','x'))->data = MAKEFOURCC('A','I','v','i');
    wind = unit_findstatus(walk_first, MAKEFOURCC('B','O','w','k'));
    invis = unit_findstatus(walk_first, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(wind); T_NOT_NULL(invis);
    level.time = wind->timestamp;
    unit_updatestatuses(walk_first);
    T_NULL(unit_findstatus(walk_first, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(G_UnitStatusLevel(walk_first, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(walk_first->s.renderfx & RF_HIDDEN);

    level.time = invis->timestamp;
    unit_updatestatuses(walk_first);
    T_ASSERT(!(walk_first->s.renderfx & RF_HIDDEN));

    walk_second = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    walk_second->data.UnitAbilities = &abilities;
    walk_second->s.player = 0;
    T_ASSERT(S_CastNoTargetSpell(walk_second, MAKEFOURCC('A','O','w','k')));
    unit_addtimedstatus(walk_second, "Bixx", 1, 1.0f);
    unit_findstatus(walk_second, MAKEFOURCC('B','i','x','x'))->data = MAKEFOURCC('A','I','v','i');
    wind = unit_findstatus(walk_second, MAKEFOURCC('B','O','w','k'));
    invis = unit_findstatus(walk_second, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(wind); T_NOT_NULL(invis);
    level.time = invis->timestamp;
    unit_updatestatuses(walk_second);
    T_ASSERT(G_UnitStatusLevel(walk_second, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(walk_second->s.renderfx & RF_HIDDEN);

    wind = unit_findstatus(walk_second, MAKEFOURCC('B','O','w','k'));
    level.time = wind->timestamp;
    unit_updatestatuses(walk_second);
    T_ASSERT(!(walk_second->s.renderfx & RF_HIDDEN));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, invisibility_spell_uses_authored_buff_and_breaks_through_status_owner) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"9\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"Bixx\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target, *enemy;
    heroabilitystatus_t *status;

    setup_test_world();
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER;
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 100.0f;
    enemy->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), target));
    status = unit_findstatus(target, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(status);
    if (status) T_EQ(status->data, MAKEFOURCC('A','i','v','s'));
    T_ASSERT(target->s.renderfx & RF_HIDDEN);
    T_ASSERT(S_UnitUsesInvisibilityRenderFlag(target));
    T_ASSERT(S_UnitIsInvisibleToPlayer(target, 1));

    S_ResolveAttackHit(target, enemy, 10);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, stock_invisibility_spell_applies_and_expires_binv) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"120\"\nC;Y2;X8;K\"120\"\nC;Y2;X9;K\"Binv\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target;
    heroabilitystatus_t *status;

    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), target));
    status = unit_findstatus(target, MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);
    if (status) {
        T_EQ(status->duration_ms, 120000);
        T_EQ(status->data, MAKEFOURCC('A','i','v','s'));
        T_ASSERT(target->s.renderfx & RF_HIDDEN);
        level.time = status->timestamp;
        unit_updatestatuses(target);
        T_NULL(unit_findstatus(target, MAKEFOURCC('B','i','n','v')));
        T_ASSERT(!(target->s.renderfx & RF_HIDDEN));
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, invisibility_spell_breaks_on_committed_spell) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"9\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"Bixx\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\n"
        "C;Y3;X1;K\"AOwk\"\nC;Y3;X2;K\"AOwk\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"13\"\nC;Y3;X7;K\"2.75\"\n"
        "C;Y3;X8;K\"2.75\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    UnitAbilities_t walk_abilities = { .abilList = "AOwk,Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *unit;

    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    unit->data.UnitAbilities = &walk_abilities;
    unit->s.player = 0;
    unit->targtype = TARG_GROUND;
    unit->health.value = unit->health.max_value = 100.0f;
    unit->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), unit));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(S_CastNoTargetSpell(unit, MAKEFOURCC('A','O','w','k')));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    T_ASSERT(S_CastUnitTargetSpell(unit, MAKEFOURCC('A','i','v','s'), unit));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    T_ASSERT(!S_SpellCooldownReady(unit, MAKEFOURCC('A','O','w','k')));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, numeric_status_payload_is_not_dispatched_as_wind_walk_owner) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"10\"\n"
        "C;Y2;X7;K\"10\"\nC;Y2;X8;K\"77\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    heroabilitystatus_t *payload;

    unit->data.UnitAbilities = &abilities;
    unit->s.player = 0;
    unit_addtimedstatus(unit, "Bams", 1, 30.0f);
    payload = unit_findstatus(unit, MAKEFOURCC('B','a','m','s'));
    T_NOT_NULL(payload);
    if (payload) payload->data = MAKEFOURCC('A','O','w','k');
    T_ASSERT(S_CastNoTargetSpell(unit, MAKEFOURCC('A','O','w','k')));

    T_EQ(S_UnitStatusAbilityEvent(unit, A_ATTACK_DAMAGE_BONUS, NULL), 77);
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_EQ(payload ? payload->data : 0, MAKEFOURCC('A','O','w','k'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Validation and deferred completion must use the issued alias as well as the effect callback. */
TEST(wc3_spell, custom_spells_keep_identity_in_validation_and_channel_completion) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"Area1\"\n"
        "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\nC;Y2;X4;K\"600\"\n"
        "C;Y2;X5;K\"600\"\nC;Y2;X6;K\"0\"\n"
        "C;Y3;X1;K\"A003\"\nC;Y3;X2;K\"AEbl\"\nC;Y3;X3;K\"10\"\nC;Y3;X4;K\"600\"\n"
        "C;Y3;X5;K\"600\"\nC;Y3;X6;K\"100\"\n"
        "C;Y4;X1;K\"A004\"\nC;Y4;X2;K\"AHbz\"\nC;Y4;X3;K\"20\"\nC;Y4;X4;K\"600\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"10\"\nC;Y4;X7;K\"1\"\nC;Y4;X8;K\"80\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "A003,A004" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    vec2_t point = {50, 0};

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(!S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','3'), &point));
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_FEQ(caster->s.origin2.x, 0, 0.001f);
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','4'), &point));
    T_FEQ(caster->mana.value, 180, 0.001f);
    edict_t *thinker = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].owner == caster && g_edicts[i].think) { thinker = g_edicts + i; break; }
    T_NOT_NULL(thinker);
    level.time = thinker->freetime; G_RunEntity(thinker);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_ASSERT(!caster->channel || caster->channel->code == 0);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, registry_keeps_identity_outside_shared_handlers) {
    cstring_t const fires[] = { "Afih", "Afin", "Afio", "Afir", "Afiu" };
    cstring_t const abstract[] = { "abil", "AAin", "AAbt", "AAsp", "AAsm", "Amor", "ABon", "ATrn" };
    InitAbilities();
    T_EQ(FindAbilityByClassname(STR_CmdTrains)->proc, CAbilityTrain);
    T_EQ(GetAbilityByIndex(GetAbilityIndex(CAbilityTrain))->proc, CAbilityTrain);
    FOR_LOOP(i, sizeof(fires) / sizeof(*fires)) {
        uint32_t code = FS_SLKKey(fires[i]);
        abilityitem_t item = S_AbilityItem(code);
        T_EQ(item.code, code);
        T_EQ(item.ability->proc, CAbilityOnFireHuman);
    }
    FOR_LOOP(i, sizeof(abstract) / sizeof(*abstract)) T_NULL(FindAbilityByClassname(abstract[i]));
    T_EQ(FindAbilityByClassname("AIco")->proc, CAbilityCharm);
    T_EQ(FindAbilityByClassname("Afbk")->proc, CAbilityFeedback);
    T_ASSERT(!(FindAbilityByClassname("Afbk")->flags & AB_SPELL));
    T_EQ(FindAbilityByClassname("Abtl")->proc, CAbilityBattlestations);
    T_ASSERT(FindAbilityByClassname("Abtl")->flags & AB_COMMAND);
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *abil = GetAbilityByIndex(i);
        T_NOT_NULL(abil);
        T_EQ(GetAbilityByIndex(GetAbilityIndex(abil->proc))->proc, abil->proc);
        if (abil->flags & AB_SPELL) {
            T_NOT_NULL(abil->proc); T_ASSERT(S_AbilityHasCommand(abil));
        }
    }
}

/* Barkskin is a friendly target spell whose modal parent owns autocast selection, not a self-toggle. */
static void test_barkskin_contract(cstring_t armor_field) {
	char slk[1024];
	uint32_t const barkskin = MAKEFOURCC('A','b','a','r'), buff = MAKEFOURCC('B','b','a','r');
	UnitAbilities_t abilities = { .abilList = "Abar" };
	slkTestData_t *rows, *old;
	edict_t *caster = make_hero(MAKEFOURCC('e','d','o','c'), 500, 200, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('e','a','r','c'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 75, 0);
	ability_t const *ability;

	snprintf(slk, sizeof(slk),
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
		"C;Y1;X7;K\"%s\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
		"C;Y2;X1;K\"Abar\"\nC;Y2;X2;K\"Abar\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"600\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"4\"\n"
		"C;Y2;X7;K\"3\"\nC;Y2;X8;K\"Bbar\"\nC;Y2;X9;K\"1\"\nE\n", armor_field);
	rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->targtype = enemy->targtype = TARG_GROUND;
	ally->health.value = ally->health.max_value = enemy->health.value = enemy->health.max_value = 100;
	ally->armor_value = 2.0f; level.time = 1000;

	ability = FindAbilityByClassname("Abar");
	T_NOT_NULL(ability); T_EQ(ability->proc, CAbilityBarkskin);
	T_ASSERT(ability->flags & AB_SPELL); T_ASSERT(ability->flags & AB_AUTOCAST);
	T_ASSERT(!(ability->flags & AB_TOGGLE)); T_EQ(ability->target_type, SPELL_TARGET_UNIT);
	T_ASSERT(!G_UnitAutocastIsOn(caster, barkskin));
	T_ASSERT(G_SetUnitAutocast(caster, barkskin, true)); T_ASSERT(G_UnitAutocastIsOn(caster, barkskin));
	T_ASSERT(test_ability_message(caster, A_AUTOCAST_ACQUIRE, &(abilityitem_t){
		.code = barkskin, .ability = ability
	}, NULL));
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	T_ASSERT(!test_ability_message(caster, A_AUTOCAST_ACQUIRE, &(abilityitem_t){
		.code = barkskin, .ability = ability
	}, NULL));
	T_ASSERT(!S_CastUnitTargetSpell(caster, barkskin, enemy));
	T_ASSERT(S_CastUnitTargetSpell(caster, barkskin, ally));
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	T_ASSERT(G_SetUnitAutocast(caster, barkskin, false)); T_ASSERT(!G_UnitAutocastIsOn(caster, barkskin));
	T_EQ(G_UnitStatusLevel(ally, buff), 1);

	level.time = 6000;
	T_ASSERT(S_CastUnitTargetSpell(caster, barkskin, ally));
	level.time = 15000;
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	level.time = 16001; unit_updatestatuses(ally);
	T_EQ(G_UnitStatusLevel(ally, buff), 0); T_FEQ(G_UnitArmorValue(ally), 2.0f, 0.001f);
	ally->health.value = 0;
	T_ASSERT(!S_CastUnitTargetSpell(caster, barkskin, ally));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, barkskin_roc_schema_uses_timed_friendly_armor_buff) { test_barkskin_contract("Data11"); }
TEST(wc3_spell, barkskin_tft_schema_uses_timed_friendly_armor_buff) { test_barkskin_contract("DataA1"); }

TEST(wc3_spell, relationship_uses_passive_alliance_not_other_flags) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	caster->s.player = 0;
	target->s.player = 1;
	target->svflags |= SVF_MONSTER;
	target->health.value = target->health.max_value = 100;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));

	memset(level.alliances, 0, sizeof(level.alliances));
	level.alliances[0][1] = 1 << ALLIANCE_SHARED_VISION;
	T_ASSERT(S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));

	level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));

	target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	T_ASSERT(S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps,
	                    ALLIANCE_PASSIVE, true);
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));

	target->s.player = PLAYER_NEUTRAL_PASSIVE;
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_PASSIVE].ps,
	                    ALLIANCE_PASSIVE, true);
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_PASSIVE].ps,
	                    ALLIANCE_PASSIVE, false);
	T_ASSERT(S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));
}

/* ---- Channel enforcement ---- */

TEST(wc3_spell, channel_cancel_stun) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	caster->channel->origin = caster->s.origin2;
	unit_addtimedstatus(caster, "Bstu", 1, 10.0f);
	spell_run_frame(caster);
	T_EQ((int)caster->channel->code, 0);
}

/* Expired status slots must stop contributing before the status cleanup tick. */
TEST(wc3_spell, expired_stun_does_not_cancel_channel_before_cleanup) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    level.time = 1000;
    unit_addtimedstatus(caster, "Bstu", 1, 0.1f);
    (caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
    caster->channel->code = MAKEFOURCC('A','H','b','z');
    caster->channel->origin = caster->s.origin2;
    level.time = 1100;
    spell_run_frame(caster);
    T_EQ(caster->channel->code, MAKEFOURCC('A','H','b','z'));
}

TEST(wc3_spell, channel_cancel_death) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	caster->channel->origin = caster->s.origin2;
	caster->health.value = 0;
	spell_run_frame(caster);
	T_EQ((int)caster->channel->code, 0);
}

TEST(wc3_spell, channel_cancel_movement) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	caster->channel->origin.x = 100; caster->channel->origin.y = 100;
	caster->s.origin.x = 101; caster->s.origin.y = 100;
	caster->s.origin2.x = 101; caster->s.origin2.y = 100;
	spell_run_frame(caster);
	T_EQ((int)caster->channel->code, 0);
}

TEST(wc3_spell, channel_persists_no_movement) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	caster->channel->origin = caster->s.origin2;
	spell_run_frame(caster);
	T_EQ((int)caster->channel->code, (int)MAKEFOURCC('A','H','b','z'));
}

TEST(wc3_spell, channel_cancel_clears_code) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	S_SpellCancelChannel(caster);
	T_EQ((int)caster->channel->code, 0);
	T_STREQ(caster->currentmove->animation, "stand");
}

TEST(wc3_spell, channel_cancel_noop_empty) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = 0;
	S_SpellCancelChannel(caster);
	T_EQ((int)caster->channel->code, 0);
}

TEST(wc3_spell, channel_run_frame_noop_when_idle) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = 0;
	unit_addtimedstatus(caster, "Bstu", 1, 10.0f);
	spell_run_frame(caster);
	T_EQ((int)caster->channel->code, 0);
}

/* ---- Toggle bypass ---- */

TEST(wc3_spell, immolation_command_toggle_damages_enemy_land_units_until_mana_or_toggle_off) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y1;X8;K\"Cost2\"\nC;Y1;X9;K\"Area2\"\nC;Y1;X10;K\"DataA2\"\nC;Y1;X11;K\"DataB2\"\n"
        "C;Y2;X1;K\"AEim\"\nC;Y2;X2;K\"AEim\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"25\"\nC;Y2;X5;K\"160\"\nC;Y2;X6;K\"10\"\nC;Y2;X7;K\"7\"\n"
        "C;Y2;X8;K\"25\"\nC;Y2;X9;K\"160\"\nC;Y2;X10;K\"15\"\nC;Y2;X11;K\"7\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "AEim" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *enemy_land, *friend_land, *enemy_air;
    edict_t *clent;
    gameClient_t *client;
    cstring_t button[] = { "button", "AEim" };
    gameCommandButton_t command_button;
    uint32_t const buff = MAKEFOURCC('B','E','i','m');
    heroabilitystatus_t *immolation_status;
    edict_t *caster_effect;

    old = G_SetSLKRows("AbilityData", rows);
    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 1000, 24, 0, 0);
    enemy_land = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    friend_land = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 20);
    enemy_air = alloc_test_unit(MAKEFOURCC('h','g','r','y'), 100, -20);
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    client->connected = true;
    client->ps.number = 0;
    caster->data.UnitAbilities = &abilities;
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','i','m'), .level = 2);
    caster->s.player = friend_land->s.player = 0;
    enemy_land->s.player = enemy_air->s.player = 1;
    enemy_land->svflags |= SVF_MONSTER;
    enemy_air->svflags |= SVF_MONSTER;
    enemy_land->targtype = friend_land->targtype = TARG_GROUND;
    enemy_air->targtype = TARG_AIR;
    enemy_land->health.value = friend_land->health.value = enemy_air->health.value = 100;
    enemy_land->health.max_value = friend_land->health.max_value = enemy_air->health.max_value = 100;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    T_ASSERT(S_SpellIsAliveTarget(enemy_land));
    T_ASSERT(S_SpellIsEnemy(caster, enemy_land));
    T_EQ(G_UnitTargetType(enemy_land), TARG_GROUND);
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','E','i','m'), 1)->area, 160.0f, 0.001f);
    G_SelectEntity(client, caster);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");

    /* Cost1 is a distinct upfront charge; insufficient mana must leave the
     * toggle off. The 7 mana/sec drain is delivered one point at a time. */
    G_ClientCommand(clent, 2, button);
    T_NULL(unit_findstatus(caster, buff));
    T_FEQ(caster->mana.value, 24.0f, 0.001f);
    caster->mana.value = 39.0f;
    G_ClientCommand(clent, 2, button);
    immolation_status = unit_findstatus(caster, buff);
    T_NOT_NULL(immolation_status);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOff.blp");
    T_EQ(G_UnitStatusBuffCode(immolation_status), buff);
    T_STREQ(G_AbilityBuffArt(buff), "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    T_STREQ(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NOT_NULL(caster_effect);
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 142;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 143;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 13.0f, 0.001f);
    level.time = 500;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 11.0f, 0.001f);
    level.time = 1000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 85.0f, 0.001f);
    T_FEQ(friend_land->health.value, 100.0f, 0.001f);
    T_FEQ(enemy_air->health.value, 100.0f, 0.001f);
    T_FEQ(caster->mana.value, 7.0f, 0.001f);
    client->commands_dirty = false;
    level.time = 2000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 70.0f, 0.001f);
    T_FEQ(caster->mana.value, 0.0f, 0.001f);
    T_NULL(unit_findstatus(caster, buff));
    T_ASSERT(client->commands_dirty);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NULL(caster_effect);
    level.time = 3000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 70.0f, 0.001f);

    /* It can also be switched off before its mana runs out. */
    caster->mana.value = 39.0f;
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(unit_findstatus(caster, buff));
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 4000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 55.0f, 0.001f);
    T_FEQ(caster->mana.value, 7.0f, 0.001f);
    G_ClientCommand(clent, 2, button);
    T_NULL(unit_findstatus(caster, buff));
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NULL(caster_effect);
    level.time = 5000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 55.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Both schemas must honor fractional and zero DataB without changing DPS. */
TEST(wc3_spell, immolation_custom_mana_rate_keeps_one_second_damage_pulses) {
    float const rates[] = { 3.5f, 0.0f, 14.0f };
    for (uint32_t roc = 0; roc < 2; roc++) {
        FOR_LOOP(i, sizeof(rates) / sizeof(rates[0])) {
            char slk[1024];
            snprintf(slk, sizeof(slk),
                "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
                "C;Y1;X4;K\"%s\"\nC;Y1;X5;K\"%s\"\nC;Y1;X6;K\"%s\"\n"
                "C;Y2;X1;K\"AEim\"\nC;Y2;X2;K\"AEim\"\nC;Y2;X3;K1\n"
                "C;Y2;X4;K160\nC;Y2;X5;K13\nC;Y2;X6;K%g\nE\n",
                "Area1", roc ? "Data11" : "DataA1", roc ? "Data12" : "DataB1", rates[i]);
            slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
            reset_entities(); setup_test_world(); level.time = 0;
            edict_t *caster = make_hero(MAKEFOURCC('E','d','e','m'), 1000, 100, 0, 0);
            edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
            static UnitAbilities_t const abilities = { .abilList = "AEim" };
            caster->data.UnitAbilities = &abilities;
            caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','i','m'), .level = 1);
            caster->s.player = 0; enemy->s.player = 1;
            enemy->svflags |= SVF_MONSTER;
            enemy->targtype = TARG_GROUND;
            enemy->health.value = enemy->health.max_value = 100;
            memset(level.alliances, 0, sizeof(level.alliances));
            ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
            ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
            abilityitem_t const item = S_AbilityItem(MAKEFOURCC('A','E','i','m'));
            abilityCall_t const call = MAKE(abilityCall_t, .item = &item);
            T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
            level.time = 999; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 100, 0.001f);
            level.time = 1000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 87, 0.001f);
            T_FEQ(caster->mana.value, 100 - rates[i], 0.001f);
            cstring_t const filename = Test_TempPath("immolation-substeps.bin");
            T_ASSERT(WriteGame(filename));
            T_ASSERT(ReadGame(filename));
            remove(filename);
            level.time = 2000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 74, 0.001f);
            T_FEQ(caster->mana.value, 100 - 2 * rates[i], 0.001f);
            T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
            level.time = 3000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 74, 0.001f);
            G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
        }
    }
}

/* ---- Spell helpers ---- */

TEST(wc3_spell, spell_code_to_string) {
	char buf[8];
	S_SpellCodeString(MAKEFOURCC('A','H','t','b'), buf);
	T_STREQ(buf, "AHtb");
}

TEST(wc3_spell, generic_data_id_keeps_rawcode_view) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\n"
		"C;Y2;X1;K\"Amil\"\n"
		"C;Y2;X2;K\"Amil\"\n"
		"C;Y2;X3;K\"hpea\"\n"
		"C;Y2;X4;K\"hmil\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_EQ((int)S_SpellDataId(MAKEFOURCC('A','m','i','l'), 1, 1), (int)MAKEFOURCC('h','p','e','a'));
	T_EQ((int)S_SpellDataId(MAKEFOURCC('A','m','i','l'), 1, 2), (int)MAKEFOURCC('h','m','i','l'));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, hero_passives_use_authored_data_and_runtime_consumers) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y9;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y2;X1;K\"AHab\"\nC;Y2;X2;K\"AHab\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"0.75\"\n"
		"C;Y3;X1;K\"AUau\"\nC;Y3;X2;K\"AUau\"\nC;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.5\"\n"
		"C;Y4;X1;K\"AUav\"\nC;Y4;X2;K\"AUav\"\nC;Y4;X3;K\"900\"\nC;Y4;X4;K\"0.2\"\n"
		"C;Y5;X1;K\"AOcr\"\nC;Y5;X2;K\"AOcr\"\nC;Y5;X4;K\"100\"\nC;Y5;X5;K\"2\"\n"
		"C;Y6;X1;K\"AEev\"\nC;Y6;X2;K\"AEev\"\nC;Y6;X4;K\"1\"\n"
		"C;Y7;X1;K\"AUts\"\nC;Y7;X2;K\"AUts\"\nC;Y7;X4;K\"0.15\"\nC;Y7;X5;K\"1\"\nC;Y7;X6;K\"3\"\n"
		"C;Y8;X1;K\"ACct\"\nC;Y8;X2;K\"ACct\"\nC;Y8;X4;K\"100\"\nC;Y8;X5;K\"2\"\n"
		"C;Y9;X1;K\"AHad\"\nC;Y9;X2;K\"AHad\"\nC;Y9;X3;K\"900\"\nC;Y9;X4;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *source = make_hero(MAKEFOURCC('H','a','m','g'), 500, 300, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	level.time = 0;
	source->s.player = target->s.player = 0;
	S_AttackProfileWrite(target, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(target, 0)->weapon = WPN_NORMAL;
	S_AttackProfileWrite(target, 0)->range = 90.0f;
	source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','b'), .level = 1);
	source->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 1);
	source->heroabilities[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','v'), .level = 1);
	source->heroabilities[3] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 1);
	target->armor_value = 2.0f;
	target->mana.value = target->mana.max_value = 100.0f;
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	T_FEQ(G_UnitArmorValue(target), 5.0f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.1f, 0.001f);
	T_FEQ(S_UnholyHealthRegen(target), 0.5f, 0.001f);
	T_FEQ(S_VampiricLifeSteal(target), 0.2f, 0.001f);
	level.time = AURA_UPDATE_MS / 2;
	target->s.origin2.x = 901.0f;
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_BrillianceManaRegen(target), 0.0f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.0f, 0.001f);
	T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

	target->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','r'), .level = 1);
	target->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','e','v'), .level = 1);
	target->heroabilities[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','t','s'), .level = 1);
	T_EQ(S_CriticalStrikeDamage(target, 10), 20);
	T_ASSERT(S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 5.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 2.0f), 1.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 20.0f), 3.0f, 0.001f);
	memset(target->heroabilities, 0, sizeof(target->heroabilities));
	T_EQ(S_CriticalStrikeDamage(target, 10), 10);
	T_ASSERT(!S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

	/* Feral Spirit's Dire/Shadow Wolves own the creep Critical Strike ability, ACct. */
	UnitAbilities_t creep_abilities = { .abilList = "ACct" };
	target->data.UnitAbilities = &creep_abilities;
	T_EQ(S_CriticalStrikeDamage(target, 10), 20);
	target->data.UnitAbilities = NULL;

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_pulverize_and_spiked_aliases_use_authored_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"DataD1\"\nC;Y1;X8;K\"DataE1\"\n"
		"C;Y2;X1;K\"Awar\"\nC;Y2;X2;K\"Awar\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"100\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"32\"\nC;Y2;X7;K\"48\"\n"
		"C;Y3;X1;K\"Aspi\"\nC;Y3;X2;K\"Aspi\"\nC;Y3;X4;K\"0.15\"\nC;Y3;X5;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 10, 0);
	edict_t *splash = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 30, 0);
	UnitAbilities_t abilities = { .abilList = "Awar,Aspi" };

	attacker->data.UnitAbilities = &abilities;
	attacker->s.player = 0; primary->s.player = splash->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	primary->svflags |= SVF_MONSTER; splash->svflags |= SVF_MONSTER;
	primary->targtype = splash->targtype = TARG_GROUND;
	primary->health.value = primary->health.max_value = 100;
	splash->health.value = splash->health.max_value = 100;
	T_EQ(G_UnitAbilityLevel(attacker, MAKEFOURCC('A','w','a','r')), 1);
	T_FEQ(S_SpellData(MAKEFOURCC('A','w','a','r'), 1, 1), 100, 0.001f);
	S_PulverizeAttack(attacker, primary);
	T_FEQ(splash->health.value, 75, 0.001f);
	T_FEQ(S_SpikedDamageReturn(attacker, 20), 3, 0.001f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_incinerate_stacks_and_explodes_from_authored_rows) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"ANia\"\nC;Y2;X2;K\"ANia\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"2\"\nC;Y2;X5;K\"20\"\nC;Y2;X6;K\"BNic\"\nC;Y2;X7;K\"80\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	UnitAbilities_t abilities = { .abilList = "ANia" };

	attacker->data.UnitAbilities = &abilities;
	attacker->s.player = 0; victim->s.player = nearby->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	victim->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
	victim->die = unit_die; nearby->die = unit_die;
	victim->targtype = nearby->targtype = TARG_GROUND;
	victim->health.value = victim->health.max_value = 100;
	nearby->health.value = nearby->health.max_value = 100;
	S_IncinerateOnHit(attacker, victim);
	T_FEQ(victim->health.value, 98, 0.001f);
	T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("BNic")), 1);
	S_IncinerateOnHit(attacker, victim);
	T_FEQ(victim->health.value, 94, 0.001f);
	T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("BNic")), 2);
	victim->health.value = 1;
	S_IncinerateOnHit(attacker, victim);
	T_ASSERT(M_IsDead(victim));
	T_FEQ(nearby->health.value, 80, 0.001f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_attack_passives_and_death_aoe_use_authored_rows) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y5;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y2;X1;K\"ANmr\"\nC;Y2;X2;K\"ANmr\"\nC;Y2;X5;K\"17\"\n"
        "C;Y3;X1;K\"Aliq\"\nC;Y3;X2;K\"Aliq\"\nC;Y3;X3;K\"100\"\nC;Y3;X4;K\"5\"\nC;Y3;X7;K\"0.25\"\nC;Y3;X9;K\"Bliq\"\n"
        "C;Y4;X1;K\"Acor\"\nC;Y4;X2;K\"Acor\"\nC;Y4;X3;K\"100\"\nC;Y4;X4;K\"5\"\nC;Y4;X9;K\"Bcor\"\n"
        "C;Y5;X1;K\"Adda\"\nC;Y5;X2;K\"Adda\"\nC;Y5;X3;K\"40\"\nC;Y5;X5;K\"30\"\nC;Y5;X6;K\"25\"\nC;Y5;X7;K\"80\"\nC;Y5;X8;K\"10\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    edict_t *dead = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 0, 0);
    edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    UnitAbilities_t attack_abilities = { .abilList = "ANmr,Aliq,Acor" };
    UnitAbilities_t death_abilities = { .abilList = "Adda" };
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Adda"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    attacker->data.UnitAbilities = &attack_abilities; dead->data.UnitAbilities = &death_abilities;
    attacker->s.player = dead->s.player = 0; target->s.player = nearby->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    target->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
    target->mana.value = target->mana.max_value = 100;
    target->health.value = target->health.max_value = 100;
    nearby->health.value = nearby->health.max_value = 100;
    dead->die = unit_die; nearby->die = unit_die;
    S_CreepAttackOnHit(attacker, target);
    T_FEQ(target->mana.value, 83, 0.001f);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bliq")), 1);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bcor")), 1);
    T_FEQ(S_CreepAttackSpeedReduction(target), 0.25f, 0.001f);
    S_AbilityMessage(dead, A_DEATH, &call);
    T_FEQ(nearby->health.value, 90, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_bash_alias_uses_its_own_authored_attack_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"targs\"\n"
        "C;Y2;X1;K\"ACbh\"\nC;Y2;X2;K\"AHbh\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"ground,enemy\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "ACbh" };

    attacker->data.UnitAbilities = &abilities; attacker->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND; target->health.value = target->health.max_value = 100;
    S_ResolveAttackHit(attacker, target, 10);
    T_FEQ(target->health.value, 83, 0.001f);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bstu")), 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_slow_aura_reads_authored_move_and_attack_factors) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"Aasl\"\nC;Y2;X2;K\"Aasl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.4\"\nC;Y2;X6;K\"0.25\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    UnitAbilities_t abilities = { .abilList = "Aasl" };

    FOR_LOOP(i,1900)alloc_test_unit(MAKEFOURCC('h','f','o','o'),1000+i*32,0);
    source->data.UnitAbilities = &abilities;
    source->s.player = 0; enemy->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    source->targtype = enemy->targtype = TARG_GROUND;
    T_FEQ(S_SlowAuraMoveReduction(enemy), 0.4f, 0.001f);
    T_FEQ(S_SlowAuraAttackReduction(enemy), 0.25f, 0.001f);
    T_FEQ(S_HumanMoveFactor(enemy), 0.6f, 0.001f);
    extern uint32_t S_TestSlowAuraVisits(bool);
    S_TestSlowAuraVisits(true);S_TestResetHeroAuraAliasResolves();
    FOR_LOOP(i,50)T_FEQ(S_SlowAuraMoveReduction(enemy),0.4f,.001f);
    T_EQ(S_TestSlowAuraVisits(true),50u);
    T_EQ(S_TestHeroAuraAliasResolves(),0u);
    FOR_LOOP(i,50) {
        edict_t *scenery=G_Spawn();scenery->class_id=MAKEFOURCC('L','T','l','t');
        scenery->svflags=SVF_STATIC_SCENERY;G_BindEntityData(scenery);
        T_FEQ(S_SlowAuraMoveReduction(enemy),.4f,.001f);
    }
    T_EQ(S_TestHeroAuraAliasResolves(),0u);
    G_SetEntityHidden(source,true);
    T_FEQ(S_SlowAuraMoveReduction(enemy),0,.001f);
    G_SetEntityHidden(source,false);
    T_FEQ(S_SlowAuraMoveReduction(enemy),.4f,.001f);
    source->s.player=enemy->s.player;
    T_FEQ(S_SlowAuraMoveReduction(enemy),0,.001f);
    source->s.player=0;
    enemy->s.origin2.x = 400;
    T_FEQ(S_SlowAuraMoveReduction(enemy), 0.0f, 0.001f);
    enemy->s.origin2.x=100;
    T_FEQ(S_SlowAuraMoveReduction(enemy),.4f,.001f);
    T_ASSERT(G_ActorRemoveSkill(source,ID_SLOW_AURA));
    T_FEQ(S_SlowAuraMoveReduction(enemy),0,.001f);
    T_ASSERT(G_ActorAddSkill(source,ID_SLOW_AURA));
    T_FEQ(S_SlowAuraMoveReduction(enemy),.4f,.001f);
    G_FreeEdict(source);
    T_FEQ(S_SlowAuraMoveReduction(enemy),0,.001f);
    level.time+=2001;
    edict_t *replacement=alloc_test_unit(MAKEFOURCC('h','f','o','o'),100,0);
    T_EQ(replacement,source);replacement->s.player=0;replacement->targtype=TARG_GROUND;
    T_FEQ(S_SlowAuraMoveReduction(enemy),0,.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_command_and_war_drums_auras_share_damage_consumer) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"ACac\"\nC;Y2;X2;K\"ACac\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"Aakb\"\nC;Y3;X2;K\"Aakb\"\nC;Y3;X3;K\"ground,friend\"\n"
        "C;Y3;X4;K\"300\"\nC;Y3;X5;K\"0.15\"\nC;Y3;X6;K\"1\"\n"
        "C;Y4;X1;K\"AOac\"\nC;Y4;X2;K\"AOac\"\nC;Y4;X3;K\"ground,friend\"\n"
        "C;Y4;X4;K\"300\"\nC;Y4;X5;K\"0.1\"\nC;Y4;X6;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    UnitAbilities_t abilities = { .abilList = "ACac,Aakb,AOac" };

    source->data.UnitAbilities = &abilities;
    source->s.player = ally->s.player = 0;
    source->targtype = ally->targtype = TARG_GROUND;
    T_FEQ(S_CommandAuraAttackBonus(ally), 0.2f, 0.001f);
    T_FEQ(S_WarDrumsAttackBonus(ally), 0.15f, 0.001f);
    ally->s.origin2.x = 400;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(ally), 0.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock creep Command Aura carries air,ground,friend,self and no `neutral`
 * token. The token describes a target that is neutral relative to the source
 * (another owner's Neutral-typed slot); the source's own slot type must not
 * turn its same-owner creeps into non-friends. */
TEST(wc3_spell, neutral_owned_aura_source_still_buffs_same_owner_creeps) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"ACac\"\nC;Y2;X2;K\"ACac\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *fellow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *sheep = alloc_test_unit(MAKEFOURCC('n','s','h','e'), 120, 0);
    edict_t *human = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 140, 0);
    UnitAbilities_t abilities = { .abilList = "ACac" };

    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_AGGRESSIVE].playerType = kPlayerTypeNeutral;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    source->data.UnitAbilities = &abilities;
    source->s.player = fellow->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    sheep->s.player = PLAYER_NEUTRAL_PASSIVE;
    human->s.player = 0;
    source->targtype = fellow->targtype = sheep->targtype = human->targtype = TARG_GROUND;

    T_FEQ(S_CommandAuraAttackBonus(fellow), 0.2f, 0.001f);
    T_FEQ(S_CommandAuraAttackBonus(source), 0.2f, 0.001f);
    /* Another owner's Neutral slot is still neutral relative to the creep, even
     * though Neutral Passive is passive-allied with every slot by default. */
    T_FEQ(S_CommandAuraAttackBonus(sheep), 0.0f, 0.001f);
    /* Neutral Hostile has no alliance with player slots. */
    T_FEQ(S_CommandAuraAttackBonus(human), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, hero_aura_aliases_honor_authored_target_masks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->armor_value = 2.0f;

    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 6.0f, 0.001f);
    target->targtype = TARG_MECHANICAL;
    level.time = AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, aura_targets_follow_source_team_alliances_and_neutral_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K4\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K1\n"
        "C;Y3;X1;K\"YHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X3;K\"ground,neutral,organic\"\n"
        "C;Y3;X4;K900\nC;Y3;X5;K7\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K1\nE\n";
    uint32_t const ally_code = MAKEFOURCC('X','H','a','d');
    uint32_t const neutral_code = MAKEFOURCC('Y','H','a','d');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *neutral_source = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 2000, 200);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *neutral = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 140, 0);
    edict_t *neutral_outside_mask = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 200);

    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[2].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[3].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    /* Original 6ac5d0: an ally is PASSIVE with SHARED_SPELLS (the force setup grants both);
     * PASSIVE alone classifies the owner as neutral. */
    G_SetPlayerAlliance(&game.clients[1].ps, &game.clients[2].ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&game.clients[1].ps, &game.clients[2].ps, ALLIANCE_SHARED_SPELLS, true);
    source->s.player = 1;
    source->heroabilities[0] = MAKE(heroability_t, .code = ally_code, .level = 1);
    neutral_source->s.player = 1;
    neutral_source->svflags |= SVF_MONSTER;
    neutral_source->heroabilities[0] = MAKE(heroability_t, .code = neutral_code, .level = 1);
    ally->s.player = 2;
    enemy->s.player = 3;
    neutral->s.player = PLAYER_NEUTRAL_PASSIVE;
    neutral->s.origin2.x = 2000;
    neutral_outside_mask->s.player = PLAYER_NEUTRAL_PASSIVE;
    ally->targtype = enemy->targtype = neutral->targtype = neutral_outside_mask->targtype = TARG_GROUND;
    ally->armor_value = enemy->armor_value = neutral->armor_value = neutral_outside_mask->armor_value = 2.0f;

    T_FEQ(S_DevotionArmorBonus(ally), 4.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(enemy), 0.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(neutral), 7.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(neutral_outside_mask), 0.0f, 0.001f);
    G_SetPlayerAlliance(&game.clients[1].ps, &game.clients[2].ps, ALLIANCE_SHARED_SPELLS, false);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(ally), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Human02 and Human03 create these recipients under Player(PLAYER_NEUTRAL_PASSIVE).
 * Keep their campaign regression tied to Devotion's stock friend/self mask. */
static void devotion_aura_does_not_target_campaign_neutral(uint32_t hero_code, uint8_t owner, uint32_t unit_code) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area3\"\nC;Y1;X5;K\"DataA3\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\n"
        "C;Y2;X3;K\"air,ground,friend,self,vuln,invu\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K3\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *devotion = make_hero(hero_code, 500, 200, 0, 0);
    edict_t *recipient = alloc_test_unit(unit_code, 100, 0);
    edict_t *friendly = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);

    ((mapInfo_t *)level.mapinfo)->players[owner].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    devotion->s.player = friendly->s.player = owner;
    devotion->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    recipient->s.player = PLAYER_NEUTRAL_PASSIVE;
    recipient->targtype = friendly->targtype = TARG_GROUND;

    T_FEQ(S_DevotionArmorBonus(friendly), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(recipient), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, human02_devotion_aura_excludes_neutral_sheep) {
    devotion_aura_does_not_target_campaign_neutral(
        MAKEFOURCC('H','u','t','h'), 9, MAKEFOURCC('n','s','h','e'));
}

TEST(wc3_spell, human03_devotion_aura_excludes_neutral_villagers) {
    devotion_aura_does_not_target_campaign_neutral(
        MAKEFOURCC('H','a','r','t'), 1, MAKEFOURCC('n','v','i','l'));
}

TEST(wc3_spell, devotion_aura_does_not_affect_static_scenery) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Area3\"\nC;Y1;X5;K\"DataA3\"\nC;Y1;X6;K\"BuffID3\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K3\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K\"Biml\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    edict_t *crate = alloc_test_unit(MAKEFOURCC('L','T','c','r'), 50, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 60, 0);

    source->s.player = footman->s.player = 0;
    crate->s.player = tree->s.player = PLAYER_NEUTRAL_PASSIVE;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    footman->targtype = TARG_GROUND;
    crate->targtype = TARG_DEBRIS; tree->targtype = TARG_TREE;
    crate->svflags |= SVF_STATIC_SCENERY; tree->svflags |= SVF_STATIC_SCENERY;
    (crate->destructable ? crate->destructable : (crate->destructable=G_AllocDestructable()));
    (tree->destructable ? tree->destructable : (tree->destructable=G_AllocDestructable()));

    T_FEQ(S_DevotionArmorBonus(footman), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(crate), 0.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(tree), 0.0f, 0.001f);

    monster_think(footman); monster_think(crate); monster_think(tree);
    T_EQ(S_DevotionAuraBuff(footman), MAKEFOURCC('B','i','m','l'));
    T_EQ(S_DevotionAuraBuff(crate), 0);
    T_EQ(S_DevotionAuraBuff(tree), 0);
    uint32_t footman_overlays = 0, scenery_overlays = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->goalentity == footman) footman_overlays++;
        if (effect->goalentity == crate || effect->goalentity == tree) scenery_overlays++;
    }
    T_EQ(footman_overlays, 1); T_EQ(scenery_overlays, 0);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, roc_rank_three_devotion_uses_shared_target_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"uberAlias\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Area3\"\nC;Y1;X6;K\"Data31\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K3\n"
        "C;Y2;X4;K\"air,ground,friend,self,vuln,invu\"\n"
        "C;Y2;X5;K900\nC;Y2;X6;K3\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    edict_t *debris = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    source->s.player = footman->s.player = debris->s.player = 0;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    footman->targtype = TARG_GROUND; debris->targtype = TARG_DEBRIS;
    T_FEQ(S_DevotionArmorBonus(footman), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(debris), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, auras_ignore_hidden_and_invisible_sources_and_recipients) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);

    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->s.renderfx |= RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->s.renderfx &= ~RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->s.renderfx |= RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->s.renderfx &= ~RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->runtime.flags |= UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->runtime.flags &= ~UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->runtime.flags |= UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->runtime.flags &= ~UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->abilities.added[0] = MAKEFOURCC('A', 's', 'h', 'm');
    ARRAY_COUNT(source->abilities.added) = 1;
    shadowMeld_t *meld = (source->shadowmeld ? source->shadowmeld : (source->shadowmeld=G_AllocShadowMeld()));
    meld->active = true;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    meld->active = false;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->abilities.added[0] = MAKEFOURCC('A', 's', 'h', 'm');
    ARRAY_COUNT(target->abilities.added) = 1;
    meld = (target->shadowmeld ? target->shadowmeld : (target->shadowmeld=G_AllocShadowMeld()));
    meld->active = true;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, devotion_aura_percent_bonus_uses_authored_base_defense) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"0.4\"\nC;Y2;X6;K\"1\"\n"
        "C;Y2;X7;K\"Biml\"\nC;Y2;X8;K\"1\"\nE\n";
    static UnitBalance_t const target_balance = { .baseArmor = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->data.UnitBalance = &target_balance;
    target->armor_value = 12.0f; /* runtime/real armor deliberately differs from authored def */

    T_FEQ(S_DevotionArmorBonus(target), 2.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 14.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, devotion_aura_recipient_presents_authored_buff_and_target_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *overlay = NULL;

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->think = monster_think;

    level.framenum = 0; G_RunEntities();
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (effect->inuse && effect->owner == target && effect->goalentity == target &&
            effect->summon_ability == MAKEFOURCC('A','H','a','d')) {
            overlay = effect;
            break;
        }
    }
    T_NOT_NULL(overlay);
    T_ASSERT(overlay->s.model != 0);

    target->s.origin2.x = 501.0f;
    level.time = AURA_UPDATE_MS;
    level.framenum++; G_RunEntities();
    T_EQ(S_DevotionAuraBuff(target), 0);
    T_NULL(overlay->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, selected_devotion_recipient_invalidates_status_panel_on_aura_transition) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K1\nE\n";
    slkTestData_t *rows, *old;
    edict_t *source;
    edict_t *target;
    edict_t *player = &g_edicts[0];

    reset_entities();
    setup_test_world();
    rows = parse_slk_string(slk);
    old = G_SetSLKRows("AbilityData", rows);
    source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    player->client = &game.clients[0];
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->think = monster_think;
    target->svflags |= SVF_MONSTER;
    target->inuse = true;
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    G_SelectEntity(&game.clients[0], target);
    game.clients[0].infopanel.entity = target->s.number;
    gi.Write = spell_panel_test_write;
    gi.unicast = spell_panel_test_unicast;

    level.time = 0;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    T_EQ(game.clients[0].infopanel.entity, UINT32_MAX);
    G_RefreshInfoPanel(player);
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    level.time = AURA_UPDATE_MS;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    target->s.origin2.x = 2000;
    level.time = AURA_UPDATE_MS * 2;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), 0);
    T_EQ(game.clients[0].infopanel.entity, UINT32_MAX);
    G_RefreshInfoPanel(player);
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* TFT AHad.BuffID is BHad. BHad.TargetArt is the soft recipient glow;
 * AHad.TargetArt is the rune that stays on the aura source. */
TEST(wc3_spell, devotion_aura_source_shows_pattern_and_recipient_keeps_glow) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"BHad\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"BHad\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    cstring_t pattern_art = G_AbilityEffectArt(MAKEFOURCC('A','H','a','d'), WC3_EFFECT_TARGET, 0);
    cstring_t glow_art = G_AbilityEffectArt(MAKEFOURCC('B','H','a','d'), WC3_EFFECT_TARGET, 0);
    int pattern = G_RegisterModel(pattern_art);
    int glow = G_RegisterModel(glow_art);
    int source_pattern, source_glow, target_pattern, target_glow;

    T_NOT_NULL(pattern_art);
    T_NOT_NULL(glow_art);
    T_ASSERT(pattern > 0 && glow > 0 && pattern != glow);
    level.time = level.framenum = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    source->think = target->think = monster_think;

    G_RunEntities();
    source_pattern = source_glow = target_pattern = target_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->goalentity != effect->owner ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == source && effect->s.model == pattern) source_pattern++;
        if (effect->owner == source && effect->s.model == glow) source_glow++;
        if (effect->owner == target && effect->s.model == pattern) target_pattern++;
        if (effect->owner == target && effect->s.model == glow) target_glow++;
    }
    T_EQ(source_pattern, 1);
    T_EQ(source_glow, 1);
    T_EQ(target_pattern, 0);
    T_EQ(target_glow, 1);

    /* A second refresh rebinds live effects and must not stack another copy. */
    level.time = AURA_UPDATE_MS;
    level.framenum++;
    G_RunEntities();
    source_pattern = source_glow = target_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->goalentity != effect->owner ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == source && effect->s.model == pattern) source_pattern++;
        if (effect->owner == source && effect->s.model == glow) source_glow++;
        if (effect->owner == target && effect->s.model == glow) target_glow++;
    }
    T_EQ(source_pattern, 1);
    T_EQ(source_glow, 1);
    T_EQ(target_glow, 1);

    target->s.origin2.x = 2000.0f;
    level.time = AURA_UPDATE_MS * 2;
    level.framenum++;
    G_RunEntities();
    target_glow = source_pattern = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == target && effect->goalentity == target) target_glow++;
        if (effect->owner == source && effect->s.model == pattern && effect->goalentity == source) source_pattern++;
    }
    T_EQ(target_glow, 0);
    T_EQ(source_pattern, 1);

    source->heroabilities[0].level = 0;
    S_MarkAuraSource(source);
    level.time = AURA_UPDATE_MS * 3;
    level.framenum++;
    G_RunEntities();
    source_pattern = source_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->owner != source || effect->goalentity != source ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->s.model == pattern) source_pattern++;
        if (effect->s.model == glow) source_glow++;
    }
    T_EQ(source_pattern, 0);
    T_EQ(source_glow, 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Count attached presentation effects by model and owner. */
static uint32_t hero_aura_art_count(edict_t *unit, uint32_t base, uint32_t model) {
    uint32_t count = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *fx = g_edicts + i;
        if (fx->inuse && fx->owner == unit && fx->goalentity == unit &&
            fx->summon_ability == base && (uint32_t)fx->s.model == model) count++;
    }
    return count;
}

TEST(wc3_spell, vampiric_aura_status_only_applies_to_melee_units) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"AUav\"\nC;Y2;X2;K\"AUav\"\nC;Y2;X3;K\"ground,air,friend,self,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"BUav\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *melee = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *ranged = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 100, 0);
    source->s.player = melee->s.player = ranged->s.player = 0;
    source->targtype = melee->targtype = ranged->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','v'), .level = 1);
    S_AttackProfileWrite(melee, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(melee, 0)->weapon = WPN_NORMAL;
    S_AttackProfileWrite(melee, 0)->range = 90.0f;
    S_AttackProfileWrite(ranged, 0)->type = ATK_PIERCE;
    S_AttackProfileWrite(ranged, 0)->weapon = WPN_INSTANT;
    S_AttackProfileWrite(ranged, 0)->range = 400.0f;

    S_UpdateHeroAuraEffects(melee);
    S_UpdateHeroAuraEffects(ranged);
    T_EQ(S_VampiricAuraBuff(melee), MAKEFOURCC('B','U','a','v'));
    T_EQ(S_VampiricAuraBuff(ranged), 0u);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

#if BZ_COMPAT_WORKAROUNDS
/* RoC omits BuffID on Cairne's AOr2 alias. Resolve recipients to BOae's
 * authored buff art so they receive the glow/icon, not AOae's caster rune. */
TEST(wc3_spell, endurance_aura_roc_compat_uses_boae_recipient_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\nC;Y1;X7;K\"TargetArt\"\n"
        "C;Y2;X1;K\"AOr2\"\nC;Y2;X2;K\"AOae\"\nC;Y2;X3;K\"ground,air,friend,self\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.3\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\n"
        "C;Y3;X1;K\"AOae\"\nC;Y3;X2;K\"AOae\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\nE\n";
    const char buff_slk[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
        "C;Y2;X1;K\"BOae\"\nC;Y2;X2;K\"BOae\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
        "C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNEnduranceAura.blp\"\n"
        "C;Y3;X1;K\"AOae\"\nC;Y3;X2;K\"AOae\"\n"
        "C;Y3;X3;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOr2" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    edict_t *source = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    uint32_t const base = MAKEFOURCC('A','O','a','e');
    uint32_t const buff = MAKEFOURCC('B','O','a','e');
    int const pattern = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));
    int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

    T_ASSERT(pattern > 0 && glow > 0 && pattern != glow);
    source->data.UnitAbilities = &abilities;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->think = target->think = monster_think;
    level.time = level.framenum = 0;
    G_RunEntities();
    T_EQ(S_EnduranceAuraBuff(target), buff);
    T_EQ(hero_aura_art_count(target, base, pattern), 0);
    T_EQ(hero_aura_art_count(target, base, glow), 1);

    G_SetSLKRows("AbilityData", old);
    G_SetSLKRows("AbilityBuffData", old_buff);
    free_slk_rows(rows);
    free_slk_rows(buff_rows);
}

/* RoC omits BuffID; the compatibility table resolves stock AHad to BHad,
 * preserving separate caster pattern and recipient glow. */
TEST(wc3_spell, devotion_aura_roc_compat_uses_bhad_recipient_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"1\"\nE\n";
    const char buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"BHad\"\nC;Y2;X2;K\"BHad\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff;
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    uint32_t base = MAKEFOURCC('A','H','a','d');
    uint32_t buff = MAKEFOURCC('B','H','a','d');
    int pattern = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));
    int glow;

    old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

    T_ASSERT(pattern > 0);
    T_ASSERT(glow > 0);
    T_NE(pattern, glow);
    level.time = level.framenum = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    source->think = target->think = monster_think;

    G_RunEntities();
    T_EQ(hero_aura_art_count(source, base, pattern), 1);
    T_EQ(hero_aura_art_count(source, base, glow), 1);
    T_EQ(hero_aura_art_count(target, base, glow), 1);
    T_EQ(S_DevotionAuraBuff(source), buff);
    T_EQ(S_DevotionAuraBuff(target), buff);

    level.time = AURA_UPDATE_MS;
    level.framenum++;
    G_RunEntities();
    T_EQ(hero_aura_art_count(source, base, pattern), 1);
    T_EQ(hero_aura_art_count(source, base, glow), 1);
    T_EQ(hero_aura_art_count(target, base, glow), 1);

    G_SetSLKRows("AbilityData", old);
    G_SetSLKRows("AbilityBuffData", old_buff);
    free_slk_rows(rows);
    free_slk_rows(buff_rows);
}
#else
/* With compatibility workarounds disabled, missing BuffID keeps the generic
 * ability-art recipient fallback. */
TEST(wc3_spell, devotion_aura_without_compat_uses_ability_art_fallback) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"1\"\nE\n";
    uint32_t const base = MAKEFOURCC('A','H','a','d');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    int const model = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));

    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = base, .level = 1);
    source->think = target->think = monster_think;
    G_RunEntities();

    T_EQ(hero_aura_art_count(source, base, model), 1);
    T_EQ(hero_aura_art_count(target, base, model), 1);
    T_EQ(S_DevotionAuraBuff(source), 0);
    T_EQ(S_DevotionAuraBuff(target), 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
#endif

/* Removing a custom alias must retire its rune while a nearby aura keeps the recipient glow alive. */
static void custom_hero_aura_removal(cstring_t base, cstring_t alias, cstring_t buff) {
    static cstring_t const format =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"%s\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"%s\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"%s\"\nC;Y3;X2;K\"%s\"\nC;Y3;X6;K\"%s\"\nC;Y3;X7;K\"1\"\nE\n";
    char slk[1024];
    uint32_t const code = FS_SLKKey(base), rawcode = FS_SLKKey(alias);
    cstring_t path = Test_TempPath("custom-hero-aura.bin");

    snprintf(slk, sizeof(slk), format, alias, base, buff, base, base, buff);
    FOR_LOOP(saved, 2) {
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
        edict_t *other = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 100, 0);
        uint32_t pattern = G_RegisterModel(G_AbilityEffectArt(rawcode, WC3_EFFECT_TARGET, 0));
        uint32_t glow = G_RegisterModel(G_AbilityEffectArt(FS_SLKKey(buff), WC3_EFFECT_TARGET, 0));

        T_ASSERT(pattern && glow && pattern != glow);
        T_NE(pattern, (uint32_t)G_RegisterModel(G_AbilityEffectArt(code, WC3_EFFECT_TARGET, 0)));
        level.time = level.framenum = 0;
        source->s.player = other->s.player = 0;
        source->targtype = other->targtype = TARG_GROUND;
        other->svflags |= SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(source, rawcode));
        T_ASSERT(G_ActorAddSkill(other, rawcode));
        source->think = other->think = monster_think;
        G_RunEntities();
        T_EQ(hero_aura_art_count(source, code, pattern), 1);
        T_EQ(hero_aura_art_count(source, code, glow), 1);

        if (saved) {
            T_ASSERT(WriteGame(path));
            G_ResetHeroPassiveCaches();
            T_ASSERT(ReadGame(path));
            remove(path);
        }
        T_ASSERT(G_ActorRemoveSkill(source, rawcode));
        FOR_LOOP(step, 3) {
            level.time += AURA_UPDATE_MS; level.framenum++;
            G_RunEntities();
            T_EQ(hero_aura_art_count(source, code, pattern), 0);
            T_EQ(hero_aura_art_count(source, code, glow), 1);
            T_EQ(hero_aura_art_count(other, code, pattern), 1);
        }
        T_ASSERT(G_ActorRemoveSkill(other, rawcode));
        level.time += AURA_UPDATE_MS; level.framenum++;
        G_RunEntities();
        T_EQ(hero_aura_art_count(source, code, glow), 0);
        T_EQ(hero_aura_art_count(other, code, pattern), 0);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

TEST(wc3_spell, devotion_aura_custom_art_is_removed_while_another_source_keeps_glow) {
    custom_hero_aura_removal("AHad", "XHfx", "BHad");
}

TEST(wc3_spell, unholy_aura_custom_art_is_removed_while_another_source_keeps_glow) {
    custom_hero_aura_removal("AUau", "XUfx", "BUau");
}

TEST(wc3_spell, unholy_aura_percent_regen_and_recipient_presentation) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.1\"\nC;Y2;X6;K\"0.01\"\n"
        "C;Y2;X7;K\"1\"\nC;Y2;X8;K\"Biml\"\nC;Y2;X9;K\"1\"\n"
        "C;Y3;X1;K\"AUau\"\nC;Y3;X2;K\"AUau\"\nC;Y3;X8;K\"Biml\"\nC;Y3;X9;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);
    edict_t *overlay = NULL;

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','U','a','u'), .level = 1);
    target->health.max_value = 1000.0f; target->health.value = 500.0f;
    target->think = monster_think;

    T_FEQ(S_UnholyMoveBonus(target), 0.1f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(target), 10.0f, 0.001f);
    level.framenum = 0; G_RunEntities();
    T_EQ(S_UnholyAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (effect->inuse && effect->owner == target && effect->goalentity == target &&
            effect->summon_ability == MAKEFOURCC('A','U','a','u')) {
            overlay = effect;
            break;
        }
    }
    T_NOT_NULL(overlay);
    T_ASSERT(overlay->s.model != 0);

    target->s.origin2.x = 501.0f;
    level.time = AURA_UPDATE_MS;
    level.framenum++; G_RunEntities();
    T_FEQ(S_UnholyMoveBonus(target), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(target), 0.0f, 0.001f);
    T_EQ(S_UnholyAuraBuff(target), 0);
    T_NULL(overlay->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, combat_aura_alias_resolution_scales_with_edicts) {
    enum { AURA_SOURCES = 32, AURA_TARGETS = 96 };
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.1\"\nC;Y2;X6;K\"0.01\"\n"
        "C;Y2;X7;K\"1\"\nC;Y2;X8;K\"Biml\"\nC;Y2;X9;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "XUau" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *sources[AURA_SOURCES], *targets[AURA_TARGETS];

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    level.time = level.framenum = 0;
    FOR_LOOP(i, AURA_SOURCES) {
        sources[i] = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
        sources[i]->data.UnitAbilities = &abilities;
        sources[i]->s.player = 0;
        sources[i]->targtype = TARG_GROUND;
    }
    FOR_LOOP(i, AURA_TARGETS) {
        targets[i] = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 0, 0);
        targets[i]->s.player = 0;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.max_value = 1000.0f;
        targets[i]->health.value = 500.0f;
    }
    /* Pathfinding maps have thousands of doodads. Their authored ability
     * strings cannot contribute an aura because scenery is never active. */
    FOR_LOOP(i, 1900) {
        edict_t *scenery = G_Spawn();
        scenery->svflags |= SVF_STATIC_SCENERY;
        scenery->data.UnitAbilities = &abilities;
        scenery->health.value = 100.f;
    }

    S_TestResetHeroAuraAliasResolves();
    FOR_LOOP(i, AURA_TARGETS)
        T_FEQ(S_UnholyHealthRegen(targets[i]), 10.0f, 0.001f);
    /* One shared provider pass should replace per-recipient scans and alias parsing. */
    T_ASSERT(S_TestHeroAuraAliasResolves() <= (AURA_SOURCES + AURA_TARGETS) * 16);

    S_TestResetHeroAuraAliasResolves();
    FOR_LOOP(frame,3) {
        level.time+=AURA_UPDATE_MS;level.framenum++;
        FOR_LOOP(i,AURA_TARGETS)T_FEQ(S_UnholyHealthRegen(targets[i]),10,.001f);
    }
    T_EQ(S_TestHeroAuraAliasResolves(),0u);
    FOR_LOOP(i,AURA_SOURCES)G_SetEntityHidden(sources[i],true);
    level.time+=AURA_UPDATE_MS;level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]),0,.001f);
    FOR_LOOP(i,AURA_SOURCES)G_SetEntityHidden(sources[i],false);
    level.time+=AURA_UPDATE_MS;level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]),10,.001f);
    T_EQ(S_TestHeroAuraAliasResolves(),0u);

    FOR_LOOP(i, AURA_SOURCES) T_ASSERT(G_ActorRemoveSkill(sources[i], MAKEFOURCC('X','U','a','u')));
    level.time += AURA_UPDATE_MS; level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]), 0.0f, 0.001f);
    T_ASSERT(G_ActorAddSkill(sources[0], MAKEFOURCC('X','U','a','u')));
    level.time += AURA_UPDATE_MS; level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]), 10.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, aura_provider_discovery_preserves_alias_precedence_and_ranks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"XHad\"\nC;Y3;X2;K\"AHad\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "XUau,XHad" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 3);
    source->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','a','s','l'), .level = 2);
    source->abilities.added[0] = MAKEFOURCC('A','C','a','c');
    source->abilities.added_count = 1;

    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).alias,
         MAKEFOURCC('X','U','a','u'));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).level, 1);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    /* A removed native alias falls back to a ranked hero ability. */
    source->abilities.removed[0] = MAKEFOURCC('X','U','a','u');
    source->abilities.removed_count = 1;
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).level, 3);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    /* Removing the added ability exposes the still-learned hero and native
     * entries; an unrelated direct list edit must not leave a stale snapshot. */
    source->abilities.added[0] = 0;
    source->abilities.added_count = 0;
    source->data.UnitAbilities = NULL;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->heroabilities[0].level = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Unrelated native/added abilities must not trigger expensive live ownership
 * checks. Positive and negative classifications are shared between passes,
 * while removals and hero rank changes are evaluated afresh. */
TEST(wc3_spell, aura_classification_cache_filters_unrelated_ownership_checks) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"XHad\"\nC;Y3;X2;K\"AHad\"\n"
        "C;Y4;X1;K\"Afoo\"\nC;Y4;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Afoo,XUau" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;
    uint32_t class_resolves;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    source->abilities.added[0] = MAKEFOURCC('X','H','a','d');
    source->abilities.added_count = 1;
    source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 3);
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraOwnershipChecks(), 2u); /* Native XUau, added XHad only. */
    class_resolves = S_TestAuraClassResolves();
    T_ASSERT(class_resolves >= 3u);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), class_resolves); /* Reused both sorts of cache hit. */
    T_EQ(S_TestAuraOwnershipChecks(), 4u);

    source->abilities.removed[0] = MAKEFOURCC('X','U','a','u');
    source->abilities.removed_count = 1;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = NULL;
    source->abilities.added[0] = 0;
    source->abilities.added_count = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->heroabilities[0].level = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Classification for the same rawcode must flip in both directions after
 * AbilityData replacement, including when an earlier result was negative. */
TEST(wc3_spell, aura_classification_cache_invalidates_on_metadata_generation) {
    const char aura_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"AUau\"\nE\n";
    const char other_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AqW1" };
    slkTestData_t *aura = parse_slk_string(aura_slk);
    slkTestData_t *other = parse_slk_string(other_slk);
    slkTestData_t *old;
    edict_t *source;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", aura);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 1u);
    G_SetSLKRows("AbilityData", other);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 2u);
    G_SetSLKRows("AbilityData", aura);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 3u);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(aura);
    free_slk_rows(other);
}

/* Hash collisions must compare full rawcodes, not just slot/generation.
 * AqW1 and Agha collide under the production 256-entry hash. */
TEST(wc3_spell, aura_classification_cache_checks_hash_collisions) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"Agha\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t aura = { .abilList = "AqW1" }, other = { .abilList = "Agha" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    S_TestResetAuraClassificationCache();
    source->data.UnitAbilities = &aura;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = &other;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = &aura;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 3u);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* An alias can itself be an aura rawcode while inheriting a different aura
 * base. The old resolver matched both; classification must preserve both. */
TEST(wc3_spell, aura_classification_cache_preserves_two_family_rawcode) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AUau\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHad" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','H','a','d')).alias,
         MAKEFOURCC('A','H','a','d'));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).alias,
         MAKEFOURCC('A','H','a','d'));
    T_EQ(S_TestAuraOwnershipChecks(), 1u);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_auras_use_alias_object_data_and_maximum_resources) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y6;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"ACnr\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"ANre\"\nC;Y3;X2;K\"Aarm\"\nC;Y3;X3;K\"ground,friend,organic\"\n"
		"C;Y3;X4;K\"500\"\nC;Y3;X5;K\"0.02\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\n"
		"C;Y4;X1;K\"Aabr\"\nC;Y4;X2;K\"Aabr\"\nC;Y4;X3;K\"ground,friend,organic\"\n"
		"C;Y4;X4;K\"500\"\nC;Y4;X5;K\"3\"\nC;Y4;X6;K\"0\"\nC;Y4;X7;K\"1\"\n"
		"C;Y5;X1;K\"Aoar\"\nC;Y5;X2;K\"Aoar\"\n"
		"C;Y6;X1;K\"Aarm\"\nC;Y6;X2;K\"Aarm\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	edict_t *health_source = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	edict_t *mana_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *blight_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);



	health_source->svflags |= SVF_MONSTER; mana_source->svflags |= SVF_MONSTER;
	blight_source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	UnitAbilities_t static_abilities = { .abilList = "ACnr" };
	health_source->s.player = mana_source->s.player = blight_source->s.player = target->s.player = 0;
	health_source->targtype = mana_source->targtype = blight_source->targtype = target->targtype = TARG_GROUND;
	health_source->data.UnitAbilities = &static_abilities;
	mana_source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','r','e'), .level = 1);
	blight_source->abilities.added[0] = MAKEFOURCC('A','a','b','r'); ARRAY_COUNT(blight_source->abilities.added) = 1;
	target->health.max_value = 1000.0f; target->health.value = 500.0f;
	target->mana.max_value = 400.0f; target->mana.value = 100.0f;

	T_FEQ(S_RegenerationHealthAura(target), 13.0f, 0.001f);
	T_FEQ(S_RegenerationManaAura(target), 8.0f, 0.001f);
	level.time = AURA_UPDATE_MS / 2;
	target->s.origin2.x = 501.0f;
	T_FEQ(S_RegenerationHealthAura(target), 13.0f, 0.001f);
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_RegenerationHealthAura(target), 0.0f, 0.001f);
	T_FEQ(S_RegenerationManaAura(target), 0.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_filters_mechanical_targets_and_uses_strongest_family_member) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"ACn1\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"ACn2\"\nC;Y3;X2;K\"Aoar\"\nC;Y3;X3;K\"ground,friend,organic\"\n"
		"C;Y3;X4;K\"500\"\nC;Y3;X5;K\"0.02\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\n"
		"C;Y4;X1;K\"Aoar\"\nC;Y4;X2;K\"Aoar\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	edict_t *first = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);



	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	first->s.player = second->s.player = target->s.player = 0;
	first->targtype = second->targtype = target->targtype = TARG_GROUND;
	first->abilities.added[0] = MAKEFOURCC('A','C','n','1'); ARRAY_COUNT(first->abilities.added) = 1;
	second->abilities.added[0] = MAKEFOURCC('A','C','n','2'); ARRAY_COUNT(second->abilities.added) = 1;
	target->health.max_value = 1000.0f; target->health.value = 500.0f;

	T_FEQ(S_RegenerationHealthAura(target), 20.0f, 0.001f);
	target->targtype = TARG_MECHANICAL;
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_RegenerationHealthAura(target), 0.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_target_art_persists_while_recipient_is_in_range) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
		"C;Y2;X1;K\"ACnr\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\n"
		"C;Y2;X7;K\"Biml\"\nC;Y2;X8;K\"1\"\n"
		"C;Y3;X1;K\"Aoar\"\nC;Y3;X2;K\"Aoar\"\nC;Y3;X7;K\"Biml\"\nC;Y3;X8;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	edict_t *source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *overlay = NULL;

	source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	source->s.player = target->s.player = 0;
	source->targtype = target->targtype = TARG_GROUND;
	source->abilities.added[0] = MAKEFOURCC('A','C','n','r');
	ARRAY_COUNT(source->abilities.added) = 1;
	target->health.max_value = 1000.0f;
	target->health.value = 900.0f;

	S_UpdateRegenerationAuraEffects(target);
	FOR_LOOP(i, globals.num_edicts) {
		edict_t *effect = g_edicts + i;
		if (effect->inuse && effect->owner == target && effect->goalentity == target &&
			effect->summon_ability == MAKEFOURCC('A','o','a','r')) {
			overlay = effect;
			break;
		}
	}
	T_NOT_NULL(overlay);
	T_ASSERT(overlay->s.model != 0);
	T_EQ(overlay->movetype, MOVETYPE_LINK);

	target->health.value = target->health.max_value;
	S_UpdateRegenerationAuraEffects(target);
	T_NULL(overlay->goalentity);

	target->health.value = 900.0f;
	S_UpdateRegenerationAuraEffects(target);
	overlay = NULL;
	FOR_LOOP(i, globals.num_edicts) {
		edict_t *effect = g_edicts + i;
		if (effect->inuse && effect->owner == target && effect->goalentity == target &&
			effect->summon_ability == MAKEFOURCC('A','o','a','r')) {
			overlay = effect;
			break;
		}
	}
	T_NOT_NULL(overlay);
	T_NOT_NULL(overlay->goalentity);

	target->s.origin2.x = 501.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateRegenerationAuraEffects(target);
	T_ASSERT(overlay->goalentity == NULL);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_base_codes_are_registered_passives) {
	static cstring_t const codes[] = { "Aoar", "Aabr", "Aarm" };
	FOR_LOOP(i, sizeof(codes) / sizeof(codes[0])) {
		ability_t const *ability = FindAbilityByClassname(codes[i]);
		T_NOT_NULL(ability);
		T_ASSERT(ability->flags & AB_PASSIVE);
	}
}

TEST(wc3_spell, thorns_aura_returns_authored_fraction_for_melee_hits) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\nC;Y1;X4;K\"DataA1\"\n"
		"C;Y2;X1;K\"AEah\"\nC;Y2;X2;K\"AEah\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"0.1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *aura = make_hero(MAKEFOURCC('E', 'd', 'r', 'u'), 500, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 100, 0);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 100, 0);
	level.time = 0;
	aura->s.player = target->s.player = attacker->s.player = 0;
	aura->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'E', 'a', 'h'), .level = 1);
	S_AttackProfileWrite(target, 0)->weapon = WPN_NORMAL;
	S_AttackProfileWrite(attacker, 0)->weapon = WPN_NORMAL;
	T_FEQ(S_ThornsDamageReturn(target, target, 100.0f), 10.0f, 0.001f);
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 10.0f, 0.001f);
	S_AttackProfileWrite(attacker, 0)->weapon = WPN_MISSILE;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	S_AttackProfileWrite(attacker, 0)->weapon = WPN_NORMAL;
	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	ability_t const *ability = FindAbilityByClassname("AEah");
	T_NOT_NULL(ability);
	T_ASSERT(ability->flags & AB_PASSIVE);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, thorns_aura_shows_recipient_status_and_buff_art) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"AEah\"\nC;Y2;X2;K\"AEah\"\nC;Y2;X3;K\"ground,air,friend,self,organic\"\n"
		"C;Y2;X4;K900\nC;Y2;X5;K0.1\nC;Y2;X6;K\"BEah\"\nC;Y2;X7;K1\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
		"C;Y2;X1;K\"BEah\"\nC;Y2;X2;K\"BEah\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNThornsAura.blp\"\nE\n";
	slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *source = make_hero(MAKEFOURCC('E','d','r','u'), 500, 200, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	uint32_t const base = MAKEFOURCC('A','E','a','h');
	uint32_t const buff = MAKEFOURCC('B','E','a','h');
	int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

	T_NOT_NULL(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));
	T_ASSERT(glow > 0);
	source->s.player = target->s.player = 0;
	source->targtype = target->targtype = TARG_GROUND;
	source->heroabilities[0] = MAKE(heroability_t, .code = base, .level = 1);
	level.time = level.framenum = 0;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_ThornsAuraBuff(target), buff);
	T_EQ(hero_aura_art_count(target, base, glow), 1);

	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_ThornsAuraBuff(target), 0u);
	T_EQ(hero_aura_art_count(target, base, glow), 0);

	G_SetSLKRows("AbilityData", old);
	G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows);
	free_slk_rows(buff_rows);
}

TEST(wc3_spell, trueshot_aura_shows_recipient_status_art_and_ranged_bonus) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"AEar\"\nC;Y2;X2;K\"AEar\"\nC;Y2;X3;K\"air,ground,friend,self,vuln,invu\"\n"
		"C;Y2;X4;K900\nC;Y2;X5;K0.1\nC;Y2;X6;K\"BEar\"\nC;Y2;X7;K1\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
		"C;Y2;X1;K\"BEar\"\nC;Y2;X2;K\"BEar\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNAEar.blp\"\nE\n";
	slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *source = make_hero(MAKEFOURCC('E','d','r','u'), 500, 200, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 100, 0);
	edict_t *melee = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 100);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	uint32_t const base = MAKEFOURCC('A','E','a','r');
	uint32_t const buff = MAKEFOURCC('B','E','a','r');
	int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

	T_ASSERT(glow > 0);
	source->s.player = target->s.player = melee->s.player = 0;
	victim->s.player = 1;
	source->targtype = target->targtype = melee->targtype = victim->targtype = TARG_GROUND;
	source->heroabilities[0] = MAKE(heroability_t, .code = base, .level = 1);
	S_AttackProfileWrite(target, 0)->type = ATK_PIERCE;
	S_AttackProfileWrite(target, 0)->weapon = WPN_INSTANT;
	S_AttackProfileWrite(target, 0)->range = 400.0f;
	S_AttackProfileWrite(melee, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(melee, 0)->weapon = WPN_NORMAL;
	S_AttackProfileWrite(melee, 0)->range = 90.0f;
	level.time = level.framenum = 0;
	S_UpdateHeroAuraEffects(target);
	S_UpdateHeroAuraEffects(melee);
	T_EQ(S_TrueshotAuraBuff(target), buff);
	T_FEQ(S_TrueshotAttackBonus(target), 0.1f, 0.001f);
	T_EQ(hero_aura_art_count(target, base, glow), 1);
	T_EQ(S_TrueshotAuraBuff(melee), 0u);
	T_FEQ(S_TrueshotAttackBonus(melee), 0.0f, 0.001f);
	/* Exercise the landed-hit damage path: a 100 damage ranged hit gains 10%.
	 * The victim has no armor, so its health delta is the resolved damage. */
	victim->armor_value = 0.0f;
	{
		float const health_before = victim->health.value;
		S_ResolveAttackHit(target, victim, 100);
		T_FEQ(health_before - victim->health.value, 110.0f, 0.001f);
	}

	S_AttackProfileWrite(target, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(target, 0)->weapon = WPN_NORMAL;
	S_AttackProfileWrite(target, 0)->range = 90.0f;
	T_FEQ(S_TrueshotAttackBonus(target), 0.0f, 0.001f);
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_TrueshotAuraBuff(target), 0u);
	{
		float const health_before = victim->health.value;
		S_ResolveAttackHit(target, victim, 100);
		T_FEQ(health_before - victim->health.value, 100.0f, 0.001f);
	}
	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_TrueshotAuraBuff(target), 0u);
	T_EQ(hero_aura_art_count(target, base, glow), 0);

	G_SetSLKRows("AbilityData", old);
	G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows);
	free_slk_rows(buff_rows);
}

TEST(wc3_spell, requested_thirty_have_concrete_handlers) {
	static cstring_t const rawcodes[] = {
		"AHab", "AHmt", "ANst", "ANsg", "ANsq", "ANsw", "AOww", "AOcr", "AHbn", "AHfs",
		"AHdr", "AHpx", "AUcb", "AUim", "AUls", "AUts", "ANba", "ANsi", "AUan", "AUdc",
		"AUdp", "AUau", "AEev", "AEme", "AUsl", "AUav", "AUin", "AOcl", "AOeq", "AOfs",
	};
	static uint32_t const passives[] = {
		MAKEFOURCC('A','H','a','b'), MAKEFOURCC('A','O','c','r'), MAKEFOURCC('A','U','t','s'),
		MAKEFOURCC('A','U','a','u'), MAKEFOURCC('A','E','e','v'), MAKEFOURCC('A','U','a','v'),
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		uint32_t code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
		bool passive = false;
		FOR_LOOP(j, sizeof(passives) / sizeof(passives[0])) passive |= code == passives[j];
		T_NOT_NULL(ability);
		if (passive) {
			T_ASSERT(ability->flags & AB_PASSIVE);
		} else {
			T_ASSERT(S_AbilityHasCommand(ability));
			T_ASSERT(ability->flags & AB_SPELL);
			T_NOT_NULL(ability->proc);
			T_EQ((int)ability_item.code, (int)code);
		}
	}
}

TEST(wc3_spell, campaign_ability_rawcodes_are_registered_explicitly) {
	static cstring_t const rawcodes[] = {
		"Aamk", "ACtn", "ANav", "ANsh", "AOw2", "ACs7", "ACs8", "ANr2", "Afbb", "Andm",
		"Asb1", "Asb2", "Asb3", "ANha", "ANen", "ACfu", "ANpa", "Acny", "Ahnl", "Arsq",
		"Arsg", "Arsp", "ANbr", "ANsb", "ANcf", "Acdh", "Acef", "ANhw", "ANhx", "Arsw",
		"AOs2", "AOr2", "AOr3", "AOls",
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		uint32_t code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
		T_NOT_NULL(ability);
		if (!strcmp(rawcodes[i], "ANha")) T_EQ(ability->proc, CAbilityHarvest);
		else {
			T_ASSERT(S_AbilityHasCommand(ability));
			T_ASSERT(ability->flags & AB_SPELL);
			T_NOT_NULL(ability->proc);
			T_EQ((int)ability_item.code, (int)code);
		}
	}
}

TEST(wc3_spell, requested_active_callback_families_change_simulation) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y6;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"AHbn\"\nC;Y2;X2;K\"AHbn\"\nC;Y2;X4;K\"12\"\nC;Y2;X5;K\"4\"\nC;Y2;X9;K\"BHbn\"\n"
		"C;Y3;X1;K\"ANsi\"\nC;Y3;X2;K\"ANsi\"\nC;Y3;X3;K\"200\"\nC;Y3;X4;K\"16\"\nC;Y3;X5;K\"8\"\nC;Y3;X9;K\"BNsi\"\n"
		"C;Y4;X1;K\"AUsl\"\nC;Y4;X2;K\"AUsl\"\nC;Y4;X4;K\"15\"\nC;Y4;X5;K\"4\"\nC;Y4;X6;K\"0.25\"\nC;Y4;X9;K\"BUsl\"\n"
		"C;Y5;X1;K\"AUdp\"\nC;Y5;X2;K\"AUdp\"\nC;Y5;X7;K\"2\"\n"
		"C;Y6;X1;K\"AOcl\"\nC;Y6;X2;K\"AOcl\"\nC;Y6;X3;K\"500\"\nC;Y6;X6;K\"100\"\nC;Y6;X7;K\"3\"\nC;Y6;X8;K\"0.5\"\nE\n";
	edict_t *caster = make_hero(MAKEFOURCC('u','d','e','a'), 200, 300, 0, 0);
	edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 700, 0);
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	caster->s.player = 0; caster->health.max_value = 500; caster->health.value = 100;
	first->s.player = second->s.player = third->s.player = 1;
	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; third->svflags |= SVF_MONSTER;
	first->die = unit_die; second->die = unit_die; third->die = unit_die;
	first->health.value = first->health.max_value = 500;
	second->health.value = second->health.max_value = 500;
	third->health.value = third->health.max_value = 500;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

	test_execute_code(caster, "AHbn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','H','b','n')));
	test_execute_code(caster, "ANsi", MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = first->s.origin2));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','N','s','i')));
	T_ASSERT(S_UnitHasStatus(second, MAKEFOURCC('B','N','s','i')));
	T_ASSERT(!S_UnitHasStatus(third, MAKEFOURCC('B','N','s','i')));

	test_execute_code(caster, "AUsl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));
	T_Damage(first, caster, 1);
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));
	T_FEQ(first->health.value, 500.0f, 0.001f);
	/* Authored initial guard blocks damage; wake is a later timer boundary. */
	level.time += 251; wc3_clock_advance(&level.pathing_clock, .251f, 0);
	S_RunAbilityTimers(); unit_updatestatuses(first);
	T_NULL(unit_findstatus(first, MAKEFOURCC('B','U','s','p')));
	T_Damage(first, caster, 1);
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));
	level.time++; wc3_clock_advance(&level.pathing_clock, .001f, 0);
	S_RunAbilityTimers();
	T_ASSERT(!S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));

	test_execute_code(caster, "AUdp", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = third));
	T_FEQ(caster->health.value, 500.0f, 0.001f);
	T_ASSERT(M_IsDead(third));

	test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	/* Banish makes the first target Ethereal, so Chain Lightning deals 166% damage. */
	T_ASSERT(unit_hasstatusstate(first, WC3_STATUS_STATE_ETHEREAL));
	T_FEQ(first->health.value, 333.0f, 0.001f);
	T_FEQ(second->health.value, 500.0f, 0.001f);
	{
		edict_t *thinker = NULL;
		FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
		T_NOT_NULL(thinker);
		level.time = thinker->freetime;
		chain_lightning_think(thinker);
		T_FEQ(second->health.value, 450.0f, 0.001f);
	}

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}


TEST(wc3_spell, militia_zero_pair_area_means_unbounded_search) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X3\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Area1\"\n"
		"C;Y2;X1;K\"Amil\"\n"
		"C;Y2;X2;K\"Amil\"\n"
		"C;Y2;X3;K\"0\"\n"
		"C;Y3;X1;K\"Amic\"\n"
		"C;Y3;X2;K\"Amic\"\n"
		"C;Y3;X3;K\"600\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);

	T_FEQ(S_MilitiaPairSearchRadius(MAKEFOURCC('A','m','i','l')), FLT_MAX, 1.0f);
	T_FEQ(S_MilitiaPairSearchRadius(MAKEFOURCC('A','m','i','c')), 600.0f, 0.01f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, spell_unit_id_from_slk) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"UnitID1\"\n"
		"C;Y2;X1;K\"AHwe\"\n"
		"C;Y2;X2;K\"AHwe\"\n"
		"C;Y2;X3;K\"hwat\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_EQ((int)S_SpellUnitId(MAKEFOURCC('A','H','w','e'), 1), (int)MAKEFOURCC('h','w','a','t'));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, simple_ability_summons_record_owner_ability_and_timed_life) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"Dur1\"\n"
        "C;Y1;X5;K\"UnitID1\"\n"
        "C;Y1;X6;K\"targs\"\n"
        "C;Y2;X1;K\"A0S1\"\n"
        "C;Y2;X2;K\"A0S1\"\n"
        "C;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"7\"\n"
        "C;Y2;X5;K\"hfoo\"\n"
        "C;Y2;X6;K\"ground\"\n"
        "E\n";
    uint32_t const code = MAKEFOURCC('A','0','S','1');
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 128.0f, 64.0f });
    uint32_t count = 0;

    S_SummonAbilityUnits(caster, code, &target);
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster && unit->class_id == MAKEFOURCC('h','f','o','o')) {
        T_EQ(unit->summon_ability, code);
        T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','T','L','F')), 1);
        count++;
    }
    T_EQ(count, 2);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* OrcX01 issues the inherited "spiritwolf" order after removing the
 * preplaced Misha. Arsg must resolve to its authored Misha data, then retire
 * that Misha when the ability is cast again. */
TEST(wc3_spell, rexxar_spiritwolf_order_uses_learned_summon_misha_alias) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"UnitID1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"Arsg\"\nC;Y2;X2;K\"AOsf\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K2\nC;Y2;X5;K\"ngzc\"\nC;Y2;X6;K1\n"
        "C;Y3;X1;K\"AOsf\"\nC;Y3;X2;K\"AOsf\"\nC;Y3;X3;K1\n"
        "C;Y3;X4;K2\nC;Y3;X5;K\"nwlg\"\nC;Y3;X6;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "", .heroAbilList = "Arsg" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *rexxar;
    uint32_t misha_count = 0, wolf_count = 0;
    edict_t *first_misha = NULL;

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    rexxar = make_hero(MAKEFOURCC('O','r','e','x'), 500.0f, 200.0f, 0.0f, 0.0f);
    rexxar->data.UnitAbilities = &abilities;
    rexxar->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','r','s','g'), .level = 1);
    rexxar->s.player = 0;

    T_EQ(G_AbilityCode(MAKEFOURCC('A','r','s','g')), MAKEFOURCC('A','O','s','f'));
    T_EQ(G_UnitAbilityLevel(rexxar, MAKEFOURCC('A','O','s','f')), 1);
    T_ASSERT(S_SpellAbilityForCode(MAKEFOURCC('A','r','s','g')) != NULL);
    T_EQ(S_SpellUnitId(MAKEFOURCC('A','r','s','g'), 1), MAKEFOURCC('n','g','z','c'));
    T_EQ(S_SpellData(MAKEFOURCC('A','r','s','g'), 1, 1), 1.0f);
    T_ASSERT(G_UnitAbilityResearchAvailable(rexxar, MAKEFOURCC('A','r','s','g')));
    T_ASSERT(G_IsUnitAbilityAvailable(rexxar, MAKEFOURCC('A','r','s','g')));
    T_ASSERT(unit_issueimmediateorder(rexxar, "spiritwolf"));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == rexxar) {
        if (unit->class_id == MAKEFOURCC('n','g','z','c')) {
            T_EQ(unit->summon_ability, MAKEFOURCC('A','r','s','g'));
            first_misha = unit;
            misha_count++;
        }
        if (unit->class_id == MAKEFOURCC('n','w','l','g')) wolf_count++;
    }
    T_EQ(misha_count, 1);
    T_EQ(wolf_count, 0);

    S_SpellResetCooldowns(rexxar);
    T_NOT_NULL(first_misha);
    AbilitySummonMisha_Execute(rexxar, (spellTarget_t){ .type = SPELL_TARGET_NONE },
        &(abilityitem_t){ .code = MAKEFOURCC('A','r','s','g'),
                          .ability = S_SpellAbilityForCode(MAKEFOURCC('A','r','s','g')) });
    T_ASSERT(first_misha && M_IsDead(first_misha));
    T_ASSERT(first_misha && first_misha->inuse);
    T_EQ(first_misha ? first_misha->class_id : 0, MAKEFOURCC('n','g','z','c'));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == rexxar && !M_IsDead(unit))
        T_ASSERT(unit->class_id != MAKEFOURCC('n','w','l','g'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, hero_duration_uses_herodur_col) {
	slkTestData_t *rows = parse_slk_string(slk_spell_data);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_FEQ(S_SpellDuration(MAKEFOURCC('A','H','t','b'), 1, true), 3.0f, 0.01f);
	T_FEQ(S_SpellDuration(MAKEFOURCC('A','H','t','b'), 1, false), 5.0f, 0.01f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, spell_is_channeling_detects_active) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	T_ASSERT(!S_SpellIsChanneling(caster));
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = MAKEFOURCC('A','H','b','z');
	T_ASSERT(S_SpellIsChanneling(caster));
	caster->channel->code = 0;
	T_ASSERT(!S_SpellIsChanneling(caster));
}

TEST(wc3_spell, identity_thinker_captures_owner_and_target_incarnations) {
    edict_t *owner = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    edict_t *thinker = S_SpellIdentityThinker(owner, MAKEFOURCC('A','l','s','h'), target);
    uint32_t owner_spawn = owner->spawn_time, target_spawn = target->spawn_time;

    T_NOT_NULL(thinker);
    T_ASSERT(S_SpellChannelOwner(thinker) == owner);
    T_ASSERT(S_SpellChannelTarget(thinker) == target);
    owner->spawn_time++;
    T_NULL(S_SpellChannelOwner(thinker));
    owner->spawn_time = owner_spawn;
    target->spawn_time++;
    T_NULL(S_SpellChannelTarget(thinker));
    target->spawn_time = target_spawn;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, channel_owner_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *thinker;
    uint32_t spawn_time;

    (caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
    caster->channel->serial = 6;
    thinker = S_SpellChannelThinker(caster, MAKEFOURCC('A','H','d','r'));
    T_ASSERT(S_SpellChannelOwner(thinker) == caster);
    spawn_time = caster->spawn_time;
    caster->spawn_time++;
    T_NULL(S_SpellChannelOwner(thinker));
    caster->spawn_time = spawn_time;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, channel_target_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    edict_t *thinker;
    uint32_t spawn_time;

    (caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
    caster->channel->serial = 7;
    thinker = S_SpellChannelTargetThinker(caster, MAKEFOURCC('A','H','d','r'), target);
    T_ASSERT(S_SpellChannelTarget(thinker) == target);
    spawn_time = target->spawn_time;
    target->spawn_time++;
    T_NULL(S_SpellChannelTarget(thinker));
    target->spawn_time = spawn_time;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, projectile_owner_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *missile = G_Spawn();
    uint32_t spawn_time = caster->spawn_time;

    missile->owner = caster;
    (missile->channel ? missile->channel : (missile->channel=G_AllocChannel()));
    missile->channel->owner_spawn_time = caster->spawn_time;
    T_ASSERT(S_SpellProjectileOwner(missile) == caster);
    caster->spawn_time++;
    T_NULL(S_SpellProjectileOwner(missile));
    caster->spawn_time = spawn_time;
    G_FreeEdict(missile);
}

TEST(wc3_spell, status_source_resolves_only_captured_incarnation) {
    edict_t *source = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    heroabilitystatus_t slot = { 0 };
    uint32_t spawn_time = source->spawn_time;

    slot.source = source;
    slot.source_spawn_time = source->spawn_time;
    T_ASSERT(S_SpellStatusSource(&slot) == source);
    source->spawn_time++;
    T_NULL(S_SpellStatusSource(&slot));
    source->spawn_time = spawn_time;
}

TEST(wc3_spell, mirror_image_immediate_order_spawns_summoned_illusion) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Cost1\"\n"
		"C;Y1;X4;K\"Cool1\"\n"
		"C;Y1;X5;K\"Dur1\"\n"
		"C;Y1;X6;K\"DataA1\"\n"
		"C;Y2;X1;K\"AOmi\"\n"
		"C;Y2;X2;K\"AOmi\"\n"
		"C;Y2;X3;K\"0\"\n"
		"C;Y2;X4;K\"0\"\n"
		"C;Y2;X5;K\"0\"\n"
		"C;Y2;X6;K\"1\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *image;
	uint32_t before;

	caster->heroabilities[0].code = MAKEFOURCC('A','O','m','i');
	caster->heroabilities[0].level = 1;
	caster->hero.level = 3;
	memset(&level.events, 0, sizeof(level.events));
	before = globals.num_edicts;

	T_ASSERT(unit_issueimmediateorder(caster, "mirrorimage"));
	T_EQ((int)globals.num_edicts, (int)before + 1);
	image = &globals.edicts[before];
	T_ASSERT(image->inuse);
	T_ASSERT(image != caster);
	T_EQ((int)image->class_id, (int)caster->class_id);
	T_ASSERT(image->aiflags & AI_ILLUSION);
	T_EQ((int)image->hero.level, (int)caster->hero.level);
	T_ASSERT(image->hero.suspend_xp);
	T_EQ((int)level.events.queue[0].type, (int)EVENT_PLAYER_UNIT_SUMMON);
	T_ASSERT(level.events.queue[0].edict == caster);
	T_ASSERT(level.events.queue[0].source == image);
	T_EQ((int)level.events.queue[1].type, (int)EVENT_UNIT_SUMMON);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_replenishes_life_then_mana_using_authored_ratios) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 50, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = 100.0f; target->health.value = 80.0f;
    target->mana.max_value = 100.0f; target->mana.value = 50.0f;
    T_NOT_NULL(item.ability);
    T_ASSERT(test_ability_message(well, A_VALIDATE, &item, &st));
    T_ASSERT(test_ability_message(well, A_EXECUTE, &item, &st));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(target->mana.value, 70.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_accepts_mana_only_replenishment) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 10, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = target->health.value = 100.0f;
    target->mana.max_value = 100.0f; target->mana.value = 50.0f;
    T_ASSERT(test_ability_message(well, A_VALIDATE, &item, &st));
    T_ASSERT(test_ability_message(well, A_EXECUTE, &item, &st));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(target->mana.value, 70.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, moon_well_autocast_uses_threshold_and_nearest_valid_target) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"Rng1\"\nC;Y1;X9;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nC;Y2;X6;K\"25\"\n"
        "C;Y2;X7;K\"100\"\nC;Y2;X8;K\"99999\"\nC;Y2;X9;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t * well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t * near = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
    edict_t * far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = near->s.player = far->s.player = 0;
    near->svflags |= SVF_MONSTER;
    far->svflags |= SVF_MONSTER;
    near->targtype = far->targtype = TARG_GROUND;
    near->health.max_value = far->health.max_value = 100.0f;
    near->health.value = far->health.value = 50.0f;
    near->mana.max_value = far->mana.max_value = 0.0f;

    T_ASSERT(item.ability && (item.ability->flags & AB_AUTOCAST));
    T_ASSERT(item.ability && (item.ability->flags & AB_UPDATE));
    T_ASSERT(S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(near->health.value, 90.0f, 0.001f); /* 80 well mana / DataB 2 = 40 life */
    T_FEQ(far->health.value, 50.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    well->mana.value = 25.0f; /* DataC is a strict minimum threshold. */
    near->health.value = 50.0f;
    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(near->health.value, 50.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_autocast_does_not_substitute_cast_range_for_area) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"Area1\"\nC;Y1;X7;K\"Rng1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y2;X3;K2\nC;Y2;X4;K0.5\nC;Y2;X5;K10\n"
        "C;Y2;X6;K0\nC;Y2;X7;K99999\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = 100.0f;
    target->health.value = 50.0f;

    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 50.0f, 0.001f);
    T_FEQ(well->mana.value, 80.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Stock ROC and TFT Ambt values from ability_audit -raw Ambt. Area=400 is the
 * autocast acquisition radius; Rng=99999 is the manual cast range. */
TEST(wc3_spell, moon_well_stock_data_uses_area_for_autocast) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"Rng1\"\nC;Y1;X9;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"air,ground,invu,vuln,friend,organic\"\n"
        "C;Y2;X4;K2\nC;Y2;X5;K0.5\nC;Y2;X6;K10\n"
        "C;Y2;X7;K400\nC;Y2;X8;K99999\nC;Y2;X9;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = 100.0f;
    target->health.value = 50.0f;
    T_EQ((int)G_AbilityLevel(item.code, 1)->area, 400);
    T_EQ((int)G_AbilityLevel(item.code, 1)->range, 99999);
    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 50.0f, 0.001f);

    target->s.origin2.x = 300.0f;
    target->s.origin.x = 300.0f;
    T_ASSERT(S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(well->mana.value, 55.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_datae_gates_only_natural_mana_regen_to_night) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t * well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    well->mana_regen_bonus = 1.0f;

    T_ASSERT(G_ActorHasSkill(well, "Ambt"));
    T_ASSERT(item.ability && item.ability->proc == CAbilityManaBattery);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_EQ(G_AbilityData(item.code)->id, item.code);
    T_EQ(G_AbilityCode(item.code), MAKEFOURCC('A','m','b','t'));
    T_FEQ(G_AbilityData(item.code)->level[0].data[4].number, 1.0f, 0.001f);
    T_ASSERT(!G_IsNight());
    T_ASSERT(S_AbilityMessage(well, A_NATURAL_MANA_REGEN_BLOCKED, &call));
    T_ASSERT(S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.1f, 0.001f); /* external +1/sec remains active */

    well->mana.value = 50.0f;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.3f, 0.001f); /* natural 2/sec + bonus 1/sec */

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_natural_mana_regen_starts_after_construction) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 0, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    balance.buildTime = 1;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    (well->construction ? well->construction : (well->construction=G_AllocConstruction()));
    well->construction->type = CONSTRUCTION_NIGHTELF;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());

    FOR_LOOP(tick, 9) G_RunEntity(well);
    T_ASSERT(well->construction);
    T_FEQ(well->mana.value, 50.0f, 0.001f);
    G_RunEntity(well);
    T_ASSERT(!well->construction);
    T_FEQ(well->mana.value, 50.2f, 0.001f);

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Registration-order updates must not invoke the expensive Moon Well
 * ownership resolver on units with unrelated native abilities. Dynamic unit
 * data must be reflected on the very next tick, without a saveable cache. */
TEST(wc3_spell, moon_well_dispatch_skips_unrelated_native_abilities) {
    UnitAbilities_t plain = { .abilList = "Aatk,Afoo" };
    UnitAbilities_t well_abilities = { .abilList = "Aatk,Ambt" };
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    unit->data.UnitAbilities = &plain;
    unit->health.value = 0.0f; /* no presentation effect spawned by the test */
    S_TestResetMoonWellUpdateCalls();
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 0);

    unit->data.UnitAbilities = &well_abilities;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellAlias(unit), MAKEFOURCC('A','m','b','t'));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    unit->data.UnitAbilities = &plain;
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);
}

/* A custom Moon Well inherits from Ambt even when neither its alias nor its
 * parent is a native ability on other units. Runtime adds/removes and native
 * removals must not return a stale cached ownership decision. */
TEST(wc3_spell, moon_well_dispatch_preserves_custom_alias_and_runtime_changes) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"Afoo\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t native = { .abilList = "Afoo,AqW1" };
    UnitAbilities_t unrelated = { .abilList = "Afoo" };
    uint32_t const custom = MAKEFOURCC('A','q','W','1');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
    edict_t *runtime;

    unit->health.value = 0.0f;
    unit->data.UnitAbilities = &native;
    T_EQ(G_AbilityCode(custom), MAKEFOURCC('A','m','b','t'));
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellAlias(unit), custom);
    S_TestResetMoonWellUpdateCalls();
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    T_ASSERT(G_ActorRemoveSkill(unit, custom));
    T_ASSERT(S_MoonWellMayUpdate(unit)); /* harmless conservative positive */
    T_EQ(S_TestMoonWellAlias(unit), 0);
    unit->data.UnitAbilities = &unrelated;
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    runtime = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
    runtime->health.value = 0.0f;
    runtime->data.UnitAbilities = &unrelated;
    T_ASSERT(G_ActorAddSkill(runtime, custom));
    T_ASSERT(S_MoonWellMayUpdate(runtime));
    T_EQ(S_TestMoonWellAlias(runtime), custom);
    S_RunAbilityUpdates(runtime);
    T_EQ(S_TestMoonWellUpdateCalls(), 2);

    T_ASSERT(G_ActorRemoveSkill(runtime, custom));
    T_ASSERT(!S_MoonWellMayUpdate(runtime));
    T_EQ(S_TestMoonWellAlias(runtime), 0);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* The expensive G_AbilityCode lookup is shared between different units
 * with the same custom or unrelated ability. The unit-list scan remains live. */
TEST(wc3_spell, moon_well_class_cache_reuses_positive_and_negative_rawcodes) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"Afoo\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t native = { .abilList = "Afoo,AqW1" };
    UnitAbilities_t unrelated = { .abilList = "Afoo" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *first, *second;

    reset_entities(); setup_test_world();
    first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);

    first->data.UnitAbilities = &native;
    second->data.UnitAbilities = &unrelated;
    S_TestResetMoonWellClassResolves();
    T_ASSERT(S_MoonWellMayUpdate(first));
    T_EQ(S_TestMoonWellClassResolves(), 2); /* Afoo and AqW1 */
    T_ASSERT(!S_MoonWellMayUpdate(second));
    T_ASSERT(S_MoonWellMayUpdate(first));
    T_EQ(S_TestMoonWellAlias(first), MAKEFOURCC('A','q','W','1'));
    T_EQ(S_TestMoonWellClassResolves(), 2); /* no repeat metadata lookups */
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Changing a custom ability's parent requires a fresh classification even
 * for rawcodes cached earlier; the generation also invalidates negatives. */
TEST(wc3_spell, moon_well_class_cache_invalidates_on_ability_data_replacement) {
    const char moon_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\nE\n";
    const char other_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    uint32_t const custom = MAKEFOURCC('A','q','W','1');
    UnitAbilities_t abilities = { .abilList = "AqW1" };
    slkTestData_t *moon = parse_slk_string(moon_slk);
    slkTestData_t *other = parse_slk_string(other_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", moon);
    slkTestData_t *replaced, *discarded_other, *discarded_moon;
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    unit->data.UnitAbilities = &abilities;
    S_TestResetMoonWellClassResolves();
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 1);
    replaced = G_SetSLKRows("AbilityData", other);
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 2);
    T_EQ(G_AbilityCode(custom), MAKEFOURCC('A','f','o','o'));
    discarded_other = G_SetSLKRows("AbilityData", replaced);
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 3);
    discarded_moon = G_SetSLKRows("AbilityData", old);
    free_slk_rows(discarded_other);
    free_slk_rows(discarded_moon);
    free_slk_rows(replaced);
    free_slk_rows(old);
    free_slk_rows(moon);
    free_slk_rows(other);
}

/* These distinct rawcodes collide in the direct-mapped hash. The key check
 * must prevent a cached non-Moon-Well alias from hiding the Moon Well. */
TEST(wc3_spell, moon_well_class_cache_handles_hash_collisions) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"AqWa\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t moon_list = { .abilList = "AqW1" };
    UnitAbilities_t other_list = { .abilList = "AqWa" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    S_TestResetMoonWellClassResolves();
    unit->data.UnitAbilities = &moon_list;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    unit->data.UnitAbilities = &other_list;
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    unit->data.UnitAbilities = &moon_list;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 3); /* same hash, different keys */
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_dispatch_keeps_construction_effect_cleanup) {
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    edict_t *effect = G_Spawn();

    well->data.UnitAbilities = &abilities;
    (well->construction ? well->construction : (well->construction=G_AllocConstruction()));
    T_NOT_NULL(well->construction);
    well->construction->paused = true;
    well->construction->type = CONSTRUCTION_NIGHTELF;
    T_NOT_NULL(effect);
    effect->owner = well;
    effect->summon_ability = MAKEFOURCC('A','m','b','t');
    effect->s.flags |= EF_NOT_SELECTABLE;
    S_TestResetMoonWellUpdateCalls();
    S_RunAbilityUpdates(well);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);
    T_ASSERT(!effect->inuse);
}

TEST(wc3_spell, non_moon_well_updates_do_not_scan_for_effect_cleanup) {
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    S_TestResetMoonWellEffectReleaseCalls();
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellEffectReleaseCalls(), 0);
}

TEST(wc3_spell, moon_well_ability_disable_still_releases_its_effect) {
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    edict_t *effect = G_Spawn();
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    T_NOT_NULL(effect);
    T_EQ(item.ability->proc, CAbilityManaBattery);
    well->data.UnitAbilities = &abilities;
    effect->owner = well;
    effect->summon_ability = item.code;
    effect->s.flags |= EF_NOT_SELECTABLE;
    S_TestResetMoonWellEffectReleaseCalls();

    T_ASSERT(S_AbilityMessage(well, A_DISABLE, &call));
    T_EQ(S_TestMoonWellEffectReleaseCalls(), 1);
    T_ASSERT(!effect->inuse);
}

TEST(wc3_spell, moon_well_missing_ability_data_does_not_restore_natural_regen) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"Afoo\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    (well->construction ? well->construction : (well->construction=G_AllocConstruction()));
    well->construction->paused = true;
    well->construction->type = CONSTRUCTION_NIGHTELF;
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    S_TestResetMoonWellMissingDataWarningCalls();
    T_ASSERT(S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    T_EQ(S_TestMoonWellMissingDataWarningCalls(), 1);
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.0f, 0.001f);

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_roc_data11_does_not_alias_datae) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);

    well->data.UnitAbilities = &abilities;
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','m','b','t'), 1)->data[0].number, 1.0f, 0.001f);
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','m','b','t'), 1)->data[4].number, 0.0f, 0.001f);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(!S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* ---- ability_t registration ---- */

TEST(wc3_spell, spell_fields_belong_to_ability) {
	abilityitem_t abil_item = S_AbilityItem(FS_SLKKey("AHtb"));
	ability_t const *abil = abil_item.ability;
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_EQ((int)abil_item.code, (int)MAKEFOURCC('A','H','t','b'));
	T_EQ((int)abil->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, forked_lightning_bounces_without_damage_decay) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Dur1\"\n"
		"C;Y2;X1;K\"ANfl\"\nC;Y2;X2;K\"ANfl\"\nC;Y2;X3;K\"85\"\n"
		"C;Y2;X4;K\"3\"\nC;Y2;X5;K\"125\"\nC;Y2;X6;K\"0.7\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	edict_t *third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300, 0);
	edict_t *fourth = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 400, 0);
	abilityitem_t spell_item = S_AbilityItem(FS_SLKKey("ANfl"));
	spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = first };

	caster->s.player = 0;
	caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','f','l'), .level = 1);
	first->s.player = second->s.player = third->s.player = fourth->s.player = 1;
	    first->svflags |= SVF_MONSTER;
	    second->svflags |= SVF_MONSTER;
	    third->svflags |= SVF_MONSTER;
	    fourth->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	first->health.value = second->health.value = third->health.value = fourth->health.value = 100;
	test_ability_message(caster, A_EXECUTE, &spell_item, &st);
	T_FEQ(first->health.value, 15.0f, 0.01f);
	T_FEQ(second->health.value, 15.0f, 0.01f);
	T_FEQ(third->health.value, 15.0f, 0.01f);
	T_FEQ(fourth->health.value, 100.0f, 0.01f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, first_new_ability_handlers_are_real_spells) {
	abilityitem_t force_item = S_AbilityItem(FS_SLKKey("AEfn"));
	ability_t const *force = force_item.ability;
	abilityitem_t starfall_item = S_AbilityItem(FS_SLKKey("AEsf"));
	ability_t const *starfall = starfall_item.ability;
	abilityitem_t shockwave_item = S_AbilityItem(FS_SLKKey("AOsh"));
	ability_t const *shockwave = shockwave_item.ability;
	abilityitem_t rain_of_fire_item = S_AbilityItem(FS_SLKKey("ANrf"));
	ability_t const *rain_of_fire = rain_of_fire_item.ability;
	abilityitem_t tranquility_item = S_AbilityItem(FS_SLKKey("AEtq"));
	ability_t const *tranquility = tranquility_item.ability;
	abilityitem_t dark_ritual_item = S_AbilityItem(FS_SLKKey("AUdr"));
	ability_t const *dark_ritual = dark_ritual_item.ability;
	abilityitem_t frost_armor_item = S_AbilityItem(FS_SLKKey("AUfa"));
	ability_t const *frost_armor = frost_armor_item.ability;
	abilityitem_t frost_armor_variant_item = S_AbilityItem(FS_SLKKey("AUfu"));
	ability_t const *frost_armor_variant = frost_armor_variant_item.ability;
	abilityitem_t divine_shield_item = S_AbilityItem(FS_SLKKey("AHds"));
	ability_t const *divine_shield = divine_shield_item.ability;
	abilityitem_t devotion_aura_item = S_AbilityItem(FS_SLKKey("AHad"));
	ability_t const *devotion_aura = devotion_aura_item.ability;
	abilityitem_t death_and_decay_item = S_AbilityItem(FS_SLKKey("AUdd"));
	ability_t const *death_and_decay = death_and_decay_item.ability;
	abilityitem_t frost_nova_item = S_AbilityItem(FS_SLKKey("AUfn"));
	ability_t const *frost_nova = frost_nova_item.ability;
	abilityitem_t thunder_clap_item = S_AbilityItem(FS_SLKKey("AHtc"));
	ability_t const *thunder_clap = thunder_clap_item.ability;

	T_NOT_NULL(force);
	T_ASSERT(force->flags & AB_SPELL);
	T_EQ((int)force_item.code, (int)MAKEFOURCC('A', 'E', 'f', 'n'));
	T_NOT_NULL(starfall);
	T_ASSERT(starfall->flags & AB_SPELL);
	T_EQ((int)starfall_item.code, (int)MAKEFOURCC('A', 'E', 's', 'f'));
	T_ASSERT(starfall->flags & AB_CHANNEL);
	T_NOT_NULL(shockwave);
	T_ASSERT(shockwave->flags & AB_SPELL);
	T_EQ((int)shockwave_item.code, (int)MAKEFOURCC('A', 'O', 's', 'h'));
	T_NOT_NULL(rain_of_fire);
	T_ASSERT(rain_of_fire->flags & AB_SPELL);
	T_EQ((int)rain_of_fire_item.code, (int)MAKEFOURCC('A', 'N', 'r', 'f'));
	T_ASSERT(rain_of_fire->flags & AB_CHANNEL);
	T_NOT_NULL(tranquility);
	T_ASSERT(tranquility->flags & AB_SPELL);
	T_EQ((int)tranquility_item.code, (int)MAKEFOURCC('A', 'E', 't', 'q'));
	T_ASSERT(tranquility->flags & AB_CHANNEL);
	T_NOT_NULL(dark_ritual);
	T_ASSERT(dark_ritual->flags & AB_SPELL);
	T_EQ((int)dark_ritual_item.code, (int)MAKEFOURCC('A', 'U', 'd', 'r'));
	T_EQ((int)dark_ritual->target_type, (int)SPELL_TARGET_UNIT);
	T_NOT_NULL(frost_armor);
	T_ASSERT(frost_armor->flags & AB_SPELL);
	T_EQ((int)frost_armor_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'a'));
	T_NOT_NULL(frost_armor_variant);
	T_ASSERT(frost_armor_variant->flags & AB_SPELL);
	T_EQ((int)frost_armor_variant_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'u'));
	T_NOT_NULL(divine_shield);
	T_ASSERT(divine_shield->flags & AB_SPELL);
	T_EQ((int)divine_shield_item.code, (int)MAKEFOURCC('A', 'H', 'd', 's'));
	T_EQ((int)divine_shield->target_type, (int)SPELL_TARGET_NONE);
	T_NOT_NULL(devotion_aura);
	T_ASSERT(devotion_aura->flags & AB_PASSIVE);
	T_ASSERT(!(devotion_aura->flags & AB_SPELL));
	T_EQ((int)devotion_aura_item.code, (int)MAKEFOURCC('A', 'H', 'a', 'd'));
	T_NOT_NULL(death_and_decay);
	T_ASSERT(death_and_decay->flags & AB_SPELL);
	T_EQ((int)death_and_decay_item.code, (int)MAKEFOURCC('A', 'U', 'd', 'd'));
	T_ASSERT(death_and_decay->flags & AB_CHANNEL);
	T_NOT_NULL(frost_nova);
	T_ASSERT(frost_nova->flags & AB_SPELL);
	T_EQ((int)frost_nova_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'n'));
	T_NOT_NULL(thunder_clap);
	T_ASSERT(thunder_clap->flags & AB_SPELL);
	T_EQ((int)thunder_clap_item.code, (int)MAKEFOURCC('A', 'H', 't', 'c'));
}

TEST(wc3_spell, carrion_swarm_damages_each_unit_for_its_rank_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X13\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"Area2\"\nC;Y1;X6;K\"Area3\"\n"
		"C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataA2\"\nC;Y1;X9;K\"DataA3\"\n"
		"C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataB2\"\nC;Y1;X12;K\"DataB3\"\n"
		"C;Y1;X13;K\"levels\"\n"
		"C;Y2;X1;K\"AUcs\"\nC;Y2;X2;K\"AUcs\"\nC;Y2;X3;K\"ground,air\"\n"
		"C;Y2;X4;K300\nC;Y2;X5;K300\nC;Y2;X6;K300\n"
		"C;Y2;X7;K75\nC;Y2;X8;K125\nC;Y2;X9;K200\n"
		"C;Y2;X10;K400\nC;Y2;X11;K700\nC;Y2;X12;K1200\nC;Y2;X13;K3\nE\n";
	slkTestData_t *rows, *old;
	edict_t *caster, *first, *second, *along_path, *cone_flank, *outside_cone;
	static int const rank_damage[] = { 75, 125, 200 };
	uint32_t rank;

	reset_entities();
	setup_test_world();
	rows = parse_slk_string(slk);
	old = G_SetSLKRows("AbilityData", rows);
	caster = make_hero(MAKEFOURCC('U','d','r','e'), 500, 200, 0, 0);
	first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 480, 0);
	second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 520, 10);
	along_path = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	cone_flank = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 100);
	outside_cone = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 250);
	caster->s.player = 0;
	first->s.player = second->s.player = along_path->s.player = cone_flank->s.player = outside_cone->s.player = 1;
	first->targtype = second->targtype = along_path->targtype = cone_flank->targtype = outside_cone->targtype = TARG_GROUND;
	first->svflags |= SVF_MONSTER;
	second->svflags |= SVF_MONSTER;
	along_path->svflags |= SVF_MONSTER;
	cone_flank->svflags |= SVF_MONSTER;
	outside_cone->svflags |= SVF_MONSTER;
	first->health.value = first->health.max_value = 1000;
	second->health.value = second->health.max_value = 1000;
	along_path->health.value = along_path->health.max_value = 1000;
	cone_flank->health.value = cone_flank->health.max_value = 1000;
	outside_cone->health.value = outside_cone->health.max_value = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

	for (rank = 1; rank <= 3; rank++) {
		caster->heroabilities[0] = MAKE(heroability_t,
			.code = MAKEFOURCC('A','U','c','s'), .level = rank);
		first->health.value = second->health.value = along_path->health.value = cone_flank->health.value =
			outside_cone->health.value = 1000;
		test_execute_code(caster, "AUcs", MAKE(spellTarget_t, .type = SPELL_TARGET_POINT,
			.point = MAKE(vec2_t, 700, 0)));
		T_FEQ(first->health.value, 1000 - rank_damage[rank - 1], 0.001f);
		T_FEQ(second->health.value, 1000 - rank_damage[rank - 1], 0.001f);
		T_FEQ(along_path->health.value, 1000 - rank_damage[rank - 1], 0.001f);
		T_FEQ(cone_flank->health.value, 1000 - rank_damage[rank - 1], 0.001f);
		T_FEQ(outside_cone->health.value, 1000.0f, 0.001f);
	}

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, bladestorm_deals_110_damage_to_nearby_enemy_ground_units) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y2;X1;K\"AOww\"\nC;Y2;X2;K\"AOww\"\n"
		"C;Y2;X3;K\"110\"\nC;Y2;X4;K\"200\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
	edict_t *ground_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *air_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
	edict_t *distant_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
	edict_t *thinker;
	uint32_t const code = MAKEFOURCC('A','O','w','w');

	caster->s.player = 0;
	caster->heroabilities[0].code = code;
	caster->heroabilities[0].level = 1;
	(caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
	caster->channel->code = code;
	caster->channel->serial = 1;

	ground_enemy->s.player = air_enemy->s.player = distant_enemy->s.player = 1;
	ally->s.player = 0;
	ground_enemy->targtype = TARG_GROUND;
	air_enemy->targtype = TARG_AIR;
	distant_enemy->targtype = TARG_GROUND;
	FOR_LOOP(i, 4) {
		edict_t *unit = i == 0 ? ground_enemy : i == 1 ? air_enemy : i == 2 ? ally : distant_enemy;
		unit->svflags |= SVF_MONSTER;
		unit->health.value = unit->health.max_value = 300;
	}
	memset(level.alliances, 0, sizeof(level.alliances));
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	thinker = S_SpellChannelThinker(caster, code);
	thinker->spawn_time = G_Time() + 5000;
	whirlwind_think(thinker);

	T_FEQ(ground_enemy->health.value, 190.0f, 0.001f);
	T_FEQ(air_enemy->health.value, 300.0f, 0.001f);
	T_FEQ(ally->health.value, 300.0f, 0.001f);
	T_FEQ(distant_enemy->health.value, 300.0f, 0.001f);

	G_FreeEdict(thinker);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, shockwave_target_mask_filters_damage_and_cap) {
    static cstring_t const codes[] = { "AOsh", "ANsh", "AOs2" };
    FOR_LOOP(edition, 2) FOR_LOOP(variant, 3) {
        char slk[2048];
        snprintf(slk, sizeof(slk),
            "ID;PWXL;N;EBB;Y2;X7\n"
            "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"targs\"\n"
            "C;X4;K\"%s\"\nC;X5;K\"%s\"\nC;X6;K\"%s\"\nC;X7;K\"Area1\"\n"
            "C;Y2;X1;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"ground,enemy\"\n"
            "C;X4;K75\nC;X5;K90\nC;X6;K600\nC;X7;K100\nE\n",
            edition ? "DataA1" : "Data11", edition ? "DataB1" : "Data12",
            edition ? "DataC1" : "Data13", codes[variant], codes[variant]);
        reset_entities(); setup_test_world();
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *caster = make_hero(MAKEFOURCC('O','t','c','h'), 500, 300, 0, 0);
        edict_t *targets[5];
        caster->s.player = 0;
        memset(level.alliances, 0, sizeof(level.alliances));
        ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
        ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
        FOR_LOOP(i, 5) {
            targets[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100 + i * 50, 0);
            targets[i]->s.player = i == 3 ? 0 : 1;
            targets[i]->targtype = i == 2 ? TARG_AIR : TARG_GROUND;
            targets[i]->svflags |= SVF_MONSTER;
            targets[i]->health.value = targets[i]->health.max_value = 300;
        }
        targets[4]->s.origin2.y = 200; /* outside the corridor */
        T_ASSERT(test_execute_code(caster, codes[variant],
            MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 600, 0 })));
        T_FEQ(targets[0]->health.value, 255.0f, 0.001f);
        T_FEQ(targets[1]->health.value, 255.0f, 0.001f);
        FOR_LOOP(i, 3) T_FEQ(targets[i + 2]->health.value, 300.0f, 0.001f);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

TEST(wc3_spell, tornado_uses_whirlwind_channel_handler) {
	abilityitem_t tornado_item = S_AbilityItem(FS_SLKKey("ANto"));
	ability_t const *tornado = tornado_item.ability;

	T_NOT_NULL(tornado);
	T_ASSERT(tornado->flags & AB_SPELL);
	T_NOT_NULL(tornado->proc);
	T_EQ((int)tornado_item.code, (int)MAKEFOURCC('A', 'N', 't', 'o'));
	T_EQ((int)tornado->target_type, (int)SPELL_TARGET_NONE);
	T_ASSERT(tornado->flags & AB_CHANNEL);
}

TEST(wc3_spell, requested_neutral_hero_abilities_have_contracts) {
	ability_t const *mana_shield = FindAbilityByClassname("ANms");
	ability_t const *revive = FindAbilityByClassname("AHre");
	ability_t const *breath = FindAbilityByClassname("ANbf");
	ability_t const *brawler = FindAbilityByClassname("ANdb");
	ability_t const *haze = FindAbilityByClassname("ANdh");
	ability_t const *doom = FindAbilityByClassname("ANdo");
	ability_t const *howl = FindAbilityByClassname("ANht");
	ability_t const *cleave = FindAbilityByClassname("ANca");

	T_NOT_NULL(mana_shield);
	T_NOT_NULL(revive);
	T_NOT_NULL(breath);
	T_NOT_NULL(brawler);
	T_NOT_NULL(haze);
	T_NOT_NULL(doom);
	T_NOT_NULL(howl);
	T_NOT_NULL(cleave);
	T_EQ(mana_shield->proc, CAbilityManaShield);
	T_ASSERT(mana_shield->flags & AB_SPELL);
	T_ASSERT(mana_shield->flags & AB_TOGGLE);
	T_EQ((int)mana_shield->target_type, (int)SPELL_TARGET_NONE);
	T_ASSERT(brawler->flags & AB_PASSIVE);
	T_ASSERT(cleave->flags & AB_PASSIVE);
	T_EQ((int)revive->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)breath->target_type, (int)SPELL_TARGET_POINT);
	T_EQ((int)haze->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)doom->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)howl->target_type, (int)SPELL_TARGET_NONE);
}

TEST(wc3_spell, selected_hero_ability_contracts_are_registered) {
	ability_t const *searing = FindAbilityByClassname("AHfa");
	ability_t const *trueshot = FindAbilityByClassname("AEar");
	ability_t const *reincarnation = FindAbilityByClassname("AOre");
	ability_t const *wave = FindAbilityByClassname("AOhw");
	ability_t const *hex = FindAbilityByClassname("AOhx");
	ability_t const *voodoo = FindAbilityByClassname("AOvd");
	ability_t const *vengeance = FindAbilityByClassname("AEsv");
	ability_t const *acid = FindAbilityByClassname("ANab");

	T_NOT_NULL(searing); T_NOT_NULL(trueshot); T_NOT_NULL(reincarnation); T_NOT_NULL(wave);
	T_NOT_NULL(hex); T_NOT_NULL(voodoo); T_NOT_NULL(vengeance); T_NOT_NULL(acid);
	T_ASSERT(searing->flags & AB_TOGGLE);
	T_ASSERT(trueshot->flags & AB_PASSIVE);
	T_ASSERT(reincarnation->flags & AB_PASSIVE);
	T_EQ((int)wave->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)hex->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)voodoo->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)vengeance->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)acid->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, human_ability_rawcodes_have_concrete_contracts) {
	static cstring_t const spells[] = {
		"Amls", "Acmg", "Amdf", "Asps", "Aclf", "Adef", "Afla", "Ainf", "Adis", "Ahea", "Aslo", "Aivs", "Aply", "AHav",
	};
	static cstring_t const passives[] = {
		"Afbk", "Aflk", "Afsh", "Aroc", "Asph", "Aphx", "Agyb", "Asth", "Agyv", "Adts",
	};

	FOR_LOOP(i, sizeof(spells) / sizeof(spells[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(spells[i]));
		ability_t const *ability = ability_item.ability;
		T_NOT_NULL(ability); T_ASSERT(S_AbilityHasCommand(ability));
		T_ASSERT(ability->flags & AB_SPELL); T_NOT_NULL(ability->proc);
		T_EQ((int)ability_item.code, (int)MAKEFOURCC(spells[i][0], spells[i][1], spells[i][2], spells[i][3]));
	}
	FOR_LOOP(i, sizeof(passives) / sizeof(passives[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(passives[i]));
		ability_t const *ability = ability_item.ability;
		T_NOT_NULL(ability); T_ASSERT(ability->flags & AB_PASSIVE);
	}
}

TEST(wc3_spell, human_support_spells_use_authored_status_and_heal_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Rng1\"\n"
		"C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ahea\"\nC;Y2;X2;K\"Ahea\"\nC;Y2;X3;K\"ground,friend\"\nC;Y2;X4;K\"250\"\nC;Y2;X7;K\"25\"\nC;Y2;X9;K\"Bhea\"\n"
		"C;Y3;X1;K\"Ainf\"\nC;Y3;X2;K\"Ainf\"\nC;Y3;X3;K\"ground,friend\"\nC;Y3;X4;K\"500\"\nC;Y3;X5;K\"60\"\nC;Y3;X6;K\"60\"\nC;Y3;X7;K\"0.1\"\nC;Y3;X8;K\"5\"\nC;Y3;X9;K\"Binf\"\n"
		"C;Y4;X1;K\"Aslo\"\nC;Y4;X2;K\"Aslo\"\nC;Y4;X3;K\"ground,enemy\"\nC;Y4;X4;K\"700\"\nC;Y4;X5;K\"60\"\nC;Y4;X6;K\"10\"\nC;Y4;X7;K\"0.6\"\nC;Y4;X8;K\"0.25\"\nC;Y4;X9;K\"Bslo\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	/* The minimal Grunt fixture omits speed; the retail Grunt owns Move. */
	enemy->unitinfo.MoveSpeed = 270;
	ally->health.value = 60; ally->health.max_value = 100; ally->armor_value = 2;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	test_execute_code(caster, "Ahea", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_FEQ(ally->health.value, 85.0f, 0.001f);
	test_execute_code(caster, "Ainf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','i','n','f'))); T_FEQ(G_UnitArmorValue(ally), 7.0f, 0.001f);
	enemy->health.value = enemy->health.max_value = 500;
	S_ResolveAttackHit(ally, enemy, 100);
	T_FEQ(enemy->health.value, 390.0f, 0.001f);
	unit_removestatus(ally, unit_findstatus(ally, MAKEFOURCC('B','i','n','f')), STATUS_REMOVE_DISPEL);
	S_ResolveAttackHit(ally, enemy, 100);
	T_FEQ(enemy->health.value, 290.0f, 0.001f);
	S_AttackProfileWrite(enemy, 0)->cooldown = 1.0f;
	S_AttackProfileWrite(enemy, 0)->damagePoint = 0.3f;
	enemy->unitinfo.MoveSpeed = 270.0f;
	(void)unit_movedistance(enemy);
	attack_melee(enemy);
	T_ASSERT(enemy->attack_damage.active);
	T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f, 0.001f);
	test_execute_code(caster, "Aslo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','s','l','o'))); T_EQ(enemy->movement.speed_factor_initialized, 1); T_FEQ(enemy->movement.speed_factor, 0.4f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock BuffIDs may be shared by custom abilities; both data schemas must
 * retain the casting alias through modifier installation and removal. */
TEST(wc3_spell, human_support_aliases_preserve_authored_modifiers) {
    FOR_LOOP(edition, 2) {
        char slk[4096];
        snprintf(slk, sizeof(slk),
            "ID;PWXL;N;EBB;Y5;X9\n"
            "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"targs\"\nC;X4;K\"Rng1\"\n"
            "C;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"%s\"\nC;X9;K\"BuffID1\"\n"
            "C;Y2;X1;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X7;K0.1\nC;X8;K2\n"
            "C;Y3;X1;K\"Aslo\"\nC;X2;K\"Aslo\"\nC;X7;K0.2\nC;X8;K0.25\n"
            "C;Y4;X1;K\"C001\"\nC;X2;K\"Ainf\"\nC;X3;K\"ground,friend\"\n"
            "C;X4;K700\nC;X5;K30\nC;X6;K30\nC;X7;K0.35\nC;X8;K9\nC;X9;K\"Binf\"\n"
            "C;Y5;X1;K\"C002\"\nC;X2;K\"Aslo\"\nC;X3;K\"ground,enemy\"\n"
            "C;X4;K700\nC;X5;K30\nC;X6;K30\nC;X7;K0.6\nC;X8;K0.45\nC;X9;K\"Bslo\"\nE\n",
            edition ? "DataA1" : "Data11", edition ? "DataB1" : "Data12");
        reset_entities(); setup_test_world();
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
        edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
        edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
        caster->s.player = ally->s.player = 0; enemy->s.player = 1;
        ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
        ally->armor_value = 2;
        enemy->health.value = enemy->health.max_value = 500;
        T_ASSERT(test_execute_code(caster, "C001", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally)));
        T_FEQ(G_UnitArmorValue(ally), 11.0f, 0.001f);
        T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT), 0.35f, 0.001f);
        S_ResolveAttackHit(ally, enemy, 100);
        T_FEQ(enemy->health.value, 365.0f, 0.001f);
        unit_removestatus(ally, unit_findstatus(ally, MAKEFOURCC('B','i','n','f')), STATUS_REMOVE_DISPEL);
        T_FEQ(G_UnitArmorValue(ally), 2.0f, 0.001f);
        S_ResolveAttackHit(ally, enemy, 100);
        T_FEQ(enemy->health.value, 265.0f, 0.001f);
        S_AttackProfileWrite(enemy, 0)->type = ATK_NORMAL;
        S_AttackProfileWrite(enemy, 0)->weapon = WPN_NORMAL;
        S_AttackProfileWrite(enemy, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
        S_AttackProfileWrite(enemy, 0)->range = 128.0f;
        enemy->stand = unit_stand;
        ally->targtype = TARG_GROUND;
        S_AttackProfileWrite(enemy, 0)->cooldown = 1.0f;
        S_AttackProfileWrite(enemy, 0)->damagePoint = 0.3f;
        enemy->unitinfo.MoveSpeed = 270.0f;
        float const normal_move_distance = unit_movedistance(enemy);
        T_ASSERT(test_execute_code(caster, "C002", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy)));
        /* Slow is owned by the native movement contribution ledger. */
        T_FEQ(S_HumanMoveFactor(enemy), 1.0f, 0.001f);
        T_FEQ(unit_movedistance(enemy), normal_move_distance * 0.4f, 0.01f);
        /* Internal Attack admission establishes the target-owned inverse;
         * direct swing helpers alone do not construct that task binding. */
        order_attack(enemy,ally);
        T_EQ(enemy->attack_target,ally);T_EQ(enemy->goalentity,ally);
        attack_melee(enemy);
        T_ASSERT(enemy->attack_damage.active);
        T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f / 0.55f, 0.001f);
        abilityPrimaryTimer_t const slowed_cooldown=enemy->attack_cooldown;
        abilityPrimaryTimer_t const slowed_damage=enemy->attack_damage;
        unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','s','l','o')), STATUS_REMOVE_DISPEL);
        attack_melee(enemy);
        /* A shorter requested cooldown does not replace the retained native
         * request, and its existing damage point is not independently rearmed. */
        T_EQ(enemy->attack_cooldown.sequence,slowed_cooldown.sequence);
        T_FEQ(enemy->attack_cooldown.deadline.time,slowed_cooldown.deadline.time,0);
        T_ASSERT(enemy->attack_damage.active);
        T_EQ(enemy->attack_damage.sequence,slowed_damage.sequence);
        T_FEQ(enemy->attack_damage.deadline.time,slowed_damage.deadline.time,0);
        T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f / 0.55f, 0.001f);
        T_ASSERT(unit_issueimmediateorder(enemy,"stop"));
        T_ASSERT(!enemy->attack_cooldown.active);T_ASSERT(!enemy->attack_damage.active);
        order_attack(enemy,ally);
        T_EQ(enemy->attack_target,ally);T_EQ(enemy->goalentity,ally);
        attack_melee(enemy);
        T_ASSERT(enemy->attack_damage.active);
        T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f, 0.001f);
        T_FEQ(unit_movedistance(enemy), normal_move_distance, 0.01f);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

TEST(wc3_spell, human_attack_passives_and_defend_change_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\nC;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Afbk\"\nC;Y2;X2;K\"Afbk\"\nC;Y2;X3;K\"20\"\nC;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"1\"\n"
		"C;Y3;X1;K\"Adef\"\nC;Y3;X2;K\"Adef\"\nC;Y3;X3;K\"0.5\"\nC;Y3;X4;K\"1\"\nC;Y3;X5;K\"0.3\"\nC;Y3;X8;K\"0\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 100, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','f','b','k'), .level = 1);
	target->mana.value = target->mana.max_value = 50; S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL;
	T_EQ(S_HumanAttackDamage(attacker, target, 10), 30); T_FEQ(target->mana.value, 30.0f, 0.001f);
	unit_addstatus(target, "Adef", 1); S_AttackProfileWrite(attacker, 0)->type = ATK_PIERCE;
	T_FEQ(S_HumanMoveFactor(target), 0.7f, 0.001f);
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 60);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_feedback_alias_reads_its_own_mana_burn_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"Afbt\"\nC;Y2;X2;K\"Afbt\"\nC;Y2;X3;K\"9\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"3\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Afbt" };
    attacker->data.UnitAbilities = &abilities; target->mana.value = target->mana.max_value = 20;
    T_EQ(S_FeedbackDamage(attacker, target, 10), 28);
    T_FEQ(target->mana.value, 11, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_hardened_skin_uses_chance_floor_and_reduction) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"Ansk\"\nC;Y2;X2;K\"Ansk\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"12\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *target = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    UnitAbilities_t abilities = { .abilList = "Ansk" };
    target->data.UnitAbilities = &abilities;
    T_EQ(S_HardenedSkinDamage(target, 40), 28);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_disease_cloud_alias_ticks_authored_area_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    source->data.UnitAbilities = &abilities; source->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND; target->health.value = target->health.max_value = 100;
    S_RunAbilityUpdates(source);
    {
        heroabilitystatus_t *slot = unit_findstatus(target, MAKEFOURCC('B','a','p','l'));
        T_NOT_NULL(slot);
        T_ASSERT(slot->buff_flags & WC3_STATUS_BUFF_PHYSICAL);
        T_ASSERT(!(slot->buff_flags & WC3_STATUS_BUFF_MAGICAL));
        T_ASSERT(!unit_status_can_magic_dispel(slot));
    }
    level.time += 1000; unit_updatestatuses(target);
    T_FEQ(target->health.value, 87, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, abomination_disease_cloud_waits_for_research_and_shows_emitter) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    edict_t *abomination = alloc_test_unit(MAKEFOURCC('u','a','b','o'), 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    uint32_t emitter_count = 0;

    abomination->data.UnitAbilities = &abilities;
    abomination->s.player = 0;
    enemy->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 100;
    S_RunAbilityUpdates(abomination);
    T_NULL(unit_findstatus(enemy, MAKEFOURCC('B','a','p','l')));
    FILTER_EDICTS(effect, effect->owner == abomination && effect->summon_ability == MAKEFOURCC('A','a','p','1'))
        emitter_count++;
    T_EQ(emitter_count, 0);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    S_RunAbilityUpdates(abomination);
    T_NOT_NULL(unit_findstatus(enemy, MAKEFOURCC('B','a','p','l')));
    emitter_count = 0;
    FILTER_EDICTS(effect, effect->owner == abomination &&
                         effect->summon_ability == MAKEFOURCC('A','a','p','1') &&
                         effect->goalentity == abomination && effect->s.model)
        emitter_count++;
    T_EQ(emitter_count, 1);
    S_RunAbilityUpdates(abomination);
    emitter_count = 0;
    FILTER_EDICTS(effect, effect->owner == abomination && effect->summon_ability == MAKEFOURCC('A','a','p','1'))
        emitter_count++;
    T_EQ(emitter_count, 1);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A distant unit must never pay for a full targeting/immunity check. Check
 * both edges of the square prefilter and the exact circular distance, while
 * retaining authored enemy targeting and an explicitly magic-immune unit. */
TEST(wc3_spell, disease_cloud_rejects_distant_edicts_before_spell_eligibility) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 4096, 4096);
    edict_t *edge = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4196, 4096);
    edict_t *diagonal_inside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4156, 4176);
    edict_t *diagonal_outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4167, 4167);
    edict_t *axis_outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4197, 4096);
    edict_t *far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 9096, 9096);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4116, 4106);
    edict_t *immune = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4136, 4146);
    edict_t *targets[] = { edge, diagonal_inside, diagonal_outside, axis_outside, far, ally, immune };
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    status_application_t immunity = {
        .buff = "Biyy", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    };
    source->data.UnitAbilities = &abilities;
    source->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    FOR_LOOP(i, sizeof(targets) / sizeof(targets[0])) {
        targets[i]->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
        targets[i]->svflags |= SVF_MONSTER;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.value = targets[i]->health.max_value = 100;
    }
    ally->s.player = 0;
    T_NOT_NULL(unit_applystatus(immune, &immunity));
    T_ASSERT(S_UnitSpellImmune(immune));
    S_TestResetDiseaseCloudTargetValidations();
    S_RunAbilityUpdates(source);
    /* Only two valid enemies, an ally and an immune unit enter the costly
     * eligibility routine. The other three lie outside the exact circle. */
    T_EQ(S_TestDiseaseCloudTargetValidations(), 4);
    T_NOT_NULL(unit_findstatus(edge, MAKEFOURCC('B','a','p','l')));
    T_NOT_NULL(unit_findstatus(diagonal_inside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(diagonal_outside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(axis_outside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(far, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(ally, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(immune, MAKEFOURCC('B','a','p','l')));
    /* Target movement must take effect on the next update; there is no
     * persistent range membership to invalidate. */
    diagonal_outside->s.origin2.x = 4146;
    diagonal_outside->s.origin2.y = 4146;
    S_TestResetDiseaseCloudTargetValidations();
    S_RunAbilityUpdates(source);
    T_EQ(S_TestDiseaseCloudTargetValidations(), 5);
    T_NOT_NULL(unit_findstatus(diagonal_outside, MAKEFOURCC('B','a','p','l')));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Two Disease Cloud emitters must own different Bapl slots and retain their
 * own tick phases; leaving the aura must not remove an existing infection. */
TEST(wc3_spell, disease_cloud_independent_sources_and_refresh) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *first = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 40, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    heroabilitystatus_t *a, *b;
    uint32_t next_a;
    second->health.value = second->health.max_value = 100;
    second->svflags |= SVF_MONSTER;
    second->stand = unit_stand;
    second->movetype = MOVETYPE_NONE;
    unit_stand(second);
    first->data.UnitAbilities = second->data.UnitAbilities = &abilities;
    first->s.player = second->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100;
    S_RunAbilityUpdates(first);
    a = unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), first);
    T_NOT_NULL(a);
    T_EQ(a->stack_policy, WC3_STATUS_STACK_INDEPENDENT);
    next_a = a->next_tick;
    S_RunAbilityUpdates(second);
    b = unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), second);
    T_NOT_NULL(b);
    T_NE(a, b);
    T_NE(a->instance_id, b->instance_id);
    T_EQ(a->next_tick, next_a);
    S_RunAbilityUpdates(first);
    T_EQ(unit_findstatussource(target, a->code, first), a);
    T_EQ(a->next_tick, next_a); /* Same-source exposure does not reset DPS. */
    unit_updatestatuses(target); /* Build the derived index before direct refresh. */
    /* Model a previously valid index that omitted this still-live status. */
    a->timestamp = G_Time() + 1500;
    CAbilityDiseaseCloud(first, A_UPDATE, NULL); /* Refresh must invalidate the timed index. */
    T_EQ(a->timestamp, G_Time() + 5000);
    unit_updatestatuses(target);
    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 74.0f, 0.001f);
    unit_removestatus(target, a, STATUS_REMOVE_DISPEL);
    T_NULL(unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), first));
    T_EQ(unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), second), b);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_orb_of_annihilation_adds_authored_attack_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"ANak\"\nC;Y2;X2;K\"ANak\"\nC;Y2;X3;K\"19\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    UnitAbilities_t abilities = { .abilList = "ANak" };
    attacker->data.UnitAbilities = &abilities;
    T_EQ(S_OrbAnnihilationDamage(attacker, 23), 42);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_data_b_and_e_scale_outgoing_and_magic_attack_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"0.75\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"0.6\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

	S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL; unit_addstatus(attacker, "Adef", 1);
	T_FEQ(S_HumanAttackDamage(attacker, target, 100), 75.0f, 0.00001f);
	G_EnsureUnitStatusSlots(attacker);
	memset(attacker->abilstatus, 0, MAX_UNIT_STATUSES * sizeof(*attacker->abilstatus));
	S_AttackProfileWrite(attacker, 0)->type = ATK_MAGIC; unit_addstatus(target, "Adef", 1);
	T_FEQ(S_HumanAttackDamage(attacker, target, 100), 60.0f, 0.00001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_toggle_reports_state_and_updates_animation_properties) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"0.3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *footman = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
	abilityitem_t item = S_AbilityItem(FS_SLKKey("Adef"));
	spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);

	T_ASSERT(!test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(test_execute_code(footman, "Adef", target));
	T_ASSERT(test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(S_UnitHasStatus(footman, MAKEFOURCC('A','d','e','f')));
	T_STREQ(G_UnitAnimationProperties(footman), "defend");
	T_FEQ(S_HumanMoveFactor(footman), 0.7f, 0.001f);

	T_ASSERT(test_execute_code(footman, "Adef", target));
	T_ASSERT(!test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(!S_UnitHasStatus(footman, MAKEFOURCC('A','d','e','f')));
	T_STREQ(G_UnitAnimationProperties(footman), "");
	T_FEQ(S_HumanMoveFactor(footman), 1.0f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Repeated requests reject before cancellation; accepted Defend/undefend
 * replace Move and pending orders, then retire their instantaneous head to0. */
TEST(wc3_spell, defend_orders_replace_busy_head_and_reject_repeated_or_disabled_requests) {
    const char slk[] =
        "ID;PWXL;N;E\nB;X4;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataC1\"\nC;X4;K\"Dur1\"\n"
        "C;Y2;X1;K\"Adef\"\nC;X2;K\"Adef\"\nC;X3;K\"0.25\"\nC;X4;K\"0\"\nE\n";
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    edict_t *unit=make_hero(MAKEFOURCC('h','f','o','o'),500,0,0,0);
    UnitAbilities_t abilities={.abilList="Adef"};unit->data.UnitAbilities=&abilities;
    vec2_t goal={512,0},queued={768,64};uint32_t code=MAKEFOURCC('A','d','e','f');
    T_EQ(G_OrderId("defend"),852055);T_EQ(G_OrderId("undefend"),852056);
    T_ASSERT(!unit_issueimmediateorder(unit,"undefend"));
    T_ASSERT(unit_issueorder(unit,"move",&goal));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&queued,true,0,0));
    T_ASSERT(unit_issueimmediateorder(unit,"defend"));
    T_EQ(unit->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(unit),0);
    T_ASSERT(!unit_is_walking(unit));T_ASSERT(S_UnitHasStatus(unit,code));T_FEQ(S_HumanMoveFactor(unit),0.75f,0);
    T_ASSERT(unit_issueorder(unit,"move",&goal));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&queued,true,0,0));
    edict_t *destination=unit->goalentity;umove_t const *move=unit->currentmove;
    uint32_t group=unit->movement.group_id;
    T_ASSERT(!unit_issueimmediateorder(unit,"defend"));T_ASSERT(S_UnitHasStatus(unit,code));
    T_EQ(unit->current_order_id,G_OrderId("move"));T_EQ(unit->goalentity,destination);
    T_EQ(unit->currentmove,move);T_EQ(unit->movement.group_id,group);T_EQ(G_UnitQueuedOrderCount(unit),1);
    T_ASSERT(WriteGame(Test_TempPath("wc3-defend119-save.bin")));
    T_ASSERT(unit_issueimmediateorder(unit,"undefend"));T_ASSERT(!S_UnitHasStatus(unit,code));
    T_EQ(unit->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(unit),0);
    T_ASSERT(ReadGame(Test_TempPath("wc3-defend119-save.bin")));
    T_ASSERT(S_UnitHasStatus(unit,code));T_EQ(unit->current_order_id,G_OrderId("move"));
    T_EQ(G_UnitQueuedOrderCount(unit),1);T_ASSERT(!unit_issueimmediateorder(unit,"defend"));
    T_ASSERT(unit_issueimmediateorder(unit,"undefend"));
    T_EQ(unit->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(unit),0);T_FEQ(S_HumanMoveFactor(unit),1,0);
    T_ASSERT(unit_issueorder(unit,"move",&goal));T_ASSERT(G_IssueUnitPointOrder(unit,"move",&queued,true,0,0));
    T_ASSERT(!unit_issueimmediateorder(unit,"undefend"));
    G_SetPlayerAbilityAvailable(&game.clients[0],code,false);
    T_ASSERT(!unit_issueimmediateorder(unit,"defend"));T_ASSERT(!S_UnitHasStatus(unit,code));
    T_EQ(unit->current_order_id,G_OrderId("move"));T_EQ(G_UnitQueuedOrderCount(unit),1);
    G_SetPlayerAbilityAvailable(&game.clients[0],code,true);
    T_ASSERT(unit_issueimmediateorder(unit,"defend"));T_EQ(unit->current_order_id,0);
    remove(Test_TempPath("wc3-defend119-save.bin"));G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

TEST(wc3_spell, defend_command_applies_primary_direction_to_controllable_selection) {
    char const slk[]="ID;PWXL;N;E\nB;X3;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataC1\"\n"
        "C;Y2;X1;K\"Adef\"\nC;X2;K\"Adef\"\nC;X3;K\"0.25\"\nE\n";
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    edict_t *player=&g_edicts[0],*units[2];
    UnitAbilities_t abilities={.abilList="Adef"};
    abilityitem_t item=S_AbilityItem(MAKEFOURCC('A','d','e','f'));
    abilityCall_t call=MAKE(abilityCall_t,.item=&item,.client=player);
    reset_entities();setup_test_world();
    player->client=&game.clients[0];player->s.player=0;
    FOR_LOOP(i,2) {
        units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,64*i);
        units[i]->health.value=units[i]->health.max_value=500;
        units[i]->svflags|=SVF_MONSTER;units[i]->stand=unit_stand;unit_stand(units[i]);
        units[i]->data.UnitAbilities=&abilities;
        G_SetEntitySelectionMask(units[i],1<<player->client->ps.number);
        T_ASSERT(unit_issueorder(units[i],"move",&(vec2_t){512,64*i}));
        T_ASSERT(G_IssueUnitPointOrder(units[i],"move",&(vec2_t){768,64*i},true,0,0));
    }
    edict_t *primary=G_GetMainSelectedUnit(player->client);
    T_ASSERT(primary==units[0] || primary==units[1]);
    T_ASSERT(S_AbilityMessage(primary,A_COMMAND,&call));
    FOR_LOOP(i,2) {
        T_ASSERT(S_UnitHasStatus(units[i],item.code));
        T_EQ(units[i]->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(units[i]),0);
    }
    edict_t *other=primary==units[0] ? units[1] : units[0];
    T_ASSERT(unit_issueimmediateorder(other,"undefend"));
    T_ASSERT(unit_issueorder(other,"move",&(vec2_t){512,0}));
    T_ASSERT(G_IssueUnitPointOrder(other,"move",&(vec2_t){768,0},true,0,0));
    T_ASSERT(S_AbilityMessage(primary,A_COMMAND,&call));
    T_ASSERT(!S_UnitHasStatus(primary,item.code));T_ASSERT(!S_UnitHasStatus(other,item.code));
    T_EQ(primary->current_order_id,0);
    T_EQ(other->current_order_id,G_OrderId("move"));T_EQ(G_UnitQueuedOrderCount(other),1);
    FOR_LOOP(i,2) G_SetEntitySelectionMask(units[i],0);
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

TEST(wc3_spell, defend_alias_removal_restores_speed_without_replacing_move_or_queue) {
    char const slk[]="ID;PWXL;N;E\nB;X3;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataC1\"\n"
        "C;Y2;X1;K\"Ad19\"\nC;X2;K\"Adef\"\nC;X3;K\"0.125\"\nE\n";
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    edict_t *unit=make_hero(MAKEFOURCC('h','f','o','o'),500,0,0,0);
    UnitAbilities_t abilities={.abilList="Ad19"};unit->data.UnitAbilities=&abilities;
    uint32_t code=MAKEFOURCC('A','d','1','9');
    T_ASSERT(unit_issueimmediateorder(unit,"defend"));T_ASSERT(S_UnitHasStatus(unit,code));
    T_FEQ(S_HumanMoveFactor(unit),0.875f,0);T_STREQ(G_UnitAnimationProperties(unit),"defend");
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){512,0}));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){768,64},true,0,0));
    T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!S_UnitHasStatus(unit,code));
    T_FEQ(S_HumanMoveFactor(unit),1,0);T_STREQ(G_UnitAnimationProperties(unit),"");
    T_EQ(unit->current_order_id,G_OrderId("move"));T_EQ(G_UnitQueuedOrderCount(unit),1);
    T_ASSERT(!unit_issueimmediateorder(unit,"defend"));
    T_EQ(unit->current_order_id,G_OrderId("move"));T_EQ(G_UnitQueuedOrderCount(unit),1);
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

TEST(wc3_spell, defend_projectile_retargets_to_unit_source_and_cannot_reflect_twice) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *missile = G_Spawn();
	float const target_hp = target->health.value, attacker_hp = attacker->health.value;

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	S_AttackProfileWrite(attacker, 0)->type = ATK_PIERCE; unit_addstatus(target, "Adef", 1);
	missile->owner = attacker; S_SetMoveGoal(missile, &missile->goalentity, target); missile->movetype = MOVETYPE_FLYMISSILE;
	missile->projectile_attack_type = ATK_PIERCE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(missile->inuse); T_ASSERT(missile->projectile_reflected); T_ASSERT(missile->goalentity == attacker);
	T_FEQ(target->health.value, target_hp, 0.001f);

	missile->s.origin = attacker->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(!missile->inuse); T_ASSERT(attacker->health.value < attacker_hp);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_rejects_recycled_projectile_owner_before_reaction) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\nC;X4;K\"DataF1\"\n"
        "C;X5;K\"DataG1\"\nC;X6;K\"DataH1\"\n"
        "C;Y2;X1;K\"Adef\"\nC;X2;K\"Adef\"\nC;X3;K0.5\nC;X4;K100\nC;X5;K0.25\nC;X6;K1\nE\n";
    reset_entities(); setup_test_world();
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
    edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *missile = G_Spawn();
    float const hp = target->health.value;
    game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
    attacker->spawn_time = 200;
    S_AttackProfileWrite(attacker, 0)->type = ATK_PIERCE;
    unit_addstatus(target, "Adef", 1);
    missile->owner = attacker; missile->goalentity = target; missile->movetype = MOVETYPE_FLYMISSILE;

    missile->projectile_attack_type = ATK_PIERCE;
    missile->projectile_owner_spawn_time = 100;
    missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
    SV_Physics_Toss(missile);
    T_ASSERT(!missile->inuse);
    T_FEQ(target->health.value, hp, 0.001f);
    game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_attack2_projectile_uses_launch_type_after_target_morph) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0.5\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	UnitWeapons_t weapons = { .attacksEnabled = 3 };
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
	edict_t *missile = G_Spawn();

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	attacker->data.UnitWeapons = &weapons;
	S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL; S_AttackProfileWrite(attacker, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
	S_AttackProfileWrite(attacker, 1)->type = ATK_PIERCE; S_AttackProfileWrite(attacker, 1)->targetsAllowed = WC3_TARGET_FLAG_AIR;
	target->targtype = TARG_AIR;
	missile->projectile_attack_type = S_AttackProfileRead(attacker, 1)->type; /* Launch-time snapshot. */
	target->targtype = TARG_GROUND; target->defense_type = 0; /* Target morphed before impact. */
	unit_addstatus(target, "Adef", 1);
	missile->owner = attacker; S_SetMoveGoal(missile, &missile->goalentity, target); missile->movetype = MOVETYPE_FLYMISSILE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);

	T_ASSERT(missile->inuse); T_ASSERT(missile->projectile_reflected); T_ASSERT(missile->goalentity == attacker);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_consumes_deflected_building_projectile_without_return_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	edict_t *tower = make_hero(MAKEFOURCC('h','t','o','w'), 500, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *missile = G_Spawn();
	float const tower_hp = tower->health.value, target_hp = target->health.value;

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	S_AttackProfileWrite(tower, 0)->type = ATK_PIERCE; unit_addstatus(target, "Adef", 1);
	missile->owner = tower; S_SetMoveGoal(missile, &missile->goalentity, target); missile->movetype = MOVETYPE_FLYMISSILE;
	missile->projectile_attack_type = ATK_PIERCE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(!missile->inuse); T_FEQ(target->health.value, target_hp, 0.001f); T_FEQ(tower->health.value, tower_hp, 0.001f);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, selected_common_ability_contracts_are_registered) {
	static cstring_t const passives[] = {
		"Abdt", "Arev", "Aawa", "Adet", "AHer", "Aalr", "Afih", "Afin", "Afio", "Afir", "Afiu", "Aloc", "Attu",
	};
	static struct { cstring_t code; abilityProc_t proc; } const commands[] = {
		{ "AEbu", CAbilityBuild }, { "AGbu", CAbilityBuild }, { "AHbu", CAbilityBuild }, { "ANbu", CAbilityBuild },
		{ "AObu", CAbilityBuild }, { "ARal", CAbilityRally }, { "AUbu", CAbilityBuild }, { "Aatk", CAbilityAttack },
		{ "Amov", CAbilityMove }, { "Atdp", CAbilityCargoDrop }, { "Atlp", CAbilityCargoLoad },
	};
	abilityitem_t poison_item = S_AbilityItem(FS_SLKKey("AEpa"));
	ability_t const *poison = poison_item.ability;

	FOR_LOOP(i, sizeof(passives) / sizeof(passives[0])) {
		ability_t const *ability = FindAbilityByClassname(passives[i]);
		T_NOT_NULL(ability);
		T_ASSERT(ability->flags & AB_PASSIVE);
	}
	FOR_LOOP(i, sizeof(commands) / sizeof(commands[0]))
		T_EQ(FindAbilityByClassname(commands[i].code)->proc, commands[i].proc);
	T_EQ(FindAbilityByClassname("Afih")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afin")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afio")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afir")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afiu")->proc, CAbilityOnFireHuman);
	T_NOT_NULL(poison);
	T_ASSERT(poison->flags & AB_SPELL);
	T_EQ((int)poison_item.code, (int)MAKEFOURCC('A', 'E', 'p', 'a'));
	T_ASSERT(poison->flags & AB_TOGGLE);
	T_ASSERT(poison->flags & AB_AUTOCAST);
}

TEST(wc3_spell, intrinsic_on_fire_level_zero_clears_effect) {
	edict_t building = {0};

	building.inuse = true;
	building.s.effect = 77;
	building.s.effect_flags = EFX_MODEL;
	building.s.flags = EF_BUILDING;
	building.health.value = building.health.max_value = 1000.0f;
	S_RefreshAbilityLevel(&building, FindAbilityByClassname("Afih"));
	T_EQ(building.s.effect, 0);
	T_EQ(building.s.effect_flags, 0);
}

TEST(wc3_spell, runtime_ability_membership_calls_enable_and_disable) {
	UnitAbilities_t abilities = { .abilList = "Afir" };
	edict_t unit = { .inuse = true, .s.effect = 77, .s.effect_flags = EFX_MODEL };
	uint32_t const code = MAKEFOURCC('A', 'f', 'i', 'r');

	unit.data.UnitAbilities = &abilities;
	T_ASSERT(G_ActorRemoveSkill(&unit, code));
	T_EQ(unit.s.effect, 0);
	T_EQ(unit.s.effect_flags, 0);
	unit.s.effect = 77; unit.s.effect_flags = EFX_MODEL;
	T_ASSERT(G_ActorAddSkill(&unit, code));
	T_EQ(unit.s.effect, 0);
	T_EQ(unit.s.effect_flags, 0);
}

TEST(wc3_spell, poison_arrows_uses_its_own_authored_bonus_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y2;X1;K\"AEpa\"\nC;Y2;X2;K\"AEpa\"\nC;Y2;X3;K\"13\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('N', 'n', 's', 'w'), 100, 100, 0, 0);

	S_AttackProfileWrite(attacker, 0)->weapon = WPN_MISSILE;
	unit_addstatus(attacker, "AEpa", 1);
	T_EQ(S_SearingArrowDamage(attacker, 20), 33);
	S_AttackProfileWrite(attacker, 0)->weapon = WPN_NORMAL;
	T_EQ(S_SearingArrowDamage(attacker, 20), 20);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, searing_arrows_adds_authored_damage_and_spends_mana_per_attack) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHfa\"\nC;Y2;X2;K\"AHfa\"\nC;Y2;X3;K\"7\"\nC;Y2;X4;K\"20\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker;
    edict_t *target;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    /* AHfa is a learned Hero ability, so it is owned through hero ranks and
     * absent from the unit's ordinary UnitAbilities list. */
    UnitAbilities_t abilities = { .abilList = "" };
    char target_number[16];
    cstring_t button[] = { "button", "AHfa" };
    cstring_t focus_target[] = { "focus", target_number };

    reset_entities(); setup_test_world();
    /* Allocate actors after installing the world fixture. */
    attacker = make_hero(MAKEFOURCC('n','n','s','w'), 100, 100, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    client->ps.number = 0;
    attacker->data.UnitAbilities = &abilities;
    attacker->heroabilities[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','H','f','a'), .level = 1);
    attacker->s.player = 0;
    attacker->mana.value = attacker->mana.max_value = 30;
    S_AttackProfileWrite(attacker, 0)->weapon = WPN_MISSILE;
    S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(attacker, 0)->targetsAllowed = G_TargetFlagForType(TARG_GROUND);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 1000;

    /* Clicking AHfa and choosing a target issues an attack order. The shot is
     * paid for at the attack damage point, and manual targeting does not turn
     * ongoing autocast on. */
    G_SelectEntity(client, attacker);
    G_SelectEntity(client, target);
    T_ASSERT(G_FocusSelectedUnit(client, attacker));
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, focus_target);
    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(attacker->goalentity == target);
    T_ASSERT(!G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    T_STREQ(S_SearingArrowsMissileArt(attacker), "Abilities\\Weapons\\SearingArrow\\Missile.mdl");
    T_FEQ(attacker->mana.value, 30.0f, 0.001f);
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 23.0f, 0.001f);
    T_FEQ(target->health.value, 880.0f, 0.001f);
    T_NULL(S_SearingArrowsMissileArt(attacker));
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 23.0f, 0.001f);
    T_FEQ(target->health.value, 780.0f, 0.001f);

    /* Right-click autocast applies the modifier on each shot while mana is
     * available; with no mana, the same target still takes a normal attack. */
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(!G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    attacker->mana.value = 7.0f;
    T_STREQ(S_SearingArrowsMissileArt(attacker), "Abilities\\Weapons\\SearingArrow\\Missile.mdl");
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 0.0f, 0.001f);
    T_FEQ(target->health.value, 660.0f, 0.001f);
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 0.0f, 0.001f);
    T_FEQ(target->health.value, 560.0f, 0.001f);
    T_NULL(S_SearingArrowsMissileArt(attacker));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, mana_shield_toggle_status_controls_authored_damage_absorption) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"BuffID1\"\nC;Y1;X6;K\"Area1\"\n"
		"C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"0.5\"\n"
		"C;Y2;X5;K\"BNms\"\nC;Y2;X6;K\"128\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 50, 0);
	edict_t *caster = make_hero(MAKEFOURCC('h', 'p', 'e', 'a'), 100, 3, 0, 0);
	caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'N', 'm', 's'), .level = 1);
	T_Damage(caster, attacker, 10);
	T_FEQ(caster->health.value, 90.0f, 0.01f); T_FEQ(caster->mana.value, 3.0f, 0.01f);
	T_ASSERT(test_execute_code(caster, "ANms", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
	T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_Damage(caster, attacker, 8);
	T_FEQ(caster->health.value, 86.0f, 0.01f); T_FEQ(caster->mana.value, 1.0f, 0.01f);
	T_Damage(caster, attacker, 10);
	T_FEQ(caster->health.value, 78.0f, 0.01f); T_FEQ(caster->mana.value, 0.0f, 0.01f);
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_Damage(caster, attacker, 10);
	T_FEQ(caster->health.value, 68.0f, 0.01f);
	T_ASSERT(test_execute_code(caster, "ANms", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	caster->mana.value = 3.0f;
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
	T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
	T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldoff"));
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, mana_shield_orders_require_ability_ownership) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\nC;Y1;X4;K\"DataA1\"\n"
		"C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\nC;Y2;X3;K\"BNms\"\nC;Y2;X4;K2\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	unit->mana.value = 10.0f;

	T_EQ(G_UnitAbilityLevel(unit, MAKEFOURCC('A','N','m','s')), 0);
	T_ASSERT(!unit_issueimmediateorder(unit, "manashieldon"));
	T_ASSERT(!S_UnitHasStatus(unit, MAKEFOURCC('B','N','m','s')));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, beastmaster_summons_use_force_of_nature_contract) {
	abilityitem_t bear_item = S_AbilityItem(FS_SLKKey("ANsg"));
	ability_t const *bear = bear_item.ability;
	abilityitem_t quilbeast_item = S_AbilityItem(FS_SLKKey("ANsq"));
	ability_t const *quilbeast = quilbeast_item.ability;
	abilityitem_t hawk_item = S_AbilityItem(FS_SLKKey("ANsw"));
	ability_t const *hawk = hawk_item.ability;

	T_EQ(bear->proc, CAbilitySummonGrizzly);
	T_EQ(quilbeast->proc, CAbilitySummonQuillbeast);
	T_EQ(hawk->proc, CAbilitySummonWarEagle);
	T_EQ((int)bear_item.code, (int)MAKEFOURCC('A', 'N', 's', 'g'));
	T_EQ((int)quilbeast_item.code, (int)MAKEFOURCC('A', 'N', 's', 'q'));
	T_EQ((int)hawk_item.code, (int)MAKEFOURCC('A', 'N', 's', 'w'));
	T_ASSERT((bear->flags & AB_SPELL) && (quilbeast->flags & AB_SPELL) && (hawk->flags & AB_SPELL));
}

static edict_t *force_nature_test_tree(float x, float y) {
	edict_t *tree = G_Spawn();
	tree->class_id = MAKEFOURCC('B','0','0','X');
	tree->s.class_id = tree->class_id;
	G_BindEntityData(tree);
	tree->s.origin = MAKE(vec3_t, x, y, 0);
	tree->s.origin2 = MAKE(vec2_t, x, y);
	tree->s.scale = 1.0f;
	tree->targtype = TARG_TREE;
	tree->health.value = tree->health.max_value = 200.0f;
	(tree->destructable ? tree->destructable : (tree->destructable=G_AllocDestructable()));
	tree->destructable->item_table = (uint32_t)-1;
	tree->destructable->placement_solid = false;
	tree->destructable->pathing_active = false;
	return tree;
}

TEST(wc3_spell, force_of_nature_consumes_required_area_trees_for_treants) {
	static const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X12\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataA2\"\nC;Y1;X5;K\"DataA3\"\n"
		"C;Y1;X6;K\"Area1\"\nC;Y1;X7;K\"Area2\"\nC;Y1;X8;K\"Area3\"\n"
		"C;Y1;X9;K\"Rng1\"\nC;Y1;X10;K\"Dur1\"\n"
		"C;Y1;X11;K\"Dur2\"\nC;Y1;X12;K\"Dur3\"\n"
		"C;Y2;X1;K\"AEfn\"\nC;Y2;X2;K\"AEfn\"\n"
		"C;Y2;X3;K2\nC;Y2;X4;K3\nC;Y2;X5;K4\n"
		"C;Y2;X6;K150\nC;Y2;X7;K225\nC;Y2;X8;K300\n"
		"C;Y2;X9;K800\nC;Y2;X10;K60\nC;Y2;X11;K60\nC;Y2;X12;K60\nE\n";
	static const char unit_ui_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\n"
		"C;Y1;X3;K\"modelScale\"\nC;Y1;X4;K\"scale\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X3;K1\nC;Y2;X4;K1\nE\n";
	static const char unit_data_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"targType\"\nC;Y1;X3;K\"movetp\"\n"
		"C;Y1;X4;K\"collision\"\nC;Y1;X5;K\"propWin\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K\"ground\"\nC;Y2;X3;K\"foot\"\n"
		"C;Y2;X4;K16\nC;Y2;X5;K60\nE\n";
	static const char unit_balance_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\n"
		"C;Y1;X3;K\"realM\"\nC;Y1;X4;K\"mana0\"\nC;Y1;X5;K\"spd\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K300\nC;Y2;X3;K0\n"
		"C;Y2;X4;K0\nC;Y2;X5;K270\nE\n";
	slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui;
	slkTestData_t *data_rows, *old_data, *balance_rows, *old_balance;
	edict_t *caster;
	abilityitem_t spell;
	abilityCall_t target_mode;
	static uint32_t const counts[] = { 2, 3, 4 };
	static float const areas[] = { 150.0f, 225.0f, 300.0f };
	uint32_t rank;

	reset_entities();
	setup_test_world();
	ability_rows = parse_slk_string(ability_slk); old_ability = G_SetSLKRows("AbilityData", ability_rows);
	ui_rows = parse_slk_string(unit_ui_slk); old_ui = G_SetSLKRows("UnitUI", ui_rows);
	data_rows = parse_slk_string(unit_data_slk); old_data = G_SetSLKRows("UnitData", data_rows);
	balance_rows = parse_slk_string(unit_balance_slk); old_balance = G_SetSLKRows("UnitBalance", balance_rows);
	caster = alloc_test_unit(MAKEFOURCC('E','k','g','r'), 0, 0);
	caster->health.value = caster->health.max_value = 500.0f;
	caster->mana.value = caster->mana.max_value = 500.0f;
	caster->svflags |= SVF_MONSTER;
	caster->stand = unit_stand;
	caster->s.player = 0;
	spell = S_AbilityItem(FS_SLKKey("AEfn"));
	T_EQ(spell.ability->target_type, SPELL_TARGET_POINT);
	target_mode = MAKE(abilityCall_t, .item = &spell);
	spell.ability->proc(caster, A_SPELL_TARGET_MODE, &target_mode);
	T_ASSERT(target_mode.spell_entity_target_mode);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;

	for (rank = 1; rank <= 3; rank++) {
		float const center_x = (float)rank * 2000.0f;
		vec2_t const center = { center_x, 0.0f };
		spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = center);
		abilityCall_t call = MAKE(abilityCall_t, .item = &spell, .target = &target);
		spellTarget_t tree_target;
		abilityCall_t tree_call;
		edict_t *trees[4] = { NULL, NULL, NULL, NULL };
		edict_t *outside;
		uint32_t i, summon_count = 0;
		float const spacing = rank == 1 ? 30.0f : 36.0f;
		caster->heroabilities[0] = MAKE(heroability_t,
			.code = MAKEFOURCC('A','E','f','n'), .level = rank);

		for (i = 0; i + 1 < counts[rank - 1]; i++)
			trees[i] = force_nature_test_tree(center_x + ((float)i - 1.0f) * spacing, 0.0f);
		T_ASSERT(!S_AbilityMessage(caster, A_VALIDATE, &call));
		trees[counts[rank - 1] - 1] = force_nature_test_tree(center_x + areas[rank - 1] * 0.5f, 0.0f);
		outside = force_nature_test_tree(center_x + areas[rank - 1] + 32.0f, 0.0f);
#if !BZ_COMPAT_WORKAROUNDS
        /* Without the omitted RoC UnitID fallback, no tree may be consumed. */
        T_ASSERT(!S_AbilityMessage(caster, A_VALIDATE, &call));
        S_AbilityMessage(caster, A_EXECUTE, &call);
        for (i = 0; i < counts[rank - 1]; i++) T_ASSERT(!trees[i]->destructable->dead);
        continue;
#endif
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &call));
		tree_target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = trees[0]);
		tree_call = MAKE(abilityCall_t, .item = &spell, .target = &tree_target);
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &tree_call));
		tree_target.point = center;
		tree_target.type = SPELL_TARGET_POINT;
		tree_call.target = &tree_target;
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &tree_call));
		S_AbilityMessage(caster, A_EXECUTE, &call);

		for (i = 0; i < counts[rank - 1]; i++)
			T_ASSERT(trees[i]->destructable->dead);
		T_ASSERT(!outside->destructable->dead);
		FILTER_EDICTS(summon, summon->summon_ability == spell.code && summon->class_id == MAKEFOURCC('e','f','o','n') &&
			Vector2_distance(&summon->s.origin2, &center) <= areas[rank - 1] + 64.0f) {
			bool near_consumed_tree = false;
			for (i = 0; i < counts[rank - 1]; i++)
				if (Vector2_distance(&summon->s.origin2, &trees[i]->s.origin2) <= 128.0f) near_consumed_tree = true;
			T_ASSERT(near_consumed_tree);
			summon_count++;
		}
		T_EQ(summon_count, counts[rank - 1]);
	}

	reset_entities();
	G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance_rows);
	G_SetSLKRows("UnitData", old_data); free_slk_rows(data_rows);
	G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui_rows);
	G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, entangling_roots_is_a_timed_unit_spell) {
	abilityitem_t roots_item = S_AbilityItem(FS_SLKKey("AEer"));
	ability_t const *roots = roots_item.ability;

	T_NOT_NULL(roots);
	T_ASSERT(roots->flags & AB_SPELL);
	T_EQ((int)roots_item.code, (int)MAKEFOURCC('A', 'E', 'e', 'r'));
	T_EQ((int)roots->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, frost_nova_spawns_target_effect_on_primary_victim) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"AUfn\"\nC;Y2;X2;K\"AUfn\"\nC;Y2;X3;K\"10\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"300\"\nC;Y2;X6;K\"Bfro\"\nC;Y2;X7;K\"5\"\nE\n";
    static char const buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"Bfro\"\nC;Y2;X2;K\"Bfro\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    slkTestData_t *ability_rows = parse_slk_string(ability_slk), *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','n','e','c'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 72, 0);
    uint32_t target_effects = 0;

    caster->s.player = 0;
    target->s.player = nearby->s.player = 1;
    target->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
    target->health.value = target->health.max_value = 100;
    nearby->health.value = nearby->health.max_value = 100;
    nearby->s.origin2.x = 100;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(test_execute_code(caster, "AUfn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_FEQ(target->health.value, 70.0f, 0.01f);
    T_FEQ(nearby->health.value, 90.0f, 0.01f);
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','f','r','o') && effect->goalentity) {
        T_ASSERT(effect->s.model > 0);
        target_effects++;
    }
    T_EQ(target_effects, 2);
    T_ASSERT(unit_findstatus(target, MAKEFOURCC('B','f','r','o'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    T_ASSERT(unit_findstatus(nearby, MAKEFOURCC('B','f','r','o'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    T_EQ(G_UnitStatusLevel(target, MAKEFOURCC('B','f','r','o')), 1);
    T_EQ(G_UnitStatusLevel(nearby, MAKEFOURCC('B','f','r','o')), 1);

    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
    G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
}

TEST(wc3_spell, frost_armor_enables_buff_target_art) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"AUfa\"\nC;Y2;X2;K\"AUfa\"\nC;Y2;X3;K\"BUfa\"\n"
        "C;Y2;X4;K\"45\"\nC;Y2;X5;K\"3\"\nE\n";
    slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','n','e','c'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *status;

    caster->s.player = 0; target->s.player = 0;
    T_ASSERT(test_execute_code(caster, "AUfa", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    status = unit_findstatus(target, MAKEFOURCC('B','U','f','a'));
    T_NOT_NULL(status);
    if (status) T_ASSERT(status->buff_flags & WC3_STATUS_BUFF_TARGET_ART);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, entangling_roots_visual_resolves_from_buff_target_art) {
	cstring_t art = G_AbilityEffectArt(MAKEFOURCC('B', 'E', 'e', 'r'), WC3_EFFECT_TARGET, 0);

	T_STREQ(art, "Abilities\\Spells\\NightElf\\EntanglingRoots\\EntanglingRootsTarget.mdl");
}

TEST(wc3_spell, entangling_roots_visual_follows_status_through_recast_and_save_load) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"8\"\nC;Y2;X5;K\"8\"\nC;Y2;X6;K\"4\"\nE\n";
    static char const buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"BEer\"\nC;Y2;X2;K\"BEer\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    cstring_t const save_path = Test_TempPath("roots-visual-save.bin");
    slkTestData_t *ability_rows = parse_slk_string(ability_slk), *old_ability;
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff;
    edict_t *caster, *target, *effect = NULL;
    uint32_t target_number;

    reset_entities(); setup_test_world(); level.time = 1000;
    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    caster->s.player = 0; target->s.player = 1;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        T_NULL(effect);
        effect = found;
    }
    T_NOT_NULL(effect);
    T_ASSERT(unit_findstatus(target, MAKEFOURCC('B','E','e','r'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    target_number = target->s.number;

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    effect = NULL;
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        effect = found;
        break;
    }
    T_NOT_NULL(effect);
    {
        uint32_t count = 0;
        FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) count++;
        T_EQ(count, 1);
    }

    T_ASSERT(WriteGame(save_path));
    T_ASSERT(ReadGame(save_path));
    target = g_edicts + target_number;
    caster = g_edicts + caster->s.number;
    {
        uint32_t count = 0;
        FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
            effect = found;
            count++;
        }
        T_EQ(count, 1);
    }
    {
        edict_t *second_source = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
        status_application_t independent = {
            .buff = "BEer", .level = 1, .duration = 8.0f,
            .stack_policy = WC3_STATUS_STACK_INDEPENDENT, .source = second_source,
            .buff_flags = WC3_STATUS_BUFF_TARGET_ART
        };
        heroabilitystatus_t *second = unit_applystatus(target, &independent);
        T_NOT_NULL(second);
        unit_removestatus(target, unit_findstatus(target, MAKEFOURCC('B','E','e','r')), STATUS_REMOVE_SCRIPT);
        T_EQ(effect->status_effect_code, MAKEFOURCC('B','E','e','r'));
        unit_removestatus(target, second, STATUS_REMOVE_SCRIPT);
    }
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(effect->status_effect_code, 0);

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    effect = NULL;
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        T_NULL(effect);
        effect = found;
    }
    T_NOT_NULL(effect);
    unit_statusdeath(target);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(effect->status_effect_code, 0);

    remove(save_path);
    G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, entangling_roots_tracks_source_interrupts_channel_and_ticks_authored_dps) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"targs\"\nC;Y1;X8;K\"Rng1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"3\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"12\"\n"
        "C;Y2;X7;K\"ground,enemy,neutral,organic\"\nC;Y2;X8;K\"600\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000;
    caster->s.player = 0; target->s.player = 1;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    (target->channel ? target->channel : (target->channel=G_AllocChannel()));
    target->channel->code = MAKEFOURCC('A','H','m','t');

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    T_EQ(slot->data, MAKEFOURCC('A','E','e','r'));
    T_EQ(slot->rank, 1);
    T_EQ(slot->source, caster);
    T_EQ(slot->source_spawn_time, caster->spawn_time);
    T_ASSERT(!target->channel || target->channel->code == 0);
    T_ASSERT(!S_UnitCanTranslate(target));

    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 88.0f, 0.001f);

    unit_expirestatus(target, slot);
    T_ASSERT(S_UnitCanTranslate(target));
    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 88.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, entangling_roots_death_cleanup_and_failed_status_do_not_interrupt) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"3\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"12\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000; caster->s.player = 0; target->s.player = 1;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    unit_statusdeath(target);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));

    G_EnsureUnitStatusSlots(target);
    memset(target->abilstatus, 0, MAX_UNIT_STATUSES * sizeof(*target->abilstatus));
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        target->abilstatus[i].code = MAKEFOURCC('X','0' + (i / 10), '0' + (i % 10), 'x');
        target->abilstatus[i].level = 1;
        target->abilstatus[i].timestamp = G_Time() + 60000;
    }
    (target->channel ? target->channel : (target->channel=G_AllocChannel()));
    target->channel->code = MAKEFOURCC('A','H','m','t');
    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(target->channel->code, MAKEFOURCC('A','H','m','t'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, entangling_roots_alias_uses_own_data_and_hero_duration) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"Aenr\"\nC;Y2;X2;K\"Aenr\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"7\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"19\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 64, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000; caster->s.player = 0; target->s.player = 1;

    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    T_ASSERT(test_execute_code(caster, "Aenr", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    T_EQ(slot->data, MAKEFOURCC('A','e','n','r'));
    T_EQ(slot->rank, 1);
    T_EQ(slot->duration_ms, 2000);
    level.time += 1000; unit_updatestatuses(target);
    T_FEQ(target->health.value, 81.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, death_and_decay_uses_percentage_damage_and_enemy_filter) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\n"
		"C;Y1;X5;K\"DataB1\"\n"
		"C;Y1;X6;K\"Area1\"\n"
		"C;Y1;X7;K\"EfctID1\"\n"
		"C;Y1;X8;K\"HeroDur1\"\n"
		"C;Y2;X1;K\"AUdd\"\n"
		"C;Y2;X2;K\"AUdd\"\n"
		"C;Y2;X3;K\"35\"\n"
		"C;Y2;X4;K\".04\"\n"
		"C;Y2;X5;K\"1\"\n"
		"C;Y2;X6;K\"300\"\n"
		"C;Y2;X7;K\"XUdd\"\n"
		"C;Y2;X8;K\"35\"\n"
		"E\n";
	const char effect_slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"EffectArt\"\n"
		"C;Y2;X1;K\"XUdd\"\nC;Y2;X2;K\"XUdd\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_oneshot.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *effect_rows = parse_slk_string(effect_slk);
	slkTestData_t *old_effects = G_SetSLKRows("AbilityBuffData", effect_rows);
	uint32_t area_effect_model = G_RegisterModel(G_AbilityEffectArt(MAKEFOURCC('X','U','d','d'), WC3_EFFECT_EFFECT, 0));
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
	edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 33, 0);
	edict_t *thinker, *area_effects[20] = { NULL };
	uint32_t thinker_slot = globals.num_edicts;
	uint32_t area_effect_count = 0;
	spellTarget_t st = { .type = SPELL_TARGET_POINT, .point = caster->s.origin2 };

	caster->s.player = 0;
	enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 100;
	(tree->destructable ? tree->destructable : (tree->destructable=G_AllocDestructable()));
	tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
	tree->health.value = tree->health.max_value = 500;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
    UnitAbilities_t abilities = { .abilList = "AUdd" };
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AUdd"), &st.point));
	thinker = &globals.edicts[thinker_slot];
	T_FEQ(enemy->health.value, 96.0f, 0.01f);
	T_FEQ(tree->health.value, 480.0f, 0.01f);
	T_FEQ(caster->health.value, 250.0f, 0.01f);
	T_ASSERT(thinker->inuse);
	T_EQ(thinker->spawn_time, G_Time() + 35000);
	T_ASSERT(area_effect_model > 0);
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) {
		T_ASSERT(area_effect_count < 20);
		T_EQ(effect->s.model, area_effect_model);
		T_FEQ(effect->s.scale, 1.0f, 0.001f);
		T_ASSERT(Vector2_distance(&effect->s.origin2, &st.point) <= 300.0f);
		T_ASSERT(effect->currentmove == &death_and_decay_area_effect_move);
		T_ASSERT(effect->currentmove->endfunc == G_FreeEdict);
		area_effects[area_effect_count++] = effect;
	}
	T_EQ(area_effect_count, 10);
	T_ASSERT(Vector2_distance(&area_effects[0]->s.origin2, &area_effects[1]->s.origin2) > 0.001f);
	/* Effects are emitted only with damage pulses, once per authored second. */
	level.time = 999;
	death_and_decay_think(thinker);
	T_FEQ(tree->health.value, 480.0f, 0.01f);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) area_effect_count++;
	T_EQ(area_effect_count, 10);
	level.time = 1000;
	death_and_decay_think(thinker);
	T_FEQ(tree->health.value, 460.0f, 0.01f);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) {
		T_ASSERT(area_effect_count < 20);
		area_effects[area_effect_count++] = effect;
	}
	T_EQ(area_effect_count, 20);
	level.time = thinker->spawn_time;
	thinker->think(thinker);
	T_ASSERT(!thinker->inuse);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) area_effect_count++;
	T_EQ(area_effect_count, 0);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
	G_SetSLKRows("AbilityBuffData", old_effects);
	free_slk_rows(effect_rows);
}

TEST(wc3_spell, death_and_decay_roc_effect_id_uses_authored_effect_object) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"AUdd\"\nC;Y2;X2;K\"AUdd\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
#if BZ_COMPAT_WORKAROUNDS
	T_EQ(G_AbilityEffectCode(MAKEFOURCC('A','U','d','d'), 1), MAKEFOURCC('X','U','d','d'));
#else
	T_EQ(G_AbilityEffectCode(MAKEFOURCC('A','U','d','d'), 1), MAKEFOURCC('A','U','d','d'));
#endif
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, holy_light_rawcode_lookup_is_nul_safe) {
	uint32_t code = MAKEFOURCC('A','H','h','b');
	ability_t const *spell = S_SpellAbilityForCode(code);
	ability_t const *abil = FindAbilityForCommand(GetClassName(code));

	/* Runtime spell dispatch starts from a uint32_t rawcode. This specifically
	 * guards against treating &code as a C string: that only worked when the
	 * unrelated byte after the four rawcode bytes happened to be zero. */
    T_NOT_NULL(abil);
    T_ASSERT(abil->proc == CAbilityHolyBolt);
    T_ASSERT(abil->flags & AB_SPELL); T_NOT_NULL(abil->proc);
	T_NOT_NULL(spell);
	T_ASSERT(S_AbilityItem(code).ability == spell);
	T_EQ((int)spell->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, blizzard_is_channel) {
	ability_t const *abil = FindAbilityByClassname("AHbz");
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_ASSERT(abil->flags & AB_CHANNEL);
	T_EQ((int)abil->target_type, (int)SPELL_TARGET_POINT);
}

TEST(wc3_spell, cold_arrows_has_autocast_flag) {
	ability_t const *abil = FindAbilityByClassname("AHca");
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_ASSERT(abil->flags & AB_AUTOCAST);
	T_ASSERT(abil->flags & AB_TOGGLE);
}


/* Holy Light must not commit mana/cooldown when the friendly target cannot be healed. */
TEST(wc3_spell, holy_light_rejects_full_health_friendly_target) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AHhb"), target));
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, FS_SLKKey("AHhb")));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Death Coil is a homing spell missile; healing/damage occurs on impact, not cast commitment. */
TEST(wc3_spell, death_coil_uses_projectile_and_rejects_self_or_full_health_ally) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    static UnitData_t human_data = { .race = STR_HUMAN };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AUdc\"\nC;Y2;X2;K\"AUdc\"\nC;Y2;X3;K\"air,ground,friend,enemy\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdc" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 120, 0);
    edict_t *missile = NULL;

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    enemy->data.UnitData = &human_data; enemy->s.player = 1; enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 500;
    ally->data.UnitData = &undead_data; ally->s.player = 0; ally->svflags |= SVF_MONSTER; ally->targtype = TARG_GROUND;
    ally->health.value = ally->health.max_value = 500;

    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), caster));
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), ally));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy));
    T_FEQ(enemy->health.value, 500, 0.001f);
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(enemy->health.value, 400, 0.001f);

    /* Mixed friend/enemy target masks still block enemy Death Coil at impact. */
    unit_addstatus(enemy, "BNss", 1);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy)); missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    T_EQ(G_UnitStatusLevel(enemy, MAKEFOURCC('B','N','s','s')), 1);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(enemy->health.value, 400, 0.001f);
    T_EQ(G_UnitStatusLevel(enemy, MAKEFOURCC('B','N','s','s')), 0);

    /* A reused target edict must not let an in-flight coil affect a new incarnation. */
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy)); missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) { enemy->spawn_time++; missile->currentmove->endfunc(missile); T_ASSERT(!missile->inuse); }
    T_FEQ(enemy->health.value, 400, 0.001f);

    ally->health.value = 250; missile = NULL;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), ally));
    T_FEQ(ally->health.value, 250, 0.001f);
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(ally->health.value, 450, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Divine Shield exposes the authored buff while the same timer owns invulnerability. */
TEST(wc3_spell, divine_shield_applies_authored_buff_for_its_duration) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHds\"\nC;Y2;X2;K\"AHds\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"BHds\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHds" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *thinker = NULL;

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHds")));
    T_ASSERT((caster && caster->invulnerable)); T_EQ(G_UnitStatusLevel(caster, FS_SLKKey("BHds")), 1);
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_ASSERT(WriteGame(Test_TempPath("divine-shield-save.bin")));
    (caster->channel ? caster->channel : (caster->channel=G_AllocChannel()));
    caster->channel->code = 0; if (thinker) thinker->think = NULL;
    T_ASSERT(ReadGame(Test_TempPath("divine-shield-save.bin")));
    FILTER_EDICTS(ent, ent->owner == caster && ent->think == divine_shield_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    if (thinker) { level.time = thinker->spawn_time; G_RunEntity(thinker); }
    unit_updatestatuses(caster);
    T_ASSERT(!(caster && caster->invulnerable)); T_EQ(G_UnitStatusLevel(caster, FS_SLKKey("BHds")), 0);
    remove(Test_TempPath("divine-shield-save.bin"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, divine_shield_expiry_rejects_recycled_caster_slot) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHds\"\nC;Y2;X2;K\"AHds\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"BHds\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHds" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *thinker = NULL;

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHds")));
    FILTER_EDICTS(ent, ent->owner == caster && ent->think == divine_shield_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_NOT_NULL(thinker ? thinker->channel : NULL);
    caster->spawn_time++;
    caster->invulnerable = true;
    if (thinker) { level.time = thinker->spawn_time; divine_shield_think(thinker); }
    T_ASSERT((caster && caster->invulnerable));
    T_ASSERT(!thinker || !thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Animate Dead rejects an empty cast, prefers the higher-level corpse, and
 * consumes that corpse into non-raisable temporary summon state. */
TEST(wc3_spell, animate_dead_prefers_higher_level_corpse_and_restores_temporary_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"targs\"\n"
        "C;Y2;X1;K\"AUan\"\nC;Y2;X2;K\"AUan\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"40\"\nC;Y2;X5;K\"900\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"ground,dead\"\nE\n";
    static UnitBalance_t const low_balance = { .level = 1 }, high_balance = { .level = 5 };
    UnitData_t corpse_data = { .deathType = 3 };
    UnitAbilities_t abilities = { .abilList = "AUan" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *low = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *high = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    low->data.UnitBalance = &low_balance; high->data.UnitBalance = &high_balance;
    low->data.UnitData = high->data.UnitData = &corpse_data;
    high->abilities.added[0] = MAKEFOURCC('A','I','n','v'); ARRAY_COUNT(high->abilities.added) = 1;
    edict_t *corpses[] = { low, high };
    FOR_LOOP(i, 2) {
        corpses[i]->s.player = 1; corpses[i]->svflags |= SVF_MONSTER;
        corpses[i]->stand = unit_stand; corpses[i]->die = unit_die;
        corpses[i]->health.value = corpses[i]->health.max_value = 500;
    }
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AUan")));
    unit_die(low, caster); unit_die(high, caster); high->aiflags |= AI_HOLD_FRAME;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AUan")));
    T_ASSERT(M_IsDead(low)); T_ASSERT(!M_IsDead(high)); T_EQ(high->s.player, 0); T_ASSERT(high->owner == caster);
    T_ASSERT(!(high->aiflags & AI_HOLD_FRAME)); T_EQ(G_UnitStatusLevel(high, FS_SLKKey("BTLF")), 1);
    T_ASSERT((high && high->vertex_color_set));
    T_EQ(high->vertex_color.r, 255); T_EQ(high->vertex_color.g, 0);
    T_EQ(high->vertex_color.b, 0); T_EQ(high->vertex_color.a, 255);
    {
        uint32_t target_effects = 0;
        FILTER_EDICTS(effect, effect->goalentity == high && effect->damage == high->spawn_time && effect->s.model)
            target_effects++;
        T_EQ(target_effects, 1);
    }
    T_ASSERT(!high->food || high->food->used == 0); T_EQ(high->summon_ability, FS_SLKKey("AUan"));
    T_ASSERT(high->aiflags & AI_CORPSE_UNRAISABLE); T_ASSERT(high->aiflags & AI_CORPSE_NO_DECAY); T_ASSERT((high && high->invulnerable));
    T_EQ(ARRAY_COUNT(high->abilities.added), 1); T_EQ(high->abilities.added[0], MAKEFOURCC('A','I','n','v'));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Resurrection uses the same high-level-first corpse preference while retaining
 * the original owner and ordinary permanent corpse revival contract. */
TEST(wc3_spell, resurrection_prefers_higher_level_friendly_corpse) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"targs\"\n"
        "C;Y2;X1;K\"AHre\"\nC;Y2;X2;K\"AHre\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"ground,friend,dead\"\nE\n";
    static UnitBalance_t const low_balance = { .level = 1 }, high_balance = { .level = 5 };
    UnitData_t corpse_data = { .deathType = 3 };
    UnitAbilities_t abilities = { .abilList = "AHre" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *low = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *high_far = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 300, 0);
    edict_t *high_near = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    low->data.UnitBalance = &low_balance;
    high_far->data.UnitBalance = high_near->data.UnitBalance = &high_balance;
    low->data.UnitData = high_far->data.UnitData = high_near->data.UnitData = &corpse_data;
    edict_t *corpses[] = { low, high_far, high_near };
    FOR_LOOP(i, 3) {
        corpses[i]->s.player = 0; corpses[i]->svflags |= SVF_MONSTER;
        corpses[i]->stand = unit_stand; corpses[i]->die = unit_die;
        corpses[i]->health.value = corpses[i]->health.max_value = 500;
        unit_die(corpses[i], caster);
    }
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    T_ASSERT(M_IsDead(low)); T_ASSERT(M_IsDead(high_far)); T_ASSERT(!M_IsDead(high_near));
    T_EQ(high_near->s.player, 0); T_ASSERT(high_near->owner != caster);
    T_ASSERT(!(high_near->aiflags & AI_CORPSE_UNRAISABLE)); T_ASSERT(!(high_near->aiflags & AI_CORPSE_NO_DECAY)); T_ASSERT((high_near && high_near->invulnerable));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Warcraft III 2.0.3 added AUa2 as the ability-preserving Animate Dead rawcode. */
TEST(wc3_spell, animate_dead_modern_aua2_registration) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUa2"));
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityAnimateDead);
}

/* Stock-style Death Pact requires a friendly non-Hero Undead victim and useful restoration. */
TEST(wc3_spell, death_pact_validates_undead_nonhero_and_full_resources) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    static UnitData_t human_data = { .race = STR_HUMAN };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend,invu\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"2\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);
    edict_t *living = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER; victim->targtype = TARG_GROUND;
    victim->health.value = victim->health.max_value = 100; victim->die = unit_die;
    living->data.UnitData = &human_data; living->s.player = 0; living->svflags |= SVF_MONSTER; living->targtype = TARG_GROUND;
    living->health.value = living->health.max_value = 100;

    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    caster->health.value = 100;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), living));
    victim->invulnerable = true;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 300, 0.001f);
    T_ASSERT(M_IsDead(victim)); T_ASSERT(victim->aiflags & AI_CORPSE_UNRAISABLE); T_ASSERT(victim->aiflags & AI_CORPSE_NO_DECAY);
    T_ASSERT(!G_UnitIsRaisableCorpse(victim));
    /* Death animation completion must skip the ordinary 90-second corpse window. */
    T_NOT_NULL(victim->currentmove);
    if (victim->currentmove && victim->currentmove->endfunc) victim->currentmove->endfunc(victim);
    T_ASSERT(victim->inuse);
    level.time += FRAMETIME;
    if (victim->currentmove && victim->currentmove->think) victim->currentmove->think(victim);
    T_ASSERT(!victim->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, death_pact_datae_can_leave_target_alive) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"DataD1\"\nC;Y1;X11;K\"DataE1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend,invu\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"0\"\nC;Y2;X11;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);

    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    caster->health.value = 100;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER;
    victim->targtype = TARG_GROUND; victim->health.value = victim->health.max_value = 100; victim->die = unit_die;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 200.0f, 0.001f);
    T_FEQ(victim->health.value, 100.0f, 0.001f);
    T_ASSERT(!M_IsDead(victim));
    T_ASSERT(!(victim->aiflags & AI_CORPSE_UNRAISABLE));
    T_ASSERT(!(victim->aiflags & AI_CORPSE_NO_DECAY));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


/* DataC/DataD switch Death Pact from current-life percentages to Warsmash's
 * authored fixed-value conversion mode. The fixed values are removed from the
 * caster resources and their sum is removed from the victim's life. */
TEST(wc3_spell, death_pact_datac_datad_fixed_value_conversion) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"DataD1\"\nC;Y1;X11;K\"DataE1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\n"
        "C;Y2;X7;K\"25\"\nC;Y2;X8;K\"40\"\nC;Y2;X9;K\"1\"\n"
        "C;Y2;X10;K\"1\"\nC;Y2;X11;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);

    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    caster->health.value = 300.0f; caster->mana.value = 150.0f;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER;
    victim->targtype = TARG_GROUND; victim->health.value = victim->health.max_value = 200.0f;
    victim->mana.value = victim->mana.max_value = 100.0f; victim->die = unit_die;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 260.0f, 0.001f);
    T_FEQ(caster->mana.value, 125.0f, 0.001f);
    T_FEQ(victim->health.value, 135.0f, 0.001f);
    T_ASSERT(!M_IsDead(victim));

    /* DataC value-mode has the Warcraft/Warsmash unit-with-mana target gate. */
    victim->mana.value = 0.0f;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 260.0f, 0.001f);
    T_FEQ(caster->mana.value, 125.0f, 0.001f);
    T_FEQ(victim->health.value, 135.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, non_spell_ability_uses_explicit_command) {
	ability_t const *abil = FindAbilityByClassname(STR_CmdMove);
	T_NOT_NULL(abil);
	T_ASSERT(!(abil->flags & AB_SPELL));  /* move is not a spell */
	T_ASSERT(S_AbilityHasCommand(abil));
	T_NULL(FindAbilityByClassname("AAsm"));
	T_ASSERT(!S_AbilityHasCommand(FindAbilityByClassname("Afbk")));
}

/* Exercise the player command route with only the flag and leaf callbacks on Holy Bolt. */
TEST(wc3_spell, holy_light_flag_dispatch_validates_and_heals) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0), *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char number[16];
    cstring_t button[] = { "button", "AHhb" }, select[] = { "select", number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 500;
    G_SelectEntity(client, caster);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)caster->s.number);
    G_ClientCommand(clent, 2, select);
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));

    G_ClientCommand(clent, 2, button);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);
    T_FEQ(target->health.value, 300, 0.001f);
    T_FEQ(caster->mana.value, 135, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_NULL(client->menu.on_entity_selected);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* A command-card cast bypasses G_IssueUnitTargetOrder; it must still retire a Stop guard point. */
TEST(wc3_spell, command_card_cast_clears_stop_guard) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0), *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char number[16];
    cstring_t button[] = { "button", "AHhb" }, select[] = { "select", number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 500;
    G_SelectEntity(client, caster);
    order_stop(caster);
    T_ASSERT(caster->movement.guard_state == GUARD_IDLE);

    G_ClientCommand(clent, 2, button);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);

    T_FEQ(target->health.value, 300, 0.001f);
    T_ASSERT(caster->movement.guard_state == GUARD_NONE);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, unavailable_spell_button_does_not_arm_targeting) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    cstring_t button[] = { "button", "AHhb" };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = client->ps.number;
    G_SelectEntity(client, caster);
    G_SetPlayerAbilityAvailable(client, MAKEFOURCC('A','H','h','b'), false);

    G_ClientCommand(clent, 2, button);
    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);

    G_SetPlayerAbilityAvailable(client, MAKEFOURCC('A','H','h','b'), true);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


/* The retained multiselect panel sends `focus <entity>` for portrait clicks.
 * While a unit-target spell is armed, Focus must feed that selected unit into
 * the target callback instead of changing subgroup focus. */
TEST(wc3_spell, holy_light_multiselect_portrait_targets_without_changing_focus) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char caster_number[16], target_number[16];
    cstring_t button[] = { "button", "AHhb" };
    cstring_t focus_caster[] = { "focus", caster_number };
    cstring_t focus_target[] = { "focus", target_number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    G_ResetSelectionFocus(client);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100;
    target->health.max_value = 500;
    G_SelectEntity(client, caster);
    G_SelectEntity(client, target);
    T_ASSERT(G_FocusSelectedUnit(client, caster));
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    snprintf(caster_number, sizeof(caster_number), "%u", (unsigned)caster->s.number);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);

    /* Clicking Arthas's own portrait is an invalid Holy Light target. The
     * spell stays armed and the focused subgroup must remain Arthas. */
    G_ClientCommand(clent, 2, focus_caster);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_FEQ(target->health.value, 100, 0.001f);

    /* Clicking the selected Footman's portrait targets him rather than
     * focusing his subgroup. Successful casting exits target mode. */
    G_ClientCommand(clent, 2, focus_target);
    T_FEQ(target->health.value, 300, 0.001f);
    T_FEQ(caster->mana.value, 135, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    T_ASSERT(G_IsEntitySelected(client, caster));
    T_ASSERT(G_IsEntitySelected(client, target));

    G_ResetSelectionFocus(client);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, unit_target_click_accepts_out_of_range_target_and_casts_after_approach) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOcl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client;
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
    edict_t *thinker;
    uint32_t thinker_slot;
    char target_number[16];
    cstring_t button[] = { "button", "AOcl" };
    cstring_t select_target[] = { "select", target_number };

    /* make_hero resets edicts, so restore the player-slot client for commands. */
    clent->client = client = &game.clients[0];
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = client->ps.number;
    caster->collision = 16.0f;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    ((mapInfo_t *)level.mapinfo)->players[caster->s.player].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[target->s.player].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SelectEntity(client, caster);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    thinker_slot = globals.num_edicts;
    G_ClientCommand(clent, 2, select_target);

    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(caster->goalentity == target);
    T_ASSERT(move_is_active_order_walk(caster));
    T_FEQ(target->health.value, 500.0f, 0.001f);
    T_FEQ(caster->mana.value, 300.0f, 0.001f);
    thinker = &globals.edicts[thinker_slot];
    T_ASSERT(thinker->inuse);
    T_NULL(thinker->think);
    T_ASSERT(S_UnitTargetApproachReceiver(caster)==thinker);

    caster->s.origin2.x = caster->s.origin.x = 425.0f;
    caster->s.origin2.y = caster->s.origin.y = 0.0f;
    spell_test_approach_tick(caster,thinker);

    T_ASSERT(!thinker->inuse);
    T_FEQ(target->health.value, 400.0f, 0.001f);
    T_FEQ(caster->mana.value, 225.0f, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','O','c','l')));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, unit_target_approach_replaces_same_target_cast) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\n"
        "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\n"
        "C;Y3;X3;K\"air,ground,enemy,neutral\"\nC;Y3;X4;K\"75\"\n"
        "C;Y3;X5;K\"9\"\nC;Y3;X6;K\"100\"\nC;Y3;X7;K\"100\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X9;K\"55\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *target, *first, *latest;
    uint32_t first_slot, latest_slot;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    caster->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','t','b'), .level = 1);

    first_slot = globals.num_edicts;
    /* This allocator-only Ofar fixture has no authored turn row. Approach
     * completion now also obeys Move's facing gate, as the retail task does. */
    caster->unitinfo.TurnSpeed=.6f;caster->unitinfo.MoveSpeed=270;
    caster->unitinfo.move_flags|=BZ_UNIT_TURN_SET|BZ_UNIT_SPEED_SET;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    first = &globals.edicts[first_slot];
    T_ASSERT(first->inuse && S_UnitTargetApproachReceiver(caster)==first);
    T_EQ(caster->current_order_id,G_OrderId("chainlightning"));

    latest_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','H','t','b'), target));
    latest = &globals.edicts[latest_slot];
    T_ASSERT(!first->inuse);
    T_ASSERT(latest->inuse && S_UnitTargetApproachReceiver(caster)==latest);
    T_EQ(latest->class_id, MAKEFOURCC('A','H','t','b'));
    T_EQ(caster->current_order_id,G_OrderId("thunderbolt"));

    /* Be inside the captured arrival radius; this test owns replacement
     * identity, not Move's separate strict boundary/facing contract. */
    S_SetUnitAxisPosition(caster,0,510);
    spell_test_approach_tick(caster,latest);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','H','t','b')));
    T_ASSERT(!latest->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, unit_target_approach_rejects_reused_edict_slots) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *target, *replacement, *approach;
    uint32_t approach_slot, old_spawn_time;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    level.time = 100;
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->spawn_time = 100; target->spawn_time = 100;
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    approach = &globals.edicts[approach_slot];
    T_ASSERT(approach->inuse && S_UnitTargetApproachReceiver(caster)==approach);
    old_spawn_time = target->spawn_time;
    G_FreeEdict(target);
    level.time = 1101;
    replacement = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    T_ASSERT(replacement == target);
    replacement->spawn_time = G_Time();
    replacement->s.player = 1;
    replacement->svflags |= SVF_MONSTER;
    replacement->targtype = TARG_GROUND;
    replacement->health.value = replacement->health.max_value = 500;
    T_NE(replacement->spawn_time, old_spawn_time);
    caster->s.origin2.x = caster->s.origin.x = 500;
    spell_test_approach_tick(caster,approach);
    T_ASSERT(!approach->inuse);
    T_FEQ(replacement->health.value, 500.0f, 0.001f);
    T_FEQ(caster->mana.value, 300.0f, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','c','l')));

    reset_entities(); setup_test_world();
    level.time = 100;
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->spawn_time = 100; target->spawn_time = 100;
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    approach = &globals.edicts[approach_slot];
    G_FreeEdict(caster);
    level.time = 1101;
    replacement = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 500, 0);
    T_ASSERT(replacement == caster);
    replacement->spawn_time = G_Time();
    replacement->s.player = 0;
    replacement->svflags |= SVF_MONSTER;
    replacement->targtype = TARG_GROUND;
    replacement->mana.value = replacement->mana.max_value = 300;
    replacement->stand = unit_stand;
    replacement->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    order_move(replacement, target);
    replacement->s.origin2.x = replacement->s.origin.x = 500;
    T_ASSERT(move_is_active_order_walk(replacement));
    spell_test_approach_tick(caster,approach);
    T_ASSERT(!approach->inuse);
    T_FEQ(target->health.value, 500.0f, 0.001f);
    T_FEQ(replacement->mana.value, 300.0f, 0.001f);
    T_ASSERT(S_SpellCooldownReady(replacement, MAKEFOURCC('A','O','c','l')));
    T_ASSERT(replacement->goalentity == target);
    T_ASSERT(move_is_active_order_walk(replacement));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, committed_cast_cancels_unit_target_approach_and_move) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\n"
        "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\n"
        "C;Y3;X3;K\"air,ground,enemy,neutral\"\nC;Y3;X4;K\"75\"\n"
        "C;Y3;X5;K\"9\"\nC;Y3;X6;K\"100\"\nC;Y3;X7;K\"100\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X9;K\"55\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *far_target, *near_target, *approach;
    uint32_t approach_slot;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    far_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    near_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    caster->s.player = 0; far_target->s.player = near_target->s.player = 1;
    caster->svflags |= SVF_MONSTER;
    far_target->svflags |= SVF_MONSTER; near_target->svflags |= SVF_MONSTER;
    caster->targtype = far_target->targtype = near_target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    far_target->health.value = far_target->health.max_value = 500;
    near_target->health.value = near_target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    caster->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','t','b'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), far_target));
    approach = &globals.edicts[approach_slot];
    T_ASSERT(approach->inuse && move_is_active_order_walk(caster));
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','H','t','b'), near_target));
    T_ASSERT(!approach->inuse);
    T_ASSERT(!move_is_active_order_walk(caster));
    T_NULL(caster->goalentity);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}


TEST(wc3_spell, target_order_name_routes_chain_lightning_through_spell_pipeline) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOcl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));

    T_EQ(G_OrderId("chainlightning"), 852119);
    T_STREQ(G_OrderId2String(852119), "chainlightning");
    T_ASSERT(unit_issuetargetorder(caster, "chainlightning", target));
    T_FEQ(target->health.value, 400.0f, 0.001f);
    T_FEQ(caster->mana.value, 225.0f, 0.001f);
    T_EQ(G_GetIssuedOrderId(caster), 852119);
    /* Original67ce30 dispatches these callbacks synchronously. Response data
     * and pre-spend cancellation are checked by cast282_*. Damage still queues. */
    for(uint32_t i=level.events.read;i<level.events.write;i++) {
        T_NE(level.events.queue[i%MAX_EVENT_QUEUE].type,EVENT_PLAYER_UNIT_SPELL_EFFECT);
        T_NE(level.events.queue[i%MAX_EVENT_QUEUE].type,EVENT_UNIT_SPELL_EFFECT);
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, point_order_name_routes_blink_and_carries_spell_point) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\n"
        "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1000\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AEbl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','w','d','n'), 500, 300, 0, 0);
    vec2_t point = { 64.0f, 32.0f };

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    caster->collision = 16.0f;
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));

    T_ASSERT(unit_issueorder(caster, "blink", &point));
    T_FEQ(caster->s.origin2.x, point.x, 0.001f);
    T_FEQ(caster->s.origin2.y, point.y, 0.001f);
    T_FEQ(caster->s.origin.x, point.x, 0.001f);
    T_FEQ(caster->s.origin.y, point.y, 0.001f);
    T_EQ(G_GetIssuedOrderId(caster), 852525);
    /* Original67ce30 dispatches these callbacks synchronously. Response data
     * and pre-spend cancellation are checked by cast282_*; nothing is queued. */
    T_EQ(level.events.write, level.events.read);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, cancel_point_target_mode_clears_spell_cursor_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    slkTestData_t *rows, *old;
    edict_t *clent = spell_cursor_test_begin(&rows, &old);

    T_ASSERT(S_SpellPointTargetMode(clent));
    T_ASSERT(spell_cursor_capture_radius > 0.0f);
    spell_cursor_clear_seen = false;
    gi.Write = spell_cursor_capture_write;
    T_ASSERT(G_CancelTargetMode(clent));
    gi.Write = old_write;

    T_ASSERT(spell_cursor_clear_seen);
    T_NULL(clent->client->menu.on_location_selected);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, command_card_refresh_clears_spell_cursor_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    slkTestData_t *rows, *old;
    edict_t *clent = spell_cursor_test_begin(&rows, &old);

    T_ASSERT(S_SpellPointTargetMode(clent));
    T_ASSERT(spell_cursor_capture_radius > 0.0f);
    spell_cursor_clear_seen = false;
    gi.Write = spell_cursor_capture_write;
    Get_Commands_f(clent);
    gi.Write = old_write;

    T_ASSERT(spell_cursor_clear_seen);
    T_NULL(clent->client->menu.on_location_selected);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, polymorph_validates_creep_limit_summons_and_restores_runtime_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
        "C;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aply\"\nC;Y2;X2;K\"Aply\"\nC;Y2;X3;K\"air,ground,enemy\"\n"
        "C;Y2;X4;K\"220\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"60\"\nC;Y2;X8;K\"60\"\nC;Y2;X9;K\"5\"\n"
        "C;Y2;X10;K\"opeo\"\nC;Y2;X11;K\"opeo\"\nC;Y2;X12;K\"opeo\"\n"
        "C;Y2;X13;K\"opeo\"\nC;Y2;X14;K\"Bply\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Aply" };
    UnitData_t ground = { .moveTypeName = "foot" };
    UnitBalance_t creep = { .level = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    abilityitem_t ability_item = S_AbilityItem(FS_SLKKey("Aply"));
    ability_t const *ability = ability_item.ability;
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 500, 500, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    T_NOT_NULL(rows); T_NOT_NULL(ability); T_NOT_NULL(ability ? ability->proc : NULL);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    game.clients[0].ps.number = 0;
    target->data.UnitData = &ground; target->data.UnitBalance = &creep;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    memset(level.alliances, 0, sizeof(level.alliances));

    G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey("Aply"), false);
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey("Aply"), true);
    T_ASSERT(test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 6; T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 5; target->summon_ability = MAKEFOURCC('A','O','s','f');
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    target->summon_ability = 0; target->aiflags |= AI_ILLUSION;
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));

    target->aiflags &= ~AI_ILLUSION;
    /* Keep this presentation-restore fixture's expected234, but construct a
     * coherent current base: retail removes the authored form delta, not an
     * entry-time snapshot (see profile271's public-setter capture). */
    creep.speed = G_UnitBalance(MAKEFOURCC('o','p','e','o'))->speed + 114.0f;
    if (!target->polymorph) target->polymorph = G_AllocPolymorph();
    assert(target->polymorph);
    *target->polymorph = MAKE(polymorph_t,
        .ability = MAKEFOURCC('A','p','l','y'), .buff = MAKEFOURCC('B','p','l','y'),
        .form_type = MAKEFOURCC('o','p','e','o'), .original_model = 17,
        .original_scale = 1.25f, .original_move_speed = 234.0f);
    target->s.model = 99; target->s.scale = 0.75f; target->unitinfo.MoveSpeed = 120.0f;
    T_ASSERT(S_UnitPolymorphed(target));
    S_PolymorphRemove(target);
    T_ASSERT(!S_UnitPolymorphed(target));
    T_EQ(target->s.model, 17); T_FEQ(target->s.scale, 1.25f, 0.001f);
    T_FEQ(target->unitinfo.MoveSpeed, 234.0f, 0.001f);
    T_EQ(target->class_id, MAKEFOURCC('h','f','o','o'));
    T_EQ(G_OrderId("polymorph"), WC3_ORDER_ID_POLYMORPH);
    T_STREQ(G_OrderId2String(WC3_ORDER_ID_POLYMORPH), "polymorph");

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Roar is one area cast, not an all-or-nothing status transaction.
 * A capacity-full ally must not prevent a second ally receiving the buff. */
TEST(wc3_spell, roar_skips_full_ally_and_buffs_other_targets) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"Area1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0.37\"\n"
        "C;Y2;X6;K\"Broa\"\nC;Y2;X7;K\"300\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
    edict_t *full = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 60, 0);
    edict_t *free_ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *roar_effect = NULL, *caster_effect = NULL;
    caster->s.player = full->s.player = free_ally->s.player = 0;
    full->svflags |= SVF_MONSTER;
    free_ally->svflags |= SVF_MONSTER;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        char buff[5] = { 'B', 'q', '0', '0', 0 };
        buff[2] = (char)('0' + i / 10);
        buff[3] = (char)('0' + i % 10);
        T_ASSERT(unit_addtimedstatus(full, buff, 1, 30.0f));
    }
    test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_EQ(G_UnitStatusLevel(full, MAKEFOURCC('B','r','o','a')), 0);
    T_EQ(G_UnitStatusLevel(free_ally, MAKEFOURCC('B','r','o','a')), 1);
    T_FEQ(S_RoarDamageBonus(free_ally), 0.37f, 0.001f);
    T_EQ(G_UnitStatusLevel(caster, MAKEFOURCC('B','r','o','a')), 1);
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','r','o','a'), WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','r','o','a') && effect->goalentity == free_ally) {
        roar_effect = effect;
        break;
    }
    FILTER_EDICTS(effect, effect->goalentity == caster && effect->s.model == G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx")) {
        caster_effect = effect;
        break;
    }
    T_NOT_NULL(roar_effect);
    T_NOT_NULL(caster_effect);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, roar_increases_attack_damage_by_authored_bonus) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"Area1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"Broa\"\nC;Y2;X7;K\"300\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *enemy;

    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 500, 200, 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    caster->s.player = 0;
    enemy->s.player = 1;
    S_AttackProfileWrite(caster, 0)->type = ATK_CHAOS;
    enemy->health.value = enemy->health.max_value = 1000.0f;

    S_ResolveAttackHit(caster, enemy, 100);
    T_FEQ(enemy->health.value, 900.0f, 0.001f);
    test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_FEQ(S_RoarDamageBonus(caster), 0.25f, 0.001f);
    S_ResolveAttackHit(caster, enemy, 100);
    T_FEQ(enemy->health.value, 775.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, roar_damage_bonus_is_shown_as_green_flat_damage_in_status_panel) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0.25\"\nC;Y2;X5;K\"Broa\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;
    char display[128];

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    S_AttackProfileWrite(unit, 0)->damageBase = 24;
    S_AttackProfileWrite(unit, 0)->numberOfDice = 1;
    S_AttackProfileWrite(unit, 0)->sidesPerDie = 1;
    T_ASSERT(unit_addtimedstatus(unit, "Broa", 1, 30.0f));
    UI_TestFormatAttackDamageWithStatus(display, sizeof(display), unit, 25, 25, 0.0f, 24.0f);
    T_STREQ(display, "25 - 25 |cff00ff00+6|r");

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Drive the same primary timer protocol used by the native morph scheduler. */
static void spell_metamorph_advance(float deadline) {
    abilityTimerRequest_t request;
    unsigned fired = 0;
    while (S_NextAbilityPrimaryTimer(&request) && request.deadline.time <= deadline) {
        T_ASSERT(++fired < 128);
        if (fired >= 128) break;
        level.pathing_clock = request.deadline;
        abilityCall_t call = {.primary_timer = &request};
        request.proc(NULL, A_PRIMARY_TIMER_FIRE, &call);
    }
    level.pathing_clock.time = deadline;
}

TEST(wc3_spell, metamorphosis_keeps_abilities_adds_ranged_attack_and_reverts_bonus_health) {
    char const ability_data[] =
        "ID;PWXL;N;EBB;Y5;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"UnitID1\"\nC;Y1;X5;K\"DataE1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AEme\"\nC;Y2;X2;K\"AEme\"\nC;Y2;X3;K1\nC;Y2;X4;K\"Edmm\"\nC;Y2;X5;K500\nC;Y2;X6;K30\nC;Y2;X7;K\"BEme\"\n"
        "C;Y3;X1;K\"AEmb\"\nC;Y3;X2;K\"AEmb\"\nC;Y3;X3;K1\n"
        "C;Y4;X1;K\"AEim\"\nC;Y4;X2;K\"AEim\"\nC;Y4;X3;K1\n"
        "C;Y5;X1;K\"AEev\"\nC;Y5;X2;K\"AEev\"\nC;Y5;X3;K1\nE\n";
    char const unit_abilities[] =
        "ID;PWXL;N;EBB;Y3;X2\nC;Y1;X1;K\"unitAbilID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"AEme\"\nC;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"AEme\"\nE\n";
    char const unit_ui[] =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"modelScale\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"Units\\NightElf\\DemonHunter\\DemonHunter.mdx\"\nC;Y2;X3;K1\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"Units\\NightElf\\DemonHunter\\DemonHunter.mdx\"\nC;Y3;X3;K1\nE\n";
    char const unit_balance[] =
        "ID;PWXL;N;EBB;Y3;X5\nC;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\nC;Y1;X3;K\"spd\"\n"
        "C;Y1;X4;K\"STR\"\nC;Y1;X5;K\"STRplus\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K575\nC;Y2;X3;K300\nC;Y2;X4;K19\nC;Y2;X5;K2.4\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K750\nC;Y3;X3;K300\nC;Y3;X4;K26\nC;Y3;X5;K2.4\nE\n";
    char const unit_data[] =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"targType\"\nC;Y1;X3;K\"movetp\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"ground\"\nC;Y2;X3;K\"foot\"\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"ground\"\nC;Y3;X3;K\"foot\"\nE\n";
    char const unit_weapons[] =
        "ID;PWXL;N;EBB;Y3;X8\nC;Y1;X1;K\"unitWeaponsID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y1;X3;K\"atkType1\"\nC;Y1;X4;K\"weapTp1\"\nC;Y1;X5;K\"rangeN1\"\nC;Y1;X6;K\"dice1\"\n"
        "C;Y1;X7;K\"cool1\"\nC;Y1;X8;K\"dmgpt1\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K1\nC;Y2;X3;K\"hero\"\nC;Y2;X4;K\"mwc\"\nC;Y2;X5;K100\nC;Y2;X6;K1\nC;Y2;X7;K1\nC;Y2;X8;K0.03\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K1\nC;Y3;X3;K\"chaos\"\nC;Y3;X4;K\"msplash\"\nC;Y3;X5;K600\nC;Y3;X6;K1\nC;Y3;X7;K1\nC;Y3;X8;K0.03\nE\n";
    slkTestData_t *ability_rows = parse_slk_string(ability_data), *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *abilities_rows = parse_slk_string(unit_abilities), *old_abilities = G_SetSLKRows("UnitAbilities", abilities_rows);
    slkTestData_t *ui_rows = parse_slk_string(unit_ui), *old_ui = G_SetSLKRows("UnitUI", ui_rows);
    slkTestData_t *balance_rows = parse_slk_string(unit_balance), *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    slkTestData_t *data_rows = parse_slk_string(unit_data), *old_data = G_SetSLKRows("UnitData", data_rows);
    slkTestData_t *weapons_rows = parse_slk_string(unit_weapons), *old_weapons = G_SetSLKRows("UnitWeapons", weapons_rows);
    edict_t *demon;

    reset_entities(); setup_test_world();
    demon = alloc_test_unit(MAKEFOURCC('E','d','e','m'), 64.0f, 64.0f);
    demon->s.player = 0;
    demon->svflags |= SVF_MONSTER;
    G_HeroApplyLevel(demon, 10);
    demon->health.max_value = demon->health.value = 1100.0f;
    T_EQ(demon->hero.level, 10u);
    demon->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','m','e'), .level = 1);
    abilityitem_t const spell = S_AbilityItem(MAKEFOURCC('A','E','m','e'));
    spellTarget_t const target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    abilityCall_t const call = MAKE(abilityCall_t, .item = &spell, .target = &target);
    T_ASSERT(S_AbilityMessage(demon, A_EXECUTE, &call));
    T_EQ(demon->class_id, MAKEFOURCC('E','d','e','m'));
    T_ASSERT(demon->morph && demon->morph->transition.active);
    if (demon->morph)
        spell_metamorph_advance(demon->morph->transition.deadline.time);
    T_EQ(demon->class_id, MAKEFOURCC('E','d','m','m'));
    /* At level 10 Edem has 1100 HP. Edmm's higher base STR and HP retain
     * that level progression; Metamorphosis DataE then adds 500 more. */
    T_EQ(demon->hero.level, 10u);
    T_EQ(demon->hero.str, 47u);
    T_FEQ(demon->health.max_value, 1775.0f, 0.001f);
    T_EQ(S_AttackProfileRead(demon, 0)->projectile.model,
         G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
    T_FEQ(S_AttackProfileRead(demon, 0)->range, 600.0f, 0.001f);
    T_EQ(S_AttackProfileRead(demon, 0)->weapon, WPN_MSPLASH);
    T_ASSERT(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')) != NULL);
    T_ASSERT(unit_statusshowstimedbar(MAKEFOURCC('B','E','m','e')));
    T_FEQ(unit_statusremainingfraction(unit_findstatus(demon, MAKEFOURCC('B','E','m','e'))), 1.0f, 0.001f);
    T_ASSERT(unit_findtimedbarstatus(demon) == unit_findstatus(demon, MAKEFOURCC('B','E','m','e')));
    S_SetUnitPaused(demon, true);
    float const paused_fraction = unit_statusremainingfraction(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')));
    spell_metamorph_advance(level.pathing_clock.time + 1.0f);
    T_FEQ(unit_statusremainingfraction(unit_findstatus(demon, MAKEFOURCC('B','E','m','e'))), paused_fraction, 0.000001f);
    S_SetUnitPaused(demon, false);
    spell_metamorph_advance(demon->movement.pause_deadline.time);
    S_RunMoveTimers();
    T_ASSERT(G_ActorHasSkill(demon, "AEmb"));
    T_ASSERT(G_ActorHasSkill(demon, "AEim"));
    T_ASSERT(G_ActorHasSkill(demon, "AEev"));
    /* Active native morph ownership and its DataE inverse survive reload. */
    {
        uint32_t slot = (uint32_t)(demon - g_edicts);
        cstring_t save = Test_TempPath("metamorphosis-active-health.bin");
        float life = demon->health.value;
        float expiry = demon->morph->expiration.deadline.time;
        T_FEQ(demon->morph->metamorphosis_health_bonus, 500.0f, 0.001f);
        T_ASSERT(WriteGame(save));
        T_ASSERT(ReadGame(save));
        remove(save);
        demon = g_edicts + slot;
        T_EQ(demon->class_id, MAKEFOURCC('E','d','m','m'));
        T_EQ(demon->hero.level, 10u);
        T_FEQ(demon->health.max_value, 1775.0f, 0.001f);
        T_FEQ(demon->health.value, life, 0.001f);
        T_NOT_NULL(demon->morph);
        if (demon->morph) {
            T_FEQ(demon->morph->metamorphosis_health_bonus, 500.0f, 0.001f);
            T_ASSERT(demon->morph->expiration.active);
            T_FEQ(demon->morph->expiration.deadline.time, expiry, 0.000001f);
        }
        T_ASSERT(G_ActorHasSkill(demon, "AEmb"));
        T_ASSERT(G_ActorHasSkill(demon, "AEim"));
        T_ASSERT(G_ActorHasSkill(demon, "AEev"));
        T_EQ(S_AttackProfileRead(demon, 0)->projectile.model,
             G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
        T_FEQ(S_AttackProfileRead(demon, 0)->range, 600.0f, 0.001f);
        T_NOT_NULL(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')));
    }
    {
        edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500.0f, 64.0f);
        edict_t *missile = NULL;
        demon->data.UnitProfile = (UnitProfile_t *)G_UnitProfile(demon->class_id);
        demon->goalentity = enemy;
        enemy->targtype = TARG_GROUND;
        enemy->armor_value = 0.0f;
        demon->s.player = 0;
        enemy->s.player = 1;
        demon->svflags |= SVF_MONSTER;
        S_AttackProfileWrite(demon, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
        enemy->targtype = TARG_GROUND;
        enemy->health.value = enemy->health.max_value = 1000.0f;
        demon->goalentity = enemy;
        demon->wait = 0.0f;
        T_ASSERT(S_AttackCanTarget(demon, enemy));
        attack_ranged(demon);
        /* Damage commits through the primary request; animation thinks no
         * longer consume wait. Keep presentation unadvanced to inspect Birth. */
        T_ASSERT(demon->attack_damage.active);
        if (demon->attack_damage.active)
            spell_metamorph_advance(demon->attack_damage.deadline.time);
        T_ASSERT(!demon->attack_damage.active);
        FILTER_EDICTS(candidate, candidate->owner == demon && candidate->movetype == MOVETYPE_FLYMISSILE) {
            missile = candidate;
            break;
        }
        T_NOT_NULL(missile);
        if (missile) {
            T_EQ(missile->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
            T_NOT_NULL(missile->animation);
            T_STREQ(missile->animation->name, "Birth");
        }
    }
    T_ASSERT(demon->morph && demon->morph->expiration.active);
    if (demon->morph) {
        spell_metamorph_advance(demon->morph->expiration.deadline.time);
        if (demon->morph->transition.active)
            spell_metamorph_advance(demon->morph->transition.deadline.time);
    }
    T_EQ(demon->class_id, MAKEFOURCC('E','d','e','m'));
    T_EQ(demon->hero.level, 10u);
    T_EQ(demon->hero.str, 40u);
    T_FEQ(demon->health.max_value, 1100.0f, 0.001f);
    T_NULL(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')));
    if (demon->morph)
        T_FEQ(demon->morph->metamorphosis_health_bonus, 0.0f, 0.001f);
    T_ASSERT(G_ActorHasSkill(demon, "AEmb"));
    T_ASSERT(G_ActorHasSkill(demon, "AEim"));
    T_ASSERT(G_ActorHasSkill(demon, "AEev"));


    G_SetSLKRows("UnitWeapons", old_weapons); free_slk_rows(weapons_rows);
    G_SetSLKRows("UnitData", old_data); free_slk_rows(data_rows);
    G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance_rows);
    G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui_rows);
    G_SetSLKRows("UnitAbilities", old_abilities); free_slk_rows(abilities_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, bear_form_hides_normal_form_spells_and_uses_reverse_button_art) {
    static cstring_t const unit_ui_slk =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\nC;Y1;X4;K\"scale\"\n"
        "C;Y2;X1;K\"edoc\"\nC;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K1\n"
        "C;Y3;X1;K\"edcm\"\nC;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\nC;Y3;X4;K1\nE\n";
    slkTestData_t *ui_rows, *old_ui;
    edict_t *bear;
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool bear_form_button = false;
    abilityitem_t bear_form = S_AbilityItem(FS_SLKKey("Abrf"));
    abilityitem_t rejuv = S_AbilityItem(FS_SLKKey("Arej"));
    abilityCall_t toggle_call = MAKE(abilityCall_t, .item = &bear_form);
    edict_t *ally;
    spellTarget_t ally_target;

    reset_entities(); setup_test_world();
    cstring_t ability_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X3;K\"DataA1\"\nC;X4;K\"Data11\"\nC;X5;K\"UnitID1\"\nC;X6;K\"Dur1\"\n"
        "C;X1;Y2;K\"Abrf\"\nC;X2;K\"Abrf\"\nC;X3;K\"edoc\"\nC;X4;K\"edoc\"\n"
        "C;X5;K\"edcm\"\nC;X6;K0.375\nE\n";
    slkTestData_t *ability_rows = parse_slk_string(ability_slk);
    slkTestData_t *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    ui_rows = parse_slk_string(unit_ui_slk);
    old_ui = G_SetSLKRows("UnitUI", ui_rows);
    cstring_t unit_abilities_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitAbilID\"\nC;X2;K\"abilList\"\n"
        "C;X1;Y2;K\"edoc\"\nC;X2;K\"Abrf,Ara2,Arej,Aroa\"\n"
        "C;X1;Y3;K\"edcm\"\nC;X2;K\"Abrf,Ara2,Arej,Aroa\"\nE\n";
    slkTestData_t *unit_ability_rows = parse_slk_string(unit_abilities_slk);
    slkTestData_t *old_unit_abilities = G_SetSLKRows("UnitAbilities", unit_ability_rows);
    cstring_t unit_balance_slk =
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\nC;X3;K\"spd\"\n"
        "C;X1;Y2;K\"edoc\"\nC;X2;K500\nC;X3;K270\n"
        "C;X1;Y3;K\"edcm\"\nC;X2;K700\nC;X3;K270\nE\n";
    slkTestData_t *balance_rows = parse_slk_string(unit_balance_slk);
    slkTestData_t *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    bear = alloc_test_unit(MAKEFOURCC('e','d','c','m'), 0, 0);
    count = G_GetCommandButtons(bear, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Abrf")) {
            bear_form_button = true;
            T_STREQ(buttons[i].art, "ReplaceableTextures\\CommandButtons\\BTNDruidOfTheClaw.blp");
        }
        T_ASSERT(strcmp(buttons[i].command, "Ara2") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Arej") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Aroa") != 0);
    }
    T_ASSERT(bear_form_button);
    T_ASSERT(S_AbilityMessage(bear, A_TOGGLE_ON, &toggle_call));

    G_SetSLKRows("AbilityData", old_ability);
    ally = alloc_test_unit(MAKEFOURCC('e','d','o','c'), 64.0f, 0.0f);
    ally_target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally);
    T_ASSERT(!test_ability_message(bear, A_VALIDATE, &rejuv, &ally_target));

    test_execute_code(bear, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_EQ(G_UnitStatusLevel(bear, MAKEFOURCC('B','r','o','a')), 0);

    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    /* The query drives the command button's Unart while the normal form is
     * active. Keep the fixture ability list attached to exercise HUD art. */
    bear->class_id = MAKEFOURCC('e','d','o','c');
    T_ASSERT(!S_AbilityMessage(bear, A_TOGGLE_ON, &toggle_call));
    bear_form_button = false;
    count = G_GetCommandButtons(bear, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Abrf")) {
            bear_form_button = true;
            T_STREQ(buttons[i].art, "ReplaceableTextures\\CommandButtons\\BTNBearForm.blp");
        }
    }
    T_ASSERT(bear_form_button);

    T_ASSERT(unit_issueimmediateorder(bear, "bearform"));
    T_EQ(bear->class_id, MAKEFOURCC('e','d','o','c'));
    T_STREQ(G_UnitAnimationRequest(bear), "morph");
    T_ASSERT(bear->morph && bear->morph->transition.active);
    if (bear->morph) spell_metamorph_advance(bear->morph->transition.deadline.time);
    T_EQ(bear->class_id, MAKEFOURCC('e','d','c','m'));
    T_ASSERT(G_ActorHasSkill(bear, "Abrf"));
    T_ASSERT(bear->health.value > 0.0f);
    T_FEQ(bear->health.max_value, 700.0f, 0.001f);
    T_ASSERT(unit_issueimmediateorder(bear, "unbearform"));
    T_EQ(bear->class_id, MAKEFOURCC('e','d','c','m'));
    T_STREQ(G_UnitAnimationRequest(bear), "morph alternate");
    T_ASSERT(bear->morph && bear->morph->transition.active);
    if (bear->morph) spell_metamorph_advance(bear->morph->transition.deadline.time);
    T_EQ(bear->class_id, MAKEFOURCC('e','d','o','c'));
    T_ASSERT(bear->health.value > 0.0f);
    T_FEQ(bear->health.max_value, 500.0f, 0.001f);
    G_SetSLKRows("UnitBalance", old_balance);
    free_slk_rows(balance_rows);
    G_SetSLKRows("UnitAbilities", old_unit_abilities);
    free_slk_rows(unit_ability_rows);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(ability_rows);
    G_SetSLKRows("UnitUI", old_ui);
    free_slk_rows(ui_rows);
}

TEST(wc3_spell, melee_spells_use_authored_status_and_bonus_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y5;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"BuffID1\"\n"
		"C;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"targs\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X4;K\"60\"\nC;Y2;X5;K\"60\"\n"
		"C;Y2;X6;K\"0.4\"\nC;Y2;X7;K\"0.25\"\nC;Y2;X8;K\"Bblo\"\n"
		"C;Y3;X1;K\"Afae\"\nC;Y3;X2;K\"Afae\"\nC;Y3;X4;K\"90\"\nC;Y3;X5;K\"60\"\n"
		"C;Y3;X6;K\"4\"\nC;Y3;X8;K\"Bfae\"\n"
		"C;Y4;X1;K\"Arej\"\nC;Y4;X2;K\"Arej\"\nC;Y4;X4;K\"12\"\nC;Y4;X5;K\"12\"\n"
		"C;Y4;X6;K\"400\"\nC;Y4;X8;K\"Brej\"\n"
		"C;Y5;X1;K\"Aroa\"\nC;Y5;X2;K\"Aroa\"\nC;Y5;X4;K\"45\"\nC;Y5;X5;K\"45\"\n"
		"C;Y5;X6;K\"0.25\"\nC;Y5;X8;K\"Broa\"\nC;Y5;X9;K\"500\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	abilityitem_t blo = S_AbilityItem(FS_SLKKey("Ablo"));
	abilityitem_t fae = S_AbilityItem(FS_SLKKey("Afae"));
	abilityitem_t rej = S_AbilityItem(FS_SLKKey("Arej"));
	abilityitem_t roa = S_AbilityItem(FS_SLKKey("Aroa"));
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->armor_value = 2; enemy->armor_value = 6;
	ally->health.value = 60; ally->health.max_value = 100;
	ally->unitinfo.MoveSpeed = 270.0f;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	T_EQ(blo.ability->proc, CAbilityBloodlust); T_ASSERT(blo.ability->flags & AB_AUTOCAST);
	T_EQ(fae.ability->proc, CAbilityFaerieFire); T_ASSERT(fae.ability->flags & AB_AUTOCAST);
	T_EQ(rej.ability->proc, CAbilityRejuvination);
	T_EQ(roa.ability->proc, CAbilityRoar);
	float const normal_move_distance = unit_movedistance(ally);
	test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));
	T_ASSERT(unit_status_can_steal(unit_findstatus(ally, MAKEFOURCC('B','b','l','o'))));
	T_FEQ(S_BloodlustAttackBonus(ally), 0.4f, 0.001f); T_FEQ(S_BloodlustMoveBonus(ally), 0.25f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.4f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.25f, 0.001f);
	T_FEQ(unit_movedistance(ally), normal_move_distance * 1.25f, 0.01f);
	{
		S_AttackProfileWrite(ally, 0)->cooldown = 1.0f;
		S_AttackProfileWrite(ally, 0)->damagePoint = 0.3f;
		attack_melee(ally);
		T_FEQ(unit_movedistance(ally), normal_move_distance * 1.25f, 0.01f);
		T_ASSERT(ally->attack_damage.active);
		T_FEQ(wc3_sub(ally->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f / 1.4f, 0.001f);
	}
	test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e')));
	T_ASSERT(unit_status_can_steal(unit_findstatus(enemy, MAKEFOURCC('B','f','a','e'))));
	T_FEQ(G_UnitArmorValue(enemy), 2.0f, 0.001f);
	test_execute_code(caster, "Arej", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','e','j'))); T_FEQ(S_RejuvHealRate(ally), 400.0f / 12.0f, 0.01f);
	T_EQ(unit_findstatus(ally, MAKEFOURCC('B','r','e','j'))->buff_flags &
		(WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL),
		WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL);
	T_ASSERT(!unit_status_can_steal(unit_findstatus(ally, MAKEFOURCC('B','r','e','j'))));
	test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','o','a'))); T_FEQ(S_RoarDamageBonus(ally), 0.25f, 0.001f);
	T_EQ(unit_findstatus(ally, MAKEFOURCC('B','r','o','a'))->buff_flags &
		(WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL),
		WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, bloodlust_autocast_waits_for_spell_cooldown) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"600\"\n"
		"C;Y2;X7;K\"30\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"Bblo\"\nE\n";
	static UnitAbilities_t abilities = { .abilList = "Ablo" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *ally, *other_ally, *enemy;
	abilityitem_t item;
	abilityCall_t call;
	float mana_after_cast;

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('o','s','h','m'), 0, 0);
	ally = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	other_ally = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
	enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = ally->s.player = other_ally->s.player = 0;
	enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->svflags |= SVF_MONSTER;
	ally->svflags |= SVF_MONSTER;
	other_ally->svflags |= SVF_MONSTER;
	enemy->svflags |= SVF_MONSTER;
	ally->targtype = TARG_GROUND;
	other_ally->targtype = TARG_GROUND;
	enemy->targtype = TARG_GROUND;
	S_AttackProfileWrite(ally, 0)->type = S_AttackProfileWrite(other_ally, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(ally, 0)->range = S_AttackProfileWrite(other_ally, 0)->range = 100.0f;
	S_AttackProfileWrite(ally, 0)->targetsAllowed = S_AttackProfileWrite(other_ally, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
	ally->health.value = ally->health.max_value = 500;
	other_ally->health.value = other_ally->health.max_value = 500;
	item = S_AbilityItem(FS_SLKKey("Ablo"));
	call = MAKE(abilityCall_t, .item = &item);

	T_ASSERT(G_SetUnitAutocast(caster, item.code, true));
	order_attack(ally, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_ASSERT(!S_SpellCooldownReady(caster, item.code));
	mana_after_cast = caster->mana.value;
	T_FEQ(mana_after_cast, 250, 0.001f);
	T_ASSERT(!S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_FEQ(caster->mana.value, mana_after_cast, 0.001f);

	level.time += 10000;
	order_attack(other_ally, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_FEQ(caster->mana.value, mana_after_cast - 50, 0.001f);
	T_ASSERT(S_UnitHasStatus(other_ally, MAKEFOURCC('B','b','l','o')));

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, bloodlust_autocast_skips_unit_already_bloodlusted) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"600\"\n"
		"C;Y2;X7;K\"30\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"Bblo\"\nE\n";
	static UnitAbilities_t abilities = { .abilList = "Ablo" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *buffed, *eligible, *enemy;
	abilityitem_t item;
	abilityCall_t call;

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('o','s','h','m'), 0, 0);
	buffed = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 32, 0);
	eligible = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = buffed->s.player = eligible->s.player = 0;
	enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->svflags |= SVF_MONSTER;
	buffed->svflags |= eligible->svflags |= SVF_MONSTER;
	enemy->svflags |= SVF_MONSTER;
	buffed->targtype = eligible->targtype = TARG_GROUND;
	enemy->targtype = TARG_GROUND;
	S_AttackProfileWrite(eligible, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(eligible, 0)->range = 100.0f;
	S_AttackProfileWrite(eligible, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
	buffed->health.value = buffed->health.max_value = 500;
	eligible->health.value = eligible->health.max_value = 500;
	item = S_AbilityItem(FS_SLKKey("Ablo"));
	call = MAKE(abilityCall_t, .item = &item);

	T_ASSERT(test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = buffed)));
	T_ASSERT(S_UnitHasStatus(buffed, MAKEFOURCC('B','b','l','o')));
	T_ASSERT(G_SetUnitAutocast(caster, item.code, true));
	T_ASSERT(!S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	order_attack(eligible, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_ASSERT(S_UnitHasStatus(eligible, MAKEFOURCC('B','b','l','o')));
	T_FEQ(caster->mana.value, 250, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, frenzy_unholy_frenzy_curse_use_authored_status_and_bonus_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"BuffID1\"\n"
		"C;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"targs\"\n"
		"C;Y2;X1;K\"Afzy\"\nC;Y2;X2;K\"Afzy\"\nC;Y2;X4;K\"20\"\nC;Y2;X5;K\"20\"\n"
		"C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"3\"\nC;Y2;X8;K\"Bfzy\"\n"
		"C;Y3;X1;K\"Auhf\"\nC;Y3;X2;K\"Auhf\"\nC;Y3;X4;K\"15\"\nC;Y3;X5;K\"15\"\n"
		"C;Y3;X6;K\"0.5\"\nC;Y3;X7;K\"2\"\nC;Y3;X8;K\"Buhf\"\n"
		"C;Y4;X1;K\"Acrs\"\nC;Y4;X2;K\"Acrs\"\nC;Y4;X4;K\"60\"\nC;Y4;X5;K\"60\"\n"
		"C;Y4;X6;K\"0.33\"\nC;Y4;X8;K\"Bcrs\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	abilityitem_t fzy = S_AbilityItem(FS_SLKKey("Afzy"));
	abilityitem_t uhf = S_AbilityItem(FS_SLKKey("Auhf"));
	abilityitem_t crs = S_AbilityItem(FS_SLKKey("Acrs"));
	edict_t *curse_effect = NULL;
	uint32_t curse_effect_count = 0;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->armor_value = 5; ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->health.value = ally->health.max_value = 100;
	T_EQ(fzy.ability->proc, CAbilityFrenzy); T_ASSERT(fzy.ability->flags & AB_AUTOCAST);
	T_EQ(uhf.ability->proc, CAbilityUnholyFrenzy); T_ASSERT(!(uhf.ability->flags & AB_AUTOCAST));
	T_EQ(crs.ability->proc, CAbilityCurse); T_ASSERT(crs.ability->flags & AB_AUTOCAST);
	test_execute_code(caster, "Afzy", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','f','z','y')));
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.5f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ARMOR_FLAT), -3.0f, 0.001f);
	T_FEQ(G_UnitArmorValue(ally), 5.0f - 3.0f, 0.001f);
	test_execute_code(caster, "Auhf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','u','h','f')));
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 1.0f, 0.001f);
	T_FEQ(S_UnholyFrenzyLifeDrain(ally), 2.0f, 0.001f);
	test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s'))); T_FEQ(S_CurseMissChance(enemy), 0.33f, 0.001f);
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','c','r','s'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','s') && effect->goalentity == enemy) {
		curse_effect = effect;
		T_EQ(effect->s.model, G_RegisterModel("TestUI\\Models\\anim_pulse.mdx"));
		curse_effect_count++;
	}
	T_EQ(curse_effect_count, 1);
	unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','c','r','s')), STATUS_REMOVE_SCRIPT);
	T_EQ(curse_effect->status_effect_code, 0);

	/* Death keeps timed status state for corpse presentation but ends its art. */
	T_ASSERT(test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy)));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s')));
	unit_die(enemy, caster);
	curse_effect_count = 0;
	FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','s') && effect->goalentity == enemy)
		curse_effect_count++;
	T_EQ(curse_effect_count, 0);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, banshee_curse_blocks_attack_damage_at_authored_miss_chance) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"targs\"\nC;Y1;X8;K\"levels\"\n"
		"C;Y2;X1;K\"Acrs\"\nC;Y2;X2;K\"Acrs\"\nC;Y2;X3;K\"60\"\n"
		"C;Y2;X4;K\"60\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"Bcrs\"\n"
		"C;Y2;X7;K\"ground,enemy,organic,nonhero\"\nC;Y2;X8;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *banshee = make_hero(MAKEFOURCC('u','b','a','n'), 300, 100, 0, 0);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
	banshee->s.player = victim->s.player = 0; attacker->s.player = 1;
	attacker->svflags |= SVF_MONSTER; victim->svflags |= SVF_MONSTER;
	S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL;
	attacker->targtype = victim->targtype = TARG_GROUND;
	attacker->health.value = attacker->health.max_value = 100;
	victim->health.value = victim->health.max_value = 100;

	/* Establish that this attacker damages the victim before Curse is applied. */
	S_ResolveAttackHit(attacker, victim, 25);
	T_ASSERT(victim->health.value < 100);
	victim->health.value = victim->health.max_value = 100;

	T_ASSERT(test_execute_code(banshee, "Acrs",
		MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = attacker)));
	T_ASSERT(S_UnitHasStatus(attacker, MAKEFOURCC('B','c','r','s')));
	T_FEQ(S_CurseMissChance(attacker), 1.0f, 0.001f);
	S_ResolveAttackHit(attacker, victim, 25);
	T_FEQ(victim->health.value, 100, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC omits BuffID; apply the TFT fourcc so Bloodlust/Faerie/Roar/Curse still land. */
TEST(wc3_spell, melee_status_roc_empty_buffid_applies_default_fourcc) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y7;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"targs\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"12\"\nC;Y2;X4;K\"12\"\nC;Y2;X5;K\"0.31\"\nC;Y2;X6;K\"0.17\"\n"
		"C;Y3;X1;K\"Afae\"\nC;Y3;X2;K\"Afae\"\nC;Y3;X3;K\"12\"\nC;Y3;X4;K\"12\"\nC;Y3;X5;K\"3\"\n"
		"C;Y4;X1;K\"Arej\"\nC;Y4;X2;K\"Arej\"\nC;Y4;X3;K\"8\"\nC;Y4;X4;K\"8\"\nC;Y4;X5;K\"80\"\n"
		"C;Y5;X1;K\"Aroa\"\nC;Y5;X2;K\"Aroa\"\nC;Y5;X3;K\"12\"\nC;Y5;X4;K\"12\"\nC;Y5;X5;K\"0.19\"\nC;Y5;X7;K\"400\"\n"
		"C;Y6;X1;K\"Acrs\"\nC;Y6;X2;K\"Acrs\"\nC;Y6;X3;K\"12\"\nC;Y6;X4;K\"12\"\nC;Y6;X5;K\"0.21\"\n"
		"C;Y7;X1;K\"Auhf\"\nC;Y7;X2;K\"Auhf\"\nC;Y7;X3;K\"12\"\nC;Y7;X4;K\"12\"\nC;Y7;X5;K\"0.37\"\nC;Y7;X6;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->health.value = ally->health.max_value = 100;
	test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));
	T_FEQ(S_BloodlustAttackBonus(ally), 0.31f, 0.001f);
	test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e')));
	test_execute_code(caster, "Arej", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','e','j')));
	test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','o','a')));
	test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s')));
	test_execute_code(caster, "Auhf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','U','h','f')));
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.37f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* targs have no allegiance token; TFT BuffID BUhf must drive the attack/drain consumers. */
TEST(wc3_spell, unholy_frenzy_accepts_enemy_and_reads_tft_buffid) {
	/* Retail AbilityData: +75% attack rate, 4 HP/sec drain, 45-second duration. */
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
		"C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"Rng1\"\n"
		"C;Y2;X1;K\"Auhf\"\nC;Y2;X2;K\"Auhf\"\nC;Y2;X3;K\"air,ground,organic\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"45\"\nC;Y2;X6;K\"45\"\n"
		"C;Y2;X7;K\"0.75\"\nC;Y2;X8;K\"4\"\nC;Y2;X9;K\"BUhf\"\nC;Y2;X10;K\"500\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	UnitBalance_t enemy_balance = { .healthRegenType = "none" };
	edict_t *caster = make_hero(MAKEFOURCC('u','n','e','c'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	caster->s.player = 0; enemy->s.player = 1;

	caster->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	caster->targtype = enemy->targtype = TARG_GROUND;
	enemy->data.UnitBalance = &enemy_balance;
	caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','u','h','f'), .level = 1);
	caster->mana.value = caster->mana.max_value = 100;
	enemy->health.value = enemy->health.max_value = 200;
	T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','u','h','f'), enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','U','h','f')));
	T_EQ(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))->duration_ms, 45000u);
	T_EQ(G_UnitStatusBuffCode(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))), MAKEFOURCC('B','u','h','f'));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','U','h','f')),
	        "ReplaceableTextures\\CommandButtons\\BTNUnholyFrenzy.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','U','h','f'), WC3_EFFECT_TARGET, 0),
	        "Abilities\\Spells\\Undead\\UnholyFrenzy\\UnholyFrenzyTarget.mdx");
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	{
		uint32_t target_art_count = 0;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','U','h','f') && effect->goalentity == enemy) {
			T_EQ(effect->s.model, G_RegisterModel("Abilities\\Spells\\Undead\\UnholyFrenzy\\UnholyFrenzyTarget.mdx"));
			target_art_count++;
		}
		T_EQ(target_art_count, 1u);
	}
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.75f, 0.001f);
	T_FEQ(S_UnholyFrenzyLifeDrain(enemy), 4.0f, 0.001f);
	S_AttackProfileWrite(enemy, 0)->cooldown = 1.0f;
	S_AttackProfileWrite(enemy, 0)->damagePoint = 0.3f;
	attack_melee(enemy);
	T_ASSERT(enemy->attack_damage.active);
	T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.3f / 1.75f, 0.001f);
	FOR_LOOP(i, 10) G_RunEntity(enemy);
	T_FEQ(enemy->health.value, 196.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Movement must consume the same authored speed modifiers as selection-group caps. */
TEST(wc3_spell, movement_statuses_change_actual_steps_and_expire) {
    cstring_t slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"2\"\n"
        "C;Y2;X5;K\"0.37\"\nC;Y2;X7;K\"Bcri\"\n"
        "C;Y3;X1;K\"Ablo\"\nC;Y3;X2;K\"Ablo\"\nC;Y3;X3;K\"2\"\nC;Y3;X4;K\"2\"\n"
        "C;Y3;X6;K\"0.17\"\nC;Y3;X7;K\"Bblo\"\nE\n";
    FOR_LOOP(kind,2) FOR_LOOP(cohort,2) {
        reset_entities(); setup_test_world();
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
        /* Both members and the goal must lie inside the native route grid. */
        static uint8_t cells[128*128]; memset(cells,0,sizeof(cells));
        CM_SetupTestPathmap(128,128,cells);
        CM_SetupTestWorldBounds(&(box2_t){.min={-2048,-2048},.max={2048,2048}});
        edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 512, 0);
        unit->s.player = kind == 0 ? 1 : 0;
        unit->svflags |= SVF_MONSTER;
        unit->health.value = unit->health.max_value = 500;
        unit->unitinfo.MoveSpeed = 200;
        unit->collision = 0;
        /* Supply the recovered Footman turn/window profile, absent from this minimal fixture. */
        UnitData_t profile=*unit->data.UnitData; profile.turnRate=0.6f; profile.propWin=60;
        unit->data.UnitData=&profile;
        unit->stand = unit_stand;
        unit_stand(unit);
        test_execute_code(caster, kind == 0 ? "Acri" : "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = unit));
        T_EQ(G_UnitStatusLevel(unit, kind == 0 ? MAKEFOURCC('B','c','r','i') : MAKEFOURCC('B','b','l','o')), 1);
        float step = 10.0f * (kind == 0 ? 126.0f : 234.0f) / FRAMETIME;
        T_FEQ(unit_movedistance(unit), step, 0.001f);
        if (!cohort) {
            T_ASSERT(unit_issueorder(unit, "move", &(vec2_t){1536, 0}));
            M_RunScheduledThinks(); /* Initial owner commits velocity before the first step. */
            vec2_t before = unit->s.origin2;
            wc3_clock_advance(&level.pathing_clock,10.0f/FRAMETIME,0); M_RunScheduledThinks();
            T_FEQ(Vector2_distance(&unit->s.origin2, &before), step, 0.002f);
            level.time+=2001; unit_updatestatuses(unit);
            T_EQ(G_UnitStatusLevel(unit,kind==0 ? MAKEFOURCC('B','c','r','i') : MAKEFOURCC('B','b','l','o')),0);
            T_FEQ(unit_movedistance(unit),20,0.001f);
            M_RunScheduledThinks(); /* Refresh the expired status before advancing its pose. */
            before=unit->s.origin2; wc3_clock_advance(&level.pathing_clock,10.0f/FRAMETIME,0); M_RunScheduledThinks();
            T_FEQ(Vector2_distance(&unit->s.origin2,&before),20,0.002f);
        } else {
            vec2_t before;
            edict_t *peer = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1024, 0);
            edict_t *clent = alloc_test_unit(0, 0, 0);
            clent->client = &game.clients[0]; clent->client->menu.order_queued=false;
            clent->client->ps.number = unit->s.player;
            peer->s.player = unit->s.player;
            peer->svflags |= SVF_MONSTER;
            peer->health.value = peer->health.max_value = 500;
            peer->unitinfo.MoveSpeed = 220;
            peer->data.UnitData=&profile;
            peer->stand = unit_stand;
            unit_stand(peer);
            G_SetEntitySelectionMask(unit, G_SetEntitySelectionMask(peer, 1u << unit->s.player));
            T_ASSERT(move_selectlocation(clent, &(vec2_t){1536, 0}));
            float group_step = kind == 0 ? 12.6f : 22.0f;
            /* Decisions retain each maximum; all-member commits share the active status cap. */
            T_FEQ(unit_movedistance(unit),step,0.001f);
            T_FEQ(unit_movedistance(peer),22,0.001f);
            M_RunScheduledThinks();
            T_FEQ(sqrtf(Vector2_lengthsq(&unit->movement.velocity)),group_step*10,0.001f);
            T_FEQ(sqrtf(Vector2_lengthsq(&peer->movement.velocity)),group_step*10,0.001f);
            before = unit->s.origin2;
            level.pathing_clock.time=wc3_add(level.pathing_clock.time,0.1f);
            M_RunScheduledThinks();
            T_FEQ(Vector2_distance(&unit->s.origin2, &before), group_step, 0.002f);
            level.time += 2001;
            unit_updatestatuses(unit);
            T_EQ(G_UnitStatusLevel(unit, kind == 0 ? MAKEFOURCC('B','c','r','i') : MAKEFOURCC('B','b','l','o')), 0);
            T_FEQ(unit_movedistance(unit), 20.0f, 0.001f);
            M_RunScheduledThinks();
            T_FEQ(sqrtf(Vector2_lengthsq(&unit->movement.velocity)),200,0.001f);
            T_FEQ(sqrtf(Vector2_lengthsq(&peer->movement.velocity)),200,0.001f);
            before = unit->s.origin2;
            level.pathing_clock.time=wc3_add(level.pathing_clock.time,0.1f);
            M_RunScheduledThinks();
            T_FEQ(Vector2_distance(&unit->s.origin2, &before), 20.0f, 0.002f);
        }
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
        reset_entities(); setup_test_world();
    }
}

TEST(wc3_spell, cripple_and_soul_burn_apply_status_and_read_authored_consumers) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
		"C;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"Area1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\nC;Y2;X4;K\"30\"\nC;Y2;X5;K\"15\"\n"
		"C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0.4\"\nC;Y2;X8;K\"Bcri\"\nC;Y2;X9;K\"0.3\"\n"
		"C;Y3;X1;K\"ANso\"\nC;Y3;X2;K\"ANso\"\nC;Y3;X4;K\"10\"\nC;Y3;X5;K\"10\"\n"
		"C;Y3;X6;K\"5\"\nC;Y3;X8;K\"BNso\"\nC;Y3;X9;K\"0.25\"\n"
		"C;Y4;X1;K\"Atau\"\nC;Y4;X2;K\"Atau\"\nC;Y4;X10;K\"400\"\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X13\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"comments\"\n"
		"C;Y1;X4;K\"isEffect\"\nC;Y1;X5;K\"version\"\nC;Y1;X6;K\"useInEditor\"\n"
		"C;Y1;X7;K\"sort\"\nC;Y1;X8;K\"race\"\nC;Y1;X9;K\"Buffart\"\n"
		"C;Y1;X10;K\"Bufftip\"\nC;Y1;X11;K\"Buffubertip\"\nC;Y1;X12;K\"TargetArt\"\n"
		"C;Y1;X13;K\"SpecialArt\"\n"
		"C;Y2;X1;K\"Bcri\"\nC;Y2;X2;K\"Bcri\"\nC;Y2;X9;K\"ReplaceableTextures\\CommandButtons\\BTNCripple.blp\"\n"
		"C;Y2;X12;K\"Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	abilityitem_t cri = S_AbilityItem(FS_SLKKey("Acri"));
	abilityitem_t nso = S_AbilityItem(FS_SLKKey("ANso"));
	abilityitem_t tau = S_AbilityItem(FS_SLKKey("Atau"));
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 500;
	T_EQ(cri.ability->proc, CAbilityCripple);
	T_EQ(nso.ability->proc, CAbilitySoulBurn);
	T_EQ(tau.ability->proc, CAbilityTaunt);
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','c','r','i')),
	        "ReplaceableTextures\\CommandButtons\\BTNCripple.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','c','r','i'), WC3_EFFECT_TARGET, 0),
	        "Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx");
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','c','r','i'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	{
		uint32_t target_art_count = 0;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','i') && effect->goalentity == enemy) {
			T_EQ(effect->s.model, G_RegisterModel("Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx"));
			target_art_count++;
		}
		T_EQ(target_art_count, 1u);
	}
	T_FEQ(S_CrippleMoveReduction(enemy), 0.5f, 0.001f);
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.5f, 0.001f);
	T_FEQ(S_CrippleAttackReduction(enemy), 0.4f, 0.001f);
	T_FEQ(S_CrippleDamageReduction(enemy), 0.3f, 0.001f);
	test_execute_code(caster, "ANso", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','N','s','o')));
	T_FEQ(S_SoulBurnDamageRate(enemy), 5.0f, 0.001f);
	T_FEQ(S_SoulBurnDamageReduction(enemy), 0.25f, 0.001f);
	test_execute_code(caster, "Atau", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
	G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
}

TEST(wc3_spell, cripple_reduces_enemy_movement_attack_rate_and_damage_from_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\n"
		"C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
		"C;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\n"
		"C;Y2;X3;K\"ground,enemy,organic\"\nC;Y2;X4;K\"60\"\nC;Y2;X5;K\"60\"\n"
		"C;Y2;X6;K\"0.75\"\nC;Y2;X7;K\"0.5\"\n"
		"C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"Bcri\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
	float const normal_move_distance = 10.0f * 270.0f / (float)FRAMETIME;
	caster->s.player = victim->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 500;
	enemy->unitinfo.MoveSpeed = 270.0f;
	S_AttackProfileWrite(enemy, 0)->type = ATK_NORMAL;
	S_AttackProfileWrite(enemy, 0)->cooldown = 1.0f;
	S_AttackProfileWrite(enemy, 0)->damagePoint = 0.3f;
	victim->health.value = victim->health.max_value = 200;
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_FEQ(S_CrippleMoveReduction(enemy), 0.75f, 0.001f);
	T_FEQ(S_CrippleAttackReduction(enemy), 0.5f, 0.001f);
	T_FEQ(S_CrippleDamageReduction(enemy), 0.5f, 0.001f);
	T_FEQ(unit_movedistance(enemy), normal_move_distance * 0.25f, 0.01f);
	attack_melee(enemy);
	T_ASSERT(enemy->attack_damage.active);
	T_FEQ(wc3_sub(enemy->attack_damage.deadline.time, G_TimerQueryClock(NULL).time), 0.6f, 0.001f);
	S_ResolveAttackHit(enemy, victim, 40);
	T_FEQ(victim->health.value, 180.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, cripple_roc_empty_buffid_applies_bcri) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\nC;Y2;X3;K\"14\"\nC;Y2;X4;K\"7\"\n"
		"C;Y2;X5;K\"0.41\"\nC;Y2;X6;K\"0.27\"\nC;Y2;X7;K\"0.13\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_FEQ(S_CrippleMoveReduction(enemy), 0.41f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, alias_registrations_resolve_correct_procs) {
	T_EQ(S_AbilityItem(FS_SLKKey("ACtb")).ability->proc, CAbilityThunderBolt);
	T_EQ(S_AbilityItem(FS_SLKKey("Aens")).ability->proc, CAbilityEnsnare);
	T_EQ(S_AbilityItem(FS_SLKKey("Adch")).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(FS_SLKKey("Advm")).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(FS_SLKKey("ACcs")).ability->proc, CAbilityCurse);
	T_EQ(S_AbilityItem(FS_SLKKey("ACcr")).ability->proc, CAbilityCripple);
}

/* Human05 turns each converted villager into a summon, then its JASS trigger
 * owns the final zombie replacement, allegiance, and attack order. */
TEST(wc3_spell, dark_conversion_consumes_target_and_publishes_zombie_summon) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"UnitID1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"SNdc\"\nC;Y2;X2;K\"ANdc\"\n"
        "C;Y2;X3;K\"air,ground,organic,nonhero\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"nzom\"\nC;Y2;X8;K\"BNdc\"\n"
        "C;Y2;X9;K\"6\"\nC;Y2;X10;K\"6\"\nE\n";
    uint32_t const ability = MAKEFOURCC('S','N','d','c');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','C','0','2'), 1000, 1000, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('n','v','i','l'), 64, 0);
    edict_t *summon;
    uint32_t summon_num;

    caster->heroabilities[0] = MAKE(heroability_t, .code = ability, .level = 1);
    caster->s.player = PLAYER_NEUTRAL_PASSIVE;
    target->s.player = 0; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100;
    memset(&level.events, 0, sizeof(level.events));
    summon_num = globals.num_edicts;

    T_NOT_NULL(S_SpellAbilityForCode(ability));
    T_ASSERT(S_AbilityHasCommand(S_SpellAbilityForCode(ability)));
    T_EQ(G_OrderId("darkconversion"), 852228);
    T_ASSERT(unit_issuetargetorder(caster, "darkconversion", target));
    summon = &globals.edicts[summon_num];
    T_ASSERT(!target->inuse);
    T_ASSERT(summon->inuse);
    T_EQ(summon->class_id, MAKEFOURCC('n','z','o','m'));
    T_EQ(summon->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_FEQ(summon->s.origin2.x, 64, 0.001f); T_FEQ(summon->s.origin2.y, 0, 0.001f);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_SUMMON);
    T_ASSERT(level.events.queue[0].edict == caster);
    T_ASSERT(level.events.queue[0].source == summon);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_SUMMON);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, purge_removes_status_applies_slow_and_damages_summoned) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"targs\"\n"
		/* Purge: slow factor 0.5, summoned damage 200, dur 15, buff Bprg */
		"C;Y2;X1;K\"Aprg\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"600\"\n"
		"C;Y2;X4;K\"15\"\nC;Y2;X5;K\"0.5\"\nC;Y2;X6;K\"200\"\n"
		"C;Y2;X7;K\"Bprg\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"air,ground,enemy,neutral\"\n"
		/* Cripple: so we can verify Purge clears it */
		"C;Y3;X1;K\"Acri\"\nC;Y3;X2;K\"Acri\"\nC;Y3;X3;K\"600\"\n"
		"C;Y3;X4;K\"30\"\nC;Y3;X5;K\"0.5\"\nC;Y3;X6;K\"0\"\n"
		"C;Y3;X7;K\"Bcri\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"air,ground,enemy\"\n"
		/* Item Purge: shares CAbilityPurge */
		"C;Y4;X1;K\"AIlp\"\nC;Y4;X2;K\"Aprg\"\nC;Y4;X3;K\"600\"\n"
		"C;Y4;X4;K\"10\"\nC;Y4;X5;K\"0.5\"\nC;Y4;X6;K\"100\"\n"
		"C;Y4;X7;K\"Bprg\"\nC;Y4;X8;K\"0\"\nC;Y4;X9;K\"air,ground,enemy\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('o','s', 'h','m'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER; enemy->health.value = enemy->health.max_value = 500;
	T_EQ(S_AbilityItem(FS_SLKKey("Aprg")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("AIlp")).ability->proc, CAbilityPurge);
	/* Apply Cripple buff, then Purge it away and check Purge slow replaces it. */
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	test_execute_code(caster, "Aprg", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(!S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','p','r','g')));
	T_FEQ(S_PurgeMoveReduction(enemy), 0.5f, 0.001f);
	/* No extra damage when target is not a summoned unit (no owner). */
	T_FEQ(enemy->health.value, 500, 0.001f);
	/* Summoned unit (owner set) takes DataC damage. */
	edict_t *summon = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 300, 0);
	summon->s.player = 1; summon->svflags |= SVF_MONSTER;
	summon->health.value = summon->health.max_value = 500; summon->owner = caster;
	test_execute_code(caster, "Aprg", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = summon));
	T_ASSERT(summon->health.value < 500);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, lightning_shield_damages_nearby_units_each_second) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"Area1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"Rng1\"\n"
		"C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\nC;Y2;X3;K\"air,ground,friend,enemy\"\n"
		"C;Y2;X4;K\"15\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"200\"\n"
		"C;Y2;X7;K\"Blsh\"\nC;Y2;X8;K\"600\"\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
		"C;Y2;X1;K\"Blsh\"\nC;Y2;X2;K\"Blsh\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 300, 0, 0);
	/* Carrier is far from caster (0,0) so caster is outside the 200-unit Area. */
	edict_t *carrier = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 600, 0);  /* 100 from carrier, within Area=200 */
	edict_t *far_unit = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 800, 0);  /* 300 from carrier, beyond Area=200 */
	carrier->s.player = 0; carrier->svflags |= SVF_MONSTER; carrier->targtype = TARG_GROUND;
	carrier->health.value = carrier->health.max_value = 1000;
	nearby->s.player = 1; nearby->svflags |= SVF_MONSTER; nearby->targtype = TARG_GROUND;
	nearby->health.value = nearby->health.max_value = 1000;
	far_unit->s.player = 1; far_unit->svflags |= SVF_MONSTER; far_unit->targtype = TARG_GROUND;
	far_unit->health.value = far_unit->health.max_value = 1000;
	T_EQ(S_AbilityItem(FS_SLKKey("Alsh")).ability->proc, CAbilityLightningShield);
	test_execute_code(caster, "Alsh", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = carrier));
	T_ASSERT(S_UnitHasStatus(carrier, MAKEFOURCC('B','l','s','h')));
	T_NOT_NULL(G_AbilityEffectArt(MAKEFOURCC('B','l','s','h'), WC3_EFFECT_TARGET, 0));
	{
		bool attached_lightning_shield = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','l','s','h') &&
		              effect->goalentity == carrier && effect->damage == carrier->spawn_time)
			attached_lightning_shield = true;
		T_ASSERT(attached_lightning_shield);
	}
	/* Find the thinker and run it — nearby unit takes the initial tick of damage. */
	edict_t *thinker = NULL;
	FILTER_EDICTS(ent, ent->owner == carrier && ent->think == lsh_think) thinker = ent;
	T_NOT_NULL(thinker);
	level.time = 1000; G_RunEntity(thinker);
	T_ASSERT(nearby->health.value < 1000);    /* took damage */
	T_FEQ(far_unit->health.value, 1000, 0.001f);  /* too far, untouched */
	/* Second tick at 2000ms deals another round. */
	float hp = nearby->health.value;
	level.time = 2000; G_RunEntity(thinker);
	T_ASSERT(nearby->health.value < hp);
	/* Past expiry the thinker stops and the carrier no longer has the buff (status expired). */
	level.time = 30000; G_RunEntity(thinker);
	T_ASSERT(!thinker->inuse);
	G_SetSLKRows("AbilityData", old); G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows); free_slk_rows(buff_rows);
}

/* A failed status insertion must not create Lightning Shield's damage thinker. */
TEST(wc3_spell, lightning_shield_full_status_capacity_rejects_cast_and_thinker) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\n"
        "C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\nC;Y2;X3;K\"Blsh\"\n"
        "C;Y2;X4;K\"15\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 0, 0, 0);
    edict_t *carrier = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 300, 0);
    abilityitem_t spell = S_AbilityItem(MAKEFOURCC('A','l','s','h'));
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = carrier);
    carrier->s.player = caster->s.player;
    carrier->svflags |= SVF_MONSTER;
    carrier->targtype = TARG_GROUND;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        char id[5] = { 'B', 'q', (char)('A' + i / 10), (char)('0' + i % 10), 0 };
        T_ASSERT(unit_addtimedstatus(carrier, id, 1, 20.0f));
    }
    T_ASSERT(!CAbilityLightningShield(caster, A_VALIDATE, &(abilityCall_t){ .item = &spell, .target = &target }));
    /* Drive execute directly as well: a delayed impact can race with capacity. */
    T_ASSERT(CAbilityLightningShield(caster, A_EXECUTE, &(abilityCall_t){ .item = &spell, .target = &target }));
    T_NULL(unit_findstatus(carrier, MAKEFOURCC('B','l','s','h')));
    FILTER_EDICTS(ent, ent->owner == carrier && ent->think == lsh_think) T_ASSERT(false);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, lightning_shield_roc_missing_buff_id_uses_compat_fallback) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\n"
        "C;Y2;X3;K\"15\"\nC;Y2;X4;K\"200\"\nC;Y2;X5;K\"25\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    static UnitAbilities_t const abilities = { .abilList = "Alsh" };
    edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 300, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    target->s.player = caster->s.player;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500;
    caster->data.UnitAbilities = &abilities;

#if BZ_COMPAT_WORKAROUNDS
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("Alsh"), target));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','l','s','h')));
#else
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("Alsh"), target));
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','l','s','h')));
#endif

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, nightelf_passive_abilities_are_registered_with_correct_handlers) {
	InitAbilities();
	ability_t const *vengeance = FindAbilityByClassname("Avng");
	ability_t const *moon_glaive = FindAbilityByClassname("Amgl");
	ability_t const *moon_glaive_r = FindAbilityByClassname("Amgr");
	ability_t const *slow_poison = FindAbilityByClassname("Aspo");
	T_NOT_NULL(vengeance); T_NOT_NULL(moon_glaive); T_NOT_NULL(moon_glaive_r); T_NOT_NULL(slow_poison);
	T_EQ(vengeance->proc, CAbilitySpiritOfVengeance); T_ASSERT(vengeance->flags & AB_SPELL);
	T_EQ((int)vengeance->target_type, (int)SPELL_TARGET_NONE);
	T_EQ(moon_glaive->proc, CAbilityMoonGlaive); T_ASSERT(moon_glaive->flags & AB_PASSIVE);
	T_EQ(moon_glaive_r->proc, CAbilityMoonGlaive); T_ASSERT(moon_glaive_r->flags & AB_PASSIVE);
	T_EQ(slow_poison->proc, CAbilitySlowPoison); T_ASSERT(slow_poison->flags & AB_PASSIVE);
}

TEST(wc3_spell, slow_poison_on_hit_applies_buff_and_returns_authored_reduction) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"5\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"0.15\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	level.time = 0;
	attacker->s.player = 0; target->s.player = 1;
	target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
	target->health.value = target->health.max_value = 200.0f;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','s','p','o'), .level = 1);
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.0f, 0.001f);
	S_SlowPoisonOnHit(attacker, target);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.25f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(target), 0.15f, 0.001f);
	{
		bool target_art = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','s','p','o') &&
		              effect->goalentity == target) target_art = true;
		T_ASSERT(target_art);
	}
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, dryad_landed_attack_applies_slow_poison_status) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"targs\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"5\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"0.15\"\nC;Y2;X7;K\"air,ground,organic\"\nE\n";
	static UnitAbilities_t const dryad_abilities = { .abilList = "Aadm,Amim,Aspo,Aien,Ault" };
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker, *target, *missile, *ally, *allied;

	reset_entities(); setup_test_world();
	attacker = alloc_test_unit(MAKEFOURCC('e','d','r','y'), 0, 0);
	attacker->data.UnitAbilities = &dryad_abilities;
	S_AttackProfileWrite(attacker, 0)->type = ATK_PIERCE;
	S_AttackProfileWrite(attacker, 0)->weapon = WPN_MISSILE;
	target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	target->health.value = target->health.max_value = 200.0f;
	target->targtype = TARG_GROUND;
	attacker->s.player = 0; target->s.player = 1;
	attacker->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[2].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	level.alliances[0][2] = 1 << ALLIANCE_PASSIVE;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = target;
	missile->s.origin = target->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;

	missile->projectile_attack_type = ATK_PIERCE;
	missile->projectile_owner_spawn_time = attacker->spawn_time;

	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','s','p','o')),
	        "ReplaceableTextures\\CommandButtons\\BTNSlowPoison.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','s','p','o'), WC3_EFFECT_TARGET, 0),
	        "TestUI\\Models\\anim_pulse.mdx");
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.25f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(target), 0.15f, 0.001f);
	{
		bool target_art = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','s','p','o') &&
		              effect->goalentity == target) target_art = true;
		T_ASSERT(target_art);
	}
	ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
	ally->health.value = ally->health.max_value = 200.0f;
	ally->targtype = TARG_GROUND;
	ally->s.player = attacker->s.player;
	ally->svflags |= SVF_MONSTER;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = ally;
	missile->s.origin = ally->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;

	missile->projectile_attack_type = ATK_PIERCE;
	missile->projectile_owner_spawn_time = attacker->spawn_time;
	T_ASSERT(!S_UnitHasStatus(ally, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','s','p','o')));
	allied = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	allied->health.value = allied->health.max_value = 200.0f;
	allied->targtype = TARG_GROUND;
	allied->s.player = 2;
	allied->svflags |= SVF_MONSTER;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = allied;
	missile->s.origin = allied->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;

	missile->projectile_attack_type = ATK_PIERCE;
	missile->projectile_owner_spawn_time = attacker->spawn_time;
	T_ASSERT(!S_UnitHasStatus(allied, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(allied, MAKEFOURCC('B','s','p','o')));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* HeroDur is 1s vs Dur 10s; DataB/C stay fractions. */
TEST(wc3_spell, slow_poison_uses_herodur_and_fraction_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0.25\"\nE\n";
	static UnitBalance_t hero_bal;
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 50, 0);
	level.time = 1000;
	attacker->s.player = 0; hero->s.player = 1;
	hero->svflags |= SVF_MONSTER; hero->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	memset(&hero_bal, 0, sizeof(hero_bal));
	hero_bal.strength = 20; hero_bal.maxHealth = 400;
	hero->data.UnitBalance = &hero_bal;
	hero->health.value = hero->health.max_value = 400;
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','s','p','o'), .level = 1);
	S_SlowPoisonOnHit(attacker, hero);
	T_ASSERT(S_UnitHasStatus(hero, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(hero), 0.5f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(hero), 0.25f, 0.001f);
	level.time += 1000; unit_updatestatuses(hero);
	T_ASSERT(!S_UnitHasStatus(hero, MAKEFOURCC('B','s','p','o')));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ACvs shares code=Aven, so the Aven row repairs its dispatch like Anhe/ACtc repair Anh1/ACt2. */
TEST(wc3_spell, poison_attacks_share_poison_procedure) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"Aven\"\nC;Y2;X2;K\"Aven\"\n"
		"C;Y3;X1;K\"ACvs\"\nC;Y3;X2;K\"Aven\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','v','e','n')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','p','o','i')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','p','o','2')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','C','v','s')).ability->proc, CAbilityPoisonAttack);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC omits poison BuffID; fallbacks apply the Bpoi/Bpsd and BIpb/BIpd pairs
 * with authored durations. Covers native ownership and a held Apo2 item; the
 * item also carries Aven, so the same poison from both sources applies once. */
TEST(wc3_spell, poison_on_hit_applies_buff_pair_with_authored_duration) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataD1\"\n"
		"C;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"0\"\n"
		"C;Y2;X4;K\"0\"\nC;Y2;X5;K\"6\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"Aven\"\nC;Y3;X2;K\"Aven\"\nC;Y3;X3;K\"7\"\n"
		"C;Y3;X4;K\"2\"\nC;Y3;X5;K\"6\"\nC;Y3;X6;K\"0\"\n"
		"C;Y4;X1;K\"Apo2\"\nC;Y4;X2;K\"Apo2\"\nC;Y4;X3;K\"6\"\n"
		"C;Y4;X4;K\"6\"\nC;Y4;X5;K\"9\"\nC;Y4;X6;K\"1\"\nE\n";
	const char items[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
		"C;Y2;X1;K\"orpv\"\nC;Y2;X2;K\"Aven,Apo2\"\nE\n";
	UnitAbilities_t abilities = { .abilList = "AInv,Aven" };
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *idata = parse_slk_string(items), *olditem = G_SetSLKRows("ItemData", idata);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 300, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 90, 0);
	edict_t *item = alloc_test_unit(MAKEFOURCC('o','r','p','v'), 32, 0);
	level.time = 0;
	attacker->data.UnitAbilities = &abilities; attacker->s.player = 0;
	attacker->svflags |= SVF_MONSTER;
	target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
	hero->s.player = 1; hero->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	item->targtype = TARG_ITEM;
	(item->item ? item->item : (item->item=G_AllocItem()));
	item->item->in_world = true;
	item->item->inventory_slot = -1;
	T_ASSERT(G_AddItemToSlot(attacker, item, 0));
	S_PoisonOnHit(attacker, target);
	S_PoisonOnHit(attacker, hero);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','s','d')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','d')));
	{
		static uint32_t const physical_buffs[] = {
			MAKEFOURCC('B','p','o','i'), MAKEFOURCC('B','p','s','d'),
			MAKEFOURCC('B','I','p','b'), MAKEFOURCC('B','I','p','d')
		};
		FOR_LOOP(i, sizeof(physical_buffs) / sizeof(physical_buffs[0])) {
			heroabilitystatus_t *slot = unit_findstatus(target, physical_buffs[i]);
			T_NOT_NULL(slot);
			T_ASSERT(slot->buff_flags & WC3_STATUS_BUFF_PHYSICAL);
			T_ASSERT(!(slot->buff_flags & WC3_STATUS_BUFF_MAGICAL));
			T_ASSERT(!unit_status_can_magic_dispel(slot));
		}
	}
	T_ASSERT(S_UnitHasStatus(hero, MAKEFOURCC('B','p','o','i')));
	level.time += 3000; unit_updatestatuses(target); unit_updatestatuses(hero);
	T_ASSERT(!S_UnitHasStatus(hero, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	level.time += 5000; unit_updatestatuses(target);
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	G_SetSLKRows("ItemData", olditem); free_slk_rows(idata);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, poison_dataa_ticks_through_entity_scheduler_and_preserves_same_source_phase) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aven\"\nC;Y2;X2;K\"Aven\"\nC;Y2;X3;K\"4\"\n"
        "C;Y2;X4;K\"4\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"Bpoi,Bpsd\"\nE\n";
    static UnitBalance_t target_balance;
    static UnitWeapons_t no_weapons;
    UnitAbilities_t abilities = { .abilList = "Aven" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = alloc_test_unit(MAKEFOURCC('e','a','r','c'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    float before;

    memset(&target_balance, 0, sizeof(target_balance));
    level.time = 0;
    attacker->data.UnitAbilities = &abilities;
    attacker->s.player = 0; attacker->spawn_time = 1;
    target->data.UnitBalance = &target_balance;
    target->data.UnitWeapons = &no_weapons;
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));

    S_ResolveAttackHit(attacker, target, 1);
    before = target->health.value;
    level.time = 999; G_RunEntity(target);
    T_FEQ(target->health.value, before, 0.001f);
    level.time = 1000; G_RunEntity(target);
    T_FEQ(target->health.value, before - 7.0f, 0.001f);

    level.time = 1500; S_ResolveAttackHit(attacker, target, 1);
    level.time = 1999; G_RunEntity(target);
    T_FEQ(target->health.value, before - 8.0f, 0.001f);
    level.time = 2000; G_RunEntity(target);
    T_FEQ(target->health.value, before - 15.0f, 0.001f);

    level.time = 5499; G_RunEntity(target);
    T_FEQ(target->health.value, before - 36.0f, 0.001f);
    level.time = 5500; G_RunEntity(target);
    T_FEQ(target->health.value, before - 36.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, ethereal_secondary_targets_reject_physical_attacks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
        "C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"200\"\n"
        "C;Y3;X1;K\"ANca\"\nC;Y3;X2;K\"ANca\"\nC;Y3;X3;K\"0.4\"\nC;Y3;X4;K\"200\"\n"
        "C;Y4;X1;K\"Asth\"\nC;Y4;X2;K\"Asth\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t codes[] = { MAKEFOURCC('A','m','g','l'), MAKEFOURCC('A','N','c','a'), MAKEFOURCC('A','s','t','h') };
    FOR_LOOP(i, 3) {
        edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
        edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
        edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 60, 0);
        attacker->s.player = 0; primary->s.player = nearby->s.player = 1;
        S_AttackProfileWrite(attacker, 0)->type = ATK_NORMAL;
        S_AttackProfileWrite(attacker, 0)->targetsAllowed = G_TargetFlagForType(TARG_GROUND);
        S_AttackProfileWrite(attacker, 1)->type = ATK_MAGIC;
        S_AttackProfileWrite(attacker, 1)->targetsAllowed = G_TargetFlagForType(TARG_GROUND);
        S_AttackProfileWrite(attacker, 0)->areaFull = S_AttackProfileWrite(attacker, 0)->areaMedium = S_AttackProfileWrite(attacker, 0)->areaSmall = 200;
        primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
        primary->targtype = nearby->targtype = TARG_GROUND;
        ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
        ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
        memset(level.alliances, 0, sizeof(level.alliances));
        primary->health.value = primary->health.max_value = 500;
        nearby->health.value = nearby->health.max_value = 500;
        attacker->heroabilities[0] = MAKE(heroability_t, .code = codes[i], .level = 1);
        heroabilitystatus_t *slot = S_SpellApplyTimedStatus(nearby, "BHbn", 1, 10);
        T_NOT_NULL(slot);
        T_ASSERT(unit_hasstatusstate(nearby, WC3_STATUS_STATE_ETHEREAL));
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 450, 0.001f);
        T_FEQ(nearby->health.value, 500, 0.001f);
        unit_expirestatus(nearby, slot);
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 400, 0.001f);
        T_FEQ(nearby->health.value, i == 0 ? 475 : (i == 1 ? 480 : 450), 0.001f);
        T_NOT_NULL(S_SpellApplyTimedStatus(nearby, "BHbn", 1, 10));
        S_AttackProfileWrite(attacker, 0)->type = ATK_MAGIC;
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 350, 0.001f);
        T_FEQ(nearby->health.value, i == 0 ? 450 : (i == 1 ? 460 : 400), 0.001f);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, moon_glaive_bounces_attack_to_nearby_enemy) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"200\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *far_away = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->s.player = 0;
	primary->s.player = nearby->s.player = far_away->s.player = 1;
	primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER; far_away->svflags |= SVF_MONSTER;
	primary->targtype = nearby->targtype = far_away->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	primary->health.value = primary->health.max_value = 500.0f;
	nearby->health.value = nearby->health.max_value = 500.0f;
	far_away->health.value = far_away->health.max_value = 500.0f;
	primary->die = nearby->die = far_away->die = unit_die;
	primary->s.origin2.x = 0; primary->s.origin2.y = 0;
	nearby->s.origin2.x = 50; nearby->s.origin2.y = 0;
	far_away->s.origin2.x = 5000; far_away->s.origin2.y = 0;
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
	S_MoonGlaiveAttack(attacker, primary, 50);
	T_FEQ(nearby->health.value, 475.0f, 0.001f);
	T_FEQ(far_away->health.value, 500.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock Amgl DataA/Area are 0: one bounce without Remg, 50% damage loss. */
TEST(wc3_spell, moon_glaive_stock_zeros_bounce_inside_attack_range) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *far_away = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->s.player = 0;
	primary->s.player = nearby->s.player = far_away->s.player = 1;
	primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER; far_away->svflags |= SVF_MONSTER;
	primary->targtype = nearby->targtype = far_away->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	primary->health.value = primary->health.max_value = 500.0f;
	nearby->health.value = nearby->health.max_value = 500.0f;
	far_away->health.value = far_away->health.max_value = 500.0f;
	primary->s.origin2.x = 0; primary->s.origin2.y = 0;
	nearby->s.origin2.x = 50; nearby->s.origin2.y = 0;
	far_away->s.origin2.x = 5000; far_away->s.origin2.y = 0;
	S_AttackProfileWrite(attacker, 0)->range = 200;
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
	S_MoonGlaiveAttack(attacker, primary, 50);
	T_FEQ(nearby->health.value, 475.0f, 0.001f);
	T_FEQ(far_away->health.value, 500.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Remg adds a second bounce; the original is always active when the Huntress
 * has Amgl. UnitBalance Upgrades Used and player tech ownership are required. */
TEST(wc3_spell, moon_glaive_research_grants_and_revokes_second_bounce) {
    UnitBalance_t huntress_balance = { .upgrades = "Remg" };
    edict_t *huntress = make_hero(MAKEFOURCC('e','h','u','n'), 500, 0, 0, 0);
    edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 240, 0);
    edict_t *out_of_range = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1000, 0);
    edict_t *targets[] = { primary, first, second, out_of_range };
    huntress->data.UnitBalance = &huntress_balance;
    huntress->s.player = 0;
    huntress->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
    S_AttackProfileWrite(huntress, 0)->weapon = WPN_MBOUNCE;
    S_AttackProfileWrite(huntress, 0)->maxTargets = 2;
    S_AttackProfileWrite(huntress, 0)->damageLoss = 0.5f;
    S_AttackProfileWrite(huntress, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    FOR_LOOP(i, 4) {
        targets[i]->s.player = 1;
        targets[i]->svflags |= SVF_MONSTER;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.value = targets[i]->health.max_value = 500;
    }
    /* Before research, attack 50 -> primary, first bounce 25. */
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 500, 0.001f);
    T_FEQ(out_of_range->health.value, 500, 0.001f);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 1);
    T_EQ(G_MoonGlaiveResearchLevel(huntress), 1);
    first->health.value = second->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 500.0f - 50.0f * 0.5f * 0.5f, 0.001f); /* Fractional second bounce: 12.5. */
    T_FEQ(out_of_range->health.value, 500, 0.001f);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 0);
    T_EQ(G_MoonGlaiveResearchLevel(huntress), 0);
    first->health.value = second->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 500, 0.001f);
}

/* Naisha's Amgr doesn't consume Remg; custom units opt out without the
 * Upgrades Used entry. An authored utc1=1 means no bounce before research. */
TEST(wc3_spell, moon_glaive_custom_targets_and_no_research_variant) {
    UnitBalance_t huntress_balance = { .upgrades = "Remg" };
    edict_t *huntress = make_hero(MAKEFOURCC('e','h','u','n'), 500, 0, 0, 0);
    edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *secondary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    huntress->data.UnitBalance = &huntress_balance;
    huntress->s.player = 0;
    primary->s.player = secondary->s.player = 1;
    primary->svflags |= SVF_MONSTER; secondary->svflags |= SVF_MONSTER;
    primary->targtype = secondary->targtype = TARG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    secondary->health.value = secondary->health.max_value = 500;
    S_AttackProfileWrite(huntress, 0)->maxTargets = 1;
    S_AttackProfileWrite(huntress, 0)->damageLoss = 0.25f;
    huntress->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 500, 0.001f);
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 1);
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 470, 0.001f); /* 25% lost */
    huntress->heroabilities[0].code = MAKEFOURCC('A','m','g','r');
    secondary->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 500, 0.001f); /* Amgr ignores Remg */
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 0);
}

TEST(wc3_spell, healing_ward_records_concrete_summon_ability) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"UnitID1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y2;X1;K\"Ahwd\"\nC;Y2;X2;K\"Ahwd\"\n"
        "C;Y2;X3;K\"7\"\nC;Y2;X4;K\"hfoo\"\nC;Y2;X5;K\"point\"\nE\n";
    uint32_t const code = MAKEFOURCC('A','h','w','d');
    slkTestData_t *rows, *old;
    edict_t *caster, *ward = NULL;
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 128.0f, 64.0f });

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    caster->s.player = 0; caster->svflags |= SVF_MONSTER;
    T_ASSERT(test_execute_code(caster, "Ahwd", target));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster && unit->class_id == MAKEFOURCC('h','f','o','o')) {
        ward = unit; break;
    }
    T_NOT_NULL(ward);
    if (ward) {
        T_EQ(ward->summon_ability, code);
        T_EQ(G_UnitStatusLevel(ward, MAKEFOURCC('B','T','L','F')), 1);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, purge_and_lightning_shield_registration_aliases) {
	T_EQ(S_AbilityItem(FS_SLKKey("Aprg")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("Apg2")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("AIlp")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("Alsh")).ability->proc, CAbilityLightningShield);
	T_EQ(S_AbilityItem(FS_SLKKey("Ahwd")).ability->proc, CAbilityHealingWard);
	T_EQ(S_AbilityItem(FS_SLKKey("Aoar")).ability->proc, CAbilityAuraRegenLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Aabr")).ability->proc, CAbilityAuraRegenLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpb")).ability->proc, CAbilityReplenish);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpl")).ability->proc, CAbilityReplenishLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpm")).ability->proc, CAbilityReplenishMana);
	T_EQ(S_AbilityItem(FS_SLKKey("Arai")).ability->proc, CAbilityRaiseDead);
	T_EQ(S_AbilityItem(FS_SLKKey("AIrd")).ability->proc, CAbilityRaiseDead);
}

TEST(wc3_spell, replenish_restores_health_and_mana_essence_heals_area_spirit_touch_restores_mana) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Rng1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Arpb\"\nC;Y2;X2;K\"Arpb\"\nC;Y2;X3;K\"500\"\n"
		"C;Y2;X5;K\"100\"\nC;Y2;X6;K\"50\"\nC;Y2;X7;K\"Brpb\"\n"
		"C;Y3;X1;K\"Arpl\"\nC;Y3;X2;K\"Arpl\"\nC;Y3;X4;K\"600\"\n"
		"C;Y3;X5;K\"80\"\n"
		"C;Y4;X1;K\"Arpm\"\nC;Y4;X2;K\"Arpm\"\nC;Y4;X4;K\"600\"\n"
		"C;Y4;X6;K\"40\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 50, 0);
	abilityitem_t rpb = S_AbilityItem(FS_SLKKey("Arpb"));
	abilityitem_t rpl = S_AbilityItem(FS_SLKKey("Arpl"));
	abilityitem_t rpm = S_AbilityItem(FS_SLKKey("Arpm"));
	caster->s.player = ally->s.player = 0;
	ally->svflags |= SVF_MONSTER;
	ally->health.value = 60; ally->health.max_value = 200;
	ally->mana.value = 10; ally->mana.max_value = 200;
	T_EQ(rpb.ability->proc, CAbilityReplenish); T_ASSERT(rpb.ability->flags & AB_AUTOCAST);
	T_EQ(rpl.ability->proc, CAbilityReplenishLife); T_ASSERT(rpl.ability->flags & AB_AUTOCAST);
	T_EQ(rpm.ability->proc, CAbilityReplenishMana); T_ASSERT(rpm.ability->flags & AB_AUTOCAST);
	test_execute_code(caster, "Arpb", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_FEQ(ally->health.value, 160.0f, 0.001f);
	T_FEQ(ally->mana.value, 60.0f, 0.001f);
	ally->health.value = 50; ally->health.max_value = 200;
	test_execute_code(caster, "Arpl", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(ally->health.value > 50.0f);
	ally->mana.value = 5; ally->mana.max_value = 200;
	test_execute_code(caster, "Arpm", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(ally->mana.value > 5.0f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, raise_dead_uses_two_authored_summon_groups_and_marks_summons) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\n"
		"C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"UnitID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"30\"\n"
		"C;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\n"
		"C;Y2;X7;K\"uske\"\nC;Y2;X8;K\"hfoo\"\n"
		"C;Y2;X9;K\"Brai\"\nC;Y2;X10;K\"ogru\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	UnitData_t corpse_data = { .deathType = 3 };
	edict_t *warrior = NULL, *second = NULL;
	abilityitem_t rai = S_AbilityItem(FS_SLKKey("Arai"));
	caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
	caster->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0]; game.clients[0].connected = true; game.clients[0].ps.number = 0;
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	T_EQ(rai.ability->proc, CAbilityRaiseDead); T_ASSERT(rai.ability->flags & AB_AUTOCAST);
	T_ASSERT(G_UnitIsRaisableCorpse(corpse));
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(!corpse->inuse);
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai")) {
		if (unit->class_id == MAKEFOURCC('u','s','k','e')) warrior = unit;
		if (unit->class_id == MAKEFOURCC('h','f','o','o')) second = unit;
	}
	T_NOT_NULL(warrior); T_NOT_NULL(second);
	T_EQ(G_UnitStatusLevel(warrior, MAKEFOURCC('B','r','a','i')), 1);
	T_EQ(G_UnitStatusLevel(second, MAKEFOURCC('B','r','a','i')), 1);
	T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("Arai")));
	T_STREQ(G_ResolveCommandErrorText(&game.clients[0], "Cantfindcorpse"), "There are no usable corpses nearby.");
	T_STREQ(game.clients[0].message.text, "");
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, raise_dead_roc_missing_unit_id_spawns_authored_skeleton_count) {
#if BZ_COMPAT_WORKAROUNDS
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\n"
		"C;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"40\"\nC;Y2;X5;K\"2\"\n"
		"C;Y2;X6;K\"-\"\nC;Y2;X7;K\"-\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	static UnitData_t corpse_data = { .deathType = 3 };
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	uint32_t skeleton_count = 0;
	caster->s.player = 0;
	caster->runtime.acquisition_range = 800.0f;
	caster->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0];
	game.clients[0].connected = true;
	game.clients[0].ps.number = 0;
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(!corpse->inuse);
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) {
		heroabilitystatus_t *life = unit_findstatus(unit, MAKEFOURCC('B','T','L','F'));
		T_NOT_NULL(life);
		T_EQ(life->duration_ms, 40000u);
		T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','r','a','i')), 1u);
		skeleton_count++;
	}
	T_EQ(skeleton_count, 2u);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
#else
	T_ASSERT(true); /* This classic-data fallback is intentionally compatibility-gated. */
#endif
}

TEST(wc3_spell, skeletal_longevity_extends_raise_dead_timed_life) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"UnitID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"30\"\n"
		"C;Y2;X5;K\"1\"\nC;Y2;X6;K\"uske\"\n"
		"C;Y2;X7;K\"Brai\"\nC;Y2;X8;K\"ogru\"\nE\n";
	const char upgrade_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"upgradeid\"\nC;Y1;X2;K\"effect1\"\n"
		"C;Y1;X3;K\"base1\"\nC;Y1;X4;K\"mod1\"\n"
		"C;Y1;X5;K\"maxlevel\"\n"
		"C;Y2;X1;K\"Rusl\"\nC;Y2;X2;K\"rrai\"\n"
		"C;Y2;X3;K\"15\"\nC;Y2;X4;K\"0\"\nC;Y2;X5;K\"1\"\nE\n";
	slkTestData_t *ability_rows, *old_ability, *upgrade_rows, *old_upgrade;
	static UnitBalance_t balance = { .upgrades = "Rusl" };
	static UnitData_t corpse_data = { .deathType = 3 };
	edict_t *caster;
	edict_t *corpse, *summon = NULL;
	heroabilitystatus_t *timed_life;
	uint32_t base_duration_ms;

	reset_entities(); setup_test_world();
	ability_rows = parse_slk_string(ability_slk); old_ability = G_SetSLKRows("AbilityData", ability_rows);
	upgrade_rows = parse_slk_string(upgrade_slk); old_upgrade = G_SetSLKRows("UpgradeData", upgrade_rows);
	caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	caster->s.player = 0;
	caster->runtime.acquisition_range = 800.0f;
	caster->data.UnitBalance = &balance;
	caster->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0];
	game.clients[0].connected = true;
	game.clients[0].ps.number = 0;

	corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) summon = unit;
	T_NOT_NULL(summon);
	timed_life = unit_findstatus(summon, MAKEFOURCC('B','T','L','F'));
	T_NOT_NULL(timed_life);
	base_duration_ms = timed_life->duration_ms;
	T_EQ(base_duration_ms, 30000u);

	G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','s','l'), 1);
	corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	summon = NULL;
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) {
		heroabilitystatus_t *status = unit_findstatus(unit, MAKEFOURCC('B','T','L','F'));
		if (status && status->duration_ms == base_duration_ms + 15000u) summon = unit;
	}
	T_NOT_NULL(summon);
	timed_life = unit_findstatus(summon, MAKEFOURCC('B','T','L','F'));
	T_NOT_NULL(timed_life);
	T_EQ(timed_life->duration_ms, base_duration_ms + 15000u);

	G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','s','l'), 0);
	G_SetSLKRows("UpgradeData", old_upgrade); free_slk_rows(upgrade_rows);
	G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}


/* Retail Raise Dead spends the lowest-ranked corpse first even when a stronger
 * corpse is closer, preserving high-value remains for Animate Dead. */
TEST(wc3_spell, raise_dead_prefers_lower_ranked_corpse_before_distance) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\nC;Y2;X3;K\"dead\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"uske\"\n"
        "C;Y2;X7;K\"Brai\"\nC;Y2;X8;K\"1\"\nE\n";
    UnitData_t corpse_data = { .deathType = 3 };
    UnitBalance_t weak_balance = { .level = 1 }, strong_balance = { .level = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
    edict_t *strong = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 25, 0);
    edict_t *weak = alloc_test_unit(MAKEFOURCC('n','s','h','e'), 150, 0);

    caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
    caster->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
    strong->data.UnitData = weak->data.UnitData = &corpse_data;
    strong->data.UnitBalance = &strong_balance; weak->data.UnitBalance = &weak_balance;
    strong->health.value = weak->health.value = 0.0f;
    strong->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    weak->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_ASSERT(strong->inuse); T_ASSERT(!weak->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}


/* Raiu/UnitID is Raise Dead's authored unit type for the fixed retail limit
 * check.  Crossing 25 retires the oldest living summon of that type while the
 * newly raised unit survives. */
TEST(wc3_spell, raise_dead_unitid_limit_check_retires_oldest_summon) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"dead\"\nC;Y2;X5;K\"30\"\nC;Y2;X6;K\"1\"\n"
        "C;Y2;X7;K\"hfoo\"\nC;Y2;X8;K\"Brai\"\nC;Y2;X9;K\"hfoo\"\nE\n";
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
    edict_t *oldest = NULL, *corpse;
    uint32_t living = 0;

    /* make_hero resets the test world, so install the fixture afterward. */
    old = G_SetSLKRows("AbilityData", rows);
    caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
    caster->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
    level.time = 10000;
    FOR_LOOP(i, 25) {
        edict_t *summon = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300.0f + (float)i, 0.0f);
        summon->s.player = 0; summon->svflags |= SVF_MONSTER;
        summon->summon_ability = FS_SLKKey("Arai"); summon->spawn_time = 100 + i;
        if (i == 0) oldest = summon;
    }
    corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100.0f, 0.0f);
    corpse->data.UnitData = &corpse_data; corpse->health.value = 0.0f;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_NOT_NULL(oldest); T_ASSERT(oldest && M_IsDead(oldest)); T_ASSERT(!corpse->inuse);
    FILTER_EDICTS(unit, unit->inuse && !M_IsDead(unit) && unit->s.player == 0 &&
                  unit->class_id == MAKEFOURCC('h','f','o','o') && unit->summon_ability) living++;
    T_EQ(living, 25);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Aast's `player,dead` target contract selects the nearest owned ordinary Tauren and restores its original edict. */
TEST(wc3_spell, ancestral_spirit_revives_nearest_owned_nonhero_tauren) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
		"C;Y2;X1;K\"Aast\"\nC;Y2;X2;K\"Aast\"\nC;Y2;X3;K\"ground,player,dead\"\n"
		"C;Y2;X4;K\"250\"\nC;Y2;X5;K\"30\"\nC;Y2;X6;K\"350\"\nC;Y2;X7;K\"0.4\"\nE\n";
	static UnitBalance_t const tauren = { .foodUsed = 5 }, hero = { .strength = 1 };
	UnitData_t corpse_data = { .deathType = 3 };
	UnitAbilities_t abilities = { .abilList = "Aast" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster = make_hero(MAKEFOURCC('o','s','p','m'), 500, 1000, 0, 0);
	edict_t *zero_health = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 10, 0);
	edict_t *living = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 20, 0);
	edict_t *hero_corpse = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 30, 0);
	edict_t *other_corpse = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 40, 0);
	edict_t *allied_corpse = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 50, 0);
	edict_t *nearest = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 100, 0);
	edict_t *next = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 200, 0);
	edict_t *distant = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 351, 0);
	uint32_t nearest_spawn = nearest->spawn_time;

	old = G_SetSLKRows("AbilityData", rows);
	caster->data.UnitAbilities = &abilities; caster->s.player = 0;
	zero_health->data.UnitBalance = &tauren; zero_health->s.player = 0;
	zero_health->svflags |= SVF_MONSTER; zero_health->health.value = 0; zero_health->health.max_value = 400;
	living->data.UnitBalance = nearest->data.UnitBalance = next->data.UnitBalance = distant->data.UnitBalance = &tauren;
	living->s.player = 0; living->svflags |= SVF_MONSTER;
	living->health.value = living->health.max_value = 400;
	hero_corpse->data.UnitBalance = &hero; other_corpse->data.UnitBalance = &tauren;
	allied_corpse->data.UnitBalance = &tauren;
	edict_t *corpses[] = { hero_corpse, other_corpse, allied_corpse, nearest, next, distant };
	FOR_LOOP(i, sizeof(corpses) / sizeof(*corpses)) {
		corpses[i]->data.UnitData = &corpse_data;
		corpses[i]->s.player = corpses[i] == allied_corpse ? 1 : 0;
		corpses[i]->svflags |= SVF_MONSTER; corpses[i]->stand = unit_stand;
		corpses[i]->health.value = corpses[i]->health.max_value = 400;
		unit_die(corpses[i], caster);
	}
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

	T_EQ(S_AbilityItem(MAKEFOURCC('A','a','s','t')).ability->proc, CAbilityAncestralSpirit);
	T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t')));
	T_EQ(nearest->spawn_time, nearest_spawn); T_EQ(nearest->s.player, 0);
	T_FEQ(nearest->health.value, 160.0f, .001f);
	T_ASSERT(!(nearest->svflags & SVF_DEADMONSTER)); T_ASSERT(!(nearest->s.flags & EF_NOT_SELECTABLE));
	T_EQ(nearest->food->used, 5); T_ASSERT(M_IsDead(next)); T_ASSERT(M_IsDead(allied_corpse));
	S_SpellEndCooldown(caster, MAKEFOURCC('A','a','s','t'));
	T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_ASSERT(!M_IsDead(next));
	S_SpellEndCooldown(caster, MAKEFOURCC('A','a','s','t'));
	float mana = caster->mana.value;
	T_ASSERT(!S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_FEQ(caster->mana.value, mana, .001f);
	level.alliances[0][1] &= ~(1 << ALLIANCE_PASSIVE);
	T_ASSERT(!S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_FEQ(caster->mana.value, mana, .001f);
	T_ASSERT(M_IsDead(zero_health)); T_ASSERT(!(zero_health->svflags & SVF_DEADMONSTER));
	T_ASSERT(!M_IsDead(living)); T_ASSERT(M_IsDead(hero_corpse)); T_ASSERT(M_IsDead(other_corpse));
	T_ASSERT(M_IsDead(allied_corpse)); T_ASSERT(M_IsDead(distant));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_bounces_respect_authored_target_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"air,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *air = alloc_test_unit(MAKEFOURCC('o','w','y','v'), 100, 0);
    edict_t *ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);

    caster->s.player = 0;
    air->s.player = ground->s.player = 1;
    air->targtype = TARG_AIR; ground->targtype = TARG_GROUND;
    air->health.value = air->health.max_value = 500;
    ground->health.value = ground->health.max_value = 500;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = air));
    T_FEQ(air->health.value, 400.0f, 0.001f);
    {
        edict_t *thinker = NULL;
        FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
        T_NOT_NULL(thinker);
        level.time = thinker->freetime;
        chain_lightning_think(thinker);
        T_ASSERT(!thinker->inuse);
    }
    T_FEQ(ground->health.value, 500.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_delays_each_jump_and_never_rehits_previous_targets) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"0.5\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);
    edict_t *third = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 200, 0);
    edict_t *thinker = NULL;

    caster->s.player = 0;
    first->s.player = second->s.player = third->s.player = 1;
    first->targtype = second->targtype = third->targtype = TARG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    first->health.value = first->health.max_value = 500;
    second->health.value = second->health.max_value = 500;
    third->health.value = third->health.max_value = 500;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    third->svflags |= SVF_MONSTER;
    level.time = 0;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_FEQ(third->health.value, 500.0f, 0.001f);
    FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);

    level.time = 249; G_RunEntities();
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_FEQ(third->health.value, 500.0f, 0.001f);

    level.time = 250; G_RunEntities();
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value + third->health.value, 950.0f, 0.001f);
    T_ASSERT(thinker->inuse);

    level.time = 499; G_RunEntities();
    T_FEQ(second->health.value + third->health.value, 950.0f, 0.001f);
    level.time = 500; G_RunEntities();
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value + third->health.value, 925.0f, 0.001f);
    T_ASSERT(!thinker->inuse);

    /* The nearest unvisited unit is second at 250 ms, then third at 500 ms. */
    T_FEQ(second->health.value, 450.0f, 0.001f);
    T_FEQ(third->health.value, 475.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_stops_if_caster_slot_is_reused) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"0.5\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);
    edict_t *thinker = NULL;

    caster->s.player = 0;
    first->s.player = second->s.player = 1;
    first->targtype = second->targtype = TARG_GROUND;
    first->health.value = first->health.max_value = 500;
    second->health.value = second->health.max_value = 500;
    level.time = 100; caster->spawn_time = 100;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
    FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_EQ(thinker->channel->owner_spawn_time, 100);

    /* Reusing the caster edict slot must not let the old delayed cast continue. */
    caster->spawn_time = 101;
    level.time = thinker->freetime;
    chain_lightning_think(thinker);
    T_ASSERT(!thinker->inuse);
    T_FEQ(second->health.value, 500.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, far_sight_reapplies_visibility_until_authored_duration_expires) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"levels\"\nC;Y1;X6;K\"targs\"\n"
        "C;Y2;X1;K\"AOfs\"\nC;Y2;X2;K\"AOfs\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"128\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster; edict_t *thinker = NULL;
    vec2_t point = { 256.0f, 0.0f };
    uint32_t cell;

    reset_entities(); setup_test_world(); G_FowInit(); G_FowConnectPlayer(0);
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    caster->health.value = caster->health.max_value = 500; caster->s.player = 0;
    level.time = 0;
    test_execute_code(caster, "AOfs", MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = point));
    FILTER_EDICTS(ent, ent->think == far_sight_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    cell = G_FowWorldToCellY(point.y) * level.fow.width + G_FowWorldToCellX(point.x);
    T_EQ(level.fow.players[0].visible[cell], 1);

    /* Current visibility is rebuilt each fog update; the active spell reveal is
     * re-applied after that clear, independently of the caster's continued life. */
    caster->inuse = false;
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 1);
    level.time = 1999;
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 1);
    level.time = 2000; far_sight_think(thinker);
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 0);
    T_ASSERT(!thinker->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows); G_FowShutdown();
}

TEST(wc3_spell, earthquake_waits_for_effect_delay_slows_ground_and_damages_structures_and_trees) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,debris,enemy,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"2\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 50, 0);
    edict_t *air = alloc_test_unit(MAKEFOURCC('o','w','y','v'), 60, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 70, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 80, 0);
    edict_t *rooted_ancient = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 90, 0);
    edict_t *uprooted_ancient = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 100, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    ground->s.player = air->s.player = building->s.player = 1;
    rooted_ancient->s.player = uprooted_ancient->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    ground->targtype = TARG_GROUND; ground->unitinfo.MoveSpeed = 300.0f;
    air->targtype = TARG_AIR; building->targtype = TARG_STRUCTURE; building->s.flags |= EF_BUILDING;
    ground->svflags |= SVF_MONSTER; air->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER;
    rooted_ancient->targtype = uprooted_ancient->targtype = TARG_STRUCTURE;
    (rooted_ancient->ancient_root ? rooted_ancient->ancient_root : (rooted_ancient->ancient_root=G_AllocAncientRoot()));
    (uprooted_ancient->ancient_root ? uprooted_ancient->ancient_root : (uprooted_ancient->ancient_root=G_AllocAncientRoot()));
    rooted_ancient->ancient_root->ability = uprooted_ancient->ancient_root->ability = MAKEFOURCC('A','r','o','o');
    rooted_ancient->ancient_root->mode = ANCIENT_ROOTED;
    rooted_ancient->s.flags |= EF_BUILDING;
    uprooted_ancient->ancient_root->mode = ANCIENT_UPROOTED;
    uprooted_ancient->s.flags &= ~EF_BUILDING;
    rooted_ancient->svflags |= SVF_MONSTER; uprooted_ancient->svflags |= SVF_MONSTER;
    ground->health.value = ground->health.max_value = 500;
    air->health.value = air->health.max_value = 500;
    building->health.value = building->health.max_value = 500;
    rooted_ancient->health.value = rooted_ancient->health.max_value = 500;
    uprooted_ancient->health.value = uprooted_ancient->health.max_value = 500;
    (tree->destructable ? tree->destructable : (tree->destructable=G_AllocDestructable()));
    tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 500;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_FEQ(building->health.value, 500.0f, 0.001f);
    T_FEQ(tree->health.value, 500.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(ground, MAKEFOURCC('B','O','e','q')), 0);

    level.time = 1999; earthquake_think(thinker);
    T_FEQ(building->health.value, 500.0f, 0.001f);
    level.time = 2000; earthquake_think(thinker);
    T_FEQ(building->health.value, 490.0f, 0.001f);
    T_FEQ(rooted_ancient->health.value, 490.0f, 0.001f);
    T_FEQ(uprooted_ancient->health.value, 500.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(uprooted_ancient, MAKEFOURCC('B','O','e','q')), 1);
    T_FEQ(tree->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(ground, MAKEFOURCC('B','O','e','q')), 1);
    T_EQ(G_UnitStatusLevel(air, MAKEFOURCC('B','O','e','q')), 0);
    T_FEQ(S_EarthquakeMoveReduction(ground), 0.75f, 0.001f);
    T_FEQ(unit_movedistance(ground), 10.0f * 140.0f / (float)FRAMETIME, 0.001f);

    S_SpellCancelChannel(caster);
    earthquake_think(thinker);
    T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_emits_provisional_random_terrain_pulses_after_effect_delay) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\n"
        "C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\n"
        "C;Y2;X3;K\"ground,structure\"\nC;Y2;X4;K\"4\"\nC;Y2;X5;K\"4\"\n"
        "C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"0.75\"\n"
        "C;Y2;X9;K\"250\"\nC;Y2;X10;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_multicast)(vec3_t const *, multicast_t) = gi.multicast;
    uint32_t first_deformation_id;
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0), *thinker = NULL;
    vec2_t point = { 500, 700 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    terrain_deform_capture_count = terrain_deform_capture_float_count = 0;
    gi.Write = terrain_deform_capture_write; gi.multicast = terrain_deform_capture_multicast;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_EQ(terrain_deform_capture_count, 0);
    level.time = 499; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 0);
    level.time = 500; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 16);
    T_EQ(terrain_deform_capture_types[0], PF_BYTE);
    T_EQ(terrain_deform_capture_types[1], PF_BYTE);
    T_EQ(terrain_deform_capture_types[2], PF_LONG);
    T_EQ(terrain_deform_capture_types[3], PF_BYTE);
    T_EQ(terrain_deform_capture_values[0], svc_temp_entity);
    T_EQ(terrain_deform_capture_values[1], TE_TERRAIN_DEFORM);
    T_ASSERT(terrain_deform_capture_values[2] > 0);
    first_deformation_id = (uint32_t)terrain_deform_capture_values[2];
    T_EQ(terrain_deform_capture_values[3], TERRAIN_DEFORM_RANDOM);
    T_EQ(terrain_deform_capture_float_count, 8);
    T_FEQ(terrain_deform_capture_floats[0], 500, 0.001f);
    T_FEQ(terrain_deform_capture_floats[1], 700, 0.001f);
    T_FEQ(terrain_deform_capture_floats[2], 250, 0.001f);
    T_FEQ(terrain_deform_capture_floats[3], -48, 0.001f);
    T_FEQ(terrain_deform_capture_floats[4], 48, 0.001f);
    T_EQ(terrain_deform_capture_types[12], PF_LONG);
    T_EQ(terrain_deform_capture_types[15], PF_BYTE);
    T_EQ(terrain_deform_capture_values[12], 1000);
    T_EQ(terrain_deform_capture_values[14], 200);
    level.time = 1500; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 32);
    T_EQ(terrain_deform_capture_float_count, 16);
    T_EQ(terrain_deform_capture_values[17], TE_TERRAIN_DEFORM);
    T_ASSERT(terrain_deform_capture_values[18] > 0);
    T_ASSERT((uint32_t)terrain_deform_capture_values[18] != first_deformation_id);
    S_SpellCancelChannel(caster); earthquake_think(thinker);
    T_ASSERT(!thinker->inuse);
    gi.Write = old_write; gi.multicast = old_multicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_respects_authored_relationship_and_destructable_tokens) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,friend,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *friend_ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 50, 0);
    edict_t *friend_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *enemy_ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 70, 0);
    edict_t *enemy_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 80, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 90, 0);
    edict_t *debris = alloc_test_unit(MAKEFOURCC('B','0','0','0'), 100, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    friend_ground->s.player = friend_building->s.player = 0;
    enemy_ground->s.player = enemy_building->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    friend_ground->targtype = enemy_ground->targtype = TARG_GROUND;
    friend_building->targtype = enemy_building->targtype = TARG_STRUCTURE;
    friend_building->s.flags |= EF_BUILDING; enemy_building->s.flags |= EF_BUILDING;
    friend_ground->svflags |= SVF_MONSTER; friend_building->svflags |= SVF_MONSTER;
    enemy_ground->svflags |= SVF_MONSTER; enemy_building->svflags |= SVF_MONSTER;
    friend_ground->health.value = friend_ground->health.max_value = 500;
    friend_building->health.value = friend_building->health.max_value = 500;
    enemy_ground->health.value = enemy_ground->health.max_value = 500;
    enemy_building->health.value = enemy_building->health.max_value = 500;
    (tree->destructable ? tree->destructable : (tree->destructable=G_AllocDestructable()));
    tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
    (debris->destructable ? debris->destructable : (debris->destructable=G_AllocDestructable()));
    debris->svflags &= ~SVF_MONSTER; debris->targtype = TARG_DEBRIS;
    tree->health.value = tree->health.max_value = 500;
    debris->health.value = debris->health.max_value = 500;

    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);

    T_EQ(G_UnitStatusLevel(friend_ground, MAKEFOURCC('B','O','e','q')), 1);
    T_FEQ(friend_building->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(enemy_ground, MAKEFOURCC('B','O','e','q')), 0);
    T_FEQ(enemy_building->health.value, 500.0f, 0.001f);
    T_FEQ(tree->health.value, 490.0f, 0.001f);
    T_FEQ(debris->health.value, 500.0f, 0.001f);

    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_uses_shared_mechanical_target_filter) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,friend,mechanical\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *mechanical = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *organic = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 70, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    mechanical->s.player = organic->s.player = 0;
    mechanical->targtype = TARG_MECHANICAL; organic->targtype = TARG_ORGANIC;
    mechanical->s.flags |= EF_BUILDING; organic->s.flags |= EF_BUILDING;
    mechanical->svflags |= SVF_MONSTER; organic->svflags |= SVF_MONSTER;
    mechanical->health.value = mechanical->health.max_value = 500;
    organic->health.value = organic->health.max_value = 500;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);
    T_FEQ(mechanical->health.value, 490.0f, 0.001f);
    T_FEQ(organic->health.value, 500.0f, 0.001f);
    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The retail mask names no relationship: Earthquake stays enemy-only, and as an area
 * effect it still slows enemies the caster cannot see. */
TEST(wc3_spell, earthquake_retail_mask_is_enemy_only_and_reaches_invisible_units) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,debris,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *friend_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *enemy_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 80, 0);
    edict_t *hidden_enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 70, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    friend_building->s.player = 0;
    enemy_building->s.player = hidden_enemy->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    friend_building->targtype = enemy_building->targtype = TARG_STRUCTURE;
    friend_building->s.flags |= EF_BUILDING; enemy_building->s.flags |= EF_BUILDING;
    hidden_enemy->targtype = TARG_GROUND;
    friend_building->svflags |= SVF_MONSTER; enemy_building->svflags |= SVF_MONSTER;
    hidden_enemy->svflags |= SVF_MONSTER;
    friend_building->health.value = friend_building->health.max_value = 500;
    enemy_building->health.value = enemy_building->health.max_value = 500;
    hidden_enemy->health.value = hidden_enemy->health.max_value = 500;
    hidden_enemy->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;

    level.time = 0;
    T_ASSERT(S_UnitIsInvisibleToPlayer(hidden_enemy, 0));
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);

    /* The caster's own building is the one target the two builds disagree on. */
#ifdef WC3_EARTHQUAKE_FRIENDLY_FIRE
    T_FEQ(friend_building->health.value, 490.0f, 0.001f);
#else
    T_FEQ(friend_building->health.value, 500.0f, 0.001f);
#endif
    T_FEQ(enemy_building->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(hidden_enemy, MAKEFOURCC('B','O','e','q')), 1);

    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Native parent destruction must not leave an owned point task alive with a
 * dangling owner, even when save is consumed before another polling frame. */
TEST(wc3_spell, graph266_point_approach_child_is_released_with_caster) {
    FOR_LOOP(mode,4) {
        edict_t *caster=make_hero(MAKEFOURCC('H','p','a','l'),500,200,288,288);
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
            "C;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Cool1\"\n"
            "C;Y2;X1;K\"AHfs\"\nC;Y2;X2;K\"AHfs\"\nC;Y2;X3;K13\n"
            "C;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataD1\"\n"
            "C;Y1;X8;K\"Data21\"\nC;Y1;X9;K\"Data41\"\n"
            "C;Y2;X4;K96\nC;Y2;X5;K7\nC;Y2;X6;K0.25\nC;Y2;X7;K0.75\n"
            "C;Y2;X8;K0.25\nC;Y2;X9;K0.75\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        UnitAbilities_t abilities={.abilList="AHfs"};caster->data.UnitAbilities=&abilities;
        caster->s.player=0;caster->movetype=MOVETYPE_STEP;caster->unitinfo.MoveSpeed=270;
        T_ASSERT(S_IssuePointTargetSpell(caster,MAKEFOURCC('A','H','f','s'),&(vec2_t){1200,288}));
        edict_t *child=caster->goalentity;
        T_NOT_NULL(child);
        if(!child) {G_SetSLKRows("AbilityData",old);free_slk_rows(rows);continue;}
        T_ASSERT(child!=caster && child->owner==caster && child->goalentity==child);
        T_NULL(child->think);T_ASSERT(S_UnitTargetApproachReceiver(caster)==child);T_NOT_NULL(child->channel);
        edict_t *borrowed=alloc_test_unit(MAKEFOURCC('h','f','o','o'),1500,288);
        if(mode&1) {
            cstring_t file=Test_TempPath("wc3-graph266-live.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            T_ASSERT(caster->goalentity==child && child->owner==caster);
        }
        if(mode&2) {G_DeferFreeEdict(caster);G_TestFinishDeferredFrees();}
        else G_FreeEdict(caster);
        T_ASSERT(!caster->inuse);T_ASSERT(!child->inuse);T_ASSERT(borrowed->inuse);
        /* Child release is idempotent through the next simulation frame, and
         * a save never captures a still-live task referring to freed storage. */
        cstring_t file=Test_TempPath("wc3-graph266-retired.bin");
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
        T_ASSERT(!child->inuse);T_ASSERT(borrowed->inuse);
        level.time+=30;globals.RunFrame();T_ASSERT(!child->inuse);
        level.time+=1000;
        bool reused=false;
        FOR_LOOP(i,4) {
            edict_t *replacement=G_Spawn();T_NOT_NULL(replacement);
            if(replacement==child) {reused=true;T_NULL(replacement->channel);T_NULL(replacement->owner);}
        }
        T_ASSERT(reused);
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

/* Retail S184 accepted and pre-effect samples retain Holy Bolt, independently
 * of its internal d0174 locomotion. Exercise native, alias and command-card
 * admission; the cast owner must survive saves and retire through real Move. */
TEST(wc3_spell, owner267_ground_approach_retains_public_spell_head) {
    FOR_LOOP(entry,3) FOR_LOOP(mode,9) {
        edict_t *caster=make_hero(MAKEFOURCC('H','p','a','l'),500,200,288,288);
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),800,288);
        char const text[]=
            "ID;PWXL;N;EBB;Y3;X8\n"
            "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
            "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
            "C;Y1;X7;K\"Data11\"\nC;Y1;X8;K\"DataA1\"\n"
            "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"ground,friend\"\n"
            "C;Y2;X4;K\"13\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"100\"\n"
            "C;Y2;X7;K\"37\"\nC;Y2;X8;K\"37\"\n"
            "C;Y3;X1;K\"A001\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K\"ground,friend\"\n"
            "C;Y3;X4;K\"13\"\nC;Y3;X5;K\"7\"\nC;Y3;X6;K\"100\"\n"
            "C;Y3;X7;K\"37\"\nC;Y3;X8;K\"37\"\nE\n";
        slkTestData_t *rows=parse_slk_string(text),*old=G_SetSLKRows("AbilityData",rows);
        uint32_t code=entry==1 ? MAKEFOURCC('A','0','0','1') : MAKEFOURCC('A','H','h','b');
        UnitAbilities_t abilities={.abilList=entry==1 ? "A001" : "AHhb"};
        caster->data.UnitAbilities=&abilities;caster->s.player=target->s.player=0;
        caster->collision=32;target->collision=31;caster->unitinfo.MoveSpeed=270;
        caster->movetype=MOVETYPE_STEP;target->svflags|=SVF_MONSTER;target->targtype=TARG_GROUND;
        target->health.value=100;target->health.max_value=1000;
        T_ASSERT(run_test_jass(
            "globals\nunit caster=null\nunit target=null\nendglobals\n"
            "function spellHead takes nothing returns nothing\n"
            "call BJassAssert(GetUnitCurrentOrder(caster)==OrderId(\"holybolt\"),\"spell owns approach\")\nendfunction\n"
            "function issue takes nothing returns nothing\n"
            "call BJassAssert(IssueTargetOrder(caster,\"holybolt\",target),\"accepted target spell\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nlocal unit u=null\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nloop\nset u=FirstOfGroup(g)\nexitwhen u==null\n"
            "if GetUnitTypeId(u)=='Hpal' then\nset caster=u\nelse\nset target=u\nendif\n"
            "call GroupRemoveUnit(g,u)\nendloop\ncall DestroyGroup(g)\nendfunction\n"));
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        /* A replaced Move must not remain visible as the spell's current head. */
        vec2_t point={288,800};T_ASSERT(G_IssueUnitPointOrder(caster,"move",&point,false,0,0));
        T_EQ(caster->current_order_id,G_OrderId("move"));
        if(entry==0) {jass_callbyname(level.vm,"issue",true);jass_runevents(level.vm);}
        if(entry==1)T_ASSERT(S_IssueUnitTargetSpell(caster,code,target));
        if(entry==2) {
            gameClient_t *client=game.clients;edict_t *clent=g_edicts;
            clent->client=client;G_SelectEntity(client,caster);
            cstring_t button[]={"button","AHhb"};char number[16];
            snprintf(number,sizeof(number),"%u",target->s.number);
            cstring_t select[]={"select",number};
            G_ClientCommand(clent,2,button);T_NOT_NULL(client->menu.on_entity_selected);
            G_ClientCommand(clent,2,select);
        }
        T_STREQ(jass_rterror_message(level.vm),"");
        edict_t *child=S_UnitTargetApproachReceiver(caster);T_NOT_NULL(child);
        T_EQ(caster->current_order_id,G_OrderId("holybolt"));
        jass_callbyname(level.vm,"spellHead",true);jass_runevents(level.vm);
        T_STREQ(jass_rterror_message(level.vm),"");
        uint32_t ci=caster-g_edicts,ti=target-g_edicts,pi=child ? child-g_edicts : 0;
        if(mode==1)T_ASSERT(unit_issueimmediateorder(caster,"stop"));
        if(mode==2)T_ASSERT(G_IssueUnitPointOrder(caster,"move",&point,false,0,0));
        if(mode==3)G_FreeEdict(target);
        if(mode==4) {caster->health.value=0;unit_die(caster,NULL);}
        if(mode==5) {
            FOR_LOOP(tick,5) {level.time+=30;globals.RunFrame();}
            cstring_t file=Test_TempPath("wc3-owner267-approach.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            caster=g_edicts+ci;target=g_edicts+ti;child=g_edicts+pi;
            T_EQ(caster->current_order_id,G_OrderId("holybolt"));
            T_ASSERT(S_UnitTargetApproachReceiver(caster)==child);
        }
        if(mode==6) {
            float mana=caster->mana.value;caster->mana.value=0;
            T_ASSERT(!S_IssueUnitTargetSpell(caster,code,target));caster->mana.value=mana;
            T_EQ(caster->current_order_id,G_OrderId("holybolt"));
            T_ASSERT(S_UnitTargetApproachReceiver(caster)==child);
        }
        if(mode==7) {
            T_ASSERT(S_IssueUnitTargetSpell(caster,code,target));T_ASSERT(!child->inuse);
            child=S_UnitTargetApproachReceiver(caster);T_NOT_NULL(child);
            T_EQ(caster->current_order_id,G_OrderId("holybolt"));
        }
        if(mode==8) {
            T_ASSERT(G_IssueUnitPointOrder(caster,"move",&point,true,0,0));
            T_EQ(G_UnitQueuedOrderCount(caster),1);
            T_EQ(caster->current_order_id,G_OrderId("holybolt"));
        }
        float effect=0;
        if(child)FOR_LOOP(tick,200) {
            if(!child->inuse)break;
            T_EQ(caster->current_order_id,G_OrderId("holybolt"));
            float health=target->health.value;
            level.time+=30;globals.RunFrame();
            if(!child->inuse)effect=target->health.value-health;
        }
        if(child)T_ASSERT(!child->inuse);
        /* The applying ability owns its cast animation completion. This
         * fixture supplies that boundary; it does not certify retail cast timing. */
        if((mode==0 || mode>=5) && caster->currentmove && caster->currentmove->endfunc)
            caster->currentmove->endfunc(caster);
        T_EQ(caster->current_order_id,(mode==2 || mode==8) ? G_OrderId("move") : 0);
        if(mode==0 || mode>=5) {T_FEQ(effect,37,0.1f);T_FEQ(caster->mana.value,187,0.1f);}
        else T_FEQ(caster->mana.value,200,0.1f);
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

/* Retail268 point and flying-caster witnesses retain the spell command through
 * the pre-effect approach. Exercise both entry shapes without claiming
 * full effect/channel timing. Payoff280 proves flying approaches also own
 * a physical receiver; preserve the command and cancellation assertions. */
TEST(wc3_spell, owner268_physical_approaches_retain_public_spell_head) {
    FOR_LOOP(shape,2) FOR_LOOP(entry,3) FOR_LOOP(mode,6) {
        edict_t *caster=make_hero(shape ? MAKEFOURCC('e','d','t','m') : MAKEFOURCC('H','b','l','m'),500,200,288,288);
        edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),1500,288);
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
            "C;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\n"
            "C;Y1;X6;K\"targs\"\n"
            "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataD1\"\n"
            "C;Y1;X9;K\"Data21\"\nC;Y1;X10;K\"Data41\"\n"
            "C;Y2;X1;K\"AHfs\"\nC;Y2;X2;K\"AHfs\"\nC;Y2;X3;K13\nC;Y2;X4;K7\nC;Y2;X5;K100\n"
            "C;Y2;X7;K0.25\nC;Y2;X8;K0.75\nC;Y2;X9;K0.25\nC;Y2;X10;K0.75\n"
            "C;Y3;X1;K\"AHhb\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K13\nC;Y3;X4;K7\nC;Y3;X5;K100\nC;Y3;X6;K\"ground,friend\"\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        cstring_t order=shape ? "holybolt" : "flamestrike";
        cstring_t raw=shape ? "AHhb" : "AHfs";uint32_t code=FS_SLKKey(raw);
        UnitAbilities_t abilities={.abilList=raw};caster->data.UnitAbilities=&abilities;
        caster->s.player=target->s.player=0;caster->unitinfo.MoveSpeed=270;
        caster->movetype=shape ? MOVETYPE_FLY : MOVETYPE_STEP;
        if(shape)caster->aiflags|=AI_FLYING;
        caster->collision=32;target->collision=31;target->targtype=TARG_GROUND;
        target->svflags|=SVF_MONSTER;target->health.value=100;target->health.max_value=1000;
        T_ASSERT(run_test_jass(
            "globals\nunit caster=null\nunit target=null\nendglobals\n"
            "function checkPoint takes nothing returns nothing\ncall BJassAssert(GetUnitCurrentOrder(caster)==OrderId(\"flamestrike\"),\"point owns approach\")\nendfunction\n"
            "function checkFly takes nothing returns nothing\ncall BJassAssert(GetUnitCurrentOrder(caster)==OrderId(\"holybolt\"),\"fly owns approach\")\nendfunction\n"
            "function issuePoint takes nothing returns nothing\ncall BJassAssert(IssuePointOrder(caster,\"flamestrike\",1500,288),\"point accepted\")\nendfunction\n"
            "function issueFly takes nothing returns nothing\ncall BJassAssert(IssueTargetOrder(caster,\"holybolt\",target),\"fly accepted\")\nendfunction\n"
            "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nlocal unit u=null\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nloop\nset u=FirstOfGroup(g)\nexitwhen u==null\n"
            "if GetUnitTypeId(u)=='hfoo' then\nset target=u\nelse\nset caster=u\nendif\n"
            "call GroupRemoveUnit(g,u)\nendloop\ncall DestroyGroup(g)\nendfunction\n"));
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        vec2_t point={1500,288},move={288,1500};
        T_ASSERT(G_IssueUnitPointOrder(caster,"move",&move,false,0,0));
        T_EQ(caster->current_order_id,G_OrderId("move"));
        if(entry==0) {jass_callbyname(level.vm,shape ? "issueFly" : "issuePoint",true);jass_runevents(level.vm);T_STREQ(jass_rterror_message(level.vm),"");}
        if(entry==1)T_ASSERT(shape ? S_IssueUnitTargetSpell(caster,code,target) : S_IssuePointTargetSpell(caster,code,&point));
        if(entry==2) {
            edict_t *clent=g_edicts;clent->client=game.clients;G_SelectEntity(clent->client,caster);
            cstring_t button[]={"button",raw};G_ClientCommand(clent,2,button);
            if(shape) {
                char number[16];snprintf(number,sizeof(number),"%u",target->s.number);
                cstring_t select[]={"select",number};G_ClientCommand(clent,2,select);
            } else {cstring_t select[]={"point","1500","288"};G_ClientCommand(clent,3,select);}
        }
        edict_t *child=NULL;
        FILTER_EDICTS(ent,ent->owner==caster && ent->class_id==code) {child=ent;break;}
        T_NOT_NULL(child);
        T_ASSERT(S_UnitTargetApproachReceiver(caster)==child);T_NULL(child->think);
        T_EQ(caster->current_order_id,G_OrderId(order));T_ASSERT(move_is_active_order_walk(caster));
        jass_callbyname(level.vm,shape ? "checkFly" : "checkPoint",true);jass_runevents(level.vm);T_STREQ(jass_rterror_message(level.vm),"");
        FOR_LOOP(tick,5) {level.time+=30;globals.RunFrame();T_EQ(caster->current_order_id,G_OrderId(order));}
        T_FEQ(caster->mana.value,200,0.1f);
        if(mode==4) {
            cstring_t file=Test_TempPath("wc3-owner268-live.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            T_EQ(caster->current_order_id,G_OrderId(order));T_ASSERT(child && child->inuse);
        }
        if(mode==5) {
            caster->mana.value=0;
            T_ASSERT(!(shape ? S_IssueUnitTargetSpell(caster,code,target) : S_IssuePointTargetSpell(caster,code,&point)));
            caster->mana.value=200;T_EQ(caster->current_order_id,G_OrderId(order));T_ASSERT(child && child->inuse);
        }
        if(mode==1)T_ASSERT(G_IssueUnitPointOrder(caster,"move",&move,false,0,0));
        else if(mode==2) {caster->health.value=0;unit_die(caster,NULL);}
        else if(mode==3)G_FreeEdict(caster);
        else T_ASSERT(unit_issueimmediateorder(caster,"stop"));
        level.time+=30;globals.RunFrame();
        T_ASSERT(child && !child->inuse);
        if(mode!=3)T_EQ(caster->current_order_id,mode==1 ? G_OrderId("move") : 0);
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}

/* The initial point predicate includes source collision and predicted pose;
 * physical arrival subsequently uses only the captured authored range. */
TEST(wc3_spell, point269_admission_uses_predicted_collision_edge) {
    FOR_LOOP(entry,3) FOR_LOOP(kind,3) {
        edict_t *caster=make_hero(MAKEFOURCC('H','b','l','m'),500,200,288,288);
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
            "C;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\n"
            "C;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataD1\"\n"
            "C;Y1;X8;K\"Data21\"\nC;Y1;X9;K\"Data41\"\n"
            "C;Y2;X1;K\"AHfs\"\nC;Y2;X2;K\"AHfs\"\nC;Y2;X3;K13\nC;Y2;X4;K7\nC;Y2;X5;K100\n"
            "C;Y2;X6;K0.25\nC;Y2;X7;K0.75\nC;Y2;X8;K0.25\nC;Y2;X9;K0.75\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        UnitAbilities_t abilities={.abilList="AHfs"};caster->data.UnitAbilities=&abilities;
        caster->s.player=0;caster->collision=16;caster->unitinfo.MoveSpeed=300;caster->movetype=MOVETYPE_STEP;
        vec2_t point={kind==0 ? 404 : 420,288};
        if(kind==1) {
            level.pathing_clock=(wc3Clock_t){.time=1};
            caster->movement.pose_clock=(wc3Clock_t){0};caster->movement.clock_valid=true;
            caster->movement.velocity=(vec2_t){32,0};
        }
        if(entry==0)T_ASSERT(G_IssueUnitPointOrder(caster,"flamestrike",&point,false,0,0));
        if(entry==1)T_ASSERT(S_IssuePointTargetSpell(caster,MAKEFOURCC('A','H','f','s'),&point));
        if(entry==2) {
            edict_t *clent=g_edicts;clent->client=game.clients;G_SelectEntity(clent->client,caster);
            cstring_t button[]={"button","AHfs"};G_ClientCommand(clent,2,button);
            cstring_t click[]={"point",kind==0 ? "404" : "420","288"};G_ClientCommand(clent,3,click);
        }
        if(kind<2) {T_NULL(S_UnitTargetApproachReceiver(caster));T_FEQ(caster->mana.value,187,0);}
        else {
            edict_t *receiver=S_UnitTargetApproachReceiver(caster);T_NOT_NULL(receiver);
            T_FEQ(caster->mana.value,200,0);moveGroup_t *group=spell185_group(caster);T_NOT_NULL(group);
            if(group)T_FEQ(group->members[0].arrival_range,3.125f,0);
            T_ASSERT(unit_issueimmediateorder(caster,"stop"));T_ASSERT(receiver && !receiver->inuse);
        }
        reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    }
}


/* Retail point approaches enter SPELL_CHANNEL before resources/effect. Script
 * replacement during the player family owns the successor; unit family keeps
 * the retained ability. Payoff283 captures live target clearing in the unit family. */
extern void (*test_preload_marker)(cstring_t);
static unsigned cast281_channels,cast281_units,cast281_effects,cast281_checked;
static void cast281_marker(cstring_t text) {
    if(!strncmp(text,"C281 failure ",13))T_ASSERT(false);
    if(sscanf(text,"C281 counts %u %u %u",&cast281_channels,&cast281_units,&cast281_effects)==3)cast281_checked++;
}
static void cast281_point_arrival(bool flying, unsigned mode, bool saved) {
        edict_t *caster=make_hero(flying ? MAKEFOURCC('e','d','t','m') : MAKEFOURCC('H','b','l','m'),500,200,288,288);
        g_edicts[0].client=game.clients;game.clients[0].ps.number=0;
        uint8_t cells[64*64];memset(cells,0x40,sizeof(cells));
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
        slkTestData_t *rows=parse_slk_string(
            "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"Cost1\"\n"
            "C;X4;K\"Cool1\"\nC;X5;K\"Rng1\"\nC;X6;K\"DataB1\"\nC;X7;K\"DataD1\"\n"
            "C;X8;K\"Data21\"\nC;X9;K\"Data41\"\n"
            "C;Y2;X1;K\"AHfs\"\nC;X2;K\"AHfs\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\n"
            "C;X6;K0.25\nC;X7;K0.75\nC;X8;K0.25\nC;X9;K0.75\nE\n");
        slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
        UnitAbilities_t abilities={.abilList="AHfs"};caster->data.UnitAbilities=&abilities;
        caster->movetype=flying ? MOVETYPE_FLY : MOVETYPE_STEP;if(flying)caster->aiflags|=AI_FLYING;
        caster->s.model=1;caster->collision=flying ? 8 : 24;caster->targtype=flying ? TARG_AIR : TARG_GROUND;
        caster->unitinfo.MoveSpeed=flying ? 350 : 270;caster->unitinfo.TurnSpeed=.6f;caster->unitinfo.PropWindow=DEG2RAD(60);
        caster->unitinfo.move_flags=BZ_UNIT_SPEED_SET|BZ_UNIT_TURN_SET|BZ_UNIT_WINDOW_SET;
        char script[5000];snprintf(script,sizeof(script),
            "globals\nunit caster=null\ninteger channels=0\ninteger units=0\ninteger effects=0\ntrigger p=null\ntrigger u=null\ntrigger e=null\nendglobals\n"
            "function assert281 takes boolean ok, string text returns nothing\nif not ok then\ncall Preload(\"C281 failure \"+text)\nendif\nendfunction\n"
            "function playerChannel takes nothing returns nothing\nset channels=channels+1\n"
            "call assert281(GetUnitCurrentOrder(caster)==OrderId(\"flamestrike\"),\"channel retains spell head\")\n"
            "call assert281(GetUnitState(caster,ConvertUnitState(2))==200.0,\"channel precedes mana\")\n"
            "call assert281(GetSpellAbilityId()=='AHfs' and GetSpellAbilityUnit()==caster and GetTriggerUnit()==caster,\"player spell source\")\n"
            "call assert281(GetSpellTargetUnit()==null and GetSpellTargetX()==1536.0 and GetSpellTargetY()==288.0,\"player point snapshot\")\n"
            "%s\nendfunction\n"
            "function unitChannel takes nothing returns nothing\nset units=units+1\n"
            "call assert281(channels==1,\"player before unit\")\n"
            "call assert281(GetSpellAbilityId()=='AHfs' and GetSpellAbilityUnit()==caster and GetTriggerUnit()==caster,\"unit spell source\")\n"
            "call assert281(GetSpellTargetUnit()==null and GetSpellTargetX()==%.1f and GetSpellTargetY()==%.1f,\"unit sees live ability target\")\n"
            "endfunction\nfunction effect takes nothing returns nothing\nset effects=effects+1\n"
            "call assert281(channels==1 and units==1,\"effect follows channel families\")\nendfunction\n"
            "function check takes nothing returns nothing\ncall Preload(\"C281 counts \"+I2S(channels)+\" \"+I2S(units)+\" \"+I2S(effects))\ncall DestroyTrigger(p)\ncall DestroyTrigger(u)\ncall DestroyTrigger(e)\nendfunction\n"
            "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nset p=CreateTrigger()\n"
            "set u=CreateTrigger()\nset e=CreateTrigger()\n"
            "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nset caster=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
            "call TriggerRegisterPlayerUnitEvent(p,Player(0),ConvertPlayerUnitEvent(272),null)\ncall TriggerAddAction(p,function playerChannel)\n"
            "call TriggerRegisterUnitEvent(u,caster,ConvertUnitEvent(289))\ncall TriggerAddAction(u,function unitChannel)\n"
            "call TriggerRegisterPlayerUnitEvent(e,Player(0),ConvertPlayerUnitEvent(274),null)\ncall TriggerAddAction(e,function effect)\nendfunction\n",
            mode==1 ? "call IssueImmediateOrder(caster,\"stop\")" : mode==2 ? "call IssuePointOrder(caster,\"move\",288,1536)" :
            mode==3 ? "call RemoveUnit(caster)" : "",
            mode ? 0.0 : 1536.0,mode ? 0.0 : 288.0);
        cast281_channels=cast281_units=cast281_effects=cast281_checked=0;test_preload_marker=cast281_marker;
        T_ASSERT(run_test_jass(script));level.started=level.scriptsConfigured=level.scriptsStarted=true;
        level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;
        level.pathing_msec=level.time=level.pathing_phase=0;
        T_ASSERT(G_IssueUnitPointOrder(caster,"move",&(vec2_t){288,896},false,0,0));
        T_ASSERT(G_IssueUnitPointOrder(caster,"flamestrike",&(vec2_t){1536,288},false,0,0));
        edict_t *receiver=S_UnitTargetApproachReceiver(caster);T_NOT_NULL(receiver);
        while(level.time<11000) {
            level.time+=5;globals.RunFrame();
            if(saved && level.time==100) {
                cstring_t file=Test_TempPath("wc3-cast281.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
                T_EQ(S_UnitTargetApproachReceiver(caster),receiver);
            }
        }
        jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));T_STREQ(jass_rterror_message(level.vm),"");
        T_EQ(cast281_checked,1);T_EQ(cast281_channels,1);T_EQ(cast281_units,1);T_EQ(cast281_effects,mode==0);
        T_ASSERT(receiver && !receiver->inuse);
        if(mode!=3)T_EQ(caster->mana.value,mode==0 ? 187 : 200);
        else T_ASSERT(!caster->inuse);
        if(mode==2) {
            T_ASSERT(Vector2_distance(&caster->s.origin2,&(vec2_t){288,1536})<TILE_SIZE);
            T_EQ(caster->current_order_id,0);
        }
        test_preload_marker=NULL;level.started=false;reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

TEST(wc3_spell, cast281_ground_success_live) { cast281_point_arrival(0,0,0); }
TEST(wc3_spell, cast281_ground_success_saved) { cast281_point_arrival(0,0,1); }
TEST(wc3_spell, cast281_ground_stop_live) { cast281_point_arrival(0,1,0); }
TEST(wc3_spell, cast281_ground_stop_saved) { cast281_point_arrival(0,1,1); }
TEST(wc3_spell, cast281_ground_move_live) { cast281_point_arrival(0,2,0); }
TEST(wc3_spell, cast281_ground_move_saved) { cast281_point_arrival(0,2,1); }
TEST(wc3_spell, cast281_ground_remove_live) { cast281_point_arrival(0,3,0); }
TEST(wc3_spell, cast281_ground_remove_saved) { cast281_point_arrival(0,3,1); }
TEST(wc3_spell, cast281_flying_success_live) { cast281_point_arrival(1,0,0); }
TEST(wc3_spell, cast281_flying_success_saved) { cast281_point_arrival(1,0,1); }
TEST(wc3_spell, cast281_flying_stop_live) { cast281_point_arrival(1,1,0); }
TEST(wc3_spell, cast281_flying_stop_saved) { cast281_point_arrival(1,1,1); }
TEST(wc3_spell, cast281_flying_move_live) { cast281_point_arrival(1,2,0); }
TEST(wc3_spell, cast281_flying_move_saved) { cast281_point_arrival(1,2,1); }
TEST(wc3_spell, cast281_flying_remove_live) { cast281_point_arrival(1,3,0); }
TEST(wc3_spell, cast281_flying_remove_saved) { cast281_point_arrival(1,3,1); }

/* Original40b380 calls both EFFECT families before3f7b00. These tests own
 * that synchronous resource/cancellation boundary; phase delays are separate. */
static unsigned cast282_players,cast282_units,cast282_checked;
static void cast282_marker(cstring_t text) {
    if(!strncmp(text,"C282 failure ",13))T_ASSERT(false);
    if(sscanf(text,"C282 counts %u %u",&cast282_players,&cast282_units)==2)cast282_checked++;
}
static void cast282_effect_boundary(bool unit_target,unsigned mode,bool far,bool saved) {
    edict_t *caster=make_hero(MAKEFOURCC('H','b','l','m'),500,200,288,288);
    g_edicts[0].client=game.clients;game.clients[0].ps.number=0;
    uint8_t cells[64*64];memset(cells,0x40,sizeof(cells));
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"Cost1\"\n"
        "C;X4;K\"Cool1\"\nC;X5;K\"Rng1\"\nC;X6;K\"DataB1\"\nC;X7;K\"DataD1\"\n"
        "C;X8;K\"Data21\"\nC;X9;K\"Data41\"\nC;X10;K\"targs\"\nC;X11;K\"BuffID1\"\nC;X12;K\"Dur1\"\n"
        "C;Y2;X1;K\"AHfs\"\nC;X2;K\"AHfs\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\n"
        "C;X6;K0.25\nC;X7;K0.75\nC;X8;K0.25\nC;X9;K0.75\n"
        "C;Y3;X1;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\n"
        "C;X10;K\"air,ground,friend,self,vuln,invu\"\nC;X11;K\"Binf\"\nC;X12;K30\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    UnitAbilities_t abilities={.abilList=unit_target ? "Ainf" : "AHfs"};caster->data.UnitAbilities=&abilities;
    caster->movetype=MOVETYPE_STEP;caster->s.model=1;caster->collision=24;caster->targtype=TARG_GROUND;
    caster->unitinfo.MoveSpeed=270;caster->unitinfo.TurnSpeed=.6f;caster->unitinfo.PropWindow=DEG2RAD(60);
    caster->unitinfo.move_flags=BZ_UNIT_SPEED_SET|BZ_UNIT_TURN_SET|BZ_UNIT_WINDOW_SET;
    float x=far ? 1536 : 384;
    edict_t *target=unit_target ? alloc_test_unit(MAKEFOURCC('h','f','o','o'),x,288) : NULL;
    if(target){target->svflags|=SVF_MONSTER;target->s.player=0;target->targtype=TARG_GROUND;target->health.value=500;}
    char script[5000];snprintf(script,sizeof(script),
        "globals\nunit caster=null\nunit target=null\ninteger players=0\ninteger units=0\ntrigger p=null\ntrigger u=null\nendglobals\n"
        "function assert282 takes boolean ok,string text returns nothing\nif not ok then\ncall Preload(\"C282 failure \"+text)\nendif\nendfunction\n"
        "function context282 takes nothing returns nothing\n"
        "call assert282(GetSpellAbilityId()=='%s' and GetSpellAbilityUnit()==caster and GetTriggerUnit()==caster,\"spell source\")\n"
        "if players==1 and units==1 then\n"
        "call assert282(GetSpellTargetUnit()==%s and GetSpellTargetX()==%.1f and GetSpellTargetY()==%.1f,\"unit live target\")\n"
        "else\ncall assert282(GetSpellTargetUnit()==target and GetSpellTargetX()==%.1f and GetSpellTargetY()==288.0,\"player target\")\nendif\nendfunction\n"
        "function playerEffect takes nothing returns nothing\nset players=players+1\ncall context282()\n"
        "call assert282(GetUnitState(caster,ConvertUnitState(2))==200.0,\"effect precedes mana\")\n%s\nendfunction\n"
        "function unitEffect takes nothing returns nothing\nset units=units+1\ncall context282()\n"
        "call assert282(players==1,\"player before unit\")\n"
        "call assert282(GetUnitState(caster,ConvertUnitState(2))==%.1f,\"unit sees callback resource mutation\")\nendfunction\n"
        "function check takes nothing returns nothing\ncall Preload(\"C282 counts \"+I2S(players)+\" \"+I2S(units))\n"
        "call DestroyTrigger(p)\ncall DestroyTrigger(u)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nset caster=FirstOfGroup(g)\ncall GroupRemoveUnit(g,caster)\nset target=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
        "set p=CreateTrigger()\nset u=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(p,Player(0),ConvertPlayerUnitEvent(274),null)\ncall TriggerAddAction(p,function playerEffect)\n"
        "call TriggerRegisterUnitEvent(u,caster,ConvertUnitEvent(291))\ncall TriggerAddAction(u,function unitEffect)\nendfunction\n",
        unit_target ? "Ainf" : "AHfs",mode>=1 && mode<=3 ? "null" : "target",
        mode>=1 && mode<=3 ? 0.0 : x,mode>=1 && mode<=3 ? 0.0 : 288.0,x,
        mode==1 ? "call IssueImmediateOrder(caster,\"stop\")" : mode==2 ? "call IssuePointOrder(caster,\"move\",288,1536)" :
        mode==3 ? "call RemoveUnit(caster)" : mode==4 ? "call SetUnitState(caster,ConvertUnitState(2),0)" : "",mode==4 ? 0.0 : 200.0);
    cast282_players=cast282_units=cast282_checked=0;test_preload_marker=cast282_marker;
    T_ASSERT(run_test_jass(script));level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;level.pathing_msec=level.time=level.pathing_phase=0;
    uint32_t code=FS_SLKKey(unit_target ? "Ainf" : "AHfs");
    T_ASSERT(unit_target ? G_IssueUnitTargetOrder(caster,"innerfire",target,false,0) :
        G_IssueUnitPointOrder(caster,"flamestrike",&(vec2_t){x,288},false,0,0));
    edict_t *receiver=S_UnitTargetApproachReceiver(caster);
    if(far)T_NOT_NULL(receiver);
    /* In-range callbacks must have completed before the native returns. */
    if(!far){jass_callbyname(level.vm,"check",false);T_EQ(cast282_players,1);T_EQ(cast282_units,1);}
    while(level.time<11000) {
        level.time+=5;globals.RunFrame();
        if(saved && level.time==100) {
            cstring_t file=Test_TempPath("wc3-cast282.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
            T_EQ(S_UnitTargetApproachReceiver(caster),receiver);
        }
    }
    if(far)jass_callbyname(level.vm,"check",false);
    T_ASSERT(!jass_rterror_pending(level.vm));T_STREQ(jass_rterror_message(level.vm),"");
    T_EQ(cast282_checked,1);T_EQ(cast282_players,1);T_EQ(cast282_units,1);
    if(receiver)T_ASSERT(!receiver->inuse);
    if(mode==3)T_ASSERT(!caster->inuse);
    else {
        T_EQ(caster->mana.value,mode==0 ? 187 : mode==4 ? 0 : 200);
        if(mode!=0 && mode!=4)T_ASSERT(S_SpellCooldownReady(caster,code));
        if(unit_target)T_EQ(G_UnitStatusLevel(target,FS_SLKKey("Binf")),mode==0 || mode==4);
    }
    if(mode==2){T_ASSERT(Vector2_distance(&caster->s.origin2,&(vec2_t){288,1536})<TILE_SIZE);T_EQ(caster->current_order_id,0);}
    test_preload_marker=NULL;level.started=false;reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}
TEST(wc3_spell, cast282_point_near_success) { cast282_effect_boundary(0,0,0,0); }
TEST(wc3_spell, cast282_point_near_stop) { cast282_effect_boundary(0,1,0,0); }
TEST(wc3_spell, cast282_point_near_move) { cast282_effect_boundary(0,2,0,0); }
TEST(wc3_spell, cast282_point_near_remove) { cast282_effect_boundary(0,3,0,0); }
TEST(wc3_spell, cast282_point_near_drain) { cast282_effect_boundary(0,4,0,0); }
TEST(wc3_spell, cast282_point_far_success) { cast282_effect_boundary(0,0,1,0); }
TEST(wc3_spell, cast282_point_far_stop) { cast282_effect_boundary(0,1,1,0); }
TEST(wc3_spell, cast282_point_far_move) { cast282_effect_boundary(0,2,1,0); }
TEST(wc3_spell, cast282_point_far_remove) { cast282_effect_boundary(0,3,1,0); }
TEST(wc3_spell, cast282_point_far_drain) { cast282_effect_boundary(0,4,1,0); }
TEST(wc3_spell, cast282_point_saved_success) { cast282_effect_boundary(0,0,1,1); }
TEST(wc3_spell, cast282_point_saved_stop) { cast282_effect_boundary(0,1,1,1); }
TEST(wc3_spell, cast282_point_saved_move) { cast282_effect_boundary(0,2,1,1); }
TEST(wc3_spell, cast282_point_saved_remove) { cast282_effect_boundary(0,3,1,1); }
TEST(wc3_spell, cast282_point_saved_drain) { cast282_effect_boundary(0,4,1,1); }
TEST(wc3_spell, cast282_unit_near_success) { cast282_effect_boundary(1,0,0,0); }
TEST(wc3_spell, cast282_unit_near_stop) { cast282_effect_boundary(1,1,0,0); }
TEST(wc3_spell, cast282_unit_near_move) { cast282_effect_boundary(1,2,0,0); }
TEST(wc3_spell, cast282_unit_near_remove) { cast282_effect_boundary(1,3,0,0); }
TEST(wc3_spell, cast282_unit_near_drain) { cast282_effect_boundary(1,4,0,0); }
TEST(wc3_spell, cast282_unit_far_success) { cast282_effect_boundary(1,0,1,0); }
TEST(wc3_spell, cast282_unit_far_stop) { cast282_effect_boundary(1,1,1,0); }
TEST(wc3_spell, cast282_unit_far_move) { cast282_effect_boundary(1,2,1,0); }
TEST(wc3_spell, cast282_unit_far_remove) { cast282_effect_boundary(1,3,1,0); }
TEST(wc3_spell, cast282_unit_far_drain) { cast282_effect_boundary(1,4,1,0); }
TEST(wc3_spell, cast282_unit_saved_success) { cast282_effect_boundary(1,0,1,1); }
TEST(wc3_spell, cast282_unit_saved_stop) { cast282_effect_boundary(1,1,1,1); }
TEST(wc3_spell, cast282_unit_saved_move) { cast282_effect_boundary(1,2,1,1); }
TEST(wc3_spell, cast282_unit_saved_remove) { cast282_effect_boundary(1,3,1,1); }
TEST(wc3_spell, cast282_unit_saved_drain) { cast282_effect_boundary(1,4,1,1); }

/* Original425aa0 routes exact primary requests to channel/cast/effect/finish.
 * Public callbacks retain the spell head until ENDCAST; Cast is class-owned. */
static unsigned cast283_counts[2][5];
static float cast283_times[2][5];
static void cast283_marker(cstring_t text) {
    unsigned family,phase;
    if(!strncmp(text,"C283 failure ",13))T_ASSERT(false);
    if(sscanf(text,"C283 event %u %u",&family,&phase)==2 && family<2 && phase<5) {
        cast283_counts[family][phase]++;
        cast283_times[family][phase]=G_TimerQueryClock(jass_getcontext(level.vm)).time;
    }
}
static void cast283_phases(bool unit_target,unsigned mode,bool saved,float castpoint) {
    edict_t *caster=make_hero(MAKEFOURCC('H','b','l','m'),500,200,288,288);
    g_edicts[0].client=game.clients;game.clients[0].ps.number=0;
    UnitData_t data=*caster->data.UnitData;data.castPoint=castpoint;data.castBackSwing=2.4f;
    UnitWeapons_t weapons=*caster->data.UnitWeapons;weapons.castPoint=castpoint;weapons.castBackSwing=2.4f;
    caster->data.UnitData=&data;caster->data.UnitWeapons=&weapons;
    UnitAbilities_t abilities={.abilList=unit_target ? "Ainf" : "AHfs"};caster->data.UnitAbilities=&abilities;
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"Cost1\"\nC;X4;K\"Cool1\"\n"
        "C;X5;K\"Rng1\"\nC;X6;K\"Cast1\"\nC;X7;K\"DataB1\"\nC;X8;K\"DataD1\"\n"
        "C;X9;K\"targs\"\nC;X10;K\"BuffID1\"\nC;X11;K\"Dur1\"\n"
        "C;Y2;X1;K\"AHfs\"\nC;X2;K\"AHfs\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\n"
        "C;X6;K0.2\nC;X7;K0.25\nC;X8;K0.75\n"
        "C;Y3;X1;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\n"
        "C;X6;K0.2\nC;X9;K\"air,ground,friend,self,vuln,invu\"\nC;X10;K\"Binf\"\nC;X11;K30\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    edict_t *target=unit_target ? alloc_test_unit(MAKEFOURCC('h','f','o','o'),384,288) : NULL;
    if(target){target->s.player=0;target->svflags|=SVF_MONSTER;target->targtype=TARG_GROUND;target->health.value=500;}
    caster->collision=24;caster->targtype=TARG_GROUND;
    char script[16000],callbacks[11000]={0};size_t used=0;
    for(unsigned f=0;f<2;f++)for(unsigned q=0;q<5;q++) {
        bool clear=q==3 || (q==4 && (mode==0 || mode==5 || mode==6)) ||
            (f==1 && ((q==1 && mode==1) || (q==2 && mode==2) || (q==0 && mode>=7)));
        char context[600];snprintf(context,sizeof(context),
            "if GetSpellTargetUnit()!=%s or GetSpellTargetX()!=%.1f or GetSpellTargetY()!=%.1f then\ncall Preload(\"C283 failure live target\")\nendif\n",
            !clear && unit_target ? "target" : "null",clear ? 0.0 : 384.0,clear ? 0.0 : 288.0);
        used+=snprintf(callbacks+used,sizeof(callbacks)-used,
            "function cb%u%u takes nothing returns nothing\n"
            "call Preload(\"C283 event %u %u\")\n"
            "if GetSpellAbilityUnit()!=caster or GetTriggerUnit()!=caster or GetSpellAbilityId()!='%s' then\ncall Preload(\"C283 failure source\")\nendif\n"
            "%s%s\nendfunction\n",f,q,f,q,unit_target ? "Ainf" : "AHfs",context,
            f==0 && ((mode==1 && q==1)||(mode==2 && q==2)||(mode==7 && q==0)) ? "call IssueImmediateOrder(caster,\"stop\")" :
            f==0 && mode==8 && q==0 ? "call IssuePointOrder(caster,\"move\",288,1536)" :
            f==0 && mode==9 && q==0 ? "call RemoveUnit(caster)" :
            f==0 && mode==6 && q==2 ? "call SetUnitState(caster,ConvertUnitState(2),0.0)" : "");
    }
    snprintf(script,sizeof(script),
        "globals\nunit caster=null\nunit target=null\ntrigger array t\nendglobals\n%s\n"
        "function mutate takes nothing returns nothing\n%s\nendfunction\n"
        "function main takes nothing returns nothing\nlocal group g=CreateGroup()\n"
        "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nset caster=FirstOfGroup(g)\ncall GroupRemoveUnit(g,caster)\nset target=FirstOfGroup(g)\ncall DestroyGroup(g)\n",
        callbacks, mode==3 ? "call IssueImmediateOrder(caster,\"stop\")" : mode==4 ? "call RemoveUnit(caster)" :
        mode==5 ? "call KillUnit(target)" : "");
    used=strlen(script);
    for(unsigned f=0;f<2;f++)for(unsigned q=0;q<5;q++) {
        used+=snprintf(script+used,sizeof(script)-used,
            "set t[%u]=CreateTrigger()\ncall %s(t[%u],%s,Convert%sEvent(%u)%s)\ncall TriggerAddAction(t[%u],function cb%u%u)\n",
            f*5+q,f ? "TriggerRegisterUnitEvent" : "TriggerRegisterPlayerUnitEvent",f*5+q,
            f ? "caster" : "Player(0)",f ? "Unit" : "PlayerUnit",(f ? 289 : 272)+q,f ? "" : ",null",f*5+q,f,q);
    }
    snprintf(script+used,sizeof(script)-used,"endfunction\nfunction cleanup takes nothing returns nothing\nlocal integer i=0\nloop\nexitwhen i==10\ncall DestroyTrigger(t[i])\nset i=i+1\nendloop\nendfunction\n");
    memset(cast283_counts,0,sizeof(cast283_counts));memset(cast283_times,0,sizeof(cast283_times));
    test_preload_marker=cast283_marker;T_ASSERT(run_test_jass(script));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;level.pathing_msec=level.time=level.pathing_phase=0;
    T_ASSERT(unit_target ? G_IssueUnitTargetOrder(caster,"innerfire",target,false,0) :
        G_IssueUnitPointOrder(caster,"flamestrike",&(vec2_t){384,288},false,0,0));
    T_EQ(cast283_counts[0][0],1);T_EQ(cast283_counts[1][0],1);
    T_EQ(cast283_counts[0][2],0);T_EQ(caster->mana.value,200);
    while(level.time<3500) {
        level.time+=5;globals.RunFrame();
        if(saved && level.time==100) {
            /* ReadGame flushes the discarded live timeline first. These C
             * counters are observers outside saved state; rewind them too. */
            unsigned counts[2][5];float times[2][5];
            memcpy(counts,cast283_counts,sizeof(counts));memcpy(times,cast283_times,sizeof(times));
            cstring_t path=Test_TempPath("wc3-cast283.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
            memcpy(cast283_counts,counts,sizeof(counts));memcpy(cast283_times,times,sizeof(times));
        }
        if(level.time==120 && mode>=3 && mode<=5)jass_callbyname(level.vm,"mutate",false);
    }
    unsigned effect=mode==0 || mode==6;
    unsigned finish=mode==0 || mode==1 || mode==5 || mode==6 || mode>=7;
    FOR_LOOP(f,2) {
        T_EQ(cast283_counts[f][0],1);T_EQ(cast283_counts[f][1],!(mode>=7 || (unit_target && (mode==3 || mode==4 || mode==5))));
        T_EQ(cast283_counts[f][2],effect || mode==2);T_EQ(cast283_counts[f][3],finish);T_EQ(cast283_counts[f][4],1);
        if(effect) {
            T_FEQ(cast283_times[f][1],unit_target ? .2f : 0,1e-6f);
            T_FEQ(cast283_times[f][2],wc3_add(unit_target ? .2f : 0,castpoint),1e-6f);
            T_FEQ(cast283_times[f][3],wc3_add(wc3_add(unit_target ? .2f : 0,castpoint),unit_target ? 2.4f : .2f),1e-6f);
        }
        if(mode==1)T_FEQ(cast283_times[f][3],wc3_add(unit_target ? .2f : 0,castpoint),1e-6f);
    }
    if(mode!=4 && mode!=9)T_EQ(caster->mana.value,mode==6 ? 0 : effect ? 187 : 200);
    if(target)T_EQ(G_UnitStatusLevel(target,FS_SLKKey("Binf")),effect);
    T_ASSERT(!jass_rterror_pending(level.vm));jass_callbyname(level.vm,"cleanup",false);
    test_preload_marker=NULL;level.started=false;reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}
TEST(wc3_spell, cast283_point_success) { cast283_phases(0,0,0,.3f); }
TEST(wc3_spell, cast283_point_success_saved) { cast283_phases(0,0,1,.3f); }
TEST(wc3_spell, cast283_point_cast_stop) { cast283_phases(0,1,0,.3f); }
TEST(wc3_spell, cast283_point_cast_stop_saved) { cast283_phases(0,1,1,.3f); }
TEST(wc3_spell, cast283_point_effect_stop) { cast283_phases(0,2,0,.3f); }
TEST(wc3_spell, cast283_point_effect_stop_saved) { cast283_phases(0,2,1,.3f); }
TEST(wc3_spell, cast283_point_windup_stop) { cast283_phases(0,3,0,.3f); }
TEST(wc3_spell, cast283_point_windup_stop_saved) { cast283_phases(0,3,1,.3f); }
TEST(wc3_spell, cast283_point_windup_remove) { cast283_phases(0,4,0,.3f); }
TEST(wc3_spell, cast283_point_windup_remove_saved) { cast283_phases(0,4,1,.3f); }
TEST(wc3_spell, cast283_point_mana_zero) { cast283_phases(0,6,0,.3f); }
TEST(wc3_spell, cast283_point_mana_zero_saved) { cast283_phases(0,6,1,.3f); }
TEST(wc3_spell, cast283_unit_success) { cast283_phases(1,0,0,.3f); }
TEST(wc3_spell, cast283_unit_success_saved) { cast283_phases(1,0,1,.3f); }
TEST(wc3_spell, cast283_unit_cast_stop) { cast283_phases(1,1,0,.3f); }
TEST(wc3_spell, cast283_unit_cast_stop_saved) { cast283_phases(1,1,1,.3f); }
TEST(wc3_spell, cast283_unit_effect_stop) { cast283_phases(1,2,0,.3f); }
TEST(wc3_spell, cast283_unit_effect_stop_saved) { cast283_phases(1,2,1,.3f); }
TEST(wc3_spell, cast283_unit_windup_stop) { cast283_phases(1,3,0,.3f); }
TEST(wc3_spell, cast283_unit_windup_stop_saved) { cast283_phases(1,3,1,.3f); }
TEST(wc3_spell, cast283_unit_windup_remove) { cast283_phases(1,4,0,.3f); }
TEST(wc3_spell, cast283_unit_windup_remove_saved) { cast283_phases(1,4,1,.3f); }
TEST(wc3_spell, cast283_unit_target_dies) { cast283_phases(1,5,0,.3f); }
TEST(wc3_spell, cast283_unit_target_dies_saved) { cast283_phases(1,5,1,.3f); }
TEST(wc3_spell, cast283_unit_mana_zero) { cast283_phases(1,6,0,.3f); }
TEST(wc3_spell, cast283_unit_mana_zero_saved) { cast283_phases(1,6,1,.3f); }
TEST(wc3_spell, cast283_authored_cast_point) {cast283_phases(1,0,0,.17f);}

TEST(wc3_spell, cast283_point_channel_stop) {cast283_phases(0,7,0,.3f);}
TEST(wc3_spell, cast283_point_channel_move) {cast283_phases(0,8,0,.3f);}
TEST(wc3_spell, cast283_point_channel_remove) {cast283_phases(0,9,0,.3f);}
TEST(wc3_spell, cast283_unit_channel_stop) {cast283_phases(1,7,0,.3f);}
TEST(wc3_spell, cast283_unit_channel_move) {cast283_phases(1,8,0,.3f);}
TEST(wc3_spell, cast283_unit_channel_remove) {cast283_phases(1,9,0,.3f);}
/* Active-record growth, arbitrary heap deletion and stable equal deadlines.
 * This checks the engine's established primary sequence contract, not a new
 * retail tie-order claim. ReadGame must rebuild the same logical schedule. */
static unsigned cast283_bulk_effects,cast283_bulk_finishes;
static void cast283_bulk_marker(cstring_t text) {
    unsigned id;
    if(sscanf(text,"C283 bulk effect %u",&id)==1) {
        T_EQ(id,cast283_bulk_effects*2);cast283_bulk_effects++;
    }
    if(sscanf(text,"C283 bulk finish %u",&id)==1) {
        T_EQ(id,cast283_bulk_finishes*2);cast283_bulk_finishes++;
    }
}
static void cast283_bulk(bool saved) {
    reset_entities();setup_test_world();
    g_edicts[0].client=game.clients;game.clients[0].ps.number=0;
    edict_t *casters[130],*target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),384,288);
    target->s.player=0;target->svflags|=SVF_MONSTER;target->targtype=TARG_GROUND;target->health.value=500;
    UnitAbilities_t abilities={.abilList="Ainf"};
    UnitData_t data={0};UnitWeapons_t weapons={.castPoint=.3f,.castBackSwing=2.4f};
    slkTestData_t *rows=parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"Cost1\"\nC;X4;K\"Cool1\"\n"
        "C;X5;K\"Rng1\"\nC;X6;K\"Cast1\"\nC;X7;K\"targs\"\nC;X8;K\"BuffID1\"\nC;X9;K\"Dur1\"\n"
        "C;Y2;X1;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X3;K13\nC;X4;K7\nC;X5;K333\nC;X6;K0.2\n"
        "C;X7;K\"air,ground,friend,self,vuln,invu\"\nC;X8;K\"Binf\"\nC;X9;K30\nE\n");
    slkTestData_t *old=G_SetSLKRows("AbilityData",rows);
    FOR_LOOP(i,130) {
        casters[i]=alloc_test_unit(MAKEFOURCC('H','b','l','m'),288,288);
        casters[i]->health=(typeof(casters[i]->health)){.value=500,.max_value=500};
        casters[i]->mana=(typeof(casters[i]->mana)){.value=200,.max_value=200};
        casters[i]->svflags|=SVF_MONSTER;casters[i]->stand=unit_stand;casters[i]->movetype=MOVETYPE_NONE;
        unit_stand(casters[i]);
        casters[i]->data.UnitData=&data;casters[i]->data.UnitWeapons=&weapons;casters[i]->data.UnitAbilities=&abilities;
        casters[i]->collision=24;casters[i]->targtype=TARG_GROUND;casters[i]->user_data=i;
    }
    T_ASSERT(run_test_jass(
        "function effect takes nothing returns nothing\ncall Preload(\"C283 bulk effect \"+I2S(GetUnitUserData(GetSpellAbilityUnit())))\nendfunction\n"
        "function finish takes nothing returns nothing\ncall Preload(\"C283 bulk finish \"+I2S(GetUnitUserData(GetSpellAbilityUnit())))\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger e=CreateTrigger()\nlocal trigger f=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(e,Player(0),ConvertPlayerUnitEvent(274),null)\ncall TriggerAddAction(e,function effect)\n"
        "call TriggerRegisterPlayerUnitEvent(f,Player(0),ConvertPlayerUnitEvent(275),null)\ncall TriggerAddAction(f,function finish)\nendfunction\n"));
    cast283_bulk_effects=cast283_bulk_finishes=0;test_preload_marker=cast283_bulk_marker;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;level.pathing_msec=level.time=level.pathing_phase=0;
    FOR_LOOP(i,130)T_ASSERT(G_IssueUnitTargetOrder(casters[i],"innerfire",target,false,0));
    while(level.time<100){level.time+=5;globals.RunFrame();}
    /* Odd slots include root/interior/leaf removals as the heap changes. */
    FOR_LOOP(i,130)if(i&1)T_ASSERT(unit_issueimmediateorder(casters[i],"stop"));
    T_EQ(cast283_bulk_effects,0);T_EQ(cast283_bulk_finishes,0);
    if(saved) {
        cstring_t path=Test_TempPath("wc3-cast283-bulk.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);
        cast283_bulk_effects=cast283_bulk_finishes=0;
    }
    while(level.time<3500){level.time+=5;globals.RunFrame();}
    T_EQ(cast283_bulk_effects,65);T_EQ(cast283_bulk_finishes,65);
    FOR_LOOP(i,130)T_EQ(casters[i]->mana.value,(i&1) ? 200 : 187);
    T_ASSERT(!jass_rterror_pending(level.vm));
    test_preload_marker=NULL;level.started=false;reset_entities();setup_test_world();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}
TEST(wc3_spell, cast283_active_pool_growth_and_deadline_order) {cast283_bulk(false);}
TEST(wc3_spell, cast283_active_pool_growth_and_deadline_order_saved) {cast283_bulk(true);}
/* Retail stock Shadow Strike applies BEsh and halves a333-speed victim at impact. */
TEST(wc3_spell, power349_shadow_strike_applies_owned_movement_status) {
    char const slk[]=
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\nC;X4;K\"DataB1\"\n"
        "C;X5;K\"DataC1\"\nC;X6;K\"DataD1\"\nC;X7;K\"DataE1\"\nC;X8;K\"Dur1\"\n"
        "C;X9;K\"HeroDur1\"\nC;X10;K\"BuffID1\"\nC;X11;K\"Cast1\"\nC;X12;K\"Rng1\"\n"
        "C;X13;K\"Cost1\"\nC;X14;K\"targs\"\n"
        "C;Y2;X1;K\"AEsh\"\nC;X2;K\"AEsh\"\nC;X3;K\"15\"\nC;X4;K\"0.5\"\n"
        "C;X5;K\"0\"\nC;X6;K\"3\"\nC;X7;K\"75\"\nC;X8;K\"15.1\"\n"
        "C;X9;K\"15.1\"\nC;X10;K\"BEsh\"\nC;X11;K\"3\"\nC;X12;K\"300\"\n"
        "C;X13;K\"0\"\nC;X14;K\"ground,air,enemy,organic\"\nE\n";
    reset_entities();setup_test_world();
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    edict_t *caster=alloc_test_unit(MAKEFOURCC('h','f','o','o'),512,768);
    edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),672,768);
    caster->s.player=0;target->s.player=1;caster->svflags|=SVF_MONSTER;target->svflags|=SVF_MONSTER;
    target->health.value=target->health.max_value=5000;
    S_SetUnitMoveSpeed(target,333);level.time=level.pathing_msec=0;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_phase=0;level.pathing_due=false;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    T_ASSERT(test_execute_code(caster,"AEsh",MAKE(spellTarget_t,.type=SPELL_TARGET_UNIT,.entity=target)));
    for(unsigned i=0;i<100;i++){level.time+=5;globals.RunFrame();}
    T_ASSERT(S_UnitHasStatus(target,MAKEFOURCC('B','E','s','h')));
    T_EQ(wc3_float_bits(S_UnitMoveSpeed(target)),0x43268000u);
    heroabilitystatus_t *status=unit_findstatus(target,MAKEFOURCC('B','E','s','h'));
    T_NOT_NULL(status);
    if(status) {
        T_EQ(status->source_ability,MAKEFOURCC('A','E','s','h'));
        T_EQ(status->source,caster);
        unit_removestatus(target,status,STATUS_REMOVE_SCRIPT);
        T_EQ(wc3_float_bits(S_UnitMoveSpeed(target)),0x43a68000u);
    }
    level.started=false;reset_entities();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

/* RoC numeric data columns, custom data, a live missile callback and status
 * replacement/removal all go through production dispatch and the scheduler. */
static void power349_custom_lifecycle(bool roc) {
    reset_entities();setup_test_world();
    char slk[2400];
    char const *rank="1";
    snprintf(slk,sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"Data%s\"\nC;X4;K\"Data%s\"\n"
        "C;X5;K\"Data%s\"\nC;X6;K\"Data%s\"\nC;X7;K\"Data%s\"\nC;X8;K\"Dur%s\"\n"
        "C;X9;K\"HeroDur%s\"\nC;X10;K\"BuffID%s\"\nC;X11;K\"Cast%s\"\nC;X12;K\"Rng%s\"\n"
        "C;X13;K\"Cost%s\"\nC;X14;K\"targs\"\n"
        "C;Y2;X1;K\"ACss\"\nC;X2;K\"AEsh\"\nC;X3;K\"17\"\nC;X4;K\"0.375\"\n"
        "C;X5;K\"0.25\"\nC;X6;K\"0.5\"\nC;X7;K\"37\"\nC;X8;K\"5.7\"\n"
        "C;X9;K\"5.7\"\nC;X10;K\"BX49\"\nC;X11;K\"0.7\"\nC;X12;K\"300\"\n"
        "C;X13;K\"0\"\nC;X14;K\"ground,air,enemy,organic\"\nE\n",
        roc ? "11" : "A1",roc ? "12" : "B1",roc ? "13" : "C1",roc ? "14" : "D1","E1",rank,rank,rank,rank,rank,rank);
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    void (*old_write)(pfWriteType_t,void const *)=gi.Write;void (*old_unicast)(edict_t *)=gi.unicast;
    gi.Write=spell_panel_test_write;gi.unicast=spell_panel_test_unicast;
    edict_t *caster=alloc_test_unit(MAKEFOURCC('h','f','o','o'),512,768);
    edict_t *target=alloc_test_unit(MAKEFOURCC('h','f','o','o'),672,768);
    caster->s.player=0;target->s.player=1;caster->svflags|=SVF_MONSTER;target->svflags|=SVF_MONSTER;
    target->health.value=target->health.max_value=5000;target->die=unit_die;
    S_SetUnitMoveSpeed(target,333);level.time=level.pathing_msec=0;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_phase=0;level.pathing_due=false;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    T_ASSERT(test_execute_code(caster,"ACss",MAKE(spellTarget_t,.type=SPELL_TARGET_UNIT,.entity=target)));
    while(level.time<50){level.time+=5;globals.RunFrame();}
    edict_t *missile=NULL;
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].shadow_strike && !g_edicts[i].shadow_strike->status_id)missile=g_edicts+i;
    T_NOT_NULL(missile);
    cstring_t file=Test_TempPath("wc3-power349-flight.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
    if(missile) {T_NOT_NULL(missile->shadow_strike);T_EQ(missile->currentmove->endfunc,S_ShadowStrikeProjectileHit);}
    while(level.time<500){level.time+=5;globals.RunFrame();}
    T_FEQ(target->health.value,4963,0.5f);
    T_FEQ(S_UnitMoveSpeed(target),208.125f,0.0001f);
    heroabilitystatus_t *status=unit_findstatus(target,MAKEFOURCC('B','X','4','9'));T_NOT_NULL(status);
    T_NOT_NULL(target->shadow_strike);
    while(level.time<1300){level.time+=5;globals.RunFrame();}
    T_FEQ(target->health.value,4946,1.0f);
    if(target->shadow_strike) {T_ASSERT(target->shadow_strike->factor>0);T_ASSERT(target->shadow_strike->factor<1);
        /* Original502600, authored5.7/exponent.5, elapsed3f800008. */
        T_EQ(wc3_float_bits(target->shadow_strike->factor),0x3f687638u);
    }
    T_ASSERT(S_UnitMoveSpeed(target)>208.125f);T_ASSERT(S_UnitMoveSpeed(target)<333);
    /* Native500a60 replaces the BEsh class even when authored BuffID changes. */
    memcpy(strstr(slk,"BX49"),"BY49",4);
    slkTestData_t *replacement=parse_slk_string(slk);
    G_SetSLKRows("AbilityData",replacement);free_slk_rows(rows);rows=replacement;
    T_ASSERT(test_execute_code(caster,"ACss",MAKE(spellTarget_t,.type=SPELL_TARGET_UNIT,.entity=target)));
    while(level.time<1800){level.time+=5;globals.RunFrame();}
    T_FEQ(S_UnitMoveSpeed(target),208.125f,0.0001f);
    status=unit_findstatus(target,MAKEFOURCC('B','Y','4','9'));T_NOT_NULL(status);
    T_ASSERT(!S_UnitHasStatus(target,MAKEFOURCC('B','X','4','9')));
    if(status)unit_removestatus(target,status,STATUS_REMOVE_DISPEL);
    T_NULL(target->shadow_strike);T_FEQ(S_UnitMoveSpeed(target),333,0.0001f);
    float health=target->health.value;
    while(level.time<2600){level.time+=5;globals.RunFrame();}
    T_FEQ(target->health.value,health,0.5f);
    T_ASSERT(test_execute_code(caster,"ACss",MAKE(spellTarget_t,.type=SPELL_TARGET_UNIT,.entity=target)));
    while(level.time<3000){level.time+=5;globals.RunFrame();}
    T_NOT_NULL(target->shadow_strike);G_FreeEdict(caster);
    while(level.time<3500){level.time+=5;globals.RunFrame();}
    T_NOT_NULL(target->shadow_strike); /* Poison outlives its source incarnation. */
    T_Damage(target,NULL,10000);
    T_NULL(target->shadow_strike);T_NULL(target->health_regen);
    while(level.time<4000){level.time+=5;globals.RunFrame();}
    T_EQ(wc3_float_bits(target->health.value),0u);
    T_NULL(target->shadow_strike);
    level.started=false;reset_entities();G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
    gi.Write=old_write;gi.unicast=old_unicast;
}
TEST(wc3_spell,power349_custom_roc_lifecycle){power349_custom_lifecycle(true);}
TEST(wc3_spell,power349_custom_tft_lifecycle){power349_custom_lifecycle(false);}
#endif

#ifdef BZ_TESTS
/*307 candidate owner tests: direct acquisition intentionally isolates the
 * saved scalar/status lifecycle; it is not a replacement for public299 replay. */
TEST(wc3_spell, temporary_invisibility307_pending_release_has_no_suppression_inverse) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    unit->health.value=unit->health.max_value=100;
    T_ASSERT(unit_addtimedstatus(unit,"Binv",1,10));
    heroabilitystatus_t *status=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);if(!status)return;
    S_TemporaryInvisibilityAcquire(unit,status,.5f);
    T_ASSERT(status->invisibility_owned);T_ASSERT(status->invisibility_pending);
    T_ASSERT(!status->invisibility_suppression_held);
    T_ASSERT(!S_UnitHasTemporaryInvisibility(unit,NULL));
    T_ASSERT(unit->permanent_invisibility_fade.request.active);
    uint32_t before=unit->attack_invisibility_suppression;
    S_TemporaryInvisibilityRelease(unit,status);
    T_EQ(unit->attack_invisibility_suppression,before);
    T_EQ(unit->permanent_invisibility_fade.temporary_contributions,0);
    T_ASSERT(!unit->permanent_invisibility_fade.request.active);
}

TEST(wc3_save, temporary_invisibility307_pending_scalar_roundtrip_and_publish) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    unit->health.value=unit->health.max_value=100;
    T_ASSERT(unit_addtimedstatus(unit,"Binv",1,10));
    heroabilitystatus_t *status=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);if(!status)return;
    S_TemporaryInvisibilityAcquire(unit,status,.5f);
    uint32_t index=unit->s.number,instance=status->instance_id;
    wc3Clock_t deadline=unit->permanent_invisibility_fade.request.deadline;
    cstring_t file=Test_TempPath("invisibility307-pending.bin");
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    unit=g_edicts+index;status=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);if(!status)return;
    T_EQ(status->instance_id,instance);T_ASSERT(status->invisibility_pending);
    T_EQ(wc3_float_bits(unit->permanent_invisibility_fade.request.deadline.time),wc3_float_bits(deadline.time));
    T_ASSERT(CAbilityPermanentInvisibility(NULL,A_PRIMARY_TIMER_FIRE,NULL));
    T_ASSERT(unit->permanent_invisibility_fade.published);
    T_ASSERT(!status->invisibility_pending);
    T_ASSERT(S_UnitHasTemporaryInvisibility(unit,NULL));
    S_TemporaryInvisibilityRelease(unit,status);
    T_EQ(unit->permanent_invisibility_fade.temporary_contributions,0);
}

TEST(wc3_spell, invisibility307_authored_positive_fade_refresh_retains_instance) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    cstring_t data_column=atoi(gi.CvarString("fs_expansion","0")) ? "DataA1" : "Data11";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\nC;Y1;X11;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"9\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"Bixx\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\nC;Y2;X11;K\"0.5\"\nE\n", targs_column,data_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target, *enemy;
    heroabilitystatus_t *status;

    setup_test_world();
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER;
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 100.0f;
    enemy->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), target));
    status = unit_findstatus(target, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(status);
    if (status) T_EQ(status->data, MAKEFOURCC('A','i','v','s'));
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));
    T_ASSERT(status && status->invisibility_pending);
    uint32_t instance=status ? status->instance_id : 0;
    wc3Clock_t deadline=target->permanent_invisibility_fade.request.deadline;
    T_ASSERT(S_CastUnitTargetSpell(caster,MAKEFOURCC('A','i','v','s'),target));
    status=unit_findstatus(target,MAKEFOURCC('B','i','x','x'));
    T_ASSERT(status && status->instance_id==instance);
    T_EQ(wc3_float_bits(target->permanent_invisibility_fade.request.deadline.time),wc3_float_bits(deadline.time));
    T_ASSERT(CAbilityPermanentInvisibility(NULL,A_PRIMARY_TIMER_FIRE,NULL));
    T_ASSERT(target->s.renderfx & RF_HIDDEN);
    T_ASSERT(status && !status->invisibility_pending);
    S_ResolveAttackHit(target,enemy,10);
    T_NULL(unit_findstatus(target,MAKEFOURCC('B','i','x','x')));
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, invisibility307_overlap_notifies_immediately_and_active_release_is_counted) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    unit->health.value=unit->health.max_value=100;
    T_ASSERT(unit_addtimedstatus(unit,"Binv",1,10));
    T_ASSERT(unit_addtimedstatus(unit,"Bixx",1,10));
    heroabilitystatus_t *first=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    heroabilitystatus_t *second=unit_findstatus(unit,MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(first);T_NOT_NULL(second);if(!first || !second)return;
    second->data=MAKEFOURCC('A','i','v','s');
    S_TemporaryInvisibilityAcquire(unit,first,.5f);
    wc3Clock_t deadline=unit->permanent_invisibility_fade.request.deadline;
    S_TemporaryInvisibilityAcquire(unit,second,.75f);
    first=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    second=unit_findstatus(unit,MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(first);T_NOT_NULL(second);if(!first || !second)return;
    T_EQ(unit->permanent_invisibility_fade.temporary_contributions,2);
    T_ASSERT(!first->invisibility_pending);T_ASSERT(!second->invisibility_pending);
    T_ASSERT(!unit->permanent_invisibility_fade.published);
    T_EQ(wc3_float_bits(unit->permanent_invisibility_fade.request.deadline.time),wc3_float_bits(deadline.time));
    T_ASSERT(CAbilityPermanentInvisibility(NULL,A_PRIMARY_TIMER_FIRE,NULL));
    T_ASSERT(unit->permanent_invisibility_fade.published);
    S_TemporaryInvisibilityRelease(unit,first);
    T_EQ(unit->permanent_invisibility_fade.temporary_contributions,1);
    T_ASSERT(unit->permanent_invisibility_fade.published);
    /* Remove the manually authored first legacy slot before checking last
     * contribution visibility: it intentionally remains a legacy status. */
    unit_removestatus(unit,first,STATUS_REMOVE_SCRIPT);
    S_TemporaryInvisibilityRelease(unit,second);
    T_EQ(unit->permanent_invisibility_fade.temporary_contributions,0);
    T_ASSERT(!unit->permanent_invisibility_fade.published);
    T_EQ(unit->attack_invisibility_suppression,0);
}

TEST(wc3_spell, invisibility307_old_held_token_does_not_decrement_replacement_attack) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    unit->health.value=unit->health.max_value=100;
    UnitWeapons_t weapons={.attacksEnabled=1};unit->data.UnitWeapons=&weapons;
    G_ActorAddSkill(unit,MAKEFOURCC('A','a','t','k'));
    T_ASSERT(unit_addtimedstatus(unit,"Binv",1,10));
    heroabilitystatus_t *status=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);if(!status)return;
    S_TemporaryInvisibilityAcquire(unit,status,0);
    T_ASSERT(status->invisibility_suppression_held);
    uint32_t old=status->invisibility_attack_generation;
    T_ASSERT(old!=0);T_EQ(unit->attack_invisibility_suppression,1);
    /* Actual owner inverse and reattachment, without altering status lifetime. */
    CAbilityAttack(unit,A_DISABLE,NULL);CAbilityAttack(unit,A_UNIT_INIT,NULL);
    T_ASSERT(unit->attack_invisibility_generation!=old);
    T_EQ(unit->attack_invisibility_suppression,0);
    S_TemporaryInvisibilityRelease(unit,status);
    T_EQ(unit->attack_invisibility_suppression,0);
}

TEST(wc3_save, invisibility307_rejects_invalid_clock_sequence_and_owner_counts) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    unit->health.value=unit->health.max_value=100;
    T_ASSERT(unit_addtimedstatus(unit,"Binv",1,10));
    heroabilitystatus_t *status=unit_findstatus(unit,MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);if(!status)return;
    S_TemporaryInvisibilityAcquire(unit,status,.5f);
    cstring_t file=Test_TempPath("invisibility307-invalid.bin");
    T_ASSERT(S_ValidateInvisibilityOwners());
    abilityPrimaryTimer_t saved=unit->permanent_invisibility_fade.request;
    unit->permanent_invisibility_fade.request.sequence=0;
    T_ASSERT(!S_ValidateInvisibilityOwners());T_ASSERT(!WriteGame(file));
    unit->permanent_invisibility_fade.request=saved;
    unit->permanent_invisibility_fade.request.deadline.time=wc3_float(0x7fc00000);
    T_ASSERT(!S_ValidateInvisibilityOwners());T_ASSERT(!WriteGame(file));
    unit->permanent_invisibility_fade.request=saved;
    unit->permanent_invisibility_fade.temporary_contributions++;
    T_ASSERT(!S_ValidateInvisibilityOwners());T_ASSERT(!WriteGame(file));
    unit->permanent_invisibility_fade.temporary_contributions--;
    uint32_t code=status->code;
    status->code=MAKEFOURCC('B','i','n','f');
    T_ASSERT(!S_ValidateInvisibilityOwners());T_ASSERT(!WriteGame(file));
    status->code=code;
    unit->permanent_invisibility_fade.request.active=false;
    unit->permanent_invisibility_fade.published=true;
    T_ASSERT(!S_ValidateInvisibilityOwners());T_ASSERT(!WriteGame(file));
    unit->permanent_invisibility_fade.published=false;
    unit->permanent_invisibility_fade.request=saved;
    T_ASSERT(S_ValidateInvisibilityOwners());
    S_TemporaryInvisibilityRelease(unit,status);remove(file);
}

#endif /* BZ_TESTS: invisibility307 candidate */
