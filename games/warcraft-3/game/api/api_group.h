#define IS_UNIT(ent) (ent->svflags & SVF_MONSTER)

void group_add_entity(ggroup_t *group, LPEDICT ent) {
    if (!group || !ent || group->num_units >= MAX_GROUP_SIZE) {
        return;
    }
    group->units[group->num_units++] = ent;
}

DWORD CreateGroup(LPJASS j) {
    API_ALLOC(ggroup_t, group);
    (void)group;
    return 1;
}
DWORD GroupAddUnit(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    group_add_entity(whichGroup, whichUnit);
    return 0;
}
DWORD GroupRemoveUnit(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    if (!whichGroup || !whichUnit) {
        return 0;
    }
    FOR_LOOP(i, whichGroup->num_units) {
        if (whichGroup->units[i] == whichUnit) {
            for (DWORD j = i; j < whichGroup->num_units - 1; j++) {
                whichGroup->units[j] = whichGroup->units[j + 1];
            }
            whichGroup->num_units--;
            return 0;
        }
    }
    return 0;
}
DWORD GroupClear(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (whichGroup) whichGroup->num_units = 0;
    return 0;
}
DWORD GroupEnumUnitsOfType(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LPCSTR unitname = jass_checkstring(j, 2);
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    return 0;
}
DWORD GroupEnumUnitsOfPlayer(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichGroup || !whichPlayer) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (ent->s.player == PLAYER_NUM(whichPlayer)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
DWORD GroupEnumUnitsOfTypeCounted(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LPCSTR unitname = jass_checkstring(j, 2);
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    //LONG countLimit = jass_checkinteger(j, 4);
    return 0;
}
DWORD GroupEnumUnitsInRect(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPBOX2 r = jass_checkhandle(j, 2, "rect");
    /* boolexpr filter (e.g. GetUnitsInRectOfPlayer's owner==player test):
     * evaluated per candidate with the unit bound so GetFilterUnit() resolves.
     * NULL passes (no filter). */
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichGroup || !r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Box2_containsPoint(r, &ent->s.origin2) &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}

DWORD GroupEnumUnitsInRectCounted(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    HANDLE r = jass_checkhandle(j, 2, "rect");
//    HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 4);
    if (!whichGroup || !r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Box2_containsPoint(r, &ent->s.origin2) && countLimit > 0) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRange(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT radius = jass_checknumber(j, 4);
    //HANDLE filter = jass_checkhandle(j, 5, "boolexpr");
    if (!whichGroup) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, &MAKE(VECTOR2, x, y)) < radius) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRangeOfLoc(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    FLOAT radius = jass_checknumber(j, 3);
    //HANDLE filter = jass_checkhandle(j, 4, "boolexpr");
    if (!whichGroup || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, whichLocation) < radius) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRangeCounted(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT radius = jass_checknumber(j, 4);
    //HANDLE filter = jass_checkhandle(j, 5, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 6);
    if (!whichGroup) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, &MAKE(VECTOR2, x, y)) < radius && countLimit > 0) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRangeOfLocCounted(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    HANDLE whichLocation = jass_checkhandle(j, 2, "location");
    FLOAT radius = jass_checknumber(j, 3);
    //HANDLE filter = jass_checkhandle(j, 4, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 5);
    if (!whichGroup || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, whichLocation) < radius && countLimit > 0) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
DWORD GroupEnumUnitsSelected(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    return 0;
}
DWORD GroupImmediateOrder(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCSTR order = jass_checkstring(j, 2);
    if (!whichGroup) return jass_pushboolean(j, 0);
    BOOL any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueimmediateorder(whichGroup->units[i], order)) any = true;
    }
    return jass_pushboolean(j, any);
}
DWORD GroupImmediateOrderById(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LONG order = jass_checkinteger(j, 2);
    return jass_pushboolean(j, 0);
}
DWORD GroupPointOrder(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCSTR order = jass_checkstring(j, 2);
    VECTOR2 dest = MAKE(VECTOR2, jass_checknumber(j, 3), jass_checknumber(j, 4));
    if (!whichGroup) return jass_pushboolean(j, 0);
    BOOL any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueorder(whichGroup->units[i], order, &dest)) any = true;
    }
    return jass_pushboolean(j, any);
}
DWORD GroupPointOrderLoc(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCSTR order = jass_checkstring(j, 2);
    LPCVECTOR2 dest = jass_checkhandle(j, 3, "location");
    if (!whichGroup || !dest) return jass_pushboolean(j, 0);
    BOOL any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueorder(whichGroup->units[i], order, dest)) any = true;
    }
    return jass_pushboolean(j, any);
}
DWORD GroupPointOrderById(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LONG order = jass_checkinteger(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
DWORD GroupPointOrderByIdLoc(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LONG order = jass_checkinteger(j, 2);
    //HANDLE whichLocation = jass_checkhandle(j, 3, "location");
    return jass_pushboolean(j, 0);
}
DWORD GroupTargetOrder(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCSTR order = jass_checkstring(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    if (!whichGroup) return jass_pushboolean(j, 0);
    BOOL any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issuetargetorder(whichGroup->units[i], order, targetWidget)) any = true;
    }
    return jass_pushboolean(j, any);
}
DWORD GroupTargetOrderById(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LONG order = jass_checkinteger(j, 2);
    //HANDLE targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, 0);
}
DWORD ForGroup(LPJASS j) {
    extern LPEDICT currentunit;
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCJASSFUNC callback = jass_checkcode(j, 2);
    if (!whichGroup || !callback) {
        return 0;
    }
    FOR_LOOP(i, whichGroup->num_units) {
        currentunit = whichGroup->units[i];
        jass_pushfunction(j, callback);
        jass_call(j, 0);
    }
    return 0;
}
DWORD FirstOfGroup(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (whichGroup && whichGroup->num_units > 0) {
        return jass_pushlighthandle(j, whichGroup->units[0], "unit");
    }
    return jass_pushnullhandle(j, "unit");
}
