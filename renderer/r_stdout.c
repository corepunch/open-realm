#include "r_local.h"
#ifndef _WIN32
#include <strings.h>
#endif

typedef struct stdout_name_s {
    struct stdout_name_s *next;
    HANDLE handle;
    char name[512];
    DWORD size;
} stdout_name_t;

typedef struct stdout_font_s {
    struct stdout_font_s *next;
    char name[512];
    DWORD size;
} stdout_font_t;

static stdout_name_t *stdout_names;
static stdout_font_t *stdout_fonts;
static DWORD stdout_next_texture_id = 1;
static DWORD stdout_frame_number;
static size2_t stdout_window = { 800, 600 };

static void RStd_Escape(LPCSTR text) {
    for (LPCSTR p = text ? text : ""; *p; p++) {
        switch (*p) {
            case '\\': fputs("\\\\", stdout); break;
            case '"': fputs("\\\"", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default: fputc(*p, stdout); break;
        }
    }
}

static void RStd_PrintName(LPCSTR key, LPCSTR value) {
    printf(" %s=\"", key);
    RStd_Escape(value);
    printf("\"");
}

static LPCSTR RStd_HandleName(void const *handle) {
    FOR_EACH_LIST(stdout_name_t, it, stdout_names) {
        if (it->handle == handle) {
            return it->name;
        }
    }
    return "<null>";
}

static void RStd_SetHandleName(void const *handle, LPCSTR name, DWORD size) {
    stdout_name_t *it;

    if (!handle) {
        return;
    }
    it = ri.MemAlloc(sizeof(*it));
    it->handle = (HANDLE)handle;
    it->size = size;
    snprintf(it->name, sizeof(it->name), "%s", name ? name : "");
    ADD_TO_LIST(it, stdout_names);
}

static void RStd_RemoveHandleName(void const *handle) {
    stdout_name_t **prev = &stdout_names;

    FOR_EACH_LIST(stdout_name_t, it, stdout_names) {
        if (it->handle == handle) {
            *prev = it->next;
            ri.MemFree(it);
            return;
        }
        prev = &it->next;
    }
}

static void RStd_PrintRect(LPCSTR key, LPCRECT rect) {
    printf(" %s={x:%.6f,y:%.6f,w:%.6f,h:%.6f}",
           key,
           rect ? rect->x : 0,
           rect ? rect->y : 0,
           rect ? rect->w : 0,
           rect ? rect->h : 0);
}

static void RStd_PrintColor(COLOR32 color) {
    printf(" color=#%02x%02x%02x%02x", color.a, color.r, color.g, color.b);
}

static DWORD RStd_ReadLE32(BYTE const *data) {
    DWORD value;

    memcpy(&value, data, sizeof(value));
    return value;
}

static void RStd_TextureSizeFromHeader(LPCSTR fileName, DWORD *width, DWORD *height) {
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(fileName, &buffer);

    *width = 64;
    *height = 64;
    if (fileSize >= 20 && buffer) {
        BYTE const *data = buffer;
        DWORD magic = RStd_ReadLE32(data);

        if (magic == ID_BLP1 || magic == ID_BLP2) {
            *width = RStd_ReadLE32(data + 12);
            *height = RStd_ReadLE32(data + 16);
        }
    }
    if (buffer) {
        ri.FS_FreeFile(buffer);
    }
}

static void RStd_Init(DWORD width, DWORD height) {
    stdout_window.width = width;
    stdout_window.height = height;
    stdout_frame_number = 0;
    printf("renderer_init backend=\"stdout\"");
    printf(" window={w:%u,h:%u}\n", width, height);
}

static void RStd_Shutdown(void) {
    while (stdout_fonts) {
        stdout_font_t *next = stdout_fonts->next;
        ri.MemFree(stdout_fonts);
        stdout_fonts = next;
    }
    while (stdout_names) {
        stdout_name_t *next = stdout_names->next;
        ri.MemFree(stdout_names);
        stdout_names = next;
    }
    printf("renderer_shutdown backend=\"stdout\"\n");
}

static void RStd_RegisterMap(LPCSTR mapFileName) {
    printf("register_map");
    RStd_PrintName("name", mapFileName);
    printf("\n");
}

static void RStd_RenderFrame(viewDef_t const *viewdef) {
    printf("render_frame");
    printf(" time=%u delta=%u entities=%u decals=%u",
           viewdef ? viewdef->time : 0,
           viewdef ? viewdef->deltaTime : 0,
           viewdef ? viewdef->num_entities : 0,
           viewdef ? viewdef->num_decals : 0);
    if (viewdef) {
        RStd_PrintRect("viewport", &viewdef->viewport);
        RStd_PrintRect("scissor", &viewdef->scissor);
    }
    printf("\n");
}

static void RStd_SetFogOfWarData(DWORD width, DWORD height, BYTE const *data) {
    (void)width;
    (void)height;
    (void)data;
}

static LPTEXTURE RStd_LoadTexture(LPCSTR fileName) {
    LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));

    texture->texid = stdout_next_texture_id++;
    RStd_TextureSizeFromHeader(fileName, &texture->width, &texture->height);
    RStd_SetHandleName(texture, fileName, 0);
    printf("load_texture id=%u", texture->texid);
    RStd_PrintName("name", fileName);
    printf(" size={w:%u,h:%u}\n", texture->width, texture->height);
    return texture;
}

static LPMODEL RStd_LoadModel(LPCSTR fileName) {
    LPMODEL model = ri.MemAlloc(sizeof(MODEL));

    RStd_SetHandleName(model, fileName, 0);
    printf("load_model");
    RStd_PrintName("name", fileName);
    printf("\n");
    return model;
}

static LPFONT RStd_LoadFont(LPCSTR fileName, DWORD size) {
    stdout_font_t *font;

    FOR_EACH_LIST(stdout_font_t, it, stdout_fonts) {
        if (it->size == size && !strcasecmp(it->name, fileName ? fileName : "")) {
            return (LPFONT)it;
        }
    }

    font = ri.MemAlloc(sizeof(*font));
    snprintf(font->name, sizeof(font->name), "%s", fileName ? fileName : "");
    font->size = size;
    ADD_TO_LIST(font, stdout_fonts);
    printf("load_font size=%u", size);
    RStd_PrintName("name", fileName);
    printf("\n");
    return (LPFONT)font;
}

static size2_t RStd_GetWindowSize(void) {
    return stdout_window;
}

static size2_t RStd_GetTextureSize(LPCTEXTURE texture) {
    if (!texture) {
        return MAKE(size2_t, 0, 0);
    }
    return MAKE(size2_t, texture->width, texture->height);
}

static void RStd_ReleaseTexture(LPTEXTURE texture) {
    if (!texture) {
        return;
    }
    printf("release_texture id=%u", texture->texid);
    RStd_PrintName("name", RStd_HandleName(texture));
    printf("\n");
    RStd_RemoveHandleName(texture);
    ri.MemFree(texture);
}

static void RStd_ReleaseModel(LPMODEL model) {
    if (!model) {
        return;
    }
    printf("release_model");
    RStd_PrintName("name", RStd_HandleName(model));
    printf("\n");
    RStd_RemoveHandleName(model);
    ri.MemFree(model);
}

static void RStd_BeginFrame(void) {
    stdout_frame_number++;
    printf("begin_frame index=%u\n", stdout_frame_number);
}

static void RStd_EndFrame(void) {
    printf("end_frame index=%u\n", stdout_frame_number);
    fflush(stdout);
}

static void RStd_DrawChar(int x, int y, int c) {
    printf("draw_char x=%d y=%d c=%d\n", x, y, c & 255);
}

static void RStd_DrawFill(LPCRECT rect, COLOR32 color) {
    printf("draw_fill");
    RStd_PrintRect("screen", rect);
    RStd_PrintColor(color);
    printf("\n");
}

static void RStd_DrawSelectionRect(LPCRECT rect, COLOR32 color) {
    printf("draw_selection_rect");
    RStd_PrintRect("screen", rect);
    RStd_PrintColor(color);
    printf("\n");
}

static void RStd_DrawPic(LPCTEXTURE texture, float x, float y) {
    printf("draw_pic texture=%u", texture ? texture->texid : 0);
    RStd_PrintName("name", RStd_HandleName(texture));
    printf(" x=%.6f y=%.6f\n", x, y);
}

static void RStd_DrawImageEx(LPCDRAWIMAGE drawImage) {
    if (!drawImage) {
        return;
    }
    printf("draw_image texture=%u", drawImage->texture ? drawImage->texture->texid : 0);
    RStd_PrintName("name", RStd_HandleName(drawImage->texture));
    printf(" shader=%u blend=%u angle=%.3f glow=%.3f",
           drawImage->shader,
           drawImage->alphamode,
           drawImage->angle,
           drawImage->uActiveGlow);
    RStd_PrintRect("screen", &drawImage->screen);
    RStd_PrintRect("uv", &drawImage->uv);
    if (drawImage->hasClip) {
        RStd_PrintRect("clip", &drawImage->clip);
    }
    RStd_PrintColor(drawImage->color);
    printf("\n");
}

static void RStd_DrawBackdrop(LPCDRAWBACKDROP db) {
    if (!db) return;
    printf("draw_backdrop screen=");
    RStd_PrintRect("", &db->screen);
    if (db->bg_texture) printf(" bg=%s", RStd_HandleName(db->bg_texture));
    if (db->edge_texture) printf(" edge=%s", RStd_HandleName(db->edge_texture));
    printf("\n");
}

static void RStd_DrawImage(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    RStd_DrawImageEx(&MAKE(drawImage_t,
                           .texture = texture,
                           .shader = SHADER_UI,
                           .alphamode = BLEND_MODE_BLEND,
                           .screen = screen ? *screen : MAKE(RECT, 0, 0, 0, 0),
                           .uv = uv ? *uv : MAKE(RECT, 0, 0, 1, 1),
                           .color = color));
}

static void RStd_DrawMinimap(LPCRECT screen) {
    printf("draw_minimap");
    RStd_PrintRect("screen", screen);
    printf("\n");
}

static void RStd_DrawLoadingIndicator(LPCRECT rect, DWORD time, COLOR32 color) {
    printf("draw_loading_indicator time=%u", time);
    RStd_PrintRect("screen", rect);
    RStd_PrintColor(color);
    printf("\n");
}

static void RStd_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    printf("draw_sprite");
    RStd_PrintName("model", RStd_HandleName((HANDLE)model));
    RStd_PrintName("anim", anim);
    printf(" x=%.6f y=%.6f\n", x, y);
}

static bool RStd_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    (void)model;
    (void)anim;
    if (!entity) {
        return false;
    }
    entity->frame = 0; entity->oldframe = 0;
    return true;
}

static DWORD RStd_VisibleTextLength(LPCSTR text) {
    DWORD count = 0;

    for (LPCSTR p = text ? text : ""; *p;) {
        if ((*p == '|' || *p == '\n') && p[1]) {
            if (!strncasecmp(p, "|c", 2) && strlen(p) >= 10) {
                p += 10;
                continue;
            }
            if (!strncasecmp(p, "|r", 2) || !strncasecmp(p, "|n", 2)) {
                p += 2;
                continue;
            }
        }
        count++;
        p++;
    }
    return count;
}

static VECTOR2 RStd_GetTextSize(LPCDRAWTEXT drawText) {
    stdout_font_t const *font = drawText ? (stdout_font_t const *)drawText->font : NULL;
    FLOAT size = font && font->size ? font->size / 1000.0f : 0.013f;
    FLOAT width = RStd_VisibleTextLength(drawText ? drawText->text : "") * size * 0.52f;
    FLOAT height = size;

    if (drawText && drawText->wordWrap && drawText->textWidth > 0 && width > drawText->textWidth) {
        DWORD lines = (DWORD)ceilf(width / drawText->textWidth);
        width = drawText->textWidth;
        height *= MAX(1, lines) * (drawText->lineHeight > 0 ? drawText->lineHeight : 1.0f);
    }
    return MAKE(VECTOR2, width, height);
}

static void RStd_DrawText(LPCDRAWTEXT drawText) {
    stdout_font_t const *font = drawText ? (stdout_font_t const *)drawText->font : NULL;
    VECTOR2 size = RStd_GetTextSize(drawText);

    printf("draw_text");
    if (font) {
        printf(" font_size=%u", font->size);
        RStd_PrintName("font", font->name);
    }
    if (drawText) {
        printf(" width=%.6f measured={w:%.6f,h:%.6f} halign=%u valign=%u wrap=%d",
               drawText->textWidth,
               size.x,
               size.y,
               drawText->halign,
               drawText->valign,
               drawText->wordWrap);
        RStd_PrintRect("rect", &drawText->rect);
        if (drawText->hasClip) {
            RStd_PrintRect("clip", &drawText->clip);
        }
        RStd_PrintColor(drawText->color);
        RStd_PrintName("text", drawText->text);
    }
    printf("\n");
}

static bool RStd_GetModelInfo(LPMODEL model, LPMODELINFO info) {
    if (!model || !info) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    return true;
}

static void RStd_DrawBoundingBox(LPCBOX3 box, LPCMATRIX4 modelMatrix, LPCMATRIX4 vpMatrix, COLOR32 color) {
    (void)box;
    (void)modelMatrix;
    (void)vpMatrix;
    printf("draw_bounding_box");
    RStd_PrintColor(color);
    printf("\n");
}

static FLOAT RStd_GetHeightAtPoint(float x, float y) {
    (void)x;
    (void)y;
    return 0.0f;
}

static bool RStd_TraceEntity(viewDef_t const *viewdef, float x, float y, LPDWORD number) {
    (void)viewdef;
    (void)x;
    (void)y;
    (void)number;
    return false;
}

static bool RStd_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point) {
    (void)viewdef;
    (void)x;
    (void)y;
    if (point) {
        *point = MAKE(VECTOR3, 0, 0, 0);
    }
    return false;
}

static DWORD RStd_EntitiesInRect(viewDef_t const *viewdef, LPCRECT rect, DWORD max, LPDWORD array) {
    (void)viewdef;
    (void)rect;
    (void)max;
    (void)array;
    return 0;
}

refExport_t R_StdoutGetAPI(refImport_t imp) {
    ri = imp;
    return (refExport_t) {
        .Init = RStd_Init,
        .Shutdown = RStd_Shutdown,
        .RegisterMap = RStd_RegisterMap,
        .RenderFrame = RStd_RenderFrame,
        .SetFogOfWarData = RStd_SetFogOfWarData,
        .LoadTexture = RStd_LoadTexture,
        .LoadModel = RStd_LoadModel,
        .LoadFont = RStd_LoadFont,
        .GetWindowSize = RStd_GetWindowSize,
        .GetTextureSize = RStd_GetTextureSize,
        .ReleaseTexture = RStd_ReleaseTexture,
        .ReleaseModel = RStd_ReleaseModel,
        .BeginFrame = RStd_BeginFrame,
        .EndFrame = RStd_EndFrame,
        .DrawChar = RStd_DrawChar,
        .DrawFill = RStd_DrawFill,
        .DrawSelectionRect = RStd_DrawSelectionRect,
        .DrawPic = RStd_DrawPic,
        .DrawImage = RStd_DrawImage,
        .DrawImageEx = RStd_DrawImageEx,
        .DrawBackdrop = RStd_DrawBackdrop,
        .DrawMinimap = RStd_DrawMinimap,
        .DrawLoadingIndicator = RStd_DrawLoadingIndicator,
        .DrawSprite = RStd_DrawSprite,
        .SetEntityAnimFrame = RStd_SetEntityAnimFrame,
        .DrawText = RStd_DrawText,
        .GetTextSize = RStd_GetTextSize,
        .GetModelInfo = RStd_GetModelInfo,
        .DrawBoundingBox = RStd_DrawBoundingBox,
        .GetHeightAtPoint = RStd_GetHeightAtPoint,
        .TraceEntity = RStd_TraceEntity,
        .TraceLocation = RStd_TraceLocation,
        .EntitiesInRect = RStd_EntitiesInRect,
    };
}
