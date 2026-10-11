/* Server side of the UI canvas contract (docs/architecture/ui-canvas.md): widescreen console tiles are
 * registered and authored for wide clients only, and the ui_canvas command re-authors the console through
 * the real per-frame scheduler. */
#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"

bool run_test_jass(cstring_t src);

typedef struct {
    PATHSTR images[MAX_IMAGES];
    uint32_t tiles, wide_tiles, console_layouts, unicasts;
    char cinematic_texture_path[PATH_MAX];
    int32_t layer;
    bool layer_pending, in_console;
    stbIniCache_t saved_theme;
    __typeof__(gi.Write) write;
    __typeof__(gi.unicast) unicast;
    int (*image)(cstring_t);
    cstring_t (*configstring)(uint32_t);
} canvasCap_t;
static canvasCap_t cap;
static cstring_t const canvas_esc_menu_keys[] = {
    "EscMenuBackground",
    "EscMenuButtonBackground",
    "EscMenuButtonPushedBackground",
    "EscMenuButtonDisabledBackground",
    "EscMenuButtonDisabledPushedBackground",
    "EscMenuButtonBorder",
    "EscMenuButtonPushedBorder",
    "EscMenuButtonDisabledBorder",
    "EscMenuButtonDisabledPushedBorder",
    "EscMenuButtonMouseOverHighlight",
};

static int canvas_image_index(cstring_t name) {
    if (!name || !*name) return 0;
    for (uint32_t i = 1; i < MAX_IMAGES; i++) if (cap.images[i][0] && !strcmp(cap.images[i], name)) return (int)i;
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (cap.images[i][0]) continue;
        snprintf(cap.images[i], sizeof(cap.images[i]), "%s", name);
        return (int)i;
    }
    return 0;
}
static cstring_t canvas_configstring(uint32_t index) {
    return index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES ? cap.images[index - CS_IMAGES] : "";
}
static bool canvas_registered(cstring_t needle) {
    for (uint32_t i = 1; i < MAX_IMAGES; i++) if (cap.images[i][0] && strstr(cap.images[i], needle)) return true;
    return false;
}
/* Count console tiles by the skin path they resolved to, so the recipient's race is part of the evidence. */
static void canvas_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_BYTE) {
        int32_t byte = *(int32_t const *)value;
        if (cap.layer_pending) {
            cap.layer = byte; cap.layer_pending = false; cap.in_console = byte == LAYER_CONSOLE;
            if (cap.in_console) cap.console_layouts++;
        } else if (byte == svc_layout) cap.layer_pending = true;
        return;
    }
    if (type != PF_UIFRAME || (!cap.in_console && cap.layer != LAYER_CINEMATIC)) return;
    uiFrame_t const *frame = value;
    if (!cap.in_console && frame->flags.type == FT_TEXTURE && frame->tex.index < MAX_IMAGES) {
        snprintf(cap.cinematic_texture_path, sizeof(cap.cinematic_texture_path), "%s", cap.images[frame->tex.index]);
        return;
    }
    if (frame->flags.type != FT_TEXTURE || !frame->tex.index || frame->tex.index >= MAX_IMAGES) return;
    cstring_t name = cap.images[frame->tex.index];
    if (!strstr(name, "-tile0")) return;
    cap.tiles++;
    if (strstr(name, "tile05") || strstr(name, "tile06")) cap.wide_tiles++;
}
static void canvas_unicast(edict_t *ent) { (void)ent; cap.unicasts++; cap.in_console = false; }

static void canvas_reset_counts(void) {
    cap.tiles = cap.wide_tiles = cap.console_layouts = cap.unicasts = 0;
    cap.in_console = false;
    cap.layer_pending = false;
    cap.cinematic_texture_path[0] = '\0';
}

/* The fixture archive carries the retail 1.30 ConsoleUI.fdf plus ResourceBar/UpperButtonBar and a skin with
 * per-race tiles, so the real parse and write paths run against authored data. */
static void canvas_setup(void) {
    cap.write = gi.Write; cap.unicast = gi.unicast;
    cap.image = gi.ImageIndex; cap.configstring = gi.GetConfigstring;
    cap.saved_theme = game.config.theme;
    memset(cap.images, 0, sizeof(cap.images));
    canvas_reset_counts();
    gi.Write = canvas_write; gi.unicast = canvas_unicast;
    gi.ImageIndex = canvas_image_index; gi.GetConfigstring = canvas_configstring;
    memset(&game.config.theme, 0, sizeof(game.config.theme));
    T_ASSERT(Stb_IniCacheLoad(&game.config.theme, "UI\\war3skins.txt"));
    UI_ResetHud();
    UI_LoadHudConsole();
}

static void canvas_teardown(void) {
    UI_ResetHud();
    Stb_IniCacheFree(&game.config.theme);
    game.config.theme = cap.saved_theme;
    gi.Write = cap.write; gi.unicast = cap.unicast; gi.ImageIndex = cap.image; gi.GetConfigstring = cap.configstring;
}

static void write_console(edict_t *ent) {
    UI_WriteStart(LAYER_CONSOLE);
    UI_WriteConsoleBackdrop(ent->client, 10, 20);
    UI_WriteMinimapFrame();
    UI_WriteEnd(ent);
}

TEST(wc3_canvas, console_load_defers_widescreen_tiles_and_collects_their_frames) {
    canvas_setup();
    T_NOT_NULL(hud.console.ConsoleUI);
    T_EQ(hud.console_wide_count, 4);
    FOR_LOOP(i, hud.console_wide_count) {
        frameDef_t const *frame = hud.console_wide[i];
        T_ASSERT(frame->Texture.Image >= HUD_DEFERRED_IMAGE_BASE);
        T_ASSERT(UI_IsWideChromeKey(UI_ImageKey(frame->Texture.Image)));
        T_ASSERT(frame->Parent == hud.console.ConsoleUI);
    }
    /* The 4:3 tiles precache as before; the extension art waits for a wide client. */
    T_ASSERT(canvas_registered("human-tile01"));
    T_ASSERT(!canvas_registered("tile05") && !canvas_registered("tile06"));
    T_ASSERT(!UI_IsWideChromeKey("ConsoleTexture01") && UI_IsWideChromeKey("ConsoleTexture06"));
    T_STREQ(UI_ImageKey(HUD_DEFERRED_IMAGE_BASE + HUD_DEFERRED_IMAGES), "");
    canvas_teardown();
}

TEST(wc3_canvas, esc_menu_art_resolves_for_each_race_and_fits_deferred_table) {
    gameClient_t client = { 0 };
    gameClient_t *saved_client = ui_current_client;
    char skin_text[4096];
    size_t skin_size = 0;
    uint32_t image[sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0])];

    canvas_setup();

    Stb_IniCacheFree(&game.config.theme);
    memset(&game.config.theme, 0, sizeof(game.config.theme));
    skin_size += (size_t)snprintf(skin_text + skin_size, sizeof(skin_text) - skin_size, "[NightElf]\n");
    FOR_LOOP(i, sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0]))
        skin_size += (size_t)snprintf(skin_text + skin_size, sizeof(skin_text) - skin_size,
            "%s=TestUI\\Textures\\nightelf-%u.blp\n", canvas_esc_menu_keys[i], (unsigned)i);
    skin_size += (size_t)snprintf(skin_text + skin_size, sizeof(skin_text) - skin_size, "\n[Undead]\n");
    FOR_LOOP(i, sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0]))
        skin_size += (size_t)snprintf(skin_text + skin_size, sizeof(skin_text) - skin_size,
            "%s=TestUI\\Textures\\undead-%u.blp\n", canvas_esc_menu_keys[i], (unsigned)i);
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.theme, skin_text));

    FOR_LOOP(i, sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0])) {
        image[i] = UI_LoadTexture(canvas_esc_menu_keys[i], true);
        T_ASSERT(image[i] >= HUD_DEFERRED_IMAGE_BASE);
        T_ASSERT(image[i] < HUD_DEFERRED_IMAGE_BASE + HUD_DEFERRED_IMAGES);
        T_STREQ(UI_ImageKey(image[i]), canvas_esc_menu_keys[i]);
        FOR_LOOP(j, i) T_ASSERT(image[i] != image[j]);
    }
    /* ConsoleUI.fdf already occupies two deferred slots; all ten EscMenu keys must still fit. */
    T_STREQ(UI_ImageKey(HUD_DEFERRED_IMAGE_BASE + 12), "");

    UI_SetCurrentClient(&client);
    client.ps.race = kPlayerRaceNightElf;
    FOR_LOOP(i, sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0])) {
        uint32_t live = UI_LiveImage(image[i]);
        T_ASSERT(live > 0 && live < MAX_IMAGES);
        T_STREQ(cap.images[live], UI_ThemeImagePath(canvas_esc_menu_keys[i]));
        T_ASSERT(strstr(cap.images[live], "nightelf-") != NULL);
    }
    client.ps.race = kPlayerRaceUndead;
    FOR_LOOP(i, sizeof(canvas_esc_menu_keys) / sizeof(canvas_esc_menu_keys[0])) {
        uint32_t live = UI_LiveImage(image[i]);
        T_ASSERT(live > 0 && live < MAX_IMAGES);
        T_STREQ(cap.images[live], UI_ThemeImagePath(canvas_esc_menu_keys[i]));
        T_ASSERT(strstr(cap.images[live], "undead-") != NULL);
    }
    UI_SetCurrentClient(saved_client);

    canvas_teardown();
}

TEST(wc3_canvas, cinematic_layer_restores_previous_skin_context) {
    gameClient_t client = { 0 }, previous = { 0 };
    edict_t ent = { 0 };
    frameDef_t *root = NULL, *scene = NULL, *portrait = NULL, *texture = NULL;
    char skin_text[512];

    canvas_setup();
    Stb_IniCacheFree(&game.config.theme);
    memset(&game.config.theme, 0, sizeof(game.config.theme));
    snprintf(skin_text, sizeof(skin_text),
             "[NightElf]\nEscMenuBackground=TestUI\\Textures\\nightelf-cinematic.blp\n"
             "\n[Undead]\nEscMenuBackground=TestUI\\Textures\\undead-cinematic.blp\n");
    T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.theme, skin_text));
    client.ps.race = kPlayerRaceNightElf;
    ent.client = &client;
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!frames[i].inuse) {
            if (!root) root = frames + i;
            else if (!scene) scene = frames + i;
            else if (!portrait) portrait = frames + i;
            else { texture = frames + i; break; }
        }
    }
    T_NOT_NULL(root); T_NOT_NULL(scene); T_NOT_NULL(portrait); T_NOT_NULL(texture);
    UI_InitFrame(root, FT_FRAME);
    UI_InitFrame(scene, FT_FRAME);
    UI_InitFrame(portrait, FT_PORTRAIT);
    UI_InitFrame(texture, FT_TEXTURE);
    snprintf(root->Name, sizeof(root->Name), "CinematicPanel");
    snprintf(scene->Name, sizeof(scene->Name), "CinematicScenePanel");
    snprintf(portrait->Name, sizeof(portrait->Name), "CinematicPortrait");
    snprintf(texture->Name, sizeof(texture->Name), "CinematicTestTexture");
    scene->Parent = root;
    portrait->Parent = scene;
    texture->Parent = scene;
    texture->Texture.Image = UI_LoadTexture("EscMenuBackground", true);
    hud.cinematic.CinematicPanel = root;
    hud.cinematic.CinematicScenePanel = scene;
    hud.cinematic.CinematicPortraitBackground = NULL;
    hud.cinematic.CinematicPortrait = portrait;
    hud.cinematic.CinematicPortraitCover = NULL;
    hud.cinematic.CinematicSpeakerText = NULL;
    hud.cinematic.CinematicDialogueText = NULL;
    canvas_reset_counts();
    client.ps.cinematic_portrait = 1;
    ui_current_client = &previous;
    UI_WriteCinematicLayer(&ent);
    T_ASSERT(strstr(cap.cinematic_texture_path, "nightelf-cinematic.blp") != NULL);
    T_EQ(ui_current_client, &previous);
    ui_current_client = NULL;
    memset(&hud.cinematic, 0, sizeof(hud.cinematic));
    canvas_teardown();
}

TEST(wc3_canvas, console_write_authors_extension_tiles_for_wide_clients_only) {
    edict_t *ent = &g_edicts[0];
    gameClient_t *client = ent->client;
    canvas_setup();
    client->connected = true;
    client->ps.race = kPlayerRaceOrc;

    client->canvas = UI_CANVAS_STANDARD;
    write_console(ent);
    T_EQ(cap.layer, LAYER_CONSOLE); T_EQ(cap.unicasts, 1);
    T_EQ(cap.tiles, 9); T_EQ(cap.wide_tiles, 0);
    T_ASSERT(canvas_registered("orc-tile01"));
    T_ASSERT(!canvas_registered("tile05") && !canvas_registered("tile06"));

    canvas_reset_counts();
    client->canvas = UI_CANVAS_WIDE;
    write_console(ent);
    T_EQ(cap.tiles, 13); T_EQ(cap.wide_tiles, 4);
    /* Resolved for the recipient's race at write time, never through the parse-time Default section. */
    T_ASSERT(canvas_registered("orc-tile05") && canvas_registered("orc-tile06"));
    T_ASSERT(!canvas_registered("human-tile05"));

    /* Back to standard: the frames leave the layout; CS_IMAGES slots stay occupied until the next level. */
    canvas_reset_counts();
    client->canvas = UI_CANVAS_STANDARD;
    write_console(ent);
    T_EQ(cap.tiles, 9); T_EQ(cap.wide_tiles, 0);
    T_ASSERT(canvas_registered("orc-tile05"));
    canvas_teardown();
}

TEST(wc3_canvas, ui_canvas_command_validates_class_and_resends_console_through_run_frame) {
    edict_t *ent = &g_edicts[0];
    gameClient_t *client = ent->client;
    cstring_t rejected[] = { "2", "-1", "x", "1x", "" };
    canvas_setup();
    client->connected = true;

    G_ClientCommand(ent, 2, (cstring_t[]){ "ui_canvas", "1" });
    T_EQ(client->canvas, UI_CANVAS_WIDE);
    G_ClientCommand(ent, 2, (cstring_t[]){ "ui_canvas", "0" });
    T_EQ(client->canvas, UI_CANVAS_STANDARD);
    FOR_LOOP(i, sizeof(rejected) / sizeof(rejected[0])) {
        client->canvas = UI_CANVAS_WIDE;
        G_ClientCommand(ent, 2, (cstring_t[]){ "ui_canvas", rejected[i] });
        T_EQ(client->canvas, UI_CANVAS_WIDE);
    }
    G_ClientCommand(ent, 1, (cstring_t[]){ "ui_canvas" });
    T_EQ(client->canvas, UI_CANVAS_WIDE);
    client->canvas = UI_CANVAS_STANDARD;

    /* Real scheduler: the first frame authors the console, a quiet frame re-sends nothing, and the class
     * change alone re-sends it. */
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started = level.scriptsStarted = true;
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 1); T_EQ(cap.wide_tiles, 0);
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 0);
    G_ClientCommand(ent, 2, (cstring_t[]){ "ui_canvas", "1" });
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 1); T_EQ(cap.wide_tiles, 4);
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 0);
    canvas_teardown();
}
#endif
