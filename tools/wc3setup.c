/* Import intact Warcraft III game MPQs stored inside classic Setup.mpq files.
 *
 * Anonymous entries are supported only when they are raw, whole-file MPQs:
 * their names are not recoverable from an MPQ hash table. Match multiple
 * known internal filenames instead of guessing identities from sizes/order.
 * Named entries use the project's MPQ decoder (including compressed sectors).
 * This is a separate installer utility, not an engine/runtime dependency.
 */
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define access _access
#define fseeko _fseeki64
#define ftello _ftelli64
#define make_dir(path) _mkdir(path)
#else
#include <unistd.h>
#define make_dir(path) mkdir(path, 0777)
#endif

#include "common/mpq.h"

#define MAX_MPQ_TABLE 65536u
#define MAX_RAW_BLOCKS 8192u
#define MAX_GAME_MPQ (512u * 1024u * 1024u)
#define MAX_EMBEDDED_TOTAL (1024ull * 1024ull * 1024ull)
#define MPQ_EXISTS 0x80000000u
#define MPQ_HEADER_SIZE 32u
#define COPY_BUFFER (64u * 1024u)

typedef struct {
    uint32_t offset, packed, size, flags;
} block_t;

typedef struct {
    uint32_t a, b, locale, index;
} hash_t;

typedef struct {
    FILE *file;
    uint64_t file_size, base;
    uint32_t archive_size, hash_count, block_count;
    hash_t *hashes;
    block_t *blocks;
} index_t;

typedef struct {
    char const *name;
    char const *paths[4];
    unsigned needed;
} fingerprint_t;

static fingerprint_t const fingerprints[] = {
    { "War3.mpq", { "Scripts\\common.j", "Scripts\\human.ai", "UI\\FrameDef\\Glue\\StandardTemplates.fdf" }, 3 },
    { "War3Local.mpq", { "Maps\\Campaign\\Human01.w3m", "Maps\\Campaign\\Undead01.w3m", "Maps\\Campaign\\Orc01.w3m" }, 2 },
    { "War3x.mpq", { "Melee_V0\\Units\\UnitBalance.slk", "Custom_V0\\Units\\UnitBalance.slk", "Custom_V1\\Units\\UnitBalance.slk" }, 2 },
    { "War3xLocal.mpq", { "Maps\\FrozenThrone\\Campaign\\HumanX01.w3x", "Maps\\FrozenThrone\\Campaign\\HumanX06.w3x", "Maps\\FrozenThrone\\Campaign\\UndeadX01.w3x", "Maps\\FrozenThrone\\Campaign\\NightElfX01.w3x" }, 2 },
    { "War3Patch.mpq", { NULL }, 0 }
};
#define GAME_COUNT (sizeof(fingerprints) / sizeof(fingerprints[0]))

static uint32_t storm[0x500];
static uint32_t word(uint8_t const *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void init_storm(void) {
    uint32_t seed = 0x100001;
    for (unsigned i = 0; i < 0x100; ++i) for (unsigned j = i; j < 0x500; j += 0x100) {
        seed = (seed * 125 + 3) % 0x2aaaab;
        uint32_t high = (seed & 0xffff) << 16;
        seed = (seed * 125 + 3) % 0x2aaaab;
        storm[j] = high | (seed & 0xffff);
    }
}

static uint32_t hash_name(char const *name, unsigned kind) {
    uint32_t a = 0x7fed7fed, b = 0xeeeeeeee;
    for (; *name; ++name) {
        unsigned char c = (unsigned char)*name;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == '/') c = '\\';
        a = storm[(kind << 8) + c] ^ (a + b);
        b = c + a + b + (b << 5) + 3;
    }
    return a;
}

static void decrypt(uint8_t *bytes, size_t size, uint32_t key) {
    uint32_t seed = 0xeeeeeeee;
    for (size_t i = 0; i < size; i += 4) {
        seed += storm[0x400 + (key & 255)];
        uint32_t value = word(bytes + i) ^ (key + seed);
        bytes[i] = (uint8_t)value;
        bytes[i + 1] = (uint8_t)(value >> 8);
        bytes[i + 2] = (uint8_t)(value >> 16);
        bytes[i + 3] = (uint8_t)(value >> 24);
        key = ((~key << 21) + 0x11111111) | (key >> 11);
        seed = value + seed + (seed << 5) + 3;
    }
}

static bool read_at(FILE *f, uint64_t at, uint64_t length, uint64_t total, void *out) {
    return at <= total && length <= total - at &&
           fseeko(f, (off_t)at, SEEK_SET) == 0 && fread(out, 1, (size_t)length, f) == length;
}

static void close_index(index_t *idx) {
    free(idx->hashes);
    free(idx->blocks);
    memset(idx, 0, sizeof(*idx));
}

/* Read only two bounded encrypted tables. The FILE is owned by the caller. */
static bool open_index(FILE *f, uint64_t file_size, uint64_t base, bool exact_size, index_t *idx) {
    uint8_t hdr[MPQ_HEADER_SIZE];
    uint32_t header_size, archive_size, hash_pos, block_pos, hc, bc;
    uint16_t version, shift;
    uint8_t *raw_hash = NULL, *raw_block = NULL;
    bool ok = false;

    memset(idx, 0, sizeof(*idx));
    if (!read_at(f, base, sizeof(hdr), file_size, hdr) || memcmp(hdr, "MPQ\x1a", 4)) return false;
    header_size = word(hdr + 4);
    archive_size = word(hdr + 8);
    version = (uint16_t)(hdr[12] | hdr[13] << 8);
    shift = (uint16_t)(hdr[14] | hdr[15] << 8);
    hash_pos = word(hdr + 16);
    block_pos = word(hdr + 20);
    hc = word(hdr + 24);
    bc = word(hdr + 28);
    if (version > 1 || header_size < (version ? 44u : 32u) || header_size > archive_size ||
        shift > 15 || archive_size > file_size - base ||
        (exact_size && archive_size != file_size - base) ||
        !hc || hc > MAX_MPQ_TABLE || (hc & (hc - 1)) ||
        !bc || bc > MAX_MPQ_TABLE ||
        hash_pos < header_size || block_pos < header_size ||
        (uint64_t)hash_pos + (uint64_t)hc * 16 > archive_size ||
        (uint64_t)block_pos + (uint64_t)bc * 16 > archive_size) return false;

    raw_hash = malloc((size_t)hc * 16);
    raw_block = malloc((size_t)bc * 16);
    idx->hashes = calloc(hc, sizeof(hash_t));
    idx->blocks = calloc(bc, sizeof(block_t));
    if (!raw_hash || !raw_block || !idx->hashes || !idx->blocks) goto done;
    if (!read_at(f, base + hash_pos, (size_t)hc * 16, file_size, raw_hash) ||
        !read_at(f, base + block_pos, (size_t)bc * 16, file_size, raw_block)) goto done;
    decrypt(raw_hash, (size_t)hc * 16, 0xC3AF3770);
    decrypt(raw_block, (size_t)bc * 16, 0xEC83B3A3);
    for (uint32_t i = 0; i < hc; ++i) {
        uint8_t const *p = raw_hash + i * 16;
        idx->hashes[i] = (hash_t){word(p), word(p + 4), word(p + 8), word(p + 12)};
    }
    for (uint32_t i = 0; i < bc; ++i) {
        uint8_t const *p = raw_block + i * 16;
        idx->blocks[i] = (block_t){word(p), word(p + 4), word(p + 8), word(p + 12)};
    }
    idx->file = f;
    idx->file_size = file_size;
    idx->base = base;
    idx->archive_size = archive_size;
    idx->hash_count = hc;
    idx->block_count = bc;
    ok = true;
done:
    free(raw_hash);
    free(raw_block);
    if (!ok) close_index(idx);
    return ok;
}

static int find_block(index_t const *idx, char const *name) {
    uint32_t a = hash_name(name, 1), b = hash_name(name, 2);
    uint32_t mask = idx->hash_count - 1;
    uint32_t slot = hash_name(name, 0) & mask;
    for (uint32_t i = 0; i < idx->hash_count; ++i, slot = (slot + 1) & mask) {
        hash_t const *h = idx->hashes + slot;
        if (h->index == 0xffffffffu) break;
        if (h->index != 0xfffffffeu && h->index < idx->block_count &&
            h->a == a && h->b == b) {
            block_t const *binfo = idx->blocks + h->index;
            if ((binfo->flags & MPQ_EXISTS) &&
                binfo->offset <= idx->archive_size &&
                binfo->packed <= idx->archive_size - binfo->offset) return (int)h->index;
        }
    }
    return -1;
}

static int fingerprint(index_t const *idx) {
    int matched = -1;
    for (size_t k = 0; k < GAME_COUNT - 1; ++k) {
        unsigned found = 0;
        for (unsigned p = 0; p < 4 && fingerprints[k].paths[p]; ++p)
            if (find_block(idx, fingerprints[k].paths[p]) >= 0) ++found;
        if (found >= fingerprints[k].needed) {
            if (matched >= 0) return -1; /* ambiguous, never guess */
            matched = (int)k;
        }
    }
    return matched;
}

static bool file_size(FILE *f, uint64_t *size) {
    off_t end;
    if (fseeko(f, 0, SEEK_END) || (end = ftello(f)) < 0) return false;
    *size = (uint64_t)end;
    return true;
}

static bool copy_range(FILE *source, uint64_t at, uint32_t size, char const *dest) {
    uint8_t buffer[COPY_BUFFER];
    FILE *out = fopen(dest, "wb");
    if (!out) return false;
    bool ok = fseeko(source, (off_t)at, SEEK_SET) == 0;
    uint32_t left = size;
    while (ok && left) {
        size_t n = left < sizeof(buffer) ? left : sizeof(buffer);
        ok = fread(buffer, 1, n, source) == n && fwrite(buffer, 1, n, out) == n;
        left -= (uint32_t)n;
    }
    if (fclose(out)) ok = false;
    if (!ok) remove(dest);
    return ok;
}

static bool validate_file(char const *path) {
    FILE *f = fopen(path, "rb");
    uint64_t size;
    uint8_t probe[1024];
    index_t idx;
    bool ok = false;
    if (f && file_size(f, &size)) {
        size_t n = size < sizeof(probe) ? (size_t)size : sizeof(probe);
        if (read_at(f, 0, n, size, probe)) {
            for (size_t i = 0; i + 32 <= n; ++i) {
                if (!memcmp(probe + i, "MPQ\x1a", 4)) {
                    ok = open_index(f, size, i, false, &idx);
                    if (ok) close_index(&idx);
                    break;
                }
            }
        }
    }
    if (f) fclose(f);
    return ok;
}

static bool publish(FILE *source, uint64_t offset, uint32_t size, handle_t outer,
                    char const *member, char const *name, char const *out_dir) {
    char dest[4096], part[4096];
    int n = snprintf(dest, sizeof(dest), "%s/%s", out_dir, name);
    if (n < 0 || (size_t)n >= sizeof(dest)) return false;
    n = snprintf(part, sizeof(part), "%s.part", dest);
    if (n < 0 || (size_t)n >= sizeof(part)) return false;
    if (access(dest, F_OK) == 0) {
        if (!validate_file(dest)) {
            fprintf(stderr, "wc3setup: refusing to replace invalid existing %s\n", dest);
            return false;
        }
        printf("wc3setup: keeping %s\n", dest);
        return true;
    }
    remove(part);
    bool ok = member ? SFileExtractFile(outer, member, part, 0)
                     : copy_range(source, offset, size, part);
    if (!ok || !validate_file(part) || rename(part, dest)) {
        fprintf(stderr, "wc3setup: failed to recover %s\n", name);
        remove(part);
        return false;
    }
    printf("wc3setup: recovered %s\n", name);
    return true;
}

static int run(char const *command, char const *path, char const *out_dir) {
    FILE *f = fopen(path, "rb");
    index_t outer;
    uint64_t bytes;
    int rc = 1;
    if (!f || !file_size(f, &bytes)) {
        fprintf(stderr, "wc3setup: cannot open %s: %s\n", path, strerror(errno));
        if (f) fclose(f);
        return 1;
    }
    uint8_t probe[1024];
    size_t read = bytes < sizeof(probe) ? (size_t)bytes : sizeof(probe);
    uint64_t base = UINT64_MAX;
    if (read_at(f, 0, read, bytes, probe)) {
        for (size_t i = 0; i + 32 <= read; ++i)
            if (!memcmp(probe + i, "MPQ\x1a", 4)) { base = i; break; }
    }
    if (base == UINT64_MAX || !open_index(f, bytes, base, false, &outer)) {
        fprintf(stderr, "wc3setup: invalid or unsupported MPQ: %s\n", path);
        fclose(f);
        return 1;
    }
    if (!strcmp(command, "verify")) {
        rc = 0;
        goto done;
    }
    if (!strcmp(command, "maps")) {
        rc = find_block(&outer, "SetupDat\\maps.lst") >= 0 ||
             find_block(&outer, "SetupDat\\Scenario.lst") >= 0 ? 0 : 1;
        goto done;
    }
    if (strcmp(command, "extract") || !out_dir) goto done;
    if (make_dir(out_dir) && errno != EEXIST) {
        fprintf(stderr, "wc3setup: cannot create %s: %s\n", out_dir, strerror(errno));
        goto done;
    }
    /* The occupied-block scan is deliberately bounded, even on hostile discs. */
    if (outer.block_count > MAX_RAW_BLOCKS) {
        fprintf(stderr, "wc3setup: too many installer blocks\n");
        goto done;
    }
    int named[GAME_COUNT];
    for (size_t k = 0; k < GAME_COUNT; ++k) named[k] = find_block(&outer, fingerprints[k].name);
    int anonymous[GAME_COUNT];
    for (size_t k = 0; k < GAME_COUNT; ++k) anonymous[k] = -1;
    for (uint32_t i = 0; i < outer.block_count; ++i) {
        block_t const *block = &outer.blocks[i];
        if (block->flags != MPQ_EXISTS || block->packed != block->size ||
            block->size < MPQ_HEADER_SIZE || block->size > MAX_GAME_MPQ ||
            block->offset < 32 || block->offset > outer.archive_size ||
            block->size > outer.archive_size - block->offset) continue;
        bool is_named = false;
        for (size_t k = 0; k < GAME_COUNT; ++k)
            if (named[k] == (int)i) is_named = true;
        if (is_named) continue;
        index_t inner;
        uint64_t offset = outer.base + block->offset;
        if (!open_index(f, bytes, offset, false, &inner)) continue;
        bool exact = inner.archive_size == block->size;
        int role = exact ? fingerprint(&inner) : -1;
        close_index(&inner);
        if (role < 0 || named[role] >= 0) continue;
        /* -2 records ambiguous duplicate candidates: do not install either. */
        anonymous[role] = anonymous[role] == -1 ? (int)i : -2;
    }
    handle_t mpq = NULL;
    bool need_named = false;
    for (size_t k = 0; k < GAME_COUNT; ++k) if (named[k] >= 0) need_named = true;
    if (need_named && !SFileOpenArchive(path, 0, 0, &mpq)) {
        fprintf(stderr, "wc3setup: cannot decode named installer members\n");
        goto done;
    }
    /* Cap the sum of declared extracted sizes before writing any archive. */
    uint64_t total = 0;
    for (size_t k = 0; k < GAME_COUNT; ++k) {
        int index = named[k] >= 0 ? named[k] : anonymous[k];
        if (index >= 0) {
            uint32_t size = outer.blocks[index].size;
            if (size > MAX_GAME_MPQ || size > MAX_EMBEDDED_TOTAL - total) {
                fprintf(stderr, "wc3setup: embedded archives exceed safety limit\n");
                if (mpq) SFileCloseArchive(mpq);
                goto done;
            }
            total += size;
        }
    }
    rc = 0;
    for (size_t k = 0; k < GAME_COUNT; ++k) {
        if (named[k] < 0 && anonymous[k] < 0) {
            if (anonymous[k] == -2) fprintf(stderr, "wc3setup: ambiguous %s candidates\n", fingerprints[k].name);
            continue;
        }
        block_t const *block = NULL;
        char const *member = NULL;
        if (named[k] >= 0) {
            block = &outer.blocks[named[k]];
            member = fingerprints[k].name;
        } else block = &outer.blocks[anonymous[k]];
        /* The native MPQ reader re-inflates single-unit data per read;
         * never attempt that with an enormous installer member. */
        if (block->size < MPQ_HEADER_SIZE || block->size > MAX_GAME_MPQ ||
            ((block->flags & 0x01000200u) == 0x01000200u && block->size > 32u * 1024u * 1024u) ||
            !publish(f, outer.base + block->offset, block->size, mpq,
                     member, fingerprints[k].name, out_dir)) rc = 1;
    }
    if (mpq) SFileCloseArchive(mpq);
done:
    close_index(&outer);
    fclose(f);
    return rc;
}

int main(int argc, char **argv) {
    if ((argc == 3 && !strcmp(argv[1], "maps")) ||
        (argc == 3 && !strcmp(argv[1], "verify")) ||
        (argc == 4 && !strcmp(argv[1], "extract"))) {
        init_storm();
        return run(argv[1], argv[2], argc == 4 ? argv[3] : NULL);
    }
    fprintf(stderr, "Usage: wc3setup verify <archive.mpq>\n"
                    "       wc3setup maps <Setup.mpq>\n"
                    "       wc3setup extract <Setup.mpq> <output-dir>\n");
    return 1;
}
