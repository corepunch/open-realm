/*
 * cl_configstrings.c — Client-side configstring resource lifecycle.
 *
 * Parsing stores the server table; this module performs the initial precache
 * pass and the late replacement pass so those lifecycles cannot be confused.
 */
#include <stdlib.h>

#include "client.h"
#include "games/warcraft-3/common/map_load_debug.h"
#include "sound/s_local.h"

#ifdef WC3_DEBUG_MAP_LOAD
static struct {
    uint32_t visits, cached, model_loads, portrait_probes, portrait_loads;
    double model_ms, portrait_probe_ms, portrait_load_ms;
} cl_model_load_debug;

void CL_ResetModelLoadDebug(void) {
    memset(&cl_model_load_debug, 0, sizeof(cl_model_load_debug));
}

void CL_ReportModelLoadDebug(cstring_t phase, cstring_t map) {
    WC3_MAP_LOAD_LOG("map=%s phase=%s visits=%u cached=%u model-loads=%u model-ms=%.3f portrait-probes=%u portrait-loads=%u portrait-probe-ms=%.3f portrait-load-ms=%.3f\n",
                     map, phase, cl_model_load_debug.visits, cl_model_load_debug.cached,
                     cl_model_load_debug.model_loads, cl_model_load_debug.model_ms,
                     cl_model_load_debug.portrait_probes, cl_model_load_debug.portrait_loads,
                     cl_model_load_debug.portrait_probe_ms, cl_model_load_debug.portrait_load_ms);
}
#endif

/* Avoid reloading a resource when the server resends the same configstring. */
static bool CL_SameResource(void const *handle, cstring_t olds, cstring_t name) {
    return handle && name && name[0] && olds && !strcmp(olds, name);
}

/* Register the model and its companion portrait for one model configstring. */
static void CL_RegisterModelConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t model = index - CS_MODELS;
    cstring_t name = cl.configstrings[index];
#ifdef WC3_DEBUG_MAP_LOAD
    cl_model_load_debug.visits++;
#endif
    if (!replace && cl.models[model]) {
#ifdef WC3_DEBUG_MAP_LOAD
        cl_model_load_debug.cached++;
#endif
        return;
    }
    if (replace && CL_SameResource(cl.models[model], olds, name)) {
#ifdef WC3_DEBUG_MAP_LOAD
        cl_model_load_debug.cached++;
#endif
        return;
    }
    if (cl.models[model]) SAFE_DELETE(cl.models[model], re.ReleaseModel);
    if (cl.portraits[model]) SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
    if (!*name) return;
    PATHSTR portrait = { 0 };
    cstring_t ext = strstr(name, ".m");
    if (ext) {
        size_t base_len = (size_t)(ext - name);
        if (base_len >= sizeof(portrait)) base_len = sizeof(portrait) - 1;
        memcpy(portrait, name, base_len);
        portrait[base_len] = '\0';
        snprintf(portrait + base_len, sizeof(portrait) - base_len, "_Portrait%s", ext);
    }
#ifdef WC3_DEBUG_MAP_LOAD
    double phase_start = WC3_MAP_LOAD_NOW();
#endif
    cl.models[model] = re.LoadModel(name);
#ifdef WC3_DEBUG_MAP_LOAD
    cl_model_load_debug.model_ms += WC3_MAP_LOAD_NOW() - phase_start;
    if (cl.models[model]) cl_model_load_debug.model_loads++;
#endif
    if (!cl.models[model]) fprintf(stderr, "CL_RegisterModelConfigString: failed to load %s\n", name);
    if (portrait[0]) {
#ifdef WC3_DEBUG_MAP_LOAD
        cl_model_load_debug.portrait_probes++;
        phase_start = WC3_MAP_LOAD_NOW();
#endif
        bool const portrait_exists = FS_FileExists(portrait);
#ifdef WC3_DEBUG_MAP_LOAD
        cl_model_load_debug.portrait_probe_ms += WC3_MAP_LOAD_NOW() - phase_start;
#endif
        if (portrait_exists) {
#ifdef WC3_DEBUG_MAP_LOAD
            phase_start = WC3_MAP_LOAD_NOW();
#endif
            cl.portraits[model] = re.LoadModel(portrait);
#ifdef WC3_DEBUG_MAP_LOAD
            cl_model_load_debug.portrait_load_ms += WC3_MAP_LOAD_NOW() - phase_start;
            if (cl.portraits[model]) cl_model_load_debug.portrait_loads++;
#endif
        }
    }
}

/* Register one image configstring during refresh preparation or a late update. */
static void CL_RegisterImageConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t image = index - CS_IMAGES;
    cstring_t name = cl.configstrings[index];
    if (!replace && cl.pics[image]) return;
    if (replace && CL_SameResource(cl.pics[image], olds, name)) return;
    if (cl.pics[image]) {
        re.ReleaseTexture((texture_t *)cl.pics[image]);
        cl.pics[image] = NULL;
    }
    if (*name) cl.pics[image] = re.LoadTexture(name);
}

/* Register one font configstring after parsing its optional path,size encoding. */
static void CL_RegisterFontConfigString(uint32_t index, bool replace, cstring_t olds) {
    uint32_t font = index - CS_FONTS;
    cstring_t spec = cl.configstrings[index];
    if (cl.fonts[font]) return;
    if (replace && CL_SameResource(cl.fonts[font], olds, spec)) return;
    if (*spec) {
        cstring_t split = strstr(spec, ",");
        if (split) {
            PATHSTR name = { 0 };
            memcpy(name, spec, split - spec);
            cl.fonts[font] = re.LoadFont(name, atoi(split + 1));
        } else cl.fonts[font] = re.LoadFont(spec, 16);
    }
}

/* The server chooses point-order art; an empty slot explicitly disables that presentation. */
static void CL_RegisterOrderMarker(void) {
    cstring_t name = cl.configstrings[CS_ORDER_MARKER];
    SAFE_DELETE(cl.moveConfirmation, re.ReleaseModel);
    if (!*name) return;
    cl.moveConfirmation = re.LoadModel(name);
    if (!cl.moveConfirmation) fprintf(stderr, "CL_RegisterOrderMarker: failed to load %s\n", name);
}

void CL_RegisterConfigString(uint32_t index) {
    if (index == CS_ORDER_MARKER) CL_RegisterOrderMarker();
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) CL_RegisterModelConfigString(index, false, NULL);
    else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, false, NULL);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, false, NULL);
}

void CL_UpdateConfigString(uint32_t index, cstring_t olds) {
    if (index == CS_ORDER_MARKER && strcmp(olds, cl.configstrings[index])) CL_RegisterOrderMarker();
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) CL_RegisterModelConfigString(index, true, olds);
    else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, true, olds);
    else if (index > CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS && *cl.configstrings[index]) S_RegisterSound(cl.configstrings[index]);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, true, olds);
}

/* svc_loading_screen commits the screen after its dependencies; present before the bulk table/world arrives. */
void CL_PrepLoading(void) {
    if (cl.refresh_prepped) return;
    if (!cl.layout[LAYER_LOADING] || !*cl.configstrings[CS_WORLD]) {
        Com_Error(ERR_DROP, "Incomplete loading presentation");
        return;
    }
    double const load_start = WC3_MAP_LOAD_NOW();
    double stage_start = load_start;
#ifdef WC3_DEBUG_MAP_LOAD
    CL_ResetModelLoadDebug();
#endif
    uint32_t registered_models = 0, registered_images = 0, registered_fonts = 0;
    if (cl.playerstate.client_ui_state != CLIENT_UI_LOADING)
        CL_BeginLoadingMap(cl.configstrings[CS_WORLD]);
    re.SetAssetScope(cl.configstrings[CS_ASSET_SCOPE]);
    for (uint32_t i = 1; i < MAX_MODELS; i++) {
        if (!*cl.configstrings[CS_MODELS + i]) continue;
        registered_models++;
        CL_RegisterConfigString(CS_MODELS + i);
    }
    WC3_MAP_LOAD_LOG("map=%s phase=client-preload-models count=%u ms=%.3f\n",
                     cl.configstrings[CS_WORLD], registered_models, WC3_MAP_LOAD_NOW() - stage_start);
#ifdef WC3_DEBUG_MAP_LOAD
    CL_ReportModelLoadDebug("client-preload-model-detail", cl.configstrings[CS_WORLD]);
#endif
    stage_start = WC3_MAP_LOAD_NOW();
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (!*cl.configstrings[CS_IMAGES + i]) continue;
        registered_images++;
        CL_RegisterConfigString(CS_IMAGES + i);
    }
    WC3_MAP_LOAD_LOG("map=%s phase=client-preload-images count=%u ms=%.3f\n",
                     cl.configstrings[CS_WORLD], registered_images, WC3_MAP_LOAD_NOW() - stage_start);
    stage_start = WC3_MAP_LOAD_NOW();
    for (uint32_t i = 1; i < MAX_FONTSTYLES; i++) {
        if (!*cl.configstrings[CS_FONTS + i]) continue;
        registered_fonts++;
        CL_RegisterConfigString(CS_FONTS + i);
    }
    WC3_MAP_LOAD_LOG("map=%s phase=client-preload-fonts count=%u ms=%.3f\n",
                     cl.configstrings[CS_WORLD], registered_fonts, WC3_MAP_LOAD_NOW() - stage_start);
    SCR_UpdateLoadingPlaque();
    WC3_MAP_LOAD_LOG("map=%s phase=client-preload-total ms=%.3f\n",
                     cl.configstrings[CS_WORLD], WC3_MAP_LOAD_NOW() - load_start);
#ifndef WC3_DEBUG_MAP_LOAD
    (void)stage_start;
#endif
}
