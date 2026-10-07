#ifdef BZ_TESTS
#include "../g_local.h"
#include "shared/test.h"

extern void reset_entities(void),setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t,float,float);

static handle_t (*captain_storage_alloc)(long);
static uint64_t captain_storage_bytes;
static uint32_t captain_storage_calls;
static handle_t CaptainStorageAlloc(long bytes) {
    captain_storage_bytes+=bytes;captain_storage_calls++;
    return captain_storage_alloc(bytes);
}

/* The native prepends each recruit. Capacity must not require copying and
 * allocating the entire roster once per unit; allocation volume stays linear. */
TEST(wc3_bot, captain_roster_bulk_storage_is_linear_and_keeps_owned_order) {
    G_BotStop(0);reset_entities();setup_test_world();
    enum {COUNT=256};edict_t *units[COUNT];
    FOR_LOOP(i,COUNT)units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    G_SetUnitPlayer(units[0],1);G_SetUnitPlayer(units[0],0);
    G_BotCreateCaptains(&game.clients[0].ps);
    captain_storage_alloc=gi.MemAlloc;captain_storage_bytes=0;captain_storage_calls=0;
    gi.MemAlloc=CaptainStorageAlloc;
    bool accepted=G_BotAddAssault(&game.clients[0].ps,COUNT,MAKEFOURCC('h','f','o','o'));
    gi.MemAlloc=captain_storage_alloc;
    fprintf(stderr,"WC3_CAPTAIN_STORAGE units=%u allocations=%u bytes=%llu\n",COUNT,
        captain_storage_calls,(unsigned long long)captain_storage_bytes);
    T_ASSERT(accepted);T_EQ(G_BotCaptainGroupSize(&game.clients[0].ps),COUNT);
    T_ASSERT(captain_storage_bytes<=COUNT*sizeof(edict_t *)*16);
    T_ASSERT(captain_storage_calls<=16);
    botCaptain_t *captain=level.bots[0].captains+BOT_CAPTAIN_ATTACK;
    FOR_LOOP(i,COUNT)T_EQ(captain->units[i],units[(i+1)%COUNT]);
    G_BotRemoveCaptainUnit(units[127]);
    T_EQ(ARRAY_COUNT(captain->units),COUNT-1);
    FOR_LOOP(i,COUNT-1)T_EQ(captain->units[i],units[(i+1+(i>=126))%COUNT]);
    T_ASSERT(G_BotAddAssault(&game.clients[0].ps,COUNT,MAKEFOURCC('h','f','o','o')));
    T_EQ(captain->units[0],units[127]);T_EQ(ARRAY_COUNT(captain->units),COUNT);
    G_BotCreateCaptains(&game.clients[0].ps);
    T_EQ(ARRAY_COUNT(captain->units),0);T_NULL(captain->units);
    G_BotStop(0);reset_entities();setup_test_world();
}
#endif
