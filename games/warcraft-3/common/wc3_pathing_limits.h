#ifndef BZ_WC3_PATHING_LIMITS_H
#define BZ_WC3_PATHING_LIMITS_H

#define BZ_WC3_FINE_WORK 2048 // queue attempts/request; retain the engine's bounded synchronous routing budget
#define BZ_WC3_UNIT_ACC_WORK 400 // original166060 path+86; ordinary owned-path adaptive request
#define BZ_WC3_UNIT_FINE_WORK 700 // original166060 path+84; actual denied next iteration is also charged
#define BZ_WC3_FINE_NODES (8 * BZ_WC3_FINE_WORK + 2) // nodes; eight discoveries/pop plus start and goal
#define BZ_WC3_FINE_HASH 32768 // slots; power of two, roughly half full at maximum node capacity

#endif
