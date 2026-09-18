/* Warcraft III multiboards and texttags are server-owned JASS registries.
 * Multiboard display/minimize is per-client local, matching leaderboards.
 * Texttag presentation (TE_FLOATING_TEXT sync) is deferred; natives mutate state. */

static COLOR32 multiboard_color(LONG r, LONG g, LONG b, LONG a) {
    return MAKE(COLOR32, (BYTE)MAX(0, MIN(255, r)), (BYTE)MAX(0, MIN(255, g)),
                (BYTE)MAX(0, MIN(255, b)), (BYTE)MAX(0, MIN(255, a)));
}

static struct gmultiboardcell_s *multiboard_item_cell(LPMULTIBOARDITEM item) {
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    return board ? G_MultiboardCell(board, item->row, item->col) : NULL;
}

static void multiboard_each_cell(LPMULTIBOARD board, void (*fn)(struct gmultiboardcell_s *, void *), void *ud) {
    DWORD row, col;
    if (!board || !board->inuse) return;
    for (row = 0; row < board->rows; row++)
        for (col = 0; col < board->cols; col++)
            fn(&board->cells[row * MAX_MULTIBOARD_COLS + col], ud);
}

static void multiboard_set_style_cell(struct gmultiboardcell_s *cell, void *ud) {
    BOOL *flags = ud;
    cell->show_value = flags[0];
    cell->show_icon = flags[1];
}

static void multiboard_set_width_cell(struct gmultiboardcell_s *cell, void *ud) {
    cell->width = *(FLOAT *)ud;
}

DWORD CreateMultiboard(LPJASS j) {
    LPMULTIBOARD board = G_AllocMultiboard();
    if (!board) {
        jass_rterror(j, "CreateMultiboard: multiboard registry is full");
        return jass_pushnullhandle(j, "multiboard");
    }
    return jass_pushlighthandle(j, board, "multiboard");
}

DWORD DestroyMultiboard(LPJASS j) {
    G_FreeMultiboard(jass_checkhandle(j, 1, "multiboard"));
    return 0;
}

DWORD MultiboardDisplay(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    BOOL show = jass_checkboolean(j, 2);
    G_SetMultiboardDisplayed(board, currentplayer, show);
    return 0;
}

DWORD MultiboardMinimize(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    BOOL minimize = jass_checkboolean(j, 2);
    G_SetMultiboardMinimized(board, currentplayer, minimize);
    return 0;
}

DWORD IsMultiboardMinimized(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    return jass_pushboolean(j, G_IsMultiboardMinimized(board, currentplayer));
}

DWORD MultiboardSetTitleText(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    LPCSTR label = jass_checkstring(j, 2);
    if (board && board->inuse) {
        strlcpy(board->title, G_LevelString(label ? label : ""), sizeof(board->title));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetRowCount(LPJASS j) {
    G_MultiboardSetRowCount(jass_checkhandle(j, 1, "multiboard"), jass_checkinteger(j, 2));
    return 0;
}

DWORD MultiboardSetColumnCount(LPJASS j) {
    G_MultiboardSetColumnCount(jass_checkhandle(j, 1, "multiboard"), jass_checkinteger(j, 2));
    return 0;
}

DWORD MultiboardGetItem(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    LONG row = jass_checkinteger(j, 2), col = jass_checkinteger(j, 3);
    LPMULTIBOARDITEM item = G_MultiboardGetItem(board, row, col);
    if (!item) {
        if (board && board->inuse)
            jass_rterror(j, "MultiboardGetItem: cell out of range or item registry full");
        return jass_pushnullhandle(j, "multiboarditem");
    }
    return jass_pushlighthandle(j, item, "multiboarditem");
}

DWORD MultiboardReleaseItem(LPJASS j) {
    G_MultiboardReleaseItem(jass_checkhandle(j, 1, "multiboarditem"));
    return 0;
}

DWORD MultiboardSetItemStyle(LPJASS j) {
    LPMULTIBOARDITEM item = jass_checkhandle(j, 1, "multiboarditem");
    BOOL show_value = jass_checkboolean(j, 2), show_icon = jass_checkboolean(j, 3);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->show_value = show_value;
        cell->show_icon = show_icon;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetItemValue(LPJASS j) {
    LPMULTIBOARDITEM item = jass_checkhandle(j, 1, "multiboarditem");
    LPCSTR value = jass_checkstring(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    if (cell) {
        strlcpy(cell->value, G_LevelString(value ? value : ""), sizeof(cell->value));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetItemValueColor(LPJASS j) {
    LPMULTIBOARDITEM item = jass_checkhandle(j, 1, "multiboarditem");
    LONG r = jass_checkinteger(j, 2), g = jass_checkinteger(j, 3);
    LONG b = jass_checkinteger(j, 4), a = jass_checkinteger(j, 5);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->value_color = multiboard_color(r, g, b, a);
        cell->value_color_set = true;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetItemWidth(LPJASS j) {
    LPMULTIBOARDITEM item = jass_checkhandle(j, 1, "multiboarditem");
    FLOAT width = jass_checknumber(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->width = width;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetItemIcon(LPJASS j) {
    LPMULTIBOARDITEM item = jass_checkhandle(j, 1, "multiboarditem");
    LPCSTR icon = jass_checkstring(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    LPMULTIBOARD board = G_MultiboardItemBoard(item);
    if (cell) {
        strlcpy(cell->icon, G_LevelString(icon ? icon : ""), sizeof(cell->icon));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

DWORD MultiboardSetItemsStyle(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    BOOL flags[2] = { jass_checkboolean(j, 2), jass_checkboolean(j, 3) };
    multiboard_each_cell(board, multiboard_set_style_cell, flags);
    G_MarkMultiboardDirty(board);
    return 0;
}

DWORD MultiboardSetItemsWidth(LPJASS j) {
    LPMULTIBOARD board = jass_checkhandle(j, 1, "multiboard");
    FLOAT width = jass_checknumber(j, 2);
    multiboard_each_cell(board, multiboard_set_width_cell, &width);
    G_MarkMultiboardDirty(board);
    return 0;
}

DWORD CreateTextTag(LPJASS j) {
    LPTEXTTAG tag = G_AllocTextTag();
    if (!tag) {
        jass_rterror(j, "CreateTextTag: texttag registry is full");
        return jass_pushnullhandle(j, "texttag");
    }
    return jass_pushlighthandle(j, tag, "texttag");
}

DWORD DestroyTextTag(LPJASS j) {
    G_FreeTextTag(jass_checkhandle(j, 1, "texttag"));
    return 0;
}

DWORD SetTextTagText(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    LPCSTR text = jass_checkstring(j, 2);
    FLOAT height = jass_checknumber(j, 3);
    if (tag && tag->inuse) {
        strlcpy(tag->text, G_LevelString(text ? text : ""), sizeof(tag->text));
        tag->height = height;
    }
    return 0;
}

DWORD SetTextTagColor(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    LONG r = jass_checkinteger(j, 2), g = jass_checkinteger(j, 3);
    LONG b = jass_checkinteger(j, 4), a = jass_checkinteger(j, 5);
    if (tag && tag->inuse) tag->color = multiboard_color(r, g, b, a);
    return 0;
}

DWORD SetTextTagPosUnit(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    LPEDICT unit = jass_checkhandle(j, 2, "unit");
    FLOAT height_offset = jass_checknumber(j, 3);
    if (tag && tag->inuse) {
        tag->unit = unit;
        tag->height_offset = height_offset;
        if (unit) {
            tag->x = unit->s.origin.x;
            tag->y = unit->s.origin.y;
        }
    }
    return 0;
}

DWORD SetTextTagVelocity(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    FLOAT xvel = jass_checknumber(j, 2), yvel = jass_checknumber(j, 3);
    if (tag && tag->inuse) {
        tag->xvel = xvel;
        tag->yvel = yvel;
    }
    return 0;
}

DWORD SetTextTagVisibility(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    BOOL visible = jass_checkboolean(j, 2);
    G_SetTextTagVisible(tag, currentplayer, visible);
    return 0;
}

DWORD SetTextTagPermanent(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    BOOL permanent = jass_checkboolean(j, 2);
    if (tag && tag->inuse) tag->permanent = permanent;
    return 0;
}

DWORD SetTextTagLifespan(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    FLOAT lifespan = jass_checknumber(j, 2);
    if (tag && tag->inuse) tag->lifespan = lifespan;
    return 0;
}

DWORD SetTextTagFadepoint(LPJASS j) {
    LPTEXTTAG tag = jass_checkhandle(j, 1, "texttag");
    FLOAT fadepoint = jass_checknumber(j, 2);
    if (tag && tag->inuse) tag->fadepoint = fadepoint;
    return 0;
}
