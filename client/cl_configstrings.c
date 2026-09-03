/*
 * cl_configstrings.c — Client-side configstring resource lifecycle.
 *
 * Parsing stores the server table; this module performs the initial precache
 * pass and the late replacement pass so those lifecycles cannot be confused.
 */
#include <stdlib.h>

#include "client.h"
#include "sound/s_local.h"

/* Avoid reloading a resource when the server resends the same configstring. */
static BOOL CL_SameResource(void const *handle, LPCSTR olds, LPCSTR name) {
    return handle && name && name[0] && olds && !strcmp(olds, name);
}

/* Register the model and its companion portrait for one model configstring. */
static void CL_RegisterModelConfigString(DWORD index, BOOL replace, LPCSTR olds) {
    DWORD model = index - CS_MODELS;
    LPCSTR name = cl.configstrings[index];
    if (!replace && cl.models[model]) return;
    if (replace && CL_SameResource(cl.models[model], olds, name)) return;
    if (cl.models[model]) SAFE_DELETE(cl.models[model], re.ReleaseModel);
    if (cl.portraits[model]) SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
    if (!*name) return;
    PATHSTR portrait = { 0 };
    LPCSTR ext = strstr(name, ".m");
    if (ext) {
        size_t base_len = (size_t)(ext - name);
        if (base_len >= sizeof(portrait)) base_len = sizeof(portrait) - 1;
        memcpy(portrait, name, base_len);
        portrait[base_len] = '\0';
        snprintf(portrait + base_len, sizeof(portrait) - base_len, "_Portrait%s", ext);
    }
    cl.models[model] = re.LoadModel(name);
    if (!cl.models[model]) fprintf(stderr, "CL_RegisterModelConfigString: failed to load %s\n", name);
    if (portrait[0] && FS_FileExists(portrait)) cl.portraits[model] = re.LoadModel(portrait);
}

/* Register one image configstring during refresh preparation or a late update. */
static void CL_RegisterImageConfigString(DWORD index, BOOL replace, LPCSTR olds) {
    DWORD image = index - CS_IMAGES;
    LPCSTR name = cl.configstrings[index];
    if (!replace && cl.pics[image]) return;
    if (replace && CL_SameResource(cl.pics[image], olds, name)) return;
    if (cl.pics[image]) {
        re.ReleaseTexture((LPTEXTURE)cl.pics[image]);
        cl.pics[image] = NULL;
    }
    if (*name) cl.pics[image] = re.LoadTexture(CL_ResolveImagePath(name));
}

/* Register one font configstring after parsing its optional path,size encoding. */
static void CL_RegisterFontConfigString(DWORD index, BOOL replace, LPCSTR olds) {
    DWORD font = index - CS_FONTS;
    LPCSTR spec = cl.configstrings[index];
    if (cl.fonts[font]) return;
    if (replace && CL_SameResource(cl.fonts[font], olds, spec)) return;
    if (*spec) {
        LPCSTR split = strstr(spec, ",");
        if (split) {
            PATHSTR name = { 0 };
            memcpy(name, spec, split - spec);
            cl.fonts[font] = re.LoadFont(name, atoi(split + 1));
        } else cl.fonts[font] = re.LoadFont(spec, 16);
    }
}

void CL_RegisterConfigString(DWORD index) {
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) CL_RegisterModelConfigString(index, false, NULL);
    else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, false, NULL);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, false, NULL);
}

void CL_UpdateConfigString(DWORD index, LPCSTR olds) {
    if (index > CS_MODELS && index < CS_MODELS + MAX_MODELS) CL_RegisterModelConfigString(index, true, olds);
    else if (index > CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) CL_RegisterImageConfigString(index, true, olds);
    else if (index > CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS && *cl.configstrings[index]) S_RegisterSound(cl.configstrings[index]);
    else if (index > CS_FONTS && index < CS_FONTS + MAX_FONTSTYLES) CL_RegisterFontConfigString(index, true, olds);
}
