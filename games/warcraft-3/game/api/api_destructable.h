uint32_t CreateDestructable(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float face = jass_checknumber(j, 4);
    float scale = jass_checknumber(j, 5);
    int32_t variation = jass_checkinteger(j, 6);
    edict_t *d = G_CreateDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                     DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t CreateDestructableZ(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float z = jass_checknumber(j, 4);
    float face = jass_checknumber(j, 5);
    float scale = jass_checknumber(j, 6);
    int32_t variation = jass_checkinteger(j, 7);
    edict_t *d = G_CreateDestructable(objectid, x, y, z, DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t CreateDeadDestructable(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float face = jass_checknumber(j, 4);
    float scale = jass_checknumber(j, 5);
    int32_t variation = jass_checkinteger(j, 6);
    edict_t *d = G_CreateDeadDestructable(objectid, x, y, CM_GetHeightAtPoint(x, y),
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t CreateDeadDestructableZ(jass_t *j) {
    int32_t objectid = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float z = jass_checknumber(j, 4);
    float face = jass_checknumber(j, 5);
    float scale = jass_checknumber(j, 6);
    int32_t variation = jass_checkinteger(j, 7);
    edict_t *d = G_CreateDeadDestructable(objectid, x, y, z,
                                         DEG2RAD(face), scale, variation);
    return jass_pushlighthandle(j, d, "destructable");
}
uint32_t RemoveDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    G_RemoveDestructable(d);
    return 0;
}
uint32_t KillDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    G_KillDestructable(d, NULL);
    return 0;
}
/* Ghidra: SetDestructableInvulnerable=FUN_003f83a0 sets an invuln flag bit on
 * the destructable (vtable+0xac); IsDestructableInvulnerable=FUN_003f83d0 reads
 * it (bit 3 of flags @+0x20).  Our edict already carries `invulnerable`, honored
 * by the damage path, so reuse it. */
uint32_t SetDestructableInvulnerable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    bool flag = jass_checkboolean(j, 2);
    if (d) {
        d->invulnerable = flag;
    }
    return 0;
}
uint32_t IsDestructableInvulnerable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushboolean(j, d && d->invulnerable);
}
uint32_t EnumDestructablesInRect(jass_t *j) {
    /* Visit every destructable inside the rect, exposing each as the enum
     * destructable (GetEnumDestructable) while the action runs.  Mirrors
     * GroupEnumUnitsInRect + ForGroup; like GroupEnumUnitsInRect we ignore the
     * boolexpr filter (arg 2) for now. */
    extern edict_t *currentdestructable;
    box2_t *r = jass_checkhandle(j, 1, "rect");
    jassFunc_t const *actionFunc = jass_checkcode(j, 3);
    if (!r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
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
uint32_t GetDestructableTypeId(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushinteger(j, d ? (int32_t)d->class_id : 0);
}
uint32_t GetDestructableX(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.x : 0);
}
uint32_t GetDestructableY(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? d->s.origin.y : 0);
}
uint32_t SetDestructableLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float life = jass_checknumber(j, 2);
    G_SetDestructableLife(d, life);
    return 0;
}
uint32_t GetDestructableLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (float)d->health.value : 0);
}
uint32_t SetDestructableMaxLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float max = jass_checknumber(j, 2);
    if (d) {
        d->health.max_value = MAX(0.0f, max);
        if (d->health.value > d->health.max_value || d->health.max_value <= 0.0f) {
            G_SetDestructableLife(d, d->health.max_value);
        }
    }
    return 0;
}
uint32_t GetDestructableMaxLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, d ? (float)d->health.max_value : 0);
}
uint32_t SetDestructableOccluderHeight(jass_t *j) {
    //handle_t d = jass_checkhandle(j, 1, "destructable");
    //(void)jass_checknumber(j, 2);
    return 0;
}
uint32_t GetDestructableOccluderHeight(jass_t *j) {
    //handle_t d = jass_checkhandle(j, 1, "destructable");
    return jass_pushnumber(j, 0);
}
uint32_t DestructableRestoreLife(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    float life = jass_checknumber(j, 2);
    bool birth = jass_checkboolean(j, 3);
    G_RestoreDestructable(d, life, birth);
    return 0;
}
uint32_t QueueDestructableAnimation(jass_t *j) {
    //handle_t d = jass_checkhandle(j, 1, "destructable");
    //cstring_t whichAnimation = jass_checkstring(j, 2);
    return 0;
}
uint32_t SetDestructableAnimation(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    cstring_t animation = jass_checkstring(j, 2);
    if (G_IsDestructable(d) && animation) {
        G_SetUnitAnimation(d, animation);
        if (d->animation) {
            d->animation_override = true;
            d->s.frame = d->animation->interval[0];
        }
    }
    return 0;
}
/* Ghidra: ShowDestructable=FUN_003f8790 — show (flag!=0) calls the entity's
 * show method (vtable+0x84), hide calls hide (vtable+0x88).  Our equivalent of
 * that visibility toggle is the RF_HIDDEN renderfx bit, exactly as ShowUnit. */
uint32_t ShowDestructable(jass_t *j) {
    edict_t *d = jass_checkhandle(j, 1, "destructable");
    bool show = jass_checkboolean(j, 2);
    if (d) {
        bool const was_hidden = !!(d->s.renderfx & RF_HIDDEN);
        if (show) {
            d->s.renderfx &= ~RF_HIDDEN;
        } else {
            d->s.renderfx |= RF_HIDDEN;
        }
        if ((d->s.flags & EF_FOW_BLOCKER) && was_hidden != !!(d->s.renderfx & RF_HIDDEN)) G_FowMarkBlockersDirty();
    }
    return 0;
}
