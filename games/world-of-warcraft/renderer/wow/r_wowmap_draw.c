#include "r_wowmap.h"

/* Day-cycle fraction [0,1) from the engine frame clock. Classic has no
   Light*.dbc / .lit, so there is no authored game-time source; the cycle is
   synthesized over WOW_DAY_LENGTH_MS of engine time. */
FLOAT Wow_DayFraction(void) {
    return fmodf((FLOAT)tr.viewDef.time / WOW_DAY_LENGTH_MS, 1.0f);
}

/* Synthesized sun direction, pointing from the surface toward the sun (the
   convention the model/terrain shaders expect for N.L). Classic ships no
   Light*.dbc, so the authored sun path is unavailable; this is WoWee's
   time-of-day directionalDir negated and Y-up→Z-up swapped to engine axes.
   day_frac 0=midnight, 0.25=dawn(sun in -X), 0.5=noon(overhead), 0.75=dusk. */
void Wow_SunDirection(FLOAT day_frac, LPVECTOR3 out) {
    FLOAT a = day_frac * 6.283185307f; /* 2π over the day cycle */
    out->x = -0.6f * sinf(a);
    out->y = -0.6f * cosf(a);
    out->z = 0.6f - 0.4f * cosf(a);
    Vector3_normalize(out);
}

BOOL Wow_EntityInView(renderEntity_t const *entity) {
    VECTOR3 camera_origin;
    VECTOR3 delta;
    float radius;

    if (!entity) {
        return false;
    }

    camera_origin = tr.viewDef.camerastate[0].origin;
    delta = Vector3_sub(&entity->origin, &camera_origin);
    if (Vector3_len(&delta) > ((entity->flags & RF_GROUND_EFFECT) ? WOW_GRASS_DRAW_DISTANCE : WOW_DOODAD_DRAW_DISTANCE)) {
        return false;
    }

    radius = MAX(entity->radius * MAX(entity->scale, 1.0f), 16.0f);
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){ .center = entity->origin, .radius = radius, });
}

BOOL Wow_TerrainChunkInRange(wowAdtChunk_t const *chunk) {
    VECTOR3 camera_origin;
    VECTOR3 center;
    VECTOR3 extents;
    float dx = 0.0f;
    float dy = 0.0f;
    float max_distance_sq;
    float radius;

    if (!chunk) {
        return false;
    }

    camera_origin = tr.viewDef.camerastate[0].origin;
    if (camera_origin.x < chunk->bounds.min.x) {
        dx = chunk->bounds.min.x - camera_origin.x;
    } else if (camera_origin.x > chunk->bounds.max.x) {
        dx = camera_origin.x - chunk->bounds.max.x;
    }
    if (camera_origin.y < chunk->bounds.min.y) {
        dy = chunk->bounds.min.y - camera_origin.y;
    } else if (camera_origin.y > chunk->bounds.max.y) {
        dy = camera_origin.y - chunk->bounds.max.y;
    }

    max_distance_sq = WOW_TERRAIN_DRAW_DISTANCE * WOW_TERRAIN_DRAW_DISTANCE;
    if (dx * dx + dy * dy > max_distance_sq) {
        return false;
    }

    center = (VECTOR3){
        (chunk->bounds.min.x + chunk->bounds.max.x) * 0.5f,
        (chunk->bounds.min.y + chunk->bounds.max.y) * 0.5f,
        (chunk->bounds.min.z + chunk->bounds.max.z) * 0.5f,
    };
    extents = (VECTOR3){
        chunk->bounds.max.x - center.x,
        chunk->bounds.max.y - center.y,
        chunk->bounds.max.z - center.z,
    };
    radius = MAX(Vector3_len(&extents), WOW_ADT_CHUNK_SIZE);
    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){ .center = center, .radius = radius, });
}

BOOL Wow_WmoGroupInView(wowWmoGroup_t const *group, LPCMATRIX4 matrix) {
    VECTOR3 center;
    VECTOR3 extents;
    VECTOR3 world_center;
    VECTOR3 delta;
    float radius;

    if (!group || !matrix || !group->has_bounds) {
        return true;
    }

    center = (VECTOR3){
        (group->bounds.min.x + group->bounds.max.x) * 0.5f,
        (group->bounds.min.y + group->bounds.max.y) * 0.5f,
        (group->bounds.min.z + group->bounds.max.z) * 0.5f,
    };
    extents = (VECTOR3){
        group->bounds.max.x - center.x,
        group->bounds.max.y - center.y,
        group->bounds.max.z - center.z,
    };
    world_center = Matrix4_multiply_vector3(matrix, &center);
    radius = Vector3_len(&extents) * 2.0f;

    /* Fully fogged WMO geometry cannot contribute; preserve large buildings that cross the fog boundary. */
    delta = Vector3_sub(&world_center, &tr.viewDef.camerastate[0].origin);
    if (Vector3_len(&delta) - radius > tr.viewDef.fogEnd) return false;

    return Frustum_ContainsSphere(&tr.viewDef.frustum, &(SPHERE3){ .center = world_center, .radius = MAX(radius, 16.0f), });
}

/* Returns true if the world-space point lies within any interior group's AABB (in world space).
   Each group's local AABB is conservatively transformed to world space by expanding over all 8
   corners; this is correct for axis-aligned groups and conservative for rotated ones. */
BOOL Wow_WmoContainsPoint(wowWmoModel_t const *model, LPCMATRIX4 matrix, VECTOR3 point) {
    if (!model || !matrix || !model->portals) return false;
    FOR_LOOP(i, model->num_groups) {
        wowWmoGroup_t const *group = &model->groups[i];
        BOX3 world;
        float cx[2], cy[2], cz[2];
        if (!group->has_bounds) continue;
        /* Only interior groups contribute to containment */
        if (!group->portal_count) continue;
        world = Wow_EmptyBounds();
        cx[0] = group->bounds.min.x; cx[1] = group->bounds.max.x;
        cy[0] = group->bounds.min.y; cy[1] = group->bounds.max.y;
        cz[0] = group->bounds.min.z; cz[1] = group->bounds.max.z;
        FOR_LOOP(ix, 2) FOR_LOOP(iy, 2) FOR_LOOP(iz, 2) {
            VECTOR3 corner = { cx[ix], cy[iy], cz[iz] };
            VECTOR3 w = Matrix4_multiply_vector3(matrix, &corner);
            Wow_AddBoundsPoint(&world, &w);
        }
        if (point.x < world.min.x || point.x > world.max.x ||
            point.y < world.min.y || point.y > world.max.y ||
            point.z < world.min.z || point.z > world.max.z)
            continue;
        return true;
    }
    return false;
}

void Wow_BindWorldTexture(LPCTEXTURE texture, DWORD unit, LPCTEXTURE bound[5], LPDWORD binds) {
    texture = texture ? texture : tr.texture[TEX_WHITE];
    if (unit >= 5 || bound[unit] == texture) {
        return;
    }
    R_BindTexture(texture, unit);
    bound[unit] = texture;
    if (binds) {
        (*binds)++;
    }
}
