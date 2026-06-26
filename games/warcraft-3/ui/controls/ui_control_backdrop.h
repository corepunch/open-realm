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

static void UI_DrawBackdropWithColor(LPCFRAMEDEF frame, LPCRECT rect, COLOR32 color) {
    LPRENDERER renderer = uiimport.GetRenderer();

    if (!UI_BackdropHasArt(frame)) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_NO_ART, "has no background or edge texture");
        return;
    }
    if (!renderer || !renderer->DrawBackdrop) {
        return;
    }
    if (frame->Backdrop.Background && !UI_GetTexture(frame->Backdrop.Background)) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_BACKGROUND_TEXTURE, "background texture is missing");
    }
    if (frame->Backdrop.EdgeFile && !UI_GetTexture(frame->Backdrop.EdgeFile)) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_EDGE_TEXTURE, "edge texture is missing");
    }
    if (frame->Backdrop.EdgeFile && frame->Backdrop.CornerFlags && frame->Backdrop.CornerSize <= 0.0f) {
        UI_BackdropWarnOnce(frame, UI_BACKDROP_WARN_ZERO_CORNER_SIZE, "has edge art but BackdropCornerSize is zero");
    }
    renderer->DrawBackdrop(&MAKE(drawBackdrop_t,
                                 .screen = *rect,
                                 .bg_texture = UI_GetTexture(frame->Backdrop.Background),
                                 .edge_texture = UI_GetTexture(frame->Backdrop.EdgeFile),
                                 .bg_color = color,
                                 .edge_color = color,
                                 .corner_flags = frame->Backdrop.CornerFlags,
                                 .corner_size = frame->Backdrop.CornerSize,
                                 .bg_insets = { frame->Backdrop.BackgroundInsets[BACKDROPINSET_RIGHT],
                                                frame->Backdrop.BackgroundInsets[BACKDROPINSET_TOP],
                                                frame->Backdrop.BackgroundInsets[BACKDROPINSET_BOTTOM],
                                                frame->Backdrop.BackgroundInsets[BACKDROPINSET_LEFT] },
                                 .tile_bg = frame->Backdrop.TileBackground,
                                 .mirrored = frame->Backdrop.Mirrored));
}

static void UI_DrawBackdrop(LPCFRAMEDEF frame, LPCRECT rect) {
    UI_DrawBackdropWithColor(frame, rect, frame->Color);
}

#endif
