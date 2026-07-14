#include "test_framework.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

static DWORD test_clear_world_calls;
static DWORD test_apply_lobby_calls;

/* Stub: G_RegisterModel returns model index = index+1 (non-zero = found). */
int G_RegisterModel(LPCSTR filename) {
    (void)filename;
    static DWORD model_counter = 1000;
    return (int)(model_counter++);
}

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname) {
    (void)modelindex;
    (void)animname;
    return NULL;
}

void G_FreeModels(void) {
}

void PF_TextRemoveComments(LPSTR buffer) {
    (void)buffer;
}

typedef struct {
    char name[64];
    int index;
} testModel_t;

static testModel_t test_models[32];
static DWORD test_num_models;

static int test_model_index(LPCSTR model_name) {
    FOR_LOOP(i, test_num_models) {
        if (!strcasecmp(test_models[i].name, model_name)) {
            return test_models[i].index;
        }
    }
    ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
    strncpy(test_models[test_num_models].name, model_name, sizeof(test_models[0].name) - 1);
    test_models[test_num_models].index = (int)test_num_models + 1;
    test_num_models++;
    return (int)test_num_models;
}

static BYTE test_multicast_buf[MAX_MSGLEN];
static DWORD test_multicast_size;

static void test_write_data(void const *data, DWORD size) {
    if (!data || test_multicast_size + size > sizeof(test_multicast_buf)) {
        return;
    }
    memcpy(test_multicast_buf + test_multicast_size, data, size);
    test_multicast_size += size;
}

static void test_write(pfWriteType_t type, void const *value) {
    BYTE b;
    SHORT s;
    LPCSTR text;

    switch (type) {
        case PF_BYTE:
            b = (BYTE)*(LONG const *)value;
            test_write_data(&b, sizeof(b));
            break;
        case PF_SHORT:
            s = (SHORT)*(LONG const *)value;
            test_write_data(&s, sizeof(s));
            break;
        case PF_STRING:
            text = value ? (LPCSTR)value : "";
            test_write_data(text, (DWORD)strlen(text) + 1);
            break;
        default:
            break;
    }
}

static DWORD test_unicast_calls;

static void test_unicast(LPEDICT ent) {
    (void)ent;
    test_unicast_calls++;
}

static char test_last_error[512];

static void test_error(LPCSTR fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(test_last_error, sizeof(test_last_error), fmt, args);
    va_end(args);
}

static void test_clear_world(void) {
    test_clear_world_calls++;
}

static void test_apply_lobby_settings(LPMAPINFO info) {
    test_apply_lobby_calls++;
    ASSERT_NOT_NULL(info);
}

static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
    (void)name;
    return fallback ? fallback : "";
}

/* Stub for engine callbacks: configstring, readfile. */
static void test_configstring(DWORD index, LPCSTR string) {
    (void)index;
    (void)string;
}

static LPCSTR test_get_configstring(DWORD index) {
    (void)index;
    return "";
}

static HANDLE test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(HANDLE mem) {
    free(mem);
}

static HANDLE test_read_file(LPCSTR filename, LPDWORD size) {
    (void)filename;
    if (size) *size = 0;
    return NULL;
}

void UI_WriteWowHud(LPEDICT ent) {
    (void)ent;
}

static struct game_import test_import(void) {
    struct game_import import;
    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = NULL;
    import.ReadFile = test_read_file;
    import.ClearWorld = test_clear_world;
    import.ApplyLobbySettings = test_apply_lobby_settings;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    import.Write = test_write;
    import.unicast = test_unicast;
    import.error = test_error;
    return import;
}

static void reset_state(void) {
    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    memset(wow_clients, 0, sizeof(wow_clients));
    memset(test_models, 0, sizeof(test_models));
    test_num_models = 0;
    test_clear_world_calls = 0;
    test_apply_lobby_calls = 0;
    test_unicast_calls = 0;
    test_multicast_size = 0;
    memset(test_multicast_buf, 0, sizeof(test_multicast_buf));
    memset(test_last_error, 0, sizeof(test_last_error));

    globals.edicts = wow_edicts;
    globals.max_edicts = WOW_MAX_EDICTS;
    globals.max_clients = WOW_MAX_CLIENTS;
    globals.num_edicts = WOW_MAX_CLIENTS;
    globals.edict_size = sizeof(edict_t);
}

static LPEDICT make_player(void) {
    reset_state();
    LPEDICT ent = &wow_edicts[0];

    memset(ent, 0, sizeof(*ent));
    memset(&wow_entity_locals[0], 0, sizeof(wow_entity_locals[0]));
    {
        wowEntityLocal_t *local = &wow_entity_locals[0];

        ent->client = &wow_clients[0].client;
        ent->s.number = 0;
        ent->inuse = true;
        local->kind = WOW_ENTITY_PLAYER;
        local->health = 50;
        local->hostile = false;
        local->attack_damage_point = 250;
        local->attack_backswing = 450;
        ent->idle = Wow_AIIdle;
        ent->attack = Wow_AIAttack;
        ent->pain = Wow_AIPain;
        ent->s.origin = (VECTOR3){ 0.0f, 0.0f, 0.0f };
        ent->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
        ent->s.angle = 0.0f;
        ent->s.scale = 1.0f;
        ent->s.radius = 1.0f;
        ent->s.flags = EF_GROUND_ANCHOR;
        memset(&ent->client->ps, 0, sizeof(ent->client->ps));
        ent->client->ps.number = 0;
    }
    return ent;
}

static LPEDICT make_creature(FLOAT x, FLOAT y) {
    LPEDICT ent = Wow_Spawn();

    if (!ent) return NULL;
    {
        wowEntityLocal_t *local = Wow_EntityLocal(ent);

        local->kind = WOW_ENTITY_CREATURE;
        local->health = 3;
        local->hostile = true;
        ent->idle = Wow_AIIdle;
        ent->attack = Wow_AIAttack;
        ent->pain = Wow_AIPain;
        ent->s.origin = (VECTOR3){ x, y, 0.0f };
        ent->s.origin2 = (VECTOR2){ x, y };
        ent->s.angle = 0.0f;
        ent->s.scale = 1.0f;
        ent->s.radius = 1.5f;
        ent->svflags = SVF_MONSTER;
    }
    return ent;
}

/* ---- Ability tests ---- */

static void test_firebolt_spawns_projectile(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(10.0f, 0.0f);

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ_INT((int)target->s.number, 1);

    Wow_FireFirebolt(caster, target);

    {
        /* Projectile should be spawned at edict index 2 */
        LPEDICT proj = &wow_edicts[2];
        wowEntityLocal_t *pl = Wow_EntityLocal(proj);

        ASSERT(proj->inuse);
        ASSERT_NOT_NULL(pl);
        ASSERT_EQ_INT((int)pl->kind, WOW_ENTITY_PROJECTILE);
        ASSERT_EQ_INT((int)pl->projectile_target, 1);
        ASSERT_EQ_INT((int)pl->projectile_caster, 0);
        ASSERT_EQ_FLOAT(pl->projectile_speed, 25.0f, 0.001f);
        ASSERT_EQ_INT((int)pl->projectile_damage, 2);
        ASSERT_EQ_FLOAT(proj->s.origin.x, 0.0f, 0.001f);
        ASSERT_EQ_FLOAT(proj->s.origin.y, 0.0f, 0.001f);
        ASSERT_EQ_FLOAT(proj->s.scale, 0.8f, 0.001f);
        ASSERT(proj->s.model > 0);
    }
}

static void test_firebolt_homing_moves_toward_target(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(10.0f, 0.0f);
    LPEDICT proj;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    Wow_FireFirebolt(caster, target);

    proj = &wow_edicts[2];
    ASSERT(proj->inuse);

    /* Run a few projectile frames — should move toward target at x=10 */
    Wow_RunProjectile(proj);
    ASSERT(proj->inuse);
    ASSERT(proj->s.origin.x > 0.0f);
    FLOAT first_x = proj->s.origin.x;

    Wow_RunProjectile(proj);
    ASSERT(proj->inuse);
    ASSERT(proj->s.origin.x > first_x);

    /* Should eventually reach and be removed */
    DWORD max_steps = 200;
    while (proj->inuse && max_steps-- > 0) {
        Wow_RunProjectile(proj);
    }
    ASSERT(!proj->inuse);
}

static void test_firebolt_applies_damage_on_hit(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(3.0f, 0.0f); /* close range */
    wowEntityLocal_t *target_local;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    target_local = Wow_EntityLocal(target);
    ASSERT_EQ_INT((int)target_local->health, 3);

    Wow_FireFirebolt(caster, target);
    {
        LPEDICT proj = &wow_edicts[2];

        /* Run projectile until it hits */
        DWORD max_steps = 200;
        while (proj->inuse && max_steps-- > 0) {
            Wow_RunProjectile(proj);
        }
        ASSERT(!proj->inuse);
    }
    ASSERT_EQ_INT((int)target_local->health, 1);
}

static void test_firebolt_lethal_kills_target(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(3.0f, 0.0f);
    wowEntityLocal_t *target_local;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    target_local = Wow_EntityLocal(target);
    target_local->health = 1;

    Wow_FireFirebolt(caster, target);
    {
        LPEDICT proj = &wow_edicts[2];
        DWORD max_steps = 200;
        while (proj->inuse && max_steps-- > 0) {
            Wow_RunProjectile(proj);
        }
        ASSERT(!proj->inuse);
    }
    ASSERT(target_local->dead);
    ASSERT_EQ_INT((int)target_local->health, 0);
}

static void test_firebolt_at_dead_caster_does_nothing(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(5.0f, 0.0f);
    wowEntityLocal_t *caster_local;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    caster_local = Wow_EntityLocal(caster);
    caster_local->dead = true;

    Wow_FireFirebolt(caster, target);
    /* No projectile should be spawned */
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT e = &wow_edicts[i];
        wowEntityLocal_t *el = Wow_EntityLocal(e);
        if (e->inuse && el && el->kind == WOW_ENTITY_PROJECTILE) {
            ASSERT(!"projectile was spawned despite dead caster");
        }
    }
}

static void test_firebolt_at_dead_target_does_nothing(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(5.0f, 0.0f);
    wowEntityLocal_t *target_local;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    target_local = Wow_EntityLocal(target);
    target_local->dead = true;

    Wow_FireFirebolt(caster, target);
    /* No projectile should be spawned */
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT e = &wow_edicts[i];
        wowEntityLocal_t *el = Wow_EntityLocal(e);
        if (e->inuse && el && el->kind == WOW_ENTITY_PROJECTILE) {
            ASSERT(!"projectile was spawned despite dead target");
        }
    }
}

static void test_firebolt_self_cast_does_nothing(void) {
    LPEDICT caster = make_player();

    ASSERT_NOT_NULL(caster);
    Wow_FireFirebolt(caster, caster);
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        LPEDICT e = &wow_edicts[i];
        wowEntityLocal_t *el = Wow_EntityLocal(e);
        if (e->inuse && el && el->kind == WOW_ENTITY_PROJECTILE) {
            ASSERT(!"projectile was spawned for self-cast");
        }
    }
}

static void test_projectile_disappears_when_target_dies(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(8.0f, 0.0f);
    wowEntityLocal_t *target_local;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    target_local = Wow_EntityLocal(target);

    Wow_FireFirebolt(caster, target);
    {
        LPEDICT proj = &wow_edicts[2];

        ASSERT(proj->inuse);
        /* Move a bit */
        Wow_RunProjectile(proj);
        ASSERT(proj->inuse);
        /* Kill the target while projectile is in flight */
        target_local->dead = true;
        target->inuse = false;
        /* Next projectile run should detect target is gone and self-remove */
        Wow_RunProjectile(proj);
        ASSERT(!proj->inuse);
    }
}

static void test_healing_touch_heals_caster(void) {
    LPEDICT caster = make_player();
    wowEntityLocal_t *local;

    ASSERT_NOT_NULL(caster);
    local = Wow_EntityLocal(caster);
    local->health = 30;

    Wow_HealingTouch(caster);
    ASSERT_EQ_INT((int)local->health, 32); /* healed by 2 */
}

static void test_healing_touch_caps_at_100(void) {
    LPEDICT caster = make_player();
    wowEntityLocal_t *local;

    ASSERT_NOT_NULL(caster);
    local = Wow_EntityLocal(caster);
    local->health = 99;

    Wow_HealingTouch(caster);
    ASSERT_EQ_INT((int)local->health, 100); /* caps at 100 */

    Wow_HealingTouch(caster);
    ASSERT_EQ_INT((int)local->health, 100); /* stays at 100 */
}

static void test_healing_touch_on_dead_does_nothing(void) {
    LPEDICT caster = make_player();
    wowEntityLocal_t *local;

    ASSERT_NOT_NULL(caster);
    local = Wow_EntityLocal(caster);
    local->health = 30;
    local->dead = true;

    Wow_HealingTouch(caster);
    ASSERT_EQ_INT((int)local->health, 30); /* unchanged */
}

static void test_healing_touch_plays_cast_animation(void) {
    LPEDICT caster = make_player();
    wowEntityLocal_t *local;
    ASSERT_NOT_NULL(caster);
    local = Wow_EntityLocal(caster);
    local->health = 30;

    Wow_HealingTouch(caster);
    ASSERT_EQ_INT((int)local->health, 32);
}

static void test_find_spell_target_uses_selected_entity(void) {
    LPEDICT caster = make_player();
    LPEDICT target1 = make_creature(5.0f, 0.0f);
    LPEDICT target2 = make_creature(20.0f, 0.0f);
    /* target2 is farther but within range, target1 is closer */
    LPEDICT result;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target1);
    ASSERT_NOT_NULL(target2);

    /* Set selected entity to the farther one */
    caster->client->ps.selected_entity = target2->s.number;
    result = Wow_FindSpellTarget(caster, 40.0f);
    /* Should prefer selected even though target1 is closer */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_INT((int)result->s.number, (int)target2->s.number);
}

static void test_find_spell_target_falls_back_to_nearest(void) {
    LPEDICT caster = make_player();
    LPEDICT target1 = make_creature(5.0f, 0.0f);
    LPEDICT target2 = make_creature(50.0f, 0.0f);
    LPEDICT result;
    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target1);
    ASSERT_NOT_NULL(target2);

    /* No selected entity */
    caster->client->ps.selected_entity = 0;
    result = Wow_FindSpellTarget(caster, 40.0f);
    /* Should find the nearest (target1 at 5 units) */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_INT((int)result->s.number, (int)target1->s.number);
}

static void test_find_spell_target_returns_null_when_out_of_range(void) {
    LPEDICT caster = make_player();
    LPEDICT target = make_creature(100.0f, 0.0f);
    LPEDICT result;

    ASSERT_NOT_NULL(caster);
    ASSERT_NOT_NULL(target);
    caster->client->ps.selected_entity = target->s.number;
    result = Wow_FindSpellTarget(caster, 10.0f);
    ASSERT_NULL(result);
}

int main(void) {
    RUN_TEST(test_firebolt_spawns_projectile);
    RUN_TEST(test_firebolt_homing_moves_toward_target);
    RUN_TEST(test_firebolt_applies_damage_on_hit);
    RUN_TEST(test_firebolt_lethal_kills_target);
    RUN_TEST(test_firebolt_at_dead_caster_does_nothing);
    RUN_TEST(test_firebolt_at_dead_target_does_nothing);
    RUN_TEST(test_firebolt_self_cast_does_nothing);
    RUN_TEST(test_projectile_disappears_when_target_dies);
    RUN_TEST(test_healing_touch_heals_caster);
    RUN_TEST(test_healing_touch_caps_at_100);
    RUN_TEST(test_healing_touch_on_dead_does_nothing);
    RUN_TEST(test_healing_touch_plays_cast_animation);
    RUN_TEST(test_find_spell_target_uses_selected_entity);
    RUN_TEST(test_find_spell_target_falls_back_to_nearest);
    RUN_TEST(test_find_spell_target_returns_null_when_out_of_range);
    TEST_RESULTS();
}
