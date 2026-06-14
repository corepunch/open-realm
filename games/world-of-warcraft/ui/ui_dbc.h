#pragma once
#include "ui_local.h"
#include <lua.h>

/* Character-creation DBC Lua bindings. */
int UIWow_LuaGetAvailableRaces(lua_State *L);
int UIWow_LuaGetAvailableClasses(lua_State *L);
int UIWow_LuaGetClassesForRace(lua_State *L);
int UIWow_LuaGetFactionForRace(lua_State *L);
int UIWow_LuaGetNameForRace(lua_State *L);
int UIWow_LuaGetSelectedRace(lua_State *L);
int UIWow_LuaGetSelectedSex(lua_State *L);
int UIWow_LuaGetSelectedClass(lua_State *L);
int UIWow_LuaSetSelectedRace(lua_State *L);
int UIWow_LuaSetSelectedSex(lua_State *L);
int UIWow_LuaSetSelectedClass(lua_State *L);
int UIWow_LuaIsRaceClassValid(lua_State *L);
int UIWow_LuaGetHairCustomization(lua_State *L);
int UIWow_LuaGetFacialHairCustomization(lua_State *L);
int UIWow_LuaGetCharacterCreateFacing(lua_State *L);
int UIWow_LuaSetCharacterCreateFacing(lua_State *L);
int UIWow_LuaResetCharCustomize(lua_State *L);
int UIWow_LuaGetRandomName(lua_State *L);
int UIWow_LuaCreateCharacter(lua_State *L);
