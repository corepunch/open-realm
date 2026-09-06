#ifndef r_game_h
#define r_game_h

#include "r_local.h"

typedef struct {
	DWORD id;
	LPCSTR dir;
	LPCSTR file;
} w3TerrainArt_t;

typedef struct {
	DWORD id;
	LPCSTR texDir;
	LPCSTR texFile;
	DWORD groundTile;
	DWORD upperTile;
	LPCSTR rampModelDir;
	LPCSTR cliffModelDir;
} w3CliffType_t;

void R_LoadAssets(void);
void R_Init(void);
void R_Shutdown(void);
void R_SetupTextureMatrix(void);

/* Draw the game's minimap into the given UI-space rect. Each game owns its content. */
void R_DrawMinimap(LPCRECT screen);

void R_RegisterMap(LPCSTR mapFileName);
void R_SetupEnvironmentLighting(void);
void R_DrawWorld(void);
void R_DrawTerrainShadows(void);
void R_DrawAlphaSurfaces(void);
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
FLOAT R_GetHeightAtPoint(FLOAT x, FLOAT y);
VECTOR2 R_WorldSize(void);

LPMODEL R_LoadModel(LPCSTR modelFilename);
void R_ReleaseModel(LPMODEL model);
void R_RenderModel(renderEntity_t const *entity);
void R_RenderModelInstanced(LPCMODEL model, LPCINSTANCEBUFFER instances, DWORD flags);
bool R_ModelCanStaticInstance(LPCMODEL model);
bool R_TraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance);
bool R_GetModelInfo(LPMODEL model, LPMODELINFO info);
bool R_EntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix);
#ifndef USE_SHADOWMAPS
bool R_RenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin);
#endif
/* Selection-circle radius for the shared entity path; per-game tuning (e.g. WoW's fractional-creature clamp). */
FLOAT R_SelectionRadius(renderEntity_t const *entity);
FLOAT R_EntityHeight(renderEntity_t const *entity);
BOOL R_EntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out);
BOOL R_EntityAttachmentPosition(renderEntity_t const *entity, LPCSTR prefix, LPVECTOR3 out);

bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef);
bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
bool R_GetModelAnimationDuration(LPCMODEL model, LPCSTR anim, LPDWORD duration);
void R_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);
bool R_DrawCursor(float x, float y, COLOR32 tint);
w3TerrainArt_t const *R_TerrainArt(DWORD id);
w3CliffType_t const *R_CliffType(DWORD id);

#endif
