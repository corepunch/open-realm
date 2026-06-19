#ifndef r_game_h
#define r_game_h

#include "r_local.h"

void R_GameLoadAssets(void);
void R_GameInit(void);
void R_GameShutdown(void);
void R_GameSetupTextureMatrix(void);

void R_GameRegisterMap(LPCSTR mapFileName);
void R_GameDrawWorld(void);
void R_GameDrawTerrainShadows(void);
void R_GameDrawAlphaSurfaces(void);
bool R_GameTraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
FLOAT R_GameGetHeightAtPoint(FLOAT x, FLOAT y);
VECTOR2 R_GameWorldSize(void);

LPMODEL R_GameLoadModel(LPCSTR modelFilename);
void R_GameReleaseModel(LPMODEL model);
void R_GameRenderModel(renderEntity_t const *entity);
bool R_GameTraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance);
bool R_GameGetModelInfo(LPMODEL model, LPMODELINFO info);
bool R_GameEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix);
bool R_GameRenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin);

void R_GameDrawPortrait(LPCPORTRAITDEF params);
void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);

#endif
