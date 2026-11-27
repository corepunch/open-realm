#include "test.h"
#include "game/g_exploration.h"
#include <string.h>

TEST(wow_exploration, any_authored_subzone_can_reveal_shared_art) {
    wowExploration_t player = {0}, other = {0};
    wowMapOverlay_t overlay = {.areas = {10, 0, 65535, 0}};
    T_ASSERT(!Wow_MapOverlayDiscovered(&player, &overlay));
    T_ASSERT(!Wow_ExploreArea(&player, 0));
    T_ASSERT(!Wow_ExploreArea(&player, 65536));
    T_ASSERT(Wow_ExploreArea(&player, 15));
    T_ASSERT(!Wow_MapOverlayDiscovered(&player, &overlay));
    T_ASSERT(Wow_ExploreArea(&player, 65535));
    T_ASSERT(Wow_MapOverlayDiscovered(&player, &overlay));
    T_ASSERT(!Wow_MapOverlayDiscovered(&other, &overlay));
    T_ASSERT(!Wow_ExploreArea(&player, 65535));
}

TEST(wow_exploration, edge_pieces_use_the_padding_of_their_own_texture) {
    wowMapOverlay_t overlay = {.width = 315, .height = 321, .left = 10, .top = 30};
    wowMapPiece_t tile;
    T_ASSERT(Wow_MapOverlayPiece(&overlay, 0, &tile));
    T_EQ(tile.sequence, 1);
    T_EQ(tile.width, 256);
    T_FEQ(tile.u1, 1, 0.00001);
    T_ASSERT(Wow_MapOverlayPiece(&overlay, 3, &tile));
    T_EQ(tile.sequence, 4);
    T_EQ(tile.x, 266);
    T_EQ(tile.y, 286);
    T_EQ(tile.width, 59);
    T_EQ(tile.height, 65);
    T_FEQ(tile.u1, 59.0/64, 0.00001);
    T_FEQ(tile.v1, 65.0/128, 0.00001);
    T_ASSERT(!Wow_MapOverlayPiece(&overlay, 4, &tile));
    overlay.tile_width[3] = 256;
    T_ASSERT(Wow_MapOverlayPiece(&overlay, 3, &tile));
    T_FEQ(tile.u1, 59.0/256, 0.00001);
    overlay.tile_width[3] = 16;
    T_ASSERT(!Wow_MapOverlayPiece(&overlay, 3, &tile));
}

TEST(wow_exploration, crop_preserves_texels_when_an_overlay_crosses_the_map_edge) {
    wowMapOverlay_t overlay = {.width = 64, .height = 32, .left = -8, .top = 655};
    wowMapPiece_t tile;
    T_ASSERT(Wow_MapOverlayPiece(&overlay, 0, &tile));
    T_EQ(tile.x, 0);
    T_EQ(tile.width, 56);
    T_EQ(tile.height, 13);
    T_FEQ(tile.u0, 8.0/64, 0.00001);
    T_FEQ(tile.v1, 13.0/32, 0.00001);
    overlay.left = 1003;
    T_ASSERT(!Wow_MapOverlayPiece(&overlay, 0, &tile));
}

TEST(wow_exploration, dbc_rejects_unterminated_paths_and_invalid_dimensions) {
    uint32_t data[5 + 17 + 2] = {0};
    memcpy(data, "WDBC", 4);
    data[1] = 1;
    data[2] = 17;
    data[3] = 68;
    data[4] = 8;
    uint32_t *row = data + 5;
    char *strings = (char *)(row + 17);
    row[1] = 42;
    row[2] = 121;
    row[8] = 1;
    row[9] = 128;
    row[10] = 26;
    memcpy(strings + 1, "Zone", 5);
    wowMapOverlay_t overlay;
    T_ASSERT(Wow_MapOverlayRead((uint8_t *)data, sizeof(data), 0, &overlay));
    T_STREQ(overlay.texture, "Zone");
    T_EQ(overlay.map_area, 42);
    T_EQ(overlay.areas[0], 121);
    T_EQ(Wow_MapOverlayRows((uint8_t *)data, sizeof(data)), 1);
    T_ASSERT(!Wow_MapOverlayRead((uint8_t *)data, sizeof(data), 1, &overlay));
    row[9] = 0;
    T_ASSERT(!Wow_MapOverlayRead((uint8_t *)data, sizeof(data), 0, &overlay));
    row[9] = 100;
    strings[1] = '/';
    T_ASSERT(!Wow_MapOverlayRead((uint8_t *)data, sizeof(data), 0, &overlay));
    memset(strings + 1, 'A', 7);
    T_ASSERT(!Wow_MapOverlayRead((uint8_t *)data, sizeof(data), 0, &overlay));
    data[1] = UINT32_MAX;
    T_EQ(Wow_MapOverlayRows((uint8_t *)data, sizeof(data)), 0);
}
