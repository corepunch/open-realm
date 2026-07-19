#define _FILE_OFFSET_BITS 64

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(path) _mkdir(path)
#else
#define mkdir_one(path) mkdir(path, 0777)
#endif

#define ISO_SECTOR_SIZE 2048u
#define ISO_MAX_PATH    4096u
#define ISO_MAX_NAME    512u

typedef struct {
    uint32_t extent;
    uint32_t size;
    bool is_dir;
    bool associated;
    bool special;
    char name[ISO_MAX_NAME];
} iso_record_t;

typedef struct {
    FILE *file;
    char const *path;
    uint64_t size;
    uint32_t root_extent;
    uint32_t root_size;
    bool joliet;
    unsigned files;
    unsigned dirs;
    uint64_t bytes;
} iso_t;

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  isoextract ls <image.iso>\n"
            "  isoextract extract <image.iso> <output-dir>\n"
            "  isoextract <image.iso> <output-dir>\n"
            "\n"
            "Notes:\n"
            "  Reads ISO 9660 images and uses Joliet names when present.\n"
            "\n"
            "Examples:\n"
            "  isoextract ls WoWDisc1.iso\n"
            "  isoextract WoWDisc1.iso data/world-of-warcraft/disc1\n");
}

static void *xmalloc(size_t size) {
    void *ptr = calloc(1, size ? size : 1);
    if (!ptr) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

static uint16_t rd_u16be(unsigned char const *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd_u32le(unsigned char const *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool seek_abs(FILE *file, uint64_t off) {
#ifdef _WIN32
    return _fseeki64(file, (__int64)off, SEEK_SET) == 0;
#else
    return fseeko(file, (off_t)off, SEEK_SET) == 0;
#endif
}

static bool tell_size(FILE *file, uint64_t *out_size) {
#ifdef _WIN32
    __int64 pos;
    if (_fseeki64(file, 0, SEEK_END) != 0) return false;
    pos = _ftelli64(file);
    if (pos < 0) return false;
    *out_size = (uint64_t)pos;
#else
    off_t pos;
    if (fseeko(file, 0, SEEK_END) != 0) return false;
    pos = ftello(file);
    if (pos < 0) return false;
    *out_size = (uint64_t)pos;
#endif
    rewind(file);
    return true;
}

static bool read_exact(FILE *file, void *data, size_t size) {
    return size == 0 || fread(data, 1, size, file) == size;
}

static bool read_at(iso_t *iso, uint64_t off, void *data, size_t size) {
    if (off > iso->size || (uint64_t)size > iso->size - off) {
        fprintf(stderr, "isoextract: read outside image at %llu size %zu\n", (unsigned long long)off, size);
        return false;
    }
    if (!seek_abs(iso->file, off) || !read_exact(iso->file, data, size)) {
        fprintf(stderr, "isoextract: cannot read %s: %s\n", iso->path, strerror(errno));
        return false;
    }
    return true;
}

static bool read_extent(iso_t *iso, uint32_t extent, uint32_t size, unsigned char **out_data) {
    uint64_t off = (uint64_t)extent * ISO_SECTOR_SIZE;
    unsigned char *data = xmalloc((size_t)size + 1);
    if (size && !read_at(iso, off, data, (size_t)size)) {
        free(data);
        return false;
    }
    data[size] = 0;
    *out_data = data;
    return true;
}

static bool append_utf8(char *out, size_t out_size, size_t *pos, uint32_t c) {
    unsigned char bytes[4];
    size_t count = 0;

    if (c == 0) return true;
    if (c < 0x80) {
        bytes[count++] = (unsigned char)c;
    } else if (c < 0x800) {
        bytes[count++] = (unsigned char)(0xc0 | (c >> 6));
        bytes[count++] = (unsigned char)(0x80 | (c & 0x3f));
    } else {
        bytes[count++] = (unsigned char)(0xe0 | (c >> 12));
        bytes[count++] = (unsigned char)(0x80 | ((c >> 6) & 0x3f));
        bytes[count++] = (unsigned char)(0x80 | (c & 0x3f));
    }

    if (*pos + count >= out_size) return false;
    memcpy(out + *pos, bytes, count);
    *pos += count;
    out[*pos] = 0;
    return true;
}

static void strip_iso_version(char *name) {
    char *semi = strrchr(name, ';');
    if (semi && semi[1]) {
        bool digits = true;
        for (char *p = semi + 1; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                digits = false;
                break;
            }
        }
        if (digits) *semi = 0;
    }

    size_t len = strlen(name);
    if (len > 1 && name[len - 1] == '.') {
        name[len - 1] = 0;
    }
}

static void sanitize_name(char *name, size_t name_size) {
    for (char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || *p == '/' || *p == '\\' || *p == ':') {
            *p = '_';
        }
    }
    if (!name[0] && name_size >= 2) {
        name[0] = '_';
        name[1] = 0;
    }
}

static bool decode_name(unsigned char const *in, unsigned len, bool joliet, char *out, size_t out_size) {
    size_t pos = 0;
    out[0] = 0;

    if (joliet) {
        for (unsigned i = 0; i + 1 < len; i += 2) {
            uint16_t c = rd_u16be(in + i);
            if (!append_utf8(out, out_size, &pos, c)) return false;
        }
    } else {
        for (unsigned i = 0; i < len; i++) {
            unsigned char c = in[i];
            if (pos + 1 >= out_size) return false;
            out[pos++] = (char)c;
            out[pos] = 0;
        }
    }

    strip_iso_version(out);
    sanitize_name(out, out_size);
    return true;
}

static bool parse_record(unsigned char const *rec, size_t avail, bool joliet, iso_record_t *out) {
    unsigned len;
    unsigned name_len;

    memset(out, 0, sizeof(*out));
    if (avail < 1) return false;
    len = rec[0];
    if (len == 0 || len > avail || len < 34) return false;

    name_len = rec[32];
    if (33u + name_len > len) return false;

    out->extent = rd_u32le(rec + 2);
    out->size = rd_u32le(rec + 10);
    out->is_dir = (rec[25] & 2) != 0;
    out->associated = (rec[25] & 4) != 0;
    out->special = name_len == 1 && (rec[33] == 0 || rec[33] == 1);
    if (!out->special && !decode_name(rec + 33, name_len, joliet, out->name, sizeof(out->name))) {
        fprintf(stderr, "isoextract: path component too long\n");
        return false;
    }
    return true;
}

static bool parse_root_record(unsigned char const *descriptor, uint32_t *out_extent, uint32_t *out_size) {
    iso_record_t root;
    if (!parse_record(descriptor + 156, ISO_SECTOR_SIZE - 156, false, &root) || !root.is_dir) {
        return false;
    }
    *out_extent = root.extent;
    *out_size = root.size;
    return true;
}

static bool is_joliet_descriptor(unsigned char const *sector) {
    return sector[88] == '%' && sector[89] == '/' && (sector[90] == '@' || sector[90] == 'C' || sector[90] == 'E');
}

static bool open_iso(char const *path, iso_t *iso) {
    unsigned char sector[ISO_SECTOR_SIZE];
    uint32_t pvd_extent = 0;
    uint32_t pvd_size = 0;
    uint32_t joliet_extent = 0;
    uint32_t joliet_size = 0;
    bool have_pvd = false;
    bool have_joliet = false;

    memset(iso, 0, sizeof(*iso));
    iso->path = path;
    iso->file = fopen(path, "rb");
    if (!iso->file) {
        fprintf(stderr, "isoextract: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!tell_size(iso->file, &iso->size)) {
        fprintf(stderr, "isoextract: cannot size %s\n", path);
        fclose(iso->file);
        return false;
    }

    for (uint32_t i = 16; i < 256; i++) {
        if (!read_at(iso, (uint64_t)i * ISO_SECTOR_SIZE, sector, sizeof(sector))) {
            fclose(iso->file);
            return false;
        }
        if (memcmp(sector + 1, "CD001", 5) != 0) {
            fprintf(stderr, "isoextract: %s is not an ISO 9660 image\n", path);
            fclose(iso->file);
            return false;
        }
        if (sector[0] == 1) {
            if (!parse_root_record(sector, &pvd_extent, &pvd_size)) {
                fprintf(stderr, "isoextract: invalid primary volume descriptor\n");
                fclose(iso->file);
                return false;
            }
            have_pvd = true;
        } else if (sector[0] == 2 && is_joliet_descriptor(sector)) {
            if (!parse_root_record(sector, &joliet_extent, &joliet_size)) {
                fprintf(stderr, "isoextract: invalid Joliet volume descriptor\n");
                fclose(iso->file);
                return false;
            }
            have_joliet = true;
        } else if (sector[0] == 255) {
            break;
        }
    }

    if (have_joliet) {
        iso->root_extent = joliet_extent;
        iso->root_size = joliet_size;
        iso->joliet = true;
    } else if (have_pvd) {
        iso->root_extent = pvd_extent;
        iso->root_size = pvd_size;
    } else {
        fprintf(stderr, "isoextract: missing primary volume descriptor\n");
        fclose(iso->file);
        return false;
    }

    return true;
}

static bool path_join(char *out, size_t out_size, char const *a, char const *b) {
    int n;
    if (!a[0]) {
        n = snprintf(out, out_size, "%s", b);
    } else if (!b[0]) {
        n = snprintf(out, out_size, "%s", a);
    } else {
        n = snprintf(out, out_size, "%s/%s", a, b);
    }
    return n >= 0 && (size_t)n < out_size;
}

static bool mkdir_p(char const *path) {
    char tmp[ISO_MAX_PATH];
    size_t len = strlen(path);

    if (len >= sizeof(tmp)) {
        fprintf(stderr, "isoextract: path too long: %s\n", path);
        return false;
    }
    strlcpy(tmp, path, sizeof(tmp));

    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[--len] = 0;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char old = *p;
        *p = 0;
        if (tmp[0] && mkdir_one(tmp) != 0 && errno != EEXIST) {
            fprintf(stderr, "isoextract: cannot create %s: %s\n", tmp, strerror(errno));
            return false;
        }
        *p = old;
    }

    if (mkdir_one(tmp) != 0 && errno != EEXIST) {
        fprintf(stderr, "isoextract: cannot create %s: %s\n", tmp, strerror(errno));
        return false;
    }
    return true;
}

static bool extract_file(iso_t *iso, iso_record_t const *rec, char const *path) {
    unsigned char buffer[64 * 1024];
    uint64_t off = (uint64_t)rec->extent * ISO_SECTOR_SIZE;
    uint32_t remaining = rec->size;
    FILE *out = fopen(path, "wb");

    if (off > iso->size || (uint64_t)remaining > iso->size - off) {
        fprintf(stderr, "isoextract: file outside image: %s\n", path);
        return false;
    }
    if (!out) {
        fprintf(stderr, "isoextract: cannot write %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!seek_abs(iso->file, off)) {
        fprintf(stderr, "isoextract: cannot seek %s\n", iso->path);
        fclose(out);
        return false;
    }

    while (remaining) {
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!read_exact(iso->file, buffer, chunk) || fwrite(buffer, 1, chunk, out) != chunk) {
            fprintf(stderr, "isoextract: failed extracting %s\n", path);
            fclose(out);
            return false;
        }
        remaining -= (uint32_t)chunk;
    }

    fclose(out);
    iso->files++;
    iso->bytes += rec->size;
    return true;
}

static bool walk_dir(iso_t *iso, uint32_t extent, uint32_t size, char const *out_dir, char const *rel, bool list_only) {
    unsigned char *data;
    size_t off = 0;

    if (!read_extent(iso, extent, size, &data)) {
        return false;
    }

    while (off < size) {
        iso_record_t rec;
        char child_rel[ISO_MAX_PATH];
        char child_out[ISO_MAX_PATH];
        unsigned len = data[off];

        if (len == 0) {
            off = ((off / ISO_SECTOR_SIZE) + 1) * ISO_SECTOR_SIZE;
            continue;
        }
        if (!parse_record(data + off, (size_t)size - off, iso->joliet, &rec)) {
            fprintf(stderr, "isoextract: invalid directory record at sector %u offset %zu\n", extent, off);
            free(data);
            return false;
        }
        off += len;

        if (rec.special) continue;
        if (rec.associated) continue;
        if (!path_join(child_rel, sizeof(child_rel), rel, rec.name) ||
            !path_join(child_out, sizeof(child_out), out_dir, child_rel)) {
            fprintf(stderr, "isoextract: output path too long\n");
            free(data);
            return false;
        }

        if (rec.is_dir) {
            if (list_only) {
                printf("%s/\n", child_rel);
            } else if (!mkdir_p(child_out)) {
                free(data);
                return false;
            }
            iso->dirs++;
            if (!walk_dir(iso, rec.extent, rec.size, out_dir, child_rel, list_only)) {
                free(data);
                return false;
            }
        } else {
            if (list_only) {
                printf("%s %u\n", child_rel, rec.size);
                iso->files++;
                iso->bytes += rec.size;
            } else if (!extract_file(iso, &rec, child_out)) {
                free(data);
                return false;
            }
        }
    }

    free(data);
    return true;
}

static int cmd_ls(char const *image) {
    iso_t iso;
    int rc = 1;

    if (!open_iso(image, &iso)) return 1;
    if (walk_dir(&iso, iso.root_extent, iso.root_size, "", "", true)) {
        fflush(stdout);
        fprintf(stderr, "isoextract: listed %u files, %u dirs, %llu bytes (%s names)\n",
                iso.files, iso.dirs, (unsigned long long)iso.bytes, iso.joliet ? "Joliet" : "ISO 9660");
        rc = 0;
    }
    fclose(iso.file);
    return rc;
}

static int cmd_extract(char const *image, char const *out_dir) {
    iso_t iso;
    int rc = 1;

    if (!open_iso(image, &iso)) return 1;
    if (mkdir_p(out_dir) && walk_dir(&iso, iso.root_extent, iso.root_size, out_dir, "", false)) {
        printf("isoextract: extracted %u files, %u dirs, %llu bytes to %s (%s names)\n",
               iso.files, iso.dirs, (unsigned long long)iso.bytes, out_dir, iso.joliet ? "Joliet" : "ISO 9660");
        rc = 0;
    }
    fclose(iso.file);
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "ls") == 0) {
        return cmd_ls(argv[2]);
    }
    if (argc == 4 && strcmp(argv[1], "extract") == 0) {
        return cmd_extract(argv[2], argv[3]);
    }
    if (argc == 3) {
        return cmd_extract(argv[1], argv[2]);
    }

    usage();
    return 1;
}
