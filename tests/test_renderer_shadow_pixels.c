#include "test.h"
#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/w3m/r_war3map.h"
#include <stdlib.h>

static handle_t pixels_alloc(long size) { return calloc(1, size); }
static cstring_t pixels_cvar(cstring_t name, cstring_t fallback) { (void)name; return fallback; }

/* Opt-in driver test: CPU upload/source assertions cannot detect a fragment
 * cutoff. Run with a GL display (a hidden SDL window is sufficient). */
TEST(renderer_shadow_pixels, transparent_sampler_border_fades_without_changing_interior) {
    SDL_Window *window;
    SDL_GLContext context;
    GLuint fbo, target;
    uint8_t image[256 * 192 * 4];
    war3mapVertex_t corners[9] = {0};
    war3map_t map = { .width = 3, .height = 3, .vertices = corners };
    vec2_t mins = {56.5f,56.5f}, maxs = {216.5f,152.5f};
    vec2_t uv0 = {-0.5f/9, -0.5f/5}, uv1 = {1+0.5f/9, 1+0.5f/5};
    T_EQ(SDL_Init(SDL_INIT_VIDEO), 0);
#ifdef BZ_GL_ES3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif
    window = SDL_CreateWindow("Shadow pixels", 0, 0, 256, 192, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    T_NOT_NULL(window); if (!window) { SDL_Quit(); return; }
    context = SDL_GL_CreateContext(window);
    T_NOT_NULL(context); if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return; }
    ri.MemAlloc = pixels_alloc; ri.MemFree = free; ri.CvarString = pixels_cvar;
    R_InitTextureFormats();
    tr.buffer[RBUF_TEMP1] = R_MakeVertexArrayObject(NULL, 0);
    FOR_LOOP(i, 9) corners[i] = (war3mapVertex_t){ .accurate_height = 8192, .level = 2 };
    tr.world = &map;
    Matrix4_ortho(&tr.viewDef.viewProjectionMatrix, 0, 256, 0, 192, -1000, 1000);
    glGenTextures(1, &target); glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    T_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 256, 192); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    texture_t *texture = R_AllocateTexture(9, 5);
    static color32_t const backgrounds[] = {{255,255,255,255}, {64,128,48,255}, {128,96,64,255}};
    FOR_LOOP(background, 3) FOR_LOOP(border, 2) FOR_LOOP(fog, 2) FOR_LOOP(overlap, 2) {
        color32_t pixels[45];
        FOR_LOOP(y, 5) FOR_LOOP(x, 9)
            pixels[x+y*9] = (color32_t){0,0,0, border || (x && y && x < 8 && y < 4) ? 255 : 0};
        R_LoadTextureMipLevel(texture, &(texMip_t){pixels,9,5,0,PIXEL_RGBA});
        tr.viewDef.fogEnable = fog;
        tr.viewDef.fogColor = (vec3_t){0.4f,0.4f,0.4f};
        tr.viewDef.fogStart = 0; tr.viewDef.fogEnd = 1;
        splat_shader_t *shader = R_W3ShadowShader(texture);
        R_InvalidateSplatGeometryCache();
        FOR_LOOP(pass, 3) {
            color32_t back = backgrounds[background];
            glClearColor(back.r/255.0f,back.g/255.0f,back.b/255.0f,1); glClear(GL_COLOR_BUFFER_BIT);
            R_BeginSplatBatch(shader);
            FOR_LOOP(copy, overlap + 1)
                R_AddRectSplatUV(&(rectSplatParams_t){ .mins = &mins, .maxs = &maxs, .uv_mins = &uv0, .uv_maxs = &uv1,
                    .texture = texture, .shader = shader, .color = {0,0,0,128} });
            R_EndSplatBatch();
            glReadPixels(0,0,256,192,GL_RGBA,GL_UNSIGNED_BYTE,image);
#define PIX(x) image[(104*256+(x))*4]
            T_EQ(PIX(55), back.r); T_EQ(PIX(217), back.r);
            T_ASSERT(PIX(136) < back.r);
            if (border) {
                T_ASSERT(PIX(60) > PIX(64)); T_ASSERT(PIX(64) > PIX(72));
                T_ASSERT(PIX(212) > PIX(208)); T_ASSERT(PIX(208) > PIX(200));
                if (!fog && !overlap) {
                    T_ASSERT(fabsf(PIX(64) - back.r*191.0f/255) <= 1);
                    T_ASSERT(fabsf(PIX(60) - back.r*223.0f/255) <= 1);
                }
            } else { T_EQ(PIX(64),back.r); T_EQ(PIX(72),back.r); }
            if (!fog) {
                float transmission = 127.0f/255;
                if (overlap) transmission *= transmission;
                T_ASSERT(fabsf(PIX(136) - back.r * transmission) <= 1);
            }
#undef PIX
            T_EQ(glGetError(), GL_NO_ERROR);
        }
    }
    R_InvalidateSplatGeometryCache(); R_ReleaseTexture(texture);
    R_ReleaseVertexArrayObject(tr.buffer[RBUF_TEMP1]); tr.buffer[RBUF_TEMP1] = NULL; tr.world = NULL;
    R_W3ShutdownShadowShader();
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&target);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
}
