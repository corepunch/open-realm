/*
 * t_hashtable.c — Patch 1.24 hashtable native coverage (InitHashtable / Save* / Load*).
 */
#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"

BOOL run_test_jass(LPCSTR src);

TEST(wc3_api, hashtable_scalar_round_trip_and_missing_keys) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  call SaveInteger(ht, 1, 2, 42)\n"
        "  call SaveReal(ht, 1, 3, 3.5)\n"
        "  call SaveBoolean(ht, 1, 4, true)\n"
        "  call BJassAssert(SaveStr(ht, 1, 5, \"arthas\"), \"SaveStr failed\")\n"
        "  call BJassAssert(HaveSavedInteger(ht, 1, 2), \"missing integer\")\n"
        "  call BJassAssert(LoadInteger(ht, 1, 2) == 42, \"wrong integer\")\n"
        "  call BJassAssert(LoadReal(ht, 1, 3) == 3.5, \"wrong real\")\n"
        "  call BJassAssert(LoadBoolean(ht, 1, 4), \"wrong boolean\")\n"
        "  call BJassAssert(LoadStr(ht, 1, 5) == \"arthas\", \"wrong string\")\n"
        "  call BJassAssert(LoadInteger(ht, 9, 9) == 0, \"missing integer not 0\")\n"
        "  call BJassAssert(LoadReal(ht, 9, 9) == 0.0, \"missing real not 0\")\n"
        "  call BJassAssert(not LoadBoolean(ht, 9, 9), \"missing boolean not false\")\n"
        "  call BJassAssert(LoadStr(ht, 9, 9) == \"\", \"missing string not empty\")\n"
        "  call BJassAssert(LoadUnitHandle(ht, 9, 9) == null, \"missing unit not null\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_type_isolation) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  call SaveInteger(ht, 7, 8, 99)\n"
        "  call BJassAssert(HaveSavedInteger(ht, 7, 8), \"integer slot missing\")\n"
        "  call BJassAssert(not HaveSavedReal(ht, 7, 8), \"integer aliased real\")\n"
        "  call BJassAssert(not HaveSavedBoolean(ht, 7, 8), \"integer aliased boolean\")\n"
        "  call BJassAssert(not HaveSavedString(ht, 7, 8), \"integer aliased string\")\n"
        "  call BJassAssert(not HaveSavedHandle(ht, 7, 8), \"integer aliased handle\")\n"
        "  call BJassAssert(LoadUnitHandle(ht, 7, 8) == null, \"integer loaded as unit\")\n"
        "  call SaveStr(ht, 7, 8, \"x\")\n"
        "  call BJassAssert(HaveSavedString(ht, 7, 8), \"string overwrite missing\")\n"
        "  call BJassAssert(HaveSavedInteger(ht, 7, 8), \"typed slots must not collide\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_flush_child_vs_parent) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  call SaveInteger(ht, 1, 1, 10)\n"
        "  call SaveInteger(ht, 1, 2, 20)\n"
        "  call SaveInteger(ht, 2, 1, 30)\n"
        "  call FlushChildHashtable(ht, 1)\n"
        "  call BJassAssert(not HaveSavedInteger(ht, 1, 1), \"child flush left 1,1\")\n"
        "  call BJassAssert(not HaveSavedInteger(ht, 1, 2), \"child flush left 1,2\")\n"
        "  call BJassAssert(LoadInteger(ht, 2, 1) == 30, \"child flush wiped other parent\")\n"
        "  call FlushParentHashtable(ht)\n"
        "  call BJassAssert(not HaveSavedInteger(ht, 2, 1), \"parent flush left 2,1\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_remove_saved_by_type) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  call SaveInteger(ht, 3, 4, 11)\n"
        "  call SaveReal(ht, 3, 4, 1.25)\n"
        "  call RemoveSavedInteger(ht, 3, 4)\n"
        "  call BJassAssert(not HaveSavedInteger(ht, 3, 4), \"RemoveSavedInteger failed\")\n"
        "  call BJassAssert(HaveSavedReal(ht, 3, 4), \"RemoveSavedInteger wiped real\")\n"
        "  call BJassAssert(LoadReal(ht, 3, 4) == 1.25, \"real value lost\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_unit_and_group_handles) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  local group g = CreateGroup()\n"
        "  local integer uid\n"
        "  call BJassAssert(u != null, \"CreateUnit failed\")\n"
        "  call BJassAssert(SaveUnitHandle(ht, GetHandleId(u), 0, u), \"SaveUnitHandle failed\")\n"
        "  call BJassAssert(SaveGroupHandle(ht, 5, 6, g), \"SaveGroupHandle failed\")\n"
        "  set uid = GetHandleId(u)\n"
        "  call BJassAssert(uid != 0, \"GetHandleId(unit) was 0\")\n"
        "  call BJassAssert(GetHandleId(null) == 0, \"GetHandleId(null) not 0\")\n"
        "  call BJassAssert(LoadUnitHandle(ht, uid, 0) == u, \"LoadUnitHandle mismatch\")\n"
        "  call BJassAssert(LoadGroupHandle(ht, 5, 6) == g, \"LoadGroupHandle mismatch\")\n"
        "  call BJassAssert(HaveSavedHandle(ht, uid, 0), \"HaveSavedHandle unit missing\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_destroyed_group_load) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  local group g = CreateGroup()\n"
        "  call BJassAssert(SaveGroupHandle(ht, 1, 1, g), \"SaveGroupHandle failed\")\n"
        "  call DestroyGroup(g)\n"
        "  call BJassAssert(HaveSavedHandle(ht, 1, 1), \"destroy cleared hashtable slot\")\n"
        "  call BJassAssert(LoadGroupHandle(ht, 1, 1) == null, \"Load of destroyed group not null\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_string_hash_known_vectors) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(StringHash(\"\") == 0, \"empty StringHash\")\n"
        "  call BJassAssert(StringHash(\"case\") == 1865766789, \"StringHash case\")\n"
        "  call BJassAssert(StringHash(\"CASE\") == 1865766789, \"StringHash CASE\")\n"
        "  call BJassAssert(StringHash(\"path/to\") == StringHash(\"path\\\\to\"), \"slash normalize\")\n"
        "  call BJassAssert(StringHash(\"path/to\") == -1197512958, \"StringHash path\")\n"
        "endfunction\n"));
}

TEST(wc3_api, hashtable_player_location_and_gethandleid_stable) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  local player p = Player(0)\n"
        "  local location loc = Location(10.0, 20.0)\n"
        "  local integer a\n"
        "  local integer b\n"
        "  call BJassAssert(SavePlayerHandle(ht, 1, 1, p), \"SavePlayerHandle failed\")\n"
        "  call BJassAssert(SaveLocationHandle(ht, 1, 2, loc), \"SaveLocationHandle failed\")\n"
        "  call BJassAssert(LoadPlayerHandle(ht, 1, 1) == p, \"LoadPlayerHandle mismatch\")\n"
        "  call BJassAssert(LoadLocationHandle(ht, 1, 2) == loc, \"LoadLocationHandle mismatch\")\n"
        "  set a = GetHandleId(p)\n"
        "  set b = GetHandleId(p)\n"
        "  call BJassAssert(a == b and a != 0, \"GetHandleId not stable\")\n"
        "endfunction\n"));
}

#endif /* BZ_TESTS */
