/* SC2 unit lifecycle. Like WC3 m_unit.c, this owns vitals and the inverse paths;
 * Galaxy wrappers only resolve handles and call the owner. */
static sc2UnitState_t sc2_units[SC2_MAX_EDICTS];
static uint32_t SC2_EdictNumber(edict_t const *ent);
static void SC2_UnitAnimation(edict_t *ent,cstring_t name);
static void SC2_LinkUnit(edict_t *ent);

static sc2UnitState_t *SC2_UnitState(void *ptr) {
    uint32_t n=SC2_EdictNumber(ptr);
    return n<SC2_MAX_EDICTS && sc2_units[n].initialized && ((edict_t *)ptr)->inuse ? &sc2_units[n] : NULL;
}
static void SC2_UnitInit(edict_t *ent,sc2MapObject_t const *object) {
    sc2UnitState_t *u=&sc2_units[SC2_EdictNumber(ent)];
    *u=(sc2UnitState_t){.initialized=true,.map_id=object->id,.states=1u<<SC2_UNIT_SELECTABLE};
    snprintf(u->type,sizeof(u->type),"%s",object->name);
    for (int i=0;i<3;i++) {
        int p=i*4; u->vitals[i]=(sc2Vital_t){object->unit_properties[p],object->unit_properties[p+2],object->unit_properties[p+3]};
        SC2_UnitSetProperty(u,p,u->vitals[i].value);
    }
    u->speed=object->unit_properties[20]; u->height=object->unit_properties[19];
    u->radius=object->radius; u->resources=object->resources;
    u->supplies_used=object->unit_properties[12]; u->supplies_made=object->unit_properties[13];
    memcpy(u->normal,object->unit_properties,sizeof(u->normal));
    for (int p=0;p<24;p++) u->normal[p]=SC2_UnitProperty(u,p);
    sc2_move[ent->s.number].speed=u->speed;
}
static void SC2_UnitChanged(void *ptr) {
    edict_t *ent=ptr; uint32_t n=SC2_EdictNumber(ent); sc2UnitState_t *u=SC2_UnitState(ent);
    if (!u) return;
    bool dead=!SC2_UnitAlive(u), was_dead=!!(ent->svflags & SVF_DEADMONSTER);
    if (dead) ent->svflags |= SVF_DEADMONSTER; else ent->svflags &= ~SVF_DEADMONSTER;
    if (dead != was_dead) {
        sc2_move[n].moving=false; sc2_move[n].path.valid=false; ent->s.ability=0; ent->selected=0;
        SC2_UnitAnimation(ent,dead ? "Death" : "Stand");
    }
    if (u->states & (1u<<SC2_UNIT_HIDDEN)) ent->s.renderfx |= RF_HIDDEN;
    else ent->s.renderfx &= ~RF_HIDDEN;
    if (dead || (u->states & (1u<<SC2_UNIT_HIDDEN)) || !(u->states & (1u<<SC2_UNIT_SELECTABLE))) ent->selected=0;
    ent->s.stats[ENT_HEALTH]=(uint8_t)(SC2_UnitProperty(u,1)*255/100);
    ent->s.stats[ENT_MANA]=(uint8_t)(SC2_UnitProperty(u,5)*255/100);
    sc2_move[n].speed=u->speed; sc2_move[n].height=u->height;
    SC2_LinkUnit(ent);
}
static void SC2_UnitRemove(void *ptr) {
    edict_t *ent=ptr; uint32_t n=SC2_EdictNumber(ent);
    if (n>=SC2_MAX_EDICTS || !ent->inuse) return;
    gi.UnlinkEntity(ent); ent->inuse=false; ent->selected=0;
    memset(&sc2_move[n],0,sizeof(sc2_move[n]));
    memset(&sc2_units[n],0,sizeof(sc2_units[n]));
}
static void SC2_UnitSetOwner(void *ptr,int player,bool change_color) {
    edict_t *ent=ptr;
    if (!ent || !ent->inuse) return;
    if (change_color) ent->s.effect_flags &= ~EFX_TEAM_COLOR_MASK;
    else if (!(ent->s.effect_flags & EFX_TEAM_COLOR_MASK)) {
        if (ent->s.player >= 31) { fprintf(stderr,"SC2 UnitSetOwner: team color %u cannot fit the existing snapshot override\n",ent->s.player); return; }
        ent->s.effect_flags |= (ent->s.player+1)<<EFX_TEAM_COLOR_SHIFT;
    }
    ent->s.player=player; ent->selected=0;
}
static bool SC2_UnitLocation(void *ptr,float *x,float *y,float *z,float *facing) {
    edict_t *ent=ptr; if (!ent || !ent->inuse) return false;
    *x=ent->s.origin.x; *y=ent->s.origin.y; *z=ent->s.origin.z;
    *facing=ent->s.angle*180.0f/(float)M_PI; return true;
}
static void *SC2_UnitFromId(uint32_t id) {
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++)
        if (sc2_edicts[i].inuse && sc2_units[i].initialized && sc2_units[i].map_id==id) return &sc2_edicts[i];
    return NULL;
}
static bool SC2_UnitCanMove(uint32_t n) {
    sc2UnitState_t const *u=&sc2_units[n];
    return !u->initialized || (SC2_UnitAlive(u) && !(u->states & ((1u<<SC2_UNIT_PAUSED)|(1u<<SC2_UNIT_MOVE_SUPPRESSED))));
}
static void SC2_UnitTick(edict_t *ent) {
    sc2UnitState_t *u=SC2_UnitState(ent); if (!u) return;
    bool alive=SC2_UnitAlive(u); SC2_UnitRegenerate(u,FRAMETIME/1000.0f);
    ent->s.stats[ENT_HEALTH]=(uint8_t)(SC2_UnitProperty(u,1)*255/100);
    ent->s.stats[ENT_MANA]=(uint8_t)(SC2_UnitProperty(u,5)*255/100);
    if (alive != SC2_UnitAlive(u)) SC2_UnitChanged(ent);
}
