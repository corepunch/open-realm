#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
#include "../skills/s_skills.h"
#include "shared/test.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
bool run_test_jass(cstring_t src);
void reset_entities(void);
void setup_test_pathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void setup_test_world(void);

static UnitAbilities_t const bot_harvester_abilities = { .abilList = "Ahar" };
static UnitAbilities_t const bot_repair_abilities = { .abilList = "Arep" };
static UnitAbilities_t const bot_hall_abilities = { .abilList = "Argl" };
static UnitAbilities_t const bot_attack_hall_abilities = { .abilList = "Argl" };
/* Natural ngol fixtures use the finite Agld ability; Abgm denotes a racial
 * overlay and is intentionally excluded from ordinary worker harvesting. */
static UnitAbilities_t const bot_mine_abilities = { .abilList = "Agld" };

/* Retail admits the older unit after a real owner round-trip, but a same-owner
 * call leaves the newer peer first. Edict order cannot represent this pool. */
TEST(wc3_bot, public_owned_pool_recruitment_tracks_owner_insertion) {
    FOR_LOOP(transfer,2) {
        G_BotStop(0); reset_entities(); setup_test_world();
        char script[1000];
        snprintf(script,sizeof(script),
            "globals\nunit first\nunit second\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set first=CreateUnit(Player(0),'hfoo',32,64,90)\n"
            "set second=CreateUnit(Player(0),'hfoo',128,64,90)\n%s\n"
            "call SetUnitOwner(first,Player(0),false)\n"
            "call StartCampaignAI(Player(0),\"test_captain_pool.ai\")\nendfunction\n",
            transfer ? "call SetUnitOwner(first,Player(1),false)" : "");
        T_ASSERT(run_test_jass(script));
        G_BotRunFrame();
        edict_t *first=NULL,*second=NULL;
        FILTER_EDICTS(unit,unit->inuse && unit->class_id==MAKEFOURCC('h','f','o','o')) {
            if(!first)first=unit;else second=unit;
        }
        T_NOT_NULL(first); T_NOT_NULL(second);
        T_EQ(G_BotCaptainGroupSize(G_GetPlayerByNumber(0)),1);
        if(G_BotCaptainGroupSize(G_GetPlayerByNumber(0)))
            T_EQ(level.bots[0].captains[BOT_CAPTAIN_ATTACK].units[0],transfer ? first : second);
        T_EQ((transfer ? second : first)->current_order_id,0);
    }
    G_BotStop(0); reset_entities(); setup_test_world();
}

/* Native public computer/neutral controls: enrollment precedes script startup,
 * genuine transfer clears/re-enrolls, same-owner retains, VM lifetime is separate. */
TEST(wc3_bot, help_policy_follows_ai_enrollment_transfer_and_save) {
    reset_entities();setup_test_world();level.timer_clock_valid=false;
    level.pathing_clock=(wc3Clock_t){.25f,0,300};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    game.clients[0].jass.controller=1;
    ((mapInfo_t *)level.mapinfo)->players[0].used=true;
    game.clients[0].mapplayer=&level.mapinfo->players[0];G_BotInitPlayers();
    T_ASSERT(run_test_jass("globals\nunit victim\nunit helper\nunit source\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set victim=CreateUnit(Player(0),'hfoo',800,800,0)\n"
        "set helper=CreateUnit(Player(1),'hfoo',1650,800,0)\n"
        "set source=CreateUnit(Player(3),'hfoo',1800,1800,0)\nendfunction\n"));
    edict_t *victim=NULL,*helper=NULL,*source=NULL;
    FILTER_EDICTS(unit,unit->inuse && unit->class_id==MAKEFOURCC('h','f','o','o')) {
        if(unit->s.player==0)victim=unit;
        if(unit->s.player==1)helper=unit;
        if(unit->s.player==3)source=unit;
    }
    T_NOT_NULL(victim);T_NOT_NULL(helper);T_NOT_NULL(source);
    if(!victim || !helper || !source)return;
    T_ASSERT(victim->aiflags&AI_TOWN_OWNED);T_ASSERT(!(helper->aiflags&AI_TOWN_OWNED));
    float old=game.constants.callForHelp;game.constants.callForHelp=173;
    victim->s.model=helper->s.model=1;victim->collision=helper->collision=31;
    S_AttackProfileWrite(victim,0)->type=S_AttackProfileWrite(helper,0)->type=ATK_NORMAL;
    G_PublishMoveSpatialObject(victim);G_PublishMoveSpatialObject(helper);
    G_SetPlayerAlliance(&game.clients[0].ps,&game.clients[1].ps,ALLIANCE_HELP_REQUEST,true);
    G_SetPlayerAlliance(&game.clients[1].ps,&game.clients[0].ps,ALLIANCE_HELP_RESPONSE,true);
    T_Damage(victim,source,0);
    T_ASSERT(helper->attack_speed_cap.active);
    T_EQ(wc3_float_bits(victim->combat_help.deadline.time),wc3_float_bits(.75f));
    uint32_t serial=victim->combat_help.sequence;
    T_ASSERT(G_BotStart(&game.clients[0].ps,"test_idle.ai",BOT_CAMPAIGN));
    T_ASSERT(victim->aiflags&AI_TOWN_OWNED);G_BotStop(0);
    T_ASSERT(victim->aiflags&AI_TOWN_OWNED);
    G_SetUnitPlayer(victim,2);T_ASSERT(!(victim->aiflags&AI_TOWN_OWNED));
    T_EQ(victim->combat_help.sequence,serial);
    T_EQ(wc3_float_bits(victim->combat_help.deadline.time),wc3_float_bits(.75f));
    G_SetUnitPlayer(victim,2);T_ASSERT(!(victim->aiflags&AI_TOWN_OWNED));
    G_SetUnitPlayer(victim,0);T_ASSERT(victim->aiflags&AI_TOWN_OWNED);
    cstring_t file="/tmp/wc3-help-ai153.bin";
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);
    T_ASSERT(victim->aiflags&AI_TOWN_OWNED);T_EQ(level.ai_owned_players,(1u<<0)|(1u<<PLAYER_NEUTRAL_AGGRESSIVE));
    T_EQ(victim->combat_help.sequence,serial);
    game.constants.callForHelp=old;G_BotStop(0);reset_entities();setup_test_world();
}

/* Town membership belongs to CUnit, not every widget with a default data row. */
TEST(wc3_bot, ai_enrollment_preserves_preplaced_identity_and_excludes_nonunits) {
    reset_entities();setup_test_world();
    edict_t *unit=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){0,0},0);
    edict_t *ward=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){128,0},0);
    edict_t *dead=unit_create(0,MAKEFOURCC('h','f','o','o'),&(vec2_t){256,0},0);
    ward->targtype=TARG_WARD;dead->health.value=0;
    edict_t *widget=G_Spawn();widget->class_id=MAKEFOURCC('L','T','l','t');
    G_BindEntityData(widget);widget->health.value=100;
    game.clients[0].jass.controller=1;
    ((mapInfo_t *)level.mapinfo)->players[0].used=true;
    game.clients[0].mapplayer=&level.mapinfo->players[0];
    uint32_t sequence=unit->own_seq;
    G_BotInitPlayers();
    T_ASSERT(unit->aiflags&AI_TOWN_OWNED);T_EQ(unit->own_seq,sequence);
    T_ASSERT(!(ward->aiflags&AI_TOWN_OWNED));T_ASSERT(!(dead->aiflags&AI_TOWN_OWNED));
    T_ASSERT(!(widget->aiflags&AI_TOWN_OWNED));
    G_BotUnitOwnerChanged(widget);T_ASSERT(!(widget->aiflags&AI_TOWN_OWNED));
    game.clients[0].jass.removed=true;
    game.clients[1].jass.controller=1;game.clients[1].mapplayer=&level.mapinfo->players[1];
    G_BotInitPlayers();T_EQ(level.ai_owned_players,1u<<PLAYER_NEUTRAL_AGGRESSIVE);
    cstring_t file="/tmp/wc3-help-ai153-invalid.bin";
    level.ai_owned_players|=1u<<PLAYER_NEUTRAL_PASSIVE;
    T_ASSERT(!WriteGame(file));remove(file);
    reset_entities();setup_test_world();
}

TEST(wc3_bot, neutral_help_policy_distinguishes_aggressive_and_passive) {
    reset_entities();setup_test_world();G_BotInitPlayers();level.timer_clock_valid=false;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.pathing_clock=(wc3Clock_t){.25f,0,300};
    edict_t *aggressive=unit_create(PLAYER_NEUTRAL_AGGRESSIVE,MAKEFOURCC('h','f','o','o'),&(vec2_t){800,800},0);
    edict_t *passive=unit_create(PLAYER_NEUTRAL_PASSIVE,MAKEFOURCC('h','f','o','o'),&(vec2_t){800,1200},0);
    edict_t *source=alloc_test_unit(MAKEFOURCC('h','f','o','o'),1800,1800);G_SetUnitPlayer(source,3);
    T_ASSERT(aggressive->aiflags&AI_TOWN_OWNED);T_ASSERT(!(passive->aiflags&AI_TOWN_OWNED));
    T_Damage(aggressive,source,0);T_Damage(passive,source,0);
    T_EQ(wc3_float_bits(aggressive->combat_help.deadline.time),wc3_float_bits(.75f));
    T_EQ(wc3_float_bits(passive->combat_help.deadline.time),wc3_float_bits(3.25f));
    G_SetUnitPlayer(aggressive,PLAYER_NEUTRAL_PASSIVE);T_ASSERT(!(aggressive->aiflags&AI_TOWN_OWNED));
    G_SetUnitPlayer(passive,PLAYER_NEUTRAL_AGGRESSIVE);T_ASSERT(passive->aiflags&AI_TOWN_OWNED);
    reset_entities();setup_test_world();
}

TEST(wc3_bot, display_text_formats_only_authoritative_integer_templates) {
    int32_t values[] = {12, -3, 7};
    char text[64], small[8];

    BotDisplayFormat(text, sizeof(text), "values %d %d %d %% %x\\n", values, sizeof(values) / sizeof(*values));
    T_STREQ(text, "values 12 -3 7 % %x\n");
    BotDisplayFormat(text, sizeof(text), "missing %d", values, 0);
    T_STREQ(text, "missing %d");
    BotDisplayFormat(small, sizeof(small), "123456789", values, sizeof(values) / sizeof(*values));
    T_STREQ(small, "1234567");
}

static edict_t *make_bot_harvest_unit(uint32_t class_id, float x, float y, uint32_t player, UnitAbilities_t const *abilities) {
    edict_t *unit = alloc_test_unit(class_id, x, y);
    unit->s.player = player; unit->data.UnitAbilities = abilities;
    unit->health.value = unit->health.max_value = 1000; unit->stand = unit_stand;
    S_AttackProfileWrite(unit, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(unit, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    unit->targtype = TARG_GROUND;
    return unit;
}

TEST(wc3_bot, binds_player_and_pauses_sleeping_script) {
    player_t *player = &game.clients[2].ps;

    T_ASSERT(G_BotStart(player, "test_player.ai", BOT_CAMPAIGN));
    T_NOT_NULL(level.bots[2].vm);
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);

    G_BotPause(2, true);
    level.time = 20000;
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);

    G_BotPause(2, false);
    G_BotRunFrame();
    T_NULL(level.bots[2].vm);
}

TEST(wc3_bot, roots_are_independent_and_stop_individually) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_idle.ai", BOT_MELEE));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
    T_EQ(level.bots[1].mode, BOT_CAMPAIGN);
    T_EQ(level.bots[2].mode, BOT_MELEE);
    level.time = 20000;
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);

    G_BotStop(1);
    T_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
    G_BotShutdown();
    T_NULL(level.bots[2].vm);
}

TEST(wc3_bot, repeated_start_preserves_vm_and_missing_script_is_bounded) {
    player_t *player = &game.clients[0].ps;

    T_ASSERT(G_BotStart(player, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(player, "Scripts\\test_idle.ai", BOT_MELEE));
    T_NOT_NULL(level.bots[0].vm);
    T_EQ(level.bots[0].mode, BOT_CAMPAIGN);
    T_ASSERT(!G_BotStart(player, "missing.ai", BOT_CAMPAIGN));
    T_NOT_NULL(level.bots[0].vm);
    G_BotStop(0);
    T_ASSERT(!G_BotStart(player, "missing.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
    T_ASSERT(!G_BotStart(player, "test_no_main.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
    T_ASSERT(!G_BotStart(player, "test_bad_init.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
}

TEST(wc3_bot, deferred_stop_removes_only_requested_player) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_idle.ai", BOT_CAMPAIGN));
    G_BotRequestStop(1);
    G_BotRunFrame();
    T_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
}

TEST(wc3_bot, repeated_start_inside_ai_preserves_current_script) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_replace.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_EQ(level.bots[1].mode, BOT_CAMPAIGN);
    T_STREQ(level.bots[1].script, "Scripts\\test_replace.ai");
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
}

TEST(wc3_bot, query_natives_read_authoritative_player_state) {
    edict_t *done = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *training = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *dead = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    edict_t *other = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    edict_t *hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 128, 2, &bot_hall_abilities);
    edict_t *mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 128, MAX_PLAYERS, &bot_mine_abilities);
    edict_t *builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 128);

    done->s.player = building->s.player = training->s.player = dead->s.player = 2;
    other->s.player = 1;
    done->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER; training->svflags |= SVF_MONSTER;
    dead->svflags |= SVF_MONSTER | SVF_DEADMONSTER; other->svflags |= SVF_MONSTER;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    training->training = true;
    builder->s.player = 2; builder->health.value = 100; builder->build_project = MAKEFOURCC('h','b','a','r');
    mine->resources = 1000;
    G_SetPlayerTechResearched(&game.clients[2], MAKEFOURCC('R','h','m','e'), 2);

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_queries.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    if (level.bots[2].vm) T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_EQ(G_BotTown(&game.clients[2].ps, 0), hall); T_EQ(G_BotTownMine(&game.clients[2].ps, 0), mine);
}

TEST(wc3_bot, town_unit_count_scopes_owned_units_and_completion_to_nearest_town) {
    player_t *player = &game.clients[2].ps;
    uint32_t footman = MAKEFOURCC('h','f','o','o');
    edict_t *hall0, *hall1, *done, *building, *training, *pending_builder;

    reset_entities();
    hall0 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    hall1 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 1000, 0, 2, &bot_hall_abilities);
    done = alloc_test_unit(footman, 64, 0); done->s.player = 2; done->svflags |= SVF_MONSTER;
    building = alloc_test_unit(footman, 96, 0); building->s.player = 2; building->svflags |= SVF_MONSTER;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    training = alloc_test_unit(footman, 1024, 0); training->s.player = 2; training->svflags |= SVF_MONSTER;
    training->training = true;
    pending_builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128, 0);
    pending_builder->s.player = 2; pending_builder->health.value = 100; pending_builder->build_project = footman;

    T_EQ(G_BotTown(player, 0), hall0); T_EQ(G_BotTown(player, 1), hall1);
    T_EQ(G_BotTownUnitCount(player, footman, 0, false), 3);
    T_EQ(G_BotTownUnitCount(player, footman, 0, true), 1);
    T_EQ(G_BotTownUnitCount(player, footman, 1, false), 1);
    T_EQ(G_BotTownUnitCount(player, footman, 1, true), 0);
    T_EQ(G_BotTownUnitCount(player, footman, 2, false), 0);
}

TEST(wc3_bot, mines_belong_to_the_nearest_owned_town) {
    player_t *player = &game.clients[2].ps;
    reset_entities();
    edict_t *hall0 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    edict_t *hall1 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 1000, 0, 2, &bot_hall_abilities);
    edict_t *mine0 = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 100, 0, MAX_PLAYERS, &bot_mine_abilities);
    edict_t *mine1 = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 900, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine0->resources = 1000; mine1->resources = 2000;

    T_EQ(G_BotTown(player, 0), hall0); T_EQ(G_BotTown(player, 1), hall1);
    T_EQ(G_BotTownMine(player, 0), mine0); T_EQ(G_BotTownMine(player, 1), mine1);
    T_EQ(G_BotTownWithMine(player), 0); T_EQ(G_BotMinesOwned(player), 2); T_EQ(G_BotGoldOwned(player), 3000);
    mine0->resources = 0;
    T_NULL(G_BotTownMine(player, 0)); T_EQ(G_BotTownWithMine(player), 1); T_EQ(G_BotMinesOwned(player), 1);
    T_EQ(G_BotGoldOwned(player), 2000);
}

TEST(wc3_bot, expansion_natives_keep_one_viable_mine_and_order_hall_normally) {
	enum { CELLS = 64 };
	static uint8_t pathmap[CELLS * CELLS];
    player_t *player = &game.clients[2].ps;
    uint32_t const hall_id = MAKEFOURCC('h','h','o','u');
    UnitProfile_t worker_profile = { .builds = "hhou" };
    UnitAbilities_t mine_abilities = { .abilList = "Agld" };
    edict_t *main_hall, *mine, *worker, *foe;
    vec2_t requested;
    vec2_t position;

    reset_entities(); setup_test_world(); setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-512.0f, -512.0f}, .max = {1536.0f, 1536.0f}));
    main_hall = make_bot_harvest_unit(hall_id, 0, 0, 2, &bot_hall_abilities);
    mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 700, 0, PLAYER_NEUTRAL_AGGRESSIVE, &mine_abilities);
    mine->resources = 1000;
    T_ASSERT(S_UnitAbilityEvent(mine, A_UNIT_INIT));
    worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 128, 0, 2, &bot_harvester_abilities);
    worker->data.UnitProfile = &worker_profile;
    foe = make_bot_harvest_unit(MAKEFOURCC('o','f','o','o'), 700, 256, 3, NULL);
    foe->svflags |= SVF_MONSTER;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    G_SetPlayerAlliance(player, &game.clients[3].ps, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(player, &game.clients[3].ps, ALLIANCE_PASSIVE));

    T_EQ(G_BotNextExpansion(player), 0);
    position = G_BotExpansionPosition(player);
    T_FEQ(position.x, mine->s.origin2.x, 0.001f); T_FEQ(position.y, mine->s.origin2.y, 0.001f);
    T_EQ(G_BotExpansionMine(player), mine);
    T_EQ(G_BotExpansionFoe(player), foe);
    T_EQ(G_BotExpansionPeon(player), worker);
    requested = MAKE(vec2_t, 700.0f - WC3_GOLD_MINE_MIN_DISTANCE - 32.0f, 0.0f);
    T_EQ(G_GetBuildCommandState(PLAYER_CLIENT(player), worker, hall_id, NULL, 0), BUILD_COMMAND_AVAILABLE);
    T_EQ(G_EvaluateBuildPlacement(worker, hall_id, &requested, NULL), PLACE_OK);
    T_ASSERT(!G_BotSetExpansion(player, worker, MAKEFOURCC('x','x','x','x')));
    G_FreeEdict(foe);
    T_NULL(G_BotExpansionFoe(player));
    T_ASSERT(G_BotSetExpansion(player, worker, hall_id));
    T_EQ(worker->build_project, hall_id);
    T_NOT_NULL(worker->goalentity);
    T_EQ(G_BotTown(player, 0), main_hall);
    T_EQ(G_BotExpansionMine(player), mine);
}

TEST(wc3_bot, expansion_native_sequence_runs_from_common_ai_context) {
	enum { CELLS = 64 };
	static uint8_t pathmap[CELLS * CELLS];
    player_t *player = &game.clients[2].ps;
    UnitProfile_t worker_profile = { .builds = "hhou" };
    UnitAbilities_t mine_abilities = { .abilList = "Agld" };
    edict_t *hall, *mine, *worker;

    reset_entities(); setup_test_world(); setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-512.0f, -512.0f}, .max = {1536.0f, 1536.0f}));
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 700, 0, PLAYER_NEUTRAL_AGGRESSIVE, &mine_abilities);
    mine->resources = 1000;
    T_ASSERT(S_UnitAbilityEvent(mine, A_UNIT_INIT));
    worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 128, 0, 2, &bot_harvester_abilities);
    worker->data.UnitProfile = &worker_profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    T_NOT_NULL(hall); T_NOT_NULL(worker);

    T_ASSERT(G_BotStart(player, "test_expansion.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    if (level.bots[2].vm) T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_EQ(worker->build_project, MAKEFOURCC('h','h','o','u'));
    T_NOT_NULL(worker->goalentity);
}

TEST(wc3_bot, produce_queues_trainable_units_and_rejects_unknown_types) {
    player_t *player = &game.clients[2].ps;
    edict_t *producer;
    UnitProfile_t profile = { .trains = "hfoo" };
    reset_entities();
    setup_test_world();
    InitUnitData();
    producer = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 0, 0, 2, NULL);
    producer->data.UnitProfile = &profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    T_ASSERT(G_BotProduce(player, 1, MAKEFOURCC('h','f','o','o'), -1));
    T_NOT_NULL(producer->build);
    if (producer->build) {
        T_EQ(producer->build->class_id, MAKEFOURCC('h','f','o','o'));
    }
    T_ASSERT(!G_BotProduce(player, 1, MAKEFOURCC('u','n','k','n'), -1));
}

TEST(wc3_bot, production_uses_idle_barracks_at_the_requested_town) {
    player_t *player = &game.clients[2].ps;
    UnitProfile_t profile = { .trains = "hfoo" };
    edict_t *hall, *first, *second;
    reset_entities(); setup_test_world();
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    first = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 64, 0, 2, NULL);
    second = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), -64, 0, 2, NULL);
    first->data.UnitProfile = second->data.UnitProfile = &profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    T_NOT_NULL(hall);
    first->build = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    first->build->training = true;
    T_ASSERT(G_BotProduce(player, 1, MAKEFOURCC('h','f','o','o'), 0));
    T_NOT_NULL(second->build);
}

TEST(wc3_bot, production_prefers_idle_then_fills_most_available_queue_slots) {
    player_t *player = &game.clients[2].ps;
    UnitProfile_t profile = { .trains = "hfoo" };
    edict_t *busy, *idle;
    uint32_t i;
    reset_entities(); setup_test_world(); InitUnitData();
    busy = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 64, 0, 2, NULL);
    idle = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), -64, 0, 2, NULL);
    busy->data.UnitProfile = idle->data.UnitProfile = &profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 200;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    for (i = 0; i < MAX_BUILD_QUEUE - 1; i++)
        T_ASSERT(SP_TrainUnit(busy, MAKEFOURCC('h','f','o','o')));
    T_EQ(G_ProductionQueueCount(busy), MAX_BUILD_QUEUE - 1);
    T_EQ(G_ProductionQueueCount(idle), 0);

    T_ASSERT(G_BotProduce(player, 1, MAKEFOURCC('h','f','o','o'), -1));
    T_EQ(G_ProductionQueueCount(idle), 1);
    T_EQ(G_ProductionQueueCount(busy), MAX_BUILD_QUEUE - 1);
    T_ASSERT(G_BotProduce(player, 7, MAKEFOURCC('h','f','o','o'), -1));
    T_EQ(G_ProductionQueueCount(idle), MAX_BUILD_QUEUE);
    T_EQ(G_ProductionQueueCount(busy), MAX_BUILD_QUEUE);
}

TEST(wc3_bot, guard_post_replacement_trains_once_and_consumes_post_budget) {
    player_t *player = &game.clients[2].ps;
    bot_t *bot = level.bots + 2;
    UnitProfile_t profile = { .trains = "hfoo" };
    edict_t *producer;

    reset_entities();
    producer = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 0, 0, 2, NULL);
    producer->data.UnitProfile = &profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    bot->replacement_count = 1;

    G_BotAddGuardPost(player, MAKEFOURCC('h','f','o','o'), 256, 0);
    T_EQ(bot->guards[0].replacements_used, 0);
    G_BotFillGuardPosts(player);
    T_NOT_NULL(producer->build);
    T_ASSERT(bot->guards[0].replacement_pending);
    T_EQ(bot->guards[0].replacements_used, 1);
    T_NULL(producer->build->build);

    G_BotFillGuardPosts(player);
    T_NULL(producer->build->build); /* repeated FillGuardPosts must not queue the same replacement again */

    producer->build->training = false;
    producer->build->health.value = producer->build->health.max_value = 100;
    G_BotFillGuardPosts(player);
    T_EQ(bot->guards[0].unit, producer->build);
    T_ASSERT(!bot->guards[0].replacement_pending);

    bot->guards[0].unit->health.value = 0;
    G_BotFillGuardPosts(player);
    T_NULL(bot->guards[0].unit);
    T_NULL(producer->build->build); /* replacement quota was exhausted */
}

TEST(wc3_bot, peons_repair_policy_enables_existing_repair_autocast_path) {
    edict_t *worker;

    reset_entities();
    worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 0, 0, 4, &bot_repair_abilities);
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    T_ASSERT(G_BotStart(&game.clients[4].ps, "test_ai_settings_inverse.ai", BOT_CAMPAIGN));
    G_BotRunFrame(); /* script sets BOT_PEONS_REPAIR */
    G_BotRunFrame(); /* policy reconciliation applies the normal Repair autocast */
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
}

TEST(wc3_bot, peons_repair_policy_applies_to_jass_created_units) {
    edict_t *worker;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[4].ps, "test_ai_settings_inverse.ai", BOT_CAMPAIGN));
    G_BotRunFrame(); /* script enables the repair policy */
    G_BotRunFrame(); /* initial owned-unit reconciliation completes */
    worker = unit_create(4, BZ_WC3_UNIT_PEASANT, &(vec2_t){512, 512}, 0);
    T_NOT_NULL(worker);
    if (worker) T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
}

TEST(wc3_bot, hero_levels_callback_uses_ai_hero_context_and_normal_learning) {
    edict_t *hero;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_hero_levels.ai", BOT_MELEE));
    G_BotRunFrame();
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->s.player = 2;
    hero->hero.level = 1;
    hero->hero.skillpoints = 0;
    hero->hero.xp = 0;
    G_HeroSetXP(hero, G_HeroXPForLevel(2));
    T_EQ(hero->hero.level, 2);
    T_EQ(G_UnitAbilityLevel(hero, MAKEFOURCC('A','H','h','b')), 1);
    T_EQ(hero->hero.skillpoints, 0);
    T_EQ(level.bots[2].hero_id, 0);
    T_EQ(level.bots[2].hero_level, 0);
}

TEST(wc3_bot, hero_levels_callback_runs_for_jass_created_hero) {
    edict_t *hero;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_hero_levels.ai", BOT_MELEE));
    G_BotRunFrame();
    hero = unit_create(2, MAKEFOURCC('H','p','a','l'), &(vec2_t){512, 512}, 0);
    T_NOT_NULL(hero);
    if (hero) {
        T_EQ(hero->hero.level, 1);
        T_EQ(G_UnitAbilityLevel(hero, MAKEFOURCC('A','H','h','b')), 1);
        T_EQ(hero->hero.skillpoints, 0);
    }
}

TEST(wc3_bot, set_upgrade_uses_normal_research_queue_and_rejects_duplicate_request) {
    gameClient_t *client = &game.clients[2];
    player_t *player = &client->ps;
    UnitProfile_t profile = { .researches = "Rhme" };
    edict_t *producer;

    reset_entities();
    memset(client->tech, 0, sizeof(client->tech));
    producer = make_bot_harvest_unit(MAKEFOURCC('h','b','l','a'), 0, 0, 2, NULL);
    producer->data.UnitProfile = &profile;
    producer->s.flags |= EF_BUILDING;
    producer->runtime.flags |= UNIT_BALANCE_BUILDING;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;

    T_ASSERT(G_BotStart(player, "test_upgrade.ai", BOT_MELEE));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    if (level.bots[2].vm) T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_NOT_NULL(producer->build);
    if (producer->build) {
        T_EQ(producer->build->research->upgrade, MAKEFOURCC('R','h','m','e'));
        T_EQ(producer->build->research->level, 1);
    }
    T_EQ(G_GetPlayerTechInProgress(client, MAKEFOURCC('R','h','m','e')), 1);
    T_EQ(player->stats[PLAYERSTATE_RESOURCE_GOLD], 900);
    T_EQ(player->stats[PLAYERSTATE_RESOURCE_LUMBER], 950);
}

TEST(wc3_bot, shift_town_spot_redirects_subsequent_build_search) {
	enum { CELLS = 64 };
	static uint8_t pathmap[CELLS * CELLS];
    player_t *player = &game.clients[2].ps;
    UnitProfile_t worker_profile = { .builds = "hbar" };
    edict_t *hall, *worker;

    reset_entities();
    setup_test_world();
    setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-512.0f, -512.0f}, .max = {1536.0f, 1536.0f}));
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 1024, 0, 2, &bot_harvester_abilities);
    worker->data.UnitProfile = &worker_profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;

    T_ASSERT(G_BotStart(player, "test_shift_town.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_ASSERT(level.bots[2].town_spot_valid);
    T_FEQ(level.bots[2].town_spot.x, 1024.0f, 0.001f);
    T_FEQ(level.bots[2].town_spot.y, 0.0f, 0.001f);

    T_ASSERT(G_BotProduce(player, 1, MAKEFOURCC('h','b','a','r'), 0));
    T_NOT_NULL(worker->goalentity);
    if (worker->goalentity) T_ASSERT(worker->goalentity->s.origin2.x > 512.0f);
    T_FEQ(hall->s.origin2.x, 0.0f, 0.001f);
}

TEST(wc3_bot, build_site_requires_direct_static_route) {
    uint8_t cells[100] = {0};
    edict_t worker = { .collision = 0.0f, .s.origin2 = { 1.0f, 5.0f } };
    vec2_t same_side = { 4.0f, 5.0f }, across_wall = { 8.0f, 5.0f };
    FOR_LOOP(y, 10) cells[5 + y * 10] = 2;
    setup_test_pathmap(10, 10, cells);
    T_ASSERT(G_BotBuildSiteReachable(&worker, &same_side));
    T_ASSERT(!G_BotBuildSiteReachable(&worker, &across_wall));
    T_ASSERT(!G_BotBuildSiteReachable(NULL, &same_side));
    setup_test_world();
}

TEST(wc3_bot, unit_alive_rejects_null_dead_and_removed_handles) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit->health.value = 100;
    T_ASSERT(G_BotUnitAlive(unit));
    unit->health.value = 0;
    T_ASSERT(!G_BotUnitAlive(unit));
    unit->health.value = 100; unit->svflags |= SVF_DEADMONSTER;
    T_ASSERT(!G_BotUnitAlive(unit));
    unit->svflags &= ~SVF_DEADMONSTER; unit->inuse = false;
    T_ASSERT(!G_BotUnitAlive(unit));
    T_ASSERT(!G_BotUnitAlive(NULL));
}

TEST(wc3_bot, alliance_target_is_shared_consumable_state_without_orders) {
    player_t *publisher = &game.clients[1].ps;
    player_t *ally = &game.clients[2].ps;
    player_t *outsider = &game.clients[3].ps;
    edict_t *target;

    reset_entities();
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256, 64);
    target->s.player = 0; target->health.value = 100; target->svflags |= SVF_MONSTER;
    memset(level.bots + 1, 0, sizeof(level.bots[1]) * 3);
    G_SetPlayerAlliance(publisher, ally, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(ally, publisher, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(publisher, outsider, ALLIANCE_PASSIVE, false);
    G_SetPlayerAlliance(outsider, publisher, ALLIANCE_PASSIVE, false);

    G_BotSetAllianceTarget(publisher, target);
    T_EQ(G_BotGetAllianceTarget(publisher), target);
    T_EQ(G_BotGetAllianceTarget(ally), target);
    T_NULL(G_BotGetAllianceTarget(outsider));
    T_NULL(target->currentmove); /* publishing coordinates AI state; it does not issue an attack */

    G_BotSetAllianceTarget(ally, NULL);
    T_NULL(G_BotGetAllianceTarget(publisher));
    T_NULL(G_BotGetAllianceTarget(ally));
}

TEST(wc3_bot, alliance_target_drops_a_dead_shared_target) {
    player_t *publisher = &game.clients[1].ps;
    player_t *ally = &game.clients[2].ps;
    edict_t *target;

    reset_entities();
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256, 64);
    target->s.player = 0; target->health.value = 100; target->svflags |= SVF_MONSTER;
    memset(level.bots + 1, 0, sizeof(level.bots[1]) * 2);
    G_SetPlayerAlliance(publisher, ally, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(ally, publisher, ALLIANCE_PASSIVE, true);
    G_BotSetAllianceTarget(publisher, target);

    target->health.value = 0;
    T_NULL(G_BotGetAllianceTarget(ally));
}

TEST(wc3_bot, campaign_settings_persist_for_authoritative_consumers) {
    bot_t *ai;
    uint32_t enabled = BOT_TARGET_HEROES | BOT_HEROES_FLEE | BOT_IGNORE_INJURED |
        BOT_UNITS_FLEE | BOT_SLOW_CHOPPING | BOT_SMART_ARTILLERY | BOT_NEW_HEROES |
        BOT_DEFEND_PLAYER;

    T_ASSERT(G_BotStart(&game.clients[3].ps, "test_ai_settings.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    ai = level.bots + 3;
    T_NOT_NULL(ai->vm);
    T_EQ(ai->mode, BOT_CAMPAIGN);
    T_NOT_NULL(ai->hero_levels);
    T_STREQ(jass_functionname(ai->hero_levels), "hero_levels");
    T_EQ(ai->replacement_count, 3);
    T_EQ(ai->flags, enabled);
    T_ASSERT(!(ai->flags & BOT_PEONS_REPAIR));
    T_ASSERT(!(ai->flags & BOT_WATCH_MEGA));
    T_ASSERT(!(ai->flags & BOT_HEROES_TAKE_ITEM));
    T_ASSERT(!(ai->flags & BOT_GROUPS_FLEE));
    T_ASSERT(!(ai->flags & BOT_CAPTAIN_CHANGES));
    T_ASSERT(!(ai->flags & BOT_GROUP_TIMED_LIFE));
    T_ASSERT(!(ai->flags & BOT_RANDOM_PATHS));
    T_ASSERT(!(ai->flags & BOT_HEROES_BUY_ITEMS));
}

TEST(wc3_bot, melee_difficulty_uses_common_ai_integer_scale) {
    bot_t *ai;

    T_ASSERT(G_BotStart(&game.clients[4].ps, "test_melee_difficulty.ai", BOT_MELEE));
    G_BotRunFrame();
    ai = level.bots + 4;
    T_NOT_NULL(ai->vm);
    T_EQ(ai->mode, BOT_MELEE);
    T_EQ(ai->replacement_count, 2); /* MELEE_NORMAL, not AI_DIFFICULTY_NORMAL's handle payload 1. */
}

TEST(wc3_bot, melee_settings_cover_inverse_flags_and_clamp_replacements) {
    bot_t *ai;
    uint32_t enabled = BOT_PEONS_REPAIR | BOT_WATCH_MEGA | BOT_HEROES_TAKE_ITEM |
        BOT_GROUPS_FLEE | BOT_CAPTAIN_CHANGES | BOT_GROUP_TIMED_LIFE |
        BOT_RANDOM_PATHS | BOT_HEROES_BUY_ITEMS;

    T_ASSERT(G_BotStart(&game.clients[4].ps, "test_ai_settings_inverse.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    ai = level.bots + 4;
    T_NOT_NULL(ai->vm);
    T_EQ(ai->mode, BOT_MELEE);
    T_EQ(ai->replacement_count, 0);
    T_EQ(ai->flags, enabled);
    T_ASSERT(!(ai->flags & BOT_TARGET_HEROES));
    T_ASSERT(!(ai->flags & BOT_HEROES_FLEE));
    T_ASSERT(!(ai->flags & BOT_IGNORE_INJURED));
    T_ASSERT(!(ai->flags & BOT_UNITS_FLEE));
    T_ASSERT(!(ai->flags & BOT_SLOW_CHOPPING));
    T_ASSERT(!(ai->flags & BOT_SMART_ARTILLERY));
    T_ASSERT(!(ai->flags & BOT_NEW_HEROES));
    T_ASSERT(!(ai->flags & BOT_DEFEND_PLAYER));
}

TEST(wc3_bot, stop_gathering_stops_only_owned_harvesters_and_releases_mines) {
    static umove_t lumber_move = { "attack", NULL, NULL, CAbilityHarvest };
    static umove_t gold_move = { "attack", NULL, NULL, CAbilityGoldMine };
    static umove_t attack_move = { "attack", NULL, NULL, CAbilityAttack };
    edict_t *lumber = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *gold = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32, 0);
    edict_t *other = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    edict_t *fighter = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 128, 0);

    lumber->s.player = gold->s.player = fighter->s.player = 2; other->s.player = 1;
    lumber->stand = gold->stand = other->stand = fighter->stand = unit_stand;
    M_SetMove(lumber,&lumber_move); M_SetMove(gold,&gold_move);
    M_SetMove(other,&lumber_move); M_SetMove(fighter,&attack_move);
    if (!gold->goldmine) gold->goldmine = G_AllocGoldMine();
    assert(gold->goldmine);
    gold->goldmine->mine = mine; gold->goldmine->mine_spawn_time = mine->spawn_time;
    gold->invulnerable = true; gold->s.renderfx |= RF_HIDDEN; mine->peonsinside = 1;
    lumber->harvested_lumber = 7; gold->harvested_gold = 5;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_stop_gathering.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NULL(lumber->currentmove->proc);
    T_NULL(gold->currentmove->proc);
    T_EQ(lumber->harvested_lumber, 7);
    T_EQ(gold->harvested_gold, 5);
    T_NULL(gold->goldmine->mine);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!(gold->s.renderfx & RF_HIDDEN));
    T_ASSERT(!gold->invulnerable);
    T_EQ(other->currentmove->proc, CAbilityHarvest);
    T_EQ(fighter->currentmove->proc, CAbilityAttack);
}

TEST(wc3_bot, harvest_gold_assigns_nearest_owned_workers_up_to_quota) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    edict_t *hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    edict_t *trainee = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 8, 0, 2, &bot_harvester_abilities);
    edict_t *builder = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 16, 0, 2, &bot_harvester_abilities);
    edict_t *near = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    edict_t *far = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 96, 0, 2, &bot_harvester_abilities);
    edict_t *other = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 16, 0, 1, &bot_harvester_abilities);
    edict_t *mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine->resources = 1000; trainee->training = true; trainee->s.renderfx |= RF_HIDDEN;
    builder->build_project = MAKEFOURCC('h','b','a','r');

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 1); T_EQ(bot->harvesters[0], near);
    T_EQ(near->goalentity, mine); T_EQ(near->currentmove->proc, CAbilityGoldMine);
    T_NULL(trainee->currentmove); T_NULL(builder->currentmove); T_NULL(far->currentmove);
    T_NULL(other->currentmove); T_EQ(hall->s.player, 2);
}

TEST(wc3_bot, harvest_pass_reserves_workers_across_gold_and_wood_then_clears) {
    static umove_t gold_move = { "attack", NULL, NULL, CAbilityGoldMine };
    bot_t *bot = level.bots + 2;
    reset_entities();
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    edict_t *first = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    edict_t *second = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    edict_t *mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    edict_t *tree = make_bot_harvest_unit(MAKEFOURCC('L','T','l','t'), 0, 256, MAX_PLAYERS, NULL);
    mine->resources = 1000; tree->targtype = TARG_TREE;
    M_SetMove(first,&gold_move); S_SetMoveGoal(first, &first->goalentity, mine);

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    G_BotHarvest(&game.clients[2].ps, 0, 1, false);
    T_EQ(ARRAY_COUNT(bot->harvesters), 2); T_EQ(bot->harvesters[0], first); T_EQ(bot->harvesters[1], second);
    T_EQ(first->currentmove, &gold_move); T_EQ(first->goalentity, mine); T_EQ(second->goalentity, tree);
    G_BotHarvest(&game.clients[2].ps, 1, 2, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 2);
    G_BotClearHarvest(&game.clients[2].ps);
    T_EQ(ARRAY_COUNT(bot->harvesters), 0); T_NULL(bot->harvesters);
}

TEST(wc3_bot, harvest_returns_carried_resources_before_collecting) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    edict_t *hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    edict_t *worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    edict_t *mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine->resources = 1000; worker->harvested_lumber = 5;

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 1); T_EQ(worker->goalentity, hall);
    T_EQ(worker->harvested_lumber, 5); T_EQ(worker->currentmove->proc, CAbilityHarvest);
}

TEST(wc3_bot, harvest_natives_execute_through_player_bot_vm) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    edict_t *mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    edict_t *tree = make_bot_harvest_unit(MAKEFOURCC('L','T','l','t'), 0, 256, MAX_PLAYERS, NULL);
    mine->resources = 1000; tree->targtype = TARG_TREE;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_harvest.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm); T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(ARRAY_COUNT(bot->harvesters), 2);
    T_EQ(bot->harvesters[0]->goalentity, mine); T_EQ(bot->harvesters[1]->goalentity, tree);
}

TEST(wc3_bot, create_captains_resets_both_bot_owned_captains) {
    bot_t *bot = level.bots + 2;
    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_ACTIVE;
    bot->captains[BOT_CAPTAIN_ATTACK].full = true;
    bot->captains[BOT_CAPTAIN_ATTACK].home.x = 128;
    bot->captains[BOT_CAPTAIN_DEFENSE].state = BOT_CAPTAIN_RETREATING;
    bot->captains[BOT_CAPTAIN_DEFENSE].full = true;
    bot->captains[BOT_CAPTAIN_DEFENSE].goal.y = 256;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_create_captains.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        T_EQ(bot->captains[i].state, BOT_CAPTAIN_IDLE);
        T_ASSERT(!bot->captains[i].full);T_ASSERT(!bot->captains[i].home_set);
        T_EQ(ARRAY_COUNT(bot->captains[i].units), 0);
        T_NULL(bot->captains[i].units);
        T_FEQ(bot->captains[i].home.x, 0, 0.001f);
        T_FEQ(bot->captains[i].home.y, 0, 0.001f);
        T_FEQ(bot->captains[i].goal.x, 0, 0.001f);
        T_FEQ(bot->captains[i].goal.y, 0, 0.001f);
    }
}

TEST(wc3_bot, ignored_units_counts_only_live_owned_captain_members) {
    bot_t *bot = level.bots + 2;
    edict_t *attack = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *defense = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *dead = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *removed = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    edict_t *wrong_type = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 128, 0);
    edict_t *wrong_owner = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 160, 0);

    attack->s.player = defense->s.player = dead->s.player = removed->s.player = wrong_type->s.player = 2;
    wrong_owner->s.player = 1;
    attack->health.value = defense->health.value = removed->health.value = wrong_type->health.value = 100;
    wrong_owner->health.value = 100; dead->health.value = 0; removed->inuse = false;
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_ignored_units.ai", BOT_CAMPAIGN));
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(3 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 3;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = attack;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = dead;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = wrong_type;
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(3 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 3;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = defense;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[1] = removed;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[2] = wrong_owner;

    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, MAKEFOURCC('h','f','o','o')), 2);
    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, MAKEFOURCC('h','r','i','f')), 1);
    T_EQ(G_BotIgnoredUnits(NULL, MAKEFOURCC('h','f','o','o')), 0);
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, attack_move_kill_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_attack_move_kill.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, captain_retreating_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_captain_retreating.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, captain_retreating_reports_attack_captain_state_only) {
    bot_t *bot = level.bots + 2;
    player_t *player = &game.clients[2].ps;

    bot->player = player;
    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_ACTIVE;
    bot->captains[BOT_CAPTAIN_DEFENSE].state = BOT_CAPTAIN_RETREATING;
    T_ASSERT(!G_BotCaptainRetreating(player));

    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_RETREATING;
    T_ASSERT(G_BotCaptainRetreating(player));

    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_IDLE;
    T_ASSERT(!G_BotCaptainRetreating(player));
}

TEST(wc3_bot, group_flee_enters_retreat_after_persistent_local_disadvantage_and_returns_home) {
    static UnitBalance_t const friendly_balance = { .maxHealth = 100, .level = 1 };
    static UnitBalance_t const enemy_balance = { .maxHealth = 100, .level = 4 };
    static UnitWeapons_t const enabled_attack = { .attacksEnabled = 1 };
    bot_t *bot = level.bots + 2;
    player_t *player = &game.clients[2].ps;
    botCaptain_t *captain = bot->captains + BOT_CAPTAIN_ATTACK;
    edict_t *first, *second, *enemy;

    reset_entities();
    G_BotInitAssault(player);
    first = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 512, 0, 2, NULL);
    second = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 544, 0, 2, NULL);
    enemy = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 600, 0, 1, NULL);
    first->data.UnitBalance = second->data.UnitBalance = &friendly_balance;
    enemy->data.UnitBalance = &enemy_balance; enemy->data.UnitWeapons = &enabled_attack;
    enemy->svflags |= SVF_MONSTER;
    first->health.value = first->health.max_value = 100;
    second->health.value = second->health.max_value = 100;
    enemy->health.value = enemy->health.max_value = 100;
    bot->player = player;
    bot->flags = BOT_GROUPS_FLEE;
    G_SetPlayerAlliance(player, &game.clients[1].ps, ALLIANCE_PASSIVE, false);
    captain->state = BOT_CAPTAIN_ACTIVE;
    captain->home = MAKE(vec2_t, 0, 0);
    captain->units = gi.MemAlloc(2 * sizeof(edict_t *));
    ARRAY_COUNT(captain->units) = 2;
    captain->units[0] = first; captain->units[1] = second;
    unit_entercombat(first, enemy); unit_entercombat(second, enemy);

    level.time = 1000;
    G_BotUpdateGroupFlee(player);
    T_EQ(captain->state, BOT_CAPTAIN_ACTIVE);
    T_ASSERT(captain->disadvantage_active);

    level.time = 3499;
    G_BotUpdateGroupFlee(player);
    T_EQ(captain->state, BOT_CAPTAIN_ACTIVE);

    level.time = 3500;
    G_BotUpdateGroupFlee(player);
    T_EQ(captain->state, BOT_CAPTAIN_RETREATING);
    T_ASSERT(G_BotCaptainRetreating(player));
    T_NULL(first->combatentity); T_NULL(second->combatentity);
    T_NOT_NULL(first->currentmove); T_EQ(first->currentmove->proc, CAbilityMove);
    T_NOT_NULL(second->currentmove); T_EQ(second->currentmove->proc, CAbilityMove);
    T_NOT_NULL(first->goalentity); T_EQ(second->goalentity, first->goalentity);
    T_FEQ(first->goalentity->s.origin2.x, 0, 0.001f);
    T_FEQ(first->goalentity->s.origin2.y, 0, 0.001f);

    first->s.origin2 = MAKE(vec2_t, 32, 0);
    second->s.origin2 = MAKE(vec2_t, 64, 0);
    level.time = 3600;
    G_BotUpdateGroupFlee(player);
    T_EQ(captain->state, BOT_CAPTAIN_IDLE);
    T_ASSERT(!G_BotCaptainRetreating(player));
}

TEST(wc3_bot, group_flee_policy_disabled_does_not_enter_retreat) {
    static UnitBalance_t const friendly_balance = { .maxHealth = 100, .level = 1 };
    static UnitBalance_t const enemy_balance = { .maxHealth = 100, .level = 10 };
    static UnitWeapons_t const enabled_attack = { .attacksEnabled = 1 };
    bot_t *bot = level.bots + 2;
    player_t *player = &game.clients[2].ps;
    botCaptain_t *captain = bot->captains + BOT_CAPTAIN_ATTACK;
    edict_t *member, *enemy;

    reset_entities();
    G_BotInitAssault(player);
    member = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 512, 0, 2, NULL);
    enemy = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 544, 0, 1, NULL);
    member->data.UnitBalance = &friendly_balance; enemy->data.UnitBalance = &enemy_balance; enemy->data.UnitWeapons = &enabled_attack;
    member->health.value = member->health.max_value = 100;
    enemy->health.value = enemy->health.max_value = 100;
    bot->player = player; bot->flags = 0;
    captain->state = BOT_CAPTAIN_ACTIVE;
    captain->units = gi.MemAlloc(sizeof(edict_t *)); ARRAY_COUNT(captain->units) = 1; captain->units[0] = member;
    unit_entercombat(member, enemy);

    level.time = 1000; G_BotUpdateGroupFlee(player);
    level.time = 10000; G_BotUpdateGroupFlee(player);
    T_EQ(captain->state, BOT_CAPTAIN_ACTIVE);
    T_ASSERT(!captain->disadvantage_active);
}

TEST(wc3_bot, retreating_captain_rejects_attack_refresh_until_home) {
    bot_t *bot = level.bots + 2;
    player_t *player = &game.clients[2].ps;
    botCaptain_t *captain = bot->captains + BOT_CAPTAIN_ATTACK;
    edict_t *member, *target, *retreat_goal;

    reset_entities();
    G_BotInitAssault(player);
    member = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 512, 0, 2, NULL);
    target = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 900, 0, 1, NULL);
    bot->player = player; captain->state = BOT_CAPTAIN_RETREATING; captain->home = MAKE(vec2_t, 0, 0);
    captain->units = gi.MemAlloc(sizeof(edict_t *)); ARRAY_COUNT(captain->units) = 1; captain->units[0] = member;
    G_BotUpdateGroupFlee(player);
    retreat_goal = member->goalentity;
    T_NOT_NULL(retreat_goal);

    G_BotAttackMoveKill(player, target);
    T_EQ(captain->state, BOT_CAPTAIN_RETREATING);
    T_EQ(member->goalentity, retreat_goal);
    T_FEQ(captain->goal.x, 0, 0.001f);
    T_FEQ(captain->goal.y, 0, 0.001f);
}

TEST(wc3_bot, attack_move_kill_orders_live_assault_members_toward_current_target_position) {
    bot_t *bot = level.bots + 2;
    edict_t *first = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 0, 0, 2, NULL);
    edict_t *second = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 32, 0, 2, NULL);
    edict_t *dead = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 64, 0, 2, NULL);
    edict_t *target = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 512, 256, 1, NULL);
    edict_t *waypoint;

    dead->health.value = 0;
    bot->player = &game.clients[2].ps;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(3 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 3;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = first;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = second;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = dead;

    G_BotAttackMoveKill(&game.clients[2].ps, target);

    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_ACTIVE);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.x, 512, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.y, 256, 0.001f);
    waypoint = first->goalentity;
    T_NOT_NULL(waypoint);
    T_EQ(second->goalentity, waypoint);
    T_NULL(dead->goalentity);
    T_FEQ(waypoint->s.origin2.x, 512, 0.001f);
    T_FEQ(waypoint->s.origin2.y, 256, 0.001f);

    target->s.origin2 = MAKE(vec2_t, 768, -64);
    G_BotAttackMoveKill(&game.clients[2].ps, target);
    T_FEQ(first->goalentity->s.origin2.x, 768, 0.001f);
    T_FEQ(first->goalentity->s.origin2.y, -64, 0.001f);

    target->health.value = 0;
    waypoint = first->goalentity;
    G_BotAttackMoveKill(&game.clients[2].ps, target);
    T_EQ(first->goalentity, waypoint);
}

TEST(wc3_bot, captain_in_combat_selects_roster_and_clears_stale_targets) {
    bot_t *bot = level.bots + 2;
    edict_t *attack = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *defense = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    attack->health.value = defense->health.value = enemy->health.value = 100;
    attack->s.player = defense->s.player = 2; enemy->s.player = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(edict_t *));
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = attack;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = defense;
    attack->combatentity = enemy;

    T_ASSERT(G_BotCaptainInCombat(&game.clients[2].ps, true));
    T_ASSERT(!G_BotCaptainInCombat(&game.clients[2].ps, false));
    enemy->inuse = false;
    T_ASSERT(!G_BotCaptainInCombat(&game.clients[2].ps, true)); T_NULL(attack->combatentity);
}

TEST(wc3_bot, add_defenders_fills_idempotently_from_completed_owned_units) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *first = alloc_test_unit(type, 0, 0), *second = alloc_test_unit(type, 32, 0);
    edict_t *training = alloc_test_unit(type, 64, 0), *other = alloc_test_unit(type, 96, 0);
    first->s.player = second->s.player = training->s.player = 2; other->s.player = 1;
    first->health.value = second->health.value = training->health.value = other->health.value = 100;
    training->training = true;

    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_EQ(bot->captains[BOT_CAPTAIN_DEFENSE].units[0], first);
    T_EQ(bot->captains[BOT_CAPTAIN_DEFENSE].units[1], second);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_ASSERT(!G_BotAddDefenders(&game.clients[2].ps, 3, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 0, type));
}

TEST(wc3_bot, assault_init_retains_captains_and_fill_tracks_formation_result) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *first = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    edict_t *second = make_bot_harvest_unit(type, 32, 0, 2, NULL);
    edict_t *building = make_bot_harvest_unit(type, 64, 0, 2, NULL);
    edict_t *enemy = make_bot_harvest_unit(type, 96, 0, 1, NULL);
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);

    G_BotCreateCaptains(&game.clients[2].ps);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 1, type));
    /* The original owned pool admits newest first; the defense captain owns second. */
    T_EQ(bot->captains[BOT_CAPTAIN_DEFENSE].units[0],second);
    G_BotSetCaptainHome(&game.clients[2].ps, 1, 128, 256);
    G_BotInitAssault(&game.clients[2].ps);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].home.x, 128, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].home.y, 256, 0.001f);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_FORMING);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 1);
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units[0], first);
    T_ASSERT(!G_BotCaptainIsFull(&game.clients[2].ps));
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);
    T_ASSERT(!G_BotCaptainIsFull(&game.clients[2].ps));
    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_ACTIVE;
    bot->captains[BOT_CAPTAIN_ATTACK].goal = (vec2_t){-321,456};
    edict_t **members = bot->captains[BOT_CAPTAIN_ATTACK].units;
    G_BotInitAssault(&game.clients[2].ps);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units, members);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);T_EQ(members[0], first);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_ACTIVE);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].home.x, 128, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].home.y, 256, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.x, -321, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.y, 456, 0.001f);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 1);
    T_ASSERT(first != second && building != enemy);
}

TEST(wc3_bot, add_assault_duplicate_entries_reconcile_retained_type_total) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');

    reset_entities();
    FOR_LOOP(i, 5) make_bot_harvest_unit(type, (float)i * 32.0f, 0, 2, NULL);
    G_BotCreateCaptains(&game.clients[2].ps);
    G_BotInitAssault(&game.clients[2].ps);

    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 3, type));
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 3);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 3);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
}

TEST(wc3_bot, set_assault_group_form_group_uses_max_with_retained_type_total) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');

    reset_entities();
    FOR_LOOP(i, 6) {
        edict_t *unit = make_bot_harvest_unit(type, (float)i * 32.0f, 0, 2, NULL);
        unit->svflags |= SVF_MONSTER;
    }

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_set_assault_group.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 5);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 5);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    G_BotStop(2);
}

TEST(wc3_bot, suicide_player_launches_full_and_timeout_partial_assaults_at_target_player) {
    bot_t *bot = level.bots + 2;
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *first = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    edict_t *second = make_bot_harvest_unit(type, 32, 0, 2, NULL);
    edict_t *enemy = make_bot_harvest_unit(type, 256, 128, 1, NULL);
    player_t *target = &game.clients[1].ps;

    enemy->svflags |= SVF_MONSTER;
    mapinfo->players[1].used = true;
    mapinfo->players[1].startingPosition = MAKE(vec2_t, 256, 128);
    bot->player = &game.clients[2].ps;
    G_BotSetStagePoint(&game.clients[2].ps, 256, 128);
    G_BotCreateCaptains(&game.clients[2].ps);
    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 3, type));
    T_ASSERT(!G_BotSuicidePlayer(&game.clients[2].ps, PLAYER_NUM(target), true));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_FORMING);

    T_ASSERT(G_BotSuicidePlayer(&game.clients[2].ps, PLAYER_NUM(target), false));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_ACTIVE);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.x, 256, 0.001f);
    T_FEQ(bot->captains[BOT_CAPTAIN_ATTACK].goal.y, 128, 0.001f);
    T_EQ(first->goalentity, enemy);
    T_EQ(second->goalentity, enemy);
    T_ASSERT(enemy != NULL);

    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 3, type));
    T_ASSERT(G_BotSuicidePlayer(&game.clients[2].ps, PLAYER_NUM(target), false));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_ACTIVE);
}

TEST(wc3_bot, suicide_player_skips_nearest_hidden_target_for_visible_enemy) {
    bot_t *bot = level.bots + 2;
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;
    uint32_t const type = MAKEFOURCC('h','f','o','o');
    edict_t *unit = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    edict_t *hidden = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 64, 0, 1, NULL);
    edict_t *visible = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 256, 0, 1, NULL);

    hidden->svflags |= SVF_MONSTER;
    visible->svflags |= SVF_MONSTER;
    hidden->s.renderfx |= RF_HIDDEN;
    mapinfo->players[1].used = true;
    mapinfo->players[1].startingPosition = visible->s.origin2;
    bot->player = &game.clients[2].ps;
    G_BotSetStagePoint(&game.clients[2].ps, visible->s.origin2.x, visible->s.origin2.y);
    G_BotCreateCaptains(&game.clients[2].ps);
    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 1, type));

    T_ASSERT(S_UnitIsHiddenFromPlayer(hidden, 2));
    /* Keep the hidden unit nearer so the regression fails if selection ignores visibility. */
    T_ASSERT(Vector2_distance(&unit->s.origin2, &hidden->s.origin2) <
             Vector2_distance(&unit->s.origin2, &visible->s.origin2));
    T_ASSERT(G_BotSuicidePlayer(&game.clients[2].ps, 1, false));

    T_EQ(unit->goalentity, visible);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_bot, suicide_player_native_runs_in_player_bound_ai_vm) {
    bot_t *bot = level.bots + 2;
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;
    edict_t *unit = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 0, 0, 2, NULL);
    edict_t *enemy = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 256, 128, 1, NULL);

    enemy->svflags |= SVF_MONSTER;
    mapinfo->players[1].used = true;
    mapinfo->players[1].startingPosition = MAKE(vec2_t, 256, 128);
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_suicide_player.ai", BOT_CAMPAIGN));
    G_BotCreateCaptains(&game.clients[2].ps);
    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 1, MAKEFOURCC('h','f','o','o')));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_ACTIVE);
    T_EQ(unit->goalentity, enemy);
    T_ASSERT(enemy != NULL);
}

TEST(wc3_bot, public_captain_go_home_near_home_keeps_the_actor_and_roster_request) {
    setup_test_world();
    player_t *player=&game.clients[2].ps;
    edict_t *unit=make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'),32,64,2,NULL);
    T_ASSERT(G_BotStart(player,"test_captain_go_home_near.ai",BOT_CAMPAIGN));
    G_BotRunFrame();
    botCaptain_t const *captain=level.bots[2].captains+BOT_CAPTAIN_ATTACK;
    T_NOT_NULL(level.bots[2].vm); T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_NOT_NULL(captain->home_actor);
    T_EQ(captain->home_actor->s.origin2.x,128); T_EQ(captain->home_actor->s.origin2.y,256);
    T_EQ(captain->home_actor->movement.group_id,0);
    T_EQ(unit->movement.captain_home.home.x,192);
    T_EQ(unit->movement.captain_home.roster_actor,captain->home_actor);
    T_ASSERT(unit->movement.captain_home.active);
    /* Recruitment can transfer a point owner to Follow. Near-home GoHome
     * must preserve the resulting owner, independent of that allocation. */
    uint64_t before=level.next_move_group_sequence;
    uint32_t group=unit->movement.group_id;
    edict_t *goal=unit->goalentity;
    T_NE(group,0);
    FOR_LOOP(i,2) {
        G_BotCaptainGoHome(player);
        T_EQ(level.next_move_group_sequence,before);
        T_EQ(unit->movement.group_id,group);
        T_EQ(unit->goalentity,goal);
        T_EQ(captain->home_actor->s.origin2.x,128);
        T_EQ(captain->home_actor->s.origin2.y,256);
        T_EQ(captain->home_actor->movement.group_id,0);
        T_EQ(unit->movement.captain_home.home.x,192);
        T_EQ(unit->movement.captain_home.roster_actor,captain->home_actor);
        T_ASSERT(unit->movement.captain_home.active);
    }
}

TEST(wc3_bot, captain_full_tracks_formation_retry_without_reordering_retained_members) {
    player_t *player = &game.clients[2].ps;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *unit = make_bot_harvest_unit(type, 32, 64, 2, NULL);
    G_BotCreateCaptains(player);
    T_ASSERT(!G_BotCaptainIsFull(player));
    G_BotSetCaptainHome(player, 1, 192, 288);
    G_BotInitAssault(player);
    T_ASSERT(G_BotCaptainIsFull(player));
    T_ASSERT(G_BotAddAssault(player, 1, type));
    T_ASSERT(G_BotCaptainIsFull(player));
    edict_t *goal = unit->goalentity;
    T_NOT_NULL(goal);
    T_ASSERT(!G_BotAddAssault(player, 2, type));
    T_ASSERT(!G_BotCaptainIsFull(player));
    T_ASSERT(G_BotAddAssault(player, 1, type));
    T_ASSERT(!G_BotCaptainIsFull(player));
    G_BotInitAssault(player);
    T_ASSERT(G_BotCaptainIsFull(player));
    T_EQ(G_BotCaptainGroupSize(player), 1);
    T_EQ(unit->goalentity, goal);
    T_EQ(unit->current_order_id, G_OrderId("move"));
    T_ASSERT(G_BotAddAssault(player, 1, type));
    T_ASSERT(G_BotCaptainIsFull(player));
    T_EQ(unit->goalentity, goal);
    G_BotCreateCaptains(player);
    T_ASSERT(!G_BotCaptainIsFull(player));
    T_EQ(G_BotCaptainGroupSize(player), 0);
}

TEST(wc3_bot, captain_size_counts_live_members_separately_from_formation_full) {
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *first = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    edict_t *second = make_bot_harvest_unit(type, 32, 0, 2, NULL);

    G_BotCreateCaptains(&game.clients[2].ps);
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 0);
    T_ASSERT(!G_BotCaptainIsFull(&game.clients[2].ps));
    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 2);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    first->svflags |= SVF_DEADMONSTER;
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 1);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    second->inuse = false;
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 0);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
}

TEST(wc3_bot, remove_injuries_drops_sub_half_health_assault_members_and_sends_them_home) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *hall;
    edict_t *injured = make_bot_harvest_unit(type, 256, 0, 2, NULL);
    edict_t *half = make_bot_harvest_unit(type, 288, 0, 2, NULL);
    edict_t *healthy = make_bot_harvest_unit(type, 320, 0, 2, NULL);
    edict_t *dead = make_bot_harvest_unit(type, 352, 0, 2, NULL);

    reset_entities();
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    injured = make_bot_harvest_unit(type, 256, 0, 2, NULL);
    half = make_bot_harvest_unit(type, 288, 0, 2, NULL);
    healthy = make_bot_harvest_unit(type, 320, 0, 2, NULL);
    dead = make_bot_harvest_unit(type, 352, 0, 2, NULL);
    injured->health.value = 499;
    half->health.value = 500;
    healthy->health.value = 900;
    dead->health.value = 0;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(4 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 4;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = injured;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = half;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = dead;
    bot->captains[BOT_CAPTAIN_ATTACK].units[3] = healthy;

    G_BotRemoveInjuries(&game.clients[2].ps);

    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 2);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units[0], half);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units[1], healthy);
    T_NOT_NULL(injured->currentmove);
    T_EQ(injured->currentmove->proc, CAbilityMove);
    T_NOT_NULL(injured->goalentity);
    T_FEQ(injured->goalentity->s.origin2.x, hall->s.origin2.x, 0.001f);
    T_FEQ(injured->goalentity->s.origin2.y, hall->s.origin2.y, 0.001f);
    T_NULL(half->currentmove);
    T_NULL(healthy->currentmove);
}

TEST(wc3_bot, remove_injuries_native_runs_in_player_bound_ai_vm) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *hall, *injured;

    reset_entities();
    setup_test_world();
    InitUnitData();
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    injured = make_bot_harvest_unit(type, 256, 0, 2, NULL);

    injured->health.value = 250;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = injured;
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_remove_injuries.ai", BOT_CAMPAIGN));

    G_BotRunFrame();

    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 0);
    /* Script startup preserves Town membership, so RemoveInjuries now
     * actually withdraws this member and admits its existing return policy. */
    T_NOT_NULL(injured->goalentity);
    if (injured->goalentity) {
        T_FEQ(injured->goalentity->s.origin2.x,hall->s.origin2.x,0.001f);
        T_FEQ(injured->goalentity->s.origin2.y,hall->s.origin2.y,0.001f);
    }
    T_NULL(hall->goalentity);
}

TEST(wc3_bot, remove_siege_drops_siege_attack_members_without_ordering_them) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *siege1 = make_bot_harvest_unit(type, 256, 0, 2, NULL);
    edict_t *siege2 = make_bot_harvest_unit(type, 288, 0, 2, NULL);
    edict_t *normal = make_bot_harvest_unit(type, 320, 0, 2, NULL);
    edict_t *dead = make_bot_harvest_unit(type, 352, 0, 2, NULL);

    S_AttackProfileWrite(siege1, 0)->type = ATK_SIEGE;
    S_AttackProfileWrite(siege2, 1)->type = ATK_SIEGE;
    S_AttackProfileWrite(normal, 0)->type = ATK_NORMAL;
    dead->health.value = 0;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(4 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 4;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = siege1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = normal;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = dead;
    bot->captains[BOT_CAPTAIN_ATTACK].units[3] = siege2;

    G_BotRemoveSiege(&game.clients[2].ps);

    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units[0], normal);
    T_NULL(siege1->currentmove);
    T_NULL(siege2->currentmove);
    T_NULL(normal->currentmove);
}

TEST(wc3_bot, remove_siege_native_runs_in_player_bound_ai_vm) {
    bot_t *bot = level.bots + 2;
    edict_t *siege = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 256, 0, 2, NULL);

    S_AttackProfileWrite(siege, 0)->type = ATK_SIEGE;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = siege;
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_remove_siege.ai", BOT_CAMPAIGN));

    G_BotRunFrame();

    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 0);
    T_NULL(siege->currentmove);
}

TEST(wc3_bot, captain_readiness_uses_lower_hero_and_unit_aggregate) {
    static UnitBalance_t hero_balance = { .strength = 1 };
    static UnitBalance_t unit_balance = {0};
    bot_t *bot = level.bots + 2;
    edict_t *hero = make_bot_harvest_unit(MAKEFOURCC('H','p','a','l'), 0, 0, 2, NULL);
    edict_t *first = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 32, 0, 2, NULL);
    edict_t *second = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 64, 0, 2, NULL);
    hero->data.UnitBalance = &hero_balance; first->data.UnitBalance = second->data.UnitBalance = &unit_balance;
    hero->health.value = 333; hero->health.max_value = 1000;
    first->health.value = 100; first->health.max_value = 100;
    second->health.value = 50; second->health.max_value = 100;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(3 * sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 3;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = hero;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = first;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = second;

    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 33);
    hero->health.value = 1000;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 75);
    first->svflags |= SVF_DEADMONSTER; second->inuse = false;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 100);
}

TEST(wc3_bot, captain_readiness_treats_empty_and_zero_mana_categories_as_full) {
    static UnitBalance_t unit_balance = {0};
    bot_t *bot = level.bots + 2;
    edict_t *unit = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 0, 0, 2, NULL);
    unit->data.UnitBalance = &unit_balance;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 100);
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 100);
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = unit;
    unit->mana.value = unit->mana.max_value = 0;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 100);
    unit->mana.value = 2; unit->mana.max_value = 3;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 66);
    T_EQ(G_BotCaptainReadiness(NULL, true), 100);
}

TEST(wc3_bot, guard_posts_fill_typed_units_without_stealing_captain_members) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *captain = alloc_test_unit(type, 0, 0), *first = alloc_test_unit(type, 32, 0);
    edict_t *second = alloc_test_unit(type, 64, 0), *other = alloc_test_unit(type, 96, 0);
    captain->s.player = first->s.player = second->s.player = 2; other->s.player = 1;
    captain->health.value = first->health.value = second->health.value = other->health.value = 100;
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(sizeof(edict_t *));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 1;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = captain;

    G_BotAddGuardPost(&game.clients[2].ps, type, 100, 200);
    G_BotAddGuardPost(&game.clients[2].ps, type, 300, 400);
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(ARRAY_COUNT(bot->guards), 2);
    T_EQ(bot->guards[0].unit, first); T_EQ(bot->guards[1].unit, second);
    T_FEQ(bot->guards[0].origin.x, 100, 0.001f); T_FEQ(bot->guards[1].origin.y, 400, 0.001f);
    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, type), 3);
}

TEST(wc3_bot, guard_posts_replace_dead_members_and_leave_missing_types_empty) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *dead = alloc_test_unit(type, 0, 0), *replacement = alloc_test_unit(type, 32, 0);
    dead->s.player = replacement->s.player = 2; dead->health.value = replacement->health.value = 100;
    G_BotAddGuardPost(&game.clients[2].ps, type, 100, 200);
    G_BotAddGuardPost(&game.clients[2].ps, MAKEFOURCC('h','r','i','f'), 300, 400);
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, dead); T_NULL(bot->guards[1].unit);
    dead->health.value = 0;
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, replacement); T_NULL(bot->guards[1].unit);
}

TEST(wc3_bot, return_guard_posts_moves_idle_units_but_preserves_combat) {
    bot_t *bot = level.bots + 2;
    uint32_t type = MAKEFOURCC('h','f','o','o');
    edict_t *idle = alloc_test_unit(type, 0, 0), *fighting = alloc_test_unit(type, 0, 32);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    idle->s.player = fighting->s.player = 2; enemy->s.player = 1;
    idle->health.value = fighting->health.value = enemy->health.value = 100;
    idle->stand = fighting->stand = unit_stand; fighting->combatentity = enemy;
    G_BotAddGuardPost(&game.clients[2].ps, type, 256, 0);
    G_BotAddGuardPost(&game.clients[2].ps, type, 256, 32);
    G_BotFillGuardPosts(&game.clients[2].ps);
    G_BotReturnGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, idle); T_EQ(idle->currentmove->proc, CAbilityMove);
    T_FEQ(idle->goalentity->s.origin2.x, 256, 0.001f);
    T_EQ(bot->guards[1].unit, fighting); T_NULL(fighting->currentmove);
    G_BotReturnGuardPosts(&game.clients[3].ps);
    T_EQ(ARRAY_COUNT(level.bots[3].guards), 0);
}

TEST(wc3_bot, command_stack_is_player_owned_and_consumed_by_ai_natives) {
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_bot_commands.ai", BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call CommandAI(Player(2),10,20)\ncall CommandAI(Player(2),30,40)\n"
        "call CommandAI(Player(1),50,60)\nendfunction"));
    T_EQ(G_BotCommandsWaiting(&game.clients[2].ps), 2);
    T_EQ(G_BotCommandsWaiting(&game.clients[1].ps), 1);
    T_EQ(G_BotLastCommand(&game.clients[1].ps), 50);
    T_EQ(G_BotLastData(&game.clients[1].ps), 60);

    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_EQ(G_BotCommandsWaiting(&game.clients[2].ps), 0);
    T_EQ(G_BotCommandsWaiting(&game.clients[1].ps), 1);
}

TEST(wc3_bot, convert_units_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_convert_units.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, town_threatened_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_town_threatened.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, is_towered_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_is_towered.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, get_mega_target_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_get_mega_target.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, get_enemy_expansion_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_get_enemy_expansion.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, enemy_base_discovery_natives_are_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    level.time = 1000;
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_get_enemy_base.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, enemy_base_discovery_requires_start_waits_then_returns_nearest_hostile_town) {
    player_t *caller = &game.clients[2].ps;
    edict_t *home, *far, *near;

    reset_entities();
    InitUnitData();
    home = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    far = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 5000, 0, 1, &bot_hall_abilities);
    near = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 2500, 0, 3, &bot_hall_abilities);
    level.time = 1000;

    T_NULL(G_BotGetEnemyBase(caller));
    G_BotStartGetEnemyBase(caller);
    T_NULL(G_BotGetEnemyBase(caller));
    T_ASSERT(G_BotWaitGetEnemyBase(caller));
    level.time = 1999;
    T_ASSERT(G_BotWaitGetEnemyBase(caller));
    level.time = 2000;
    T_ASSERT(!G_BotWaitGetEnemyBase(caller));
    T_ASSERT(G_BotGetEnemyBase(caller) == near);

    near->health.value = 0;
    T_NULL(G_BotGetEnemyBase(caller));
    G_BotStartGetEnemyBase(caller);
    level.time = 3000;
    T_ASSERT(!G_BotWaitGetEnemyBase(caller));
    T_ASSERT(G_BotGetEnemyBase(caller) == far);
    (void)home;
}

TEST(wc3_bot, get_enemy_expansion_returns_nearest_hostile_non_primary_town) {
    player_t *caller = &game.clients[2].ps;
    player_t *enemy = &game.clients[1].ps;
    edict_t *caller_hall, *enemy_main, *enemy_far, *enemy_near;

    reset_entities();
    InitUnitData();
    caller_hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    enemy_main = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 3000, 0, 1, &bot_hall_abilities);
    enemy_far = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 5000, 0, 1, &bot_hall_abilities);
    enemy_near = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 2000, 0, 1, &bot_hall_abilities);

    T_ASSERT(G_BotGetEnemyExpansion(caller) == enemy_near);
    G_SetPlayerAlliance(caller, enemy, ALLIANCE_PASSIVE, true);
    T_NULL(G_BotGetEnemyExpansion(caller));

    (void)caller_hall; (void)enemy_main; (void)enemy_far;
}

TEST(wc3_bot, get_enemy_expansion_ignores_enemy_primary_town) {
    player_t *caller = &game.clients[2].ps;

    reset_entities();
    InitUnitData();
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 3000, 0, 1, &bot_hall_abilities);
    T_NULL(G_BotGetEnemyExpansion(caller));
}

TEST(wc3_bot, get_creep_camp_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_get_creep_camp.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, get_creep_camp_filters_total_level_flyers_and_nearest_camp) {
    static UnitBalance_t const level2 = { .maxHealth = 100, .level = 2 };
    static UnitBalance_t const level3 = { .maxHealth = 100, .level = 3 };
    static UnitData_t const ground_data = { .moveTypeName = "foot" };
    static UnitData_t const fly_data = { .moveTypeName = "fly" };
    player_t *player = &game.clients[2].ps;
    edict_t *hall, *near_a, *near_b, *far_a, *far_b;

    reset_entities();
    InitUnitData();
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    near_a = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1000, 0);
    near_b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1300, 0);
    far_a = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3000, 0);
    far_b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3300, 0);
    for (edict_t *u = near_a; u; u = u == near_a ? near_b : u == near_b ? far_a : u == far_a ? far_b : NULL) {
        u->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
        u->svflags |= SVF_MONSTER;
        u->health.value = u->health.max_value = 100;
        u->data.UnitData = &ground_data;
    }
    near_a->data.UnitBalance = &level2;
    near_b->data.UnitBalance = &level3; /* near camp power 5 */
    far_a->data.UnitBalance = &level3;
    far_b->data.UnitBalance = &level3;  /* far camp power 6 */

    T_ASSERT(G_BotGetCreepCamp(player, 5, 6, false) == near_a);
    T_ASSERT(G_BotGetCreepCamp(player, 6, 6, false) == far_a);
    T_NULL(G_BotGetCreepCamp(player, 7, 9, false));

    near_b->data.UnitData = &fly_data;
    T_ASSERT(G_BotGetCreepCamp(player, 5, 6, false) == far_a);
    T_ASSERT(G_BotGetCreepCamp(player, 5, 5, true) == near_a);
    (void)hall;
}

TEST(wc3_bot, purchase_zeppelin_native_is_registered_for_player_bound_ai) {
    bot_t *bot = level.bots + 2;

    reset_entities();
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_purchase_zeppelin.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, purchase_zeppelin_requires_nearby_hero_and_uses_neutral_shop_purchase) {
    static UnitProfile_t lab_profile;
    static UnitBalance_t hero_balance = { .strength = 1 };
    static UnitAbilities_t const lab_abilities = { .abilList = "Aneu,Asud", .heroAbilList = "" };
    player_t *player = &game.clients[2].ps;
    gameClient_t *client = &game.clients[2];
    edict_t *lab, *hero;
    uint32_t zeppelins = 0;

    reset_entities();
    setup_test_world();
    InitUnitData();
    client->ps.number = 2;
    {
        edict_t *player_ent = &g_edicts[2];
        player_ent->client = client;
        client->connected = true;
    }
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 20;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    memset(&lab_profile, 0, sizeof(lab_profile));
    lab_profile.sellUnits = "nzep";
    lab = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    lab->s.player = PLAYER_NEUTRAL_PASSIVE;
    lab->data.UnitProfile = &lab_profile;
    lab->data.UnitAbilities = &lab_abilities;
    if (!lab->stock) lab->stock = G_AllocStock();
    assert(lab->stock);
    lab->stock->unit_slots = 11;
    lab->spawn_time = G_Time();
    lab->collision = 32.0f;
    gi.LinkEntity(lab);
    lab->stock->units_initialized = true;
    lab->stock->unit_count = 1;
    lab->stock->units[0] = (shopStockItem_t){ .id = MAKEFOURCC('n','z','e','p'), .current = 1, .maximum = 1 };
    lab->movetype = MOVETYPE_NONE;

    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 1000, 0);
    hero->s.player = 2;
    hero->collision = 16.0f;
    hero->data.UnitBalance = &hero_balance;
    hero->svflags |= SVF_MONSTER;
    hero->movetype = MOVETYPE_STEP;
    hero->health.value = hero->health.max_value = 1000.0f;
    S_AttackProfileWrite(hero, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    hero->targtype = TARG_GROUND;
    gi.LinkEntity(hero);

    G_BotPurchaseZeppelin(player);
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == 2 && unit->class_id == MAKEFOURCC('n','z','e','p')) zeppelins++;
    T_EQ(zeppelins, 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1000);

    hero->s.origin2 = MAKE(vec2_t, 64, 0);
    hero->s.origin.x = 64;
    gi.LinkEntity(hero);
    G_BotPurchaseZeppelin(player);

    zeppelins = 0;
    FILTER_EDICTS(unit, G_BotUnitAlive(unit) && unit->s.player == 2 && unit->class_id == MAKEFOURCC('n','z','e','p')) zeppelins++;
    T_EQ(zeppelins, 1);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 760);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 440);
    T_EQ(lab->stock->units[0].current, 0);
}

TEST(wc3_bot, get_mega_target_requires_watch_and_vulnerable_hostile_main) {
    static UnitWeapons_t const enabled_attack = { .attacksEnabled = 1 };
    player_t *caller = &game.clients[2].ps;
    player_t *enemy = &game.clients[1].ps;
    edict_t *caller_hall, *enemy_hall, *worker, *defender, *tower;

    reset_entities();
    InitUnitData();
    caller_hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    enemy_hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 3000, 0, 1, &bot_hall_abilities);

    level.bots[2].flags &= ~BOT_WATCH_MEGA;
    T_NULL(G_BotGetMegaTarget(caller));
    level.bots[2].flags |= BOT_WATCH_MEGA;
    T_ASSERT(G_BotGetMegaTarget(caller) == enemy_hall);

    worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 3072, 0, 1, &bot_harvester_abilities);
    worker->data.UnitWeapons = &enabled_attack; S_AttackProfileWrite(worker, 0)->type = ATK_NORMAL;
    T_ASSERT(G_BotGetMegaTarget(caller) == enemy_hall); /* economy workers alone do not protect the main */

    defender = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 3100, 0, 1, NULL);
    defender->data.UnitWeapons = &enabled_attack; S_AttackProfileWrite(defender, 0)->type = ATK_NORMAL;
    T_NULL(G_BotGetMegaTarget(caller));
    defender->s.origin2.x = 5000;
    T_ASSERT(G_BotGetMegaTarget(caller) == enemy_hall);

    tower = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 3200, 0, 1, NULL);
    tower->data.UnitWeapons = &enabled_attack; S_AttackProfileWrite(tower, 0)->type = ATK_PIERCE;
    T_NULL(G_BotGetMegaTarget(caller));
    S_AttackProfileWrite(tower, 0)->type = ATK_NONE;
    T_ASSERT(G_BotGetMegaTarget(caller) == enemy_hall);

    G_SetPlayerAlliance(caller, enemy, ALLIANCE_PASSIVE, true);
    T_NULL(G_BotGetMegaTarget(caller));
    (void)caller_hall;
}

TEST(wc3_bot, is_towered_requires_nearby_base_and_attack_capable_defending_building) {
    static UnitWeapons_t const enabled_attack = { .attacksEnabled = 1 };
    static UnitWeapons_t const no_attack = { .attacksEnabled = 0 };
    player_t *caller = &game.clients[2].ps;
    edict_t *hall, *target, *tower;

    reset_entities();
    InitUnitData();
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 1, &bot_hall_abilities);
    target = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 128, 0, 1, NULL);
    tower = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 256, 0, 1, NULL);
    if (!tower->ancient_root) tower->ancient_root = G_AllocAncientRoot();
    assert(tower->ancient_root);
    tower->ancient_root->ability = 0;
    hall->data.UnitAbilities = &bot_hall_abilities;
    tower->data.UnitWeapons = &enabled_attack; S_AttackProfileWrite(tower, 0)->type = ATK_PIERCE; S_AttackProfileWrite(tower, 0)->range = 0;

    T_ASSERT(G_BotIsTowered(caller, target));

    /* Authored attack/acquisition ranges are not part of retail IsTowered classification. */
    S_AttackProfileWrite(tower, 0)->range = 99999; tower->runtime.acquisition_range = 1;
    T_ASSERT(G_BotIsTowered(caller, target));

    /* The target owner changes with the tower so the query tests its ownership filter. */
    target->s.player = 2;
    tower->s.player = 3;
    T_ASSERT(!G_BotIsTowered(caller, target));
    tower->s.player = 1;
    target->s.player = 1;

    S_AttackProfileWrite(tower, 0)->type = ATK_NONE;
    S_AttackProfileWrite(tower, 1)->type = ATK_NONE;
    tower->data.UnitWeapons = &no_attack;
    target->s.player = 2;
    T_ASSERT(!G_BotIsTowered(caller, target));
    target->s.player = 1;

    target->s.player = 2;
    target->s.origin2.x = 2048;
    T_ASSERT(!G_BotIsTowered(caller, target));
    (void)hall;
}

TEST(wc3_bot, town_threatened_tracks_active_hostile_attacks_on_any_owned_unit) {
    player_t *player = &game.clients[2].ps;
    edict_t *unit, *building, *enemy, *friendly;

    reset_entities();
    InitUnitData();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 128, 0);
    friendly = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 192, 0);
    unit->s.player = building->s.player = friendly->s.player = 2;
    enemy->s.player = 1;
    unit->svflags |= SVF_MONSTER;
    building->svflags |= SVF_MONSTER;
    building->s.flags |= EF_BUILDING;
    enemy->svflags |= SVF_MONSTER;
    friendly->svflags |= SVF_MONSTER;
    enemy->stand = friendly->stand = unit_stand;
    unit->data.UnitWeapons = enemy->data.UnitWeapons = friendly->data.UnitWeapons = NULL;
    unit->targtype = building->targtype = TARG_GROUND;
    S_AttackProfileWrite(enemy, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(enemy, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE;
    enemy->targtype = TARG_GROUND;
    enemy->data.UnitWeapons = &(UnitWeapons_t const){ .attacksEnabled = 1 };
    G_SetPlayerAlliance(&game.clients[1].ps, player, ALLIANCE_PASSIVE, false);
    G_SetPlayerAlliance(player, &game.clients[1].ps, ALLIANCE_PASSIVE, false);
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeComputer;
    ((mapInfo_t *)level.mapinfo)->players[2].playerType = kPlayerTypeHuman;

    T_ASSERT(!G_BotTownThreatened(player));
    order_attack(enemy, unit);
    T_ASSERT(S_AttackCanTarget(enemy, unit));
    T_ASSERT(enemy->currentmove && enemy->currentmove->proc == CAbilityAttack);
    T_EQ(enemy->goalentity, unit);
    T_ASSERT(G_BotTownThreatened(player));
    order_stop(enemy);
    T_ASSERT(!G_BotTownThreatened(player));

    order_attack(enemy, building);
    T_ASSERT(G_BotTownThreatened(player));
    order_stop(enemy);

    /* Friendly force-fire may install attack behavior, but it is not a hostile threat. */
    order_attack(friendly, unit);
    T_ASSERT(!G_BotTownThreatened(player));
}

TEST(wc3_bot, town_threatened_ignores_owned_units_attacking_outward) {
    player_t *player = &game.clients[2].ps;
    edict_t *owned, *enemy;

    reset_entities();
    InitUnitData();
    owned = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
    owned->s.player = 2; enemy->s.player = 1;
    order_attack(owned, enemy);
    T_ASSERT(!G_BotTownThreatened(player));
}

TEST(wc3_bot, convert_units_rejects_missing_conversion_ability) {
    player_t *player = &game.clients[2].ps;
    edict_t *source;

    reset_entities();
    source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    source->s.player = 2;
    source->data.UnitAbilities = NULL;
    T_ASSERT(!G_BotConvertUnits(player, 1, MAKEFOURCC('h','p','e','a')));
}

/* Conversion lookup must read the caller's rawcode through each ability source, not a shadowed macro local. */
TEST(wc3_bot, convert_units_finds_authored_added_and_ranked_abilities) {
    player_t *player = &game.clients[2].ps;
    UnitAbilities_t const abilities = { .abilList = "Aave" };
    uint32_t code = MAKEFOURCC('A','a','v','e'), source_id = MAKEFOURCC('h','p','e','a');
    FOR_LOOP(i, 3) {
        reset_entities();
        InitUnitData();
        edict_t *source = alloc_test_unit(source_id, 0, 0);
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
        source->s.player = target->s.player = 2;
        source->data.UnitAbilities = i ? NULL : &abilities;
        if (i == 1) T_ASSERT(G_ActorAddSkill(source, code));
        if (i == 2) source->heroabilities[0] = (heroability_t){ .code = code, .level = 1 };
        /* The desired target count is already present, so discovery succeeds without starting a transform. */
        T_ASSERT(G_BotConvertUnits(player, 1, source_id));
    }
}

TEST(wc3_bot, individual_flee_policy_moves_damaged_combat_unit_home) {
    player_t *player = &game.clients[2].ps;
    edict_t *hall, *unit, *enemy;
    reset_entities();
    memset(level.bots + 2, 0, sizeof(level.bots[2]));
    hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    unit = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 500, 0, 2, NULL);
    enemy = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 550, 0, 1, NULL);
    unit->health.max_value = 100; unit->health.value = 20;
    unit->svflags |= SVF_MONSTER;
    enemy->svflags |= SVF_MONSTER;
    S_AttackProfileWrite(unit, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(enemy, 0)->type = ATK_NORMAL;
    G_SetPlayerAlliance(player, &game.clients[1].ps, ALLIANCE_PASSIVE, false);
    level.bots[2].flags = BOT_UNITS_FLEE;
    level.time = 1000;

    G_BotUpdateIndividualFlee(player);

    T_NOT_NULL(hall);
    T_NOT_NULL(unit->currentmove);
    T_EQ(unit->currentmove->proc, CAbilityMove);
    T_NOT_NULL(unit->goalentity);
    T_ASSERT(Vector2_distance(&unit->goalentity->s.origin2, &hall->s.origin2) < 1.0f);
    T_NOT_NULL(enemy);
}

TEST(wc3_bot, defend_player_redirects_only_defense_captain_and_returns_home) {
    static umove_t attack_move = { "attack", NULL, NULL, CAbilityAttack };
    player_t *player = &game.clients[2].ps;
    player_t *ally = &game.clients[1].ps;
    edict_t *defender, *ally_unit, *attacker;
    reset_entities();
    memset(level.bots + 2, 0, sizeof(level.bots[2]));
    defender = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 0, 0, 2, NULL);
    ally_unit = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 400, 0, 1, NULL);
    attacker = make_bot_harvest_unit(MAKEFOURCC('o','g','r','u'), 450, 0, 0, NULL);
    G_SetPlayerAlliance(player, ally, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(ally, player, ALLIANCE_PASSIVE, true);
    G_BotCreateCaptains(player);
    T_ASSERT(G_BotAddDefenders(player, 1, defender->class_id));
    G_BotSetCaptainHome(player, 2, 0, 0);
    level.bots[2].flags = BOT_DEFEND_PLAYER;
    S_SetMoveGoal(attacker, &attacker->goalentity, ally_unit);
    M_SetMove(attacker,&attack_move);
    level.time = 1000;

    G_BotUpdateDefendPlayer(player);
    T_EQ(level.bots[2].captains[BOT_CAPTAIN_DEFENSE].state, BOT_CAPTAIN_ACTIVE);
    T_NOT_NULL(defender->currentmove);
    T_EQ(defender->currentmove->proc, CAbilityAttack);

    M_SetMove(attacker,NULL);
    level.time += 600;
    G_BotUpdateDefendPlayer(player);
    T_EQ(level.bots[2].captains[BOT_CAPTAIN_DEFENSE].state, BOT_CAPTAIN_IDLE);
    T_NOT_NULL(defender->currentmove);
    T_EQ(defender->currentmove->proc, CAbilityMove);
}

#endif /* BZ_TESTS */
