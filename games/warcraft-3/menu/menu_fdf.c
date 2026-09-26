/*
 * menu_fdf.c — menu-module host functions for FDF parsing.
 *
 * The FDF parser itself lives in stb_fdf.h (STB_FDF_IMPLEMENTATION).
 * This file provides the menu-module-specific implementations of host
 * services that the parser calls: texture/model loading via the renderer,
 * FDF file reading via mi, and font/string resolution.
 */

#include <stdlib.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include "menu_local.h"

#define UI_MAX_TEXTURES  1024
#define UI_MAX_MODELS    256

#define BZ_HOST_HIDDEN __attribute__((visibility("hidden")))

/* ---- Texture/model cache (menu-module specific) ----------------------------- */

static LPCTEXTURE ui_textures[UI_MAX_TEXTURES] = { 0 };
static PATHSTR ui_texture_names[UI_MAX_TEXTURES] = { 0 };
static PATHSTR ui_texture_keys[UI_MAX_TEXTURES] = { 0 };
static bool ui_texture_decorated[UI_MAX_TEXTURES] = { 0 };
static LPCMODEL ui_models[UI_MAX_MODELS] = { 0 };
static PATHSTR ui_model_names[UI_MAX_MODELS] = { 0 };

void UI_ReleaseAssets(void) {
    LPRENDERER renderer = mi.GetRenderer();

    FOR_LOOP(i, UI_MAX_TEXTURES)
        if (ui_textures[i]) renderer->ReleaseTexture((LPTEXTURE)ui_textures[i]);
    FOR_LOOP(i, UI_MAX_MODELS)
        if (ui_models[i]) renderer->ReleaseModel((LPMODEL)ui_models[i]);
    UI_ClearTextures();
}

BZ_HOST_HIDDEN void UI_ClearTextures(void) {
    memset(ui_textures, 0, sizeof(ui_textures));
    memset(ui_texture_names, 0, sizeof(ui_texture_names));
    memset(ui_texture_keys, 0, sizeof(ui_texture_keys));
    memset(ui_texture_decorated, 0, sizeof(ui_texture_decorated));
    memset(ui_models, 0, sizeof(ui_models));
    memset(ui_model_names, 0, sizeof(ui_model_names));
}

static bool UI_HasKnownTextureExtension(cstring_t file) {
    cstring_t dot = file ? strrchr(file, '.') : NULL;
    return dot && (!strcasecmp(dot, ".blp") ||
                   !strcasecmp(dot, ".tga") ||
                   !strcasecmp(dot, ".dds"));
}

static cstring_t EnsureExtension(cstring_t file, cstring_t ext) {
    static PATHSTR blp;
    if (!UI_HasKnownTextureExtension(file)) {
        snprintf(blp, sizeof(blp), "%s%s", file, ext);
        return blp;
    }
    return file;
}

BZ_HOST_HIDDEN uint32_t UI_LoadTexture(cstring_t file, bool decorate) {
    cstring_t resolved;
    uint32_t index;

    if (!file || !*file) return 0;

    resolved = decorate ? Theme_String(file, "Default") : file;
    resolved = EnsureExtension(resolved, ".blp");

    FOR_LOOP(i, UI_MAX_TEXTURES) {
        if (!ui_texture_names[i][0]) continue;
        if (decorate) {
            if (ui_texture_decorated[i] && !strcmp(ui_texture_keys[i], file))
                return i;
        } else if (!ui_texture_decorated[i] && !strcmp(ui_texture_names[i], resolved)) {
            return i;
        }
    }

    index = 0;
    for (uint32_t i = 1; i < UI_MAX_TEXTURES; i++) {
        if (!ui_texture_names[i][0]) { index = i; break; }
    }
    if (!index || !mi.GetRenderer) return 0;

    snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
    snprintf(ui_texture_keys[index], sizeof(ui_texture_keys[index]), "%s", file);
    ui_texture_decorated[index] = decorate;
    /* FDF templates can contain unused/overridden art; resolve GPU resources only when drawn. */
    return index;
}

cstring_t UI_TextureName(uint32_t index) {
    if (!index || index >= UI_MAX_TEXTURES) return NULL;
    return ui_texture_names[index][0] ? ui_texture_names[index] : NULL;
}

LPCTEXTURE UI_GetTexture(uint32_t index) {
    if (!index || index >= UI_MAX_TEXTURES || !ui_texture_names[index][0]) return NULL;
    LPRENDERER renderer = mi.GetRenderer();
    if (ui_texture_decorated[index] && ui_texture_keys[index][0]) {
        cstring_t resolved = EnsureExtension(Theme_String(ui_texture_keys[index], "Default"), ".blp");
        if (strcmp(ui_texture_names[index], resolved)) {
            if (ui_textures[index]) renderer->ReleaseTexture((LPTEXTURE)ui_textures[index]);
            ui_textures[index] = NULL;
            snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
        }
    }
    if (!ui_textures[index]) ui_textures[index] = renderer->LoadTexture(ui_texture_names[index]);
    return ui_textures[index];
}

LPCMODEL UI_GetModel(uint32_t index) {
    if (!index || index >= UI_MAX_MODELS) return NULL;
    return ui_models[index];
}

BZ_HOST_HIDDEN uint32_t UI_LoadModel(cstring_t file, bool decorate) {
    LPRENDERER renderer = NULL;
    uint32_t modelIndex = 0;
    cstring_t model = file;

    if (!model || !*model) return 0;

    model = decorate ? Theme_String(model, "Default") : model;
    FOR_LOOP(i, UI_MAX_MODELS) {
        if (ui_model_names[i][0] && !strcmp(ui_model_names[i], model))
            return i;
    }

    for (uint32_t i = 1; i < UI_MAX_MODELS; i++) {
        if (!ui_model_names[i][0]) { modelIndex = i; break; }
    }
    if (!modelIndex || !mi.GetRenderer) return 0;

    snprintf(ui_model_names[modelIndex], sizeof(ui_model_names[modelIndex]), "%s", model);
    renderer = mi.GetRenderer();
    if (renderer && renderer->LoadModel && !ui_models[modelIndex])
        ui_models[modelIndex] = renderer->LoadModel(model);
    return modelIndex;
}

/* ---- FDF host services (UI module) ---------------------------------------- */

BZ_HOST_HIDDEN handle_t UI_FdfAlloc(long size) { return mi.MemAlloc(size); }
BZ_HOST_HIDDEN void UI_FdfFree(handle_t ptr) { mi.MemFree(ptr); }
BZ_HOST_HIDDEN uint32_t UI_FdfFontIndex(cstring_t name, uint32_t size) { return mi.FontIndex(name, size); }
BZ_HOST_HIDDEN int UI_FdfReadFile(cstring_t name, handle_t *out) {
    int size = mi.FS_ReadFile(name, out);
    return size;
}
BZ_HOST_HIDDEN void UI_FdfFreeFile(handle_t buf) { mi.FS_FreeFile(buf); }

/* ---- UI_BindMapList (menu-module specific) ----------------------------------- */

void UI_BindMapList(LPFRAMEDEF frame,
                    uiMapListState_t *state,
                    LPCFRAMEDEF label,
                    uint32_t visible_rows,
                    cstring_t select_command)
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
