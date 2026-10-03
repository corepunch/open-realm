#include "g_local.h"
#include <stdio.h>
#include <string.h>

#define POOL_CAP 2048

#define DEFINE_POOL(name, type) \
static struct name##_slot { type data; uint16_t next; uint8_t live; } name##_slots[POOL_CAP]; \
static uint16_t name##_free; \
static uint8_t name##_ready; \
static type name##_empty; \
static void name##_boot(void) { \
    if (name##_ready) return; \
    name##_free = 1; \
    for (uint16_t i = 1; i < POOL_CAP - 1; i++) name##_slots[i].next = (uint16_t)(i + 1); \
    name##_slots[POOL_CAP - 1].next = 0; \
    name##_ready = 1; \
} \
type *G_PoolAlloc_##name(void) { \
    name##_boot(); \
    uint16_t i = name##_free; \
    if (!i) { fprintf(stderr, "WC3 pool %s exhausted\n", #name); return NULL; } \
    name##_free = name##_slots[i].next; \
    memset(&name##_slots[i], 0, sizeof(name##_slots[i])); \
    name##_slots[i].live = 1; \
    return &name##_slots[i].data; \
} \
static void name##_drop(type *ptr) { \
    if (!ptr || ptr == &name##_empty) return; \
    struct name##_slot *slot = (struct name##_slot *)ptr; \
    uint16_t i = (uint16_t)(slot - name##_slots); \
    if (i == 0 || i >= POOL_CAP || !slot->live) return; \
    slot->live = 0; \
    slot->next = name##_free; \
    name##_free = i; \
} \
type *E_##name(edict_t *ent) { \
    if (!ent) return &name##_empty; \
    if (!ent->name) ent->name = G_PoolAlloc_##name(); \
    return ent->name ? ent->name : &name##_empty; \
} \
type const *E_##name##_get(edict_t const *ent) { \
    return ent && ent->name ? ent->name : &name##_empty; \
} \
void G_PoolDrop_##name(edict_t *ent) { \
    if (!ent || !ent->name) return; \
    name##_drop(ent->name); \
    ent->name = NULL; \
}

DEFINE_POOL(construction, edictConstruction_s)
DEFINE_POOL(research, edictResearch_s)
DEFINE_POOL(rally, edictRally_s)
DEFINE_POOL(food, edictFood_s)
DEFINE_POOL(buildwork, edictBuildwork_s)
DEFINE_POOL(revival, edictRevival_s)
DEFINE_POOL(sacrifice, edictSacrifice_s)
DEFINE_POOL(unsummon, edictUnsummon_s)
DEFINE_POOL(shadowmeld, edictShadowMeld_s)
DEFINE_POOL(militia, edictMilitia_s)
DEFINE_POOL(polymorph, edictPolymorph_s)
DEFINE_POOL(raven, edictRaven_s)
DEFINE_POOL(blight_growth, edictBlightGrowth_s)
DEFINE_POOL(ensnare, edictEnsnare_s)
DEFINE_POOL(ancient_root, edictAncientRoot_s)
DEFINE_POOL(goldmine, edictGoldMine_s)
DEFINE_POOL(mineoverlay, edictMineOverlay_s)
DEFINE_POOL(acolyte_mine, edictAcolyteMine_s)
DEFINE_POOL(item, edictItem_s)
DEFINE_POOL(destructable, edictDestructable_s)
DEFINE_POOL(cargo, edictCargo_s)
DEFINE_POOL(stock, edictStock_t)
DEFINE_POOL(waygate, edictWaygate_s)
DEFINE_POOL(artillery, edictArtillery_t)
DEFINE_POOL(avatar, edictAvatar_s)
DEFINE_POOL(sleep, edictSleep_s)
DEFINE_POOL(channel, edictChannel_s)

void G_PoolsReset(void) {
    construction_ready = 0; memset(construction_slots, 0, sizeof(construction_slots)); construction_free = 0;
    research_ready = 0; memset(research_slots, 0, sizeof(research_slots)); research_free = 0;
    rally_ready = 0; memset(rally_slots, 0, sizeof(rally_slots)); rally_free = 0;
    food_ready = 0; memset(food_slots, 0, sizeof(food_slots)); food_free = 0;
    buildwork_ready = 0; memset(buildwork_slots, 0, sizeof(buildwork_slots)); buildwork_free = 0;
    revival_ready = 0; memset(revival_slots, 0, sizeof(revival_slots)); revival_free = 0;
    sacrifice_ready = 0; memset(sacrifice_slots, 0, sizeof(sacrifice_slots)); sacrifice_free = 0;
    unsummon_ready = 0; memset(unsummon_slots, 0, sizeof(unsummon_slots)); unsummon_free = 0;
    shadowmeld_ready = 0; memset(shadowmeld_slots, 0, sizeof(shadowmeld_slots)); shadowmeld_free = 0;
    militia_ready = 0; memset(militia_slots, 0, sizeof(militia_slots)); militia_free = 0;
    polymorph_ready = 0; memset(polymorph_slots, 0, sizeof(polymorph_slots)); polymorph_free = 0;
    raven_ready = 0; memset(raven_slots, 0, sizeof(raven_slots)); raven_free = 0;
    blight_growth_ready = 0; memset(blight_growth_slots, 0, sizeof(blight_growth_slots)); blight_growth_free = 0;
    ensnare_ready = 0; memset(ensnare_slots, 0, sizeof(ensnare_slots)); ensnare_free = 0;
    ancient_root_ready = 0; memset(ancient_root_slots, 0, sizeof(ancient_root_slots)); ancient_root_free = 0;
    goldmine_ready = 0; memset(goldmine_slots, 0, sizeof(goldmine_slots)); goldmine_free = 0;
    mineoverlay_ready = 0; memset(mineoverlay_slots, 0, sizeof(mineoverlay_slots)); mineoverlay_free = 0;
    acolyte_mine_ready = 0; memset(acolyte_mine_slots, 0, sizeof(acolyte_mine_slots)); acolyte_mine_free = 0;
    item_ready = 0; memset(item_slots, 0, sizeof(item_slots)); item_free = 0;
    destructable_ready = 0; memset(destructable_slots, 0, sizeof(destructable_slots)); destructable_free = 0;
    cargo_ready = 0; memset(cargo_slots, 0, sizeof(cargo_slots)); cargo_free = 0;
    stock_ready = 0; memset(stock_slots, 0, sizeof(stock_slots)); stock_free = 0;
    waygate_ready = 0; memset(waygate_slots, 0, sizeof(waygate_slots)); waygate_free = 0;
    artillery_ready = 0; memset(artillery_slots, 0, sizeof(artillery_slots)); artillery_free = 0;
    avatar_ready = 0; memset(avatar_slots, 0, sizeof(avatar_slots)); avatar_free = 0;
    sleep_ready = 0; memset(sleep_slots, 0, sizeof(sleep_slots)); sleep_free = 0;
    channel_ready = 0; memset(channel_slots, 0, sizeof(channel_slots)); channel_free = 0;
}

void G_PoolsReleaseEdict(edict_t *ent) {
    if (!ent) return;
    G_PoolDrop_construction(ent);
    G_PoolDrop_research(ent);
    G_PoolDrop_rally(ent);
    G_PoolDrop_food(ent);
    G_PoolDrop_buildwork(ent);
    G_PoolDrop_revival(ent);
    G_PoolDrop_sacrifice(ent);
    G_PoolDrop_unsummon(ent);
    G_PoolDrop_shadowmeld(ent);
    G_PoolDrop_militia(ent);
    G_PoolDrop_polymorph(ent);
    G_PoolDrop_raven(ent);
    G_PoolDrop_blight_growth(ent);
    G_PoolDrop_ensnare(ent);
    G_PoolDrop_ancient_root(ent);
    G_PoolDrop_goldmine(ent);
    G_PoolDrop_mineoverlay(ent);
    G_PoolDrop_acolyte_mine(ent);
    G_PoolDrop_item(ent);
    G_PoolDrop_destructable(ent);
    G_PoolDrop_cargo(ent);
    G_PoolDrop_stock(ent);
    G_PoolDrop_waygate(ent);
    G_PoolDrop_artillery(ent);
    G_PoolDrop_avatar(ent);
    G_PoolDrop_sleep(ent);
    G_PoolDrop_channel(ent);
}
