#ifndef WOW_G_TAXI_H
#define WOW_G_TAXI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The graph owns its decoded records and remains valid after the DBC buffers
 * are released. IDs are DBC IDs, never array indices. */
typedef struct { uint32_t id, map; float x, y, z; char name[128]; } wowTaxiNode_t;
typedef struct { uint32_t id, from, to, price; size_t first_point, point_count; } wowTaxiPath_t;
typedef struct { uint32_t path, index, map; float x, y, z; uint32_t flags, delay; } wowTaxiPoint_t;
typedef struct {
    wowTaxiNode_t *nodes; size_t node_count;
    wowTaxiPath_t *paths; size_t path_count;
    wowTaxiPoint_t *points; size_t point_count;
} wowTaxiGraph_t;
typedef struct { uint32_t paths[128]; size_t count; uint64_t price; } wowTaxiRoute_t;

bool WowTaxi_Load(wowTaxiGraph_t *graph, const uint8_t *nodes, size_t nodes_size,
                  const uint8_t *paths, size_t paths_size,
                  const uint8_t *points, size_t points_size);
void WowTaxi_Free(wowTaxiGraph_t *graph);
const wowTaxiNode_t *WowTaxi_FindNode(const wowTaxiGraph_t *graph, uint32_t id);
/* known is a bitset indexed by DBC node ID. */
bool WowTaxi_Route(const wowTaxiGraph_t *graph, uint32_t from, uint32_t to,
                   const uint8_t *known, size_t known_size, wowTaxiRoute_t *route);
#endif
