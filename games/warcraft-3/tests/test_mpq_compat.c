#include "common/mpq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MPQ_HASH_NAME_A 1
#define MPQ_HASH_NAME_B 2
#define MPQ_HASH_FILE_KEY 3
#define MPQ_KEY_HASH_TABLE 0xC3AF3770u
#define MPQ_KEY_BLOCK_TABLE 0xEC83B3A3u
#define MPQ_HASH_ENTRY_FREE 0xFFFFFFFFu
#define MPQ_FILE_COMPRESS 0x00000200u
#define MPQ_FILE_ENCRYPTED 0x00010000u
#define MPQ_FILE_EXISTS 0x80000000u
#define MPQ_LARGE_SHIFT 13 // >12 so sector_size = 4 MiB exceeds the old 64 KiB clamp
#define MPQ_LARGE_FILE_SIZE 5001u // >4096 and size%4==1 so EncryptBlock leaves 1 trailing byte

#pragma pack(push, 1)
typedef struct {
    uint32_t dwID;
    uint32_t dwHeaderSize;
    uint32_t dwArchiveSize;
    uint16_t wFormatVersion;
    uint16_t wSectorSizeShift;
    uint32_t dwHashTablePos;
    uint32_t dwBlockTablePos;
    uint32_t dwHashTableSize;
    uint32_t dwBlockTableSize;
} testMpqHeader_t;

typedef struct {
    uint32_t dwNameHash1;
    uint32_t dwNameHash2;
    uint16_t wLocale;
    uint8_t bPlatform;
    uint8_t bFlags;
    uint32_t dwBlockIndex;
} testMpqHash_t;

typedef struct {
    uint32_t dwBlockOffset;
    uint32_t dwBlockSize;
    uint32_t dwFileSize;
    uint32_t dwFlags;
} testMpqBlock_t;
#pragma pack(pop)

static void fail(char const *msg)
{
    fprintf(stderr, "test_mpq_compat: %s\n", msg);
    exit(1);
}

static char const *resolve_mpq_path(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-mpq=", 5) == 0) {
            return argv[i] + 5;
        }
    }

    return "data/Warcraft III/War3.mpq";
}

static char const *resolve_dota_path(int argc, char **argv)
{
    static char const *const candidates[] = {
        "data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x",
        "/Users/igor/Developer/openwarcraft3/data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x",
        NULL
    };
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-dota=", 6) == 0)
            return argv[i] + 6;
    }
    for (i = 0; candidates[i]; i++) {
        if (access(candidates[i], R_OK) == 0)
            return candidates[i];
    }
    return NULL;
}

/* Build an in-memory MPQ with one compressed+encrypted member and wSectorSizeShift>12. */
static uint8_t *build_large_sector_archive(uint32_t *out_size, uint8_t *expected, uint32_t expected_size)
{
    enum { HASH_SIZE = 16 };
    char const *name = "big.bin";
    uint32_t sector_table_bytes = 2 * sizeof(uint32_t);
    uint32_t block_offset = sizeof(testMpqHeader_t);
    uint32_t block_size = sector_table_bytes + expected_size;
    uint32_t hash_pos = block_offset + block_size;
    uint32_t block_pos = hash_pos + HASH_SIZE * sizeof(testMpqHash_t);
    uint32_t archive_size = block_pos + sizeof(testMpqBlock_t);
    uint8_t *blob;
    testMpqHeader_t *hdr;
    testMpqHash_t *hashes;
    testMpqBlock_t *block;
    uint32_t *offsets;
    uint8_t *sector;
    uint32_t file_key;
    uint32_t slot;
    uint32_t i;

    if (expected_size != MPQ_LARGE_FILE_SIZE)
        fail("large-sector fixture size mismatch");

    blob = (uint8_t *)calloc(1, archive_size);
    if (!blob)
        fail("calloc failed for large-sector archive");

    for (i = 0; i < expected_size; i++)
        expected[i] = (uint8_t)((i * 17u + 3u) & 0xFFu);
    memcpy(expected, "W3E!", 4);
    expected[expected_size - 1] = 0xA5; // trailing non-uint32_t byte must survive encrypt/decrypt

    hdr = (testMpqHeader_t *)blob;
    hdr->dwID = 0x1A51504Du;
    hdr->dwHeaderSize = sizeof(*hdr);
    hdr->dwArchiveSize = archive_size;
    hdr->wFormatVersion = 0;
    hdr->wSectorSizeShift = MPQ_LARGE_SHIFT;
    hdr->dwHashTablePos = hash_pos;
    hdr->dwBlockTablePos = block_pos;
    hdr->dwHashTableSize = HASH_SIZE;
    hdr->dwBlockTableSize = 1;

    offsets = (uint32_t *)(blob + block_offset);
    offsets[0] = sector_table_bytes;
    offsets[1] = sector_table_bytes + expected_size;
    sector = blob + block_offset + sector_table_bytes;
    memcpy(sector, expected, expected_size);

    file_key = Mpq_TestHashString(name, MPQ_HASH_FILE_KEY);
    Mpq_TestEncryptBlock((uint8_t *)offsets, sector_table_bytes, file_key - 1);
    Mpq_TestEncryptBlock(sector, expected_size, file_key);

    hashes = (testMpqHash_t *)(blob + hash_pos);
    for (i = 0; i < HASH_SIZE; i++) {
        hashes[i].dwNameHash1 = 0xFFFFFFFFu;
        hashes[i].dwNameHash2 = 0xFFFFFFFFu;
        hashes[i].wLocale = 0xFFFF;
        hashes[i].bPlatform = 0xFF;
        hashes[i].bFlags = 0xFF;
        hashes[i].dwBlockIndex = MPQ_HASH_ENTRY_FREE;
    }
    slot = Mpq_TestHashString(name, MPQ_HASH_NAME_A) & (HASH_SIZE - 1);
    hashes[slot].dwNameHash1 = Mpq_TestHashString(name, MPQ_HASH_NAME_A);
    hashes[slot].dwNameHash2 = Mpq_TestHashString(name, MPQ_HASH_NAME_B);
    hashes[slot].wLocale = 0;
    hashes[slot].bPlatform = 0;
    hashes[slot].bFlags = 0;
    hashes[slot].dwBlockIndex = 0;
    Mpq_TestEncryptBlock((uint8_t *)hashes, HASH_SIZE * sizeof(*hashes), MPQ_KEY_HASH_TABLE);

    block = (testMpqBlock_t *)(blob + block_pos);
    block->dwBlockOffset = block_offset;
    block->dwBlockSize = block_size;
    block->dwFileSize = expected_size;
    block->dwFlags = MPQ_FILE_EXISTS | MPQ_FILE_COMPRESS | MPQ_FILE_ENCRYPTED;
    Mpq_TestEncryptBlock((uint8_t *)block, sizeof(*block), MPQ_KEY_BLOCK_TABLE);

    *out_size = archive_size;
    return blob;
}

static void test_large_sector_archive(void)
{
    uint8_t expected[MPQ_LARGE_FILE_SIZE];
    uint8_t *got;
    uint8_t *archive_data;
    uint32_t archive_size = 0;
    uint32_t bytes_read = 0;
    handle_t archive;
    handle_t file;
    uint32_t i;

    archive_data = build_large_sector_archive(&archive_size, expected, sizeof(expected));
    if (!SFileOpenArchiveFromMemory(archive_data, archive_size, 0, &archive)) {
        free(archive_data);
        fail("SFileOpenArchiveFromMemory failed for wSectorSizeShift>12 archive");
    }
    if (!SFileOpenFileEx(archive, "big.bin", SFILE_OPEN_FROM_MPQ, &file)) {
        SFileCloseArchive(archive);
        free(archive_data);
        fail("SFileOpenFileEx failed for compressed+encrypted member with large sectors");
    }
    if (SFileGetFileSize(file, NULL) != sizeof(expected)) {
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        free(archive_data);
        fail("large-sector member has unexpected size");
    }
    got = (uint8_t *)malloc(sizeof(expected));
    if (!got || !SFileReadFile(file, got, sizeof(expected), &bytes_read, NULL) || bytes_read != sizeof(expected)) {
        free(got);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        free(archive_data);
        fail("SFileReadFile failed for large-sector member");
    }
    if (memcmp(got, expected, sizeof(expected))) {
        free(got);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        free(archive_data);
        fail("large-sector member payload mismatch (including non-uint32_t tail byte)");
    }
    /* Spot-check the undecrypted tail survived: last byte was size%4 leftover. */
    if (got[sizeof(expected) - 1] != 0xA5) {
        free(got);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        free(archive_data);
        fail("encrypted sector leftover byte was corrupted");
    }
    for (i = 0; i < 4; i++) {
        if (got[i] != expected[i]) {
            free(got);
            SFileCloseFile(file);
            SFileCloseArchive(archive);
            free(archive_data);
            fail("large-sector member magic mismatch");
        }
    }
    free(got);
    SFileCloseFile(file);
    SFileCloseArchive(archive);
    free(archive_data);
}

static void test_dota_map_if_present(char const *dota_path)
{
    handle_t archive;
    handle_t file;
    uint8_t magic[4];
    uint32_t bytes_read = 0;

    if (!dota_path)
        return;
    if (!SFileOpenArchive(dota_path, 0, 0, &archive))
        fail("SFileOpenArchive failed for DotA map");
    if (!SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file)) {
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for DotA war3map.w3e (sector size clamp?)");
    }
    if (!SFileReadFile(file, magic, sizeof(magic), &bytes_read, NULL) || bytes_read != 4 || memcmp(magic, "W3E!", 4)) {
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("DotA war3map.w3e magic is not W3E!");
    }
    SFileCloseFile(file);
    SFileCloseArchive(archive);
    printf("test_mpq_compat: DotA war3map.w3e ok (%s)\n", dota_path);
}

int main(int argc, char **argv)
{
    static uint8_t const adpcm_mono[] = { 0x40, 0, 0, 0x34, 0x12 };
    static uint8_t const adpcm_stereo[] = { 0x80, 0, 0, 0x34, 0x12, 0x78, 0x56 };
    char const *mpq_path = resolve_mpq_path(argc, argv);
    char const *dota_path = resolve_dota_path(argc, argv);
    handle_t archive;
    handle_t file;
    sfileFindData_t find_data;
    handle_t find;
    uint32_t bytes_read;
    uint32_t size_low;
    uint32_t size_high;
    uint8_t header[128];
    int32_t dist_hi;
    char out_path[] = "/tmp/openwarcraft3_mpq_extract.bin";
    struct stat st;
    uint8_t *map_buffer;
    handle_t map_file;
    handle_t map_archive;
    handle_t map_info;
    handle_t nested_map_info;
    handle_t owned_map_info;
    uint32_t map_size;
    uint32_t map_bytes_read;
    uint32_t sound_size;
    uint32_t riff_size;
    uint8_t *sound_data;
    uint8_t decoded[4];
    uint32_t decoded_size;

    if (!Mpq_TestDecompressSector(adpcm_mono, sizeof(adpcm_mono), decoded, 2, &decoded_size) ||
        decoded_size != 2 || decoded[0] != 0x34 || decoded[1] != 0x12)
        fail("pure mono ADPCM sector decode failed");
    if (!Mpq_TestDecompressSector(adpcm_stereo, sizeof(adpcm_stereo), decoded, 4, &decoded_size) ||
        decoded_size != 4 || memcmp(decoded, "\x34\x12\x78\x56", 4))
        fail("pure stereo ADPCM sector decode failed");

    test_large_sector_archive();
    test_dota_map_if_present(dota_path);

    if (!SFileOpenArchive(mpq_path, 0, 0, &archive)) {
        fail("SFileOpenArchive failed");
    }

    if (!SFileOpenFileEx(archive, "ReplaceableTextures/Selection/SelectionCircleLarge.blp", SFILE_OPEN_FROM_MPQ, &file)) {
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for ReplaceableTextures/Selection/SelectionCircleLarge.blp");
    }

    size_low = SFileGetFileSize(file, &size_high);
    if (size_high != 0 || size_low < 256) {
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("SFileGetFileSize returned unexpected value");
    }

    if (!SFileReadFile(file, header, sizeof(header), &bytes_read, NULL) || bytes_read == 0) {
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("SFileReadFile failed");
    }

    dist_hi = 0;
    if (SFileSetFilePointer(file, 0, &dist_hi, FILE_BEGIN) != 0) {
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("SFileSetFilePointer failed to seek to start");
    }

    SFileCloseFile(file);

    if (!SFileExtractFile(archive, "ReplaceableTextures/Selection/SelectionCircleLarge.blp", out_path, 0)) {
        SFileCloseArchive(archive);
        fail("SFileExtractFile failed");
    }

    if (stat(out_path, &st) != 0 || st.st_size < 256) {
        unlink(out_path);
        SFileCloseArchive(archive);
        fail("extracted file invalid");
    }

    unlink(out_path);

    if (!SFileOpenFileEx(archive, "Units\\Human\\Footman\\FootmanWhat1.wav", SFILE_OPEN_FROM_MPQ, &file)) {
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for compressed unit sound");
    }
    sound_size = SFileGetFileSize(file, NULL);
    sound_data = (uint8_t *)malloc(sound_size);
    if (!sound_data || !SFileReadFile(file, sound_data, sound_size, &bytes_read, NULL) || bytes_read != sound_size) {
        free(sound_data);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("Huffman/ADPCM unit sound was not read in full");
    }
    if (sound_size < 12 || memcmp(sound_data, "RIFF", 4) || memcmp(sound_data + 8, "WAVE", 4)) {
        free(sound_data);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("Huffman/ADPCM unit sound has an invalid WAV header");
    }
    riff_size = sound_data[4] | (sound_data[5] << 8) | (sound_data[6] << 16) | (sound_data[7] << 24);
    if (riff_size + 8 != sound_size) {
        free(sound_data);
        SFileCloseFile(file);
        SFileCloseArchive(archive);
        fail("Huffman/ADPCM unit sound has an incomplete WAV payload");
    }
    free(sound_data);
    SFileCloseFile(file);

    find = SFileFindFirstFile(archive, "*", &find_data, NULL);
    if (!find) {
        SFileCloseArchive(archive);
        fail("SFileFindFirstFile failed for root");
    }

    if (!find_data.cFileName[0]) {
        SFileFindClose(find);
        SFileCloseArchive(archive);
        fail("SFILE_FIND_DATA is empty");
    }

    SFileFindClose(find);

    if (!SFileOpenFileEx(archive, "Maps\\Campaign\\Human02.w3m\\war3map.w3i", SFILE_OPEN_FROM_MPQ, &nested_map_info)) {
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for nested map path");
    }
    if (SFileGetFileSize(nested_map_info, NULL) < 32) {
        SFileCloseFile(nested_map_info);
        SFileCloseArchive(archive);
        fail("nested path war3map.w3i is unexpectedly small");
    }
    SFileCloseFile(nested_map_info);

    if (!SFileOpenFileEx(archive, "Maps\\Campaign\\Human02.w3m", SFILE_OPEN_FROM_MPQ, &map_file)) {
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for nested map");
    }
    map_size = SFileGetFileSize(map_file, NULL);
    map_buffer = (uint8_t *)malloc(map_size);
    if (!map_buffer) {
        SFileCloseFile(map_file);
        SFileCloseArchive(archive);
        fail("malloc failed for nested map");
    }
    if (!SFileReadFile(map_file, map_buffer, map_size, &map_bytes_read, NULL) || map_bytes_read != map_size) {
        free(map_buffer);
        SFileCloseFile(map_file);
        SFileCloseArchive(archive);
        fail("SFileReadFile failed for nested map");
    }
    SFileCloseFile(map_file);

    if (!SFileOpenFileFromArchiveMemory(map_buffer, map_size, "war3map.w3i", SFILE_OPEN_FROM_MPQ, &owned_map_info)) {
        free(map_buffer);
        SFileCloseArchive(archive);
        fail("SFileOpenFileFromArchiveMemory failed for war3map.w3i");
    }
    if (SFileGetFileSize(owned_map_info, NULL) < 32) {
        SFileCloseFile(owned_map_info);
        SFileCloseArchive(archive);
        fail("owned nested war3map.w3i is unexpectedly small");
    }
    SFileCloseFile(owned_map_info);

    map_buffer = (uint8_t *)malloc(map_size);
    if (!map_buffer) {
        SFileCloseArchive(archive);
        fail("second malloc failed for nested map");
    }
    if (!SFileOpenFileEx(archive, "Maps\\Campaign\\Human02.w3m", SFILE_OPEN_FROM_MPQ, &map_file)) {
        free(map_buffer);
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for second nested map");
    }
    if (!SFileReadFile(map_file, map_buffer, map_size, &map_bytes_read, NULL) || map_bytes_read != map_size) {
        free(map_buffer);
        SFileCloseFile(map_file);
        SFileCloseArchive(archive);
        fail("SFileReadFile failed for second nested map");
    }
    SFileCloseFile(map_file);

    if (!SFileOpenArchiveFromMemory(map_buffer, map_size, 0, &map_archive)) {
        free(map_buffer);
        SFileCloseArchive(archive);
        fail("SFileOpenArchiveFromMemory failed for nested map");
    }
    if (!SFileOpenFileEx(map_archive, "war3map.w3i", SFILE_OPEN_FROM_MPQ, &map_info)) {
        SFileCloseArchive(map_archive);
        free(map_buffer);
        SFileCloseArchive(archive);
        fail("SFileOpenFileEx failed for nested war3map.w3i");
    }
    if (SFileGetFileSize(map_info, NULL) < 32) {
        SFileCloseFile(map_info);
        SFileCloseArchive(map_archive);
        free(map_buffer);
        SFileCloseArchive(archive);
        fail("nested war3map.w3i is unexpectedly small");
    }
    SFileCloseFile(map_info);
    SFileCloseArchive(map_archive);
    free(map_buffer);
    SFileCloseArchive(archive);

    printf("test_mpq_compat: ok (%s)\n", mpq_path);
    return 0;
}
