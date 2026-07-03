#include "common/mpq.h"
#include "tool_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATHSTR
#define PATHSTR char[512]
#endif

typedef struct {
    char name[512];
    bool is_dir;
} entry_t;

typedef struct {
    entry_t *items;
    size_t count;
    size_t cap;
} entry_list_t;

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  mpqtool -mpq <archive.mpq> ls [path]\n"
        "  mpqtool -mpq <archive.mpq> cat <file>\n"
        "  mpqtool -mpq <archive.mpq> info <file>\n"
        "  mpqtool -mpq <archive.mpq> imginfo <file>\n"
        "  mpqtool -mpq <archive.mpq> grep <text> [path]\n"
    "  mpqtool -mpq <archive.mpq> create [max-files]\n"
    "  mpqtool -mpq <archive.mpq> pack <src> <archive-file> [<src> <archive-file> ...]\n"
    "  mpqtool wow-install [-strip-data-prefix] <output-dir> <disc1.mpq> <disc2.mpq> <disc3.mpq> <disc4.mpq>\n"
        "\n"
        "Notes:\n"
        "  create, pack, and wow-install create new archives, overwriting existing targets.\n"
        "  grep searches text content of archive files (case-insensitive); skips binary files.\n"
        "\n"
        "Examples:\n"
        "  mpqtool -mpq War3.mpq ls\n"
        "  mpqtool -mpq War3.mpq ls Units\n"
        "  mpqtool -mpq War3.mpq cat Units/UnitData.slk\n"
    "  mpqtool -mpq War3.mpq info Maps/Campaign/Human02.w3m\n"
    "  mpqtool -mpq tests.mpq pack ./basic.fdf TestUI/Frames/basic.fdf ./checker.blp TestUI/Textures/checker.blp\n"
    "  mpqtool wow-install data/world-of-warcraft/installed data/world-of-warcraft/WoWDisc1.mpq data/world-of-warcraft/WoWDisc2.mpq data/world-of-warcraft/WoWDisc3.mpq data/world-of-warcraft/WoWDisc4.mpq\n"
    "  mpqtool -mpq War3.mpq imginfo UI/Widgets/Glues/GlueScreen-Button1-Border.blp\n"
    "  mpqtool -mpq data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data grep UI/MenuBarButtonNormal GameData\n");
}

static unsigned short rd_u16le(const unsigned char *p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned int rd_u32le(const unsigned char *p) {
    return (unsigned int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static const char *blp_content_name(unsigned int content) {
    switch (content) {
        case 0: return "JPEG";
        case 1: return "DIRECT_PALETTED";
        default: return "UNKNOWN";
    }
}

static const char *blp2_encoding_name(unsigned int encoding) {
    switch (encoding) {
        case 0: return "JPEG";
        case 1: return "PALETTIZED";
        case 2: return "DXT";
        case 3: return "BGRA";
        default: return "UNKNOWN";
    }
}

static int cmd_imginfo(HANDLE archive, const char *file_path) {
    HANDLE file;
    unsigned char header[512];
    DWORD read_bytes = 0;
    DWORD file_size = 0;

    if (!SFileOpenFileEx(archive, file_path, SFILE_OPEN_FROM_MPQ, &file)) {
        fprintf(stderr, "Cannot open MPQ file: %s\n", file_path);
        return 1;
    }

    file_size = SFileGetFileSize(file, NULL);
    if (!SFileReadFile(file, header, sizeof(header), &read_bytes, NULL) || read_bytes < 16) {
        fprintf(stderr, "Failed reading file header: %s\n", file_path);
        SFileCloseFile(file);
        return 1;
    }

    printf("file=%s\n", file_path);
    printf("size=%u\n", (unsigned)file_size);

    if (read_bytes >= 4 && memcmp(header, "BLP1", 4) == 0) {
        unsigned int content;
        unsigned int alpha_bits;
        unsigned int width;
        unsigned int height;
        unsigned int extra;
        unsigned int has_mips;
        unsigned int mip_count = 0;

        if (read_bytes < 0x9C) {
            fprintf(stderr, "BLP1 header too small in %s\n", file_path);
            SFileCloseFile(file);
            return 1;
        }

        content = rd_u32le(header + 0x04);
        alpha_bits = rd_u32le(header + 0x08);
        width = rd_u32le(header + 0x0C);
        height = rd_u32le(header + 0x10);
        extra = rd_u32le(header + 0x14);
        has_mips = rd_u32le(header + 0x18);

        for (unsigned int i = 0; i < 16; i++) {
            unsigned int off = rd_u32le(header + 0x1C + i * 4);
            unsigned int sz = rd_u32le(header + 0x5C + i * 4);
            if (off != 0 && sz != 0) {
                mip_count++;
            }
        }

        printf("type=BLP1\n");
        printf("content=%u (%s)\n", content, blp_content_name(content));
        printf("alpha_bits=%u\n", alpha_bits);
        printf("width=%u\n", width);
        printf("height=%u\n", height);
        printf("extra=%u\n", extra);
        printf("has_mipmaps=%u\n", has_mips ? 1u : 0u);
        printf("mip_levels_present=%u\n", mip_count);

        for (unsigned int i = 0; i < 16; i++) {
            unsigned int off = rd_u32le(header + 0x1C + i * 4);
            unsigned int sz = rd_u32le(header + 0x5C + i * 4);
            if (off == 0 && sz == 0) {
                continue;
            }
            printf("mip[%u].offset=%u mip[%u].size=%u\n", i, off, i, sz);
        }
    } else if (read_bytes >= 4 && memcmp(header, "BLP2", 4) == 0) {
        unsigned int version;
        unsigned int encoding;
        unsigned int alpha_bits;
        unsigned int alpha_type;
        unsigned int has_mips;
        unsigned int width;
        unsigned int height;
        unsigned int mip_count = 0;

        if (read_bytes < 0x94) {
            fprintf(stderr, "BLP2 header too small in %s\n", file_path);
            SFileCloseFile(file);
            return 1;
        }

        version = rd_u32le(header + 0x04);
        encoding = header[0x08];
        alpha_bits = header[0x09];
        alpha_type = header[0x0A];
        has_mips = header[0x0B];
        width = rd_u32le(header + 0x0C);
        height = rd_u32le(header + 0x10);

        for (unsigned int i = 0; i < 16; i++) {
            unsigned int off = rd_u32le(header + 0x14 + i * 4);
            unsigned int sz = rd_u32le(header + 0x54 + i * 4);
            if (off != 0 && sz != 0) {
                mip_count++;
            }
        }

        printf("type=BLP2\n");
        printf("version=%u\n", version);
        printf("encoding=%u (%s)\n", encoding, blp2_encoding_name(encoding));
        printf("alpha_bits=%u\n", alpha_bits);
        printf("alpha_type=%u\n", alpha_type);
        printf("width=%u\n", width);
        printf("height=%u\n", height);
        printf("has_mipmaps=%u\n", has_mips ? 1u : 0u);
        printf("mip_levels_present=%u\n", mip_count);

        for (unsigned int i = 0; i < 16; i++) {
            unsigned int off = rd_u32le(header + 0x14 + i * 4);
            unsigned int sz = rd_u32le(header + 0x54 + i * 4);
            if (off == 0 && sz == 0) {
                continue;
            }
            printf("mip[%u].offset=%u mip[%u].size=%u\n", i, off, i, sz);
        }
    } else if (read_bytes >= 8 &&
               header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') {
        printf("type=PNG\n");
        if (read_bytes >= 24) {
            unsigned int width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
            unsigned int height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
            printf("width=%u\n", width);
            printf("height=%u\n", height);
        }
    } else if (read_bytes >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        printf("type=JPEG\n");
    } else if (read_bytes >= 18) {
        unsigned int id_len = header[0];
        unsigned int image_type = header[2];
        unsigned int width = rd_u16le(header + 12);
        unsigned int height = rd_u16le(header + 14);
        unsigned int bpp = header[16];

        if (image_type != 0 && width > 0 && height > 0) {
            printf("type=TGA\n");
            printf("image_type=%u\n", image_type);
            printf("id_length=%u\n", id_len);
            printf("width=%u\n", width);
            printf("height=%u\n", height);
            printf("bits_per_pixel=%u\n", bpp);
        } else {
            printf("type=UNKNOWN\n");
        }
    } else {
        printf("type=UNKNOWN\n");
    }

    SFileCloseFile(file);
    return 0;
}

static bool starts_with_ci(const char *s, const char *prefix) {
#ifdef _WIN32
    return _strnicmp(s, prefix, strlen(prefix)) == 0;
#else
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
#endif
}

static void entries_add_or_merge(entry_list_t *list, const char *name, bool is_dir) {
    size_t i;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].name, name) == 0) {
            list->items[i].is_dir = list->items[i].is_dir || is_dir;
            return;
        }
    }

    if (list->count == list->cap) {
        size_t next = (list->cap == 0) ? 64 : list->cap * 2;
        entry_t *tmp = (entry_t *)realloc(list->items, next * sizeof(*tmp));
        if (!tmp) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
        list->items = tmp;
        list->cap = next;
    }

    strncpy(list->items[list->count].name, name, sizeof(list->items[list->count].name) - 1);
    list->items[list->count].name[sizeof(list->items[list->count].name) - 1] = '\0';
    list->items[list->count].is_dir = is_dir;
    list->count++;
}

static int entry_cmp(const void *a, const void *b) {
    const entry_t *ea = (const entry_t *)a;
    const entry_t *eb = (const entry_t *)b;
#ifdef _WIN32
    return _stricmp(ea->name, eb->name);
#else
    return strcasecmp(ea->name, eb->name);
#endif
}

static int cmd_cat(HANDLE archive, const char *file_path) {
    HANDLE file;
    unsigned char buf[64 * 1024];
    DWORD read_bytes = 0;

    if (!SFileOpenFileEx(archive, file_path, SFILE_OPEN_FROM_MPQ, &file)) {
        fprintf(stderr, "Cannot open MPQ file: %s\n", file_path);
        return 1;
    }

    while (SFileReadFile(file, buf, sizeof(buf), &read_bytes, NULL) && read_bytes > 0) {
        size_t wrote = fwrite(buf, 1, read_bytes, stdout);
        if (wrote != read_bytes) {
            fprintf(stderr, "Failed writing output\n");
            SFileCloseFile(file);
            return 1;
        }
    }

    SFileCloseFile(file);
    return 0;
}

static int cmd_info(HANDLE archive, const char *file_path) {
    SFILE_FIND_DATA fd;
    HANDLE hfind;

    hfind = SFileFindFirstFile(archive, file_path, &fd, NULL);
    if (!hfind) {
        fprintf(stderr, "Cannot find MPQ file: %s\n", file_path);
        return 1;
    }
    SFileFindClose(hfind);

    printf("file=%s\n", fd.cFileName);
    printf("size=%u\n", (unsigned)fd.dwFileSize);
    printf("compressed_size=%u\n", (unsigned)fd.dwCompSize);
    printf("flags=0x%08x\n", (unsigned)fd.dwFileFlags);
    printf("compressed=%s\n", (fd.dwFileFlags & 0x00000200u) ? "yes" : "no");
    printf("imploded=%s\n", (fd.dwFileFlags & 0x00000100u) ? "yes" : "no");
    printf("encrypted=%s\n", (fd.dwFileFlags & 0x00010000u) ? "yes" : "no");
    printf("single_unit=%s\n", (fd.dwFileFlags & 0x01000000u) ? "yes" : "no");
    printf("exists=%s\n", (fd.dwFileFlags & 0x80000000u) ? "yes" : "no");
    return 0;
}

static const char *mpq_strcasestr(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen) return hay;
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, nlen) == 0)
            return hay;
    return NULL;
}

static bool is_binary_ext(const char *filename) {
    static const char *const exts[] = {
        ".dds", ".blp", ".wav", ".ogg", ".mp3", ".m3", ".m3a",
        ".png", ".jpg", ".jpeg", ".tga", ".bmp", NULL,
    };
    const char *ext = Tool_PathExt(filename);
    for (int i = 0; exts[i]; i++)
        if (strcasecmp(ext, exts[i] + 1) == 0)
            return true;
    return false;
}

static int cmd_grep(HANDLE archive, const char *pattern, const char *path_prefix) {
    SFILE_FIND_DATA fd;
    HANDLE hfind;
    char prefix[512] = {0};
    size_t prefix_len = 0;
    int matches = 0;

    if (path_prefix && *path_prefix) {
        strncpy(prefix, path_prefix, sizeof(prefix) - 1);
        Tool_NormalizeSlashes(prefix, '\\');
        Tool_TrimEdgeSlashes(prefix);
        prefix_len = strlen(prefix);
    }

    hfind = SFileFindFirstFile(archive, "*", &fd, NULL);
    if (!hfind) {
        fprintf(stderr, "Unable to enumerate archive contents\n");
        return 1;
    }

    do {
        const char *full = fd.cFileName;
        HANDLE file;
        DWORD file_size, read_bytes;
        char *buf;

        if (prefix_len && !starts_with_ci(full, prefix))
            continue;
        if (is_binary_ext(full))
            continue;

        if (!SFileOpenFileEx(archive, full, SFILE_OPEN_FROM_MPQ, &file))
            continue;

        file_size = SFileGetFileSize(file, NULL);
        if (file_size == 0 || file_size > 8 * 1024 * 1024) {
            SFileCloseFile(file);
            continue;
        }

        buf = malloc(file_size + 1);
        if (!buf) { SFileCloseFile(file); continue; }

        if (SFileReadFile(file, buf, file_size, &read_bytes, NULL) && read_bytes > 0) {
            buf[read_bytes] = '\0';
            char display[512];
            strncpy(display, full, sizeof(display) - 1);
            display[sizeof(display) - 1] = '\0';
            Tool_NormalizeSlashes(display, '/');

            char *line = buf;
            int line_num = 1;
            while (*line) {
                char *nl = strchr(line, '\n');
                size_t llen = nl ? (size_t)(nl - line) : strlen(line);
                char saved = line[llen];
                line[llen] = '\0';
                if (mpq_strcasestr(line, pattern)) {
                    printf("%s:%d:%s\n", display, line_num, line);
                    matches++;
                }
                line[llen] = saved;
                if (!nl) break;
                line = nl + 1;
                line_num++;
            }
        }

        free(buf);
        SFileCloseFile(file);
    } while (SFileFindNextFile(hfind, &fd));

    SFileFindClose(hfind);
    return matches > 0 ? 0 : 1;
}

static int cmd_ls(HANDLE archive, const char *path) {
    SFILE_FIND_DATA fd;
    HANDLE hfind;
    char prefix[512];
    size_t prefix_len;
    entry_list_t out = { 0 };

    prefix[0] = '\0';
    if (path && *path) {
        strncpy(prefix, path, sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
        Tool_NormalizeSlashes(prefix, '\\');
        Tool_TrimEdgeSlashes(prefix);
    }

    prefix_len = strlen(prefix);
    hfind = SFileFindFirstFile(archive, "*", &fd, NULL);
    if (!hfind) {
        fprintf(stderr, "Unable to enumerate archive contents\n");
        return 1;
    }

    do {
        const char *full = fd.cFileName;
        const char *rest = full;

        if (prefix_len > 0) {
            if (!starts_with_ci(full, prefix)) {
                continue;
            }
            rest = full + prefix_len;
            if (*rest == '\\') {
                rest++;
            } else if (*rest != '\0') {
                continue;
            }
            if (*rest == '\0') {
                continue;
            }
        }

        {
            const char *slash = strchr(rest, '\\');
            char child[512];
            bool is_dir = (slash != NULL);
            size_t n = is_dir ? (size_t)(slash - rest) : strlen(rest);
            if (n == 0 || n >= sizeof(child)) {
                continue;
            }
            memcpy(child, rest, n);
            child[n] = '\0';
            entries_add_or_merge(&out, child, is_dir);
        }
    } while (SFileFindNextFile(hfind, &fd));

    SFileFindClose(hfind);

    qsort(out.items, out.count, sizeof(*out.items), entry_cmp);
    for (size_t i = 0; i < out.count; i++) {
        if (out.items[i].is_dir) {
            printf("%s/\n", out.items[i].name);
        } else {
            printf("%s\n", out.items[i].name);
        }
    }

    free(out.items);
    return 0;
}

static int cmd_create(const char *mpq_path, const char *arg)
{
    HANDLE archive;
    DWORD max_files = 16;

    if (arg && *arg) {
        char *end = NULL;
        unsigned long parsed = strtoul(arg, &end, 10);
        if (!end || *end != '\0' || parsed == 0 || parsed > 65535) {
            fprintf(stderr, "Invalid max-files value: %s\n", arg);
            return 1;
        }
        max_files = (DWORD)parsed;
    }

    if (!SFileCreateArchive(mpq_path, 0, max_files, &archive)) {
        fprintf(stderr, "Cannot create archive: %s\n", mpq_path);
        return 1;
    }
    if (!SFileCloseArchive(archive)) {
        fprintf(stderr, "Cannot finalize archive: %s\n", mpq_path);
        return 1;
    }
    return 0;
}

static int cmd_pack(const char *mpq_path, int pair_count, char **pairs)
{
    HANDLE archive;
    int i;

    if (pair_count <= 0 || (pair_count % 2) != 0) {
        fprintf(stderr, "pack requires <src> <archive-file> pairs\n");
        return 1;
    }

    if (!SFileCreateArchive(mpq_path, 0, (DWORD)(pair_count / 2 + 1), &archive)) {
        fprintf(stderr, "Cannot create archive: %s\n", mpq_path);
        return 1;
    }

    for (i = 0; i < pair_count; i += 2) {
        char path[512];
        strncpy(path, pairs[i + 1], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        Tool_NormalizeSlashes(path, '\\');
        Tool_TrimEdgeSlashes(path);
        if (!SFileAddFile(archive, pairs[i], path)) {
            fprintf(stderr, "Cannot add file %s as %s\n", pairs[i], path);
            SFileCloseArchive(archive);
            return 1;
        }
    }

    if (!SFileCloseArchive(archive)) {
        fprintf(stderr, "Cannot finalize archive: %s\n", mpq_path);
        return 1;
    }

    return 0;
}

typedef struct {
    char container[256];
    char path[512];
    HANDLE archive;
    unsigned int files;
} wow_target_t;

static void xml_unescape(char *s)
{
    char *out = s;

    while (*s) {
        if (strncmp(s, "&amp;", 5) == 0) {
            *out++ = '&';
            s += 5;
        } else if (strncmp(s, "&quot;", 6) == 0) {
            *out++ = '"';
            s += 6;
        } else if (strncmp(s, "&apos;", 6) == 0) {
            *out++ = '\'';
            s += 6;
        } else if (strncmp(s, "&lt;", 4) == 0) {
            *out++ = '<';
            s += 4;
        } else if (strncmp(s, "&gt;", 4) == 0) {
            *out++ = '>';
            s += 4;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static bool xml_attr(const char *line, const char *attr, char *out, size_t out_size)
{
    char pattern[64];
    const char *p;
    const char *end;
    size_t len;

    snprintf(pattern, sizeof(pattern), "%s=\"", attr);
    p = strstr(line, pattern);
    if (!p) {
        return false;
    }
    p += strlen(pattern);
    end = strchr(p, '"');
    if (!end) {
        return false;
    }
    len = (size_t)(end - p);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    xml_unescape(out);
    return true;
}

static int mkpath(const char *path)
{
    char tmp[512];
    size_t len;

    if (!path || !*path) {
        return 0;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return 1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        return 1;
    }
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char tmp[512];
    char *slash;

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    slash = strrchr(tmp, '/');
    if (!slash) {
        return 0;
    }
    *slash = '\0';
    return mkpath(tmp);
}

static bool read_archive_file(HANDLE archive, const char *path, BYTE **out_data, DWORD *out_size)
{
    HANDLE file;
    DWORD size;
    DWORD total = 0;
    BYTE *data;

    if (!SFileOpenFileEx(archive, path, SFILE_OPEN_FROM_MPQ, &file)) {
        return false;
    }

    size = SFileGetFileSize(file, NULL);
    data = (BYTE *)malloc(size ? size : 1);
    if (!data) {
        SFileCloseFile(file);
        return false;
    }

    while (total < size) {
        DWORD read_bytes = 0;
        DWORD chunk = size - total;
        if (!SFileReadFile(file, data + total, chunk, &read_bytes, NULL) || read_bytes == 0) {
            free(data);
            SFileCloseFile(file);
            return false;
        }
        total += read_bytes;
    }

    SFileCloseFile(file);
    *out_data = data;
    *out_size = size;
    return true;
}

static void archive_path_join(char *out, size_t out_size, const char *base, const char *from)
{
    snprintf(out, out_size, "%s%s", base ? base : "", from ? from : "");
    Tool_NormalizeSlashes(out, '\\');
    Tool_TrimEdgeSlashes(out);
}

static void output_path_for_container(char *out, size_t out_size, const char *out_dir, const char *container)
{
    char converted[256];

    strncpy(converted, container, sizeof(converted) - 1);
    converted[sizeof(converted) - 1] = '\0';
    Tool_NormalizeSlashes(converted, '/');
    Tool_TrimEdgeSlashes(converted);
    snprintf(out, out_size, "%s/%s", out_dir, converted);
}

static void strip_wow_data_prefix(char *container)
{
    if (starts_with_ci(container, "Data\\")) {
        memmove(container, container + 5, strlen(container + 5) + 1);
    }
}

static wow_target_t *get_wow_target(wow_target_t *targets, size_t *count, const char *out_dir, const char *container)
{
    size_t i;

    for (i = 0; i < *count; i++) {
        if (strcmp(targets[i].container, container) == 0) {
            return &targets[i];
        }
    }

    if (*count >= 32) {
        return NULL;
    }

    wow_target_t *target = &targets[(*count)++];
    memset(target, 0, sizeof(*target));
    strncpy(target->container, container, sizeof(target->container) - 1);
    output_path_for_container(target->path, sizeof(target->path), out_dir, container);
    if (ensure_parent_dir(target->path) != 0) {
        fprintf(stderr, "Cannot create output directory for %s\n", target->path);
        return NULL;
    }
    if (!SFileCreateArchive(target->path, 0, 65535, &target->archive)) {
        fprintf(stderr, "Cannot create archive: %s\n", target->path);
        return NULL;
    }
    fprintf(stderr, "creating %s\n", target->path);
    return target;
}

static int close_wow_targets(wow_target_t *targets, size_t count)
{
    int rc = 0;
    size_t i;

    for (i = 0; i < count; i++) {
        if (!targets[i].archive) {
            continue;
        }
        if (!SFileCloseArchive(targets[i].archive)) {
            fprintf(stderr, "Cannot finalize archive: %s\n", targets[i].path);
            rc = 1;
        } else {
            fprintf(stderr, "finalized %s (%u files)\n", targets[i].path, targets[i].files);
        }
        targets[i].archive = NULL;
    }
    return rc;
}

static int cmd_wow_install(const char *out_dir, const char **disc_paths, bool strip_data_prefix)
{
    HANDLE discs[4] = { 0 };
    BYTE *manifest = NULL;
    DWORD manifest_size = 0;
    wow_target_t targets[32];
    size_t target_count = 0;
    char current_container[256] = "";
    char current_base[512] = "";
    int current_disc = 0;
    unsigned int added = 0;
    int rc = 1;

    memset(targets, 0, sizeof(targets));
    if (mkpath(out_dir) != 0) {
        fprintf(stderr, "Cannot create output directory: %s\n", out_dir);
        goto done;
    }

    for (int i = 0; i < 4; i++) {
        if (!SFileOpenArchive(disc_paths[i], 0, 0, &discs[i])) {
            fprintf(stderr, "Cannot open disc archive: %s\n", disc_paths[i]);
            goto done;
        }
    }

    if (!read_archive_file(discs[0], "InstallCD\\InstallerFileList\\InstallerFileList.xml", &manifest, &manifest_size)) {
        fprintf(stderr, "Cannot read installer manifest from disc 1\n");
        goto done;
    }

    {
        BYTE *resized = (BYTE *)realloc(manifest, (size_t)manifest_size + 1);
        if (!resized) {
            fprintf(stderr, "Out of memory\n");
            goto done;
        }
        manifest = resized;
        manifest[manifest_size] = '\0';
    }

    for (char *line = strtok((char *)manifest, "\n"); line; line = strtok(NULL, "\n")) {
        char value[512];

        if (strstr(line, "<disc") && xml_attr(line, "with_file", value, sizeof(value))) {
            if (strcmp(value, "{Tome1}") == 0) current_disc = 1;
            else if (strcmp(value, "{Tome2}") == 0) current_disc = 2;
            else if (strcmp(value, "{Tome3}") == 0) current_disc = 3;
            else if (strcmp(value, "{Tome4}") == 0) current_disc = 4;
            else current_disc = 0;
            current_container[0] = '\0';
            current_base[0] = '\0';
            continue;
        }

        if (strstr(line, "<repack_into")) {
            char type[32];
            if (xml_attr(line, "type", type, sizeof(type)) &&
                strcmp(type, "mpq") == 0 &&
                xml_attr(line, "container", current_container, sizeof(current_container))) {
                Tool_NormalizeSlashes(current_container, '\\');
                Tool_TrimEdgeSlashes(current_container);
                if (strip_data_prefix) {
                    strip_wow_data_prefix(current_container);
                }
                current_base[0] = '\0';
            } else {
                current_container[0] = '\0';
                current_base[0] = '\0';
            }
            continue;
        }

        if (strstr(line, "</repack_into>")) {
            current_container[0] = '\0';
            current_base[0] = '\0';
            continue;
        }

        if (!current_disc || !current_container[0]) {
            continue;
        }

        if (strstr(line, "<base") && xml_attr(line, "path", current_base, sizeof(current_base))) {
            Tool_NormalizeSlashes(current_base, '\\');
            continue;
        }

        if (strstr(line, "<repack") && xml_attr(line, "from", value, sizeof(value))) {
            char source_path[1024];
            char dest_path[512];
            BYTE *data = NULL;
            DWORD size = 0;
            wow_target_t *target;

            archive_path_join(source_path, sizeof(source_path), current_base, value);
            if (!xml_attr(line, "to", dest_path, sizeof(dest_path))) {
                strncpy(dest_path, value, sizeof(dest_path) - 1);
                dest_path[sizeof(dest_path) - 1] = '\0';
            }
            Tool_NormalizeSlashes(dest_path, '\\');
            Tool_TrimEdgeSlashes(dest_path);

            fprintf(stderr, "%s\n", source_path);
            if (!read_archive_file(discs[current_disc - 1], source_path, &data, &size)) {
                fprintf(stderr, "Cannot read disc %d source: %s\n", current_disc, source_path);
                goto done;
            }

            target = get_wow_target(targets, &target_count, out_dir, current_container);
            if (!target || !SFileAddFileFromBuffer(target->archive, dest_path, data, size)) {
                fprintf(stderr, "Cannot add %s to %s as %s\n", source_path, current_container, dest_path);
                free(data);
                goto done;
            }
            free(data);
            target->files++;
            added++;
        }
    }

    fprintf(stderr, "repacked %u files total\n", added);
    rc = close_wow_targets(targets, target_count);

done:
    if (rc != 0) {
        close_wow_targets(targets, target_count);
    }
    for (int i = 0; i < 4; i++) {
        if (discs[i]) {
            SFileCloseArchive(discs[i]);
        }
    }
    free(manifest);
    return rc;
}

int main(int argc, char **argv) {
    const char *mpq = NULL;
    const char *cmd = NULL;
    const char *arg = NULL;
    char **extra = NULL;
    int extra_count = 0;
    HANDLE archive;
    int rc;

    if (argc >= 7 && strcmp(argv[1], "wow-install") == 0) {
        bool strip_data_prefix = false;
        int argi = 2;

        if (strcmp(argv[argi], "-strip-data-prefix") == 0) {
            strip_data_prefix = true;
            argi++;
        }
        if (argc < argi + 5) {
            usage();
            return 1;
        }
        {
            const char *disc_paths[4] = { argv[argi + 1], argv[argi + 2], argv[argi + 3], argv[argi + 4] };
            return cmd_wow_install(argv[argi], disc_paths, strip_data_prefix);
        }
    }

    if (argc < 4) {
        usage();
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-mpq") == 0) {
            if (i + 1 >= argc) {
                usage();
                return 1;
            }
            mpq = argv[++i];
        } else if (!cmd) {
            cmd = argv[i];
        } else if (!arg) {
            arg = argv[i];
        } else {
            extra = &argv[i];
            extra_count = argc - i;
            break;
        }
    }

    if (!mpq || !cmd) {
        usage();
        return 1;
    }

    if (strcmp(cmd, "create") == 0) {
        return cmd_create(mpq, arg);
    } else if (strcmp(cmd, "pack") == 0) {
        if (!arg) {
            usage();
            return 1;
        }
        if (!extra) {
            fprintf(stderr, "pack requires <src> <archive-file> pairs\n");
            return 1;
        }
        {
            char *pairs[256];
            int pair_count = 0;
            pairs[pair_count++] = (char *)arg;
            for (int i = 0; i < extra_count && pair_count < (int)(sizeof(pairs) / sizeof(pairs[0])); i++) {
                pairs[pair_count++] = extra[i];
            }
            return cmd_pack(mpq, pair_count, pairs);
        }
    }

    if (!SFileOpenArchive(mpq, 0, 0, &archive)) {
        fprintf(stderr, "Cannot open archive: %s\n", mpq);
        return 1;
    }

    if (strcmp(cmd, "grep") == 0) {
        if (!arg) { usage(); SFileCloseArchive(archive); return 1; }
        rc = cmd_grep(archive, arg, extra && extra_count > 0 ? extra[0] : NULL);
    } else if (strcmp(cmd, "ls") == 0) {
        rc = cmd_ls(archive, arg ? arg : "");
    } else if (strcmp(cmd, "cat") == 0) {
        char path[512];
        if (!arg) {
            usage();
            SFileCloseArchive(archive);
            return 1;
        }
        strncpy(path, arg, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        Tool_NormalizeSlashes(path, '\\');
        Tool_TrimEdgeSlashes(path);
        rc = cmd_cat(archive, path);
    } else if (strcmp(cmd, "info") == 0) {
        char path[512];
        if (!arg) {
            usage();
            SFileCloseArchive(archive);
            return 1;
        }
        strncpy(path, arg, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        Tool_NormalizeSlashes(path, '\\');
        Tool_TrimEdgeSlashes(path);
        rc = cmd_info(archive, path);
    } else if (strcmp(cmd, "imginfo") == 0) {
        char path[512];
        if (!arg) {
            usage();
            SFileCloseArchive(archive);
            return 1;
        }
        strncpy(path, arg, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        Tool_NormalizeSlashes(path, '\\');
        Tool_TrimEdgeSlashes(path);
        rc = cmd_imginfo(archive, path);
    } else {
        usage();
        rc = 1;
    }

    SFileCloseArchive(archive);
    return rc;
}
