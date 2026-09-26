/* Region geometry is an immutable expression tree; copying a region never aliases later edits. */
#define SC2_MAX_REGIONS 1024
#define SC2_MAX_REGION_NODES 8192
typedef struct { int kind, a, b, depth; float x1, y1, x2, y2; } sc2RegionNode_t;
typedef struct { int root; int32_t unit; sc2GPoint_t offset; } sc2Region_t;
static sc2RegionNode_t sc2_region_nodes[SC2_MAX_REGION_NODES];
static sc2Region_t sc2_regions[SC2_MAX_REGIONS];
static int sc2_region_n = 1, sc2_region_node_n = 1;

static int sc2_region_node(jass_t *j, sc2RegionNode_t node) {
    node.depth = 1 + MAX(sc2_region_nodes[node.a].depth, sc2_region_nodes[node.b].depth);
    if (sc2_region_node_n == SC2_MAX_REGION_NODES || node.depth > 64) {
        jass_rterror(j,"Galaxy region geometry capacity exceeded"); return 0;
    }
    int h = sc2_region_node_n++; sc2_region_nodes[h] = node; return h;
}
static sc2Region_t *sc2_region(jass_t *j, int arg) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,arg,"region");
    if (h <= 0 || h >= sc2_region_n) { jass_rterror(j,"Invalid Galaxy region"); return NULL; }
    return &sc2_regions[h];
}
static uint32_t sc2_region_result(jass_t *j, int root) {
    if (sc2_region_n == SC2_MAX_REGIONS) { jass_rterror(j,"Galaxy region table full"); return 0; }
    int h = sc2_region_n++; sc2_regions[h] = (sc2Region_t){ .root = root };
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"region");
}
static sc2GPoint_t sc2_region_offset(sc2Region_t const *r) {
    sc2GPoint_t p = r->offset, u;
    if (r->unit && sc2_unit_location_handle(r->unit,&u)) { p.x += u.x; p.y += u.y; }
    return p;
}
static bool sc2_region_contains(int h, float x, float y) {
    if (!h) return false;
    sc2RegionNode_t const *n = &sc2_region_nodes[h];
    switch (n->kind) {
    case 1: return x >= n->x1 && y >= n->y1 && x <= n->x2 && y <= n->y2;
    case 2: return (x-n->x1)*(x-n->x1)+(y-n->y1)*(y-n->y1) <= n->x2*n->x2;
    case 3: return sc2_region_contains(n->a,x,y) || sc2_region_contains(n->b,x,y);
    case 4: return sc2_region_contains(n->a,x,y) && !sc2_region_contains(n->b,x,y);
    case 5: return sc2_region_contains(n->a,x-n->x1,y-n->y1);
    }
    return false;
}
static bool sc2_region_has_point(sc2Region_t const *r, sc2GPoint_t p) {
    sc2GPoint_t offset = sc2_region_offset(r);
    return sc2_region_contains(r->root,p.x-offset.x,p.y-offset.y);
}
/* Subtraction cannot enlarge bounds. Empty geometry has no bounds. */
static bool sc2_region_bounds(int h, sc2GPoint_t *lo, sc2GPoint_t *hi) {
    if (!h) return false;
    sc2RegionNode_t const *n = &sc2_region_nodes[h]; sc2GPoint_t a,b;
    if (n->kind == 1) { *lo = (sc2GPoint_t){n->x1,n->y1}; *hi = (sc2GPoint_t){n->x2,n->y2}; return true; }
    if (n->kind == 2) { *lo = (sc2GPoint_t){n->x1-n->x2,n->y1-n->x2}; *hi = (sc2GPoint_t){n->x1+n->x2,n->y1+n->x2}; return true; }
    bool found = sc2_region_bounds(n->a,lo,hi);
    if (n->kind == 3 && sc2_region_bounds(n->b,&a,&b)) {
        if (!found) { *lo = a; *hi = b; found = true; }
        else { lo->x = MIN(lo->x,a.x); lo->y = MIN(lo->y,a.y); hi->x = MAX(hi->x,b.x); hi->y = MAX(hi->y,b.y); }
    }
    if (n->kind == 5 && found) { lo->x += n->x1; lo->y += n->y1; hi->x += n->x1; hi->y += n->y1; }
    return found;
}
static int sc2_region_rect(jass_t *j, int arg) {
    float x1 = jass_checknumber(j,arg), y1 = jass_checknumber(j,arg+1);
    float x2 = jass_checknumber(j,arg+2), y2 = jass_checknumber(j,arg+3);
    return sc2_region_node(j,(sc2RegionNode_t){ .kind=1, .x1=MIN(x1,x2), .y1=MIN(y1,y2), .x2=MAX(x1,x2), .y2=MAX(y1,y2) });
}
static int sc2_region_circle(jass_t *j, int arg) {
    sc2GPoint_t p = *sc2_point(j,arg); float radius = jass_checknumber(j,arg+1);
    if (radius < 0) { jass_rterror(j,"Negative region radius"); return 0; }
    return sc2_region_node(j,(sc2RegionNode_t){ .kind=2, .x1=p.x, .y1=p.y, .x2=radius });
}
static uint32_t sc2_RegionEmpty(jass_t *j) { return sc2_region_result(j,0); }
static uint32_t sc2_RegionRect(jass_t *j) { return sc2_region_result(j,sc2_region_rect(j,1)); }
static uint32_t sc2_RegionCircle(jass_t *j) { return sc2_region_result(j,sc2_region_circle(j,1)); }
static uint32_t sc2_RegionAddRect(jass_t *j) {
    sc2Region_t *r = sc2_region(j,1); int shape = sc2_region_rect(j,3);
    r->root = sc2_region_node(j,(sc2RegionNode_t){ .kind=jass_checkboolean(j,2)?3:4, .a=r->root, .b=shape }); return 0;
}
static uint32_t sc2_RegionAddCircle(jass_t *j) {
    sc2Region_t *r = sc2_region(j,1); int shape = sc2_region_circle(j,3);
    r->root = sc2_region_node(j,(sc2RegionNode_t){ .kind=jass_checkboolean(j,2)?3:4, .a=r->root, .b=shape }); return 0;
}
static uint32_t sc2_RegionAddRegion(jass_t *j) {
    sc2Region_t *r = sc2_region(j,1), *other = sc2_region(j,2);
    sc2GPoint_t p = sc2_region_offset(other), own = sc2_region_offset(r);
    int translated = sc2_region_node(j,(sc2RegionNode_t){ .kind=5, .a=other->root, .x1=p.x-own.x, .y1=p.y-own.y });
    r->root = sc2_region_node(j,(sc2RegionNode_t){ .kind=3, .a=r->root, .b=translated }); return 0;
}
static uint32_t sc2_RegionContainsPoint(jass_t *j) { return jass_pushboolean(j,sc2_region_has_point(sc2_region(j,1),*sc2_point(j,2))); }
static uint32_t sc2_region_bound_result(jass_t *j, int which) {
    /* RegionFromId is still null. A script error here aborts InitTriggers before the intro. */
    if (!jass_checkhandle(j, 1, "region")) {
        fprintf(stderr, "SC2 galaxy: region bound skipped, region handle is null\n");
        return jass_pushnullhandle(j, "point");
    }
    sc2Region_t *r = sc2_region(j,1); sc2GPoint_t lo={0},hi={0},p=sc2_region_offset(r);
    if (!sc2_region_bounds(r->root,&lo,&hi)) return jass_pushnullhandle(j,"point");
    p.x += which == 0 ? lo.x : which == 1 ? hi.x : (lo.x+hi.x)*0.5f;
    p.y += which == 0 ? lo.y : which == 1 ? hi.y : (lo.y+hi.y)*0.5f;
    return sc2_point_result(j,p);
}
static uint32_t sc2_RegionGetBoundsMin(jass_t *j) { return sc2_region_bound_result(j,0); }
static uint32_t sc2_RegionGetBoundsMax(jass_t *j) { return sc2_region_bound_result(j,1); }
static uint32_t sc2_RegionGetCenter(jass_t *j) { return sc2_region_bound_result(j,2); }
static uint32_t sc2_RegionGetOffset(jass_t *j) { return sc2_point_result(j,sc2_region(j,1)->offset); }
static uint32_t sc2_RegionSetOffset(jass_t *j) { sc2_region(j,1)->offset = *sc2_point(j,2); return 0; }
static uint32_t sc2_RegionSetCenter(jass_t *j) {
    sc2Region_t *r = sc2_region(j,1); sc2GPoint_t lo={0},hi={0},p=*sc2_point(j,2),base=sc2_region_offset(r);
    if (!sc2_region_bounds(r->root,&lo,&hi)) { jass_rterror(j,"Cannot center an empty region"); return 0; }
    r->offset.x += p.x-base.x-(lo.x+hi.x)*0.5f; r->offset.y += p.y-base.y-(lo.y+hi.y)*0.5f; return 0;
}
static uint32_t sc2_RegionAttachToUnit(jass_t *j) {
    sc2Region_t *r = sc2_region(j,1); r->unit = (int32_t)(uintptr_t)jass_checkhandle(j,2,"unit");
    r->offset = *sc2_point(j,3); return 0;
}
static uint32_t sc2_RegionGetAttachUnit(jass_t *j) { return jass_pushlighthandle(j,(handle_t)(uintptr_t)sc2_region(j,1)->unit,"unit"); }
/* Map-authored regions and random sampling are outside the geometry subset. */
static uint32_t sc2_RegionEntireMap(jass_t *j) { return jass_pushnullhandle(j,"region"); }
static uint32_t sc2_RegionPlayableMap(jass_t *j) { return jass_pushnullhandle(j,"region"); }
static uint32_t sc2_RegionPlayableMapSet(jass_t *j) { return jass_pushnull(j); }
static uint32_t sc2_RegionFromId(jass_t *j) { return jass_pushnullhandle(j,"region"); }
static uint32_t sc2_RegionRandomPoint(jass_t *j) { return jass_pushnullhandle(j,"point"); }
