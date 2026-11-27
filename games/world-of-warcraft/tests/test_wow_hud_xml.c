#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games/world-of-warcraft/menu/menu_local.h"
#include "common/mpq.h"

/* Reuse the same test harness pattern as test_wow_ui.c. */

#ifndef TEST_WOW_MPQ
#define TEST_WOW_MPQ "build/tests/test-wow.mpq"
#endif

struct texture { uint32_t texid; uint32_t width; uint32_t height; char name[256]; };
struct font    { uint32_t size; char name[256]; };

static handle_t test_archive;
static refExport_t test_renderer;
static player_t test_ps;
static texture_t const *test_textures[MAX_IMAGES];
static uint32_t next_texture_id;
static int geometry_warnings;
static drawText_t last_draw_text;
static char last_draw_text_value[128];
static char last_draw_image_name[256];
static uint32_t draw_text_calls;
static bool test_missing_font;
static uint32_t test_missing_font_loads;

static cstring_t test_get_configstring(uint32_t index) {
    return index == WOW_CS_MAPINFO ? "\\title\\The Barrens\\preview\\normal.blp" : "";
}

static int test_fs_read_file(cstring_t fileName, void **buf) {
    handle_t file; uint32_t size, read = 0;
    if (!buf) return -1; *buf = NULL;
    if (!test_archive ||
        !SFileOpenFileEx(test_archive, fileName, SFILE_OPEN_FROM_MPQ, &file)) return -1;
    size = SFileGetFileSize(file, NULL);
    *buf = calloc(1, (size_t)size + 1);
    if (!*buf || !SFileReadFile(file, *buf, size, &read, NULL) || read != size) {
        free(*buf); *buf = NULL; SFileCloseFile(file); return -1;
    }
    SFileCloseFile(file);
    return (int)size;
}
static void test_fs_free_file(void *buf) { free(buf); }
static handle_t test_mem_alloc(long sz) { return calloc(1, (size_t)sz); }
static void   test_mem_free(handle_t m) { free(m); }
static void test_printf(cstring_t fmt, ...) {
    char msg[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    if (strstr(msg, "UIWow: unresolved FrameXML geometry")) geometry_warnings++;
}

static texture_t *test_load_texture(cstring_t name) {
    texture_t *t = calloc(1, sizeof(*t));
    T_NOT_NULL(t);
    t->texid = ++next_texture_id;
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "");
    return t;
}
static font_t *test_load_font(cstring_t name, uint32_t sz) {
    if (test_missing_font && !strcmp(name, "Fonts\\Missing.ttf")) {
        test_missing_font_loads++;
        return NULL;
    }
    font_t *f = calloc(1, sizeof(*f));
    T_NOT_NULL(f);
    f->size = sz;
    snprintf(f->name, sizeof(f->name), "%s", name ? name : "");
    return f;
}
static void     test_release_texture(texture_t *t) { free(t); }
static size2_t  test_get_texture_size(texture_t const *t) { size2_t s = {0,0}; if(t){s.width=t->width;s.height=t->height;} return s; }
static void     test_draw_image(texture_t const *t, rect_t const *s, rect_t const *u, color32_t c) { (void)t;(void)s;(void)u;(void)c; }
static void test_draw_image_ex(drawImage_t const *i) {
    snprintf(last_draw_image_name, sizeof(last_draw_image_name), "%s", i && i->texture ? i->texture->name : "");
}
static void     test_draw_fill(rect_t const *r, color32_t c) { (void)r;(void)c; }
static void     test_draw_minimap(rect_t const *r, cstring_t map) { (void)r; (void)map; }
static vec2_t  test_get_text_size(drawText_t const *dt) { return MAKE(vec2_t, dt&&dt->text?(float)strlen(dt->text)*0.01f:0.0f, 0.012f); }
static void test_draw_text(drawText_t const *dt) {
    draw_text_calls++;
    if (!dt) return;
    last_draw_text = *dt;
    snprintf(last_draw_text_value, sizeof(last_draw_text_value), "%s", dt->text ? dt->text : "");
    last_draw_text.text = last_draw_text_value;
}
static texture_t const *test_get_texture(uint32_t i) { return i < MAX_IMAGES ? test_textures[i] : NULL; }
static int test_image_index(cstring_t n) {
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (!test_textures[i]) { test_textures[i] = test_load_texture(n); return (int)i; }
    }
    return 0;
}
static refExport_t *test_get_renderer(void) { return &test_renderer; }

static void reset_state(void) {
    memset(&test_ps, 0, sizeof(test_ps));
    memset(test_textures, 0, sizeof(test_textures));
    memset(&test_renderer, 0, sizeof(test_renderer));
    next_texture_id = 0;
    geometry_warnings = 0;
    draw_text_calls = 0;
    test_missing_font = false;
    test_missing_font_loads = 0;
    memset(&last_draw_text, 0, sizeof(last_draw_text));
    last_draw_text_value[0] = '\0';
    last_draw_image_name[0] = '\0';
    test_ps.client_ui_state = CLIENT_UI_GAME;
    test_renderer.LoadTexture     = test_load_texture;
    test_renderer.LoadFont        = test_load_font;
    test_renderer.ReleaseTexture  = test_release_texture;
    test_renderer.GetTextureSize  = test_get_texture_size;
    test_renderer.DrawImage       = test_draw_image;
    test_renderer.DrawImageEx     = test_draw_image_ex;
    test_renderer.DrawFill        = test_draw_fill;
    test_renderer.DrawMinimap     = test_draw_minimap;
    test_renderer.DrawText        = test_draw_text;
    test_renderer.GetTextSize     = test_get_text_size;
}

static menuExport_t init_ui(void) {
    menuExport_t menu = M_GetAPI((menuImport_t){
        .FS_ReadFile   = test_fs_read_file,
        .FS_FreeFile   = test_fs_free_file,
        .MemAlloc      = test_mem_alloc,
        .MemFree       = test_mem_free,
        .ImageIndex    = test_image_index,
        .GetConfigString = test_get_configstring,


        .GetRenderer   = test_get_renderer,
        .Printf        = test_printf,
    });
    T_NOT_NULL(menu.Init);
    /* Do NOT call menu.Init() — that spins up the Lua glue state which
     * requires the full MPQ.  We only need the XML runtime here. */
    UIWow_XMLInitRuntime();
    return menu;
}

/* -------------------------------------------------------------------------- */
/* Unit tests — parse from static XML buffers (no MPQ needed)                 */
/* -------------------------------------------------------------------------- */

TEST(wow_hud_xml, parse_basic_frame) {
    static const char xml[] = "<Ui><Frame name=\"TestFrame\"/></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test"));
    idx = UIWow_XmlFindByNamePub("TestFrame");
    T_ASSERT(idx >= 0);
    T_EQ(UIWow_XmlElemType(idx), (int)WOW_XML_FRAME);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, parse_fontstring_type_and_text) {
    static const char xml[] =
        "<Ui><FontString name=\"FS\" text=\"Hello World\"/></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("FS");
    T_ASSERT(idx >= 0);
    T_EQ(UIWow_XmlElemType(idx), (int)WOW_XML_FONTSTRING);
    T_STREQ(UIWow_XmlElemText(idx), "Hello World");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, font_inheritance_draws_authored_typeface_and_height) {
    static const char xml[] =
        "<Ui><Font name=\"BaseFont\" font=\"Fonts\\Base.ttf\" virtual=\"true\">"
        "<FontHeight><AbsValue val=\"19\"/></FontHeight><Color r=\"1\" g=\"0\" b=\"0\"/></Font>"
        "<Font name=\"ChildFont\" inherits=\"BaseFont\" virtual=\"true\"/>"
        "<FontString name=\"Message\" inherits=\"ChildFont\" text=\"Hello\"/>"
        "<FontString name=\"Override\" inherits=\"ChildFont\" font=\"Fonts\\Other.ttf\" text=\"Hi\">"
        "<FontHeight><AbsValue val=\"21\"/></FontHeight></FontString></Ui>";
    reset_state(); init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, sizeof(xml) - 1, "fonts"));
    T_ASSERT(UIWow_XMLDrawFrame("Message"));
    T_NOT_NULL(last_draw_text.font);
    if (last_draw_text.font) {
        T_STREQ(last_draw_text.font->name, "Fonts\\Base.ttf");
        T_EQ(last_draw_text.font->size, 19);
    }
    T_EQ(last_draw_text.color.r, 255);
    T_EQ(last_draw_text.color.g, 0);
    T_ASSERT(UIWow_XMLDrawFrame("Override"));
    T_NOT_NULL(last_draw_text.font);
    if (last_draw_text.font) {
        T_STREQ(last_draw_text.font->name, "Fonts\\Other.ttf");
        T_EQ(last_draw_text.font->size, 21);
    }
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, font_lua_methods_and_missing_font_fallback) {
    static const char xml[] =
        "<Ui><Font name=\"BaseFont\" font=\"Fonts\\Base.ttf\">"
        "<FontHeight><AbsValue val=\"19\"/></FontHeight></Font>"
        "<FontString name=\"Message\" text=\"Hello\"/></Ui>";
    reset_state();
    test_missing_font = true;
    wow_ui.lua = luaL_newstate();
    luaL_openlibs(wow_ui.lua);
    init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, sizeof(xml) - 1, "font methods"));
    T_ASSERT(UIWow_RunLuaString("font methods",
        "Message:SetFontObject(BaseFont)\n"
        "local name, size = Message:GetFont(); assert(name == 'Fonts\\\\Base.ttf' and size == 19)\n"
        "assert(not Message:SetFont('invalid', 0))\n"
        "assert(Message:SetFont('Fonts\\\\Missing.ttf', 23))\n"
        "name, size = Message:GetFont(); assert(name == 'Fonts\\\\Missing.ttf' and size == 23)"));
    T_ASSERT(UIWow_XMLDrawFrame("Message"));
    T_NOT_NULL(last_draw_text.font);
    if (last_draw_text.font) T_STREQ(last_draw_text.font->name, "Fonts\\FRIZQT__.TTF");
    T_EQ(test_missing_font_loads, 1);
    T_ASSERT(UIWow_XMLDrawFrame("Message"));
    T_EQ(test_missing_font_loads, 1);
    UIWow_XMLClearFrames();
    lua_close(wow_ui.lua);
    wow_ui.lua = NULL;
}

TEST(wow_hud_xml, parse_button_onclick) {
    static const char xml[] =
        "<Ui><Button name=\"Btn\" text=\"OK\">"
        "  <Scripts><OnClick>window_close WelcomeFrame</OnClick></Scripts>"
        "</Button></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("Btn");
    T_ASSERT(idx >= 0);
    T_EQ(UIWow_XmlElemType(idx), (int)WOW_XML_BUTTON);
    T_STREQ(UIWow_XmlElemText(idx), "OK");
    T_STREQ(UIWow_XmlElemOnClick(idx), "window_close WelcomeFrame");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, button_press_state_selects_xml_pushed_texture) {
    static const char xml[] =
        "<Ui><Button name=\"Btn\"><Size><AbsDimension x=\"80\" y=\"20\"/></Size>"
        "<NormalTexture file=\"normal.blp\"/><PushedTexture file=\"pushed.blp\"/></Button></Ui>";

    reset_state(); init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test"));
    T_ASSERT(UIWow_XMLDrawFrame("Btn"));
    T_STREQ(last_draw_image_name, "normal.blp");
    T_ASSERT(UIWow_XMLSetButtonPressed("Btn", true));
    T_ASSERT(UIWow_XMLDrawFrame("Btn"));
    T_STREQ(last_draw_image_name, "pushed.blp");
    T_ASSERT(UIWow_XMLSetButtonPressed("Btn", false));
    T_ASSERT(UIWow_XMLDrawFrame("Btn"));
    T_STREQ(last_draw_image_name, "normal.blp");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, checkbutton_checked_state_selects_native_checked_texture) {
    static const char xml[] = "<Ui><CheckButton name=\"Check\"><Size><AbsDimension x=\"24\" y=\"24\"/></Size>"
        "<NormalTexture file=\"normal.blp\"/><CheckedTexture file=\"checked.blp\"/></CheckButton></Ui>";

    reset_state(); init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test"));
    T_ASSERT(UIWow_XMLSetButtonChecked("Check", false)); UIWow_XMLDraw(); T_STREQ(last_draw_image_name, "normal.blp");
    T_ASSERT(UIWow_XMLSetButtonChecked("Check", true)); UIWow_XMLDraw(); T_STREQ(last_draw_image_name, "checked.blp");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, parse_center_anchor) {
    static const char xml[] =
        "<Ui><Frame name=\"F\">"
        "  <Anchors><Anchor point=\"CENTER\"/></Anchors>"
        "</Frame></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("F");
    T_ASSERT(idx >= 0);
    T_STREQ(UIWow_XmlElemPoint(idx), "CENTER");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, reversed_anchors_span_the_same_rectangle) {
    static const char xml[] =
        "<Ui><Frame name=\"F\"><Anchors>"
        "<Anchor point=\"BOTTOMRIGHT\"><Offset><AbsDimension x=\"-128\" y=\"96\"/></Offset></Anchor>"
        "<Anchor point=\"TOPLEFT\"><Offset><AbsDimension x=\"128\" y=\"-96\"/></Offset></Anchor>"
        "</Anchors></Frame></Ui>";
    float x, y, w, h;

    reset_state(); init_ui();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, sizeof(xml) - 1, "anchors"));
    UIWow_XmlComputeRectPub(UIWow_XmlFindByNamePub("F"), &x, &y, &w, &h);
    T_FEQ(x, 0.125f, 0.001f);
    T_FEQ(y, 0.125f, 0.001f);
    T_FEQ(w, 0.75f, 0.001f);
    T_FEQ(h, 0.75f, 0.001f);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, relative_anchor_cycles_are_safe_and_parent_token_uses_anonymous_parent) {
    static const char xml[] =
        "<Ui><Frame><Size><AbsDimension x=\"512\" y=\"384\"/></Size>"
        "<Frames><Frame name=\"Child\"><Anchors>"
        "<Anchor point=\"TOPLEFT\" relativeTo=\"$parent\"/>"
        "<Anchor point=\"BOTTOMRIGHT\" relativeTo=\"$parent\"/>"
        "</Anchors></Frame></Frames></Frame>"
        "<Frame name=\"A\"><Anchors><Anchor point=\"CENTER\" relativeTo=\"B\"/></Anchors></Frame>"
        "<Frame name=\"B\"><Anchors><Anchor point=\"CENTER\" relativeTo=\"A\"/></Anchors></Frame>"
        "<Frame name=\"Unconstrained\" setAllPoints=\"true\"/></Ui>";
    float x, y, w, h;

    reset_state(); init_ui();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, sizeof(xml) - 1, "relative anchors"));
    UIWow_XmlComputeRectPub(UIWow_XmlFindByNamePub("Child"), &x, &y, &w, &h);
    T_FEQ(w, 0.5f, 0.001f);
    T_FEQ(h, 0.5f, 0.001f);
    UIWow_XmlComputeRectPub(UIWow_XmlFindByNamePub("A"), &x, &y, &w, &h);
    T_FEQ(x, 0.5f, 0.001f);
    T_FEQ(y, 0.5f, 0.001f);
    UIWow_XmlComputeRectPub(UIWow_XmlFindByNamePub("Unconstrained"), &x, &y, &w, &h);
    T_FEQ(w, 1.0f, 0.001f);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, script_can_fill_and_clear_frames_and_scroll_in_ui_units) {
    static const char xml[] =
        "<Ui><Frame name=\"Host\"><Size><AbsDimension x=\"512\" y=\"384\"/></Size>"
        "<Frames><Frame name=\"Child\"><Size><AbsDimension x=\"64\" y=\"48\"/></Size>"
        "<Anchors><Anchor point=\"CENTER\"/></Anchors></Frame></Frames></Frame>"
        "<ScrollFrame name=\"Viewport\"><Size><AbsDimension x=\"256\" y=\"192\"/></Size>"
        "<ScrollChild><Frame name=\"Content\"><Size><AbsDimension x=\"256\" y=\"384\"/></Size>"
        "</Frame></ScrollChild></ScrollFrame>"
        "<ScrollFrame name=\"Empty\"/><Frame name=\"UIParent\"/></Ui>";
    reset_state();
    wow_ui.lua = luaL_newstate();
    luaL_openlibs(wow_ui.lua);
    init_ui();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, sizeof(xml) - 1, "frame methods"));
    T_ASSERT(UIWow_RunLuaString("frame methods",
        "assert(UIParent:GetWidth() == 1024 and UIParent:GetHeight() == 768)\n"
        "Child:SetAllPoints(); assert(Child:GetWidth() == 512 and Child:GetHeight() == 384)\n"
        "Child:SetAllPoints(Child); assert(Child:GetHeight() == 384)\n"
        "Child:SetAllPoints('UIParent'); assert(Child:GetWidth() == 1024)\n"
        "Child:ClearAllPoints(); assert(Child:GetWidth() == 64 and Child:GetHeight() == 48)\n"
        "Child:SetPoint('BOTTOMRIGHT', Host); assert(Child:GetWidth() == 64)\n"
        "assert(Empty:GetVerticalScrollRange() == 0)\n"
        "assert(Viewport:GetVerticalScrollRange() == 192)\n"
        "Viewport:SetVerticalScroll(96); assert(Viewport:GetVerticalScroll() == 96)\n"
        "Viewport:SetVerticalScroll(300); assert(Viewport:GetVerticalScroll() == 192)\n"
        "Viewport:SetVerticalScroll(-20); assert(Viewport:GetVerticalScroll() == 0)\n"
        "Viewport:SetVerticalScroll(192); Content:SetHeight(240)\n"
        "assert(select('#', Viewport:UpdateScrollChildRect()) == 0)\n"
        "assert(Viewport:GetVerticalScroll() == 48)"));
    UIWow_XMLClearFrames();
    lua_close(wow_ui.lua);
    wow_ui.lua = NULL;
}

TEST(wow_hud_xml, parse_hidden_attribute) {
    static const char xml[] =
        "<Ui><Frame name=\"F\" hidden=\"true\"/></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("F");
    T_ASSERT(idx >= 0);
    T_EQ(UIWow_XmlElemHidden(idx), 1);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, parse_parent_child) {
    static const char xml[] =
        "<Ui><Frame name=\"Parent\">"
        "  <Frames><FontString name=\"Child\" parent=\"Parent\" text=\"hi\"/></Frames>"
        "</Frame></Ui>";
    int child_idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    child_idx = UIWow_XmlFindByNamePub("Child");
    T_ASSERT(child_idx >= 0);
    T_STREQ(UIWow_XmlElemParent(child_idx), "Parent");
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, clear_frames_resets_count) {
    static const char xml[] = "<Ui><Frame name=\"F\"/></Ui>";

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    T_ASSERT(UIWow_XmlElemCount() > 0);
    UIWow_XMLClearFrames();
    T_EQ(UIWow_XmlElemCount(), 0);
}

TEST(wow_hud_xml, set_frame_visible_toggles_hidden) {
    static const char xml[] =
        "<Ui><Frame name=\"Win\" hidden=\"true\"/></Ui>";
    int idx;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("Win");
    T_ASSERT(idx >= 0);
    T_EQ(UIWow_XmlElemHidden(idx), 1);

    UIWow_XMLSetFrameVisible("Win", true);
    T_EQ(UIWow_XmlElemHidden(idx), 0);

    UIWow_XMLSetFrameVisible("Win", false);
    T_EQ(UIWow_XmlElemHidden(idx), 1);

    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, frame_size_reflected_in_rect) {
    static const char xml[] =
        "<Ui><Frame name=\"F\">"
        "  <Size><AbsDimension x=\"512\" y=\"384\"/></Size>"
        "  <Anchors><Anchor point=\"TOPLEFT\"/></Anchors>"
        "</Frame></Ui>";
    int idx;
    float x, y, w, h;

    reset_state(); init_ui();
    UIWow_XMLClearFrames();
    UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test");
    idx = UIWow_XmlFindByNamePub("F");
    T_ASSERT(idx >= 0);
    UIWow_XmlComputeRectPub(idx, &x, &y, &w, &h);
    /* 512/1024 = 0.5, 384/768 = 0.5 */
    T_ASSERT(w > 0.49f && w < 0.51f);
    T_ASSERT(h > 0.49f && h < 0.51f);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, unresolved_frame_has_no_invented_geometry) {
    static const char xml[] = "<Ui><Frame name=\"NoGeometry\"/></Ui>";
    int idx; float x, y, w, h;

    reset_state(); init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test"));
    idx = UIWow_XmlFindByNamePub("NoGeometry");
    UIWow_XmlComputeRectPub(idx, &x, &y, &w, &h);
    T_EQ(w, 0.0f); T_EQ(h, 0.0f);
    UIWow_XMLDraw(); UIWow_XMLDraw();
    T_EQ(geometry_warnings, 1);
    UIWow_XMLClearFrames();
}

TEST(wow_hud_xml, unauthored_fontstring_uses_natural_text_size) {
    static const char xml[] = "<Ui><FontString name=\"NaturalText\" text=\"Hello\"/></Ui>";
    int idx; float x, y, w, h;

    reset_state(); init_ui(); UIWow_XMLClearFrames();
    T_ASSERT(UIWow_XMLLoadBuffer(xml, (int)(sizeof(xml)-1), "test"));
    idx = UIWow_XmlFindByNamePub("NaturalText");
    UIWow_XmlComputeRectPub(idx, &x, &y, &w, &h);
    T_EQ(w, 0.0f); T_EQ(h, 0.0f);
    UIWow_XMLDraw();
    UIWow_XmlComputeRectPub(idx, &x, &y, &w, &h);
    T_ASSERT(w > 0.049f && w < 0.051f); T_EQ(h, 0.012f);
    T_EQ(geometry_warnings, 0);
    UIWow_XMLClearFrames();
}

/* -------------------------------------------------------------------------- */
/* Integration test — WelcomeFrame.xml from the test MPQ                      */
/* -------------------------------------------------------------------------- */

TEST(wow_hud_xml, welcome_frame_loads_from_mpq) {
    int root_idx, header_idx, text_idx, btn_idx, i;

    reset_state();
    T_ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));
    init_ui();
    UIWow_XMLClearFrames();

    T_ASSERT(UIWow_XMLLoadFile("Interface\\FrameXML\\WelcomeFrame.xml"));

    /* Root frame */
    root_idx = UIWow_XmlFindByNamePub("WelcomeFrame");
    T_ASSERT(root_idx >= 0);
    T_EQ(UIWow_XmlElemType(root_idx), (int)WOW_XML_FRAME);
    T_STREQ(UIWow_XmlElemPoint(root_idx), "CENTER");

    /* Size ≈ 388×175 / 1024×768 */
    {
        float x, y, w, h;
        UIWow_XmlComputeRectPub(root_idx, &x, &y, &w, &h);
        T_ASSERT(w > 0.37f && w < 0.39f);
        T_ASSERT(h > 0.22f && h < 0.24f);
    }

    /* Title FontString */
    header_idx = UIWow_XmlFindByNamePub("WelcomeFrameHeader");
    T_ASSERT(header_idx >= 0);
    T_EQ(UIWow_XmlElemType(header_idx), (int)WOW_XML_FONTSTRING);
    T_STREQ(UIWow_XmlElemText(header_idx), "Welcome to World of Warcraft");
    T_STREQ(UIWow_XmlElemPoint(header_idx), "TOP");

    /* Body FontString */
    text_idx = UIWow_XmlFindByNamePub("WelcomeFrameText");
    T_ASSERT(text_idx >= 0);
    T_EQ(UIWow_XmlElemType(text_idx), (int)WOW_XML_FONTSTRING);

    /* OK button */
    btn_idx = UIWow_XmlFindByNamePub("WelcomeFrameAccept");
    T_ASSERT(btn_idx >= 0);
    T_EQ(UIWow_XmlElemType(btn_idx), (int)WOW_XML_BUTTON);
    T_STREQ(UIWow_XmlElemText(btn_idx), "Okay");
    T_STREQ(UIWow_XmlElemOnClick(btn_idx), "window_close WelcomeFrame");
    T_STREQ(UIWow_XmlElemPoint(btn_idx), "BOTTOM");

    /* All children reference WelcomeFrame as parent */
    for (i = 0; i < UIWow_XmlElemCount(); i++) {
        if (i == root_idx) continue;
        T_STREQ(UIWow_XmlElemParent(i), "WelcomeFrame");
    }

    UIWow_XMLClearFrames();
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}

TEST(wow_hud_xml, loading_title_draws_from_runtime_without_project_framexml) {
    reset_state();
    T_ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));
    init_ui();
    UIWow_XMLClearFrames();

    UIWow_DrawLoadingScreenC(NULL, NULL, 0.0f);
    T_EQ(draw_text_calls, 1);
    T_STREQ(last_draw_text.text, "The Barrens");
    T_FEQ(last_draw_text.rect.x, 0.16f, 0.001f);
    T_FEQ(last_draw_text.rect.y, 0.77f, 0.001f);
    T_EQ(UIWow_XmlFindByNamePub("OpenWarcraftLoadingScreen"), -1);

    UIWow_XMLClearFrames();
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}
