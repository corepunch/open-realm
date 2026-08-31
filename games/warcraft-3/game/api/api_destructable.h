DWORD CreateDestructable(LPJASS j) {
    LONG objectid = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT face = jass_checknumber(j, 4);
    FLOAT scale = jass_checknumber(j, 5);
    LONG variation = jass_checkinteger(j, 6);
    LPEDICT d = G_CreateDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                     DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
DWORD CreateDestructableZ(LPJASS j) {
    LONG objectid = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT z = jass_checknumber(j, 4);
    FLOAT face = jass_checknumber(j, 5);
    FLOAT scale = jass_checknumber(j, 6);
    LONG variation = jass_checkinteger(j, 7);
    LPEDICT d = G_CreateDestructable(objectid, x, y, z, DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
DWORD CreateDeadDestructable(LPJASS j) {
    LONG objectid = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT face = jass_checknumber(j, 4);
    FLOAT scale = jass_checknumber(j, 5);
    LONG variation = jass_checkinteger(j, 6);
    LPEDICT d = G_CreateDeadDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
DWORD CreateDeadDestructableZ(LPJASS j) {
    LONG objectid = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT z = jass_checknumber(j, 4);
    FLOAT face = jass_checknumber(j, 5);
    FLOAT scale = jass_checknumber(j, 6);
    LONG variation = jass_checkinteger(j, 7);
    LPEDICT d = G_CreateDeadDestructable(objectid, x, y, z,
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
DWORD RemoveDestructable(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    G_RemoveDestructable(d);
    return 0;
}
DWORD KillDestructable(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    G_KillDestructable(d, NULL);
    return 0;
}
/* Ghidra: SetDestructableInvulnerable=FUN_003f83a0 sets an invuln flag bit on
 * the destructable (vtable+0xac); IsDestructableInvulnerable=FUN_003f83d0 reads
 * it (bit 3 of flags @+0x20).  Our edict already carries `invulnerable`, honored
 * by the damage path, so reuse it. */
DWORD SetDestructableInvulnerable(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    BOOL flag = jass_checkboolean(j, 2);
    if (d) {
        d->invulnerable = flag;
    }
    return 0;
}
DWORD IsDestructableInvulnerable(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushboolean(j, d && d->invulnerable);
}
DWORD EnumDestructablesInRect(LPJASS j) {
    /* Visit every destructable inside the rect, exposing each as the enum
     * destructable (GetEnumDestructable) while the action runs.  Mirrors
     * GroupEnumUnitsInRect + ForGroup; like GroupEnumUnitsInRect we ignore the
     * boolexpr filter (arg 2) for now. */
    extern LPEDICT currentdestructable;
    LPBOX2 r = jass_checkhandle(j, 1, "rect");
    LPCJASSFUNC actionFunc = jass_checkcode(j, 3);
    if (!r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (G_IsDestructable(ent) && Box2_containsPoint(r, &ent->s.origin2)) {
            currentdestructable = ent;
            if (actionFunc) {
                jass_pushfunction(j, actionFunc);
                jass_call(j, 0);
            }
        }
    }
    currentdestructable = NULL;
    return 0;
}
DWORD GetDestructableTypeId(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushinteger(j, d ? (LONG)d->class_id : 0);
}
DWORD GetDestructableX(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.x : 0);
}
DWORD GetDestructableY(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.y : 0);
}
DWORD SetDestructableLife(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    FLOAT life = jass_checknumber(j, 2);
    G_SetDestructableLife(d, life);
    return 0;
}
DWORD GetDestructableLife(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (FLOAT)d->health.value : 0);
}
DWORD SetDestructableMaxLife(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    FLOAT max = jass_checknumber(j, 2);
    if (d) {
        d->health.max_value = MAX(0.0f, max);
        if (d->health.value > d->health.max_value || d->health.max_value <= 0.0f) {
            G_SetDestructableLife(d, d->health.max_value);
        }
    }
    return 0;
}
DWORD GetDestructableMaxLife(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (FLOAT)d->health.max_value : 0);
}
DWORD SetDestructableOccluderHeight(LPJASS j) {
    //HANDLE d = jass_checkhandle(j, 1, "destructable");
    //(void)jass_checknumber(j, 2);
    return 0;
}
DWORD GetDestructableOccluderHeight(LPJASS j) {
    //HANDLE d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, 0);
}
DWORD DestructableRestoreLife(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    FLOAT life = jass_checknumber(j, 2);
    BOOL birth = jass_checkboolean(j, 3);
    G_RestoreDestructable(d, life, birth);
    return 0;
}
DWORD QueueDestructableAnimation(LPJASS j) {
    //HANDLE d = jass_checkhandle(j, 1, "destructable");
    //LPCSTR whichAnimation = jass_checkstring(j, 2);
    return 0;
}
DWORD SetDestructableAnimation(LPJASS j) {
    /* Ghidra: the original SetDestructableAnimation (FUN_003f8740) ignores its
     * args and calls FUN_00418990(0) — effectively a no-op at this layer, so
     * the empty body is already parity. */
    //HANDLE d = jass_checkhandle(j, 1, "destructable");
    //LPCSTR whichAnimation = jass_checkstring(j, 2);
    return 0;
}
/* Ghidra: ShowDestructable=FUN_003f8790 — show (flag!=0) calls the entity's
 * show method (vtable+0x84), hide calls hide (vtable+0x88).  Our equivalent of
 * that visibility toggle is the RF_HIDDEN renderfx bit, exactly as ShowUnit. */
DWORD ShowDestructable(LPJASS j) {
    LPEDICT d = jass_checkhandle(j, 1, "destructable");
    BOOL show = jass_checkboolean(j, 2);
    if (d) {
        BOOL const was_hidden = !!(d->s.renderfx & RF_HIDDEN);
        if (show) {
            d->s.renderfx &= ~RF_HIDDEN;
        } else {
            d->s.renderfx |= RF_HIDDEN;
        }
        if ((d->s.flags & EF_FOW_BLOCKER) && was_hidden != !!(d->s.renderfx & RF_HIDDEN)) G_FowMarkBlockersDirty();
    }
    return 0;
}
