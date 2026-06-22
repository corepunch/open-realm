#include "r_local.h"
#include "r_game.h"
#include "stb/stb_image.h"

#include <SDL2/SDL.h>
#ifndef __APPLE__
#include <SDL2/SDL_opengl.h>
#endif

refImport_t ri;
struct render_globals tr;

SDL_Window *window;
SDL_GLContext context;

static bool renderer_shutdown = false;

LPTEXTURE R_LoadTextureBLP1(HANDLE data, DWORD filesize);
LPTEXTURE R_LoadTextureBLP2(HANDLE data, DWORD filesize);
LPTEXTURE R_LoadTextureDDS(HANDLE data, DWORD filesize);
BOOL R_IsTexturePCX(HANDLE data, DWORD filesize);
LPTEXTURE R_LoadTexturePCX(HANDLE data, DWORD filesize);

static BOOL R_PathHasExtension(LPCSTR path, LPCSTR extension) {
    size_t pathLen;
    size_t extLen;

    if (!path || !extension) {
        return false;
    }
    pathLen = strlen(path);
    extLen = strlen(extension);
    if (pathLen < extLen) {
        return false;
    }
    for (size_t i = 0; i < extLen; i++) {
        char a = path[pathLen - extLen + i];
        char b = extension[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + 0x20);
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + 0x20);
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static LPTEXTURE R_LoadTextureSTB(HANDLE data, DWORD filesize) {
    int width;
    int height;
    BYTE *image;
    LPCOLOR32 pixels;
    LPTEXTURE texture;

    if (!data || filesize > INT32_MAX) {
        return NULL;
    }
    image = stbi_load_from_memory((stbi_uc const *)data, (int)filesize, &width, &height, NULL, STBI_rgb_alpha);
    if (!image || width <= 0 || height <= 0) {
        return NULL;
    }

    pixels = ri.MemAlloc(sizeof(COLOR32) * width * height);
    if (!pixels) {
        stbi_image_free(image);
        return NULL;
    }
    for (DWORD i = 0; i < (DWORD)(width * height); i++) {
        pixels[i].r = image[i * 4 + 2];
        pixels[i].g = image[i * 4 + 1];
        pixels[i].b = image[i * 4 + 0];
        pixels[i].a = image[i * 4 + 3];
    }

    texture = R_AllocateTexture((DWORD)width, (DWORD)height);
    R_LoadTextureMipLevel(texture, 0, pixels, (DWORD)width, (DWORD)height);
    ri.MemFree(pixels);
    stbi_image_free(image);
    return texture;
}

void R_Viewport(LPCRECT viewport) {
    glViewport(viewport->x * tr.drawableSize.width / 800,
               viewport->y * tr.drawableSize.height / 600,
               viewport->w * tr.drawableSize.width / 800,
               viewport->h * tr.drawableSize.height / 600);
}

LPTEXTURE R_AllocateSinglePixelTexture(int color) {
    LPTEXTURE texture = R_AllocateTexture(1, 1);
    R_LoadTextureMipLevel(texture, 0, (LPCCOLOR32)&color, 1, 1);
    return texture;
}

static FLOAT R_SmoothStep(FLOAT edge0, FLOAT edge1, FLOAT x) {
    FLOAT t = (x - edge0) / (edge1 - edge0);
    t = MAX(0.0f, MIN(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

LPTEXTURE R_MakeLoadingIndicatorTexture(void) {
    enum { TEXTURE_SIZE = 128 };
    COLOR32 pixels[TEXTURE_SIZE * TEXTURE_SIZE];
    LPTEXTURE texture = R_AllocateTexture(TEXTURE_SIZE, TEXTURE_SIZE);

    FOR_LOOP(y, TEXTURE_SIZE) {
        FOR_LOOP(x, TEXTURE_SIZE) {
            FLOAT const fx = 1.0f - ((FLOAT)x + 0.5f) / (FLOAT)TEXTURE_SIZE * 2.0f;
            FLOAT const fy = ((FLOAT)y + 0.5f) / (FLOAT)TEXTURE_SIZE * 2.0f - 1.0f;
            FLOAT const distance = sqrtf(fx * fx + fy * fy);
            FLOAT angle = atan2f(fy, fx) / (FLOAT)(M_PI * 2.0);
            FLOAT const outer = 0.92f;
            FLOAT const inner = outer * 0.68f;
            FLOAT const edge = 0.035f;
            FLOAT const ring = R_SmoothStep(inner - edge, inner, distance) *
                               (1.0f - R_SmoothStep(outer, outer + edge, distance));
            BYTE alpha;

            if (angle < 0.0f) {
                angle += 1.0f;
            }
            alpha = (BYTE)(255.0f * ring * angle);
            pixels[y * TEXTURE_SIZE + x] = MAKE(COLOR32, 255, 255, 255, alpha);
        }
    }
    R_LoadTextureMipLevel(texture, 0, pixels, TEXTURE_SIZE, TEXTURE_SIZE);
    return texture;
}

LPTEXTURE R_LoadTexture(LPCSTR textureFilename) {
    LPTEXTURE texture = NULL;
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(textureFilename, &buffer);
    if (fileSize < 0 || !buffer) {
        fprintf(stderr, "R_LoadTexture: not found: %s\n", textureFilename);
        return R_AllocateSinglePixelTexture(0xffffffff);
    }
    switch (*(DWORD *)buffer) {
        case ID_BLP1:
            texture = R_LoadTextureBLP1(buffer, fileSize);
            break;
        case ID_BLP2:
            texture = R_LoadTextureBLP2(buffer, fileSize);
            break;
        case ID_DDS:
            texture = R_LoadTextureDDS(buffer, fileSize);
            break;
        default:
            if (R_IsTexturePCX(buffer, fileSize) || R_PathHasExtension(textureFilename, ".pcx")) {
                texture = R_LoadTexturePCX(buffer, fileSize);
            } else if (R_PathHasExtension(textureFilename, ".tga")) {
                texture = R_LoadTextureSTB(buffer, fileSize);
            }
            if (!texture) {
                fprintf(stderr, "Unknown texture format %.4s in file %s\n", (LPSTR)buffer, textureFilename);
            }
            break;
    }
    ri.FS_FreeFile(buffer);
    return texture;
}

static LPMODEL R_LoadEmptyModel(LPCSTR modelFilename, LPCSTR reason) {
    LPMODEL model;

    fprintf(stderr, "R_LoadModel: %s: %s, using empty model\n", reason, modelFilename);
    model = ri.MemAlloc(sizeof(model_t));
    memset(model, 0, sizeof(*model));
    return model;
}

LPMODEL R_LoadModel(LPCSTR modelFilename) {
    LPMODEL model = R_GameLoadModel(modelFilename);
    return model ? model : R_LoadEmptyModel(modelFilename, "not found");
}

void R_ReleaseModel(LPMODEL model) {
    if (!model) {
        return;
    }
    R_GameReleaseModel(model);
}

LPRENDERTARGET
R_AllocateRenderTexture(GLsizei width,
                        GLsizei height,
                        GLenum format,
                        GLenum type,
                        GLenum attachment)
{
    LPRENDERTARGET rt = ri.MemAlloc(sizeof(RENDERTARGET));
    R_Call(glGenFramebuffers, 1, &rt->buffer);
    R_Call(glGenTextures, 1, &rt->texture);
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, rt->buffer);
    R_Call(glBindTexture, GL_TEXTURE_2D, rt->texture);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    R_Call(glTexImage2D, GL_TEXTURE_2D, 0, format, width, height, 0, format, type, NULL);
    R_Call(glFramebufferTexture2D, GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, rt->texture, 0);
    if (attachment == GL_COLOR_ATTACHMENT0) {
        glClear(GL_COLOR_BUFFER_BIT);
    }
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, 0);
    return rt;
}

void R_ReleaseRenderTexture(LPRENDERTARGET rt) {
    if (!rt) {
        return;
    }
    if (rt->buffer) {
        glDeleteFramebuffers(1, &rt->buffer);
        rt->buffer = 0;
    }
    if (rt->texture) {
        glDeleteTextures(1, &rt->texture);
        rt->texture = 0;
    }
    ri.MemFree(rt);
}

static void R_SetupGL(bool drawLight) {
    size2_t const window = R_GetWindowSize();
    
    MATRIX4 model_matrix;
    MATRIX3 normal_matrix;
    MATRIX4 ui_matrix;

    Matrix4_identity(&model_matrix);
    R_GameSetupTextureMatrix();
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    Matrix3_normal(&normal_matrix, &model_matrix);

    R_Call(glEnable, GL_CULL_FACE);
    R_Call(glCullFace, GL_BACK);
    
    GLfloat const *viewProjectionMatrix = R_ViewProjectionForPass()->v;

    R_Call(glUseProgram, tr.shader[SHADER_DEFAULT]->progid);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uViewProjectionMatrix, 1, GL_FALSE, viewProjectionMatrix);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_DEFAULT]->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniformMatrix3fv, tr.shader[SHADER_DEFAULT]->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);

    R_Call(glUseProgram, tr.shader[SHADER_UI]->progid);

    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uViewProjectionMatrix, 1, GL_FALSE, ui_matrix.v);
    R_Call(glUniformMatrix4fv, tr.shader[SHADER_UI]->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);

    if (drawLight) {
        R_Call(glViewport, 0, 0, SHADOW_TEXSIZE, SHADOW_TEXSIZE);
        R_Call(glScissor, 0, 0, SHADOW_TEXSIZE, SHADOW_TEXSIZE);
        R_Call(glBindFramebuffer, GL_FRAMEBUFFER, tr.rt[RT_DEPTHMAP]->buffer);
        R_Call(glDepthMask, GL_TRUE);
        R_Call(glClear, GL_DEPTH_BUFFER_BIT);
    } else {
        R_Call(glBindFramebuffer, GL_FRAMEBUFFER, 0);
        R_Call(glActiveTexture, GL_TEXTURE1);
        R_Call(glBindTexture, GL_TEXTURE_2D, tr.rt[RT_DEPTHMAP]->texture);
    }
}

static LPCSTR R_GLString(GLenum name) {
    GLubyte const *value = glGetString(name);
    return value ? (LPCSTR)value : "unknown";
}

static void R_PrintGLExtensions(void) {
    GLint num_extensions = 0;

    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    fprintf(stderr, "GL_EXTENSIONS:");
    FOR_LOOP(i, num_extensions) {
        GLubyte const *extension = glGetStringi(GL_EXTENSIONS, i);
        if (extension) {
            fprintf(stderr, " %s", extension);
        }
    }
    fprintf(stderr, "\n\n");
}

static void R_PrintDisplayModes(void) {
    int num_modes = SDL_GetNumDisplayModes(0);

    fprintf(stderr, "SDL display modes:\n");
    if (num_modes <= 0) {
        fprintf(stderr, " - none reported\n");
        return;
    }

    FOR_LOOP(i, num_modes) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(0, i, &mode) != 0) {
            continue;
        }
        fprintf(stderr,
                " - Mode %2d: %dx%d@%d\n",
                i,
                mode.w,
                mode.h,
                mode.refresh_rate);
    }
}

void R_Init(DWORD width, DWORD height) {
    renderer_shutdown = false;
    BOOL gl_current = false;
    SDL_version sdl_version;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
#ifdef SC2
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    fprintf(stderr, "Video initialization.\n");
    SDL_GetVersion(&sdl_version);
    fprintf(stderr,
            "SDL version is: %d.%d.%d\n",
            sdl_version.major,
            sdl_version.minor,
            sdl_version.patch);
    fprintf(stderr, "SDL video driver is \"%s\".\n", SDL_GetCurrentVideoDriver());
    R_PrintDisplayModes();
    fprintf(stderr, "Video initialized.\n\n");
    
    fprintf(stderr, "Refresher initialization.\n");
    window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    context = SDL_GL_CreateContext(window);
    if (context && SDL_GL_MakeCurrent(window, context) == 0) {
        gl_current = true;
    } else {
        fprintf(stderr, "ref_gl::R_Init() - could not make GL context current: %s\n", SDL_GetError());
    }
    
    SDL_GL_GetDrawableSize(window, (int *)&tr.drawableSize.width, (int *)&tr.drawableSize.height);
    fprintf(stderr, "Refresh: OpenWarcraft3 OpenGL Refresher\n");
    fprintf(stderr, "Client: OpenWarcraft3\n\n");
    fprintf(stderr, "Drawable size: %dx%d\n\n", tr.drawableSize.width, tr.drawableSize.height);
    if (gl_current) {
        fprintf(stderr, "OpenGL setting:\n");
        fprintf(stderr, "GL_VENDOR: %s\n", R_GLString(GL_VENDOR));
        fprintf(stderr, "GL_RENDERER: %s\n", R_GLString(GL_RENDERER));
        fprintf(stderr, "GL_VERSION: %s\n", R_GLString(GL_VERSION));
        fprintf(stderr, "GL_SHADING_LANGUAGE_VERSION: %s\n", R_GLString(GL_SHADING_LANGUAGE_VERSION));
        R_PrintGLExtensions();
    }
    
//    m3 = R_LoadModel("Assets\\Units\\Terran\\SpecialOpsDropship\\SpecialOpsDropship.m3");
//    R_LoadModel("Assets\\Units\\Terran\\MarineTychus\\MarineTychus.m3");
//    R_LoadModel("Assets\\Units\\Zerg\\Queen\\Queen.m3");
    
    // Disabled until skin/alphatest shaders are wired; Linux -Wall warns on unused extern hooks.
//    extern LPCSTR vs_skin;
    extern LPCSTR vs_default;
    extern LPCSTR fs_default;
    extern LPCSTR fs_ui;
    extern LPCSTR fs_splat;
//    extern LPCSTR fs_alphatest;
    extern LPCSTR fs_commandbutton;
    extern LPCSTR fs_minimap_fog;
    
    R_GameLoadAssets();

    tr.shader[SHADER_DEFAULT] = R_InitShader(vs_default, fs_default);
    tr.shader[SHADER_UI] = R_InitShader(vs_default, fs_ui);
    tr.shader[SHADER_SPLAT] = R_InitShader(vs_default, fs_splat);
    tr.shader[SHADER_COMMANDBUTTON] = R_InitShader(vs_default, fs_commandbutton);
    tr.shader[SHADER_MINIMAP_FOG] = R_InitShader(vs_default, fs_minimap_fog);
    fprintf(stderr, "Loading shaders succeeded.\n");

    tr.buffer[RBUF_TEMP1] = R_MakeVertexArrayObject(NULL, 0);
    tr.texture[TEX_WHITE] = R_AllocateSinglePixelTexture(0xffffffff);
    tr.texture[TEX_BLACK] = R_AllocateSinglePixelTexture(0xff000000);
    tr.texture[TEX_LOADING_INDICATOR] = R_MakeLoadingIndicatorTexture();
    tr.texture[TEX_FONT] = R_MakeSysFontTexture();
    tr.rt[RT_DEPTHMAP] = R_AllocateRenderTexture(SHADOW_TEXSIZE, SHADOW_TEXSIZE, GL_DEPTH_COMPONENT, GL_FLOAT, GL_DEPTH_ATTACHMENT);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glClearColor, 0.0, 0.0, 0.0, 1.0);
    R_Call(glViewport, 0, 0, tr.drawableSize.width, tr.drawableSize.height);
    R_InitParticles();
    R_GameInit();
    fprintf(stderr, "Refresher initialized.\n\n");
}

void R_Shutdown(void) {
    if (renderer_shutdown) {
        return;
    }
    renderer_shutdown = true;
    R_GameShutdown();
    R_ShutdownFonts();
    
    R_ShutdownFogOfWar();
    R_ShutdownParticles();
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void R_SetupViewport(LPCRECT r) {
    R_Call(glViewport,
           r->x * tr.drawableSize.width,
           r->y * tr.drawableSize.height,
           r->w * tr.drawableSize.width,
           r->h * tr.drawableSize.height);
}

void R_SetupScissor(LPCRECT r) {
    R_Call(glEnable, GL_SCISSOR_TEST);
    R_Call(glScissor,
           r->x * tr.drawableSize.width,
           r->y * tr.drawableSize.height,
           r->w * tr.drawableSize.width,
           r->h * tr.drawableSize.height);
}

void R_RevertSettings(void) {
    R_SetupViewport(&(RECT){0,0,1,1});
    R_SetupScissor(&(RECT){0,0,1,1});
}

void R_RenderShadowMap(void) {
    tr.pass = R_PASS_SHADOW;
    R_SetupGL(true);
    R_BindTexture(tr.texture[TEX_SHADOWMAP], 1);
    R_GameDrawWorld();
    R_DrawEntities();
}

void R_RenderView(void) {
    tr.pass = R_PASS_COLOR;
    R_SetupViewport(&tr.viewDef.viewport);
    R_SetupScissor(&tr.viewDef.scissor);
    R_SetupGL(false);
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) {
        R_Call(glClear, GL_DEPTH_BUFFER_BIT);
    }
    R_GameDrawWorld();
    R_DrawDecals();
    R_DrawEntities();
    R_GameDrawAlphaSurfaces();
    R_DrawParticles();
    R_RevertSettings();
    
//    extern LPCTEXTURE dds;
//    R_DrawPic(dds, 0, 0);
}

void R_RenderFrame(viewDef_t const *viewDef) {
    tr.viewDef = *viewDef;

    if (!tr.viewDef.scissor.w && !tr.viewDef.scissor.h) {
        tr.viewDef.scissor = (RECT){0, 0, 1, 1};
    }

    if ((tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) && tr.viewDef.num_entities > 0) {
        renderEntity_t const *entity = &tr.viewDef.entities[0];
        float aspect = (tr.viewDef.viewport.w * tr.drawableSize.width) > 0.0f
            ? (tr.viewDef.viewport.w * tr.drawableSize.width) / (tr.viewDef.viewport.h * tr.drawableSize.height)
            : 1.0f;
        if (!R_GameExtractEntityCamera(entity, aspect, &tr.viewDef)) {
            Matrix4_identity(&tr.viewDef.viewProjectionMatrix);
            Matrix4_identity(&tr.viewDef.textureMatrix);
            Matrix4_identity(&tr.viewDef.lightMatrix);
        }
    }

    Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);

    R_RenderFogOfWar();
    R_Call(glActiveTexture, GL_TEXTURE2);
    R_Call(glBindTexture, GL_TEXTURE_2D, R_GetFogOfWarTexture());
    R_Call(glActiveTexture, GL_TEXTURE3);
    R_Call(glBindTexture, GL_TEXTURE_2D, tr.texture[TEX_TERRAIN_SHADOW] ? tr.texture[TEX_TERRAIN_SHADOW]->texid : tr.texture[TEX_WHITE]->texid);
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_RenderShadowMap();
    R_RenderView();
}

void R_DrawBuffer(LPCBUFFER buffer, DWORD num_vertices) {
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
}

void R_DrawIndexedBuffer(LPCBUFFER buffer, DWORD num_indices) {
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buffer->ibo);
    R_Call(glDrawElements, GL_TRIANGLES, num_indices, GL_UNSIGNED_INT, NULL);
}

void R_BeginFrame(void) {
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);
    R_Call(glEnable, GL_CULL_FACE);
    R_Call(glCullFace, GL_BACK);
    R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    R_Call(glClear, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#ifdef SC2
    R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
#endif
}

void R_EndFrame(void) {
#ifdef SC2
    R_Call(glColorMask, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#endif
    SDL_GL_SwapWindow(window);
    SDL_Delay(1);
}

size2_t R_GetWindowSize(void) {
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    return (size2_t) {
        .width = width,
        .height = height,
    };
}

size2_t R_GetTextureSize(LPCTEXTURE texture) {
    if (!texture) {
        return (size2_t) { 0, 0 };
    } else {
        return (size2_t) {
            .width = texture->width,
            .height = texture->height,
        };
    }
}

bool R_GetModelInfo(LPMODEL model, LPMODELINFO info) {
    if (!model || !info) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    return R_GameGetModelInfo(model, info);
}

void R_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    R_GameDrawSprite(model, anim, x, y);
}

bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    return R_GameSetEntityAnimFrame(model, anim, entity);
}

refExport_t R_GetAPI(refImport_t imp) {
#ifdef DEBUG_PATHFINDING
    void R_SetPathTexture(LPCCOLOR32 debugTexture);
#endif
    ri = imp;
    return (refExport_t) {
        .Init = R_Init,
        .RegisterMap = R_GameRegisterMap,
        .LoadTexture = R_LoadTexture,
        .LoadModel = R_LoadModel,
        .LoadFont = R_LoadFont,
        .SetFogOfWarData = R_SetFogOfWarData,
        .ReleaseTexture = R_ReleaseTexture,
        .ReleaseModel = R_ReleaseModel,
        .RenderFrame = R_RenderFrame,
        .Shutdown = R_Shutdown,
        .BeginFrame = R_BeginFrame,
        .EndFrame = R_EndFrame,
        .DrawPic = R_DrawPic,
        .DrawImage = R_DrawImage,
        .DrawImageEx = R_DrawImageEx,
        .DrawMinimap = R_DrawMinimap,
        .DrawLoadingIndicator = R_DrawLoadingIndicator,
        .DrawSelectionRect = R_DrawSelectionRect,
        .DrawChar = R_DrawChar,
        .DrawFill = R_DrawFill,
        .GetWindowSize = R_GetWindowSize,
        .GetTextureSize = R_GetTextureSize,
        .DrawSprite = R_DrawSprite,
        .SetEntityAnimFrame = R_SetEntityAnimFrame,
        .DrawText = R_DrawText,
        .GetTextSize = R_GetTextSize,
        .GetModelInfo = R_GetModelInfo,
        .DrawBoundingBox = R_DrawBoundingBox,
        .GetHeightAtPoint = R_GameGetHeightAtPoint,
        .TraceEntity = R_TraceEntity,
        .TraceLocation = R_GameTraceLocation,
        .EntitiesInRect = R_EntitiesInRect,
#ifdef DEBUG_PATHFINDING
        .SetPathTexture = R_SetPathTexture,
#endif
    };
}
