#include "s_skills.h"

typedef struct {
    edict_t *unit;
    uint32_t spawn;
    int32_t hero,level,distance;
} itemTeleportCandidate_t;

static bool item_teleport_candidate(edict_t *caster,edict_t *unit,float area) {
    if(unit==caster || !(unit->svflags&SVF_MONSTER) || !S_SpellIsAliveTarget(unit) ||
       !G_UnitIsWorldActive(unit) || G_IsDeferredFree(unit) || (unit->s.renderfx&RF_HIDDEN) ||
       unit->s.player!=caster->s.player || G_UnitIsStructure(unit) ||
       S_UnitMovementType(unit->data.UnitData)==UNIT_MOVE_FLOAT ||
       G_ActorHasAbilityCode(unit,MAKEFOURCC('A','l','o','c')) ||
       Vector2_distance(&unit->s.origin2,&caster->s.origin2)>area)return false;
    for(unitAbilityDisable_t const *state=unit->ability_disables;state;state=state->next)
        if(state->exclusive_owned && state->exclusive_inventory)return false;
    return true;
}
/*3fb3c0: within an owner, Heroes, level, then wrapped squared3D distance.
 * The final engine identity breaks ties without consuming simulation RNG. */
static int item_teleport_candidate_compare(void const *a,void const *b) {
    itemTeleportCandidate_t const *x=a,*y=b;
    if(x->hero!=y->hero)return x->hero>y->hero ? -1 : 1;
    if(x->level!=y->level)return x->level>y->level ? -1 : 1;
    if(x->distance!=y->distance)return x->distance<y->distance ? -1 : 1;
    return x->unit->s.number>y->unit->s.number ? -1 : x->unit->s.number<y->unit->s.number;
}
static bool item_teleport_relocate(edict_t *unit,uint32_t code,vec2_t const *requested) {
    vec2_t position;
    if(!G_FindUnitUnstuckPosition(unit,requested,&position))return false;
    S_SpellRelocateUnit(unit,code,&position);return true;
}
static bool item_teleport_execute(edict_t *caster,uint32_t code,edict_t *target) {
    if(!target)return false;
    uint32_t rank=S_SpellLevel(caster,code),count=0;
    float area=S_SpellNumber(code,ABILITY_NUMBER_AREA,rank);
    uint32_t limit=wc3_int_bits(wc3_float_bits(S_SpellData(code,rank,1)))-1;
    FILTER_EDICTS(unit,item_teleport_candidate(caster,unit,area))count++;
    itemTeleportCandidate_t *candidates=count ? gi.MemAlloc(count*sizeof(*candidates)) : NULL;
    uint32_t n=0;
    FILTER_EDICTS(unit,item_teleport_candidate(caster,unit,area)) {
        float dx=wc3_sub(unit->s.origin.x,caster->s.origin.x);
        float dy=wc3_sub(unit->s.origin.y,caster->s.origin.y);
        float dz=wc3_sub(unit->s.origin.z,caster->s.origin.z);
        int32_t distance=(int32_t)wc3_int_bits(wc3_float_bits(wc3_add(
            wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy)),wc3_mul(dz,dz))));
        bool hero=G_UnitIsHero(unit);
        candidates[n++]=(itemTeleportCandidate_t){unit,unit->spawn_time,hero,
            hero ? (int32_t)unit->hero.level : unit->data.UnitBalance ? unit->data.UnitBalance->level : 0,distance};
    }
    if(count>limit) {qsort(candidates,count,sizeof(*candidates),item_teleport_candidate_compare);count=limit;}
    cstring_t setting=Stb_IniCacheFind(&game.config.misc,"Misc","TownPortalCluster");
    bool cluster=setting && atoi(setting)!=0 && S_SpellData(code,rank,2)!=0;
    vec2_t source=caster->s.origin2;
    bool success=item_teleport_relocate(caster,code,&target->s.origin2);
    vec2_t destination=success ? caster->s.origin2 : target->s.origin2;
    vec2_t translation=Vector2_sub(&destination,&source);
    FOR_LOOP(i,count) {
        edict_t *unit=candidates[i].unit;
        if(!unit->inuse || unit->spawn_time!=candidates[i].spawn || G_IsDeferredFree(unit)) {success=false;continue;}
        vec2_t requested=cluster ? destination : Vector2_add(&unit->s.origin2,&translation);
        if(!item_teleport_relocate(unit,code,&requested))success=false;
    }
    gi.MemFree(candidates);return success;
}

/* AItp3ff000: authored TownHall classification and completed construction;
 * 3fef60 uses the destination's directional shared-spells grant. */
static bool item_teleport_destination(edict_t *caster,edict_t *target) {
    if(!S_SpellIsAliveTarget(target) || target->construction ||
       caster->s.player>=MAX_PLAYERS || target->s.player>=MAX_PLAYERS)return false;
    if(caster->s.player!=target->s.player &&
       !G_GetPlayerAlliance(&game.clients[target->s.player].ps,&game.clients[caster->s.player].ps,
            ALLIANCE_SHARED_SPELLS))return false;
    cstring_t types=target->data.UnitBalance ? target->data.UnitBalance->type : NULL;
    if(!types || !*types)types=target->data.UnitData ? target->data.UnitData->unitClassification : NULL;
    if(types) {PARSE_LIST(types,type,parse_segment)if(!strcasecmp(type,"TownHall"))return true;}
    return false;
}

/* ItemTeleport435f10/40be30 share the ordered spell's saved continuation.
 * The item and Inventory are exempt from the all-owner disable contribution;
 * Unit198 still retains Stop until completion. */
BZ_ABILITY_PROC(CAbilityItemTeleport) {
    uint32_t code=call && call->item ? call->item->code : 0;
    switch(msg) {
    case A_CAST_PROFILE:
        *call->cast_profile=(spellCastProfile_t){
            .channel_delay=S_SpellNumber(code,ABILITY_NUMBER_CAST,S_SpellLevel(ent,code)),
            .intercept_before_work=true,.finish_on_target_loss=true};
        return true;
    case A_VALIDATE:
        return call->target && call->target->type==SPELL_TARGET_UNIT &&
            item_teleport_destination(ent,call->target->entity);
    case A_CAST_BEGIN:S_AcquireItemAbilityDisables(ent,code);return true;
    case A_CAST_END:return S_ReleaseItemAbilityDisables(ent,code);
    case A_UNIT_SUSPEND:case A_DISABLE:
        S_CancelSpellOwner(ent,code);return true;
    case A_ITEM_REMOVE:
        S_CancelItemSpell(ent,call->source_item,call->source_item_spawn_time);return true;
    case A_EXECUTE:
        return item_teleport_execute(ent,code,call->target ? call->target->entity : NULL);
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
