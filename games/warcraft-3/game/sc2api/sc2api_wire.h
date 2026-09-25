#ifndef WC3_SC2API_WIRE_H
#define WC3_SC2API_WIRE_H

#include "sc2api_compat.h"

typedef struct {
    LPBYTE data;
    DWORD capacity;
    DWORD size;
    BOOL failed;
} wc3Sc2PbWriter_t;

typedef struct {
    DWORD length_pos;
    DWORD payload_pos;
    BOOL valid;
} wc3Sc2PbMessageMark_t;

typedef struct {
    BYTE const *data;
    DWORD size;
    DWORD pos;
    BOOL failed;
} wc3Sc2PbReader_t;

void WC3_SC2API_PbWriterInit(wc3Sc2PbWriter_t *writer, LPBYTE data, DWORD capacity);
BOOL WC3_SC2API_PbWriteVarintField(wc3Sc2PbWriter_t *writer, DWORD field, uint64_t value);
BOOL WC3_SC2API_PbWriteSInt32Field(wc3Sc2PbWriter_t *writer, DWORD field, LONG value);
BOOL WC3_SC2API_PbWriteBoolField(wc3Sc2PbWriter_t *writer, DWORD field, BOOL value);
BOOL WC3_SC2API_PbWriteFloatField(wc3Sc2PbWriter_t *writer, DWORD field, FLOAT value);
BOOL WC3_SC2API_PbWriteBytesField(wc3Sc2PbWriter_t *writer, DWORD field, void const *data, DWORD size);
BOOL WC3_SC2API_PbWriteStringField(wc3Sc2PbWriter_t *writer, DWORD field, LPCSTR value);
wc3Sc2PbMessageMark_t WC3_SC2API_PbBeginMessage(wc3Sc2PbWriter_t *writer, DWORD field);
BOOL WC3_SC2API_PbEndMessage(wc3Sc2PbWriter_t *writer, wc3Sc2PbMessageMark_t mark);

void WC3_SC2API_PbReaderInit(wc3Sc2PbReader_t *reader, void const *data, DWORD size);
BOOL WC3_SC2API_PbNext(wc3Sc2PbReader_t *reader, DWORD *field, DWORD *wire_type);
BOOL WC3_SC2API_PbReadVarint(wc3Sc2PbReader_t *reader, uint64_t *value);
BOOL WC3_SC2API_PbReadFloat(wc3Sc2PbReader_t *reader, FLOAT *value);
BOOL WC3_SC2API_PbReadBytes(wc3Sc2PbReader_t *reader, BYTE const **data, DWORD *size);
BOOL WC3_SC2API_PbReadSubmessage(wc3Sc2PbReader_t *reader, wc3Sc2PbReader_t *sub);
BOOL WC3_SC2API_PbSkip(wc3Sc2PbReader_t *reader, DWORD wire_type);

#endif
