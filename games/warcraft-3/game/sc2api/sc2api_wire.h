#ifndef WC3_SC2API_WIRE_H
#define WC3_SC2API_WIRE_H

#include "sc2api_compat.h"

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t size;
    bool failed;
} wc3Sc2PbWriter_t;

typedef struct {
    uint32_t length_pos;
    uint32_t payload_pos;
    bool valid;
} wc3Sc2PbMessageMark_t;

typedef struct {
    uint8_t const *data;
    uint32_t size;
    uint32_t pos;
    bool failed;
} wc3Sc2PbReader_t;

void WC3_SC2API_PbWriterInit(wc3Sc2PbWriter_t *writer, uint8_t *data, uint32_t capacity);
bool WC3_SC2API_PbWriteVarintField(wc3Sc2PbWriter_t *writer, uint32_t field, uint64_t value);
bool WC3_SC2API_PbWriteSInt32Field(wc3Sc2PbWriter_t *writer, uint32_t field, int32_t value);
bool WC3_SC2API_PbWriteBoolField(wc3Sc2PbWriter_t *writer, uint32_t field, bool value);
bool WC3_SC2API_PbWriteFloatField(wc3Sc2PbWriter_t *writer, uint32_t field, float value);
bool WC3_SC2API_PbWriteBytesField(wc3Sc2PbWriter_t *writer, uint32_t field, void const *data, uint32_t size);
bool WC3_SC2API_PbWriteStringField(wc3Sc2PbWriter_t *writer, uint32_t field, cstring_t value);
wc3Sc2PbMessageMark_t WC3_SC2API_PbBeginMessage(wc3Sc2PbWriter_t *writer, uint32_t field);
bool WC3_SC2API_PbEndMessage(wc3Sc2PbWriter_t *writer, wc3Sc2PbMessageMark_t mark);

void WC3_SC2API_PbReaderInit(wc3Sc2PbReader_t *reader, void const *data, uint32_t size);
bool WC3_SC2API_PbNext(wc3Sc2PbReader_t *reader, uint32_t *field, uint32_t *wire_type);
bool WC3_SC2API_PbReadVarint(wc3Sc2PbReader_t *reader, uint64_t *value);
bool WC3_SC2API_PbReadFloat(wc3Sc2PbReader_t *reader, float *value);
bool WC3_SC2API_PbReadBytes(wc3Sc2PbReader_t *reader, uint8_t const **data, uint32_t *size);
bool WC3_SC2API_PbReadSubmessage(wc3Sc2PbReader_t *reader, wc3Sc2PbReader_t *sub);
bool WC3_SC2API_PbSkip(wc3Sc2PbReader_t *reader, uint32_t wire_type);

#endif
