#include "g_local.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Test builds poison released records so a pointer cached across a release
 * faults instead of reading the stale payload; allocation zeroes the slot. */
#ifdef BZ_TESTS
#define POISON_RELEASED(slot) memset(&(slot).data, 0xa5, sizeof((slot).data))
#else
#define POISON_RELEASED(slot) ((void)0)
#endif

/* Fixed typed storage. Slot zero is reserved, so a zero free index means exhaustion. */
#define DEFINE_STORAGE(member, Name, type, POOL_CAP) \
static struct { type data; uint16_t next; bool live; } member##_slots[POOL_CAP]; \
static uint16_t member##_free; \
static bool member##_ready; \
static uint16_t member##_live, member##_peak; \
static type *Alloc##Name(void) { \
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
} \
static void Reset##Name(void) { \
    member##_ready = false; \
    member##_live = member##_peak = 0; \
    memset(member##_slots, 0, sizeof(member##_slots)); \
    member##_free = 0; \
} \
static void Report##Name(void) { \
    if (member##_peak) fprintf(stderr, "WC3 pool peak %s=%u/%d\n", #member, member##_peak, (POOL_CAP) - 1); \
}

/* Per-unit records that per-frame paths read through direct edict pointers. */
#define G_DIRECT_POOLS(X) \
    X(unit_state, UnitState, unitState_t) \
    X(sound, Sounds, unitSounds_t) \
    X(missile, Missile, attackMissile_t) \
    X(inventory, Inventory, inventory_t) \
    X(harvest, Harvest, harvest_t) \
    X(vision, Visibility, visibility_t) \
    X(orders, Orders, unitOrders_t) \
    X(movement, Movement, movement_t) \
    X(route, Route, routeCache_t) \
    X(wander, Wander, wander_t)

#define DEFINE_DIRECT_POOL(member, Name, type) \
DEFINE_STORAGE(member, Name, type, MAX_ENTITIES) \
type *G_Alloc##Name(void) { return Alloc##Name(); } \
void G_Free##Name(edict_t *ent) { \
    assert(ent); \
    if (!ent->member) return; \
    Release##Name(ent->member); \
    ent->member = NULL; \
}
G_DIRECT_POOLS(DEFINE_DIRECT_POOL)

static void *AddComponent(edict_t *ent, componentId_t id);
static void FreeComponent(edict_t *ent, componentId_t id);

/* Each row owns its storage and typed entries. The header must lead the record
 * so list nodes and typed records share one address. */
#define DEFINE_COMPONENT_POOL(id, ID, member, Name, type, cap, fields) \
_Static_assert((id) > 0, #ID " needs a nonzero component ID"); \
_Static_assert(offsetof(type, component) == 0, #type " must start with its component header"); \
DEFINE_STORAGE(member, Name, type, cap) \
type *G_Add##Name(edict_t *ent) { return AddComponent(ent, COMPONENT_##ID); } \
void G_Free##Name(edict_t *ent) { FreeComponent(ent, COMPONENT_##ID); }
G_COMPONENTS(DEFINE_COMPONENT_POOL)

/* The switches are the registry: duplicate IDs are duplicate case labels and fail to compile. */
static void *ComponentAlloc(componentId_t id) {
    switch (id) {
#define COMPONENT_ALLOC_CASE(id, ID, member, Name, type, cap, fields) case COMPONENT_##ID: return Alloc##Name();
    G_COMPONENTS(COMPONENT_ALLOC_CASE)
#undef COMPONENT_ALLOC_CASE
    }
    gi.error("WC3 unknown component type %u", (unsigned)id);
    abort();
}

static void ComponentRelease(component_t *record) {
    switch ((componentId_t)record->id) {
#define COMPONENT_RELEASE_CASE(id, ID, member, Name, type, cap, fields) case COMPONENT_##ID: Release##Name(record); return;
    G_COMPONENTS(COMPONENT_RELEASE_CASE)
#undef COMPONENT_RELEASE_CASE
    }
    gi.error("WC3 unknown component type %u", (unsigned)record->id);
    abort();
}

void *G_AllocDetachedComponent(componentId_t id) {
    return ComponentAlloc(id);
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
static void *AddComponent(edict_t *ent, componentId_t id) {
    assert(ent);
    component_t *record = G_GetComponent(ent, id);
    if (record) return record;
    record = ComponentAlloc(id);
    G_AttachComponent(ent, record, id);
    return record;
}

/* Unlink before releasing storage so head and interior removal preserve the remaining owner list. */
static void FreeComponent(edict_t *ent, componentId_t id) {
    assert(ent);
    for (component_t **link = &ent->components; *link; link = &(*link)->next) {
        component_t *cur = *link;
        if (cur->id != id) continue;
        *link = cur->next;
        ComponentRelease(cur);
        return;
    }
}

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
#define REPORT_COMPONENT(id, ID, member, Name, type, cap, fields) Report##Name();
#define REPORT_DIRECT(member, Name, type) Report##Name();
    G_COMPONENTS(REPORT_COMPONENT)
    G_DIRECT_POOLS(REPORT_DIRECT)
#undef REPORT_COMPONENT
#undef REPORT_DIRECT
}

void G_PoolsReset(void) {
    if (g_edicts) {
        FOR_LOOP(i, globals.max_edicts) {
            g_edicts[i].components = NULL;
#define CLEAR_DIRECT(member, Name, type) g_edicts[i].member = NULL;
            G_DIRECT_POOLS(CLEAR_DIRECT)
#undef CLEAR_DIRECT
        }
    }
#define RESET_COMPONENT(id, ID, member, Name, type, cap, fields) Reset##Name();
#define RESET_DIRECT(member, Name, type) Reset##Name();
    G_COMPONENTS(RESET_COMPONENT)
    G_DIRECT_POOLS(RESET_DIRECT)
#undef RESET_COMPONENT
#undef RESET_DIRECT
}

void G_PoolsReleaseEdict(edict_t *ent) {
    assert(ent);
#define FREE_DIRECT(member, Name, type) G_Free##Name(ent);
    G_DIRECT_POOLS(FREE_DIRECT)
#undef FREE_DIRECT
    while (ent->components) FreeComponent(ent, ent->components->id);
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
} while (0);
    G_DIRECT_POOLS(CHECK_POOL)
#undef CHECK_POOL
    G_PoolsReleaseEdict(unit);
#define CHECK_DIRECT_ABSENT(member, Name, type) T_NULL(unit->member);
#define CHECK_COMPONENT_ABSENT(id, ID, member, Name, type, cap, fields) T_NULL(G_Get##Name(unit));
    G_DIRECT_POOLS(CHECK_DIRECT_ABSENT)
    G_COMPONENTS(CHECK_COMPONENT_ABSENT)
#undef CHECK_DIRECT_ABSENT
#undef CHECK_COMPONENT_ABSENT
    G_AddAncientRoot(unit);
    G_PoolsReset();
    T_NULL(G_GetAncientRoot(unit));
}

TEST(wc3_pools, destructable_pool_holds_more_than_the_default_cap) {
    reset_entities();
    edict_t *first = G_Spawn();
    G_AddDestructable(first);
    FOR_LOOP(i, DESTRUCTABLE_POOL_CAP - 2) T_ASSERT(AllocDestructable() != NULL);
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
    T_NULL(G_GetWaygate(unit));
    T_NULL(unit->components);
    waygate_t *gate = G_AddWaygate(unit);
    gate->destination = (vec2_t){128, 256};
    avatar_t *avatar = G_AddAvatar(unit);
    avatar->health = 250;
    T_ASSERT(unit->components == &avatar->component);
    T_ASSERT(unit->components->next == &gate->component);
    T_ASSERT(G_GetWaygate(unit) == gate);
    T_ASSERT(G_AddWaygate(unit) == gate);
    T_FEQ(gate->destination.x, 128, 0.001f); /* Repeated attachment keeps the live record. */
    G_FreeWaygate(unit);
    T_NULL(G_GetWaygate(unit));
    T_ASSERT(G_GetAvatar(unit) == avatar);
    T_NULL(unit->components->next);
    G_FreeWaygate(unit); /* Releasing an absent record is a no-op. */
    T_ASSERT(unit->components == &avatar->component);
    waygate_t *again = G_AddWaygate(unit);
    T_ASSERT(again == gate);
    T_FEQ(again->destination.x, 0, 0.001f);
    T_EQ(again->component.id, COMPONENT_WAYGATE);
    G_FreeWaygate(unit);
    T_ASSERT(unit->components == &avatar->component);
    G_FreeEdict(unit);
    T_NULL(unit->components);
    unit = G_Spawn();
    T_NULL(unit->components);
    G_AddWaygate(unit);
    G_PoolsReset();
    T_NULL(unit->components);
}

/* Every table row allocates a zeroed header-first record through its typed entries,
 * is found by its exact ID only, releases into poisoned storage and drains on teardown. */
TEST(wc3_components, every_registered_type_attaches_releases_and_rejects_other_ids) {
    static componentId_t const ids[] = {
#define COMPONENT_ID_ROW(id, ID, member, Name, type, cap, fields) COMPONENT_##ID,
        G_COMPONENTS(COMPONENT_ID_ROW)
#undef COMPONENT_ID_ROW
    };
    size_t const count = sizeof(ids) / sizeof(ids[0]);
    reset_entities();
    edict_t *unit = G_Spawn();
    size_t attached = 0;
#define CHECK_TYPED_ADD(row_id, ID, member, Name, type, cap, fields) do { \
    type *record = G_Add##Name(unit); \
    T_EQ(record->component.id, COMPONENT_##ID); \
    T_ASSERT(unit->components == &record->component); \
    T_ASSERT(G_Get##Name(unit) == record && G_Add##Name(unit) == record); \
    type empty; memset(&empty, 0, sizeof(empty)); empty.component = record->component; \
    T_EQ(memcmp(record, &empty, sizeof(empty)), 0); \
    attached++; \
    FOR_LOOP(j, count) T_ASSERT((G_GetComponent(unit, ids[j]) != NULL) == (j < attached)); \
} while (0);
    G_COMPONENTS(CHECK_TYPED_ADD)
#undef CHECK_TYPED_ADD
    T_EQ(attached, count);
    T_ASSERT(!G_HeroIsReviving(unit) && !G_HeroAwaitsRevival(unit));
    G_GetRevival(unit)->reviving = true;
    T_ASSERT(G_HeroIsReviving(unit) && !G_HeroAwaitsRevival(NULL));
    artillery_t *splash = G_GetArtillery(unit);
    splash->area_full = 40.0f;
    G_FreeArtillery(unit); /* Interior removal keeps the later-attached neighbours linked. */
    T_NULL(G_GetArtillery(unit));
    T_NOT_NULL(G_GetEnsnare(unit));
    T_NOT_NULL(G_GetWaygate(unit));
    uint8_t poison[sizeof(*splash)];
    memset(poison, 0xa5, sizeof(poison));
    T_EQ(memcmp(splash, poison, sizeof(poison)), 0); /* Test builds poison the released slot. */
    artillery_t *again = G_AddArtillery(unit);
    T_ASSERT(again == splash);
    T_FEQ(again->area_full, 0, 0.001f); /* Reuse hands back zeroed storage. */
    G_PoolsReleaseEdict(unit);
    T_NULL(unit->components);
    FOR_LOOP(i, count) T_NULL(G_GetComponent(unit, ids[i]));
}

/* Restore allocates detached zeroed storage and attaches it under the saved exact ID. */
TEST(wc3_components, detached_restore_storage_attaches_by_exact_id) {
    reset_entities();
    edict_t *unit = G_Spawn();
    component_t *record = G_AllocDetachedComponent(COMPONENT_FOOD);
    T_NOT_NULL(record);
    T_EQ(record->id, 0);
    T_NULL(unit->components);
    G_AttachComponent(unit, record, COMPONENT_FOOD);
    T_ASSERT(G_GetFood(unit) == (food_t *)record);
    T_EQ(record->id, COMPONENT_FOOD);
    G_FreeFood(unit);
    T_NULL(unit->components);
}
#endif
