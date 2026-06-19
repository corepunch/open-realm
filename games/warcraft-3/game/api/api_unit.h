#define UNIT_TYPED_ACCESS(NAME, FIELD, TYPE) \
DWORD SetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    if (whichUnit) { \
        memcpy(&whichUnit->FIELD, jass_checkhandle(j, 2, #TYPE), sizeof(whichUnit->FIELD)); \
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
    if(whichUnit)whichUnit->FIELD = jass_checknumber(j, 2); \
    return 0; \
}  \
DWORD GetUnit##NAME(LPJASS j) {  \
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");  \
    return jass_pushnumber(j, whichUnit ? whichUnit->FIELD : 0); \
}

#define UNITINFO_ACCESS(FIELD) UNIT_ACCESS(FIELD, unitinfo.FIELD)

UNIT_TYPED_ACCESS(PositionLoc, s.origin2, location);
UNIT_ACCESS(X, s.origin.x);
UNIT_ACCESS(Y, s.origin.y);
UNITINFO_ACCESS(MoveSpeed);
UNITINFO_ACCESS(FlyHeight);
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
    if (whichUnit) whichUnit->health.value = 0;
    return 0;
}
DWORD RemoveUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    if (whichUnit) {
        G_FreeEdict(whichUnit);
    }
    return 0;
}
DWORD ShowUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL show = jass_checkboolean(j, 2);
    if (!whichUnit) {
        return 0;
    }
    if (show) {
        whichUnit->s.renderfx &= ~RF_HIDDEN;
    } else {
        whichUnit->s.renderfx |= RF_HIDDEN;
    }
    return 0;
}

JASS_API(SetUnitState,
(EDICT, whichUnit, "unit"),
(UNITSTATE, whichUnitState, "unitstate"),
(number, newVal))
{
    if (!whichUnit || !whichUnitState) {
        return;
    }
    (&whichUnit->health.value)[*whichUnitState] = newVal;
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
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    if (whichUnit) {
        whichUnit->s.origin.x = x;
        whichUnit->s.origin.y = y;
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
    return jass_pushnumber(j, whichUnit ? whichUnit->unitinfo.FlyHeight : 0);
}
DWORD SetUnitOwner(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
//    BOOL changeColor = jass_checkboolean(j, 3);
    if (whichUnit && whichPlayer) whichUnit->s.player = PLAYER_NUM(whichPlayer);
    return 0;
}
DWORD SetUnitColor(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    DWORD *pColor = jass_checkhandle(j, 2, "playercolor");
    if (whichUnit && pColor) {
        whichUnit->unit_color = *pColor;
    }
    return 0;
}
DWORD SetUnitScale(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
//    FLOAT scaleX = jass_checknumber(j, 2);
//    FLOAT scaleY = jass_checknumber(j, 3);
    FLOAT scaleZ = jass_checknumber(j, 4);
    if (whichUnit) whichUnit->s.scale = scaleZ;
    return 0;
}
DWORD SetUnitTimeScale(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //FLOAT timeScale = jass_checknumber(j, 2);
    return 0;
}
DWORD SetUnitBlendTime(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //FLOAT blendTime = jass_checknumber(j, 2);
    return 0;
}
DWORD SetUnitVertexColor(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
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
    if (whichUnit) whichUnit->animation = G_GetAnimation(whichUnit->s.model, whichAnimation);
    return 0;
}
DWORD SetUnitAnimationByIndex(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG whichAnimation = jass_checkinteger(j, 2);
    return 0;
}
DWORD SetUnitAnimationWithRarity(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LPCSTR whichAnimation = jass_checkstring(j, 2);
    //HANDLE rarity = jass_checkhandle(j, 3, "raritycontrol");
    return 0;
}
DWORD AddUnitAnimationProperties(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LPCSTR animProperties = jass_checkstring(j, 2);
    //BOOL add = jass_checkboolean(j, 3);
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
         * (level only increases); attributes/stats derive from the new level. */
        DWORD const need = G_HeroXPForLevel((DWORD)level);
        if (whichHero->hero.xp < need) {
            whichHero->hero.xp = need;
        }
        G_HeroApplyLevel(whichHero, (DWORD)level);
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
        unit_learnability(whichHero, (DWORD)abilcode);
    }
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
    if (unit_additem(whichUnit, whichItem)) {
        /* Item is now tracked in inventory — remove from world but keep alive. */
        gi.UnlinkEntity(whichItem);
        Get_Portrait_f(G_GetPlayerEntityByNumber(whichUnit->s.player));
        return jass_pushboolean(j, true);
    } else {
        return jass_pushboolean(j, false);
    }
}
DWORD UnitAddItemById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemId = jass_checkinteger(j, 2);
    if (!whichUnit) return jass_pushnullhandle(j, "item");
    LPEDICT item = SP_SpawnAtLocation(itemId, 0, &whichUnit->s.origin2);
    if (item && unit_additem(whichUnit, item)) {
        gi.UnlinkEntity(item);
        Get_Portrait_f(G_GetPlayerEntityByNumber(whichUnit->s.player));
        return jass_pushlighthandle(j, item, "item");
    } else {
        if (item) G_FreeEdict(item);
        return jass_pushnullhandle(j, "item");
    }
}
DWORD UnitAddItemToSlotById(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemId = jass_checkinteger(j, 2);
    LONG itemSlot = jass_checkinteger(j, 3);
    if (!whichUnit) return jass_pushboolean(j, false);
    LPEDICT item = SP_SpawnAtLocation(itemId, 0, &whichUnit->s.origin2);
    if (item && unit_additemtoslot(whichUnit, item, (DWORD)itemSlot)) {
        gi.UnlinkEntity(item);
        Get_Portrait_f(G_GetPlayerEntityByNumber(whichUnit->s.player));
        return jass_pushboolean(j, true);
    } else {
        if (item) G_FreeEdict(item);
        return jass_pushboolean(j, false);
    }
}
DWORD UnitRemoveItem(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichItem = jass_checkhandle(j, 2, "item");
    return 0;
}
DWORD UnitRemoveItemFromSlot(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG itemSlot = jass_checkinteger(j, 2);
    if (!whichUnit || itemSlot < 0 || itemSlot >= MAX_INVENTORY) {
        return jass_pushnullhandle(j, "item");
    }
    LPEDICT item = whichUnit->inventory[itemSlot];
    if (!item) return jass_pushnullhandle(j, "item");
    whichUnit->inventory[itemSlot] = NULL;
    /* Move item to unit's current position and put it back in the world. */
    item->s.origin.x = whichUnit->s.origin.x;
    item->s.origin.y = whichUnit->s.origin.y;
    item->s.origin2  = whichUnit->s.origin2;
    gi.LinkEntity(item);
    Get_Portrait_f(G_GetPlayerEntityByNumber(whichUnit->s.player));
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
    if (!whichUnit || itemSlot < 0 || itemSlot >= MAX_INVENTORY) {
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
    LPCSTR name = whichUnit ? UnitStringField(UnitsMetaData, whichUnit->class_id, "unam") : NULL;
    return jass_pushstring(j, name ? name : "");
}
DWORD GetUnitFoodUsed(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? UnitIntegerField(UnitsMetaData, whichUnit->class_id, "IUfu") : 0);
}
DWORD GetUnitFoodMade(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushinteger(j, whichUnit ? UnitIntegerField(UnitsMetaData, whichUnit->class_id, "IUfm") : 0);
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
    LPPLAYER unitOwner = G_GetPlayerByNumber(whichUnit->s.player);
    return jass_pushboolean(j, G_GetPlayerAlliance(unitOwner, whichPlayer, ALLIANCE_PASSIVE));
}
DWORD IsUnitEnemy(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (!whichUnit || !whichPlayer) return jass_pushboolean(j, 0);
    LPPLAYER unitOwner = G_GetPlayerByNumber(whichUnit->s.player);
    return jass_pushboolean(j, !G_GetPlayerAlliance(unitOwner, whichPlayer, ALLIANCE_PASSIVE));
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
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichUnitType = jass_checkhandle(j, 2, "unittype");
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
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitInTransport(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //HANDLE whichTransport = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, 0);
}
DWORD IsUnitLoaded(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
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
DWORD UnitRemoveAbility(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG abilityId = jass_checkinteger(j, 2);
    return jass_pushboolean(j, 0);
}
DWORD UnitRemoveBuffs(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL removePositive = jass_checkboolean(j, 2);
    //BOOL removeNegative = jass_checkboolean(j, 3);
    return 0;
}
DWORD UnitAddSleep(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL add = jass_checkboolean(j, 2);
    return 0;
}
DWORD UnitCanSleep(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
}
DWORD UnitAddSleepPerm(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //BOOL add = jass_checkboolean(j, 2);
    return 0;
}
DWORD UnitCanSleepPerm(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
}
DWORD UnitIsSleeping(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, 0);
}
DWORD UnitWakeUp(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
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
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG order = jass_checkinteger(j, 2);
    return jass_pushboolean(j, 0);
}
DWORD IssuePointOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    FLOAT x = jass_checknumber(j, 3);
    FLOAT y = jass_checknumber(j, 4);
    BOOL ret = unit_issueorder(whichUnit, order, &MAKE(VECTOR2, x, y));
    return jass_pushboolean(j, ret);
}
DWORD IssuePointOrderLoc(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 3, "location");
    BOOL ret = unit_issueorder(whichUnit, order, whichLocation);
    return jass_pushboolean(j, ret);
}
DWORD IssuePointOrderById(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG order = jass_checkinteger(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
DWORD IssuePointOrderByIdLoc(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG order = jass_checkinteger(j, 2);
    //HANDLE whichLocation = jass_checkhandle(j, 3, "location");
    return jass_pushboolean(j, 0);
}
DWORD IssueTargetOrder(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LPCSTR order = jass_checkstring(j, 2);
    LPEDICT targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, unit_issuetargetorder(whichUnit, order, targetWidget));
}
DWORD IssueTargetOrderById(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG order = jass_checkinteger(j, 2);
    //HANDLE targetWidget = jass_checkhandle(j, 3, "widget");
    return jass_pushboolean(j, 0);
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
    //HANDLE whichPeon = jass_checkhandle(j, 1, "unit");
    //LPCSTR unitToBuild = jass_checkstring(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
DWORD IssueBuildOrderById(LPJASS j) {
    //HANDLE whichPeon = jass_checkhandle(j, 1, "unit");
    //LONG unitId = jass_checkinteger(j, 2);
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    return jass_pushboolean(j, 0);
}
DWORD SetResourceAmount(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    LONG amount = jass_checkinteger(j, 2);
    if (whichUnit) whichUnit->resources = amount;
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
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
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
    LPEDICT unit =
    unit_createorfind(PLAYER_NUM(player),
                        unitid,
                        &location,
                        facing);
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
    LPEDICT unit =
    unit_createorfind(PLAYER_NUM(player),
                      unitid,
                      location,
                      facing);
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
    //HANDLE id = jass_checkhandle(j, 1, "player");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //FLOAT face = jass_checknumber(j, 4);
    return jass_pushnullhandle(j, "unit");
}
