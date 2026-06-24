#ifndef UI_CONTROL_CHECKBOX_H
#define UI_CONTROL_CHECKBOX_H

static BOOL UI_CheckBoxEnabled(LPCFRAMEDEF frame) {
    return frame && !frame->disabled;
}

static BOOL UI_CheckBoxIsPushed(LPCFRAMEDEF frame, LPCRECT rect) {
    return UI_CheckBoxEnabled(frame) &&
           !UI_PointerBlockedByPopup(frame) &&
           UI_MouseContains(rect) &&
           ui_mouse.button == 1 &&
           ui_mouse.down;
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

static void UI_DrawCheckBox(LPCFRAMEDEF frame, LPCRECT rect) {
    LPCFRAMEDEF backdrop;
    LPCFRAMEDEF check_highlight;

    if (!frame || !rect) {
        return;
    }

    if (!UI_PointerBlockedByPopup(frame) &&
        UI_MouseContains(rect) &&
        ui_mouse.event == UI_WC3_MOUSE_LEFT_UP) {
        ((LPFRAMEDEF)frame)->CheckBox.Checked = !frame->CheckBox.Checked;
        if (frame->OnClick[0]) {
            UI_MenuCommandLocal(frame->OnClick);
        }
    }

    backdrop = UI_CheckBoxBackdrop(frame, rect);
    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    UI_DrawTexture(frame, rect);

    check_highlight = UI_CheckBoxCheckHighlight(frame);
    UI_DrawHighlightFrame(check_highlight, rect);
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
