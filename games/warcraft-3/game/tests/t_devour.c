#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t code, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

/* Explicit Dev1/Dev2 values prove both the neutral-level cap and damage come
 * from the selected ability row, not fixed retail constants. */
static char const devour_slk[] =
    "ID;PWXL;N;EBB;Y4;X8\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
    "C;Y2;X1;K\"Adev\"\nC;Y2;X2;K\"Adev\"\nC;Y2;X3;K\"ground,nonhero,enemy,organic\"\n"
    "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"100\"\n"
    "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\n"
    "C;Y3;X1;K\"ACdv\"\nC;Y3;X2;K\"ACdv\"\nC;Y3;X3;K\"ground,nonhero,enemy,organic\"\n"
    "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"100\"\n"
    "C;Y3;X7;K\"4\"\nC;Y3;X8;K\"7\"\n"
    "C;Y4;X1;K\"Aeat\"\nC;Y4;X2;K\"Aeat\"\n"
    "C;Y4;X3;K\"tree\"\nC;Y4;X4;K\"0\"\nC;Y4;X5;K\"0\"\n"
    "C;Y4;X6;K\"100\"\nC;Y4;X7;K\"50\"\nC;Y4;X8;K\"0\"\nE\n";

static UnitAbilities_t devour_caster_abilities = { .abilList = "Adev,ACdv,Aeat" };
static bool devour_cursor_clear_seen;

static void devour_cursor_capture_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE && *(int32_t const *)data == svc_cursor)
        devour_cursor_clear_seen = true;
}

static void devour_units(edict_t **caster, edict_t **victim) {
    reset_entities();
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    *caster = alloc_test_unit(MAKEFOURCC('n','d','t','u'), 100, 100);
    *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 140, 100);
    (*caster)->targtype = TARG_GROUND;
    (*victim)->targtype = TARG_GROUND;
    (*caster)->svflags |= SVF_MONSTER;
    (*victim)->svflags |= SVF_MONSTER;
    (*caster)->data.UnitAbilities = &devour_caster_abilities;
    (*caster)->s.player = 0;
    (*victim)->s.player = 1;
    (*caster)->health.max_value = (*caster)->health.value = 500;
    (*victim)->health.max_value = (*victim)->health.value = 100;
    (*caster)->stand = (*victim)->stand = unit_stand;
    (*caster)->die = (*victim)->die = unit_die;
    memset(level.alliances, 0, sizeof(level.alliances));
}

static bool devour_message(edict_t *caster, uint32_t code, edict_t *victim, abilityMsg_t msg) {
    abilityitem_t item = S_AbilityItem(code);
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = victim);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &target);
    return S_AbilityMessage(caster, msg, &call) != 0;
}

static edict_t *devour_find_thinker(edict_t *caster) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        if (ent->inuse && ent->owner == caster && ent->think == devour_think) return ent;
    }
    return NULL;
}

TEST(wc3_devour, escape_cancel_clears_tree_target_mode) {
    edict_t *caster, *victim;
    edict_t *client_ent;
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);

    devour_units(&caster, &victim);
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    client_ent->inuse = true;
    client_ent->client->connected = true;
    client_ent->client->ps.number = 0;
    caster->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','e','a','t'), .level = 1);
    G_SelectEntity(client_ent->client, caster);

    G_ClientCommand(client_ent, 2, (cstring_t[]){ "button", "Aeat" });
    T_EQ(client_ent->client->menu.ability_code, MAKEFOURCC('A','e','a','t'));
    T_NOT_NULL(client_ent->client->menu.on_entity_selected);

    /* Exercise the actual Cancel command-card dispatch used by Escape. */
    G_ClientCommand(client_ent, 2, (cstring_t[]){ "button", "CmdCancel" });
    T_NULL(client_ent->client->menu.on_entity_selected);
    T_NULL(client_ent->client->menu.on_location_selected);
    T_ASSERT(!S_SpellEntityTargetMode(client_ent));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_devour, command_refresh_clears_tree_target_mode) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *caster, *victim, *client_ent;
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);

    devour_units(&caster, &victim);
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    client_ent->inuse = true;
    client_ent->client->connected = true;
    client_ent->client->ps.number = 0;
    caster->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','e','a','t'), .level = 1);
    G_SelectEntity(client_ent->client, caster);
    G_ClientCommand(client_ent, 2, (cstring_t[]){ "button", "Aeat" });
    T_NOT_NULL(client_ent->client->menu.on_entity_selected);

    devour_cursor_clear_seen = false;
    gi.Write = devour_cursor_capture_write;
    Get_Commands_f(client_ent);
    gi.Write = old_write;
    T_NULL(client_ent->client->menu.on_entity_selected);
    T_NULL(client_ent->client->menu.on_location_selected);
    T_ASSERT(devour_cursor_clear_seen);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_devour, digest_ticks_then_caster_death_releases_living_victim) {
    edict_t *caster, *victim, *thinker;
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);
    devour_units(&caster, &victim);
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','d','e','v'), victim, A_VALIDATE));
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','d','e','v'), victim, A_EXECUTE));
    T_ASSERT(S_DevourHasVictim(caster));
    T_ASSERT(victim->aiflags & AI_DEVOURED);
    T_ASSERT(victim->s.renderfx & RF_HIDDEN);
    T_ASSERT(!G_UnitIsWorldActive(victim));
    T_ASSERT(!devour_message(caster, MAKEFOURCC('A','d','e','v'), victim, A_VALIDATE));
    T_ASSERT(!S_SpellIsAliveTarget(victim));
    T_Damage(victim, caster, 20); /* External attacks cannot reach swallowed units. */
    T_FEQ(victim->health.value, 100.0f, 0.001f);

    thinker = devour_find_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) { level.time = thinker->freetime; G_RunEntity(thinker); }
    T_FEQ(victim->health.value, 95.0f, 0.001f);
    unit_die(caster, victim);
    T_ASSERT(!S_DevourHasVictim(caster));
    T_ASSERT(!devour_find_thinker(caster));
    T_ASSERT(G_UnitIsWorldActive(victim));
    T_ASSERT(!(victim->s.renderfx & RF_HIDDEN));
    T_ASSERT(!G_UnitHasFlag(victim, UNIT_FLAG_PAUSED));
    T_FEQ(victim->health.value, 95.0f, 0.001f);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_devour, lethal_digestion_and_forced_victim_removal_do_not_eject) {
    edict_t *caster, *victim, *thinker;
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);
    devour_units(&caster, &victim);
    victim->health.value = 4.0f;
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','d','e','v'), victim, A_EXECUTE));
    thinker = devour_find_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) { level.time = thinker->freetime; G_RunEntity(thinker); }
    T_ASSERT(M_IsDead(victim));
    T_ASSERT(victim->s.renderfx & RF_HIDDEN);
    T_ASSERT(victim->aiflags & AI_CORPSE_UNRAISABLE);
    T_ASSERT(!S_DevourHasVictim(caster));
    T_ASSERT(!devour_find_thinker(caster));
    unit_die(caster, NULL);
    T_ASSERT(victim->s.renderfx & RF_HIDDEN);

    devour_units(&caster, &victim);
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','d','e','v'), victim, A_EXECUTE));
    G_FreeEdict(victim);
    T_ASSERT(!S_DevourHasVictim(caster));
    T_ASSERT(!devour_find_thinker(caster));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_devour, creep_alias_uses_own_damage_and_level_limit) {
    edict_t *caster, *victim, *thinker;
    UnitBalance_t creep = MAKE(UnitBalance_t, .level = 5);
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);
    devour_units(&caster, &victim);
    victim->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    victim->data.UnitBalance = &creep;
    T_ASSERT(!devour_message(caster, MAKEFOURCC('A','C','d','v'), victim, A_VALIDATE));
    creep.level = 4;
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','C','d','v'), victim, A_VALIDATE));
    T_ASSERT(devour_message(caster, MAKEFOURCC('A','C','d','v'), victim, A_EXECUTE));
    thinker = devour_find_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) { level.time = thinker->freetime; G_RunEntity(thinker); }
    T_FEQ(victim->health.value, 93.0f, 0.001f);
    G_FreeEdict(caster);
    T_ASSERT(G_UnitIsWorldActive(victim));
    T_ASSERT(!(victim->s.renderfx & RF_HIDDEN));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
TEST(wc3_devour, ordinary_unit_cast_survives_save_mid_digestion) {
    cstring_t filename = Test_TempPath("wc3-devour-mid-digestion.bin");
    uint32_t const code = MAKEFOURCC('A', 'd', 'e', 'v');
    edict_t *caster, *victim, *thinker;
    uint32_t next_tick;
    slkTestData_t *rows = parse_slk_string(devour_slk), *old = G_SetSLKRows("AbilityData", rows);

    devour_units(&caster, &victim);
    T_ASSERT(S_CastUnitTargetSpell(caster, code, victim));
    thinker = devour_find_thinker(caster);
    T_NOT_NULL(thinker);
    if (thinker) {
        next_tick = thinker->freetime;
        T_ASSERT(WriteGame(filename));
        /* Deliberately destroy only live state serialized with the thinker.
         * ReadGame must restore the callback, identity, flags and tick rate. */
        thinker->think = NULL;
        thinker->owner = thinker->goalentity = NULL;
        thinker->wait = 0.0f;
        caster->aiflags &= ~AI_DEVOURING;
        victim->aiflags &= ~AI_DEVOURED;
        T_ASSERT(ReadGame(filename));
        T_ASSERT(thinker->think == devour_think);
        T_ASSERT(S_SpellChannelOwner(thinker) == caster);
        T_ASSERT(S_SpellChannelTarget(thinker) == victim);
        T_ASSERT(S_DevourHasVictim(caster));
        T_ASSERT(victim->aiflags & AI_DEVOURED);
        T_FEQ(thinker->wait, 5.0f, 0.001f);
        level.time = next_tick;
        G_RunEntity(thinker);
        T_FEQ(victim->health.value, 95.0f, 0.001f);
        G_FreeEdict(caster);
        T_ASSERT(G_UnitIsWorldActive(victim));
    }
    remove(filename);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
#endif
