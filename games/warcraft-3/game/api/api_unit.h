extern LPPLAYER currentplayer;

static uint32_t const order_ugol = BZ_WC3_UNIT_HAUNTED_GOLD_MINE;
static uint32_t const unit_ngol = BZ_WC3_UNIT_GOLD_MINE;

#define UNIT_TYPED_ACCESS(NAME, FIELD, TYPE) \
uint32_t SetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    if (whichUnit) { \
        memcpy(&whichUnit->FIELD, jass_checkhandle(j, 2, #TYPE), sizeof(whichUnit->FIELD)); \
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty(); \
        gi.LinkEntity(whichUnit); \
    } \
    return 0; \
}  \
uint32_t GetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    return whichUnit ? jass_pushlighthandle(j, &whichUnit->FIELD, #TYPE) : jass_pushnullhandle(j, #TYPE); \
}

#define UNIT_ACCESS(NAME, FIELD) \
uint32_t SetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    if (whichUnit) { \
        whichUnit->FIELD = jass_checknumber(j, 2); \
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty(); \
    } \
    return 0; \
}  \
uint32_t GetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    return jass_pushnumber(j, whichUnit ? whichUnit->FIELD : 0); \
}

#define UNITINFO_ACCESS(FIELD) UNIT_ACCESS(FIELD, unitinfo.FIELD)

#define UNIT_POSITION_ACCESS(NAME, FIELD) \
uint32_t SetUnit##NAME(LPJASS j) { \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit"); \
    if (whichUnit) { \
        VECTOR2 old_position = whichUnit->s.origin2; \
        whichUnit->FIELD = jass_checknumber(j, 2); \
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty(); \
        gi.LinkEntity(whichUnit); \
        G_UnitPositionChanged(whichUnit, &old_position); \
    } \
    return 0; \
} \
uint32_t GetUnit##NAME(LPJASS j) { \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit"); \
    return jass_pushnumber(j, whichUnit ? whichUnit->FIELD : 0); \
}

UNIT_POSITION_ACCESS(X, s.origin.x);
UNIT_POSITION_ACCESS(Y, s.origin.y);
#undef UNIT_POSITION_ACCESS

uint32_t SetUnitPositionLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    VECTOR2 position;

    if (whichUnit && whichLocation) {
        VECTOR2 old_position = whichUnit->s.origin2;
        G_FindUnitUnstuckPosition(whichUnit, whichLocation, &position);
        whichUnit->s.origin.x = position.x;
        whichUnit->s.origin.y = position.y;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
        G_UnitPositionChanged(whichUnit, &old_position);
    }
    return 0;
}
uint32_t GetUnitPositionLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return whichUnit ? jass_pushlighthandle(j, &whichUnit->s.origin2, "location") : jass_pushnullhandle(j, "location");
}
UNITINFO_ACCESS(MoveSpeed);

uint32_t SetUnitFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float const newHeight = jass_checknumber(j, 2);
    (void)jass_checknumber(j, 3); /* Warsmash currently ignores rate too. */
    if (whichUnit) {
        whichUnit->unitinfo.FlyHeight = newHeight;
        M_CheckGround(whichUnit);
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
    }
    return 0;
}

uint32_t GetUnitFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.FlyHeight : 0);
}
UNITINFO_ACCESS(TurnSpeed);
UNITINFO_ACCESS(PropWindow);
UNITINFO_ACCESS(AcquireRange);

uint32_t GetUnitFacing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) {
        return jass_pushnumber(j, 0);
    }
    float facingAngle = whichUnit->s.angle;
    jass_pushnumber(j, RAD2DEG(facingAngle));
    return 1;
}

uint32_t SetUnitFacing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float facingAngle = jass_checknumber(j, 2);
    if (whichUnit) whichUnit->s.angle = DEG2RAD(facingAngle);
    return 0;
}

uint32_t SetUnitFacingTimed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float facingAngle = jass_checknumber(j, 2);
//    float duration = jass_checknumber(j, 3);
    if (whichUnit) whichUnit->s.angle = DEG2RAD(facingAngle);
    return 0;
}

uint32_t KillUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    /* KillUnit is a death transition, not a raw life write; unit_die owns the death animation, events, and cleanup. */
    if (whichUnit && whichUnit->inuse && !(whichUnit->svflags & SVF_DEADMONSTER)) {
        unit_die(whichUnit, NULL);
    }
    return 0;
}
uint32_t RemoveUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) {
        LPGAMECLIENT owner = G_GetPlayerClientByNumber(whichUnit->s.player);
        if (owner && owner->ps.number == whichUnit->s.player) G_InvalidateCommands(owner);
        G_DeferFreeEdict(whichUnit);
    }
    return 0;
}
uint32_t ShowUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool show = jass_checkboolean(j, 2);
    bool was_hidden;
    bool is_hidden;
    if (!whichUnit) {
        return 0;
    }
    was_hidden = !!(whichUnit->s.renderfx & RF_HIDDEN);
    if (show) {
        whichUnit->s.renderfx &= ~RF_HIDDEN;
    } else {
        whichUnit->s.renderfx |= RF_HIDDEN;
    }
    is_hidden = !!(whichUnit->s.renderfx & RF_HIDDEN);
    if (was_hidden != is_hidden) {
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        /* Visibility is part of both Hero-shortcut and idle-worker eligibility.
         * Rebuild only on a real transition; the shared hook cheaply rejects
         * ordinary non-Hero/non-worker units. */
        G_InvalidateUnitShortcuts(G_GetPlayerClientByNumber(whichUnit->s.player));
    }
    return 0;
}

JASS_API(SetUnitState,
(EDICT, whichUnit, "unit"),
(UNITSTATE, whichUnitState, "unitstate"),
(number, newVal))
{
    bool was_dead;
    if (!whichUnit || !whichUnitState) {
        return;
    }
    was_dead = M_IsDead(whichUnit);
    if (*whichUnitState == WC3_UNIT_STATE_LIFE) G_SetHealth(whichUnit, newVal);
    else (&whichUnit->health.value)[*whichUnitState] = newVal;
    if ((whichUnit->s.flags & EF_FOW_BLOCKER) && was_dead != M_IsDead(whichUnit)) G_FowMarkBlockersDirty();
}
//uint32_t SetUnitState(LPJASS j) {
//    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
//    UNITSTATE *whichUnitState = jass_checkhandle(j, 2, "unitstate");
//    float newVal = jass_checknumber(j, 3);
//    (&whichUnit->health.value)[*whichUnitState] = newVal;
//    return 0;
//}
uint32_t GetUnitState(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    UNITSTATE *whichUnitState = jass_checkhandle(j, 2, "unitstate");
    float value = whichUnit && whichUnitState ? (&whichUnit->health.value)[*whichUnitState] : 0;
    return jass_pushnumber(j, value);
}
uint32_t SetUnitPosition(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    VECTOR2 requested = MAKE(VECTOR2, jass_checknumber(j, 2), jass_checknumber(j, 3));
    VECTOR2 position;

    if (whichUnit) {
        VECTOR2 old_position = whichUnit->s.origin2;
        G_FindUnitUnstuckPosition(whichUnit, &requested, &position);
        whichUnit->s.origin.x = position.x;
        whichUnit->s.origin.y = position.y;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
        G_UnitPositionChanged(whichUnit, &old_position);
    }
    return 0;
}
uint32_t GetUnitDefaultAcquireRange(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.AcquireRange : 0);
}
uint32_t GetUnitDefaultTurnSpeed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.TurnSpeed : 0);
}
uint32_t GetUnitDefaultPropWindow(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.PropWindow : 0);
}
uint32_t GetUnitDefaultFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit && whichUnit->data.UnitData
        ? whichUnit->data.UnitData->moveHeight : 0);
}
uint32_t SetUnitOwner(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    bool change_color = jass_checkboolean(j, 3);
    if (whichUnit && whichPlayer) {
        uint32_t const previous_player = whichUnit->s.player;
        uint32_t const previous_color = G_GetUnitTeamColor(whichUnit);
        G_SetUnitPlayer(whichUnit, PLAYER_NUM(whichPlayer));
        if (change_color) {
            G_ClearUnitColorOverride(whichUnit);
            G_SetUnitTeamColor(whichUnit, whichPlayer->color);
        } else if (previous_player != PLAYER_NUM(whichPlayer)) {
            G_SetUnitColorOverride(whichUnit, previous_color);
        }
    }
    return 0;
}
uint32_t SetUnitColor(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t *pColor = jass_checkhandle(j, 2, "playercolor");
    if (whichUnit && pColor) G_SetUnitColorOverride(whichUnit, *pColor);
    return 0;
}
uint32_t SetUnitScale(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float scaleX = jass_checknumber(j, 2);
    (void)jass_checknumber(j, 3);
    (void)jass_checknumber(j, 4);
    if (whichUnit) {
        /* Warsmash applies WC3's XYZ API as uniform model scale from X. */
        whichUnit->s.scale = scaleX;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    }
    return 0;
}
uint32_t SetUnitTimeScale(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float timeScale = jass_checknumber(j, 2);

    if (whichUnit) whichUnit->animation_speed = MAX(0.0f, timeScale);
    return 0;
}
uint32_t SetUnitBlendTime(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //float blendTime = jass_checknumber(j, 2);
    return 0;
}
/* Store the clamped persistent RGBA override consumed by the WC3 presentation datagram. */
uint32_t SetUnitVertexColor(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t red = jass_checkinteger(j, 2);
    int32_t green = jass_checkinteger(j, 3);
    int32_t blue = jass_checkinteger(j, 4);
    int32_t alpha = jass_checkinteger(j, 5);
    if (whichUnit) {
        whichUnit->vertex_color = MAKE(COLOR32,
            BZ_CLAMP_U8(red), BZ_CLAMP_U8(green), BZ_CLAMP_U8(blue), BZ_CLAMP_U8(alpha));
        whichUnit->vertex_color_set = true;
        whichUnit->vertex_color_override_set = true;
    }
    return 0;
}
uint32_t SetUnitUserData(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) whichUnit->user_data = jass_checkinteger(j, 2);
    return 0;
}
uint32_t GetUnitUserData(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->user_data : 0);
}
uint32_t UnitSetUsesAltIcon(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) whichUnit->uses_alt_icon = jass_checkboolean(j, 2);
    return 0;
}
uint32_t QueueUnitAnimation(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //cstring_t whichAnimation = jass_checkstring(j, 2);
    return 0;
}
uint32_t SetUnitAnimation(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t whichAnimation = jass_checkstring(j, 2);
    if (whichUnit) G_SetUnitAnimation(whichUnit, whichAnimation);
    return 0;
}
uint32_t SetUnitAnimationByIndex(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //int32_t whichAnimation = jass_checkinteger(j, 2);
    return 0;
}
uint32_t SetUnitAnimationWithRarity(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t whichAnimation = jass_checkstring(j, 2);
    uint32_t *rarity = jass_checkhandle(j, 3, "raritycontrol");
    LPCANIMATION animation;

    if (whichUnit && whichAnimation) {
        animation = G_GetAnimationVariant(whichUnit->s.model, whichAnimation, rarity && *rarity == 1);
        G_SetUnitAnimation(whichUnit, whichAnimation);
        if (animation) {
            whichUnit->animation = animation;
            whichUnit->s.frame = animation->interval[0];
            whichUnit->animation_override = true;
        }
    }
    return 0;
}
uint32_t AddUnitAnimationProperties(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t animProperties = jass_checkstring(j, 2);
    bool add = jass_checkboolean(j, 3);
    if (whichUnit) G_AddUnitAnimationProperties(whichUnit, animProperties, add);
    return 0;
}

//JASS_API(SetUnitLookAt,
//(EDICT, whichUnit, "unit"),
//(string, whichBone),
//(EDICT, lookAtTarget, "unit"),
//(number, offsetX),
//(number, offsetY),
//(number, offsetZ))
//{
//}
uint32_t SetUnitLookAt(LPJASS j) {
//    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
//    cstring_t whichBone = jass_checkstring(j, 2);
//    handle_t lookAtTarget = jass_checkhandle(j, 3, "unit");
//    float offsetX = jass_checknumber(j, 4);
//    float offsetY = jass_checknumber(j, 5);
//    float offsetZ = jass_checknumber(j, 6);
    return 0;
}
uint32_t ResetUnitLookAt(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
uint32_t SetUnitRescuable(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t byWhichPlayer = jass_checkhandle(j, 2, "player");
    //bool flag = jass_checkboolean(j, 3);
    return 0;
}
uint32_t SetUnitRescueRange(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //float range = jass_checknumber(j, 2);
    return 0;
}
uint32_t SetHeroStr(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t newStr = jass_checkinteger(j, 2);
//    bool permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.str = (uint32_t)MAX(0, newStr); G_RecomputeHeroStats(whichHero); }
    return 0;
}
uint32_t SetHeroAgi(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t newAgi = jass_checkinteger(j, 2);
//    bool permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.agi = (uint32_t)MAX(0, newAgi); G_RecomputeHeroStats(whichHero); }
    return 0;
}
uint32_t SetHeroInt(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t newInt = jass_checkinteger(j, 2);
//    bool permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.intel = (uint32_t)MAX(0, newInt); G_RecomputeHeroStats(whichHero); }
    return 0;
}
uint32_t GetHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichHero ? (int32_t)whichHero->hero.xp : 0);
}
uint32_t SetHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t newXpVal = jass_checkinteger(j, 2);
//    bool showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && !whichHero->hero.suspend_xp) {
        G_HeroSetXP(whichHero, (uint32_t)MAX(0, newXpVal));
    }
    return 0;
}
uint32_t GetHeroSkillPoints(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t const points = whichHero && whichHero->data.UnitBalance && G_UnitIsHero(whichHero)
        ? (int32_t)whichHero->hero.skillpoints : 0;
    return jass_pushinteger(j, points);
}
uint32_t UnitModifySkillPoints(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t const delta = jass_checkinteger(j, 2);
    bool const result = G_HeroModifySkillPoints(whichHero, delta);
    return jass_pushboolean(j, result);
}
uint32_t AddHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t xpToAdd = jass_checkinteger(j, 2);
//    bool showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && !whichHero->hero.suspend_xp && xpToAdd > 0) {
        uint32_t add = (uint32_t)xpToAdd;
        uint32_t cur = whichHero->hero.xp;
        /* Cap at INT32_MAX so GetHeroXP (signed return) never reads negative. */
        uint32_t sum = cur + add;
        G_HeroSetXP(whichHero, (sum < cur || sum > (uint32_t)INT32_MAX) ? (uint32_t)INT32_MAX : sum);
    }
    return 0;
}
uint32_t SetHeroLevel(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t level = jass_checkinteger(j, 2);
//    bool showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && level > (int32_t)whichHero->hero.level) {
        /* WC3 SetHeroLevel raises the level by granting enough XP to reach it
         * (level only increases). Route through the XP transition so skill
         * points and both Hero-level event families per crossed level stay
         * identical to ordinary XP gains. */
        uint32_t const target = MIN((uint32_t)level, G_MaxHeroLevel());
        uint32_t const need = G_HeroXPForLevel(target);
        G_HeroSetXP(whichHero, MAX(whichHero->hero.xp, need));
    }
    return 0;
}
uint32_t GetHeroLevel(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichHero ? whichHero->hero.level : 0);
}
uint32_t SuspendHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    if (whichHero) whichHero->hero.suspend_xp = flag;
    return 0;
}
uint32_t IsSuspendedXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichHero && whichHero->hero.suspend_xp);
}
uint32_t SelectHeroSkill(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    int32_t abilcode = jass_checkinteger(j, 2);
    if (whichHero) {
        G_HeroLearnSkill(whichHero, (uint32_t)abilcode);
    }
    return 0;
}
uint32_t GetUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t abilcode = jass_checkinteger(j, 2);
    return jass_pushinteger(j, whichUnit ? (int32_t)G_UnitAbilityLevel(whichUnit, (uint32_t)abilcode) : 0);
}
uint32_t SetUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t abilcode = jass_checkinteger(j, 2);
    int32_t level = jass_checkinteger(j, 3);
    return jass_pushinteger(j, whichUnit ? (int32_t)G_UnitSetAbilityLevel(whichUnit, (uint32_t)abilcode, level) : 0);
}
uint32_t IncUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t abilcode = jass_checkinteger(j, 2);
    uint32_t cur;
    if (!whichUnit) return jass_pushinteger(j, 0);
    cur = G_UnitAbilityLevel(whichUnit, (uint32_t)abilcode);
    if (!cur) return jass_pushinteger(j, 0);
    return jass_pushinteger(j, (int32_t)G_UnitSetAbilityLevel(whichUnit, (uint32_t)abilcode, (int32_t)cur + 1));
}
uint32_t GetHeroStr(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    bool includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses; /* TODO: attribute bonuses from items/auras are not tracked separately yet. */
    return jass_pushinteger(j, whichHero ? (int32_t)whichHero->hero.str : 0);
}
uint32_t GetHeroAgi(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    bool includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses;
    return jass_pushinteger(j, whichHero ? (int32_t)whichHero->hero.agi : 0);
}
uint32_t GetHeroInt(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    bool includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses;
    return jass_pushinteger(j, whichHero ? (int32_t)whichHero->hero.intel : 0);
}
uint32_t GetUnitLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) return jass_pushinteger(j, 0);
    if (G_UnitIsHero(whichUnit)) return jass_pushinteger(j, (int32_t)whichUnit->hero.level);
    return jass_pushinteger(j, whichUnit->data.UnitBalance ? whichUnit->data.UnitBalance->level : 0);
}
uint32_t GetUnitCurrentOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (int32_t)G_GetIssuedOrderId(whichUnit) : 0);
}
uint32_t UnitInventorySize(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (int32_t)G_InventoryCapacity(whichUnit) : 0);
}
uint32_t UnitDropItemPoint(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    float x = jass_checknumber(j, 3);
    float y = jass_checknumber(j, 4);
    int32_t slot;
    if (!whichUnit || !whichItem || whichItem->item.carrier != whichUnit)
        return jass_pushboolean(j, 0);
    slot = whichItem->item.inventory_slot;
    if (slot < 0) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, G_DropItemAt(whichUnit, (uint32_t)slot, &MAKE(VECTOR2, x, y)));
}
uint32_t UnitDamageTarget(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT target = jass_checkhandle(j, 2, "widget");
    float amount = jass_checknumber(j, 3);
    /* attack/ranged/attackType/damageType/weaponType are accepted but not yet modeled. */
    (void)jass_checkboolean(j, 4);
    (void)jass_checkboolean(j, 5);
    (void)jass_checkhandle(j, 6, "attacktype");
    (void)jass_checkhandle(j, 7, "damagetype");
    (void)jass_checkhandle(j, 8, "weapontype");
    if (!whichUnit || !target || amount <= 0.0f) return jass_pushboolean(j, 0);
    if (target->invulnerable || M_IsDead(target)) return jass_pushboolean(j, 0);
    T_Damage(target, whichUnit, (int)amount);
    return jass_pushboolean(j, 1);
}
uint32_t UnitAddType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t * whichUnitType = jass_checkhandle(j, 2, "unittype");
    if (!whichUnit || !whichUnitType || *whichUnitType >= 32) return jass_pushboolean(j, 0);
    whichUnit->script_unit_types |= (1u << *whichUnitType);
    return jass_pushboolean(j, 1);
}
uint32_t UnitRemoveType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t * whichUnitType = jass_checkhandle(j, 2, "unittype");
    if (!whichUnit || !whichUnitType || *whichUnitType >= 32) return jass_pushboolean(j, 0);
    whichUnit->script_unit_types &= ~(1u << *whichUnitType);
    return jass_pushboolean(j, 1);
}
uint32_t UnitCountBuffsEx(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool timedLife;
    int32_t count = 0;
    /* TODO: positive/negative/magic/physical/aura/autoDispel filters need buff-type metadata. */
    (void)jass_checkboolean(j, 2); (void)jass_checkboolean(j, 3);
    (void)jass_checkboolean(j, 4); (void)jass_checkboolean(j, 5);
    timedLife = jass_checkboolean(j, 6);
    (void)jass_checkboolean(j, 7); (void)jass_checkboolean(j, 8);
    if (!whichUnit) return jass_pushinteger(j, 0);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *s = whichUnit->abilstatus + i;
        if (!s->level) continue;
        if (timedLife && s->code != MAKEFOURCC('B', 'T', 'L', 'F')) continue;
        count++;
    }
    return jass_pushinteger(j, count);
}
uint32_t UnitPauseTimedLife(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    uint32_t now;
    if (!whichUnit || whichUnit->timed_life_paused == flag) return 0;
    now = G_Time();
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *s = whichUnit->abilstatus + i;
        if (!s->level || s->code != MAKEFOURCC('B', 'T', 'L', 'F')) continue;
        if (flag) {
            s->data = s->timestamp > now ? s->timestamp - now : 0;
            s->timestamp = 0;
        } else if (s->data) {
            s->timestamp = now + s->data;
            s->data = 0;
        }
    }
    whichUnit->timed_life_paused = flag;
    return 0;
}
uint32_t UnitResetCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    S_SpellResetCooldowns(whichUnit);
    return 0;
}

uint32_t BlzGetUnitAbilityCooldownRemaining(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t const abilityId = (uint32_t)jass_checkinteger(j, 2);
    return jass_pushnumber(j, S_SpellCooldownRemaining(whichUnit, abilityId));
}

uint32_t BlzEndUnitAbilityCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t const abilityId = (uint32_t)jass_checkinteger(j, 2);
    S_SpellEndCooldown(whichUnit, abilityId);
    return 0;
}

uint32_t BlzStartUnitAbilityCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t const abilityId = (uint32_t)jass_checkinteger(j, 2);
    float const cooldown = jass_checknumber(j, 3);
    S_SpellStartCooldownDuration(whichUnit, abilityId, cooldown);
    return 0;
}

uint32_t ReviveHero(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    //bool doEyecandy = jass_checkboolean(j, 4);
    if (whichHero) {
        G_ReviveHero(whichHero, x, y);
        return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
uint32_t ReviveHeroLoc(LPJASS j) {
    //LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    //handle_t loc = jass_checkhandle(j, 2, "location");
    //bool doEyecandy = jass_checkboolean(j, 3);
    return jass_pushboolean(j, 0);
}
uint32_t SetUnitExploded(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //bool exploded = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetUnitInvulnerable(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->invulnerable = flag;
    return 0;
}
uint32_t PauseUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->paused = flag;
    return 0;
}
uint32_t IsUnitPaused(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && whichUnit->paused);
}
uint32_t SetUnitPathing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->no_pathing = !flag;
    return 0;
}
uint32_t GetUnitPointValue(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, 0);
}
uint32_t GetUnitPointValueByType(LPJASS j) {
    //int32_t unitType = jass_checkinteger(j, 1);
    return jass_pushinteger(j, 0);
}
uint32_t UnitAddItem(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    if (!whichUnit || !whichItem) return jass_pushboolean(j, false);
    return jass_pushboolean(j, G_PickupItem(whichUnit, whichItem));
}
uint32_t UnitAddItemById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t itemId = jass_checkinteger(j, 2);
    if (!whichUnit) return jass_pushnullhandle(j, "item");
    LPEDICT item = SP_SpawnAtLocation(itemId, whichUnit->s.player, &whichUnit->s.origin2);
    if (item && G_PickupItem(whichUnit, item)) {
        return jass_pushlighthandle(j, item, "item");
    } else {
        if (item) G_RemoveItem(item);
        return jass_pushnullhandle(j, "item");
    }
}
uint32_t UnitAddItemToSlotById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t itemId = jass_checkinteger(j, 2);
    int32_t itemSlot = jass_checkinteger(j, 3);
    if (!whichUnit || itemSlot < 0 || (uint32_t)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushboolean(j, false);
    }
    LPEDICT item = SP_SpawnAtLocation(itemId, whichUnit->s.player, &whichUnit->s.origin2);
    if (item && G_AddItemToSlot(whichUnit, item, (uint32_t)itemSlot)) {
        return jass_pushboolean(j, true);
    } else {
        if (item) G_RemoveItem(item);
        return jass_pushboolean(j, false);
    }
}
uint32_t UnitRemoveItem(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    if (!whichUnit || !whichItem) {
        return 0;
    }
    FOR_LOOP(i, MAX_INVENTORY) {
        if (whichUnit->inventory[i] == whichItem) {
            G_DropItem(whichUnit, i);
            break;
        }
    }
    return 0;
}
uint32_t UnitRemoveItemFromSlot(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t itemSlot = jass_checkinteger(j, 2);
    if (!whichUnit || itemSlot < 0 || (uint32_t)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = whichUnit->inventory[itemSlot];
    if (!item) return jass_pushnullhandle(j, "item");
    if (!G_DropItem(whichUnit, (uint32_t)itemSlot)) {
        return jass_pushnullhandle(j, "item");
    }
    return jass_pushlighthandle(j, item, "item");
}
uint32_t UnitHasItem(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    if (!whichUnit || !whichItem) return jass_pushboolean(j, 0);
    FOR_LOOP(i, MAX_INVENTORY) {
        if (whichUnit->inventory[i] == whichItem) return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
uint32_t UnitItemInSlot(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t itemSlot = jass_checkinteger(j, 2);
    if (!whichUnit || itemSlot < 0 || (uint32_t)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = whichUnit->inventory[itemSlot];
    if (!item) return jass_pushnullhandle(j, "item");
    return jass_pushlighthandle(j, item, "item");
}
uint32_t UnitUseItem(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichItem = jass_checkhandle(j, 2, "item");
    return jass_pushboolean(j, 0);
}
uint32_t UnitUseItemPoint(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichItem = jass_checkhandle(j, 2, "item");
    //float x = jass_checknumber(j, 3);
    //float y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
uint32_t UnitUseItemTarget(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichItem = jass_checkhandle(j, 2, "item");
    //handle_t target = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t GetUnitRallyPoint(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    API_ALLOC(VECTOR2, location);
    if (whichUnit) {
        G_ResolveRallyTarget(whichUnit, location, NULL);
    }
    return 1;
}
uint32_t GetUnitRallyUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT target = NULL;
    rallyTargetType_t type;

    if (!whichUnit) return jass_pushnullhandle(j, "unit");
    type = G_ResolveRallyTarget(whichUnit, NULL, &target);
    if ((type == RALLY_TARGET_SELF || type == RALLY_TARGET_ENTITY) &&
        target && (target->svflags & SVF_MONSTER)) {
        return jass_pushlighthandle(j, target, "unit");
    }
    return jass_pushnullhandle(j, "unit");
}
uint32_t GetUnitRallyDestructable(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT target = NULL;
    rallyTargetType_t type;

    if (!whichUnit) return jass_pushnullhandle(j, "destructable");
    type = G_ResolveRallyTarget(whichUnit, NULL, &target);
    if (type == RALLY_TARGET_ENTITY && target && G_IsDestructable(target)) {
        return jass_pushlighthandle(j, target, "destructable");
    }
    return jass_pushnullhandle(j, "destructable");
}
uint32_t GetUnitLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    API_ALLOC(VECTOR2, location);
    if (whichUnit) {
        *location = whichUnit->s.origin2;
    }
    return 1;
}
uint32_t GetUnitDefaultMoveSpeed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.MoveSpeed : 0);
}
uint32_t GetOwningPlayer(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) {
        return jass_pushnullhandle(j, "player");
    }
    return jass_pushlighthandle(j, G_GetPlayerByNumber(whichUnit->s.player), "player");
}
uint32_t GetUnitTypeId(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (int32_t)whichUnit->class_id : 0);
}
static uint32_t JassPushRaceHandle(LPJASS j, int32_t value);
uint32_t GetUnitRace(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t race = whichUnit && whichUnit->data.UnitData
        ? WC3_JassRaceFromString(whichUnit->data.UnitData->race) : 0;
    return JassPushRaceHandle(j, race);
}
uint32_t GetUnitName(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t name = whichUnit ? G_UnitName(whichUnit->class_id) : NULL;
    return jass_pushstring(j, name ? name : "");
}
uint32_t GetUnitFoodUsed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->data.UnitBalance->foodUsed : 0);
}
uint32_t GetUnitFoodMade(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->data.UnitBalance->foodMade : 0);
}
uint32_t IsUnitInGroup(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    ggroup_t *whichGroup = jass_checkhandle(j, 2, "group");
    if (!whichUnit || !whichGroup) return jass_pushboolean(j, 0);
    FOR_LOOP(i, whichGroup->num_units) {
        if (whichGroup->units[i] == whichUnit) return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitInForce(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t * whichForce = jass_checkhandle(j, 2, "force");
    if (!whichUnit || !whichForce) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, (*whichForce) & (1 << whichUnit->s.player));
}
uint32_t IsUnitOwnedByPlayer(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichUnit->s.player == PLAYER_NUM(whichPlayer));
}
uint32_t IsUnitAlly(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j,
        G_PlayerTreatsPlayerAsAlly(PLAYER_NUM(whichPlayer), whichUnit->s.player));
}
uint32_t IsUnitEnemy(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j,
        !G_PlayerTreatsPlayerAsAlly(PLAYER_NUM(whichPlayer), whichUnit->s.player));
}
uint32_t IsUnitVisible(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitDetected(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitInvisible(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitFogged(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitMasked(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitSelected(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, (whichUnit->selected >> PLAYER_NUM(whichPlayer)) & 1);
}
uint32_t IsUnitRace(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichRace = jass_checkhandle(j, 2, "race");
    return jass_pushboolean(j, 0);
}
uint32_t IsUnitType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t * whichUnitType = jass_checkhandle(j, 2, "unittype");
    if (!whichUnit || !whichUnitType) return jass_pushboolean(j, 0);
    if (*whichUnitType < 32 && (whichUnit->script_unit_types & (1u << *whichUnitType)))
        return jass_pushboolean(j, 1);
    if (*whichUnitType == WC3_UNIT_TYPE_STRUCTURE)
        return jass_pushboolean(j, G_UnitIsBuilding(whichUnit->class_id));
    if (*whichUnitType == 0) /* UNIT_TYPE_HERO */
        return jass_pushboolean(j, G_UnitIsHero(whichUnit));
    if (*whichUnitType == 1) /* UNIT_TYPE_DEAD */
        return jass_pushboolean(j, M_IsDead(whichUnit));
    if (*whichUnitType == WC3_UNIT_TYPE_POLYMORPHED)
        return jass_pushboolean(j, S_UnitPolymorphed(whichUnit));
    if (*whichUnitType == 23) /* UNIT_TYPE_SLEEPING */
        return jass_pushboolean(j, G_UnitIsSleeping(whichUnit));
    if (*whichUnitType == 11) /* UNIT_TYPE_STUNNED */
        return jass_pushboolean(j, whichUnit->stunned);
    if (*whichUnitType == 3) /* UNIT_TYPE_FLYING */
        return jass_pushboolean(j, whichUnit->aiflags & AI_FLYING);
    if (*whichUnitType == 10) /* UNIT_TYPE_SUMMONED */
        return jass_pushboolean(j, whichUnit->summon_ability != 0);
    if (*whichUnitType == 14) /* UNIT_TYPE_UNDEAD */
        return jass_pushboolean(j, whichUnit->data.UnitData &&
            WC3_RaceFromString(whichUnit->data.UnitData->race) == RACE_UNDEAD);
    return jass_pushboolean(j, 0);
}
uint32_t IsUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichSpecifiedUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichUnit != NULL && whichUnit == whichSpecifiedUnit);
}
uint32_t IsUnitInRange(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT otherUnit = jass_checkhandle(j, 2, "unit");
    float distance = jass_checknumber(j, 3);
    if (!whichUnit || !otherUnit) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, &otherUnit->s.origin2) <= distance);
}
uint32_t IsUnitInRangeXY(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float distance = jass_checknumber(j, 4);
    if (!whichUnit) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, &MAKE(VECTOR2, x, y)) <= distance);
}
uint32_t IsUnitInRangeLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    float distance = jass_checknumber(j, 3);
    if (!whichUnit || !whichLocation) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, whichLocation) <= distance);
}
uint32_t IsUnitHidden(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && (whichUnit->s.renderfx & RF_HIDDEN));
}
uint32_t IsUnitIllusion(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && (whichUnit->aiflags & AI_ILLUSION));
}
uint32_t IsUnitInTransport(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichTransport = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichUnit && whichTransport &&
                              S_CargoTransportForUnit(whichUnit) == whichTransport);
}
uint32_t IsUnitLoaded(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && S_CargoTransportForUnit(whichUnit) != NULL);
}
uint32_t IsHeroUnitId(LPJASS j) {
    //int32_t unitId = jass_checkinteger(j, 1);
    return jass_pushboolean(j, 0);
}
uint32_t UnitShareVision(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    //bool share = jass_checkboolean(j, 3);
    return 0;
}
uint32_t UnitSuspendDecay(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //bool suspend = jass_checkboolean(j, 2);
    return 0;
}
uint32_t UnitAddAbility(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t abilityId = jass_checkinteger(j, 2);
    return jass_pushboolean(j, G_ActorAddSkill(whichUnit, abilityId));
}
uint32_t UnitRemoveAbility(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t abilityId = jass_checkinteger(j, 2);
    return jass_pushboolean(j, G_ActorRemoveSkill(whichUnit, abilityId));
}
uint32_t UnitMakeAbilityPermanent(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool permanent = jass_checkboolean(j, 2);
    uint32_t abilityId = jass_checkinteger(j, 3);
    return jass_pushboolean(j, G_ActorSetSkillPermanent(whichUnit, abilityId, permanent));
}
uint32_t UnitRemoveBuffs(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //bool removePositive = jass_checkboolean(j, 2);
    //bool removeNegative = jass_checkboolean(j, 3);
    return 0;
}
uint32_t UnitRemoveBuffsEx(LPJASS j) {
    /* TODO: ability-owned dispel filtering is not represented yet. */
    (void)j;
    return 0;
}
uint32_t UnitAddSleep(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool add = jass_checkboolean(j, 2);
    G_UnitSetCanSleep(whichUnit, add);
    return 0;
}
uint32_t UnitCanSleep(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_UnitCanSleep(whichUnit));
}
uint32_t UnitAddSleepPerm(LPJASS j) {
    /* Sleep Always (Asla) has separate ability-owned semantics (including its
     * Sleep Once/Allow On Any Player Slot data). Keep this native conservative
     * until that ability is represented instead of aliasing it to night sleep. */
    (void)j;
    return 0;
}
uint32_t UnitCanSleepPerm(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && G_ActorHasSkill(whichUnit, "Asla"));
}
uint32_t UnitIsSleeping(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_UnitIsSleeping(whichUnit));
}
uint32_t UnitWakeUp(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    G_UnitWakeUp(whichUnit);
    return 0;
}
uint32_t UnitApplyTimedLife(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //int32_t buffId = jass_checkinteger(j, 2);
    //float duration = jass_checknumber(j, 3);
    return 0;
}
uint32_t IssueImmediateOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t order = jass_checkstring(j, 2);
    return jass_pushboolean(j, unit_issueimmediateorder(whichUnit, order));
}
uint32_t IssueImmediateOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t order = (uint32_t)jass_checkinteger(j, 2);
    return jass_pushboolean(j, unit_issueimmediateorder(whichUnit, G_OrderId2String(order)));
}
uint32_t IssuePointOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t order = jass_checkstring(j, 2);
    float x = jass_checknumber(j, 3);
    float y = jass_checknumber(j, 4);
    bool ret = unit_issueorder(whichUnit, order, &MAKE(VECTOR2, x, y));
    return jass_pushboolean(j, ret);
}
uint32_t IssuePointOrderLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t order = jass_checkstring(j, 2);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 3, "location");
    bool ret = unit_issueorder(whichUnit, order, whichLocation);
    return jass_pushboolean(j, ret);
}
uint32_t IssuePointOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t order = (uint32_t)jass_checkinteger(j, 2);
    VECTOR2 point = { jass_checknumber(j, 3), jass_checknumber(j, 4) };

    /* Building rawcodes are valid point-order ids, but they are not entries in
     * the canonical Warcraft order table. Keep construction on the existing
     * authoritative build path and leave ordinary ids to the generic router. */
    if (G_UnitIsBuilding(order))
        return jass_pushboolean(j, G_IssueBuildOrder(whichUnit, order, &point));
    return jass_pushboolean(j, unit_issueorder(whichUnit, G_OrderId2String(order), &point));
}
uint32_t IssuePointOrderByIdLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t order = (uint32_t)jass_checkinteger(j, 2);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 3, "location");

    if (G_UnitIsBuilding(order))
        return jass_pushboolean(j, G_IssueBuildOrder(whichUnit, order, whichLocation));
    return jass_pushboolean(j, unit_issueorder(whichUnit, G_OrderId2String(order), whichLocation));
}
uint32_t IssueTargetOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    cstring_t order = jass_checkstring(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, unit_issuetargetorder(whichUnit, order, targetWidget));
}
uint32_t IssueTargetOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    uint32_t order = (uint32_t)jass_checkinteger(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    cstring_t order_name = G_OrderId2String(order);
    bool accepted;

    /* Blizzard.j routes IssueBuildOrderByIdLocBJ('ugol') through this target
     * native after finding the neutral ngol. Translate that retail-specific
     * target order into the shared build path so the Acolyte walks to the mine
     * and construction performs the normal overlay binding on arrival. */
    if (order == order_ugol && targetWidget && targetWidget->class_id == unit_ngol)
        accepted = G_IssueBuildOrder(whichUnit, order, &targetWidget->s.origin2);
    else
        accepted = unit_issuetargetorder(whichUnit, order_name, targetWidget);

    /* The numeric argument is an order id, not a unit rawcode. Build-on-mine
     * placement uses the explicit ugol target case above; other ids remain
     * ordinary order ids and must not be interpreted as unit rawcodes. */
    return jass_pushboolean(j, accepted);
}
uint32_t IssueInstantTargetOrder(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //cstring_t order = jass_checkstring(j, 2);
    //handle_t targetWidget = jass_checkhandle(j, 3, "widget");
    //handle_t instantTargetWidget = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t IssueInstantTargetOrderById(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //int32_t order = jass_checkinteger(j, 2);
    //handle_t targetWidget = jass_checkhandle(j, 3, "widget");
    //handle_t instantTargetWidget = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t IssueBuildOrder(LPJASS j) {
    return jass_pushboolean(j, 0);
}
uint32_t IssueBuildOrderById(LPJASS j) {
    LPEDICT whichPeon = jass_checkhandle(j, 1, "unit");
    uint32_t unitId = (uint32_t)jass_checkinteger(j, 2);
    VECTOR2 point = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    bool accepted;

    accepted = G_IssueBuildOrder(whichPeon, unitId, &point);
    return jass_pushboolean(j, accepted);
}
uint32_t SetResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t amount = jass_checkinteger(j, 2);
    if (whichUnit) S_GoldMineSetResourceAmount(whichUnit, MAX(0, amount));
    return 0;
}
uint32_t AddResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t amount = jass_checkinteger(j, 2);
    if (whichUnit) whichUnit->resources += amount;
    return 0;
}
uint32_t GetResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->resources : 0);
}
uint32_t WaygateGetDestinationX(LPJASS j) {
    LPEDICT waygate = jass_checkhandle(j, 1, "unit");
    VECTOR2 destination = {0};
    S_WaygateGetDestination(waygate, &destination);
    return jass_pushnumber(j, destination.x);
}
uint32_t WaygateGetDestinationY(LPJASS j) {
    LPEDICT waygate = jass_checkhandle(j, 1, "unit");
    VECTOR2 destination = {0};
    S_WaygateGetDestination(waygate, &destination);
    return jass_pushnumber(j, destination.y);
}
uint32_t WaygateSetDestination(LPJASS j) {
    LPEDICT waygate = jass_checkhandle(j, 1, "unit");
    VECTOR2 destination = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    S_WaygateSetDestination(waygate, &destination);
    return 0;
}
uint32_t WaygateActivate(LPJASS j) {
    LPEDICT waygate = jass_checkhandle(j, 1, "unit");
    bool activate = jass_checkboolean(j, 2);
    S_WaygateSetActive(waygate, activate);
    return 0;
}
uint32_t WaygateIsActive(LPJASS j) {
    LPEDICT waygate = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, S_WaygateIsActive(waygate));
}
uint32_t UnitAddIndicator(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    int32_t red = jass_checkinteger(j, 2);
    int32_t green = jass_checkinteger(j, 3);
    int32_t blue = jass_checkinteger(j, 4);
    int32_t alpha = jass_checkinteger(j, 5);
    COLOR32 color = MAKE(COLOR32,
        (uint8_t)MAX(0, MIN(255, red)),
        (uint8_t)MAX(0, MIN(255, green)),
        (uint8_t)MAX(0, MIN(255, blue)),
        (uint8_t)MAX(0, MIN(255, alpha)));

    G_SendWidgetIndicator(whichUnit, color, currentplayer);
    return 0;
}
uint32_t RemoveGuardPosition(LPJASS j) {
    //handle_t hUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
uint32_t RecycleGuardPosition(LPJASS j) {
    //handle_t hUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
uint32_t CreateUnit(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t unitid = jass_checkinteger(j, 2);
    VECTOR2 location = MAKE(VECTOR2, jass_checknumber(j, 3), jass_checknumber(j, 4));
    float facing = jass_checknumber(j, 5);
    if (!player) {
        return jass_pushnullhandle(j, "unit");
    }
    LPEDICT unit = unit_create(PLAYER_NUM(player), unitid, &location, facing);
    return jass_pushlighthandle(j, unit, "unit");
}
uint32_t CreateUnitByName(LPJASS j) {
    //handle_t whichPlayer = jass_checkhandle(j, 1, "player");
    //cstring_t unitname = jass_checkstring(j, 2);
    //float x = jass_checknumber(j, 3);
    //float y = jass_checknumber(j, 4);
    //float face = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
uint32_t CreateUnitAtLoc(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t unitid = jass_checkinteger(j, 2);
    LPCVECTOR2 location = jass_checkhandle(j, 3, "location");
    float facing = jass_checknumber(j, 4);
    if (!player || !location) {
        return jass_pushnullhandle(j, "unit");
    }
    LPEDICT unit = unit_create(PLAYER_NUM(player), unitid, location, facing);
    return jass_pushlighthandle(j, unit, "unit");
}
uint32_t CreateUnitAtLocByName(LPJASS j) {
    //handle_t id = jass_checkhandle(j, 1, "player");
    //cstring_t unitname = jass_checkstring(j, 2);
    //handle_t whichLocation = jass_checkhandle(j, 3, "location");
    //float face = jass_checknumber(j, 4);
    return jass_pushnullhandle(j, "unit");
}
uint32_t CreateCorpse(LPJASS j) {
    //handle_t whichPlayer = jass_checkhandle(j, 1, "player");
    //int32_t unitid = jass_checkinteger(j, 2);
    //float x = jass_checknumber(j, 3);
    //float y = jass_checknumber(j, 4);
    //float face = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
uint32_t CreateBlightedGoldmine(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    VECTOR2 origin = MAKE(VECTOR2, jass_checknumber(j, 2), jass_checknumber(j, 3));
    float facing = jass_checknumber(j, 4);
    LPEDICT mine;

    if (!player || !(mine = S_CreateBlightedGoldmine(PLAYER_NUM(player), &origin, facing)))
        return jass_pushnullhandle(j, "unit");
    return jass_pushlighthandle(j, mine, "unit");
}
