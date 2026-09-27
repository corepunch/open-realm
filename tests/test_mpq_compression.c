#include "common/mpq.h"
#include "shared/test.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Our reader scans the whole table. Retail stops at the first free slot;
 * inspect the written table so a self-roundtrip cannot hide bad placement. */
TEST(mpq_compression, writer_retail_hash_probe) {
    char path[] = "/tmp/openrealm-mpq-hash-XXXXXX";
    char const *names[] = {"war3map.j", "war3map.w3e", "war3map.w3i"};
    uint32_t header[8], *hashes, count, i;
    handle_t archive;
    FILE *fp;
    int fd = mkstemp(path);

    T_ASSERT(fd >= 0);
    close(fd);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    for (i = 0; i < 3; i++)
        T_ASSERT(SFileAddFileFromBuffer(archive, names[i], "test", 4));
    T_ASSERT(SFileCloseArchive(archive));
    fp = fopen(path, "rb");
    T_ASSERT(fp != NULL);
    T_EQ(fread(header, sizeof(header), 1, fp), 1);
    count = header[6];
    hashes = malloc(count * 16);
    T_ASSERT(hashes != NULL);
    T_EQ(fseek(fp, header[4], SEEK_SET), 0);
    T_EQ(fread(hashes, 16, count, fp), count);
    fclose(fp);
    unlink(path);
    Mpq_TestDecryptBlock((uint8_t *)hashes, count * 16, 0xc3af3770);
    for (i = 0; i < 3; i++) {
        uint32_t slot = Mpq_TestHashString(names[i], 0) & (count - 1);
        uint32_t probes;
        for (probes = 0; probes < count; probes++, slot = (slot + 1) & (count - 1)) {
            uint32_t *entry = hashes + slot * 4;
            T_ASSERT(entry[3] != 0xffffffff);
            if (entry[3] == 0xffffffff)
                break;
            if (entry[0] == Mpq_TestHashString(names[i], 1) &&
                entry[1] == Mpq_TestHashString(names[i], 2)) {
                T_EQ(entry[3], i);
                break;
            }
        }
        T_ASSERT(probes < count);
    }
    free(hashes);
}

TEST(mpq_compression, writer_retail_sector_layout) {
    char path[] = "/tmp/openrealm-mpq-sectors-XXXXXX";
    uint8_t input[9001], output[9001];
    uint32_t header[8], block[4], offsets[4], read;
    handle_t archive, file;
    FILE *fp;
    int fd = mkstemp(path);

    T_ASSERT(fd >= 0);
    close(fd);
    memset(input, 'A', sizeof(input));
    input[4096] = 'B';
    input[8192] = 'C';
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "war3map.j", input, sizeof(input)));
    T_ASSERT(SFileCloseArchive(archive));
    fp = fopen(path, "rb");
    T_ASSERT(fp != NULL);
    T_EQ(fread(header, sizeof(header), 1, fp), 1);
    T_EQ(fseek(fp, header[5], SEEK_SET), 0);
    T_EQ(fread(block, sizeof(block), 1, fp), 1);
    Mpq_TestDecryptBlock((uint8_t *)block, sizeof(block), 0xec83b3a3);
    T_EQ(block[3], 0x80000200u); /* sector-compressed, not SINGLE_UNIT */
    T_EQ(fseek(fp, block[0], SEEK_SET), 0);
    T_EQ(fread(offsets, sizeof(offsets), 1, fp), 1);
    fclose(fp);
    T_EQ(offsets[0], sizeof(offsets));
    T_ASSERT(offsets[0] < offsets[1] && offsets[1] < offsets[2]);
    T_ASSERT(offsets[2] < offsets[3]);
    T_EQ(offsets[3], block[1]);
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    T_ASSERT(SFileOpenFileEx(archive, "war3map.j", 0, &file));
    T_ASSERT(SFileReadFile(file, output, sizeof(output), &read, NULL));
    T_EQ(read, sizeof(input));
    T_EQ(memcmp(input, output, sizeof(input)), 0);
    SFileCloseFile(file);
    SFileCloseArchive(archive);
    unlink(path);
}

/* Mark Adler's blast.c reference stream, wrapped in the MPQ method byte. */
static uint8_t const pkware_sector[] = { 0x08, 0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f };

TEST(mpq_compression, pkware_sector) {
    uint8_t out[14] = {0};
    uint32_t size = 0;
    T_ASSERT(Mpq_TestDecompressSector(pkware_sector, sizeof(pkware_sector), out, 13, &size));
    T_EQ(size, 13);
    T_STREQ((char *)out, "AIAIAIAIAIAIA");
}

TEST(mpq_compression, pkware_rejects_short_input_and_output_overflow) {
    uint8_t out[14] = {0};
    uint32_t size = 0;
    out[12] = 0x55;
    T_ASSERT(!Mpq_TestDecompressSector(pkware_sector, sizeof(pkware_sector), out, 12, &size));
    T_EQ(out[12], 0x55);
    T_ASSERT(!Mpq_TestDecompressSector(pkware_sector, 5, out, sizeof(out), &size));
}
