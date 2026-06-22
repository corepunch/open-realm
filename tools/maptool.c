#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/w3m/r_war3map.h"
#include "tools/viewer_common.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATHSTR
#define PATHSTR char[512]
#endif

static HANDLE archives[64] = { 0 };
static const char *g_map_path = NULL;
static struct {
    VECTOR2 position;
    VECTOR3 viewangles;
    float target_distance;
    float fov;
    float z_offset;
    bool dragging;
} camera = {
    .viewangles = { 326, 0, 0 },
    .target_distance = 1650.0f,
    .fov = 50.0f,
};

static void Tool_DrawString(refExport_t const *re, LPCSTR string, int x, int y) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re->DrawChar(x + i * 8, y, (BYTE)string[i]);
    }
}

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  maptool -mpq <archive.mpq> -map <file.w3m>\n"
        "\n"
        "Examples:\n"
        "  maptool -mpq War3.mpq -map Maps\\\\Campaign\\\\Human02.w3m\n");
}

static void errorf(LPCSTR fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

HANDLE FS_OpenFile(LPCSTR fileName) {
    return Viewer_OpenFile(archives, sizeof(archives) / sizeof(archives[0]), fileName);
}

void FS_CloseFile(HANDLE file) {
    Viewer_CloseFile(file);
}

bool FS_ExtractFile(LPCSTR toExtract, LPCSTR extracted) {
    return Viewer_ExtractFile(archives, sizeof(archives) / sizeof(archives[0]), toExtract, extracted);
}

bool FS_FileExists(LPCSTR fileName) {
    return Viewer_FileExists(archives, sizeof(archives) / sizeof(archives[0]), fileName);
}

HANDLE MemAlloc(long size) { return Viewer_MemAlloc(size); }
void MemFree(HANDLE mem) { Viewer_MemFree(mem); }

void Sys_Quit(void) { exit(0); }

static void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, FLOAT distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotate(output, angles, ROTATE_ZYX);
    Matrix4_translate(output, &vieworg);
}

static void Matrix4_fromViewQuat(LPCVECTOR3 target, LPCQUATERNION quat, FLOAT distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotateQuat(output, quat);
    Matrix4_translate(output, &vieworg);
}

static void Matrix4_getWorldShadowMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
    MATRIX4 proj, view;
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, 100.0, 3500.0);
    Matrix4_fromViewAngles(target, sunangles, 1000, &view);
    Matrix4_multiply(&proj, &view, output);
}

static void MapTool_Pan(float xrel, float yrel) {
    float const scale = camera.target_distance * 0.0025f;
    camera.position.x -= xrel * scale;
    camera.position.y += yrel * scale;
}

static void BuildMapCamera(refExport_t const *re, viewDef_t *viewdef) {
    if (!tr.world) {
        Matrix4_identity(&viewdef->viewProjectionMatrix);
        Matrix4_identity(&viewdef->lightMatrix);
        return;
    }
    VECTOR2 size = GetWar3MapSize(tr.world);
    VECTOR3 target = {
        camera.position.x,
        camera.position.y,
        re->GetHeightAtPoint(camera.position.x, camera.position.y) - 128.0f + camera.z_offset,
    };
    float radius = MAX(size.x, size.y) * 0.65f + 600.0f;
    size2_t window = R_GetWindowSize();
    float aspect = window.height ? (float)window.width / (float)window.height : 1.0f;

    MATRIX4 proj, view;
    QUATERNION quat = Quaternion_fromEuler(&camera.viewangles, ROTATE_ZYX);
    Matrix4_perspective(&proj, camera.fov, aspect, 100.0f, 5000.0f);
    Matrix4_fromViewQuat(&target, &quat, camera.target_distance, &view);
    Matrix4_multiply(&proj, &view, &viewdef->viewProjectionMatrix);
    Matrix4_getWorldShadowMatrix(&(VECTOR3){ -40, 0, 60 }, &target, radius, &viewdef->lightMatrix);
}

int main(int argc, char **argv) {
    const char *mpq = NULL;
    const char *mapPath = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "-mpq=", 5)) {
            mpq = argv[i] + 5;
        } else if (!strcmp(argv[i], "-mpq")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            mpq = argv[i];
        } else if (!strncmp(argv[i], "-map=", 5)) {
            mapPath = argv[i] + 5;
        } else if (!strcmp(argv[i], "-map")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            mapPath = argv[i];
        } else if (argv[i][0] != '-' && !mapPath) {
            mapPath = argv[i];
        } else {
            usage();
            return 1;
        }
    }

    if (!mpq || !mapPath) {
        usage();
        return 1;
    }

    if (!Viewer_AddArchive(archives, sizeof(archives) / sizeof(archives[0]), mpq)) {
        return 1;
    }
    Tool_SetSheetHost(archives, sizeof(archives) / sizeof(archives[0]));

    refExport_t re = R_GetAPI((refImport_t){
        .FS_ReadFile = Tool_FS_ReadFile,
        .FS_FreeFile = Tool_FS_FreeFile,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .ReadSheet = FS_ParseSLK,
        .FindSheetCell = FS_FindSheetCell,
        .error = errorf,
    });

    fprintf(stderr, "maptool: renderer init\n");
    re.Init(VIEWER_WINDOW_WIDTH, VIEWER_WINDOW_HEIGHT);
    fprintf(stderr, "maptool: renderer ready\n");

    fprintf(stderr, "maptool: registering map %s\n", mapPath);
    re.RegisterMap(mapPath);
    if (!tr.world) {
        fprintf(stderr, "Failed to register map: %s\n", mapPath);
        re.Shutdown();
        Viewer_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));
        return 1;
    }

    VECTOR2 mapSize = GetWar3MapSize(tr.world);
    camera.position = (VECTOR2){
        tr.world->center.x + mapSize.x * 0.5f,
        tr.world->center.y + mapSize.y * 0.5f,
    };
    camera.target_distance = 1650.0f;
    camera.fov = 50.0f;
    camera.z_offset = 0.0f;
    g_map_path = mapPath;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
                camera.dragging = true;
            } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
                camera.dragging = false;
            } else if (event.type == SDL_MOUSEMOTION && camera.dragging) {
                MapTool_Pan((float)event.motion.xrel, (float)event.motion.yrel);
            } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y != 0) {
                float const factor = powf(0.85f, (float)(-event.wheel.y));
                camera.target_distance = MAX(400.0f, MIN(4000.0f, camera.target_distance * factor));
            }
        }

        viewDef_t viewdef = { 0 };
        BuildMapCamera(&re, &viewdef);
        viewdef.viewport = (RECT){ 0, 0, 1, 1 };
        viewdef.scissor = (RECT){ 0, 0, 1, 1 };
        viewdef.time = SDL_GetTicks();
        viewdef.deltaTime = 16;
        viewdef.lerpfrac = 0.0f;
        viewdef.rdflags = 0;
        Matrix4_identity(&viewdef.textureMatrix);

        re.BeginFrame();
        re.RenderFrame(&viewdef);
        Tool_DrawString(&re, "maptool: orbit camera", 10, 10);
        Tool_DrawString(&re, g_map_path, 10, 28);
        re.EndFrame();
    }

    re.Shutdown();
    Viewer_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));
    return 0;
}
