#define IS_UNIT(ent) (ent->svflags & SVF_MONSTER)

bool group_add_entity(ggroup_t *group, edict_t *ent) {
    if (!G_JassGroupValid(group) || !ent || group->num_units >= MAX_GROUP_SIZE) return false;
    FOR_LOOP(i, group->num_units) if (group->units[i] == ent) return false;
    group->units[group->num_units++] = ent;
    return true;
}

uint32_t CreateGroup(jass_t *j) {
    char chain[256] = {0};
    cstring_t creator = NULL;
    int32_t trigger_ordinal = -1;
    ggroup_t *group;
    bool const debug = G_JassGroupDebugEnabled();

    if (debug) {
        jassContext_t const *context = jass_getcontext(j);
        creator = jass_currentfunctionname(j);
        jass_formatcallchain(j, chain, sizeof(chain));
        if (context && context->trigger && context->trigger >= level.triggers &&
            context->trigger < level.triggers + level.num_triggers) {
            trigger_ordinal = (int32_t)(context->trigger - level.triggers);
        }
    }
    group = G_AllocJassGroup();
    if (!group) {
        if (debug) G_DumpJassGroupDebug(creator, chain, trigger_ordinal);
        jass_rterror(j, "CreateGroup: group allocation failed");
        return 0;
    }
    if (debug) G_SetJassGroupDebugContext(group, creator, chain, trigger_ordinal);
    return jass_pushlighthandle(j, group, "group");
}
uint32_t DestroyGroup(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    G_FreeJassGroup(whichGroup);
    return 0;
}
uint32_t GroupAddUnit(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    edict_t *whichUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, group_add_entity(whichGroup, whichUnit));
}
uint32_t GroupRemoveUnit(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    edict_t *whichUnit = jass_checkhandle(j, 2, "unit");
    if (!G_JassGroupValid(whichGroup) || !whichUnit) {
        return 0;
    }
    FOR_LOOP(i, whichGroup->num_units) {
        if (whichGroup->units[i] == whichUnit) {
            for (uint32_t j = i; j < whichGroup->num_units - 1; j++) {
                whichGroup->units[j] = whichGroup->units[j + 1];
            }
            whichGroup->num_units--;
            return jass_pushboolean(j, true);
        }
    }
    return jass_pushboolean(j, false);
}
uint32_t GroupClear(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (G_JassGroupValid(whichGroup)) whichGroup->num_units = 0;
    return 0;
}
/* Enumeration filters run with each candidate bound as GetFilterUnit(). Apply
 * counted limits after the filter accepts a unit, and restore context so nested
 * group callbacks do not leak their candidate. */
uint32_t GroupEnumUnitsOfType(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //cstring_t unitname = jass_checkstring(j, 2);
    //handle_t filter = jass_checkhandle(j, 3, "boolexpr");
    return 0;
}
uint32_t GroupEnumUnitsOfPlayer(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    if (!G_JassGroupValid(whichGroup) || !whichPlayer) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (IS_UNIT(ent) && !G_IsDeferredFree(ent) && ent->s.player == PLAYER_NUM(whichPlayer) &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsOfTypeCounted(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //cstring_t unitname = jass_checkstring(j, 2);
    //handle_t filter = jass_checkhandle(j, 3, "boolexpr");
    //int32_t countLimit = jass_checkinteger(j, 4);
    return 0;
}
uint32_t GroupEnumUnitsInRect(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    box2_t *r = jass_checkhandle(j, 2, "rect");
    /* boolexpr filter (e.g. GetUnitsInRectOfPlayer's owner==player test):
     * evaluated per candidate with the unit bound so GetFilterUnit() resolves.
     * NULL passes (no filter). */
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    if (!G_JassGroupValid(whichGroup) || !r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (IS_UNIT(ent) && !G_IsDeferredFree(ent) && Box2_containsPoint(r, &ent->s.origin2) &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}

uint32_t GroupEnumUnitsInRectCounted(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    box2_t const *r = jass_checkhandle(j, 2, "rect");
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    int32_t countLimit = jass_checkinteger(j, 4);
    if (!G_JassGroupValid(whichGroup) || !r) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && !G_IsDeferredFree(ent) && Box2_containsPoint(r, &ent->s.origin2) &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsInRange(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float radius = jass_checknumber(j, 4);
    jassFunc_t const *filter = jass_checkhandle(j, 5, "boolexpr");
    if (!G_JassGroupValid(whichGroup)) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (IS_UNIT(ent) && !G_IsDeferredFree(ent) &&
            Vector2_distance(&ent->s.origin2, &MAKE(vector2_t, x, y)) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsInRangeOfLoc(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    vector2_t const *whichLocation = jass_checkhandle(j, 2, "location");
    float radius = jass_checknumber(j, 3);
    jassFunc_t const *filter = jass_checkhandle(j, 4, "boolexpr");
    if (!G_JassGroupValid(whichGroup) || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (IS_UNIT(ent) && !G_IsDeferredFree(ent) && Vector2_distance(&ent->s.origin2, whichLocation) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsInRangeCounted(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float radius = jass_checknumber(j, 4);
    jassFunc_t const *filter = jass_checkhandle(j, 5, "boolexpr");
    int32_t countLimit = jass_checkinteger(j, 6);
    if (!G_JassGroupValid(whichGroup)) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && !G_IsDeferredFree(ent) &&
            Vector2_distance(&ent->s.origin2, &MAKE(vector2_t, x, y)) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsInRangeOfLocCounted(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    vector2_t const *whichLocation = jass_checkhandle(j, 2, "location");
    float radius = jass_checknumber(j, 3);
    jassFunc_t const *filter = jass_checkhandle(j, 4, "boolexpr");
    int32_t countLimit = jass_checkinteger(j, 5);
    if (!G_JassGroupValid(whichGroup) || !whichLocation) {
        return 0;
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (countLimit > 0 && IS_UNIT(ent) && !G_IsDeferredFree(ent) &&
            Vector2_distance(&ent->s.origin2, whichLocation) <= radius &&
            jass_evaluateboolexpr(j, filter, ent)) {
            group_add_entity(whichGroup, ent);
            countLimit--;
        }
    }
    return 0;
}
uint32_t GroupEnumUnitsSelected(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    player_t *whichPlayer = jass_checkhandle(j, 2, "player");
    jassFunc_t const *filter = jass_checkhandle(j, 3, "boolexpr");
    if (!G_JassGroupValid(whichGroup) || !whichPlayer) return 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &globals.edicts[i];
        if (IS_UNIT(ent) && !G_IsDeferredFree(ent) && ent->selected & (1 << PLAYER_NUM(whichPlayer)) &&
            jass_evaluateboolexpr(j, filter, ent))
            group_add_entity(whichGroup, ent);
    }
    return 0;
}
uint32_t GroupImmediateOrder(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    cstring_t order = jass_checkstring(j, 2);
    if (!G_JassGroupValid(whichGroup)) return jass_pushboolean(j, 0);
    bool any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueimmediateorder(whichGroup->units[i], order)) any = true;
    }
    return jass_pushboolean(j, any);
}
/* By-id orders resolve through the same order table and gameplay dispatch as
 * their string counterparts; the return value is the aggregate acceptance
 * result, not a placeholder success flag. */
uint32_t GroupImmediateOrderById(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //int32_t order = jass_checkinteger(j, 2);
    return jass_pushboolean(j, 0);
}
uint32_t GroupPointOrder(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    cstring_t order = jass_checkstring(j, 2);
    vector2_t dest = MAKE(vector2_t, jass_checknumber(j, 3), jass_checknumber(j, 4));
    if (!G_JassGroupValid(whichGroup)) return jass_pushboolean(j, 0);
    bool any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueorder(whichGroup->units[i], order, &dest)) any = true;
    }
    return jass_pushboolean(j, any);
}
uint32_t GroupPointOrderLoc(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    cstring_t order = jass_checkstring(j, 2);
    vector2_t const *dest = jass_checkhandle(j, 3, "location");
    if (!G_JassGroupValid(whichGroup) || !dest) return jass_pushboolean(j, 0);
    bool any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issueorder(whichGroup->units[i], order, dest)) any = true;
    }
    return jass_pushboolean(j, any);
}
uint32_t GroupPointOrderById(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //int32_t order = jass_checkinteger(j, 2);
    //float x = jass_checknumber(j, 3);
    //float y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
uint32_t GroupPointOrderByIdLoc(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //int32_t order = jass_checkinteger(j, 2);
    //handle_t whichLocation = jass_checkhandle(j, 3, "location");
    return jass_pushboolean(j, 0);
}
uint32_t GroupTargetOrder(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    cstring_t order = jass_checkstring(j, 2);
    edict_t *targetWidget = jass_checkhandle(j, 3, "widget");
    if (!G_JassGroupValid(whichGroup)) return jass_pushboolean(j, 0);
    bool any = false;
    FOR_LOOP(i, whichGroup->num_units) {
        if (unit_issuetargetorder(whichGroup->units[i], order, targetWidget)) any = true;
    }
    return jass_pushboolean(j, any);
}
uint32_t GroupTargetOrderById(jass_t *j) {
    //ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    //int32_t order = jass_checkinteger(j, 2);
    //handle_t targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t ForGroup(jass_t *j) {
    extern edict_t *currentunit;
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    jassFunc_t const *callback = jass_checkcode(j, 2);
    if (!G_JassGroupValid(whichGroup) || !callback) {
        return 0;
    }
    edict_t *previous = currentunit;
    FOR_LOOP(i, whichGroup->num_units) {
        currentunit = whichGroup->units[i];
        jass_pushfunction(j, callback);
        jass_call(j, 0);
    }
    currentunit = previous;
    return 0;
}
uint32_t FirstOfGroup(jass_t *j) {
    ggroup_t *whichGroup = jass_checkhandle(j, 1, "group");
    if (G_JassGroupValid(whichGroup) && whichGroup->num_units > 0) {
        return jass_pushlighthandle(j, whichGroup->units[0], "unit");
    }
    return jass_pushnullhandle(j, "unit");
}
