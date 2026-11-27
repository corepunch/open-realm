#ifndef WOW_R_M2_PORTRAIT_H
#define WOW_R_M2_PORTRAIT_H

#include "common/shared.h"
#include <math.h>

/* Frame the visible geometry when a model does not supply a camera. Coordinates
 * remain in model space so the caller can apply the entity transform once. */
static void M2_FallbackPortraitCamera(box3_t const *bounds, bool character,
                                      vec3_t const *head, float aspect, float fov,
                                      vec3_t *eye, vec3_t *target,
                                      float *znear, float *zfar) {
    float width = bounds->max.x - bounds->min.x;
    float depth = bounds->max.y - bounds->min.y;
    float height = bounds->max.z - bounds->min.z;
    if (!isfinite(width) || !isfinite(depth) || !isfinite(height) ||
        width < 0.001f || depth < 0.001f || height < 0.001f) {
        *target = (vec3_t){0.0f, 0.0f, 0.0f};
        *eye = (vec3_t){0.0f, -4.0f, 0.0f};
        *znear = 0.05f;
        *zfar = 100.0f;
        return;
    }
    if (!isfinite(aspect) || aspect <= 0.0f) aspect = 1.0f;
    if (!isfinite(fov) || fov <= 1.0f || fov >= 179.0f) fov = 35.0f;
    *target = (vec3_t){ (bounds->min.x + bounds->max.x) * 0.5f,
                       (bounds->min.y + bounds->max.y) * 0.5f,
                       (bounds->min.z + bounds->max.z) * 0.5f };
    float frame_height = height;
    float frame_width = width;
    if (character) {
        /* The helm attachment follows the posed head when present. Its height
         * is adjusted toward the face and bounded by the visible model. */
        if (head && isfinite(head->x) && isfinite(head->y) && isfinite(head->z) &&
            head->z >= bounds->min.z && head->z <= bounds->max.z) {
            target->z = head->z - height * 0.06f;
        } else {
            target->z = bounds->min.z + height * 0.80f;
        }
        frame_height = height * 0.42f;
        frame_width = width * 0.55f;
    }
    float half_height = MAX(frame_height * 0.5f, frame_width * 0.5f / aspect);
    float distance = half_height / tanf(fov * (float)M_PI / 360.0f) + depth * 0.5f;
    distance *= 1.12f;
    *eye = (vec3_t){ target->x, target->y - distance, target->z };
    *znear = MAX(distance * 0.01f, 0.001f);
    *zfar = distance + MAX(height, MAX(width, depth)) * 2.0f;
}

#endif
