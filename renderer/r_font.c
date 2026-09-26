#include "r_local.h"
#include "common/ui_constants.h"
#ifndef _WIN32
#include <strings.h>
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION

#define MAX_GLYPHSET 256
#define MAX_CACHED_FONTS 64
#define FONT_SCALE 2
#define INV_SCALE_X(x) ((x) / (FONT_SCALE * UI_FONT_COORD_SCALE))
#define INV_SCALE_Y(y) (INV_SCALE_X(y) * UI_PIXEL_ASPECT)
#define TEXT_BATCH_VERTICES 1020

typedef struct {
    texture_t * image;
    stbtt_bakedchar glyphs[MAX_GLYPHSET];
} glyphSet_t;

typedef struct  font {
    struct font *next;
    char filename[MAX_PATHLEN];
    uint32_t requested_size;
    void *data;
    stbtt_fontinfo stbfont;
    glyphSet_t *sets[MAX_GLYPHSET];
    float size;
    int height;
} font_t;

static font_t *r_fonts;
static uint32_t r_num_fonts;

static const char* utf8_to_codepoint(const char *p, unsigned *dst) {
    unsigned res, n;
    switch (*p & 0xf0) {
        case 0xf0 :  res = *p & 0x07;  n = 3;  break;
        case 0xe0 :  res = *p & 0x0f;  n = 2;  break;
        case 0xd0 :
        case 0xc0 :  res = *p & 0x1f;  n = 1;  break;
        default   :  res = *p;         n = 0;  break;
    }
    while (n--) {
        res = (res << 6) | (*(++p) & 0x3f);
    }
    *dst = res;
    return p + 1;
}

static glyphSet_t* R_LoadGlyphSet(font_t *font, int idx) {
    glyphSet_t *set = ri.MemAlloc(sizeof(glyphSet_t));
    
    /* init image */
    int width = 128;
    int height = 128;
    uint8_t *fontimage;
    float s;
    int res;
    
retry:
    fontimage = ri.MemAlloc(width * height);
    /* load glyphs */
    s = stbtt_ScaleForMappingEmToPixels(&font->stbfont, 1) /
    stbtt_ScaleForPixelHeight(&font->stbfont, 1);
    res = stbtt_BakeFontBitmap(font->data, 0, font->size * s, fontimage,
                               width, height, idx * 256, 256, set->glyphs);
    
    /* retry with a larger image buffer if the buffer wasn't large enough */
    if (res < 0) {
        width *= 2;
        height *= 2;
        ri.MemFree(fontimage);
        goto retry;
    }
    
    /* adjust glyph yoffsets and xadvance */
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font->stbfont, &ascent, &descent, &linegap);
    float scale = stbtt_ScaleForMappingEmToPixels(&font->stbfont, font->size);
    int scaled_ascent = ascent * scale + 0.5;
    for (int i = 0; i < 256; i++) {
        set->glyphs[i].yoff += scaled_ascent;
        set->glyphs[i].xadvance = floor(set->glyphs[i].xadvance);
    }
    
    color32_t * pixels = ri.MemAlloc(sizeof(color32_t) * width * height);
    /* convert 8bit data to 32bit */
    for (int i = 0; i < width * height; i++) {
        uint8_t n = fontimage[i];
        pixels[i] = (color32_t) { .r = 255, .g = 255, .b = 255, .a = n };
    }
    set->image = R_AllocateTexture(width, height);
    
    R_LoadTextureMipLevel(set->image, &(texMip_t){ pixels, width, height, 0, PIXEL_RGBA });
    ri.MemFree(pixels);
    ri.MemFree(fontimage);
    
    return set;
}


static glyphSet_t* R_GetGlyphSet(font_t *font, int codepoint) {
    int idx = (codepoint >> 8) % MAX_GLYPHSET;
    if (!font->sets[idx]) {
        font->sets[idx] = R_LoadGlyphSet(font, idx);
    }
    return font->sets[idx];
}


font_t * R_LoadFont(cstring_t filename, uint32_t size) {
    if (!filename || !*filename) {
        return NULL;
    }

    size = MAX(9, size);
    for (font_t *cached = r_fonts; cached; cached = cached->next) {
        if (cached->requested_size == size && !strcasecmp(cached->filename, filename)) {
            return cached;
        }
    }

    if (r_num_fonts >= MAX_CACHED_FONTS) {
        return NULL;
    }

    font_t *font = ri.MemAlloc(sizeof(font_t));
    memset(font, 0, sizeof(*font));
    snprintf(font->filename, sizeof(font->filename), "%s", filename);
    font->requested_size = size;
    font->size = size * FONT_SCALE;
    
    /* load font into buffer */
    void *buffer = NULL;
    int buf_size = ri.FS_ReadFile(filename, &buffer);
    if (buf_size < 0 || !buffer) { goto fail; }
    font->data = buffer;
    
    /* init stbfont */
    int ok = stbtt_InitFont(&font->stbfont, font->data, 0);
    if (!ok) { goto fail; }
    
    /* get height and scale */
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font->stbfont, &ascent, &descent, &linegap);
    float scale = stbtt_ScaleForMappingEmToPixels(&font->stbfont, size);
    font->height = (ascent - descent + linegap) * scale + 0.5;
    
    /* make tab and newline glyphs invisible */
    stbtt_bakedchar *g = R_GetGlyphSet(font, '\n')->glyphs;
    g['\t'].x1 = g['\t'].x0;
    g['\n'].x1 = g['\n'].x0;
    
    font->next = r_fonts;
    r_fonts = font;
    r_num_fonts++;
    return font;
    
fail:
    if (font) { ri.MemFree(font->data); }
    ri.MemFree(font);
    return NULL;
}

void R_ReleaseFont(font_t * font) {
    font_t **link = &r_fonts;
    while (*link) {
        if (*link == font) {
            *link = font->next;
            r_num_fonts--;
            break;
        }
        link = &(*link)->next;
    }
    for (int i = 0; i < MAX_GLYPHSET; i++) {
        glyphSet_t *set = font->sets[i];
        if (set) {
            R_ReleaseTexture(set->image);
            ri.MemFree(set);
        }
    }
    ri.MemFree(font->data);
    ri.MemFree(font);
}

void R_ShutdownFonts(void) {
    while (r_fonts) {
        R_ReleaseFont(r_fonts);
    }
}

float R_GetFontWidth(font_t * font, cstring_t text) {
    float x = 0;
    cstring_t p = text;
    unsigned codepoint;
    while (*p) {
        p = utf8_to_codepoint(p, &codepoint);
        glyphSet_t *set = R_GetGlyphSet(font, codepoint);
        stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
        x += INV_SCALE_X(g->xadvance);
    }
    return x;
}


float R_GetFontHeight(font_t * font) {
    return FONT_SCALE * INV_SCALE_Y(font->height);
}

bool will_word_fit(cstring_t text, float width, font_t const * font) {
    cstring_t p = text;
    for (; *p && !isspace(*p) && *p != '|';) {
        unsigned codepoint;
        p = utf8_to_codepoint(p, &codepoint);
        glyphSet_t *set = R_GetGlyphSet((font_t *)font, codepoint);
        stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
        width -= INV_SCALE_X(g->xadvance);
    }
    for (; *p && isspace(*p) && *p != '\n';) {
        unsigned codepoint;
        p = utf8_to_codepoint(p, &codepoint);
        glyphSet_t *set = R_GetGlyphSet((font_t *)font, codepoint);
        stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
        width -= INV_SCALE_X(g->xadvance);
    }
    /* Measurement and drawing subtract the same advances in different orders; tolerate sub-pixel residue. */
    return R_TextFitsWidth(width);
}

static vector2_t get_position(drawText_t const * arg) {
    vector2_t pos = { 0 };
    vector2_t size = R_GetTextSize(arg);
    switch (arg->halign) {
        case FONT_JUSTIFYRIGHT: pos.x = arg->rect.x + arg->rect.w - size.x; break;
        case FONT_JUSTIFYCENTER: pos.x = arg->rect.x + (arg->rect.w - size.x) / 2; break;
        case FONT_JUSTIFYLEFT: pos.x = arg->rect.x; break;
    }
    switch (arg->valign) {
        case FONT_JUSTIFYBOTTOM: pos.y = arg->rect.y + arg->rect.h - size.y; break;
        case FONT_JUSTIFYMIDDLE: pos.y = arg->rect.y + (arg->rect.h - size.y) / 2; break;
        case FONT_JUSTIFYTOP: pos.y = arg->rect.y; break;
    }
    if (pos.y < arg->rect.y) pos.y = arg->rect.y;
    return pos;
}

static rect_t get_uvrect(stbtt_bakedchar *g, float h, float w) {
    rect_t const uv_rect = {
        .x = g->x0 / w,
        .y = g->y0 / h,
        .w = (g->x1 - g->x0) / w,
        .h = (g->y1 - g->y0) / h,
    };
    return uv_rect;
}

static rect_t get_screenrect(vector2_t const * cursor, stbtt_bakedchar *g) {
    rect_t const screen = {
        .x = cursor->x + INV_SCALE_X(g->xoff),
        .y = cursor->y + INV_SCALE_Y(g->yoff),
        .w = INV_SCALE_X(g->x1 - g->x0),
        .h = INV_SCALE_Y(g->y1 - g->y0),
    };
    return screen;
}

typedef struct {
    vertex_t vertices[TEXT_BATCH_VERTICES];
    uint32_t count;
    texture_t const * texture;
} textBatch_t;

static void flush_text_batch(textBatch_t *batch, drawText_t const * arg) {
    if (!batch->count) {
        return;
    }
    R_DrawImageBatch(batch->texture,
                     SHADER_UI,
                     BLEND_MODE_BLEND,
                     0.0f,
                     0.0f,
                     arg->flags & DRAW_CLIP,
                     &arg->clip,
                     batch->vertices,
                     batch->count,
                     false);
    batch->count = 0;
    batch->texture = NULL;
}

static void add_text_glyph(textBatch_t *batch,
                           drawText_t const * arg,
                           texture_t const * texture,
                           rect_t const * screen,
                           rect_t const * uv,
                           color32_t color)
{
    if (batch->texture != texture || batch->count + 6 > TEXT_BATCH_VERTICES) {
        flush_text_batch(batch, arg);
        batch->texture = texture;
    }
    R_AddQuad(batch->vertices + batch->count, screen, uv, color, 0);
    batch->count += 6;
}

static vector2_t process_text(drawText_t const * arg, bool draw) {
    if (!arg->font) {
        return MAKE(vector2_t, 0, 0);
    }
    vector2_t pos = draw ? get_position(arg) : MAKE(vector2_t, 0, 0);
    color32_t color = arg->color;
    vector2_t cursor = pos;
    vector2_t linesize = MAKE(vector2_t, 0.5f * arg->font->size / UI_FONT_COORD_SCALE, 0.5f * arg->font->size / UI_FONT_COORD_SCALE * UI_PIXEL_ASPECT);
    float line_height = R_GetFontHeight((font_t *)arg->font);
    float line_advance = line_height * (arg->lineHeight > 0 ? arg->lineHeight : 1.0f);
    float max_cursor_x = pos.x;
    float min_cursor_y = pos.y;
    float max_cursor_y = pos.y;
    textBatch_t batch = { 0 };
    for (cstring_t p = arg->text; *p;) {
        if (*p == '\n') {
            cursor.x = pos.x;
            cursor.y += line_advance;
            max_cursor_y = MAX(max_cursor_y, cursor.y);
            p++;
            continue;
        }
        if (!strncmp(p, "|n", 2) || !strncmp(p, "|N", 2)) {
        // next_line:
            cursor.x = pos.x;
            cursor.y += line_advance;
            max_cursor_y = MAX(max_cursor_y, cursor.y);
            p += 2;
            continue;
        }
        if (*p == '<') {
            cstring_t end = strchr(p + 1, '>');
            uint32_t icon = 0;
            if (!end) {
                break;
            }
            if (end > p + 6) {
                icon = (uint32_t)atoi(p + 6);
            }
            switch (*(uint32_t*)(p+1)) {
                case MAKEFOURCC('I', 'c', 'o', 'n'):
                    if (draw && arg->icons && icon < MAX_IMAGES && arg->icons[icon]) {
                        flush_text_batch(&batch, arg);
                        R_DrawImageEx(&MAKE(drawImage_t,
                                            .texture = arg->icons[icon],
                                            .shader = SHADER_UI,
                                            .alphamode = BLEND_MODE_BLEND,
                                            .screen = MAKE(rect_t, cursor.x, cursor.y + linesize.y * 0.1f, linesize.x, linesize.y),
                                            .uv = MAKE(rect_t, 0, 0, 1, 1),
                                            .color = COLOR32_WHITE,
                                            .flags = (arg->flags & DRAW_CLIP),
                                            .clip = arg->clip));
                    }
                    cursor.x += linesize.x;
                    break;
            }
            p = end + 1;
            continue;;
        }
        if (!strncmp(p, "|r", 2) || !strncmp(p, "|R", 2)) {
            color = arg->color;
            p += 2;
            continue;
        }
        if (!strncmp(p, "|c", 2) || !strncmp(p, "|C", 2)) {
            color32_t c;
            sscanf(p+2, "%08x", (uint32_t *)&c);
            color.a = c.a;
            color.b = c.r;
            color.g = c.g;
            color.r = c.b;
            p += 10;
            continue;
        }
        if ((arg->flags & DRAW_WORD_WRAP) && cursor.x > pos.x && !will_word_fit(p, arg->textWidth - (cursor.x - pos.x), arg->font)) {
            cursor.x = pos.x;
            cursor.y += line_advance;
            max_cursor_y = MAX(max_cursor_y, cursor.y);
        }
        unsigned codepoint;
        p = utf8_to_codepoint(p, &codepoint);
        glyphSet_t *set = R_GetGlyphSet((font_t *)arg->font, codepoint);
        stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
        if (draw) {
            float const w = set->image->width;
            float const h = set->image->height;
            rect_t const uv_rect = get_uvrect(g, h, w);
            rect_t const screen = get_screenrect(&cursor, g);
            add_text_glyph(&batch, arg, set->image, &screen, &uv_rect, color);
        }
        cursor.x += INV_SCALE_X(g->xadvance);
        max_cursor_x = MAX(max_cursor_x, cursor.x);
        max_cursor_y = MAX(max_cursor_y, cursor.y);
    }
    if (draw) {
        flush_text_batch(&batch, arg);
    }
    return MAKE(vector2_t,
                max_cursor_x - pos.x,
                (max_cursor_y - min_cursor_y) + R_GetFontHeight((font_t *)arg->font));
}


void R_DrawText(drawText_t const * arg) {
    process_text(arg, true);
    
//    R_DrawWireRect(&arg->rect, MAKE(COLOR32, 255, 0, 255, 255));
}

vector2_t R_GetTextSize(drawText_t const * arg) {
    return process_text(arg, false);
}
