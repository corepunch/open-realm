uint32_t AddWeatherEffect(LPJASS j) {
    LPCBOX2 where = jass_checkhandle(j, 1, "rect");
    uint32_t effectID = (uint32_t)jass_checkinteger(j, 2);
    LPGWEATHER effect = G_WeatherAdd(where, effectID, false);
    return effect ? jass_pushlighthandle(j, effect, "weathereffect")
                  : jass_pushnullhandle(j, "weathereffect");
}
uint32_t RemoveWeatherEffect(LPJASS j) {
    LPGWEATHER whichEffect = jass_checkhandle(j, 1, "weathereffect");
    G_WeatherRemove(whichEffect);
    return 0;
}
uint32_t EnableWeatherEffect(LPJASS j) {
    LPGWEATHER whichEffect = jass_checkhandle(j, 1, "weathereffect");
    bool enable = jass_checkboolean(j, 2);
    G_WeatherEnable(whichEffect, enable);
    return 0;
}

static bool JassEffectType(LPJASS j, int32_t arg, wc3EffectType_t *out) {
    uint32_t * type = jass_checkhandle(j, arg, "effecttype");
    if (!type || *type > WC3_EFFECT_LIGHTNING) return false;
    *out = (wc3EffectType_t)*type;
    return true;
}

static uint32_t JassPushEffect(LPJASS j, LPEDICT effect) {
    return effect ? jass_pushlighthandle(j, effect, "effect") : jass_pushnullhandle(j, "effect");
}

static uint32_t JassAbilityStringId(cstring_t ability) {
    uint32_t id = 0;
    if (ability && strlen(ability) >= 4) memcpy(&id, ability, 4);
    return id;
}

uint32_t AddSpecialEffect(LPJASS j) {
    cstring_t modelName = jass_checkstring(j, 1);
    VECTOR2 where = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    return JassPushEffect(j, G_SpawnModelEffect(modelName, &where, NULL, NULL, false));
}
uint32_t AddSpecialEffectLoc(LPJASS j) {
    cstring_t modelName = jass_checkstring(j, 1);
    LPCVECTOR2 where = jass_checkhandle(j, 2, "location");
    return JassPushEffect(j, where ? G_SpawnModelEffect(modelName, where, NULL, NULL, false) : NULL);
}
uint32_t AddSpecialEffectTarget(LPJASS j) {
    cstring_t modelName = jass_checkstring(j, 1);
    LPEDICT targetWidget = jass_checkhandle(j, 2, "widget");
    cstring_t attachPointName = jass_checkstring(j, 3);
    return JassPushEffect(j, targetWidget
        ? G_SpawnModelEffect(modelName, NULL, targetWidget, attachPointName, false)
        : NULL);
}
uint32_t DestroyEffect(LPJASS j) {
    LPEDICT whichEffect = jass_checkhandle(j, 1, "effect");
    G_DestroyEffect(whichEffect);
    return 0;
}
uint32_t AddSpellEffect(LPJASS j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    VECTOR2 where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
uint32_t AddSpellEffectLoc(LPJASS j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    LPCVECTOR2 where = jass_checkhandle(j, 3, "location");
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
uint32_t AddSpellEffectById(LPJASS j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    VECTOR2 where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
uint32_t AddSpellEffectByIdLoc(LPJASS j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    LPCVECTOR2 where = jass_checkhandle(j, 3, "location");
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
uint32_t AddSpellEffectTarget(LPJASS j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    cstring_t attachPoint = jass_checkstring(j, 4);
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
uint32_t AddSpellEffectTargetById(LPJASS j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    cstring_t attachPoint = jass_checkstring(j, 4);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
