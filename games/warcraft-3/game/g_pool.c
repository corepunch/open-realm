#include "g_local.h"
#include <stdio.h>
#include <string.h>

#define DEFINE_POOL(member, Name, type, POOL_CAP) \
static struct { type data; uint16_t next; bool live; } member##_slots[POOL_CAP]; \
static uint16_t member##_free; \
static bool member##_ready; \
static uint16_t member##_live, member##_peak; \
type *G_Alloc##Name(void) { \
    if (!member##_ready) { \
        member##_free = 1; \
        for (uint16_t i = 1; i < POOL_CAP - 1; i++) member##_slots[i].next = i + 1; \
        member##_ready = true; \
    } \
    uint16_t i = member##_free; \
    if (!i) { \
        fprintf(stderr, "WC3 pool %s exhausted\n", #member); \
        gi.error("WC3 pool %s exhausted", #member); \
        abort(); \
    } \
    member##_free = member##_slots[i].next; \
    memset(&member##_slots[i], 0, sizeof(member##_slots[i])); \
    member##_slots[i].live = true; \
    if (++member##_live > member##_peak) member##_peak = member##_live; \
    return &member##_slots[i].data; \
} \
void G_Free##Name(edict_t *ent) { \
    assert(ent); \
    if (!ent->member) return; \
    uint16_t i = (uint16_t)(((unsigned char *)ent->member - (unsigned char *)member##_slots) / sizeof(member##_slots[0])); \
    assert(i > 0 && i < POOL_CAP && member##_slots[i].live); \
    member##_slots[i].live = false; \
    member##_live--; \
    member##_slots[i].next = member##_free; \
    member##_free = i; \
    ent->member = NULL; \
}

DEFINE_POOL(construction, Construction, construction_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(research, Research, research_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(rally, Rally, rally_t, LIFECYCLE_POOL_CAP)
/* Every mobile unit owns food state, even when training limits are disabled. */
DEFINE_POOL(food, Food, food_t, MAX_ENTITIES)
DEFINE_POOL(buildwork, Buildwork, buildwork_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(revival, Revival, revival_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(sacrifice, Sacrifice, sacrifice_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(unsummon, Unsummon, unsummon_t, LIFECYCLE_POOL_CAP)
/* Hide-capable units retain this state even while walking in daylight. */
DEFINE_POOL(shadowmeld, ShadowMeld, shadowMeld_t, MAX_ENTITIES)
DEFINE_POOL(militia, Militia, militia_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(polymorph, Polymorph, polymorph_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(raven, Raven, raven_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(blight_growth, BlightGrowth, blightGrowth_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(ensnare, Ensnare, ensnare_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(ancient_root, AncientRoot, ancientRoot_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(goldmine, GoldMine, goldMine_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(mineoverlay, MineOverlay, mineOverlay_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(acolyte_mine, AcolyteMine, acolyteMine_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(item, Item, item_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(destructable, Destructable, destructable_t, DESTRUCTABLE_POOL_CAP)
DEFINE_POOL(cargo, Cargo, cargo_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(stock, Stock, stock_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(waygate, Waygate, waygate_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(artillery, Artillery, artillery_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(avatar, Avatar, avatar_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(sleep, Sleep, sleep_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(channel, Channel, channel_t, LIFECYCLE_POOL_CAP)

void G_PoolsReportPeaks(void) {
#define REPORT_POOL(member) if (member##_peak) fprintf(stderr, "WC3 pool peak %s=%u/%d\n", #member, member##_peak, (int)(sizeof(member##_slots) / sizeof(member##_slots[0])) - 1)
    REPORT_POOL(construction);
    REPORT_POOL(research);
    REPORT_POOL(rally);
    REPORT_POOL(food);
    REPORT_POOL(buildwork);
    REPORT_POOL(revival);
    REPORT_POOL(sacrifice);
    REPORT_POOL(unsummon);
    REPORT_POOL(shadowmeld);
    REPORT_POOL(militia);
    REPORT_POOL(polymorph);
    REPORT_POOL(raven);
    REPORT_POOL(blight_growth);
    REPORT_POOL(ensnare);
    REPORT_POOL(ancient_root);
    REPORT_POOL(goldmine);
    REPORT_POOL(mineoverlay);
    REPORT_POOL(acolyte_mine);
    REPORT_POOL(item);
    REPORT_POOL(destructable);
    REPORT_POOL(cargo);
    REPORT_POOL(stock);
    REPORT_POOL(waygate);
    REPORT_POOL(artillery);
    REPORT_POOL(avatar);
    REPORT_POOL(sleep);
    REPORT_POOL(channel);
#undef REPORT_POOL
}

void G_PoolsReset(void) {
    if (g_edicts) {
        FOR_LOOP(i, globals.max_edicts) {
            g_edicts[i].construction = NULL;
            g_edicts[i].research = NULL;
            g_edicts[i].rally = NULL;
            g_edicts[i].food = NULL;
            g_edicts[i].buildwork = NULL;
            g_edicts[i].revival = NULL;
            g_edicts[i].sacrifice = NULL;
            g_edicts[i].unsummon = NULL;
            g_edicts[i].shadowmeld = NULL;
            g_edicts[i].militia = NULL;
            g_edicts[i].polymorph = NULL;
            g_edicts[i].raven = NULL;
            g_edicts[i].blight_growth = NULL;
            g_edicts[i].ensnare = NULL;
            g_edicts[i].ancient_root = NULL;
            g_edicts[i].goldmine = NULL;
            g_edicts[i].mineoverlay = NULL;
            g_edicts[i].acolyte_mine = NULL;
            g_edicts[i].item = NULL;
            g_edicts[i].destructable = NULL;
            g_edicts[i].cargo = NULL;
            g_edicts[i].stock = NULL;
            g_edicts[i].waygate = NULL;
            g_edicts[i].artillery = NULL;
            g_edicts[i].avatar = NULL;
            g_edicts[i].sleep = NULL;
            g_edicts[i].channel = NULL;
        }
    }
    construction_ready = false; construction_live = construction_peak = 0; memset(construction_slots, 0, sizeof(construction_slots)); construction_free = 0;
    research_ready = false; research_live = research_peak = 0; memset(research_slots, 0, sizeof(research_slots)); research_free = 0;
    rally_ready = false; rally_live = rally_peak = 0; memset(rally_slots, 0, sizeof(rally_slots)); rally_free = 0;
    food_ready = false; food_live = food_peak = 0; memset(food_slots, 0, sizeof(food_slots)); food_free = 0;
    buildwork_ready = false; buildwork_live = buildwork_peak = 0; memset(buildwork_slots, 0, sizeof(buildwork_slots)); buildwork_free = 0;
    revival_ready = false; revival_live = revival_peak = 0; memset(revival_slots, 0, sizeof(revival_slots)); revival_free = 0;
    sacrifice_ready = false; sacrifice_live = sacrifice_peak = 0; memset(sacrifice_slots, 0, sizeof(sacrifice_slots)); sacrifice_free = 0;
    unsummon_ready = false; unsummon_live = unsummon_peak = 0; memset(unsummon_slots, 0, sizeof(unsummon_slots)); unsummon_free = 0;
    shadowmeld_ready = false; shadowmeld_live = shadowmeld_peak = 0; memset(shadowmeld_slots, 0, sizeof(shadowmeld_slots)); shadowmeld_free = 0;
    militia_ready = false; militia_live = militia_peak = 0; memset(militia_slots, 0, sizeof(militia_slots)); militia_free = 0;
    polymorph_ready = false; polymorph_live = polymorph_peak = 0; memset(polymorph_slots, 0, sizeof(polymorph_slots)); polymorph_free = 0;
    raven_ready = false; raven_live = raven_peak = 0; memset(raven_slots, 0, sizeof(raven_slots)); raven_free = 0;
    blight_growth_ready = false; blight_growth_live = blight_growth_peak = 0; memset(blight_growth_slots, 0, sizeof(blight_growth_slots)); blight_growth_free = 0;
    ensnare_ready = false; ensnare_live = ensnare_peak = 0; memset(ensnare_slots, 0, sizeof(ensnare_slots)); ensnare_free = 0;
    ancient_root_ready = false; ancient_root_live = ancient_root_peak = 0; memset(ancient_root_slots, 0, sizeof(ancient_root_slots)); ancient_root_free = 0;
    goldmine_ready = false; goldmine_live = goldmine_peak = 0; memset(goldmine_slots, 0, sizeof(goldmine_slots)); goldmine_free = 0;
    mineoverlay_ready = false; mineoverlay_live = mineoverlay_peak = 0; memset(mineoverlay_slots, 0, sizeof(mineoverlay_slots)); mineoverlay_free = 0;
    acolyte_mine_ready = false; acolyte_mine_live = acolyte_mine_peak = 0; memset(acolyte_mine_slots, 0, sizeof(acolyte_mine_slots)); acolyte_mine_free = 0;
    item_ready = false; item_live = item_peak = 0; memset(item_slots, 0, sizeof(item_slots)); item_free = 0;
    destructable_ready = false; destructable_live = destructable_peak = 0; memset(destructable_slots, 0, sizeof(destructable_slots)); destructable_free = 0;
    cargo_ready = false; cargo_live = cargo_peak = 0; memset(cargo_slots, 0, sizeof(cargo_slots)); cargo_free = 0;
    stock_ready = false; stock_live = stock_peak = 0; memset(stock_slots, 0, sizeof(stock_slots)); stock_free = 0;
    waygate_ready = false; waygate_live = waygate_peak = 0; memset(waygate_slots, 0, sizeof(waygate_slots)); waygate_free = 0;
    artillery_ready = false; artillery_live = artillery_peak = 0; memset(artillery_slots, 0, sizeof(artillery_slots)); artillery_free = 0;
    avatar_ready = false; avatar_live = avatar_peak = 0; memset(avatar_slots, 0, sizeof(avatar_slots)); avatar_free = 0;
    sleep_ready = false; sleep_live = sleep_peak = 0; memset(sleep_slots, 0, sizeof(sleep_slots)); sleep_free = 0;
    channel_ready = false; channel_live = channel_peak = 0; memset(channel_slots, 0, sizeof(channel_slots)); channel_free = 0;
}

void G_PoolsReleaseEdict(edict_t *ent) {
    assert(ent);
    G_FreeConstruction(ent);
    G_FreeResearch(ent);
    G_FreeRally(ent);
    G_FreeFood(ent);
    G_FreeBuildwork(ent);
    G_FreeRevival(ent);
    G_FreeSacrifice(ent);
    G_FreeUnsummon(ent);
    G_FreeShadowMeld(ent);
    G_FreeMilitia(ent);
    G_FreePolymorph(ent);
    G_FreeRaven(ent);
    G_FreeBlightGrowth(ent);
    G_FreeEnsnare(ent);
    G_FreeAncientRoot(ent);
    G_FreeGoldMine(ent);
    G_FreeMineOverlay(ent);
    G_FreeAcolyteMine(ent);
    G_FreeItem(ent);
    G_FreeDestructable(ent);
    G_FreeCargo(ent);
    G_FreeStock(ent);
    G_FreeWaygate(ent);
    G_FreeArtillery(ent);
    G_FreeAvatar(ent);
    G_FreeSleep(ent);
    G_FreeChannel(ent);
}

#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);

TEST(wc3_pools, common_mobile_state_scales_with_world_entity_capacity) {
    reset_entities();
    FOR_LOOP(i,3000) {
        edict_t *unit=G_Spawn();unit->food=G_AllocFood();
        T_ASSERT(unit->food!=NULL);
        unit->shadowmeld=G_AllocShadowMeld();
        T_ASSERT(unit->shadowmeld!=NULL);
    }
    reset_entities();
}

TEST(wc3_pools, release_reuses_zeroed_owned_state) {
    edict_t *unit;
    reset_entities();
    unit = G_Spawn();
#define CHECK_POOL(member, Name, type) do { \
    T_NULL(unit->member); \
    unit->member = G_Alloc##Name(); \
    type *first = unit->member; \
    memset(first, 0x5a, sizeof(*first)); \
    G_Free##Name(unit); \
    T_NULL(unit->member); \
    unit->member = G_Alloc##Name(); \
    T_ASSERT(unit->member == first); \
    type empty; memset(&empty, 0, sizeof(empty)); \
    T_EQ(memcmp(unit->member, &empty, sizeof(empty)), 0); \
} while (0)
    CHECK_POOL(construction, Construction, construction_t);
    CHECK_POOL(research, Research, research_t);
    CHECK_POOL(rally, Rally, rally_t);
    CHECK_POOL(food, Food, food_t);
    CHECK_POOL(buildwork, Buildwork, buildwork_t);
    CHECK_POOL(revival, Revival, revival_t);
    CHECK_POOL(sacrifice, Sacrifice, sacrifice_t);
    CHECK_POOL(unsummon, Unsummon, unsummon_t);
    CHECK_POOL(shadowmeld, ShadowMeld, shadowMeld_t);
    CHECK_POOL(militia, Militia, militia_t);
    CHECK_POOL(polymorph, Polymorph, polymorph_t);
    CHECK_POOL(raven, Raven, raven_t);
    CHECK_POOL(blight_growth, BlightGrowth, blightGrowth_t);
    CHECK_POOL(ensnare, Ensnare, ensnare_t);
    CHECK_POOL(ancient_root, AncientRoot, ancientRoot_t);
    CHECK_POOL(goldmine, GoldMine, goldMine_t);
    CHECK_POOL(mineoverlay, MineOverlay, mineOverlay_t);
    CHECK_POOL(acolyte_mine, AcolyteMine, acolyteMine_t);
    CHECK_POOL(item, Item, item_t);
    CHECK_POOL(destructable, Destructable, destructable_t);
    CHECK_POOL(cargo, Cargo, cargo_t);
    CHECK_POOL(stock, Stock, stock_t);
    CHECK_POOL(waygate, Waygate, waygate_t);
    CHECK_POOL(artillery, Artillery, artillery_t);
    CHECK_POOL(avatar, Avatar, avatar_t);
    CHECK_POOL(sleep, Sleep, sleep_t);
    CHECK_POOL(channel, Channel, channel_t);
#undef CHECK_POOL
    G_PoolsReleaseEdict(unit);
    T_NULL(unit->construction);
    T_NULL(unit->research);
    T_NULL(unit->rally);
    T_NULL(unit->food);
    T_NULL(unit->buildwork);
    T_NULL(unit->revival);
    T_NULL(unit->sacrifice);
    T_NULL(unit->unsummon);
    T_NULL(unit->shadowmeld);
    T_NULL(unit->militia);
    T_NULL(unit->polymorph);
    T_NULL(unit->raven);
    T_NULL(unit->blight_growth);
    T_NULL(unit->ensnare);
    T_NULL(unit->ancient_root);
    T_NULL(unit->goldmine);
    T_NULL(unit->mineoverlay);
    T_NULL(unit->acolyte_mine);
    T_NULL(unit->item);
    T_NULL(unit->destructable);
    T_NULL(unit->cargo);
    T_NULL(unit->stock);
    T_NULL(unit->waygate);
    T_NULL(unit->artillery);
    T_NULL(unit->avatar);
    T_NULL(unit->sleep);
    T_NULL(unit->channel);
    unit->ancient_root = G_AllocAncientRoot();
    G_PoolsReset();
    T_NULL(unit->ancient_root);
}

TEST(wc3_pools, destructable_pool_holds_more_than_the_default_cap) {
    reset_entities();
    edict_t *first = G_Spawn();
    first->destructable = G_AllocDestructable();
    FOR_LOOP(i, DESTRUCTABLE_POOL_CAP - 2) T_ASSERT(G_AllocDestructable() != NULL);
    G_PoolsReset();
    T_NULL(first->destructable);
}

TEST(wc3_pools, register_model_ignores_missing_names) {
    T_EQ(G_RegisterModel(NULL), 0);
    T_EQ(G_RegisterModel(""), 0);
}
#endif
