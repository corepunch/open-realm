/* Leaderboards are server-owned data models serialized through the existing UI
 * payload path. C owns labels/items/styles; authoritative layout files own all
 * geometry, and PlayerSetLeaderboard selects the model per client. */
DWORD CreateLeaderboard(LPJASS j) {
    return jass_pushnullhandle(j, "leaderboard");
}
DWORD DestroyLeaderboard(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    return 0;
}
DWORD LeaderboardDisplay(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //BOOL show = jass_checkboolean(j, 2);
    return 0;
}
DWORD IsLeaderboardDisplayed(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    return jass_pushboolean(j, 0);
}
DWORD LeaderboardGetItemCount(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    return jass_pushinteger(j, 0);
}
DWORD LeaderboardSetSizeByItemCount(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG count = jass_checkinteger(j, 2);
    return 0;
}
DWORD LeaderboardAddItem(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LPCSTR label = jass_checkstring(j, 2);
    //LONG value = jass_checkinteger(j, 3);
    //HANDLE p = jass_checkhandle(j, 4, "player");
    return 0;
}
DWORD LeaderboardRemoveItem(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG index = jass_checkinteger(j, 2);
    return 0;
}
DWORD LeaderboardRemovePlayerItem(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //HANDLE p = jass_checkhandle(j, 2, "player");
    return 0;
}
DWORD LeaderboardClear(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    return 0;
}
DWORD LeaderboardSortItemsByValue(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //BOOL ascending = jass_checkboolean(j, 2);
    return 0;
}
DWORD LeaderboardSortItemsByPlayer(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //BOOL ascending = jass_checkboolean(j, 2);
    return 0;
}
DWORD LeaderboardSortItemsByLabel(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //BOOL ascending = jass_checkboolean(j, 2);
    return 0;
}
DWORD LeaderboardHasPlayerItem(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //HANDLE p = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD LeaderboardGetPlayerIndex(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //HANDLE p = jass_checkhandle(j, 2, "player");
    return jass_pushinteger(j, 0);
}
DWORD LeaderboardSetLabel(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LPCSTR label = jass_checkstring(j, 2);
    return 0;
}
DWORD LeaderboardGetLabelText(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    return jass_pushstring(j, 0);
}
DWORD PlayerSetLeaderboard(LPJASS j) {
    //HANDLE toPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE lb = jass_checkhandle(j, 2, "leaderboard");
    return 0;
}
DWORD PlayerGetLeaderboard(LPJASS j) {
    //HANDLE toPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushnullhandle(j, "leaderboard");
}
DWORD LeaderboardSetLabelColor(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
    return 0;
}
DWORD LeaderboardSetValueColor(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
    return 0;
}
DWORD LeaderboardSetStyle(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //BOOL showLabel = jass_checkboolean(j, 2);
    //BOOL showNames = jass_checkboolean(j, 3);
    //BOOL showValues = jass_checkboolean(j, 4);
    //BOOL showIcons = jass_checkboolean(j, 5);
    return 0;
}
DWORD LeaderboardSetItemValue(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG whichItem = jass_checkinteger(j, 2);
    //LONG val = jass_checkinteger(j, 3);
    return 0;
}
DWORD LeaderboardSetItemLabel(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG whichItem = jass_checkinteger(j, 2);
    //LPCSTR val = jass_checkstring(j, 3);
    return 0;
}
DWORD LeaderboardSetItemStyle(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG whichItem = jass_checkinteger(j, 2);
    //BOOL showLabel = jass_checkboolean(j, 3);
    //BOOL showValue = jass_checkboolean(j, 4);
    //BOOL showIcon = jass_checkboolean(j, 5);
    return 0;
}
DWORD LeaderboardSetItemLabelColor(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG whichItem = jass_checkinteger(j, 2);
    //LONG red = jass_checkinteger(j, 3);
    //LONG green = jass_checkinteger(j, 4);
    //LONG blue = jass_checkinteger(j, 5);
    //LONG alpha = jass_checkinteger(j, 6);
    return 0;
}
DWORD LeaderboardSetItemValueColor(LPJASS j) {
    //HANDLE lb = jass_checkhandle(j, 1, "leaderboard");
    //LONG whichItem = jass_checkinteger(j, 2);
    //LONG red = jass_checkinteger(j, 3);
    //LONG green = jass_checkinteger(j, 4);
    //LONG blue = jass_checkinteger(j, 5);
    //LONG alpha = jass_checkinteger(j, 6);
    return 0;
}
