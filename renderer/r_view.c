#include "r_local.h"
#include "r_game.h"
#include <limits.h>

typedef struct {
    uintptr_t instance_id;
    particleScene_t particles;
} entityCameraParticleState_t;

static entityCameraParticleState_t *entity_camera_particle_states;
static size_t entity_camera_particle_count, entity_camera_particle_capacity;

static particleScene_t *R_EntityCameraParticleScene(uintptr_t instance_id) {
    FOR_LOOP(i, entity_camera_particle_count) {
        if (entity_camera_particle_states[i].instance_id == instance_id)
            return &entity_camera_particle_states[i].particles;
    }
    if (entity_camera_particle_count == entity_camera_particle_capacity) {
        size_t capacity = entity_camera_particle_capacity ? entity_camera_particle_capacity * 2 : 16;
        if (capacity < entity_camera_particle_capacity || capacity > (size_t)LONG_MAX / sizeof(*entity_camera_particle_states)) {
            fprintf(stderr, "Renderer: entity-camera particle-state capacity overflow\n");
            return NULL;
        }
        entityCameraParticleState_t *states = ri.MemAlloc
            ? ri.MemAlloc((long)(capacity * sizeof(*states))) : NULL;
        if (!states) {
            fprintf(stderr, "Renderer: failed to grow entity-camera particle states to %zu entries\n", capacity);
            return NULL;
        }
        memset(states, 0, capacity * sizeof(*states));
        if (entity_camera_particle_count)
            memcpy(states, entity_camera_particle_states,
                   entity_camera_particle_count * sizeof(*states));
        if (entity_camera_particle_states) {
            if (ri.MemFree) ri.MemFree(entity_camera_particle_states);
            else fprintf(stderr, "Renderer: cannot free replaced entity-camera particle-state table without MemFree\n");
        }
        entity_camera_particle_states = states;
        entity_camera_particle_capacity = capacity;
    }
    entityCameraParticleState_t *state = entity_camera_particle_states + entity_camera_particle_count++;
    state->instance_id = instance_id;
    return &state->particles;
}

void R_ClearEntityCameraParticleScenes(void) {
    FOR_LOOP(i, entity_camera_particle_count)
        R_ClearParticleScene(&entity_camera_particle_states[i].particles);
    if (entity_camera_particle_states) {
        if (ri.MemFree) ri.MemFree(entity_camera_particle_states);
        else fprintf(stderr, "Renderer: cannot free entity-camera particle-state table without MemFree\n");
    }
    entity_camera_particle_states = NULL;
    entity_camera_particle_count = entity_camera_particle_capacity = 0;
}

void R_ReleaseEntityCameraEvents(uintptr_t instance_id) {
    FOR_LOOP(i, entity_camera_particle_count) {
        if (entity_camera_particle_states[i].instance_id != instance_id) continue;
        R_ClearParticleScene(&entity_camera_particle_states[i].particles);
        entity_camera_particle_count--;
        if (i != entity_camera_particle_count)
            entity_camera_particle_states[i] = entity_camera_particle_states[entity_camera_particle_count];
        break;
    }
    R_ReleaseGameEntityCameraEvents(instance_id);
}

/* UI scenes borrow the renderer view; retaining a portrait hid the later minimap camera outline. */
void R_RenderFrame(viewDef_t const *viewDef) {
    viewDef_t saved = tr.viewDef;
    tr.viewDef = *viewDef;
    /* Shadows used to darken the fog itself; give their colour blend the same scene fog as terrain. */
    tr.shader_shadowSplat.state.fogEnable = viewDef->fogEnable && !(viewDef->rdflags & RDF_NOWORLDMODEL);
    tr.shader_shadowSplat.state.fogColor = viewDef->fogColor;
    tr.shader_shadowSplat.state.fogParams = (vec2_t){ viewDef->fogStart, viewDef->fogEnd };

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
        particleScene_t temporary_particles = {0};
        bool const isolated_particles = (tr.viewDef.rdflags & RDF_ISOLATED_PARTICLES) != 0;
        particleScene_t *particle_scene = isolated_particles
            ? R_EntityCameraParticleScene(entity->instance_id) : NULL;
        if (!particle_scene) particle_scene = &temporary_particles;
        bool const temporary_particle_scene = particle_scene == &temporary_particles;
        cparticle_t *previous_particles = R_BeginParticleScene(particle_scene);
        R_DrawEntityCameraEventSpawns(entity->model, entity->instance_id);
        R_DrawEntities();
        if (isolated_particles && !(tr.viewDef.rdflags & RDF_NOPARTICLES))
            R_DrawParticles();
        R_EndParticleScene(particle_scene, previous_particles);
        if (temporary_particle_scene || (tr.viewDef.rdflags & RDF_NOPARTICLES))
            R_ClearParticleScene(particle_scene);
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
    particleScene_t temporary_particles = {0};
    bool const retain_camera_particles = (tr.viewDef.rdflags & RDF_ISOLATED_PARTICLES) != 0;
    bool const discard_suppressed_particles =
        (tr.viewDef.rdflags & (RDF_NOWORLDMODEL | RDF_NOPARTICLES)) ==
        (RDF_NOWORLDMODEL | RDF_NOPARTICLES);
    bool const manage_camera_particles = tr.viewDef.num_entities > 0 &&
        (retain_camera_particles || discard_suppressed_particles);
    particleScene_t *particle_scene = NULL;
    bool temporary_particle_scene = false;
    cparticle_t *previous_particles = NULL;
    if (manage_camera_particles) {
        particle_scene = retain_camera_particles
            ? R_EntityCameraParticleScene(tr.viewDef.entities[0].instance_id) : NULL;
        if (!particle_scene) {
            particle_scene = &temporary_particles;
            temporary_particle_scene = true;
        }
        previous_particles = R_BeginParticleScene(particle_scene);
    }
    R_RenderView();
    if (manage_camera_particles) {
        R_EndParticleScene(particle_scene, previous_particles);
        if (temporary_particle_scene || (tr.viewDef.rdflags & RDF_NOPARTICLES))
            R_ClearParticleScene(particle_scene);
    }
    if (viewDef->rdflags & RDF_NOWORLDMODEL) tr.viewDef = saved;
}
