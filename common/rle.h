#ifndef common_rle_h
#define common_rle_h

/* Shared 1-bit run-length codec for FOW and terrain-mask (Blight) datagrams.
 * Wire: payload[0] is the initial value (0/1), payload[1..] are run lengths.
 * A 255 run continues the same value (next byte extends it); a run < 255
 * toggles the value, with an explicit 0 run preserving alternation after 255.
 * Static inline so game libs (which never link the common C sources) share one
 * codec with the engine, exactly like the TerrainMask_* helpers. */

typedef BYTE (*rleBitReader_t)(DWORD index, void *ctx); // 1-bit mask source; returns 0 or 1
typedef void (*rleRunWriter_t)(DWORD index, BYTE value, DWORD count, void *ctx); // run sink; index is the first bit

static inline DWORD MSG_EncodeRLE(LPBYTE out, DWORD capacity, DWORD bit_count, rleBitReader_t read, void *ctx) {
    DWORD n = 0, i;
    BOOL started = false;
    BYTE cur = 0, run = 0;
    if (!out || !read || !bit_count || capacity < 2) return 0;
    for (i = 0; i < bit_count; i++) {
        BYTE v = read(i, ctx) ? 1 : 0;
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

static inline BOOL MSG_ValidateRLE(BYTE const *payload, DWORD payload_bytes, DWORD expected_bits) {
    DWORD bits = 0, i;
    if (!payload || payload_bytes < 2 || !expected_bits || (payload[0] != 0 && payload[0] != 1)) return false;
    for (i = 1; i < payload_bytes; i++) {
        if (bits == expected_bits) return false;
        bits += payload[i];
        if (bits > expected_bits) return false;
    }
    return bits == expected_bits;
}

static inline DWORD MSG_DecodeRLE(BYTE const *payload, DWORD payload_bytes, DWORD expected_bits,
    rleRunWriter_t write, void *ctx) {
    DWORD done = 0, i;
    BYTE value;
    if (!payload || !write || payload_bytes < 2 || !expected_bits || (payload[0] != 0 && payload[0] != 1)) return 0;
    value = payload[0] ? 1 : 0;
    for (i = 1; i < payload_bytes; i++) {
        DWORD run = payload[i];
        while (run) {
            DWORD count;
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
