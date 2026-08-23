#ifndef r_game_h
#define r_game_h

#include "r_local.h"

void R_GameLoadAssets(void);
void R_GameInit(void);
void R_GameShutdown(void);
void R_GameSetupTextureMatrix(void);

/* Draw the game's minimap into the given UI-space rect. Each game owns its content. */
void R_GameDrawMinimap(LPCRECT screen);

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
void R_GameRenderModelInstanced(LPCMODEL model, LPCINSTANCEBUFFER instances, DWORD flags);
bool R_GameModelCanStaticInstance(LPCMODEL model);
bool R_GameTraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance);
bool R_GameGetModelInfo(LPMODEL model, LPMODELINFO info);
bool R_GameEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix);
bool R_GameRenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin);
/* Selection-circle radius for the shared entity path; per-game tuning (e.g. WoW's fractional-creature clamp). */
FLOAT R_GameSelectionRadius(renderEntity_t const *entity);
FLOAT R_GameEntityHeight(renderEntity_t const *entity);

bool R_GameExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef);
bool R_GameSetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);

#endif
