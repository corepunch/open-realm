#include "games/warcraft-3/renderer/mdx/r_mdx.h"
#include "tools/viewer_common.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "renderer/stb/stb_image_write.h"

#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATHSTR
#define PATHSTR char[512]
#endif

static const char *g_model_path = NULL;
static const char *g_requested_animation = NULL;
static bool g_use_model_camera = false;
static bool g_use_front_ortho = false;
static bool g_info_only = false;
static bool g_dump_all = false;
static bool g_run_once = false;
/* Deterministic golden-image output: when g_output_path is set, mdxtool renders
 * one clean frame (no overlay/bbox), with a fixed logical time and seeded RNG so
 * the PNG is byte-stable across runs, then exits. */
static const char *g_output_path = NULL;
static long g_fixed_time = -1;   /* logical ms; -1 => wall clock (SDL_GetTicks) */
static long g_seed = -1;         /* srand seed for particle RNG; -1 => unseeded */
static HANDLE archives[64] = { 0 };
static viewer_orbit_t orbit;
static VECTOR3 g_model_center = { 0, 0, 0 };
static float g_preview_scale = 1.0f;

typedef struct {
    DWORD num_sequences;
    DWORD selected_sequence;
    DWORD num_textures;
    DWORD num_geosets;
    DWORD num_lights;
    DWORD num_emitters;
    DWORD num_attachments;
    DWORD num_helpers;
    DWORD num_bones;
    DWORD num_collision_shapes;
    DWORD num_cameras;
    DWORD num_pivots;
    float bounds_w;
    float bounds_d;
    float bounds_h;
    float orbit_distance;
    char selected_sequence_name[81];
} model_overlay_info_t;

static model_overlay_info_t g_overlay = { 0 };

#define TEXTURE_PREVIEW_PATH_LENGTH 512

static const float TEX_PANEL_X = 0.53f;
static const float TEX_PANEL_Y = 0.02f;
static const float TEX_CELL_W = 0.062f;
static const float TEX_CELL_H = 0.080f;
static const float TEX_THUMB_W = 0.055f;
static const float TEX_THUMB_H = 0.046f;
static const DWORD TEX_COLS = 4;

typedef struct {
    DWORD count;
    char (*paths)[TEXTURE_PREVIEW_PATH_LENGTH];
    LPCTEXTURE *textures;
    size2_t *sizes;
} texture_preview_cache_t;

static texture_preview_cache_t texture_previews = { 0 };

static BOX3 GetPreviewBounds(mdxModel_t const *mdx);

static void Tool_DrawString(refExport_t const *re, LPCSTR string, int x, int y) {
    if (!string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re->DrawChar(x + i * 8, y, (BYTE)string[i]);
    }
}

static void FreeTexturePreviewCache(void) {
    if (texture_previews.paths) {
        free(texture_previews.paths);
        texture_previews.paths = NULL;
    }
    if (texture_previews.textures) {
        free(texture_previews.textures);
        texture_previews.textures = NULL;
    }
    if (texture_previews.sizes) {
        free(texture_previews.sizes);
        texture_previews.sizes = NULL;
    }
    texture_previews.count = 0;
}

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
    "  mdxtool -mpq <archive.mpq> -model <file.mdx> [--anim <sequence>] [--use-model-camera] [--front-ortho] [--info] [--dump-all] [--once] [-o <out.png> [--frame <ms>] [--seed <n>]]\n"
        "\n"
        "Examples:\n"
    "  mdxtool -mpq War3x.mpq -model UI\\\\Glues\\\\MainMenu\\\\MainMenu3D_exp\\\\MainMenu3D_exp.mdx --use-model-camera -o out.png\n"
        "  mdxtool -mpq War3.mpq -model UI\\\\Glues\\\\MainMenu\\\\MainMenu3d\\\\MainMenu3d.mdx\n"
    "  mdxtool -mpq War3.mpq -model units\\\\orc\\\\Peon\\\\Peon.mdx --use-model-camera\n"
    "  mdxtool -mpq War3.mpq -model UI\\\\Glues\\\\SpriteLayers\\\\TopRightPanel.mdx --front-ortho\n"
    "  mdxtool -mpq War3.mpq -model UI\\\\Glues\\\\MainMenu\\\\WarCraftIIILogo\\\\WarCraftIIILogo.mdx --anim \"MainMenu Stand\"\n"
    "  mdxtool -mpq War3.mpq -model UI\\\\Glues\\\\MainMenu\\\\WarCraftIIILogo\\\\WarCraftIIILogo.mdx --info\n"
    "\n"
    "Notes:\n"
    "  --info prints model metadata and exits without creating a window.\n"
    "  --front-ortho uses a front-facing orthographic preview camera for flat UI models.\n"
    "  --dump-all prints loaded model details (nodes, bones, geosets, materials, cameras).\n"
    "  --once renders one frame and exits.\n"
    "  -o/--output <file.png> renders one clean frame (no overlay/bbox) to a PNG and exits.\n"
    "    Deterministic by default (--frame 1000 --seed 1234) for golden-image tests.\n"
    "  --frame <ms> sets the logical animation time (fixes the frame; default wall clock).\n"
    "  --seed <n> seeds the particle RNG for reproducible output.\n");
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

HANDLE FS_ReadFile(LPCSTR filename, LPDWORD size);

HANDLE MemAlloc(long size) { return Viewer_MemAlloc(size); }
void MemFree(HANDLE mem) { Viewer_MemFree(mem); }

void Sys_Quit(void) {
    exit(0);
}

static void Matrix4_getPreviewCameraMatrix(viewer_orbit_t const *orbit,
                                           float aspect,
                                           float near_clip,
                                           float far_clip,
                                           LPMATRIX4 output)
{
    Viewer_OrbitBuildCamera(orbit, aspect, 35.0f, near_clip, far_clip, output);
}

static void Matrix4_getFrontOrthoCameraMatrix(mdxModel_t const *mdx,
                                              float aspect,
                                              float scale,
                                              LPMATRIX4 output)
{
    // Mirror the UI scene camera: fixed UI coordinate range 0..0.8 x 0..0.6
    // (aspect and scale are unused — model local coords ARE screen coords)
    BOX3 const bounds = GetPreviewBounds(mdx);
    float const max_z = isfinite(bounds.max.z) ? bounds.max.z : 512.0f;
    float const min_z = isfinite(bounds.min.z) ? bounds.min.z : -512.0f;
    float const eye_z  = max_z + 100.0f;
    float const near_clip = 1.0f;
    float const far_clip  = eye_z - min_z + 100.0f;
    MATRIX4 proj, view;

    // Same UI ortho range: left=0, right=0.8, bottom=0, top=0.6
    Matrix4_ortho(&proj, 0.0f, 0.8f, 0.0f, 0.6f, near_clip, far_clip);
    Matrix4_lookAt(&view,
                   &(VECTOR3){ 0, 0, eye_z },
                   &(VECTOR3){ 0, 0, -1 },
                   &(VECTOR3){ 0, 1, 0 });
    Matrix4_multiply(&proj, &view, output);
}

static void Matrix4_getSideLightMatrix(LPCVECTOR3 eye, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
    MATRIX4 proj, view;
    VECTOR3 forward = Vector3_sub(target, eye);
    VECTOR3 right = Vector3_cross(&forward, &(VECTOR3){ 0, 0, 1 });
    VECTOR3 lightEye;
    VECTOR3 lightOffset;
    VECTOR3 lightDir;
    VECTOR3 lightUp = { 0, 0, 1 };

    if (Vector3_len(&right) < 0.001f) {
        right = (VECTOR3){ 1, 0, 0 };
    } else {
        Vector3_normalize(&right);
    }

    lightOffset = Vector3_mad(&right, -1000.0f, &(VECTOR3){ 0, 0, 0 });
    lightOffset.z += 300.0f;
    lightEye = Vector3_add(eye, &lightOffset);
    lightDir = Vector3_sub(target, &lightEye);

    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0, 3000.0);
    Matrix4_lookAt(&view, &lightEye, &lightDir, &lightUp);
    Matrix4_multiply(&proj, &view, output);
}

static BOX3 GetPreviewBounds(mdxModel_t const *mdx) {
    BOX3 bounds = { 0 };
    bool has_bounds = false;

    if (!mdx) {
        return bounds;
    }

    FOR_EACH_LIST(mdxGeoset_t, geoset, mdx->geosets) {
        if (geoset->vertices && geoset->num_vertices > 0) {
            FOR_LOOP(i, geoset->num_vertices) {
                VECTOR3 const *v = &geoset->vertices[i];
                if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z)) {
                    continue;
                }
                if (fabsf(v->x) > 100000.0f || fabsf(v->y) > 100000.0f || fabsf(v->z) > 100000.0f) {
                    continue;
                }
                if (!has_bounds) {
                    bounds.min = *v;
                    bounds.max = *v;
                    has_bounds = true;
                } else {
                    bounds.min.x = MIN(bounds.min.x, v->x);
                    bounds.min.y = MIN(bounds.min.y, v->y);
                    bounds.min.z = MIN(bounds.min.z, v->z);
                    bounds.max.x = MAX(bounds.max.x, v->x);
                    bounds.max.y = MAX(bounds.max.y, v->y);
                    bounds.max.z = MAX(bounds.max.z, v->z);
                }
            }
        }
    }

    if (!has_bounds) {
        FOR_EACH_LIST(mdxGeoset_t, geoset, mdx->geosets) {
            BOX3 const *box = &geoset->default_bounds.box;
            if (!has_bounds) {
                bounds = *box;
                has_bounds = true;
            } else {
                bounds.min.x = MIN(bounds.min.x, box->min.x);
                bounds.min.y = MIN(bounds.min.y, box->min.y);
                bounds.min.z = MIN(bounds.min.z, box->min.z);
                bounds.max.x = MAX(bounds.max.x, box->max.x);
                bounds.max.y = MAX(bounds.max.y, box->max.y);
                bounds.max.z = MAX(bounds.max.z, box->max.z);
            }
        }
    }

    if (!has_bounds) {
        bounds = mdx->bounds.box;
    }

    return bounds;
}

static float FitPreviewDistance(mdxModel_t const *mdx, float aspect, float fov_deg) {
    if (!mdx) {
        return 850.0f;
    }

    BOX3 const box = GetPreviewBounds(mdx);
    float const width = fabsf(box.max.x - box.min.x);
    float const depth = fabsf(box.max.y - box.min.y);
    float const height = fabsf(box.max.z - box.min.z);
    float const radius = MAX(sqrtf(width * width + depth * depth + height * height) * 0.5f, 0.001f);
    float const fill = 0.65f;
    float const vfov = fov_deg * (float)M_PI / 180.0f;
    float const hfov = 2.0f * atanf(tanf(vfov * 0.5f) * aspect);
    float const min_half_fov = MIN(vfov, hfov) * 0.5f;
    float distance = radius / (fill * sinf(min_half_fov));

    if (!isfinite(distance) || distance < 0.25f) {
        distance = 0.25f;
    }

    distance = MIN(distance, 5000.0f);

    return distance * 1.05f;
}

static DWORD CountGeosets(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxGeoset_t, geoset, mdx->geosets) {
        (void)geoset;
        count++;
    }
    return count;
}

static DWORD CountLights(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxLight_t, light, mdx->lights) {
        (void)light;
        count++;
    }
    return count;
}

static DWORD CountEmitters(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxParticleEmitter_t, emitter, mdx->emitters) {
        (void)emitter;
        count++;
    }
    return count;
}

static DWORD CountAttachments(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxAttachment_t, attachment, mdx->attachments) {
        (void)attachment;
        count++;
    }
    return count;
}

static DWORD CountHelpers(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxHelper_t, helper, mdx->helpers) {
        (void)helper;
        count++;
    }
    return count;
}

static DWORD CountBones(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxBone_t, bone, mdx->bones) {
        (void)bone;
        count++;
    }
    return count;
}

static DWORD CountCollisionShapes(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxCollisionShape_t, shape, mdx->collisionShapes) {
        (void)shape;
        count++;
    }
    return count;
}

static DWORD CountCameras(mdxModel_t const *mdx) {
    DWORD count = 0;
    FOR_EACH_LIST(mdxCamera_t, camera, mdx->cameras) {
        (void)camera;
        count++;
    }
    return count;
}

static DWORD CountVariableSizeEntries(LPBYTE data, DWORD size) {
    DWORD count = 0;
    DWORD offset = 0;
    while (offset + sizeof(DWORD) <= size) {
        DWORD entrySize = *(DWORD *)(data + offset);
        if (entrySize == 0 || offset + sizeof(DWORD) + entrySize > size) {
            break;
        }
        count++;
        offset += sizeof(DWORD) + entrySize;
    }
    return count;
}

static DWORD CountInclusiveSizeEntries(LPBYTE data, DWORD size) {
    DWORD count = 0;
    DWORD offset = 0;
    while (offset + sizeof(DWORD) <= size) {
        DWORD entrySize = *(DWORD *)(data + offset);
        if (entrySize < sizeof(DWORD) || offset + entrySize > size) {
            break;
        }
        count++;
        offset += entrySize;
    }
    return count;
}

static void CopySequenceName(mdxSequence_t const *seq, char *out, size_t outSize) {
    size_t i;

    if (!out || outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (!seq) {
        return;
    }

    for (i = 0; i + 1 < outSize && i < sizeof(seq->name); i++) {
        char c = seq->name[i];
        if (!c) {
            break;
        }
        out[i] = isprint((unsigned char)c) ? c : '?';
    }
    out[i] = '\0';
}

static bool StringEqualsNoCase(LPCSTR a, LPCSTR b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool StringContainsNoCase(LPCSTR haystack, LPCSTR needle) {
    size_t nlen;

    if (!haystack || !needle) {
        return false;
    }
    nlen = strlen(needle);
    if (nlen == 0) {
        return true;
    }

    for (; *haystack; haystack++) {
        size_t i;
        for (i = 0; i < nlen; i++) {
            if (!haystack[i]) {
                return false;
            }
            if (tolower((unsigned char)haystack[i]) != tolower((unsigned char)needle[i])) {
                break;
            }
        }
        if (i == nlen) {
            return true;
        }
    }

    return false;
}

static int FindSequenceIndex(mdxModel_t const *mdx, mdxSequence_t const *seq) {
    if (!mdx || !seq || !mdx->sequences || mdx->num_sequences <= 0) {
        return -1;
    }
    FOR_LOOP(i, mdx->num_sequences) {
        if (mdx->sequences + i == seq) {
            return (int)i;
        }
    }
    return -1;
}

static mdxSequence_t const *FindSequenceByNameExact(mdxModel_t const *mdx, LPCSTR wanted) {
    char seqName[81];

    if (!mdx || !mdx->sequences || mdx->num_sequences <= 0 || !wanted) {
        return NULL;
    }
    FOR_LOOP(i, mdx->num_sequences) {
        mdxSequence_t const *seq = mdx->sequences + i;
        CopySequenceName(seq, seqName, sizeof(seqName));
        if (StringEqualsNoCase(seqName, wanted)) {
            return seq;
        }
    }
    return NULL;
}

static mdxSequence_t const *FindSequenceByNameContains(mdxModel_t const *mdx, LPCSTR wanted) {
    char seqName[81];

    if (!mdx || !mdx->sequences || mdx->num_sequences <= 0 || !wanted) {
        return NULL;
    }
    FOR_LOOP(i, mdx->num_sequences) {
        mdxSequence_t const *seq = mdx->sequences + i;
        CopySequenceName(seq, seqName, sizeof(seqName));
        if (StringContainsNoCase(seqName, wanted)) {
            return seq;
        }
    }
    return NULL;
}

static mdxSequence_t const *FindSequenceByRequestedName(mdxModel_t const *mdx, LPCSTR wanted) {
    mdxSequence_t const *seq;

    if (!mdx || !wanted || !*wanted) {
        return NULL;
    }

    seq = FindSequenceByNameExact(mdx, wanted);
    if (seq) {
        return seq;
    }

    return FindSequenceByNameContains(mdx, wanted);
}

static bool DumpModelInfoNoWindow(LPCSTR modelPath) {
    HANDLE file = FS_OpenFile(modelPath);
    if (!file) {
        fprintf(stderr, "mdxtool: failed to open model %s\n", modelPath);
        return false;
    }

    DWORD fileSize = SFileGetFileSize(file, NULL);
    if (fileSize < 4) {
        FS_CloseFile(file);
        fprintf(stderr, "mdxtool: model too small %s\n", modelPath);
        return false;
    }

    LPBYTE data = MemAlloc(fileSize);
    SFileReadFile(file, data, fileSize, NULL, NULL);
    FS_CloseFile(file);

    if (*(DWORD *)data != MAKEFOURCC('M', 'D', 'L', 'X')) {
        MemFree(data);
        fprintf(stderr, "mdxtool: unsupported model header for %s\n", modelPath);
        return false;
    }

    fprintf(stderr, "mdxtool --info: model=%s size=%u bytes\n", modelPath, (unsigned)fileSize);

    DWORD offset = 4;
    while (offset + 8 <= fileSize) {
        DWORD chunkId = *(DWORD *)(data + offset + 0);
        DWORD chunkSize = *(DWORD *)(data + offset + 4);
        offset += 8;
        if (offset + chunkSize > fileSize) {
            fprintf(stderr, "mdxtool --info: invalid chunk size at offset=%u\n", (unsigned)offset);
            break;
        }

        LPBYTE chunk = data + offset;
        switch (chunkId) {
            case MAKEFOURCC('M', 'O', 'D', 'L'):
                if (chunkSize >= sizeof(mdxInfo_t)) {
                    mdxInfo_t const *info = (mdxInfo_t const *)chunk;
                    fprintf(stderr,
                            "  MODL: name=%s blendTime=%u boundsRadius=%.3f\n",
                            info->name,
                            (unsigned)info->blendTime,
                            info->bounds.radius);
                }
                break;
            case MAKEFOURCC('S', 'E', 'Q', 'S'):
            {
                DWORD seqCount = chunkSize / sizeof(mdxSequence_t);
                mdxSequence_t const *seqs = (mdxSequence_t const *)chunk;
                fprintf(stderr, "  SEQS: count=%u\n", (unsigned)seqCount);
                FOR_LOOP(i, seqCount) {
                    char seqName[81];
                    mdxSequence_t const *seq = seqs + i;
                    CopySequenceName(seq, seqName, sizeof(seqName));
                    fprintf(stderr,
                            "    [%u] name=%s interval=%u..%u flags=0x%x\n",
                            (unsigned)i,
                            seqName,
                            (unsigned)seq->interval[0],
                            (unsigned)seq->interval[1],
                            (unsigned)seq->flags);
                }
                break;
            }
            case MAKEFOURCC('T', 'E', 'X', 'S'):
                fprintf(stderr, "  TEXS: count=%u\n", (unsigned)(chunkSize / MDX_TEXTURE_RECORD_SIZE));
                break;
            case MAKEFOURCC('P', 'I', 'V', 'T'):
                fprintf(stderr, "  PIVT: count=%u\n", (unsigned)(chunkSize / sizeof(VECTOR3)));
                break;
            case MAKEFOURCC('C', 'A', 'M', 'S'):
                fprintf(stderr, "  CAMS: count=%u\n", (unsigned)CountInclusiveSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('G', 'E', 'O', 'S'):
                fprintf(stderr, "  GEOS: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('L', 'I', 'T', 'E'):
                fprintf(stderr, "  LITE: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('P', 'R', 'E', '2'):
                fprintf(stderr, "  PRE2: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('A', 'T', 'C', 'H'):
                fprintf(stderr, "  ATCH: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('H', 'E', 'L', 'P'):
                fprintf(stderr, "  HELP: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('B', 'O', 'N', 'E'):
                fprintf(stderr, "  BONE: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('C', 'L', 'I', 'D'):
                fprintf(stderr, "  CLID: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            case MAKEFOURCC('G', 'E', 'O', 'A'):
                fprintf(stderr, "  GEOA: count=%u\n", (unsigned)CountVariableSizeEntries(chunk, chunkSize));
                break;
            default:
                break;
        }

        offset += chunkSize;
    }

    MemFree(data);
    return true;
}

static void BuildTexturePreviewCache(refExport_t const *re, LPMODEL model) {
    FreeTexturePreviewCache();
    MODELINFO modelInfo = { 0 };
    DWORD textureCount = 0;
    if (re->GetModelInfo && re->GetModelInfo(model, &modelInfo)) {
        textureCount = modelInfo.textureCount;
    }
    if (textureCount == 0) {
        return;
    }

    texture_previews.paths = calloc(textureCount, sizeof(*texture_previews.paths));
    texture_previews.textures = calloc(textureCount, sizeof(*texture_previews.textures));
    texture_previews.sizes = calloc(textureCount, sizeof(*texture_previews.sizes));

    if (!texture_previews.paths || !texture_previews.textures || !texture_previews.sizes) {
        FreeTexturePreviewCache();
        return;
    }

    for (DWORD i = 0; i < textureCount; i++) {
        LPCSTR texturePath = modelInfo.texturePaths[i];
        if (!texturePath || !*texturePath) {
            continue;
        }

        strncpy(texture_previews.paths[texture_previews.count], texturePath, TEXTURE_PREVIEW_PATH_LENGTH - 1);
        texture_previews.paths[texture_previews.count][TEXTURE_PREVIEW_PATH_LENGTH - 1] = '\0';
        texture_previews.textures[texture_previews.count] = re->LoadTexture(texturePath);
        if (!texture_previews.textures[texture_previews.count]) {
            continue;
        }
        if (re->GetTextureSize) {
            texture_previews.sizes[texture_previews.count] = re->GetTextureSize(texture_previews.textures[texture_previews.count]);
        } else {
            texture_previews.sizes[texture_previews.count] = (size2_t){ 1, 1 };
        }
        texture_previews.count++;
    }
}

static void DrawTexturePreviews(refExport_t const *re) {
    if (!texture_previews.count) {
        return;
    }

    size2_t window = re->GetWindowSize();
    for (DWORD i = 0; i < texture_previews.count; i++) {
        LPCTEXTURE texture = texture_previews.textures[i];
        if (!texture) {
            continue;
        }

        DWORD col = i % TEX_COLS;
        DWORD row = i / TEX_COLS;
        float cellX = TEX_PANEL_X + (float)col * TEX_CELL_W;
        float cellY = TEX_PANEL_Y + (float)row * TEX_CELL_H;
        size2_t texSize = texture_previews.sizes[i];
        float aspect = texSize.height ? (float)texSize.width / (float)texSize.height : 1.0f;
        float drawW = TEX_THUMB_W;
        float drawH = TEX_THUMB_H;

        if (aspect > 1.0f) {
            drawH = drawW / aspect;
        } else if (aspect > 0.0f) {
            drawW = drawH * aspect;
        }

        if (drawW > TEX_THUMB_W) {
            drawW = TEX_THUMB_W;
        }
        if (drawH > TEX_THUMB_H) {
            drawH = TEX_THUMB_H;
        }

        float drawX = cellX + (TEX_THUMB_W - drawW) * 0.5f;
        float drawY = cellY + (TEX_THUMB_H - drawH) * 0.5f;
        RECT screen = { drawX, drawY, drawW, drawH };
        RECT uv = { 0, 0, 1, 1 };

        re->DrawImage(texture, &screen, &uv, COLOR32_WHITE);

        char line[512];
        snprintf(line, sizeof(line), "%s", Tool_PathBasename(texture_previews.paths[i]));
        DWORD textX = (DWORD)((drawX / 0.8f) * window.width);
        DWORD textY = (DWORD)(((drawY + drawH + 0.005f) / 0.6f) * window.height);
        Tool_DrawString(re, line, textX, textY);
    }
}

static bool BuildModelCameraMatrix(mdxModel_t const *model, float aspect, LPMATRIX4 output, LPVECTOR3 root) {
    if (!model || !model->cameras) {
        return false;
    }
    MATRIX4 proj, view;
    mdxCamera_t const *camera = model->cameras;
    VECTOR3 dir = Vector3_sub(&camera->targetPivot, &camera->pivot);
    float fov_deg = camera->fieldOfView * (180.0f / (float)M_PI);
    float near_clip = camera->nearClip;
    float far_clip = camera->farClip;

    if (!isfinite(fov_deg) || fov_deg <= 1.0f || fov_deg >= 179.0f) {
        fov_deg = 35.0f;
    }
    if (!isfinite(near_clip) || near_clip < 0.01f) {
        near_clip = 1.0f;
    }
    if (!isfinite(far_clip) || far_clip <= near_clip + 1.0f) {
        far_clip = near_clip + 5000.0f;
    }

    Matrix4_perspective(&proj, fov_deg, aspect, near_clip, far_clip);
    Matrix4_lookAt(&view, &camera->pivot, &dir, &(VECTOR3){ 0, 0, 1 });
    Matrix4_multiply(&proj, &view, output);

    if (root) {
        *root = (VECTOR3){ 0, 0, 0 };
    }
    return true;
}

static mdxSequence_t const *PickSequence(mdxModel_t const *mdx) {
    mdxSequence_t const *seq;

    if (!mdx || !mdx->sequences || mdx->num_sequences <= 0) {
        return NULL;
    }

    seq = FindSequenceByRequestedName(mdx, g_requested_animation);
    if (seq) {
        return seq;
    }

    seq = FindSequenceByNameExact(mdx, "MainMenu Stand");
    if (seq) {
        return seq;
    }
    seq = FindSequenceByNameExact(mdx, "Stand");
    if (seq) {
        return seq;
    }
    seq = FindSequenceByNameContains(mdx, "Stand");
    if (seq) {
        return seq;
    }
    seq = FindSequenceByNameExact(mdx, "MainMenu Birth");
    if (seq) {
        return seq;
    }
    seq = FindSequenceByNameExact(mdx, "Birth");
    if (seq) {
        return seq;
    }
    seq = FindSequenceByNameContains(mdx, "Birth");
    if (seq) {
        return seq;
    }

    return mdx->sequences;
}

static void DumpLoadedModel(mdxModel_t const *mdx, DWORD sample_frame) {
    int materialIndex = 0;

    if (!mdx) {
        return;
    }

    fprintf(stderr,
            "mdxtool --dump-all: name=%s version=%u sequences=%d textures=%d pivots=%d\n",
            mdx->info.name,
            (unsigned)mdx->version,
            mdx->num_sequences,
            mdx->num_textures,
            mdx->num_pivots);

    FOR_EACH_LIST(mdxBone_t, bone, mdx->bones) {
        fprintf(stderr,
                "  BONE: node_id=%u parent=%u geoset=%u geoa=%u flags=0x%x name=%s\n",
                (unsigned)bone->node.node_id,
                (unsigned)bone->node.parent_id,
                (unsigned)bone->geoset_id,
                (unsigned)bone->geoset_animation_id,
                (unsigned)bone->node.flags,
                bone->node.name);
    }

    FOR_EACH_LIST(mdxHelper_t, helper, mdx->helpers) {
        fprintf(stderr,
                "  HELP: node_id=%u parent=%u flags=0x%x name=%s\n",
                (unsigned)helper->node.node_id,
                (unsigned)helper->node.parent_id,
                (unsigned)helper->node.flags,
                helper->node.name);
    }

    FOR_EACH_LIST(mdxLight_t, light, mdx->lights) {
        VECTOR3 pivot = { 0, 0, 0 };
        if (light->node.node_id < (DWORD)mdx->num_pivots) {
            pivot = mdx->pivots[light->node.node_id];
        }
        fprintf(stderr,
                "  LITE: node_id=%u parent=%u type=%u pivot=(%.3f %.3f %.3f) atten=(%.3f %.3f) color=(%.3f %.3f %.3f) intensity=%.3f ambient=(%.3f %.3f %.3f) ambientIntensity=%.3f flags=0x%x name=%s\n",
                (unsigned)light->node.node_id,
                (unsigned)light->node.parent_id,
                (unsigned)light->type,
                pivot.x,
                pivot.y,
                pivot.z,
                light->AttenuationStart,
                light->AttenuationEnd,
                light->Color.x,
                light->Color.y,
                light->Color.z,
                light->Intensity,
                light->AmbColor.x,
                light->AmbColor.y,
                light->AmbColor.z,
                light->AmbIntensity,
                (unsigned)light->node.flags,
                light->node.name);
    }

    FOR_EACH_LIST(mdxGeoset_t, geoset, mdx->geosets) {
        fprintf(stderr,
                "  GEOS: material=%d group=%d selectable=%d vertices=%d triangles=%d matrix_groups=%d matrices=%d geoa=%p bounds=(%.3f %.3f %.3f)->(%.3f %.3f %.3f)\n",
                geoset->materialID,
                geoset->group,
                geoset->selectable,
                geoset->num_vertices,
                geoset->num_triangles,
                geoset->num_matrixGroupSizes,
                geoset->num_matrices,
                (void *)geoset->geosetAnim,
                geoset->default_bounds.box.min.x,
                geoset->default_bounds.box.min.y,
                geoset->default_bounds.box.min.z,
                geoset->default_bounds.box.max.x,
                geoset->default_bounds.box.max.y,
                geoset->default_bounds.box.max.z);

        if (geoset->matrixGroupSizes && geoset->num_matrixGroupSizes > 0) {
            fprintf(stderr, "    MTGC:");
            FOR_LOOP(i, geoset->num_matrixGroupSizes) {
                fprintf(stderr, " %d", geoset->matrixGroupSizes[i]);
            }
            fprintf(stderr, "\n");
        }
        if (geoset->matrices && geoset->num_matrices > 0) {
            fprintf(stderr, "    MATR:");
            FOR_LOOP(i, geoset->num_matrices) {
                fprintf(stderr, " %d", geoset->matrices[i]);
            }
            fprintf(stderr, "\n");
        }
        if (geoset->vertexGroups && geoset->num_vertexGroups > 0) {
            int maxGroup = 0;
            FOR_LOOP(i, geoset->num_vertexGroups) {
                maxGroup = MAX(maxGroup, (unsigned char)geoset->vertexGroups[i]);
            }
            fprintf(stderr, "    GNDX: count=%d max_group=%d\n", geoset->num_vertexGroups, maxGroup);
        }
        if (geoset->geosetAnim) {
            float alpha = geoset->geosetAnim->staticAlpha;
            if (geoset->geosetAnim->alphas) {
                MDLX_GetModelKeytrackValue(mdx, geoset->geosetAnim->alphas, sample_frame, &alpha);
            }
            fprintf(stderr,
                    "    GEOA: staticAlpha=%.3f sampledAlpha=%.3f flags=0x%x geosetId=%u alphaTrack=%p colorTrack=%p\n",
                    geoset->geosetAnim->staticAlpha,
                    alpha,
                    (unsigned)geoset->geosetAnim->flags,
                    (unsigned)geoset->geosetAnim->geosetId,
                    (void *)geoset->geosetAnim->alphas,
                    (void *)geoset->geosetAnim->colors);
        }
    }

    FOR_EACH_LIST(mdxMaterial_t, material, mdx->materials) {
        fprintf(stderr,
                "  MATS[%d]: priority=%d flags=0x%x layers=%d\n",
                materialIndex,
                material->priority,
                material->flags,
                material->num_layers);
        FOR_LOOP(layerID, material->num_layers) {
            mdxMaterialLayer_t const *layer = &material->layers[layerID];
            fprintf(stderr,
                    "    LAY[%u]: blend=%u flags=0x%x tex=%u coord=%d alpha=%.3f alphaTrack=%p flipbook=%p\n",
                    (unsigned)layerID,
                    (unsigned)layer->blendMode,
                    (unsigned)layer->flags,
                    (unsigned)layer->textureId,
                    layer->coordId,
                    layer->staticAlpha,
                    (void *)layer->alpha,
                    (void *)layer->flipbook);
        }
        materialIndex++;
    }

    FOR_EACH_LIST(mdxCamera_t, camera, mdx->cameras) {
        fprintf(stderr,
                "  CAMS: name=%s pivot=(%.3f %.3f %.3f) target=(%.3f %.3f %.3f) fov=%.4f near=%.4f far=%.4f\n",
                camera->name,
                camera->pivot.x,
                camera->pivot.y,
                camera->pivot.z,
                camera->targetPivot.x,
                camera->targetPivot.y,
                camera->targetPivot.z,
                camera->fieldOfView,
                camera->nearClip,
                camera->farClip);
    }
}

static void DumpFrameCoverage(refExport_t const *re) {
    size2_t window = re->GetWindowSize();
    DWORD width = window.width;
    DWORD height = window.height;
    BYTE *pixels;
    DWORD lit = 0;
    int min_x = (int)width;
    int min_y = (int)height;
    int max_x = -1;
    int max_y = -1;

    if (!width || !height) {
        fprintf(stderr, "mdxtool: frame coverage unavailable (empty framebuffer)\n");
        return;
    }

    pixels = malloc((size_t)width * (size_t)height * 4);
    if (!pixels) {
        fprintf(stderr, "mdxtool: frame coverage unavailable (alloc failed)\n");
        return;
    }

    R_Call(glReadPixels, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    FOR_LOOP(y, height) {
        FOR_LOOP(x, width) {
            BYTE const *pixel = pixels + ((y * width + x) * 4);
            if (pixel[0] <= 8 && pixel[1] <= 8 && pixel[2] <= 8 && pixel[3] <= 8) {
                continue;
            }
            lit++;
            min_x = MIN(min_x, (int)x);
            min_y = MIN(min_y, (int)y);
            max_x = MAX(max_x, (int)x);
            max_y = MAX(max_y, (int)y);
        }
    }

    if (lit > 0) {
        fprintf(stderr,
                "mdxtool: frame coverage lit_pixels=%u bbox=(%d,%d)-(%d,%d)\n",
                (unsigned)lit,
                min_x,
                min_y,
                max_x,
                max_y);
    } else {
        fprintf(stderr, "mdxtool: frame coverage lit_pixels=0\n");
    }

    free(pixels);
}

/* Read the rendered framebuffer back and write it as a PNG.  glReadPixels gives
 * rows bottom-up, so flip on write.  Used for deterministic golden images. */
static bool SaveFramePNG(refExport_t const *re, const char *path) {
    size2_t window = re->GetWindowSize();
    DWORD width = window.width;
    DWORD height = window.height;
    BYTE *pixels;
    int ok;

    if (!width || !height) {
        fprintf(stderr, "mdxtool: cannot save '%s' (empty framebuffer)\n", path);
        return false;
    }
    pixels = malloc((size_t)width * (size_t)height * 4);
    if (!pixels) {
        fprintf(stderr, "mdxtool: cannot save '%s' (alloc failed)\n", path);
        return false;
    }
    R_Call(glReadPixels, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_flip_vertically_on_write(1);
    ok = stbi_write_png(path, (int)width, (int)height, 4, pixels, (int)width * 4);
    free(pixels);
    if (!ok) {
        fprintf(stderr, "mdxtool: failed to write PNG '%s'\n", path);
        return false;
    }
    fprintf(stderr, "mdxtool: wrote %ux%u PNG '%s'\n", (unsigned)width, (unsigned)height, path);
    return true;
}

static void RenderModelFrame(refExport_t const *re, LPMODEL model, DWORD now, bool useModelCamera) {
    viewDef_t viewdef = { 0 };
    renderEntity_t entity = { 0 };
    mdxModel_t const *mdx = model->mdx;
    VECTOR3 target = { 0, 0, 0 };
    VECTOR3 root = { 0, 0, 0 };
    size2_t windowSize = re->GetWindowSize();
    mdxSequence_t const *seq = PickSequence(mdx);
    int seq_index = FindSequenceIndex(mdx, seq);
    float aspect = windowSize.height ? (float)windowSize.width / (float)windowSize.height : 1.0f;
    float model_extent = 1000.0f;
    float model_radius = 500.0f;
    float near_clip;
    float far_clip;
    VECTOR3 preview_origin = Vector3_scale(&g_model_center, -g_preview_scale);

    entity.model = model;
    entity.scale = g_preview_scale;
    entity.frame = 0;
    entity.oldframe = 0;
    entity.origin = (VECTOR3){ 0, 0, 0 };

    if (seq && seq->interval[1] > seq->interval[0]) {
        DWORD duration = seq->interval[1] - seq->interval[0];
        entity.frame = seq->interval[0] + now % duration;
        entity.oldframe = entity.frame;
    }

    if (mdx) {
        BOX3 const *box = &mdx->bounds.box;
        float width = fabsf(box->max.x - box->min.x);
        float depth = fabsf(box->max.y - box->min.y);
        float height = fabsf(box->max.z - box->min.z);
        model_extent = MAX(width, MAX(depth, height));
        model_radius = MAX(1.0f, model_extent * 0.5f);
    }

    model_radius *= g_preview_scale;

    near_clip = MAX(0.01f, orbit.distance * 0.01f);
    far_clip = MAX(100.0f, orbit.distance + model_radius * 8.0f);

    if (useModelCamera && BuildModelCameraMatrix(mdx, aspect, &viewdef.viewProjectionMatrix, &root)) {
        entity.origin = root;
        Matrix4_getSideLightMatrix(&model->mdx->cameras->pivot, &model->mdx->cameras->targetPivot, PORTRAIT_SHADOW_SIZE, &viewdef.lightMatrix);
    } else if (g_use_front_ortho) {
        // Use UI coordinate space: no offset, no scale.
        // The model's local vertices are already in 0..0.8 x 0..0.6 world space.
        entity.origin = (VECTOR3){ 0, 0, 0 };
        entity.scale  = 1.0f;
        target = (VECTOR3){ 0.4f, 0.3f, 0 };
        Matrix4_getFrontOrthoCameraMatrix(mdx, aspect, 1.0f, &viewdef.viewProjectionMatrix);
        Matrix4_getSideLightMatrix(&(VECTOR3){
            target.x + 8.0f,
            target.y - 12.0f,
            target.z + 16.0f,
        }, &target, PORTRAIT_SHADOW_SIZE, &viewdef.lightMatrix);
    } else {
        entity.origin = preview_origin;
        target = (VECTOR3){ 0, 0, 0 };
        orbit.target = target;
        Matrix4_getPreviewCameraMatrix(&orbit, aspect, near_clip, far_clip, &viewdef.viewProjectionMatrix);
        Matrix4_getSideLightMatrix(&(VECTOR3){
            orbit.target.x + orbit.distance * cosf(orbit.pitch_deg * (float)M_PI / 180.0f) * cosf(orbit.yaw_deg * (float)M_PI / 180.0f),
            orbit.target.y + orbit.distance * cosf(orbit.pitch_deg * (float)M_PI / 180.0f) * sinf(orbit.yaw_deg * (float)M_PI / 180.0f),
            orbit.target.z + orbit.distance * sinf(orbit.pitch_deg * (float)M_PI / 180.0f),
        }, &target, PORTRAIT_SHADOW_SIZE, &viewdef.lightMatrix);
    }

    viewdef.viewport = (RECT){ 0, 0, 1, 1 };
    viewdef.scissor = (RECT){ 0, 0, 1, 1 };
    viewdef.time = now;
    viewdef.deltaTime = 16;
    viewdef.lerpfrac = 0.0f;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL;
    Matrix4_identity(&viewdef.textureMatrix);

    re->BeginFrame();
    re->RenderFrame(&viewdef);
    /* Clean render: golden-image PNG output skips the debug bbox/overlay/text. */
    bool const clean = (g_output_path != NULL);
    if (mdx && !clean) {
        MATRIX4 entityMatrix;
        Matrix4_identity(&entityMatrix);
        Matrix4_translate(&entityMatrix, &entity.origin);
        Matrix4_scale(&entityMatrix, &(VECTOR3){ entity.scale, entity.scale, entity.scale });
        COLOR32 box_color = { 0, 255, 128, 180 };
        re->DrawBoundingBox(&mdx->bounds.box, &entityMatrix, &viewdef.viewProjectionMatrix, box_color);
    }
    if (g_run_once) {
        DumpFrameCoverage(re);
    }
    if (g_output_path) {
        /* Save before EndFrame() swaps buffers and before any overlay is drawn. */
        SaveFramePNG(re, g_output_path);
        re->EndFrame();
        return;
    }
    Tool_DrawString(re, useModelCamera ? "mdxtool: model camera" : (g_use_front_ortho ? "mdxtool: front ortho" : "mdxtool: preview camera"), 10, 10);
    Tool_DrawString(re, g_model_path, 10, 28);
    {
        size2_t window = re->GetWindowSize();
        int left_x = 10;
        int right_x = 100;
        int y0 = 46;
        int dy = 18;
        char line[512];

        snprintf(line, sizeof(line), "SEQS %u", (unsigned)g_overlay.num_sequences);
        Tool_DrawString(re, line, left_x, y0 + dy * 0);
        snprintf(line, sizeof(line), "SEQ #%d %s", (int)g_overlay.selected_sequence, g_overlay.selected_sequence_name);
        Tool_DrawString(re, line, left_x, y0 + dy * 8);
        if (seq) {
            snprintf(line,
                     sizeof(line),
                     "INT  %u..%u",
                     (unsigned)seq->interval[0],
                     (unsigned)seq->interval[1]);
            Tool_DrawString(re, line, left_x, y0 + dy * 9);
        }
        snprintf(line, sizeof(line), "TEXS %u", (unsigned)g_overlay.num_textures);
        Tool_DrawString(re, line, left_x, y0 + dy * 1);
        snprintf(line, sizeof(line), "GEOS %u", (unsigned)g_overlay.num_geosets);
        Tool_DrawString(re, line, left_x, y0 + dy * 2);
        snprintf(line, sizeof(line), "CAMS %u", (unsigned)g_overlay.num_cameras);
        Tool_DrawString(re, line, left_x, y0 + dy * 3);
        snprintf(line, sizeof(line), "LITE %u", (unsigned)g_overlay.num_lights);
        Tool_DrawString(re, line, left_x, y0 + dy * 4);
        snprintf(line, sizeof(line), "PRE2 %u", (unsigned)g_overlay.num_emitters);
        Tool_DrawString(re, line, left_x, y0 + dy * 5);
        snprintf(line, sizeof(line), "ATCH %u", (unsigned)g_overlay.num_attachments);
        Tool_DrawString(re, line, left_x, y0 + dy * 6);
        snprintf(line, sizeof(line), "HELP %u", (unsigned)g_overlay.num_helpers);
        Tool_DrawString(re, line, left_x, y0 + dy * 7);

        snprintf(line, sizeof(line), "CENTER %.3f %.3f %.3f", g_model_center.x, g_model_center.y, g_model_center.z);
        Tool_DrawString(re, line, right_x, y0 + dy * 0);
        snprintf(line, sizeof(line), "BONE %u", (unsigned)g_overlay.num_bones);
        Tool_DrawString(re, line, right_x, y0 + dy * 1);
        snprintf(line, sizeof(line), "CLID %u", (unsigned)g_overlay.num_collision_shapes);
        Tool_DrawString(re, line, right_x, y0 + dy * 2);
        snprintf(line, sizeof(line), "PIVT %u", (unsigned)g_overlay.num_pivots);
        Tool_DrawString(re, line, right_x, y0 + dy * 3);
        snprintf(line, sizeof(line), "BBOX %.3f %.3f %.3f", g_overlay.bounds_w, g_overlay.bounds_d, g_overlay.bounds_h);
        Tool_DrawString(re, line, right_x, y0 + dy * 4);
        snprintf(line, sizeof(line), "DIST %.3f", g_overlay.orbit_distance);
        Tool_DrawString(re, line, right_x, y0 + dy * 5);
        snprintf(line, sizeof(line), "SCALE  %.3f", g_preview_scale);
        Tool_DrawString(re, line, right_x, y0 + dy * 6);

        if (mdx && mdx->sequences && mdx->num_sequences > 0) {
            int maxRows = 10;
            int startRow = 0;
            int listX = window.width - 420;
            int listY = 46;

            if (seq_index > maxRows / 2) {
                startRow = seq_index - maxRows / 2;
            }
            if (startRow + maxRows > (int)mdx->num_sequences) {
                startRow = (int)mdx->num_sequences - maxRows;
            }
            if (startRow < 0) {
                startRow = 0;
            }

            for (int row = 0; row < maxRows; row++) {
                int seqi = startRow + row;
                char seqName[81];
                if (seqi < 0 || seqi >= (int)mdx->num_sequences) {
                    continue;
                }
                CopySequenceName(mdx->sequences + seqi, seqName, sizeof(seqName));
                snprintf(line,
                         sizeof(line),
                         "%c[%02d] %s",
                         seqi == seq_index ? '*' : ' ',
                         seqi,
                         seqName);
                Tool_DrawString(re, line, listX, listY + row * dy);
            }
        }
    }
    DrawTexturePreviews(re);
    re->EndFrame();
}

int main(int argc, char **argv) {
    const char *mpq = NULL;
    const char *modelPath = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--use-model-camera") || !strcmp(argv[i], "-use-model-camera")) {
            g_use_model_camera = true;
        } else if (!strcmp(argv[i], "--front-ortho") || !strcmp(argv[i], "-front-ortho")) {
            g_use_front_ortho = true;
        } else if (!strcmp(argv[i], "--info") || !strcmp(argv[i], "-info")) {
            g_info_only = true;
        } else if (!strcmp(argv[i], "--dump-all") || !strcmp(argv[i], "-dump-all")) {
            g_dump_all = true;
        } else if (!strcmp(argv[i], "--once") || !strcmp(argv[i], "-once")) {
            g_run_once = true;
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output") || !strcmp(argv[i], "-output")) {
            if (++i >= argc) { usage(); return 1; }
            g_output_path = argv[i];
            g_run_once = true;
            if (g_fixed_time < 0) g_fixed_time = 1000; /* deterministic default frame/time */
            if (g_seed < 0) g_seed = 1234;             /* deterministic default RNG seed */
        } else if (!strncmp(argv[i], "--output=", 9)) {
            g_output_path = argv[i] + 9;
            g_run_once = true;
            if (g_fixed_time < 0) g_fixed_time = 1000;
            if (g_seed < 0) g_seed = 1234;
        } else if (!strcmp(argv[i], "--frame") || !strcmp(argv[i], "-frame")) {
            if (++i >= argc) { usage(); return 1; }
            g_fixed_time = strtol(argv[i], NULL, 10);
        } else if (!strncmp(argv[i], "--frame=", 8)) {
            g_fixed_time = strtol(argv[i] + 8, NULL, 10);
        } else if (!strcmp(argv[i], "--seed") || !strcmp(argv[i], "-seed")) {
            if (++i >= argc) { usage(); return 1; }
            g_seed = strtol(argv[i], NULL, 10);
        } else if (!strncmp(argv[i], "--seed=", 7)) {
            g_seed = strtol(argv[i] + 7, NULL, 10);
        } else if (!strcmp(argv[i], "--anim") || !strcmp(argv[i], "-anim")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            g_requested_animation = argv[i];
        } else if (!strncmp(argv[i], "--anim=", 7) || !strncmp(argv[i], "-anim=", 6)) {
            g_requested_animation = strchr(argv[i], '=') + 1;
        } else if (!strncmp(argv[i], "-mpq=", 5)) {
            mpq = argv[i] + 5;
        } else if (!strcmp(argv[i], "-mpq")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            mpq = argv[i];
        } else if (!strncmp(argv[i], "-model=", 7)) {
            modelPath = argv[i] + 7;
        } else if (!strcmp(argv[i], "-model")) {
            if (++i >= argc) {
                usage();
                return 1;
            }
            modelPath = argv[i];
        } else if (argv[i][0] != '-' && !modelPath) {
            modelPath = argv[i];
        } else {
            usage();
            return 1;
        }
    }

    if (!mpq || !modelPath) {
        usage();
        return 1;
    }

    if (!Viewer_AddArchive(archives, sizeof(archives) / sizeof(archives[0]), mpq)) {
        return 1;
    }
    Tool_SetSheetHost(archives, sizeof(archives) / sizeof(archives[0]));

    if (g_info_only) {
        bool ok = DumpModelInfoNoWindow(modelPath);
        Viewer_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));
        return ok ? 0 : 1;
    }

    refExport_t re = R_GetAPI((refImport_t){
        .FS_ReadFile = Tool_FS_ReadFile,
        .FS_FreeFile = Tool_FS_FreeFile,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .ReadSheet = FS_ParseSLK,
        .FindSheetCell = FS_FindSheetCell,
        .error = errorf,
    });

    fprintf(stderr, "mdxtool: renderer init\n");
    re.Init(VIEWER_WINDOW_WIDTH, VIEWER_WINDOW_HEIGHT);
    fprintf(stderr, "mdxtool: renderer ready\n");

    g_model_path = modelPath;
    fprintf(stderr, "mdxtool: loading model %s\n", modelPath);
    LPMODEL model = re.LoadModel(modelPath);
    if (!model || !model->mdx) {
        fprintf(stderr, "Failed to load MDX model: %s\n", modelPath);
        re.Shutdown();
        return 1;
    }

    fprintf(stderr, "mdxtool: loaded model %s\n", modelPath);
    {
        BOX3 bounds = GetPreviewBounds(model->mdx);
        float width = fabsf(bounds.max.x - bounds.min.x);
        float depth = fabsf(bounds.max.y - bounds.min.y);
        float height = fabsf(bounds.max.z - bounds.min.z);
        float extent = MAX(width, MAX(depth, height));
        g_model_center = Box3_Center(&bounds);
        g_preview_scale = extent > 0.0001f ? MAX(1.0f, 8.0f / extent) : 1.0f;
        g_preview_scale = MIN(g_preview_scale, 2000.0f);
        fprintf(stderr,
                "mdxtool: preview bounds min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f) size=(%.3f %.3f %.3f) center=(%.3f %.3f %.3f)\n",
                bounds.min.x, bounds.min.y, bounds.min.z,
                bounds.max.x, bounds.max.y, bounds.max.z,
                width, depth, height,
                g_model_center.x, g_model_center.y, g_model_center.z);
        fprintf(stderr, "mdxtool: preview auto-scale=%.3f\n", g_preview_scale);
        g_overlay.bounds_w = width;
        g_overlay.bounds_d = depth;
        g_overlay.bounds_h = height;
    }
    fprintf(stderr, "Sequences: %d, cameras: %p, pivots: %d\n",
            model->mdx->num_sequences,
            (void *)model->mdx->cameras,
            model->mdx->num_pivots);

    {
        mdxSequence_t const *seq = PickSequence(model->mdx);
        int seq_index = FindSequenceIndex(model->mdx, seq);
        g_overlay.selected_sequence = seq_index >= 0 ? (DWORD)seq_index : 0;
        CopySequenceName(seq, g_overlay.selected_sequence_name, sizeof(g_overlay.selected_sequence_name));
    }

        g_overlay.num_sequences = model->mdx->num_sequences;
        g_overlay.num_textures = model->mdx->num_textures;
        g_overlay.num_pivots = model->mdx->num_pivots;
        g_overlay.num_geosets = CountGeosets(model->mdx);
        g_overlay.num_lights = CountLights(model->mdx);
        g_overlay.num_emitters = CountEmitters(model->mdx);
        g_overlay.num_attachments = CountAttachments(model->mdx);
        g_overlay.num_helpers = CountHelpers(model->mdx);
        g_overlay.num_bones = CountBones(model->mdx);
        g_overlay.num_collision_shapes = CountCollisionShapes(model->mdx);
        g_overlay.num_cameras = CountCameras(model->mdx);

    BuildTexturePreviewCache(&re, model);

    size2_t window = re.GetWindowSize();
    float const aspect = window.height ? (float)window.width / (float)window.height : 1.0f;
    VECTOR3 orbit_target = g_model_center;
#ifdef MDXTOOL_USE_OVERLAY_ORBIT_DISTANCE
    float orbit_distance = g_overlay.orbit_distance;
    if (orbit_distance < 1.0f) {
        orbit_distance = FitPreviewDistance(model->mdx, aspect, 35.0f) * g_preview_scale;
        g_overlay.orbit_distance = orbit_distance;
    }
#else
    float orbit_distance = FitPreviewDistance(model->mdx, aspect, 35.0f) * g_preview_scale;
    g_overlay.orbit_distance = orbit_distance;
#endif
    fprintf(stderr,
        "mdxtool: preview mode=%s aspect=%.3f orbit_distance=%.3f\n",
        g_use_model_camera ? "model-camera" : (g_use_front_ortho ? "front-ortho" : "orbit"),
        aspect,
        orbit_distance);
    Viewer_OrbitInit(&orbit, orbit_target, orbit_distance, -45.0f, 20.0f);
    orbit.reverse_drag = true;

    if (g_dump_all) {
        // Print high-level summary info
        fprintf(stderr, "mdxtool --dump-all: model=%s\n", modelPath);
        fprintf(stderr, "  version: %u\n", model->mdx->version);
        fprintf(stderr, "  bounds: min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f) size=(%.3f %.3f %.3f) center=(%.3f %.3f %.3f) radius=%.3f\n",
            model->mdx->bounds.box.min.x, model->mdx->bounds.box.min.y, model->mdx->bounds.box.min.z,
            model->mdx->bounds.box.max.x, model->mdx->bounds.box.max.y, model->mdx->bounds.box.max.z,
            fabsf(model->mdx->bounds.box.max.x - model->mdx->bounds.box.min.x),
            fabsf(model->mdx->bounds.box.max.y - model->mdx->bounds.box.min.y),
            fabsf(model->mdx->bounds.box.max.z - model->mdx->bounds.box.min.z),
            g_model_center.x, g_model_center.y, g_model_center.z,
            model->mdx->bounds.radius);
        fprintf(stderr, "  preview_scale: %.3f\n", g_preview_scale);
        fprintf(stderr, "  orbit_distance: %.3f\n", g_overlay.orbit_distance);
        fprintf(stderr, "  sequences: %u\n", g_overlay.num_sequences);
        fprintf(stderr, "  textures: %u\n", g_overlay.num_textures);
        fprintf(stderr, "  geosets: %u\n", g_overlay.num_geosets);
        fprintf(stderr, "  lights: %u\n", g_overlay.num_lights);
        fprintf(stderr, "  emitters: %u\n", g_overlay.num_emitters);
        fprintf(stderr, "  attachments: %u\n", g_overlay.num_attachments);
        fprintf(stderr, "  helpers: %u\n", g_overlay.num_helpers);
        fprintf(stderr, "  bones: %u\n", g_overlay.num_bones);
        fprintf(stderr, "  collision_shapes: %u\n", g_overlay.num_collision_shapes);
        fprintf(stderr, "  cameras: %u\n", g_overlay.num_cameras);
        fprintf(stderr, "  pivots: %u\n", g_overlay.num_pivots);
        mdxSequence_t const *seq = PickSequence(model->mdx);
        DWORD sample_frame = 0;
        if (seq && seq->interval[1] > seq->interval[0]) {
            sample_frame = seq->interval[0] + ((seq->interval[1] - seq->interval[0]) / 2);
        }
        fprintf(stderr, "mdxtool --dump-all: sample_frame=%u requested_anim=%s\n",
                (unsigned)sample_frame,
                g_requested_animation ? g_requested_animation : "(auto)");
        DumpLoadedModel(model->mdx, sample_frame);
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else {
                Viewer_OrbitHandleEvent(&orbit, &event);
            }
        }

        DWORD now = (g_fixed_time >= 0) ? (DWORD)g_fixed_time : SDL_GetTicks();
        if (g_seed >= 0) {
            srand((unsigned)g_seed); /* deterministic particle RNG for golden renders */
        }
        RenderModelFrame(&re, model, now, g_use_model_camera);
        if (g_run_once) {
            running = false;
        }
    }

    re.ReleaseModel(model);
    FreeTexturePreviewCache();
    re.Shutdown();

    Viewer_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));

    return 0;
}
