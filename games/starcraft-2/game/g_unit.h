/* SC2 unit lifecycle. Like WC3 m_unit.c, this owns vitals and the inverse paths;
 * Galaxy wrappers only resolve handles and call the owner. */
static uint32_t SC2_EdictNumber(edict_t const *ent);
static void SC2_UnitAnimation(edict_t *ent,cstring_t name);
static void SC2_LinkUnit(edict_t *ent);

static sc2UnitState_t *SC2_UnitState(void *ptr) {
    edict_t *ent=ptr;
    return SC2_EdictNumber(ent)<SC2_MAX_EDICTS && ent->inuse && ent->unit.initialized ? &ent->unit : NULL;
}
/* Abilities, weapons, and behaviors start from the resolved CUnit arrays; scripts change them afterwards. */
static void SC2_UnitInitLinks(sc2UnitState_t *u) {
    char links[SC2_UNIT_ABILS][SC2_LINK_LEN];
    u->abil_n=(uint8_t)SC2_MapUnitLinks(u->type,SC2_LINK_ABIL,links,SC2_UNIT_ABILS);
    for (int i=0;i<u->abil_n;i++) { snprintf(u->abils[i].link,SC2_LINK_LEN,"%.*s",SC2_LINK_LEN-1,links[i]); u->abils[i].level=1; }
    u->weapon_n=(uint8_t)SC2_MapUnitLinks(u->type,SC2_LINK_WEAPON,links,SC2_UNIT_WEAPONS);
    for (int i=0;i<u->weapon_n;i++) snprintf(u->weapons[i].link,SC2_LINK_LEN,"%.*s",SC2_LINK_LEN-1,links[i]);
    u->behavior_n=(uint8_t)SC2_MapUnitLinks(u->type,SC2_LINK_BEHAVIOR,links,SC2_UNIT_BEHAVIORS);
    for (int i=0;i<u->behavior_n;i++) { snprintf(u->behaviors[i].link,SC2_LINK_LEN,"%.*s",SC2_LINK_LEN-1,links[i]); u->behaviors[i].count=1; }
}
static void SC2_UnitInit(edict_t *ent,sc2MapObject_t const *object) {
    sc2UnitState_t *u=&ent->unit;
    *u=(sc2UnitState_t){.initialized=true,.map_id=object->id,.states=1u<<SC2_UNIT_SELECTABLE};
    snprintf(u->type,sizeof(u->type),"%s",object->name);
    u->target_flags=SC2_MapUnitTargetFlags(u->type);
    for (int i=0;i<3;i++) {
        int p=i*4; u->vitals[i]=(sc2Vital_t){object->unit_properties[p],object->unit_properties[p+2],object->unit_properties[p+3]};
        SC2_UnitSetProperty(u,p,u->vitals[i].value);
    }
    u->speed=object->unit_properties[20]; u->height=object->unit_properties[19];
    u->radius=object->radius; u->resources=object->resources;
    u->supplies_used=object->unit_properties[12]; u->supplies_made=object->unit_properties[13];
    memcpy(u->normal,object->unit_properties,sizeof(u->normal));
    for (int p=0;p<24;p++) u->normal[p]=SC2_UnitProperty(u,p);
    SC2_UnitInitLinks(u);
    ent->move.speed=u->speed;
}
static void SC2_UnitChanged(void *ptr) {
    edict_t *ent=ptr; sc2UnitState_t *u=SC2_UnitState(ent);
    if (!u) return;
    uint32_t selected_before=ent->selected;
    bool dead=!SC2_UnitAlive(u), was_dead=!!(ent->svflags & SVF_DEADMONSTER);
    if (dead) ent->svflags |= SVF_DEADMONSTER; else ent->svflags &= ~SVF_DEADMONSTER;
    if (dead != was_dead) {
        ent->move.moving=false; ent->move.path.valid=false; ent->s.ability=0; ent->selected=0;
        SC2_UnitAnimation(ent,dead ? "Death" : "Stand");
    }
    if (u->states & (1u<<SC2_UNIT_HIDDEN)) ent->s.renderfx |= RF_HIDDEN;
    else ent->s.renderfx &= ~RF_HIDDEN;
    if (dead || (u->states & (1u<<SC2_UNIT_HIDDEN)) || !(u->states & (1u<<SC2_UNIT_SELECTABLE))) ent->selected=0;
    if (selected_before!=ent->selected) sc2_level.selection_dirty|=selected_before;
    ent->s.stats[ENT_HEALTH]=(uint8_t)(SC2_UnitProperty(u,1)*255/100);
    ent->s.stats[ENT_MANA]=(uint8_t)(SC2_UnitProperty(u,5)*255/100);
    ent->move.speed=u->speed; ent->move.height=u->height;
    SC2_LinkUnit(ent);
}
static void SC2_UnitRemove(void *ptr) {
    edict_t *ent=ptr;
    if (SC2_EdictNumber(ent)>=SC2_MAX_EDICTS || !ent->inuse) return;
    sc2_level.selection_dirty|=ent->selected;
    gi.UnlinkEntity(ent); ent->inuse=false; ent->selected=0;
    memset(&ent->move,0,sizeof(ent->move));
    memset(&ent->unit,0,sizeof(ent->unit));
}
static void SC2_UnitSetOwner(void *ptr,int player,bool change_color) {
    edict_t *ent=ptr;
    if (!ent || !ent->inuse) return;
    if (change_color) ent->s.effect_flags &= ~EFX_TEAM_COLOR_MASK;
    else if (!(ent->s.effect_flags & EFX_TEAM_COLOR_MASK)) {
        if (ent->s.player >= 31) { fprintf(stderr,"SC2 UnitSetOwner: team color %u cannot fit the existing snapshot override\n",ent->s.player); return; }
        ent->s.effect_flags |= (ent->s.player+1)<<EFX_TEAM_COLOR_SHIFT;
    }
    sc2_level.selection_dirty|=ent->selected;
    ent->s.player=player; ent->selected=0;
}
/* Script selection skips the ownership test player input needs, but never selects the dead or hidden. */
static void SC2_UnitSelect(void *ptr,int player,bool select) {
    edict_t *ent=ptr; sc2UnitState_t const *u=SC2_UnitState(ent);
    if (!ent || !ent->inuse || player<0 || player>=32) return;
    uint32_t bit=1u<<player, before=ent->selected;
    if (select && u && SC2_UnitAlive(u) && (u->states & (1u<<SC2_UNIT_SELECTABLE)) && !(u->states & (1u<<SC2_UNIT_HIDDEN)))
        ent->selected |= bit;
    else if (!select) ent->selected &= ~bit;
    if (ent->selected != before) sc2_level.selection_dirty |= bit;
}
static bool SC2_UnitIsSelected(void *ptr,int player) {
    edict_t const *ent=ptr; return ent && ent->inuse && player>=0 && player<32 && (ent->selected & (1u<<player));
}
/* The override uses the same snapshot field UnitSetOwner(…, false) fills; index is a player color slot. */
static void SC2_UnitTeamColor(void *ptr,int index) {
    edict_t *ent=ptr;
    if (!ent || !ent->inuse) return;
    ent->s.effect_flags &= ~EFX_TEAM_COLOR_MASK;
    if (index>=31) { fprintf(stderr,"SC2 UnitSetTeamColorIndex: color %d cannot fit the snapshot override\n",index); return; }
    if (index>=0) ent->s.effect_flags |= (uint32_t)(index+1)<<EFX_TEAM_COLOR_SHIFT;
}
static bool SC2_UnitIsFlying(void *ptr) { edict_t const *ent=ptr; return ent && ent->inuse && ent->move.flying; }
static bool SC2_UnitLocation(void *ptr,float *x,float *y,float *z,float *facing) {
    edict_t *ent=ptr; if (!ent || !ent->inuse) return false;
    *x=ent->s.origin.x; *y=ent->s.origin.y; *z=ent->s.origin.z;
    *facing=ent->s.angle*180.0f/(float)M_PI; return true;
}
static void *SC2_UnitFromId(uint32_t id) {
    for (uint32_t i=globals.max_clients;i<globals.num_edicts;i++)
        if (sc2_edicts[i].inuse && sc2_edicts[i].unit.initialized && sc2_edicts[i].unit.map_id==id) return &sc2_edicts[i];
    return NULL;
}
static bool SC2_UnitCanMove(uint32_t n) {
    sc2UnitState_t const *u=&sc2_edicts[n].unit;
    return !u->initialized || (SC2_UnitAlive(u) && !(u->states & ((1u<<SC2_UNIT_PAUSED)|(1u<<SC2_UNIT_MOVE_SUPPRESSED))));
}
static void SC2_UnitTick(edict_t *ent) {
    sc2UnitState_t *u=SC2_UnitState(ent); if (!u) return;
    bool alive=SC2_UnitAlive(u); SC2_UnitRegenerate(u,FRAMETIME/1000.0f); SC2_UnitAdvanceTimers(u,FRAMETIME/1000.0f);
    ent->s.stats[ENT_HEALTH]=(uint8_t)(SC2_UnitProperty(u,1)*255/100);
    ent->s.stats[ENT_MANA]=(uint8_t)(SC2_UnitProperty(u,5)*255/100);
    if (alive != SC2_UnitAlive(u)) SC2_UnitChanged(ent);
}
