uint32_t AddWeatherEffect(jass_t *j) {
    box2_t const *where = jass_checkhandle(j, 1, "rect");
    uint32_t effectID = (uint32_t)jass_checkinteger(j, 2);
    gweather_t *effect = G_WeatherAdd(where, effectID, false);
    return effect ? jass_pushlighthandle(j, effect, "weathereffect")
                  : jass_pushnullhandle(j, "weathereffect");
}
uint32_t RemoveWeatherEffect(jass_t *j) {
    gweather_t *whichEffect = jass_checkhandle(j, 1, "weathereffect");
    G_WeatherRemove(whichEffect);
    return 0;
}
uint32_t EnableWeatherEffect(jass_t *j) {
    gweather_t *whichEffect = jass_checkhandle(j, 1, "weathereffect");
    bool enable = jass_checkboolean(j, 2);
    G_WeatherEnable(whichEffect, enable);
    return 0;
}

static bool JassEffectType(jass_t *j, int32_t arg, wc3EffectType_t *out) {
    uint32_t *type = jass_checkhandle(j, arg, "effecttype");
    if (!type || *type > WC3_EFFECT_LIGHTNING) return false;
    *out = (wc3EffectType_t)*type;
    return true;
}

static uint32_t JassPushEffect(jass_t *j, edict_t *effect) {
    return effect ? jass_pushlighthandle(j, effect, "effect") : jass_pushnullhandle(j, "effect");
}

static uint32_t JassAbilityStringId(cstring_t ability) {
    uint32_t id = 0;
    if (ability && strlen(ability) >= 4) memcpy(&id, ability, 4);
    return id;
}

uint32_t AddSpecialEffect(jass_t *j) {
    cstring_t modelName = jass_checkstring(j, 1);
    vector2_t where = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    return JassPushEffect(j, G_SpawnModelEffect(modelName, &where, NULL, NULL, false));
}
uint32_t AddSpecialEffectLoc(jass_t *j) {
    cstring_t modelName = jass_checkstring(j, 1);
    vector2_t const *where = jass_checkhandle(j, 2, "location");
    return JassPushEffect(j, where ? G_SpawnModelEffect(modelName, where, NULL, NULL, false) : NULL);
}
uint32_t AddSpecialEffectTarget(jass_t *j) {
    cstring_t modelName = jass_checkstring(j, 1);
    edict_t *targetWidget = jass_checkhandle(j, 2, "widget");
    cstring_t attachPointName = jass_checkstring(j, 3);
    return JassPushEffect(j, targetWidget
        ? G_SpawnModelEffect(modelName, NULL, targetWidget, attachPointName, false)
        : NULL);
}
uint32_t DestroyEffect(jass_t *j) {
    edict_t *whichEffect = jass_checkhandle(j, 1, "effect");
    G_DestroyEffect(whichEffect);
    return 0;
}
uint32_t AddSpellEffect(jass_t *j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    vector2_t where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
uint32_t AddSpellEffectLoc(jass_t *j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    vector2_t const *where = jass_checkhandle(j, 3, "location");
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
uint32_t AddSpellEffectById(jass_t *j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    vector2_t where = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    if (!abilityId || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, &where, false));
}
uint32_t AddSpellEffectByIdLoc(jass_t *j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    vector2_t const *where = jass_checkhandle(j, 3, "location");
    if (!abilityId || !where || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectAtPoint(abilityId, type, 0, where, false));
}
uint32_t AddSpellEffectTarget(jass_t *j) {
    cstring_t abilityString = jass_checkstring(j, 1);
    wc3EffectType_t type;
    edict_t *targetWidget = jass_checkhandle(j, 3, "widget");
    cstring_t attachPoint = jass_checkstring(j, 4);
    uint32_t abilityId = JassAbilityStringId(abilityString);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
uint32_t AddSpellEffectTargetById(jass_t *j) {
    uint32_t abilityId = (uint32_t)jass_checkinteger(j, 1);
    wc3EffectType_t type;
    edict_t *targetWidget = jass_checkhandle(j, 3, "widget");
    cstring_t attachPoint = jass_checkstring(j, 4);
    if (!abilityId || !targetWidget || !JassEffectType(j, 2, &type)) return jass_pushnullhandle(j, "effect");
    return JassPushEffect(j, G_SpawnAbilityEffectTarget(abilityId, type, 0, targetWidget, attachPoint, false));
}
