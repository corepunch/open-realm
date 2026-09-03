DWORD AddWeatherEffect(LPJASS j) {
    //HANDLE where = jass_checkhandle(j, 1, "rect");
    //LONG effectID = jass_checkinteger(j, 2);
    return jass_pushnullhandle(j, "weathereffect");
}
DWORD RemoveWeatherEffect(LPJASS j) {
    //HANDLE whichEffect = jass_checkhandle(j, 1, "weathereffect");
    return 0;
}
DWORD EnableWeatherEffect(LPJASS j) {
    //HANDLE whichEffect = jass_checkhandle(j, 1, "weathereffect");
    //BOOL enable = jass_checkboolean(j, 2);
    return 0;
}

static BOOL JassEffectType(LPJASS j, LONG arg, wc3EffectType_t *out) {
    LPDWORD type = jass_checkhandle(j, arg, "effecttype");
    if (!type || *type > WC3_EFFECT_LIGHTNING) return false;
    *out = (wc3EffectType_t)*type;
    return true;
}

static DWORD JassPushEffect(LPJASS j, LPEDICT effect) {
    return effect ? jass_pushlighthandle(j, effect, "effect") : jass_pushnullhandle(j, "effect");
}

static DWORD JassAbilityStringId(LPCSTR ability) {
    DWORD id = 0;
    if (ability && strlen(ability) >= 4) memcpy(&id, ability, 4);
    return id;
}

DWORD AddSpecialEffect(LPJASS j) {
    LPCSTR modelName = jass_checkstring(j, 1);
    VECTOR2 where = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    return JassPushEffect(j, G_SpawnModelEffect(modelName, &where, NULL, NULL, false));
}
DWORD AddSpecialEffectLoc(LPJASS j) {
    LPCSTR modelName = jass_checkstring(j, 1);
    LPCVECTOR2 where = jass_checkhandle(j, 2, "location");
    return JassPushEffect(j, where ? G_SpawnModelEffect(modelName, where, NULL, NULL, false) : NULL);
}
DWORD AddSpecialEffectTarget(LPJASS j) {
    LPCSTR modelName = jass_checkstring(j, 1);
    LPEDICT targetWidget = jass_checkhandle(j, 2, "widget");
    LPCSTR attachPointName = jass_checkstring(j, 3);
    return JassPushEffect(j, targetWidget
        ? G_SpawnModelEffect(modelName, NULL, targetWidget, attachPointName, false)
        : NULL);
}
DWORD DestroyEffect(LPJASS j) {
    LPEDICT whichEffect = jass_checkhandle(j, 1, "effect");
    G_DestroyEffect(whichEffect);
    return 0;
}
DWORD AddSpellEffect(LPJASS j) {
    LPCSTR abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    VECTOR2 where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    DWORD abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
DWORD AddSpellEffectLoc(LPJASS j) {
    LPCSTR abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    LPCVECTOR2 where = jass_checkhandle(j, 3, "location");
    DWORD abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
DWORD AddSpellEffectById(LPJASS j) {
    DWORD abilityId = (DWORD)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    VECTOR2 where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
DWORD AddSpellEffectByIdLoc(LPJASS j) {
    DWORD abilityId = (DWORD)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    LPCVECTOR2 where = jass_checkhandle(j, 3, "location");
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
DWORD AddSpellEffectTarget(LPJASS j) {
    LPCSTR abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    LPCSTR attachPoint = jass_checkstring(j, 4);
    DWORD abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
DWORD AddSpellEffectTargetById(LPJASS j) {
    DWORD abilityId = (DWORD)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    LPCSTR attachPoint = jass_checkstring(j, 4);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
