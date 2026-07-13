/*
 * ui_fdf.c — UI-module host functions for FDF parsing.
 *
 * The FDF parser itself lives in stb_fdf.h (STB_FDF_IMPLEMENTATION).
 * This file provides the UI-module-specific implementations of host
 * services that the parser calls: texture/model loading via the renderer,
 * FDF file reading via uiimport, and font/string resolution.
 */

#include <stdlib.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include "ui_local.h"

#define MAX_IMAGES  1024
#define MAX_MODELS  256

#define BZ_HOST_HIDDEN __attribute__((visibility("hidden")))

/* ---- Texture/model cache (UI-module specific) ----------------------------- */

static LPCTEXTURE ui_textures[MAX_IMAGES] = { 0 };
static PATHSTR ui_texture_names[MAX_IMAGES] = { 0 };
static PATHSTR ui_texture_keys[MAX_IMAGES] = { 0 };
static BOOL ui_texture_decorated[MAX_IMAGES] = { 0 };
static LPCMODEL ui_models[MAX_MODELS] = { 0 };
static PATHSTR ui_model_names[MAX_MODELS] = { 0 };

BZ_HOST_HIDDEN void UI_ClearTextures(void) {
    memset(ui_textures, 0, sizeof(ui_textures));
    memset(ui_texture_names, 0, sizeof(ui_texture_names));
    memset(ui_texture_keys, 0, sizeof(ui_texture_keys));
    memset(ui_texture_decorated, 0, sizeof(ui_texture_decorated));
    memset(ui_models, 0, sizeof(ui_models));
    memset(ui_model_names, 0, sizeof(ui_model_names));
}

static BOOL UI_HasKnownTextureExtension(LPCSTR file) {
    LPCSTR dot = file ? strrchr(file, '.') : NULL;
    return dot && (!strcasecmp(dot, ".blp") ||
                   !strcasecmp(dot, ".tga") ||
                   !strcasecmp(dot, ".dds"));
}

static LPCSTR EnsureExtension(LPCSTR file, LPCSTR ext) {
    static PATHSTR blp;
    if (!UI_HasKnownTextureExtension(file)) {
        snprintf(blp, sizeof(blp), "%s%s", file, ext);
        return blp;
    }
    return file;
}

BZ_HOST_HIDDEN DWORD UI_LoadTexture(LPCSTR file, BOOL decorate) {
    LPRENDERER renderer;
    LPCSTR resolved;
    DWORD index;

    if (!file || !*file) return 0;

    resolved = decorate ? Theme_String(file, "Default") : file;
    resolved = EnsureExtension(resolved, ".blp");

    FOR_LOOP(i, MAX_IMAGES) {
        if (!ui_texture_names[i][0]) continue;
        if (decorate) {
            if (ui_texture_decorated[i] && !strcmp(ui_texture_keys[i], file))
                return i;
        } else if (!ui_texture_decorated[i] && !strcmp(ui_texture_names[i], resolved)) {
            return i;
        }
    }

    index = 0;
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!ui_texture_names[i][0]) { index = i; break; }
    }
    if (!index || !uiimport.GetRenderer) return 0;

    snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
    snprintf(ui_texture_keys[index], sizeof(ui_texture_keys[index]), "%s", file);
    ui_texture_decorated[index] = decorate;
    renderer = uiimport.GetRenderer();
    if (renderer && renderer->LoadTexture && !ui_textures[index])
        ui_textures[index] = renderer->LoadTexture(resolved);
    return index;
}

LPCSTR UI_TextureName(DWORD index) {
    if (!index || index >= MAX_IMAGES) return NULL;
    return ui_texture_names[index][0] ? ui_texture_names[index] : NULL;
}

LPCTEXTURE UI_GetTexture(DWORD index) {
    LPRENDERER renderer;
    LPCSTR resolved;

    if (!index || index >= MAX_IMAGES) return NULL;
    if (ui_texture_decorated[index] && ui_texture_keys[index][0]) {
        resolved = EnsureExtension(Theme_String(ui_texture_keys[index], "Default"), ".blp");
        if (strcmp(ui_texture_names[index], resolved)) {
            renderer = uiimport.GetRenderer();
            if (renderer && renderer->LoadTexture) {
                if (ui_textures[index] && renderer->ReleaseTexture)
                    renderer->ReleaseTexture((LPTEXTURE)ui_textures[index]);
                ui_textures[index] = renderer->LoadTexture(resolved);
                snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
            }
        }
    }
    return ui_textures[index];
}

LPCMODEL UI_GetModel(DWORD index) {
    if (!index || index >= MAX_MODELS) return NULL;
    return ui_models[index];
}

BZ_HOST_HIDDEN DWORD UI_LoadModel(LPCSTR file, BOOL decorate) {
    LPRENDERER renderer = NULL;
    DWORD modelIndex = 0;
    LPCSTR model = file;

    if (!model || !*model) return 0;

    model = decorate ? Theme_String(model, "Default") : model;
    FOR_LOOP(i, MAX_MODELS) {
        if (ui_model_names[i][0] && !strcmp(ui_model_names[i], model))
            return i;
    }

    for (DWORD i = 1; i < MAX_MODELS; i++) {
        if (!ui_model_names[i][0]) { modelIndex = i; break; }
    }
    if (!modelIndex || !uiimport.GetRenderer) return 0;

    snprintf(ui_model_names[modelIndex], sizeof(ui_model_names[modelIndex]), "%s", model);
    renderer = uiimport.GetRenderer();
    if (renderer && renderer->LoadModel && !ui_models[modelIndex])
        ui_models[modelIndex] = renderer->LoadModel(model);
    return modelIndex;
}

DWORD UI_GetTime(void) { return 0; /* TODO: implement via client time */ }

/* ---- FDF host services (UI module) ---------------------------------------- */

BZ_HOST_HIDDEN HANDLE UI_FdfAlloc(long size) { return uiimport.MemAlloc(size); }
BZ_HOST_HIDDEN void UI_FdfFree(HANDLE ptr) { uiimport.MemFree(ptr); }
BZ_HOST_HIDDEN DWORD UI_FdfFontIndex(LPCSTR name, DWORD size) { return uiimport.FontIndex(name, size); }
BZ_HOST_HIDDEN int UI_FdfReadFile(LPCSTR name, HANDLE *out) {
    int size = uiimport.FS_ReadFile(name, out);
    return size;
}
BZ_HOST_HIDDEN void UI_FdfFreeFile(HANDLE buf) { uiimport.FS_FreeFile(buf); }

/* ---- UI_BindMapList (UI-module specific) ----------------------------------- */

void UI_BindMapList(LPFRAMEDEF frame,
                    uiMapListState_t *state,
                    LPCFRAMEDEF label,
                    DWORD visible_rows,
                    LPCSTR select_command)
{
    uiMapListControl_t *control;

    if (!frame) return;

    control = &frame->MapListControl;
    memset(control, 0, sizeof(*control));
    control->State = state;
    UI_WireFrameTypeFunctions(frame);
    control->VisibleRows = visible_rows;
    control->RowHeight = 0.019f;
    control->InsetX = 0.008f;
    control->InsetY = 0.007f;
    snprintf(control->SelectCommand, sizeof(control->SelectCommand),
             "%s", select_command ? select_command : "");
    snprintf(control->FontName, sizeof(control->FontName),
             "%s", label && label->Font.Name[0] ? label->Font.Name : "MasterFont");
    control->FontSize = label && label->Font.Size > 0 ? label->Font.Size : 0.010f;
    control->TextColor = Theme_ListBoxTextColor();
    control->SelectedTextColor = Theme_ListBoxSelectedTextColor();
}
