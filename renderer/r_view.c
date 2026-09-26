#include "r_local.h"
#include "r_game.h"

/* UI scenes borrow the renderer view; retaining a portrait hid the later minimap camera outline. */
void R_RenderFrame(viewDef_t const *viewDef) {
    viewDef_t saved = tr.viewDef;
    tr.viewDef = *viewDef;
    /* Shadows used to darken the fog itself; give their colour blend the same scene fog as terrain. */
    tr.shader_shadowSplat.state.fogEnable = viewDef->fogEnable && !(viewDef->rdflags & RDF_NOWORLDMODEL);
    tr.shader_shadowSplat.state.fogColor = viewDef->fogColor;
    tr.shader_shadowSplat.state.fogParams = (VECTOR2){ viewDef->fogStart, viewDef->fogEnd };

    /* UI scene and portrait callers zero-initialise their viewDef, leaving
     * time == 0, which would freeze model animations (MDLX_SetEntityAnimationFrame
     * uses tr.viewDef.time to compute the current frame).  Fall back to the
     * wall clock so the menu background and portraits animate. */
    if (tr.viewDef.time == 0) {
        tr.viewDef.time = SDL_GetTicks();
    }
    R_SetupEnvironmentLighting();
    R_ConformGroundSurfaces(&tr.viewDef);

    if (!tr.viewDef.scissor.w && !tr.viewDef.scissor.h) {
        tr.viewDef.scissor = (rect_t){0, 0, 1, 1};
    }

    if ((tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) && tr.viewDef.num_entities > 0) {
        renderEntity_t const *entity = &tr.viewDef.entities[0];
        float aspect = (tr.viewDef.viewport.w * tr.drawableSize.width) > 0.0f
            ? (tr.viewDef.viewport.w * tr.drawableSize.width) / (tr.viewDef.viewport.h * tr.drawableSize.height)
            : 1.0f;
        if (!R_ExtractEntityCamera(entity, aspect, &tr.viewDef)) {
            Matrix4_identity(&tr.viewDef.viewProjectionMatrix);
            Matrix4_identity(&tr.viewDef.textureMatrix);
            Matrix4_identity(&tr.viewDef.lightMatrix);
        }
        Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);
        R_SetupViewport(&tr.viewDef.viewport);
        R_SetupScissor(&tr.viewDef.scissor);
        R_SetupGL(false);
        R_Call(glClear, GL_DEPTH_BUFFER_BIT);
        R_DrawEntities();
        R_RevertSettings();
        if (viewDef->rdflags & RDF_NOWORLDMODEL) tr.viewDef = saved;
        return;
    }

    Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);

    R_RenderFogOfWar();
    R_Call(glActiveTexture, GL_TEXTURE2);
    R_Call(glBindTexture, GL_TEXTURE_2D, R_GetFogOfWarTexture());
    R_Call(glActiveTexture, GL_TEXTURE0);
#ifdef USE_SHADOWMAPS
    /* Layout-provided model cameras have no world shadow pass either. */
    if (!(tr.viewDef.rdflags & (RDF_USE_ENTITY_CAMERA | RDF_NOWORLDMODEL))) {
        R_RenderShadowMap();
    }
#endif
    R_RenderView();
    if (viewDef->rdflags & RDF_NOWORLDMODEL) tr.viewDef = saved;
}
