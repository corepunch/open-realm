/*
 * g_doodad.c — Scripted animation control for map doodads.
 *
 * Doodads are static scenery entities rather than destructables: JASS cannot
 * hold a doodad handle, so Warcraft exposes area-based SetDoodadAnimation*
 * natives instead.  A scripted animation temporarily gives the matching
 * scenery entity a simulation animation clock; it does not change pathing or
 * convert the doodad into a destructable.
 */
#include "g_local.h"
#include <float.h>

static umove_t doodad_scripted_move = { .animation = "stand", .think = NULL, .endfunc = G_DoodadAnimationEnd };

/* Identify static scenery that has an authored doodad row for scripted animation. */
bool G_IsDoodad(edict_t const *ent) {
    return ent && ent->inuse && ent->class_id && (ent->svflags & SVF_STATIC_SCENERY) &&
        ent->data.Doodads && ent->data.Doodads->id == ent->class_id;
}

/* Hold a non-looping scripted doodad animation on its authored final frame. */
void G_DoodadAnimationEnd(edict_t *ent) {
    animation_t const *anim;

    if (!ent || !(anim = ent->animation)) return;
    if (!(anim->flags & 1)) return;

    /* Non-looping doodad animations persist on their authored last frame.
     * Prologue01's LOo2 banner uses this to leave the flag gone after its
     * Death sequence has emitted the smoke puff. */
    if (anim->interval[1] > anim->interval[0])
        ent->s.frame = anim->interval[1] - 1;
    ent->aiflags |= AI_HOLD_FRAME;
}

/* Apply a named scripted animation without changing the doodad footprint. */
bool G_DoodadSetAnimation(edict_t *ent, cstring_t anim_name, bool random_animation) {
    animation_t const *anim;

    if (!G_IsDoodad(ent) || !anim_name || !*anim_name) return false;

    /* Retail exposes these two special animation names for doodads.  They are
     * presentation-only and deliberately do not affect the doodad footprint. */
    if (!strcasecmp(anim_name, "hide")) {
        ent->s.renderfx |= RF_HIDDEN;
        return true;
    }
    if (!strcasecmp(anim_name, "show")) {
        ent->s.renderfx &= ~RF_HIDDEN;
        return true;
    }

    anim = G_GetAnimationVariant(ent->s.model, anim_name, random_animation);
    if (!anim) return false;

    /* Persist the resolved sequence name so an animRandom choice survives save/load. */
    strlcpy(ent->animation_request, anim->name, sizeof(ent->animation_request));
    ent->animation = anim;
    ent->currentmove = &doodad_scripted_move;
    ent->aiflags &= ~AI_HOLD_FRAME;
    ent->s.frame = anim->interval[0];
    ent->think = monster_think;
    return true;
}

/* Apply a scripted doodad animation to matching scenery in a circular area. */
uint32_t G_SetDoodadAnimationRadius(doodadAnimationRadiusParams_t const *params) {
    edict_t *nearest = NULL;
    float nearest_distance_sq = FLT_MAX;
    uint32_t changed = 0;

    if (!params || params->radius < 0.0f || !params->doodad_id || !params->anim_name || !*params->anim_name)
        return 0;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        float dx, dy, distance_sq;

        if (!G_IsDoodad(ent) || ent->class_id != params->doodad_id) continue;
        dx = ent->s.origin.x - params->x;
        dy = ent->s.origin.y - params->y;
        distance_sq = dx * dx + dy * dy;
        if (distance_sq > params->radius * params->radius) continue;

        if (params->nearest_only) {
            if (!nearest || distance_sq < nearest_distance_sq) {
                nearest = ent;
                nearest_distance_sq = distance_sq;
            }
            continue;
        }
        if (G_DoodadSetAnimation(ent, params->anim_name, params->random_animation)) changed++;
    }

    if (nearest && G_DoodadSetAnimation(nearest, params->anim_name, params->random_animation)) changed++;
    return changed;
}

uint32_t G_SetDoodadAnimationRect(box2_t const *rect, uint32_t doodad_id,
                               cstring_t anim_name, bool random_animation) {
    uint32_t changed = 0;

    if (!rect || !doodad_id || !anim_name || !*anim_name) return 0;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;

        if (!G_IsDoodad(ent) || ent->class_id != doodad_id ||
            !Box2_containsPoint(rect, &ent->s.origin2))
            continue;
        if (G_DoodadSetAnimation(ent, anim_name, random_animation)) changed++;
    }
    return changed;
}
