/* Warcraft III leaderboards are server-owned data models.  FDF owns the board
 * chrome; JASS owns labels/items/styles and per-player board assignment. */
static COLOR32 leaderboard_color(int32_t r, int32_t g, int32_t b, int32_t a) {
    return MAKE(COLOR32, (uint8_t)MAX(0, MIN(255, r)), (uint8_t)MAX(0, MIN(255, g)),
                (uint8_t)MAX(0, MIN(255, b)), (uint8_t)MAX(0, MIN(255, a)));
}
static struct gleaderboarditem_s *leaderboard_item(LPLEADERBOARD board, int32_t index) {
    return board && board->inuse && index >= 0 && (uint32_t)index < board->item_count
        ? &board->items[index] : NULL;
}
static int leaderboard_compare_items(struct gleaderboarditem_s const *x,
                                     struct gleaderboarditem_s const *y, int key) {
    if (key == 0) return (x->value > y->value) - (x->value < y->value);
    if (key == 1) return (x->player > y->player) - (x->player < y->player);
    return strcmp(x->label, y->label);
}
static void leaderboard_sort(LPLEADERBOARD board, int key, bool ascending) {
    if (!board || !board->inuse) return;
    for (uint32_t i = 1; i < board->item_count; i++) {
        struct gleaderboarditem_s item = board->items[i];
        uint32_t j = i;
        while (j) {
            int cmp = leaderboard_compare_items(&board->items[j - 1], &item, key);
            if (ascending ? cmp <= 0 : cmp >= 0) break;
            board->items[j] = board->items[j - 1];
            j--;
        }
        board->items[j] = item;
    }
    G_MarkLeaderboardDirty(board);
}
uint32_t CreateLeaderboard(LPJASS j) {
    LPLEADERBOARD board = G_AllocLeaderboard();
    if (!board) {
        jass_rterror(j, "CreateLeaderboard: leaderboard registry is full");
        return jass_pushnullhandle(j, "leaderboard");
    }
    return jass_pushlighthandle(j, board, "leaderboard");
}
uint32_t DestroyLeaderboard(LPJASS j) { G_FreeLeaderboard(jass_checkhandle(j, 1, "leaderboard")); return 0; }
uint32_t LeaderboardDisplay(LPJASS j) { LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); bool show = jass_checkboolean(j, 2); G_SetLeaderboardDisplayed(b, currentplayer, show); return 0; }
uint32_t IsLeaderboardDisplayed(LPJASS j) { LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); return jass_pushboolean(j, G_IsLeaderboardDisplayed(b, currentplayer)); }
uint32_t LeaderboardGetItemCount(LPJASS j) { LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); return jass_pushinteger(j, b && b->inuse ? (int32_t)b->item_count : 0); }
uint32_t LeaderboardSetSizeByItemCount(LPJASS j) { LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); int32_t n = jass_checkinteger(j, 2); if (b && b->inuse) { b->size_by_item_count = MAX(0, n); G_MarkLeaderboardDirty(b); } return 0; }
uint32_t LeaderboardAddItem(LPJASS j) {
    LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); cstring_t label = jass_checkstring(j, 2);
    int32_t value = jass_checkinteger(j, 3); LPPLAYER p = jass_checkhandle(j, 4, "player");
    if (!b || !b->inuse || b->item_count >= MAX_LEADERBOARD_ITEMS) return 0;
    struct gleaderboarditem_s *item = &b->items[b->item_count++]; memset(item, 0, sizeof(*item));
    strlcpy(item->label, G_LevelString(label ? label : ""), sizeof(item->label)); item->value = value;
    item->player = p ? (int32_t)PLAYER_NUM(p) : -1; item->show_label = item->show_value = item->show_icon = true;
    G_MarkLeaderboardDirty(b); return 0;
}
uint32_t LeaderboardRemoveItem(LPJASS j) { LPLEADERBOARD b = jass_checkhandle(j, 1, "leaderboard"); int32_t n = jass_checkinteger(j, 2); if (b && b->inuse && n >= 0 && (uint32_t)n < b->item_count) { memmove(&b->items[n], &b->items[n + 1], (b->item_count - (uint32_t)n - 1) * sizeof(b->items[0])); b->item_count--; memset(&b->items[b->item_count], 0, sizeof(b->items[0])); G_MarkLeaderboardDirty(b); } return 0; }
uint32_t LeaderboardRemovePlayerItem(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); LPPLAYER p=jass_checkhandle(j,2,"player"); if(b&&p)FOR_LOOP(i,b->item_count)if(b->items[i].player==(int32_t)PLAYER_NUM(p)){memmove(&b->items[i],&b->items[i+1],(b->item_count-i-1)*sizeof(b->items[0]));b->item_count--;memset(&b->items[b->item_count],0,sizeof(b->items[0]));G_MarkLeaderboardDirty(b);break;} return 0; }
uint32_t LeaderboardClear(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); if(b&&b->inuse){memset(b->items,0,sizeof(b->items));b->item_count=0;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSortItemsByValue(LPJASS j) { leaderboard_sort(jass_checkhandle(j, 1, "leaderboard"), 0, jass_checkboolean(j, 2)); return 0; }
uint32_t LeaderboardSortItemsByPlayer(LPJASS j) { leaderboard_sort(jass_checkhandle(j, 1, "leaderboard"), 1, jass_checkboolean(j, 2)); return 0; }
uint32_t LeaderboardSortItemsByLabel(LPJASS j) { leaderboard_sort(jass_checkhandle(j, 1, "leaderboard"), 2, jass_checkboolean(j, 2)); return 0; }
uint32_t LeaderboardHasPlayerItem(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); LPPLAYER p=jass_checkhandle(j,2,"player"); if(b&&p)FOR_LOOP(i,b->item_count)if(b->items[i].player==(int32_t)PLAYER_NUM(p))return jass_pushboolean(j,true); return jass_pushboolean(j,false); }
uint32_t LeaderboardGetPlayerIndex(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); LPPLAYER p=jass_checkhandle(j,2,"player"); if(b&&p)FOR_LOOP(i,b->item_count)if(b->items[i].player==(int32_t)PLAYER_NUM(p))return jass_pushinteger(j,(int32_t)i); return jass_pushinteger(j,-1); }
uint32_t LeaderboardSetLabel(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); cstring_t label=jass_checkstring(j,2); if(b&&b->inuse){strlcpy(b->label,G_LevelString(label?label:""),sizeof(b->label));G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardGetLabelText(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); return jass_pushstring(j,b&&b->inuse?b->label:""); }
uint32_t PlayerSetLeaderboard(LPJASS j) { LPPLAYER p=jass_checkhandle(j,1,"player"); LPLEADERBOARD b=jass_checkhandle(j,2,"leaderboard"); if(p)G_SetPlayerLeaderboard(PLAYER_NUM(p),b); return 0; }
uint32_t PlayerGetLeaderboard(LPJASS j) { LPPLAYER p=jass_checkhandle(j,1,"player"); LPLEADERBOARD b=p?G_PlayerLeaderboard(PLAYER_NUM(p)):NULL; return b?jass_pushlighthandle(j,b,"leaderboard"):jass_pushnullhandle(j,"leaderboard"); }
uint32_t LeaderboardSetLabelColor(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); int32_t r=jass_checkinteger(j,2),g=jass_checkinteger(j,3),bl=jass_checkinteger(j,4),a=jass_checkinteger(j,5); if(b&&b->inuse){b->label_color=leaderboard_color(r,g,bl,a);b->label_color_set=true;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetValueColor(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); int32_t r=jass_checkinteger(j,2),g=jass_checkinteger(j,3),bl=jass_checkinteger(j,4),a=jass_checkinteger(j,5); if(b&&b->inuse){b->value_color=leaderboard_color(r,g,bl,a);b->value_color_set=true;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetStyle(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); if(b&&b->inuse){b->show_label=jass_checkboolean(j,2);b->show_names=jass_checkboolean(j,3);b->show_values=jass_checkboolean(j,4);b->show_icons=jass_checkboolean(j,5);G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetItemValue(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); struct gleaderboarditem_s *i=leaderboard_item(b,jass_checkinteger(j,2)); int32_t v=jass_checkinteger(j,3); if(i){i->value=v;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetItemLabel(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); struct gleaderboarditem_s *i=leaderboard_item(b,jass_checkinteger(j,2)); cstring_t v=jass_checkstring(j,3); if(i){strlcpy(i->label,G_LevelString(v?v:""),sizeof(i->label));G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetItemStyle(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); struct gleaderboarditem_s *i=leaderboard_item(b,jass_checkinteger(j,2)); bool l=jass_checkboolean(j,3),v=jass_checkboolean(j,4),icon=jass_checkboolean(j,5); if(i){i->show_label=l;i->show_value=v;i->show_icon=icon;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetItemLabelColor(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); struct gleaderboarditem_s *i=leaderboard_item(b,jass_checkinteger(j,2)); int32_t r=jass_checkinteger(j,3),g=jass_checkinteger(j,4),bl=jass_checkinteger(j,5),a=jass_checkinteger(j,6); if(i){i->label_color=leaderboard_color(r,g,bl,a);i->label_color_set=true;G_MarkLeaderboardDirty(b);} return 0; }
uint32_t LeaderboardSetItemValueColor(LPJASS j) { LPLEADERBOARD b=jass_checkhandle(j,1,"leaderboard"); struct gleaderboarditem_s *i=leaderboard_item(b,jass_checkinteger(j,2)); int32_t r=jass_checkinteger(j,3),g=jass_checkinteger(j,4),bl=jass_checkinteger(j,5),a=jass_checkinteger(j,6); if(i){i->value_color=leaderboard_color(r,g,bl,a);i->value_color_set=true;G_MarkLeaderboardDirty(b);} return 0; }
