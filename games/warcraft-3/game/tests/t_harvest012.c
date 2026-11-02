/* ORDER-01.12 Human gold Harvest/Return current-order ownership (retail H012). */
#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
#include <stdarg.h>

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
void unit_stand(edict_t *);
void unit_die(edict_t *, edict_t *);
void monster_think(edict_t *);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);
bool WriteGame(cstring_t);
bool ReadGame(cstring_t);
extern float HARVEST_GOLD_CAPACITY;

/* Agld 12500 gold, one-second interval, one miner (stock Data1..Data3). */
static const char h012_ability_rows[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\nC;Y1;X4;K\"Data12\"\nC;Y1;X5;K\"Data13\"\n"
    "C;Y2;X1;K\"Agld\"\nC;Y2;X2;K\"Agld\"\nC;Y2;X3;K12500\nC;Y2;X4;K1\nC;Y2;X5;K1\n"
    "E\n";

/* Public expectations come from the retail H012 capture: each JASS phase
 * reads GetUnitCurrentOrder and the last issued-order callback. */
static char const h012_script[] =
    "globals\nunit worker=null\nunit mine=null\nunit hall=null\ntrigger issued=null\n"
    "trigger pointIssued=null\ntrigger immediateIssued=null\n"
    "integer lastId=0\nunit lastTarget=null\nboolean lastPoint=false\nboolean lastImmediate=false\ninteger events=0\nendglobals\n"
    "function record takes boolean point, boolean immediate returns nothing\nif GetTriggerUnit()!=worker then\nreturn\nendif\n"
    "set events=events+1\nset lastId=GetIssuedOrderId()\nset lastTarget=GetOrderTargetUnit()\n"
    "set lastPoint=point\nset lastImmediate=immediate\nendfunction\n"
    "function seen takes nothing returns nothing\ncall record(false,false)\nendfunction\n"
    "function seenPoint takes nothing returns nothing\ncall record(true,false)\nendfunction\n"
    "function seenImmediate takes nothing returns nothing\ncall record(false,true)\nendfunction\n"
    "function head takes integer id, string label returns nothing\n"
    "call BJassAssert(GetUnitCurrentOrder(worker)==id, label+\" head=\"+I2S(GetUnitCurrentOrder(worker)))\nendfunction\n"
    "function last takes integer id, unit target, string label returns nothing\n"
    "call BJassAssert(lastId==id and lastTarget==target, label+\" last=\"+I2S(lastId)+\" target=\"+I2S(GetUnitTypeId(lastTarget)))\nendfunction\n"
    "function smart takes nothing returns nothing\n"
    "call BJassAssert(IssueTargetOrder(worker,\"smart\",mine),\"Smart mine accepted\")\n"
    "call head(851971,\"Smart owns approach\")\ncall last(851971,mine,\"Smart published\")\nendfunction\n"
    "function inside takes nothing returns nothing\ncall BJassAssert(IsUnitHidden(worker),\"inside hidden\")\n"
    "call head(851971,\"Smart retained inside\")\nendfunction\n"
    "function returning takes nothing returns nothing\ncall head(852017,\"exit publishes resume to hall\")\n"
    "call last(852017,hall,\"resumeharvesting targets hall\")\ncall BJassAssert(not lastPoint and not lastImmediate,\"target event\")\nendfunction\n"
    "function resumed takes nothing returns nothing\ncall head(852018,\"deposit resumes Harvest\")\n"
    "call last(852018,mine,\"Harvest targets mine\")\nendfunction\n"
    "function insideHarvest takes nothing returns nothing\ncall head(852018,\"Harvest retained inside\")\nendfunction\n"
    "function insideMove takes nothing returns nothing\nlocal integer before=events\n"
    "call BJassAssert(IssuePointOrder(worker,\"move\",384,512),\"inside Move retained\")\n"
    "call head(852018,\"inside Move keeps Harvest head\")\ncall BJassAssert(events==before,\"retained Move unpublished\")\nendfunction\n"
    "function exitMove takes nothing returns nothing\ncall head(851986,\"retained Move activates at exit\")\n"
    "call BJassAssert(lastId==851986 and lastPoint,\"Move published at exit\")\nendfunction\n"
    "function returnNow takes nothing returns nothing\n"
    "call BJassAssert(IssueImmediateOrder(worker,\"returnresources\"),\"carried Return accepted\")\n"
    "call head(852020,\"Return Resources head\")\ncall BJassAssert(lastId==852020 and lastImmediate,\"Return published\")\nendfunction\n"
    "function returned takes nothing returns nothing\ncall head(0,\"explicit Return completes without resume\")\nendfunction\n"
    "function rejects takes nothing returns nothing\nlocal integer before=events\n"
    "call BJassAssert(not IssueImmediateOrder(worker,\"returnresources\"),\"empty Return rejected\")\n"
    "call BJassAssert(not IssueTargetOrder(worker,\"harvest\",hall),\"Harvest hall rejected\")\n"
    "call BJassAssert(events==before,\"rejections unpublished\")\nendfunction\n"
    "function resume takes nothing returns nothing\n"
    "call BJassAssert(IssueImmediateOrder(worker,\"resumeharvesting\"),\"resume accepted\")\n"
    "call head(852017,\"resume head\")\ncall BJassAssert(lastId==852017 and lastImmediate,\"resume published\")\nendfunction\n"
    "function resumeHead takes nothing returns nothing\ncall head(852017,\"resume retained inside\")\nendfunction\n"
    "function stop takes nothing returns nothing\ncall BJassAssert(IssueImmediateOrder(worker,\"stop\"),\"Stop\")\n"
    "call head(0,\"Stop retires\")\nendfunction\n"
    "function kill takes nothing returns nothing\ncall KillUnit(worker)\ncall head(0,\"death retires inside\")\nendfunction\n"
    "function removeMine takes nothing returns nothing\ncall RemoveUnit(mine)\nendfunction\n"
    "function removeHall takes nothing returns nothing\ncall RemoveUnit(hall)\n"
    "call head(852017,\"Return retained through removal\")\nendfunction\n"
    "function newHall takes nothing returns nothing\nset hall=CreateUnit(Player(0),'hTST',768,0,270)\n"
    "call head(0,\"new drop-off does not resume\")\nendfunction\n"
    "function idle takes nothing returns nothing\ncall head(0,\"idle\")\nendfunction\n"
    "function fresh takes nothing returns nothing\nset worker=CreateUnit(Player(0),'hpea',384,0,180)\n"
    "call BJassAssert(not IssueImmediateOrder(worker,\"resumeharvesting\"),\"no previous mine rejected\")\n"
    "call BJassAssert(not IssueImmediateOrder(worker,\"returnresources\"),\"fresh empty Return rejected\")\nendfunction\n"
    "function auto takes nothing returns nothing\n"
    "call BJassAssert(IssueImmediateOrder(worker,\"autoharvestgold\"),\"autoharvestgold accepted\")\n"
    "call head(852018,\"autoharvestgold becomes Harvest\")\ncall last(852018,mine,\"auto Harvest published\")\nendfunction\n"
    "function main takes nothing returns nothing\n"
    "set mine=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'ngol',0,0,270)\ncall SetResourceAmount(mine,12500)\n"
    "set hall=CreateUnit(Player(0),'hTST',768,0,270)\nset worker=CreateUnit(Player(0),'hpea',384,0,180)\n"
    "set issued=CreateTrigger()\nset pointIssued=CreateTrigger()\nset immediateIssued=CreateTrigger()\n"
    "call TriggerRegisterPlayerUnitEvent(immediateIssued,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
    "call TriggerRegisterPlayerUnitEvent(pointIssued,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
    "call TriggerRegisterPlayerUnitEvent(issued,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)\n"
    "call TriggerAddAction(issued,function seen)\ncall TriggerAddAction(pointIssued,function seenPoint)\n"
    "call TriggerAddAction(immediateIssued,function seenImmediate)\nendfunction\n";

static void h012_call(cstring_t name) {
    jass_callbyname(level.vm, name, false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    if (jass_rterror_pending(level.vm)) {
        fprintf(stderr, "H012 phase %s: %s\n", name, jass_rterror_message(level.vm));
        jass_rterror_clear(level.vm);
    }
}

static edict_t *h012_find(uint32_t class_id) {
    edict_t *found = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == class_id && !G_IsDeferredFree(g_edicts + i)) found = g_edicts + i;
    return found;
}

static void h012_prepare_worker(edict_t *worker) {
    worker->health.value = worker->health.max_value = 220;
    worker->collision = 16; worker->unitinfo.MoveSpeed = 270;
    worker->movetype = MOVETYPE_STEP; worker->svflags |= SVF_MONSTER;
    worker->think = monster_think; worker->stand = unit_stand; worker->die = unit_die;
    gi.LinkEntity(worker);
}

typedef enum { H012_INSIDE, H012_OUTSIDE, H012_DEPOSIT } h012Wait_t;

/* Runs real server frames until the public resource state changes. */
static bool h012_run_until(edict_t *worker, h012Wait_t wait, uint32_t gold) {
    FOR_LOOP(frame, 400) {
        bool const inside = S_GoldMineWorkerIsInside(worker);
        if ((wait == H012_INSIDE && inside) || (wait == H012_OUTSIDE && !inside) ||
            (wait == H012_DEPOSIT && game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] > gold))
            return true;
        level.time += FRAMETIME; globals.RunFrame();
    }
    return false;
}

TEST(wc3_harvest012, human_gold_heads_follow_retail_phase_publications) {
    cstring_t const save = Test_TempPath("h012-gold.bin");
    float const old_capacity = HARVEST_GOLD_CAPACITY;
    slkTestData_t *rows = parse_slk_string(h012_ability_rows);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass(h012_script));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    HARVEST_GOLD_CAPACITY = 10;
    edict_t *worker = h012_find(MAKEFOURCC('h','p','e','a'));
    edict_t *mine = h012_find(MAKEFOURCC('n','g','o','l'));
    edict_t *hall = h012_find(MAKEFOURCC('h','T','S','T'));
    T_NOT_NULL(worker); T_NOT_NULL(mine); T_NOT_NULL(hall);
    if (!worker || !mine || !hall) goto done;
    h012_prepare_worker(worker);
    mine->health.value = mine->health.max_value = 1000; mine->collision = 64; gi.LinkEntity(mine);
    /* The synthetic hTST drop-off row has no UnitBalance profile; give it the
     * live-unit class GetOrderTargetUnit requires of every structure. */
    hall->svflags |= SVF_MONSTER;
    hall->health.value = hall->health.max_value = 1000; hall->collision = 64; gi.LinkEntity(hall);
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    unit_stand(worker);

    /* Smart owns approach and the hidden interval; exit and deposit publish. */
    h012_call("smart");
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0)); h012_call("inside");
    T_EQ(worker->movement.repulse.disable_depth, 1);
    T_ASSERT(h012_run_until(worker, H012_OUTSIDE, 0)); h012_call("returning");
    T_EQ(worker->harvested_gold, 10); T_EQ(worker->movement.repulse.disable_depth, 0);
    T_ASSERT(h012_run_until(worker, H012_DEPOSIT, 1000)); h012_call("resumed");
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1010);

    /* An inside Move is retained below the head and survives a save. */
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0)); h012_call("insideHarvest");
    h012_call("insideMove"); T_EQ(worker->order_queue.count, 1);
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save));
    T_ASSERT(S_GoldMineWorkerIsInside(worker)); T_EQ(worker->order_queue.count, 1);
    T_EQ(worker->movement.repulse.disable_depth, 1);
    h012_call("insideHarvest");
    T_ASSERT(h012_run_until(worker, H012_OUTSIDE, 0)); h012_call("exitMove");
    T_EQ(worker->harvested_gold, 10); T_EQ(mine->peonsinside, 0);

    /* Explicit Return completes at deposit; empty/invalid commands reject. */
    h012_call("returnNow");
    T_ASSERT(h012_run_until(worker, H012_DEPOSIT, 1010)); h012_call("returned");
    T_EQ(worker->harvested_gold, 0);
    h012_call("rejects");

    /* Resume remembers the last mine, then carries through a saved return. */
    h012_call("resume");
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0)); h012_call("resumeHead");
    T_ASSERT(h012_run_until(worker, H012_OUTSIDE, 0)); h012_call("returning");
    h012_call("stop"); h012_call("resume"); T_EQ(worker->harvested_gold, 10);
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save)); h012_call("resumeHead");
    T_ASSERT(h012_run_until(worker, H012_DEPOSIT, 1020)); h012_call("resumed");

    /* A removed drop-off retires the carried return on the next tick. */
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0));
    T_ASSERT(h012_run_until(worker, H012_OUTSIDE, 0)); h012_call("returning");
    h012_call("removeHall");
    level.time += FRAMETIME; globals.RunFrame();
    h012_call("idle"); T_EQ(worker->harvested_gold, 10);
    h012_call("newHall");
    hall = h012_find(MAKEFOURCC('h','T','S','T')); T_NOT_NULL(hall);
    if (!hall) goto done;
    hall->svflags |= SVF_MONSTER;
    hall->health.value = hall->health.max_value = 1000; hall->collision = 64; gi.LinkEntity(hall);
    FOR_LOOP(frame, 4) { level.time += FRAMETIME; globals.RunFrame(); }
    h012_call("idle"); T_EQ(worker->harvested_gold, 10);
    h012_call("smart");

    /* Death inside releases occupancy and the separation owner at once. */
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0));
    h012_call("kill"); T_EQ(mine->peonsinside, 0); T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_EQ(worker->movement.repulse.disable_depth, 0);
    G_FreeEdict(worker); level.time += 1001;

    /* A recreated worker has no previous mine; autoharvest uses Harvest. */
    h012_call("fresh");
    worker = h012_find(MAKEFOURCC('h','p','e','a')); T_NOT_NULL(worker);
    if (!worker) goto done;
    h012_prepare_worker(worker); unit_stand(worker);
    h012_call("auto");

    /* Removing the mine releases the hidden worker without a payout. */
    T_ASSERT(h012_run_until(worker, H012_INSIDE, 0));
    h012_call("removeMine");
    level.time += FRAMETIME; globals.RunFrame();
    T_ASSERT(!S_GoldMineWorkerIsInside(worker)); T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->movement.repulse.disable_depth, 0); h012_call("idle");
done:
    remove(save);
    HARVEST_GOLD_CAPACITY = old_capacity;
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}

extern float HARVEST_RANGE, HARVEST_TREE_DAMAGE, HARVEST_LUMBER_CAPACITY, HARVEST_COOLDOWN, HARVEST_SEARCH_RANGE;
bool unit_issueimmediateorder(edict_t *, cstring_t);

typedef struct { edict_t *worker, *mine, *hall, *tree; } h012World_t;

static bool h012_world_type(h012World_t *w, cstring_t worker_type, bool with_tree) {
    *w = (h012World_t){0};
    reset_entities(); setup_test_world();
    char script[sizeof(h012_script)];
    memcpy(script, h012_script, sizeof(script));
    for (char *at = script; (at = strstr(at, "'hpea'")); at += 6) memcpy(at + 1, worker_type, 4);
    if (!run_test_jass(script)) return false;
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    HARVEST_GOLD_CAPACITY = 10; HARVEST_RANGE = 64; HARVEST_TREE_DAMAGE = 1;
    HARVEST_LUMBER_CAPACITY = 10; HARVEST_COOLDOWN = 0.1f; HARVEST_SEARCH_RANGE = 1000;
    w->worker = h012_find(FS_SLKKey(worker_type));
    w->mine = h012_find(MAKEFOURCC('n','g','o','l'));
    w->hall = h012_find(MAKEFOURCC('h','T','S','T'));
    if (!w->worker || !w->mine || !w->hall) return false;
    h012_prepare_worker(w->worker);
    S_AttackProfileWrite(w->worker, 0)->damagePoint = 0.1f;
    w->mine->health.value = w->mine->health.max_value = 1000; w->mine->collision = 64; gi.LinkEntity(w->mine);
    w->hall->svflags |= SVF_MONSTER;
    w->hall->health.value = w->hall->health.max_value = 1000; w->hall->collision = 64; gi.LinkEntity(w->hall);
    if (with_tree) {
        w->tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 384, 256);
        SP_monster_tree(w->tree);
        if (!w->tree->destructable) w->tree->destructable = G_AllocDestructable();
        w->tree->destructable->item_table = (uint32_t)-1;
        w->tree->targtype = TARG_TREE;
        w->tree->health.value = w->tree->health.max_value = 50;
    }
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    unit_stand(w->worker);
    return true;
}

static bool h012_world(h012World_t *w) { return h012_world_type(w, "hpea", true); }

static void h012_frames(uint32_t count) {
    FOR_LOOP(i, count) { level.time += FRAMETIME; globals.RunFrame(); }
}

void harvest_cooldown(edict_t *);

/* Runs real frames until the public head becomes the given value. Fixture
 * models have no attack sequence, so each lumber swing is completed at its
 * animation boundary exactly as the existing lumber engine tests do. */
static uint32_t h012_heads[8], h012_head_count;

static bool h012_until_head(edict_t *worker, uint32_t head) {
    h012_head_count = 0;
    FOR_LOOP(frame, 2000) {
        uint32_t const current = worker->current_order_id;
        if (!h012_head_count || h012_heads[h012_head_count - 1] != current)
            if (h012_head_count < 8) h012_heads[h012_head_count++] = current;
        if (current == head) return true;
        if (worker->currentmove && worker->currentmove->proc == CAbilityHarvest &&
            !strcmp(worker->currentmove->animation, "attack")) {
            worker->wait = 0.01f;
            worker->currentmove->think(worker);
            harvest_cooldown(worker);
            continue;
        }
        h012_frames(1);
    }
    return false;
}

#define H012_ISSUED(w, id, target) do { T_EQ(G_GetIssuedOrderId((w)->worker), (id)); \
    T_ASSERT(G_GetIssuedOrderTarget((w)->worker) == (target)); } while (0)

/* Live I012 scenes7..9: Peasant lumber keeps Smart while chopping, publishes
 * Resume to the hall when full and Harvest to the tree at deposit. */
TEST(wc3_harvest012, human_lumber_heads_follow_retail_phase_publications) {
    float const saved[] = {HARVEST_GOLD_CAPACITY, HARVEST_RANGE, HARVEST_TREE_DAMAGE, HARVEST_LUMBER_CAPACITY, HARVEST_COOLDOWN, HARVEST_SEARCH_RANGE};
    slkTestData_t *rows = parse_slk_string(h012_ability_rows);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    h012World_t w;
    T_ASSERT(h012_world(&w));
    if (!w.worker) goto done;
    T_ASSERT(G_IssueUnitTargetOrder(w.worker, "smart", w.tree, false, 0));
    T_EQ(w.worker->current_order_id, 851971);
    T_ASSERT(h012_until_head(w.worker, 852017)); T_EQ(w.worker->harvested_lumber, 10);
    T_EQ(h012_head_count, 2); T_EQ(h012_heads[0], 851971); /* Smart through ten chops */
    T_FEQ(w.tree->health.value, 40.0f, 0.01f);
    H012_ISSUED(&w, 852017, w.hall);
    T_ASSERT(h012_until_head(w.worker, 852018));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 1010);
    H012_ISSUED(&w, 852018, w.tree);

    /* Explicit Return completes; Stop then Resume returns and resumes. */
    T_ASSERT(h012_until_head(w.worker, 852017));
    T_ASSERT(unit_issueimmediateorder(w.worker, "returnresources"));
    T_EQ(w.worker->current_order_id, 852020); T_EQ(G_GetIssuedOrderId(w.worker), 852020);
    T_ASSERT(h012_until_head(w.worker, 0));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 1020); T_EQ(w.worker->harvested_lumber, 0);
    T_ASSERT(G_IssueUnitTargetOrder(w.worker, "harvest", w.tree, false, 0));
    T_EQ(w.worker->current_order_id, 852018);
    T_ASSERT(h012_until_head(w.worker, 852017));
    T_ASSERT(unit_issueimmediateorder(w.worker, "stop")); T_EQ(w.worker->current_order_id, 0);
    T_ASSERT(unit_issueimmediateorder(w.worker, "resumeharvesting"));
    T_EQ(w.worker->current_order_id, 852017); T_EQ(G_GetIssuedOrderId(w.worker), 852017);
    T_ASSERT(h012_until_head(w.worker, 852018));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 1030);
    H012_ISSUED(&w, 852018, w.tree);
done:
    HARVEST_GOLD_CAPACITY = saved[0]; HARVEST_RANGE = saved[1]; HARVEST_TREE_DAMAGE = saved[2];
    HARVEST_LUMBER_CAPACITY = saved[3]; HARVEST_COOLDOWN = saved[4]; HARVEST_SEARCH_RANGE = saved[5];
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}

typedef enum { H012_IN_SMART_HALL, H012_IN_HARVEST, H012_IN_HOLD, H012_IN_RETURN, H012_IN_RESUME, H012_IN_AUTO, H012_IN_COUNT } h012Inside_t;

/* Live I012 scenes0..6: hidden-miner commands are retained below Unit198
 * and activate at exit; empty Return rejects without a successor. */
TEST(wc3_harvest012, hidden_miner_commands_activate_at_exit) {
    float const saved[] = {HARVEST_GOLD_CAPACITY, HARVEST_RANGE, HARVEST_TREE_DAMAGE, HARVEST_LUMBER_CAPACITY, HARVEST_COOLDOWN, HARVEST_SEARCH_RANGE};
    slkTestData_t *rows = parse_slk_string(h012_ability_rows);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    FOR_LOOP(kind, H012_IN_COUNT) {
        h012World_t w;
        T_ASSERT(h012_world(&w));
        if (!w.worker) break;
        T_ASSERT(G_IssueUnitTargetOrder(w.worker, "smart", w.mine, false, 0));
        T_ASSERT(h012_run_until(w.worker, H012_INSIDE, 0));
        bool accepted = false;
        switch (kind) {
        case H012_IN_SMART_HALL: accepted = G_IssueUnitTargetOrder(w.worker, "smart", w.hall, false, 0); break;
        case H012_IN_HARVEST: accepted = G_IssueUnitTargetOrder(w.worker, "harvest", w.mine, false, 0); break;
        case H012_IN_HOLD: accepted = unit_issueimmediateorder(w.worker, "holdposition"); break;
        case H012_IN_RETURN: accepted = unit_issueimmediateorder(w.worker, "returnresources"); break;
        case H012_IN_RESUME: accepted = unit_issueimmediateorder(w.worker, "resumeharvesting"); break;
        case H012_IN_AUTO: accepted = unit_issueimmediateorder(w.worker, "autoharvestgold"); break;
        }
        T_EQ(accepted, kind != H012_IN_RETURN);
        T_EQ(w.worker->current_order_id, 851971); T_EQ(w.worker->order_queue.count, accepted ? 1u : 0u);
        T_EQ(G_GetIssuedOrderId(w.worker), 851971);
        T_ASSERT(h012_run_until(w.worker, H012_OUTSIDE, 0));
        T_EQ(w.worker->harvested_gold, 10);
        switch (kind) {
        case H012_IN_SMART_HALL: T_EQ(w.worker->current_order_id, 851971); H012_ISSUED(&w, 851971, w.hall); break;
        case H012_IN_HARVEST:
            T_EQ(w.worker->current_order_id, 852018); H012_ISSUED(&w, 852018, w.mine);
            T_ASSERT(h012_until_head(w.worker, 852017)); H012_ISSUED(&w, 852017, w.hall); break;
        case H012_IN_HOLD: T_EQ(w.worker->current_order_id, 0); T_EQ(G_GetIssuedOrderId(w.worker), 851993); break;
        case H012_IN_RETURN: T_EQ(w.worker->current_order_id, 852017); H012_ISSUED(&w, 852017, w.hall); break;
        case H012_IN_RESUME: T_EQ(w.worker->current_order_id, 852017); T_EQ(G_GetIssuedOrderId(w.worker), 852017);
            T_NULL(G_GetIssuedOrderTarget(w.worker)); break;
        case H012_IN_AUTO:
            T_EQ(w.worker->current_order_id, 852018); H012_ISSUED(&w, 852018, w.mine);
            T_ASSERT(h012_until_head(w.worker, 852017)); H012_ISSUED(&w, 852017, w.hall); break;
        }
        if (kind != H012_IN_HOLD) {
            T_ASSERT(h012_run_until(w.worker, H012_DEPOSIT, 1000));
            T_EQ(w.worker->current_order_id, 852018); H012_ISSUED(&w, 852018, w.mine);
        }
    }
    HARVEST_GOLD_CAPACITY = saved[0]; HARVEST_RANGE = saved[1]; HARVEST_TREE_DAMAGE = saved[2];
    HARVEST_LUMBER_CAPACITY = saved[3]; HARVEST_COOLDOWN = saved[4]; HARVEST_SEARCH_RANGE = saved[5];
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}
/* Mine membership is owned by the Gold Mine procedure even though the worker
 * has Ahar rather than Agld. Generic death delivery must reach that owner;
 * the public KillUnit journey above separately drives the real death path. */
TEST(wc3_harvest012, gold_mine_owner_releases_hidden_worker_on_death_event) {
    float const saved[] = {HARVEST_GOLD_CAPACITY, HARVEST_RANGE, HARVEST_TREE_DAMAGE, HARVEST_LUMBER_CAPACITY, HARVEST_COOLDOWN, HARVEST_SEARCH_RANGE};
    slkTestData_t *rows = parse_slk_string(h012_ability_rows);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    h012World_t w = {0};
    T_ASSERT(h012_world(&w));
    if (!w.worker) goto done;
    T_ASSERT(G_IssueUnitTargetOrder(w.worker, "smart", w.mine, false, 0));
    T_ASSERT(h012_run_until(w.worker, H012_INSIDE, 0));
    T_ASSERT(!G_ActorHasAbilityCode(w.worker, MAKEFOURCC('A','g','l','d')));
    T_EQ(w.mine->peonsinside, 1); T_EQ(w.worker->movement.repulse.disable_depth, 1);
    w.worker->health.value = 0; w.worker->svflags |= SVF_DEADMONSTER;
    S_UnitAbilityEvent(w.worker, A_DEATH);
    T_ASSERT(!S_GoldMineWorkerIsInside(w.worker));
    T_EQ(w.mine->peonsinside, 0); T_EQ(w.worker->movement.repulse.disable_depth, 0);
    T_ASSERT(!(w.worker->svflags & SVF_NOCLIENT));
    /* A repeated retirement notification cannot release somebody else's count. */
    S_UnitAbilityEvent(w.worker, A_DEATH);
    T_EQ(w.mine->peonsinside, 0); T_EQ(w.worker->movement.repulse.disable_depth, 0);
done:
    reset_entities();
    HARVEST_GOLD_CAPACITY = saved[0]; HARVEST_RANGE = saved[1]; HARVEST_TREE_DAMAGE = saved[2];
    HARVEST_LUMBER_CAPACITY = saved[3]; HARVEST_COOLDOWN = saved[4]; HARVEST_SEARCH_RANGE = saved[5];
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}
static unsigned h012_restore_errors;
static void (*h012_saved_error)(cstring_t, ...);
static void h012_record_restore_error(cstring_t format, ...) {
    char text[1024]; va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    h012_restore_errors++;
    h012_saved_error("%s", text);
}

/* Retail I012 includes a stock Peon. Its Ahar must come from the authored
 * UnitAbilities row rather than a Peasant-only fixture or native shortcut. */
TEST(wc3_harvest012, orc_peon_gold_heads_and_saved_hidden_successor) {
    float const saved[] = {HARVEST_GOLD_CAPACITY, HARVEST_RANGE, HARVEST_TREE_DAMAGE, HARVEST_LUMBER_CAPACITY, HARVEST_COOLDOWN, HARVEST_SEARCH_RANGE};
    slkTestData_t *rows = parse_slk_string(h012_ability_rows);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    h012World_t w = {0};
    T_ASSERT(h012_world_type(&w, "opeo", false));
    if (!w.worker) goto done;
    bool authored = G_ActorHasAbilityCode(w.worker, MAKEFOURCC('A','h','a','r'));
    T_ASSERT(authored);
    if (!authored) goto done;
    T_EQ(w.worker->class_id, FS_SLKKey("opeo"));
    T_NOT_NULL(w.worker->data.UnitBalance);
    if (!w.worker->data.UnitBalance) goto done;
    w.worker->health.value = w.worker->health.max_value = w.worker->data.UnitBalance->maxHealth;
    h012_call("smart");
    T_ASSERT(h012_run_until(w.worker, H012_INSIDE, 0)); h012_call("inside");
    T_EQ(w.worker->movement.repulse.disable_depth, 1);
    T_ASSERT(h012_run_until(w.worker, H012_OUTSIDE, 0)); h012_call("returning");
    T_EQ(w.worker->harvested_gold, 10);
    T_ASSERT(h012_run_until(w.worker, H012_DEPOSIT, 1000)); h012_call("resumed");
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1010);
    T_ASSERT(h012_run_until(w.worker, H012_INSIDE, 0)); h012_call("insideMove");
    cstring_t save = Test_TempPath("h012-peon.bin");
    T_ASSERT(WriteGame(save));
    h012_saved_error = gi.error; h012_restore_errors = 0; gi.error = h012_record_restore_error;
    bool restored = ReadGame(save);
    gi.error = h012_saved_error;
    T_ASSERT(restored); T_EQ(h012_restore_errors, 0); remove(save);
    T_EQ(w.worker->class_id, FS_SLKKey("opeo"));
    T_ASSERT(S_GoldMineWorkerIsInside(w.worker));
    T_EQ(w.worker->order_queue.count, 1); T_EQ(w.worker->movement.repulse.disable_depth, 1);
    T_ASSERT(h012_run_until(w.worker, H012_OUTSIDE, 0)); h012_call("exitMove");
    T_EQ(w.worker->harvested_gold, 10); T_EQ(w.worker->movement.repulse.disable_depth, 0);
done:
    reset_entities();
    HARVEST_GOLD_CAPACITY = saved[0]; HARVEST_RANGE = saved[1]; HARVEST_TREE_DAMAGE = saved[2];
    HARVEST_LUMBER_CAPACITY = saved[3]; HARVEST_COOLDOWN = saved[4]; HARVEST_SEARCH_RANGE = saved[5];
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}
#endif
