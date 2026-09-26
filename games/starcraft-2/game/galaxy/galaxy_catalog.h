/* galaxy_catalog.h — ability, order, and catalog natives */

#define MAX_GALAXY_ABILCMDS 1024
typedef struct { char ability[64]; int32_t cmd_idx; } sc2GAbilCmd_t;
static sc2GAbilCmd_t sc2_gabilcmds[MAX_GALAXY_ABILCMDS];
static int32_t sc2_gabilcmd_n = 1; /* 1-based; 0 = null */

#define MAX_GALAXY_ORDERS 1024
typedef struct { int32_t abilcmd_h, pt_h, unit_h; int player, target_type; uint32_t flags; } sc2GOrder_t;
static sc2GOrder_t sc2_gorders[MAX_GALAXY_ORDERS];
static int32_t sc2_gorder_n = 1;  /* 1-based; 0 = null */

static uint32_t sc2_AbilityClass(jass_t *j)       { return jass_pushinteger(j, 0); }
static uint32_t sc2_AbilityCommand(jass_t *j) {
    cstring_t name = jass_checkstring(j, 1);
    int32_t   cmd  = jass_checkinteger(j, 2);
    if (sc2_gabilcmd_n < MAX_GALAXY_ABILCMDS) {
        int32_t h = sc2_gabilcmd_n++;
        snprintf(sc2_gabilcmds[h].ability, sizeof(sc2_gabilcmds[h].ability),
                 "%s", name ? name : "");
        sc2_gabilcmds[h].cmd_idx = cmd;
        return jass_pushinteger(j, h);
    }
    fprintf(stderr, "AbilityCommand: table full (%d entries) — '%s' lost\n",
            MAX_GALAXY_ABILCMDS, name ? name : "");
    return jass_pushinteger(j, 0);
}
static uint32_t sc2_AbilityCommandGetAbility(jass_t *j) {
    int32_t h = jass_checkinteger(j, 1);
    return jass_pushstring(j, (h > 0 && h < sc2_gabilcmd_n) ? sc2_gabilcmds[h].ability : "");
}
static uint32_t sc2_AbilityCommandGetCommand(jass_t *j) {
    int32_t h = jass_checkinteger(j, 1);
    return jass_pushinteger(j, (h > 0 && h < sc2_gabilcmd_n) ? sc2_gabilcmds[h].cmd_idx : 0);
}
/* Action index is a higher-level concept (e.g. cast vs auto-cast); stub as 0 for now. */
static uint32_t sc2_AbilityCommandGetAction(jass_t *j) { (void)j; return jass_pushinteger(j, 0); }
static sc2GOrder_t *sc2_order(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"order");
    if (h<=0 || h>=sc2_gorder_n) { jass_rterror(j,"Invalid Galaxy order"); return NULL; }
    return &sc2_gorders[h];
}
static uint32_t sc2_Order(jass_t *j) {
    if (sc2_gorder_n==MAX_GALAXY_ORDERS) { jass_rterror(j,"Galaxy order table full"); return 0; }
    int32_t h=sc2_gorder_n++; sc2_gorders[h]=(sc2GOrder_t){ .abilcmd_h=sc2_ev_abil(j,1), .player=-1 };
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"order");
}
static uint32_t sc2_OrderTargetingPoint(jass_t *j) {
    int32_t abilcmd_h = sc2_ev_abil(j, 1);
    int32_t pt_h      = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (sc2_gorder_n < MAX_GALAXY_ORDERS) {
        int32_t h = sc2_gorder_n++;
        sc2_gorders[h] = (sc2GOrder_t){ .abilcmd_h=abilcmd_h, .pt_h=pt_h, .player=-1, .target_type=1 };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "order");
    }
    fprintf(stderr, "OrderTargetingPoint: table full (%d entries)\n", MAX_GALAXY_ORDERS);
    return jass_pushnullhandle(j, "order");
}
static uint32_t sc2_OrderTargetingUnit(jass_t *j) {
    int32_t abilcmd_h = sc2_ev_abil(j, 1);
    int32_t unit_h    = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "unit");
    if (sc2_gorder_n < MAX_GALAXY_ORDERS) {
        int32_t h = sc2_gorder_n++;
        sc2_gorders[h] = (sc2GOrder_t){ .abilcmd_h=abilcmd_h, .unit_h=unit_h, .player=-1, .target_type=2 };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "order");
    }
    return jass_pushnullhandle(j, "order");
}
static uint32_t sc2_OrderSetPlayer(jass_t *j) { sc2_order(j)->player=sc2_player_index(j,2); return 0; }
static uint32_t sc2_OrderGetPlayer(jass_t *j) { return jass_pushinteger(j,sc2_order(j)->player); }
static uint32_t sc2_OrderSetAbilityCommand(jass_t *j) { sc2_order(j)->abilcmd_h=sc2_ev_abil(j,2); return 0; }
static uint32_t sc2_OrderGetAbilityCommand(jass_t *j) { return jass_pushinteger(j,sc2_order(j)->abilcmd_h); }
static uint32_t sc2_OrderGetTargetType(jass_t *j) { return jass_pushinteger(j,sc2_order(j)->target_type); }
static uint32_t sc2_OrderSetTargetPoint(jass_t *j) {
    sc2GOrder_t *o=sc2_order(j); o->pt_h=(int32_t)(uintptr_t)jass_checkhandle(j,2,"point");
    o->target_type=1; o->unit_h=0; return 0;
}
static uint32_t sc2_OrderGetTargetPoint(jass_t *j) { return jass_pushlighthandle(j,(handle_t)(uintptr_t)sc2_order(j)->pt_h,"point"); }
static uint32_t sc2_OrderSetTargetUnit(jass_t *j) {
    sc2GOrder_t *o=sc2_order(j); o->unit_h=(int32_t)(uintptr_t)jass_checkhandle(j,2,"unit");
    o->target_type=2; o->pt_h=0; return 0;
}
static uint32_t sc2_OrderGetTargetUnit(jass_t *j) { return jass_pushlighthandle(j,(handle_t)(uintptr_t)sc2_order(j)->unit_h,"unit"); }
static uint32_t sc2_OrderSetFlag(jass_t *j) {
    sc2GOrder_t *o=sc2_order(j); uint32_t bit=1u<<sc2_checked_index(j,2,32);
    if (jass_checkboolean(j,3)) o->flags |= bit; else o->flags &= ~bit; return 0;
}
static uint32_t sc2_OrderGetFlag(jass_t *j) { return jass_pushboolean(j,(sc2_order(j)->flags & (1u<<sc2_checked_index(j,2,32))) != 0); }
static uint32_t sc2_OrderGetTargetPosition(jass_t *j) {
    sc2GOrder_t *o=sc2_order(j); sc2GPoint_t p;
    if (o->target_type==2) return sc2_unit_location_handle(o->unit_h,&p) ? sc2_point_result(j,p) : jass_pushnullhandle(j,"point");
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)o->pt_h,"point");
}

static uint32_t sc2_UnitOrderIsValid(jass_t *j)   { return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogEntryClass(jass_t *j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogEntryCount(jass_t *j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogEntryGet(jass_t *j)    { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogEntryIsValid(jass_t *j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogEntryParent(jass_t *j) { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogEntryScope(jass_t *j)  { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldCount(jass_t *j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogFieldGet(jass_t *j)    { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldIsArray(jass_t *j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogFieldIsScope(jass_t *j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogFieldType(jass_t *j)   { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldValueCount(jass_t *j){ return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogFieldValueGet(jass_t *j){ return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldValueSet(jass_t *j){ return jass_pushboolean(j, false); }
