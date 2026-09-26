#ifndef r_game_h
#define r_game_h

#include "r_local.h"

/* Renderer-derived pose; source/wire Euler fields keep their authored layout. */
typedef struct {
    VECTOR3 origin;
    orientation_t angles;
    float scale;
} modelPose_t;

typedef struct {
	uint32_t id;
	cstring_t dir;
	cstring_t file;
} w3TerrainArt_t;

typedef struct {
	uint32_t id;
	cstring_t texDir;
	cstring_t texFile;
	uint32_t groundTile;
	uint32_t upperTile;
	cstring_t rampModelDir;
	cstring_t cliffModelDir;
} w3CliffType_t;

void R_LoadAssets(void);
void R_Init(void);
void R_Shutdown(void);
void R_SetupTextureMatrix(void);

/* Draw the game's minimap into the given UI-space rect. Each game owns its content. */
void R_DrawMinimap(rect_t const * screen, cstring_t map);

void R_RegisterMap(cstring_t mapFileName);
void R_SetupEnvironmentLighting(void);
void R_ConformGroundSurfaces(viewDef_t *viewdef);
void R_DrawWorld(void);
void R_DrawTerrainShadows(void);
void R_DrawAlphaSurfaces(void);
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
float R_GetHeightAtPoint(float x, float y);
float R_GetCameraHeightAtPoint(float x, float y);
bool R_CameraUsesTerrainHeight(void);
VECTOR2 R_WorldSize(void);

LPMODEL R_LoadModel(cstring_t modelFilename);
void R_ReleaseModel(LPMODEL model);
void R_UpdateEntityPresentation(renderEntity_t const *entity);
void R_RenderModel(renderEntity_t const *entity);
void R_RenderModelInstanced(LPCMODEL model, LPCINSTANCEBUFFER instances, uint32_t flags);
bool R_ModelCanStaticInstance(LPCMODEL model);
bool R_TraceModel(renderEntity_t const *entity, LPCLINE3 line, float * distance);
bool R_GetEntityBounds(renderEntity_t const *entity, LPBOX3 bounds);
bool R_GetModelInfo(LPMODEL model, LPMODELINFO info);
/* Mandatory for every game, including identity conversions. Returns native-model -> actor basis. */
LPCMATRIX4 R_EntityPose(renderEntity_t const *entity, modelPose_t *pose);
#ifndef USE_SHADOWMAPS
bool R_RenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin);
#endif
/* Selection-circle radius for the shared entity path; per-game tuning (e.g. WoW's fractional-creature clamp). */
float R_SelectionRadius(renderEntity_t const *entity);
float R_EntityHeight(renderEntity_t const *entity);
bool R_EntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out);
bool R_EntityAttachmentPosition(renderEntity_t const *entity, cstring_t prefix, LPVECTOR3 out);

bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef);
bool R_SetEntityAnimFrame(LPCMODEL model, cstring_t anim, renderEntity_t *entity);
void R_DrawSprite(drawSprite_t const *sprite);
bool R_DrawCursor(float x, float y, COLOR32 tint);
w3TerrainArt_t const *R_TerrainArt(uint32_t id);
w3CliffType_t const *R_CliffType(uint32_t id);

#endif
