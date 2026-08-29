#include "client/tr_public.h"
#include "games/starcraft-2/renderer/m3/r_m3.h"
#include "tools/viewer_common.h"

#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE archives[64];
static LPCSTR g_model_path;
static bool g_info_only;
static bool g_dump_all;
static bool g_run_once;
static viewer_orbit_t g_orbit;

HANDLE MemAlloc(long size) { return Tool_MemAlloc(size); }
void MemFree(HANDLE mem) { Viewer_MemFree(mem); }

void Sys_Quit(void) {
    exit(0);
}

bool FS_ExtractFile(LPCSTR toExtract, LPCSTR extracted) {
    return Viewer_ExtractFile(archives, sizeof(archives) / sizeof(archives[0]), toExtract, extracted);
}

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  m3tool -mpq <archive.SC2Assets> [-mpq <archive.SC2Data> ...] -model <file.m3> [--info] [--dump-all] [--once]\n"
            "\n"
            "Examples:\n"
            "  m3tool -mpq data/StarCraft2/Mods/Liberty.SC2Mod/base.SC2Assets -model Assets\\\\Cliffs\\\\CliffMade0\\\\CliffMade0_ABBB_00.m3\n"
            "  m3tool -mpq data/StarCraft2/Mods/Liberty.SC2Mod/base.SC2Assets -model Assets\\\\Units\\\\Terran\\\\Marine\\\\Marine.m3 --dump-all\n");
}

static void errorf(LPCSTR fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void Tool_DrawString(refExport_t const *re, LPCSTR string, int x, int y) {
    if (!re || !string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re->DrawChar(x + (int)i * 8, y, (BYTE)string[i]);
    }
}

static void PrintLayer(LPCSTR label, m3Layer_t const *layer, DWORD count) {
    FOR_LOOP(i, count) {
        COLOR32 color = layer[i].color.initValue;
        fprintf(stderr,
                "    %s[%u]: flags=0x%08x uv=%u color=(%u %u %u %u) bright=%.3f mid=%.3f tex=%s\n",
                label,
                (unsigned)i,
                (unsigned)layer[i].flags,
                (unsigned)layer[i].uvSource1,
                (unsigned)color.r,
                (unsigned)color.g,
                (unsigned)color.b,
                (unsigned)color.a,
                layer[i].brightMult.initValue,
                layer[i].midtoneOffset.initValue,
                layer[i].imagePath ? layer[i].imagePath : "");
    }
}

static BOX3 M3PreviewBounds(m3Model_t const *m3) {
    BOX3 bounds = { 0 };
    BOOL has_bounds = false;

    if (!m3) {
        return bounds;
    }
    FOR_LOOP(i, m3->verticesNum) {
        VECTOR3 const *v = &m3->vertices[i].pos;
        if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z)) {
            continue;
        }
        if (!has_bounds) {
            bounds.min = *v;
            bounds.max = *v;
            has_bounds = true;
            continue;
        }
        bounds.min.x = MIN(bounds.min.x, v->x);
        bounds.min.y = MIN(bounds.min.y, v->y);
        bounds.min.z = MIN(bounds.min.z, v->z);
        bounds.max.x = MAX(bounds.max.x, v->x);
        bounds.max.y = MAX(bounds.max.y, v->y);
        bounds.max.z = MAX(bounds.max.z, v->z);
    }
    if (!has_bounds) {
        bounds.min = m3->boundings.min;
        bounds.max = m3->boundings.max;
    }
    return bounds;
}

static void PrintModelInfo(LPCMODEL model) {
    m3Model_t const *m3 = model ? model->m3 : NULL;
    BOX3 bounds = M3PreviewBounds(m3);
    FLOAT width = fabsf(bounds.max.x - bounds.min.x);
    FLOAT depth = fabsf(bounds.max.y - bounds.min.y);
    FLOAT height = fabsf(bounds.max.z - bounds.min.z);

    if (!m3) {
        fprintf(stderr, "m3tool: no M3 model loaded\n");
        return;
    }
    fprintf(stderr, "m3tool --info: model=%s\n", g_model_path);
    fprintf(stderr, "  type: %u\n", (unsigned)m3->type);
    fprintf(stderr, "  vertex_flags: 0x%08x\n", (unsigned)m3->vertexFlags);
    fprintf(stderr, "  vertices: %u\n", (unsigned)m3->verticesNum);
    fprintf(stderr, "  divisions: %u\n", (unsigned)m3->divisionsNum);
    fprintf(stderr, "  bones: %u\n", (unsigned)m3->bonesNum);
    fprintf(stderr, "  bone_lookup: %u\n", (unsigned)m3->boneLookupNum);
    fprintf(stderr, "  sequences: %u\n", (unsigned)m3->sequencesNum);
    fprintf(stderr, "  materials: %u\n", (unsigned)m3->materialStandardNum);
    fprintf(stderr,
            "  bounds: min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f) size=(%.3f %.3f %.3f)\n",
            bounds.min.x, bounds.min.y, bounds.min.z,
            bounds.max.x, bounds.max.y, bounds.max.z,
            width, depth, height);
    fprintf(stderr,
            "  authored_bounds: min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f) radius=%.3f\n",
            m3->boundings.min.x, m3->boundings.min.y, m3->boundings.min.z,
            m3->boundings.max.x, m3->boundings.max.y, m3->boundings.max.z,
            m3->boundings.radius);
    if (g_dump_all) {
        FOR_LOOP(i, m3->divisionsNum) {
            m3Divisions_t const *div = &m3->divisions[i];
            fprintf(stderr,
                    "  DIV[%u]: faces=%u regions=%u batches=%u\n",
                    (unsigned)i,
                    (unsigned)div->facesNum,
                    (unsigned)div->regionsNum,
                    (unsigned)div->batchesNum);
            FOR_LOOP(j, div->regionsNum) {
                m3Region_t const *region = &div->regions[j];
                fprintf(stderr,
                        "    REG[%u]: firstVertex=%u vertices=%u firstTri=%u tris=%u boneLookup=%u weights=%u\n",
                        (unsigned)j,
                        (unsigned)region->firstVertexIndex,
                        (unsigned)region->verticesCount,
                        (unsigned)region->firstTriangleIndex,
                        (unsigned)region->triangleIndicesCount,
                        (unsigned)region->firstBoneLookupIndex,
                        (unsigned)region->boneWeightPairsCount);
            }
            FOR_LOOP(j, div->batchesNum) {
                m3Batch_t const *batch = &div->batches[j];
                fprintf(stderr,
                        "    BAT[%u]: region=%u materialRef=%u unknown=(%u %u %u)\n",
                        (unsigned)j,
                        (unsigned)batch->regionIndex,
                        (unsigned)batch->materialReferenceIndex,
                        (unsigned)batch->unknown0,
                        (unsigned)batch->unknown1,
                        (unsigned)batch->unknown2);
            }
        }
        FOR_LOOP(i, m3->materialReferencesNum) {
            m3MaterialReference_t const *mref = &m3->materialReferences[i];
            fprintf(stderr,
                    "  MREF[%u]: type=%u material=%u\n",
                    (unsigned)i,
                    (unsigned)mref->materialType,
                    (unsigned)mref->materialIndex);
        }
        FOR_LOOP(i, m3->materialCompositeNum) {
            m3CompositeMaterial_t const *composite = &m3->materialComposite[i];
            fprintf(stderr,
                    "  CMP[%u]: name=%s unknown=%u sections=%u\n",
                    (unsigned)i,
                    composite->name ? composite->name : "",
                    (unsigned)composite->unknown,
                    (unsigned)composite->sectionsNum);
            FOR_LOOP(j, composite->sectionsNum) {
                m3CompositeMaterialSection_t const *section = &composite->sections[j];
                fprintf(stderr,
                        "    CMS[%u]: materialRef=%u alpha=%.3f\n",
                        (unsigned)j,
                        (unsigned)section->materialReferenceIndex,
                        section->alphaFactor.initValue);
            }
        }
        FOR_LOOP(i, m3->materialStandardNum) {
            m3Material_t const *mat = &m3->materialStandard[i];
            fprintf(stderr,
                    "  MAT[%u]: name=%s flags=0x%08x addFlags=0x%08x blend=%u priority=%d cutout=%u spec=%.3f specMult=%.3f emisMult=%.3f layerBlend=%u emisBlend=%u emisMode=%u specType=%u\n",
                    (unsigned)i,
                    mat->name ? mat->name : "",
                    (unsigned)mat->flags,
                    (unsigned)mat->additionalFlags,
                    (unsigned)mat->blendMode,
                    (int)mat->priority,
                    (unsigned)mat->cutoutThreshold,
                    mat->specularity,
                    mat->specMult,
                    mat->emisMult,
                    (unsigned)mat->layerBlendType,
                    (unsigned)mat->emisBlendType,
                    (unsigned)mat->emisMode,
                    (unsigned)mat->specType);
            PrintLayer("DIFF", mat->diffuseLayer, mat->diffuseLayerNum);
            PrintLayer("DECL", mat->decalLayer, mat->decalLayerNum);
            PrintLayer("SPEC", mat->specularLayer, mat->specularLayerNum);
            PrintLayer("GLOS", mat->glossLayer, mat->glossLayerNum);
            PrintLayer("EMIS", mat->emissiveLayer, mat->emissiveLayerNum);
            PrintLayer("EMI2", mat->emissive2Layer, mat->emissive2LayerNum);
            PrintLayer("EVIO", mat->evioLayer, mat->evioLayerNum);
            PrintLayer("EVIM", mat->evioMaskLayer, mat->evioMaskLayerNum);
            PrintLayer("ALPH", mat->alphaMaskLayer, mat->alphaMaskLayerNum);
            PrintLayer("ALP2", mat->alphaMask2Layer, mat->alphaMask2LayerNum);
            PrintLayer("NORM", mat->normalLayer, mat->normalLayerNum);
            PrintLayer("HGHT", mat->heightLayer, mat->heightLayerNum);
            PrintLayer("LITE", mat->lightMapLayer, mat->lightMapLayerNum);
            PrintLayer("AOCL", mat->ambientOcclusionLayer, mat->ambientOcclusionLayerNum);
        }
        FOR_LOOP(i, m3->sequencesNum) {
            m3Sequence_t const *seq = &m3->sequences[i];
            fprintf(stderr,
                    "  SEQ[%u]: interval=%u..%u flags=0x%08x\n",
                    (unsigned)i,
                    (unsigned)seq->interval[0],
                    (unsigned)seq->interval[1],
                    (unsigned)seq->flags);
        }
    }
}

static void RenderFrame(refExport_t const *re, LPCMODEL model, LPCBOX3 bounds, DWORD now) {
    viewDef_t viewdef = { 0 };
    renderEntity_t entity = { 0 };
    VECTOR3 center = Box3_Center(bounds);
    FLOAT width = fabsf(bounds->max.x - bounds->min.x);
    FLOAT depth = fabsf(bounds->max.y - bounds->min.y);
    FLOAT height = fabsf(bounds->max.z - bounds->min.z);
    FLOAT radius = MAX(1.0f, MAX(width, MAX(depth, height)));
    size2_t window = re->GetWindowSize();
    FLOAT aspect = window.height ? (FLOAT)window.width / (FLOAT)window.height : 1.0f;
    MATRIX4 entity_matrix;

    entity.model = model;
    entity.scale = 1.0f;
    entity.origin = (VECTOR3){ -center.x, -center.y, -center.z };
    entity.frame = now;
    entity.oldframe = now;

    viewdef.viewport = (RECT){ 0, 0, 1, 1 };
    viewdef.scissor = (RECT){ 0, 0, 1, 1 };
    viewdef.time = now;
    viewdef.deltaTime = 16;
    viewdef.lerpfrac = 0.0f;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_NOFOGMASK;
    Matrix4_identity(&viewdef.textureMatrix);
    Viewer_OrbitBuildCamera(&g_orbit, aspect, 35.0f, 0.1f, MAX(100.0f, g_orbit.distance + radius * 8.0f), &viewdef.viewProjectionMatrix);
    Viewer_OrbitBuildLight(&g_orbit, &(VECTOR3){ 0.0f, 0.0f, 0.0f }, MAX(32.0f, radius * 2.0f), &viewdef.lightMatrix);

    re->BeginFrame();
    re->RenderFrame(&viewdef);
    Matrix4_identity(&entity_matrix);
    Matrix4_translate(&entity_matrix, &entity.origin);
    re->DrawBoundingBox(bounds, &entity_matrix, &viewdef.viewProjectionMatrix, (COLOR32){ 0, 255, 128, 180 });
    Tool_DrawString(re, "m3tool: M3 viewer", 10, 10);
    Tool_DrawString(re, g_model_path, 10, 28);
    re->EndFrame();
}

int main(int argc, char **argv) {
    refExport_t re;
    LPMODEL model;
    BOX3 bounds;
    FLOAT width, depth, height, extent;
    BOOL running = true;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-mpq") && i + 1 < argc) {
            if (!Viewer_AddArchive(archives, sizeof(archives) / sizeof(archives[0]), argv[++i])) {
                return 1;
            }
        } else if (!strcmp(argv[i], "-model") && i + 1 < argc) {
            g_model_path = argv[++i];
        } else if (!strcmp(argv[i], "--info") || !strcmp(argv[i], "-info")) {
            g_info_only = true;
        } else if (!strcmp(argv[i], "--dump-all") || !strcmp(argv[i], "-dump-all")) {
            g_dump_all = true;
        } else if (!strcmp(argv[i], "--once")) {
            g_run_once = true;
        } else {
            usage();
            return 1;
        }
    }
    if (!g_model_path) {
        usage();
        return 1;
    }

    Tool_SetSheetHost(archives, sizeof(archives) / sizeof(archives[0]));
    re = R_GetAPI((refImport_t){
        .FS_ReadFile = Tool_FS_ReadFile,
        .FS_FreeFile = Tool_FS_FreeFile,
        .FileExtract = FS_ExtractFile,
        .MemAlloc = Tool_MemAlloc,
        .MemFree = Tool_MemFree,
        .LoadSlkCache = Stb_SlkCacheLoad,
        .error = errorf,
    });

    fprintf(stderr, "m3tool: renderer init\n");
    re.Init(VIEWER_WINDOW_WIDTH, VIEWER_WINDOW_HEIGHT);
    fprintf(stderr, "m3tool: loading model %s\n", g_model_path);
    model = re.LoadModel(g_model_path);
    if (!model || model->modeltype != ID_43DM || !model->m3) {
        fprintf(stderr, "m3tool: failed to load M3 model %s\n", g_model_path);
        re.Shutdown();
        return 1;
    }
    PrintModelInfo(model);
    if (g_info_only) {
        re.Shutdown();
        return 0;
    }

    bounds = M3PreviewBounds(model->m3);
    width = fabsf(bounds.max.x - bounds.min.x);
    depth = fabsf(bounds.max.y - bounds.min.y);
    height = fabsf(bounds.max.z - bounds.min.z);
    extent = MAX(1.0f, MAX(width, MAX(depth, height)));
    Viewer_OrbitInit(&g_orbit, (VECTOR3){ 0, 0, 0 }, extent * 2.5f, -45.0f, 20.0f);
    g_orbit.reverse_drag = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else {
                Viewer_OrbitHandleEvent(&g_orbit, &event);
            }
        }
        RenderFrame(&re, model, &bounds, SDL_GetTicks());
        if (g_run_once) {
            running = false;
        }
    }

    re.Shutdown();
    return 0;
}
