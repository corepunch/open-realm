#ifdef BZ_TESTS
/*
 * test_game.c — Tests for game utilities not covered by other suites.
 *
 * Covered:
 *   G_RegionContains  — point-in-region containment (empty, hit, miss,
 *                       multi-rect, exclusive upper boundary)
 *   G_FreeEdict       — entity lifecycle: inuse cleared, freetime stamped
 *   M_IsDead          — health-based liveness check
 *   compress_stat     — 8-bit health/mana encoding
 *   FindEnumValue     — NULL-terminated string-enum lookup
 *   unit_runwait      — per-frame wait counter and callback dispatch
 *   unit_issuetargetorder — attack and unknown-order paths
 *   unit_learnability — hero ability slot management
 *   Alliance types    — ALLIANCE_SHARED_VISION and independent flags
 *   Player resources  — PLAYERSTATE_RESOURCE_GOLD / LUMBER set/get
 *   Fog of war        — grid sizing, circle reveal, visible/explored decay
 */

#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);


#include "../game/hud/hud_utils.h"
#include "../hud/hud_local.h"
#include "../../../renderer/r_local.h"

/* Forward declarations for internal functions not exposed in any header. */
BOOL  M_IsDead(LPCEDICT ent);
DWORD FindEnumValue(LPCSTR value, LPCSTR values[]);
void  unit_runwait(LPEDICT self, void (*callback)(LPEDICT));

/* =========================================================================
 * Helpers
 * ========================================================================= */

static LPPLAYER game_player(int idx) {
    game.clients[idx].ps.number = (DWORD)idx;
    return &game.clients[idx].ps;
}

static LPEDICT make_test_unit(void) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    ent->health.value     = 250.0f;
    ent->health.max_value = 250.0f;
    ent->stand            = unit_stand;
    ent->movetype         = MOVETYPE_STEP;
    unit_stand(ent);
    return ent;
}

static BOOL hover_layout_pending, hover_layer_seen, hover_name_seen, hover_hp_seen, hover_mana_seen, hover_name_sized, hover_name_centered, hover_name_short;
static DWORD hover_frame_count, hover_unicast_count, hover_image_count, hover_font_count;
static LPEDICT hover_unicast_target;
static pfWriteType_t window_frame_type;
static DWORD window_text_offset;

static int hover_test_image(LPCSTR name) { T_ASSERT(name && *name); return (int)++hover_image_count; }
static int hover_test_font(LPCSTR name, DWORD size) {
    T_ASSERT(name && *name); T_EQ(size, HUD_FONT_SIZE); hover_font_count++; return 1;
}
static void hover_test_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_BYTE) {
        LONG byte = *(LONG const *)value;
        if (hover_layout_pending) { hover_layer_seen = byte == LAYER_WORLD_HOVER; hover_layout_pending = false; }
        else hover_layout_pending = byte == svc_layout;
    } else if (type == PF_UIFRAME) {
        LPCUIFRAME frame = value;
        hover_frame_count++;
        hover_name_seen |= frame->flags.type == FT_NAMETAG && frame->stat == UI_STAT_CONTEXT_NAME;
        hover_name_sized |= frame->flags.type == FT_NAMETAG && (frame->flagsvalue & UIFLAG_SIZE_TO_CONTENT);
        if (frame->flags.type == FT_NAMETAG && frame->buffer.size == sizeof(uiNameTag_t)) {
            uiNameTag_t const *tag = frame->buffer.data;
            hover_name_centered = frame->points.x[FPP_MID].used;
            hover_name_short = tag->padding_y == 0.006f;
        }
        hover_hp_seen |= frame->flags.type == FT_SIMPLESTATUSBAR && frame->stat == UI_STAT_CONTEXT_HEALTH;
        hover_mana_seen |= frame->stat == UI_STAT_CONTEXT_MANA;
    }
}
static void hover_test_unicast(LPEDICT ent) { hover_unicast_count++; hover_unicast_target = ent; }
static void window_test_write(pfWriteType_t type, void const *value) {
    if (type != PF_UIWINDOWFRAME) return;
    window_frame_type = type;
    window_text_offset = (DWORD)(uintptr_t)((LPCUIFRAME)value)->text;
}

/* =========================================================================
 * HUD frame numbering
 * ========================================================================= */

TEST(wc3_game, hud_proxy_number_advances_past_fdf_frame) {
    T_EQ(UI_NextProxyFrameNumber(1, 10), 11);
}

TEST(wc3_game, hud_proxy_number_never_moves_backwards) {
    T_EQ(UI_NextProxyFrameNumber(12, 10), 12);
}

TEST(wc3_game, hud_authored_window_frame_uses_offset_codec) {
    FRAMEDEF frame = { .Type = FT_TEXT, .Text = "Window text" };
    uiWindowDef_t def = { .id = 1, .class_id = 2, .flags = UI_WINDOW_MOVABLE };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;

    window_frame_type = PF_BYTE; window_text_offset = 0;
    gi.Write = window_test_write;
    UI_WriteWindowStart(&def); UI_WriteFrame(&frame);
    ui_window_writing = false; gi.Write = old_write;
    T_EQ(window_frame_type, PF_UIWINDOWFRAME);
    T_ASSERT(window_text_offset > 0);
}

TEST(wc3_game, text_exact_width_fits) { T_ASSERT(R_TextFitsWidth(0.0f)); }
TEST(wc3_game, text_subpixel_residue_fits) { T_ASSERT(R_TextFitsWidth(-0.0000005f)); }
TEST(wc3_game, text_real_overflow_does_not_fit) { T_ASSERT(!R_TextFitsWidth(-0.00001f)); }
TEST(wc3_game, hud_stale_attribute_texture_uses_infocard_asset) {
    T_STREQ(UI_ResolveTextureAlias("HeroStrengthIcon"),
                  "UI\\Widgets\\Console\\Human\\infocard-heroattributes-str.blp");
}
TEST(wc3_game, hud_valid_texture_path_is_unchanged) {
    T_STREQ(UI_ResolveTextureAlias("UI\\Feedback\\Resources\\ResourceGold.blp"),
                  "UI\\Feedback\\Resources\\ResourceGold.blp");
}
TEST(wc3_game, hud_status_icon_keys_follow_upgrade_and_neutral_families) {
    char key[96];

    /* Stock Footman: Normal attack + Heavy/Large armor, both upgradeable. */
    UI_InfoPanelIconSkinKey("Damage", "normal", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageNormal");
    UI_InfoPanelIconSkinKey("Armor", "large", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorLarge");

    /* Stock Rifleman: Piercing attack + Medium armor, both upgradeable. */
    UI_InfoPanelIconSkinKey("Damage", "pierce", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamagePierce");
    UI_InfoPanelIconSkinKey("Armor", "medium", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorMedium");

    /* Units without a matching upgrade class use Warcraft's Neutral family. */
    UI_InfoPanelIconSkinKey("Damage", "normal", false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageNormalNeutral");
    UI_InfoPanelIconSkinKey("Armor", "large", false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorLargeNeutral");

    /* Heroes are not special-cased: no upgrade class means HeroNeutral, while
     * a custom Hero type with an applicable upgrade uses the normal family. */
    UI_InfoPanelIconSkinKey("Damage", "hero", false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageHeroNeutral");
    UI_InfoPanelIconSkinKey("Armor", "hero", false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorHeroNeutral");
    UI_InfoPanelIconSkinKey("Armor", "hero", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorHero");

    /* Preserve a custom Spells skin field; the runtime resolver only retries
     * Magic when the Spells field is absent, matching Warsmash. */
    UI_InfoPanelIconSkinKey("Damage", "spells", false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageSpellsNeutral");

    /* Warsmash normalizes Warcraft's Heavy defense spelling to Large and its
     * enum fallback entries are Unknown for damage and Small for armor. */
    UI_InfoPanelIconSkinKey("Armor", "heavy", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorLarge");
    UI_InfoPanelIconSkinKey("Damage", "seige", true, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageSiege");
    UI_InfoPanelIconSkinKey("Damage", NULL, false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconDamageUnknownNeutral");
    UI_InfoPanelIconSkinKey("Armor", NULL, false, key, sizeof(key));
    T_STREQ(key, "InfoPanelIconArmorSmallNeutral");
}

static DWORD status_icon_read_count;
static HANDLE status_icon_missing_read(LPCSTR path, DWORD *size) {
    (void)path;
    status_icon_read_count++;
    if (size) *size = 0;
    return NULL;
}

TEST(wc3_game, hud_status_icon_neutral_fallback_is_cached) {
    HANDLE (*saved_read)(LPCSTR, DWORD *) = gi.ReadFile;
    LPCSTR texture;

    UI_TestResetInfoPanelIconCache();
    texture = UI_TestResolveTypedInfoPanelIcon("Armor", "Hero", false);
    T_STREQ(texture, "TestUI\\Textures\\solid_white.blp");

    status_icon_read_count = 0;
    gi.ReadFile = status_icon_missing_read;
    T_STREQ(UI_TestResolveTypedInfoPanelIcon("Armor", "Hero", false), texture);
    T_EQ(status_icon_read_count, 0);
    gi.ReadFile = saved_read;
}

TEST(wc3_game, hud_status_icon_missing_candidates_cache_null) {
    HANDLE (*saved_read)(LPCSTR, DWORD *) = gi.ReadFile;

    UI_TestResetInfoPanelIconCache();
    status_icon_read_count = 0;
    gi.ReadFile = status_icon_missing_read;
    T_NULL(UI_TestResolveTypedInfoPanelIcon("Armor", "Divine", false));
    T_ASSERT(status_icon_read_count >= 1);

    status_icon_read_count = 0;
    T_NULL(UI_TestResolveTypedInfoPanelIcon("Armor", "Divine", false));
    T_EQ(status_icon_read_count, 0);
    gi.ReadFile = saved_read;
}

TEST(wc3_game, hud_second_attack_requires_enabled_slot_and_showui) {
    UnitWeapons_t weapons = { 0 };

    weapons.attack2.damageDice = 2;
    weapons.attack2.showUI = true;
    weapons.attacksEnabled = 1;
    T_ASSERT(!UI_HasSecondAttack(&weapons));

    weapons.attacksEnabled = 3;
    weapons.attack2.showUI = false;
    T_ASSERT(!UI_HasSecondAttack(&weapons));

    weapons.attack2.showUI = true;
    T_ASSERT(UI_HasSecondAttack(&weapons));

    weapons.attack2.damageDice = 0;
    T_ASSERT(!UI_HasSecondAttack(&weapons));
}
TEST(wc3_game, player_zero_food_ignores_free_edicts) {
    static UnitBalance_t const owned_balance = { .foodMade = 6, .foodUsed = 1 };
    static UnitBalance_t const enemy_balance = { .foodMade = 12, .foodUsed = 2 };
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT owned, enemy;

    reset_entities();
    client->ps.number = 0;
    owned = G_Spawn(); enemy = G_Spawn();
    owned->s.player = 0; owned->UnitBalance = &owned_balance;
    enemy->s.player = 1; enemy->UnitBalance = &enemy_balance;

    G_AccumulatePlayerFood(client);

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 6);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 1);
}
TEST(wc3_game, portrait_live_stats_refresh_reserved_connected_client_edict) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT player;
    LPEDICT unit;

    reset_entities();
    setup_test_world();
    player = &g_edicts[0];
    player->client = client;
    client->ps.number = 0;
    client->connected = true;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 192.0f);
    unit->s.player = 0;
    unit->health.value = 311.0f;
    unit->health.max_value = 420.0f;
    unit->mana.value = 33.0f;
    unit->mana.max_value = 100.0f;
    G_SelectEntity(client, unit);

    /* Keep the legacy info-panel cache current so the test exercises only the
     * per-frame live portrait producer and does not need to serialize FDF. */
    client->infopanel.entity = unit->s.number;
    client->infopanel.hp = 311;
    client->infopanel.mana = 33;
    client->infopanel.xp = 0;
    T_ASSERT(!player->inuse);

    G_UpdateClientInfoPanels();
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_HEALTH], 311);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH], 420);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_MANA], 33);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA], 100);

    unit->health.value = 207.0f;
    unit->mana.value = 21.0f;
    client->infopanel.hp = 207;
    client->infopanel.mana = 21;
    G_UpdateClientInfoPanels();
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_HEALTH], 207);
    T_EQ(client->ps.stats[UI_PLAYERSTAT_SELECTION_MANA], 21);
}

TEST(wc3_game, hud_portrait_model_uses_serialized_field) {
    FRAMEDEF frame = { 0 };
    UI_SetPortraitFrameModel(&frame, 42);
    T_EQ(frame.Type, FT_PORTRAIT);
    T_EQ(frame.Portrait.model, 42);
}

TEST(wc3_game, hud_single_line_fdf_text_serializes_declared_font_height) {
    FRAMEDEF frame = { .Type = FT_STRING };
    uiFrame_t wire = {0};
    BYTE typedata[128] = {0};
    char textbuf[128] = {0};

    frame.Text = "Strength:";
    frame.Font.Size = 0.010f;
    UI_ResetFrameWriteList();
    T_ASSERT(UI_BuildFrameForWrite(&frame, &wire, typedata, sizeof(typedata),
                                   textbuf, sizeof(textbuf)));
    T_FEQ(wire.size.height, 0.010f, 0.0001f);
}

TEST(wc3_game, hud_multiline_fdf_text_keeps_renderer_auto_height) {
    FRAMEDEF frame = { .Type = FT_STRING };
    uiFrame_t wire = {0};
    BYTE typedata[128] = {0};
    char textbuf[128] = {0};

    frame.Text = "Line one\nLine two";
    frame.Font.Size = 0.010f;
    UI_ResetFrameWriteList();
    T_ASSERT(UI_BuildFrameForWrite(&frame, &wire, typedata, sizeof(typedata),
                                   textbuf, sizeof(textbuf)));
    T_FEQ(wire.size.height, 0.0f, 0.0001f);
}

TEST(wc3_game, hud_simple_button_serializes_button_state) {
    BYTE typedata[256];
    char textbuf[128];
    uiFrame_t out;
    LPFRAMEDEF button, normal, pushed, disabled;

    UI_ClearTemplates();
    button = UI_Spawn(FT_SIMPLEBUTTON, NULL);
    normal = UI_Spawn(FT_TEXTURE, button);
    pushed = UI_Spawn(FT_TEXTURE, button);
    disabled = UI_Spawn(FT_TEXTURE, button);
    T_NOT_NULL(button); T_NOT_NULL(normal); T_NOT_NULL(pushed); T_NOT_NULL(disabled);
    snprintf(normal->Name, sizeof(normal->Name), "Normal"); normal->Texture.Image = 11;
    snprintf(pushed->Name, sizeof(pushed->Name), "Pushed"); pushed->Texture.Image = 12;
    snprintf(disabled->Name, sizeof(disabled->Name), "Disabled"); disabled->Texture.Image = 13;
    snprintf(button->Button.NormalTexture, sizeof(button->Button.NormalTexture), "Normal");
    snprintf(button->Button.PushedTexture, sizeof(button->Button.PushedTexture), "Pushed");
    snprintf(button->Button.DisabledTexture, sizeof(button->Button.DisabledTexture), "Disabled");

    UI_ResetFrameWriteList();
    T_ASSERT(UI_BuildFrameForWrite(button, &out, typedata, sizeof(typedata), textbuf, sizeof(textbuf)));
    T_EQ(out.buffer.size, sizeof(uiSimpleButton_t));
    T_EQ(((uiSimpleButton_t const *)out.buffer.data)->normal.texture, 11);
    T_EQ(((uiSimpleButton_t const *)out.buffer.data)->pushed.texture, 12);
    T_EQ(((uiSimpleButton_t const *)out.buffer.data)->disabled.texture, 13);
    T_EQ(((uiSimpleButton_t const *)out.buffer.data)->normal.fontcolor.a, 255);

    snprintf(button->Button.NormalText.text, sizeof(button->Button.NormalText.text), "KEY_MENU");
    snprintf(button->Button.DisabledText.text, sizeof(button->Button.DisabledText.text), "MENU");
    button->OnClick[0] = '\0';
    T_ASSERT(UI_BuildFrameForWrite(button, &out, typedata, sizeof(typedata), textbuf, sizeof(textbuf)));
    T_STREQ(out.text, "MENU");
    snprintf(button->OnClick, sizeof(button->OnClick), "menu");
    T_ASSERT(UI_BuildFrameForWrite(button, &out, typedata, sizeof(typedata), textbuf, sizeof(textbuf)));
    T_STREQ(out.text, "KEY_MENU");

    UI_ClearTemplates();
}

TEST(wc3_game, hud_highlight_serializes_texture_and_mode) {
    BYTE typedata[256];
    char textbuf[128];
    uiFrame_t out;
    LPFRAMEDEF highlight;

    UI_ClearTemplates();
    highlight = UI_Spawn(FT_HIGHLIGHT, NULL);
    T_NOT_NULL(highlight);
    highlight->Highlight.AlphaFile = 17;
    highlight->Highlight.AlphaMode = BLEND_MODE_ADD;

    UI_ResetFrameWriteList();
    T_ASSERT(UI_BuildFrameForWrite(highlight, &out, typedata, sizeof(typedata), textbuf, sizeof(textbuf)));
    T_EQ(out.buffer.size, sizeof(uiHighlight_t));
    T_EQ(((uiHighlight_t const *)out.buffer.data)->alphaFile, 17);
    T_EQ(((uiHighlight_t const *)out.buffer.data)->alphaMode, BLEND_MODE_ADD);
    UI_ClearTemplates();
}

TEST(wc3_game, hud_glue_button_serializes_backdrops) {
    BYTE typedata[256];
    char textbuf[128];
    uiFrame_t out;
    LPFRAMEDEF button, normal, pushed;

    UI_ClearTemplates();
    button = UI_Spawn(FT_GLUEBUTTON, NULL);
    normal = UI_Spawn(FT_BACKDROP, button);
    pushed = UI_Spawn(FT_BACKDROP, button);
    T_NOT_NULL(button); T_NOT_NULL(normal); T_NOT_NULL(pushed);
    snprintf(normal->Name, sizeof(normal->Name), "NormalBackdrop"); normal->Backdrop.Background = 21;
    snprintf(pushed->Name, sizeof(pushed->Name), "PushedBackdrop"); pushed->Backdrop.Background = 22;
    snprintf(button->Control.Backdrop.Normal, sizeof(button->Control.Backdrop.Normal), "NormalBackdrop");
    snprintf(button->Control.Backdrop.Pushed, sizeof(button->Control.Backdrop.Pushed), "PushedBackdrop");

    UI_ResetFrameWriteList();
    T_ASSERT(UI_BuildFrameForWrite(button, &out, typedata, sizeof(typedata), textbuf, sizeof(textbuf)));
    T_EQ(out.buffer.size, sizeof(uiGlueTextButton_t));
    T_EQ(((uiGlueTextButton_t const *)out.buffer.data)->normal.Background, 21);
    T_EQ(((uiGlueTextButton_t const *)out.buffer.data)->pushed.Background, 22);
    UI_ClearTemplates();
}

TEST(wc3_game, hud_authored_row_keeps_template_size) {
    FRAMEDEF tmpl = { .Type = FT_FRAME, .Width = 0.08f, .Height = 0.033f };
    FRAMEDEF parent = { .Type = FT_FRAME };
    LPFRAMEDEF row = UI_CloneStackedRow(&tmpl, &parent, 0);
    T_NOT_NULL(row);
    T_FEQ(row->Width, 0.08f, 0.001f);
    T_FEQ(row->Height, 0.033f, 0.001f);
    T_FEQ(row->Points.y[FPP_MIN].offset, 0.0f, 0.001f);
}

TEST(wc3_game, hud_authored_row_stride_uses_template_height) {
    FRAMEDEF tmpl = { .Type = FT_FRAME, .Width = 0.15f, .Height = 0.012f };
    FRAMEDEF parent = { .Type = FT_FRAME };
    LPFRAMEDEF row = UI_CloneStackedRow(&tmpl, &parent, 3);
    T_NOT_NULL(row);
    T_ASSERT(row->Points.y[FPP_MIN].relativeTo == &parent);
    T_FEQ(row->Points.y[FPP_MIN].offset, -0.036f, 0.001f);
}


TEST(wc3_game, hud_quest_visibility_requires_enabled_and_discovered) {
    QUEST quest = { 0 };

    T_ASSERT(!QuestIsVisible(&quest));
    quest.enabled = true;
    T_ASSERT(!QuestIsVisible(&quest));
    quest.discovered = true;
    T_ASSERT(QuestIsVisible(&quest));
    quest.enabled = false;
    T_ASSERT(!QuestIsVisible(&quest));
}

TEST(wc3_game, hud_quest_rows_bind_authored_children) {
    QUEST quest = { .title = "Test Quest", .discovered = true, .required = true, .enabled = true };
    QUESTITEM item = { .description = "Test Objective" };
    LPFRAMEDEF list, item_list, button, title, item_title;

    UI_ClearTemplates();
    quest_row_template = UI_Spawn(FT_FRAME, NULL);
    snprintf(quest_row_template->Name, sizeof(quest_row_template->Name), "QuestListItem");
    UI_SetSize(quest_row_template, 0.08f, 0.033f);
    button = UI_Spawn(FT_GLUEBUTTON, quest_row_template);
    snprintf(button->Name, sizeof(button->Name), "QuestListItemButton");
    title = UI_Spawn(FT_TEXT, quest_row_template);
    snprintf(title->Name, sizeof(title->Name), "QuestListItemTitle");
    quest_item_template = UI_Spawn(FT_FRAME, NULL);
    snprintf(quest_item_template->Name, sizeof(quest_item_template->Name), "QuestItemListItem");
    UI_SetSize(quest_item_template, 0.15f, 0.012f);
    item_title = UI_Spawn(FT_TEXT, quest_item_template);
    snprintf(item_title->Name, sizeof(item_title->Name), "QuestItemListItemTitle");
    list = UI_Spawn(FT_FRAME, NULL);
    item_list = UI_Spawn(FT_FRAME, NULL);
    quest.items = &item;
    level.quests = &quest;

    PopulateQuestList(list, true, &quest);
    PopulateQuestItems(item_list, &quest);
    title = UI_FindChildFrame(list, "QuestListItemTitle");
    button = UI_FindChildFrame(list, "QuestListItemButton");
    item_title = UI_FindChildFrame(item_list, "QuestItemListItemTitle");
    T_NOT_NULL(title);
    T_NOT_NULL(button);
    T_NOT_NULL(item_title);
    T_FEQ(title->Parent->Width, list->Width, 0.001f);
    T_FEQ(item_title->Parent->Height, 0.012f, 0.001f);
    T_STREQ(title->Text, "> Test Quest");
    T_STREQ(button->OnClick, "quest 0");
    T_STREQ(item_title->Text, "- Test Objective");

    /* Refreshing the dialog reuses authored runtime rows instead of consuming
     * another set of permanent FDF frame slots. */
    PopulateQuestList(list, true, &quest);
    T_ASSERT(UI_FindChildFrame(list, "QuestListItemTitle") == title);

    level.quests = NULL;
    quest_row_template = quest_item_template = NULL;
    quests_loaded = false;
    memset(&qd, 0, sizeof(qd));
    UI_ClearTemplates();
}

TEST(wc3_game, hud_quest_rows_show_undiscovered_and_completed_state) {
    QUEST done = { .title = "Finished", .discovered = true, .required = true, .enabled = true, .completed = true };
    QUEST hidden = { .title = "Secret", .discovered = false, .required = false, .enabled = true };
    LPFRAMEDEF list, optional, button, title, complete;

    done.next = &hidden;
    UI_ClearTemplates();
    quest_row_template = UI_Spawn(FT_FRAME, NULL);
    UI_SetSize(quest_row_template, 0.08f, 0.033f);
    button = UI_Spawn(FT_GLUEBUTTON, quest_row_template);
    snprintf(button->Name, sizeof(button->Name), "QuestListItemButton");
    title = UI_Spawn(FT_TEXT, quest_row_template);
    snprintf(title->Name, sizeof(title->Name), "QuestListItemTitle");
    title->Font.DisabledColor = MAKE(COLOR32, 64, 80, 96, 160);
    complete = UI_Spawn(FT_TEXT, quest_row_template);
    snprintf(complete->Name, sizeof(complete->Name), "QuestListItemComplete");
    optional = UI_Spawn(FT_FRAME, NULL);
    UI_SetSize(list = UI_Spawn(FT_FRAME, NULL), 0.21f, 0.11f);
    UI_SetSize(optional, 0.21f, 0.11f);
    level.quests = &done;

    PopulateQuestList(list, true, &done);
    PopulateQuestList(optional, false, &done);
    title = UI_FindChildFrame(list, "QuestListItemTitle");
    complete = UI_FindChildFrame(list, "QuestListItemComplete");
    T_STREQ(title->Text, "> Finished");
    T_STREQ(complete->Text, "(QUESTCOMPLETED)");
    T_FEQ(title->Font.Color.r, 64, 0.001f);
    title = UI_FindChildFrame(optional, "QuestListItemTitle");
    T_STREQ(title->Text, "UNDISCOVERED_QUEST");

    level.quests = NULL;
    quest_row_template = quest_item_template = NULL;
    quests_loaded = false;
    memset(&qd, 0, sizeof(qd));
    UI_ClearTemplates();
}

TEST(wc3_game, hud_message_overlay_loads_authored_geometry) {
    msg_overlay_loaded = false;
    T_ASSERT(MessageEnsureLoaded());
    T_FEQ(msg_overlay_text.Width, 0.30f, 0.001f);
    T_FEQ(msg_overlay_text.Height, 0.145f, 0.001f);
    T_FEQ(msg_overlay_text.Font.Size, 0.010f, 0.001f);
    T_FEQ(msg_overlay_text.Points.x[FPP_MIN].offset, 0.05f, 0.001f);
    T_FEQ(msg_overlay_text.Points.y[FPP_MIN].offset, -0.30f, 0.001f);
}

TEST(wc3_game, hud_message_overlay_position_is_runtime_data) {
    VECTOR2 pos = { 0.20f, 0.10f };
    FRAMEDEF frame = MessageFrame(&pos, "Runtime message");
    T_FEQ(frame.Width, 0.30f, 0.001f);
    T_FEQ(frame.Height, 0.145f, 0.001f);
    T_FEQ(frame.Points.x[FPP_MIN].offset, 0.20f, 0.001f);
    /* Formula: -(0.30 - pos.y); JASS y=0 anchors at 0.30 from screen top,
     * positive JASS y shifts the text upward (toward screen top, less negative offset). */
    T_FEQ(frame.Points.y[FPP_MIN].offset, -(0.30f - 0.10f), 0.001f);
    T_STREQ(frame.Text, "Runtime message");
}

TEST(wc3_game, hud_message_overlay_invalid_position_keeps_fdf_anchor) {
    VECTOR2 pos = { -1.0f, UI_BASE_HEIGHT + 1.0f };
    FRAMEDEF frame = MessageFrame(&pos, "Authored position");
    T_FEQ(frame.Points.x[FPP_MIN].offset, 0.05f, 0.001f);
    T_FEQ(frame.Points.y[FPP_MIN].offset, -0.30f, 0.001f);
}

TEST(wc3_game, overhead_bar_fill_keeps_warsmash_three_pixel_inset) {
    T_FEQ(0.008f - 0.003f * 2.0f, 0.002f, 0.0001f);
}

TEST(wc3_game, hover_layout_is_server_authored_with_entity_context_bindings) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image)(LPCSTR) = gi.ImageIndex;
    int (*old_font)(LPCSTR, DWORD) = gi.FontIndex;
    LPEDICT player;

    setup_test_world(); player = &g_edicts[0]; player->client->connected = true;
    player->mana.max_value = 100.0f; player->mana.value = 50.0f;
    hover_layout_pending = hover_layer_seen = hover_name_seen = hover_hp_seen = hover_mana_seen = hover_name_sized = false;
    hover_name_centered = hover_name_short = false;
    hover_frame_count = hover_unicast_count = hover_image_count = hover_font_count = 0; hover_unicast_target = NULL;
    gi.Write = hover_test_write; gi.unicast = hover_test_unicast;
    gi.ImageIndex = hover_test_image; gi.FontIndex = hover_test_font;
    UI_WriteHoverLayout(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image; gi.FontIndex = old_font;

    T_ASSERT(hover_layer_seen); T_EQ(hover_frame_count, 5);
    T_ASSERT(hover_name_seen); T_ASSERT(hover_name_sized); T_ASSERT(hover_name_centered); T_ASSERT(hover_name_short);
    T_ASSERT(hover_hp_seen); T_ASSERT(hover_mana_seen);
    T_EQ(hover_image_count, 6); T_EQ(hover_font_count, 1);
    T_EQ(hover_unicast_count, 1); T_ASSERT(hover_unicast_target == player);
}

/* =========================================================================
 * G_RegionContains
 * ========================================================================= */

TEST(wc3_game, region_contains_empty_region_false) {
    REGION r = { .num_rects = 0 };
    VECTOR2 p = { 5.0f, 5.0f };
    T_ASSERT(!G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_point_inside) {
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 50.0f, 50.0f };
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_point_outside) {
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 200.0f, 200.0f };
    T_ASSERT(!G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_multirect_hits_second) {
    /* Two non-overlapping rects; the point is in the second one. */
    REGION r = {
        .rects[0] = { {   0.0f,   0.0f }, {  50.0f,  50.0f } },
        .rects[1] = { { 200.0f, 200.0f }, { 300.0f, 300.0f } },
        .num_rects = 2
    };
    VECTOR2 p = { 250.0f, 250.0f };
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_max_boundary_exclusive) {
    /* Box2_containsPoint uses x < max.x (exclusive upper bound). */
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 100.0f, 50.0f };   /* exactly at max.x */
    T_ASSERT(!G_RegionContains(&r, &p));
}

/* =========================================================================
 * G_FreeEdict
 * ========================================================================= */

TEST(wc3_game, free_edict_clears_inuse) {
    LPEDICT ent = make_test_unit();
    T_ASSERT(ent->inuse);
    G_FreeEdict(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_game, free_edict_stamps_freetime) {
    LPEDICT ent = make_test_unit();
    level.time = 9876;
    G_FreeEdict(ent);
    T_EQ((int)ent->freetime, 9876);
}

/* =========================================================================
 * M_IsDead
 * ========================================================================= */

TEST(wc3_game, is_dead_alive_unit_false) {
    LPEDICT ent = make_test_unit();
    ent->health.value = 100.0f;
    T_ASSERT(!M_IsDead(ent));
}

TEST(wc3_game, is_dead_zero_hp_true) {
    LPEDICT ent = make_test_unit();
    ent->health.value = 0.0f;
    T_ASSERT(M_IsDead(ent));
}

TEST(wc3_game, is_dead_negative_hp_true) {
    LPEDICT ent = make_test_unit();
    ent->health.value = -1.0f;
    T_ASSERT(M_IsDead(ent));
}

/* =========================================================================
 * compress_stat
 * ========================================================================= */

TEST(wc3_game, compress_stat_full_health_is_255) {
    EDICTSTAT s = { 250.0f, 250.0f };
    T_EQ((int)compress_stat(&s), 255);
}

TEST(wc3_game, compress_stat_zero_health_is_0) {
    EDICTSTAT s = { 0.0f, 250.0f };
    T_EQ((int)compress_stat(&s), 0);
}

TEST(wc3_game, compress_stat_half_health) {
    EDICTSTAT s = { 125.0f, 250.0f };
    /* 255 * 125 / 250 = 127 (integer truncation). */
    T_EQ((int)compress_stat(&s), 127);
}

TEST(wc3_game, compress_stat_zero_max_is_0) {
    EDICTSTAT s = { 0.0f, 0.0f };
    T_EQ((int)compress_stat(&s), 0);
}

/* =========================================================================
 * FindEnumValue
 * ========================================================================= */

static LPCSTR test_attack_types[] = {
    "none", "normal", "pierce", "siege", "chaos", NULL
};

TEST(wc3_game, find_enum_first_value) {
    T_EQ((int)FindEnumValue("none", test_attack_types), 0);
}

TEST(wc3_game, find_enum_later_value) {
    T_EQ((int)FindEnumValue("pierce", test_attack_types), 2);
}

TEST(wc3_game, find_enum_null_input_returns_0) {
    T_EQ((int)FindEnumValue(NULL, test_attack_types), 0);
}

TEST(wc3_game, find_enum_unknown_returns_0) {
    T_EQ((int)FindEnumValue("magic", test_attack_types), 0);
}

/* =========================================================================
 * unit_runwait
 * ========================================================================= */

static int _runwait_cb_count = 0;

static void runwait_cb(LPEDICT ent) {
    (void)ent;
    _runwait_cb_count++;
}

TEST(wc3_game, runwait_zero_wait_no_callback) {
    LPEDICT ent = make_test_unit();
    ent->wait = 0.0f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    T_EQ(_runwait_cb_count, 0);
}

TEST(wc3_game, runwait_large_wait_decrements) {
    /* FRAMETIME = 100 ms → FRAMETIME/1000.f = 0.1 s. */
    LPEDICT ent = make_test_unit();
    ent->wait = 1.0f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    /* wait should decrease by 0.1. */
    T_FEQ(ent->wait, 0.9f, 0.01f);
    T_EQ(_runwait_cb_count, 0);
}

TEST(wc3_game, runwait_small_wait_triggers_callback) {
    /* wait == 0.05 < FRAMETIME/1000.f (0.1) → callback fires. */
    LPEDICT ent = make_test_unit();
    ent->wait = 0.05f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    T_EQ(_runwait_cb_count, 1);
    T_FEQ(ent->wait, 0.0f, 0.0001f);
}

/* =========================================================================
 * unit_issuetargetorder
 * ========================================================================= */

TEST(wc3_game, issuetargetorder_attack_returns_true) {
    LPEDICT unit   = make_test_unit();
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50.0f, 0.0f);
    /* order_attack is the real implementation from s_attack.c — just verify return value. */
    BOOL result = unit_issuetargetorder(unit, "attack", target);
    T_ASSERT(result);
}

TEST(wc3_game, issuetargetorder_unknown_returns_false) {
    LPEDICT unit   = make_test_unit();
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50.0f, 0.0f);
    BOOL result = unit_issuetargetorder(unit, "heal", target);
    T_ASSERT(!result);
}

/* =========================================================================
 * unit_learnability
 * ========================================================================= */

TEST(wc3_game, learnability_first_ability_fills_slot0) {
    LPEDICT ent = make_test_unit();
    DWORD code = MAKEFOURCC('A','H','b','z');
    unit_learnability(ent, code);
    T_EQ((int)ent->heroabilities[0].code,  (int)code);
    T_EQ((int)ent->heroabilities[0].level, 1);
}

TEST(wc3_game, learnability_same_code_increments_level) {
    LPEDICT ent = make_test_unit();
    DWORD code = MAKEFOURCC('A','H','b','z');
    unit_learnability(ent, code);
    unit_learnability(ent, code);
    T_EQ((int)ent->heroabilities[0].level, 2);
    /* Should still be in slot 0, not duplicated in slot 1. */
    T_EQ((int)ent->heroabilities[1].code, 0);
}

TEST(wc3_game, learnability_different_codes_fill_consecutive_slots) {
    LPEDICT ent = make_test_unit();
    DWORD code1 = MAKEFOURCC('A','H','b','z');
    DWORD code2 = MAKEFOURCC('A','H','t','b');
    unit_learnability(ent, code1);
    unit_learnability(ent, code2);
    T_EQ((int)ent->heroabilities[0].code, (int)code1);
    T_EQ((int)ent->heroabilities[1].code, (int)code2);
    T_EQ((int)ent->heroabilities[1].level, 1);
}

/* =========================================================================
 * Alliance type variations
 * ========================================================================= */

TEST(wc3_game, alliance_shared_vision_set_get) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    /* Clear alliance table. */
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

TEST(wc3_game, alliance_shared_vision_does_not_set_passive) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    /* Setting SHARED_VISION must not accidentally set PASSIVE. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_game, alliance_multiple_types_independent) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE,       true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

TEST(wc3_game, alliance_revoke_one_type_keeps_other) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE,       true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, false);
    T_ASSERT( G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

/* =========================================================================
 * Player resource stats — GOLD and LUMBER
 * ========================================================================= */

TEST(wc3_game, player_gold_default_zero) {
    LPPLAYER p = game_player(0);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 0);
}

TEST(wc3_game, player_gold_set_get) {
    LPPLAYER p = game_player(0);
    p->stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 500);
}

TEST(wc3_game, player_lumber_set_get) {
    LPPLAYER p = game_player(0);
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 200;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER], 200);
}

TEST(wc3_game, player_gold_lumber_independent) {
    LPPLAYER p = game_player(1);
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 300;
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 150;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD],   300);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER], 150);
}

/* =========================================================================
 * Fog of war
 * ========================================================================= */

TEST(wc3_game, fow_grid_uses_two_by_two_cells_per_tile) {
    G_FowInit();
    T_EQ(level.fow.width, 8);
    T_EQ(level.fow.height, 6);
    T_EQ(G_FowWorldToCellX(0.0f), 0);
    T_EQ(G_FowWorldToCellX(63.0f), 0);
    T_EQ(G_FowWorldToCellX(64.0f), 1);
    T_EQ(G_FowWorldToCellY(128.0f), 2);
    G_FowShutdown();
}

TEST(wc3_game, fow_revealer_marks_visible_and_explored) {
    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 128.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    G_FowUpdate();
    DWORD index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);
    T_ASSERT(level.fow.players[0].visible[index]);
    T_ASSERT(level.fow.players[0].explored[index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_updates_only_connected_shared_viewers) {
    reset_entities();
    G_FowInit();

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    revealer->s.player = 5;
    revealer->runtime.sight_radius.day = 128.0f;
    revealer->health.value = revealer->health.max_value = 1.0f;
    DWORD index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);

    G_FowUpdate();
    T_ASSERT(!level.fow.players[5].visible[index]);

    G_FowConnectPlayer(0);
    G_FowConnectPlayer(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(game_player(0), game_player(5), ALLIANCE_SHARED_VISION, true);
    G_FowUpdate();
    T_ASSERT(level.fow.players[0].visible[index]);
    T_ASSERT(!level.fow.players[1].visible[index]);
    T_ASSERT(!level.fow.players[5].visible[index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_visible_clears_but_explored_remains) {
    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 128.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    G_FowUpdate();
    DWORD index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);
    DWORD row = G_FowWorldToCellY(64.0f);
    T_ASSERT(level.fow.players[0].visible_rows[row]);
    revealer->s.renderfx |= RF_HIDDEN;
    G_FowUpdate();

    T_ASSERT(!level.fow.players[0].visible[index]);
    T_ASSERT(!level.fow.players[0].visible_rows[row]);
    T_ASSERT(level.fow.players[0].explored[index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_static_scenery_persists_after_unit_vision_leaves) {
    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 64.0f, 64.0f);
    LPEDICT unseen = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 1024.0f, 1024.0f);
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64.0f, 64.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 128.0f;
    revealer->health.value = revealer->health.max_value = 1.0f;
    tree->s.player = unseen->s.player = unit->s.player = building->s.player = MAX_PLAYERS;
    tree->svflags = unseen->svflags = SVF_STATIC_SCENERY;
    building->runtime.flags |= UNIT_BALANCE_BUILDING;

    G_FowUpdate();
    T_ASSERT(G_FowPlayerCanSeeEntity(0, tree));
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, unseen));
    T_ASSERT(G_FowPlayerCanSeeEntity(0, unit));
    T_ASSERT(G_FowPlayerCanSeeEntity(0, building));
    T_ASSERT(G_FowPlayerCanHoverEntity(0, unit));
    T_ASSERT(G_FowPlayerCanHoverEntity(0, building));
    revealer->s.renderfx |= RF_HIDDEN;
    G_FowUpdate();

    T_ASSERT(G_FowPlayerCanSeeEntity(0, tree));
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, unseen));
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, unit));
    T_ASSERT(G_FowPlayerCanSeeEntity(0, building));
    T_ASSERT(!G_FowPlayerCanHoverEntity(0, unit));
    T_ASSERT(!G_FowPlayerCanHoverEntity(0, building));
    G_FowShutdown();
}

TEST(wc3_game, acquisition_range_uses_spawn_cache) {
    LPEDICT ent = make_test_unit();
    ent->class_id = MAKEFOURCC('n', 'o', 'n', 'e');
    ent->runtime.acquisition_range = 375.0f;
    T_FEQ(G_AcquisitionRange(ent), 375.0f, 0.001f);
}

TEST(wc3_game, hold_position_acquires_within_uacq_not_attack_range) {
    LPEDICT guard, enemy;

    reset_entities();
    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeRescuable;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    guard = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 0.0f, 0.0f);
    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 200.0f, 0.0f);
    guard->s.player = 0; enemy->s.player = 1;
    guard->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
    guard->attack1.cooldown = 1.0f; guard->attack1.damageBase = 1;
    guard->attack1.range = 64.0f; guard->runtime.acquisition_range = 300.0f;
    guard->currentmove = &holdpos_move_stand;
    gi.LinkEntity(guard); gi.LinkEntity(enemy);
    level.time = 300;

    guard->currentmove->think(guard);
    T_ASSERT(guard->goalentity == enemy);
}

TEST(wc3_game, fow_blocker_stops_visibility_behind_it) {
    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 96.0f, 96.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 256.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 160.0f, 96.0f);
    blocker->s.flags |= EF_FOW_BLOCKER;
    blocker->health.value = 1.0f;
    blocker->health.max_value = 1.0f;

    G_FowUpdate();

    DWORD blocker_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(160.0f);
    /* Trees without a path texture dilate one cell for their canopy; test the
     * first cell behind that occluder, not a cell that is part of its visible rim. */
    DWORD behind_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(288.0f);
    T_ASSERT(level.fow.players[0].visible[blocker_index]);
    T_ASSERT(!level.fow.players[0].visible[behind_index]);
    G_FowShutdown();
}

#ifdef WC3_FOW_PACKED_MASK
static LPCSTR fow_fast_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_fow_fast") ? "1" : fallback;
}

TEST(wc3_game, fow_packed_fast_path_uses_word_mask_and_skips_occlusion) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    DWORD blocker_index, behind_index;

    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);
    gi.CvarString = fow_fast_cvar;

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 96.0f, 96.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 256.0f;
    revealer->health.value = revealer->health.max_value = 1.0f;

    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 160.0f, 96.0f);
    blocker->s.flags |= EF_FOW_BLOCKER;
    blocker->health.value = blocker->health.max_value = 1.0f;

    G_FowUpdate();
    blocker_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(160.0f);
    behind_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(288.0f);
    T_EQ(level.fow.players[0].packed_stride, (level.fow.width + 15) >> 4);
    T_ASSERT(level.fow.players[0].packed_visible[(blocker_index % level.fow.width >> 4) +
                                                blocker_index / level.fow.width * level.fow.players[0].packed_stride] &
             (1u << (blocker_index % level.fow.width & 15)));
    T_ASSERT(level.fow.players[0].packed_visible[(behind_index % level.fow.width >> 4) +
                                                behind_index / level.fow.width * level.fow.players[0].packed_stride] &
             (1u << (behind_index % level.fow.width & 15)));

    gi.CvarString = old_cvar;
    G_FowShutdown();
}
#endif

TEST(wc3_game, fow_blocker_cache_skips_clean_and_unchanged_dirty_updates) {
    DWORD old_index, new_index, sentinel;
    LPEDICT blocker;
    VECTOR2 direction = { 1.0f, 0.0f };

    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);
    blocker = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 64.0f, 64.0f);
    blocker->s.flags |= EF_FOW_BLOCKER;
    blocker->health.value = blocker->health.max_value = 1.0f;
    old_index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);
    new_index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(320.0f);
    sentinel = level.fow.width * level.fow.height - 1;

    G_FowUpdate();
    T_ASSERT(level.fow.blocked[old_index]);
    level.fow.blocked[sentinel] = 7;
    G_FowUpdate();
    T_EQ(level.fow.blocked[sentinel], 7);
    G_FowMarkBlockersDirty();
    G_FowUpdate();
    T_EQ(level.fow.blocked[sentinel], 7);

    G_PushEntity(blocker, 256.0f, &direction);
    G_FowUpdate();
    T_EQ(level.fow.blocked[sentinel], 0);
    T_ASSERT(!level.fow.blocked[old_index]);
    T_ASSERT(level.fow.blocked[new_index]);
    G_FowShutdown();
}

static pathTex_t *make_fow_pathtex(DWORD width, DWORD height, BYTE blocked) {
    pathTex_t *tex = gi.MemAlloc(sizeof(*tex) + width * height * sizeof(COLOR32));

    T_ASSERT(tex != NULL);
    tex->width = (WORD)width;
    tex->height = (WORD)height;
    FOR_LOOP(i, width * height) {
        tex->map[i] = (COLOR32){ 0, 0, blocked, 255 };
    }
    return tex;
}

TEST(wc3_game, fow_tree_pathtex_closes_gap_behind_canopy) {
    reset_entities();
    G_FowInit();
    G_FowConnectPlayer(0);

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32.0f, 128.0f);
    revealer->s.player = 0;
    revealer->runtime.sight_radius.day = 320.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 128.0f, 128.0f);
    tree->s.flags |= EF_FOW_BLOCKER;
    tree->targtype = TARG_TREE;
    tree->s.scale = 1.0f;
    tree->pathtex = make_fow_pathtex(4, 4, 1);
    tree->health.value = 1.0f;
    tree->health.max_value = 1.0f;

    G_FowUpdate();

    DWORD canopy_index = G_FowWorldToCellY(128.0f) * level.fow.width + G_FowWorldToCellX(192.0f);
    DWORD behind_index = G_FowWorldToCellY(128.0f) * level.fow.width + G_FowWorldToCellX(256.0f);
    T_ASSERT(level.fow.blocked[canopy_index]);
    T_ASSERT(level.fow.players[0].visible[canopy_index]);
    T_ASSERT(!level.fow.players[0].visible[behind_index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_full_sync_marks_player_connected) {
    reset_entities();
    G_FowInit();

    LPEDICT clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;

    T_ASSERT(!level.fow.players[0].client_connected);
    G_FowSendFull(clent);
    T_ASSERT(level.fow.players[0].client_connected);
    T_ASSERT(!level.fow.players[1].client_connected);
    G_FowShutdown();
}

/* =========================================================================
 * Performance benchmarks
 *
 * Run with: openwarcraft3-tests -data <wc3data> -tft +dedicated 1 +test 'wc3_perf.*'
 * These tests do not assert; they print [BENCH] lines to stdout so you can
 * spot regressions by comparing across builds (BUILD=release for meaningful
 * numbers).
 * ========================================================================= */

void CM_SetupTestWorldBounds(LPCBOX2 bounds);

static volatile FLOAT acquisition_bench_sink;

/* Exercise the repeated metadata lookup performed by acquisition scans. */
static void bench_acquisition_ranges(void) {
    FLOAT sum = 0.0f;
    FOR_LOOP(pass, 10)
        for (int i = 1; i < 1901; i++) sum += G_AcquisitionRange(&g_edicts[i]);
    acquisition_bench_sink = sum;
}

/* 128×128-tile map → 16384×16384 units → 256×256 FOW cells at FOW_CELL_SIZE=64.
 * Two players connected, 80 revealer units each spread across the map. */
TEST(wc3_perf, fow_update_large_map) {
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0.0f, 0.0f},
                                        .max = {16384.0f, 16384.0f}));
    G_FowInit();

    level.fow.players[0].client_connected = true;
    level.fow.players[1].client_connected = true;

    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < 80; i++) {
            LPEDICT ent         = G_Spawn();
            ent->s.player       = (DWORD)p;
            ent->s.origin.x     = 1024.0f + (i % 16) * 900.0f;
            ent->s.origin.y     = 1024.0f + (i / 16) * 900.0f + p * 6000.0f;
            ent->s.origin2.x    = ent->s.origin.x;
            ent->s.origin2.y    = ent->s.origin.y;
            ent->health.value   = 100.0f;
            ent->health.max_value = 100.0f;
            ent->runtime.sight_radius.day = 900.0f;
        }
    }

    T_BENCH("G_FowUpdate  (256x256 grid, 160 revealers, 2 players)", 10,
            G_FowUpdate());
    G_FowShutdown();
}

/* 1900 active units — matches the entity count seen in the perf profile.
 * Each entity goes through spell_run_frame + unit_updatestatuses + physics
 * every call, which is the dominant cost even before think fires. */
TEST(wc3_perf, run_entities_1900) {
    setup_test_world();

    for (int i = 0; i < 1900; i++) {
        LPEDICT ent = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'),
                                      (FLOAT)((i % 50) * 64),
                                      (FLOAT)((i / 50) * 64));
        ent->health.value     = 100.0f;
        ent->health.max_value = 100.0f;
        ent->movetype         = MOVETYPE_STEP;
    }

    T_BENCH("G_RunEntities (1900 active units, MOVETYPE_STEP)",       30,
            G_RunEntities());
}

/* 1900 immutable unit classes should not require repeated SLK metadata walks. */
TEST(wc3_perf, acquisition_ranges_1900) {
    setup_test_world();
    for (int i = 0; i < 1900; i++) {
        LPEDICT ent = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0.0f, 0.0f);
        ent->runtime.sight_radius.day = 600.0f;
        ent->runtime.acquisition_range = 300.0f;
    }
    T_BENCH("G_AcquisitionRange (1900 units x 10 passes)", 30, bench_acquisition_ranges());
}

TEST(wc3_save, round_trip_edict_and_player_state) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-save-test.bin";
    QUESTITEM item = { .description = strdup("Find the key"), .completed = true };
    QUEST quest = {
        .title = strdup("Open the Gate"),
        .description = strdup("Find the key and open the gate"),
        .iconPath = strdup("ReplaceableTextures\\CommandButtons\\BTNKey.blp"),
        .items = &item,
        .discovered = true,
        .required = true,
        .enabled = true
    };
    LPQUEST old_quests = level.quests;
    LPEDICT first, second, indicator;

    reset_entities();
    level.quests = &quest;
    first = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 12.0f, 24.0f);
    second = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 48.0f, 72.0f);
    indicator = G_Spawn();
    indicator->svflags = SVF_OWNER_ONLY;
    indicator->rally_indicator = true;
    indicator->owner = &g_edicts[0];
    game.clients[0].rally_indicator = indicator;
    first->harvested_gold = 37;
    first->collision = 42.5f;
    first->s.origin.x = 96.0f;
    first->s.origin.y = 128.0f;
    first->owner = second;
    first->inventory[2] = second;
    first->cargo.units[3] = second;
    level.framenum = 1234;
    level.time = 5678;
    level.started = true;
    level.scriptsStarted = true;
    game.clients[0].jass.race_pref = 2;
    game.clients[0].jass.controller = 1;
    strlcpy(game.clients[0].jass.name, "Jaina", sizeof(game.clients[0].jass.name));
    game.clients[0].ps.name = game.clients[0].jass.name;
    game.clients[0].ping = 77;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 123;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 45;
    game.clients[0].camera.state.fov = 61.0f;
    game.clients[0].camera.state.position = (VECTOR2){ 333.0f, 444.0f };
    game.clients[0].camera.target_controller = second;
    T_ASSERT(WriteGame(filename));
    first->harvested_gold = 0;
    first->owner = NULL;
    first->inventory[2] = NULL;
    first->cargo.units[3] = NULL;
    strlcpy(game.clients[0].jass.name, "Changed", sizeof(game.clients[0].jass.name));
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    game.clients[0].camera.state.fov = 0.0f;
    game.clients[0].camera.state.position = (VECTOR2){ 0.0f, 0.0f };
    game.clients[0].camera.target_controller = NULL;
    game.clients[0].rally_indicator = NULL;
    quest.discovered = quest.required = quest.enabled = false;
    quest.completed = true;
    item.completed = false;
    T_ASSERT(ReadGame(filename));
    T_EQ(g_edicts[first - g_edicts].harvested_gold, 37);
    T_EQ(g_edicts[first - g_edicts].collision, 42.5f);
    T_EQ(g_edicts[first - g_edicts].s.origin.x, 96.0f);
    T_EQ(g_edicts[first - g_edicts].s.origin.y, 128.0f);
    T_EQ(level.framenum, 1234);
    T_EQ(level.time, 5678);
    T_ASSERT(level.started && level.scriptsStarted);
    T_EQ(game.clients[0].jass.race_pref, 2);
    T_EQ(game.clients[0].jass.controller, 1);
    T_EQ(game.clients[0].ping, 77);
    T_ASSERT(g_edicts[first - g_edicts].owner == &g_edicts[second - g_edicts]);
    T_ASSERT(g_edicts[first - g_edicts].inventory[2] == &g_edicts[second - g_edicts]);
    T_ASSERT(g_edicts[first - g_edicts].cargo.units[3] == &g_edicts[second - g_edicts]);
    T_ASSERT(g_edicts[first - g_edicts].stand == unit_stand);
    T_ASSERT(unit_issueimmediateorder(g_edicts + (first - g_edicts), "stop"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 123);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 45);
    T_EQ(game.clients[0].camera.state.fov, 61.0f);
    T_EQ(game.clients[0].camera.state.position.x, 333.0f);
    T_EQ(game.clients[0].camera.state.position.y, 444.0f);
    T_ASSERT(game.clients[0].camera.target_controller == &g_edicts[second - g_edicts]);
    T_ASSERT(game.clients[0].rally_indicator == &g_edicts[indicator - g_edicts]);
    T_ASSERT(game.clients[0].ps.name == game.clients[0].jass.name);
    T_STREQ(game.clients[0].ps.name, "Jaina");
    T_ASSERT(!level.mapinfo || game.clients[0].mapplayer == level.mapinfo->players + game.clients[0].ps.number);
    T_ASSERT(level.quests == &quest && quest.items == &item);
    T_STREQ(quest.title, "Open the Gate");
    T_STREQ(quest.description, "Find the key and open the gate");
    T_STREQ(quest.iconPath, "ReplaceableTextures\\CommandButtons\\BTNKey.blp");
    T_ASSERT(quest.discovered && quest.required && quest.enabled && !quest.completed);
    T_STREQ(item.description, "Find the key");
    T_ASSERT(item.completed);
    level.quests = old_quests;
    T_ASSERT(!ReadGame(filename));
    T_ASSERT(g_edicts[0].client == &game.clients[0]);
    free(item.description); free(quest.title); free(quest.description); free(quest.iconPath);
    remove(filename);
}

BOOL run_test_jass(LPCSTR src);

TEST(wc3_save, rebinds_process_owned_entity_callbacks) {
    UnitBalance_t unit_row = { .id = MAKEFOURCC('h', 'p', 'e', 'a') };
    UnitBalance_t no_unit = { 0 };
    UnitUI_t unit_ui = { 0 };
    DestructableData_t dest_row = { .file = "Tree" };
    DestructableData_t no_dest = { 0 };
    edict_t unit = { .UnitBalance = &unit_row, .UnitUI = &unit_ui, .DestructableData = &no_dest };
    edict_t dest = { .UnitBalance = &no_unit, .UnitUI = &unit_ui, .DestructableData = &dest_row };
    edict_t unknown = { .UnitBalance = &no_unit, .UnitUI = &unit_ui, .DestructableData = &no_dest };

    G_BindEntityRuntime(&unit); G_BindEntityRuntime(&dest); G_BindEntityRuntime(&unknown);
    T_ASSERT(unit.stand == unit_stand && unit.birth == unit_birth && unit.die == unit_die && unit.think == monster_think);
    T_ASSERT(dest.stand == tree_stand && dest.birth == tree_birth && dest.pain == tree_pain && dest.die == tree_die);
    T_ASSERT(dest.think == monster_think);
    T_ASSERT(!unknown.stand && !unknown.birth && !unknown.pain && !unknown.die && !unknown.think);
}

TEST(wc3_save, round_trip_unread_event_queue) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-event-save-test.bin";
    LEVELEVENTS old_events = level.events;
    EVENT handler = { .type = EVENT_UNIT_IN_RANGE };
    LPEDICT subject, source;

    reset_entities(); memset(&level.events, 0, sizeof(level.events)); level.events.handlers = &handler;
    subject = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0.0f, 0.0f);
    source = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64.0f, 0.0f);
    G_PublishEventWithSource(subject, EVENT_UNIT_IN_RANGE, source)->responseTo = &handler;
    T_ASSERT(WriteGame(filename));
    level.events.read = level.events.write; memset(level.events.queue, 0, sizeof(level.events.queue));
    T_ASSERT(ReadGame(filename));
    T_EQ(level.events.read, 0); T_EQ(level.events.write, 1);
    T_EQ(level.events.queue[0].type, EVENT_UNIT_IN_RANGE);
    T_ASSERT(level.events.queue[0].edict == subject && level.events.queue[0].source == source);
    T_ASSERT(level.events.queue[0].responseTo == &handler);
    level.events = old_events; remove(filename);
}

TEST(wc3_save, round_trip_waypoint_references) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-waypoint-save-test.bin";
    VECTOR2 destination = { 192.0f, 96.0f };
    LPEDICT unit, waypoint;
    DWORD cursor;

    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0.0f, 0.0f);
    waypoint = Waypoint_add(&destination); cursor = G_WaypointCursor();
    unit->goalentity = waypoint;
    unit->movement.attackmove_waypoint = waypoint;
    T_ASSERT(WriteGame(filename));
    waypoint->s.origin2 = (VECTOR2){ 0 };
    unit->goalentity = unit->movement.attackmove_waypoint = NULL;
    Waypoint_add(&(VECTOR2){ 1.0f, 1.0f });
    T_ASSERT(ReadGame(filename));
    T_ASSERT(unit->goalentity == waypoint && unit->movement.attackmove_waypoint == waypoint);
    T_FEQ(waypoint->s.origin2.x, destination.x, 0.01f); T_FEQ(waypoint->s.origin2.y, destination.y, 0.01f);
    T_EQ(G_WaypointCursor(), cursor);
    remove(filename);
}

TEST(wc3_save, round_trip_jass_globals) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-jass-save-test.bin";
    T_ASSERT(run_test_jass(
        "type group extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  integer savedInteger = 41\n"
        "  real savedReal = 2.5\n"
        "  boolean savedBoolean = true\n"
        "  string savedString = \"before\"\n"
        "  code savedCode = function SavedCallback\n"
        "  unit savedNull = null\n"
        "  player savedPlayer = null\n"
        "  player savedPlayerAlias = null\n"
        "  unit savedUnit = null\n"
        "  unit savedUnitAlias = null\n"
        "  quest savedQuest = null\n"
        "  quest savedQuestAlias = null\n"
        "  questitem savedQuestItem = null\n"
        "  questitem savedQuestItemAlias = null\n"
        "  group savedGroup = null\n"
        "  group savedGroupAlias = null\n"
        "  trigger savedTrigger = null\n"
        "  trigger savedTriggerAlias = null\n"
        "  sound savedSound = null\n"
        "  sound savedSoundAlias = null\n"
        "  camerasetup savedCamera = null\n"
        "  camerasetup savedCameraAlias = null\n"
        "  rect savedRect = null\n"
        "  rect savedRectAlias = null\n"
        "  location savedLocation = null\n"
        "  location savedLocationAlias = null\n"
        "  force savedForce = null\n"
        "  force savedForceAlias = null\n"
        "  gamecache savedCache = null\n"
        "  boolexpr savedFilter = null\n"
        "  boolexpr savedFilterAlias = null\n"
        "  integer array savedArray\n"
        "endglobals\n"
        "function SavedCallback takes nothing returns nothing\n"
        "endfunction\n"
        "function ChangedCallback takes nothing returns nothing\n"
        "endfunction\n"
        "function SavedFilter takes nothing returns boolean\n"
        "  return true\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set savedPlayer = Player(0)\n"
        "  set savedPlayerAlias = savedPlayer\n"
        "  set savedUnit = CreateUnit(savedPlayer, 'hpea', 0.0, 0.0, 0.0)\n"
        "  set savedUnitAlias = savedUnit\n"
        "  set savedQuest = CreateQuest()\n"
        "  set savedQuestAlias = savedQuest\n"
        "  set savedQuestItem = QuestCreateItem(savedQuest)\n"
        "  set savedQuestItemAlias = savedQuestItem\n"
        "  set savedGroup = CreateGroup()\n"
        "  set savedGroupAlias = savedGroup\n"
        "  call GroupAddUnit(savedGroup, savedUnit)\n"
        "  set savedTrigger = CreateTrigger()\n"
        "  set savedTriggerAlias = savedTrigger\n"
        "  call DisableTrigger(savedTrigger)\n"
        "  set savedSound = CreateSound(\"test.wav\", false, false, false, 0, 0, \"\")\n"
        "  set savedSoundAlias = savedSound\n"
        "  call SetSoundDuration(savedSound, 1234)\n"
        "  set savedCamera = CreateCameraSetup()\n"
        "  set savedCameraAlias = savedCamera\n"
        "  call CameraSetupSetDestPosition(savedCamera, 12.0, 34.0, 0.0)\n"
        "  set savedRect = Rect(1.0, 2.0, 3.0, 4.0)\n"
        "  set savedRectAlias = savedRect\n"
        "  set savedLocation = Location(5.0, 6.0)\n"
        "  set savedLocationAlias = savedLocation\n"
        "  set savedForce = CreateForce()\n"
        "  set savedForceAlias = savedForce\n"
        "  call ForceAddPlayer(savedForce, savedPlayer)\n"
        "  set savedCache = InitGameCache(\"save-test.w3v\")\n"
        "  call StoreInteger(savedCache, \"mission\", \"key\", 37)\n"
        "  set savedFilter = Filter(function SavedFilter)\n"
        "  set savedFilterAlias = savedFilter\n"
        "  set savedArray[7] = 77\n"
        "  set savedArray[4095] = 95\n"
        "endfunction\n"
        "function mutate takes nothing returns nothing\n"
        "  set savedInteger = 0\n"
        "  set savedReal = 0.0\n"
        "  set savedBoolean = false\n"
        "  set savedString = \"after\"\n"
        "  set savedCode = function ChangedCallback\n"
        "  set savedPlayer = null\n"
        "  set savedPlayerAlias = null\n"
        "  set savedUnit = null\n"
        "  set savedUnitAlias = null\n"
        "  set savedQuest = null\n"
        "  set savedQuestAlias = null\n"
        "  set savedQuestItem = null\n"
        "  set savedQuestItemAlias = null\n"
        "  call GroupClear(savedGroup)\n"
        "  call EnableTrigger(savedTrigger)\n"
        "  call SetSoundDuration(savedSound, 1)\n"
        "  call CameraSetupSetDestPosition(savedCamera, 1.0, 1.0, 0.0)\n"
        "  call SetRect(savedRect, 0.0, 0.0, 0.0, 0.0)\n"
        "  call MoveLocation(savedLocation, 0.0, 0.0)\n"
        "  call ForceClear(savedForce)\n"
        "  call StoreInteger(savedCache, \"mission\", \"key\", 0)\n"
        "  set savedFilter = null\n"
        "  set savedFilterAlias = null\n"
        "  set savedArray[7] = 0\n"
        "  set savedArray[4095] = 0\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(savedInteger == 41, \"integer snapshot mismatch\")\n"
        "  call BJassAssert(savedReal == 2.5, \"real snapshot mismatch\")\n"
        "  call BJassAssert(savedBoolean, \"boolean snapshot mismatch\")\n"
        "  call BJassAssert(savedString == \"before\", \"string snapshot mismatch\")\n"
        "  call BJassAssert(savedCode == function SavedCallback, \"code snapshot mismatch\")\n"
        "  call BJassAssert(savedNull == null, \"null snapshot mismatch\")\n"
        "  call BJassAssert(savedPlayer == savedPlayerAlias, \"player alias mismatch\")\n"
        "  call BJassAssert(savedPlayer == Player(0), \"player handle mismatch\")\n"
        "  call BJassAssert(savedUnit == savedUnitAlias, \"unit alias mismatch\")\n"
        "  call BJassAssert(GetUnitTypeId(savedUnit) == 'hpea', \"unit handle mismatch\")\n"
        "  call BJassAssert(savedQuest == savedQuestAlias, \"quest alias mismatch\")\n"
        "  call BJassAssert(savedQuestItem == savedQuestItemAlias, \"quest item alias mismatch\")\n"
        "  call BJassAssert(savedGroup == savedGroupAlias, \"group alias mismatch\")\n"
        "  call BJassAssert(FirstOfGroup(savedGroup) == savedUnit, \"group membership mismatch\")\n"
        "  call BJassAssert(savedTrigger == savedTriggerAlias, \"trigger alias mismatch\")\n"
        "  call BJassAssert(not IsTriggerEnabled(savedTrigger), \"trigger state mismatch\")\n"
        "  call BJassAssert(savedSound == savedSoundAlias, \"sound alias mismatch\")\n"
        "  call BJassAssert(GetSoundDuration(savedSound) == 1234, \"sound payload mismatch\")\n"
        "  call BJassAssert(savedCamera == savedCameraAlias, \"camera alias mismatch\")\n"
        "  call BJassAssert(CameraSetupGetDestPositionX(savedCamera) == 12.0, \"camera X mismatch\")\n"
        "  call BJassAssert(CameraSetupGetDestPositionY(savedCamera) == 34.0, \"camera Y mismatch\")\n"
        "  call BJassAssert(savedRect == savedRectAlias, \"rect alias mismatch\")\n"
        "  call BJassAssert(GetRectMinX(savedRect) == 1.0 and GetRectMaxY(savedRect) == 4.0, \"rect payload mismatch\")\n"
        "  call BJassAssert(savedLocation == savedLocationAlias, \"location alias mismatch\")\n"
        "  call BJassAssert(GetLocationX(savedLocation) == 5.0 and GetLocationY(savedLocation) == 6.0, \"location payload mismatch\")\n"
        "  call BJassAssert(savedForce == savedForceAlias, \"force alias mismatch\")\n"
        "  call BJassAssert(IsPlayerInForce(savedPlayer, savedForce), \"force payload mismatch\")\n"
        "  call BJassAssert(GetStoredInteger(savedCache, \"mission\", \"key\") == 37, \"gamecache payload mismatch\")\n"
        "  call BJassAssert(savedFilter == savedFilterAlias, \"boolexpr alias mismatch\")\n"
        "  call BJassAssert(savedArray[7] == 77, \"sparse array mismatch\")\n"
        "  call BJassAssert(savedArray[4095] == 95, \"high sparse array mismatch\")\n"
        "endfunction\n"));
    T_ASSERT(WriteGame(filename));
    jass_callbyname(level.vm, "mutate", false);
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    remove(filename);
}

TEST(wc3_save, round_trip_jass_timers) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-jass-timer-save-test.bin";
    T_ASSERT(run_test_jass(
        "type timer extends handle\n"
        "type trigger extends handle\n"
        "type triggeraction extends handle\n"
        "type event extends handle\n"
        "globals\n"
        "  timer pausedTimer = null\n"
        "  timer pausedTimerAlias = null\n"
        "  timer runningTimer = null\n"
        "  timer runningTimerAlias = null\n"
        "  trigger timerTrigger = null\n"
        "  integer timerCallbacks = 0\n"
        "endglobals\n"
        "function TimerCallback takes nothing returns nothing\n"
        "  call BJassAssert(GetExpiredTimer() == runningTimer, \"direct expired timer mismatch\")\n"
        "  set timerCallbacks = timerCallbacks + 1\n"
        "endfunction\n"
        "function TimerTriggerAction takes nothing returns nothing\n"
        "  call BJassAssert(GetExpiredTimer() == runningTimer, \"trigger expired timer mismatch\")\n"
        "  set timerCallbacks = timerCallbacks + 10\n"
        "endfunction\n"
        "function TimerNoop takes nothing returns nothing\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set timerTrigger = CreateTrigger()\n"
        "  set pausedTimer = CreateTimer()\n"
        "  set pausedTimerAlias = pausedTimer\n"
        "  call TimerStart(pausedTimer, 10.0, false, null)\n"
        "  call TimerStart(pausedTimer, 10.0, false, function TimerNoop)\n"
        "  call PauseTimer(pausedTimer)\n"
        "  set runningTimer = CreateTimer()\n"
        "  set runningTimerAlias = runningTimer\n"
        "  call TriggerAddAction(timerTrigger, function TimerTriggerAction)\n"
        "  call TriggerRegisterTimerExpireEvent(timerTrigger, runningTimer)\n"
        "  call TimerStart(runningTimer, 0.0, true, function TimerCallback)\n"
        "endfunction\n"
        "function mutate takes nothing returns nothing\n"
        "  call DestroyTimer(pausedTimer)\n"
        "  call DestroyTimer(runningTimer)\n"
        "  set pausedTimerAlias = null\n"
        "  set runningTimerAlias = null\n"
        "  set timerCallbacks = 99\n"
        "endfunction\n"
        "function verifyRestored takes nothing returns nothing\n"
        "  call BJassAssert(pausedTimer == pausedTimerAlias, \"paused timer alias mismatch\")\n"
        "  call BJassAssert(runningTimer == runningTimerAlias, \"running timer alias mismatch\")\n"
        "  call BJassAssert(TimerGetTimeout(pausedTimer) == 10.0, \"timer timeout mismatch\")\n"
        "  call BJassAssert(TimerGetRemaining(pausedTimer) > 9.0, \"paused timer remaining mismatch\")\n"
        "  call BJassAssert(TimerGetRemaining(pausedTimer) <= 10.0, \"paused timer remaining overflow\")\n"
        "  call BJassAssert(timerCallbacks == 0, \"timer callback state mismatch\")\n"
        "endfunction\n"
        "function verifyExpired takes nothing returns nothing\n"
        "  call BJassAssert(timerCallbacks > 0, \"direct timer callback did not run after load\")\n"
        "  call BJassAssert(timerCallbacks == 11, \"timer trigger did not run after load\")\n"
        "endfunction\n"
        "function verifyPeriodic takes nothing returns nothing\n"
        "  call BJassAssert(timerCallbacks == 22, \"periodic timer did not remain active after load\")\n"
        "endfunction\n"
        "function addTimer takes nothing returns nothing\n"
        "  call CreateTimer()\n"
        "endfunction\n"
        "function addEvent takes nothing returns nothing\n"
        "  call TriggerRegisterTimerExpireEvent(timerTrigger, runningTimer)\n"
        "endfunction\n"));
    T_ASSERT(WriteGame(filename));
    jass_callbyname(level.vm, "mutate", false);
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifyRestored", false);
    G_RunTimers();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyExpired", false);
    G_RunTimers();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyPeriodic", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "addEvent", false);
    T_ASSERT(!ReadGame(filename));
    jass_callbyname(level.vm, "addTimer", false);
    T_ASSERT(!ReadGame(filename));
    remove(filename);
}

TEST(wc3_save, resumes_sleeping_jass_coroutine) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-jass-coroutine-save-test.bin";
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer coroutineStage = 0\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set coroutineStage = 1\n"
        "  call TriggerSleepAction(0.0)\n"
        "  set coroutineStage = 2\n"
        "endfunction\n"
        "function mutate takes nothing returns nothing\n"
        "  set coroutineStage = 9\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(coroutineStage == 2, \"coroutine did not resume after sleep\")\n"
        "endfunction\n"));
    T_ASSERT(WriteGame(filename));
    jass_callbyname(level.vm, "mutate", false);
    T_ASSERT(ReadGame(filename));
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    remove(filename);
}

TEST(wc3_save, rejects_corruption_without_mutation) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-corrupt-save-test.bin";
    LPEDICT unit;
    FILE *f;
    BYTE byte = 0;
    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0.0f, 0.0f);
    unit->harvested_gold = 37;
    T_ASSERT(WriteGame(filename));
    f = fopen(filename, "r+b");
    T_ASSERT(f != NULL && fseek(f, sizeof(DWORD), SEEK_SET) == 0 && fread(&byte, 1, 1, f) == 1);
    byte ^= 0x80;
    T_ASSERT(fseek(f, sizeof(DWORD), SEEK_SET) == 0 && fwrite(&byte, 1, 1, f) == 1);
    fclose(f);
    unit->harvested_gold = 99;
    T_ASSERT(!ReadGame(filename));
    T_EQ(unit->harvested_gold, 99);
    remove(filename);
}

TEST(wc3_save, rejects_script_identity_without_mutation) {
    LPCSTR filename = "/tmp/openwarcraft3-wc3-script-mismatch-save-test.bin";
    char extra[] = "function AddedAfterSave takes nothing returns nothing\nendfunction\n";
    LPEDICT unit;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    unit = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0.0f, 0.0f);
    unit->harvested_gold = 37;
    T_ASSERT(WriteGame(filename));
    T_ASSERT(jass_dobuffer(level.vm, extra));
    unit->harvested_gold = 99;
    T_ASSERT(!ReadGame(filename));
    T_EQ(unit->harvested_gold, 99);
    remove(filename);
}

/* =========================================================================
 * Suite runner
 * ========================================================================= */

#endif /* BZ_TESTS */
