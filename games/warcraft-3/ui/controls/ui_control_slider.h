#ifndef UI_CONTROL_SLIDER_H
#define UI_CONTROL_SLIDER_H

static FLOAT UI_SliderFraction(LPCFRAMEDEF frame) {
    FLOAT min_value = frame->Slider.MinValue;
    FLOAT max_value = frame->Slider.MaxValue;
    FLOAT value = frame->Slider.InitialValue;

    if (max_value <= min_value) {
        return 0.0f;
    }
    value = MAX(min_value, MIN(max_value, value));
    return (value - min_value) / (max_value - min_value);
}

static RECT UI_SliderThumbRect(LPCFRAMEDEF slider, LPCRECT slider_rect, LPCFRAMEDEF thumb) {
    FLOAT const fraction = UI_SliderFraction(slider);
    FLOAT const thumb_w = thumb && thumb->base.size.width > 0 ? thumb->base.size.width : slider_rect->h;
    FLOAT const thumb_h = thumb && thumb->base.size.height > 0 ? thumb->base.size.height : slider_rect->h;
    FLOAT const travel_w = MAX(0.0f, slider_rect->w - thumb_w);
    FLOAT const travel_h = MAX(0.0f, slider_rect->h - thumb_h);
    RECT rect = {
        .x = slider_rect->x + (slider_rect->w - thumb_w) * 0.5f,
        .y = slider_rect->y + (slider_rect->h - thumb_h) * 0.5f,
        .w = thumb_w,
        .h = thumb_h,
    };

    if (slider->Slider.Layout == LAYOUT_VERTICAL) {
        rect.y = slider_rect->y + travel_h * (1.0f - fraction);
    } else {
        rect.x = slider_rect->x + travel_w * fraction;
    }
    return rect;
}

static FLOAT UI_SliderValueFromMousePos(LPCFRAMEDEF slider, LPCRECT slider_rect, LPCFRAMEDEF thumb, VECTOR2 mouse) {
    FLOAT const min_value = slider->Slider.MinValue;
    FLOAT const max_value = slider->Slider.MaxValue;
    FLOAT value_range = max_value - min_value;
    FLOAT fraction;
    FLOAT value;

    if (value_range <= 0.0f) {
        return min_value;
    }

    if (slider->Slider.Layout == LAYOUT_VERTICAL) {
        FLOAT const thumb_h = thumb && thumb->base.size.height > 0 ? thumb->base.size.height : slider_rect->h;
        FLOAT const travel_h = MAX(0.0f, slider_rect->h - thumb_h);
        FLOAT const local = mouse.y - slider_rect->y - thumb_h * 0.5f;
        fraction = travel_h > 0.0f ? 1.0f - (local / travel_h) : 0.0f;
    } else {
        FLOAT const thumb_w = thumb && thumb->base.size.width > 0 ? thumb->base.size.width : slider_rect->h;
        FLOAT const travel_w = MAX(0.0f, slider_rect->w - thumb_w);
        FLOAT const local = mouse.x - slider_rect->x - thumb_w * 0.5f;
        fraction = travel_w > 0.0f ? local / travel_w : 0.0f;
    }

    fraction = MAX(0.0f, MIN(1.0f, fraction));
    value = min_value + fraction * value_range;
    if (slider->Slider.StepSize > 0.0f) {
        value = min_value + roundf((value - min_value) / slider->Slider.StepSize) * slider->Slider.StepSize;
    }
    return MAX(min_value, MIN(max_value, value));
}

static void UI_UpdateSliderInteraction(LPCFRAMEDEF frame, LPCRECT rect, LPCFRAMEDEF thumb) {
    RECT thumb_rect = UI_SliderThumbRect(frame, rect, thumb);
    VECTOR2 mouse = UI_MouseToFdf();
    BOOL const can_start = !UI_PointerBlockedByPopup(frame) &&
                           (UI_MouseContains(rect) || UI_MouseContains(&thumb_rect));

    if (can_start) {
        active_slider = frame;
    }
    if (active_slider == frame) {
        ((LPFRAMEDEF)frame)->Slider.InitialValue = UI_SliderValueFromMousePos(frame, rect, thumb, mouse);
    }
    if (!(active_slider && (active_slider->ui_flags & UIFLAG_PRESSED)) && active_slider == frame) {
        active_slider = NULL;
    }
}

static void UI_DrawSlider(LPCFRAMEDEF frame, LPCRECT rect) {
    LPCFRAMEDEF backdrop;
    LPCFRAMEDEF thumb;

    if (frame->Control.Backdrop.Normal[0]) {
        backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
        UI_DrawBackdropWithColor(backdrop, rect, frame->base.color);
    }

    thumb = UI_FindFrameNear(frame, frame->Slider.ThumbButtonFrame);
    if (thumb) {
        UI_UpdateSliderInteraction(frame, rect, thumb);
        RECT thumb_rect = UI_SliderThumbRect(frame, rect, thumb);
        LPCFRAMEDEF thumb_backdrop = UI_FindFrameNear(thumb, thumb->Control.Backdrop.Normal);
        if (!thumb_backdrop) {
            thumb_backdrop = UI_ButtonBackdrop(thumb, &thumb_rect);
        }
        UI_DrawBackdropWithColor(thumb_backdrop, &thumb_rect, thumb->base.color);
        UI_DrawTexture(thumb, &thumb_rect);
    }
}

#endif
