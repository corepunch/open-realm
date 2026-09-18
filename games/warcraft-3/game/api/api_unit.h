extern LPPLAYER currentplayer;

static DWORD const order_ugol = BZ_WC3_UNIT_HAUNTED_GOLD_MINE;
static DWORD const unit_ngol = BZ_WC3_UNIT_GOLD_MINE;

#define UNIT_TYPED_ACCESS(NAME, FIELD, TYPE) \
DWORD SetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    if (whichUnit) { \
        memcpy(&whichUnit->FIELD, jass_checkhandle(j, 2, #TYPE), sizeof(whichUnit->FIELD)); \
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty(); \
        gi.LinkEntity(whichUnit); \
    } \
    return 0; \
}  \
DWORD GetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    return whichUnit ? jass_pushlighthandle(j, &whichUnit->FIELD, #TYPE) : jass_pushnullhandle(j, #TYPE); \
}

#define UNIT_ACCESS(NAME, FIELD) \
DWORD SetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    if (whichUnit) { \
        whichUnit->FIELD = jass_checknumber(j, 2); \
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty(); \
    } \
    return 0; \
}  \
DWORD GetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    return jass_pushnumber(j, whichUnit ? whichUnit->FIELD : 0); \
}

#define UNITINFO_ACCESS(FIELD) UNIT_ACCESS(FIELD, unitinfo.FIELD)

UNIT_ACCESS(X, s.origin.x);
UNIT_ACCESS(Y, s.origin.y);

DWORD SetUnitPositionLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    VECTOR2 position;

    if (whichUnit && whichLocation) {
        G_FindUnitUnstuckPosition(whichUnit, whichLocation, &position);
        whichUnit->s.origin.x = position.x;
        whichUnit->s.origin.y = position.y;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
    }
    return 0;
}
DWORD GetUnitPositionLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return whichUnit ? jass_pushlighthandle(j, &whichUnit->s.origin2, "location") : jass_pushnullhandle(j, "location");
}
UNITINFO_ACCESS(MoveSpeed);

DWORD SetUnitFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT const newHeight = jass_checknumber(j, 2);
    (void)jass_checknumber(j, 3); /* Warsmash currently ignores rate too. */
    if (whichUnit) {
        whichUnit->unitinfo.FlyHeight = newHeight;
        M_CheckGround(whichUnit);
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
    }
    return 0;
}

DWORD GetUnitFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.FlyHeight : 0);
}
UNITINFO_ACCESS(TurnSpeed);
UNITINFO_ACCESS(PropWindow);
UNITINFO_ACCESS(AcquireRange);

DWORD GetUnitFacing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) {
        return jass_pushnumber(j, 0);
    }
    FLOAT facingAngle = whichUnit->s.angle;
    jass_pushnumber(j, RAD2DEG(facingAngle));
    return 1;
}

DWORD SetUnitFacing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT facingAngle = jass_checknumber(j, 2);
    if (whichUnit) whichUnit->s.angle = DEG2RAD(facingAngle);
    return 0;
}

DWORD SetUnitFacingTimed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT facingAngle = jass_checknumber(j, 2);
//    FLOAT duration = jass_checknumber(j, 3);
    if (whichUnit) whichUnit->s.angle = DEG2RAD(facingAngle);
    return 0;
}

DWORD KillUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    /* KillUnit is a death transition, not a raw life write; unit_die owns the death animation, events, and cleanup. */
    if (whichUnit && whichUnit->inuse && !(whichUnit->svflags & SVF_DEADMONSTER)) {
        unit_die(whichUnit, NULL);
    }
    return 0;
}
DWORD RemoveUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) {
        LPGAMECLIENT owner = G_GetPlayerClientByNumber(whichUnit->s.player);
        if (owner && owner->ps.number == whichUnit->s.player) G_InvalidateCommands(owner);
        G_DeferFreeEdict(whichUnit);
    }
    return 0;
}
DWORD ShowUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL show = jass_checkboolean(j, 2);
    BOOL was_hidden;
    BOOL is_hidden;
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
    BOOL was_dead;
    if (!whichUnit || !whichUnitState) {
        return;
    }
    was_dead = M_IsDead(whichUnit);
    (&whichUnit->health.value)[*whichUnitState] = newVal;
    if ((whichUnit->s.flags & EF_FOW_BLOCKER) && was_dead != M_IsDead(whichUnit)) G_FowMarkBlockersDirty();
}
//DWORD SetUnitState(LPJASS j) {
//    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
//    UNITSTATE *whichUnitState = jass_checkhandle(j, 2, "unitstate");
//    FLOAT newVal = jass_checknumber(j, 3);
//    (&whichUnit->health.value)[*whichUnitState] = newVal;
//    return 0;
//}
DWORD GetUnitState(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    UNITSTATE *whichUnitState = jass_checkhandle(j, 2, "unitstate");
    FLOAT value = whichUnit && whichUnitState ? (&whichUnit->health.value)[*whichUnitState] : 0;
    return jass_pushnumber(j, value);
}
DWORD SetUnitPosition(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    VECTOR2 requested = MAKE(VECTOR2, jass_checknumber(j, 2), jass_checknumber(j, 3));
    VECTOR2 position;

    if (whichUnit) {
        G_FindUnitUnstuckPosition(whichUnit, &requested, &position);
        whichUnit->s.origin.x = position.x;
        whichUnit->s.origin.y = position.y;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        gi.LinkEntity(whichUnit);
    }
    return 0;
}
DWORD GetUnitDefaultAcquireRange(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.AcquireRange : 0);
}
DWORD GetUnitDefaultTurnSpeed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.TurnSpeed : 0);
}
DWORD GetUnitDefaultPropWindow(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.PropWindow : 0);
}
DWORD GetUnitDefaultFlyHeight(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit && whichUnit->data.UnitData
        ? whichUnit->data.UnitData->moveHeight : 0);
}
DWORD SetUnitOwner(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
//    BOOL changeColor = jass_checkboolean(j, 3);
    if (whichUnit && whichPlayer) {
        G_SetUnitPlayer(whichUnit, PLAYER_NUM(whichPlayer));
    }
    return 0;
}
DWORD SetUnitColor(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD *pColor = jass_checkhandle(j, 2, "playercolor");
    if (whichUnit && pColor) {
        DWORD const encoded = MIN(*pColor, 30u) + 1u;
        whichUnit->unit_color = *pColor;
        whichUnit->s.effect_flags = (whichUnit->s.effect_flags & ~EFX_TEAM_COLOR_MASK) |
            (USHORT)(encoded << EFX_TEAM_COLOR_SHIFT);
    }
    return 0;
}
DWORD SetUnitScale(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT scaleX = jass_checknumber(j, 2);
    (void)jass_checknumber(j, 3);
    (void)jass_checknumber(j, 4);
    if (whichUnit) {
        /* Warsmash applies WC3's XYZ API as uniform model scale from X. */
        whichUnit->s.scale = scaleX;
        if (whichUnit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    }
    return 0;
}
DWORD SetUnitTimeScale(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT timeScale = jass_checknumber(j, 2);

    if (whichUnit) whichUnit->animation_speed = MAX(0.0f, timeScale);
    return 0;
}
DWORD SetUnitBlendTime(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //FLOAT blendTime = jass_checknumber(j, 2);
    return 0;
}
/* Store the clamped persistent RGBA override consumed by the WC3 presentation datagram. */
DWORD SetUnitVertexColor(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG red = jass_checkinteger(j, 2);
    LONG green = jass_checkinteger(j, 3);
    LONG blue = jass_checkinteger(j, 4);
    LONG alpha = jass_checkinteger(j, 5);
    if (whichUnit) {
        whichUnit->vertex_color = MAKE(COLOR32,
            BZ_CLAMP_U8(red), BZ_CLAMP_U8(green), BZ_CLAMP_U8(blue), BZ_CLAMP_U8(alpha));
        whichUnit->vertex_color_set = true;
    }
    return 0;
}
DWORD SetUnitUserData(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) whichUnit->user_data = jass_checkinteger(j, 2);
    return 0;
}
DWORD GetUnitUserData(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->user_data : 0);
}
DWORD UnitSetUsesAltIcon(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) whichUnit->uses_alt_icon = jass_checkboolean(j, 2);
    return 0;
}
DWORD QueueUnitAnimation(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LPCSTR whichAnimation = jass_checkstring(j, 2);
    return 0;
}
DWORD SetUnitAnimation(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR whichAnimation = jass_checkstring(j, 2);
    if (whichUnit) G_SetUnitAnimation(whichUnit, whichAnimation);
    return 0;
}
DWORD SetUnitAnimationByIndex(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG whichAnimation = jass_checkinteger(j, 2);
    return 0;
}
DWORD SetUnitAnimationWithRarity(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR whichAnimation = jass_checkstring(j, 2);
    DWORD *rarity = jass_checkhandle(j, 3, "raritycontrol");
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
DWORD AddUnitAnimationProperties(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR animProperties = jass_checkstring(j, 2);
    BOOL add = jass_checkboolean(j, 3);
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
DWORD SetUnitLookAt(LPJASS j) {
//    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
//    LPCSTR whichBone = jass_checkstring(j, 2);
//    HANDLE lookAtTarget = jass_checkhandle(j, 3, "unit");
//    FLOAT offsetX = jass_checknumber(j, 4);
//    FLOAT offsetY = jass_checknumber(j, 5);
//    FLOAT offsetZ = jass_checknumber(j, 6);
    return 0;
}
DWORD ResetUnitLookAt(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
DWORD SetUnitRescuable(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE byWhichPlayer = jass_checkhandle(j, 2, "player");
    //BOOL flag = jass_checkboolean(j, 3);
    return 0;
}
DWORD SetUnitRescueRange(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //FLOAT range = jass_checknumber(j, 2);
    return 0;
}
DWORD SetHeroStr(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG newStr = jass_checkinteger(j, 2);
//    BOOL permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.str = (DWORD)MAX(0, newStr); G_RecomputeHeroStats(whichHero); }
    return 0;
}
DWORD SetHeroAgi(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG newAgi = jass_checkinteger(j, 2);
//    BOOL permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.agi = (DWORD)MAX(0, newAgi); G_RecomputeHeroStats(whichHero); }
    return 0;
}
DWORD SetHeroInt(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG newInt = jass_checkinteger(j, 2);
//    BOOL permanent = jass_checkboolean(j, 3);
    if (whichHero) { whichHero->hero.intel = (DWORD)MAX(0, newInt); G_RecomputeHeroStats(whichHero); }
    return 0;
}
DWORD GetHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichHero ? (LONG)whichHero->hero.xp : 0);
}
DWORD SetHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG newXpVal = jass_checkinteger(j, 2);
//    BOOL showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && !whichHero->hero.suspend_xp) {
        G_HeroSetXP(whichHero, (DWORD)MAX(0, newXpVal));
    }
    return 0;
}
DWORD GetHeroSkillPoints(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG const points = whichHero && whichHero->data.UnitBalance && G_UnitIsHero(whichHero)
        ? (LONG)whichHero->hero.skillpoints : 0;
    return jass_pushinteger(j, points);
}
DWORD UnitModifySkillPoints(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG const delta = jass_checkinteger(j, 2);
    BOOL const result = G_HeroModifySkillPoints(whichHero, delta);
    return jass_pushboolean(j, result);
}
DWORD AddHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG xpToAdd = jass_checkinteger(j, 2);
//    BOOL showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && !whichHero->hero.suspend_xp && xpToAdd > 0) {
        DWORD add = (DWORD)xpToAdd;
        DWORD cur = whichHero->hero.xp;
        /* Cap at INT32_MAX so GetHeroXP (signed return) never reads negative. */
        DWORD sum = cur + add;
        G_HeroSetXP(whichHero, (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum);
    }
    return 0;
}
DWORD SetHeroLevel(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG level = jass_checkinteger(j, 2);
//    BOOL showEyeCandy = jass_checkboolean(j, 3);
    if (whichHero && level > (LONG)whichHero->hero.level) {
        /* WC3 SetHeroLevel raises the level by granting enough XP to reach it
         * (level only increases). Route through the XP transition so skill
         * points and both Hero-level event families per crossed level stay
         * identical to ordinary XP gains. */
        DWORD const target = MIN((DWORD)level, G_MaxHeroLevel());
        DWORD const need = G_HeroXPForLevel(target);
        G_HeroSetXP(whichHero, MAX(whichHero->hero.xp, need));
    }
    return 0;
}
DWORD GetHeroLevel(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichHero ? whichHero->hero.level : 0);
}
DWORD SuspendHeroXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
    if (whichHero) whichHero->hero.suspend_xp = flag;
    return 0;
}
DWORD IsSuspendedXP(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichHero && whichHero->hero.suspend_xp);
}
DWORD SelectHeroSkill(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    LONG abilcode = jass_checkinteger(j, 2);
    if (whichHero) {
        G_HeroLearnSkill(whichHero, (DWORD)abilcode);
    }
    return 0;
}
DWORD GetUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG abilcode = jass_checkinteger(j, 2);
    return jass_pushinteger(j, whichUnit ? (LONG)G_UnitAbilityLevel(whichUnit, (DWORD)abilcode) : 0);
}
DWORD SetUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG abilcode = jass_checkinteger(j, 2);
    LONG level = jass_checkinteger(j, 3);
    return jass_pushinteger(j, whichUnit ? (LONG)G_UnitSetAbilityLevel(whichUnit, (DWORD)abilcode, level) : 0);
}
DWORD IncUnitAbilityLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG abilcode = jass_checkinteger(j, 2);
    DWORD cur;
    if (!whichUnit) return jass_pushinteger(j, 0);
    cur = G_UnitAbilityLevel(whichUnit, (DWORD)abilcode);
    if (!cur) return jass_pushinteger(j, 0);
    return jass_pushinteger(j, (LONG)G_UnitSetAbilityLevel(whichUnit, (DWORD)abilcode, (LONG)cur + 1));
}
DWORD GetHeroStr(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    BOOL includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses; /* TODO: attribute bonuses from items/auras are not tracked separately yet. */
    return jass_pushinteger(j, whichHero ? (LONG)whichHero->hero.str : 0);
}
DWORD GetHeroAgi(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    BOOL includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses;
    return jass_pushinteger(j, whichHero ? (LONG)whichHero->hero.agi : 0);
}
DWORD GetHeroInt(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    BOOL includeBonuses = jass_checkboolean(j, 2);
    (void)includeBonuses;
    return jass_pushinteger(j, whichHero ? (LONG)whichHero->hero.intel : 0);
}
DWORD GetUnitLevel(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) return jass_pushinteger(j, 0);
    if (G_UnitIsHero(whichUnit)) return jass_pushinteger(j, (LONG)whichUnit->hero.level);
    return jass_pushinteger(j, whichUnit->data.UnitBalance ? whichUnit->data.UnitBalance->level : 0);
}
DWORD GetUnitCurrentOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (LONG)G_GetIssuedOrderId(whichUnit) : 0);
}
DWORD UnitInventorySize(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (LONG)G_InventoryCapacity(whichUnit) : 0);
}
DWORD UnitDropItemPoint(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    FLOAT x = jass_checknumber(j, 3);
    FLOAT y = jass_checknumber(j, 4);
    LONG slot;
    if (!whichUnit || !whichItem || whichItem->item.carrier != whichUnit)
        return jass_pushboolean(j, 0);
    slot = whichItem->item.inventory_slot;
    if (slot < 0) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, G_DropItemAt(whichUnit, (DWORD)slot, &MAKE(VECTOR2, x, y)));
}
DWORD UnitDamageTarget(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT target = jass_checkhandle(j, 2, "widget");
    FLOAT amount = jass_checknumber(j, 3);
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
DWORD UnitAddType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPDWORD whichUnitType = jass_checkhandle(j, 2, "unittype");
    if (!whichUnit || !whichUnitType || *whichUnitType >= 32) return jass_pushboolean(j, 0);
    whichUnit->script_unit_types |= (1u << *whichUnitType);
    return jass_pushboolean(j, 1);
}
DWORD UnitRemoveType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPDWORD whichUnitType = jass_checkhandle(j, 2, "unittype");
    if (!whichUnit || !whichUnitType || *whichUnitType >= 32) return jass_pushboolean(j, 0);
    whichUnit->script_unit_types &= ~(1u << *whichUnitType);
    return jass_pushboolean(j, 1);
}
DWORD UnitCountBuffsEx(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL timedLife;
    LONG count = 0;
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
DWORD UnitPauseTimedLife(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
    DWORD now;
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
DWORD UnitResetCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    S_SpellResetCooldowns(whichUnit);
    return 0;
}

DWORD BlzGetUnitAbilityCooldownRemaining(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD const abilityId = (DWORD)jass_checkinteger(j, 2);
    return jass_pushnumber(j, S_SpellCooldownRemaining(whichUnit, abilityId));
}

DWORD BlzEndUnitAbilityCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD const abilityId = (DWORD)jass_checkinteger(j, 2);
    S_SpellEndCooldown(whichUnit, abilityId);
    return 0;
}

DWORD BlzStartUnitAbilityCooldown(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD const abilityId = (DWORD)jass_checkinteger(j, 2);
    FLOAT const cooldown = jass_checknumber(j, 3);
    S_SpellStartCooldownDuration(whichUnit, abilityId, cooldown);
    return 0;
}

DWORD ReviveHero(LPJASS j) {
    LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    //BOOL doEyecandy = jass_checkboolean(j, 4);
    if (whichHero) {
        G_ReviveHero(whichHero, x, y);
        return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
DWORD ReviveHeroLoc(LPJASS j) {
    //LPEDICT whichHero = jass_checkhandle(j, 1, "unit");
    //HANDLE loc = jass_checkhandle(j, 2, "location");
    //BOOL doEyecandy = jass_checkboolean(j, 3);
    return jass_pushboolean(j, 0);
}
DWORD SetUnitExploded(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL exploded = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetUnitInvulnerable(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->invulnerable = flag;
    return 0;
}
DWORD PauseUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->paused = flag;
    return 0;
}
DWORD IsUnitPaused(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && whichUnit->paused);
}
DWORD SetUnitPathing(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
    if (whichUnit) whichUnit->no_pathing = !flag;
    return 0;
}
DWORD GetUnitPointValue(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, 0);
}
DWORD GetUnitPointValueByType(LPJASS j) {
    //LONG unitType = jass_checkinteger(j, 1);
    return jass_pushinteger(j, 0);
}
DWORD UnitAddItem(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    if (!whichUnit || !whichItem) return jass_pushboolean(j, false);
    return jass_pushboolean(j, G_PickupItem(whichUnit, whichItem));
}
DWORD UnitAddItemById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemId = jass_checkinteger(j, 2);
    if (!whichUnit) return jass_pushnullhandle(j, "item");
    LPEDICT item = SP_SpawnAtLocation(itemId, whichUnit->s.player, &whichUnit->s.origin2);
    if (item && G_PickupItem(whichUnit, item)) {
        return jass_pushlighthandle(j, item, "item");
    } else {
        if (item) G_RemoveItem(item);
        return jass_pushnullhandle(j, "item");
    }
}
DWORD UnitAddItemToSlotById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemId = jass_checkinteger(j, 2);
    LONG itemSlot = jass_checkinteger(j, 3);
    if (!whichUnit || itemSlot < 0 || (DWORD)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushboolean(j, false);
    }
    LPEDICT item = SP_SpawnAtLocation(itemId, whichUnit->s.player, &whichUnit->s.origin2);
    if (item && G_AddItemToSlot(whichUnit, item, (DWORD)itemSlot)) {
        return jass_pushboolean(j, true);
    } else {
        if (item) G_RemoveItem(item);
        return jass_pushboolean(j, false);
    }
}
DWORD UnitRemoveItem(LPJASS j) {
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
DWORD UnitRemoveItemFromSlot(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemSlot = jass_checkinteger(j, 2);
    if (!whichUnit || itemSlot < 0 || (DWORD)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = whichUnit->inventory[itemSlot];
    if (!item) return jass_pushnullhandle(j, "item");
    if (!G_DropItem(whichUnit, (DWORD)itemSlot)) {
        return jass_pushnullhandle(j, "item");
    }
    return jass_pushlighthandle(j, item, "item");
}
DWORD UnitHasItem(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichItem = jass_checkhandle(j, 2, "item");
    if (!whichUnit || !whichItem) return jass_pushboolean(j, 0);
    FOR_LOOP(i, MAX_INVENTORY) {
        if (whichUnit->inventory[i] == whichItem) return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
DWORD UnitItemInSlot(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemSlot = jass_checkinteger(j, 2);
    if (!whichUnit || itemSlot < 0 || (DWORD)itemSlot >= G_InventoryCapacity(whichUnit)) {
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = whichUnit->inventory[itemSlot];
    if (!item) return jass_pushnullhandle(j, "item");
    return jass_pushlighthandle(j, item, "item");
}
DWORD UnitUseItem(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichItem = jass_checkhandle(j, 2, "item");
    return jass_pushboolean(j, 0);
}
DWORD UnitUseItemPoint(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichItem = jass_checkhandle(j, 2, "item");
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
DWORD UnitUseItemTarget(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichItem = jass_checkhandle(j, 2, "item");
    //HANDLE target = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, 0);
}
DWORD GetUnitRallyPoint(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    API_ALLOC(VECTOR2, location);
    if (whichUnit) {
        G_ResolveRallyTarget(whichUnit, location, NULL);
    }
    return 1;
}
DWORD GetUnitRallyUnit(LPJASS j) {
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
DWORD GetUnitRallyDestructable(LPJASS j) {
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
DWORD GetUnitLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    API_ALLOC(VECTOR2, location);
    if (whichUnit) {
        *location = whichUnit->s.origin2;
    }
    return 1;
}
DWORD GetUnitDefaultMoveSpeed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.MoveSpeed : 0);
}
DWORD GetOwningPlayer(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (!whichUnit) {
        return jass_pushnullhandle(j, "player");
    }
    return jass_pushlighthandle(j, G_GetPlayerByNumber(whichUnit->s.player), "player");
}
DWORD GetUnitTypeId(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? (LONG)whichUnit->class_id : 0);
}
DWORD GetUnitRace(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushnullhandle(j, "race");
}
DWORD GetUnitName(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR name = whichUnit ? G_UnitName(whichUnit->class_id) : NULL;
    return jass_pushstring(j, name ? name : "");
}
DWORD GetUnitFoodUsed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->data.UnitBalance->foodUsed : 0);
}
DWORD GetUnitFoodMade(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->data.UnitBalance->foodMade : 0);
}
DWORD IsUnitInGroup(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    ggroup_t *whichGroup = jass_checkhandle(j, 2, "group");
    if (!whichUnit || !whichGroup) return jass_pushboolean(j, 0);
    FOR_LOOP(i, whichGroup->num_units) {
        if (whichGroup->units[i] == whichUnit) return jass_pushboolean(j, 1);
    }
    return jass_pushboolean(j, 0);
}
DWORD IsUnitInForce(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPDWORD whichForce = jass_checkhandle(j, 2, "force");
    if (!whichUnit || !whichForce) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, (*whichForce) & (1 << whichUnit->s.player));
}
DWORD IsUnitOwnedByPlayer(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, whichUnit->s.player == PLAYER_NUM(whichPlayer));
}
DWORD IsUnitAlly(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j,
        G_PlayerTreatsPlayerAsAlly(PLAYER_NUM(whichPlayer), whichUnit->s.player));
}
DWORD IsUnitEnemy(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j,
        !G_PlayerTreatsPlayerAsAlly(PLAYER_NUM(whichPlayer), whichUnit->s.player));
}
DWORD IsUnitVisible(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitDetected(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitInvisible(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitFogged(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitMasked(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitSelected(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, (whichUnit->selected >> PLAYER_NUM(whichPlayer)) & 1);
}
DWORD IsUnitRace(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichRace = jass_checkhandle(j, 2, "race");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitType(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPDWORD whichUnitType = jass_checkhandle(j, 2, "unittype");
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
    return jass_pushboolean(j, 0);
}
DWORD IsUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichSpecifiedUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichUnit != NULL && whichUnit == whichSpecifiedUnit);
}
DWORD IsUnitInRange(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT otherUnit = jass_checkhandle(j, 2, "unit");
    FLOAT distance = jass_checknumber(j, 3);
    if (!whichUnit || !otherUnit) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, &otherUnit->s.origin2) <= distance);
}
DWORD IsUnitInRangeXY(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT distance = jass_checknumber(j, 4);
    if (!whichUnit) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, &MAKE(VECTOR2, x, y)) <= distance);
}
DWORD IsUnitInRangeLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    FLOAT distance = jass_checknumber(j, 3);
    if (!whichUnit || !whichLocation) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, Vector2_distance(&whichUnit->s.origin2, whichLocation) <= distance);
}
DWORD IsUnitHidden(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && (whichUnit->s.renderfx & RF_HIDDEN));
}
DWORD IsUnitIllusion(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && (whichUnit->aiflags & AI_ILLUSION));
}
DWORD IsUnitInTransport(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPEDICT whichTransport = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichUnit && whichTransport &&
                              S_CargoTransportForUnit(whichUnit) == whichTransport);
}
DWORD IsUnitLoaded(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && S_CargoTransportForUnit(whichUnit) != NULL);
}
DWORD IsHeroUnitId(LPJASS j) {
    //LONG unitId = jass_checkinteger(j, 1);
    return jass_pushboolean(j, 0);
}
DWORD UnitShareVision(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    //BOOL share = jass_checkboolean(j, 3);
    return 0;
}
DWORD UnitSuspendDecay(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL suspend = jass_checkboolean(j, 2);
    return 0;
}
DWORD UnitAddAbility(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD abilityId = jass_checkinteger(j, 2);
    return jass_pushboolean(j, G_ActorAddSkill(whichUnit, abilityId));
}
DWORD UnitRemoveAbility(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD abilityId = jass_checkinteger(j, 2);
    return jass_pushboolean(j, G_ActorRemoveSkill(whichUnit, abilityId));
}
DWORD UnitMakeAbilityPermanent(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL permanent = jass_checkboolean(j, 2);
    DWORD abilityId = jass_checkinteger(j, 3);
    return jass_pushboolean(j, G_ActorSetSkillPermanent(whichUnit, abilityId, permanent));
}
DWORD UnitRemoveBuffs(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL removePositive = jass_checkboolean(j, 2);
    //BOOL removeNegative = jass_checkboolean(j, 3);
    return 0;
}
DWORD UnitRemoveBuffsEx(LPJASS j) {
    /* TODO: ability-owned dispel filtering is not represented yet. */
    (void)j;
    return 0;
}
DWORD UnitAddSleep(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL add = jass_checkboolean(j, 2);
    G_UnitSetCanSleep(whichUnit, add);
    return 0;
}
DWORD UnitCanSleep(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_UnitCanSleep(whichUnit));
}
DWORD UnitAddSleepPerm(LPJASS j) {
    /* Sleep Always (Asla) has separate ability-owned semantics (including its
     * Sleep Once/Allow On Any Player Slot data). Keep this native conservative
     * until that ability is represented instead of aliasing it to night sleep. */
    (void)j;
    return 0;
}
DWORD UnitCanSleepPerm(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, whichUnit && G_ActorHasSkill(whichUnit, "Asla"));
}
DWORD UnitIsSleeping(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_UnitIsSleeping(whichUnit));
}
DWORD UnitWakeUp(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    G_UnitWakeUp(whichUnit);
    return 0;
}
DWORD UnitApplyTimedLife(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG buffId = jass_checkinteger(j, 2);
    //FLOAT duration = jass_checknumber(j, 3);
    return 0;
}
DWORD IssueImmediateOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    return jass_pushboolean(j, unit_issueimmediateorder(whichUnit, order));
}
DWORD IssueImmediateOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD order = (DWORD)jass_checkinteger(j, 2);
    return jass_pushboolean(j, unit_issueimmediateorder(whichUnit, G_OrderId2String(order)));
}
DWORD IssuePointOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    FLOAT x = jass_checknumber(j, 3);
    FLOAT y = jass_checknumber(j, 4);
    BOOL ret = unit_issueorder(whichUnit, order, &MAKE(VECTOR2, x, y));
    if (WC3_HUMAN09_DEBUG_ENABLED())
        fprintf(stderr, "WC3_HUMAN09 native=IssuePointOrder unit=%ld id=%.4s order=\"%s\" point=(%.1f,%.1f) accepted=%d caller=\"%s\"\n",
                whichUnit ? (long)(whichUnit - globals.edicts) : -1L,
                whichUnit ? (LPCSTR)&whichUnit->class_id : "----", order ? order : "(null)",
                x, y, (int)ret,
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(root)");
    return jass_pushboolean(j, ret);
}
DWORD IssuePointOrderLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 3, "location");
    BOOL ret = unit_issueorder(whichUnit, order, whichLocation);
    if (WC3_HUMAN09_DEBUG_ENABLED())
        fprintf(stderr, "WC3_HUMAN09 native=IssuePointOrderLoc unit=%ld id=%.4s order=\"%s\" point=(%.1f,%.1f) accepted=%d caller=\"%s\"\n",
                whichUnit ? (long)(whichUnit - globals.edicts) : -1L,
                whichUnit ? (LPCSTR)&whichUnit->class_id : "----", order ? order : "(null)",
                whichLocation ? whichLocation->x : 0.0f, whichLocation ? whichLocation->y : 0.0f,
                (int)ret,
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(root)");
    return jass_pushboolean(j, ret);
}
DWORD IssuePointOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD order = (DWORD)jass_checkinteger(j, 2);
    VECTOR2 point = { jass_checknumber(j, 3), jass_checknumber(j, 4) };

    /* Building rawcodes are valid point-order ids, but they are not entries in
     * the canonical Warcraft order table. Keep construction on the existing
     * authoritative build path and leave ordinary ids to the generic router. */
    if (G_UnitIsBuilding(order))
        return jass_pushboolean(j, G_IssueBuildOrder(whichUnit, order, &point));
    return jass_pushboolean(j, unit_issueorder(whichUnit, G_OrderId2String(order), &point));
}
DWORD IssuePointOrderByIdLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD order = (DWORD)jass_checkinteger(j, 2);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 3, "location");

    if (G_UnitIsBuilding(order))
        return jass_pushboolean(j, G_IssueBuildOrder(whichUnit, order, whichLocation));
    return jass_pushboolean(j, unit_issueorder(whichUnit, G_OrderId2String(order), whichLocation));
}
DWORD IssueTargetOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, unit_issuetargetorder(whichUnit, order, targetWidget));
}
DWORD IssueTargetOrderById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD order = (DWORD)jass_checkinteger(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    LPCSTR order_name = G_OrderId2String(order);
    BOOL accepted;

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
DWORD IssueInstantTargetOrder(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LPCSTR order = jass_checkstring(j, 2);
    //HANDLE targetWidget = jass_checkhandle(j, 3, "widget");
    //HANDLE instantTargetWidget = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
DWORD IssueInstantTargetOrderById(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG order = jass_checkinteger(j, 2);
    //HANDLE targetWidget = jass_checkhandle(j, 3, "widget");
    //HANDLE instantTargetWidget = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
DWORD IssueBuildOrder(LPJASS j) {
    return jass_pushboolean(j, 0);
}
DWORD IssueBuildOrderById(LPJASS j) {
    LPEDICT whichPeon = jass_checkhandle(j, 1, "unit");
    DWORD unitId = (DWORD)jass_checkinteger(j, 2);
    VECTOR2 point = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    BOOL accepted;

    accepted = G_IssueBuildOrder(whichPeon, unitId, &point);
    return jass_pushboolean(j, accepted);
}
DWORD SetResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG amount = jass_checkinteger(j, 2);
    if (whichUnit) S_GoldMineSetResourceAmount(whichUnit, MAX(0, amount));
    return 0;
}
DWORD AddResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG amount = jass_checkinteger(j, 2);
    if (whichUnit) whichUnit->resources += amount;
    return 0;
}
DWORD GetResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? whichUnit->resources : 0);
}
DWORD WaygateGetDestinationX(LPJASS j) {
    //HANDLE waygate = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, 0);
}
DWORD WaygateGetDestinationY(LPJASS j) {
    //HANDLE waygate = jass_checkhandle(j, 1, "unit");
    return jass_pushnumber(j, 0);
}
DWORD WaygateSetDestination(LPJASS j) {
    //HANDLE waygate = jass_checkhandle(j, 1, "unit");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    return 0;
}
DWORD WaygateActivate(LPJASS j) {
    //HANDLE waygate = jass_checkhandle(j, 1, "unit");
    //BOOL activate = jass_checkboolean(j, 2);
    return 0;
}
DWORD WaygateIsActive(LPJASS j) {
    //HANDLE waygate = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
}
DWORD UnitAddIndicator(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG red = jass_checkinteger(j, 2);
    LONG green = jass_checkinteger(j, 3);
    LONG blue = jass_checkinteger(j, 4);
    LONG alpha = jass_checkinteger(j, 5);
    COLOR32 color = MAKE(COLOR32,
        (BYTE)MAX(0, MIN(255, red)),
        (BYTE)MAX(0, MIN(255, green)),
        (BYTE)MAX(0, MIN(255, blue)),
        (BYTE)MAX(0, MIN(255, alpha)));

    G_SendWidgetIndicator(whichUnit, color, currentplayer);
    return 0;
}
DWORD RemoveGuardPosition(LPJASS j) {
    //HANDLE hUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
DWORD RecycleGuardPosition(LPJASS j) {
    //HANDLE hUnit = jass_checkhandle(j, 1, "unit");
    return 0;
}
DWORD CreateUnit(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    DWORD unitid = jass_checkinteger(j, 2);
    VECTOR2 location = MAKE(VECTOR2, jass_checknumber(j, 3), jass_checknumber(j, 4));
    FLOAT facing = jass_checknumber(j, 5);
    if (!player) {
        return jass_pushnullhandle(j, "unit");
    }
    LPEDICT unit = unit_create(PLAYER_NUM(player), unitid, &location, facing);
    return jass_pushlighthandle(j, unit, "unit");
}
DWORD CreateUnitByName(LPJASS j) {
    //HANDLE whichPlayer = jass_checkhandle(j, 1, "player");
    //LPCSTR unitname = jass_checkstring(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    //FLOAT face = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
DWORD CreateUnitAtLoc(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    DWORD unitid = jass_checkinteger(j, 2);
    LPCVECTOR2 location = jass_checkhandle(j, 3, "location");
    FLOAT facing = jass_checknumber(j, 4);
    if (!player || !location) {
        return jass_pushnullhandle(j, "unit");
    }
    LPEDICT unit = unit_create(PLAYER_NUM(player), unitid, location, facing);
    return jass_pushlighthandle(j, unit, "unit");
}
DWORD CreateUnitAtLocByName(LPJASS j) {
    //HANDLE id = jass_checkhandle(j, 1, "player");
    //LPCSTR unitname = jass_checkstring(j, 2);
    //HANDLE whichLocation = jass_checkhandle(j, 3, "location");
    //FLOAT face = jass_checknumber(j, 4);
    return jass_pushnullhandle(j, "unit");
}
DWORD CreateCorpse(LPJASS j) {
    //HANDLE whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG unitid = jass_checkinteger(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    //FLOAT face = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
DWORD CreateBlightedGoldmine(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    VECTOR2 origin = MAKE(VECTOR2, jass_checknumber(j, 2), jass_checknumber(j, 3));
    FLOAT facing = jass_checknumber(j, 4);
    LPEDICT mine;

    if (!player || !(mine = S_CreateBlightedGoldmine(PLAYER_NUM(player), &origin, facing)))
        return jass_pushnullhandle(j, "unit");
    return jass_pushlighthandle(j, mine, "unit");
}
