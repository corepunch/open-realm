#ifndef r_trail_h
#define r_trail_h

#include "common/shared.h"

/* Engine trail: game-agnostic spine history plus strip synthesis. Q3 splits
   transient effects the same way: the game owns emitter state and materials
   (cgame localEntities), the renderer turns endpoints into geometry (RT_BEAM).
   Here the caller owns each trail_t (one per owner+emitter), advances it once
   per drawn frame, and converts trailVert_t into its own vertex format. No
   globals, no GL, no materials, no franchise literals. */

#define TRAIL_MAX_EDGES 48 // max live edges; 20/s * 0.6s * 4 headroom covers stock missile ribbons
#define TRAIL_STALE_MS 250 // gap that drops a trail: owner id reused or long-culled entity reappearing

typedef struct {
    VECTOR3 above, below;
    COLOR32 color; /* per-edge tint: animated emitter colors stay historic instead of repainting the strip */
    float age;
} trailEdge_t;

typedef struct {
    trailEdge_t edges[TRAIL_MAX_EDGES];
    int head, count;
    float acc;   /* edge emission accumulator (rate * dt), clamped before emitting */
    DWORD stamp; /* last now_ms this trail advanced; guards same-frame redraws, detects owner reuse */
} trail_t;

/* Strip vertex in engine space; games add their own normal/skinning/material fields. */
typedef struct {
    VECTOR3 position;
    VECTOR2 uv;
    COLOR32 color;
} trailVert_t;

/* Ages live edges by dt, drops expired ones, and pushes a new edge when the
   emission accumulator crosses 1. now_ms/delta_ms come from the view def:
   a redraw stamped in the same frame gets dt = 0 and only re-emits, while a
   gap past TRAIL_STALE_MS clears the trail (owner id reused). A respawn inside
   the same tick still slips past the gap check. Returns live edge count. */
int R_TrailAdvance(trail_t *trail, VECTOR3 above, VECTOR3 below, COLOR32 color,
                   float lifespan, float rate, float gravity, DWORD now_ms, DWORD delta_ms);

/* One quad (6 verts) per consecutive edge pair. U is age-based (oldest edges
   flow toward the end of the unwrap) so adding or expiring an edge never
   rescales the rest. Returns vertices written. */
DWORD R_TrailStripVerts(trail_t const *trail, float lifespan, DWORD columns, DWORD rows, DWORD slot,
                        trailVert_t *out, DWORD max);

#endif
