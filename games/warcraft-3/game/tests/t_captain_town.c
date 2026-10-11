#ifdef BZ_TESTS
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "shared/test.h"
#include "fixtures/retail_captain291.h"
void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t,float,float);

/* Controlled pre-call bridge positions; original binary executes all arithmetic.
 * These are not claims of replaying the preceding placement journey. */
TEST(wc3_bot, captain291_town_spot_refresh_matches_original_words) {
    FOR_LOOP(i,sizeof(captain291_refresh)/sizeof(*captain291_refresh)) {
        reset_entities();setup_test_world();
        typeof(*captain291_refresh) const *row=captain291_refresh+i;
        edict_t *building=alloc_test_unit(MAKEFOURCC('h','t','o','w'),wc3_float(row->building[0]),wc3_float(row->building[1]));
        edict_t *mine=alloc_test_unit(MAKEFOURCC('n','g','o','l'),wc3_float(row->mine[0]),wc3_float(row->mine[1]));
        building->s.origin=(vec3_t){wc3_float(row->building[0]),wc3_float(row->building[1]),wc3_float(row->building[2])};
        mine->s.origin=(vec3_t){wc3_float(row->mine[0]),wc3_float(row->mine[1]),wc3_float(row->mine[2])};
        botTownSpot_t spot={.attack_home={wc3_float(row->before[0]),wc3_float(row->before[1])},
            .defense_home={wc3_float(row->before[2]),wc3_float(row->before[3])},
            .building={row->has_building ? building : NULL,building->spawn_time},
            .mine={row->has_mine ? mine : NULL,mine->spawn_time}};
        G_BotRefreshTownSpot(&spot);
        float values[]={spot.attack_home.x,spot.attack_home.y,spot.defense_home.x,spot.defense_home.y};
        FOR_LOOP(k,4)T_EQ(wc3_float_bits(values[k]),row->after[k]);
        /* Stale identity is absent, independently of slot reuse/liveness. */
        vec2_t attack=spot.attack_home,defense=spot.defense_home;
        spot.building.spawn_time=building->spawn_time+1;spot.mine.spawn_time=mine->spawn_time+1;
        G_BotRefreshTownSpot(&spot);
        T_EQ(wc3_float_bits(spot.attack_home.x),wc3_float_bits(attack.x));
        T_EQ(wc3_float_bits(spot.defense_home.y),wc3_float_bits(defense.y));
    }
    reset_entities();setup_test_world();
}

/* Exercise authored ability classification, public creation, the actual owner query,
 * shared spot indexes, removal inverse, and cold save restoration. */
TEST(wc3_bot, captain291_resource_building_defaults_survive_save_and_removal) {
    gameClient_t *original=G_GetPlayerClientByNumber(0);
    typeof(original->jass.controller) controller=original->jass.controller;
    bool removed=original->jass.removed;
    uint32_t start=original->ps.start_location,owned=level.ai_owned_players;
    typeof(original->mapplayer) mapplayer=original->mapplayer;
    /* Minimal archive has no htow row. Supply its authored gold-return ability,
     * and keep the mine's authored capability available in the same table. */
    UnitAbilities_t abilities[]={
        {.id=MAKEFOURCC('h','t','o','w'),.abilList="Argd"},
        {.id=MAKEFOURCC('n','g','o','l'),.abilList="Agld"}
    };
    slkTestData_t table={.rows=abilities,.count=2};
    slkTestData_t *old=G_SetSLKRows("UnitAbilities",&table);
    FOR_LOOP(saved,2) {
        G_BotShutdown();reset_entities();setup_test_world();
        static uint8_t cells[128*128];
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{4096,4096}});CM_SetupTestPathmap(128,128,cells);
        level.pathing_clock=(wc3Clock_t){.time=10,.span=300};
        gameClient_t *client=G_GetPlayerClientByNumber(0);
        client->jass.controller=1;client->jass.removed=false;client->ps.start_location=0;
        client->mapplayer=level.mapinfo->players;
        ((mapInfo_t *)level.mapinfo)->players[0].used=true;
        ((mapInfo_t *)level.mapinfo)->players[client->ps.start_location].startingPosition=(vec2_t){128,256};
        G_BotInitPlayers();
        edict_t *mine=unit_create(PLAYER_NEUTRAL_PASSIVE,MAKEFOURCC('n','g','o','l'),&(vec2_t){1536,1024},0);
        edict_t *hall=unit_create(0,MAKEFOURCC('h','t','o','w'),&(vec2_t){1024,1024},0);
        T_ASSERT(S_GoldMineIsMine(mine));T_ASSERT(S_UnitTypeReturnsGold(hall->class_id));
        T_NE(hall->ai_anchor_spot,0);T_EQ(hall->ai_anchor_spot,mine->ai_anchor_spot);
        T_EQ(level.ai_town_spots_count,1);T_EQ(level.bots[0].town_spots[0],hall->ai_anchor_spot);
        G_BotCreateCaptains(&client->ps);
        botCaptain_t *attack=level.bots[0].captains+BOT_CAPTAIN_ATTACK;
        botCaptain_t *defense=level.bots[0].captains+BOT_CAPTAIN_DEFENSE;
        botTownSpot_t *spot=level.ai_town_spots;
        T_EQ(wc3_float_bits(attack->home.x),wc3_float_bits(spot->attack_home.x));
        T_EQ(wc3_float_bits(defense->home.x),wc3_float_bits(spot->defense_home.x));
        T_NE(wc3_float_bits(attack->home.x),wc3_float_bits(defense->home.x));
        uint32_t hall_number=hall->s.number,mine_number=mine->s.number,index=hall->ai_anchor_spot;
        T_ASSERT(G_BotValidateTownSpots(false));
        /* Invalid references/counts must be rejected, never silently rebuilt
         * on write. Load alone may reconstruct the derived count array. */
        uint32_t members=spot->members[0];spot->members[0]++;
        T_ASSERT(!G_BotValidateTownSpots(false));spot->members[0]=members;
        level.bots[0].town_spots[0]=level.ai_town_spots_count+1;
        T_ASSERT(!G_BotValidateTownSpots(false));level.bots[0].town_spots[0]=index;
        hall->ai_town_spot=level.ai_town_spots_count+1;
        T_ASSERT(!G_BotValidateTownSpots(false));hall->ai_town_spot=index;
        hall->ai_town_player=0;T_ASSERT(!G_BotValidateTownSpots(false));hall->ai_town_player=1;
        T_ASSERT(G_BotValidateTownSpots(false));
        if(saved) {
            cstring_t file=Test_TempPath("wc3-captain291-town.bin");
            T_ASSERT(WriteGame(file));G_BotShutdown();T_ASSERT(ReadGame(file));remove(file);
            hall=g_edicts+hall_number;mine=g_edicts+mine_number;
            T_EQ(level.bots[0].town_spots[0],index);T_EQ(hall->ai_anchor_spot,index);
            T_EQ(mine->ai_anchor_spot,index);T_ASSERT(G_BotValidateTownSpots(false));
        }
        G_BotSetCaptainHome(&client->ps,3,3000,3000);
        G_FreeEdict(mine);G_BotResetCaptainLocs(&client->ps);
        T_EQ(wc3_float_bits(attack->home.x),wc3_float_bits(hall->s.origin2.x));
        T_EQ(wc3_float_bits(defense->home.y),wc3_float_bits(hall->s.origin2.y));
        T_EQ(level.ai_town_spots_count,1);T_EQ(level.bots[0].town_spots[0],index);
        G_FreeEdict(hall);T_EQ(level.bots[0].town_spots[0],0);
        T_EQ(level.ai_town_spots_count,1); /* Stable global identities are not reclaimed. */
        G_BotCreateCaptains(&client->ps);T_EQ(level.ai_town_spots_count,2);
        T_EQ(level.bots[0].town_spots[0],2);T_FEQ(attack->home.x,128,0);T_FEQ(attack->home.y,256,0);
        T_ASSERT(G_BotValidateTownSpots(false));
        cstring_t invalid=Test_TempPath("wc3-captain291-truncated.bin");
        FILE *stream=fopen(invalid,"wb+");T_ASSERT(stream!=NULL);
        if(stream) {
            uint32_t count=UINT32_MAX;fwrite(&count,sizeof(count),1,stream);rewind(stream);
            T_ASSERT(!G_ReadTownSpots(stream));fclose(stream);remove(invalid);
            T_EQ(level.ai_town_spots_count,2);T_ASSERT(G_BotValidateTownSpots(false));
        }
        G_BotShutdown();T_EQ(level.ai_town_spots_count,0);T_NULL(level.ai_town_spots);
    }
    G_SetSLKRows("UnitAbilities",old);free(old);
    reset_entities();setup_test_world();
    original=G_GetPlayerClientByNumber(0);
    original->jass.controller=controller;original->jass.removed=removed;
    original->ps.start_location=start;original->mapplayer=mapplayer;
    level.ai_owned_players=owned;
}
#endif
