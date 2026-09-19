#include "r_lightning.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/weather.h"

typedef struct {
    DWORD id;
    LPCSTR dir;
    LPCSTR file;
    FLOAT avg_seg_len;
    FLOAT width;
    DWORD r, g, b, a;
    FLOAT noise_scale;
    FLOAT texcoord_scale;
    FLOAT duration;
    DWORD version;
    LPCTEXTURE texture;
} w3LightningArt_t;

static slkField_t const lightning_schema[] = {
    { "", offsetof(w3LightningArt_t, id), STB_SLK_FOURCC },
    { "Dir", offsetof(w3LightningArt_t, dir), STB_SLK_STR },
    { "file", offsetof(w3LightningArt_t, file), STB_SLK_STR },
    { "AvgSegLen", offsetof(w3LightningArt_t, avg_seg_len), STB_SLK_FLOAT },
    { "Width", offsetof(w3LightningArt_t, width), STB_SLK_FLOAT },
    { "R", offsetof(w3LightningArt_t, r), STB_SLK_INT },
    { "G", offsetof(w3LightningArt_t, g), STB_SLK_INT },
    { "B", offsetof(w3LightningArt_t, b), STB_SLK_INT },
    { "A", offsetof(w3LightningArt_t, a), STB_SLK_INT },
    { "NoiseScale", offsetof(w3LightningArt_t, noise_scale), STB_SLK_FLOAT },
    { "TexCoordScale", offsetof(w3LightningArt_t, texcoord_scale), STB_SLK_FLOAT },
    { "Duration", offsetof(w3LightningArt_t, duration), STB_SLK_FLOAT },
    { "version", offsetof(w3LightningArt_t, version), STB_SLK_INT },
    { NULL, 0, 0 },
};

static w3LightningArt_t *lightning_rows;
static DWORD lightning_count;
static slkIndex_t lightning_index;

static DWORD R_LightningLoadSlk(LPCSTR filename, void **dest) {
    PATHSTR scoped;
    DWORD count = 0;
    if (R_MapAssetCandidate(filename, scoped, sizeof(scoped)))
        count = ri.LoadSlk(scoped, lightning_schema, dest, sizeof(w3LightningArt_t));
    if (!count) count = ri.LoadSlk(filename, lightning_schema, dest, sizeof(w3LightningArt_t));
    return count;
}

static LPCTEXTURE R_LightningTexture(w3LightningArt_t *art) {
    PATHSTR path;
    if (!art) return NULL;
    if (art->texture) return art->texture;
    if (!art->file || !*art->file) art->texture = R_LoadTexture("Textures\\white.blp");
    else {
        if (art->dir && *art->dir) snprintf(path, sizeof(path), "%s\\%s", art->dir, art->file);
        else strlcpy(path, art->file, sizeof(path));
        art->texture = R_LoadTexture(path);
    }
    if (art->texture) R_SetTextureWrap(art->texture, true, false);
    return art->texture;
}

void R_LightningInit(void) {
    lightning_rows = NULL;
    lightning_count = 0;
    memset(&lightning_index, 0, sizeof(lightning_index));
}

void R_LightningShutdown(void) {
    FS_SLKFreeIndex(&lightning_index);
    FS_SLKFreeRows(lightning_schema, lightning_rows, lightning_count, sizeof(w3LightningArt_t));
    lightning_rows = NULL;
    lightning_count = 0;
}

void R_LightningRegisterMap(void) {
    FS_SLKFreeIndex(&lightning_index);
    FS_SLKFreeRows(lightning_schema, lightning_rows, lightning_count, sizeof(w3LightningArt_t));
    lightning_rows = NULL;
    lightning_count = R_LightningLoadSlk("Splats\\LightningData.slk", (void **)&lightning_rows);
    FS_SLKBuildIndex(&lightning_index, lightning_rows, lightning_count, sizeof(w3LightningArt_t));
}

static BYTE R_LightningMulByte(DWORD authored, BYTE tint) {
    DWORD value = MIN(authored, 255u) * (DWORD)tint;
    return (BYTE)((value + 127u) / 255u);
}

void R_LightningDraw(void) {
    FOR_LOOP(i, tr.viewDef.num_lightning_effects) {
        wc3LightningEffect_t const *state = tr.viewDef.lightning_effects + i;
        w3LightningArt_t *art = FS_SLKLookup(&lightning_index, state->effect_id);
        COLOR32 color;
        LPCTEXTURE texture;
        FLOAT width;
        if (!art) continue;
        texture = R_LightningTexture(art);
        if (!texture) continue;
        width = art->width > 0.0f ? art->width * 2.0f : 16.0f;
        color = MAKE(COLOR32,
            R_LightningMulByte(art->r, state->color.r),
            R_LightningMulByte(art->g, state->color.g),
            R_LightningMulByte(art->b, state->color.b),
            R_LightningMulByte(art->a, state->color.a));
        R_DrawRibbonSprite(texture, &state->source, &state->target, width, color, BLEND_MODE_ADD, false);
    }
}
