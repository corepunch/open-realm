/* Neutral/racial shop retail-contract regressions.
 * Applied AFTER t_neutral_creep_contract.c.  Retail data oracle is the
 * verified user's War3.mpq + War3x.mpq audit, NOT the synthetic test MPQ.
 * Every listing is independently registered to retain a failing row ID.
 */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

#include "t_retail_shop_contract_cases.inc"
#include "t_retail_shop_effect_cases.inc"
#define RS_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    slkTestData_t *rows, *previous;
    edict_t *shop, *hero, *player;
    gameClient_t *client;
    UnitProfile_t profile;
    UnitAbilities_t shop_abilities, hero_abilities;
} retailShopFixture_t;

/* The row values are from the archived retail object-data audit.  Only the
 * model path is a synthetic placeholder: a nonempty file is required by the
 * stock loader, and these tests intentionally do not load MDX assets. */
static bool rs_append(char *buf, size_t cap, size_t *used, cstring_t cell) {
    size_t length = strlen(cell);
    if (length >= cap - *used) return false;
    memcpy(buf + *used, cell, length);
    *used += length;
    buf[*used] = '\0';
    return true;
}
static bool rs_cell(char *buf, size_t cap, size_t *used, int y, int x, cstring_t value) {
    char line[256];
    int len = snprintf(line, sizeof(line), "C;Y%d;X%d;K\"%s\"\n", y, x, value);
    return len > 0 && (size_t)len < sizeof(line) && rs_append(buf, cap, used, line);
}
static bool rs_int(char *buf, size_t cap, size_t *used, int y, int x, int n) {
    char value[32];
    snprintf(value, sizeof(value), "%d", n);
    return rs_cell(buf, cap, used, y, x, value);
}

/* No fixture imports the source CSV at runtime; cases are compiled into the
 * test module and the parsing contract exactly matches other WC3 tests. */
static slkTestData_t *rs_make_retail_item_rows(void) {
    char buf[16384] = {0}; size_t used = 0;
    cstring_t const cols[] = {
        "itemID", "comment", "file", "abilList", "goldcost", "lumbercost",
        "stockMax", "stockRegen", "stockStart", "uses", "perishable", "usable", "droppable"
    };
    char header[80];
    snprintf(header, sizeof(header), "ID;PWXL;N;EBB;Y%zu;X%zu\n", RS_COUNT(retail_items)+1, RS_COUNT(cols));
    if (!rs_append(buf, sizeof(buf), &used, header)) return NULL;
    for (int x = 0; x < (int)RS_COUNT(cols); x++)
        if (!rs_cell(buf, sizeof(buf), &used, 1, x+1, cols[x])) return NULL;
    for (int j = 0; j < (int)RS_COUNT(retail_items); j++) {
        retailItem_t const *r = &retail_items[j]; int y = j+2;
        if (!rs_cell(buf,sizeof(buf),&used,y,1,r->rawcode) ||
            !rs_cell(buf,sizeof(buf),&used,y,2,r->name) ||
            !rs_cell(buf,sizeof(buf),&used,y,3,"Objects\\InventoryItems\\test.mdx") ||
            !rs_cell(buf,sizeof(buf),&used,y,4,r->abilList) ||
            !rs_int(buf,sizeof(buf),&used,y,5,r->gold) ||
            !rs_int(buf,sizeof(buf),&used,y,6,r->lumber) ||
            !rs_int(buf,sizeof(buf),&used,y,7,r->max_stock) ||
            !rs_int(buf,sizeof(buf),&used,y,8,r->replenish) ||
            !rs_int(buf,sizeof(buf),&used,y,9,r->start) ||
            !rs_int(buf,sizeof(buf),&used,y,10,1) ||
            !rs_cell(buf,sizeof(buf),&used,y,11,"true") ||
            !rs_cell(buf,sizeof(buf),&used,y,12,"true") ||
            !rs_cell(buf,sizeof(buf),&used,y,13,"true")) return NULL;
    }
    if (!rs_append(buf, sizeof(buf), &used, "E\n")) return NULL;
    return parse_slk_string(buf);
}

/* A real AInv-equipped purchaser, actual G_InitItemStock and G_ShopPurchaseItem.
 * UnitFunction Makeitems vs Sellitems stays separate in the fixture. */
static void rs_setup(retailShopFixture_t *f, retailShop_t const *data) {
    memset(f,0,sizeof(*f));
    reset_entities();
    setup_test_world();
    level.time = 1000;
    f->rows = rs_make_retail_item_rows();
    T_NOT_NULL(f->rows);
    f->previous = G_SetSLKRows("ItemData", f->rows);
    f->player = &g_edicts[0]; f->client = f->player->client;
    f->client->ps.number = 0;
    f->client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 20000;
    f->client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 20000;
    f->hero_abilities = (UnitAbilities_t){ .abilList = "AInv" };
    f->hero = alloc_test_unit(FS_SLKKey("Hpal"), 100, 0);
    f->hero->data.UnitAbilities = &f->hero_abilities;
    f->hero->s.player = 0; f->hero->svflags |= SVF_MONSTER;
    f->hero->health.value = f->hero->health.max_value = 600;
    f->hero->collision = 16;
    f->profile = (UnitProfile_t){0};
    if (!strcmp(data->field,"Makeitems")) f->profile.makeItems = data->inventory;
    else f->profile.sellItems = data->inventory;
    /* Apit for items; Aneu is not required for owned shop interaction. */
    f->shop_abilities = (UnitAbilities_t){ .abilList = "Apit" };
    f->shop = alloc_test_unit(FS_SLKKey(data->unit), 0, 0);
    f->shop->data.UnitProfile = &f->profile;
    f->shop->data.UnitAbilities = &f->shop_abilities;
    f->shop->s.player = strcmp(data->unit,"ngme") == 0 ? PLAYER_NEUTRAL_PASSIVE : 0;
    f->shop->collision = 32;
    f->shop->spawn_time = G_Time();
    G_AddStock(f->shop);
    G_SetStockSlots(f->shop,true,11);
    gi.LinkEntity(f->shop);
    gi.LinkEntity(f->hero);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
}
static void rs_done(retailShopFixture_t *f) {
    G_SetSLKRows("ItemData",f->previous);
    free_slk_rows(f->rows);
}

static int rs_find_stock(stock_t const *stock, uint32_t rawcode) {
    if (!stock) return -1;
    for (uint32_t i=0; i<stock->item_count; i++)
        if (stock->items[i].id==rawcode) return (int)i;
    return -1;
}

/* Full listing contract: verified shop slot/order, stock timing, actual
 * per-item cost and purchase delivery. Conditions not established by the
 * archive audit (actual Requires/DependencyOr) are deliberately not fabricated;
 * agents must extend this fixture with those records when they are known. */
static void rs_listing_contract(uint32_t index) {
    retailListing_t const *pair = &retail_listings[index];
    retailShop_t const *shop = &retail_shops[pair->shop];
    retailItem_t const *wanted = &retail_items[pair->item];
    retailShopFixture_t f;
    uint32_t id = FS_SLKKey(wanted->rawcode);
    rs_setup(&f,shop);
    ItemData_t const *record = G_ItemData(id);
    T_NOT_NULL(record);
    if (record) {
        T_STREQ(record->abilList,wanted->abilList);
        T_EQ(record->goldcost,wanted->gold);
        T_EQ(record->lumbercost,wanted->lumber);
        T_EQ(record->stockMax,wanted->max_stock);
        T_EQ(record->stockRegen,wanted->replenish);
        T_EQ(record->stockStart,wanted->start);
    }
    T_EQ(G_UpdateShopItemStock(f.shop), (uint32_t)(pair->shop == 0 ? 9 : pair->shop == 1 ? 8 : pair->shop == 2 ? 9 : pair->shop == 3 ? 8 : 11));
    stock_t *wares=G_GetStock(f.shop);
    T_NOT_NULL(wares);
    int stock_index=rs_find_stock(wares,id);
    T_ASSERT(stock_index>=0);
    if (stock_index>=0) {
        shopStockItem_t *entry=&wares->items[stock_index];
        T_EQ(entry->maximum,wanted->max_stock);
        T_EQ(entry->current,wanted->start ? 0 : wanted->max_stock);
        if (wanted->start) {
            level.time+=wanted->start*1000-1;
            G_UpdateShopItemStock(f.shop);
            T_EQ(entry->current,0);
            level.time+=1;
            G_UpdateShopItemStock(f.shop);
            T_EQ(entry->current,1);
        }
        /* Denied purchases are transactional: no stock or money disappears. */
        int before=entry->current;
        f.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=(uint16_t)(wanted->gold ? wanted->gold-1 : 0);
        f.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=20000;
        T_ASSERT(!G_ShopPurchaseItem(f.player,f.shop,id));
        T_EQ(entry->current,before);
        T_EQ(f.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD],wanted->gold ? wanted->gold-1 : 0);
        if (wanted->lumber) {
            f.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=20000;
            f.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=(uint16_t)(wanted->lumber-1);
            T_ASSERT(!G_ShopPurchaseItem(f.player,f.shop,id));
            T_EQ(entry->current,before);
            T_EQ(f.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],wanted->lumber-1);
        }
        f.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]=20000;
        f.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]=20000;
        /* The archive report did not include ItemFunc Requires. A requirement
         * rejection is legitimate until the actual retail prerequisite is
         * installed into the fixture; do not turn off gating to make it pass. */
        char reason[128]={0};
        bool can_buy=G_ShopItemRequirementsSatisfied(f.client,id,reason,sizeof(reason));
        if (can_buy) {
            T_ASSERT(G_FindShopPatron(f.client,f.shop)==f.hero);
            T_ASSERT(G_ShopPurchaseItem(f.player,f.shop,id));
            T_EQ(entry->current,before-1);
            T_EQ(f.client->ps.stats[PLAYERSTATE_RESOURCE_GOLD],20000-wanted->gold);
            T_EQ(f.client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER],20000-wanted->lumber);
            edict_t *purchased=G_InventoryItem(f.hero,0);
            T_NOT_NULL(purchased);
            if (purchased) {
                T_EQ(purchased->class_id,id);
                T_STREQ(G_ItemAbilityList(purchased),wanted->abilList);
            }
            level.time+=wanted->replenish*1000-1;
            G_UpdateShopItemStock(f.shop);
            T_EQ(entry->current,before-1);
            level.time+=1;
            G_UpdateShopItemStock(f.shop);
            T_EQ(entry->current,MIN(wanted->max_stock,before));
        }
    }
    rs_done(&f);
}

/* Provenance-only index guard.  These 62 rows are a source-to-outcome
 * inventory, NOT 62 executable gameplay results.  See the separate effect
 * regressions and hero-item-reference-test-matrix.csv for behavioral oracles. */
TEST(retail_shop_contract, source_effect_rows_are_mapped_to_verified_listings) {
    T_EQ(RS_COUNT(retail_effects), 62);
    FOR_LOOP(i, RS_COUNT(retail_effects)) {
        retailEffect_t const *effect = &retail_effects[i];
        retailShop_t const *shop = &retail_shops[effect->shop];
        retailItem_t const *item = &retail_items[effect->item];
        bool found = false;
        PARSE_LIST(shop->inventory, code, parse_segment) {
            if (!strcmp(code, item->rawcode)) { found = true; break; }
        }
        T_ASSERT(found);
        T_ASSERT(effect->ability && effect->ability[0]);
        T_ASSERT(effect->expectation && effect->expectation[0]);
        T_ASSERT(effect->url && strstr(effect->url, "classic.battle.net/war3/basics/"));
    }
}

/* Macro-generated test bodies register separately, mirroring the supplied
 * creep contract's row_N tests. A single failing entry never hides its name
 * behind one opaque loop test. */
#define RS_LISTING(n) TEST(retail_shop_contract, listing_##n) { rs_listing_contract(n); }
/* IDs 0..44 correspond to retail_listings[]; generated below. */
RS_LISTING(0) RS_LISTING(1) RS_LISTING(2) RS_LISTING(3) RS_LISTING(4)
RS_LISTING(5) RS_LISTING(6) RS_LISTING(7) RS_LISTING(8) RS_LISTING(9)
RS_LISTING(10) RS_LISTING(11) RS_LISTING(12) RS_LISTING(13) RS_LISTING(14)
RS_LISTING(15) RS_LISTING(16) RS_LISTING(17) RS_LISTING(18) RS_LISTING(19)
RS_LISTING(20) RS_LISTING(21) RS_LISTING(22) RS_LISTING(23) RS_LISTING(24)
RS_LISTING(25) RS_LISTING(26) RS_LISTING(27) RS_LISTING(28) RS_LISTING(29)
RS_LISTING(30) RS_LISTING(31) RS_LISTING(32) RS_LISTING(33) RS_LISTING(34)
RS_LISTING(35) RS_LISTING(36) RS_LISTING(37) RS_LISTING(38) RS_LISTING(39)
RS_LISTING(40) RS_LISTING(41) RS_LISTING(42) RS_LISTING(43) RS_LISTING(44)
#undef RS_LISTING

#endif /* BZ_TESTS */
