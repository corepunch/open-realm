#ifndef WC3_COORDS_H
#define WC3_COORDS_H

#include "common/shared.h"

/* Native +X forward, +Z up -> canonical +X forward, +Z up. */
static mat4_t const wc3_model_basis = { .v = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 } };

#endif
