#ifndef R_SC2MAP_H
#define R_SC2MAP_H

#include "renderer/r_game.h"
#include "games/starcraft-2/common/sc2_map.h"

void      R_SC2ShutdownShaders(void);
void      R_SC2RegisterMap(LPCSTR mapFileName);
void      R_SC2DrawWorld(void);
bool      R_SC2TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output);
FLOAT     R_SC2GetHeightAtPoint(FLOAT x, FLOAT y);
VECTOR2   R_SC2WorldSize(void);

#endif
