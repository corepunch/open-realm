#ifndef BZ_WC3_PATHING_LIMITS_H
#define BZ_WC3_PATHING_LIMITS_H

#define BZ_WC3_FINE_WORK 2048 // queue attempts/request; retain the engine's bounded synchronous routing budget
#define BZ_WC3_FINE_NODES (8 * BZ_WC3_FINE_WORK + 2) // nodes; eight discoveries/pop plus start and goal
#define BZ_WC3_FINE_HASH 32768 // slots; power of two, roughly half full at maximum node capacity

#endif
