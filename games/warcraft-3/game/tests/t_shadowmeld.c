#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"
#include "../hud/hud_local.h"

#define ID_ASHM MAKEFOURCC('A', 's', 'h', 'm')
#define ID_AHID MAKEFOURCC('A', 'h', 'i', 'd')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

static uiFrame_t shadowmeld_command_frame;
static bool shadowmeld_command_frame_seen;

static int shadowmeld_test_image_index(cstring_t name) { (void)name; return 1; }

static void shadowmeld_capture_write(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame = value;
    if (type == PF_UIFRAME && frame && frame->flags.type == FT_COMMANDBUTTON) {
        shadowmeld_command_frame = *frame;
        shadowmeld_command_frame_seen = true;
    }
}

static char const shadowmeld_slk[] =
    "ID;PWXL;N;E9;Y3;X9\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
    "C;Y1;X9;K\"DataA1\"\n"
    "C;Y2;X1;K\"Ashm\"\nC;Y2;X2;K\"Ashm\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"1.5\"\n"
    "C;Y3;X1;K\"Ahid\"\nC;Y3;X2;K\"Ahid\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"0\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"1.5\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *unit;
} shadowmeldFix_t;

static void shadowmeld_setup_as(shadowmeldFix_t *fix, uint32_t class_id) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(shadowmeld_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->unit = alloc_test_unit(class_id, 0, 0);
    fix->unit->s.player = 0;
    fix->unit->svflags |= SVF_MONSTER;
    fix->unit->abilities.added[0] = ID_ASHM;
    ARRAY_COUNT(fix->unit->abilities.added) = 1;
    fix->unit->stand = unit_stand;
    unit_stand(fix->unit);
}

static void shadowmeld_setup(shadowmeldFix_t *fix) {
    shadowmeld_setup_as(fix, MAKEFOURCC('e', 'a', 'r', 'c'));
}

static void shadowmeld_done(shadowmeldFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

static void shadowmeld_tick(edict_t *unit, uint32_t ms) {
    level.time += ms;
    S_RunAbilityUpdates(unit);
}

static bool shadowmeld_datagram_tint(edict_t *client_ent, uint32_t entity_number, color32_t *out) {
    uint8_t data[1024];
    uint32_t size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    uint32_t offset = 0;
    uint16_t header, count;

    if (!size || size < sizeof(header)) return false;
    memcpy(&header, data, sizeof(header));
    offset += sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count;
        if (offset + sizeof(lightning_count) > size) return false;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count));
        offset += sizeof(lightning_count) + lightning_count * sizeof(lightningEffect_t);
    }
    if (!(header & BZ_GAME_DATAGRAM_ENTITY_TINTS) || offset + sizeof(count) > size) return false;
    memcpy(&count, data + offset, sizeof(count));
    offset += sizeof(count);
    FOR_LOOP(i, count) {
        uint16_t number;
        color32_t color;
        if (offset + sizeof(number) + sizeof(color) > size) return false;
        memcpy(&number, data + offset, sizeof(number)); offset += sizeof(number);
        memcpy(&color, data + offset, sizeof(color)); offset += sizeof(color);
        if (number != entity_number) continue;
        if (out) *out = color;
        return true;
    }
    return false;
}

TEST(wc3_shadowmeld, ability_classes_are_not_wind_walk) {
    abilityitem_t passive = S_AbilityItem(ID_ASHM);
    abilityitem_t hide = S_AbilityItem(ID_AHID);
    T_NOT_NULL(passive.ability); T_NOT_NULL(hide.ability);
    T_EQ(passive.ability->proc, CAbilityShadowMeld);
    T_EQ(hide.ability->proc, CAbilityShadowMeldAkama);
    T_ASSERT((passive.ability->flags & (AB_PASSIVE | AB_UPDATE | AB_SPELL)) ==
             (AB_PASSIVE | AB_UPDATE | AB_SPELL));
    T_ASSERT((hide.ability->flags & (AB_PASSIVE | AB_UPDATE | AB_SPELL)) ==
             (AB_PASSIVE | AB_UPDATE | AB_SPELL));
    T_EQ(G_OrderId("ambush"), 852131);
}

TEST(wc3_shadowmeld, akama_variant_is_distinct_and_suppresses_auto_acquire) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    fix.unit->abilities.added[0] = ID_AHID;
    ARRAY_COUNT(fix.unit->abilities.added) = 1;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, passive_fades_after_stationary_night_interval) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    shadowmeld_tick(fix.unit, 1499);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    shadowmeld_tick(fix.unit, 1);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(S_UnitIsInvisibleToPlayer(fix.unit, 1));
    T_ASSERT(!S_UnitIsInvisibleToPlayer(fix.unit, 0));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, passive_fade_duration_uses_authored_shm1) {
    shadowmeldFix_t fix;
    AbilityData_t *ability;

    shadowmeld_setup(&fix);
    ability = (AbilityData_t *)G_AbilityData(ID_ASHM);
    ability->level[0].data[0].number = 0.75f; /* Shm1 / DataA1 */
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    shadowmeld_tick(fix.unit, 749);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    shadowmeld_tick(fix.unit, 1);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    shadowmeld_done(&fix);
}


TEST(wc3_shadowmeld, owner_presentation_uses_smoothstep_ghost_alpha) {
    shadowmeldFix_t fix;
    edict_t *clent;
    color32_t color;

    shadowmeld_setup(&fix);
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    T_FEQ(S_ShadowMeldPresentationAlpha(fix.unit), 1.0f, 0.001f);

    level.time += 750;
    /* smoothstep(0.5) = 0.5; lerp 1.0 -> 0.35 therefore yields 0.675 */
    T_FEQ(S_ShadowMeldPresentationAlpha(fix.unit), 0.675f, 0.001f);
    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.r, 255); T_EQ(color.g, 255); T_EQ(color.b, 255);
    T_EQ(color.a, 172);

    level.time += 750;
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_FEQ(S_ShadowMeldPresentationAlpha(fix.unit), 0.35f, 0.001f);
    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.a, 89);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, owner_ghost_alpha_multiplies_authored_vertex_alpha) {
    shadowmeldFix_t fix;
    edict_t *clent;
    color32_t color;

    shadowmeld_setup(&fix);
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;
    fix.unit->vertex_color = MAKE(color32_t, 210, 180, 150, 200);
    fix.unit->vertex_color_set = true;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);

    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.r, 210); T_EQ(color.g, 180); T_EQ(color.b, 150);
    T_EQ(color.a, 70); /* round(200 * 0.35) */
    T_EQ(fix.unit->vertex_color.a, 200); /* presentation did not mutate authority */

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, timed_invisibility_uses_owner_ghost_alpha) {
    shadowmeldFix_t fix;
    edict_t *clent;
    color32_t color;

    shadowmeld_setup(&fix);
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;
    fix.unit->s.renderfx |= RF_HIDDEN;
    unit_addtimedstatus(fix.unit, "Binv", 1, 120.0f);

    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.a, 89);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, persistent_invisibility_uses_owner_ghost_alpha) {
    shadowmeldFix_t fix;
    edict_t *clent;
    color32_t color;
    abilityitem_t ghost = S_AbilityItem(MAKEFOURCC('A','g','h','o'));
    abilityCall_t call = MAKE(abilityCall_t, .item = &ghost);

    shadowmeld_setup(&fix);
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;
    fix.unit->vertex_color = MAKE(color32_t, 210, 180, 150, 200);
    fix.unit->vertex_color_set = true;

    fix.unit->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    fix.unit->permanent_invisibility_reveal_until = 0;
    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.a, 70);

    fix.unit->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
    T_ASSERT(S_AbilityMessage(fix.unit, A_ENABLE, &call));
    T_ASSERT(S_GhostActive(fix.unit));
    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.a, 70);

    T_ASSERT(S_AbilityMessage(fix.unit, A_DISABLE, &call));
    T_ASSERT(shadowmeld_datagram_tint(clent, fix.unit->s.number, &color));
    T_EQ(color.a, 200);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, daylight_cancels_fade_and_active_invisibility) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(!S_UnitIsInvisibleToPlayer(fix.unit, 1));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hide_ambush_suppresses_acquisition_and_uses_same_fade) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    S_UnitAbilityEvent(fix.unit, A_MOVE_LEAVE);
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hide_toggle_state_tracks_explicit_hide) {
    shadowmeldFix_t fix;
    abilityitem_t item;
    abilityCall_t call;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    item = S_AbilityItem(ID_ASHM);
    call = MAKE(abilityCall_t, .item = &item);

    T_ASSERT(!S_AbilityMessage(fix.unit, A_TOGGLE_ON, &call));

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(S_AbilityMessage(fix.unit, A_TOGGLE_ON, &call));

    T_ASSERT(unit_issueimmediateorder(fix.unit, "stop"));
    T_ASSERT(!S_AbilityMessage(fix.unit, A_TOGGLE_ON, &call));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, active_hide_keeps_authored_art_when_unart_is_absent) {
    cstring_t const authored = "ReplaceableTextures\\CommandButtons\\BTNAmbush.blp";

    T_STREQ(G_CommandButtonValue(authored, NULL, true), authored);
    T_STREQ(G_CommandButtonValue(authored, "active.blp", true), "active.blp");
    T_STREQ(G_CommandButtonValue(authored, "active.blp", false), authored);
}

TEST(wc3_shadowmeld, hide_button_preserves_already_active_shadowmeld) {
    shadowmeldFix_t fix;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    cstring_t button[] = { "button", "Ashm" };

    shadowmeld_setup(&fix);
    clent->client = client;
    fix.unit->s.player = client->ps.number;
    G_SelectEntity(client, fix.unit);
    client->commands_dirty = false;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    G_ClientCommand(clent, 2, button);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->fading);
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(client->commands_dirty);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hide_state_changes_invalidate_command_card) {
    shadowmeldFix_t fix;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    cstring_t button[] = { "button", "Ashm" };

    shadowmeld_setup(&fix);
    clent->client = client;
    fix.unit->s.player = client->ps.number;
    G_SelectEntity(client, fix.unit);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    {
        abilityitem_t item = S_AbilityItem(ID_ASHM);
        T_ASSERT(!G_CommandButtonToggleOn(fix.unit, &item, false, -1));
    }
    client->commands_dirty = false;
    G_ClientCommand(clent, 2, button);
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(client->commands_dirty);
    T_EQ(G_UnitAbilityLevel(fix.unit, ID_ASHM), 1);
    T_ASSERT(G_ActorHasSkill(fix.unit, "Ashm"));
    T_ASSERT(S_UnitAbilityMessage(fix.unit, A_TOGGLE_ON, NULL));
    {
        abilityitem_t active_item = S_AbilityItem(ID_ASHM);
        abilityCall_t active_call = MAKE(abilityCall_t, .item = &active_item);
        T_ASSERT(S_AbilityHasCommand(active_item.ability));
        T_ASSERT(S_AbilityMessage(fix.unit, A_TOGGLE_ON, &active_call));
        T_ASSERT(G_CommandButtonToggleOn(fix.unit, &active_item, false, -1));
        T_EQ(GetAbilityIndex(active_item.ability->proc), GetAbilityIndex(CAbilityShadowMeld));
    }
    T_STREQ(G_CommandButtonValue("normal", "alternate", true), "alternate");
    {
        abilityitem_t item = S_AbilityItem(ID_ASHM);
        gameCommandButton_t button;
        gameCommandButton_t state = { .engaged = G_CommandButtonToggleOn(fix.unit, &item, false, -1) };
        void (*old_write)(pfWriteType_t, void const *) = gi.Write;
        int (*old_image_index)(cstring_t) = gi.ImageIndex;
        gi.Write = shadowmeld_capture_write;
        gi.ImageIndex = shadowmeld_test_image_index;
        shadowmeld_command_frame_seen = false;
        UI_WriteCommandButtonFrame(&state);
        T_ASSERT(shadowmeld_command_frame_seen);
        T_ASSERT(shadowmeld_command_frame.flagsvalue & UIFLAG_ABILITY_ENGAGED);
        T_ASSERT(!(shadowmeld_command_frame.flagsvalue & UIFLAG_ALTERNATE_ACTIVE));
        gi.Write = old_write;
        gi.ImageIndex = old_image_index;
        T_ASSERT(G_BuildCommandButton(fix.unit, "Ashm", false, 0, &button));
        T_EQ(button.engaged, 1);
    }

    client->commands_dirty = false;
    T_ASSERT(unit_issueimmediateorder(fix.unit, "stop"));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(client->commands_dirty);
    T_STREQ(G_CommandButtonValue("normal", "alternate", false), "normal");
    {
        abilityitem_t item = S_AbilityItem(ID_ASHM);
        T_ASSERT(!G_CommandButtonToggleOn(fix.unit, &item, false, -1));
    }

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, stop_retires_explicit_hide_then_allows_passive_refade) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);

    T_ASSERT(unit_issueimmediateorder(fix.unit, "stop"));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hold_position_allows_passive_shadowmeld_without_hide_hold_fire) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "holdposition"));
    T_ASSERT(fix.unit->movement.holding_position);
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->movement.holding_position);
    T_ASSERT(!S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, attack_order_immediately_breaks_shadowmeld_and_hide) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    shadowmeld_setup(&fix);
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);

    order_attack(fix.unit, enemy);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->fading);
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);
    T_EQ(fix.unit->goalentity, enemy);
    T_NOT_NULL(fix.unit->currentmove);
    T_EQ(fix.unit->currentmove->proc, CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, explicit_hide_does_not_retaliate_when_hit_during_fade) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    shadowmeld_setup(&fix);
    fix.unit->health.value = fix.unit->health.max_value = 100.0f;
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld->fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);

    T_Damage(fix.unit, enemy, 1);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(fix.unit->shadowmeld->fading);
    T_ASSERT(fix.unit->goalentity != enemy);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, explicit_hide_blocks_idle_automatic_attack_acquisition) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    uint32_t i;
    shadowmeld_setup_as(&fix, MAKEFOURCC('E', 't', 'y', 'r'));
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.cooldown = 1.0f;
    fix.unit->attack1.damageBase = 10;
    fix.unit->attack1.range = 150.0f;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    fix.unit->runtime.acquisition_range = 128.0f;
    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    gi.LinkEntity(fix.unit); gi.LinkEntity(enemy);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    T_ASSERT(!S_UnitAbilityEvent(fix.unit, A_IDLE));
    T_ASSERT(!(fix.unit->aiflags & AI_AUTOCAST_ACTIVE));
    T_FEQ(G_AcquisitionRange(fix.unit), 128.0f, 0.01f);
    T_ASSERT(G_FindNearestEnemy(fix.unit, 128.0f) == enemy);

    /* Exercise a frame on which ai_stand's staggered acquisition scan runs. */
    for (i = 0; i < 300; i++) {
        if (G_ShouldAcquireThisFrame(fix.unit)) break;
        level.time++;
    }
    T_ASSERT(i < 300);
    T_ASSERT(G_ShouldAcquireThisFrame(fix.unit));
    ai_stand(fix.unit);

    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_NULL(fix.unit->goalentity);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_save, shadowmeld_state_round_trips) {
    cstring_t filename = Test_TempPath("shadowmeld-save.bin");
    shadowmeldFix_t fix;
    uint32_t unit_number;

    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    unit_number = fix.unit->s.number;

    T_ASSERT(WriteGame(filename));
    S_ShadowMeldBreak(fix.unit);
    T_ASSERT(ReadGame(filename));
    fix.unit = g_edicts + unit_number;
    T_ASSERT(fix.unit->shadowmeld->hide_order_active);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));

    remove(filename);
    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, ambush_is_rejected_during_day) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();

    T_ASSERT(!unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(!fix.unit->shadowmeld || !fix.unit->shadowmeld->hide_order_active);

    shadowmeld_done(&fix);
}
#endif
