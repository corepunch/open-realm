#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "construction_trace.h"

TEST(wc3_spawn, map_script_object_filter_keeps_units_and_items_only) {
    T_ASSERT(G_TestMapObjectCreatedByMapScript(MAKEFOURCC('o', 'p', 'e', 'o')));
    T_ASSERT(G_TestMapObjectCreatedByMapScript(MAKEFOURCC('s', 'p', 'r', 'o')));
    T_ASSERT(!G_TestMapObjectCreatedByMapScript(MAKEFOURCC('s', 'l', 'o', 'c')));
}

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);

static uint64_t construction_hashes[128];
static uint32_t construction_count, construction_case, construction_callbacks;
static bool construction_compare, construction_nested;
static edictData_s construction_replacement;
static uint32_t construction_outer_slot;

static uint64_t construction_word(uint64_t hash, uint32_t word) {
    FOR_LOOP(i, 4) { hash ^= (word >> (i * 8)) & 255; hash *= UINT64_C(1099511628211); }
    return hash;
}

/* Canonical logical values, without padding, process pointers or pool addresses.
 * The optional journal compares the instrumented original with later revisions. */
static void construction_record(unitConstructionStage_t stage, edict_t const *ent, edictData_s const *captured) {
    uint64_t hash = UINT64_C(14695981039346656037);
#define WORD(value) hash = construction_word(hash, (uint32_t)(value))
#define REAL(value) WORD(wc3_float_bits(value))
    WORD(stage); WORD(ent->s.number); WORD(ent->class_id); WORD(ent->s.player);
    WORD(ent->own_seq); WORD(ent->own_seq >> 32); WORD(ent->inuse);
    WORD(ent->s.flags); WORD(ent->svflags); WORD(ent->aiflags); WORD(ent->movetype);
    REAL(ent->s.origin.x); REAL(ent->s.origin.y); REAL(ent->s.origin.z);
    REAL(ent->s.origin2.x); REAL(ent->s.origin2.y); REAL(ent->s.angle);
    REAL(ent->s.scale); REAL(ent->s.radius); REAL(ent->s.collision); REAL(ent->collision);
    REAL(ent->bounds.min.x); REAL(ent->bounds.min.y); REAL(ent->bounds.max.x); REAL(ent->bounds.max.y);
    REAL(ent->health.value); REAL(ent->health.max_value); REAL(ent->mana.value); REAL(ent->mana.max_value);
    REAL(ent->unitinfo.MoveSpeed); REAL(ent->unitinfo.FlyHeight); REAL(ent->unitinfo.PropWindow);
    REAL(ent->unitinfo.TurnSpeed); REAL(ent->unitinfo.AcquireRange); WORD(ent->unitinfo.move_flags);
    WORD(ent->runtime.flags); REAL(ent->runtime.sight_radius.day); REAL(ent->runtime.sight_radius.night);
    REAL(ent->runtime.acquisition_range); WORD(ent->defense_type); REAL(ent->armor_value);
    WORD(ent->hero.str); WORD(ent->hero.agi); WORD(ent->hero.intel);
    unitAttack_t const *attacks[] = { S_AttackProfileRead(ent, 0), S_AttackProfileRead(ent, 1) };
    FOR_LOOP(i, 2) {
        unitAttack_t const *attack = attacks[i];
        WORD(attack->type); WORD(attack->weapon); WORD(attack->damageBase);
        WORD(attack->numberOfDice); WORD(attack->sidesPerDie); WORD(attack->targetsAllowed);
        REAL(attack->permanentDamageBonus); REAL(attack->temporaryDamageBonus);
        REAL(attack->damagePoint); REAL(attack->backswingPoint); REAL(attack->cooldown);
        REAL(attack->range); REAL(attack->rangeBuffer); REAL(attack->areaFull);
        REAL(attack->areaMedium); REAL(attack->areaSmall); REAL(attack->factorMedium);
        REAL(attack->factorSmall); WORD(attack->maxTargets); REAL(attack->damageLoss);
        REAL(attack->origin.x); REAL(attack->origin.y); REAL(attack->origin.z);
        REAL(attack->projectile.arc); REAL(attack->projectile.speed);
    }
    WORD(ent->movement.fine_class); WORD(ent->movement.pose_valid); WORD(ent->movement.clock_valid);
    REAL(ent->movement.fine_pose.x); REAL(ent->movement.fine_pose.y);
    REAL(ent->movement.velocity.x); REAL(ent->movement.velocity.y);
    WORD(ent->movement.repulse.active); WORD(ent->invulnerable); REAL(ent->wait);
    WORD(ent->abilities.added_count); WORD(ent->abilities.removed_count);
    WORD(ent->food != NULL); WORD(ent->cargo != NULL); WORD(ent->stock != NULL);
    WORD(ent->currentmove != NULL); WORD(ent->stand != NULL); WORD(ent->think != NULL);
    WORD(globals.num_edicts); WORD(level.next_unit_seq); WORD(level.pathing_random.sum);
    WORD(construction_callbacks);
    if (captured) {
        WORD(captured->UnitBalance->id); REAL(captured->UnitBalance->maxHealth);
        REAL(captured->UnitBalance->speed); REAL(captured->UnitUI->modelScale);
        REAL(captured->UnitData->propWin); WORD(captured->UnitWeapons->attack1.damageBase);
    }
#undef REAL
#undef WORD
    T_ASSERT(construction_count < sizeof(construction_hashes) / sizeof(*construction_hashes));
    if (construction_count >= sizeof(construction_hashes) / sizeof(*construction_hashes)) return;
    T_EQ(hash, construction_original[construction_case][construction_count]);
    if (construction_compare) T_EQ(hash, construction_hashes[construction_count]);
    else construction_hashes[construction_count] = hash;
    construction_count++;
    cstring_t journal = getenv("WC3_CONSTRUCTION_TRACE");
    if (journal && !construction_compare) {
        FILE *out = fopen(journal, "a");
        T_NOT_NULL(out);
        if (out) { fprintf(out, "%u %u %u %016llx\n", construction_case, ent->s.number, stage,
                          (unsigned long long)hash); fclose(out); }
    }
    if (ent->s.number != construction_outer_slot) return;
    if (stage == UNIT_CONSTRUCT_ABILITIES_BEGIN) {
        T_FEQ(ent->health.value, 0, 0); T_EQ(S_AttackProfileRead(ent, 0)->damageBase, 0);
        T_NULL(ent->stand); T_ASSERT(ent->own_seq != 0);
    }
    if (construction_case && stage == UNIT_CONSTRUCT_STATS) {
        T_FEQ(ent->health.value, captured->UnitBalance->maxHealth, 0);
        T_FEQ(ent->unitinfo.MoveSpeed, captured->UnitBalance->speed, 0);
        T_ASSERT(ent->data.UnitBalance != captured->UnitBalance);
        T_FEQ(ent->data.UnitBalance->maxHealth, 902, 0);
        T_EQ(ent->s.player, 1);
    }
    if (construction_case && stage == UNIT_CONSTRUCT_COMBAT) {
        T_EQ(S_AttackProfileRead(ent, 0)->damageBase, captured->UnitWeapons->attack1.damageBase);
        T_NE(S_AttackProfileRead(ent, 0)->damageBase, ent->data.UnitWeapons->attack1.damageBase);
    }
}

static intptr_t construction_initializer(edict_t *ent, abilityMsg_t msg, abilityCall_t const *call) {
    if (msg != A_UNIT_INIT) return CAbilityMove(ent, msg, call);
    construction_callbacks++;
    T_FEQ(ent->health.value, 0, 0); T_EQ(S_AttackProfileRead(ent, 0)->damageBase, 0);
    if (construction_case && !construction_nested) {
        ent->data = construction_replacement;
        ent->s.player = 1;
        ent->collision = 13;
        if (construction_case == 2) {
            construction_nested = true;
            vec2_t point = { -97.125f, 160.375f };
            edict_t *nested = unit_create(2, MAKEFOURCC('h','f','o','o'), &point, 19);
            T_NOT_NULL(nested);
            T_EQ(nested->s.number, ent->s.number + 1);
            T_ASSERT(nested->own_seq > ent->own_seq);
            construction_nested = false;
        }
    }
    return CAbilityMove(ent, msg, call);
}

TEST(wc3_spawn, construction_stages_preserve_captured_rows_nested_creation_and_rng) {
    reset_entities(); setup_test_world(); InitAbilities();
    edict_t *sample = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    construction_replacement = sample->data;
    UnitBalance_t balance = *sample->data.UnitBalance;
    UnitUI_t ui = *sample->data.UnitUI;
    UnitData_t data = *sample->data.UnitData;
    UnitWeapons_t weapons = *sample->data.UnitWeapons;
    UnitAbilities_t abilities = { .abilList = "" };
    balance.maxHealth = 902; balance.speed = 97; ui.modelScale = 3;
    data.propWin = 7; weapons.attack1.damageBase = 713;
    construction_replacement.UnitBalance = &balance; construction_replacement.UnitUI = &ui;
    construction_replacement.UnitData = &data; construction_replacement.UnitWeapons = &weapons;
    construction_replacement.UnitAbilities = &abilities;
    ability_t *move = (ability_t *)FindAbilityByClassname("Amov");
    abilityProc_t previous = move->proc;
    S_ReplaceAbilityProcedure(move, construction_initializer);
    G_TestSetConstructionTrace(construction_record);
    FOR_LOOP(scenario, 3) {
        uint32_t expected_count = 0; int expected_random = 0;
        FOR_LOOP(pass, 2) {
            reset_entities(); setup_test_world();
            level.next_unit_seq = 0; level.time = 1234; level.pathing_random.sum = 2137;
            srand(917);
            construction_count = 0; construction_callbacks = 0; construction_nested = false;
            construction_case = scenario; construction_compare = pass != 0;
            construction_outer_slot = globals.num_edicts;
            vec2_t point = { 63.125f, -96.375f };
            edict_t *unit = unit_create(0, MAKEFOURCC('h','f','o','o'), &point, 37);
            T_NOT_NULL(unit);
            T_EQ(construction_callbacks, scenario == 2 ? 2 : 1);
            T_EQ(construction_count, scenario == 2 ? 52 : 26);
            T_EQ(level.pathing_random.sum, 2137);
            int random_after = rand();
            if (!pass) { expected_count = construction_count; expected_random = random_after; }
            else { T_EQ(construction_count, expected_count); T_EQ(random_after, expected_random); }
        }
    }
    G_TestSetConstructionTrace(NULL);
    S_ReplaceAbilityProcedure(move, previous); InitAbilities();
    reset_entities(); setup_test_world();
}

TEST(wc3_spawn, runtime_definitions_retain_versions_without_type_eviction) {
    G_ClearUnitRuntimeTypes();
    enum { TYPES = 1536 };
    unitRuntimeType_t const *types[TYPES];
    G_TestUnitTypeResolutions(true);
    FOR_LOOP(i, TYPES) types[i] = G_UnitRuntimeType(0x71000000u + i);
    T_EQ(G_TestUnitTypeResolutions(false), TYPES);
    FOR_LOOP(pass, 3) FOR_LOOP(i, TYPES)
        T_EQ(G_UnitRuntimeType(0x71000000u + i), types[i]);
    T_EQ(G_TestUnitTypeResolutions(false), TYPES);
    uint32_t code = MAKEFOURCC('h','f','o','o');
    unitRuntimeType_t const *old_type = G_UnitRuntimeType(code);
    float old_health = old_type->data.UnitBalance->maxHealth;
    UnitBalance_t row = *old_type->data.UnitBalance;
    row.maxHealth = 917;
    slkTestData_t table = { .rows = &row, .count = 1 };
    slkTestData_t *previous = G_SetSLKRows("UnitBalance", &table);
    unitRuntimeType_t const *new_type = G_UnitRuntimeType(code);
    T_NE(new_type, old_type);
    T_NE(new_type->version, old_type->version);
    T_EQ(new_type->rawcode, old_type->rawcode);
    T_FEQ(old_type->data.UnitBalance->maxHealth, old_health, 0);
    T_EQ(new_type->data.UnitBalance, &row);
    G_SetSLKRows("UnitBalance", previous); free(previous);
    G_ClearUnitRuntimeTypes();
}

TEST(wc3_spawn, attack_defaults_share_storage_and_mutations_isolate_instances) {
    reset_entities(); setup_test_world();
    uint32_t code = MAKEFOURCC('h','f','o','o');
    UnitWeapons_t row = { .id = code, .attacksEnabled = 1,
        .attack1 = { .attackType = "normal", .weaponType = "normal", .damageBase = 37,
                     .damageDice = 2, .damageSides = 5, .range = 128, .cooldown = 1.7f } };
    slkTestData_t table = { .rows = &row, .count = 1 };
    slkTestData_t *previous = G_SetSLKRows("UnitWeapons", &table);
    vec2_t point = {64, 64};
    edict_t *first = unit_create(0, code, &point, 0);
    point.x = 192;
    edict_t *second = unit_create(0, code, &point, 0);
    T_NOT_NULL(first); T_NOT_NULL(second);
    unitAttack_t const *defaults = S_AttackProfileRead(first, 0);
    T_EQ(defaults, S_AttackProfileRead(second, 0));
    T_EQ(defaults->damageBase, 37);
    T_NULL(first->attack_overrides[0]); T_NULL(second->attack_overrides[0]);
    FOR_LOOP(i, 4096) T_EQ(S_AttackProfileRead(first, 0), defaults);
    T_NULL(first->attack_overrides[0]);
    S_AttackProfileWrite(first, 0)->range = 301;
    S_AttackProfileWrite(first, 0)->temporaryDamageBonus = 17;
    T_FEQ(S_AttackProfileRead(first, 0)->range, 301, 0);
    T_FEQ(S_AttackProfileRead(second, 0)->range, 128, 0);
    T_FEQ(defaults->temporaryDamageBonus, 0, 0);
    /* An in-place metadata edit creates new defaults; existing instances keep
     * the actual values they received, including the unmodified second unit. */
    row.attack1.damageBase = 91;
    table = (slkTestData_t){ .rows = &row, .count = 1 };
    slkTestData_t *discard = G_SetSLKRows("UnitWeapons", &table);
    point.x = 320;
    edict_t *third = unit_create(0, code, &point, 0);
    T_EQ(S_AttackProfileRead(third, 0)->damageBase, 91);
    T_EQ(S_AttackProfileRead(first, 0)->damageBase, 37);
    T_EQ(S_AttackProfileRead(second, 0)->damageBase, 37);
    T_NULL(third->attack_overrides[0]);
    vec2_t destination = {448, 192};
    T_ASSERT(unit_issueorder(second, "move", &destination));
    T_NULL(second->attack_overrides[0]); T_NULL(second->attack_overrides[1]);
    uint32_t first_id = first->s.number, second_id = second->s.number, third_id = third->s.number;
    cstring_t file = Test_TempPath("openrealm-versioned-attack-defaults.bin");
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    first = g_edicts + first_id; second = g_edicts + second_id; third = g_edicts + third_id;
    T_EQ(S_AttackProfileRead(first, 0)->damageBase, 37);
    T_EQ(S_AttackProfileRead(second, 0)->damageBase, 37);
    T_EQ(S_AttackProfileRead(third, 0)->damageBase, 91);
    T_FEQ(S_AttackProfileRead(first, 0)->range, 301, 0);
    T_NOT_NULL(first->attack_overrides[0]); T_NULL(second->attack_overrides[0]);
    T_NULL(third->attack_overrides[0]);
    remove(file);
    slkTestData_t *restored = G_SetSLKRows("UnitWeapons", previous);
    T_NOT_NULL(restored);
    free(restored); free(previous); free(discard);
    reset_entities(); setup_test_world();
}
TEST(wc3_spawn, prepared_initialization_reacquires_plan_after_registry_reset) {
    reset_entities(); setup_test_world(); InitAbilities();
    uint32_t code = MAKEFOURCC('h','f','o','o');
    vec2_t point = {64, 64};
    edict_t *first = unit_create(0, code, &point, 0);
    unitRuntimeType_t *type = G_UnitRuntimeType(code);
    T_NOT_NULL(type->initialization);
    uint32_t epoch = type->initialization_epoch;
    unitAttack_t const *defaults = S_AttackProfileRead(first, 0);
    InitAbilities();
    point.x = 192;
    edict_t *second = unit_create(0, code, &point, 0);
    T_EQ(type, G_UnitRuntimeType(code));
    T_NE(type->initialization_epoch, epoch);
    T_NOT_NULL(type->initialization);
    T_EQ(S_AttackProfileRead(second, 0), defaults);
    T_EQ(second->movement.fine_class, first->movement.fine_class);
    reset_entities(); setup_test_world();
}
TEST(wc3_spawn, fresh_arena_slots_omit_only_proven_redundant_clears) {
    reset_entities(); setup_test_world();
    uint64_t before = spawn_clear_bytes;
    edict_t *first = G_Spawn();
    T_EQ(spawn_clear_bytes, before);
    T_EQ(first->health.value, 0);
    T_EQ(first->sound.pending, 0);
    G_FreeEdict(first);
    first->health.value = 71; /* a stale writer must not survive slot reuse */
    first->sound.pending = 42;
    level.time += 1001;
    edict_t *reused = G_Spawn();
    T_ASSERT(reused == first);
    T_EQ(spawn_clear_bytes, before + sizeof(*first));
    T_EQ(reused->health.value, 0);
    T_EQ(reused->sound.pending, 0);
    T_EQ(reused->freetime, 0);
    edict_t *fresh = G_Spawn();
    T_EQ(spawn_clear_bytes, before + sizeof(*first));
    T_NE(fresh, reused);
    /* Restored/externally populated prefixes are always considered used. */
    G_MarkEdictStorageUsed(globals.num_edicts + 1);
    before = spawn_clear_bytes;
    G_Spawn();
    T_EQ(spawn_clear_bytes, before + sizeof(*first));
    reset_entities(); setup_test_world();
}

#endif
