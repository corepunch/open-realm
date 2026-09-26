#include "s_skills.h"

static void raven_forward_end(edict_t *unit);
static void raven_reverse_end(edict_t *unit);

static umove_t raven_morph = { "morph", NULL, raven_forward_end, CAbilityRavenForm };
static umove_t raven_morph_alt = { "morph alternate", NULL, raven_reverse_end, CAbilityRavenForm };
typedef struct ravenform_s {
    AbilityData_t const *ability;
    uint32_t base_type;
    uint32_t raven_type;
} ravenForm_t;



static uint32_t const raven_codes[] = {
    MAKEFOURCC('A','m','r','f'), /* Medivh Crow Form */
    MAKEFOURCC('A','r','a','v'), /* Druid Storm Crow Form */
};

/* Play the authored morph sequence after a form rebind; the reverse transform
 * uses the source form's Alternate sequence before returning to ordinary stand. */
static void raven_play_morph(edict_t *unit, bool raven_form) {
    animation_t const *morph;

    if (raven_form) G_AddUnitAnimationProperties(unit, "alternate,alternateex", false);
    /* Keep the morph as the active move, rather than only replacing the
     * animation pointer; the ordinary stand move can otherwise reassert its
     * animation while the geoset alpha track is revealing the new form. */
    unit_setmove(unit, raven_form ? &raven_morph : &raven_morph_alt);
    morph = unit->animation;
    if (raven_form) {
        G_AddUnitAnimationProperties(unit, "alternateex", true);
        /* Keep the untagged Morph selected after restoring the form tag;
         * later cinematic orders must immediately use crow animations. */
        unit->animation = morph;
    }
    if (!morph) {
        fprintf(stderr, "WC3_RAVEN missing morph animation order=%s class=%.4s\n",
                raven_form ? "ravenform" : "unravenform", (cstring_t)&unit->class_id);
        /* TODO: Custom models may omit morph clips; complete the form after reporting the missing sequence. */
        (raven_form ? raven_forward_end : raven_reverse_end)(unit);
    }
    if (unit->animation) unit->s.frame = unit->animation->interval[0];
}

/* Takeoff is independent of the active order once the destination form has been installed. */
static void raven_begin_rise(edict_t *unit) {
    if (unit->raven.rise_state != RAVEN_RISE_AFTER_MORPH) return;
    if (unit->raven.rise_duration > 0.0f) {
        unit->raven.rise_start = (float)G_Time();
        unit->raven.rise_state = RAVEN_RISE_ACTIVE;
    } else {
        unit->unitinfo.FlyHeight = unit->raven.fly_height;
        unit->raven.rise_state = RAVEN_RISE_NONE;
        M_CheckGround(unit);
        gi.LinkEntity(unit);
    }
}

static void raven_forward_end(edict_t *unit) {
    /* nmdm requires alternateex; alternate would select Medivh's base stand sequence. */
    G_AddUnitAnimationProperties(unit, "alternateex", true);
    raven_begin_rise(unit);
    unit_stand(unit);
}

/* Advance Raven Form's authored takeoff height independently from animation. */
static void raven_update(edict_t *unit) {
    float fraction;
    if (!unit) return;
    /* Prologue01 replaces Morph with Move after 0.5s. Its endfunc then never runs;
     * finish the pending takeoff transition without replacing the new order. */
    if (unit->raven.rise_state == RAVEN_RISE_AFTER_MORPH && unit->currentmove != &raven_morph)
        raven_begin_rise(unit);
    if (unit->raven.rise_state != RAVEN_RISE_ACTIVE) return;
    fraction = ((float)G_Time() - unit->raven.rise_start) / (unit->raven.rise_duration * 1000.0f);
    if (fraction >= 1.0f) {
        unit->unitinfo.FlyHeight = unit->raven.fly_height;
        unit->raven.rise_state = RAVEN_RISE_NONE;
    } else {
        unit->unitinfo.FlyHeight = unit->raven.fly_height * MAX(0.0f, fraction);
    }
    M_CheckGround(unit);
    gi.LinkEntity(unit);
}

static void raven_reverse_end(edict_t *unit) {
    G_AddUnitAnimationProperties(unit, "alternate,alternateex", false);
    unit_stand(unit);
}

/* Preplaced campaign forms resolve through the same authored endpoints as learned abilities. */
static bool raven_form_data(edict_t *unit, ravenForm_t *out) {
    if (!unit || !out) return false;
    FOR_LOOP(i, sizeof(raven_codes) / sizeof(raven_codes[0])) {
        AbilityData_t const *ability = G_AbilityData(raven_codes[i]);
        uint32_t const base_type = ability->level[0].data[0].id;
        uint32_t const raven_type = ability->level[0].unitID;
        ravenForm_t current;

        if (!ability->id || !base_type || !raven_type) continue;
        current = (ravenForm_t){
            .ability = ability,
            .base_type = base_type,
            .raven_type = raven_type,
        };
        /* Endpoint identity is authoritative for preplaced campaign forms,
         * which may not expose the transform ability through the runtime skill
         * list before the map issues unravenform. */
        if (unit->class_id == base_type || unit->class_id == raven_type) {
            *out = current;
            return true;
        }
    }
    return false;
}

/* Rebind in place so script handles and selection survive both morph directions. */
static bool raven_form_order(edict_t *unit, bool raven_form) {
    ravenForm_t form = {0};
    uint32_t target_type;

    if (!raven_form_data(unit, &form)) {
        fprintf(stderr, "Raven Form: no transform endpoints for %.4s\n", (cstring_t)&unit->class_id);
        return false;
    }
    target_type = raven_form ? form.raven_type : form.base_type;

    if (unit->class_id == target_type) {
        return true;
    }
    if (raven_form ? unit->class_id != form.base_type : unit->class_id != form.raven_type) {
        return false;
    }

    G_ClearUnitOrderQueue(unit);
    if (!G_TransformUnitType(unit, target_type)) {
        fprintf(stderr, "Raven Form: cannot transform %.4s to %.4s\n", (cstring_t)&unit->class_id, (cstring_t)&target_type);
        return false;
    }

    unit->raven.rise_state = RAVEN_RISE_NONE;
    if (raven_form) {
        unit->raven.fly_height = unit->unitinfo.FlyHeight;
        unit->raven.rise_duration = form.ability->level[0].data[2].number;
        unit->raven.rise_state = RAVEN_RISE_AFTER_MORPH;
        unit->unitinfo.FlyHeight = 0.0f;
        M_CheckGround(unit);
        gi.LinkEntity(unit);
    }

    unit->goalentity = NULL;
    unit->secondarygoal = NULL;
    move_reset_progress(unit);
    unit_stand(unit);
    raven_play_morph(unit, raven_form);
    return true;
}

/* Orders and the command card share the same ability-owned form transition. */
static bool raven_order(edict_t *unit, cstring_t order) {
    return raven_form_order(unit, !strcmp(order, raven_orders[0]));
}

static bool raven_is_on(edict_t *unit) {
    ravenForm_t form;
    return raven_form_data(unit, &form) && unit->class_id == form.raven_type;
}

static void raven_command(edict_t *ent) {
    FOR_CONTROLLABLE_SELECTED_UNITS(ent->client, unit)
        unit_issueimmediateorder(unit, raven_orders[raven_is_on(unit) ? 1 : 0]);
}

BZ_ABILITY_PROC(CAbilityRavenForm) {
    switch (msg) {
    case A_COMMAND: raven_command(call && call->client ? call->client : ent); return true;
    case A_TOGGLE_ON: return raven_is_on(ent);
    case A_ORDER: return call && call->order && raven_order(ent, call->order);
    case A_UPDATE: raven_update(ent); return true;
    default: return false;
    }
}
