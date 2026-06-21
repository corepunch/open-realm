#ifndef UI_CONTROL_BACKDROP_H
#define UI_CONTROL_BACKDROP_H

typedef enum {
    BACKDROPINSET_RIGHT,
    BACKDROPINSET_TOP,
    BACKDROPINSET_BOTTOM,
    BACKDROPINSET_LEFT,
} BACKDROPINSET;

typedef enum {
    UI_BACKDROP_WARN_NO_ART,
    UI_BACKDROP_WARN_BACKGROUND_TEXTURE,
    UI_BACKDROP_WARN_EDGE_TEXTURE,
    UI_BACKDROP_WARN_ZERO_CORNER_SIZE,
    UI_BACKDROP_WARN_COUNT,
} uiBackdropWarn_t;

static BOOL UI_BackdropHasArt(LPCFRAMEDEF frame) {
    return frame && (frame->Backdrop.Background || frame->Backdrop.EdgeFile);
}

static void UI_BackdropWarnOnce(LPCFRAMEDEF frame, uiBackdropWarn_t warn, LPCSTR detail) {
    static BOOL warned[UI_BACKDROP_WARN_COUNT][MAX_UI_CLASSES];
    DWORD index;

    if (!uiimport.Printf || !UI_FrameIndex(frame, &index) || warn >= UI_BACKDROP_WARN_COUNT || warned[warn][index]) return;
    warned[warn][index] = true;
    uiimport.Printf("ERROR: UI backdrop '%s' %s\n", frame->Name[0] ? frame->Name : "<unnamed>", detail);
}

static void UI_BackdropRects(LPCRECT screen, LPRECT rects, FLOAT corner_size) {
    FLOAT x[] = { 0, corner_size, screen->w - corner_size, screen->w };
    FLOAT y[] = { 0, corner_size, screen->h - corner_size, screen->h };
    FOR_LOOP(i, BACKDROP_SIZE) {
        rects[i].x = screen->x + x[i % 3];
        rects[i].y = screen->y + y[i / 3];
        rects[i].w = x[(i % 3) + 1] - x[i % 3];
        rects[i].h = y[(i / 3) + 1] - y[i / 3];
    }
}

static FLOAT UI_BackdropEdgeTile(LPCRECT rect, BACKDROPCORNER edge, FLOAT image_size) {
    if (image_size <= 0) {
        return 1;
    }
    switch (edge) {
        case BACKDROP_LEFT_EDGE:
        case BACKDROP_RIGHT_EDGE:
            return ceilf(rect->h / image_size);
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return ceilf(rect->w / image_size);
        default:
            return 1;
    }
}

static BOOL UI_BackdropEdgeFlip(BACKDROPCORNER edge) {
    switch (edge) {
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return true;
        default:
            return false;
    }
}

static void UI_DrawBackdropWithColor(LPCFRAMEDEF frame, LPCRECT rect, COLOR32 color) {
    LPRENDERER renderer = UI_GetRenderer();
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
    RECT rects[BACKDROP_SIZE];

    if (!UI_BackdropHasArt(frame)) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_NO_ART, "has no background or edge texture");
        return;
    }

    if (!renderer || !renderer->DrawImageEx) {
        return;
    }

    if (frame->Backdrop.Background) {
        LPCTEXTURE tex = UI_GetTexture(frame->Backdrop.Background);
        if (tex) {
            size2_t back_size = renderer->GetTextureSize ? renderer->GetTextureSize(tex) : MAKE(size2_t, 0, 0);
            RECT uv = { frame->Backdrop.Mirrored ? 1 : 0, 0, frame->Backdrop.Mirrored ? -1 : 1, 1 };
            RECT background = *rect;
            background.x += frame->Backdrop.BackgroundInsets[BACKDROPINSET_LEFT];
            background.y += frame->Backdrop.BackgroundInsets[BACKDROPINSET_TOP];
            background.w -= frame->Backdrop.BackgroundInsets[BACKDROPINSET_LEFT];
            background.w -= frame->Backdrop.BackgroundInsets[BACKDROPINSET_RIGHT];
            background.h -= frame->Backdrop.BackgroundInsets[BACKDROPINSET_TOP];
            background.h -= frame->Backdrop.BackgroundInsets[BACKDROPINSET_BOTTOM];
            if (frame->Backdrop.TileBackground && back_size.width > 0 && back_size.height > 0) {
                uv.w = background.w / (back_size.width / 1000.0f);
                if (frame->Backdrop.Mirrored) {
                    uv.x = uv.w;
                    uv.w = -uv.w;
                }
                uv.h = background.h / (back_size.height / 1000.0f);
            }
            renderer->DrawImageEx(&MAKE(drawImage_t,
                                        .texture = tex,
                                        .shader = SHADER_UI,
                                        .alphamode = BLEND_MODE_BLEND,
                                        .screen = background,
                                        .uv = uv,
                                        .color = color,
                                        .rotate = FALSE));
        } else {
            UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_BACKGROUND_TEXTURE, "background texture is missing");
        }
    }

    if (!frame->Backdrop.EdgeFile || !frame->Backdrop.CornerFlags) {
        return;
    }
    if (frame->Backdrop.CornerSize <= 0.0f) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_ZERO_CORNER_SIZE, "has edge art but BackdropCornerSize is zero");
    }

    LPCTEXTURE edge_tex = UI_GetTexture(frame->Backdrop.EdgeFile);
    if (!edge_tex) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_EDGE_TEXTURE, "edge texture is missing");
        return;
    }

    size2_t edge_size = renderer->GetTextureSize ? renderer->GetTextureSize(edge_tex) : MAKE(size2_t, 0, 0);
    FLOAT const edge_image_height = edge_size.height / 1000.0f;
    UI_BackdropRects(rect, rects, frame->Backdrop.CornerSize);
    FOR_LOOP(i, NUM_BACKDROP_CORNERS) {
        if ((frame->Backdrop.CornerFlags & (1 << corners[i])) == 0) {
            continue;
        }
        FLOAT const k = 1.0f / NUM_BACKDROP_CORNERS;
        FLOAT const tile = UI_BackdropEdgeTile(rects + corners[i], corners[i], edge_image_height);
        BOOL const flip = UI_BackdropEdgeFlip(corners[i]);
        RECT const uv = { i * k, 0, k, tile };
        renderer->DrawImageEx(&MAKE(drawImage_t,
                                    .texture = edge_tex,
                                    .shader = SHADER_UI,
                                    .alphamode = BLEND_MODE_BLEND,
                                    .screen = rects[corners[i]],
                                    .uv = uv,
                                    .color = color,
                                    .rotate = flip));
    }
}

static void UI_DrawBackdrop(LPCFRAMEDEF frame, LPCRECT rect) {
    UI_DrawBackdropWithColor(frame, rect, frame->Color);
}

#endif
