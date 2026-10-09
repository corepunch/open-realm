#include <SDL2/SDL.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define MAX_TOOLS 5
#define MAX_FIELDS 4
#define MAX_ENTRIES 512
#define MAX_HISTORY 64
#define MAX_OUTPUT 32768

typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct {
    SDL_Surface *surf;
    SDL_Texture *texture;
    int width;
    int height;
    int scale;
} lowres_t;

typedef struct {
    const char *name;
    const char *exe;
    const char *summary;
    const char *template_args;
} tool_t;

typedef struct {
    char name[256];
    bool is_dir;
} mpq_entry_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    lowres_t lowres;
    int width;
    int height;
    char exe_dir[PATH_MAX];
    char history_path[PATH_MAX];
    char mpqtool_path[PATH_MAX];
    char mpq_path[PATH_MAX];
    char browser_path[PATH_MAX];
    char archive_path[PATH_MAX];
    char out_dir[PATH_MAX];
    char extra_args[1024];
    int selected_tool;
    int selected_field;
    int selected_entry;
    int browser_scroll;
    int history_scroll;
    int output_scroll;
    int dragging_entry;
    int filter_mode;
    bool pick_to_extra;
    bool run_on_open;
    bool running;
    bool needs_browser_refresh;
    bool needs_help_refresh;
    char tool_help[8192];
    char command[4096];
    char output[MAX_OUTPUT];
    mpq_entry_t entries[MAX_ENTRIES];
    int entry_count;
    char history[MAX_HISTORY][4096];
    int history_count;
} app_t;

static const tool_t tools[MAX_TOOLS] = {
    { "mpqtool", "mpqtool", "List, inspect, pack, and dump MPQ files.", "-mpq {mpq} cat {file}" },
    { "blpconvert", "blpconvert", "Convert MPQ BLP textures to JPEG.", "-mpq {mpq} jpg {out} {path}" },
    { "fdftool", "fdftool", "Inspect FDF frame definitions.", "-mpq {mpq} -fdf {file} --info" },
    { "mdxtool", "mdxtool", "Inspect or preview MDX models.", "-mpq {mpq} -model {file} --info" },
    { "maptool", "maptool", "Open a Warcraft III map.", "-mpq {mpq} -map {file}" },
};

enum {
    FILTER_ALL,
    FILTER_MODELS,
    FILTER_TEXTURES,
    FILTER_UI,
    FILTER_MAPS,
    FILTER_COUNT
};

static const char *filter_names[FILTER_COUNT] = {
    "All files",
    "Models",
    "Textures",
    "UI",
    "Maps",
};

typedef enum icon_kind_e {
    ICON_FILE,
    ICON_FOLDER,
    ICON_ARCHIVE,
    ICON_TEXTURE,
    ICON_MODEL,
    ICON_MAP,
    ICON_UI,
    ICON_TOOL,
    ICON_TERMINAL,
    ICON_OUTPUT,
    ICON_ARGS
} icon_kind_t;

static void render(app_t *app);

static const char *font_rows(char c) {
    switch ((unsigned char)c) {
        case 'A': return "01110""10001""10001""11111""10001""10001""10001";
        case 'B': return "11110""10001""10001""11110""10001""10001""11110";
        case 'C': return "01111""10000""10000""10000""10000""10000""01111";
        case 'D': return "11110""10001""10001""10001""10001""10001""11110";
        case 'E': return "11111""10000""10000""11110""10000""10000""11111";
        case 'F': return "11111""10000""10000""11110""10000""10000""10000";
        case 'G': return "01111""10000""10000""10111""10001""10001""01111";
        case 'H': return "10001""10001""10001""11111""10001""10001""10001";
        case 'I': return "11111""00100""00100""00100""00100""00100""11111";
        case 'J': return "00111""00010""00010""00010""10010""10010""01100";
        case 'K': return "10001""10010""10100""11000""10100""10010""10001";
        case 'L': return "10000""10000""10000""10000""10000""10000""11111";
        case 'M': return "10001""11011""10101""10101""10001""10001""10001";
        case 'N': return "10001""11001""10101""10011""10001""10001""10001";
        case 'O': return "01110""10001""10001""10001""10001""10001""01110";
        case 'P': return "11110""10001""10001""11110""10000""10000""10000";
        case 'Q': return "01110""10001""10001""10001""10101""10010""01101";
        case 'R': return "11110""10001""10001""11110""10100""10010""10001";
        case 'S': return "01111""10000""10000""01110""00001""00001""11110";
        case 'T': return "11111""00100""00100""00100""00100""00100""00100";
        case 'U': return "10001""10001""10001""10001""10001""10001""01110";
        case 'V': return "10001""10001""10001""10001""10001""01010""00100";
        case 'W': return "10001""10001""10001""10101""10101""10101""01010";
        case 'X': return "10001""10001""01010""00100""01010""10001""10001";
        case 'Y': return "10001""10001""01010""00100""00100""00100""00100";
        case 'Z': return "11111""00001""00010""00100""01000""10000""11111";
        case 'a': return "00000""00000""01110""00001""01111""10001""01111";
        case 'b': return "10000""10000""10110""11001""10001""10001""11110";
        case 'c': return "00000""00000""01111""10000""10000""10000""01111";
        case 'd': return "00001""00001""01101""10011""10001""10001""01111";
        case 'e': return "00000""00000""01110""10001""11111""10000""01110";
        case 'f': return "00110""01001""01000""11100""01000""01000""01000";
        case 'g': return "00000""00000""01111""10001""01111""00001""01110";
        case 'h': return "10000""10000""10110""11001""10001""10001""10001";
        case 'i': return "00100""00000""01100""00100""00100""00100""01110";
        case 'j': return "00010""00000""00110""00010""00010""10010""01100";
        case 'k': return "10000""10000""10010""10100""11000""10100""10010";
        case 'l': return "01100""00100""00100""00100""00100""00100""01110";
        case 'm': return "00000""00000""11010""10101""10101""10101""10101";
        case 'n': return "00000""00000""10110""11001""10001""10001""10001";
        case 'o': return "00000""00000""01110""10001""10001""10001""01110";
        case 'p': return "00000""00000""11110""10001""11110""10000""10000";
        case 'q': return "00000""00000""01111""10001""01111""00001""00001";
        case 'r': return "00000""00000""10110""11001""10000""10000""10000";
        case 's': return "00000""00000""01111""10000""01110""00001""11110";
        case 't': return "01000""01000""11100""01000""01000""01001""00110";
        case 'u': return "00000""00000""10001""10001""10001""10011""01101";
        case 'v': return "00000""00000""10001""10001""10001""01010""00100";
        case 'w': return "00000""00000""10001""10101""10101""10101""01010";
        case 'x': return "00000""00000""10001""01010""00100""01010""10001";
        case 'y': return "00000""00000""10001""10001""01111""00001""01110";
        case 'z': return "00000""00000""11111""00010""00100""01000""11111";
        case '0': return "01110""10001""10011""10101""11001""10001""01110";
        case '1': return "00100""01100""00100""00100""00100""00100""01110";
        case '2': return "01110""10001""00001""00010""00100""01000""11111";
        case '3': return "11110""00001""00001""01110""00001""00001""11110";
        case '4': return "00010""00110""01010""10010""11111""00010""00010";
        case '5': return "11111""10000""10000""11110""00001""00001""11110";
        case '6': return "01110""10000""10000""11110""10001""10001""01110";
        case '7': return "11111""00001""00010""00100""01000""01000""01000";
        case '8': return "01110""10001""10001""01110""10001""10001""01110";
        case '9': return "01110""10001""10001""01111""00001""00001""01110";
        case '.': return "00000""00000""00000""00000""00000""01100""01100";
        case ',': return "00000""00000""00000""00000""01100""01100""01000";
        case ':': return "00000""01100""01100""00000""01100""01100""00000";
        case ';': return "00000""01100""01100""00000""01100""01100""01000";
        case '-': return "00000""00000""00000""11111""00000""00000""00000";
        case '_': return "00000""00000""00000""00000""00000""00000""11111";
        case '+': return "00000""00100""00100""11111""00100""00100""00000";
        case '/': return "00001""00010""00010""00100""01000""01000""10000";
        case '\\': return "10000""01000""01000""00100""00010""00010""00001";
        case '*': return "00000""10101""01110""11111""01110""10101""00000";
        case '?': return "01110""10001""00001""00010""00100""00000""00100";
        case '!': return "00100""00100""00100""00100""00100""00000""00100";
        case '"': return "01010""01010""01010""00000""00000""00000""00000";
        case '\'': return "00100""00100""01000""00000""00000""00000""00000";
        case '#': return "01010""11111""01010""01010""11111""01010""00000";
        case '$': return "00100""01111""10100""01110""00101""11110""00100";
        case '%': return "11001""11010""00010""00100""01000""01011""10011";
        case '&': return "01100""10010""10100""01000""10101""10010""01101";
        case '(': return "00010""00100""01000""01000""01000""00100""00010";
        case ')': return "01000""00100""00010""00010""00010""00100""01000";
        case '[': return "01110""01000""01000""01000""01000""01000""01110";
        case ']': return "01110""00010""00010""00010""00010""00010""01110";
        case '<': return "00010""00100""01000""10000""01000""00100""00010";
        case '>': return "01000""00100""00010""00001""00010""00100""01000";
        case '=': return "00000""11111""00000""11111""00000""00000""00000";
        case '@': return "01110""10001""10111""10101""10111""10000""01110";
        case '|': return "00100""00100""00100""00100""00100""00100""00100";
        case '~': return "00000""00000""01001""10110""00000""00000""00000";
        case ' ': return "00000""00000""00000""00000""00000""00000""00000";
        default:  return "11111""10001""00010""00100""00000""00100""00100";
    }
}

static int lowres_coord(int value, int scale) {
    return value / scale;
}

static int lowres_span(int value, int scale) {
    return (value + scale - 1) / scale;
}

static Uint32 lowres_color(lowres_t *lr, int rgb) {
    return SDL_MapRGBA(lr->surf->format,
                       (rgb >> 16) & 255,
                       (rgb >> 8) & 255,
                       rgb & 255,
                       255);
}

static lowres_t lowres_create(SDL_Renderer *renderer, int screen_w, int screen_h, int scale) {
    lowres_t lr;
    memset(&lr, 0, sizeof(lr));
    lr.scale = scale > 0 ? scale : 1;
    lr.width = lowres_span(screen_w, lr.scale);
    lr.height = lowres_span(screen_h, lr.scale);
    if (lr.width < 1) {
        lr.width = 1;
    }
    if (lr.height < 1) {
        lr.height = 1;
    }
    lr.surf = SDL_CreateRGBSurfaceWithFormat(0, lr.width, lr.height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!lr.surf) {
        return lr;
    }
    lr.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, lr.width, lr.height);
    if (!lr.texture) {
        SDL_FreeSurface(lr.surf);
        lr.surf = NULL;
        return lr;
    }
    SDL_SetTextureScaleMode(lr.texture, SDL_ScaleModeNearest);
    return lr;
}

static void lowres_destroy(lowres_t *lr) {
    if (lr->texture) {
        SDL_DestroyTexture(lr->texture);
        lr->texture = NULL;
    }
    if (lr->surf) {
        SDL_FreeSurface(lr->surf);
        lr->surf = NULL;
    }
    lr->width = 0;
    lr->height = 0;
}

static void lowres_ensure_size(lowres_t *lr, SDL_Renderer *renderer, int screen_w, int screen_h) {
    int wanted_w = lowres_span(screen_w, lr->scale);
    int wanted_h = lowres_span(screen_h, lr->scale);
    if (wanted_w < 1) {
        wanted_w = 1;
    }
    if (wanted_h < 1) {
        wanted_h = 1;
    }
    if (lr->surf && lr->texture && lr->width == wanted_w && lr->height == wanted_h) {
        return;
    }
    lowres_destroy(lr);
    *lr = lowres_create(renderer, screen_w, screen_h, lr->scale);
}

static void lowres_clear(lowres_t *lr, int color) {
    if (!lr->surf) {
        return;
    }
    SDL_FillRect(lr->surf, NULL, lowres_color(lr, color));
}

static void lowres_fill_rect(lowres_t *lr, rect_t rc, int color) {
    if (!lr->surf) {
        return;
    }
    SDL_Rect s = {
        lowres_coord(rc.x, lr->scale),
        lowres_coord(rc.y, lr->scale),
        lowres_span(rc.w, lr->scale),
        lowres_span(rc.h, lr->scale),
    };
    SDL_FillRect(lr->surf, &s, lowres_color(lr, color));
}

static void lowres_stroke_rect(lowres_t *lr, rect_t rc, int color) {
    if (!lr->surf) {
        return;
    }
    Uint32 pixel = lowres_color(lr, color);
    int x = lowres_coord(rc.x, lr->scale);
    int y = lowres_coord(rc.y, lr->scale);
    int w = lowres_span(rc.w, lr->scale);
    int h = lowres_span(rc.h, lr->scale);
    Uint32 *pixels = (Uint32 *)lr->surf->pixels;
    int pitch = lr->surf->pitch / 4;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= lr->width || y >= lr->height) {
        return;
    }
    if (x + w > lr->width) {
        w = lr->width - x;
    }
    if (y + h > lr->height) {
        h = lr->height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int xx = 0; xx < w; xx++) {
        pixels[y * pitch + (x + xx)] = pixel;
        pixels[(y + h - 1) * pitch + (x + xx)] = pixel;
    }
    for (int yy = 0; yy < h; yy++) {
        pixels[(y + yy) * pitch + x] = pixel;
        pixels[(y + yy) * pitch + (x + w - 1)] = pixel;
    }
}

static void lowres_draw_char(lowres_t *lr, char c, int x, int y, int scale, int color) {
    if (!lr->surf) {
        return;
    }
    const char *rows = font_rows(c);
    Uint32 *pixels = (Uint32 *)lr->surf->pixels;
    int pitch = lr->surf->pitch / 4;
    Uint32 fg_color = lowres_color(lr, color);

    for (int yy = 0; yy < 7; yy++) {
        for (int xx = 0; xx < 5; xx++) {
            if (rows[yy * 5 + xx] == '1') {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + xx * scale + sx;
                        int py = y + yy * scale + sy;
                        if (px >= 0 && py >= 0 && px < lr->width && py < lr->height) {
                            pixels[py * pitch + px] = fg_color;
                        }
                    }
                }
            }
        }
    }
}

static void lowres_draw_text_clip(lowres_t *lr, const char *text, int x, int y, int scale, int color, int max_w) {
    int text_scale = scale / lr->scale;
    int cx;
    int x0;
    int max_w_lr;
    int step;
    if (!text || !lr->surf) {
        return;
    }
    if (text_scale < 1) {
        text_scale = 1;
    }
    x0 = lowres_coord(x, lr->scale);
    cx = x0;
    max_w_lr = lowres_span(max_w, lr->scale);
    step = 6 * text_scale;
    for (; *text; text++) {
        if (*text == '\n' || cx + 5 * text_scale > x0 + max_w_lr) {
            break;
        }
        lowres_draw_char(lr, *text, cx, lowres_coord(y, lr->scale), text_scale, color);
        cx += step;
    }
}

static void lowres_draw_label(lowres_t *lr, rect_t rc, const char *text, bool active) {
    lowres_fill_rect(lr, rc, active ? 0x2c4a5a : 0x18242a);
    lowres_stroke_rect(lr, rc, active ? 0x86d9ff : 0x42525b);
    lowres_draw_text_clip(lr, text, rc.x + 8, rc.y + 8, 2, active ? 0xffffff : 0xc9d6d3, rc.w - 16);
}

static void lowres_draw_icon(lowres_t *lr, icon_kind_t icon, int x, int y, int color, int accent) {
    rect_t body = { x + 2, y + 5, 15, 12 };
    rect_t tab = { x + 3, y + 2, 8, 5 };
    switch (icon) {
        case ICON_FOLDER:
            lowres_fill_rect(lr, tab, accent);
            lowres_fill_rect(lr, body, color);
            lowres_stroke_rect(lr, (rect_t){ x + 2, y + 4, 15, 13 }, 0x0c1113);
            break;
        case ICON_ARCHIVE:
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 2, 13, 15 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 3, y + 2, 13, 15 }, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 6, y + 3, 3, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 10, y + 6, 3, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 6, y + 9, 3, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 10, y + 12, 3, 2 }, accent);
            break;
        case ICON_TEXTURE:
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 5, 4, 4 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 10, y + 10, 4, 4 }, accent);
            break;
        case ICON_MODEL:
            lowres_fill_rect(lr, (rect_t){ x + 8, y + 2, 5, 5 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 4, y + 7, 5, 5 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 11, y + 10, 5, 5 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 4, y + 7, 12, 8 }, 0x0c1113);
            break;
        case ICON_MAP:
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 5, 3, 9 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 11, y + 5, 3, 9 }, accent);
            break;
        case ICON_UI:
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 3, y + 3, 13, 13 }, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 6, 9, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 11, 9, 2 }, accent);
            break;
        case ICON_TOOL:
            lowres_fill_rect(lr, (rect_t){ x + 4, y + 5, 11, 4 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 7, y + 9, 5, 7 }, accent);
            lowres_stroke_rect(lr, (rect_t){ x + 4, y + 5, 11, 11 }, 0x0c1113);
            break;
        case ICON_TERMINAL:
            lowres_fill_rect(lr, (rect_t){ x + 2, y + 4, 15, 11 }, color);
            lowres_stroke_rect(lr, (rect_t){ x + 2, y + 4, 15, 11 }, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 7, 5, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 11, y + 11, 4, 2 }, accent);
            break;
        case ICON_OUTPUT:
            lowres_fill_rect(lr, body, color);
            lowres_stroke_rect(lr, body, 0x0c1113);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 8, 9, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 5, y + 12, 6, 2 }, accent);
            break;
        case ICON_ARGS:
            lowres_fill_rect(lr, (rect_t){ x + 4, y + 4, 3, 11 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 9, y + 4, 3, 11 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 14, y + 4, 3, 11 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 8, 15, 2 }, accent);
            lowres_fill_rect(lr, (rect_t){ x + 3, y + 13, 15, 2 }, accent);
            break;
        case ICON_FILE:
        default:
            lowres_fill_rect(lr, (rect_t){ x + 4, y + 2, 11, 15 }, color);
            lowres_fill_rect(lr, (rect_t){ x + 11, y + 2, 4, 4 }, accent);
            lowres_stroke_rect(lr, (rect_t){ x + 4, y + 2, 11, 15 }, 0x0c1113);
            break;
    }
}

static void lowres_draw_checkbox(lowres_t *lr, rect_t rc, const char *text, bool checked, bool active) {
    lowres_fill_rect(lr, rc, active ? 0x20353c : 0x11191c);
    lowres_stroke_rect(lr, rc, active ? 0x86d9ff : 0x42525b);
    rect_t box = { rc.x + 8, rc.y + 7, 14, 14 };
    lowres_fill_rect(lr, box, checked ? 0x2f6b54 : 0x0c1113);
    lowres_stroke_rect(lr, box, checked ? 0x9ff2b2 : 0x6a7b7c);
    if (checked) {
        lowres_fill_rect(lr, (rect_t){ box.x + 3, box.y + 7, 3, 3 }, 0xffffff);
        lowres_fill_rect(lr, (rect_t){ box.x + 6, box.y + 9, 3, 3 }, 0xffffff);
        lowres_fill_rect(lr, (rect_t){ box.x + 9, box.y + 4, 3, 8 }, 0xffffff);
    }
    lowres_draw_text_clip(lr, text, rc.x + 30, rc.y + 8, 2, active ? 0xffffff : 0xc9d6d3, rc.w - 38);
}

static void lowres_draw_dropdown(lowres_t *lr, rect_t rc, const char *label, const char *value, bool active) {
    char text[128];
    snprintf(text, sizeof(text), "%s: %s", label, value);
    lowres_fill_rect(lr, rc, active ? 0x20353c : 0x11191c);
    lowres_stroke_rect(lr, rc, active ? 0x86d9ff : 0x42525b);
    lowres_draw_text_clip(lr, text, rc.x + 8, rc.y + 8, 2, active ? 0xffffff : 0xc9d6d3, rc.w - 34);
    lowres_fill_rect(lr, (rect_t){ rc.x + rc.w - 21, rc.y + 10, 13, 2 }, active ? 0xffffff : 0x91a6a6);
    lowres_fill_rect(lr, (rect_t){ rc.x + rc.w - 18, rc.y + 13, 7, 2 }, active ? 0xffffff : 0x91a6a6);
    lowres_fill_rect(lr, (rect_t){ rc.x + rc.w - 15, rc.y + 16, 2, 2 }, active ? 0xffffff : 0x91a6a6);
}

static void lowres_present(lowres_t *lr, SDL_Renderer *renderer, int screen_w, int screen_h) {
    SDL_Rect dst;
    if (!lr->surf || !lr->texture) {
        return;
    }
    SDL_UpdateTexture(lr->texture, NULL, lr->surf->pixels, lr->surf->pitch);
    dst.x = 0;
    dst.y = 0;
    dst.w = screen_w;
    dst.h = screen_h;
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, lr->texture, NULL, &dst);
    SDL_RenderPresent(renderer);
}

static bool point_in(rect_t rc, int x, int y) {
    return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h;
}

static bool equals_ci(const char *a, const char *b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

static const char *path_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *sep = slash > backslash ? slash : backslash;
    return dot && (!sep || dot > sep) ? dot + 1 : "";
}

static bool ext_is_one_of(const char *ext, const char *a, const char *b, const char *c, const char *d) {
    return (a && equals_ci(ext, a)) ||
           (b && equals_ci(ext, b)) ||
           (c && equals_ci(ext, c)) ||
           (d && equals_ci(ext, d));
}

static bool entry_matches_filter(app_t *app, const char *name, bool is_dir) {
    if (is_dir || app->filter_mode == FILTER_ALL) {
        return true;
    }
    const char *ext = path_ext(name);
    switch (app->filter_mode) {
        case FILTER_MODELS:
            return ext_is_one_of(ext, "mdx", "mdl", "m3", NULL);
        case FILTER_TEXTURES:
            return ext_is_one_of(ext, "blp", "tga", "jpg", "jpeg") || equals_ci(ext, "dds");
        case FILTER_UI:
            return ext_is_one_of(ext, "fdf", "toc", "txt", NULL);
        case FILTER_MAPS:
            return ext_is_one_of(ext, "w3m", "w3x", "w3n", NULL);
        default:
            return true;
    }
}

static icon_kind_t entry_icon(const mpq_entry_t *entry) {
    if (entry->is_dir) {
        return ICON_FOLDER;
    }
    const char *ext = path_ext(entry->name);
    if (ext_is_one_of(ext, "mpq", "w3m", "w3x", "w3n")) {
        return equals_ci(ext, "mpq") ? ICON_ARCHIVE : ICON_MAP;
    }
    if (ext_is_one_of(ext, "blp", "tga", "jpg", "jpeg") || equals_ci(ext, "dds")) {
        return ICON_TEXTURE;
    }
    if (ext_is_one_of(ext, "mdx", "mdl", "m3", NULL)) {
        return ICON_MODEL;
    }
    if (ext_is_one_of(ext, "fdf", "toc", "txt", NULL)) {
        return ICON_UI;
    }
    return ICON_FILE;
}

static icon_kind_t tool_icon(int tool_index) {
    switch (tool_index) {
        case 0: return ICON_ARCHIVE;
        case 1: return ICON_TEXTURE;
        case 2: return ICON_UI;
        case 3: return ICON_MODEL;
        case 4: return ICON_MAP;
        case 5: return ICON_TERMINAL;
        default: return ICON_TOOL;
    }
}

static icon_kind_t field_icon(int field_index) {
    switch (field_index) {
        case 0: return ICON_ARCHIVE;
        case 1: return ICON_FILE;
        case 2: return ICON_OUTPUT;
        case 3: return ICON_ARGS;
        default: return ICON_FILE;
    }
}

static void dirname_of(const char *path, char *out, size_t out_size) {
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *back = strrchr(path, '\\');
    if (!slash || (back && back > slash)) {
        slash = back;
    }
#endif
    if (!slash) {
        snprintf(out, out_size, ".");
        return;
    }
    size_t n = (size_t)(slash - path);
    if (n >= out_size) {
        n = out_size - 1;
    }
    memcpy(out, path, n);
    out[n] = 0;
}

static void path_join(char *out, size_t out_size, const char *dir, const char *name) {
    size_t len = strlen(dir);
    const char *sep = (len > 0 && dir[len - 1] == '/') ? "" : "/";
    snprintf(out, out_size, "%s%s%s", dir, sep, name);
}

static void shell_quote_append(char *out, size_t out_size, const char *s) {
    strncat(out, "'", out_size - strlen(out) - 1);
    for (; s && *s; s++) {
        if (*s == '\'') {
            strncat(out, "'\\''", out_size - strlen(out) - 1);
        } else {
            char tmp[2] = { *s, 0 };
            strncat(out, tmp, out_size - strlen(out) - 1);
        }
    }
    strncat(out, "'", out_size - strlen(out) - 1);
}

static char *run_capture(const char *cmd) {
    FILE *fp;
    char *buf = malloc(MAX_OUTPUT);
    size_t used = 0;
    char shell_cmd[8192];
    if (!buf) {
        return NULL;
    }
    buf[0] = 0;
    snprintf(shell_cmd, sizeof(shell_cmd), "%s 2>&1", cmd);
    fp = popen(shell_cmd, "r");
    if (!fp) {
        snprintf(buf, MAX_OUTPUT, "popen failed: %s\n", strerror(errno));
        return buf;
    }
    while (used + 1 < MAX_OUTPUT) {
        size_t got = fread(buf + used, 1, MAX_OUTPUT - used - 1, fp);
        used += got;
        if (got == 0) {
            break;
        }
    }
    buf[used] = 0;
    pclose(fp);
    return buf;
}

static void add_history(app_t *app, const char *cmd) {
    if (!cmd || !*cmd) {
        return;
    }
    if (app->history_count == MAX_HISTORY) {
        memmove(app->history, app->history + 1, sizeof(app->history[0]) * (MAX_HISTORY - 1));
        app->history_count--;
    }
    snprintf(app->history[app->history_count++], sizeof(app->history[0]), "%s", cmd);
}

static void save_history(app_t *app) {
    FILE *f = fopen(app->history_path, "w");
    if (!f) {
        return;
    }
    for (int i = 0; i < app->history_count; i++) {
        fprintf(f, "%s\n", app->history[i]);
    }
    fclose(f);
}

static void load_history(app_t *app) {
    FILE *f = fopen(app->history_path, "r");
    char line[4096];
    if (!f) {
        return;
    }
    while (fgets(line, sizeof(line), f) && app->history_count < MAX_HISTORY) {
        line[strcspn(line, "\r\n")] = 0;
        if (*line) {
            snprintf(app->history[app->history_count++], sizeof(app->history[0]), "%s", line);
        }
    }
    fclose(f);
}

static void append_bounded(char *dst, size_t dst_size, const char *src) {
    size_t used, copy;
    if (!dst || !src || !dst_size) return;
    used = strlen(dst);
    if (used >= dst_size) return;
    copy = strlen(src);
    if (copy > dst_size - used - 1) copy = dst_size - used - 1;
    memmove(dst + used, src, copy);
    dst[used + copy] = '\0';
}

static void build_command(app_t *app) {
    const tool_t *tool = &tools[app->selected_tool];
    char exe[PATH_MAX];
    char *dst = app->command;
    size_t left = sizeof(app->command);
    path_join(exe, sizeof(exe), app->exe_dir, tool->exe);
    app->command[0] = 0;
    shell_quote_append(app->command, sizeof(app->command), exe);
    append_bounded(app->command, sizeof(app->command), " ");

    const char *p = tool->template_args;
    while (*p && strlen(app->command) + 8 < sizeof(app->command)) {
        if (!strncmp(p, "{mpq}", 5)) {
            shell_quote_append(app->command, sizeof(app->command), app->mpq_path);
            p += 5;
        } else if (!strncmp(p, "{file}", 6)) {
            shell_quote_append(app->command, sizeof(app->command), app->archive_path);
            p += 6;
        } else if (!strncmp(p, "{path}", 6)) {
            shell_quote_append(app->command, sizeof(app->command), app->archive_path);
            p += 6;
        } else if (!strncmp(p, "{out}", 5)) {
            shell_quote_append(app->command, sizeof(app->command), app->out_dir);
            p += 5;
        } else {
            char tmp[2] = { *p++, 0 };
            append_bounded(app->command, sizeof(app->command), tmp);
        }
    }
    if (app->extra_args[0]) {
        append_bounded(app->command, sizeof(app->command), " ");
        append_bounded(app->command, sizeof(app->command), app->extra_args);
    }
    (void)dst;
    (void)left;
}

static void refresh_help(app_t *app) {
    char exe[PATH_MAX], cmd[PATH_MAX + 16];
    char *out;
    path_join(exe, sizeof(exe), app->exe_dir, tools[app->selected_tool].exe);
    cmd[0] = 0;
    shell_quote_append(cmd, sizeof(cmd), exe);
    strncat(cmd, " -?", sizeof(cmd) - strlen(cmd) - 1);
    out = run_capture(cmd);
    if (out) {
        snprintf(app->tool_help, sizeof(app->tool_help), "%s", out);
        free(out);
    }
    app->needs_help_refresh = false;
}

static int entry_cmp(const void *a, const void *b) {
    const mpq_entry_t *ea = (const mpq_entry_t *)a;
    const mpq_entry_t *eb = (const mpq_entry_t *)b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
#ifdef _WIN32
    return _stricmp(ea->name, eb->name);
#else
    return strcasecmp(ea->name, eb->name);
#endif
}

static void parent_path(char *path) {
    char *slash;
    if (!path[0]) {
        return;
    }
    slash = strrchr(path, '\\');
    if (!slash) {
        slash = strrchr(path, '/');
    }
    if (slash) {
        *slash = 0;
    } else {
        path[0] = 0;
    }
}

static void append_archive_child(app_t *app, const char *name, bool is_dir) {
    size_t len = strlen(is_dir ? app->browser_path : app->archive_path);
    if (is_dir) {
        if (len > 0) {
            append_bounded(app->browser_path, sizeof(app->browser_path), "\\");
        }
        append_bounded(app->browser_path, sizeof(app->browser_path), name);
    } else {
        if (len > 0) {
            append_bounded(app->archive_path, sizeof(app->archive_path), "\\");
        }
        append_bounded(app->archive_path, sizeof(app->archive_path), name);
    }
}

static void refresh_browser(app_t *app) {
    char cmd[8192];
    char *out;
    app->entry_count = 0;
    cmd[0] = 0;
    shell_quote_append(cmd, sizeof(cmd), app->mpqtool_path);
    strncat(cmd, " -mpq ", sizeof(cmd) - strlen(cmd) - 1);
    shell_quote_append(cmd, sizeof(cmd), app->mpq_path);
    strncat(cmd, " ls ", sizeof(cmd) - strlen(cmd) - 1);
    shell_quote_append(cmd, sizeof(cmd), app->browser_path);
    out = run_capture(cmd);
    if (!out) {
        return;
    }

    if (app->browser_path[0] && app->entry_count < MAX_ENTRIES) {
        snprintf(app->entries[app->entry_count].name, sizeof(app->entries[0].name), "..");
        app->entries[app->entry_count].is_dir = true;
        app->entry_count++;
    }

    char *save = NULL;
    for (char *line = strtok_r(out, "\r\n", &save); line && app->entry_count < MAX_ENTRIES; line = strtok_r(NULL, "\r\n", &save)) {
        size_t n = strlen(line);
        if (n == 0 || strstr(line, "Cannot open archive")) {
            continue;
        }
        bool is_dir = line[n - 1] == '/';
        if (is_dir) {
            line[n - 1] = 0;
        }
        if (!entry_matches_filter(app, line, is_dir)) {
            continue;
        }
        snprintf(app->entries[app->entry_count].name, sizeof(app->entries[0].name), "%s", line);
        app->entries[app->entry_count].is_dir = is_dir;
        app->entry_count++;
    }
    free(out);

    if (app->entry_count > (app->browser_path[0] ? 1 : 0)) {
        qsort(app->entries + (app->browser_path[0] ? 1 : 0),
              (size_t)(app->entry_count - (app->browser_path[0] ? 1 : 0)),
              sizeof(app->entries[0]), entry_cmp);
    }
    app->selected_entry = app->entry_count ? 0 : -1;
    app->browser_scroll = 0;
    app->needs_browser_refresh = false;
}

static void run_current_command(app_t *app) {
    build_command(app);
    add_history(app, app->command);
    save_history(app);
    snprintf(app->output, sizeof(app->output), "Running:\n%s\n\n", app->command);
    render(app);
    char *out = run_capture(app->command);
    if (out) {
        strncat(app->output, out, sizeof(app->output) - strlen(app->output) - 1);
        free(out);
    }
    app->output_scroll = 0;
}

static void put_selected_entry_in_field(app_t *app) {
    if (app->dragging_entry < 0 || app->dragging_entry >= app->entry_count) {
        return;
    }
    mpq_entry_t *e = &app->entries[app->dragging_entry];
    if (!strcmp(e->name, "..")) {
        return;
    }
    char value[PATH_MAX];
    snprintf(value, sizeof(value), "%s", app->browser_path);
    if (value[0]) {
        strncat(value, "\\", sizeof(value) - strlen(value) - 1);
    }
    strncat(value, e->name, sizeof(value) - strlen(value) - 1);
    if (app->pick_to_extra) {
        if (app->extra_args[0]) {
            strncat(app->extra_args, " ", sizeof(app->extra_args) - strlen(app->extra_args) - 1);
        }
        shell_quote_append(app->extra_args, sizeof(app->extra_args), value);
    } else if (app->selected_field == 1) {
        snprintf(app->archive_path, sizeof(app->archive_path), "%s", value);
    } else if (app->selected_field == 3) {
        if (app->extra_args[0]) {
            strncat(app->extra_args, " ", sizeof(app->extra_args) - strlen(app->extra_args) - 1);
        }
        shell_quote_append(app->extra_args, sizeof(app->extra_args), value);
    }
}

static void draw_panel_title(lowres_t *lr, rect_t rc, const char *title) {
    lowres_fill_rect(lr, (rect_t){ rc.x, rc.y, rc.w, 26 }, 0x22343c);
    lowres_stroke_rect(lr, rc, 0x4f6972);
    lowres_draw_text_clip(lr, title, rc.x + 8, rc.y + 7, 2, 0xf6e8b1, rc.w - 16);
}

static void draw_lines(lowres_t *lr, const char *text, rect_t rc, int scroll, int color) {
    int y = rc.y + 8;
    int line_h = 18;
    int skip = scroll;
    const char *p = text ? text : "";
    while (*p && y + line_h <= rc.y + rc.h) {
        char line[512];
        int n = 0;
        while (*p && *p != '\n' && n < (int)sizeof(line) - 1) {
            line[n++] = *p++;
        }
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        line[n] = 0;
        if (skip > 0) {
            skip--;
            continue;
        }
        lowres_draw_text_clip(lr, line, rc.x + 8, y, 2, color, rc.w - 16);
        y += line_h;
    }
}

static void render(app_t *app) {
    lowres_t *lr = &app->lowres;
    rect_t header = { 0, 0, app->width, 42 };
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    rect_t center = { browser.x + browser.w + 10, 52, app->width * 32 / 100, app->height - 240 };
    rect_t right = { center.x + center.w + 10, 52, app->width - center.x - center.w - 20, app->height - 240 };
    rect_t bottom = { 10, app->height - 178, app->width - 20, 168 };

    lowres_clear(lr, 0x101619);
    lowres_fill_rect(lr, header, 0x1c2a30);
    lowres_draw_text_clip(lr, "OPENWARCRAFT3 TOOLBOX", 14, 12, 3, 0xffffff, 420);
    lowres_draw_text_clip(lr, "F5 RUN  F6 HELP  ENTER OPEN  F FILTER  D DRAG/COPY  ESC QUIT", 460, 15, 2, 0xa9bbb8, app->width - 470);

    draw_panel_title(lr, browser, "MPQ BROWSER");
    lowres_draw_text_clip(lr, app->browser_path[0] ? app->browser_path : "\\", browser.x + 8, browser.y + 34, 2, 0x8bd7ff, browser.w - 16);
    rect_t filter = { browser.x + 8, browser.y + 56, browser.w - 16, 28 };
    lowres_draw_dropdown(lr, filter, "Filter", filter_names[app->filter_mode], true);
    int row_y = browser.y + 90;
    int rows = (browser.h - 96) / 20;
    for (int i = 0; i < rows && i + app->browser_scroll < app->entry_count; i++) {
        int idx = i + app->browser_scroll;
        rect_t row = { browser.x + 6, row_y + i * 20, browser.w - 12, 18 };
        if (idx == app->selected_entry) {
            lowres_fill_rect(lr, row, 0x315263);
        }
        icon_kind_t icon = entry_icon(&app->entries[idx]);
        int icon_color = app->entries[idx].is_dir ? 0xe5ba52 : 0x93adb3;
        lowres_draw_icon(lr, icon, row.x + 3, row.y + 1, icon_color, app->entries[idx].is_dir ? 0xf7d989 : 0xc7d2d0);
        lowres_draw_text_clip(lr, app->entries[idx].name, row.x + 28, row.y + 3, 2, app->entries[idx].is_dir ? 0xf8d77e : 0xdde9e4, row.w - 34);
    }

    draw_panel_title(lr, center, "TOOLS AND FIELDS");
    int y = center.y + 36;
    for (int i = 0; i < MAX_TOOLS; i++) {
        rect_t row = { center.x + 8, y, center.w - 16, 30 };
        lowres_draw_label(lr, row, "", i == app->selected_tool);
        lowres_draw_icon(lr, tool_icon(i), row.x + 8, row.y + 5, i == app->selected_tool ? 0xf6e8b1 : 0x7fa4aa, 0xffffff);
        lowres_draw_text_clip(lr, tools[i].name, row.x + 34, row.y + 8, 2, i == app->selected_tool ? 0xffffff : 0xc9d6d3, row.w - 42);
        y += 34;
    }
    y += 6;
    const char *field_names[MAX_FIELDS] = { "MPQ", "ARCHIVE PATH / FILE", "OUTPUT DIR", "EXTRA ARGS" };
    const char *field_values[MAX_FIELDS] = { app->mpq_path, app->archive_path, app->out_dir, app->extra_args };
    for (int i = 0; i < MAX_FIELDS; i++) {
        lowres_draw_text_clip(lr, field_names[i], center.x + 8, y, 2, 0x91a6a6, center.w - 16);
        y += 18;
        rect_t fr = { center.x + 8, y, center.w - 16, 28 };
        lowres_draw_label(lr, fr, "", i == app->selected_field);
        lowres_draw_icon(lr, field_icon(i), fr.x + 8, fr.y + 5, i == app->selected_field ? 0xf6e8b1 : 0x7fa4aa, 0xffffff);
        lowres_draw_text_clip(lr, field_values[i], fr.x + 34, fr.y + 8, 2, i == app->selected_field ? 0xffffff : 0xc9d6d3, fr.w - 42);
        y += 34;
    }
    build_command(app);
    lowres_draw_text_clip(lr, "COMMAND", center.x + 8, y, 2, 0x91a6a6, center.w - 16);
    y += 18;
    lowres_fill_rect(lr, (rect_t){ center.x + 8, y, center.w - 16, 58 }, 0x0c1113);
    lowres_stroke_rect(lr, (rect_t){ center.x + 8, y, center.w - 16, 58 }, 0x4f6972);
    draw_lines(lr, app->command, (rect_t){ center.x + 8, y, center.w - 16, 58 }, 0, 0xdde9e4);

    draw_panel_title(lr, right, "HELP / ACCEPTED SETTINGS");
    lowres_draw_text_clip(lr, tools[app->selected_tool].summary, right.x + 8, right.y + 34, 2, 0xdde9e4, right.w - 16);
    lowres_draw_checkbox(lr, (rect_t){ right.x + 8, right.y + 58, right.w - 16, 28 }, "Copy picks to extra args", app->pick_to_extra, false);
    lowres_draw_checkbox(lr, (rect_t){ right.x + 8, right.y + 92, right.w - 16, 28 }, "Run file on open", app->run_on_open, false);
    draw_lines(lr, app->tool_help, (rect_t){ right.x + 4, right.y + 128, right.w - 8, right.h - 134 }, app->output_scroll, 0xa9bbb8);

    draw_panel_title(lr, bottom, "OUTPUT AND RECENT COMMANDS");
    rect_t out = { bottom.x + 4, bottom.y + 32, bottom.w * 62 / 100, bottom.h - 38 };
    rect_t hist = { out.x + out.w + 8, out.y, bottom.w - out.w - 16, out.h };
    lowres_fill_rect(lr, out, 0x0c1113);
    lowres_stroke_rect(lr, out, 0x4f6972);
    draw_lines(lr, app->output[0] ? app->output : "No command run yet.", out, app->output_scroll, 0xdde9e4);
    lowres_fill_rect(lr, hist, 0x0c1113);
    lowres_stroke_rect(lr, hist, 0x4f6972);
    int hy = hist.y + 8;
    int max_h = (hist.h - 16) / 18;
    int start = app->history_count - max_h - app->history_scroll;
    if (start < 0) start = 0;
    for (int i = start; i < app->history_count && hy + 18 <= hist.y + hist.h; i++) {
        lowres_draw_text_clip(lr, app->history[i], hist.x + 8, hy, 2, 0x9fd3a8, hist.w - 16);
        hy += 18;
    }

    lowres_present(lr, app->renderer, app->width * 2, app->height * 2);
}

static int field_at(app_t *app, int mx, int my) {
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    rect_t center = { browser.x + browser.w + 10, 52, app->width * 32 / 100, app->height - 240 };
    int y = center.y + 36 + MAX_TOOLS * 34 + 6;
    for (int i = 0; i < MAX_FIELDS; i++) {
        y += 18;
        rect_t fr = { center.x + 8, y, center.w - 16, 28 };
        if (point_in(fr, mx, my)) {
            return i;
        }
        y += 34;
    }
    return -1;
}

static int tool_at(app_t *app, int mx, int my) {
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    rect_t center = { browser.x + browser.w + 10, 52, app->width * 32 / 100, app->height - 240 };
    int y = center.y + 36;
    for (int i = 0; i < MAX_TOOLS; i++) {
        rect_t row = { center.x + 8, y, center.w - 16, 30 };
        if (point_in(row, mx, my)) {
            return i;
        }
        y += 34;
    }
    return -1;
}

static int entry_at(app_t *app, int mx, int my) {
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    int row_y = browser.y + 90;
    int rows = (browser.h - 96) / 20;
    int row = (my - row_y) / 20;
    if (mx < browser.x || mx >= browser.x + browser.w || row < 0 || row >= rows) {
        return -1;
    }
    int idx = app->browser_scroll + row;
    return idx >= 0 && idx < app->entry_count ? idx : -1;
}

static bool filter_at(app_t *app, int mx, int my) {
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    rect_t filter = { browser.x + 8, browser.y + 56, browser.w - 16, 28 };
    return point_in(filter, mx, my);
}

static int checkbox_at(app_t *app, int mx, int my) {
    rect_t browser = { 10, 52, app->width * 34 / 100 - 15, app->height - 240 };
    rect_t center = { browser.x + browser.w + 10, 52, app->width * 32 / 100, app->height - 240 };
    rect_t right = { center.x + center.w + 10, 52, app->width - center.x - center.w - 20, app->height - 240 };
    rect_t pick = { right.x + 8, right.y + 58, right.w - 16, 28 };
    rect_t run = { right.x + 8, right.y + 92, right.w - 16, 28 };
    if (point_in(pick, mx, my)) {
        return 0;
    }
    if (point_in(run, mx, my)) {
        return 1;
    }
    return -1;
}

static void open_selected_entry(app_t *app) {
    if (app->selected_entry < 0 || app->selected_entry >= app->entry_count) {
        return;
    }
    mpq_entry_t *e = &app->entries[app->selected_entry];
    if (!strcmp(e->name, "..")) {
        parent_path(app->browser_path);
        app->needs_browser_refresh = true;
        return;
    }
    if (e->is_dir) {
        append_archive_child(app, e->name, true);
        app->needs_browser_refresh = true;
    } else {
        char base[PATH_MAX];
        snprintf(base, sizeof(base), "%s", app->browser_path);
        if (base[0]) {
            strncat(base, "\\", sizeof(base) - strlen(base) - 1);
        }
        strncat(base, e->name, sizeof(base) - strlen(base) - 1);
        snprintf(app->archive_path, sizeof(app->archive_path), "%s", base);
        if (app->run_on_open) {
            run_current_command(app);
        }
    }
}

static void edit_field(app_t *app, const char *text) {
    char *dst = NULL;
    size_t cap = 0;
    if (app->selected_field == 0) { dst = app->mpq_path; cap = sizeof(app->mpq_path); }
    if (app->selected_field == 1) { dst = app->archive_path; cap = sizeof(app->archive_path); }
    if (app->selected_field == 2) { dst = app->out_dir; cap = sizeof(app->out_dir); }
    if (app->selected_field == 3) { dst = app->extra_args; cap = sizeof(app->extra_args); }
    if (dst && text) {
        strncat(dst, text, cap - strlen(dst) - 1);
        if (app->selected_field == 0) {
            app->needs_browser_refresh = true;
        }
    }
}

static void backspace_field(app_t *app) {
    char *dst = NULL;
    if (app->selected_field == 0) dst = app->mpq_path;
    if (app->selected_field == 1) dst = app->archive_path;
    if (app->selected_field == 2) dst = app->out_dir;
    if (app->selected_field == 3) dst = app->extra_args;
    if (dst && dst[0]) {
        dst[strlen(dst) - 1] = 0;
        if (app->selected_field == 0) {
            app->needs_browser_refresh = true;
        }
    }
}

static void init_app(app_t *app, int argc, char **argv) {
    memset(app, 0, sizeof(*app));
    app->lowres.scale = 2;
    app->width = 1280;
    app->height = 820;
    app->selected_entry = -1;
    app->dragging_entry = -1;
    dirname_of(argv[0], app->exe_dir, sizeof(app->exe_dir));
    path_join(app->history_path, sizeof(app->history_path), app->exe_dir, "toolbox_recent.txt");
    path_join(app->mpqtool_path, sizeof(app->mpqtool_path), app->exe_dir, "mpqtool");
    snprintf(app->mpq_path, sizeof(app->mpq_path), "data/Warcraft III/War3.mpq");
    snprintf(app->out_dir, sizeof(app->out_dir), "/tmp/openwarcraft3-toolbox");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-mpq") && i + 1 < argc) {
            snprintf(app->mpq_path, sizeof(app->mpq_path), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "-mpqtool") && i + 1 < argc) {
            snprintf(app->mpqtool_path, sizeof(app->mpqtool_path), "%s", argv[++i]);
        }
    }

    load_history(app);
    app->needs_browser_refresh = true;
    app->needs_help_refresh = true;
}

static void update_render_scale(app_t *app) {
    int window_w = 0;
    int window_h = 0;

    SDL_GetWindowSize(app->window, &window_w, &window_h);
    if (window_w > 0 && window_h > 0) {
        app->width = window_w;
        app->height = window_h;
        lowres_ensure_size(&app->lowres, app->renderer, app->width, app->height);
    }
}

int main(int argc, char **argv) {
    app_t app;
    bool quit = false;

    init_app(&app, argc, argv);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    app.window = SDL_CreateWindow("OpenWarcraft3 Toolbox",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  app.width, app.height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app.renderer) {
        app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
        if (!app.renderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(app.window);
            SDL_Quit();
            return 1;
        }
    }
    app.lowres = lowres_create(app.renderer, app.width, app.height, app.lowres.scale);
    if (!app.lowres.surf || !app.lowres.texture) {
        fprintf(stderr, "lowres initialization failed: %s\n", SDL_GetError());
        lowres_destroy(&app.lowres);
        SDL_DestroyRenderer(app.renderer);
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    update_render_scale(&app);

    SDL_StartTextInput();
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    while (!quit) {
        if (app.needs_help_refresh) {
            refresh_help(&app);
        }
        if (app.needs_browser_refresh) {
            refresh_browser(&app);
        }
        render(&app);

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                quit = true;
            } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                app.width = ev.window.data1;
                app.height = ev.window.data2;
                update_render_scale(&app);
            } else if (ev.type == SDL_TEXTINPUT) {
                edit_field(&app, ev.text.text);
            } else if (ev.type == SDL_DROPFILE) {
                const char *d = ev.drop.file;
                size_t n = strlen(d);
                if (n >= 4 && equals_ci(d + n - 4, ".mpq")) {
                    snprintf(app.mpq_path, sizeof(app.mpq_path), "%s", d);
                    app.archive_path[0] = 0;
                    app.browser_path[0] = 0;
                    app.needs_browser_refresh = true;
                } else {
                    snprintf(app.out_dir, sizeof(app.out_dir), "%s", d);
                }
                SDL_free(ev.drop.file);
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int t = tool_at(&app, ev.button.x, ev.button.y);
                int f = field_at(&app, ev.button.x, ev.button.y);
                int e = entry_at(&app, ev.button.x, ev.button.y);
                int c = checkbox_at(&app, ev.button.x, ev.button.y);
                if (filter_at(&app, ev.button.x, ev.button.y)) {
                    app.filter_mode = (app.filter_mode + 1) % FILTER_COUNT;
                    app.needs_browser_refresh = true;
                } else if (c == 0) {
                    app.pick_to_extra = !app.pick_to_extra;
                } else if (c == 1) {
                    app.run_on_open = !app.run_on_open;
                } else if (t >= 0) {
                    app.selected_tool = t;
                    app.needs_help_refresh = true;
                } else if (f >= 0) {
                    app.selected_field = f;
                } else if (e >= 0) {
                    app.selected_entry = e;
                    app.dragging_entry = e;
                    if (ev.button.clicks >= 2) {
                        open_selected_entry(&app);
                        app.dragging_entry = -1;
                    }
                }
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                int f = field_at(&app, ev.button.x, ev.button.y);
                int t = tool_at(&app, ev.button.x, ev.button.y);
                if (f >= 0) {
                    app.selected_field = f;
                    put_selected_entry_in_field(&app);
                } else if (t >= 0) {
                    app.selected_tool = t;
                    app.selected_field = 1;
                    put_selected_entry_in_field(&app);
                    app.needs_help_refresh = true;
                }
                app.dragging_entry = -1;
            } else if (ev.type == SDL_MOUSEWHEEL) {
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                rect_t browser = { 10, 52, app.width * 34 / 100 - 15, app.height - 240 };
                if (point_in(browser, mx, my)) {
                    app.browser_scroll -= ev.wheel.y * 3;
                    if (app.browser_scroll < 0) app.browser_scroll = 0;
                    int max_scroll = app.entry_count > 0 ? app.entry_count - 1 : 0;
                    if (app.browser_scroll > max_scroll) app.browser_scroll = max_scroll;
                } else {
                    app.output_scroll -= ev.wheel.y * 3;
                    if (app.output_scroll < 0) app.output_scroll = 0;
                }
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    quit = true;
                } else if (k == SDLK_F5) {
                    run_current_command(&app);
                } else if (k == SDLK_F6) {
                    app.needs_help_refresh = true;
                } else if (k == SDLK_RETURN) {
                    open_selected_entry(&app);
                } else if (k == SDLK_BACKSPACE) {
                    backspace_field(&app);
                } else if (k == SDLK_TAB) {
                    app.selected_field = (app.selected_field + 1) % MAX_FIELDS;
                } else if (k == SDLK_UP) {
                    if (app.selected_entry > 0) app.selected_entry--;
                    if (app.selected_entry < app.browser_scroll) app.browser_scroll = app.selected_entry;
                } else if (k == SDLK_DOWN) {
                    if (app.selected_entry + 1 < app.entry_count) app.selected_entry++;
                    int rows = (app.height - 240 - 96) / 20;
                    if (app.selected_entry >= app.browser_scroll + rows) app.browser_scroll++;
                } else if (k == SDLK_d) {
                    app.dragging_entry = app.selected_entry;
                    put_selected_entry_in_field(&app);
                    app.dragging_entry = -1;
                } else if (k == SDLK_f) {
                    app.filter_mode = (app.filter_mode + 1) % FILTER_COUNT;
                    app.needs_browser_refresh = true;
                } else if (k == SDLK_r) {
                    app.needs_browser_refresh = true;
                }
            }
        }
    }

    save_history(&app);
    SDL_StopTextInput();
    lowres_destroy(&app.lowres);
    SDL_DestroyRenderer(app.renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
