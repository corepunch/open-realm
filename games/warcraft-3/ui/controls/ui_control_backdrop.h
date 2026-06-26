#ifndef UI_CONTROL_BACKDROP_H
#define UI_CONTROL_BACKDROP_H

typedef enum {
    BACKDROPINSET_RIGHT,
    BACKDROPINSET_TOP,
    BACKDROPINSET_BOTTOM,
    BACKDROPINSET_LEFT,
} BACKDROPINSET;

static BOOL UI_BackdropHasArt(LPCFRAMEDEF frame) {
    return frame && (frame->Backdrop.Background || frame->Backdrop.EdgeFile);
}

static void UI_DrawBackdropWithColor(LPCFRAMEDEF frame, LPCRECT rect, COLOR32 color) {
    LPRENDERER renderer = uiimport.GetRenderer();

    if (!UI_BackdropHasArt(frame) || !renderer || !renderer->DrawBackdrop) {
        return;
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
