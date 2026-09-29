#include "r_terrain_layers.h"

void R_DrawTerrainSegment(mapsegment_t const *segment, uint32_t mask) {
    bool first_ground = true;
    if (!segment || !Frustum_ContainsAABox(&tr.viewDef.frustum, &segment->bbox))
        return;
    FOR_EACH_LIST(maplayer_t, layer, segment->layers) {
        if (((1 << layer->type) & mask) == 0)
            continue;
        if (layer->type == MAPLAYERTYPE_GROUND) {
            if (first_ground) R_Call(glDisable, GL_BLEND);
            else {
                R_Call(glEnable, GL_BLEND);
                R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            first_ground = false;
        }
        R_BindTexture(layer->texture, 0);
        R_ApplyShader(&tr.shader_default);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
}
