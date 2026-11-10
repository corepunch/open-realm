#ifndef SC2_COORDS_H
#define SC2_COORDS_H

#include "common/shared.h"

/* Native -Y forward, +Z up -> canonical +X forward, +Z up. World actors only. */
static mat4_t const sc2_model_basis = { .v = { 0,1,0,0, -1,0,0,0, 0,0,1,0, 0,0,0,1 } };
/* Layout cameras and portrait framing are authored in native M3 axes. */
static mat4_t const sc2_native_basis = { .v = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } };

/* Placed-object Angle already orients native M3 geometry. Decode it to actor heading
 * so the mandatory model basis preserves that authored transform, including scenery. */
static inline float SC2_PlacementHeading(float angle) { return angle - (float)M_PI / 2; }
static inline float SC2_FacingRadians(float degrees) { return (float)DEG2RAD(degrees); }

/* Map/Galaxy yaw selects the eye side of the target; using it as view rotation put the camera
 * across the map (TRaynor01 bridge). Orbit identity looks down -Z, so tilt is pitch minus 90. */
static inline vec3_t SC2_EulerFromCamera(float pitch, float yaw) {
    return (vec3_t){ pitch - 90.0f, 0.0f, yaw - 180.0f };
}
static inline vec3_t SC2_CameraFromEuler(vec3_t const *euler, float height) {
    return (vec3_t){ euler->x + 90.0f, euler->z + 180.0f, height };
}

/* Encode a turret world-space angle (radians) into hover_value for network transport.
 * hover_value == 0 means no independent turret; values 1-65536 cover the full circle. */
static inline uint32_t SC2_PackTurretAngle(float angle) {
    float norm = fmodf(angle, 2.0f * (float)M_PI);
    if (norm < 0.0f) norm += 2.0f * (float)M_PI;
    return (uint32_t)(norm / (2.0f * (float)M_PI) * 65535.0f) + 1; /* 1..65535 */
}
static inline float SC2_UnpackTurretAngle(uint32_t hover) {
    return (float)(hover - 1) / 65535.0f * 2.0f * (float)M_PI;
}

#endif
