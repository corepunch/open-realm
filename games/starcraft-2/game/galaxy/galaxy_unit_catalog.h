/* galaxy_unit_catalog.h — unit abilities, weapons, behaviors, cooldowns, and charges.
 *
 * The lists start from the resolved CUnit AbilArray/WeaponArray/BehaviorArray at spawn (g_unit.h) and
 * live in sc2UnitState_t. These natives keep that bookkeeping; they do not run CAbil, CWeapon, or
 * CBehavior data, so an added behavior has no stat effect and a weapon does not fire.
 * Get(index) natives are 1-based like UnitGroupUnit. */

static cstring_t sc2_link_arg(jass_t *j, int arg) {
    cstring_t link = jass_checkstring(j, arg);
    if (link && strlen(link) >= SC2_LINK_LEN) { jass_rterror(j, "Galaxy catalog link exceeds 47 characters"); return NULL; }
    return link;
}
static sc2UnitState_t *sc2_unit_arg(jass_t *j, int arg) { return sc2_unit_data(j, sc2_ent_from_handle(j, arg)); }

/* Abilities */
static sc2UnitAbil_t *sc2_unit_abil(sc2UnitState_t *u, cstring_t link) {
    if (u && link) for (int i = 0; i < u->abil_n; i++) if (!strcmp(u->abils[i].link, link)) return &u->abils[i];
    return NULL;
}
static uint32_t sc2_UnitAbilityCount(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); return jass_pushinteger(j, u ? u->abil_n : 0);
}
static uint32_t sc2_UnitAbilityGet(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int32_t n = jass_checkinteger(j, 2) - 1;
    return jass_pushstring(j, u && n >= 0 && n < u->abil_n ? u->abils[n].link : "");
}
static uint32_t sc2_UnitAbilityExists(jass_t *j) {
    return jass_pushboolean(j, sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)) != NULL);
}
static uint32_t sc2_UnitAbilityCheck(jass_t *j) {
    sc2UnitAbil_t *a = sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2));
    return jass_pushboolean(j, a && (!jass_checkboolean(j, 3) || !a->disabled));
}
static uint32_t sc2_UnitAbilityEnable(jass_t *j) {
    sc2UnitAbil_t *a = sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)); bool on = jass_checkboolean(j, 3);
    if (a) a->disabled = !on;
    return 0;
}
static uint32_t sc2_UnitAbilityShow(jass_t *j) {
    sc2UnitAbil_t *a = sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)); bool on = jass_checkboolean(j, 3);
    if (a) a->hidden = !on;
    return 0;
}
static uint32_t sc2_UnitAbilityGetLevel(jass_t *j) {
    sc2UnitAbil_t *a = sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)); return jass_pushinteger(j, a ? a->level : 0);
}
static uint32_t sc2_UnitAbilityChangeLevel(jass_t *j) {
    sc2UnitAbil_t *a = sc2_unit_abil(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)); int32_t level = jass_checkinteger(j, 3);
    if (a) a->level = (uint8_t)MAX(1, MIN(255, level));
    return 0;
}

/* Cooldowns and charges. A Get on an untouched link reads zero without claiming a slot. */
static sc2UnitCooldown_t *sc2_unit_cooldown(jass_t *j, sc2UnitState_t *u, cstring_t link, bool create) {
    if (!u || !link) return NULL;
    for (int i = 0; i < u->cooldown_n; i++) if (!strcmp(u->cooldowns[i].link, link)) return &u->cooldowns[i];
    if (!create) return NULL;
    if (u->cooldown_n == SC2_UNIT_COOLDOWNS) { jass_rterror(j, "Unit cooldown table full"); return NULL; }
    sc2UnitCooldown_t *c = &u->cooldowns[u->cooldown_n++];
    *c = (sc2UnitCooldown_t){0}; snprintf(c->link, SC2_LINK_LEN, "%s", link);
    return c;
}
enum { SC2_CD_COOLDOWN, SC2_CD_CHARGE_USED, SC2_CD_CHARGE_REGEN };
static float *sc2_cooldown_field(sc2UnitCooldown_t *c, int field) {
    return field == SC2_CD_COOLDOWN ? &c->cooldown : field == SC2_CD_CHARGE_USED ? &c->charge_used : &c->charge_regen;
}
/* link_arg is the cooldown/charge link: 2 for Unit*, 3 for UnitAbility* and UnitBehavior*. */
static uint32_t sc2_cooldown_get(jass_t *j, int link_arg, int field) {
    sc2UnitCooldown_t *c = sc2_unit_cooldown(j, sc2_unit_arg(j, 1), sc2_link_arg(j, link_arg), false);
    return jass_pushnumber(j, c ? *sc2_cooldown_field(c, field) : 0);
}
static uint32_t sc2_cooldown_add(jass_t *j, int link_arg, int field) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, link_arg);
    float value = jass_checknumber(j, link_arg + 1);
    sc2UnitCooldown_t *c = sc2_unit_cooldown(j, u, link, true);
    if (c) { float *f = sc2_cooldown_field(c, field); *f = MAX(0, *f + value); }
    return 0;
}
static uint32_t sc2_UnitGetCooldown(jass_t *j)                { return sc2_cooldown_get(j, 2, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitAddCooldown(jass_t *j)                { return sc2_cooldown_add(j, 2, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitGetChargeUsed(jass_t *j)              { return sc2_cooldown_get(j, 2, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitAddChargeUsed(jass_t *j)              { return sc2_cooldown_add(j, 2, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitGetChargeRegen(jass_t *j)             { return sc2_cooldown_get(j, 2, SC2_CD_CHARGE_REGEN); }
static uint32_t sc2_UnitAddChargeRegen(jass_t *j)             { return sc2_cooldown_add(j, 2, SC2_CD_CHARGE_REGEN); }
static uint32_t sc2_UnitAbilityGetCooldown(jass_t *j)         { return sc2_cooldown_get(j, 3, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitAbilityAddCooldown(jass_t *j)         { return sc2_cooldown_add(j, 3, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitAbilityGetChargeUsed(jass_t *j)       { return sc2_cooldown_get(j, 3, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitAbilityAddChargeUsed(jass_t *j)       { return sc2_cooldown_add(j, 3, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitAbilityGetChargeRegen(jass_t *j)      { return sc2_cooldown_get(j, 3, SC2_CD_CHARGE_REGEN); }
static uint32_t sc2_UnitAbilityAddChargeRegen(jass_t *j)      { return sc2_cooldown_add(j, 3, SC2_CD_CHARGE_REGEN); }
static uint32_t sc2_UnitBehaviorGetCooldown(jass_t *j)        { return sc2_cooldown_get(j, 3, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitBehaviorAddCooldown(jass_t *j)        { return sc2_cooldown_add(j, 3, SC2_CD_COOLDOWN); }
static uint32_t sc2_UnitBehaviorGetChargeUsed(jass_t *j)      { return sc2_cooldown_get(j, 3, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitBehaviorAddChargeUsed(jass_t *j)      { return sc2_cooldown_add(j, 3, SC2_CD_CHARGE_USED); }
static uint32_t sc2_UnitBehaviorGetChargeRegen(jass_t *j)     { return sc2_cooldown_get(j, 3, SC2_CD_CHARGE_REGEN); }
static uint32_t sc2_UnitBehaviorAddChargeRegen(jass_t *j)     { return sc2_cooldown_add(j, 3, SC2_CD_CHARGE_REGEN); }

/* Weapons */
static int sc2_unit_weapon(sc2UnitState_t const *u, cstring_t link) {
    if (u && link) for (int i = 0; i < u->weapon_n; i++) if (!strcmp(u->weapons[i].link, link)) return i;
    return -1;
}
static uint32_t sc2_UnitWeaponCount(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); return jass_pushinteger(j, u ? u->weapon_n : 0);
}
static uint32_t sc2_UnitWeaponGet(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int32_t n = jass_checkinteger(j, 2) - 1;
    return jass_pushstring(j, u && n >= 0 && n < u->weapon_n ? u->weapons[n].link : "");
}
static uint32_t sc2_UnitWeaponIsEnabled(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int32_t n = jass_checkinteger(j, 2) - 1;
    return jass_pushboolean(j, u && n >= 0 && n < u->weapon_n && !u->weapons[n].disabled);
}
/* The turret argument only selects presentation; membership is the weapon link. */
static uint32_t sc2_UnitWeaponAdd(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, 2);
    if (!u || !link || !*link || sc2_unit_weapon(u, link) >= 0) return 0;
    if (u->weapon_n == SC2_UNIT_WEAPONS) { jass_rterror(j, "Unit weapon table full"); return 0; }
    u->weapons[u->weapon_n++] = (sc2UnitWeapon_t){0};
    snprintf(u->weapons[u->weapon_n - 1].link, SC2_LINK_LEN, "%s", link);
    return 0;
}
static uint32_t sc2_UnitWeaponRemove(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int i = sc2_unit_weapon(u, sc2_link_arg(j, 2));
    if (i >= 0) memmove(&u->weapons[i], &u->weapons[i + 1], (size_t)(--u->weapon_n - i) * sizeof(u->weapons[0]));
    return 0;
}

/* Behaviors. Stacks of one link share a slot; count c_unitBehaviorCountAll (-1) removes every stack. */
#define SC2_BEHAVIOR_COUNT_ALL (-1) // natives.galaxy c_unitBehaviorCountAll
static int sc2_unit_behavior(sc2UnitState_t const *u, cstring_t link) {
    if (u && link) for (int i = 0; i < u->behavior_n; i++) if (!strcmp(u->behaviors[i].link, link)) return i;
    return -1;
}
static sc2UnitBehavior_t *sc2_behavior_add(jass_t *j, sc2UnitState_t *u, cstring_t link, int32_t count) {
    int i = sc2_unit_behavior(u, link);
    if (!u || !link || !*link || count <= 0) return NULL;
    if (i < 0) {
        if (u->behavior_n == SC2_UNIT_BEHAVIORS) { jass_rterror(j, "Unit behavior table full"); return NULL; }
        i = u->behavior_n++;
        u->behaviors[i] = (sc2UnitBehavior_t){0}; snprintf(u->behaviors[i].link, SC2_LINK_LEN, "%s", link);
    }
    u->behaviors[i].count += count;
    return &u->behaviors[i];
}
/* Returns how many stacks were removed. */
static int32_t sc2_behavior_remove(sc2UnitState_t *u, cstring_t link, int32_t count) {
    int i = sc2_unit_behavior(u, link);
    if (i < 0 || (count <= 0 && count != SC2_BEHAVIOR_COUNT_ALL)) return 0;
    sc2UnitBehavior_t *b = &u->behaviors[i];
    int32_t removed = count == SC2_BEHAVIOR_COUNT_ALL ? b->count : MIN(count, b->count);
    if ((b->count -= removed) <= 0) memmove(b, b + 1, (size_t)(--u->behavior_n - i) * sizeof(*b));
    return removed;
}
static uint32_t sc2_UnitBehaviorAdd(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, 2);
    (void)jass_checkhandle(j, 3, "unit"); /* caster only matters to behavior effects, which are not run */
    sc2_behavior_add(j, u, link, jass_checkinteger(j, 4)); return 0;
}
static uint32_t sc2_UnitBehaviorAddPlayer(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, 2); (void)sc2_player_index(j, 3);
    sc2_behavior_add(j, u, link, jass_checkinteger(j, 4)); return 0;
}
static uint32_t sc2_UnitBehaviorRemove(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, 2);
    if (u) sc2_behavior_remove(u, link, jass_checkinteger(j, 3));
    return 0;
}
static uint32_t sc2_UnitBehaviorRemovePlayer(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); cstring_t link = sc2_link_arg(j, 2); (void)sc2_player_index(j, 3);
    if (u) sc2_behavior_remove(u, link, jass_checkinteger(j, 4));
    return 0;
}
static uint32_t sc2_UnitBehaviorTransfer(jass_t *j) {
    sc2UnitState_t *from = sc2_unit_arg(j, 1), *to = sc2_unit_arg(j, 2); cstring_t link = sc2_link_arg(j, 3);
    int32_t count = jass_checkinteger(j, 4); int i = sc2_unit_behavior(from, link);
    if (i < 0 || !to || from == to) return 0;
    float duration = from->behaviors[i].duration;
    sc2UnitBehavior_t *b = sc2_behavior_add(j, to, link, sc2_behavior_remove(from, link, count));
    if (b && duration > 0) b->duration = MAX(b->duration, duration);
    return 0;
}
static uint32_t sc2_UnitHasBehavior(jass_t *j) {
    return jass_pushboolean(j, sc2_unit_behavior(sc2_unit_arg(j, 1), sc2_link_arg(j, 2)) >= 0);
}
/* Behaviors are removed rather than disabled here, so a present behavior is enabled. */
static uint32_t sc2_UnitBehaviorEnabled(jass_t *j) { return sc2_UnitHasBehavior(j); }
static uint32_t sc2_UnitBehaviorCount(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int i = sc2_unit_behavior(u, sc2_link_arg(j, 2));
    return jass_pushinteger(j, i >= 0 ? u->behaviors[i].count : 0);
}
static uint32_t sc2_UnitBehaviorCountAll(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int32_t n = 0;
    if (u) for (int i = 0; i < u->behavior_n; i++) n += u->behaviors[i].count;
    return jass_pushinteger(j, n);
}
static uint32_t sc2_UnitBehaviorGet(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int32_t n = jass_checkinteger(j, 2) - 1;
    return jass_pushstring(j, u && n >= 0 && n < u->behavior_n ? u->behaviors[n].link : "");
}
static uint32_t sc2_UnitBehaviorDuration(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int i = sc2_unit_behavior(u, sc2_link_arg(j, 2));
    return jass_pushnumber(j, i >= 0 ? u->behaviors[i].duration : 0);
}
/* A positive duration counts down in SC2_UnitAdvanceTimers and removes the behavior at zero. */
static uint32_t sc2_UnitBehaviorSetDuration(jass_t *j) {
    sc2UnitState_t *u = sc2_unit_arg(j, 1); int i = sc2_unit_behavior(u, sc2_link_arg(j, 2));
    float duration = jass_checknumber(j, 3);
    if (i >= 0) u->behaviors[i].duration = MAX(0, duration);
    return 0;
}
