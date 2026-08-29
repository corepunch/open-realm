#include "common/mpq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void fail(const char *msg)
{
    fprintf(stderr, "test_mpq_compat: %s\n", msg);
    exit(1);
}

static const char *resolve_mpq_path(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-mpq=", 5) == 0) {
            return argv[i] + 5;
        }
    }

    return "data/Warcraft III/War3.mpq";
}

int main(int argc, char **argv)
{
    const char *mpq_path = resolve_mpq_path(argc, argv);
    HANDLE archive;
    HANDLE file;
    SFILE_FIND_DATA find_data;
    HANDLE find;
    DWORD bytes_read;
    DWORD size_low;
    DWORD size_high;
    BYTE header[128];
    LONG dist_hi;
    char out_path[] = "/tmp/openwarcraft3_mpq_extract.bin";
    struct stat st;
    BYTE *map_buffer;
    HANDLE map_file;
    HANDLE map_archive;
    HANDLE map_info;
    HANDLE nested_map_info;
    HANDLE owned_map_info;
    DWORD map_size;
    DWORD map_bytes_read;
    DWORD sound_size;
    DWORD riff_size;
    BYTE *sound_data;

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
    sound_data = (BYTE *)malloc(sound_size);
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
    map_buffer = (BYTE *)malloc(map_size);
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

    map_buffer = (BYTE *)malloc(map_size);
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
