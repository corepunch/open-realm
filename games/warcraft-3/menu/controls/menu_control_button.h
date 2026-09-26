#ifndef UI_CONTROL_BUTTON_H
#define UI_CONTROL_BUTTON_H

static bool UI_ButtonIsPopupArrow(frameDef_t const *frame) {
    frameDef_t const *parent = frame ? frame->Parent : NULL;

    return frame &&
           parent &&
           UI_IsPopupFrameType(parent->Type) &&
           parent->Popup.ArrowFrame[0] &&
           !strcmp(frame->Name, parent->Popup.ArrowFrame);
}

static bool UI_ButtonBackdropNameContains(frameDef_t const *frame, cstring_t text) {
    return frame &&
           frame->Control.Backdrop.Normal[0] &&
           text &&
           strstr(frame->Control.Backdrop.Normal, text);
}

static bool UI_ButtonBackdropTextureContains(frameDef_t const *frame, cstring_t text) {
    frameDef_t const *backdrop;
    cstring_t background;
    cstring_t edge;

    if (!frame || !frame->Control.Backdrop.Normal[0] || !text) {
        return false;
    }
    backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
    if (!backdrop) {
        return false;
    }
    background = UI_TextureName(backdrop->Backdrop.Background);
    edge = UI_TextureName(backdrop->Backdrop.EdgeFile);
    return (background && strstr(background, text)) ||
           (edge && strstr(edge, text));
}

static cstring_t UI_ButtonPopupPushedBackdropName(frameDef_t const *frame) {
    if (!frame || !UI_IsPopupFrameType(frame->Type)) {
        return NULL;
    }
    if (UI_ButtonBackdropNameContains(frame, "Campaign")) {
        return "StandardCampaignButtonPushedBackdropTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "BattleNet")) {
        return "BattleNetButtonPushedBackdropTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "EscMenu")) {
        return "EscMenuButtonPushedBackdropTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "Standard")) {
        return "StandardButtonPushedBackdropTemplate";
    }
    return NULL;
}

static cstring_t UI_ButtonFallbackMouseOverHighlightName(frameDef_t const *frame) {
    if (!frame) {
        return NULL;
    }
    if (UI_ButtonBackdropNameContains(frame, "Campaign")) {
        return "StandardCampaignButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "BattleNet")) {
        return "BattleNetButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "EscMenu")) {
        return "EscMenuButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropNameContains(frame, "Standard")) {
        return "StandardButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropTextureContains(frame, "CampaignButton")) {
        return "StandardCampaignButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropTextureContains(frame, "BorderedBackdropBorder")) {
        return "StandardBorderedButtonMouseOverHighlightTemplate";
    }
    if (UI_ButtonBackdropTextureContains(frame, "GlueScreen-Button1-")) {
        return "StandardButtonMouseOverHighlightTemplate";
    }
    return NULL;
}

static vec2_t UI_ButtonPushedTextOffset(frameDef_t const *frame) {
    if (frame &&
        (frame->Button.PushedTextOffset.x != 0.0f ||
         frame->Button.PushedTextOffset.y != 0.0f)) {
        return frame->Button.PushedTextOffset;
    }
    if (frame && UI_IsPopupFrameType(frame->Type)) {
        if (UI_ButtonBackdropNameContains(frame, "BattleNet")) {
            return MAKE(vec2_t, -0.002f, -0.003f);
        }
        if (UI_ButtonBackdropNameContains(frame, "EscMenu")) {
            return MAKE(vec2_t, 0.002f, -0.002f);
        }
        return MAKE(vec2_t, -0.0015f, -0.0015f);
    }
    return MAKE(vec2_t, 0.0f, 0.0f);
}

static bool UI_ButtonEnabled(frameDef_t const *frame) {
    return frame &&
           !(frame->ui_flags & UIFLAG_DISABLED) &&
           (frame->OnClick[0] ||
            frame->Type == FT_POPUPMENU ||
            frame->Type == FT_GLUEPOPUPMENU ||
            UI_ButtonIsPopupArrow(frame));
}

static bool UI_ButtonIsPushed(frameDef_t const *frame, rect_t const *rect) {
    (void)rect;
    return UI_ButtonEnabled(frame) && (frame->ui_flags & UIFLAG_PRESSED);
}

static void UI_DrawButtonText(frameDef_t const *frame, rect_t const *rect) {
    frameDef_t *text_frame = NULL;
    rect_t text_rect = *rect;
    color32_t original_color;
    bool use_disabled_color;

    if (frame->Text && *frame->Text) {
        text_frame = UI_FindChildFrame((frameDef_t *)frame, frame->Text);
    }
    if (!text_frame && frame->Button.NormalText.frame[0]) {
        text_frame = UI_FindChildFrame((frameDef_t *)frame, frame->Button.NormalText.frame);
    }
    if (!text_frame && frame->Button.NormalText.frame[0]) {
        text_frame = UI_FindFrameNear(frame, frame->Button.NormalText.frame);
        if (text_frame && frame->Button.NormalText.text[0]) {
            UI_SetText((frameDef_t *)text_frame, "%s", frame->Button.NormalText.text);
        }
    }
    if (!text_frame) {
        return;
    }

    if (UI_ButtonIsPushed(frame, rect)) {
        vec2_t pushed_offset = UI_ButtonPushedTextOffset(frame);
        text_rect.x += pushed_offset.x;
        text_rect.y -= pushed_offset.y;
    }

    original_color = text_frame->Font.Color;
    use_disabled_color = !UI_ButtonEnabled(frame) &&
                         (text_frame->Font.DisabledColor.a ||
                          text_frame->Font.DisabledColor.r ||
                          text_frame->Font.DisabledColor.g ||
                          text_frame->Font.DisabledColor.b);
    if (use_disabled_color) {
        text_frame->Font.Color = text_frame->Font.DisabledColor;
    }
    UI_DrawText(text_frame, &text_rect);
    text_frame->Font.Color = original_color;
}

static frameDef_t const *UI_ButtonBackdrop(frameDef_t const *frame, rect_t const *rect) {
    cstring_t backdrop_name = frame->Control.Backdrop.Normal;
    bool const pushed = UI_ButtonIsPushed(frame, rect);

    if (!UI_ButtonEnabled(frame)) {
        backdrop_name = pushed && frame->Control.Backdrop.DisabledPushed[0]
                        ? frame->Control.Backdrop.DisabledPushed
                        : frame->Control.Backdrop.Disabled;
    } else if (pushed) {
        backdrop_name = frame->Control.Backdrop.Pushed[0]
                        ? frame->Control.Backdrop.Pushed
                        : UI_ButtonPopupPushedBackdropName(frame);
    }

    frameDef_t const *backdrop = UI_FindFrameNear(frame, backdrop_name);
    if (!backdrop && frame->Button.NormalTexture[0]) {
        backdrop = UI_FindFrameNear(frame, frame->Button.NormalTexture);
    }
    return backdrop;
}

static frameDef_t const *UI_ButtonMouseOverHighlight(frameDef_t const *frame) {
    cstring_t highlight_name;

    if (!frame) {
        return NULL;
    }
    highlight_name = frame->Control.Backdrop.MouseOver[0]
                     ? frame->Control.Backdrop.MouseOver
                     : UI_ButtonFallbackMouseOverHighlightName(frame);
    return UI_FindFrameNear(frame, highlight_name);
}

#endif
