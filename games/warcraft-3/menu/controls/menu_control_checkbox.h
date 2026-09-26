#ifndef UI_CONTROL_CHECKBOX_H
#define UI_CONTROL_CHECKBOX_H

static bool UI_CheckBoxEnabled(LPCFRAMEDEF frame) {
    return frame && !(frame->ui_flags & UIFLAG_DISABLED);
}

static bool UI_CheckBoxIsPushed(LPCFRAMEDEF frame, rect_t const * rect) {
    (void)rect;
    return UI_CheckBoxEnabled(frame) &&
           !UI_PointerBlockedByPopup(frame) &&
           (frame->ui_flags & UIFLAG_PRESSED);
}

static LPCFRAMEDEF UI_CheckBoxBackdrop(LPCFRAMEDEF frame, rect_t const * rect) {
    cstring_t backdrop_name;

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
    cstring_t highlight_name;

    if (!frame || !frame->CheckBox.Checked) {
        return NULL;
    }
    highlight_name = UI_CheckBoxEnabled(frame)
                     ? frame->CheckBox.CheckHighlight
                     : frame->CheckBox.DisabledCheckHighlight;
    return UI_FindFrameNear(frame, highlight_name);
}

static void UI_DrawCheckBoxMouseOverHighlight(LPCFRAMEDEF frame) {
    rect_t const * rect;

    if (!frame || !UI_CheckBoxEnabled(frame) || UI_PointerBlockedByPopup(frame)) {
        return;
    }
    rect = UI_LayoutRect(frame);
    if (!rect || !(frame->ui_flags & UIFLAG_HOVERED)) {
        return;
    }
    UI_DrawHighlightFrame(UI_ButtonMouseOverHighlight(frame), rect);
}

#endif
