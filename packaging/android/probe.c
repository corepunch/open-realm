#include "renderer/r_shader.h"
#include <SDL2/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#include <math.h>

static modelProg_t model;
static GLuint vao, vbo, texture;
static const vec3_t triangle[] = { { -0.22f, -0.65f, 0 }, { 0.22f, -0.65f, 0 }, { 0, -0.05f, 0 } };
static struct { shader_desc_t const *desc; size_t size; } cases[] = {
    { &sd_unlit, sizeof(spriteState_t) }, { &sd_minimap, sizeof(spriteState_t) },
    { &sd_splat, sizeof(spriteState_t) }, { &sd_shadow_splat, sizeof(spriteState_t) },
    { &sd_commandbutton, sizeof(spriteState_t) }, { &sd_minimap_fog, sizeof(spriteState_t) },
    { &sd_default, sizeof(defaultState_t) },
};

/* SDL owns the Activity; only the diagnostic subclass knows about the text overlay. */
static void show_report(char const *text) {
    __android_log_print(ANDROID_LOG_INFO, "OpenRealmProbe", "%s", text);
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID method = (*env)->GetMethodID(env, cls, "showReport", "(Ljava/lang/String;)V");
    if (method) {
        jstring message = (*env)->NewStringUTF(env, text);
        if (message) (*env)->CallVoidMethod(env, activity, method, message);
        if (message) (*env)->DeleteLocalRef(env, message);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        __android_log_print(ANDROID_LOG_ERROR, "OpenRealmProbe", "Diagnostic overlay update failed");
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}

/* Compile the real descriptors and generator; never substitute a reduced bone palette. */
static int check_shaders(void) {
    int passed = 0;
    gs_options_t options = { .dialect = GLSL_DIALECT_ES3, .defines = "#define USE_FOGOFWAR 1\n" };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        shaderProg_t prog = {0};
        void *state = calloc(1, cases[i].size);
        bool ok = state && gs_load(&prog, cases[i].desc, state, cases[i].size, &options);
        __android_log_print(ANDROID_LOG_INFO, "OpenRealmProbe", "SHADER %s %s", cases[i].desc->Name, ok ? "PASS" : "FAIL");
        passed += ok;
        gs_delete(&prog);
        free(state);
    }
    for (int variant = 0; variant < 8; variant++) {
        char defines[160];
        snprintf(defines, sizeof(defines), "#define USE_FOGOFWAR 1\n%s%s%s",
            variant & 1 ? "#define BZ_USE_INSTANCING 1\n" : "",
            variant & 2 ? "#define USE_SHADOWMAPS 1\n" : "",
            variant & 4 ? "#define BZ_USE_MSAA 1\n" : "");
        options.defines = defines;
        modelProg_t next = {0};
        bool ok = gs_load(&next.prog, &sd_model, &next.state, sizeof(next.state), &options);
        __android_log_print(ANDROID_LOG_INFO, "OpenRealmProbe", "SHADER model variant=%d bones=%d %s",
            variant, BZ_BONE_PALETTE_MAX, ok ? "PASS" : "FAIL");
        passed += ok;
        if (ok && !variant) model = next;
        else gs_delete(&next.prog);
    }
    return passed;
}

/* A synthetic fixture exercises the final palette entry without needing retail MPQs. */
static void setup_triangle(void) {
    for (int i = 0; i < BZ_BONE_PALETTE_MAX; i++) Matrix4_identity(&model.state.bones[i]);
    model.state.boneCount = BZ_BONE_PALETTE_MAX;
    Matrix4_identity(&model.state.viewProjection);
    Matrix4_identity(&model.state.model);
    Matrix4_identity(&model.state.textureMatrix);
    Matrix4_identity(&model.state.lightMatrix);
    model.state.normalMatrix = (mat3_t){ .v = { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
    model.state.uvMatrix = model.state.normalMatrix;
    model.state.layerAlpha = 1;
    model.state.geosetColor = (vec4_t){ 0.2f, 0.72f, 0.92f, 1 };
    model.state.unshaded = true;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glVertexAttribPointer(attrib_position, 3, GL_FLOAT, GL_FALSE, sizeof(vec3_t), NULL);
    glEnableVertexAttribArray(attrib_position);
    glVertexAttrib4f(attrib_color, 1, 1, 1, 1);
    glVertexAttrib3f(attrib_normal, 0, 0, 1);
    glVertexAttrib4f(attrib_skin1, BZ_BONE_PALETTE_MAX - 1, 0, 0, 0);
    glVertexAttrib4f(attrib_boneWeight1, 1, 0, 0, 0);
    glGenTextures(1, &texture);
    unsigned char white[] = {255, 255, 255, 255};
    for (int unit = 0; unit < 3; unit++) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glClearColor(0.035f, 0.055f, 0.09f, 1);
}

static bool draw_triangle(float offset) {
    Matrix4_identity(&model.state.bones[BZ_BONE_PALETTE_MAX - 1]);
    Matrix4_translate(&model.state.bones[BZ_BONE_PALETTE_MAX - 1], &(vec3_t){ offset, 0, 0 });
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gs_apply(&model.prog, &model.state)) return false;
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return true;
}

/* Read both old/new locations: a successful compile alone does not prove skinning or drawing. */
static bool check_pixels(int width, int height) {
    unsigned char left[4], right[4];
    bool ok = true;
    for (int step = 0; step < 2; step++) {
        if (!draw_triangle(step ? 0.4f : -0.4f)) return false;
        glReadPixels(width * 3 / 10, height * 13 / 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
        glReadPixels(width * 7 / 10, height * 13 / 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
        unsigned char const *lit = step ? right : left, *dark = step ? left : right;
        ok = ok && abs(lit[0] - 51) <= 3 && abs(lit[1] - 184) <= 3 && abs(lit[2] - 235) <= 3;
        ok = ok && dark[0] < 20 && dark[1] < 30 && dark[2] < 40 && glGetError() == GL_NO_ERROR;
        __android_log_print(ANDROID_LOG_INFO, "OpenRealmProbe", "PIXELS step=%d left=%u,%u,%u right=%u,%u,%u",
            step, left[0], left[1], left[2], right[0], right[1], right[2]);
    }
    return ok;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        show_report(SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    /* Keep Android navigation visible in this diagnostic app; immersive mode adds first-run system chrome. */
    SDL_Window *window = SDL_CreateWindow("OpenRealm GLES Probe", 0, 0, 1280, 800, SDL_WINDOW_OPENGL);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    if (!context) {
        show_report(SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    int width, height, vectors, components, depth;
    SDL_GL_GetDrawableSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vectors);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &components);
    glGetIntegerv(GL_DEPTH_BITS, &depth);
    int passed = check_shaders();
    bool pixels = false;
    char name[32];
    snprintf(name, sizeof(name), "u_bones[%d]", BZ_BONE_PALETTE_MAX - 1);
    GLint bone = model.prog.progid ? glGetUniformLocation(model.prog.progid, name) : -1;
    if (model.prog.progid) {
        setup_triangle();
        pixels = check_pixels(width, height);
    }
    bool ok = passed == 15 && pixels && bone >= 0 && depth >= 24;
    char report[2048];
    snprintf(report, sizeof(report), "OpenRealm Android / SDL2 / GLES3\n%s\n%s\n"
        "Shaders: %d/15 | bones: %d | last bone location: %d\n"
        "Vertex uniforms: %d vectors / %d components | depth: %d bits\n"
        "Bone 127 movement + pixel readback: %s\nRESULT %s",
        glGetString(GL_VERSION), glGetString(GL_RENDERER), passed, BZ_BONE_PALETTE_MAX, bone,
        vectors, components, depth, pixels ? "PASS" : "FAIL", ok ? "PASS" : "FAIL");
    show_report(report);
    bool running = true, foreground = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_AC_BACK)) running = false;
            if (event.type == SDL_APP_WILLENTERBACKGROUND) foreground = false;
            if (event.type == SDL_APP_DIDENTERFOREGROUND) foreground = true;
        }
        if (foreground) {
            SDL_GL_GetDrawableSize(window, &width, &height);
            glViewport(0, 0, width, height);
            if (model.prog.progid && !draw_triangle(sinf(SDL_GetTicks() * 0.001f) * 0.4f)) running = false;
            SDL_GL_SwapWindow(window);
        }
        SDL_Delay(16);
    }
    gs_delete(&model.prog);
    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}
