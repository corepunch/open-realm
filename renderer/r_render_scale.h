#ifndef r_render_scale_h
#define r_render_scale_h

/* Scale both axes together; logical UI and input dimensions remain native. */
static inline size2_t R_RenderScaleSize(size2_t native, int percent) {
    if (percent < 50 || percent > 100) percent = 100;
    return (size2_t){ MAX(1, native.width * (uint64_t)percent / 100),
                      MAX(1, native.height * (uint64_t)percent / 100) };
}
#endif
