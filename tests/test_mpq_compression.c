#include "common/mpq.h"
#include "shared/test.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

TEST(mpq_compression, distinct_missing_names_do_not_scan_archive_hash_table) {
    char path[] = "/tmp/openrealm-mpq-index-XXXXXX", name[64];
    handle_t archive, file;
    int fd = mkstemp(path);
    T_ASSERT(fd >= 0); close(fd);
    T_ASSERT(SFileCreateArchive(path, 0, 8192, &archive));
    FOR_LOOP(i, 1024) {
        snprintf(name, sizeof(name), "Units\\Fixture\\Model%u.mdx", i);
        T_ASSERT(SFileAddFileFromBuffer(archive, name, &i, sizeof(i)));
    }
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    uint32_t probes = Mpq_TestLookupProbes();
    FOR_LOOP(i, 128) {
        snprintf(name, sizeof(name), "Units\\Fixture\\Missing%u.mdx", i);
        T_ASSERT(!SFileOpenFileEx(archive, name, 0, &file));
    }
    T_ASSERT(Mpq_TestLookupProbes() - probes < 1024);
    /* The index must also retain positive block identity and canonical keys. */
    FOR_LOOP(i, 1024) {
        uint32_t actual = UINT32_MAX, read = 0;
        snprintf(name, sizeof(name), "units/fixture/model%u.mdx", i);
        T_ASSERT(SFileOpenFileEx(archive, name, 0, &file));
        T_ASSERT(SFileReadFile(file, &actual, sizeof(actual), &read, NULL));
        T_EQ(read, sizeof(actual)); T_EQ(actual, i);
        T_ASSERT(SFileCloseFile(file));
    }
    T_ASSERT(SFileCloseArchive(archive)); unlink(path);
}

TEST(mpq_compression, absent_names_are_cached_per_archive) {
    char path[] = "/tmp/openrealm-mpq-missing-XXXXXX";
    handle_t archive, file;
    uint32_t scans;
    int fd = mkstemp(path);

    T_ASSERT(fd >= 0); close(fd);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "Units\\Archer\\Death.wav", "test", 4));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    T_ASSERT(!SFileOpenFileEx(archive, "Units/Archer/Death1.wav", 0, &file));
    scans = Mpq_TestLookupScans();
    for (unsigned i = 0; i < 1024; i++)
        T_ASSERT(!SFileOpenFileEx(archive, "UNITS\\ARCHER\\DEATH1.WAV", 0, &file));
    T_EQ(Mpq_TestLookupScans(), scans);
    T_ASSERT(SFileOpenFileEx(archive, "Units/Archer/Death.wav", 0, &file));
    T_ASSERT(SFileCloseFile(file));
    T_ASSERT(SFileCloseArchive(archive));

    /* A new archive at the same pathname must not inherit the old absence. */
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "Units\\Archer\\Death1.wav", "new", 3));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
    T_ASSERT(SFileOpenFileEx(archive, "units/archer/death1.wav", 0, &file));
    T_EQ(SFileGetFileSize(file, NULL), 3);
    T_ASSERT(SFileCloseFile(file)); T_ASSERT(SFileCloseArchive(archive));
    unlink(path);
}

TEST(mpq_compression, indexed_lookup_retains_probe_and_first_slot_precedence) {
    char path[] = "/tmp/openrealm-mpq-duplicate-XXXXXX", name[32];
    uint32_t values[] = {11, 22}, header[8], count, start;
    handle_t archive, file;
    int fd = mkstemp(path);
    T_ASSERT(fd >= 0); close(fd);
    T_ASSERT(SFileCreateArchive(path, 0, 16, &archive));
    T_ASSERT(SFileAddFileFromBuffer(archive, "first", values, sizeof(uint32_t)));
    T_ASSERT(SFileAddFileFromBuffer(archive, "second", values + 1, sizeof(uint32_t)));
    T_ASSERT(SFileCloseArchive(archive));
    FILE *fp = fopen(path, "r+b"); T_NOT_NULL(fp);
    T_EQ(fread(header, sizeof(header), 1, fp), 1);
    count = header[6];
    uint32_t *hashes = malloc(count * 16); T_NOT_NULL(hashes);
    FOR_LOOP(shape, 2) {
        count = shape ? 15 : 16;
        uint32_t step = shape ? 2 : 1;
        header[6] = count;
        T_EQ(fseek(fp, 0, SEEK_SET), 0);
        T_EQ(fwrite(header, sizeof(header), 1, fp), 1);
        for (uint32_t i = 0;; i++) {
            snprintf(name, sizeof(name), "duplicate%u", i);
            start = Mpq_TestHashString(name, 1) & (count - 1);
            if (start >= 3 && start < count - step) break;
        }
        FOR_LOOP(mode, 5) {
            memset(hashes, 0xff, count * 16);
            if (mode >= 3) FOR_LOOP(i, count) {
                hashes[i * 4] = 0; hashes[i * 4 + 1] = 0;
                hashes[i * 4 + 3] = mode == 4 ? 0xfffffffeu : 0;
            }
            uint32_t first = mode == 2 ? 1 : 0, second = start + (mode != 1) * step;
            hashes[first * 4] = hashes[second * 4] = Mpq_TestHashString(name, 1);
            hashes[first * 4 + 1] = hashes[second * 4 + 1] = Mpq_TestHashString(name, 2);
            hashes[first * 4 + 2] = 0; hashes[first * 4 + 3] = 0;
            hashes[second * 4 + 2] = 1; hashes[second * 4 + 3] = 1;
            if (mode == 2) {
                memcpy(hashes, hashes + 4, 16); hashes[3] = 0xfffffffeu;
            }
            Mpq_TestEncryptBlock((uint8_t *)hashes, count * 16, 0xc3af3770);
            T_EQ(fseek(fp, header[4], SEEK_SET), 0);
            T_EQ(fwrite(hashes, 16, count, fp), count); fflush(fp);
            T_ASSERT(SFileOpenArchive(path, 0, 0, &archive));
            T_ASSERT(SFileOpenFileEx(archive, name, 0, &file));
            uint32_t actual = 0, read = 0;
            T_ASSERT(SFileReadFile(file, &actual, sizeof(actual), &read, NULL));
            T_EQ(read, sizeof(actual)); T_EQ(actual, mode == 1 || mode >= 3 ? 22 : 11);
            T_ASSERT(SFileCloseFile(file)); T_ASSERT(SFileCloseArchive(archive));
        }
    }
    free(hashes); fclose(fp); unlink(path);
}

/* Our reader retains the old full-scan choice. Retail stops at a free slot;
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

TEST(mpq_compression, small_reads_reuse_file_sector_without_cross_file_aliasing) {
    char path[] = "/tmp/openrealm-mpq-small-reads-XXXXXX";
    uint8_t input[9001], output[32];
    handle_t archive, first, second;
    uint32_t reads, actual;
    int fd = mkstemp(path);
    T_ASSERT(fd >= 0); close(fd);
    memset(input,'A',sizeof(input)); input[8]='B'; input[4104]='C';
    T_ASSERT(SFileCreateArchive(path,0,16,&archive));
    T_ASSERT(SFileAddFileFromBuffer(archive,"first",input,sizeof(input)));
    memset(input,'Z',sizeof(input));
    T_ASSERT(SFileAddFileFromBuffer(archive,"second",input,sizeof(input)));
    T_ASSERT(SFileCloseArchive(archive));
    T_ASSERT(SFileOpenArchive(path,0,0,&archive));
    T_ASSERT(SFileOpenFileEx(archive,"first",0,&first));
    T_ASSERT(SFileOpenFileEx(archive,"second",0,&second));
    reads = Mpq_TestPayloadReads();
    FOR_LOOP(i,16) {
        T_EQ(SFileSetFilePointer(first,8,NULL,FILE_BEGIN),8);
        T_ASSERT(SFileReadFile(first,output,1,&actual,NULL));
        T_EQ(actual,1); T_EQ(output[0],'B');
        T_EQ(SFileSetFilePointer(second,8,NULL,FILE_BEGIN),8);
        T_ASSERT(SFileReadFile(second,output,1,&actual,NULL));
        T_EQ(actual,1); T_EQ(output[0],'Z');
    }
    T_EQ(Mpq_TestPayloadReads()-reads,2);
    T_EQ(SFileSetFilePointer(first,4104,NULL,FILE_BEGIN),4104);
    T_ASSERT(SFileReadFile(first,output,1,&actual,NULL)); T_EQ(output[0],'C');
    T_EQ(Mpq_TestPayloadReads()-reads,3);
    SFileCloseFile(first); SFileCloseFile(second); SFileCloseArchive(archive); unlink(path);
}

TEST(mpq_compression, single_unit_payload_is_decoded_once_per_open_file) {
    char path[]="/tmp/openrealm-mpq-single-reads-XXXXXX";
    uint8_t input[2001], byte;
    uint32_t header[8], block[4], actual, reads;
    handle_t archive,file;
    int fd=mkstemp(path);T_ASSERT(fd>=0);close(fd);
    memset(input,'A',sizeof(input));input[23]='B';
    T_ASSERT(SFileCreateArchive(path,0,16,&archive));
    T_ASSERT(SFileAddFileFromBuffer(archive,"single",input,sizeof(input)));
    T_ASSERT(SFileCloseArchive(archive));
    /* A one-sector member's method/payload is also a valid SINGLE_UNIT
     * member once its sector table is skipped in the block-table record. */
    FILE *fp=fopen(path,"r+b");T_NOT_NULL(fp);
    T_EQ(fread(header,sizeof(header),1,fp),1);
    T_EQ(fseek(fp,header[5],SEEK_SET),0);T_EQ(fread(block,sizeof(block),1,fp),1);
    Mpq_TestDecryptBlock((uint8_t *)block,sizeof(block),0xec83b3a3);
    block[0]+=8;block[1]-=8;block[3]|=0x01000000;
    Mpq_TestEncryptBlock((uint8_t *)block,sizeof(block),0xec83b3a3);
    T_EQ(fseek(fp,header[5],SEEK_SET),0);T_EQ(fwrite(block,sizeof(block),1,fp),1);fclose(fp);
    T_ASSERT(SFileOpenArchive(path,0,0,&archive));
    T_ASSERT(SFileOpenFileEx(archive,"single",0,&file));
    reads=Mpq_TestPayloadReads();
    FOR_LOOP(i,32) {
        T_EQ(SFileSetFilePointer(file,23,NULL,FILE_BEGIN),23);
        T_ASSERT(SFileReadFile(file,&byte,1,&actual,NULL));T_EQ(actual,1);T_EQ(byte,'B');
    }
    T_EQ(Mpq_TestPayloadReads()-reads,1);
    SFileCloseFile(file);SFileCloseArchive(archive);unlink(path);
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
