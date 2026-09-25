/* Server side of the UI canvas contract (docs/architecture/ui-canvas.md): widescreen console tiles are
 * registered and authored for wide clients only, and the ui_canvas command re-authors the console through
 * the real per-frame scheduler. */
#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"

BOOL run_test_jass(LPCSTR src);

typedef struct {
    PATHSTR images[MAX_IMAGES];
    DWORD tiles, wide_tiles, console_layouts, unicasts;
    LONG layer;
    BOOL layer_pending, in_console;
    stbIniCache_t saved_theme;
    __typeof__(gi.Write) write;
    __typeof__(gi.unicast) unicast;
    int (*image)(LPCSTR);
    LPCSTR (*configstring)(DWORD);
} CANVASCAP;
static CANVASCAP cap;

static int canvas_image_index(LPCSTR name) {
    if (!name || !*name) return 0;
    for (DWORD i = 1; i < MAX_IMAGES; i++) if (cap.images[i][0] && !strcmp(cap.images[i], name)) return (int)i;
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (cap.images[i][0]) continue;
        snprintf(cap.images[i], sizeof(cap.images[i]), "%s", name);
        return (int)i;
    }
    return 0;
}
static LPCSTR canvas_configstring(DWORD index) {
    return index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES ? cap.images[index - CS_IMAGES] : "";
}
static BOOL canvas_registered(LPCSTR needle) {
    for (DWORD i = 1; i < MAX_IMAGES; i++) if (cap.images[i][0] && strstr(cap.images[i], needle)) return true;
    return false;
}
/* Count console tiles by the skin path they resolved to, so the recipient's race is part of the evidence. */
static void canvas_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_BYTE) {
        LONG byte = *(LONG const *)value;
        if (cap.layer_pending) {
            cap.layer = byte; cap.layer_pending = false; cap.in_console = byte == LAYER_CONSOLE;
            if (cap.in_console) cap.console_layouts++;
        } else if (byte == svc_layout) cap.layer_pending = true;
        return;
    }
    if (type != PF_UIFRAME || !cap.in_console) return;
    LPCUIFRAME frame = value;
    if (frame->flags.type != FT_TEXTURE || !frame->tex.index || frame->tex.index >= MAX_IMAGES) return;
    LPCSTR name = cap.images[frame->tex.index];
    if (!strstr(name, "-tile0")) return;
    cap.tiles++;
    if (strstr(name, "tile05") || strstr(name, "tile06")) cap.wide_tiles++;
}
static void canvas_unicast(LPEDICT ent) { (void)ent; cap.unicasts++; cap.in_console = false; }

static void canvas_reset_counts(void) {
    cap.tiles = cap.wide_tiles = cap.console_layouts = cap.unicasts = 0;
    cap.in_console = false;
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

static void write_console(LPEDICT ent) {
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
        LPCFRAMEDEF frame = hud.console_wide[i];
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

TEST(wc3_canvas, console_write_authors_extension_tiles_for_wide_clients_only) {
    LPEDICT ent = &g_edicts[0];
    LPGAMECLIENT client = ent->client;
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
    LPEDICT ent = &g_edicts[0];
    LPGAMECLIENT client = ent->client;
    LPCSTR rejected[] = { "2", "-1", "x", "1x", "" };
    canvas_setup();
    client->connected = true;

    G_ClientCommand(ent, 2, (LPCSTR[]){ "ui_canvas", "1" });
    T_EQ(client->canvas, UI_CANVAS_WIDE);
    G_ClientCommand(ent, 2, (LPCSTR[]){ "ui_canvas", "0" });
    T_EQ(client->canvas, UI_CANVAS_STANDARD);
    FOR_LOOP(i, sizeof(rejected) / sizeof(rejected[0])) {
        client->canvas = UI_CANVAS_WIDE;
        G_ClientCommand(ent, 2, (LPCSTR[]){ "ui_canvas", rejected[i] });
        T_EQ(client->canvas, UI_CANVAS_WIDE);
    }
    G_ClientCommand(ent, 1, (LPCSTR[]){ "ui_canvas" });
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
    G_ClientCommand(ent, 2, (LPCSTR[]){ "ui_canvas", "1" });
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 1); T_EQ(cap.wide_tiles, 4);
    canvas_reset_counts();
    globals.RunFrame();
    T_EQ(cap.console_layouts, 0);
    canvas_teardown();
}
#endif
