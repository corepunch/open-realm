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

static RECT backdrop_edge_uv(BACKDROPCORNER c, FLOAT tile, int idx, BOOL wow) {
    if (wow) {
        switch (c) {
            case BACKDROP_LEFT_EDGE:   return (RECT){ 0.0f, 0.0f, 0.5f, tile };
            case BACKDROP_RIGHT_EDGE:  return (RECT){ 0.5f, 0.0f, 0.5f, tile };
            case BACKDROP_TOP_EDGE:    return (RECT){ 0.0f, 0.0f, tile, 0.5f };
            case BACKDROP_BOTTOM_EDGE: return (RECT){ 0.0f, 0.5f, tile, 0.5f };
            case BACKDROP_TOP_LEFT_CORNER:     return (RECT){ 0.0f, 0.0f, 0.5f, 0.5f };
            case BACKDROP_TOP_RIGHT_CORNER:    return (RECT){ 0.5f, 0.0f, 0.5f, 0.5f };
            case BACKDROP_BOTTOM_LEFT_CORNER:  return (RECT){ 0.0f, 0.5f, 0.5f, 0.5f };
            case BACKDROP_BOTTOM_RIGHT_CORNER: return (RECT){ 0.5f, 0.5f, 0.5f, 0.5f };
            default: return (RECT){ 0, 0, 1, 1 };
        }
    }
    /* WC3: 8 equal horizontal strips */
    FLOAT const k = 1.0f / NUM_BACKDROP_CORNERS;
    return (RECT){ idx * k, 0, k, tile };
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
    BOOL const wow = (db->flags & DRAW_EDGE_2X2) != 0;

    if (!db || (!db->bg.texture && !db->edge.texture)) {
        return;
    }

    backdrop_rects(&db->screen, rects, db->corner.size);

    /* --- background quad (possibly tiled) --- */
    if (db->bg.texture) {
        backSize = R_GetTextureSize(db->bg.texture);

        background = db->screen;
        background.x += db->insets.left;
        background.y += db->insets.top;
        background.w -= db->insets.left + db->insets.right;
        background.h -= db->insets.top + db->insets.bottom;

        RECT bg_uv = { 0, 0, 1, 1 };
        if ((db->flags & DRAW_TILE) && backSize.width > 0 && backSize.height > 0) {
            bg_uv.w = background.w / (backSize.width / 1000.f);
            bg_uv.h = background.h / (backSize.height / 1000.f);
            if (db->flags & DRAW_MIRRORED) {
                bg_uv.x = bg_uv.w;
                bg_uv.w = -bg_uv.w;
            }
        }

        num_vertices = 0;
        R_AddQuad(vertices + num_vertices, &background, &bg_uv, db->bg.color, 0);
        num_vertices += 6;

        R_DrawImageBatch(db->bg.texture, SHADER_UI, BLEND_MODE_BLEND,
                         0, false, NULL, vertices, num_vertices,
                         (db->flags & DRAW_TILE) && (bg_uv.w > 1 || bg_uv.h > 1));
    }

    /* --- edge/corner quads (batched into one drawcall) --- */
    if (db->edge.texture) {
        edgeSize = R_GetTextureSize(db->edge.texture);
        BOOL edge_repeat = false;

        num_vertices = 0;
        FOR_LOOP(i, NUM_BACKDROP_CORNERS) {
            BACKDROPCORNER const c = corners[i];
            if ((db->corner.flags & (1 << c)) == 0)
                continue;

            FLOAT tile;
            if (wow) {
                /* WoW 2×2 quadrant: edges tile by their strip dimension */
                FLOAT const half_h = edgeSize.height * 0.5f / 1000.f;
                FLOAT const full_w = edgeSize.width / 1000.f;
                if (c == BACKDROP_LEFT_EDGE || c == BACKDROP_RIGHT_EDGE)
                    tile = half_h > 0 ? ceilf(rects[c].h / half_h) : 1;
                else if (c == BACKDROP_TOP_EDGE || c == BACKDROP_BOTTOM_EDGE)
                    tile = full_w > 0 ? ceilf(rects[c].w / full_w) : 1;
                else
                    tile = 1;
            } else {
                FLOAT const h = edgeSize.height / 1000.f;
                tile = backdrop_edge_tile(rects + c, c, h);
            }

            RECT const uv = backdrop_edge_uv(c, tile, i, wow);
            BOOL const flip = !wow && backdrop_edge_flip(c);

            if (tile > 1.0f) edge_repeat = true;
            R_AddQuad(vertices + num_vertices, rects + c, &uv, db->edge.color, 0);
            if (flip) {
                VECTOR2 tmp = vertices[num_vertices + 1].texcoord;
                vertices[num_vertices + 1].texcoord = vertices[num_vertices + 5].texcoord;
                vertices[num_vertices + 5].texcoord = tmp;
            }
            num_vertices += 6;
        }

        if (num_vertices > 0) {
            R_DrawImageBatch(db->edge.texture, SHADER_UI, BLEND_MODE_BLEND,
                             0, false, NULL, vertices, num_vertices, edge_repeat);
        }
    }
}
