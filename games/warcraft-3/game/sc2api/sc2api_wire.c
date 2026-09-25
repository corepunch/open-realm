#ifdef WC3_SC2API

#include "sc2api_wire.h"

static BOOL pb_reserve(wc3Sc2PbWriter_t *writer, DWORD count) {
    if (!writer || writer->failed || writer->size > writer->capacity ||
        count > writer->capacity - writer->size) {
        if (writer) writer->failed = true;
        return false;
    }
    return true;
}

static DWORD pb_encode_varint(uint64_t value, LPBYTE out) {
    DWORD size = 0;
    do {
        BYTE byte = (BYTE)(value & 0x7f);
        value >>= 7;
        if (value) byte |= 0x80;
        out[size++] = byte;
    } while (value);
    return size;
}

static BOOL pb_write_varint(wc3Sc2PbWriter_t *writer, uint64_t value) {
    BYTE encoded[10];
    DWORD size = pb_encode_varint(value, encoded);
    if (!pb_reserve(writer, size)) return false;
    memcpy(writer->data + writer->size, encoded, size);
    writer->size += size;
    return true;
}

static BOOL pb_write_key(wc3Sc2PbWriter_t *writer, DWORD field, DWORD wire_type) {
    if (!field || wire_type > 5) {
        if (writer) writer->failed = true;
        return false;
    }
    return pb_write_varint(writer, ((uint64_t)field << 3) | wire_type);
}

void WC3_SC2API_PbWriterInit(wc3Sc2PbWriter_t *writer, LPBYTE data, DWORD capacity) {
    if (!writer) return;
    *writer = (wc3Sc2PbWriter_t){ .data = data, .capacity = capacity };
    if (!data && capacity) writer->failed = true;
}

BOOL WC3_SC2API_PbWriteVarintField(wc3Sc2PbWriter_t *writer, DWORD field, uint64_t value) {
    return pb_write_key(writer, field, 0) && pb_write_varint(writer, value);
}

BOOL WC3_SC2API_PbWriteSInt32Field(wc3Sc2PbWriter_t *writer, DWORD field, LONG value) {
    /* SC2 proto fields using int32 use protobuf's ordinary two's-complement
     * varint encoding, not sint32 zig-zag encoding. */
    return WC3_SC2API_PbWriteVarintField(writer, field, (uint64_t)(int64_t)value);
}

BOOL WC3_SC2API_PbWriteBoolField(wc3Sc2PbWriter_t *writer, DWORD field, BOOL value) {
    return WC3_SC2API_PbWriteVarintField(writer, field, value ? 1 : 0);
}

BOOL WC3_SC2API_PbWriteFloatField(wc3Sc2PbWriter_t *writer, DWORD field, FLOAT value) {
    uint32_t bits;
    if (!pb_write_key(writer, field, 5) || !pb_reserve(writer, 4)) return false;
    memcpy(&bits, &value, sizeof(bits));
    writer->data[writer->size++] = (BYTE)(bits & 0xff);
    writer->data[writer->size++] = (BYTE)((bits >> 8) & 0xff);
    writer->data[writer->size++] = (BYTE)((bits >> 16) & 0xff);
    writer->data[writer->size++] = (BYTE)((bits >> 24) & 0xff);
    return true;
}

BOOL WC3_SC2API_PbWriteBytesField(wc3Sc2PbWriter_t *writer, DWORD field, void const *data, DWORD size) {
    if (!pb_write_key(writer, field, 2) || !pb_write_varint(writer, size) || !pb_reserve(writer, size)) return false;
    if (size) memcpy(writer->data + writer->size, data, size);
    writer->size += size;
    return true;
}

BOOL WC3_SC2API_PbWriteStringField(wc3Sc2PbWriter_t *writer, DWORD field, LPCSTR value) {
    if (!value) value = "";
    return WC3_SC2API_PbWriteBytesField(writer, field, value, (DWORD)strlen(value));
}

wc3Sc2PbMessageMark_t WC3_SC2API_PbBeginMessage(wc3Sc2PbWriter_t *writer, DWORD field) {
    wc3Sc2PbMessageMark_t mark = { 0 };
    if (!pb_write_key(writer, field, 2) || !pb_reserve(writer, 5)) return mark;
    mark.length_pos = writer->size;
    writer->size += 5;
    mark.payload_pos = writer->size;
    mark.valid = true;
    return mark;
}

BOOL WC3_SC2API_PbEndMessage(wc3Sc2PbWriter_t *writer, wc3Sc2PbMessageMark_t mark) {
    BYTE encoded[10];
    DWORD payload_size, encoded_size, reserved = 5;

    if (!writer || writer->failed || !mark.valid || mark.payload_pos > writer->size ||
        mark.payload_pos != mark.length_pos + reserved) {
        if (writer) writer->failed = true;
        return false;
    }
    payload_size = writer->size - mark.payload_pos;
    encoded_size = pb_encode_varint(payload_size, encoded);
    if (encoded_size > reserved) {
        writer->failed = true;
        return false;
    }
    if (encoded_size < reserved) {
        memmove(writer->data + mark.length_pos + encoded_size,
                writer->data + mark.payload_pos,
                payload_size);
        writer->size -= reserved - encoded_size;
    }
    memcpy(writer->data + mark.length_pos, encoded, encoded_size);
    return true;
}

void WC3_SC2API_PbReaderInit(wc3Sc2PbReader_t *reader, void const *data, DWORD size) {
    if (!reader) return;
    *reader = (wc3Sc2PbReader_t){ .data = data, .size = size };
    if (!data && size) reader->failed = true;
}

BOOL WC3_SC2API_PbReadVarint(wc3Sc2PbReader_t *reader, uint64_t *value) {
    uint64_t out = 0;
    DWORD shift = 0;
    if (!reader || reader->failed) return false;
    while (reader->pos < reader->size && shift < 64) {
        BYTE byte = reader->data[reader->pos++];
        out |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            if (value) *value = out;
            return true;
        }
        shift += 7;
    }
    reader->failed = true;
    return false;
}

BOOL WC3_SC2API_PbNext(wc3Sc2PbReader_t *reader, DWORD *field, DWORD *wire_type) {
    uint64_t key;
    if (!reader || reader->failed || reader->pos >= reader->size) return false;
    if (!WC3_SC2API_PbReadVarint(reader, &key) || !(key >> 3)) {
        reader->failed = true;
        return false;
    }
    if (field) *field = (DWORD)(key >> 3);
    if (wire_type) *wire_type = (DWORD)(key & 7);
    return true;
}

BOOL WC3_SC2API_PbReadFloat(wc3Sc2PbReader_t *reader, FLOAT *value) {
    uint32_t bits;
    if (!reader || reader->failed || reader->size - reader->pos < 4) {
        if (reader) reader->failed = true;
        return false;
    }
    bits = (uint32_t)reader->data[reader->pos] |
           ((uint32_t)reader->data[reader->pos + 1] << 8) |
           ((uint32_t)reader->data[reader->pos + 2] << 16) |
           ((uint32_t)reader->data[reader->pos + 3] << 24);
    reader->pos += 4;
    if (value) memcpy(value, &bits, sizeof(bits));
    return true;
}

BOOL WC3_SC2API_PbReadBytes(wc3Sc2PbReader_t *reader, BYTE const **data, DWORD *size) {
    uint64_t length;
    if (!WC3_SC2API_PbReadVarint(reader, &length) || length > UINT32_MAX || length > reader->size - reader->pos) {
        if (reader) reader->failed = true;
        return false;
    }
    if (data) *data = reader->data + reader->pos;
    if (size) *size = (DWORD)length;
    reader->pos += (DWORD)length;
    return true;
}

BOOL WC3_SC2API_PbReadSubmessage(wc3Sc2PbReader_t *reader, wc3Sc2PbReader_t *sub) {
    BYTE const *data;
    DWORD size;
    if (!sub || !WC3_SC2API_PbReadBytes(reader, &data, &size)) return false;
    WC3_SC2API_PbReaderInit(sub, data, size);
    return true;
}

BOOL WC3_SC2API_PbSkip(wc3Sc2PbReader_t *reader, DWORD wire_type) {
    uint64_t ignored;
    BYTE const *bytes;
    DWORD size;
    if (!reader || reader->failed) return false;
    switch (wire_type) {
        case 0: return WC3_SC2API_PbReadVarint(reader, &ignored);
        case 1:
            if (reader->size - reader->pos < 8) break;
            reader->pos += 8;
            return true;
        case 2: return WC3_SC2API_PbReadBytes(reader, &bytes, &size);
        case 5:
            if (reader->size - reader->pos < 4) break;
            reader->pos += 4;
            return true;
        default: break;
    }
    reader->failed = true;
    return false;
}

#endif
