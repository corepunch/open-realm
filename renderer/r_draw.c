#include "r_local.h"
#include "r_game.h"

#ifdef WOW
#define R_UI_BASE_WIDTH  1.0f
#define R_UI_BASE_HEIGHT 1.0f
#else
#define R_UI_BASE_WIDTH  0.8f
#define R_UI_BASE_HEIGHT 0.6f
#endif
#define R_UI_MIN_ASPECT  (4.0f / 3.0f)

RECT R_UISceneRect(void) {
    size2_t window = R_GetWindowSize();
    float aspect = (float)window.width / (float)window.height;
    float x_scale = aspect > R_UI_MIN_ASPECT ? aspect / R_UI_MIN_ASPECT : 1.0f;
    float y_scale = aspect < R_UI_MIN_ASPECT ? R_UI_MIN_ASPECT / aspect : 1.0f;
    float scene_w = R_UI_BASE_WIDTH  * x_scale;
    float scene_h = R_UI_BASE_HEIGHT * y_scale;
    return MAKE(RECT,
                (R_UI_BASE_WIDTH  - scene_w) * 0.5f,
                (R_UI_BASE_HEIGHT - scene_h) * 0.5f,
                scene_w, scene_h);
}

void R_DrawChar(int x, int y, int c) {
    VERTEX simp[6];
    DWORD ch;
    float fx;
    float fy;
    size2_t window = R_GetWindowSize();
    MATRIX4 ui_matrix;

    c &= 255;
    if ((c & 127) == 32 || y <= -SYSFONT_DRAW_HEIGHT) {
        return;
    }

    ch = (DWORD)c;
    fx = ch & 15;
    fy = ch >> 4;

    R_AddQuad(simp, &(RECT ) {
        x, y, SYSFONT_DRAW_WIDTH, SYSFONT_DRAW_HEIGHT
    }, &(RECT ) {
        fx / SYSFONT_COLS, fy / SYSFONT_ROWS, 1.f / SYSFONT_COLS, 1.f / SYSFONT_ROWS
    }, COLOR32_WHITE, 0);

    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);
    
    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_STATIC_DRAW);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);
    
    R_BindTexture(tr.texture[TEX_FONT], 0);
    
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, 6);
}

void R_DrawFill(LPCRECT rect, COLOR32 color) {
    VERTEX simp[6];
    MATRIX4 ui_matrix;
    size2_t window = R_GetWindowSize();

    if (!rect || rect->w <= 0.0f || rect->h <= 0.0f || !color.a) {
        return;
    }

    R_AddQuad(simp, rect, &(RECT){0, 0, 1, 1}, color, 0);
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_STATIC_DRAW);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);

    R_BindTexture(tr.texture[TEX_WHITE], 0);

    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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

static void R_SetUIClipScissor(LPCRECT clip) {
    RECT const scene = R_UISceneRect();
    FLOAT x = (clip->x - scene.x) / scene.w;
    FLOAT y = 1.0f - ((clip->y + clip->h - scene.y) / scene.h);
    FLOAT w = clip->w / scene.w;
    FLOAT h = clip->h / scene.h;

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

void R_DrawImageBatch(LPCTEXTURE texture,
                      SHADERTYPE shaderType,
                      BLEND_MODE alphamode,
                      FLOAT uActiveGlow,
                      BOOL hasClip,
                      LPCRECT clip,
                      LPCVERTEX vertices,
                      DWORD num_vertices,
                      BOOL repeat)
{
    if (!vertices || !num_vertices) {
        return;
    }

    LPCSHADER shader = tr.shader[shaderType];
    
    MATRIX4 ui_matrix, model_matrix;
    RECT const scene = R_UISceneRect();
    Matrix4_ortho(&ui_matrix, scene.x, scene.x + scene.w, scene.y + scene.h, scene.y, 0.0f, 100.0f);
    Matrix4_identity(&model_matrix);
    
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glUniform1f , shader->uActiveGlow, uActiveGlow);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VERTEX) * num_vertices, vertices, GL_STATIC_DRAW);
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
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    if (hasClip) {
        R_ResetUIScissor();
    }

    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void R_DrawImageEx(LPCDRAWIMAGE drawImage) {
    VERTEX simp[6];
    R_AddQuad(simp, &drawImage->screen, &drawImage->uv, drawImage->color, 0);

    if (drawImage->rotate) {
        VECTOR2 tmp1 = simp[1].texcoord;
        VECTOR2 tmp2 = simp[5].texcoord;
        simp[1].texcoord = tmp2;
        simp[5].texcoord = tmp1;
    }
    if (drawImage->angle) {
        FLOAT const cx = drawImage->screen.x + drawImage->screen.w * 0.5f;
        FLOAT const cy = drawImage->screen.y + drawImage->screen.h * 0.5f;
        FLOAT const radians = drawImage->angle * (FLOAT)(M_PI / 180.0);
        FLOAT const c = cosf(radians);
        FLOAT const s = sinf(radians);

        FOR_LOOP(i, 6) {
            FLOAT const x = simp[i].position.x - cx;
            FLOAT const y = simp[i].position.y - cy;
            simp[i].position.x = cx + x * c - y * s;
            simp[i].position.y = cy + x * s + y * c;
        }
    }

    R_DrawImageBatch(drawImage->texture,
                     drawImage->shader,
                     drawImage->alphamode,
                     drawImage->uActiveGlow,
                     drawImage->hasClip,
                     &drawImage->clip,
                     simp,
                     6,
                     drawImage->uv.w > 1 || drawImage->uv.h > 1);
}

void R_DrawImage(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    R_DrawImageEx(&MAKE(drawImage_t,
                        .texture = texture,
                        .screen = *screen,
                        .uv = uv ? *uv : MAKE(RECT,0,0,1,1),
                        .color = color,
                        .rotate = false,
                        .shader = SHADER_UI));
}

static BOOL R_MinimapPointForWorld(LPCVECTOR3 world, LPCRECT screen, LPVECTOR2 out) {
    VECTOR2 map_size;
    FLOAT nx;
    FLOAT ny;

    if (!tr.world || !world || !screen || !out) {
        return false;
    }

    map_size = R_GameWorldSize();
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

static BOOL R_TraceViewportCornerToMinimap(FLOAT x, FLOAT y, LPCRECT screen, LPVECTOR2 out) {
    VECTOR3 world;
    LINE3 line;
    PLANE3 ground = {
        .normal = { 0.0f, 0.0f, 1.0f },
        .distance = 0.0f,
    };

    line = R_LineForScreenPoint(&tr.viewDef, x, y);
    if (!Line3_intersect_plane3(&line, &ground, &world)) {
        return false;
    }
    return R_MinimapPointForWorld(&world, screen, out);
}

static void R_DrawMinimapCameraRect(LPCRECT screen) {
    size2_t window = R_GetWindowSize();
    FLOAT left = tr.viewDef.scissor.x * window.width;
    FLOAT right = (tr.viewDef.scissor.x + tr.viewDef.scissor.w) * window.width;
    FLOAT top = (1.0f - (tr.viewDef.scissor.y + tr.viewDef.scissor.h)) * window.height;
    FLOAT bottom = (1.0f - tr.viewDef.scissor.y) * window.height;
    VECTOR2 corners[4];
    VERTEX vertices[5];
    COLOR32 color = MAKE(COLOR32, 255, 255, 255, 220);
    RECT uv = { 0, 0, 1, 1 };
    MATRIX4 ui_matrix;
    MATRIX4 model_matrix;

    if (!tr.world || !screen ||
        (tr.viewDef.rdflags & (RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL)) ||
        window.width == 0 ||
        window.height == 0) {
        return;
    }

    if (!R_TraceViewportCornerToMinimap(left, top, screen, &corners[0]) ||
        !R_TraceViewportCornerToMinimap(right, top, screen, &corners[1]) ||
        !R_TraceViewportCornerToMinimap(right, bottom, screen, &corners[2]) ||
        !R_TraceViewportCornerToMinimap(left, bottom, screen, &corners[3])) {
        return;
    }

    FOR_LOOP(i, 5) {
        VECTOR2 const *corner = &corners[i % 4];
        vertices[i] = (VERTEX){
            .position = { corner->x, corner->y, 0 },
            .texcoord = { uv.x, uv.y },
            .color = color,
        };
    }

    RECT const scene = R_UISceneRect();
    Matrix4_ortho(&ui_matrix, scene.x, scene.x + scene.w, scene.y + scene.h, scene.y, 0.0f, 100.0f);
    Matrix4_identity(&model_matrix);

    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_BindTexture(tr.texture[TEX_WHITE], 0);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    R_Call(glDrawArrays, GL_LINE_STRIP, 0, 5);
}

void R_DrawMinimap(LPCRECT screen) {
    if (!tr.minimap || !screen) {
        return;
    }

    R_DrawImage(tr.minimap, screen, &MAKE(RECT, 0, 0, 1, 1), COLOR32_WHITE);

    if (tr.world && tr.shader[SHADER_MINIMAP_FOG]) {
        DWORD const fow_texid = R_GetFogOfWarTexture();
        if (fow_texid && (!tr.texture[TEX_WHITE] || fow_texid != tr.texture[TEX_WHITE]->texid)) {
            TEXTURE fog_texture = {
                .texid = fow_texid,
                .width = (tr.world->width - 1) * 4,
                .height = (tr.world->height - 1) * 4,
            };

            R_DrawImageEx(&MAKE(drawImage_t,
                                .texture = &fog_texture,
                                .screen = *screen,
                                .uv = MAKE(RECT, 0, 0, 1, 1),
                                .color = MAKE(COLOR32, 0, 0, 0, 230),
                                .shader = SHADER_MINIMAP_FOG,
                                .alphamode = BLEND_MODE_BLEND));
        }
    }

    R_DrawMinimapCameraRect(screen);
}

void R_DrawPic(LPCTEXTURE texture, float x, float y) {
    RECT screen = { x, y, texture->width / 2000.0, texture->height / 2000.0};
    R_DrawImage(texture, &screen, NULL, COLOR32_WHITE);
}

void R_DrawLoadingIndicator(LPCRECT rect, DWORD time, COLOR32 color) {
    FLOAT const cx = rect->x + rect->w * 0.5f;
    FLOAT const cy = rect->y + rect->h * 0.5f;
    FLOAT const size = MAX(MIN(rect->w, rect->h) * 0.11f, 0.006f);
    RECT const screen = { cx - size * 0.5f, cy - size * 0.5f, size, size };

    if (!color.a) {
        color = MAKE(COLOR32, 235, 220, 180, 255);
    }

    R_DrawImageEx(&MAKE(drawImage_t,
                        .texture = tr.texture[TEX_LOADING_INDICATOR],
                        .screen = screen,
                        .uv = MAKE(RECT, 0, 0, 1, 1),
                        .color = color,
                        .shader = SHADER_UI,
                        .angle = -360.0f * (FLOAT)(time % 900) / 900.0f));
}

void R_DrawWireRect(LPCRECT rect, COLOR32 color) {
    static VERTEX simp[5];
    R_AddStrip(simp, rect, color);

    MATRIX4 ui_matrix;
    size2_t const window = R_GetWindowSize();
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_STATIC_DRAW);
    
    R_BindTexture(tr.texture[TEX_WHITE], 0);
    
    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDrawArrays, GL_LINE_STRIP, 0, sizeof(simp) / sizeof(*simp));
}

void R_DrawSelectionRect(LPCRECT rect, COLOR32 color) {
    R_DrawWireRect(rect, color);
}

void R_DrawBoundingBox(LPCBOX3 box, LPCMATRIX4 modelMatrix, LPCMATRIX4 vpMatrix, COLOR32 color) {
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7},
    };
    VECTOR3 corners[8] = {
        { box->min.x, box->min.y, box->min.z },
        { box->max.x, box->min.y, box->min.z },
        { box->max.x, box->max.y, box->min.z },
        { box->min.x, box->max.y, box->min.z },
        { box->min.x, box->min.y, box->max.z },
        { box->max.x, box->min.y, box->max.z },
        { box->max.x, box->max.y, box->max.z },
        { box->min.x, box->max.y, box->max.z },
    };
    VERTEX simp[24] = { 0 };
    for (int i = 0; i < 12; i++) {
        simp[i * 2 + 0].position = corners[edges[i][0]];
        simp[i * 2 + 0].color = color;
        simp[i * 2 + 1].position = corners[edges[i][1]];
        simp[i * 2 + 1].color = color;
    }

    R_Call(glUseProgram, tr.shader[SHADER_DEFAULT]->progid);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uViewProjectionMatrix, 1, GL_FALSE, vpMatrix->v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uModelMatrix, 1, GL_FALSE, modelMatrix->v);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(simp), simp, GL_STATIC_DRAW);

    R_BindTexture(tr.texture[TEX_WHITE], 0);

    R_Call(glDisable, GL_CULL_FACE);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDrawArrays, GL_LINES, 0, 24);
    R_Call(glEnable, GL_DEPTH_TEST);
}
