#include "g_landmarks.h"
#include "common/stb_dbc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

uint32_t Wow_LandmarkRows(uint8_t const *data, uint32_t size) {
    stbDbc_t dbc;
    if (!data || size <= 20) return 0;
    uint64_t records = Stb_DbcRead32(data + 4);
    uint64_t record_size = Stb_DbcRead32(data + 12);
    uint64_t strings = Stb_DbcRead32(data + 16);
    if (20u + records * record_size + strings > size ||
        !Stb_DbcValid(data, size, &dbc) || dbc.fields != 19 || dbc.record_size != 76) return 0;
    return dbc.records;
}

bool Wow_LandmarkRead(uint8_t const *data, uint32_t size, uint32_t row,
                      uint32_t map, float left, float right, float top, float bottom,
                      wowLandmark_t *out) {
    stbDbc_t dbc;
    if (!out || !Wow_LandmarkRows(data, size) || !Stb_DbcValid(data, size, &dbc) || row >= dbc.records ||
        !isfinite(left) || !isfinite(right) || !isfinite(top) || !isfinite(bottom) ||
        fabsf(right - left) < 1 || fabsf(bottom - top) < 1) return false;
    uint8_t const *record = Stb_DbcRecordAt(Stb_DbcRecords(data), row, dbc.record_size);
    if (Stb_DbcField(&dbc, record, 7) != map) return false;
    float x = Stb_DbcReadFloat(record + 16), y = Stb_DbcReadFloat(record + 20);
    float u = (y - left) / (right - left), v = (x - top) / (bottom - top);
    if (!isfinite(u) || !isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1) return false;
    uint8_t const *strings = Stb_DbcStrings(data, &dbc);
    for (uint32_t field = 10; field < 18; ++field) {
        uint32_t offset = Stb_DbcField(&dbc, record, field);
        if (!offset || offset >= dbc.string_size) continue;
        char const *name = Stb_DbcString(strings, dbc.string_size, offset);
        if (!name || !*name || !memchr(name, 0, dbc.string_size - offset)) continue;
        *out = (wowLandmark_t){ .id = Stb_DbcField(&dbc, record, 0), .row = row,
                               .x = x, .y = y, .u = u, .v = v };
        snprintf(out->name, sizeof(out->name), "%s", name);
        return true;
    }
    return false;
}
