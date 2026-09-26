#ifndef UI_CONTROL_SLIDER_H
#define UI_CONTROL_SLIDER_H

static float UI_SliderFraction(frameDef_t const *frame) {
    float min_value = frame->Slider.MinValue;
    float max_value = frame->Slider.MaxValue;
    float value = frame->Slider.InitialValue;

    if (max_value <= min_value) {
        return 0.0f;
    }
    value = MAX(min_value, MIN(max_value, value));
    return (value - min_value) / (max_value - min_value);
}

static rect_t UI_SliderThumbRect(frameDef_t const *slider, rect_t const *slider_rect, frameDef_t const *thumb) {
    float const fraction = UI_SliderFraction(slider);
    float const thumb_w = thumb && thumb->Width > 0 ? thumb->Width : slider_rect->h;
    float const thumb_h = thumb && thumb->Height > 0 ? thumb->Height : slider_rect->h;
    float const travel_w = MAX(0.0f, slider_rect->w - thumb_w);
    float const travel_h = MAX(0.0f, slider_rect->h - thumb_h);
    rect_t rect = {
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

static float UI_SliderValueFromMousePos(frameDef_t const *slider, rect_t const *slider_rect, frameDef_t const *thumb, vector2_t mouse) {
    float const min_value = slider->Slider.MinValue;
    float const max_value = slider->Slider.MaxValue;
    float value_range = max_value - min_value;
    float fraction;
    float value;

    if (value_range <= 0.0f) {
        return min_value;
    }

    if (slider->Slider.Layout == LAYOUT_VERTICAL) {
        float const thumb_h = thumb && thumb->Height > 0 ? thumb->Height : slider_rect->h;
        float const travel_h = MAX(0.0f, slider_rect->h - thumb_h);
        float const local = mouse.y - slider_rect->y - thumb_h * 0.5f;
        fraction = travel_h > 0.0f ? 1.0f - (local / travel_h) : 0.0f;
    } else {
        float const thumb_w = thumb && thumb->Width > 0 ? thumb->Width : slider_rect->h;
        float const travel_w = MAX(0.0f, slider_rect->w - thumb_w);
        float const local = mouse.x - slider_rect->x - thumb_w * 0.5f;
        fraction = travel_w > 0.0f ? local / travel_w : 0.0f;
    }

    fraction = MAX(0.0f, MIN(1.0f, fraction));
    value = min_value + fraction * value_range;
    if (slider->Slider.StepSize > 0.0f) {
        value = min_value + roundf((value - min_value) / slider->Slider.StepSize) * slider->Slider.StepSize;
    }
    return MAX(min_value, MIN(max_value, value));
}

static void UI_DrawSlider(frameDef_t const *frame, rect_t const *rect) {
    frameDef_t const *backdrop;
    frameDef_t const *thumb;

    if (frame->Control.Backdrop.Normal[0]) {
        backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
        UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    }

    thumb = UI_FindFrameNear(frame, frame->Slider.ThumbButtonFrame);
    if (thumb) {
        rect_t thumb_rect = UI_SliderThumbRect(frame, rect, thumb);
        frameDef_t const *thumb_backdrop = UI_FindFrameNear(thumb, thumb->Control.Backdrop.Normal);
        if (!thumb_backdrop) {
            thumb_backdrop = UI_ButtonBackdrop(thumb, &thumb_rect);
        }
        UI_DrawBackdropWithColor(thumb_backdrop, &thumb_rect, thumb->Color);
        UI_DrawTexture(thumb, &thumb_rect);
    }
}

#endif
