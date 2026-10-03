#include "common.h"
#include "online_packet.h"
#include <stdlib.h>

static uint32_t Online_ReadWord(uint8_t const *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void Online_WriteWord(uint8_t *p, uint32_t value) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

void Online_ClearAssembly(onlineAssembly_t *assembly) {
    free(assembly->data);
    memset(assembly, 0, sizeof(*assembly));
}

uint32_t Online_WriteFragment(uint8_t *out, uint32_t id, uint32_t size, uint32_t offset, void const *data) {
    if (!size || size > MAX_MSGLEN || offset >= size) return 0;
    uint32_t bytes = MIN(size - offset, ONLINE_FRAGMENT_DATA);
    Online_WriteWord(out, id);
    Online_WriteWord(out + 4, size);
    Online_WriteWord(out + 8, offset);
    memcpy(out + ONLINE_FRAGMENT_HEADER, (uint8_t const *)data + offset, bytes);
    return bytes + ONLINE_FRAGMENT_HEADER;
}

int Online_ReadFragment(onlineAssembly_t *assembly, void const *data, uint32_t size, sizeBuf_t *message) {
    uint8_t const *p = data;
    if (size <= ONLINE_FRAGMENT_HEADER || size > ONLINE_FRAGMENT_SIZE) goto invalid;
    uint32_t id = Online_ReadWord(p), total = Online_ReadWord(p + 4), offset = Online_ReadWord(p + 8);
    uint32_t bytes = size - ONLINE_FRAGMENT_HEADER;
    if (!total || total > MAX_MSGLEN || total > message->maxsize || offset >= total || bytes > total - offset)
        goto invalid;
    if (bytes != MIN(total - offset, ONLINE_FRAGMENT_DATA)) goto invalid;
    if (!offset) {
        Online_ClearAssembly(assembly);
        assembly->data = malloc(total);
        if (!assembly->data) goto invalid;
        assembly->id = id; assembly->size = total;
    }
    if (!assembly->data || assembly->id != id || assembly->size != total || assembly->used != offset)
        goto invalid;
    memcpy(assembly->data + offset, p + ONLINE_FRAGMENT_HEADER, bytes);
    assembly->used += bytes;
    assembly->age = 0;
    if (assembly->used < total) return 0;
    memcpy(message->data, assembly->data, total);
    message->cursize = total; message->readcount = 0; message->overflowed = false;
    Online_ClearAssembly(assembly);
    return (int)total;
invalid:
    Online_ClearAssembly(assembly);
    return -1;
}
