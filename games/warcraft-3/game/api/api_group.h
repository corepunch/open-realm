#define IS_UNIT(ent) (ent->svflags & SVF_MONSTER)

BOOL group_add_entity(ggroup_t *group, LPEDICT ent) {
    if (!group || !ent || group->num_units >= MAX_GROUP_SIZE) return false;
    FOR_LOOP(i, group->num_units) if (group->units[i] == ent) return false;
    group->units[group->num_units++] = ent;
    return true;
}

DWORD CreateGroup(LPJASS j) {
    ggroup_t *group = G_AllocJassGroup();
    if (!group) { jass_rterror(j, "CreateGroup: group registry is full"); return 0; }
    return jass_pushlighthandle(j, group, "group");
}
DWORD DestroyGroup(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (whichGroup) whichGroup->num_units = 0;
    return 0;
}
DWORD GroupAddUnit(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, group_add_entity(whichGroup, whichUnit));
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
            return jass_pushboolean(j, true);
        }
    }
    return jass_pushboolean(j, false);
}
DWORD GroupClear(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (whichGroup) whichGroup->num_units = 0;
    return 0;
}
/* Enumeration filters run with each candidate bound as GetFilterUnit(). Apply
 * counted limits after the filter accepts a unit, and restore context so nested
 * group callbacks do not leak their candidate. */
DWORD GroupEnumUnitsOfType(LPJASS j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //LPCSTR unitname = jass_checkstring(j, 2);
    //HANDLE filter = jass_checkhandle(j, 3, "boolexpr");
    return 0;
}
DWORD GroupEnumUnitsOfPlayer(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichGroup || !whichPlayer) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && ent->s.player == PLAYER_NUM(whichPlayer) && jass_evaluateboolexpr(j, filter, ent)) {
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
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 4);
    if (!whichGroup || !r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && Box2_containsPoint(r, &ent->s.origin2) &&
            jass_evaluateboolexpr(j, filter, ent)) {
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
    LPCJASSFUNC filter = jass_checkhandle(j, 5, "boolexpr");
    if (!whichGroup) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, &MAKE(VECTOR2, x, y)) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRangeOfLoc(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    FLOAT radius = jass_checknumber(j, 3);
    LPCJASSFUNC filter = jass_checkhandle(j, 4, "boolexpr");
    if (!whichGroup || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, whichLocation) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
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
    LPCJASSFUNC filter = jass_checkhandle(j, 5, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 6);
    if (!whichGroup) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, &MAKE(VECTOR2, x, y)) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
DWORD GroupEnumUnitsInRangeOfLocCounted(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    FLOAT radius = jass_checknumber(j, 3);
    LPCJASSFUNC filter = jass_checkhandle(j, 4, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 5);
    if (!whichGroup || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && Vector2_distance(&ent->s.origin2, whichLocation) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
DWORD GroupEnumUnitsSelected(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichGroup || !whichPlayer) return 0;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &globals.edicts[i];
        if (IS_UNIT(ent) && ent->selected & (1 << PLAYER_NUM(whichPlayer)) && jass_evaluateboolexpr(j, filter, ent))
            group_add_entity(whichGroup, ent);
    }
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
/* By-id orders resolve through the same order table and gameplay dispatch as
 * their string counterparts; the return value is the aggregate acceptance
 * result, not a placeholder success flag. */
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
    LPEDICT previous = currentunit;
    FOR_LOOP(i, whichGroup->num_units) {
        currentunit = whichGroup->units[i];
        jass_pushfunction(j, callback);
        jass_call(j, 0);
    }
    currentunit = previous;
    return 0;
}
DWORD FirstOfGroup(LPJASS j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (whichGroup && whichGroup->num_units > 0) {
        return jass_pushlighthandle(j, whichGroup->units[0], "unit");
    }
    return jass_pushnullhandle(j, "unit");
}
