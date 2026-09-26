/*
 * hud_write.c — Frame-write primitives, theme lookup, text formatting.
 *
 * Low-level helpers that build uiFrame_t structs and serialize them
 * to the network via gi.Write.  All HUD panels use these to emit frames.
 */

#include <string.h>

#include "hud_local.h"
#include "hud_utils.h"

uint32_t ui_next_frame_number;
gameClient_t *ui_current_client;
static uint8_t ui_window_text[MAX_MSGLEN];
static uint32_t ui_window_text_size;
bool ui_window_writing;

cstring_t UI_LevelStringSafe(cstring_t text) {
    if (!text || !*text) {
        return " ";
    }
    return G_LevelString(text);
}

void UI_SetCurrentClient(gameClient_t *client) {
    ui_current_client = client;
}

void UI_CenterFrame(frameDef_t *frame) {
    if (!frame) return;
    memset(&frame->Points, 0, sizeof(frame->Points));
    frame->AnyPointsSet = true;
    UI_SetPoint(frame, FRAMEPOINT_CENTER, NULL, FRAMEPOINT_CENTER, 0.0f, 0.0f);
}

void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, uint32_t relative, float offset, bool y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (uint8_t)relative;
    point->offset = (int16_t)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

void UI_SetFrameRect(uiFrame_t *frame, float x, float y, float w, float h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

void UI_WriteProxyFrame(uiFrame_t *frame, handle_t data, uint32_t data_size) {
    frame->number = ui_next_frame_number++;
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    if (!frame->tex.coord[1] && !frame->tex.coord[3]) {
        frame->tex.coord[1] = 0xff;
        frame->tex.coord[3] = 0xff;
    }
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(ui_window_writing ? PF_UIWINDOWFRAME : PF_UIFRAME, frame);
}

void UI_WriteProxyFrameToParent(uiFrame_t *frame, handle_t data, uint32_t data_size, uint32_t parent) {
    frame->parent = parent;
    UI_WriteProxyFrame(frame, data, data_size);
}

void UI_SetFramePointRelative(uiFramePoint_t *point, uiFramePointPos_t target, uint32_t relative, float offset, bool y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (uint8_t)relative;
    point->offset = (int16_t)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

void UI_WriteTextFrame(float x, float y, float w, float h, cstring_t text, color32_t color,
                       uiFontJustificationH_t align) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

void UI_WriteTextureFrame(float x, float y, float w, float h, cstring_t art) {
    uiFrame_t frame;

    if (!art || !*art) {
        return;
    }
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ImageIndex(art);
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteTextFrameSized(float x, float y, float w, float h, cstring_t text, color32_t color,
                            uiFontJustificationH_t align, uint32_t font_size) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", font_size);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

void UI_WriteCommandTextFrame(float x, float y, float w, float h, cstring_t text, cstring_t command,
                              color32_t color, uiFontJustificationH_t align, uint32_t font_size) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.onclick = command;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", font_size);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

void UI_WriteBackdropFrame(float x, float y, float w, float h, cstring_t background, cstring_t edge) {
    uiFrame_t frame;
    uiBackdrop_t backdrop;

    memset(&frame, 0, sizeof(frame));
    memset(&backdrop, 0, sizeof(backdrop));
    frame.flags.type = FT_BACKDROP;
    frame.color = MAKE(color32_t, 255, 255, 255, 235);
    backdrop.Background = gi.ImageIndex(background);
    backdrop.EdgeFile = gi.ImageIndex(edge);
    backdrop.CornerFlags = 0x1ff;
    backdrop.CornerSize = 0.008f;
    backdrop.BackgroundSize = 0.036f;
    backdrop.BackgroundInsets[0] = 0.0025f;
    backdrop.BackgroundInsets[1] = 0.0025f;
    backdrop.BackgroundInsets[2] = 0.0025f;
    backdrop.BackgroundInsets[3] = 0.0025f;
    backdrop.TileBackground = true;
    backdrop.BlendAll = true;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &backdrop, sizeof(backdrop));
}

void UI_WriteTextAreaFrame(float x, float y, float w, float h, cstring_t text, color32_t color,
                           uint32_t font_size, float inset) {
    uiFrame_t frame;
    uiTextArea_t textarea;

    memset(&frame, 0, sizeof(frame));
    memset(&textarea, 0, sizeof(textarea));
    frame.flags.type = FT_TEXTAREA;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    textarea.font = gi.FontIndex(Theme_String("MessageFont", "Fonts\\FRIZQT__.TTF"), font_size);
    textarea.inset = inset;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &textarea, sizeof(textarea));
}

void UI_WriteTooltipFrame(void) {
    uiFrame_t frame;
    uiTooltip_t tooltip;

    memset(&frame, 0, sizeof(frame));
    memset(&tooltip, 0, sizeof(tooltip));
    frame.flags.type = FT_TOOLTIPTEXT;
    frame.color = COLOR32_WHITE;
    tooltip.background.Background = gi.ImageIndex("ToolTipBackground");
    tooltip.background.EdgeFile = gi.ImageIndex("ToolTipBorder");
    tooltip.background.CornerFlags = 0x1ff;
    tooltip.background.CornerSize = 0.008f;
    tooltip.background.BackgroundSize = 0.036f;
    tooltip.background.BackgroundInsets[0] = 0.0025f;
    tooltip.background.BackgroundInsets[1] = 0.0025f;
    tooltip.background.BackgroundInsets[2] = 0.0025f;
    tooltip.background.BackgroundInsets[3] = 0.0025f;
    tooltip.background.TileBackground = true;
    tooltip.background.BlendAll = true;
    tooltip.text.font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    tooltip.text.textalignx = FONT_JUSTIFYLEFT;
    tooltip.text.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, 0.580f, 0.340f, 0.220f, 0.100f);
    UI_WriteProxyFrame(&frame, &tooltip, sizeof(tooltip));
}

void UI_AppendMessageText(string_t out, uint32_t out_size, cstring_t text) {
    if (!out || out_size == 0 || !text) {
        return;
    }
    strncat(out, text, out_size - strlen(out) - 1);
}

cstring_t UI_FormatMessageText(cstring_t text) {
    static char buffers[4][1024];
    static uint32_t cursor;
    char temp[1024];
    string_t out = buffers[cursor++ & 3];
    cstring_t source = text && *text ? text : " ";
    bool quest_message = strstr(source, "MAIN QUEST") || strstr(source, "OPTIONAL QUEST");
    bool inserted_heading_break = false;
    cstring_t heading = quest_message ? strstr(source, "QUEST") : NULL;

    temp[0] = '\0';
    out[0] = '\0';

    for (cstring_t p = source; *p && strlen(temp) < sizeof(temp) - 1;) {
        if (quest_message && p[0] == ' ' && p[1] == '-' && p[2] == ' ') {
            UI_AppendMessageText(temp, sizeof(temp), "|n- ");
            p += 3;
            continue;
        }
        strncat(temp, p, 1);
        p++;
    }

    source = temp;
    heading = quest_message ? strstr(source, "QUEST") : NULL;
    for (cstring_t p = source; *p && strlen(out) < sizeof(buffers[0]) - 1;) {
        if (quest_message && !inserted_heading_break && heading &&
            (p == heading + 5 || (!strncmp(p, "|r", 2) && p > heading))) {
            if (!strncmp(p, "|r", 2)) {
                UI_AppendMessageText(out, sizeof(buffers[0]), "|r");
                p += 2;
            }
            if (strncmp(p, "|n", 2) && *p != '\n') {
                UI_AppendMessageText(out, sizeof(buffers[0]), "|n");
            }
            inserted_heading_break = true;
            continue;
        }
        strncat(out, p, 1);
        p++;
    }

    return out;
}

#define BZ_HOST_HIDDEN __attribute__((visibility("hidden")))

/* Widescreen console tiles are written for wide clients only (docs/architecture/ui-canvas.md).  Registering
 * their keys while ConsoleUI.fdf is parsed would put the art in CS_IMAGES for every session, so these keys
 * get a deferred handle instead and reach gi.ImageIndex the first time a wide client's console is written. */
static cstring_t const hud_wide_chrome_keys[] = { "ConsoleTexture05", "ConsoleTexture06" };

bool UI_IsWideChromeKey(cstring_t key) {
    FOR_LOOP(i, sizeof(hud_wide_chrome_keys) / sizeof(hud_wide_chrome_keys[0]))
        if (key && !strcmp(key, hud_wide_chrome_keys[i])) return true;
    return false;
}

static uint32_t UI_DeferredImage(cstring_t key) {
    FOR_LOOP(i, HUD_DEFERRED_IMAGES) {
        if (!hud.deferred_key[i][0]) snprintf(hud.deferred_key[i], sizeof(hud.deferred_key[i]), "%s", key);
        if (!strcmp(hud.deferred_key[i], key)) return HUD_DEFERRED_IMAGE_BASE + i;
    }
    fprintf(stderr, "WC3 HUD: deferred image table full; %s registers at load\n", key);
    return 0;
}

/* Symbolic key (or concrete path) behind a FRAMEDEF image handle, deferred or already registered. */
cstring_t UI_ImageKey(uint32_t image) {
    if (image >= HUD_DEFERRED_IMAGE_BASE && image < HUD_DEFERRED_IMAGE_BASE + HUD_DEFERRED_IMAGES)
        return hud.deferred_key[image - HUD_DEFERRED_IMAGE_BASE];
    return image && image < MAX_IMAGES ? hud.image_key[image] : "";
}

static void UI_RememberImage(uint32_t index, cstring_t key, cstring_t resolved, bool decorate) {
    if (!index || index >= MAX_IMAGES) return;
    /* After SV_Map reuses CS_IMAGES slots, a stale FRAMEDEF still holds the old
     * index. Keep the original name until memset(&hud); overwriting it with the
     * new occupant is the shuffled-icon bug. */
    if (hud.image_key[index][0] && key && strcmp(hud.image_key[index], key))
        return;
    /* Live-image lookup can return the same slot; snprintf forbids self-copy. */
    if (key != hud.image_key[index])
        snprintf(hud.image_key[index], sizeof(hud.image_key[index]), "%s", key ? key : "");
    if (resolved != hud.image_name[index])
        snprintf(hud.image_name[index], sizeof(hud.image_name[index]), "%s", resolved ? resolved : "");
    hud.image_decorated[index] = decorate;
}

BZ_HOST_HIDDEN void UI_ClearTextures(void) {
    memset(hud.image_key, 0, sizeof(hud.image_key));
    memset(hud.image_name, 0, sizeof(hud.image_name));
    memset(hud.image_decorated, 0, sizeof(hud.image_decorated));
    memset(hud.font_spec, 0, sizeof(hud.font_spec));
}

BZ_HOST_HIDDEN uint32_t UI_FdfFontIndex(cstring_t name, uint32_t size) {
    uint32_t index;
    if (!name || !*name || !gi.FontIndex) return 0;
    index = gi.FontIndex(name, size);
    if (index && index < MAX_FONTSTYLES)
        snprintf(hud.font_spec[index], sizeof(hud.font_spec[index]), "%s,%u", name, (unsigned)size);
    return index;
}

uint32_t UI_LiveFont(uint32_t font) {
    PATHSTR spec, name;
    cstring_t comma;
    uint32_t size;

    if (!font) return 0;
    if (font >= MAX_FONTSTYLES || !hud.font_spec[font][0] || !gi.FontIndex) return font;
    snprintf(spec, sizeof(spec), "%s", hud.font_spec[font]);
    comma = strstr(spec, ",");
    if (!comma) return gi.FontIndex(spec, HUD_FONT_SIZE);
    memcpy(name, spec, (size_t)(comma - spec));
    name[comma - spec] = '\0';
    size = (uint32_t)atoi(comma + 1);
    font = gi.FontIndex(name, size ? size : HUD_FONT_SIZE);
    if (font && font < MAX_FONTSTYLES)
        snprintf(hud.font_spec[font], sizeof(hud.font_spec[font]), "%s", spec);
    return font;
}

uint32_t UI_LiveImage(uint32_t image) {
    cstring_t key = NULL, name = NULL, path;
    bool decorate = false;
    uint32_t live;

    if (!image) return 0;
    if (image >= HUD_DEFERRED_IMAGE_BASE) {
        key = UI_ImageKey(image);
        if (!*key) {
            fprintf(stderr, "UI_LiveImage: unknown deferred image handle %u\n", (unsigned)image);
            return 0;
        }
        path = UI_ThemeImagePath(key);
        live = gi.ImageIndex(path);
        UI_RememberImage(live, key, path, true);
        return live;
    }
    if (image < MAX_IMAGES && hud.image_key[image][0]) {
        key = hud.image_key[image];
        name = hud.image_name[image];
        decorate = hud.image_decorated[image];
    }
    if (!key || !*key) return image;
    /* Resolve each recipient's skin before publishing concrete resource paths. */
    if (decorate || (!strchr(key, '\\') && !strchr(key, '/'))) {
        path = UI_ThemeImagePath(key);
        live = gi.ImageIndex(path);
        UI_RememberImage(live, key, path, decorate);
        return live;
    }
    path = UI_ResolveTextureAlias(name && *name ? name : key);
    live = gi.ImageIndex(path);
    UI_RememberImage(live, key, path, decorate);
    return live;
}

BZ_HOST_HIDDEN uint32_t UI_LoadTexture(cstring_t path, bool decorate) {
    uint32_t index;

    if (!path || !*path) return 0;
    if (UI_IsWideChromeKey(path) && (index = UI_DeferredImage(path))) return index;

    cstring_t resolved = UI_ThemeImagePath(path);
    index = gi.ImageIndex(resolved);
    UI_RememberImage(index, path, resolved, decorate);
    return index;
}

BZ_HOST_HIDDEN cstring_t Theme_String(cstring_t key, cstring_t def) {
    cstring_t value = NULL;
    if (key && !strstr(key, "\\")) {
        if (game.config.map_skin.source)
            value = Stb_IniCacheFind(&game.config.map_skin, "CustomSkin", key);
        if (!value && game.config.theme.source)
            value = Stb_IniCacheFind(&game.config.theme, "Default", key);
    }
    return value ? value : def;
}

/* war3skins uses the console race category rather than the selected unit race. */
static cstring_t Theme_PlayerRaceCategory(uint32_t race) {
    switch (race) {
        case kPlayerRaceHuman: return "Human";
        case kPlayerRaceOrc: return "Orc";
        case kPlayerRaceUndead: return "Undead";
        case kPlayerRaceNightElf: return "NightElf";
        default: return "Default";
    }
}

static uint32_t Theme_GameVersion(void) {
    cstring_t expansion = gi.CvarString ? gi.CvarString("fs_expansion", "0") : "0";
    return expansion && atoi(expansion) != 0 ? 1 : 0;
}

/* Resolve a local player's race skin first, then the shared Default section.
 * Warcraft skin data also carries versioned aliases (for example Music_V1),
 * so fall back to the mounted game edition when the unversioned key is absent. */
cstring_t Theme_PlayerString(gameClient_t *client, cstring_t key, cstring_t def) {
    cstring_t category, value;
    char versioned[128];

    if (!key || strstr(key, "\\")) return def;
    snprintf(versioned, sizeof(versioned), "%s_V%u", key, (unsigned)Theme_GameVersion());

    /* war3mapSkin.txt is a map-authored Game Interface override and therefore
     * wins over the stock race skin, including edition-specific Music_V0/V1
     * aliases. */
    if (game.config.map_skin.source) {
        value = Stb_IniCacheFind(&game.config.map_skin, "CustomSkin", key);
        if (!value) value = Stb_IniCacheFind(&game.config.map_skin, "CustomSkin", versioned);
        if (value) return value;
    }

    if (!game.config.theme.source) return def;
    category = Theme_PlayerRaceCategory(client ? client->ps.race : kPlayerRaceNone);
    value = Stb_IniCacheFind(&game.config.theme, category, key);
    if (!value && strcmp(category, "Default")) value = Stb_IniCacheFind(&game.config.theme, "Default", key);
    if (value) return value;

    value = Stb_IniCacheFind(&game.config.theme, category, versioned);
    if (!value && strcmp(category, "Default")) value = Stb_IniCacheFind(&game.config.theme, "Default", versioned);
    return value ? value : def;
}

/* Some editions omit one widescreen extension key but provide its paired 05/06 texture.
 * Retain that established file-family rule here, alongside the authoritative skin lookup. */
cstring_t UI_ThemeImagePath(cstring_t key) {
    static PATHSTR path;
    cstring_t value, sibling;
    char digit = 0, *end, *dot;
    if (!key || !*key || strchr(key, '\\') || strchr(key, '/')) return UI_ResolveTextureAlias(key ? key : "");
    value = Theme_PlayerString(ui_current_client, key, NULL);
    if (value) return UI_ResolveTextureAlias(value);
    if (!strcmp(key, "ConsoleTexture05")) digit = '5';
    else if (!strcmp(key, "ConsoleTexture06")) digit = '6';
    if (digit) {
        sibling = Theme_PlayerString(ui_current_client, digit == '5' ? "ConsoleTexture06" : "ConsoleTexture05", NULL);
        if (sibling) {
            snprintf(path, sizeof(path), "%s", sibling);
            dot = strrchr(path, '.'); end = dot ? dot : path + strlen(path);
            if (end - path >= 2 && end[-2] == '0' && end[-1] == (digit == '5' ? '6' : '5')) {
                end[-1] = digit;
                return path;
            }
        }
    }
    value = UI_ResolveTextureAlias(key);
    if (!strchr(value, '.') && !strchr(value, '\\'))
        fprintf(stderr, "WC3 HUD: unresolved image skin key %s\n", key);
    return value;
}

BZ_HOST_HIDDEN float Theme_Float(cstring_t key, cstring_t def) {
    (void)key;
    return def ? atof(def) : 0.0f;
}

void UI_WriteStart(uint32_t layer) {
    UI_ResetFrameWriteList();
    gi.Write(PF_BYTE, &(int32_t){svc_layout});
    gi.Write(PF_BYTE, &(int32_t){layer});
    ui_next_frame_number = 1;
}

void UI_WriteEnd(edict_t *ent) {
    gi.Write(PF_LONG, &(int32_t){0});   /* bits=0 */
    gi.Write(PF_SHORT, &(int32_t){0});  /* number=0  — MSG_ReadEntityBits reads int32_t+int16_t */
    /* A NULL recipient leaves the initial loading layout for the server signon buffer. */
    if (ent) gi.unicast(ent);
}

uint32_t UI_WindowTextOffset(cstring_t text) {
    uint32_t offset, size;

    if (!text || !*text) return 0;
    size = strlen(text) + 1;
    if (size > sizeof(ui_window_text) - ui_window_text_size) {
        fprintf(stderr, "WC3 window text arena overflow: used=%u add=%u\n",
                (unsigned)ui_window_text_size, (unsigned)size);
        return 0;
    }
    offset = ui_window_text_size;
    memcpy(ui_window_text + offset, text, size);
    ui_window_text_size += size;
    return offset;
}

void UI_WriteWindowStart(uiWindowDef_t const *def) {
    UI_ResetFrameWriteList();
    ui_window_writing = true;
    ui_window_text[0] = '\0'; ui_window_text_size = 1;
    gi.Write(PF_BYTE, &(int32_t){svc_window});
    gi.Write(PF_BYTE, &(int32_t){UI_WINDOW_OPEN});
    gi.Write(PF_LONG, &def->id); gi.Write(PF_LONG, &def->class_id); gi.Write(PF_LONG, &def->flags);
    ui_next_frame_number = 1;
}

void UI_WriteWindowEnd(edict_t *ent) {
    pfWriteData_t text = { .data = ui_window_text, .size = ui_window_text_size };
    ui_window_writing = false;
    gi.Write(PF_LONG, &(int32_t){0}); gi.Write(PF_SHORT, &(int32_t){0});
    gi.Write(PF_LONG, &ui_window_text_size); gi.Write(PF_DATA, &text);
    gi.unicast(ent);
}
