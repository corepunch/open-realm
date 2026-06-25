/*
 * r_backdrop.c — Single-mesh backdrop renderer.
 *
 * Builds all 9-slice quads (background + up to 8 edge/corner pieces) into
 * vertex arrays and draws them in at most 2 drawcalls (background texture,
 * edge texture), replacing per-piece DrawImageEx calls in the client layout
 * renderer and WoW XML UI.
 */

#include "r_local.h"
#include "r_game.h"

#define NUM_BACKDROP_CORNERS 8

static void backdrop_rects(LPCRECT screen, LPRECT rects, FLOAT corner_size) {
    FLOAT x[] = { 0, corner_size, screen->w - corner_size, screen->w };
    FLOAT y[] = { 0, corner_size, screen->h - corner_size, screen->h };
    FOR_LOOP(i, BACKDROP_SIZE) {
        rects[i].x = screen->x + x[i % 3];
        rects[i].y = screen->y + y[i / 3];
        rects[i].w = x[(i % 3) + 1] - x[i % 3];
        rects[i].h = y[(i / 3) + 1] - y[i / 3];
    }
}

static FLOAT backdrop_edge_tile(LPCRECT rect, BACKDROPCORNER edge, FLOAT imagesize) {
    switch (edge) {
        case BACKDROP_LEFT_EDGE:
        case BACKDROP_RIGHT_EDGE:
            return imagesize > 0 ? ceilf(rect->h / imagesize) : 1;
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return imagesize > 0 ? ceilf(rect->w / imagesize) : 1;
        default:
            return 1;
    }
}

static BOOL backdrop_edge_flip(BACKDROPCORNER edge) {
    return edge == BACKDROP_TOP_EDGE || edge == BACKDROP_BOTTOM_EDGE;
}

void R_DrawBackdrop(LPCDRAWBACKDROP db) {
    RECT rects[BACKDROP_SIZE];
    RECT background;
    VERTEX vertices[(1 + NUM_BACKDROP_CORNERS) * 6];
    DWORD num_vertices;
    BACKDROPCORNER const corners[NUM_BACKDROP_CORNERS] = {
        BACKDROP_LEFT_EDGE,
        BACKDROP_RIGHT_EDGE,
        BACKDROP_TOP_EDGE,
        BACKDROP_BOTTOM_EDGE,
        BACKDROP_TOP_LEFT_CORNER,
        BACKDROP_TOP_RIGHT_CORNER,
        BACKDROP_BOTTOM_LEFT_CORNER,
        BACKDROP_BOTTOM_RIGHT_CORNER,
    };
    size2_t backSize, edgeSize;

    if (!db || (!db->bg_texture && !db->edge_texture)) {
        return;
    }

    backdrop_rects(&db->screen, rects, db->corner_size);

    /* --- background quad (possibly tiled) --- */
    if (db->bg_texture) {
        backSize = R_GetTextureSize(db->bg_texture);

        background = db->screen;
        background.x += db->bg_insets[3]; /* left */
        background.y += db->bg_insets[1]; /* top */
        background.w -= db->bg_insets[3] + db->bg_insets[0]; /* left + right */
        background.h -= db->bg_insets[1] + db->bg_insets[2]; /* top + bottom */

        RECT bg_uv = { 0, 0, 1, 1 };
        if (db->tile_bg && backSize.width > 0 && backSize.height > 0) {
            bg_uv.w = background.w / (backSize.width / 1000.f);
            bg_uv.h = background.h / (backSize.height / 1000.f);
            if (db->mirrored) {
                bg_uv.x = bg_uv.w;
                bg_uv.w = -bg_uv.w;
            }
        }

        num_vertices = 0;
        R_AddQuad(vertices + num_vertices, &background, &bg_uv, db->bg_color, 0);
        num_vertices += 6;

        R_DrawImageBatch(db->bg_texture, SHADER_UI, BLEND_MODE_BLEND,
                         0, false, NULL, vertices, num_vertices,
                         db->tile_bg && (bg_uv.w > 1 || bg_uv.h > 1));
    }

    /* --- edge/corner quads (batched into one drawcall) --- */
    if (db->edge_texture) {
        edgeSize = R_GetTextureSize(db->edge_texture);

        num_vertices = 0;
        FOR_LOOP(i, NUM_BACKDROP_CORNERS) {
            FLOAT const k = 1.0f / NUM_BACKDROP_CORNERS;
            FLOAT const h = edgeSize.height / 1000.f;
            FLOAT const tile = backdrop_edge_tile(rects + corners[i], corners[i], h);
            BOOL const flip = backdrop_edge_flip(corners[i]);
            RECT const uv = { i * k, 0, k, tile };

            if ((db->corner_flags & (1 << corners[i])) == 0) {
                continue;
            }
            R_AddQuad(vertices + num_vertices, rects + corners[i], &uv, db->edge_color, 0);
            if (flip) {
                VECTOR2 tmp = vertices[num_vertices + 0].texcoord;
                vertices[num_vertices + 0].texcoord = vertices[num_vertices + 2].texcoord;
                vertices[num_vertices + 2].texcoord = tmp;
                vertices[num_vertices + 3].texcoord = vertices[num_vertices + 0].texcoord;
                vertices[num_vertices + 4].texcoord = vertices[num_vertices + 2].texcoord;
                vertices[num_vertices + 5].texcoord = vertices[num_vertices + 2].texcoord;
            }
            num_vertices += 6;
        }

        if (num_vertices > 0) {
            R_DrawImageBatch(db->edge_texture, SHADER_UI, BLEND_MODE_BLEND,
                             0, false, NULL, vertices, num_vertices, false);
        }
    }
}
