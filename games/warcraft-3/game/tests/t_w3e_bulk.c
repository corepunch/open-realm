#ifdef BZ_TESTS
#include "shared/test.h"
#include "common/war3map_vertices.h"
#include <stdlib.h>

/* Match the engine allocation callback while keeping fixture ownership local. */
static handle_t Test_W3EAlloc(long size) {
    void *memory = malloc((size_t)size);
    if (memory) memset(memory, 0, (size_t)size);
    return memory;
}

/* Release memory returned by the fixture allocator. */
static void Test_W3EFree(handle_t memory) {
    free(memory);
}

/* Exercise the same bounded reader used by both CM_ReadHeightmap and the renderer.
 * Synthetic MPQs keep the test independent of installed retail data. */
static bool Test_ReadW3EVertices(cstring_t filename, uint8_t const *payload, uint32_t size,
                                 war3map_t *map) {
    handle_t archive = NULL, file = NULL;
    cstring_t path = Test_TempPath(filename);
    bool ok = false;
    if (!SFileCreateArchive(path, 0, 16, &archive)) return false;
    if (!SFileAddFileFromBuffer(archive, "war3map.w3e", payload, size)) {
        SFileCloseArchive(archive);
        return false;
    }
    SFileCloseArchive(archive);
    if (!SFileOpenArchive(path, 0, 0, &archive)) return false;
    if (SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file)) {
        ok = W3E_ReadVertexBlock(file, map, Test_W3EAlloc, Test_W3EFree);
        SFileCloseFile(file);
    }
    SFileCloseArchive(archive);
    return ok;
}

TEST(wc3_terrain, w3e_vertex_decodes_all_packed_flags) {
    uint8_t const raw[MAP_VERTEX_FILE_SIZE] = { 0x34, 0x12, 0x67, 0x55, 0xff, 0xb5, 0xa3 };
    war3mapVertex_t vertex;
    W3E_DecodeVertex(raw, &vertex);
    T_EQ(vertex.accurate_height, 0x1234);
    T_EQ(vertex.waterlevel, 0x1567);
    T_EQ(vertex.mapedge, 1);
    T_EQ(vertex.ground, 15);
    T_EQ(vertex.ramp, 1);
    T_EQ(vertex.blight, 1);
    T_EQ(vertex.water, 1);
    T_EQ(vertex.boundary, 1);
    T_EQ(vertex.groundVariation, 21);
    T_EQ(vertex.cliffVariation, 5);
    T_EQ(vertex.cliff, 10);
    T_EQ(vertex.level, 3);
    W3E_DecodeVertex((uint8_t[7]){ 0 }, &vertex);
    T_EQ(vertex.accurate_height, 0);
    T_EQ(vertex.waterlevel, 0);
    T_EQ(vertex.ramp, 0);
    T_EQ(vertex.mapedge, 0);
}

TEST(wc3_terrain, w3e_bulk_reader_crosses_batch_boundary) {
    enum { VERTICES = W3E_VERTEX_BATCH + 4 };
    uint8_t *payload = Test_W3EAlloc(VERTICES * MAP_VERTEX_FILE_SIZE);
    war3map_t map = { .width = 100, .height = 41 };
    war3mapVertex_t *vertices;
    payload[0] = 0x12;
    payload[W3E_VERTEX_BATCH * MAP_VERTEX_FILE_SIZE] = 0x56;
    payload[(VERTICES - 1) * MAP_VERTEX_FILE_SIZE] = 0x78;
    T_ASSERT(Test_ReadW3EVertices("w3e-batch.mpq", payload,
                                   VERTICES * MAP_VERTEX_FILE_SIZE, &map));
    vertices = map.vertices;
    T_NOT_NULL(vertices);
    T_EQ(vertices[0].accurate_height, 0x12);
    T_EQ(vertices[W3E_VERTEX_BATCH].accurate_height, 0x56);
    T_EQ(vertices[VERTICES - 1].accurate_height, 0x78);
    Test_W3EFree(map.vertices);
    Test_W3EFree(payload);
}


TEST(wc3_terrain, w3e_header_reads_palettes_and_vertices_from_current_offset) {
    uint8_t payload[80] = { 0 };
    uint8_t *cursor = payload;
    uint32_t const magic = MAKEFOURCC('W', '3', 'E', '!');
    uint32_t const version = 11, custom = 0, one = 1, width = 2, height = 2;
    uint32_t const ground = MAKEFOURCC('L', 'd', 'r', 't');
    uint32_t const cliff = MAKEFOURCC('C', 'L', 'f', 'f');
    vec2_t const center = { 128, 256 };
    war3map_t map = { 0 };
    handle_t archive = NULL, file = NULL;
    cstring_t path = Test_TempPath("w3e-header.mpq");

#define TEST_WRITE(value) do { memcpy(cursor, &(value), sizeof(value)); cursor += sizeof(value); } while (0)
    TEST_WRITE(magic);
    TEST_WRITE(version);
    *cursor++ = 'L';
    TEST_WRITE(custom);
    TEST_WRITE(one);
    TEST_WRITE(ground);
    TEST_WRITE(one);
    TEST_WRITE(cliff);
    TEST_WRITE(width);
    TEST_WRITE(height);
    TEST_WRITE(center);
#undef TEST_WRITE
    /* First vertex deliberately differs from header values. */
    *cursor++ = 0x34;
    *cursor++ = 0x12;
    cursor += 3 * MAP_VERTEX_FILE_SIZE + 5;
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3map.w3e", payload,
                                    (uint32_t)(cursor - payload)));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    T_ASSERT(SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file));
    T_ASSERT(W3E_ReadHeader(file, &map, Test_W3EAlloc));
    T_ASSERT(W3E_ReadVertexBlock(file, &map, Test_W3EAlloc, Test_W3EFree));
    T_EQ(map.width, 2);
    T_EQ(map.height, 2);
    T_EQ(map.num_grounds, 1);
    T_EQ(map.grounds[0], ground);
    T_EQ(map.cliffs[0], cliff);
    T_EQ(((war3mapVertex_t *)map.vertices)[0].accurate_height, 0x1234);
    Test_W3EFree(map.vertices);
    Test_W3EFree(map.grounds);
    Test_W3EFree(map.cliffs);
    SFileCloseFile(file);
    SFileCloseArchive(archive);
}

TEST(wc3_terrain, w3e_bulk_reader_rejects_truncated_and_invalid_geometry) {
    uint8_t const payload[3 * MAP_VERTEX_FILE_SIZE] = { 0 };
    war3map_t map = { .width = 2, .height = 2 };
    T_ASSERT(!Test_ReadW3EVertices("w3e-short.mpq", payload, sizeof(payload), &map));
    T_NULL(map.vertices);
    map.width = 0;
    T_ASSERT(!Test_ReadW3EVertices("w3e-zero.mpq", payload, sizeof(payload), &map));
    T_NULL(map.vertices);
    map.width = W3E_MAX_VERTEX_DIMENSION + 1;
    T_ASSERT(!Test_ReadW3EVertices("w3e-wide.mpq", payload, sizeof(payload), &map));
    T_NULL(map.vertices);
    map.width = W3E_MAX_VERTEX_DIMENSION;
    map.height = W3E_MAX_VERTEX_DIMENSION;
    T_ASSERT(!Test_ReadW3EVertices("w3e-overflow.mpq", payload, sizeof(payload), &map));
    T_NULL(map.vertices);
}
#endif
