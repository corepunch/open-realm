#include "g_local.h"
#include "skills/s_skills.h"
#include <stdio.h>
#include <string.h>

#define DEFINE_POOL_OWNER_CALLBACKS(member, owner, Name, type, POOL_CAP, allocated, released) \
_Static_assert(POOL_CAP > 1 && POOL_CAP <= UINT16_MAX + 1u, "WC3 pool index capacity"); \
static struct { type data; uint16_t next; bool live; } member##_slots[POOL_CAP]; \
static uint16_t member##_free; \
static uint32_t member##_virgin = 1; \
static uint16_t member##_live, member##_peak; \
type *G_Alloc##Name(void) { \
    uint16_t i = member##_free; \
    if (i) member##_free = member##_slots[i].next; \
    else if (member##_virgin < POOL_CAP) i = (uint16_t)member##_virgin++; \
    if (!i) { \
        fprintf(stderr, "WC3 pool %s exhausted\n", #member); \
        gi.error("WC3 pool %s exhausted", #member); \
        abort(); \
    } \
    memset(&member##_slots[i], 0, sizeof(member##_slots[i])); \
    member##_slots[i].live = true; \
    if (++member##_live > member##_peak) member##_peak = member##_live; \
    allocated; \
    return &member##_slots[i].data; \
} \
void G_Free##Name(edict_t *ent) { \
    assert(ent); \
    if (!owner) return; \
    uint16_t i = (uint16_t)(((unsigned char *)owner - (unsigned char *)member##_slots) / sizeof(member##_slots[0])); \
    assert(i > 0 && i < POOL_CAP && member##_slots[i].live); \
    member##_slots[i].live = false; \
    member##_live--; \
    member##_slots[i].next = member##_free; \
    member##_free = i; \
    owner = NULL; \
    released; \
} \
static void G_Reset##Name##Pool(void) { \
    for (uint32_t i = 1; i < member##_virgin; i++) member##_slots[i].live = false; \
    member##_virgin = 1; member##_free = member##_live = member##_peak = 0; \
}

#define DEFINE_POOL_CALLBACKS(member, Name, type, POOL_CAP, allocated, released) \
    DEFINE_POOL_OWNER_CALLBACKS(member, ent->member, Name, type, POOL_CAP, allocated, released)

#define DEFINE_POOL(member, Name, type, POOL_CAP) \
    DEFINE_POOL_CALLBACKS(member, Name, type, POOL_CAP, (void)0, (void)0)

/* Virgin records enter in ascending order; recycled records remain LIFO.
 * First use is constant time, and reset invalidates only the allocated prefix.
 * Payload bytes are cleared on allocation before callbacks can observe them. */

DEFINE_POOL(construction, Construction, construction_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(research, Research, research_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL_CALLBACKS(rally, Rally, rally_t, LIFECYCLE_POOL_CAP,
    S_InvalidateRallyProducers(), S_ForgetRallyProducer(ent))
/* Every mobile unit owns food state, even when training limits are disabled. */
DEFINE_POOL(food, Food, food_t, MAX_ENTITIES)
DEFINE_POOL_OWNER_CALLBACKS(attack_one, ent->attack_overrides[0], AttackOne, unitAttack_t, MAX_ENTITIES,
    (void)0, (void)0)
DEFINE_POOL_OWNER_CALLBACKS(attack_two, ent->attack_overrides[1], AttackTwo, unitAttack_t, MAX_ENTITIES,
    (void)0, (void)0)
/* Plain units carry no status storage. Records retain their original slot
 * identities until edict release, including synchronous inverse callbacks. */
DEFINE_POOL(abilstatus, UnitStatus, unitStatusStorage_t, MAX_ENTITIES)
DEFINE_POOL_OWNER_CALLBACKS(orders, ent->order_queue.entries, UnitOrdersInline, unitOrderStorage_t, MAX_ENTITIES,
    (void)0, ent->order_queue.head = ent->order_queue.count = 0)
/* Large FIFOs grow geometrically. Most units never allocate a queue, and
 * ordinary short queues use the original LIFO pool. No maximum-sized bucket
 * is charged to every unit merely because the retail admission bound is501. */
unitOrderStorage_t *G_AllocUnitOrders(void) { return G_AllocUnitOrdersInline(); }

void G_FreeUnitOrders(edict_t *ent) {
    unitOrderQueue_t *queue=&ent->order_queue;
    if(queue->entries) {
        if(queue->capacity>UNIT_ORDER_INITIAL_CAPACITY) free(queue->entries);
        else G_FreeUnitOrdersInline(ent);
    }
    *queue=(unitOrderQueue_t){0};
}

bool G_ReserveUnitOrders(edict_t *ent,uint32_t required) {
    unitOrderQueue_t *queue=&ent->order_queue;
    if(required>UNIT_ORDER_STORAGE_CAPACITY)return false;
    if(queue->entries && !queue->capacity)queue->capacity=UNIT_ORDER_INITIAL_CAPACITY;
    if(queue->entries && required<=queue->capacity)return true;
    if(!queue->entries && required<=UNIT_ORDER_INITIAL_CAPACITY) {
        queue->entries=G_AllocUnitOrders()->entries;
        queue->capacity=UNIT_ORDER_INITIAL_CAPACITY;return true;
    }
    uint32_t capacity=queue->capacity ? queue->capacity : UNIT_ORDER_INITIAL_CAPACITY;
    while(capacity<required)capacity=MIN(capacity*2,UNIT_ORDER_STORAGE_CAPACITY);
    unitOrder_t *entries=calloc(capacity,sizeof(*entries));
    if(!entries) {gi.error("Unit order queue: cannot allocate %u entries",capacity);abort();}
    if(queue->count) {
        uint32_t first=MIN(queue->count,queue->capacity-queue->head);
        memcpy(entries,queue->entries+queue->head,first*sizeof(*entries));
        memcpy(entries+first,queue->entries,(queue->count-first)*sizeof(*entries));
    }
    uint32_t count=queue->count;
    G_FreeUnitOrders(ent);
    *queue=(unitOrderQueue_t){.entries=entries,.count=count,.capacity=capacity};
    return true;
}

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
DEFINE_POOL_CALLBACKS(ancient_root, AncientRoot, ancientRoot_t, LIFECYCLE_POOL_CAP,
    (void)0, S_MarkMoveGoals(ent))
DEFINE_POOL(goldmine, GoldMine, goldMine_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(mineoverlay, MineOverlay, mineOverlay_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(acolyte_mine, AcolyteMine, acolyteMine_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(item, Item, item_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(destructable, Destructable, destructable_t, DESTRUCTABLE_POOL_CAP)
DEFINE_POOL_CALLBACKS(cargo, Cargo, cargo_t, LIFECYCLE_POOL_CAP,
    S_InvalidateCargoHolders(), S_CargoForgetHolder(ent))
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
    REPORT_POOL(attack_one);
    REPORT_POOL(attack_two);
    REPORT_POOL(abilstatus);
    REPORT_POOL(orders);
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
    S_ClearTimedLives();
    G_ResetWaypointCache();
    S_InvalidateCargoHolders();
    S_InvalidateRallyProducers();
    S_ResetLandMineThinkers();
    if (g_edicts) {
        FOR_LOOP(i, globals.max_edicts) {
            g_edicts[i].construction = NULL;
            g_edicts[i].research = NULL;
            g_edicts[i].rally = NULL;
            g_edicts[i].food = NULL;
            memset(g_edicts[i].attack_overrides, 0, sizeof(g_edicts[i].attack_overrides));
            g_edicts[i].abilstatus = NULL;
            G_FreeUnitOrders(g_edicts+i);
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
    G_ResetConstructionPool();
    G_ResetResearchPool();
    G_ResetRallyPool();
    G_ResetFoodPool();
    G_ResetAttackOnePool();
    G_ResetAttackTwoPool();
    G_ResetUnitStatusPool();
    G_ResetUnitOrdersInlinePool();
    G_ResetBuildworkPool();
    G_ResetRevivalPool();
    G_ResetSacrificePool();
    G_ResetUnsummonPool();
    G_ResetShadowMeldPool();
    G_ResetMilitiaPool();
    G_ResetPolymorphPool();
    G_ResetRavenPool();
    G_ResetBlightGrowthPool();
    G_ResetEnsnarePool();
    G_ResetAncientRootPool();
    G_ResetGoldMinePool();
    G_ResetMineOverlayPool();
    G_ResetAcolyteMinePool();
    G_ResetItemPool();
    G_ResetDestructablePool();
    G_ResetCargoPool();
    G_ResetStockPool();
    G_ResetWaygatePool();
    G_ResetArtilleryPool();
    G_ResetAvatarPool();
    G_ResetSleepPool();
    G_ResetChannelPool();
}

void G_PoolsReleaseEdict(edict_t *ent) {
    assert(ent);
    S_ReleaseTimedLives(ent);
    G_FreeConstruction(ent);
    G_FreeResearch(ent);
    G_FreeRally(ent);
    G_FreeFood(ent);
    G_FreeAttackOne(ent);
    G_FreeAttackTwo(ent);
    G_FreeUnitStatus(ent);
    G_FreeUnitOrders(ent);
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
