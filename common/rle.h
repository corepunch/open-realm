#ifndef common_rle_h
#define common_rle_h

#include "common/shared.h"

/* Shared 1-bit run-length codec for FOW and terrain-mask (Blight) datagrams.
 * Wire: payload[0] selects the mode. 0/1 is RLE with that initial value and
 * payload[1..] are run lengths; 2 is a raw bitpack escape holding LSB-first
 * bits in payload[1..]. A 255 RLE run continues the same value (next byte
 * extends it); a run < 255 toggles the value, with an explicit 0 run
 * preserving alternation after 255. RLE has no worst-case bound (an
 * alternating row costs about one byte per bit), so Blight falls back to the
 * bitpack escape when RLE overflows: one row then always fits the bitpack
 * reservation and the sweep keeps making progress. FOW never emits the
 * escape, so its wire is unchanged. Static inline so game libs (which never
 * link the common C sources) share one codec with the engine, exactly like
 * the TerrainMask_* helpers. */

typedef uint8_t (*rleBitReader_t)(uint32_t index, void *ctx); // 1-bit mask source; returns 0 or 1
typedef void (*rleRunWriter_t)(uint32_t index, uint8_t value, uint32_t count, void *ctx); // run sink; index is the first bit

static inline uint32_t MSG_EncodeRLE(uint8_t * out, uint32_t capacity, uint32_t bit_count, rleBitReader_t read, void *ctx) {
    uint32_t n = 0, i;
    bool started = false;
    uint8_t cur = 0, run = 0;
    if (!out || !read || !bit_count || capacity < 2) return 0;
    for (i = 0; i < bit_count; i++) {
        uint8_t v = read(i, ctx) ? 1 : 0;
        if (!started) { out[n++] = v; cur = v; run = 1; started = true; continue; }
        if (v == cur) {
            if (run == 255) { if (n >= capacity) return 0; out[n++] = 255; run = 0; }
            run++;
            continue;
        }
        if (n >= capacity) return 0;
        out[n++] = run;
        if (run == 255) { if (n >= capacity) return 0; out[n++] = 0; }
        cur = v; run = 1;
    }
    if (!started || n >= capacity) return 0;
    out[n++] = run;
    return n;
}

/* Bitpack escape: bounded at 1 + (bits+7)/8 bytes, so a single row always fits
 * the datagram reservation even for checkerboard masks RLE cannot compress. */
static inline uint32_t MSG_EncodeBitpack(uint8_t * out, uint32_t capacity, uint32_t bit_count, rleBitReader_t read, void *ctx) {
    uint32_t n, i;
    if (!out || !read || !bit_count || capacity < 1 + (bit_count + 7) / 8) return 0;
    n = 1 + (bit_count + 7) / 8;
    out[0] = 2; memset(out + 1, 0, n - 1);
    for (i = 0; i < bit_count; i++) if (read(i, ctx)) out[1 + (i >> 3)] |= (uint8_t)(1u << (i & 7));
    return n;
}

static inline bool MSG_ValidateRLE(uint8_t const *payload, uint32_t payload_bytes, uint32_t expected_bits) {
    uint32_t bits = 0, i;
    if (!payload || payload_bytes < 2 || !expected_bits) return false;
    if (payload[0] == 2) return payload_bytes == 1 + (expected_bits + 7) / 8;
    if (payload[0] != 0 && payload[0] != 1) return false;
    for (i = 1; i < payload_bytes; i++) {
        if (bits == expected_bits) return false;
        bits += payload[i];
        if (bits > expected_bits) return false;
    }
    return bits == expected_bits;
}

static inline uint32_t MSG_DecodeRLE(uint8_t const *payload, uint32_t payload_bytes, uint32_t expected_bits,
    rleRunWriter_t write, void *ctx) {
    uint32_t done = 0, i;
    uint8_t value;
    if (!payload || !write || payload_bytes < 2 || !expected_bits) return 0;
    if (payload[0] == 2) { /* bitpack escape; runs are coalesced so plane writers keep memset speed */
        uint32_t bit = 0;
        if (payload_bytes != 1 + (expected_bits + 7) / 8) return 0;
        while (bit < expected_bits) {
            uint32_t end = bit + 1;
            uint8_t v = (payload[1 + (bit >> 3)] >> (bit & 7)) & 1;
            while (end < expected_bits && (((payload[1 + (end >> 3)] >> (end & 7)) & 1) == v)) end++;
            write(bit, v, end - bit, ctx);
            bit = end;
        }
        return expected_bits;
    }
    if (payload[0] != 0 && payload[0] != 1) return 0;
    value = payload[0] ? 1 : 0;
    for (i = 1; i < payload_bytes; i++) {
        uint32_t run = payload[i];
        while (run) {
            uint32_t count;
            if (done >= expected_bits) return 0;
            count = MIN(run, expected_bits - done);
            write(done, value, count, ctx);
            done += count; run -= count;
        }
        if (payload[i] != 255) value = !value;
    }
    return done == expected_bits ? done : 0;
}

#endif
