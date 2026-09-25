#ifndef R_SC2_ROAD_DRAW_H
#define R_SC2_ROAD_DRAW_H

/* Coplanar road surfaces use raster depth bias, not a world-space lift above selection rings. */
static inline void r_sc2_draw_road_layers(LPCMAPLAYER layers, renderEntity_t const *entity) {
    /* Ground/cliffs already cast this coplanar surface. Negative decal bias in the light pass
       made the road shadow itself and also cleared the positive bias needed by later units. */
    if (!layers || tr.render_phase == RENDER_PHASE_LIGHTS) return;
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    /* Cliff material overlays already use -1; roads must sit ahead of that depth layer. */
    R_Call(glPolygonOffset, -2.0f, -2.0f);
    for (LPCMAPLAYER layer = layers; layer; layer = layer->next)
        M3_RenderBuffer(entity, entity->model->m3, layer->buffer, layer->num_vertices, layer->num_indices);
    R_Call(glPolygonOffset, 0.0f, 0.0f);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
}

#endif
