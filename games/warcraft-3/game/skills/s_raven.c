#include "s_skills.h"

static void raven_forward_end(edict_t *unit);
static void raven_reverse_end(edict_t *unit);

static umove_t raven_morph = { "morph", NULL, raven_forward_end, CAbilityRavenForm };
static umove_t raven_morph_alt = { "morph alternate", NULL, raven_reverse_end, CAbilityRavenForm };
typedef struct ravenform_s {
    abilityLevel_t const *data;
    uint32_t code;
    uint32_t base_type;
    uint32_t alternate_type;
} morphForm_t;



static uint32_t const morph_codes[] = {
    MAKEFOURCC('A','m','r','f'), /* Medivh Crow Form */
    MAKEFOURCC('A','r','a','v'), /* Druid Storm Crow Form */
    MAKEFOURCC('A','b','r','f'), /* Druid Bear Form */
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
        /* Missing presentation does not bypass the authored transition timer. */
    }
    if (unit->animation) unit->s.frame = unit->animation->interval[0];
}

/* Takeoff is independent of the active order once the destination form has been installed. */
void S_BeginMorphRise(edict_t *unit) {
    assert(unit && unit->raven);
    if (unit->raven->rise_state != RAVEN_RISE_AFTER_MORPH) return;
    if (unit->raven->rise_duration > 0.0f) {
        unit->raven->rise_start = (float)G_Time();
        unit->raven->rise_state = RAVEN_RISE_ACTIVE;
    } else {
        unit->unitinfo.FlyHeight = unit->raven->fly_height;
        unit->raven->rise_state = RAVEN_RISE_NONE;
        M_CheckGround(unit);
        gi.LinkEntity(unit);
    }
}

static void raven_forward_end(edict_t *unit) {
    if(S_HoldMorphAnimation(unit))return;
    /* nmdm requires alternateex; alternate would select Medivh's base stand sequence. */
    G_AddUnitAnimationProperties(unit, "alternateex", true);
    S_BeginMorphRise(unit);
    unit_stand(unit);
}

/* Advance Raven Form's authored takeoff height independently from animation. */
void S_UpdateMorphRise(edict_t *unit) {
    float fraction;
    if (!unit || !unit->raven) return;
    /* Prologue01 replaces Morph with Move after 0.5s. Its endfunc then never runs;
     * finish the pending takeoff transition without replacing the new order. */
    if (unit->raven->rise_state == RAVEN_RISE_AFTER_MORPH && !(unit->morph && unit->morph->transition.active))
        S_BeginMorphRise(unit);
    if (unit->raven->rise_state != RAVEN_RISE_ACTIVE) return;
    fraction = ((float)G_Time() - unit->raven->rise_start) / (unit->raven->rise_duration * 1000.0f);
    if (fraction >= 1.0f) {
        unit->unitinfo.FlyHeight = unit->raven->fly_height;
        unit->raven->rise_state = RAVEN_RISE_NONE;
    } else {
        unit->unitinfo.FlyHeight = unit->raven->fly_height * MAX(0.0f, fraction);
    }
    M_CheckGround(unit);
    gi.LinkEntity(unit);
}

static void raven_reverse_end(edict_t *unit) {
    if(S_HoldMorphAnimation(unit))return;
    G_AddUnitAnimationProperties(unit, "alternate,alternateex", false);
    unit_stand(unit);
}

/* Preplaced campaign forms resolve through the same authored endpoints as learned abilities. */
static bool morph_form_data(edict_t *unit,morphForm_t *out,bool bear) {
    if(!unit || !out)return false;
    uint32_t count;uint32_t const *authored=G_UnitAbilityCodes(unit->data.UnitAbilities,&count);
    abilityProc_t proc=bear ? CAbilityBearForm : CAbilityRavenForm;
    /* Attached aliases precede stock endpoint fallbacks for preplaced forms. */
    uint32_t total=count+MAX_ABILITIES+MAX_HERO_ABILITIES+3;
    FOR_LOOP(i,total) {
        uint32_t code=i<count ? authored[i] : i<count+MAX_ABILITIES ? unit->abilities.added[i-count] :
            i<count+MAX_ABILITIES+MAX_HERO_ABILITIES ? unit->heroabilities[i-count-MAX_ABILITIES].code :
            morph_codes[i-count-MAX_ABILITIES-MAX_HERO_ABILITIES];
        if(!code)continue;
        abilityitem_t item=S_AbilityItem(code);
        if(!item.ability || item.ability->proc!=proc)continue;
        abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
        uint32_t base=data->data[0].id,alternate=data->unitID;
        if(base && alternate && (unit->class_id==base || unit->class_id==alternate)) {
            *out=(morphForm_t){.data=data,.code=code,.base_type=base,.alternate_type=alternate};return true;
        }
    }
    return false;
}
static bool raven_form_data(edict_t *unit,morphForm_t *out) {return morph_form_data(unit,out,false);}

static void bear_morph_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit))unit_stand(unit);
}
static umove_t bear_morph={"morph",NULL,bear_morph_end,CAbilityBearForm};
static umove_t bear_morph_alt={"morph alternate",NULL,bear_morph_end,CAbilityBearForm};

/* Flight profile changes precede the common primary transition. Landing
 * retains the source type; takeoff installs the destination before acquiring. */
bool S_PrepareMorphFlight(edict_t *unit,uint32_t target,abilityLevel_t const *data,
                          bool *takeoff,bool *landing) {
    bool source_flying=S_UnitMovementProfile(unit->data.UnitData)->support==WC3_SUPPORT_FLIGHT;
    bool target_flying=S_UnitMovementProfile(G_UnitData(target))->support==WC3_SUPPORT_FLIGHT;
    *takeoff=!source_flying && target_flying;*landing=source_flying && !target_flying;
    G_ClearUnitOrderQueue(unit);
    if(*takeoff && !G_TransformUnitType(unit,target))return false;
    if(!unit->raven)unit->raven=G_AllocRaven();
    assert(unit->raven);unit->raven->rise_state=RAVEN_RISE_NONE;
    if(*takeoff) {
        unit->raven->fly_height=unit->unitinfo.FlyHeight;
        unit->raven->rise_duration=data->data[2].number;
        unit->raven->rise_state=RAVEN_RISE_AFTER_MORPH;
        unit->unitinfo.FlyHeight=0;
        M_CheckGround(unit);gi.LinkEntity(unit);
    } else if(*landing) {
        /* The fine ground profile is published before the delayed type rebind;
         * the authored coarse class remains the source form's. */
        unit->aiflags&=~AI_FLYING;
        unit->unitinfo.FlyHeight=0;
        M_CheckGround(unit);gi.LinkEntity(unit);
    }
    return true;
}

/* Rebind in place so script handles and selection survive both morph directions. */
static bool morph_form_order(edict_t *unit,bool alternate,bool bear) {
    morphForm_t form={0};
    if(!morph_form_data(unit,&form,bear))return false;
    if(unit->morph && unit->morph->transition.active)return false;
    uint32_t target=alternate ? form.alternate_type : form.base_type;
    if(unit->class_id==target)return true;
    bool takeoff,landing;
    if(!S_PrepareMorphFlight(unit,target,form.data,&takeoff,&landing))return false;
    S_SetMoveGoal(unit,&unit->goalentity,NULL);
    S_SetMoveGoal(unit,&unit->secondarygoal,NULL);
    move_reset_progress(unit);
    if(bear)unit_setmove(unit,alternate ? &bear_morph : &bear_morph_alt);
    else raven_play_morph(unit,alternate);
    unit->current_order_id=G_OrderId((bear ? bear_orders : raven_orders)[alternate ? 0 : 1]);
    float delay=wc3_add(form.data->dur,landing ? form.data->data[2].number : 0);
    S_BeginMorphTransition(unit,form.code,form.base_type,target,!alternate,!takeoff,delay);
    return true;
}

/*54c640 reads the integer setting with default0;54eb80 tests the full
 * result against zero. This policy belongs to ordinary Morph, not Submerge. */
static bool morph_alternate_disables(void) {
    cstring_t value=Stb_IniCacheFind(&game.config.misc,"Misc","MorphAlternateDisable");
    if(!value)return false;
    if(value[0]=='\'') {
        uint32_t code=0;
        for(unsigned i=1;i<=4 && value[i] && value[i]!='\'';i++)code=(code<<8)|(uint8_t)value[i];
        return code!=0;
    }
    return strtol(value,NULL,0)!=0;
}

void S_InitializeConfiguredMorphAvailability(edict_t *unit,abilityCall_t const *call) {
    if(!unit || !call || !call->item || !call->item->code || !morph_alternate_disables())return;
    uint32_t code=call->item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    if(!data->unitID || unit->class_id!=data->unitID)return;
    /* Deferred Bear/Robo-Goblin rebind precedes completion acquire. Raven's takeoff
     * rebind happens before S_BeginMorphTransition and acquires immediately. */
    if(unit->morph && unit->morph->code==code && unit->morph->deferred_rebind && !unit->morph->reverse)return;
    S_SetMorphAbilityDisables(unit,code,true);
}
void S_CompleteConfiguredMorphAvailability(edict_t *unit,abilityCall_t const *call) {
    if(call->enabled)S_SetMorphAbilityDisables(unit,call->item->code,false);
    else if(morph_alternate_disables())S_SetMorphAbilityDisables(unit,call->item->code,true);
}
void S_DetachConfiguredMorphAvailability(edict_t *unit,abilityCall_t const *call) {
    uint32_t code=call->item->code;
    if(!unit || M_IsDead(unit) || G_IsDeferredFree(unit))return;
    /* Keep permanent-form removal's existing no-inverse contract. */
    bool permanent=(wc3_int_bits(wc3_float_bits(S_SpellData(code,S_SpellLevel(unit,code),2)))&8)!=0;
    S_CompleteMorphDetachTransition(unit,code);
    S_RemoveMorphAbility(unit,code);
    if(!permanent)S_SetMorphAbilityDisables(unit,code,false);
}

/* Orders and the command card share the same ability-owned form transition. */
static bool raven_order(edict_t *unit, cstring_t order) {
    return morph_form_order(unit,!strcmp(order,raven_orders[0]),false);
}

static bool raven_is_on(edict_t *unit) {
    morphForm_t form;
    return raven_form_data(unit, &form) && unit->class_id == form.alternate_type;
}

static void raven_command(edict_t *ent) {
    FOR_CONTROLLABLE_SELECTED_UNITS(ent->client, unit)
        unit_issueimmediateorder(unit, raven_orders[raven_is_on(unit) ? 1 : 0]);
}

BZ_ABILITY_PROC(CAbilityRavenForm) {
    switch (msg) {
    case A_ENABLE:case A_UNIT_INIT:S_InitializeConfiguredMorphAvailability(ent,call);return true;
    case A_DISABLE:S_DetachConfiguredMorphAvailability(ent,call);return true;
    case A_MORPH_COMPLETE:
        S_CompleteConfiguredMorphAvailability(ent,call);
        if(call->enabled)raven_reverse_end(ent);else raven_forward_end(ent);return true;
    case A_COMMAND: raven_command(call && call->client ? call->client : ent); return true;
    case A_TOGGLE_ON: return raven_is_on(ent);
    case A_ORDER: return call && call->order && raven_order(ent, call->order);
    default: return false;
    }
}

BZ_ABILITY_PROC(CAbilityBearForm) {
    switch(msg) {
    case A_ORDER:return call && call->order && morph_form_order(ent,!strcmp(call->order,bear_orders[0]),true);
    case A_TOGGLE_ON: {
        morphForm_t form;return morph_form_data(ent,&form,true) && ent->class_id==form.alternate_type;
    }
    case A_COMMAND:
        FOR_CONTROLLABLE_SELECTED_UNITS(call && call->client ? call->client->client : ent->client,unit) {
            morphForm_t form;if(morph_form_data(unit,&form,true))
                unit_issueimmediateorder(unit,bear_orders[unit->class_id==form.alternate_type ? 1 : 0]);
        }
        return true;
    case A_ENABLE:case A_UNIT_INIT:S_InitializeConfiguredMorphAvailability(ent,call);return true;
    case A_DISABLE:S_DetachConfiguredMorphAvailability(ent,call);return true;
    case A_MORPH_COMPLETE:S_CompleteConfiguredMorphAvailability(ent,call);bear_morph_end(ent);return true;
    default:return false;
    }
}
