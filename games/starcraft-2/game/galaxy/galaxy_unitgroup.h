static sc2GGroup_t *sc2_unit_group(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unitgroup");
    /* Cargo remains a live view; mutation through this view is rejected explicitly. */
    static sc2GGroup_t cargo;
    if (h & CARGO_GROUP_FLAG) {
        int32_t t=h & ~CARGO_GROUP_FLAG;
        if (t>0 && t<=sc2_gunit_n) { cargo=(sc2GGroup_t){sc2_gcargo[t-1],sc2_gcargo_n[t-1],-1}; return &cargo; }
    }
    return h>0 && h<sc2_unit_group_n ? &sc2_unit_groups[h] : NULL;
}
static sc2GGroup_t *sc2_mutable_unit_group(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j);
    if (g && g->capacity<0) { jass_rterror(j,"Copy a cargo group before changing membership"); return NULL; }
    return g;
}
static bool sc2_group_unit_matches(int32_t h,int mode) {
    if (h<=0 || h>sc2_gunit_n || !sc2_gunits[h-1]) return false;
    bool alive=!sc2_galaxy_unit_is_alive || sc2_galaxy_unit_is_alive(sc2_gunits[h-1]);
    return mode==0 || (mode==1 ? alive : !alive);
}
static uint32_t sc2_UnitGroupEmpty(jass_t *j) {
    int32_t h=sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,NULL);
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"unitgroup");
}
static uint32_t sc2_UnitGroupCopy(jass_t *j) {
    int32_t h=sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,sc2_unit_group(j));
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"unitgroup");
}
static uint32_t sc2_UnitGroupClear(jass_t *j) { sc2GGroup_t *g=sc2_mutable_unit_group(j); if (g) g->count=0; return 0; }
static uint32_t sc2_UnitGroupAdd(jass_t *j) {
    sc2GGroup_t *g=sc2_mutable_unit_group(j); int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,2,"unit");
    if (g && sc2_group_unit_matches(h,0)) sc2_group_append(j,g,h); return 0;
}
static uint32_t sc2_UnitGroupRemove(jass_t *j) {
    sc2GGroup_t *g=sc2_mutable_unit_group(j); int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,2,"unit");
    if (g) sc2_group_remove(g,h); return 0;
}
static uint32_t sc2_UnitGroupCount(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int mode=sc2_checked_index(j,2,3), count=0;
    if (g) for (int i=0;i<g->count;i++) count+=sc2_group_unit_matches(g->items[i],mode);
    return jass_pushinteger(j,count);
}
static uint32_t sc2_UnitGroupHasUnit(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,2,"unit");
    if (g && sc2_group_unit_matches(h,0)) for (int i=0;i<g->count;i++) if (g->items[i]==h) return jass_pushboolean(j,true);
    return jass_pushboolean(j,false);
}
static uint32_t sc2_UnitGroupUnit(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int32_t n=jass_checkinteger(j,2)-1;
    int32_t h=g && n>=0 && n<g->count ? g->items[n] : 0;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)(sc2_group_unit_matches(h,0)?h:0),"unit");
}
static uint32_t sc2_unit_group_filter(jass_t *j, bool region) {
    sc2GGroup_t *g=sc2_unit_group(j); int player=region ? -1 : jass_checkinteger(j,2);
    sc2Region_t *r=region ? sc2_region(j,2) : NULL; int32_t limit=jass_checkinteger(j,3);
    int32_t h=sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,NULL);
    if (g) for (int i=0;i<g->count && (limit<=0 || sc2_unit_groups[h].count<limit);i++) {
        int32_t u=g->items[i]; sc2GPoint_t p;
        if (!sc2_group_unit_matches(u,0)) continue;
        bool match=region ? (sc2_unit_location_handle(u,&p) && sc2_region_has_point(r,p)) :
            (player==-1 || (sc2_galaxy_unit_owner && sc2_galaxy_unit_owner(sc2_gunits[u-1])==player));
        if (match) sc2_group_append(j,&sc2_unit_groups[h],u);
    }
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"unitgroup");
}
static uint32_t sc2_UnitGroupFilterPlayer(jass_t *j) { return sc2_unit_group_filter(j,false); }
static uint32_t sc2_UnitGroupFilterRegion(jass_t *j) { return sc2_unit_group_filter(j,true); }
static uint32_t sc2_UnitGroupNearestUnit(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); sc2GPoint_t p=*sc2_point(j,2),u; int32_t best=0; float distance=INFINITY;
    if (g) for (int i=0;i<g->count;i++) if (sc2_unit_location_handle(g->items[i],&u)) {
        float d=(p.x-u.x)*(p.x-u.x)+(p.y-u.y)*(p.y-u.y);
        if (d<distance) { best=g->items[i]; distance=d; }
    }
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)best,"unit");
}
static uint32_t sc2_UnitGroupRandomUnit(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int mode=sc2_checked_index(j,2,3),count=0; int32_t result=0;
    if (g) for (int i=0;i<g->count;i++) if (sc2_group_unit_matches(g->items[i],mode) && rand()%++count==0) result=g->items[i];
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)result,"unit");
}
static uint32_t sc2_UnitGroupLoopBegin(jass_t *j) { sc2_loop_begin(j,0,sc2_unit_group(j)); return 0; }
static uint32_t sc2_UnitGroupLoopEnd(jass_t *j) { sc2_loop_end(j,0); return 0; }
static uint32_t sc2_UnitGroupLoopStep(jass_t *j) { sc2GLoop_t *l=sc2_loop_current(j,0); if (l) l->index++; return 0; }
static uint32_t sc2_UnitGroupLoopDone(jass_t *j) { sc2GLoop_t *l=sc2_loop_current(j,0); return jass_pushboolean(j,!l || l->index>=l->snapshot.count); }
static uint32_t sc2_UnitGroupLoopCurrent(jass_t *j) {
    sc2GLoop_t *l=sc2_loop_current(j,0); int32_t h=l && l->index<l->snapshot.count ? l->snapshot.items[l->index] : 0;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)(sc2_group_unit_matches(h,0)?h:0),"unit");
}

static uint32_t sc2_UnitGroupIssueOrder(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int32_t o=(int32_t)(uintptr_t)jass_checkhandle(j,2,"order"), queue=jass_checkinteger(j,3);
    bool accepted=g && g->count>0;
    if (g) for (int i=0;i<g->count;i++) if (!sc2_issue_unit_order(j,g->items[i],o,queue)) accepted=false;
    return jass_pushboolean(j,accepted);
}
