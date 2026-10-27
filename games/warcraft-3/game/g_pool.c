#include "g_local.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Component storage keeps typed pools; this registry owns only attachment and raw release. */
typedef struct {
    componentId_t id;
    void *(*alloc)(void);
    void (*release)(void *);
} componentType_t;

/* Test builds poison released records so a pointer cached across a release
 * faults instead of reading the stale payload; allocation zeroes the slot. */
#ifdef BZ_TESTS
#define POISON_RELEASED(slot) memset(&(slot).data, 0xa5, sizeof((slot).data))
#else
#define POISON_RELEASED(slot) ((void)0)
#endif

#define DEFINE_STORAGE(member, Name, type, POOL_CAP) \
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
static void Release##Name(void *record) { \
    uint16_t i = (uint16_t)(((unsigned char *)record - (unsigned char *)member##_slots) / sizeof(member##_slots[0])); \
    assert(i > 0 && i < POOL_CAP && member##_slots[i].live); \
    POISON_RELEASED(member##_slots[i]); \
    member##_slots[i].live = false; \
    member##_live--; \
    member##_slots[i].next = member##_free; \
    member##_free = i; \
}

#define DEFINE_POOL(member, Name, type, POOL_CAP) \
DEFINE_STORAGE(member, Name, type, POOL_CAP) \
void G_Free##Name(edict_t *ent) { \
    assert(ent); \
    if (!ent->member) return; \
    Release##Name(ent->member); \
    ent->member = NULL; \
}

DEFINE_STORAGE(construction, Construction, construction_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(research, Research, research_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(rally, Rally, rally_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(food, Food, food_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(buildwork, Buildwork, buildwork_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(revival, Revival, revival_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(sacrifice, Sacrifice, sacrifice_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(unsummon, Unsummon, unsummon_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(shadowmeld, ShadowMeld, shadowMeld_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(militia, Militia, militia_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(polymorph, Polymorph, polymorph_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(raven, Raven, raven_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(blight_growth, BlightGrowth, blightGrowth_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(ensnare, Ensnare, ensnare_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(ancient_root, AncientRoot, ancientRoot_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(goldmine, GoldMine, goldMine_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(mineoverlay, MineOverlay, mineOverlay_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(acolyte_mine, AcolyteMine, acolyteMine_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(item, Item, item_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(destructable, Destructable, destructable_t, DESTRUCTABLE_POOL_CAP)
DEFINE_STORAGE(cargo, Cargo, cargo_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(stock, Stock, stock_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(waygate, Waygate, waygate_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(artillery, Artillery, artillery_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(avatar, Avatar, avatar_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(sleep, Sleep, sleep_t, LIFECYCLE_POOL_CAP)
DEFINE_STORAGE(soultrap, SoulTrap, soulTrap_t, LIFECYCLE_POOL_CAP)
DEFINE_POOL(unit_state, UnitState, unitState_t, MAX_ENTITIES)
DEFINE_POOL(sound, Sounds, unitSounds_t, MAX_ENTITIES)
DEFINE_POOL(missile, Missile, attackMissile_t, MAX_ENTITIES)
DEFINE_POOL(inventory, Inventory, inventory_t, MAX_ENTITIES)
DEFINE_POOL(harvest, Harvest, harvest_t, MAX_ENTITIES)
DEFINE_POOL(vision, Visibility, visibility_t, MAX_ENTITIES)
DEFINE_POOL(orders, Orders, unitOrders_t, MAX_ENTITIES)
DEFINE_POOL(movement, Movement, movement_t, MAX_ENTITIES)
DEFINE_POOL(route, Route, routeCache_t, MAX_ENTITIES)
DEFINE_POOL(wander, Wander, wander_t, MAX_ENTITIES)
DEFINE_STORAGE(channel, Channel, channel_t, LIFECYCLE_POOL_CAP)

/* Typed allocators keep their concrete return type; the registry needs the untyped record.
 * The header must lead each record so list nodes and typed records share one address. */
#define COMPONENT_ALLOC(Name, type) \
_Static_assert(offsetof(type, component) == 0, #type " must start with its component header"); \
static void *Alloc##Name##Component(void) { return G_Alloc##Name(); }
COMPONENT_ALLOC(Waygate, waygate_t)
COMPONENT_ALLOC(Avatar, avatar_t)
COMPONENT_ALLOC(Polymorph, polymorph_t)
COMPONENT_ALLOC(Raven, raven_t)
COMPONENT_ALLOC(Ensnare, ensnare_t)
COMPONENT_ALLOC(Artillery, artillery_t)
COMPONENT_ALLOC(Sleep, sleep_t)
COMPONENT_ALLOC(Rally, rally_t)
COMPONENT_ALLOC(Sacrifice, sacrifice_t)
COMPONENT_ALLOC(Unsummon, unsummon_t)
COMPONENT_ALLOC(BlightGrowth, blightGrowth_t)
COMPONENT_ALLOC(ShadowMeld, shadowMeld_t)
COMPONENT_ALLOC(Militia, militia_t)
COMPONENT_ALLOC(Revival, revival_t)
COMPONENT_ALLOC(Buildwork, buildwork_t)
COMPONENT_ALLOC(AncientRoot, ancientRoot_t)
COMPONENT_ALLOC(GoldMine, goldMine_t)
COMPONENT_ALLOC(MineOverlay, mineOverlay_t)
COMPONENT_ALLOC(AcolyteMine, acolyteMine_t)
COMPONENT_ALLOC(Cargo, cargo_t)
COMPONENT_ALLOC(SoulTrap, soulTrap_t)
COMPONENT_ALLOC(Research, research_t)
COMPONENT_ALLOC(Food, food_t)
COMPONENT_ALLOC(Stock, stock_t)
COMPONENT_ALLOC(Channel, channel_t)
COMPONENT_ALLOC(Destructable, destructable_t)
COMPONENT_ALLOC(Construction, construction_t)
COMPONENT_ALLOC(Item, item_t)

/* Exact IDs authorize the concrete layout; families and authored rawcodes are separate concepts. */
static componentType_t const *ComponentType(componentId_t id) {
    static componentType_t const component_types[] = {
        { COMPONENT_WAYGATE, AllocWaygateComponent, ReleaseWaygate },
        { COMPONENT_AVATAR, AllocAvatarComponent, ReleaseAvatar },
        { COMPONENT_POLYMORPH, AllocPolymorphComponent, ReleasePolymorph },
        { COMPONENT_RAVEN, AllocRavenComponent, ReleaseRaven },
        { COMPONENT_ENSNARE, AllocEnsnareComponent, ReleaseEnsnare },
        { COMPONENT_ARTILLERY, AllocArtilleryComponent, ReleaseArtillery },
        { COMPONENT_SLEEP, AllocSleepComponent, ReleaseSleep },
        { COMPONENT_RALLY, AllocRallyComponent, ReleaseRally },
        { COMPONENT_SACRIFICE, AllocSacrificeComponent, ReleaseSacrifice },
        { COMPONENT_UNSUMMON, AllocUnsummonComponent, ReleaseUnsummon },
        { COMPONENT_BLIGHT_GROWTH, AllocBlightGrowthComponent, ReleaseBlightGrowth },
        { COMPONENT_SHADOWMELD, AllocShadowMeldComponent, ReleaseShadowMeld },
        { COMPONENT_MILITIA, AllocMilitiaComponent, ReleaseMilitia },
        { COMPONENT_REVIVAL, AllocRevivalComponent, ReleaseRevival },
        { COMPONENT_BUILDWORK, AllocBuildworkComponent, ReleaseBuildwork },
        { COMPONENT_ANCIENT_ROOT, AllocAncientRootComponent, ReleaseAncientRoot },
        { COMPONENT_GOLDMINE, AllocGoldMineComponent, ReleaseGoldMine },
        { COMPONENT_MINE_OVERLAY, AllocMineOverlayComponent, ReleaseMineOverlay },
        { COMPONENT_ACOLYTE_MINE, AllocAcolyteMineComponent, ReleaseAcolyteMine },
        { COMPONENT_CARGO, AllocCargoComponent, ReleaseCargo },
        { COMPONENT_SOULTRAP, AllocSoulTrapComponent, ReleaseSoulTrap },
        { COMPONENT_RESEARCH, AllocResearchComponent, ReleaseResearch },
        { COMPONENT_FOOD, AllocFoodComponent, ReleaseFood },
        { COMPONENT_STOCK, AllocStockComponent, ReleaseStock },
        { COMPONENT_CHANNEL, AllocChannelComponent, ReleaseChannel },
        { COMPONENT_DESTRUCTABLE, AllocDestructableComponent, ReleaseDestructable },
        { COMPONENT_CONSTRUCTION, AllocConstructionComponent, ReleaseConstruction },
        { COMPONENT_ITEM, AllocItemComponent, ReleaseItem }
    };
    /* IDs are dense from 1, so the table is indexed directly; the identity
     * check keeps a reordered row from silently selecting another pool. */
    size_t const count = sizeof(component_types) / sizeof(component_types[0]);
    if (id >= 1 && (size_t)id <= count && component_types[id - 1].id == id) return component_types + id - 1;
    gi.error("WC3 unknown component type %u", (unsigned)id);
    abort();
}

/* Queries never allocate, including on edicts that do not own this behavior. */
void *G_GetComponent(edict_t const *ent, componentId_t id) {
    for (component_t *cur = ent ? ent->components : NULL; cur; cur = cur->next)
        if (cur->id == id) return cur;
    return NULL;
}

/* Restore and allocation both establish one owner and one instance of an exact type. */
void G_AttachComponent(edict_t *ent, component_t *record, componentId_t id) {
    assert(ent && record && !G_GetComponent(ent, id));
    assert(!record->next && (!record->id || record->id == id));
    *record = (component_t){ .id = id, .next = ent->components };
    ent->components = record;
}

/* Explicit initialization is idempotent, while ordinary observation remains read-only. */
void *G_AddComponent(edict_t *ent, componentId_t id) {
    assert(ent);
    component_t *record = G_GetComponent(ent, id);
    if (record) return record;
    record = ComponentType(id)->alloc();
    G_AttachComponent(ent, record, id);
    return record;
}

/* Unlink before releasing storage so head and interior removal preserve the remaining owner list. */
void G_FreeComponent(edict_t *ent, componentId_t id) {
    assert(ent);
    for (component_t **link = &ent->components; *link; link = &(*link)->next) {
        component_t *cur = *link;
        if (cur->id != id) continue;
        *link = cur->next;
        ComponentType(id)->release(cur);
        return;
    }
}

void G_FreeWaygate(edict_t *ent) { G_FreeComponent(ent, COMPONENT_WAYGATE); }
void G_FreeAvatar(edict_t *ent) { G_FreeComponent(ent, COMPONENT_AVATAR); }
void G_FreePolymorph(edict_t *ent) { G_FreeComponent(ent, COMPONENT_POLYMORPH); }
void G_FreeRaven(edict_t *ent) { G_FreeComponent(ent, COMPONENT_RAVEN); }
void G_FreeEnsnare(edict_t *ent) { G_FreeComponent(ent, COMPONENT_ENSNARE); }
void G_FreeArtillery(edict_t *ent) { G_FreeComponent(ent, COMPONENT_ARTILLERY); }
void G_FreeSleep(edict_t *ent) { G_FreeComponent(ent, COMPONENT_SLEEP); }
void G_FreeRally(edict_t *ent) { G_FreeComponent(ent, COMPONENT_RALLY); }
void G_FreeSacrifice(edict_t *ent) { G_FreeComponent(ent, COMPONENT_SACRIFICE); }
void G_FreeUnsummon(edict_t *ent) { G_FreeComponent(ent, COMPONENT_UNSUMMON); }
void G_FreeBlightGrowth(edict_t *ent) { G_FreeComponent(ent, COMPONENT_BLIGHT_GROWTH); }
void G_FreeShadowMeld(edict_t *ent) { G_FreeComponent(ent, COMPONENT_SHADOWMELD); }
void G_FreeMilitia(edict_t *ent) { G_FreeComponent(ent, COMPONENT_MILITIA); }
void G_FreeRevival(edict_t *ent) { G_FreeComponent(ent, COMPONENT_REVIVAL); }
void G_FreeBuildwork(edict_t *ent) { G_FreeComponent(ent, COMPONENT_BUILDWORK); }
void G_FreeAncientRoot(edict_t *ent) { G_FreeComponent(ent, COMPONENT_ANCIENT_ROOT); }
void G_FreeGoldMine(edict_t *ent) { G_FreeComponent(ent, COMPONENT_GOLDMINE); }
void G_FreeMineOverlay(edict_t *ent) { G_FreeComponent(ent, COMPONENT_MINE_OVERLAY); }
void G_FreeAcolyteMine(edict_t *ent) { G_FreeComponent(ent, COMPONENT_ACOLYTE_MINE); }
void G_FreeCargo(edict_t *ent) { G_FreeComponent(ent, COMPONENT_CARGO); }
void G_FreeSoulTrap(edict_t *ent) { G_FreeComponent(ent, COMPONENT_SOULTRAP); }
void G_FreeResearch(edict_t *ent) { G_FreeComponent(ent, COMPONENT_RESEARCH); }
void G_FreeFood(edict_t *ent) { G_FreeComponent(ent, COMPONENT_FOOD); }
void G_FreeStock(edict_t *ent) { G_FreeComponent(ent, COMPONENT_STOCK); }
void G_FreeChannel(edict_t *ent) { G_FreeComponent(ent, COMPONENT_CHANNEL); }
void G_FreeDestructable(edict_t *ent) { G_FreeComponent(ent, COMPONENT_DESTRUCTABLE); }
void G_FreeConstruction(edict_t *ent) { G_FreeComponent(ent, COMPONENT_CONSTRUCTION); }
void G_FreeItem(edict_t *ent) { G_FreeComponent(ent, COMPONENT_ITEM); }

/* Unit spawn and explicit ability/status mutations establish ownership; reads never allocate. */
void G_InitUnitState(edict_t *ent) {
    assert(ent);
    if (!ent->unit_state) ent->unit_state = G_AllocUnitState();
}

/* Explicit vision ownership entry; observation does not allocate. */
void G_InitVisibility(edict_t *ent) {
    assert(ent);
    if (!ent->vision) ent->vision = G_AllocVisibility();
}

/* Explicit harvest ownership entry; observation does not allocate. */
void G_InitHarvest(edict_t *ent) {
    assert(ent);
    if (!ent->harvest) ent->harvest = G_AllocHarvest();
}

/* The mine attaches admission state on first worker entry; worker membership uses the same role. */
void G_InitGoldMine(edict_t *ent) {
    assert(ent);
    G_AddComponent(ent, COMPONENT_GOLDMINE);
}

/* Explicit inventory ownership entry; observation does not allocate. */
void G_InitInventory(edict_t *ent) {
    assert(ent);
    if (!ent->inventory) ent->inventory = G_AllocInventory();
}

/* Explicit missile ownership entry; observation does not allocate. */
void G_InitMissile(edict_t *ent) {
    assert(ent);
    if (!ent->missile) ent->missile = G_AllocMissile();
}

/* Explicit sound ownership entry; observation does not allocate. */
void G_InitSounds(edict_t *ent) {
    assert(ent);
    if (!ent->sound) ent->sound = G_AllocSounds();
}

/* Live units retain order/guard identity between trips; effects and projectiles do not own it. */
void G_InitUnitOrders(edict_t *ent) {
    assert(ent);
    if (!ent->orders) ent->orders = G_AllocOrders();
}

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
    REPORT_POOL(soultrap);
    REPORT_POOL(unit_state);
    REPORT_POOL(sound);
    REPORT_POOL(missile);
    REPORT_POOL(inventory);
    REPORT_POOL(harvest);
    REPORT_POOL(vision);
    REPORT_POOL(orders);
    REPORT_POOL(movement);
    REPORT_POOL(route);
    REPORT_POOL(wander);
    REPORT_POOL(channel);
#undef REPORT_POOL
}

void G_PoolsReset(void) {
    if (g_edicts) {
        FOR_LOOP(i, globals.max_edicts) {
            g_edicts[i].components = NULL;
            g_edicts[i].unit_state = NULL;
            g_edicts[i].sound = NULL;
            g_edicts[i].missile = NULL;
            g_edicts[i].inventory = NULL;
            g_edicts[i].harvest = NULL;
            g_edicts[i].vision = NULL;
            g_edicts[i].orders = NULL;
            g_edicts[i].movement = NULL;
            g_edicts[i].route = NULL;
            g_edicts[i].wander = NULL;
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
    soultrap_ready = false; soultrap_live = soultrap_peak = 0; memset(soultrap_slots, 0, sizeof(soultrap_slots)); soultrap_free = 0;
    unit_state_ready = false; unit_state_live = unit_state_peak = 0; memset(unit_state_slots, 0, sizeof(unit_state_slots)); unit_state_free = 0;
    sound_ready = false; sound_live = sound_peak = 0; memset(sound_slots, 0, sizeof(sound_slots)); sound_free = 0;
    missile_ready = false; missile_live = missile_peak = 0; memset(missile_slots, 0, sizeof(missile_slots)); missile_free = 0;
    inventory_ready = false; inventory_live = inventory_peak = 0; memset(inventory_slots, 0, sizeof(inventory_slots)); inventory_free = 0;
    harvest_ready = false; harvest_live = harvest_peak = 0; memset(harvest_slots, 0, sizeof(harvest_slots)); harvest_free = 0;
    vision_ready = false; vision_live = vision_peak = 0; memset(vision_slots, 0, sizeof(vision_slots)); vision_free = 0;
    orders_ready = false; orders_live = orders_peak = 0; memset(orders_slots, 0, sizeof(orders_slots)); orders_free = 0;
    movement_ready = false; movement_live = movement_peak = 0; memset(movement_slots, 0, sizeof(movement_slots)); movement_free = 0;
    route_ready = false; route_live = route_peak = 0; memset(route_slots, 0, sizeof(route_slots)); route_free = 0;
    wander_ready = false; wander_live = wander_peak = 0; memset(wander_slots, 0, sizeof(wander_slots)); wander_free = 0;
    channel_ready = false; channel_live = channel_peak = 0; memset(channel_slots, 0, sizeof(channel_slots)); channel_free = 0;
}

void G_PoolsReleaseEdict(edict_t *ent) {
    assert(ent);
    G_FreeUnitState(ent);
    G_FreeSounds(ent);
    G_FreeMissile(ent);
    G_FreeInventory(ent);
    G_FreeHarvest(ent);
    G_FreeVisibility(ent);
    G_FreeOrders(ent);
    G_FreeMovement(ent);
    G_FreeRoute(ent);
    G_FreeWander(ent);
    while (ent->components) G_FreeComponent(ent, ent->components->id);
}

#ifdef BZ_TESTS
#include "shared/test.h"
void reset_entities(void);

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
    CHECK_POOL(unit_state, UnitState, unitState_t);
    CHECK_POOL(sound, Sounds, unitSounds_t);
    CHECK_POOL(missile, Missile, attackMissile_t);
    CHECK_POOL(inventory, Inventory, inventory_t);
    CHECK_POOL(harvest, Harvest, harvest_t);
    CHECK_POOL(vision, Visibility, visibility_t);
    CHECK_POOL(orders, Orders, unitOrders_t);
    CHECK_POOL(movement, Movement, movement_t);
    CHECK_POOL(route, Route, routeCache_t);
    CHECK_POOL(wander, Wander, wander_t);
#undef CHECK_POOL
    G_PoolsReleaseEdict(unit);
    T_NULL(G_GetConstruction(unit));
    T_NULL(G_GetResearch(unit));
    T_NULL(G_GetRally(unit));
    T_NULL(G_GetFood(unit));
    T_NULL(G_GetBuildwork(unit));
    T_NULL(G_GetRevival(unit));
    T_NULL(G_GetSacrifice(unit));
    T_NULL(G_GetUnsummon(unit));
    T_NULL(G_GetShadowMeld(unit));
    T_NULL(G_GetMilitia(unit));
    T_NULL(G_GetPolymorph(unit));
    T_NULL(G_GetRaven(unit));
    T_NULL(G_GetBlightGrowth(unit));
    T_NULL(G_GetEnsnare(unit));
    T_NULL(G_GetAncientRoot(unit));
    T_NULL(G_GetGoldMine(unit));
    T_NULL(G_GetMineOverlay(unit));
    T_NULL(G_GetAcolyteMine(unit));
    T_NULL(G_GetItem(unit));
    T_NULL(G_GetDestructable(unit));
    T_NULL(G_GetCargo(unit));
    T_NULL(G_GetStock(unit));
    T_NULL(G_GetWaygate(unit));
    T_NULL(G_GetArtillery(unit));
    T_NULL(G_GetAvatar(unit));
    T_NULL(G_GetSleep(unit));
    T_NULL(G_GetSoulTrap(unit));
    T_NULL(unit->unit_state);
    T_NULL(unit->sound);
    T_NULL(unit->missile);
    T_NULL(unit->inventory);
    T_NULL(unit->harvest);
    T_NULL(unit->vision);
    T_NULL(unit->orders);
    T_NULL(unit->movement);
    T_NULL(unit->route);
    T_NULL(unit->wander);
    T_NULL(G_GetChannel(unit));
    G_AddComponent(unit, COMPONENT_ANCIENT_ROOT);
    G_PoolsReset();
    T_NULL(G_GetAncientRoot(unit));
}

TEST(wc3_pools, destructable_pool_holds_more_than_the_default_cap) {
    reset_entities();
    edict_t *first = G_Spawn();
    G_AddComponent(first, COMPONENT_DESTRUCTABLE);
    FOR_LOOP(i, DESTRUCTABLE_POOL_CAP - 2) T_ASSERT(G_AllocDestructable() != NULL);
    G_PoolsReset();
    T_NULL(G_GetDestructable(first));
}

TEST(wc3_pools, register_model_ignores_missing_names) {
    T_EQ(G_RegisterModel(NULL), 0);
    T_EQ(G_RegisterModel(""), 0);
}
#endif

#ifdef BZ_TESTS
/* Mixed owners exercise lookup, non-head removal, pool reuse and entity teardown. */
TEST(wc3_components, mixed_list_release_reuse_and_entity_lifetime) {
    reset_entities();
    edict_t *unit = G_Spawn();
    T_NULL(G_GetComponent(unit, COMPONENT_WAYGATE));
    T_NULL(unit->components);
    waygate_t *gate = G_AddComponent(unit, COMPONENT_WAYGATE);
    gate->destination = (vec2_t){128, 256};
    avatar_t *avatar = G_AddComponent(unit, COMPONENT_AVATAR);
    avatar->health = 250;
    T_ASSERT(unit->components == &avatar->component);
    T_ASSERT(unit->components->next == &gate->component);
    T_ASSERT(G_GetComponent(unit, COMPONENT_WAYGATE) == gate);
    T_ASSERT(G_AddComponent(unit, COMPONENT_WAYGATE) == gate);
    G_FreeWaygate(unit);
    T_NULL(G_GetComponent(unit, COMPONENT_WAYGATE));
    T_ASSERT(G_GetComponent(unit, COMPONENT_AVATAR) == avatar);
    T_NULL(unit->components->next);
    waygate_t *again = G_AddComponent(unit, COMPONENT_WAYGATE);
    T_ASSERT(again == gate);
    T_FEQ(again->destination.x, 0, 0.001f);
    T_EQ(again->component.id, COMPONENT_WAYGATE);
    G_FreeWaygate(unit);
    T_ASSERT(unit->components == &avatar->component);
    G_FreeEdict(unit);
    T_NULL(unit->components);
    unit = G_Spawn();
    T_NULL(unit->components);
    G_AddComponent(unit, COMPONENT_WAYGATE);
    G_PoolsReset();
    T_NULL(unit->components);
}

/* Every registered type allocates a zeroed header-first record, is found by exact ID only, and drains on teardown. */
TEST(wc3_components, every_registered_type_attaches_releases_and_rejects_other_ids) {
    static componentId_t const ids[] = {
        COMPONENT_WAYGATE, COMPONENT_AVATAR, COMPONENT_POLYMORPH, COMPONENT_RAVEN, COMPONENT_ENSNARE, COMPONENT_ARTILLERY,
        COMPONENT_SLEEP, COMPONENT_RALLY, COMPONENT_SACRIFICE, COMPONENT_UNSUMMON, COMPONENT_BLIGHT_GROWTH,
        COMPONENT_SHADOWMELD, COMPONENT_MILITIA, COMPONENT_REVIVAL, COMPONENT_BUILDWORK, COMPONENT_ANCIENT_ROOT, COMPONENT_GOLDMINE, COMPONENT_MINE_OVERLAY, COMPONENT_ACOLYTE_MINE, COMPONENT_CARGO, COMPONENT_SOULTRAP, COMPONENT_RESEARCH, COMPONENT_FOOD, COMPONENT_STOCK, COMPONENT_CHANNEL, COMPONENT_DESTRUCTABLE, COMPONENT_CONSTRUCTION, COMPONENT_ITEM
    };
    reset_entities();
    edict_t *unit = G_Spawn();
    FOR_LOOP(i, sizeof(ids) / sizeof(ids[0])) {
        component_t *record = G_AddComponent(unit, ids[i]);
        T_EQ(record->id, ids[i]);
        T_ASSERT(unit->components == record);
        FOR_LOOP(j, sizeof(ids) / sizeof(ids[0]))
            T_ASSERT((G_GetComponent(unit, ids[j]) != NULL) == (j <= i));
    }
    T_ASSERT(G_GetPolymorph(unit) == G_GetComponent(unit, COMPONENT_POLYMORPH));
    T_ASSERT(G_GetRaven(unit) == G_GetComponent(unit, COMPONENT_RAVEN));
    T_ASSERT(G_GetEnsnare(unit) == G_GetComponent(unit, COMPONENT_ENSNARE));
    T_ASSERT(G_GetArtillery(unit) == G_GetComponent(unit, COMPONENT_ARTILLERY));
    T_ASSERT(G_GetSleep(unit) == G_GetComponent(unit, COMPONENT_SLEEP));
    T_ASSERT(G_GetRally(unit) == G_GetComponent(unit, COMPONENT_RALLY));
    T_ASSERT(G_GetSacrifice(unit) == G_GetComponent(unit, COMPONENT_SACRIFICE));
    T_ASSERT(G_GetUnsummon(unit) == G_GetComponent(unit, COMPONENT_UNSUMMON));
    T_ASSERT(G_GetBlightGrowth(unit) == G_GetComponent(unit, COMPONENT_BLIGHT_GROWTH));
    T_ASSERT(G_GetShadowMeld(unit) == G_GetComponent(unit, COMPONENT_SHADOWMELD));
    T_ASSERT(G_GetMilitia(unit) == G_GetComponent(unit, COMPONENT_MILITIA));
    T_ASSERT(G_GetRevival(unit) == G_GetComponent(unit, COMPONENT_REVIVAL));
    T_ASSERT(G_GetBuildwork(unit) == G_GetComponent(unit, COMPONENT_BUILDWORK));
    T_ASSERT(G_GetAncientRoot(unit) == G_GetComponent(unit, COMPONENT_ANCIENT_ROOT));
    T_ASSERT(G_GetGoldMine(unit) == G_GetComponent(unit, COMPONENT_GOLDMINE));
    T_ASSERT(G_GetMineOverlay(unit) == G_GetComponent(unit, COMPONENT_MINE_OVERLAY));
    T_ASSERT(G_GetAcolyteMine(unit) == G_GetComponent(unit, COMPONENT_ACOLYTE_MINE));
    T_ASSERT(G_GetCargo(unit) == G_GetComponent(unit, COMPONENT_CARGO));
    T_ASSERT(G_GetSoulTrap(unit) == G_GetComponent(unit, COMPONENT_SOULTRAP));
    T_ASSERT(G_GetResearch(unit) == G_GetComponent(unit, COMPONENT_RESEARCH));
    T_ASSERT(G_GetFood(unit) == G_GetComponent(unit, COMPONENT_FOOD));
    T_ASSERT(G_GetStock(unit) == G_GetComponent(unit, COMPONENT_STOCK));
    T_ASSERT(G_GetChannel(unit) == G_GetComponent(unit, COMPONENT_CHANNEL));
    T_ASSERT(G_GetDestructable(unit) == G_GetComponent(unit, COMPONENT_DESTRUCTABLE));
    T_ASSERT(G_GetConstruction(unit) == G_GetComponent(unit, COMPONENT_CONSTRUCTION));
    T_ASSERT(G_GetItem(unit) == G_GetComponent(unit, COMPONENT_ITEM));
    T_ASSERT(!G_HeroIsReviving(unit) && !G_HeroAwaitsRevival(unit));
    G_GetRevival(unit)->reviving = true;
    T_ASSERT(G_HeroIsReviving(unit) && !G_HeroAwaitsRevival(NULL));
    G_GetArtillery(unit)->area_full = 40.0f;
    G_FreeArtillery(unit); /* Interior removal keeps the later-attached neighbours linked. */
    T_NULL(G_GetArtillery(unit));
    T_NOT_NULL(G_GetEnsnare(unit));
    T_NOT_NULL(G_GetWaygate(unit));
    artillery_t *again = G_AddComponent(unit, COMPONENT_ARTILLERY);
    T_FEQ(again->area_full, 0, 0.001f); /* Reuse hands back zeroed storage. */
    G_PoolsReleaseEdict(unit);
    T_NULL(unit->components);
    FOR_LOOP(i, sizeof(ids) / sizeof(ids[0])) T_NULL(G_GetComponent(unit, ids[i]));
}
#endif
