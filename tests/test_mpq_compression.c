#include "common/mpq.h"
#include "shared/test.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Retail encrypts uncompressed files per sector too; partial reads must decrypt
 * from the sector's start, not restart the cipher at the requested byte. */
TEST(mpq_compression, encrypted_raw_full_partial_and_seek_reads) {
    uint8_t input[9003], encrypted[9003], output[9003];
    uint32_t random = 0x12345678;
    for (uint32_t i = 0; i < sizeof(input); i++) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        input[i] = (uint8_t)random;
    }
    for (unsigned variant = 0; variant < 4; variant++) {
        char path[] = "/tmp/openrealm-mpq-encrypted-XXXXXX";
        uint32_t header[8], block[4], key, read;
        handle_t archive, file;
        int fd = mkstemp(path);
        T_ASSERT(fd >= 0); close(fd);
        T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
        T_ASSERT(SFileAddFileFromBuffer(archive, "image.blp", input, sizeof(input)));
        T_ASSERT(SFileCloseArchive(archive));
        FILE *fp = fopen(path, "r+b");
        T_NOT_NULL(fp);
        T_EQ(fread(header, sizeof(header), 1, fp), 1);
        T_EQ(fseek(fp, header[5], SEEK_SET), 0);
        T_EQ(fread(block, sizeof(block), 1, fp), 1);
        Mpq_TestDecryptBlock((uint8_t *)block, sizeof(block), 0xec83b3a3);
        T_EQ(block[3], 0x80000000u); /* incompressible, no offset table */
        block[3] |= 0x10000u; /* ENCRYPTED */
        if (variant & 1) block[3] |= 0x1000000u; /* SINGLE_UNIT */
        if (variant & 2) block[3] |= 0x20000u; /* FIX_KEY */
        key = Mpq_TestHashString("image.blp", 3);
        if (variant & 2) key = (key + block[0]) ^ sizeof(input);
        memcpy(encrypted, input, sizeof(input));
        if (variant & 1) Mpq_TestEncryptBlock(encrypted, sizeof(encrypted), key);
        else for (uint32_t pos = 0; pos < sizeof(encrypted); pos += 4096)
            Mpq_TestEncryptBlock(encrypted + pos, MIN(4096, sizeof(encrypted) - pos), key + pos / 4096);
        T_EQ(fseek(fp, block[0], SEEK_SET), 0);
        T_EQ(fwrite(encrypted, sizeof(encrypted), 1, fp), 1);
        Mpq_TestEncryptBlock((uint8_t *)block, sizeof(block), 0xec83b3a3);
        T_EQ(fseek(fp, header[5], SEEK_SET), 0);
        T_EQ(fwrite(block, sizeof(block), 1, fp), 1);
        fclose(fp);

        T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
        T_ASSERT(SFileOpenFileEx(archive, "image.blp", 0, &file));
        T_ASSERT(SFileReadFile(file, output, sizeof(output), &read, NULL));
        T_EQ(read, sizeof(input)); T_EQ(memcmp(output, input, read), 0);
        T_EQ(SFileSetFilePointer(file, 4093, NULL, SEEK_SET), 4093);
        T_ASSERT(SFileReadFile(file, output, 4109, &read, NULL));
        T_EQ(read, 4109); T_EQ(memcmp(output, input + 4093, read), 0);
        T_ASSERT(SFileReadFile(file, output, sizeof(output), &read, NULL));
        T_EQ(read, sizeof(input) - 8202); T_EQ(memcmp(output, input + 8202, read), 0);
        SFileCloseFile(file); SFileCloseArchive(archive); unlink(path);
    }
}

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
    /* Repeated seeks hit the same cached sector, then cross into a different
     * one and revisit the original. All bytes must match the source. */
    for (int i = 0; i < 3; i++) {
        T_EQ(SFileSetFilePointer(file, 4000, NULL, SEEK_SET), 4000);
        T_ASSERT(SFileReadFile(file, output, 64, &read, NULL));
        T_EQ(read, 64); T_EQ(memcmp(output, input + 4000, read), 0);
    }
    T_EQ(SFileSetFilePointer(file, 4090, NULL, SEEK_SET), 4090);
    T_ASSERT(SFileReadFile(file, output, 32, &read, NULL));
    T_EQ(read, 32); T_EQ(memcmp(output, input + 4090, read), 0);
    T_EQ(SFileSetFilePointer(file, 8188, NULL, SEEK_SET), 8188);
    T_ASSERT(SFileReadFile(file, output, 32, &read, NULL));
    T_EQ(read, 32); T_EQ(memcmp(output, input + 8188, read), 0);
    T_EQ(SFileSetFilePointer(file, 4000, NULL, SEEK_SET), 4000);
    T_ASSERT(SFileReadFile(file, output, 64, &read, NULL));
    T_EQ(read, 64); T_EQ(memcmp(output, input + 4000, read), 0);
    SFileCloseFile(file);
    T_ASSERT(SFileOpenFileEx(archive, "war3map.j", 0, &file));
    T_ASSERT(SFileReadFile(file, output, 64, &read, NULL));
    T_EQ(read, 64); T_EQ(memcmp(output, input, read), 0);
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
