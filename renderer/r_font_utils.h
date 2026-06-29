#ifndef r_font_utils_h
#define r_font_utils_h

/* One thousandth of a pixel in normalized UI space is exact enough for glyph-fit decisions. */
static BOOL R_TextFitsWidth(FLOAT remaining) { return remaining >= -0.000001f; }

#endif
