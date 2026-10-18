#ifdef WC3_SC2API

#include "sc2api_wire.h"

static bool pb_reserve(wc3Sc2PbWriter_t *writer, uint32_t count) {
    if (!writer || writer->failed || writer->size > writer->capacity ||
        count > writer->capacity - writer->size) {
        if (writer) writer->failed = true;
        return false;
    }
    return true;
}

static uint32_t pb_encode_varint(uint64_t value, uint8_t *out) {
    uint32_t size = 0;
    do {
        uint8_t byte = (uint8_t)(value & 0x7f);
        value >>= 7;
        if (value) byte |= 0x80;
        out[size++] = byte;
    } while (value);
    return size;
}

static bool pb_write_varint(wc3Sc2PbWriter_t *writer, uint64_t value) {
    uint8_t encoded[10];
    uint32_t size = pb_encode_varint(value, encoded);
    if (!pb_reserve(writer, size)) return false;
    memcpy(writer->data + writer->size, encoded, size);
    writer->size += size;
    return true;
}

static bool pb_write_key(wc3Sc2PbWriter_t *writer, uint32_t field, uint32_t wire_type) {
    if (!field || wire_type > 5) {
        if (writer) writer->failed = true;
        return false;
    }
    return pb_write_varint(writer, ((uint64_t)field << 3) | wire_type);
}

void WC3_SC2API_PbWriterInit(wc3Sc2PbWriter_t *writer, uint8_t *data, uint32_t capacity) {
    if (!writer) return;
    * writer = (wc3Sc2PbWriter_t){ .data = data, .capacity = capacity };
    if (!data && capacity) writer->failed = true;
}

bool WC3_SC2API_PbWriteVarintField(wc3Sc2PbWriter_t *writer, uint32_t field, uint64_t value) {
    return pb_write_key(writer, field, 0) && pb_write_varint(writer, value);
}

bool WC3_SC2API_PbWriteSInt32Field(wc3Sc2PbWriter_t *writer, uint32_t field, int32_t value) {
    /* SC2 proto fields using int32 use protobuf's ordinary two's-complement
     * varint encoding, not sint32 zig-zag encoding. */
    return WC3_SC2API_PbWriteVarintField(writer, field, (uint64_t)(int64_t)value);
}

bool WC3_SC2API_PbWriteBoolField(wc3Sc2PbWriter_t *writer, uint32_t field, bool value) {
    return WC3_SC2API_PbWriteVarintField(writer, field, value ? 1 : 0);
}

bool WC3_SC2API_PbWriteFloatField(wc3Sc2PbWriter_t *writer, uint32_t field, float value) {
    uint32_t bits;
    if (!pb_write_key(writer, field, 5) || !pb_reserve(writer, 4)) return false;
    memcpy(&bits, &value, sizeof(bits));
    writer->data[writer->size++] = (uint8_t)(bits & 0xff);
    writer->data[writer->size++] = (uint8_t)((bits >> 8) & 0xff);
    writer->data[writer->size++] = (uint8_t)((bits >> 16) & 0xff);
    writer->data[writer->size++] = (uint8_t)((bits >> 24) & 0xff);
    return true;
}

bool WC3_SC2API_PbWriteBytesField(wc3Sc2PbWriter_t *writer, uint32_t field, void const *data, uint32_t size) {
    if (!pb_write_key(writer, field, 2) || !pb_write_varint(writer, size) || !pb_reserve(writer, size)) return false;
    if (size) memcpy(writer->data + writer->size, data, size);
    writer->size += size;
    return true;
}

bool WC3_SC2API_PbWriteStringField(wc3Sc2PbWriter_t *writer, uint32_t field, cstring_t value) {
    if (!value) value = "";
    return WC3_SC2API_PbWriteBytesField(writer, field, value, (uint32_t)strlen(value));
}

wc3Sc2PbMessageMark_t WC3_SC2API_PbBeginMessage(wc3Sc2PbWriter_t *writer, uint32_t field) {
    wc3Sc2PbMessageMark_t mark = { 0 };
    if (!pb_write_key(writer, field, 2) || !pb_reserve(writer, 5)) return mark;
    mark.length_pos = writer->size;
    writer->size += 5;
    mark.payload_pos = writer->size;
    mark.valid = true;
    return mark;
}

bool WC3_SC2API_PbEndMessage(wc3Sc2PbWriter_t *writer, wc3Sc2PbMessageMark_t mark) {
    uint8_t encoded[10];
    uint32_t payload_size, encoded_size, reserved = 5;

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

void WC3_SC2API_PbReaderInit(wc3Sc2PbReader_t *reader, void const *data, uint32_t size) {
    if (!reader) return;
    * reader = (wc3Sc2PbReader_t){ .data = data, .size = size };
    if (!data && size) reader->failed = true;
}

bool WC3_SC2API_PbReadVarint(wc3Sc2PbReader_t *reader, uint64_t *value) {
    uint64_t out = 0;
    uint32_t shift = 0;
    if (!reader || reader->failed) return false;
    while (reader->pos < reader->size && shift < 64) {
        uint8_t byte = reader->data[reader->pos++];
        if (shift == 63 && (byte & 0xfe)) { reader->failed = true; return false; }
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

bool WC3_SC2API_PbNext(wc3Sc2PbReader_t *reader, uint32_t *field, uint32_t *wire_type) {
    uint64_t key;
    if (!reader || reader->failed || reader->pos >= reader->size) return false;
    if (!WC3_SC2API_PbReadVarint(reader, &key) || !(key >> 3) || (key >> 3) > 0x1fffffffu) {
        reader->failed = true;
        return false;
    }
    if (field) *field = (uint32_t)(key >> 3);
    if (wire_type) *wire_type = (uint32_t)(key & 7);
    return true;
}

bool WC3_SC2API_PbReadFloat(wc3Sc2PbReader_t *reader, float *value) {
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

bool WC3_SC2API_PbReadBytes(wc3Sc2PbReader_t *reader, uint8_t const **data, uint32_t *size) {
    uint64_t length;
    if (!WC3_SC2API_PbReadVarint(reader, &length) || length > UINT32_MAX || length > reader->size - reader->pos) {
        if (reader) reader->failed = true;
        return false;
    }
    if (data) *data = reader->data + reader->pos;
    if (size) *size = (uint32_t)length;
    reader->pos += (uint32_t)length;
    return true;
}

bool WC3_SC2API_PbReadSubmessage(wc3Sc2PbReader_t *reader, wc3Sc2PbReader_t *sub) {
    uint8_t const *data;
    uint32_t size;
    if (!sub || !WC3_SC2API_PbReadBytes(reader, &data, &size)) return false;
    WC3_SC2API_PbReaderInit(sub, data, size);
    return true;
}

bool WC3_SC2API_PbSkip(wc3Sc2PbReader_t *reader, uint32_t wire_type) {
    uint64_t ignored;
    uint8_t const *bytes;
    uint32_t size;
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
