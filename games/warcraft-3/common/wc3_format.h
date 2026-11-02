#ifndef BZ_WC3_FORMAT_H
#define BZ_WC3_FORMAT_H

#include <stdio.h>
#include "wc3_math.h"

/* Original070f20: unsigned exponent and wrapping integer products, not the
 * signed-SAR scalar-power loop. The formatter supplies base10 and exponent0..9. */
static inline uint32_t wc3_u32_power(uint32_t base, uint32_t exponent) {
    uint32_t result = 1;
    while (exponent) {
        if (exponent & 1) result *= base;
        base *= base;
        exponent >>= 1;
    }
    return result;
}

/* Original0701d0 uses software scalar rounding, then integer formatting. Its
 * registered callers own256-byte buffers. Reject an oversized result before
 * writing; original overflowing-buffer/fault lifetime is a separate contract. */
static inline bool wc3_format_real(float input, int32_t width, int32_t precision,
                                   char *buffer, size_t capacity) {
    uint32_t word = wc3_float_bits(input), magnitude = word & 0x7fffffffu;
    bool negative = (word & 0x80000000u) && magnitude;
    size_t sign = negative ? 1 : 0;
    if (capacity <= sign) return false;
    char *output = buffer + sign;
    size_t available = capacity - sign;
    float value = wc3_float(magnitude);
    if (magnitude > 0x4effffffu) {
        if (magnitude == 0x7f7fffffu || magnitude == 0x7f800000u) {
            if (available < 4) return false;
            memcpy(output, "inf", 4);
        } else {
            /* Unordered COMISS also takes this reduction for NaNs. The
             * original helper divides by software10, with no host FP path. */
            unsigned zeroes = 0;
            do { value = wc3_div(value, 10.0f); zeroes++; }
            while (wc3_float_bits(value) > 0x4effffffu);
            char digits[16];
            int length = snprintf(digits, sizeof(digits), "%d",
                (int32_t)wc3_int_bits(wc3_float_bits(value)));
            int32_t places = precision < 1 ? 1 : precision;
            size_t required = (size_t)length + zeroes + 1 + (uint32_t)places;
            if (required >= available) return false;
            memcpy(output, digits, length);
            memset(output + length, '0', zeroes);
            output += length + zeroes;
            *output++ = '.';
            memset(output, '0', places);
            output[places] = '\0';
        }
    } else {
        bool trim = precision < 0;
        unsigned places = trim ? 6 : precision > 9 ? 9 : (unsigned)precision;
        uint32_t scale = wc3_u32_power(10, places);
        float fraction = wc3_mul(wc3_fraction(value), wc3_float(wc3_from_int(scale)));
        uint32_t digits = wc3_int_bits(wc3_round_bits(wc3_float_bits(fraction)));
        uint32_t whole = wc3_int_bits(magnitude);
        if (digits >= scale) { whole++; digits -= scale; }
        while (trim && places > 1 && digits % 10 == 0) { places--; digits /= 10; }
        /* SUB is32-bit before the signed minimum-width comparison. */
        uint32_t width_word = (uint32_t)width - places;
        int64_t integer_width = width_word & 0x80000000u ?
            (int64_t)width_word - 0x100000000ll : width_word;
        if (integer_width < 1) integer_width = 1;
        char whole_text[16], fraction_text[16];
        unsigned whole_digits = snprintf(whole_text, sizeof(whole_text), "%d", (int32_t)whole);
        unsigned fraction_digits = snprintf(fraction_text, sizeof(fraction_text), "%d", (int32_t)digits);
        size_t padding = integer_width > whole_digits ? (size_t)integer_width - whole_digits : 0;
        size_t zeroes = places > fraction_digits ? places - fraction_digits : 0;
        uint64_t required = padding + whole_digits + 1u + zeroes + fraction_digits;
        if (required >= available) return false;
        /* Only bounded integer conversion goes through printf. Padding uses
         * the preflighted lengths, including any integer sign characters. */
        memset(output, ' ', padding);output += padding;
        memcpy(output, whole_text, whole_digits);output += whole_digits;
        *output++ = '.';
        char const *fraction_start = fraction_text;
        if (fraction_text[0] == '-') {
            *output++ = '-';fraction_start++;fraction_digits--;
        }
        memset(output, '0', zeroes);output += zeroes;
        memcpy(output, fraction_start, fraction_digits);output[fraction_digits] = '\0';
    }
    if (negative) buffer[0] = '-';
    return true;
}
#endif
