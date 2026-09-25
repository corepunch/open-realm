/*
 * t_unit.c — In-engine unit lifecycle and order tests.
 *
 * Runs inside the real game module via +dedicated 1 +test 'wc3_unit.*'.
 * The real gi, edict pool, and unit data tables are available.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);

/* Forward declarations for functions in m_unit.c without a public header. */
void unit_stand(edict_t *self);
void unit_birth(edict_t *self);
void unit_die(edict_t *self, edict_t *attacker);
void unit_begin_decay(edict_t *self);
void unit_decay_think(edict_t *self);
void unit_entercombat(edict_t *self, edict_t *target);
void unit_leavecombat(edict_t *self);
bool unit_affectingcombat(edict_t *self);
bool unit_issuetargetorder(edict_t *self, cstring_t order, edict_t *target);
bool unit_issueorder(edict_t *self, cstring_t order, vector2_t const *point);
bool unit_issueimmediateorder(edict_t *self, cstring_t order);
bool unit_additem(edict_t *edict, edict_t *item);
bool unit_additemtoslot(edict_t *edict, edict_t *item, uint32_t slot);
slkTestData_t *parse_slk_string(cstring_t slk_text);
void free_slk_rows(slkTestData_t *rows);

static int selection_sound_index_77(cstring_t path) {
    (void)path;
    return 77;
}
static int selection_sound_index_77_alias(cstring_t path, cstring_t alias) { (void)alias; return selection_sound_index_77(path); }


static char death_sound_path[256];
static cstring_t death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
static handle_t death_sound_probe(cstring_t path, uint32_t *size) {
    if (strcmp(path, death_sound_existing)) return NULL;
    *size = 1;
    return malloc(1);
}
static int death_sound_index(cstring_t path) {
    strlcpy(death_sound_path, path, sizeof(death_sound_path));
    return 77;
}

TEST(wc3_unit, death_sound_uses_existing_numbered_asset) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = { 0 };
    edict_t ent = { .data.UnitUI = &ui, .data.UnitWeapons = &weapons };

    setup_test_world();
    death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
    death_sound_path[0] = '\0';
    gi.ReadFile = death_sound_probe;
    gi.SoundIndex = death_sound_index;
    G_RegisterUnitSounds(&ent);
    T_STREQ(death_sound_path, "Units\\Human\\Test\\TestDeath1.wav");
    T_EQ(ent.sound.death, 77);
    gi = old;
}

TEST(wc3_unit, death_sound_uses_existing_unnumbered_asset) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = { 0 };
    edict_t ent = { .data.UnitUI = &ui, .data.UnitWeapons = &weapons };

    setup_test_world();
    death_sound_existing = "Units\\Human\\Test\\TestDeath.wav";
    death_sound_path[0] = '\0';
    gi.ReadFile = death_sound_probe;
    gi.SoundIndex = death_sound_index;
    G_RegisterUnitSounds(&ent);
    T_STREQ(death_sound_path, death_sound_existing);
    T_EQ(ent.sound.death, 77);
    gi = old;
}

static int order_sound_calls, order_sound_index;
static void order_sound_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void order_sound_unicast(edict_t *ent) { (void)ent; }
static void order_sound_capture(edict_t *ent, int channel, int index, float volume, float attenuation, float offset) {
    (void)ent; (void)channel; (void)volume; (void)attenuation; (void)offset;
    order_sound_calls++; order_sound_index = index;
}

static void order_sound_policy_capture(vector3_t const *origin, edict_t *ent, int channel, int index,
                                       float volume, float attenuation, float offset, soundPolicy_t const *policy) {
    T_ASSERT(policy && policy->request);
    T_EQ(policy->request, G_UnitResponseRequest(ent, index));
    order_sound_capture(ent, channel, index, volume, attenuation, offset);
}

/* A right-click must survive command dispatch and the next entity update. */
TEST(wc3_unit, smart_move_emits_selected_unit_response) {
    struct game_import old = gi;
    cstring_t command[] = { "smartpoint", "256", "256" };
    setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 64);
    edict_t *clent = &g_edicts[0];
    ent->movetype = MOVETYPE_STEP;
    ent->stand = unit_stand;
    ent->selected = 1;
    ent->sound.yes[0] = 118; ent->sound.num_yes = 1;
    unit_stand(ent);
    gi.Write = order_sound_write; gi.unicast = order_sound_unicast; gi.Sound = order_sound_capture; gi.SoundPolicy = order_sound_policy_capture;
    order_sound_calls = order_sound_index = 0;
    G_ClientCommand(clent, 3, command);
    T_NOT_NULL(ent->goalentity);
    T_EQ(ent->sound.pending, 118);
    G_RunEntities();
    T_EQ(order_sound_calls, 1);
    T_EQ(order_sound_index, 118);
    T_EQ(ent->sound.pending, 0);
    gi = old;
}

/* Reset the entity pool between tests. */
static void reset_test_entities(void) {
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

/* Create a minimal unit edict with lifecycle callbacks wired up. */
static edict_t *make_unit(float x, float y) {
    static UnitWeapons_t const test_weapons = { .attacksEnabled = 3 };
    edict_t *ent = G_Spawn();
    ent->class_id       = MAKEFOURCC('h','p','e','a');
    G_BindEntityData(ent);
    ent->data.UnitWeapons = &test_weapons;
    ent->s.origin2      = (vector2_t){x, y};
    ent->s.origin.x     = x;
    ent->s.origin.y     = y;
    ent->s.origin.z     = 0;
    ent->s.model        = 1; /* non-zero so IS_HOLLOW is false */
    ent->movetype       = MOVETYPE_STEP;
    ent->collision      = 16.0f;
    ent->stand          = unit_stand;
    ent->birth          = unit_birth;
    ent->die            = unit_die;
    ent->health.value   = G_UnitBalance(ent->class_id)->maxHealth;
    ent->health.max_value = G_UnitBalance(ent->class_id)->maxHealth;
    ent->unitinfo.MoveSpeed = G_UnitBalance(ent->class_id)->speed;
    ent->attack1.type = ATK_NORMAL;
    ent->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    ent->targtype = TARG_GROUND;
    unit_stand(ent);
    return ent;
}

static edict_t *make_inventory_unit(float x, float y) {
    edict_t *ent = make_unit(x, y);
    ent->class_id = MAKEFOURCC('H','p','a','l');
    G_BindEntityData(ent);
    return ent;
}

static edict_t *make_world_item(uint32_t class_id) {
    edict_t *item = G_Spawn();
    item->class_id = class_id;
    G_BindEntityData(item);
    item->s.model = 1;
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    return item;
}

static edict_t *unit_make_harvest_tree(float x, float y) {
    edict_t *tree = G_Spawn();
    tree->s.origin2 = (vector2_t){x, y};
    tree->s.origin.x = x;
    tree->s.origin.y = y;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 100.0f;
    return tree;
}

static edict_t *unit_make_harvest_goldmine(float x, float y) {
    static UnitAbilities_t const abilities = { .abilList = "Agld" };
    edict_t *mine = G_Spawn();
    mine->s.origin2 = (vector2_t){x, y};
    mine->s.origin.x = x;
    mine->s.origin.y = y;
    mine->data.UnitAbilities = &abilities;
    mine->resources = 12500;
    mine->health.value = mine->health.max_value = 1000.0f;
    return mine;
}

TEST(wc3_unit, shared_test_unit_starts_alive) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);

    T_ASSERT(ent->data.UnitBalance->maxHealth > 0.0f);
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth, 0.001f);
    T_FEQ(ent->health.value, ent->health.max_value, 0.001f);
    T_ASSERT(!M_IsDead(ent));
}

TEST(wc3_unit, locust_ability_applies_untargetable_collisionless_traits) {
    static UnitAbilities_t const locust = { .abilList = "Aloc" };
    static UnitAbilities_t const ordinary = { .abilList = "Amov,Aatk" };
    edict_t unit = { .collision = 16.0f, .data.UnitAbilities = &locust };
    edict_t control = { .collision = 16.0f, .data.UnitAbilities = &ordinary };

    G_ApplyUnitAbilityTraits(&unit);
    T_ASSERT(unit.s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(unit.invulnerable);
    T_FEQ(unit.collision, 0.0f, 0.001f);
    T_ASSERT(unit.no_pathing);

    G_ApplyUnitAbilityTraits(&control);
    T_ASSERT(!(control.s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!control.invulnerable);
    T_FEQ(control.collision, 16.0f, 0.001f);
    T_ASSERT(!control.no_pathing);
}

TEST(wc3_unit, selection_sound_registration_caches_all_responses) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X6;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Priority\"\n"
        "C;Y1;X5;K\"Channel\"\n"
        "C;Y1;X6;K\"Flags\"\n"
        "C;Y2;X5;K1\n"
        "C;Y2;X6;K\"WANT3D,NODUPEUSERNAMES,CHANNELFULLPREEMPT,RANDOMPITCH\"\n"
        "C;Y2;X4;K1731\n"
        "C;Y2;X1;K\"FootmanWhat\"\n"
        "C;Y2;X2;K\"FootmanWhat1.wav,FootmanWhat2.wav,FootmanWhat3.wav,FootmanWhat4.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    G_RegisterSelectSounds(ent, "Footman");
    T_EQ(ent->sound.num_select, 4);
    FOR_LOOP(i, ent->sound.num_select) T_ASSERT(ent->sound.select[i]);
    FOR_LOOP(i, ent->sound.num_select)
        T_EQ(G_SoundIndexPolicy(ent->sound.select[i])->priority, 1731);
    soundPolicy_t const *policy = G_SoundIndexPolicy(ent->sound.select[0]);
    T_NOT_NULL(policy);
    if (policy) {
        T_EQ(policy->priority, 1731); T_EQ(policy->group, 1);
        T_EQ(policy->max_channel, 3); T_EQ(policy->max_total, 24);
        T_EQ(policy->flags, SOUND_NO_DUPLICATE_USERS | SOUND_CHANNEL_PREEMPT);
    }
    G_ResetSoundPresentationState();
    T_NULL(G_SoundIndexPolicy(ent->sound.select[0]));
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, shared_sound_file_keeps_label_policy_and_volume_independent) {
    cstring_t slk = "ID;PWXL;N;E\nB;X6;Y3;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\nC;X2;K\"FileNames\"\nC;X3;K\"Channel\"\n"
        "C;X4;K\"Priority\"\nC;X5;K\"Volume\"\nC;X6;K\"Flags\"\n"
        "C;Y2;X1;K\"AliasWhat\"\nC;X2;K\"shared.wav\"\nC;X3;K1\nC;X4;K1731\nC;X5;K127\nC;X6;K\"NODUPEUSERNAMES\"\n"
        "C;Y3;X1;K\"AliasReady\"\nC;X2;K\"shared.wav\"\nC;X3;K4\nC;X4;K900\nC;X5;K63.5\nC;X6;K\"IGNOREUSERNAME\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("UnitAckSounds", rows);
    int what = G_UnitAckSoundVariantIndex("Alias", "What", 0);
    int ready = G_UnitAckSoundVariantIndex("Alias", "Ready", 0);
    T_ASSERT(what > 0 && ready > 0); T_NE(what, ready);
    T_EQ(G_SoundIndexPolicy(what)->group, 1); T_EQ(G_SoundIndexPolicy(what)->priority, 1731);
    T_EQ(G_SoundIndexPolicy(ready)->group, 4); T_EQ(G_SoundIndexPolicy(ready)->priority, 900);
    T_FEQ(G_SoundIndexVolume(what), 1, .001f); T_FEQ(G_SoundIndexVolume(ready), .5f, .001f);
    T_EQ(G_UnitAckSoundVariantIndex("Alias", "What", 0), what);
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(rows);
}

TEST(wc3_unit, selecting_owned_unit_queues_one_ack_sound) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ent->sound.select[0] = 11;
    ent->sound.select[1] = 12;
    ent->sound.num_select = 2;
    G_QueueSelectionSound(ent, true);
    T_ASSERT(ent->sound.pending == 11 || ent->sound.pending == 12);
    T_EQ(ent->sound.pending != 0, 1);
}

TEST(wc3_unit, selection_without_responses_does_not_queue_ack) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_QueueSelectionSound(ent, true);
    T_EQ(ent->sound.pending, 0);
}

TEST(wc3_unit, queued_response_does_not_start_portrait_before_playback_feedback) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, response_preserves_full_sound_configstring_index) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 731));
    T_EQ(ent->sound.pending, 731);
}

void test_sound_event(edict_t *ent, uint32_t request, uint32_t event);

static void response_test_finish(edict_t *ent) {
    uint32_t request = G_UnitResponseRequest(ent, ent->sound.pending);
    ent->sound.pending = 0;
    test_sound_event(ent, request, SOUND_ACCEPTED);
    test_sound_event(ent, request, SOUND_ACCEPTED); /* duplicate cannot advance twice */
    test_sound_event(ent, request, SOUND_STARTED);
    test_sound_event(ent, request, SOUND_ENDED);
}

TEST(wc3_unit, response_feedback_owns_portrait_lifetime_and_rejection) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t first = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0; /* packet sent */
    T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(!G_QueueUnitResponseSound(ent, 12)); /* only one unanswered request */
    test_sound_event(ent, first, SOUND_REJECTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(G_QueueUnitResponseSound(ent, 12));
    uint32_t second = G_UnitResponseRequest(ent, 12);
    T_NE(first, second);
    test_sound_event(ent, second, SOUND_STARTED); /* cannot start before admission */
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_ACCEPTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_STARTED);
    T_ASSERT(G_UnitResponseTalking(ent));
    level.time += 100000; /* neither accelerated game time nor authored length ends playback */
    T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, first, SOUND_ENDED); /* stale receipt */
    T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_ENDED); /* completion OR preemption */
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, response_feedback_rejects_foreign_clients_resets_and_reused_units) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t request = G_UnitResponseRequest(ent, 11);
    char user[16], token[16];
    snprintf(user, sizeof(user), "%u", ent->s.number); snprintf(token, sizeof(token), "%u", request);
    cstring_t accepted[] = {"sound_event", user, token, "1"}, started[] = {"sound_event", user, token, "2"};
    g_edicts[0].client = game.clients; game.clients[0].connected = true; game.clients[0].ps.number = 1;
    G_ClientCommand(g_edicts, 4, accepted); G_ClientCommand(g_edicts, 4, started);
    T_ASSERT(!G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    T_NE(G_UnitResponseRequest(ent, 11), request);
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    request = G_UnitResponseRequest(ent, 11);
    G_ClearUnitResponses(ent); /* same-slot, same-tick reuse must not match */
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, overlapping_response_feedback_keeps_portrait_until_last_voice_ends) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11)); uint32_t a = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0;
    test_sound_event(ent, a, SOUND_ACCEPTED); test_sound_event(ent, a, SOUND_STARTED);
    T_ASSERT(G_QueueUnitResponseSound(ent, 12)); uint32_t b = G_UnitResponseRequest(ent, 12);
    test_sound_event(ent, b, SOUND_ACCEPTED); test_sound_event(ent, b, SOUND_STARTED);
    test_sound_event(ent, a, SOUND_ENDED); T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, b, SOUND_ENDED); T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(G_QueueUnitResponseSound(ent, 13)); uint32_t c = G_UnitResponseRequest(ent, 13);
    game.clients[0].connected = false;
    G_UpdateUnitResponsePresentation();
    test_sound_event(ent, c, SOUND_ACCEPTED); test_sound_event(ent, c, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState(); T_EQ(ent->sound.pending, 0);
}

TEST(wc3_unit, unused_client_slot_cannot_retire_another_clients_playback) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t request = G_UnitResponseRequest(ent, 11);
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    gameClient_t unused = {0}; /* unused slots can still carry zero-initialized player zero */
    G_UpdateUnitResponsePresentation();
    T_ASSERT(G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState();
}

TEST(wc3_unit, selection_reset_ignores_late_response_admission) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ent->sound.select[0] = 731; ent->sound.num_select = 1;
    G_ResetSelectionSoundState();
    G_QueueSelectionSound(ent, true); T_EQ(ent->sound.pending, 731);
    uint32_t request = G_UnitResponseRequest(ent, 731);
    G_QueueSelectionSound(ent, true); /* same unit reselected before old admission */
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_EQ(selection_sound_state[0].selected_sound_count, 0);
    test_sound_event(ent, request, SOUND_ENDED);
    G_QueueSelectionSound(ent, false); request = G_UnitResponseRequest(ent, 731);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_EQ(selection_sound_state[0].selected_sound_count, 1);
    G_ResetSelectionSoundState();
}

TEST(wc3_unit, response_variant_commits_on_acceptance_and_rejection_preserves_previous) {
    cstring_t text = "ID;PWXL;N;E\nB;X2;Y2;D0\nC;Y1;X1;K\"SoundLabel\"\nC;X2;K\"FileNames\"\n"
        "C;Y2;X1;K\"ProbeWhat\"\nC;X2;K\"one.wav,two.wav\"\nE\n";
    slkTestData_t *rows = parse_slk_string(text), *old = G_SetSLKRows("UnitAckSounds", rows);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    int a = G_UnitAckSoundVariantIndex("Probe", "What", 0), b = G_UnitAckSoundVariantIndex("Probe", "What", 1);
    T_ASSERT(G_QueueUnitResponseSound(ent, a));
    uint32_t request = G_UnitResponseRequest(ent, a);
    T_ASSERT(!G_SoundVariantIsLast(a, 0));
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_ASSERT(G_SoundVariantIsLast(a, 0)); T_ASSERT(!G_SoundVariantIsLast(a, 1));
    test_sound_event(ent, request, SOUND_ENDED);
    T_ASSERT(G_QueueUnitResponseSound(ent, b)); request = G_UnitResponseRequest(ent, b);
    test_sound_event(ent, request, SOUND_REJECTED);
    T_ASSERT(G_SoundVariantIsLast(a, 0)); T_ASSERT(!G_SoundVariantIsLast(b, 0));
    T_ASSERT(G_QueueUnitResponseSound(ent, b)); request = G_UnitResponseRequest(ent, b);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_ASSERT(!G_SoundVariantIsLast(a, 0)); T_ASSERT(G_SoundVariantIsLast(b, 0));
    G_ResetSelectionSoundState(); G_ResetSoundPresentationState();
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(rows);
}

TEST(wc3_unit, repeated_selection_walks_pissed_responses_after_three_what_lines) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y2;X1;K\"FootmanPissed\"\n"
        "C;Y2;X2;K\"Pissed1.wav,Pissed2.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    UnitUI_t ui = { .soundLabel = "Footman" };
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    ent->data.UnitUI = &ui;
    ent->sound.select[0] = 11;
    ent->sound.num_select = 1;
    gi.SoundIndex = selection_sound_index_77; gi.SoundIndexAlias = selection_sound_index_77_alias;
    G_ResetSelectionSoundState();

    G_QueueSelectionSound(ent, true); T_EQ(ent->sound.pending, 11);
    /* Rejected responses leave the three-What threshold untouched. */
    uint32_t rejected = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0; test_sound_event(ent, rejected, SOUND_REJECTED);
    FOR_LOOP(i, 3) {
        G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 11);
        response_test_finish(ent);
    }
    G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 77);
    response_test_finish(ent);
    G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 77);
    response_test_finish(ent);

    gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, attack_order_uses_yesattack_instead_of_weapon_swing_slot) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y2;X1;K\"FootmanYesAttack\"\n"
        "C;Y2;X2;K\"Attack1.wav,Attack2.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    UnitUI_t ui = { .soundLabel = "Footman" };
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    ent->data.UnitUI = &ui;
    ent->sound.attack = 0;
    gi.SoundIndex = selection_sound_index_77; gi.SoundIndexAlias = selection_sound_index_77_alias;
    G_QueueAttackOrderSound(ent);
    T_EQ(ent->sound.pending, 77);
    T_EQ(ent->sound.attack, 0);

    gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, ready_sound_queues_owner_only_sound) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ent->sound.ready[0] = 21;
    ent->sound.ready[1] = 22;
    ent->sound.num_ready = 2;

    G_QueueReadySound(ent);
    T_ASSERT(ent->sound.owner_pending == 21 || ent->sound.owner_pending == 22);
    T_EQ(ent->sound.owner_pending != 0, 1);
}

/* -----------------------------------------------------------------------
 * Birth tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, birth_sets_birth_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "birth");
}

TEST(wc3_unit, birth_sets_wait_from_build_time) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->wait > 0);
    T_EQ((int)ent->wait, G_UnitBalance(ent->class_id)->buildTime);
}

TEST(wc3_unit, birth_sets_no_ubersplat_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->s.renderfx & RF_NO_UBERSPLAT);
}

/* -----------------------------------------------------------------------
 * Stand tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, stand_sets_stand_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_stand(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_uses_ready_animation_in_combat) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vector2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand ready");
}

TEST(wc3_unit, stop_exits_ready_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vector2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);
    T_STREQ(ent->currentmove->animation, "stand ready");

    unit_issueimmediateorder(ent, "stop");

    T_ASSERT(!unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_clears_build_pointer) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->build = ent;
    unit_stand(ent);
    T_NULL(ent->build);
}

TEST(wc3_unit, stand_clears_no_ubersplat_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->s.renderfx |= RF_NO_UBERSPLAT;
    unit_stand(ent);
    T_ASSERT(!(ent->s.renderfx & RF_NO_UBERSPLAT));
}

TEST(wc3_unit, spawned_building_is_immobile) {
    T_ASSERT(unit_spawn_aiflags(MAKEFOURCC('h','b','a','r')) & AI_IMMOBILE);
}

TEST(wc3_unit, spawned_building_carries_renderer_building_flag) {
    T_ASSERT(G_UnitIsBuilding(MAKEFOURCC('h','b','a','r')));
}

TEST(wc3_unit, spawned_mobile_unit_is_not_immobile) {
    T_ASSERT(!(unit_spawn_aiflags(MAKEFOURCC('h','p','e','a')) & AI_IMMOBILE));
}

/* -----------------------------------------------------------------------
 * Die tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, die_sets_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_die(ent, NULL);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, paused_death_animation_advances_after_kill) {
    animation_t death = { .name = "Death", .interval = { 0, 300 } };
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->animation = &death;
    ent->s.frame = 50;
    ent->paused = true;

    unit_die(ent, NULL);
    T_ASSERT(ent->animation_override);
    T_ASSERT(ent->currentmove && !strcmp(ent->currentmove->animation, "death"));
    ent->animation = &death;
    ent->s.frame = 50;
    monster_think(ent);
    T_EQ((int)ent->s.frame, 150);
}

TEST(wc3_unit, die_emits_registered_death_sound) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->sound.death = 23;

    unit_die(ent, NULL);
    T_EQ(ent->sound.world_pending, 23);
    T_EQ(ent->sound.world_pending_event, EV_DEATH);
    T_EQ(ent->sound.world_pending, 23);
}

TEST(wc3_unit, die_is_one_shot_after_dead_monster_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->sound.death = 23;

    unit_die(ent, NULL);
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);

    /* A second lethal resolution must not replay death-side effects such as
     * sounds/events/loot-trigger publication. */
    ent->sound.world_pending = 0;
    ent->sound.world_pending_event = 0;
    unit_die(ent, NULL);

    T_EQ(ent->sound.world_pending, 0);
    T_EQ(ent->sound.world_pending_event, 0);
}

TEST(wc3_unit, die_raises_dead_monster_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    T_ASSERT(!M_IsDead(ent));
    unit_die(ent, NULL);
    T_FEQ(ent->health.value, 0.0f, 0.001f);
    T_ASSERT(M_IsDead(ent));
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);
}

TEST(wc3_unit, die_clears_selection_and_marks_corpse_unselectable) {
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *ent = make_unit(0, 0);
    client->ps.number = 0;
    ent->s.player = 0;

    G_SelectEntity(client, ent);
    T_ASSERT(G_IsEntitySelected(client, ent));

    unit_die(ent, NULL);

    T_EQ(ent->selected, 0);
    T_ASSERT(ent->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!G_IsEntitySelected(client, ent));

    G_SelectEntity(client, ent);
    T_ASSERT(!G_IsEntitySelected(client, ent));
}

TEST(wc3_unit, die_releases_held_frame_before_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->aiflags |= AI_HOLD_FRAME;

    unit_die(ent, NULL);

    T_ASSERT(!(ent->aiflags & AI_HOLD_FRAME));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, corpse_decay_uses_map_flesh_then_bone_constants) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    static UnitData_t data = { .death = 1.0f, .deathType = 3 };
    edict_t *ent;

    reset_test_entities();
    game.constants.decayTime = 0.2f;
    game.constants.boneDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    unit_begin_decay(ent);

    T_STREQ(ent->currentmove->animation, "decay flesh");
    T_STREQ(ent->animation_request, "decay flesh");
    T_NOT_NULL(ent->currentmove->animation_duration);
    T_FEQ(ent->currentmove->animation_duration(ent), 0.2f, 0.001f);
    T_FEQ(ent->wait, 0.2f, 0.001f);
    ent->currentmove->think(ent);
    ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_STREQ(ent->currentmove->animation, "decay bone");
    T_STREQ(ent->animation_request, "decay bone");
    T_NOT_NULL(ent->currentmove->animation_duration);
    T_FEQ(ent->currentmove->animation_duration(ent), 0.3f, 0.001f);
    T_FEQ(ent->wait, 0.3f, 0.001f);
    FOR_LOOP(i, 4) if (ent->inuse) ent->currentmove->think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, decaying_structure_uses_structure_decay_constant) {
    static UnitBalance_t balance = { .isBuilding = true, .maxHealth = 100.0f };
    static UnitData_t data = { .death = 1.0f, .deathType = 2 };
    edict_t *ent;

    reset_test_entities();
    game.constants.structureDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'b', 'a', 'r');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    unit_begin_decay(ent);

    T_FEQ(ent->wait, 0.3f, 0.001f);
    FOR_LOOP(i, 4) if (ent->inuse) ent->currentmove->think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, corpse_reservation_suspends_decay_timer) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    static UnitData_t data = { .deathType = 3 };
    edict_t *ent;

    reset_test_entities();
    game.constants.decayTime = 0.2f;
    game.constants.boneDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    unit_begin_decay(ent);
    ent->aiflags |= AI_CORPSE_RESERVED;

    FOR_LOOP(i, 5) ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_FEQ(ent->wait, 0.2f, 0.001f);

    ent->aiflags &= ~AI_CORPSE_RESERVED;
    ent->currentmove->think(ent);
    ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_FEQ(ent->wait, 0.3f, 0.001f);
}

TEST(wc3_unit, raisable_corpse_requires_authored_raise_bit_and_no_reservation) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    UnitData_t data = { .deathType = 2 };
    edict_t *ent;

    reset_test_entities();
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    ent->health.value = 0.0f;
    ent->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    T_ASSERT(!G_UnitIsRaisableCorpse(ent));
    data.deathType = 3;
    T_ASSERT(G_UnitIsRaisableCorpse(ent));
    ent->aiflags |= AI_CORPSE_RESERVED;
    T_ASSERT(!G_UnitIsRaisableCorpse(ent));
}

TEST(wc3_unit, nondecaying_building_uses_authored_death_type) {
    static UnitBalance_t building_balance = { .isBuilding = true, .maxHealth = 100.0f };
    static UnitData_t building_data = { .death = 1.25f, .deathType = 0 };
    edict_t *ent;

    reset_test_entities();
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'b', 'a', 'r');
    ent->data.UnitBalance = &building_balance;
    ent->data.UnitData = &building_data;
    unit_die(ent, NULL);
    unit_begin_decay(ent);

    T_ASSERT(ent->aiflags & AI_HOLD_FRAME);
    T_FEQ(ent->wait, FRAMETIME / 1000.0f, 0.001f);
    unit_decay_think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, dead_unit_rejects_orders_that_would_replace_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    vector2_t point = { 64.0f, 0.0f };

    ent->health.value = 0.0f;
    unit_die(ent, target);

    T_ASSERT(!unit_issueorder(ent, "move", &point));
    T_ASSERT(!unit_issueimmediateorder(ent, "stop"));
    T_ASSERT(!unit_issuetargetorder(ent, "smart", target));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, smart_on_passive_ally_starts_persistent_follow) {
    reset_test_entities();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(unit_issuetargetorder(follower, "smart", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, target_move_on_unit_starts_persistent_follow) {
    reset_test_entities();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = leader->s.player = 0;

    T_ASSERT(unit_issuetargetorder(follower, "move", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, follow_stop_range_uses_misc_data_not_acquisition_range) {
    float const old_follow = game.constants.followRange;
    float const old_structure = game.constants.structureFollowRange;
    edict_t *follower;
    edict_t *target;

    reset_test_entities();
    follower = make_unit(0, 0);
    target = make_unit(512, 0);
    follower->runtime.acquisition_range = 600.0f;
    follower->collision = 32.0f;
    target->collision = 0.0f;
    game.constants.followRange = 300.0f;
    game.constants.structureFollowRange = 100.0f;

    T_FEQ(G_AcquisitionRange(follower), 600.0f, 0.001f);
    T_FEQ(G_FollowStopRange(follower, target), 300.0f, 0.001f);

    target->s.flags |= EF_BUILDING;
    T_FEQ(G_FollowStopRange(follower, target), 100.0f, 0.001f);

    /* Collision remains a hard lower bound so large widgets never overlap
     * merely because Misc requests a smaller follow distance. */
    target->collision = 96.0f;
    T_FEQ(G_FollowStopRange(follower, target), 128.0f, 0.001f);

    game.constants.followRange = old_follow;
    game.constants.structureFollowRange = old_structure;
}

TEST(wc3_unit, smart_follow_building_stops_at_pathing_footprint_range) {
    enum { W = 8, H = 8 };
    float const old_structure = game.constants.structureFollowRange;
    size_t const pathtex_size = sizeof(pathTex_t) + W * H * sizeof(color32_t);
    pathTex_t *pathtex;
    edict_t *follower;
    edict_t *building;

    reset_test_entities();
    setup_test_world();
    follower = make_unit(200.0f, 0.0f);
    building = make_unit(0.0f, 0.0f);
    follower->svflags |= SVF_MONSTER;
    building->svflags |= SVF_MONSTER;
    follower->s.player = building->s.player = 0;
    building->s.flags |= EF_BUILDING;
    building->collision = 160.0f;
    game.constants.structureFollowRange = 100.0f;

    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;

    /* The building centre is 200 units away, outside StructureFollowRange,
     * while the authored 8x8 footprint edge is only about 72 units away.  A
     * centre-distance follow therefore walks back toward the blocked producer;
     * footprint-aware follow is already within the intended structure range. */
    T_ASSERT(M_DistanceToGoal(follower) == 0.0f);
    T_ASSERT(unit_issuetargetorder(follower, "smart", building));
    T_ASSERT(follower->movement.follow_target == building);
    T_ASSERT(follower->goalentity == building);
    T_ASSERT(M_DistanceToGoal(follower) > G_FollowStopRange(follower, building));
    T_FEQ(G_FollowStopRange(follower, building), 100.0f, 0.001f);
    T_ASSERT(CM_DistanceToPathingFootprint(building, &follower->s.origin2) <
             game.constants.structureFollowRange);

    follower->animation = &(animation_t){ .name = "stand", .interval = { 0, 300 } };
    follower->currentmove->think(follower);

    T_ASSERT(follower->movement.follow_target == building);
    T_ASSERT(G_AnimationHasPrimary(follower->animation, "stand"));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
    game.constants.structureFollowRange = old_structure;
}

TEST(wc3_unit, queued_smart_on_passive_ally_revalidates_to_follow) {
    reset_test_entities();
    setup_test_world();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    vector2_t first = { 96.0f, 0.0f };
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(G_IssueUnitPointOrder(follower, "move", &first, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(follower, "smart", leader, true, 0));
    T_EQ(G_UnitQueuedOrderCount(follower), 1);

    unit_stand(follower);

    T_EQ(G_UnitQueuedOrderCount(follower), 0);
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
}

TEST(wc3_unit, neutral_creep_natural_sleep_tracks_night_and_wakes_at_dawn) {
    reset_test_entities();
    setup_test_world();
    edict_t *creep = make_unit(0, 0);
    uint16_t *no_creep_sleep = &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP];
    UnitData_t data = *creep->data.UnitData;
    UnitBalance_t balance = *creep->data.UnitBalance;
    UnitUI_t ui = *creep->data.UnitUI;

    /* Spawn from authored eligibility, without an explicit ACsp ability-list entry. */
    data.canSleep = true;
    balance.maxHealth = 100;
    ui.modelFile = "Units\\Creeps\\Medivh\\Medivh.mdx";
    creep->data.UnitData = &data;
    creep->data.UnitBalance = &balance;
    creep->data.UnitUI = &ui;
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    SP_SpawnUnit(creep);
    unit_stand(creep);
    T_ASSERT(G_UnitCanSleep(creep));
    *no_creep_sleep = 0;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());
    ai_stand(creep);
    T_ASSERT(G_UnitIsSleeping(creep));
    T_STREQ(creep->animation_request, "sleep");
    ability_t const *ability = FindAbilityByClassname("ACsp");
    T_NOT_NULL(ability);
    T_ASSERT(ability && creep->currentmove->proc == ability->proc);

    G_SetTimeOfDay(game.constants.dawnTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());
    monster_think(creep);
    T_ASSERT(!G_UnitIsSleeping(creep));
    T_STREQ(creep->animation_request, "stand");
    G_BindEntityData(creep);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, no_creep_sleep_player_state_blocks_new_natural_sleep) {
    reset_test_entities();
    setup_test_world();
    edict_t *creep = make_unit(0, 0);
    uint16_t *no_creep_sleep = &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP];

    creep->svflags |= SVF_MONSTER;
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    creep->sleep.can_sleep = true;
    *no_creep_sleep = 1;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    ai_stand(creep);
    T_ASSERT(!G_UnitIsSleeping(creep));

    *no_creep_sleep = 0;
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, unit_add_sleep_policy_is_neutral_only_and_wakes_when_disabled) {
    reset_test_entities();
    setup_test_world();
    edict_t *neutral = make_unit(0, 0);
    edict_t *player_unit = make_unit(64, 0);

    neutral->svflags |= SVF_MONSTER;
    neutral->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    neutral->sleep.can_sleep = false;
    player_unit->svflags |= SVF_MONSTER;
    player_unit->s.player = 0;
    player_unit->sleep.can_sleep = false;

    G_UnitSetCanSleep(neutral, true);
    G_UnitSetCanSleep(player_unit, true);
    T_ASSERT(G_UnitCanSleep(neutral));
    T_ASSERT(!G_UnitCanSleep(player_unit));
    T_ASSERT(!player_unit->sleep.can_sleep);

    game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP] = 0;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    ai_stand(neutral);
    T_ASSERT(G_UnitIsSleeping(neutral));

    G_UnitSetCanSleep(neutral, false);
    T_ASSERT(!G_UnitCanSleep(neutral));
    T_ASSERT(!G_UnitIsSleeping(neutral));
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, smart_on_neutral_aggressive_attacks_not_follows) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_unit, smart_on_neutral_aggressive_follows_after_passive_alliance) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(&game.clients[0].ps,
                        &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps,
                        ALLIANCE_PASSIVE, true);

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->movement.follow_target == target);
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, smart_on_neutral_passive_uses_persistent_follow) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_PASSIVE;
    G_InitPlayerAlliances(level.mapinfo);

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->movement.follow_target == target);
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, smart_on_shared_vision_enemy_still_attacks) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.alliances[0][1] |= 1 << ALLIANCE_SHARED_VISION;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_unit, die_publishes_death_event) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    memset(level.events.queue, 0, sizeof(level.events.queue));
    memset(level.events.handlers, 0, sizeof(level.events.handlers));

    unit_die(ent, NULL);
    bool found = false;
    for (int i = 0; i < MAX_EVENT_QUEUE; i++) {
        if (level.events.queue[i].type == EVENT_UNIT_DEATH) {
            found = true;
            break;
        }
    }
    T_ASSERT(found);
}

TEST(wc3_unit, hero_dissipation_marks_same_hero_revivable_and_hidden) {
    reset_test_entities();
    edict_t *hero = make_inventory_unit(0, 0);
    hero->health.value = hero->health.max_value = 500.0f;

    unit_die(hero, NULL);
    T_ASSERT(hero->svflags & SVF_DEADMONSTER);
    T_ASSERT(!hero->revival.awaiting);

    unit_begin_decay(hero);
    /* Drive exactly one elapsed simulation step without depending on the
     * archive's configured DissipateTime in this unit test. */
    hero->wait = (float)FRAMETIME / 1000.0f;
    unit_decay_think(hero);

    T_ASSERT(hero->inuse);
    T_ASSERT(hero->revival.awaiting);
    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
}

TEST(wc3_unit, scripted_revive_clears_altar_revival_state_on_same_hero) {
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *altar = make_unit(0, 0);
    edict_t *hero = make_inventory_unit(0, 0);
    altar->s.player = hero->s.player = client->ps.number;
    altar->build = hero;
    hero->health.max_value = 500.0f;
    hero->health.value = 0.0f;
    hero->mana.max_value = 300.0f;
    hero->svflags |= SVF_DEADMONSTER;
    hero->s.flags |= EF_NOT_SELECTABLE;
    hero->s.renderfx |= RF_HIDDEN;
    hero->revival.awaiting = true;
    hero->revival.reviving = true;
    hero->revival.producer = altar;
    hero->revival.player = client->ps.number;
    hero->revival.gold = 100;
    hero->revival.lumber = 50;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    T_ASSERT(G_ReviveHero(hero, 64.0f, 96.0f));

    T_NULL(altar->build);
    T_ASSERT(hero->inuse);
    T_ASSERT(!(hero->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(hero->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));
    T_ASSERT(!hero->revival.awaiting);
    T_ASSERT(!hero->revival.reviving);
    T_NULL(hero->revival.producer);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50);
    T_EQ((int)hero->s.origin2.x, 64);
    T_EQ((int)hero->s.origin2.y, 96);
    T_FEQ(hero->s.origin.x, 64.0f, 0.001f);
    T_FEQ(hero->s.origin.y, 96.0f, 0.001f);
    T_ASSERT(hero->health.value > 0.0f);
    G_SelectEntity(client, hero);
    T_ASSERT(G_IsEntitySelected(client, hero));
}

TEST(wc3_unit, removing_producer_cancels_mixed_revival_and_training_queue) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *altar = make_unit(0, 0);
    edict_t *hero = make_inventory_unit(0, 0);
    edict_t *trainee = make_unit(0, 0);
    int32_t gold = MAX(0, trainee->data.UnitBalance->goldCost);
    int32_t lumber = MAX(0, trainee->data.UnitBalance->lumberCost);

    altar->s.player = hero->s.player = trainee->s.player = client->ps.number;
    altar->data.UnitProfile = &revive_profile;
    altar->build = hero;
    hero->revival.awaiting = true;
    hero->revival.reviving = true;
    hero->revival.producer = altar;
    hero->revival.queue_next = trainee;
    hero->revival.player = client->ps.number;
    hero->revival.gold = 100;
    hero->revival.lumber = 50;
    trainee->training = true;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    G_FreeEdict(altar);

    T_ASSERT(!altar->inuse);
    T_ASSERT(hero->inuse);
    T_ASSERT(!hero->revival.reviving);
    T_NULL(hero->revival.producer);
    T_ASSERT(!trainee->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100 + gold);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50 + lumber);
}

TEST(wc3_unit, worker_death_does_not_walk_construction_target_as_production_queue) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *building = make_unit(0, 0);

    building->construction.active = true;
    building->build = building;
    worker->build = building;

    unit_die(worker, NULL);

    T_ASSERT(worker->svflags & SVF_DEADMONSTER);
    T_ASSERT(building->construction.active);
    T_ASSERT(building->build == building);
}

TEST(wc3_unit, ownership_change_does_not_walk_constructing_revive_altar) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *old_client = &game.clients[0];
    gameClient_t *new_client = &game.clients[1];
    edict_t *altar = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->s.player = old_client->ps.number;
    altar->construction.active = true;
    altar->build = altar;

    G_SetUnitPlayer(altar, new_client->ps.number);

    T_EQ(altar->s.player, new_client->ps.number);
    T_ASSERT(altar->construction.active);
    T_ASSERT(altar->build == altar);
}

TEST(wc3_unit, ownership_change_does_not_walk_legacy_constructing_revive_altar) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *new_client = &game.clients[1];
    edict_t *altar = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->s.player = game.clients[0].ps.number;
    altar->build = altar;

    G_SetUnitPlayer(altar, new_client->ps.number);

    T_EQ(altar->s.player, new_client->ps.number);
    T_ASSERT(!altar->construction.active);
    T_ASSERT(altar->build == altar);
}

TEST(wc3_unit, hero_revive_cleanup_stops_on_cyclic_production_queue) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    edict_t *altar = make_unit(0, 0);
    edict_t *first = make_unit(0, 0);
    edict_t *second = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->build = first;
    first->training = second->training = true;
    first->build = second;
    second->build = first;

    G_CancelHeroRevives(altar);

    T_ASSERT(altar->build == first);
    T_ASSERT(first->build == second);
    T_ASSERT(second->build == first);
}

/* -----------------------------------------------------------------------
 * Order tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, issueorder_move_creates_waypoint) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};
    bool result = unit_issueorder(ent, "move", &dest);
    T_ASSERT(result);
    T_NOT_NULL(ent->goalentity);
}

TEST(wc3_unit, issueorder_move_sets_walk_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, shift_move_starts_immediately_when_idle_then_queues_fifo) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    vector2_t a = { 96.0f, 0.0f };
    vector2_t b = { 192.0f, 0.0f };
    vector2_t c = { 288.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &c, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 1);
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, c.x, 0.01f);
}

TEST(wc3_unit, nonqueued_move_replaces_pending_shift_orders) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    vector2_t a = { 96.0f, 0.0f };
    vector2_t b = { 192.0f, 0.0f };
    vector2_t replacement = { 320.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueorder(ent, "move", &replacement));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, replacement.x, 0.01f);
}

TEST(wc3_unit, militia_target_order_reaches_militia_behavior) {
    static UnitAbilities_t const worker_abilities = { .abilList = "Amil" };
    static UnitAbilities_t const hall_abilities = { .abilList = "Amic" };
    edict_t *worker;
    edict_t *hall;

    reset_test_entities();
    setup_test_world();
    worker = make_unit(0, 0);
    hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 256, 0);
    worker->data.UnitAbilities = &worker_abilities;
    hall->data.UnitAbilities = &hall_abilities;
    worker->s.player = hall->s.player = 1;
    hall->pathtex = NULL;

    T_ASSERT(G_IssueUnitTargetOrder(worker, "militia", hall, false, worker->s.player));
    T_ASSERT(worker->militia.partner == hall);
    T_NOT_NULL(worker->currentmove);
    T_ASSERT(worker->currentmove->proc == CAbilityMilitia);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, stale_queued_entity_target_is_skipped_for_next_order) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    vector2_t a = { 64.0f, 0.0f };
    vector2_t b = { 256.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(ent, "attack", target, true, 0));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    /* Simulate recycling the target slot before the queued Attack begins. */
    target->spawn_time++;
    unit_stand(ent);

    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);
}

TEST(wc3_unit, stop_clears_pending_shift_orders) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t a = { 96.0f, 0.0f };
    vector2_t b = { 192.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueimmediateorder(ent, "stop"));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, point_attack_order_uses_attack_move_behavior) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = { 128.0f, 0.0f };

    T_ASSERT(unit_issueorder(ent, "attack", &dest));
    T_NOT_NULL(ent->movement.attackmove_waypoint);
    T_ASSERT(ent->goalentity == ent->movement.attackmove_waypoint);
}

TEST(wc3_unit, issueorder_move_preserves_combat_state) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vector2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;
    vector2_t dest = {100.0f, 0.0f};

    unit_entercombat(ent, target);
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, issueorder_unknown_returns_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};
    bool result = unit_issueorder(ent, "patrol", &dest);
    T_ASSERT(!result);
}

TEST(wc3_unit, issueorder_null_inputs_return_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};

    T_ASSERT(!unit_issueorder(NULL, "move", &dest));
    T_ASSERT(!unit_issueorder(ent, NULL, &dest));
    T_ASSERT(!unit_issueorder(ent, "move", NULL));
}

TEST(wc3_unit, issueimmediateorder_stop) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_STREQ(ent->currentmove->animation, "walk");

    bool result = unit_issueimmediateorder(ent, "stop");
    T_ASSERT(result);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_holdposition_uses_hold_state) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vector2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_issueimmediateorder(ent, "holdposition"));
    T_ASSERT(ent->movement.holding_position);
    T_STREQ(ent->currentmove->animation, "stand");
}

static void install_raven_form_test_data(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Amrf\"\n"
        "C;Y2;X2;K\"Amrf\"\n"
        "C;Y2;X3;K\"hpea\"\n"
        "C;Y2;X4;K\"hfoo\"\n"
        "C;Y3;X1;K\"Arav\"\n"
        "C;Y3;X2;K\"Arav\"\n"
        "C;Y3;X3;K\"edot\"\n"
        "C;Y3;X4;K\"edtm\"\n"
        "C;Y4;X1;K\"Astn\"\n"
        "C;Y4;X2;K\"Astn\"\n"
        "C;Y4;X3;K\"hpea\"\n"
        "C;Y4;X4;K\"hfoo\"\n"
        "E\n";
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\n"
        "C;Y1;X4;K\"scale\"\n"
        "C;Y1;X5;K\"red\"\n"
        "C;Y1;X6;K\"green\"\n"
        "C;Y1;X7;K\"blue\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\n"
        "C;Y2;X4;K1\n"
        "C;Y2;X5;K224\n"
        "C;Y2;X6;K232\n"
        "C;Y2;X7;K255\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\n"
        "C;Y3;X4;K1\n"
        "E\n";
    static cstring_t const profile_slk =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"unitID\"\n"
        "C;Y1;X2;K\"animProps\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"\"\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"alternateex\"\n"
        "E\n";

    *ability_rows = parse_slk_string(ability_slk);
    *ui_rows = parse_slk_string(ui_slk);
    *profile_rows = parse_slk_string(profile_slk);
    T_NOT_NULL(*ability_rows); T_NOT_NULL(*ui_rows); T_NOT_NULL(*profile_rows);
    *old_ability = G_SetSLKRows("AbilityData", *ability_rows);
    *old_ui = G_SetSLKRows("UnitUI", *ui_rows);
    *old_profile = G_SetProfileRows(*profile_rows);
    T_NOT_NULL(*old_ability); T_NOT_NULL(*old_ui); T_NOT_NULL(*old_profile);
}

static void restore_raven_form_test_data(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

TEST(wc3_unit, ravenform_immediate_orders_transform_between_ability_data_types) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    G_InitializeUnitVertexColor(ent);
    T_ASSERT(ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 224); T_EQ(ent->vertex_color.g, 232);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);
    T_ASSERT(G_ActorAddSkill(ent, MAKEFOURCC('A','m','r','f')));
    G_ApplyTemporaryMaxHealthBonus(ent, 123.0f);
    G_ApplyTemporaryMaxManaBonus(ent, 77.0f);

    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth + 123.0f, 0.001f);
    T_FEQ(ent->mana.max_value, ent->data.UnitBalance->maxMana + 77.0f, 0.001f);
    T_STREQ(ent->animation_props, "alternateex");
    T_ASSERT(!ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 255); T_EQ(ent->vertex_color.g, 255);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth + 123.0f, 0.001f);
    T_FEQ(ent->mana.max_value, ent->data.UnitBalance->maxMana + 77.0f, 0.001f);
    T_STREQ(ent->animation_props, "");
    T_ASSERT(ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 224); T_EQ(ent->vertex_color.g, 232);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);

    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, stoneform_order_requires_authored_ability_ownership) {
    edict_t *ent;
    reset_test_entities(); setup_test_world();
    ent = alloc_test_unit(MAKEFOURCC('u','g','a','r'), 64.0f, 64.0f);
    T_ASSERT(!G_UnitAbilityLevel(ent, MAKEFOURCC('A','s','t','n')));
    T_ASSERT(!unit_issueimmediateorder(ent, "stoneform"));
    T_EQ(ent->class_id, MAKEFOURCC('u','g','a','r'));
}

TEST(wc3_unit, stoneform_uses_authored_transform_endpoints_in_both_directions) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    ent->svflags |= SVF_MONSTER;
    ent->abilities.added[0] = MAKEFOURCC('A','s','t','n'); ent->abilities.added_count = 1;
    T_ASSERT(G_UnitAbilityLevel(ent, MAKEFOURCC('A','s','t','n')));
    T_NOT_NULL(S_SpellAbilityForCode(MAKEFOURCC('A','s','t','n')));
    T_EQ(G_AbilityData(MAKEFOURCC('A','s','t','n'))->level[0].data[0].id, MAKEFOURCC('h','p','e','a'));
    T_EQ(G_AbilityData(MAKEFOURCC('A','s','t','n'))->level[0].unitID, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(unit_issueimmediateorder(ent, "stoneform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(unit_issueimmediateorder(ent, "unstoneform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, unravenform_accepts_preplaced_alternate_form) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    G_ResetUnitAnimationProperties(ent);
    T_STREQ(ent->animation_props, "alternateex");

    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_STREQ(ent->animation_props, "");

    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, unravenform_snaps_new_animation_frame_while_unit_is_paused) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    SP_SpawnUnit(ent);
    unit_stand(ent);
    T_NOT_NULL(ent->animation);
    ent->paused = true;
    ent->s.frame = 0x7fffffffu;

    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_STREQ(ent->animation_props, "");
    T_NOT_NULL(ent->animation);
    T_ASSERT(G_AnimationHasPrimary(ent->animation, "morph"));
    T_EQ(ent->s.frame, ent->animation->interval[0]);

    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Both order entry points must resolve the registered handler, including preplaced forms. */
TEST(wc3_unit, raven_ability_dispatch_and_toggle) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    ability_t const *ability = FindAbilityByOrder("ravenform");
    abilityitem_t item;
    abilityCall_t call;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    item = MAKE(abilityitem_t, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item);
    T_NOT_NULL(ability);
    T_EQ(ability->proc, FindAbilityByOrder("unravenform")->proc);
    T_EQ(ability->proc, FindAbilityByClassname("Amrf")->proc);
    T_EQ(ability->proc, FindAbilityByClassname("Arav")->proc);
    T_ASSERT(ability->flags & AB_COMMAND);
    T_NULL(FindAbilityByOrder("unrecognized"));
    T_NULL(FindAbilityByOrder(NULL));
    T_ASSERT(!S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    T_ASSERT(S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_ASSERT(ent->currentmove->proc == ability->proc);
    T_NULL(ent->currentmove->think); /* Morph cannot acquire enemies and replace itself with Attack. */
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform")); /* Already in this form. */
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_ASSERT(!S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_EQ(ent->raven.rise_state, RAVEN_RISE_NONE);
    ent->class_id = MAKEFOURCC('o','g','r','u');
    T_ASSERT(!unit_issueimmediateorder(ent, "ravenform"));
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Completing the ability's morph starts takeoff; an unrelated Move order must not stop its timer. */
TEST(wc3_unit, raven_morph_completion_and_takeoff_survive_move_order) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    vector2_t point = {128, 64};
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    ent->raven.fly_height = 100;
    ent->raven.rise_duration = 2;
    T_ASSERT(G_AnimationHasPrimary(ent->animation, "morph"));
    ent->s.frame = ent->animation->interval[1] - 1;
    level.time = 1000;
    monster_think(ent);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_ACTIVE);
    T_FEQ(ent->unitinfo.FlyHeight, 0, 0.001f);
    T_ASSERT(unit_issueorder(ent, "move", &point));
    ent->paused = true;
    level.time = 2000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50, 0.001f);
    T_FEQ(ent->s.origin2.x, 64, 0.001f); /* Pause stops walking, but preserves the PR's independent ascent. */
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    level.time = 3000;
    monster_think(ent);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_NONE);
    T_FEQ(ent->unitinfo.FlyHeight, 0, 0.001f);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Prologue01 issues Move after 0.5 seconds, before Medivh's forward Morph clip ends. */
TEST(wc3_unit, raven_takeoff_survives_interrupted_morph) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    vector2_t point = {128, 64};
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    ent->raven.fly_height = 100;
    ent->raven.rise_duration = 2;
    T_EQ(ent->raven.rise_state, RAVEN_RISE_AFTER_MORPH);
    T_ASSERT(unit_issueorder(ent, "move", &point));
    umove_t const *walk = ent->currentmove;
    level.time = 1000;
    S_RunAbilityUpdates(ent);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_ACTIVE);
    T_ASSERT(ent->currentmove == walk);
    level.time = 2000;
    S_RunAbilityUpdates(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50, 0.001f);
    T_ASSERT(ent->currentmove == walk);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, ability_updates_leave_ordinary_units_unchanged) {
    reset_test_entities(); setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    ent->unitinfo.FlyHeight = 37;
    ent->currentmove = NULL;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 37, 0.001f);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_NONE);
}

TEST(wc3_unit, ravenForm_takeoff_interpolates_from_ground_to_authored_height) {
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    ent->raven.fly_height = 100.0f;
    ent->raven.rise_start = 1000;
    ent->raven.rise_duration = 2.0f;
    ent->raven.rise_state = RAVEN_RISE_ACTIVE;
    level.time = 2000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50.0f, 0.001f);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_ACTIVE);
    level.time = 3000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 100.0f, 0.001f);
    T_EQ(ent->raven.rise_state, RAVEN_RISE_NONE);
}

TEST(wc3_unit, issueimmediateorder_autoharvestlumber_uses_nearest_live_tree) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *far_tree = unit_make_harvest_tree(300.0f, 0.0f);
    edict_t *dead_tree = unit_make_harvest_tree(25.0f, 0.0f);
    edict_t *near_tree = unit_make_harvest_tree(100.0f, 0.0f);
    (void)far_tree;
    dead_tree->health.value = 0.0f;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(worker->goalentity == near_tree);
    T_ASSERT(worker->secondarygoal == near_tree);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvestgold_uses_nearest_live_mine) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *far_mine = unit_make_harvest_goldmine(300.0f, 0.0f);
    edict_t *empty_mine = unit_make_harvest_goldmine(25.0f, 0.0f);
    edict_t *near_mine = unit_make_harvest_goldmine(100.0f, 0.0f);
    (void)far_mine;
    empty_mine->resources = 0;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestgold"));
    T_ASSERT(worker->goalentity == near_mine);
    T_ASSERT(worker->secondarygoal == near_mine);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvest_requires_resource_target) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestgold"));
    T_STREQ(worker->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_unknown_returns_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    bool result = unit_issueimmediateorder(ent, "patrol");
    T_ASSERT(!result);
}

TEST(wc3_unit, issueimmediateorder_null_inputs_return_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(NULL, "stop"));
    T_ASSERT(!unit_issueimmediateorder(ent, NULL));
}

/* -----------------------------------------------------------------------
 * Inventory tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, additemtoslot_fills_empty_slot) {
    reset_test_entities();
    edict_t *ent  = make_inventory_unit(0, 0);
    edict_t *item = make_world_item(MAKEFOURCC('r','a','t','f'));
    bool ok = unit_additemtoslot(ent, item, 0);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[0] == item);
}

TEST(wc3_unit, additemtoslot_rejects_occupied_slot) {
    reset_test_entities();
    edict_t *ent   = make_inventory_unit(0, 0);
    edict_t *item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    edict_t *item2 = make_world_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(ent, item1, 0);
    bool ok = unit_additemtoslot(ent, item2, 0);
    T_ASSERT(!ok);
}

TEST(wc3_unit, additem_fills_first_free_slot) {
    reset_test_entities();
    edict_t *ent   = make_inventory_unit(0, 0);
    edict_t *item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    edict_t *item2 = make_world_item(MAKEFOURCC('r','d','e','2'));
    unit_additemtoslot(ent, item1, 0);
    bool ok = unit_additem(ent, item2);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[1] == item2);
}

TEST(wc3_unit, additem_fails_when_inventory_full) {
    reset_test_entities();
    edict_t *ent = make_inventory_unit(0, 0);
    for (int i = 0; i < MAX_INVENTORY; i++) {
        edict_t *item = make_world_item(MAKEFOURCC('r','a','t','f'));
        unit_additemtoslot(ent, item, i);
    }
    edict_t *extra = make_world_item(MAKEFOURCC('r','d','e','2'));
    bool ok = unit_additem(ent, extra);
    T_ASSERT(!ok);
}


TEST(wc3_unit, different_units_have_independent_response_gates) {
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    a->s.player = b->s.player = 0;
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(a, 11));
    T_ASSERT(G_QueueUnitResponseSound(b, 12));
    T_ASSERT(!G_QueueUnitResponseSound(a, 13));
    T_EQ(b->sound.pending, 12);
}

#endif /* BZ_TESTS */
