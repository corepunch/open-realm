#include "common/mpq.h"
#include "games/starcraft-2/common/sc2_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SC2MAP_MAX_ARCHIVES 16

static HANDLE sc2map_archives[SC2MAP_MAX_ARCHIVES];
static LPCSTR sc2map_data_dir = "";

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  sc2map [-data <StarCraft2-data-dir>] [-mpq <archive>]... <map.SC2Map|map-dir>\n"
            "  sc2map [-data <StarCraft2-data-dir>] [-mpq <archive>]... -map <map.SC2Map|map-dir>\n"
            "\n"
            "Examples:\n"
            "  sc2map -mpq build/tests/test-sc2.SC2Maps Maps/Test/Tiny.SC2Map\n"
            "  sc2map -data data/StarCraft2 Maps/Campaign/TRaynor01.SC2Map\n"
            "  sc2map games/starcraft-2/tests/resources-src/Maps/Test/Tiny.SC2Map\n");
}

static HANDLE sc2map_mem_alloc(long size) {
    void *mem = calloc(1, (size_t)(size ? size : 1));

    if (!mem) {
        fprintf(stderr, "sc2map: out of memory allocating %ld bytes\n", size);
        exit(1);
    }
    return mem;
}

static void sc2map_mem_free(HANDLE mem) {
    free(mem);
}

static HANDLE sc2map_read_disk_file(LPCSTR filename, LPDWORD size) {
    FILE *file;
    long file_size;
    LPBYTE data;
    struct stat st;

    if (size) *size = 0;
    if (!filename || !*filename)
        return NULL;
    if (stat(filename, &st) != 0 || !S_ISREG(st.st_mode))
        return NULL;
    file = fopen(filename, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = sc2map_mem_alloc(file_size ? file_size : 1);
    if (file_size > 0 && fread(data, 1, file_size, file) != (size_t)file_size) {
        fclose(file);
        sc2map_mem_free(data);
        return NULL;
    }
    fclose(file);
    if (size) *size = (DWORD)file_size;
    return data;
}

static HANDLE sc2map_read_archive_file(LPCSTR filename, LPDWORD size) {
    char path[MAX_PATHLEN];

    if (size) *size = 0;
    if (!filename || !*filename)
        return NULL;
    FOR_LOOP(i, SC2MAP_MAX_ARCHIVES) {
        HANDLE file;
        DWORD file_size;
        LPBYTE data;

        if (!sc2map_archives[i])
            continue;
        snprintf(path, sizeof(path), "%s", filename);
        for (char *p = path; *p; p++) if (*p == '/') *p = '\\';
        if (!SFileOpenFileEx(sc2map_archives[i], path, SFILE_OPEN_FROM_MPQ, &file)) {
            snprintf(path, sizeof(path), "%s", filename);
            for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
            if (!SFileOpenFileEx(sc2map_archives[i], path, SFILE_OPEN_FROM_MPQ, &file))
                continue;
        }
        file_size = SFileGetFileSize(file, NULL);
        data = sc2map_mem_alloc(file_size + 1);
        if (!SFileReadFile(file, data, file_size, NULL, NULL)) {
            SFileCloseFile(file);
            sc2map_mem_free(data);
            return NULL;
        }
        SFileCloseFile(file);
        data[file_size] = 0;
        if (size) *size = file_size;
        return data;
    }
    return NULL;
}

static HANDLE sc2map_read_file(LPCSTR filename, LPDWORD size) {
    HANDLE data = sc2map_read_archive_file(filename, size);

    if (data)
        return data;
    return sc2map_read_disk_file(filename, size);
}

static BOOL sc2map_add_archive(LPCSTR filename) {
    FOR_LOOP(i, SC2MAP_MAX_ARCHIVES) {
        if (sc2map_archives[i])
            continue;
        if (!SFileOpenArchive(filename, 0, 0, &sc2map_archives[i])) {
            fprintf(stderr, "sc2map: cannot open archive %s\n", filename);
            return false;
        }
        return true;
    }
    fprintf(stderr, "sc2map: too many archives\n");
    return false;
}

static LPCSTR sc2map_cvar_string(LPCSTR name, LPCSTR fallback) {
    if (name && !strcmp(name, "data"))
        return sc2map_data_dir && *sc2map_data_dir ? sc2map_data_dir : fallback;
    return fallback;
}

static void sc2map_close_archives(void) {
    FOR_LOOP(i, SC2MAP_MAX_ARCHIVES) {
        if (sc2map_archives[i]) {
            SFileCloseArchive(sc2map_archives[i]);
            sc2map_archives[i] = NULL;
        }
    }
}

int main(int argc, char **argv) {
    LPCSTR map = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-mpq")) {
            if (++i >= argc || !sc2map_add_archive(argv[i])) {
                usage();
                sc2map_close_archives();
                return 1;
            }
        } else if (!strcmp(argv[i], "-data")) {
            if (++i >= argc) {
                usage();
                sc2map_close_archives();
                return 1;
            }
            sc2map_data_dir = argv[i];
        } else if (!strcmp(argv[i], "-map")) {
            if (++i >= argc) {
                usage();
                sc2map_close_archives();
                return 1;
            }
            map = argv[i];
        } else if (argv[i][0] != '-' && !map) {
            map = argv[i];
        } else {
            usage();
            sc2map_close_archives();
            return 1;
        }
    }
    if (!map) {
        usage();
        sc2map_close_archives();
        return 1;
    }

    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = sc2map_read_file,
        .free_file = sc2map_mem_free,
        .mem_alloc = sc2map_mem_alloc,
        .mem_free = sc2map_mem_free,
        .cvar_string = sc2map_cvar_string,
    });
    if (!SC2_MapLoad(map)) {
        fprintf(stderr, "sc2map: failed to load %s\n", map);
        sc2map_close_archives();
        return 1;
    }
    SC2_MapDump(stdout, map);
    SC2_MapShutdown();
    sc2map_close_archives();
    return 0;
}
