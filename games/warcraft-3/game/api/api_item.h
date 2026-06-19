DWORD CreateItem(LPJASS j) {
    LONG itemid = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    LPEDICT item = SP_SpawnAtLocation(itemid, 0, &MAKE(VECTOR2, x, y));
    return jass_pushlighthandle(j, item, "item");
}
DWORD RemoveItem(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) G_FreeEdict(whichItem);
    return 0;
}
DWORD GetItemPlayer(LPJASS j) {
    //HANDLE whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushnullhandle(j, "player");
}
DWORD GetItemTypeId(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (LONG)item->class_id : 0);
}
/* GetItemType: the item's classification (itemtype enum), read data-driven from
 * ItemData's "icla"/itemClass column and mapped to the ITEM_TYPE_* indices
 * (common.j: 0=Permanent..6=Miscellaneous, 7=Unknown).  Pushed as an itemtype
 * handle exactly like ConvertItemType, so `set t = GetItemType(i)` gets 1 value
 * (an unregistered/void-returning stub here desynced the VM stack). */
DWORD GetItemType(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    LPCSTR cls = item ? UnitStringField(ItemsMetaData, item->class_id, "icla") : NULL;
    DWORD type = 7; /* ITEM_TYPE_UNKNOWN */
    if (cls) {
        if      (!strcasecmp(cls, "Permanent"))     type = 0;
        else if (!strcasecmp(cls, "Charged"))       type = 1;
        else if (!strcasecmp(cls, "PowerUp"))       type = 2;
        else if (!strcasecmp(cls, "Artifact"))      type = 3;
        else if (!strcasecmp(cls, "Purchasable"))   type = 4;
        else if (!strcasecmp(cls, "Campaign"))      type = 5;
        else if (!strcasecmp(cls, "Miscellaneous")) type = 6;
    }
    API_ALLOC(DWORD, itemtype);
    *itemtype = type;
    return 1;
}
DWORD GetItemLevel(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? UnitIntegerField(ItemsMetaData, item->class_id, "ilev") : 0);
}
DWORD GetItemX(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.x : 0);
}
DWORD GetItemY(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.y : 0);
}
DWORD SetItemPosition(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    if (item) { item->s.origin.x = x; item->s.origin.y = y; }
    return 0;
}
DWORD SetItemDropOnDeath(LPJASS j) {
    //HANDLE whichItem = jass_checkhandle(j, 1, "item");
    //BOOL flag = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetItemDroppable(LPJASS j) {
    //HANDLE i = jass_checkhandle(j, 1, "item");
    //BOOL flag = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetItemPlayer(LPJASS j) {
    //HANDLE whichItem = jass_checkhandle(j, 1, "item");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //BOOL changeColor = jass_checkboolean(j, 3);
    return 0;
}
DWORD SetItemInvulnerable(LPJASS j) {
    //HANDLE whichItem = jass_checkhandle(j, 1, "item");
    //BOOL flag = jass_checkboolean(j, 2);
    return 0;
}
DWORD IsItemInvulnerable(LPJASS j) {
    //HANDLE whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, 0);
}
DWORD GetManipulatedItem(LPJASS j) {
    return jass_pushnullhandle(j, "item");
}
DWORD GetOrderTargetItem(LPJASS j) {
    return jass_pushnullhandle(j, "item");
}
