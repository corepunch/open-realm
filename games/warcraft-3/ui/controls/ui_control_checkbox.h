#ifndef UI_CONTROL_CHECKBOX_H
#define UI_CONTROL_CHECKBOX_H

static BOOL UI_CheckBoxEnabled(LPCFRAMEDEF frame) {
    return frame && !frame->base.disabled;
}

static BOOL UI_CheckBoxIsPushed(LPCFRAMEDEF frame, LPCRECT rect) {
    return UI_CheckBoxEnabled(frame) &&
           !UI_PointerBlockedByPopup(frame) &&
           UI_MouseContains(rect) &&
           (frame->base.ui_flags & UIFLAG_PRESSED);
}

static LPCFRAMEDEF UI_CheckBoxBackdrop(LPCFRAMEDEF frame, LPCRECT rect) {
    LPCSTR backdrop_name;

    if (!frame) {
        return NULL;
    }
    if (!UI_CheckBoxEnabled(frame)) {
        backdrop_name = frame->Control.Backdrop.Disabled;
    } else if (UI_CheckBoxIsPushed(frame, rect) && frame->Control.Backdrop.Pushed[0]) {
        backdrop_name = frame->Control.Backdrop.Pushed;
    } else {
        backdrop_name = frame->Control.Backdrop.Normal;
    }
    return UI_FindFrameNear(frame, backdrop_name);
}

static LPCFRAMEDEF UI_CheckBoxCheckHighlight(LPCFRAMEDEF frame) {
    LPCSTR highlight_name;

    if (!frame || !frame->CheckBox.Checked) {
        return NULL;
    }
    highlight_name = UI_CheckBoxEnabled(frame)
                     ? frame->CheckBox.CheckHighlight
                     : frame->CheckBox.DisabledCheckHighlight;
    return UI_FindFrameNear(frame, highlight_name);
}

static void UI_DrawCheckBoxMouseOverHighlight(LPCFRAMEDEF frame) {
    LPCRECT rect;

    if (!frame || !UI_CheckBoxEnabled(frame) || UI_PointerBlockedByPopup(frame)) {
        return;
    }
    rect = UI_LayoutRect(frame);
    if (!rect || !UI_MouseContains(rect)) {
        return;
    }
    UI_DrawHighlightFrame(UI_ButtonMouseOverHighlight(frame), rect);
}

#endif
