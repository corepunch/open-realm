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
    if (frame->Backdrop.EdgeFile && frame->Backdrop.CornerSize <= 0.0f) {
        uiimport.Printf("BackdropCornerSize is zero for frame '%s'\n",
                        frame->Name[0] ? frame->Name : "(unnamed)");
    }
    renderer->DrawBackdrop(&MAKE(drawBackdrop_t,
                                 .screen = *rect,
                                 .bg.texture = UI_GetTexture(frame->Backdrop.Background),
                                 .bg.color = color,
                                 .edge.texture = UI_GetTexture(frame->Backdrop.EdgeFile),
                                 .edge.color = color,
                                 .corner.flags = frame->Backdrop.CornerFlags,
                                 .corner.size = frame->Backdrop.CornerSize,
                                 .insets.right = frame->Backdrop.BackgroundInsets[BACKDROPINSET_RIGHT],
                                 .insets.top = frame->Backdrop.BackgroundInsets[BACKDROPINSET_TOP],
                                 .insets.bottom = frame->Backdrop.BackgroundInsets[BACKDROPINSET_BOTTOM],
                                 .insets.left = frame->Backdrop.BackgroundInsets[BACKDROPINSET_LEFT],
                                 .flags = (frame->Backdrop.TileBackground ? DRAW_TILE : 0)
                                        | (frame->Backdrop.Mirrored ? DRAW_MIRRORED : 0)));
}

static void UI_DrawBackdrop(LPCFRAMEDEF frame, LPCRECT rect) {
    UI_DrawBackdropWithColor(frame, rect, frame->Color);
}

#endif
