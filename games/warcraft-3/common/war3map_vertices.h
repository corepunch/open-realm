#ifndef W3E_VERTEX_IO_H
#define W3E_VERTEX_IO_H

#include "common/common.h"
#include <limits.h>

/* W3E vertices occupy seven bytes on disk, not sizeof(war3mapVertex_t).
 * Preserve this packed format for both collision and renderer terrain. */
#define W3E_VERTEX_BATCH 4096u // vertices; bounds stack reads to 28 KiB while amortizing MPQ calls
#define W3E_MAX_VERTEX_DIMENSION 2049u // vertices; bounds each axis before applying the one-million-vertex cap
#define W3E_MAX_VERTICES (1024u * 1024u) // vertices; caps decoded terrain allocation at about 14 MiB

/* Decode one seven-byte little-endian W3E record into the shared runtime shape. */
static inline void W3E_DecodeVertex(uint8_t const *data, war3mapVertex_t *vert) {
    uint16_t const water_and_edge = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    uint8_t const flags = data[4];
    uint8_t const variation = data[5];
    uint8_t const cliff_and_layer = data[6];

    memset(vert, 0, sizeof(*vert));
    vert->accurate_height = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    vert->waterlevel = water_and_edge & 0x3fffu;
    vert->mapedge = (water_and_edge & 0x4000u) != 0;
    vert->ground = flags & 0x0fu;
    vert->ramp = (flags & 0x10u) != 0;
    vert->blight = (flags & 0x20u) != 0;
    vert->water = (flags & 0x40u) != 0;
    vert->boundary = (flags & 0x80u) != 0;
    vert->cliffVariation = (variation >> 5) & 0x07u;
    vert->groundVariation = variation & 0x1fu;
    vert->cliff = (cliff_and_layer >> 4) & 0x0fu;
    vert->level = cliff_and_layer & 0x0fu;
}

#define W3E_MAX_PALETTE_ENTRIES 256u // entries; bounds malformed W3E palette allocations to 1 KiB

/* Reject short MPQ reads so callers never decode partially initialized fields. */
static inline bool W3E_ReadExact(handle_t file, void *dst, uint32_t size) {
    uint32_t actual = 0;
    return SFileReadFile(file, dst, size, &actual, NULL) && actual == size;
}

/* Avoid unchecked SFileReadArray allocations from a malformed palette count. */
static inline bool W3E_ReadPalette(handle_t file, uint32_t *count, uint32_t **ids,
                                   handle_t (*alloc)(long)) {
    uint32_t pos, file_size;
    if (!W3E_ReadExact(file, count, sizeof(*count)) || *count > W3E_MAX_PALETTE_ENTRIES)
        return false;
    if (!*count) return true;
    pos = SFileSetFilePointer(file, 0, NULL, FILE_CURRENT);
    file_size = SFileGetFileSize(file, NULL);
    if (pos == SFILE_INVALID_POS || pos > file_size || *count > (file_size - pos) / sizeof(**ids))
        return false;
    *ids = alloc((long)(*count * sizeof(**ids)));
    return *ids && W3E_ReadExact(file, *ids, *count * sizeof(**ids));
}

/* Read the fixed W3E header and bounded ground/cliff palettes in file order. */
static inline bool W3E_ReadHeader(handle_t file, war3map_t *map, handle_t (*alloc)(long)) {
    if (!file || !map || !alloc) return false;
    return W3E_ReadExact(file, &map->header, sizeof(map->header)) &&
           W3E_ReadExact(file, &map->version, sizeof(map->version)) &&
           W3E_ReadExact(file, &map->tileset, sizeof(map->tileset)) &&
           W3E_ReadExact(file, &map->custom, sizeof(map->custom)) &&
           W3E_ReadPalette(file, &map->num_grounds, &map->grounds, alloc) &&
           W3E_ReadPalette(file, &map->num_cliffs, &map->cliffs, alloc) &&
           W3E_ReadExact(file, &map->width, sizeof(map->width)) &&
           W3E_ReadExact(file, &map->height, sizeof(map->height)) &&
           W3E_ReadExact(file, &map->center, sizeof(map->center));
}

/* Validate dimensions and available packed bytes before allocating output.
 * A bounded stack buffer keeps the MPQ reads O(vertices / batch) while avoiding
 * a second full terrain-sized allocation during map transitions. */
static inline bool W3E_ReadVertexBlock(handle_t file, war3map_t *map,
                                        handle_t (*alloc)(long), void (*release)(handle_t)) {
    uint32_t const pos = file ? SFileSetFilePointer(file, 0, NULL, FILE_CURRENT) : SFILE_INVALID_POS;
    uint32_t const file_size = file ? SFileGetFileSize(file, NULL) : 0;
    uint64_t count, packed_size, decoded_size;
    uint8_t packed[W3E_VERTEX_BATCH * MAP_VERTEX_FILE_SIZE];
    war3mapVertex_t *vertices;

    if (!map || !alloc || !release || map->vertices || pos == SFILE_INVALID_POS || pos > file_size ||
        map->width < 2 || map->height < 2 ||
        map->width > W3E_MAX_VERTEX_DIMENSION || map->height > W3E_MAX_VERTEX_DIMENSION)
        return false;
    count = (uint64_t)map->width * map->height;
    packed_size = count * MAP_VERTEX_FILE_SIZE;
    decoded_size = count * sizeof(*vertices);
    if (count > W3E_MAX_VERTICES || packed_size > file_size - pos || decoded_size > LONG_MAX)
        return false;

    vertices = alloc((long)decoded_size);
    if (!vertices) return false;
    for (uint32_t first = 0; first < (uint32_t)count;) {
        uint32_t const n = MIN(W3E_VERTEX_BATCH, (uint32_t)count - first);
        uint32_t const needed = n * MAP_VERTEX_FILE_SIZE;
        uint32_t actual = 0;
        if (!SFileReadFile(file, packed, needed, &actual, NULL) || actual != needed) {
            release(vertices);
            return false;
        }
        for (uint32_t i = 0; i < n; i++)
            W3E_DecodeVertex(packed + i * MAP_VERTEX_FILE_SIZE, &vertices[first + i]);
        first += n;
    }
    map->vertices = vertices;
    return true;
}

#endif
