#include "r_terrain_layers.h"

void R_DrawTerrainSegment(mapsegment_t const *segment, uint32_t mask) {
    if (!segment || !Frustum_ContainsAABox(&tr.viewDef.frustum, &segment->bbox))
        return;
    FOR_EACH_LIST(maplayer_t, layer, segment->layers) {
        if (((1 << layer->type) & mask) == 0)
            continue;
        R_BindTexture(layer->texture, 0);
        R_ApplyShader(&tr.shader_default);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
}
