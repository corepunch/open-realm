/* galaxy_catalog.h — ability, order, and catalog natives */

#define MAX_GALAXY_ABILCMDS 1024
typedef struct { char ability[64]; int32_t cmd_idx; } sc2GAbilCmd_t;
static sc2GAbilCmd_t sc2_gabilcmds[MAX_GALAXY_ABILCMDS];
static int32_t sc2_gabilcmd_n = 1; /* 1-based; 0 = null */

#define MAX_GALAXY_ORDERS 1024
typedef struct { int32_t abilcmd_h; int32_t pt_h; int32_t unit_h; } sc2GOrder_t;
static sc2GOrder_t sc2_gorders[MAX_GALAXY_ORDERS];
static int32_t sc2_gorder_n = 1;  /* 1-based; 0 = null */

static uint32_t sc2_AbilityClass(LPJASS j)       { return jass_pushinteger(j, 0); }
static uint32_t sc2_AbilityCommand(LPJASS j) {
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
static uint32_t sc2_AbilityCommandGetAbility(LPJASS j) {
    int32_t h = jass_checkinteger(j, 1);
    return jass_pushstring(j, (h > 0 && h < sc2_gabilcmd_n) ? sc2_gabilcmds[h].ability : "");
}
static uint32_t sc2_AbilityCommandGetCommand(LPJASS j) {
    int32_t h = jass_checkinteger(j, 1);
    return jass_pushinteger(j, (h > 0 && h < sc2_gabilcmd_n) ? sc2_gabilcmds[h].cmd_idx : 0);
}
/* Action index is a higher-level concept (e.g. cast vs auto-cast); stub as 0 for now. */
static uint32_t sc2_AbilityCommandGetAction(LPJASS j) { (void)j; return jass_pushinteger(j, 0); }
static uint32_t sc2_Order(LPJASS j)              { return jass_pushnullhandle(j, "order"); }
static uint32_t sc2_OrderTargetingPoint(LPJASS j) {
    int32_t abilcmd_h = jass_checkinteger(j, 1);
    int32_t pt_h      = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (sc2_gorder_n < MAX_GALAXY_ORDERS) {
        int32_t h = sc2_gorder_n++;
        sc2_gorders[h] = (sc2GOrder_t){ abilcmd_h, pt_h };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "order");
    }
    fprintf(stderr, "OrderTargetingPoint: table full (%d entries)\n", MAX_GALAXY_ORDERS);
    return jass_pushnullhandle(j, "order");
}
static uint32_t sc2_OrderTargetingUnit(LPJASS j) {
    int32_t abilcmd_h = jass_checkinteger(j, 1);
    int32_t unit_h    = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "unit");
    if (sc2_gorder_n < MAX_GALAXY_ORDERS) {
        int32_t h = sc2_gorder_n++;
        sc2_gorders[h] = (sc2GOrder_t){ abilcmd_h, 0, unit_h };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "order");
    }
    return jass_pushnullhandle(j, "order");
}
static uint32_t sc2_OrderSetPlayer(LPJASS j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitOrderIsValid(LPJASS j)   { return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogEntryClass(LPJASS j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogEntryCount(LPJASS j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogEntryGet(LPJASS j)    { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogEntryIsValid(LPJASS j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogEntryParent(LPJASS j) { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogEntryScope(LPJASS j)  { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldCount(LPJASS j)  { return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogFieldGet(LPJASS j)    { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldIsArray(LPJASS j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogFieldIsScope(LPJASS j){ return jass_pushboolean(j, false); }
static uint32_t sc2_CatalogFieldType(LPJASS j)   { return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldValueCount(LPJASS j){ return jass_pushinteger(j, 0); }
static uint32_t sc2_CatalogFieldValueGet(LPJASS j){ return jass_pushstring(j, ""); }
static uint32_t sc2_CatalogFieldValueSet(LPJASS j){ return jass_pushboolean(j, false); }
