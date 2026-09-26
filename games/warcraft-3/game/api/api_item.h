uint32_t CreateItem(jass_t *j) {
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
    edict_t *item = SP_SpawnAtLocation(itemid, 0, &MAKE(vector2_t, x, y));
    return jass_pushlighthandle(j, item, "item");
}
uint32_t RemoveItem(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) G_RemoveItem(whichItem);
    return 0;
}
uint32_t GetItemPlayer(jass_t *j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushnullhandle(j, "player");
}
uint32_t GetItemTypeId(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)item->class_id : 0);
}
/* GetItemType: the item's classification (itemtype enum), read data-driven from
 * ItemData's "icla"/itemClass column and mapped to the ITEM_TYPE_* indices
 * (common.j: 0=Permanent..6=Miscellaneous, 7=Unknown).  Pushed as an itemtype
 * handle exactly like ConvertItemType, so `set t = GetItemType(i)` gets 1 value
 * (an unregistered/void-returning stub here desynced the VM stack). */
uint32_t GetItemType(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    cstring_t cls = item ? item->data.ItemData->itemClass : NULL;
    API_ALLOC(uint32_t, itemtype);
    *itemtype = G_ItemTypeFromClass(cls);
    return 1;
}
uint32_t GetItemLevel(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? item->data.ItemData->level : 0);
}
uint32_t GetItemCharges(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item ? (int32_t)G_ItemCharges(item) : 0);
}
uint32_t SetItemCharges(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    int32_t charges = jass_checkinteger(j, 2);
    if (item) G_SetItemCharges(item, (uint32_t)MAX(charges, 0));
    return 0;
}
uint32_t SetItemDropID(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    int32_t unit_id = jass_checkinteger(j, 2);
    if (item && G_IsItem(item)) item->item.drop_id = (uint32_t)unit_id;
    return 0;
}
uint32_t GetItemDropID(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, item && G_IsItem(item) ? (int32_t)item->item.drop_id : 0);
}
uint32_t GetItemX(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.x : 0);
}
uint32_t GetItemY(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    return jass_pushnumber(j, item ? item->s.origin.y : 0);
}
uint32_t SetItemPosition(jass_t *j) {
    edict_t *item = jass_checkhandle(j, 1, "item");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    if (item && item->item.in_world) {
        item->s.origin.x = x;
        item->s.origin.y = y;
        item->s.origin.z = CM_GetHeightAtPoint(x, y);
        item->s.origin2 = MAKE(vector2_t, x, y);
        gi.LinkEntity(item);
    }
    return 0;
}
uint32_t SetItemDropOnDeath(jass_t *j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetItemDroppable(jass_t *j) {
    //handle_t i = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetItemPlayer(jass_t *j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    //bool changeColor = jass_checkboolean(j, 3);
    return 0;
}
uint32_t SetItemInvulnerable(jass_t *j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    //bool flag = jass_checkboolean(j, 2);
    return 0;
}
uint32_t IsItemInvulnerable(jass_t *j) {
    //handle_t whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, 0);
}
uint32_t GetManipulatedItem(jass_t *j) {
    edict_t *item = jass_getcontext(j)->source;
    return item && G_IsItem(item) ? jass_pushlighthandle(j, item, "item") : jass_pushnullhandle(j, "item");
}
uint32_t GetOrderTargetItem(jass_t *j) {
    return jass_pushnullhandle(j, "item");
}
uint32_t GetEnumItem(jass_t *j) {
    extern edict_t *currentenumitem;
    return jass_pushlighthandle(j, currentenumitem, "item");
}
uint32_t EnumItemsInRect(jass_t *j) {
    /* Visit every in-world item inside the rect, exposing each as the enum item
     * (GetEnumItem) while the action runs. Mirrors EnumDestructablesInRect;
     * the boolexpr filter (arg 2) is ignored for now. */
    extern edict_t *currentenumitem;
    box2_t *r = jass_checkhandle(j, 1, "rect");
    jassFunc_t const *actionFunc = jass_checkcode(j, 3);
    if (!r) return 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (G_IsItem(ent) && ent->item.in_world && Box2_containsPoint(r, &ent->s.origin2)) {
            currentenumitem = ent;
            if (actionFunc) { jass_pushfunction(j, actionFunc); jass_call(j, 0); }
        }
    }
    currentenumitem = NULL;
    return 0;
}
uint32_t GetItemName(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    cstring_t name = whichItem ? G_ObjectName(whichItem->class_id) : NULL;
    return jass_pushstring(j, name ? name : "");
}
uint32_t GetItemUserData(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushinteger(j, whichItem ? whichItem->item.user_data : 0);
}
uint32_t SetItemUserData(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    if (whichItem) whichItem->item.user_data = jass_checkinteger(j, 2);
    return 0;
}
uint32_t SetItemVisible(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
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
uint32_t IsItemVisible(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, whichItem && !(whichItem->s.renderfx & RF_HIDDEN) &&
                              !(whichItem->svflags & SVF_NOCLIENT));
}
uint32_t IsItemOwned(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    return jass_pushboolean(j, whichItem && whichItem->item.carrier && !whichItem->item.in_world);
}
uint32_t IsItemPowerup(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    ItemData_t const *data = whichItem ? whichItem->data.ItemData : NULL;
    if (!data && whichItem) data = G_ItemData(whichItem->class_id);
    return jass_pushboolean(j, data && data->powerup);
}
uint32_t SetItemPawnable(jass_t *j) {
    edict_t *whichItem = jass_checkhandle(j, 1, "item");
    bool flag = jass_checkboolean(j, 2);
    if (whichItem) {
        whichItem->item.pawnable_set = true;
        whichItem->item.pawnable = flag;
    }
    return 0;
}
