/*
 * t_combat.c — In-engine combat tests (see CONTRIBUTING.md).
 *
 * These run inside the real WoW game module.  The +test boot calls
 * SV_InitGameProgs, so gi/globals are live and G_RegisterModel resolves real
 * animations from the mounted archives (run via `make test-wow-engine`, which
 * passes -data). Test entities occupy edicts [1..2], and death must keep that
 * allocation count unchanged when the target becomes a corpse.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "game/g_wow_local.h"

#include <string.h>

static uint32_t cheat_packets;
static int32_t cheat_opcode;
static bool cheat_writing;
static edict_t * cheat_recipient;
static char cheat_text[1024];
static cstring_t cheat_cvar(cstring_t name, cstring_t fallback) { return !strcmp(name, "sv_cheats") ? "1" : fallback; }
static void cheat_write(pfWriteType_t type, void const *data) {
    if (!cheat_writing && type == PF_BYTE) { cheat_opcode = *(int32_t const *)data; cheat_writing = true; }
    if (cheat_opcode == svc_console_print && type == PF_STRING) snprintf(cheat_text, sizeof(cheat_text), "%s", (cstring_t)data);
}
static void cheat_unicast(edict_t * ent) {
    if (cheat_opcode == svc_console_print) { cheat_packets++; cheat_recipient = ent; }
    cheat_writing = false;
}

/* Register the player model once; it carries the Attack/Pain/Death animations
 * the AI code needs to start a swing and transition the same entity to a corpse. */
static int combat_model(void) {
    static int model = -1;
    if (model < 0) model = G_RegisterModel(WOW_PLAYER_MODEL);
    return model;
}

/* Two hostile creatures 2 units apart (inside WOW_MELEE_RANGE), attacker locked
 * onto the target.  Returns real edicts driven by the real AI think function. */
static void combat_prepare(edict_t * *attacker_out, edict_t * *target_out) {
    int model = combat_model();
    edict_t * attacker = &wow_edicts[1];
    edict_t * target = &wow_edicts[2];
    wowEntityLocal_t *al, *tl;

    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    globals.num_edicts = 3;
    wow_spawns_this_frame = 0;

    attacker->inuse = target->inuse = true;
    attacker->s.number = 1;
    target->s.number = 2;
    attacker->s.model = target->s.model = model;
    attacker->svflags = target->svflags = SVF_MONSTER;
    attacker->s.origin2 = (vector2_t){ 0.0f, 0.0f };
    target->s.origin2 = (vector2_t){ 2.0f, 0.0f };

    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    al->think = tl->think = Wow_RunCreatureFrame;
    al->pain = tl->pain = Wow_AIPain;
    al->attack = tl->attack = Wow_AIAttack;
    al->health = 5;
    al->enemy = target;
    tl->health = 3;

    *attacker_out = attacker;
    *target_out = target;
}

TEST(wow_combat, cheat_feedback_reaches_issuing_client) {
    struct game_import saved = gi;
    edict_t * player, *target;
    cstring_t god[] = { "god" }, give[] = { "give", "health", "25" };
    combat_prepare(&player, &target);
    player->client = &wow_clients[0].client;
    gi.CvarString = cheat_cvar;
    gi.Write = cheat_write;
    gi.unicast = cheat_unicast;
    cheat_packets = 0;
    cheat_writing = false;
    globals.ClientCommand(player, 1, god);
    T_ASSERT(Wow_EntityLocal(player)->godmode);
    T_EQ(cheat_packets, 1);
    T_EQ(cheat_recipient, player);
    T_EQ(cheat_opcode, svc_console_print);
    T_STREQ(cheat_text, "WoW: god on");
    globals.ClientCommand(player, 3, give);
    T_EQ(Wow_EntityLocal(player)->health, 25);
    T_EQ(cheat_packets, 2);
    T_STREQ(cheat_text, "WoW: give health applied");
    gi.CvarString = saved.CvarString;
    globals.ClientCommand(player, 1, god);
    T_ASSERT(Wow_EntityLocal(player)->godmode);
    T_EQ(cheat_packets, 3);
    T_STREQ(cheat_text, "WoW: cheats are disabled; set sv_cheats 1");
    gi = saved;
}

TEST(wow_combat, attack_applies_damage_at_damage_point) {
    edict_t * attacker, *target;
    wowEntityLocal_t *al, *tl;
    uint32_t hp;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    /* Explicit timing makes the swing deterministic regardless of the model's
     * animation split. */
    al->attack_damage_point = 250;
    al->attack_backswing = 450;
    /* Clear the target's attack so a hit plays a pain reaction instead of
     * retaliating (Wow_ApplyDamage prefers retaliation when possible). */
    tl->attack = NULL;

    Wow_AIAttack(attacker);
    T_ASSERT(al->attack_damage_time > 0); /* swing started (attack anim resolved) */

    hp = tl->health;
    while (al->attack_damage_time > 0) {
        T_EQ(tl->health, hp); /* no damage before the damage point */
        Wow_AIAdvanceLockedFrame(attacker);
    }
    T_EQ(tl->health, hp - 1); /* damage lands exactly at the damage point */
    T_ASSERT(tl->pain_time > 0); /* and triggers the target's pain */
}

TEST(wow_combat, explicit_timing_overrides_animation_split) {
    edict_t * attacker, *target;
    wowEntityLocal_t *al;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    al->attack_damage_point = 120;
    al->attack_backswing = 180;

    Wow_AIAttack(attacker);
    T_EQ(al->attack_damage_time, 120);
    T_EQ(al->attack_backswing_time, 180);
    T_EQ(al->attack_time, 300);
}

TEST(wow_combat, lethal_attack_triggers_death_state) {
    edict_t * attacker, *target;
    wowEntityLocal_t *al, *tl;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    tl->health = 1;
    al->attack_damage_point = 100;
    al->attack_backswing = 100;

    Wow_AIAttack(attacker);
    while (al->attack_damage_time > 0) Wow_AIAdvanceLockedFrame(attacker);

    T_ASSERT(tl->dead);
    T_EQ(tl->health, 0);
    T_ASSERT(target->svflags & SVF_DEADMONSTER);
}

TEST(wow_combat, dead_entity_ignores_pain_and_attack) {
    edict_t * attacker, *target;
    wowEntityLocal_t *tl;

    combat_prepare(&attacker, &target);
    tl = Wow_EntityLocal(target);
    Wow_AIDie(target, attacker);

    Wow_AIPain(target);
    Wow_AIAttack(target);

    T_ASSERT(tl->dead);
    T_EQ(tl->pain_time, 0);
    T_EQ(tl->attack_damage_time, 0);
    T_EQ(tl->attack_backswing_time, 0);
}

TEST(wow_combat, death_holds_terminal_frame) {
    edict_t * attacker, *target;
    wowEntityLocal_t *tl;
    uint32_t terminal;
    int num_edicts;

    combat_prepare(&attacker, &target);
    tl = Wow_EntityLocal(target);
    num_edicts = globals.num_edicts;
    Wow_AIDie(target, attacker);
    T_NOT_NULL(tl->animation); /* real death animation resolved from the model */
    terminal = tl->animation->interval[1];

    for (int i = 0; i < 30; i++) Wow_AIAdvanceLockedFrame(target);
    T_EQ(target->s.frame, terminal - 1);
    T_EQ(globals.num_edicts, num_edicts); /* death must not allocate a duplicate model */
    T_ASSERT(tl->think == Wow_RunCorpseFrame);
    T_ASSERT(tl->corpse_timer > 0);
}

#endif /* BZ_TESTS */
