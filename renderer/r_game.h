#ifndef r_game_h
#define r_game_h

#include "r_local.h"

/* Renderer-derived pose; source/wire Euler fields keep their authored layout. */
typedef struct {
    vector3_t origin;
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
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, vector3_t * point);
float R_GetHeightAtPoint(float x, float y);
float R_GetCameraHeightAtPoint(float x, float y);
bool R_CameraUsesTerrainHeight(void);
vector2_t R_WorldSize(void);

model_t * R_LoadModel(cstring_t modelFilename);
void R_ReleaseModel(model_t * model);
void R_UpdateEntityPresentation(renderEntity_t const *entity);
void R_RenderModel(renderEntity_t const *entity);
void R_RenderModelInstanced(model_t const * model, instanceBuffer_t const * instances, uint32_t flags);
bool R_ModelCanStaticInstance(model_t const * model);
bool R_TraceModel(renderEntity_t const *entity, line3_t const * line, float * distance);
bool R_GetEntityBounds(renderEntity_t const *entity, box3_t * bounds);
bool R_GetModelInfo(model_t * model, modelInfo_t * info);
/* Mandatory for every game, including identity conversions. Returns native-model -> actor basis. */
matrix4_t const * R_EntityPose(renderEntity_t const *entity, modelPose_t *pose);
#ifndef USE_SHADOWMAPS
bool R_RenderShadow(renderEntity_t const *entity, vector2_t const * origin);
#endif
/* Selection-circle radius for the shared entity path; per-game tuning (e.g. WoW's fractional-creature clamp). */
float R_SelectionRadius(renderEntity_t const *entity);
float R_EntityHeight(renderEntity_t const *entity);
bool R_EntityOverheadPosition(renderEntity_t const *entity, vector3_t * out);
bool R_EntityAttachmentPosition(renderEntity_t const *entity, cstring_t prefix, vector3_t * out);

bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef);
bool R_SetEntityAnimFrame(model_t const * model, cstring_t anim, renderEntity_t *entity);
void R_DrawSprite(drawSprite_t const *sprite);
bool R_DrawCursor(float x, float y, color32_t tint);
w3TerrainArt_t const *R_TerrainArt(uint32_t id);
w3CliffType_t const *R_CliffType(uint32_t id);

#endif
