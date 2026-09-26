uint32_t CreateItem(LPJASS j) {
    int32_t itemid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    ItemData_t const *data;

    /* Human09 recreates Muradin's inventory after the Frostmourne cinematic.
     * Empty inventory slots arrive here as item ID 0; do not pass that
     * sentinel into the generic entity spawner. */
    if (!itemid) {
        fprintf(stderr, "CreateItem: refusing empty item ID at (%.1f, %.1f)\n", x, y);
        return jass_pushnullhandle(j, "item");
    }
    data = G_ItemData((uint32_t)itemid);
    if (!data || !data->file) {
        fprintf(stderr, "CreateItem: unresolved item ID 0x%08x at (%.1f, %.1f)\n",
                (uint32_t)itemid, x, y);
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = SP_SpawnAtLocation(itemid, 0, &MAKE(VECTOR2, x, y));
    return jass_pushlighthandle(j, item, "item");
}
uint32_t RemoveItem(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) G_RemoveItem(whichItem);
    return 0;
}
uint32_t GetItemPlayer(LPJASS j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushnullhandle(j, "player");
}
uint32_t GetItemTypeId(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)item->class_id : 0);
}
/* GetItemType: the item's classification (itemtype enum), read data-driven from
 * ItemData's "icla"/itemClass column and mapped to the ITEM_TYPE_* indices
 * (common.j: 0=Permanent..6=Miscellaneous, 7=Unknown).  Pushed as an itemtype
 * handle exactly like ConvertItemType, so `set t = GetItemType(i)` gets 1 value
 * (an unregistered/void-returning stub here desynced the VM stack). */
uint32_t GetItemType(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    cstring_t cls = item ? item->data.ItemData->itemClass : NULL;
    API_ALLOC(uint32_t, itemtype);
    *itemtype = G_ItemTypeFromClass(cls);
    return 1;
}
uint32_t GetItemLevel(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? item->data.ItemData->level : 0);
}
uint32_t GetItemCharges(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)G_ItemCharges(item) : 0);
}
uint32_t SetItemCharges(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    int32_t charges = jass_checkinteger(j, 2);
    if (item) G_SetItemCharges(item, (uint32_t)MAX(charges, 0));
    return 0;
}
uint32_t SetItemDropID(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    int32_t unit_id = jass_checkinteger(j, 2);
    if (item && G_IsItem(item)) item->item.drop_id = (uint32_t)unit_id;
    return 0;
}
uint32_t GetItemDropID(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item && G_IsItem(item) ? (int32_t)item->item.drop_id : 0);
}
uint32_t GetItemX(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.x : 0);
}
uint32_t GetItemY(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.y : 0);
}
uint32_t SetItemPosition(LPJASS j) {
    LPEDICT item = jass_checkhandle(j, 1, "item");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    if (item && item->item.in_world) {
        item->s.origin.x = x;
        item->s.origin.y = y;
        item->s.origin.z = CM_GetHeightAtPoint(x, y);
        item->s.origin2 = MAKE(VECTOR2, x, y);
        gi.LinkEntity(item);
    }
    return 0;
}
uint32_t SetItemDropOnDeath(LPJASS j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetItemDroppable(LPJASS j) {
    //handle_t i = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetItemPlayer(LPJASS j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //bool changeColor = jass_checkboolean(j, 3);
    return 0;
}
uint32_t SetItemInvulnerable(LPJASS j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t IsItemInvulnerable(LPJASS j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, 0);
}
uint32_t GetManipulatedItem(LPJASS j) {
    LPEDICT item = jass_getcontext(j)->source;
    return item && G_IsItem(item) ? jass_pushlighthandle(j, item, "item") : jass_pushnullhandle(j, "item");
}
uint32_t GetOrderTargetItem(LPJASS j) {
    return jass_pushnullhandle(j, "item");
}
uint32_t GetEnumItem(LPJASS j) {
    extern LPEDICT currentenumitem;
    return jass_pushlighthandle(j, currentenumitem, "item");
}
uint32_t EnumItemsInRect(LPJASS j) {
    /* Visit every in-world item inside the rect, exposing each as the enum item
     * (GetEnumItem) while the action runs. Mirrors EnumDestructablesInRect;
     * the boolexpr filter (arg 2) is ignored for now. */
    extern LPEDICT currentenumitem;
    LPBOX2 r = jass_checkhandle(j, 1, "rect");
    LPCJASSFUNC actionFunc = jass_checkcode(j, 3);
    if (!r) return 0;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (G_IsItem(ent) && ent->item.in_world && Box2_containsPoint(r, &ent->s.origin2)) {
            currentenumitem = ent;
            if (actionFunc) { jass_pushfunction(j, actionFunc); jass_call(j, 0); }
        }
    }
    currentenumitem = NULL;
    return 0;
}
uint32_t GetItemName(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    cstring_t name = whichItem ? G_ObjectName(whichItem->class_id) : NULL;
    return jass_pushstring(j, name ? name : "");
}
uint32_t GetItemUserData(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, whichItem ? whichItem->item.user_data : 0);
}
uint32_t SetItemUserData(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) whichItem->item.user_data = jass_checkinteger(j, 2);
    return 0;
}
uint32_t SetItemVisible(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    bool show = jass_checkboolean(j, 2);
    if (!whichItem || !G_IsItem(whichItem)) return 0;
    if (show) {
        whichItem->s.renderfx &= ~RF_HIDDEN;
        whichItem->svflags &= ~SVF_NOCLIENT;
    } else {
        whichItem->s.renderfx |= RF_HIDDEN;
        whichItem->svflags |= SVF_NOCLIENT;
    }
    return 0;
}
uint32_t IsItemVisible(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, whichItem && !(whichItem->s.renderfx & RF_HIDDEN) &&
                              !(whichItem->svflags & SVF_NOCLIENT));
}
uint32_t IsItemOwned(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, whichItem && whichItem->item.carrier && !whichItem->item.in_world);
}
uint32_t IsItemPowerup(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    ItemData_t const *data = whichItem ? whichItem->data.ItemData : NULL;
    if (!data && whichItem) data = G_ItemData(whichItem->class_id);
    return jass_pushboolean(j, data && data->powerup);
}
uint32_t SetItemPawnable(LPJASS j) {
    LPEDICT whichItem = jass_checkhandle(j, 1, "item");
    bool flag = jass_checkboolean(j, 2);
    if (whichItem) {
        whichItem->item.pawnable_set = true;
        whichItem->item.pawnable = flag;
    }
    return 0;
}
