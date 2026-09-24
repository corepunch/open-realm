#ifndef SC2_COORDS_H
#define SC2_COORDS_H

#include "common/shared.h"

/* Native -Y forward, +Z up -> canonical +X forward, +Z up. */
static MATRIX4 const sc2_model_basis = { .v = { 0,1,0,0, -1,0,0,0, 0,0,1,0, 0,0,0,1 } };

/* Placed-object Angle already orients native M3 geometry. Decode it to actor heading
 * so the mandatory model basis preserves that authored transform, including scenery. */
static FLOAT SC2_PlacementHeading(FLOAT angle) { return angle - (FLOAT)M_PI / 2; }
static FLOAT SC2_FacingRadians(FLOAT degrees) { return (FLOAT)DEG2RAD(degrees); }

/* Map/Galaxy yaw selects the eye side of the target; using it as view rotation put the camera
 * across the map (TRaynor01 bridge). Orbit identity looks down -Z, so tilt is pitch minus 90. */
static inline VECTOR3 SC2_EulerFromCamera(FLOAT pitch, FLOAT yaw) {
    return (VECTOR3){ pitch - 90.0f, 0.0f, yaw - 180.0f };
}
static inline VECTOR3 SC2_CameraFromEuler(LPCVECTOR3 euler, FLOAT height) {
    return (VECTOR3){ euler->x + 90.0f, euler->z + 180.0f, height };
}

#endif
