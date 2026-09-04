#include "r_local.h"
#include "r_game.h"
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <SDL2/SDL.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#ifdef __APPLE__
/* Forward-declare the Objective-C runtime calls we need without pulling in
 * <objc/objc.h>, which redefines BOOL and conflicts with our project typedef. */
typedef void *MacId;
typedef void *MacSel;
extern MacId  objc_getClass(const char *name);
extern MacSel sel_registerName(const char *str);
extern MacId  objc_msgSend(MacId, MacSel, ...);
#endif
#ifndef __APPLE__
#include <SDL2/SDL_opengl.h>
#endif

refImport_t ri;
struct render_globals tr;

SDL_Window *window;
SDL_GLContext context;

static bool renderer_shutdown = false;

/* Capture the physical GL drawable; SDL window dimensions are logical points on Retina. */
static void R_Screenshot(void) {
    GLint viewport[4];
    DWORD width, height;
    int slot;
    char path[512];
    BYTE *pixels;

    R_Call(glGetIntegerv, GL_VIEWPORT, viewport);
    width = viewport[2] > 0 ? (DWORD)viewport[2] : 0;
    height = viewport[3] > 0 ? (DWORD)viewport[3] : 0;
    if (!width || !height) return;
#ifndef _WIN32
    mkdir("screenshots", 0777);
#else
    _mkdir("screenshots");
#endif
    for (slot = 0; slot <= 9999; slot++) {
        snprintf(path, sizeof(path), "screenshots/shot%04d.jpg", slot);
        FILE *file = fopen(path, "rb");
        if (!file) break;
        fclose(file);
    }
    if (slot > 9999) { fprintf(stderr, "Screenshot: no free slot (max 10000)\n"); return; }
    pixels = ri.MemAlloc((long)((size_t)width * height * 3));
    if (!pixels) { fprintf(stderr, "Screenshot: alloc failed\n"); return; }
    {
        BYTE *rgba = ri.MemAlloc((long)((size_t)width * height * 4));
        if (!rgba) { ri.MemFree(pixels); return; }
        R_Call(glReadPixels, 0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        /* Strip alpha: RGBA → RGB */
        for (DWORD i = 0; i < width * height; i++) {
            pixels[i*3+0] = rgba[i*4+0];
            pixels[i*3+1] = rgba[i*4+1];
            pixels[i*3+2] = rgba[i*4+2];
        }
        ri.MemFree(rgba);
    }
    stbi_flip_vertically_on_write(1);
    if (stbi_write_jpg(path, (int)width, (int)height, 3, pixels, 90))
        fprintf(stderr, "Wrote %s (%ux%u drawable pixels)\n", path, width, height);
    else
        fprintf(stderr, "Screenshot: write failed for %s\n", path);
    ri.MemFree(pixels);
}

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
    LPTEXTURE texture;

    if (!data || filesize > INT32_MAX) {
        return NULL;
    }
    image = stbi_load_from_memory((stbi_uc const *)data, (int)filesize, &width, &height, NULL, STBI_rgb_alpha);
    if (!image || width <= 0 || height <= 0) {
        return NULL;
    }

    /* STB already returns RGBA; the old BGRA copy made colors depend on the uploader's OS branch. */
    texture = R_AllocateTexture((DWORD)width, (DWORD)height);
    R_LoadTextureMipLevel(texture, &(TEXMIP){ image, (DWORD)width, (DWORD)height, 0, PIXEL_RGBA });
    stbi_image_free(image);
    return texture;
}

void R_Viewport(LPCRECT viewport) {
    glViewport(viewport->x * tr.drawableSize.width / 800,
               viewport->y * tr.drawableSize.height / 600,
               viewport->w * tr.drawableSize.width / 800,
               viewport->h * tr.drawableSize.height / 600);
}

static LPTEXTURE R_MakePlaceholderTexture(void) {
    enum { SIZE = 16 };
    COLOR32 pixels[SIZE * SIZE];
    LPTEXTURE texture = R_AllocateTexture(SIZE, SIZE);

    FOR_LOOP(y, SIZE) FOR_LOOP(x, SIZE) {
        BOOL const checker = ((x ^ y) & 1) != 0;
        pixels[y * SIZE + x] = checker ? MAKE(COLOR32, 255, 0, 255, 255)
                                        : MAKE(COLOR32, 0, 0, 0, 255);
    }
    R_LoadTextureMipLevel(texture, &(TEXMIP){ pixels, SIZE, SIZE, 0, PIXEL_RGBA });
    return texture;
}

LPTEXTURE R_AllocateSinglePixelTexture(int color) {
    LPTEXTURE texture = R_AllocateTexture(1, 1);
    R_LoadTextureMipLevel(texture, &(TEXMIP){ &color, 1, 1, 0, PIXEL_RGBA });
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
    R_LoadTextureMipLevel(texture, &(TEXMIP){ pixels, TEXTURE_SIZE, TEXTURE_SIZE, 0, PIXEL_RGBA });
    return texture;
}

/* WoW archives do not contain Warcraft III's selection-circle assets. */
LPTEXTURE R_MakeSelectionCircleTexture(void) {
    enum { TEXTURE_SIZE = 128 };
    COLOR32 pixels[TEXTURE_SIZE * TEXTURE_SIZE];
    LPTEXTURE texture = R_AllocateTexture(TEXTURE_SIZE, TEXTURE_SIZE);

    FOR_LOOP(y, TEXTURE_SIZE) FOR_LOOP(x, TEXTURE_SIZE) {
        FLOAT fx = ((FLOAT)x + 0.5f) / TEXTURE_SIZE * 2.0f - 1.0f;
        FLOAT fy = ((FLOAT)y + 0.5f) / TEXTURE_SIZE * 2.0f - 1.0f;
        FLOAT distance = sqrtf(fx * fx + fy * fy);
        FLOAT outer = 0.92f, inner = 0.78f, edge = 0.035f;
        FLOAT ring = R_SmoothStep(inner - edge, inner, distance) *
                     (1.0f - R_SmoothStep(outer, outer + edge, distance));
        pixels[y * TEXTURE_SIZE + x] = MAKE(COLOR32, 255, 255, 255, (BYTE)(ring * 255.0f));
    }
    R_LoadTextureMipLevel(texture, &(TEXMIP){ pixels, TEXTURE_SIZE, TEXTURE_SIZE, 0, PIXEL_RGBA });
    return texture;
}

static LPTEXTURE R_MakeBlobShadowTexture(void) {
    enum { TEXTURE_SIZE = 64 };
    COLOR32 pixels[TEXTURE_SIZE * TEXTURE_SIZE];
    LPTEXTURE texture = R_AllocateTexture(TEXTURE_SIZE, TEXTURE_SIZE);

    FOR_LOOP(y, TEXTURE_SIZE) {
        FOR_LOOP(x, TEXTURE_SIZE) {
            FLOAT const fx = ((FLOAT)x + 0.5f) / (FLOAT)TEXTURE_SIZE * 2.0f - 1.0f;
            FLOAT const fy = ((FLOAT)y + 0.5f) / (FLOAT)TEXTURE_SIZE * 2.0f - 1.0f;
            FLOAT const distance = sqrtf(fx * fx + fy * fy);
            FLOAT const alpha = 1.0f - R_SmoothStep(0.25f, 1.0f, distance);

            pixels[y * TEXTURE_SIZE + x] = MAKE(COLOR32, 255, 255, 255, (BYTE)(alpha * 255.0f));
        }
    }

    R_LoadTextureMipLevel(texture, &(TEXMIP){ pixels, TEXTURE_SIZE, TEXTURE_SIZE, 0, PIXEL_RGBA });
    return texture;
}

static LPTEXTURE R_LoadTexturePath(LPCSTR textureFilename, BOOL *found) {
    LPTEXTURE texture = R_FindLoadedTexture(textureFilename);
    void *buffer = NULL;
    PATHSTR load_path;
    int fileSize;

    if (texture) {
        if (found) *found = true;
        return texture;
    }
    fileSize = R_ReadTextureFile(textureFilename, load_path, &buffer);
    if (fileSize < 0 || !buffer) {
        if (found) *found = false;
        return NULL;
    }
    if (found) *found = true;
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
            if (R_IsTexturePCX(buffer, fileSize) || R_PathHasExtension(load_path, ".pcx")) {
                texture = R_LoadTexturePCX(buffer, fileSize);
            } else if (R_PathHasExtension(load_path, ".tga")) {
                texture = R_LoadTextureSTB(buffer, fileSize);
            }
            if (!texture) {
                fprintf(stderr, "Unknown texture format %.4s in file %s\n", (LPSTR)buffer, load_path);
            }
            break;
    }
    ri.FS_FreeFile(buffer);
    if (!texture) texture = tr.texture[TEX_PLACEHOLDER];
    R_CacheLoadedTexture(textureFilename, texture);
    return texture;
}

LPTEXTURE R_LoadTexture(LPCSTR textureFilename) {
    PATHSTR scoped;
    LPTEXTURE texture;
    BOOL found = false;
    BOOL has_scope;

    if (!textureFilename || !*textureFilename) return tr.texture[TEX_PLACEHOLDER];
    has_scope = R_MapAssetCandidate(textureFilename, scoped, sizeof(scoped));
    if (has_scope) {
        texture = R_LoadTexturePath(scoped, &found);
        if (found) return texture;
    }
    texture = R_LoadTexturePath(textureFilename, &found);
    if (found) {
        if (has_scope) R_CacheLoadedTexture(scoped, texture);
        return texture;
    }
    /* Missing registrations are resident too: repeated draw paths must not search every MPQ again. */
    fprintf(stderr, "R_LoadTexture: not found: %s\n", textureFilename);
    R_CacheLoadedTexture(textureFilename, tr.texture[TEX_PLACEHOLDER]);
    if (has_scope) R_CacheLoadedTexture(scoped, tr.texture[TEX_PLACEHOLDER]);
    return tr.texture[TEX_PLACEHOLDER];
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
    R_SetupTextureMatrix();
    Matrix4_ortho(&ui_matrix, 0.0f, window.width, window.height, 0.0f, 0.0f, 100.0f);

    Matrix3_normal(&normal_matrix, &model_matrix);

    R_Call(glEnable, GL_CULL_FACE);
    R_Call(glCullFace, GL_BACK);
    
    GLfloat const *viewProjectionMatrix =
#ifdef USE_SHADOWMAPS
        drawLight ? tr.viewDef.lightMatrix.v :
#endif
        tr.viewDef.viewProjectionMatrix.v;

    memcpy(&tr.shader_default.state.viewProjection, viewProjectionMatrix, (1) * sizeof(MATRIX4));
    tr.shader_default.state.textureMatrix = tr.viewDef.textureMatrix;
    tr.shader_default.state.model = model_matrix;
    tr.shader_default.state.lightMatrix = tr.viewDef.lightMatrix;
    tr.shader_default.state.normalMatrix = normal_matrix;

    tr.shader_ui.state.viewProjection = ui_matrix;
    tr.shader_ui.state.model = model_matrix;
    
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDepthFunc, GL_LEQUAL);

#ifdef USE_SHADOWMAPS
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
#else
    (void)drawLight;
    R_Call(glBindFramebuffer, GL_FRAMEBUFFER, 0);
#endif
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

static int r_swapinterval = -999;

/* Apply console changes without repeating the expensive Cocoa/Metal swap-interval call every frame. */
static void R_UpdateSwapInterval(void) {
    int requested = atoi(ri.CvarString ? ri.CvarString("r_vsync", "0") : "0");
    requested = MAX(0, MIN(1, requested));
    if (requested == r_swapinterval) return;
    r_swapinterval = requested;
    if (SDL_GL_SetSwapInterval(requested) != 0)
        fprintf(stderr, "OpenGL: r_vsync %d unavailable: %s\n", requested, SDL_GetError());
    else
        fprintf(stderr, "OpenGL: vsync=%d\n", SDL_GL_GetSwapInterval());
}

void R_InitRenderer(DWORD width, DWORD height) {
    renderer_shutdown = false;
    r_swapinterval = -999;
    BOOL gl_current = false;
    int requested_msaa = BZ_MSAA_SAMPLES;
    SDL_version sdl_version;

#ifdef __APPLE__
    /* On macOS, SDL_Init(SDL_INIT_VIDEO) calls [NSApp finishLaunching] which
     * activates the process regardless of window visibility.  Set the policy
     * to Prohibited first so the app never appears in the Dock or takes focus.
     * NSApplicationActivationPolicyProhibited = 2. */
    if (atoi(ri.CvarString("vid_hidden", "0"))) {
        MacId ns_app = ((MacId(*)(MacId, MacSel))objc_msgSend)(
            objc_getClass("NSApplication"),
            sel_registerName("sharedApplication"));
        ((int(*)(MacId, MacSel, long))objc_msgSend)(
            ns_app,
            sel_registerName("setActivationPolicy:"),
            2L /* NSApplicationActivationPolicyProhibited */);
    }
#endif
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
#ifdef BZ_GL_ES3
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, requested_msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, requested_msaa);

    fprintf(stderr, "Video initialization.\n");
    SDL_GetVersion(&sdl_version);
    fprintf(stderr,
            "SDL version is: %d.%d.%d\n",
            sdl_version.major,
            sdl_version.minor,
            sdl_version.patch);
    fprintf(stderr, "SDL video driver is \"%s\".\n", SDL_GetCurrentVideoDriver());
    /* The full SDL mode list is diagnostic output, previously printed on every startup. */
    if (atoi(ri.CvarString("vid_modes", "0"))) R_PrintDisplayModes();
    fprintf(stderr, "Video initialized.\n\n");
    
    fprintf(stderr, "Refresher initialization.\n");
    Uint32 win_vis = atoi(ri.CvarString("vid_hidden", "0")) ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | win_vis | SDL_WINDOW_ALLOW_HIGHDPI);
    context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context && requested_msaa) {
        fprintf(stderr, "OpenGL: %dx MSAA context unavailable (%s); retrying without MSAA\n", requested_msaa, SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | win_vis | SDL_WINDOW_ALLOW_HIGHDPI);
        context = window ? SDL_GL_CreateContext(window) : NULL;
    }
    if (context && SDL_GL_MakeCurrent(window, context) == 0) {
        gl_current = true;
        R_UpdateSwapInterval();
    } else {
        fprintf(stderr, "ref_gl::R_Init() - could not make GL context current: %s\n", SDL_GetError());
    }
    
    SDL_GL_GetDrawableSize(window, (int *)&tr.drawableSize.width, (int *)&tr.drawableSize.height);
    fprintf(stderr, "Refresh: OpenWarcraft3 OpenGL Refresher\n");
    fprintf(stderr, "Client: OpenWarcraft3\n\n");
    fprintf(stderr, "Drawable size: %dx%d\n\n", tr.drawableSize.width, tr.drawableSize.height);
    if (gl_current) {
        GLint sample_buffers = 0, samples = 0;
        int sdl_buffers = 0, sdl_samples = 0;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sdl_buffers);
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &sdl_samples);
        R_Call(glGetIntegerv, GL_SAMPLE_BUFFERS, &sample_buffers);
        R_Call(glGetIntegerv, GL_SAMPLES, &samples);
        tr.msaa_samples = R_MsaaActiveSamples(sample_buffers, samples);
        fprintf(stderr, "OpenGL setting:\n");
        fprintf(stderr, "GL_VENDOR: %s\n", R_GLString(GL_VENDOR));
        fprintf(stderr, "GL_RENDERER: %s\n", R_GLString(GL_RENDERER));
        fprintf(stderr, "GL_VERSION: %s\n", R_GLString(GL_VERSION));
        fprintf(stderr, "GL_SHADING_LANGUAGE_VERSION: %s\n", R_GLString(GL_SHADING_LANGUAGE_VERSION));
        fprintf(stderr, "MSAA: requested=%dx SDL=%d/%d GL=%d/%d active=%dx alpha-key=%s\n",
                requested_msaa, sdl_buffers, sdl_samples, sample_buffers, samples, tr.msaa_samples,
#ifdef BZ_USE_MSAA
                tr.msaa_samples ? "alpha-to-coverage" : "blended fallback");
#else
                "hard discard");
#endif
        fprintf(stderr, "Bone palette: %u matrices fixed; shader compile/link validates support\n", BZ_BONE_PALETTE_MAX);
        R_PrintGLExtensions();
        R_InitTextureFormats();
    }
    
//    m3 = R_LoadModel("Assets\\Units\\Terran\\SpecialOpsDropship\\SpecialOpsDropship.m3");
//    R_LoadModel("Assets\\Units\\Terran\\MarineTychus\\MarineTychus.m3");
//    R_LoadModel("Assets\\Units\\Zerg\\Queen\\Queen.m3");
    
    R_LoadAssets();

    R_LoadBuiltinShaders();
    fprintf(stderr, "Loading shaders succeeded.\n");

    tr.buffer[RBUF_TEMP1] = R_MakeVertexArrayObject(NULL, 0);
    tr.texture[TEX_WHITE] = R_AllocateSinglePixelTexture(0xffffffff);
    tr.texture[TEX_BLACK] = R_AllocateSinglePixelTexture(0xff000000);
    tr.texture[TEX_PLACEHOLDER] = R_MakePlaceholderTexture();
    tr.texture[TEX_BLOB_SHADOW] = R_MakeBlobShadowTexture();
    tr.texture[TEX_LOADING_INDICATOR] = R_MakeLoadingIndicatorTexture();
    tr.texture[TEX_FONT] = R_MakeSysFontTexture();
#ifdef USE_SHADOWMAPS
    tr.rt[RT_DEPTHMAP] = R_AllocateRenderTexture(SHADOW_TEXSIZE, SHADOW_TEXSIZE, GL_DEPTH_COMPONENT, GL_FLOAT, GL_DEPTH_ATTACHMENT);
#endif
    R_Call(glDisable, GL_DEPTH_TEST);
    R_Call(glClearColor, 0.0, 0.0, 0.0, 1.0);
    R_Call(glViewport, 0, 0, tr.drawableSize.width, tr.drawableSize.height);
    R_InitParticles();
    R_Init();
    fprintf(stderr, "Refresher initialized.\n\n");
}

/* Alpha-key shader variants discard without MSAA and convert alpha to sample coverage with it. */
void R_SetAlphaKeyState(BOOL enabled) {
    if (!enabled) {
        R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
        return;
    }
#ifdef BZ_USE_MSAA
#ifdef USE_SHADOWMAPS
    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        /* TODO: Single-sample shadow targets need multisample depth coverage before alpha-key shadows can use ATOC. */
        R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
        R_Call(glEnable, GL_BLEND);
        R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        R_Call(glDepthMask, GL_FALSE);
        return;
    }
#endif
    if (tr.msaa_samples) {
        R_Call(glDisable, GL_BLEND);
        R_Call(glEnable, GL_SAMPLE_ALPHA_TO_COVERAGE);
        R_Call(glDepthMask, GL_TRUE);
        R_Call(glBlendFunc, GL_ONE, GL_ZERO);
    } else {
        R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
        R_Call(glEnable, GL_BLEND);
        R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        R_Call(glDepthMask, GL_FALSE);
    }
#else
    R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
    R_Call(glDisable, GL_BLEND);
    R_Call(glBlendFunc, GL_ONE, GL_ZERO);
    R_Call(glDepthMask, GL_TRUE);
#endif
}

void R_ShutdownRenderer(void) {
    if (renderer_shutdown) {
        return;
    }
    renderer_shutdown = true;
    R_ShutdownModels();
    R_Shutdown();
    R_ShutdownModelShader();
    R_ShutdownBuiltinShaders();
    R_ShutdownFonts();
    
    R_ShutdownFogOfWar();
    R_ShutdownParticles();
    SAFE_DELETE(tr.minimap, R_ReleaseTexture);
    R_ShutdownTextureCache();
    R_ShutdownDrawBufferInstanced();
    
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

void R_DrawSky(void) {
    renderEntity_t sky;
    viewCamera_t const *a = tr.viewDef.camerastate + 1;
    viewCamera_t const *b = tr.viewDef.camerastate + 0;
    DWORD rdflags;

    if (!tr.viewDef.skyModel) return;
    sky = (renderEntity_t){
        .origin = Vector3_lerp(&a->origin, &b->origin, tr.viewDef.lerpfrac),
        .model = tr.viewDef.skyModel,
        .flags = RF_NO_LIGHTING | RF_NO_FOGOFWAR | RF_NO_SHADOW,
        .scale = 1.0f,
    };
    rdflags = tr.viewDef.rdflags;
    tr.viewDef.rdflags |= RDF_NOFRUSTUMCULL;
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glDisable, GL_DEPTH_TEST);
    R_RenderModel(&sky);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    tr.viewDef.rdflags = rdflags;
}

#ifdef USE_SHADOWMAPS
void R_RenderShadowMap(void) {
    tr.render_phase = RENDER_PHASE_LIGHTS;
    R_SetupGL(true);
    /* Bias depth writes away from receivers; the old unbiased pass made each terrain triangle shadow itself. */
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, 2.0f, 4.0f);
    R_BindTexture(tr.texture[TEX_SHADOWMAP], 1);
    R_DrawWorld();
    R_DrawTerrainShadows();
    R_DrawEntities();
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, 0.0f, 0.0f);
}
#endif

void R_RenderView(void) {
    tr.render_phase = RENDER_PHASE_SOLID;
    R_SetupViewport(&tr.viewDef.viewport);
    R_SetupScissor(&tr.viewDef.scissor);
    R_SetupGL(false);
    if (tr.viewDef.rdflags & RDF_NOWORLDMODEL) {
        R_Call(glClear, GL_DEPTH_BUFFER_BIT);
    }
    R_DrawSky();
    R_DrawWorld();
    R_DrawDecals();
    R_DrawEntities();
    tr.render_phase = RENDER_PHASE_ALPHA;
    R_DrawAlphaSurfaces();
    if (!(tr.viewDef.rdflags & RDF_NOPARTICLES)) {
        R_DrawParticles();
    }
    tr.render_phase = RENDER_PHASE_SOLID;
    R_RevertSettings();
    R_SetupScissor(&(RECT){0, 0, 1, 1});

//    extern LPCTEXTURE dds;
//    R_DrawPic(dds, 0, 0);
}

void R_RenderFrame(viewDef_t const *viewDef) {
    tr.viewDef = *viewDef;

    /* UI scene and portrait callers zero-initialise their viewDef, leaving
     * time == 0, which would freeze model animations (MDLX_SetEntityAnimationFrame
     * uses tr.viewDef.time to compute the current frame).  Fall back to the
     * wall clock so the menu background and portraits animate. */
    if (tr.viewDef.time == 0) {
        tr.viewDef.time = SDL_GetTicks();
    }
    R_SetupEnvironmentLighting();

    if (!tr.viewDef.scissor.w && !tr.viewDef.scissor.h) {
        tr.viewDef.scissor = (RECT){0, 0, 1, 1};
    }

    if ((tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) && tr.viewDef.num_entities > 0) {
        renderEntity_t const *entity = &tr.viewDef.entities[0];
        float aspect = (tr.viewDef.viewport.w * tr.drawableSize.width) > 0.0f
            ? (tr.viewDef.viewport.w * tr.drawableSize.width) / (tr.viewDef.viewport.h * tr.drawableSize.height)
            : 1.0f;
        if (!R_ExtractEntityCamera(entity, aspect, &tr.viewDef)) {
            Matrix4_identity(&tr.viewDef.viewProjectionMatrix);
            Matrix4_identity(&tr.viewDef.textureMatrix);
            Matrix4_identity(&tr.viewDef.lightMatrix);
        }
        Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);
        R_SetupViewport(&tr.viewDef.viewport);
        R_SetupScissor(&tr.viewDef.scissor);
        R_SetupGL(false);
        R_Call(glClear, GL_DEPTH_BUFFER_BIT);
        R_DrawEntities();
        R_RevertSettings();
        return;
    }

    Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);

    R_RenderFogOfWar();
    R_Call(glActiveTexture, GL_TEXTURE2);
    R_Call(glBindTexture, GL_TEXTURE_2D, R_GetFogOfWarTexture());
    R_Call(glActiveTexture, GL_TEXTURE0);
#ifdef USE_SHADOWMAPS
    /* Layout-provided model cameras have no world shadow pass either. */
    if (!(tr.viewDef.rdflags & (RDF_USE_ENTITY_CAMERA | RDF_NOWORLDMODEL))) {
        R_RenderShadowMap();
    }
#endif
    R_RenderView();
}

void R_DrawBuffer(LPCBUFFER buffer, DWORD num_vertices) {
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_StatsDraw(GL_TRIANGLES, num_vertices, 1);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
}

/* Model-owned element buffers retain per-section ranges as byte offsets. */
void R_DrawIndexedBuffer16(LPCBUFFER buffer, LPCDRAWELEMENTS draw) {
    R_Call(glBindVertexArray, buffer->vao);
    R_StatsDraw(GL_TRIANGLES, draw->count, 1);
    R_Call(glDrawElements, GL_TRIANGLES, draw->count, GL_UNSIGNED_SHORT, (void *)(uintptr_t)draw->offset);
}

void R_DrawIndexedBuffer32(LPCBUFFER buffer, LPCDRAWELEMENTS draw) {
    R_Call(glBindVertexArray, buffer->vao);
    R_StatsDraw(GL_TRIANGLES, draw->count, 1);
    R_Call(glDrawElements, GL_TRIANGLES, draw->count, GL_UNSIGNED_INT, (void *)(uintptr_t)draw->offset);
}

/* Static procedural batches need only gl_InstanceID; their shared VAO has no per-instance stream. */
void R_DrawBufferCopies(LPCBUFFER buffer, DWORD num_vertices, DWORD num_instances) {
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_StatsDraw(GL_TRIANGLES, num_vertices, num_instances);
    R_Call(glDrawArraysInstanced, GL_TRIANGLES, 0, num_vertices, num_instances);
}

void R_DrawIndexedBuffer(LPCBUFFER buffer, DWORD num_indices) {
    R_Call(glBindVertexArray, buffer->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buffer->ibo);
    R_StatsDraw(GL_TRIANGLES, num_indices, 1);
    R_Call(glDrawElements, GL_TRIANGLES, num_indices, GL_UNSIGNED_INT, NULL);
}

typedef struct {
    uint64_t draws, vertices, triangles, instances;
} RENDERSTATS;

static RENDERSTATS r_frame_stats, r_stats_accum;
static DWORD r_stats_frames, r_stats_start;

DWORD R_GetFrameDrawCalls(void) { return (DWORD)r_frame_stats.draws; }

/* Count submitted work at the renderer boundary, including instanced amplification. */
void R_StatsDraw(GLenum mode, DWORD count, DWORD instances) {
    r_frame_stats.draws++;
    r_frame_stats.vertices += (uint64_t)count * instances;
    r_frame_stats.triangles += R_PrimitiveTriangles(mode, count, instances);
    r_frame_stats.instances += instances;
}

/* Emit one averaged line per second so profiling logs remain readable. */
static void R_FinishFrameStats(void) {
    DWORD now = SDL_GetTicks(), elapsed;

    r_stats_accum.draws += r_frame_stats.draws; r_stats_accum.vertices += r_frame_stats.vertices;
    r_stats_accum.triangles += r_frame_stats.triangles; r_stats_accum.instances += r_frame_stats.instances;
    r_stats_frames++;
    if (!r_stats_start) r_stats_start = now;
    elapsed = now - r_stats_start;
    if (elapsed < 1000) return;
    if (R_CvarEnabled("r_stats", "0")) {
        fprintf(stderr, "[R_STATS] fps=%.1f draws=%.1f vertices=%.0f triangles=%.0f instances=%.0f\n",
                r_stats_frames * 1000.0 / elapsed, (double)r_stats_accum.draws / r_stats_frames,
                (double)r_stats_accum.vertices / r_stats_frames, (double)r_stats_accum.triangles / r_stats_frames,
                (double)r_stats_accum.instances / r_stats_frames);
    }
    memset(&r_stats_accum, 0, sizeof(r_stats_accum)); r_stats_frames = 0; r_stats_start = now;
}

void R_BeginFrame(void) {
    memset(&r_frame_stats, 0, sizeof(r_frame_stats));
    R_Call(glDisable, GL_SAMPLE_ALPHA_TO_COVERAGE);
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
    R_FinishFrameStats();
    R_UpdateSwapInterval();
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

void R_SetWindowSize(DWORD width, DWORD height) {
    if (!window || width == 0 || height == 0) {
        return;
    }
    SDL_SetWindowFullscreen(window, 0);
    SDL_SetWindowSize(window, (int)width, (int)height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GL_GetDrawableSize(window,
                           (int *)&tr.drawableSize.width,
                           (int *)&tr.drawableSize.height);
    R_Call(glViewport, 0, 0, tr.drawableSize.width, tr.drawableSize.height);
    fprintf(stderr,
            "Video mode applied: %ux%u (drawable %ux%u)\n",
            (unsigned)width,
            (unsigned)height,
            (unsigned)tr.drawableSize.width,
            (unsigned)tr.drawableSize.height);
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



/* Keep model-format bounds inside the renderer while clients place game-owned world UI. */
bool R_GetEntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out) {
    return R_EntityOverheadPosition(entity, out);
}

/* Keep attachment-name/model-format knowledge in the selected game renderer.
 * Shared client presentation can request an authored attachment by prefix. */
bool R_GetEntityAttachmentPosition(renderEntity_t const *entity, LPCSTR prefix, LPVECTOR3 out) {
    return R_EntityAttachmentPosition(entity, prefix, out);
}

/* Cursor presentation is game-owned; the client only supplies UI coordinates. */


refExport_t R_GetAPI(refImport_t imp) {
    ri = imp;
    return (refExport_t) {
        .Init = R_InitRenderer,
        .RegisterMap = R_RegisterMapAssets,
        .LoadTexture = R_LoadTexture,
        .LoadModel = R_LoadRegisteredModel,
        .LoadFont = R_LoadFont,
        .SetFogOfWarData = R_SetFogOfWarData,
        .ReleaseTexture = R_ReleaseTexture,
        .ReleaseModel = R_ReleaseRegisteredModel,
        .RenderFrame = R_RenderFrame,
        .Shutdown = R_ShutdownRenderer,
        .BeginFrame = R_BeginFrame,
        .EndFrame = R_EndFrame,
        .Screenshot = R_Screenshot,
        .DrawPic = R_DrawPic,
        .DrawImage = R_DrawImage,
        .DrawImageEx = R_DrawImageEx,
        .DrawBackdrop = R_DrawBackdrop,
        .DrawMinimap = R_DrawMinimapScene,
        .DrawLoadingIndicator = R_DrawLoadingIndicator,
        .DrawSelectionRect = R_DrawSelectionRect,
        .DrawChar = R_DrawChar,
        .DrawString = R_DrawString,
        .DrawFill = R_DrawFill,
        .GetWindowSize = R_GetWindowSize,
        .GetDrawCalls = R_GetFrameDrawCalls,
        .SetWindowSize = R_SetWindowSize,
        .GetTextureSize = R_GetTextureSize,
        .DrawSprite = R_DrawSprite,
        .DrawCursor = R_DrawCursor,
        .SetEntityAnimFrame = R_SetEntityAnimFrame,
        .DrawText = R_DrawText,
        .GetTextSize = R_GetTextSize,
        .GetModelInfo = R_GetModelInfo,
        .GetEntityOverheadPosition = R_GetEntityOverheadPosition,
        .GetEntityAttachmentPosition = R_GetEntityAttachmentPosition,
        .DrawBoundingBox = R_DrawBoundingBox,
        .GetHeightAtPoint = R_GetHeightAtPoint,
        .TraceEntity = R_TraceEntity,
        .TraceLocation = R_TraceLocation,
        .TraceCameraPlane = R_TraceCameraPlane,
        .TraceMinimap = R_TraceMinimap,
        .WorldToMinimap = R_WorldToMinimap,
        .EntitiesInRect = R_EntitiesInRect,
    };
}
