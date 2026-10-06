#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
extern void reset_entities(void),setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t,float,float);
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);
extern bool run_test_jass(cstring_t);

static void category_test_world(void) {
    reset_entities();setup_test_world();uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
}
static edict_t *category_test_unit(float x,float y) {
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),x,y);
    unit->s.model=1;unit->collision=8;unit->svflags|=SVF_MONSTER;
    unit->stand=unit_stand;unit_stand(unit);gi.LinkEntity(unit);G_PublishMoveSpatialObject(unit);return unit;
}
static bool category_test_endpoint(edict_t *source,float x,float y,uint8_t mask) {
    movePathQuery_t query={{&source->s.origin2,NULL,1,mask},source,NULL,true};
    return G_UnitMovePathFinePointIsPathable(&query,(float[]){x,y});
}

/* CItem category18 blocks item10/build08, independently of player ownership;
 * it is not a unit blocker token (payload60706f73 vs unit60706375). */
TEST(wc3_object_categories, item_fine_membership_and_null_collector_token) {
    category_test_world();edict_t *source=category_test_unit(272,304);
    edict_t *item=SP_SpawnAtLocation(MAKEFOURCC('s','p','r','o'),0,&(vec2_t){304,304});
    T_NOT_NULL(item);if(!item)return;
    wc3FineBox_t box=G_GetMoveSpatialObject(item-g_edicts)->box;
    T_EQ(box.min.x,9);T_EQ(box.min.y,9);T_EQ(box.max.x,10);T_EQ(box.max.y,10);
    FOR_LOOP(player,MAX_PLAYERS) {
        item->s.player=player;
        T_ASSERT(category_test_endpoint(source,9.5f,9.5f,2));
        T_ASSERT(!category_test_endpoint(source,9.5f,9.5f,8));
        T_ASSERT(!category_test_endpoint(source,9.5f,9.5f,0x10));
        T_ASSERT(category_test_endpoint(source,9.5f,9.5f,4));
    }
    vec2_t goal={304,304};movePathQuery_t query={{&source->s.origin2,&goal,1,0x10},source,NULL,true};
    edict_t *tokens[32];unsigned count=G_CollectUnitMoveStepBlockers(&query,NULL,tokens);T_EQ(count,1);if(count)T_NULL(tokens[0]);
    G_RemoveItem(item);T_ASSERT(category_test_endpoint(source,9.5f,9.5f,0x10));
    reset_entities();setup_test_world();
}

/* Building identity does not retire the unit's independent fine object;
 * authored movetp determines its category, separately from static textures. */
TEST(wc3_object_categories, building_keeps_own_category_zero_or_authored_mask) {
    category_test_world();edict_t *unit=category_test_unit(304,304);
    unit->s.flags|=EF_BUILDING;
    UnitData_t data={.moveTypeName="none"};unit->data.UnitData=&data;
    G_PublishMoveSpatialObject(unit);wc3FineBox_t box=G_GetMoveSpatialObject(unit-g_edicts)->box;
    T_EQ(box.min.x,9);T_EQ(box.max.x,10);T_EQ(S_UnitMoveCategory(unit),0);
    data.moveTypeName="unbuild";T_EQ(S_UnitMoveCategory(unit),8);
    data.moveTypeName="foot";T_EQ(S_UnitMoveCategory(unit),0xca);
    T_NOT_NULL(S_GetMoveProximity(unit-g_edicts));
    reset_entities();setup_test_world();
}

/* BASE-02.2/FOOT-03.1 category witnesses. Drive the same world adapter used
 * by routing, segment validation, endpoint admission and blocker collection. */
TEST(wc3_object_categories, authored_categories_across_fine_consumers) {
    static struct { cstring_t type; uint8_t category; } const rows[] = {
        {"foot",0xca},{"horse",0xca},{"hover",0xca},{"float",0xca},
        {"amph",0xca},{"fly",0},{"unbuild",8},{"none",0}
    };
    static uint8_t const masks[] = {2,4,8,0x10,0x40,0x80};
    FOR_LOOP(i,sizeof(rows)/sizeof(*rows)) {
        category_test_world();edict_t *source=category_test_unit(208,304);
        edict_t *object=category_test_unit(304,304);
        UnitData_t data={.moveTypeName=rows[i].type};object->data.UnitData=&data;
        object->s.flags|=EF_BUILDING;
        if(!strcmp(rows[i].type,"fly"))object->aiflags|=AI_FLYING;
        G_PublishMoveSpatialObject(object);
        T_EQ(S_UnitMoveCategory(object),rows[i].category);
        T_EQ(G_GetMoveSpatialObject(object-g_edicts)->box.max.x,10);
        FOR_LOOP(j,sizeof(masks)) {
            bool blocked=(rows[i].category&masks[j])!=0;
            vec2_t goal={400,304};
            movePathQuery_t query={{&source->s.origin2,&goal,1,masks[j]},source,NULL,true};
            T_EQ(G_UnitMovePathLineIsPathable(&query),!blocked);
            T_EQ(category_test_endpoint(source,9.5f,9.5f,masks[j]),!blocked);
            object->movement.velocity.x=1;
            T_ASSERT(G_UnitMovePathLineIsPathable(&query));
            T_EQ(category_test_endpoint(source,9.5f,9.5f,masks[j]),!blocked);
            object->movement.velocity.x=0;
        }
    }
    reset_entities();setup_test_world();
}

TEST(wc3_object_categories, native_reposition_releases_carried_item) {
    category_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local unit u = CreateUnit(Player(0), 'Hpal', 304.0, 304.0, 0.0)\n"
        " local item i = CreateItem('spro', 400.0, 400.0)\n"
        " call BJassAssert(UnitAddItem(u, i), \"pickup\")\n"
        " call SetItemPosition(i, 528.0, 560.0)\n"
        " call BJassAssert(not UnitHasItem(u, i), \"release carrier\")\n"
        " call BJassAssert(GetItemX(i) == 528.0, \"admitted x\")\n"
        " call BJassAssert(GetItemY(i) == 560.0, \"admitted y\")\n"
        "endfunction\n"));
    edict_t *source=category_test_unit(496,560);
    T_ASSERT(!category_test_endpoint(source,16.5f,17.5f,0x10));
    reset_entities();setup_test_world();
}

TEST(wc3_object_categories, native_item_admission_ignores_units_but_not_other_items) {
    category_test_world();category_test_unit(304,304);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local item a = CreateItem('spro',304.0, 304.0)\n"
        " local item b = CreateItem('spro',304.0, 304.0)\n"
        " call BJassAssert(GetItemX(a) == 304.0, \"unit does not block item\")\n"
        " call BJassAssert(GetItemY(b) != 304.0, \"item blocks second item\")\n"
        " call SetItemPosition(b,304.0, 304.0)\n"
        " call BJassAssert(GetItemY(b) != 304.0, \"reposition admits against item\")\n"
        " call RemoveItem(a)\n"
        " call SetItemPosition(b,304.0, 304.0)\n"
        " call BJassAssert(GetItemY(b) == 304.0, \"removed item releases admission\")\n"
        "endfunction\n"));
    reset_entities();setup_test_world();
}

TEST(wc3_object_categories, carried_reposition_publishes_both_points_in_order) {
    category_test_world();
    edict_t *hero=alloc_test_unit(MAKEFOURCC('H','p','a','l'),304,304);
    edict_t *item=SP_SpawnAtLocation(MAKEFOURCC('s','p','r','o'),0,&(vec2_t){400,400});
    T_ASSERT(G_AddItemToSlot(hero,item,0));
    uint64_t before=G_GetMoveSpatialSerial();
    G_SetItemPosition(item,&(vec2_t){528,560});
    T_EQ(G_GetMoveSpatialSerial(),before+2);
    wc3SpatialActive_t const *object=G_GetMoveSpatialObject(item-g_edicts);
    T_EQ(object->box.min.x,16);T_EQ(object->box.min.y,17);
    T_EQ(object->ranks[0],before+2);
    T_NULL(hero->inventory[0]);T_NULL(item->item->carrier);T_ASSERT(item->item->in_world);
    reset_entities();setup_test_world();
}

TEST(wc3_object_categories, constructor_and_reposition_keep_distinct_support_callbacks) {
    category_test_world();uint8_t cells[64*64];memset(cells,0x10,sizeof(cells));
    cells[9*64+9]=cells[9*64+10]=0;CM_SetupTestPathmap(64,64,cells);
    ((war3mapVertex_t *)CM_GetWar3MapVertex(3,2))->level=1;
    edict_t *a=SP_SpawnAtLocation(MAKEFOURCC('s','p','r','o'),0,&(vec2_t){304,304});
    edict_t *b=SP_SpawnAtLocation(MAKEFOURCC('s','p','r','o'),0,&(vec2_t){304,304});
    T_NOT_NULL(a);T_NOT_NULL(b);if(!a || !b)return;
    T_FEQ(b->s.origin2.x,336,0);T_FEQ(b->s.origin2.y,304,0);
    vec2_t out;
    T_ASSERT(!G_FindWidgetPlacementPosition(b,&(vec2_t){304,304},1,0x10,true,&out));
    T_ASSERT(G_FindWidgetPlacementPosition(b,&(vec2_t){304,304},1,0x10,false,&out));
    T_FEQ(out.x,336,0);T_FEQ(out.y,304,0);
    reset_entities();setup_test_world();
}

TEST(wc3_object_categories, pickup_drop_and_save_preserve_item_occupancy) {
    category_test_world();edict_t *source=category_test_unit(272,304);
    edict_t *hero=alloc_test_unit(MAKEFOURCC('H','p','a','l'),304,304);
    edict_t *item=SP_SpawnAtLocation(MAKEFOURCC('s','p','r','o'),0,&(vec2_t){304,304});
    T_ASSERT(G_AddItemToSlot(hero,item,0));
    cstring_t carried_file="/tmp/wc3-category-carried-save.bin";
    T_ASSERT(WriteGame(carried_file));T_ASSERT(ReadGame(carried_file));remove(carried_file);
    /* Inspect before any spatial query can flush a deferred publication. */
    T_EQ(G_GetMoveSpatialObject(item-g_edicts)->box.min.x,G_GetMoveSpatialObject(item-g_edicts)->box.max.x);
    T_ASSERT(category_test_endpoint(source,9.5f,9.5f,0x10));
    T_ASSERT(G_DropItemAtScripted(hero,0,&(vec2_t){304,304}));
    T_FEQ(item->s.origin2.x,304,0);T_FEQ(item->s.origin2.y,304,0);
    T_EQ(G_GetMoveSpatialObject(item-g_edicts)->box.min.x,9);
    T_EQ(G_GetMoveSpatialObject(item-g_edicts)->box.max.x,10);
    T_ASSERT(!category_test_endpoint(source,9.5f,9.5f,0x10));
    cstring_t file="/tmp/wc3-category-item-save.bin";T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    T_ASSERT(!category_test_endpoint(source,9.5f,9.5f,0x10));
    T_ASSERT(G_AddItemToSlot(hero,item,0));T_ASSERT(category_test_endpoint(source,9.5f,9.5f,0x10));
    remove(file);reset_entities();setup_test_world();
}
#endif
