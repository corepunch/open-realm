#include "r_terrain_layers.h"

void R_DrawTerrainSegment(LPCMAPSEGMENT segment, DWORD mask) {
    if (!segment || !Frustum_ContainsAABox(&tr.viewDef.frustum, &segment->bbox))
        return;
    FOR_EACH_LIST(MAPLAYER, layer, segment->layers) {
        if (((1 << layer->type) & mask) == 0)
            continue;
        R_BindTexture(layer->texture, 0);
        R_DrawBuffer(layer->buffer, layer->num_vertices);
    }
}
