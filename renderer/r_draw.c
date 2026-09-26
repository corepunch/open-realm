#include "r_local.h"
#include "r_game.h"

#include "common/ui_constants.h"

/* The client canvas resolves the scene from its window and policy (docs/architecture/ui-canvas.md); the
 * renderer only maps it onto the whole drawable, so projection can never disagree with pointer mapping.
 * Before the first push (renderer start-up, standalone renderer tests) the authored scene applies. */
rect_t R_UISceneRect(void) {
    if (tr.uiScene.w > 0.0f && tr.uiScene.h > 0.0f) return tr.uiScene;
    return MAKE(rect_t, 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
}

void R_SetUIScene(rect_t const *scene) {
    if (!scene || scene->w <= 0.0f || scene->h <= 0.0f) {
        fprintf(stderr, "R_SetUIScene: rejected empty scene\n");
        return;
    }
    tr.uiScene = *scene;
}

/* Share glyph batching internally; only scaled characters need a renderer export. */
static void r_draw_string_scaled(float x, float y, cstring_t text, float scale) {
    vertex_t simp[6 * 128];
    uint32_t count = 0;
    size2_t window = R_GetWindowSize();
    mat4_t ui_matrix;
    float char_width;
    float char_height;

    if (!text || scale <= 0.0f) return;

    char_width = SYSFONT_DRAW_WIDTH * scale;
    char_height = SYSFONT_DRAW_HEIGHT * scale;
    if (y <= -char_height) return;

    for (uint32_t i = 0; text[i] && i < 128; i++) {
        uint32_t ch = (uint8_t)text[i];
        float fx = ch & 15, fy = ch >> 4;
        if ((ch & 127) == 32) continue;
        R_AddQuad(simp + count, &(rect_t){ x + i * char_width, y, char_width, char_height }, &(rect_t){ fx / SYSFONT_COLS, fy / SYSFONT_ROWS, 1.f / SYSFONT_COLS, 1.f / SYSFONT_ROWS }, COLOR32_WHITE, 0);
        count += 6;
    }
    if (!count) return;

    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, count * sizeof(*simp), simp, GL_DYNAMIC_DRAW);
    tr.shader_ui.state.viewProjection = ui_matrix;
    
    R_BindTexture(tr.texture[TEX_FONT], 0);
    
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_StatsDraw(GL_TRIANGLES, count, 1);
    R_ApplyShader(&tr.shader_ui);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, count);
}

void R_DrawString(int x, int y, cstring_t text) {
    r_draw_string_scaled((float)x, (float)y, text, 1.0f);
}

void R_DrawCharScaled(float x, float y, int c, float scale) {
    char text[2] = { (char)c, 0 };
    r_draw_string_scaled(x, y, text, scale);
}

void R_DrawChar(int x, int y, int c) {
    R_DrawCharScaled((float)x, (float)y, c, 1.0f);
}

void R_DrawFill(rect_t const *rect, color32_t color) {
    vertex_t simp[6];
    mat4_t ui_matrix;
    size2_t window = R_GetWindowSize();

    if (!rect || rect->w <= 0.0f || rect->h <= 0.0f || !color.a) {
        return;
    }

    R_AddQuad(simp, rect, &(rect_t){0, 0, 1, 1}, color, 0);
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_DYNAMIC_DRAW);
    tr.shader_ui.state.viewProjection = ui_matrix;

    R_BindTexture(tr.texture[TEX_WHITE], 0);

    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_StatsDraw(GL_TRIANGLES, 6, 1);
    R_ApplyShader(&tr.shader_ui);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, 6);
}

void R_SetBlending(BLEND_MODE mode) {
    if (mode == BLEND_MODE_ADD) {
        R_Call(glBlendFunc, GL_ONE, GL_ONE);
    } else {
        R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    return;
//    switch (mode) {
//        case BLEND_MODE_NONE: R_Call(glBlendFunc, GL_ONE, GL_ZERO); break;
//        case BLEND_MODE_BLEND: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
//        case BLEND_MODE_ALPHAKEY: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
//        case BLEND_MODE_ADD: R_Call(glBlendFunc, GL_ONE, GL_ONE); break;
////        case AM_ADDALPHA: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE); break;
//        case BLEND_MODE_MODULATE: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
//        case BLEND_MODE_MODULATE_2X: R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
//    }
}

static void R_SetUIClipScissor(rect_t const *clip) {
    rect_t const scene = R_UISceneRect();
    float x = (clip->x - scene.x) / scene.w;
    float y = 1.0f - ((clip->y + clip->h - scene.y) / scene.h);
    float w = clip->w / scene.w;
    float h = clip->h / scene.h;

    x = MAX(0.0f, MIN(1.0f, x));
    y = MAX(0.0f, MIN(1.0f, y));
    w = MAX(0.0f, MIN(1.0f - x, w));
    h = MAX(0.0f, MIN(1.0f - y, h));

    R_Call(glEnable, GL_SCISSOR_TEST);
    R_Call(glScissor,
           x * tr.drawableSize.width,
           y * tr.drawableSize.height,
           w * tr.drawableSize.width,
           h * tr.drawableSize.height);
}

static void R_ResetUIScissor(void) {
    R_Call(glScissor, 0, 0, tr.drawableSize.width, tr.drawableSize.height);
}

void R_DrawImageBatch(texture_t const *texture,
                      SHADERTYPE shaderType,
                      BLEND_MODE alphamode,
                      float uActiveGlow,
                      float uRadialShade,
                      bool hasClip,
                      rect_t const *clip,
                      vertex_t const *vertices,
                      uint32_t num_vertices,
                      bool repeat)
{
    if (!vertices || !num_vertices) {
        return;
    }

    spriteProg_t *shader = R_SpriteShader(shaderType);
    
    mat4_t ui_matrix, model_matrix;
    rect_t const scene = R_UISceneRect();
    Matrix4_ortho(&ui_matrix, scene.x, scene.x + scene.w, scene.y + scene.h, scene.y, 0.0f, 100.0f);
    Matrix4_identity(&model_matrix);
    
    R_Call(glDisable, GL_CULL_FACE);

    shader->state.viewProjection = ui_matrix;
    shader->state.model = model_matrix;
    shader->state.activeGlow = uActiveGlow;
    shader->state.radialShade = uRadialShade;
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertex_t) * num_vertices, vertices, GL_DYNAMIC_DRAW);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glEnable, GL_BLEND);
    
    R_SetBlending(alphamode);
    R_BindTexture(texture, 0);
    
//    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (repeat) {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    if (hasClip) {
        R_SetUIClipScissor(clip);
    }
    R_StatsDraw(GL_TRIANGLES, num_vertices, 1);
    R_ApplyShader(shader);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    if (hasClip) {
        R_ResetUIScissor();
    }

    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void R_DrawImageEx(drawImage_t const *drawImage) {
    vertex_t simp[6];
    R_AddQuad(simp, &drawImage->screen, &drawImage->uv, drawImage->color, 0);

    if (drawImage->angle) {
        float const cx = drawImage->screen.x + drawImage->screen.w * 0.5f;
        float const cy = drawImage->screen.y + drawImage->screen.h * 0.5f;
        float const radians = drawImage->angle * (float)(M_PI / 180.0);
        float const c = cosf(radians);
        float const s = sinf(radians);

        FOR_LOOP(i, 6) {
            float const x = simp[i].position.x - cx;
            float const y = simp[i].position.y - cy;
            simp[i].position.x = cx + x * c - y * s;
            simp[i].position.y = cy + x * s + y * c;
        }
    }

    R_DrawImageBatch(drawImage->texture,
                     drawImage->shader,
                     drawImage->alphamode,
                     drawImage->uActiveGlow,
                     drawImage->uRadialShade,
                     drawImage->flags & DRAW_CLIP,
                     &drawImage->clip,
                     simp,
                     6,
                     drawImage->uv.w > 1 || drawImage->uv.h > 1);
}

void R_DrawImage(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color) {
    R_DrawImageEx(&MAKE(drawImage_t,
                        .texture = texture,
                        .screen = *screen,
                        .uv = uv ? *uv : MAKE(rect_t,0,0,1,1),
                        .color = color,
                        .shader = SHADER_UI));
}

static void R_ReleaseCinematicPBO(void) {
    if (!tr.cinematic_pbo) return;
    R_Call(glDeleteBuffers, 1, &tr.cinematic_pbo);
    tr.cinematic_pbo = 0;
    tr.cinematic_pbo_size = 0;
}

static void R_DisableCinematicPBO(void) {
    R_ReleaseCinematicPBO();
    tr.cinematic_pbo_disabled = true;
}

/* Map a pixel-unpack buffer so video uploads do not make the driver copy client memory synchronously. */
static bool R_MapCinematicFrame(drawCinematicFrame_t const *frame) {
    uint32_t size = frame->width * frame->height * 4;
    void *mapped;
    GLboolean unmapped;

    if (tr.cinematic_pbo_disabled) return false;
    if (!tr.cinematic_pbo) R_Call(glGenBuffers, 1, &tr.cinematic_pbo);
    if (!tr.cinematic_pbo) {
        if (!tr.cinematic_pbo_warned) {
            fprintf(stderr, "Renderer: cinematic PBO allocation unavailable; using direct pixel uploads\n");
            tr.cinematic_pbo_warned = true;
        }
        tr.cinematic_pbo_disabled = true;
        return false;
    }
    R_Call(glBindBuffer, GL_PIXEL_UNPACK_BUFFER, tr.cinematic_pbo);
    if (tr.cinematic_pbo_size != size) {
        R_Call(glBufferData, GL_PIXEL_UNPACK_BUFFER, size, NULL, GL_STREAM_DRAW);
        tr.cinematic_pbo_size = size;
    }
#ifdef BZ_GL_ES3
    mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
#else
    mapped = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
#endif
    if (!mapped) {
        if (!tr.cinematic_pbo_warned) {
            fprintf(stderr, "Renderer: cinematic PBO mapping unavailable; using direct pixel uploads\n");
            tr.cinematic_pbo_warned = true;
        }
        R_DisableCinematicPBO();
        R_Call(glBindBuffer, GL_PIXEL_UNPACK_BUFFER, 0);
        return false;
    }
    memcpy(mapped, frame->pixels, size);
    unmapped = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    if (!unmapped) {
        if (!tr.cinematic_pbo_warned) {
            fprintf(stderr, "Renderer: cinematic PBO mapping failed; using direct pixel uploads\n");
            tr.cinematic_pbo_warned = true;
        }
        R_DisableCinematicPBO();
        R_Call(glBindBuffer, GL_PIXEL_UNPACK_BUFFER, 0);
        return false;
    }
    R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    R_Call(glBindBuffer, GL_PIXEL_UNPACK_BUFFER, 0);
    return true;
}

/* Upload and draw one decoded cinematic frame while keeping the video texture renderer-owned. */
void R_DrawCinematicFrame(drawCinematicFrame_t const *frame) {
    if (!frame) {
        SAFE_DELETE(tr.cinematic, R_ReleaseTexture);
        R_ReleaseCinematicPBO();
        tr.cinematic_pbo_disabled = false;
        tr.cinematic_pbo_warned = false;
        return;
    }
    if (!frame->pixels || !frame->width || !frame->height || frame->screen.w <= 0 || frame->screen.h <= 0) return;
    if (!tr.cinematic || tr.cinematic->width != frame->width || tr.cinematic->height != frame->height) {
        SAFE_DELETE(tr.cinematic, R_ReleaseTexture);
        tr.cinematic = R_AllocateTexture(frame->width, frame->height);
        if (!tr.cinematic) return;
        R_LoadTextureMipLevel(tr.cinematic, &(texMip_t){ frame->pixels, frame->width, frame->height, 0, PIXEL_RGBA });
    } else {
        R_Call(glBindTexture, GL_TEXTURE_2D, tr.cinematic->texid);
        if (!R_MapCinematicFrame(frame))
            R_Call(glTexSubImage2D, GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height, GL_RGBA, GL_UNSIGNED_BYTE, frame->pixels);
    }
    R_DrawImage(tr.cinematic, &frame->screen, &(rect_t){0, 0, 1, 1}, COLOR32_WHITE);
}

static bool R_MinimapPointForWorld(vec3_t const *world, rect_t const *screen, vec2_t *out) {
    vec2_t map_size;
    float nx;
    float ny;

    if (!tr.world || !world || !screen || !out) {
        return false;
    }

    map_size = R_WorldSize();
    if (map_size.x <= 0.0f || map_size.y <= 0.0f) {
        return false;
    }

    nx = (world->x - tr.world->center.x) / map_size.x;
    ny = (world->y - tr.world->center.y) / map_size.y;
    nx = MAX(0.0f, MIN(1.0f, nx));
    ny = MAX(0.0f, MIN(1.0f, ny));

    out->x = screen->x + nx * screen->w;
    out->y = screen->y + (1.0f - ny) * screen->h;
    return true;
}

static bool R_TraceViewportCornerToMinimap(float x, float y, rect_t const *screen, vec2_t *out, vec3_t *world_out) {
    vec3_t world;
    line3_t line;
    plane3_t ground = {
        .normal = { 0.0f, 0.0f, 1.0f },
        .distance = 0.0f,
    };

    line = R_LineForScreenPoint(&tr.viewDef, x, y);
    if (!Line3_intersect_plane3(&line, &ground, &world)) {
        return false;
    }
    if (world_out) {
        *world_out = world;
    }
    return R_MinimapPointForWorld(&world, screen, out);
}

static void R_DrawUILineStrip(vertex_t const *vertices, uint32_t count) {
    mat4_t ui_matrix, model_matrix;
    rect_t const scene = R_UISceneRect();

    Matrix4_ortho(&ui_matrix, scene.x, scene.x + scene.w, scene.y + scene.h, scene.y, 0.0f, 100.0f);
    Matrix4_identity(&model_matrix);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    tr.shader_ui.state.viewProjection = ui_matrix;
    tr.shader_ui.state.model = model_matrix;
    R_BindTexture(tr.texture[TEX_WHITE], 0);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertex_t) * count, vertices, GL_DYNAMIC_DRAW);
    R_StatsDraw(GL_LINE_STRIP, count, 1);
    R_ApplyShader(&tr.shader_ui);
    R_Call(glDrawArrays, GL_LINE_STRIP, 0, count);
    R_Call(glDepthMask, GL_TRUE);
}

void R_DrawMinimapCameraRect(rect_t const *screen) {
    size2_t window = R_GetWindowSize();
    float left = tr.viewDef.viewport.x * window.width;
    float right = (tr.viewDef.viewport.x + tr.viewDef.viewport.w) * window.width;
    float top = (1.0f - (tr.viewDef.viewport.y + tr.viewDef.viewport.h)) * window.height;
    float bottom = (1.0f - tr.viewDef.viewport.y) * window.height;
    vec2_t corners[4];
    vertex_t vertices[5];
    color32_t color = MAKE(color32_t, 255, 255, 255, 220);
    rect_t uv = { 0, 0, 1, 1 };

    if (!tr.world || !screen ||
        (tr.viewDef.rdflags & (RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL)) ||
        window.width == 0 ||
        window.height == 0) {
        return;
    }

    vec3_t worlds[4];
    if (!R_TraceViewportCornerToMinimap(left, top, screen, &corners[0], &worlds[0]) ||
        !R_TraceViewportCornerToMinimap(right, top, screen, &corners[1], &worlds[1]) ||
        !R_TraceViewportCornerToMinimap(right, bottom, screen, &corners[2], &worlds[2]) ||
        !R_TraceViewportCornerToMinimap(left, bottom, screen, &corners[3], &worlds[3])) {
        return;
    }

    FOR_LOOP(i, 5) {
        vec2_t const *corner = &corners[i % 4];
        vertices[i] = (vertex_t){
            .position = { corner->x, corner->y, 0 },
            .texcoord = { uv.x, uv.y },
            .color = color,
        };
    }
    R_DrawUILineStrip(vertices, 5);
}

void R_DrawMinimapBorder(rect_t const *screen, color32_t color) {
    vertex_t vertices[5];

    if (!screen || screen->w <= 0.0f || screen->h <= 0.0f) return;
    vertices[0] = (vertex_t){ .position = { screen->x, screen->y, 0 }, .color = color };
    vertices[1] = (vertex_t){ .position = { screen->x + screen->w, screen->y, 0 }, .color = color };
    vertices[2] = (vertex_t){ .position = { screen->x + screen->w, screen->y + screen->h, 0 }, .color = color };
    vertices[3] = (vertex_t){ .position = { screen->x, screen->y + screen->h, 0 }, .color = color };
    vertices[4] = vertices[0];
    R_DrawUILineStrip(vertices, 5);
}

bool R_WorldToMinimap(vec2_t const *world, vec2_t *outScreen) {
    vec3_t point;

    if (!tr.hasMinimap || !tr.world || !world || !outScreen) {
        return false;
    }
    point = (vec3_t){ world->x, world->y, 0.0f };
    return R_MinimapPointForWorld(&point, &tr.minimapRect, outScreen);
}

/* Inverse of R_MinimapPointForWorld: map a window-pixel click over the minimap
 * to a world position for camera focus or point-order input. */
bool R_TraceMinimap(float x, float y, vec2_t *outWorld) {
    size2_t window;
    rect_t scene;
    vec2_t map_size;
    float ux, uy, nx, ny;

    if (!tr.hasMinimap || !tr.world || !outWorld) {
        return false;
    }
    window = R_GetWindowSize();
    if (window.width == 0 || window.height == 0) {
        return false;
    }
    scene = R_UISceneRect();
    /* window-pixel -> UI coords (same ortho R_DrawImageBatch draws the UI with) */
    ux = scene.x + (x / (float)window.width)  * scene.w;
    uy = scene.y + (y / (float)window.height) * scene.h;

    rect_t const r = tr.minimapRect;
    if (ux < r.x || ux > r.x + r.w || uy < r.y || uy > r.y + r.h) {
        return false;
    }
    map_size = R_WorldSize();
    if (map_size.x <= 0.0f || map_size.y <= 0.0f) {
        return false;
    }
    nx = (ux - r.x) / r.w;
    ny = 1.0f - (uy - r.y) / r.h; /* y is flipped in R_MinimapPointForWorld */
    outWorld->x = tr.world->center.x + nx * map_size.x;
    outWorld->y = tr.world->center.y + ny * map_size.y;
    return true;
}

void R_DrawMinimapScene(rect_t const *screen, cstring_t map) {
    if (!screen) {
        return;
    }

    if (!map) { tr.minimapRect = *screen; tr.hasMinimap = true; }

    /* Each game owns its minimap content and authoritative texture source. */
    R_DrawMinimap(screen, map);
}

void R_DrawPic(texture_t const *texture, float x, float y) {
    rect_t screen = { x, y, texture->width / 2000.0, texture->height / 2000.0};
    R_DrawImage(texture, &screen, NULL, COLOR32_WHITE);
}

void R_DrawLoadingIndicator(rect_t const *rect, uint32_t time, color32_t color) {
    float const cx = rect->x + rect->w * 0.5f;
    float const cy = rect->y + rect->h * 0.5f;
    float const size = MAX(MIN(rect->w, rect->h) * 0.11f, 0.006f);
    rect_t const screen = { cx - size * 0.5f, cy - size * 0.5f, size, size };

    if (!color.a) {
        color = MAKE(color32_t, 235, 220, 180, 255);
    }

    R_DrawImageEx(&MAKE(drawImage_t,
                        .texture = tr.texture[TEX_LOADING_INDICATOR],
                        .screen = screen,
                        .uv = MAKE(rect_t, 0, 0, 1, 1),
                        .color = color,
                        .shader = SHADER_UI,
                        .angle = -360.0f * (float)(time % 900) / 900.0f));
}

void R_DrawWireRect(rect_t const *rect, color32_t color) {
    static vertex_t simp[5];
    R_AddStrip(simp, rect, color);

    mat4_t ui_matrix;
    size2_t const window = R_GetWindowSize();
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    tr.shader_ui.state.viewProjection = ui_matrix;
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_DYNAMIC_DRAW);
    
    R_BindTexture(tr.texture[TEX_WHITE], 0);
    
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_StatsDraw(GL_LINE_STRIP, sizeof(simp) / sizeof(*simp), 1);
    R_ApplyShader(&tr.shader_ui);
    R_Call(glDrawArrays, GL_LINE_STRIP, 0, sizeof(simp) / sizeof(*simp));
}

void R_DrawSelectionRect(rect_t const *rect, color32_t color) {
    /* Selection is a world overlay even though the marquee is drawn in window coordinates. */
    R_SetupScissor(&tr.viewDef.scissor);
    R_DrawWireRect(rect, color);
    R_SetupScissor(&(rect_t){0, 0, 1, 1});
}

void R_DrawBoundingBox(box3_t const *box, mat4_t const *modelMatrix, mat4_t const *vpMatrix, color32_t color) {
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    vec3_t corners[8] = {
        { box->min.x, box->min.y, box->min.z },
        { box->max.x, box->min.y, box->min.z },
        { box->max.x, box->max.y, box->min.z },
        { box->min.x, box->max.y, box->min.z },
        { box->min.x, box->min.y, box->max.z },
        { box->max.x, box->min.y, box->max.z },
        { box->max.x, box->max.y, box->max.z },
        { box->min.x, box->max.y, box->max.z },
    };
    vertex_t simp[24] = { 0 };
    for (int i = 0; i < 12; i++) {
        simp[i * 2 + 0].position = corners[edges[i][0]];
        simp[i * 2 + 0].color = color;
        simp[i * 2 + 1].position = corners[edges[i][1]];
        simp[i * 2 + 1].color = color;
    }

    tr.shader_default.state.viewProjection = *vpMatrix;
    tr.shader_default.state.model = *modelMatrix;
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_DYNAMIC_DRAW);

    R_BindTexture(tr.texture[TEX_WHITE], 0);

    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_StatsDraw(GL_LINES, 24, 1);
    R_ApplyShader(&tr.shader_default);
    R_Call(glDrawArrays, GL_LINES, 0, 24);
    R_Call(glEnable, GL_DEPTH_TEST);
}
