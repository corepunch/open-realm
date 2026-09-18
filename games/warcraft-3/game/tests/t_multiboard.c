#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include <string.h>

BOOL run_test_jass(LPCSTR src);
void setup_test_world(void);
extern LPPLAYER currentplayer;

TEST(wc3_api, multiboard_natives_manage_cells_display_and_minimize) {
    LPPLAYER saved = currentplayer;
    LPMULTIBOARD board;
    struct gmultiboardcell_s *cell;
    LPMULTIBOARDITEM stale;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  multiboard mb = null\n"
        "  multiboarditem mi = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set mb = CreateMultiboard()\n"
        "  call BJassAssert(mb != null, \"multiboard handle\")\n"
        "  call MultiboardSetTitleText(mb, \"Kills\")\n"
        "  call MultiboardSetRowCount(mb, 2)\n"
        "  call MultiboardSetColumnCount(mb, 3)\n"
        "  call MultiboardSetItemsStyle(mb, true, false)\n"
        "  call MultiboardSetItemsWidth(mb, 0.08)\n"
        "  set mi = MultiboardGetItem(mb, 0, 1)\n"
        "  call BJassAssert(mi != null, \"item handle\")\n"
        "  call MultiboardSetItemValue(mi, \"12\")\n"
        "  call MultiboardSetItemValueColor(mi, 255, 200, 0, 255)\n"
        "  call MultiboardSetItemWidth(mi, 0.05)\n"
        "  call MultiboardSetItemIcon(mi, \"ReplaceableTextures\\\\CommandButtons\\\\BTNHero.blp\")\n"
        "  call MultiboardSetItemStyle(mi, true, true)\n"
        "  call MultiboardReleaseItem(mi)\n"
        "  call MultiboardDisplay(mb, true)\n"
        "  call MultiboardMinimize(mb, true)\n"
        "  call BJassAssert(IsMultiboardMinimized(mb), \"minimized\")\n"
        "  call MultiboardMinimize(mb, false)\n"
        "  call BJassAssert(not IsMultiboardMinimized(mb), \"restored\")\n"
        "endfunction\n"));

    board = &level.multiboards[0];
    T_ASSERT(board->inuse);
    T_STREQ(board->title, "Kills");
    T_EQ(board->rows, 2);
    T_EQ(board->cols, 3);
    T_ASSERT(board->displayed_clients & 1u);
    cell = G_MultiboardCell(board, 0, 0);
    T_ASSERT(cell);
    T_ASSERT(cell->show_value);
    T_ASSERT(!cell->show_icon);
    T_FEQ(cell->width, 0.08f, 0.0001f);
    cell = G_MultiboardCell(board, 0, 1);
    T_ASSERT(cell);
    T_STREQ(cell->value, "12");
    T_ASSERT(cell->value_color_set);
    T_EQ(cell->value_color.r, 255);
    T_EQ(cell->value_color.g, 200);
    T_FEQ(cell->width, 0.05f, 0.0001f);
    T_ASSERT(cell->show_icon);
    T_ASSERT(strstr(cell->icon, "BTNHero.blp"));

    stale = G_MultiboardGetItem(board, 0, 1);
    T_ASSERT(stale);
    G_FreeMultiboard(board);
    T_ASSERT(stale->board < 0);
    T_NULL(G_MultiboardItemBoard(stale));
    G_MultiboardReleaseItem(stale);

    T_ASSERT(run_test_jass(
        "globals\n"
        "  multiboard mb = null\n"
        "  multiboarditem a = null\n"
        "  multiboarditem b = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set mb = CreateMultiboard()\n"
        "  call MultiboardSetRowCount(mb, 1)\n"
        "  call MultiboardSetColumnCount(mb, 1)\n"
        "  set a = MultiboardGetItem(mb, 0, 0)\n"
        "  set b = MultiboardGetItem(mb, 0, 0)\n"
        "  call BJassAssert(a != null and b != null and a != b, \"distinct item views\")\n"
        "  call MultiboardSetItemValue(a, \"A\")\n"
        "  call MultiboardReleaseItem(a)\n"
        "  call MultiboardReleaseItem(b)\n"
        "  call DestroyMultiboard(mb)\n"
        "  call MultiboardSetItemValue(a, \"gone\")\n"
        "endfunction\n"));
    currentplayer = saved;
}

TEST(wc3_api, multiboard_display_uses_client_slot_for_mapped_player) {
    LPMULTIBOARD board;
    setup_test_world();
    game.clients[0].ps.number = 1;
    game.clients[1].ps.number = 0;
    board = G_AllocMultiboard();
    G_SetMultiboardDisplayed(board, &game.clients[1].ps, true);
    G_SetMultiboardMinimized(board, &game.clients[1].ps, true);
    T_ASSERT(board->displayed_clients & (1u << 1));
    T_ASSERT(!(board->displayed_clients & 1u));
    T_ASSERT(board->minimized_clients & (1u << 1));
    T_ASSERT(G_IsMultiboardDisplayed(board, &game.clients[1].ps));
    T_ASSERT(!G_IsMultiboardDisplayed(board, &game.clients[0].ps));
    T_ASSERT(G_IsMultiboardMinimized(board, &game.clients[1].ps));
}

TEST(wc3_api, texttag_natives_store_unit_anchor_and_style) {
    LPPLAYER saved = currentplayer;
    LPTEXTTAG tag;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  texttag tt = null\n"
        "  unit u = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set u = CreateUnit(Player(0), 'hfoo', 100.0, 200.0, 0.0)\n"
        "  set tt = CreateTextTag()\n"
        "  call BJassAssert(tt != null, \"texttag handle\")\n"
        "  call SetTextTagText(tt, \"+25\", 0.024)\n"
        "  call SetTextTagColor(tt, 255, 220, 0, 255)\n"
        "  call SetTextTagPosUnit(tt, u, 40.0)\n"
        "  call SetTextTagVelocity(tt, 0.0, 0.03)\n"
        "  call SetTextTagVisibility(tt, true)\n"
        "  call SetTextTagPermanent(tt, false)\n"
        "  call SetTextTagLifespan(tt, 2.0)\n"
        "  call SetTextTagFadepoint(tt, 1.0)\n"
        "endfunction\n"));

    T_ASSERT(level.texttags[0].inuse);
    tag = &level.texttags[0];
    T_STREQ(tag->text, "+25");
    T_FEQ(tag->height, 0.024f, 0.0001f);
    T_EQ(tag->color.r, 255);
    T_EQ(tag->color.g, 220);
    T_EQ(tag->color.b, 0);
    T_EQ(tag->color.a, 255);
    T_ASSERT(tag->unit != NULL);
    T_FEQ(tag->height_offset, 40.0f, 0.001f);
    T_FEQ(tag->xvel, 0.0f, 0.0001f);
    T_FEQ(tag->yvel, 0.03f, 0.0001f);
    T_ASSERT(!tag->permanent);
    T_FEQ(tag->lifespan, 2.0f, 0.001f);
    T_FEQ(tag->fadepoint, 1.0f, 0.001f);
    T_ASSERT(G_IsTextTagVisible(tag, NULL));

    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  texttag tt = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set tt = CreateTextTag()\n"
        "  call SetTextTagVisibility(tt, false)\n"
        "  call DestroyTextTag(tt)\n"
        "endfunction\n"));
    T_ASSERT(!level.texttags[1].inuse);
    currentplayer = saved;
}
#endif
