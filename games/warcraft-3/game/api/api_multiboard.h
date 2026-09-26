/* Warcraft III multiboards and texttags are server-owned JASS registries.
 * Multiboard display/minimize is per-client local, matching leaderboards.
 * Texttag presentation (TE_FLOATING_TEXT sync) is deferred; natives mutate state. */

static color32_t multiboard_color(int32_t r, int32_t g, int32_t b, int32_t a) {
    return MAKE(color32_t, (uint8_t)MAX(0, MIN(255, r)), (uint8_t)MAX(0, MIN(255, g)),
                (uint8_t)MAX(0, MIN(255, b)), (uint8_t)MAX(0, MIN(255, a)));
}

static struct gmultiboardcell_s *multiboard_item_cell(multiboardItem_t * item) {
    multiboard_t * board = G_MultiboardItemBoard(item);
    return board ? G_MultiboardCell(board, item->row, item->col) : NULL;
}

static void multiboard_each_cell(multiboard_t * board, void (*fn)(struct gmultiboardcell_s *, void *), void *ud) {
    uint32_t row, col;
    if (!board || !board->inuse) return;
    for (row = 0; row < board->rows; row++)
        for (col = 0; col < board->cols; col++)
            fn(&board->cells[row * MAX_MULTIBOARD_COLS + col], ud);
}

static void multiboard_set_style_cell(struct gmultiboardcell_s *cell, void *ud) {
    bool *flags = ud;
    cell->show_value = flags[0];
    cell->show_icon = flags[1];
}

static void multiboard_set_width_cell(struct gmultiboardcell_s *cell, void *ud) {
    cell->width = *(float *)ud;
}

uint32_t CreateMultiboard(jass_t * j) {
    multiboard_t * board = G_AllocMultiboard();
    if (!board) {
        jass_rterror(j, "CreateMultiboard: multiboard registry is full");
        return jass_pushnullhandle(j, "multiboard");
    }
    return jass_pushlighthandle(j, board, "multiboard");
}

uint32_t DestroyMultiboard(jass_t * j) {
    G_FreeMultiboard(jass_checkhandle(j, 1, "multiboard"));
    return 0;
}

uint32_t MultiboardDisplay(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    bool show = jass_checkboolean(j, 2);
    G_SetMultiboardDisplayed(board, currentplayer, show);
    return 0;
}

uint32_t MultiboardMinimize(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    bool minimize = jass_checkboolean(j, 2);
    G_SetMultiboardMinimized(board, currentplayer, minimize);
    return 0;
}

uint32_t IsMultiboardMinimized(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    return jass_pushboolean(j, G_IsMultiboardMinimized(board, currentplayer));
}

uint32_t MultiboardSetTitleText(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    cstring_t label = jass_checkstring(j, 2);
    if (board && board->inuse) {
        strlcpy(board->title, G_LevelString(label ? label : ""), sizeof(board->title));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetRowCount(jass_t * j) {
    G_MultiboardSetRowCount(jass_checkhandle(j, 1, "multiboard"), jass_checkinteger(j, 2));
    return 0;
}

uint32_t MultiboardSetColumnCount(jass_t * j) {
    G_MultiboardSetColumnCount(jass_checkhandle(j, 1, "multiboard"), jass_checkinteger(j, 2));
    return 0;
}

uint32_t MultiboardGetItem(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    int32_t row = jass_checkinteger(j, 2), col = jass_checkinteger(j, 3);
    multiboardItem_t * item = G_MultiboardGetItem(board, row, col);
    if (!item) {
        if (board && board->inuse)
            jass_rterror(j, "MultiboardGetItem: cell out of range or item registry full");
        return jass_pushnullhandle(j, "multiboarditem");
    }
    return jass_pushlighthandle(j, item, "multiboarditem");
}

uint32_t MultiboardReleaseItem(jass_t * j) {
    G_MultiboardReleaseItem(jass_checkhandle(j, 1, "multiboarditem"));
    return 0;
}

uint32_t MultiboardSetItemStyle(jass_t * j) {
    multiboardItem_t * item = jass_checkhandle(j, 1, "multiboarditem");
    bool show_value = jass_checkboolean(j, 2), show_icon = jass_checkboolean(j, 3);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    multiboard_t * board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->show_value = show_value;
        cell->show_icon = show_icon;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetItemValue(jass_t * j) {
    multiboardItem_t * item = jass_checkhandle(j, 1, "multiboarditem");
    cstring_t value = jass_checkstring(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    multiboard_t * board = G_MultiboardItemBoard(item);
    if (cell) {
        strlcpy(cell->value, G_LevelString(value ? value : ""), sizeof(cell->value));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetItemValueColor(jass_t * j) {
    multiboardItem_t * item = jass_checkhandle(j, 1, "multiboarditem");
    int32_t r = jass_checkinteger(j, 2), g = jass_checkinteger(j, 3);
    int32_t b = jass_checkinteger(j, 4), a = jass_checkinteger(j, 5);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    multiboard_t * board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->value_color = multiboard_color(r, g, b, a);
        cell->value_color_set = true;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetItemWidth(jass_t * j) {
    multiboardItem_t * item = jass_checkhandle(j, 1, "multiboarditem");
    float width = jass_checknumber(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    multiboard_t * board = G_MultiboardItemBoard(item);
    if (cell) {
        cell->width = width;
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetItemIcon(jass_t * j) {
    multiboardItem_t * item = jass_checkhandle(j, 1, "multiboarditem");
    cstring_t icon = jass_checkstring(j, 2);
    struct gmultiboardcell_s *cell = multiboard_item_cell(item);
    multiboard_t * board = G_MultiboardItemBoard(item);
    if (cell) {
        strlcpy(cell->icon, G_LevelString(icon ? icon : ""), sizeof(cell->icon));
        G_MarkMultiboardDirty(board);
    }
    return 0;
}

uint32_t MultiboardSetItemsStyle(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    bool flags[2] = { jass_checkboolean(j, 2), jass_checkboolean(j, 3) };
    multiboard_each_cell(board, multiboard_set_style_cell, flags);
    G_MarkMultiboardDirty(board);
    return 0;
}

uint32_t MultiboardSetItemsWidth(jass_t * j) {
    multiboard_t * board = jass_checkhandle(j, 1, "multiboard");
    float width = jass_checknumber(j, 2);
    multiboard_each_cell(board, multiboard_set_width_cell, &width);
    G_MarkMultiboardDirty(board);
    return 0;
}

uint32_t CreateTextTag(jass_t * j) {
    texttag_t * tag = G_AllocTextTag();
    if (!tag) {
        jass_rterror(j, "CreateTextTag: texttag registry is full");
        return jass_pushnullhandle(j, "texttag");
    }
    return jass_pushlighthandle(j, tag, "texttag");
}

uint32_t DestroyTextTag(jass_t * j) {
    G_FreeTextTag(jass_checkhandle(j, 1, "texttag"));
    return 0;
}

uint32_t SetTextTagText(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    cstring_t text = jass_checkstring(j, 2);
    float height = jass_checknumber(j, 3);
    if (tag && tag->inuse) {
        strlcpy(tag->text, G_LevelString(text ? text : ""), sizeof(tag->text));
        tag->height = height;
    }
    return 0;
}

uint32_t SetTextTagColor(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    int32_t r = jass_checkinteger(j, 2), g = jass_checkinteger(j, 3);
    int32_t b = jass_checkinteger(j, 4), a = jass_checkinteger(j, 5);
    if (tag && tag->inuse) tag->color = multiboard_color(r, g, b, a);
    return 0;
}

uint32_t SetTextTagPosUnit(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    edict_t * unit = jass_checkhandle(j, 2, "unit");
    float height_offset = jass_checknumber(j, 3);
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

uint32_t SetTextTagVelocity(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    float xvel = jass_checknumber(j, 2), yvel = jass_checknumber(j, 3);
    if (tag && tag->inuse) {
        tag->xvel = xvel;
        tag->yvel = yvel;
    }
    return 0;
}

uint32_t SetTextTagVisibility(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    bool visible = jass_checkboolean(j, 2);
    G_SetTextTagVisible(tag, currentplayer, visible);
    return 0;
}

uint32_t SetTextTagPermanent(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    bool permanent = jass_checkboolean(j, 2);
    if (tag && tag->inuse) tag->permanent = permanent;
    return 0;
}

uint32_t SetTextTagLifespan(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    float lifespan = jass_checknumber(j, 2);
    if (tag && tag->inuse) tag->lifespan = lifespan;
    return 0;
}

uint32_t SetTextTagFadepoint(jass_t * j) {
    texttag_t * tag = jass_checkhandle(j, 1, "texttag");
    float fadepoint = jass_checknumber(j, 2);
    if (tag && tag->inuse) tag->fadepoint = fadepoint;
    return 0;
}
