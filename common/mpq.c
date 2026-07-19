/*
 * mpq.c — Pure C MPQ archive reader (War3.mpq compatible)
 *
 * Implements reading of Warcraft III MPQ archives without external dependencies.
 * Supports:
 *  - MPQ v1/v2 format
 *  - File lookup by name
 *  - ZLIB decompression
 *  - Uncompressed files
 *
 * Based on StormLib specifications and MPQ format documentation.
 */

#include "mpq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <zlib.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MPQ_HASH_NAME_A 1
#define MPQ_HASH_NAME_B 2
#define MPQ_HASH_FILE_KEY 3
#define MPQ_KEY_HASH_TABLE 0xC3AF3770
#define MPQ_KEY_BLOCK_TABLE 0xEC83B3A3
#define MPQ_HASH_ENTRY_DELETED 0xFFFFFFFE
#define MPQ_HASH_ENTRY_FREE 0xFFFFFFFF
#define MPQ_BLOCK_INDEX_MASK 0x0FFFFFFF
#define MPQ_SECTOR_SIZE_DEFAULT 4096
#define MPQ_FILE_COMPRESS 0x00000200
#define MPQ_FILE_IMPLODE 0x00000100
#define MPQ_FILE_ENCRYPTED 0x00010000
#define MPQ_FILE_KEY_V2 0x00020000
#define MPQ_FILE_SINGLE_UNIT 0x01000000
#define MPQ_COMPRESSION_ZLIB 0x02
#define MPQ_FILE_EXISTS 0x80000000u

typedef struct {
    DWORD dwID;                 // "MPQ\x1a"
    DWORD dwHeaderSize;         // Size of MPQ header
    DWORD dwArchiveSize;        // Size of entire archive
    USHORT wFormatVersion;      // 0 for v1, 1 for v2, etc.
    USHORT wSectorSizeShift;    // Power of 2 for sector size (usually 12 = 4096)
    DWORD dwHashTablePos;       // Offset to hash table from archive start
    DWORD dwBlockTablePos;      // Offset to block table from archive start
    DWORD dwHashTableSize;      // Number of hash table entries
    DWORD dwBlockTableSize;     // Number of block table entries
} MPQ_HEADER_V1;

typedef struct {
    DWORD dwNameHash1;          // First name hash
    DWORD dwNameHash2;          // Second name hash
    USHORT wLocale;             // Locale
    BYTE bPlatform;             // Platform
    BYTE bFlags;                // Flags
    DWORD dwBlockIndex;         // Block table index (or 0xFFFFFFFF for free/deleted)
} MPQ_HASH_ENTRY;

typedef struct {
    DWORD dwBlockOffset;        // Offset from MPQ header
    DWORD dwBlockSize;          // Compressed size
    DWORD dwFileSize;           // Uncompressed size
    DWORD dwFlags;              // File flags
} MPQ_BLOCK_ENTRY;

struct mpq_cache_entry;

typedef struct {
    FILE *fp;
    const BYTE *memory;
    DWORD memory_size;
    DWORD memory_pos;
    char filename[256];
    DWORD base_offset;
    struct mpq_cache_entry **lookup_cache;
    DWORD lookup_cache_size;
    MPQ_HEADER_V1 header;
    MPQ_HASH_ENTRY *hashtable;
    MPQ_BLOCK_ENTRY *blocktable;
    DWORD sector_size;
    BYTE *sector_buffer;
    BOOL write_mode;
    DWORD write_hash_table_size;
    struct mpq_write_entry *write_entries;
    DWORD write_count;
    DWORD write_capacity;
} MPQ_ARCHIVE;

typedef struct {
    MPQ_ARCHIVE *archive;
    HANDLE owner_archive;
    BYTE *owner_memory;
    DWORD block_index;
    DWORD file_size;
    DWORD current_pos;
    DWORD compressed_size;
    DWORD flags;
    DWORD file_key;
    DWORD sector_count;
    DWORD *sector_offsets;
} MPQ_FILE;

typedef struct {
    MPQ_ARCHIVE *archive;
    DWORD current_index;
    DWORD file_count;
    char **files;
    char mask[MAX_PATH];
} MPQ_FIND;

typedef struct mpq_cache_entry {
    char *name;
    DWORD block_index;
    struct mpq_cache_entry *next;
} MPQ_CACHE_ENTRY;

typedef struct mpq_write_entry {
    char *name;
    DWORD offset;
    DWORD block_size;
    DWORD file_size;
    DWORD flags;
} MPQ_WRITE_ENTRY;

typedef struct {
    char *name;
    DWORD hash1;
    DWORD hash2;
} MPQ_LISTFILE_ENTRY;

typedef struct mpq_listfile_bucket_entry {
    MPQ_LISTFILE_ENTRY *entry;
    struct mpq_listfile_bucket_entry *next;
} MPQ_LISTFILE_BUCKET_ENTRY;

#define STORM_BUFFER_SIZE 0x500

static DWORD storm_buffer[STORM_BUFFER_SIZE];
static BOOL storm_buffer_ready = FALSE;

static void InitStormBuffer(void)
{
    DWORD seed = 0x00100001;
    DWORD index1, index2, i;

    if (storm_buffer_ready) {
        return;
    }

    for (index1 = 0; index1 < 0x100; index1++) {
        for (index2 = index1, i = 0; i < 5; i++, index2 += 0x100) {
            DWORD temp1, temp2;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp1 = (seed & 0xFFFF) << 0x10;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp2 = (seed & 0xFFFF);

            storm_buffer[index2] = (temp1 | temp2);
        }
    }

    storm_buffer_ready = TRUE;
}

static BYTE AsciiToUpper(BYTE ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return ch - 0x20;
    }
    if (ch == '/') {
        return '\\';
    }
    return ch;
}

static DWORD HashString(const char *str, DWORD hash_type)
{
    DWORD seed1 = 0x7FED7FED;
    DWORD seed2 = 0xEEEEEEEE;
    BYTE ch;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    while (*str) {
        ch = AsciiToUpper((BYTE)*str++);
        seed1 = storm_buffer[(hash_type * 0x100) + ch] ^ (seed1 + seed2);
        seed2 = ch + seed1 + seed2 + (seed2 << 5) + 3;
    }

    return seed1;
}

#define MPQ_HASH_KEY2_MIX 0x400

static void NormalizeMpqPath(char *s)
{
    while (*s) {
        if (*s == '/') {
            *s = '\\';
        }
        s++;
    }
}

static void TrimEdgeSlashes(char *s)
{
    size_t len = strlen(s);

    while (len > 0 && (s[len - 1] == '\\' || s[len - 1] == '/')) {
        s[--len] = '\0';
    }
    while (*s == '\\' || *s == '/') {
        memmove(s, s + 1, strlen(s));
    }
}

static DWORD CacheKeyHash(const char *str)
{
    DWORD hash = 2166136261u;

    while (*str) {
        BYTE ch = (BYTE)*str++;
        if (ch == '/') {
            ch = '\\';
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (BYTE)(ch + 0x20);
        }
        hash ^= ch;
        hash *= 16777619u;
    }

    return hash;
}

static void CanonicalizeMpqKey(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;

    if (!src || !dst || dst_size == 0) {
        return;
    }

    while (*src && i + 1 < dst_size) {
        char ch = *src++;
        if (ch == '/') {
            ch = '\\';
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch + 0x20);
        }
        dst[i++] = ch;
    }
    dst[i] = '\0';
    TrimEdgeSlashes(dst);
}

static BOOL LookupCachedBlock(MPQ_ARCHIVE *mpq, const char *fileName, DWORD *block_index)
{
    char key[1024];
    DWORD hash;
    MPQ_CACHE_ENTRY *entry;

    if (!mpq->lookup_cache || mpq->lookup_cache_size == 0) {
        return FALSE;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    hash = CacheKeyHash(key);
    entry = mpq->lookup_cache[hash & (mpq->lookup_cache_size - 1)];
    while (entry) {
        if (!strcmp(entry->name, key)) {
            *block_index = entry->block_index;
            return TRUE;
        }
        entry = entry->next;
    }

    return FALSE;
}

static void CacheBlockLookup(MPQ_ARCHIVE *mpq, const char *fileName, DWORD block_index)
{
    char key[1024];
    DWORD hash;
    MPQ_CACHE_ENTRY *entry;

    if (!mpq->lookup_cache || mpq->lookup_cache_size == 0) {
        return;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    hash = CacheKeyHash(key);
    entry = (MPQ_CACHE_ENTRY *)malloc(sizeof(MPQ_CACHE_ENTRY));
    if (!entry) {
        return;
    }
    entry->name = (char *)malloc(strlen(key) + 1);
    if (!entry->name) {
        free(entry);
        return;
    }
    memcpy(entry->name, key, strlen(key) + 1);
    entry->block_index = block_index;
    entry->next = mpq->lookup_cache[hash & (mpq->lookup_cache_size - 1)];
    mpq->lookup_cache[hash & (mpq->lookup_cache_size - 1)] = entry;
}

static BOOL PathCharEquals(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a + 0x20);
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b + 0x20);
    }
    return a == b;
}

static BOOL HasArchiveExtensionAt(LPCSTR path, size_t dot)
{
    static LPCSTR const exts[] = { ".mpq", ".w3m", ".w3x", NULL };

    for (DWORD i = 0; exts[i]; i++) {
        BOOL match = TRUE;
        for (DWORD j = 0; exts[i][j]; j++) {
            if (!path[dot + j] || !PathCharEquals(path[dot + j], exts[i][j])) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            char next = path[dot + strlen(exts[i])];
            return next == '/' || next == '\\';
        }
    }
    return FALSE;
}

static BOOL FindBlockIndex(MPQ_ARCHIVE *mpq, const char *fileName, DWORD hash1, DWORD hash2, DWORD *block_index)
{
    DWORD hash_pos;
    DWORD index;
    BOOL trace = getenv("BZ_MPQ_TRACE") != NULL;

    hash_pos = (hash1 & (mpq->header.dwHashTableSize - 1));
    if (trace) {
        fprintf(stderr, "MPQ lookup: %s hash1=%08x hash2=%08x table=%u start=%u\n",
                fileName, hash1, hash2, mpq->header.dwHashTableSize, hash_pos);
    }
    for (index = 0; index < mpq->header.dwHashTableSize; index++) {
        MPQ_HASH_ENTRY *entry = &mpq->hashtable[(hash_pos + index) & (mpq->header.dwHashTableSize - 1)];

        if (trace && index < 8) {
            fprintf(stderr, "MPQ lookup probe[%u]: block=%08x h1=%08x h2=%08x\n",
                    index, entry->dwBlockIndex, entry->dwNameHash1, entry->dwNameHash2);
        }
        if (entry->dwBlockIndex == MPQ_HASH_ENTRY_FREE) {
            if (trace) {
                fprintf(stderr, "MPQ lookup: stopped at free slot after %u probes\n", index);
            }
            break;
        }

        if (entry->dwBlockIndex == MPQ_HASH_ENTRY_DELETED) {
            continue;
        }

        if (entry->dwNameHash1 == hash1 && entry->dwNameHash2 == hash2) {
            *block_index = entry->dwBlockIndex & MPQ_BLOCK_INDEX_MASK;
            if (trace) {
                fprintf(stderr, "MPQ lookup: found via probe at block_index=%u\n", *block_index);
            }
            CacheBlockLookup(mpq, fileName, *block_index);
            return TRUE;
        }
    }

    if (trace) {
        fprintf(stderr, "MPQ lookup: falling back to full scan\n");
    }
    for (index = 0; index < mpq->header.dwHashTableSize; index++) {
        MPQ_HASH_ENTRY *entry = &mpq->hashtable[index];

        if (entry->dwBlockIndex == MPQ_HASH_ENTRY_DELETED ||
            entry->dwBlockIndex == MPQ_HASH_ENTRY_FREE) {
            continue;
        }

        if (entry->dwNameHash1 == hash1 && entry->dwNameHash2 == hash2) {
            *block_index = entry->dwBlockIndex & MPQ_BLOCK_INDEX_MASK;
            if (trace) {
                fprintf(stderr, "MPQ lookup: found via full scan at slot=%u block_index=%u\n", index, *block_index);
            }
            CacheBlockLookup(mpq, fileName, *block_index);
            return TRUE;
        }
    }

    if (trace) {
        fprintf(stderr, "MPQ lookup: not found after full scan\n");
    }

    return FALSE;
}

static const char *BaseNamePtr(const char *path)
{
    const char *slash1;
    const char *slash2;

    if (!path) {
        return path;
    }

    slash1 = strrchr(path, '\\');
    slash2 = strrchr(path, '/');
    if (slash2 && (!slash1 || slash2 > slash1)) {
        return slash2 + 1;
    }
    if (slash1) {
        return slash1 + 1;
    }
    return path;
}

static BOOL DecryptBlock(LPBYTE data, DWORD size, DWORD seed1)
{
    LPDWORD pdw = (LPDWORD)data;
    DWORD nblocks = size >> 2;  // Convert to DWORD count
    DWORD seed2 = 0xEEEEEEEE;
    DWORD index;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    for (index = 0; index < nblocks; index++) {
        DWORD value;
        DWORD seed2_base;

        seed2_base = seed2 + storm_buffer[MPQ_HASH_KEY2_MIX + (seed1 & 0xFF)];
        value = pdw[index] ^ (seed1 + seed2_base);
        pdw[index] = value;
        seed1 = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
        seed2 = value + seed2_base + (seed2_base << 5) + 3;
    }

    return TRUE;
}

static BOOL EncryptBlock(LPBYTE data, DWORD size, DWORD seed1)
{
    LPDWORD pdw = (LPDWORD)data;
    DWORD nblocks = size >> 2;
    DWORD seed2 = 0xEEEEEEEE;
    DWORD index;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    for (index = 0; index < nblocks; index++) {
        DWORD value = pdw[index];
        DWORD seed2_base = seed2 + storm_buffer[MPQ_HASH_KEY2_MIX + (seed1 & 0xFF)];

        pdw[index] = value ^ (seed1 + seed2_base);
        seed1 = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
        seed2 = value + seed2_base + (seed2_base << 5) + 3;
    }

    return TRUE;
}

static DWORD NextPowerOfTwo(DWORD value)
{
    DWORD result = 1;

    while (result < value) {
        result <<= 1;
    }

    return result;
}

static void FreeWriterEntries(MPQ_ARCHIVE *mpq)
{
    DWORD i;

    if (!mpq || !mpq->write_entries) {
        return;
    }

    for (i = 0; i < mpq->write_count; i++) {
        free(mpq->write_entries[i].name);
    }

    free(mpq->write_entries);
    mpq->write_entries = NULL;
    mpq->write_count = 0;
    mpq->write_capacity = 0;
}

static BOOL WriterHasEntry(MPQ_ARCHIVE *mpq, const char *fileName)
{
    DWORD i;
    char key[1024];

    if (!mpq || !fileName) {
        return FALSE;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    for (i = 0; i < mpq->write_count; i++) {
        char existing[1024];
        CanonicalizeMpqKey(mpq->write_entries[i].name, existing, sizeof(existing));
        if (!strcmp(existing, key)) {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL WriterCompressData(const BYTE *data, DWORD size, BYTE **out_data, DWORD *out_size, DWORD *out_flags)
{
    BYTE *compressed;
    uLongf compressed_bound;

    if (!out_data || !out_size || !out_flags) {
        return FALSE;
    }

    *out_data = NULL;
    *out_size = size;
    *out_flags = MPQ_FILE_EXISTS;

    if (!data || size == 0) {
        return TRUE;
    }

    compressed_bound = compressBound(size);
    if (compressed_bound + 1 > 0xFFFFFFFFu) {
        return TRUE;
    }

    compressed = (BYTE *)malloc((size_t)compressed_bound + 1);
    if (!compressed) {
        return FALSE;
    }
    compressed[0] = MPQ_COMPRESSION_ZLIB;

    if (compress2(compressed + 1, &compressed_bound, data, size, Z_DEFAULT_COMPRESSION) != Z_OK ||
        compressed_bound + 1 >= size) {
        free(compressed);
        return TRUE;
    }

    *out_data = compressed;
    *out_size = (DWORD)(compressed_bound + 1);
    *out_flags = MPQ_FILE_EXISTS | MPQ_FILE_COMPRESS | MPQ_FILE_SINGLE_UNIT;
    return TRUE;
}

static BOOL WriterAddData(MPQ_ARCHIVE *mpq, const char *archivedName, const BYTE *data, DWORD size)
{
    MPQ_WRITE_ENTRY *entry;
    BYTE *compressed = NULL;
    const BYTE *to_write = data;
    DWORD write_size = size;
    DWORD flags = MPQ_FILE_EXISTS;
    long pos;
    char path[1024];

    if (!mpq || !mpq->write_mode || !archivedName || !*archivedName) {
        return FALSE;
    }

    strncpy(path, archivedName, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    NormalizeMpqPath(path);
    TrimEdgeSlashes(path);
    if (*path == '\0' || WriterHasEntry(mpq, path)) {
        return FALSE;
    }

    if (!WriterCompressData(data, size, &compressed, &write_size, &flags)) {
        return FALSE;
    }
    if (compressed) {
        to_write = compressed;
    }

    if (fseek(mpq->fp, 0, SEEK_END) != 0) {
        free(compressed);
        return FALSE;
    }
    pos = ftell(mpq->fp);
    if (pos < 0 || (unsigned long)pos > 0xFFFFFFFFu) {
        free(compressed);
        return FALSE;
    }
    if (write_size > 0 && (!to_write || fwrite(to_write, 1, write_size, mpq->fp) != write_size)) {
        free(compressed);
        return FALSE;
    }
    free(compressed);

    if (mpq->write_count == mpq->write_capacity) {
        DWORD next = mpq->write_capacity ? mpq->write_capacity * 2 : 16;
        MPQ_WRITE_ENTRY *tmp = (MPQ_WRITE_ENTRY *)realloc(mpq->write_entries, next * sizeof(*tmp));
        if (!tmp) {
            return FALSE;
        }
        mpq->write_entries = tmp;
        mpq->write_capacity = next;
    }

    entry = &mpq->write_entries[mpq->write_count++];
    memset(entry, 0, sizeof(*entry));
    entry->name = (char *)malloc(strlen(path) + 1);
    if (!entry->name) {
        mpq->write_count--;
        return FALSE;
    }
    memcpy(entry->name, path, strlen(path) + 1);
    entry->offset = (DWORD)pos;
    entry->block_size = write_size;
    entry->file_size = size;
    entry->flags = flags;
    return TRUE;
}

static BOOL FinalizeCreatedArchive(MPQ_ARCHIVE *mpq)
{
    MPQ_HEADER_V1 header;
    MPQ_HASH_ENTRY *hash_table = NULL;
    MPQ_BLOCK_ENTRY *block_table = NULL;
    DWORD total_entries;
    DWORD hash_size;
    DWORD block_size = 0;
    DWORD offset;
    long table_pos;
    DWORD i;
    BOOL ok = FALSE;

    if (!mpq || !mpq->write_mode || !mpq->fp) {
        return FALSE;
    }

    for (i = 0; i < mpq->write_count; i++) {
        block_size += (DWORD)strlen(mpq->write_entries[i].name) + 1;
    }
    block_size += (DWORD)strlen("(listfile)") + 1;

    {
        BYTE *listfile_data = (BYTE *)malloc(block_size ? block_size : 1);
        if (!listfile_data) {
            goto done;
        }

        offset = 0;
        for (i = 0; i < mpq->write_count; i++) {
            size_t len = strlen(mpq->write_entries[i].name);
            memcpy(listfile_data + offset, mpq->write_entries[i].name, len);
            offset += (DWORD)len;
            listfile_data[offset++] = '\n';
        }
        memcpy(listfile_data + offset, "(listfile)", strlen("(listfile)"));
        offset += (DWORD)strlen("(listfile)");
        listfile_data[offset++] = '\n';

        if (!WriterAddData(mpq, "(listfile)", listfile_data, offset)) {
            free(listfile_data);
            goto done;
        }
        free(listfile_data);
    }

    total_entries = mpq->write_count;
    hash_size = NextPowerOfTwo(MAX(mpq->write_hash_table_size, total_entries * 2));
    if (hash_size < 16) {
        hash_size = 16;
    }

    if (fseek(mpq->fp, 0, SEEK_END) != 0) {
        goto done;
    }
    table_pos = ftell(mpq->fp);
    if (table_pos < 0 || (unsigned long)table_pos > 0xFFFFFFFFu) {
        goto done;
    }

    hash_table = (MPQ_HASH_ENTRY *)malloc(hash_size * sizeof(*hash_table));
    block_table = (MPQ_BLOCK_ENTRY *)malloc(total_entries * sizeof(*block_table));
    if (!hash_table || !block_table) {
        goto done;
    }

    for (i = 0; i < hash_size; i++) {
        hash_table[i].dwNameHash1 = 0xFFFFFFFF;
        hash_table[i].dwNameHash2 = 0xFFFFFFFF;
        hash_table[i].wLocale = 0xFFFF;
        hash_table[i].bPlatform = 0xFF;
        hash_table[i].bFlags = 0xFF;
        hash_table[i].dwBlockIndex = MPQ_HASH_ENTRY_FREE;
    }

    memset(block_table, 0, total_entries * sizeof(*block_table));

    memset(&header, 0, sizeof(header));
    header.dwID = 0x1A51504D;
    header.dwHeaderSize = sizeof(header);
    header.wFormatVersion = 0;
    header.wSectorSizeShift = 3;
    header.dwHashTableSize = hash_size;
    header.dwBlockTableSize = total_entries;

    offset = (DWORD)table_pos;
    for (i = 0; i < total_entries; i++) {
        MPQ_WRITE_ENTRY *entry = &mpq->write_entries[i];
        DWORD slot = HashString(entry->name, MPQ_HASH_NAME_A) & (hash_size - 1);

        block_table[i].dwBlockOffset = entry->offset;
        block_table[i].dwBlockSize = entry->block_size;
        block_table[i].dwFileSize = entry->file_size;
        block_table[i].dwFlags = entry->flags;

        while (hash_table[slot].dwBlockIndex != MPQ_HASH_ENTRY_FREE) {
            slot = (slot + 1) & (hash_size - 1);
        }

        hash_table[slot].dwNameHash1 = HashString(entry->name, MPQ_HASH_NAME_A);
        hash_table[slot].dwNameHash2 = HashString(entry->name, MPQ_HASH_NAME_B);
        hash_table[slot].wLocale = 0;
        hash_table[slot].bPlatform = 0;
        hash_table[slot].bFlags = 0;
        hash_table[slot].dwBlockIndex = i;
    }

    header.dwHashTablePos = offset;
    offset += hash_size * sizeof(*hash_table);
    header.dwBlockTablePos = offset;
    offset += total_entries * sizeof(*block_table);
    header.dwArchiveSize = offset;

    EncryptBlock((LPBYTE)hash_table, hash_size * sizeof(*hash_table), MPQ_KEY_HASH_TABLE);
    EncryptBlock((LPBYTE)block_table, total_entries * sizeof(*block_table), MPQ_KEY_BLOCK_TABLE);

    if (fwrite(hash_table, sizeof(*hash_table), hash_size, mpq->fp) != hash_size) {
        goto done;
    }
    if (fwrite(block_table, sizeof(*block_table), total_entries, mpq->fp) != total_entries) {
        goto done;
    }
    if (fseek(mpq->fp, 0, SEEK_SET) != 0) {
        goto done;
    }
    if (fwrite(&header, sizeof(header), 1, mpq->fp) != 1) {
        goto done;
    }
    if (fflush(mpq->fp) != 0) {
        goto done;
    }

    ok = TRUE;

done:
    free(hash_table);
    free(block_table);
    return ok;
}

static BOOL SectorTableLooksValid(const DWORD *offsets, DWORD sector_count, DWORD compressed_size)
{
    DWORD i;

    if (!offsets || sector_count == 0) {
        return FALSE;
    }

    if (offsets[0] == 0 || offsets[0] > compressed_size) {
        return FALSE;
    }

    for (i = 0; i < sector_count; i++) {
        if (offsets[i] > offsets[i + 1]) {
            return FALSE;
        }
        if (offsets[i + 1] > compressed_size) {
            return FALSE;
        }
    }

    return TRUE;
}

static void PreloadListfileCache(MPQ_ARCHIVE *mpq);

static BOOL TryInflateSector(const BYTE *compressed, DWORD compressed_size, DWORD uncompressed_size, BYTE *out, DWORD *out_size)
{
    z_stream zs;
    int zlib_ret;
    const BYTE *payload = compressed;
    DWORD payload_size = compressed_size;

    if (!compressed || !out || !out_size || compressed_size == 0) {
        return FALSE;
    }

    if (compressed_size > 1 && compressed[0] == MPQ_COMPRESSION_ZLIB) {
        payload = compressed + 1;
        payload_size = compressed_size - 1;
    }

    memset(&zs, 0, sizeof(zs));
    zs.avail_in = payload_size;
    zs.next_in = (Bytef *)payload;
    zs.avail_out = uncompressed_size;
    zs.next_out = out;

    zlib_ret = inflateInit(&zs);
    if (zlib_ret != Z_OK) {
        memset(&zs, 0, sizeof(zs));
        zs.avail_in = compressed_size;
        zs.next_in = (Bytef *)compressed;
        zs.avail_out = uncompressed_size;
        zs.next_out = out;
        zlib_ret = inflateInit2(&zs, -MAX_WBITS);
        if (zlib_ret != Z_OK) {
            return FALSE;
        }
    }

    zlib_ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zlib_ret != Z_STREAM_END) {
        return FALSE;
    }

    *out_size = zs.total_out;
    return TRUE;
}

static BOOL MpqSeek(MPQ_ARCHIVE *mpq, DWORD offset)
{
    DWORD pos;

    if (!mpq) {
        return FALSE;
    }
    pos = mpq->base_offset + offset;
    if (mpq->memory) {
        if (pos > mpq->memory_size) {
            return FALSE;
        }
        mpq->memory_pos = pos;
        return TRUE;
    }
    return mpq->fp && fseek(mpq->fp, pos, SEEK_SET) == 0;
}

static size_t MpqRead(MPQ_ARCHIVE *mpq, void *buffer, size_t size)
{
    size_t available;

    if (!mpq || !buffer || size == 0) {
        return 0;
    }
    if (mpq->memory) {
        if (mpq->memory_pos >= mpq->memory_size) {
            return 0;
        }
        available = mpq->memory_size - mpq->memory_pos;
        if (size > available) {
            size = available;
        }
        memcpy(buffer, mpq->memory + mpq->memory_pos, size);
        mpq->memory_pos += (DWORD)size;
        return size;
    }
    return mpq->fp ? fread(buffer, 1, size, mpq->fp) : 0;
}

static void MpqFreeReadArchive(MPQ_ARCHIVE *mpq)
{
    if (!mpq) {
        return;
    }
    if (mpq->lookup_cache) {
        DWORD i;
        for (i = 0; i < mpq->lookup_cache_size; i++) {
            MPQ_CACHE_ENTRY *entry = mpq->lookup_cache[i];
            while (entry) {
                MPQ_CACHE_ENTRY *next = entry->next;
                free(entry->name);
                free(entry);
                entry = next;
            }
        }
        free(mpq->lookup_cache);
    }
    if (mpq->hashtable) free(mpq->hashtable);
    if (mpq->blocktable) free(mpq->blocktable);
    if (mpq->sector_buffer) free(mpq->sector_buffer);
    if (mpq->fp) fclose(mpq->fp);
    free(mpq);
}

static BOOL SFileOpenArchiveSource(MPQ_ARCHIVE *mpq, HANDLE *archive)
{
    unsigned char probe[1024];
    size_t read;
    long archive_offset = -1;
    size_t i;

    if (!mpq || !archive) {
        return FALSE;
    }

    mpq->lookup_cache_size = 4096;
    mpq->lookup_cache = (MPQ_CACHE_ENTRY **)calloc(mpq->lookup_cache_size, sizeof(MPQ_CACHE_ENTRY *));
    if (!mpq->lookup_cache) {
        goto fail;
    }

    read = MpqRead(mpq, probe, sizeof(probe));
    for (i = 0; i + 4 <= read; i++) {
        if (probe[i] == 'M' && probe[i + 1] == 'P' && probe[i + 2] == 'Q' && probe[i + 3] == 0x1A) {
            archive_offset = (long)i;
            break;
        }
    }
    if (archive_offset < 0) {
        goto fail;
    }
    mpq->base_offset = (DWORD)archive_offset;

    if (!MpqSeek(mpq, 0)) {
        goto fail;
    }

    // Read MPQ header
    if (MpqRead(mpq, &mpq->header, sizeof(MPQ_HEADER_V1)) != sizeof(MPQ_HEADER_V1)) {
        goto fail;
    }

    // Verify MPQ signature
    if (mpq->header.dwID != 0x1A51504D) {  // "MPQ\x1a"
        goto fail;
    }

    // Calculate sector size
    mpq->sector_size = (1 << (mpq->header.wSectorSizeShift + 9));
    if (mpq->sector_size < 512 || mpq->sector_size > 65536) {
        mpq->sector_size = MPQ_SECTOR_SIZE_DEFAULT;
    }

    // Allocate sector buffer
    mpq->sector_buffer = (BYTE *)malloc(mpq->sector_size);
    if (!mpq->sector_buffer) {
        goto fail;
    }

    // Read hash table
    mpq->hashtable = (MPQ_HASH_ENTRY *)malloc(mpq->header.dwHashTableSize * sizeof(MPQ_HASH_ENTRY));
    if (!mpq->hashtable) {
        goto fail;
    }

    if (!MpqSeek(mpq, mpq->header.dwHashTablePos)) {
        goto fail;
    }
    read = MpqRead(mpq, mpq->hashtable, mpq->header.dwHashTableSize * sizeof(MPQ_HASH_ENTRY));
    if (read != mpq->header.dwHashTableSize * sizeof(MPQ_HASH_ENTRY)) {
        goto fail;
    }

    // Decrypt hash table
    DecryptBlock((LPBYTE)mpq->hashtable, mpq->header.dwHashTableSize * sizeof(MPQ_HASH_ENTRY),
                 MPQ_KEY_HASH_TABLE);

    // Read block table
    mpq->blocktable = (MPQ_BLOCK_ENTRY *)malloc(mpq->header.dwBlockTableSize * sizeof(MPQ_BLOCK_ENTRY));
    if (!mpq->blocktable) {
        goto fail;
    }

    if (!MpqSeek(mpq, mpq->header.dwBlockTablePos)) {
        goto fail;
    }
    read = MpqRead(mpq, mpq->blocktable, mpq->header.dwBlockTableSize * sizeof(MPQ_BLOCK_ENTRY));
    if (read != mpq->header.dwBlockTableSize * sizeof(MPQ_BLOCK_ENTRY)) {
        goto fail;
    }

    // Decrypt block table
    DecryptBlock((LPBYTE)mpq->blocktable, mpq->header.dwBlockTableSize * sizeof(MPQ_BLOCK_ENTRY),
                 MPQ_KEY_BLOCK_TABLE);

    PreloadListfileCache(mpq);

    *archive = (HANDLE)mpq;
    return TRUE;

fail:
    MpqFreeReadArchive(mpq);
    return FALSE;
}

BOOL SFileOpenArchive(LPCSTR filename, DWORD priority, DWORD flags, HANDLE *archive)
{
    MPQ_ARCHIVE *mpq;
    FILE *fp;

    (void)priority;
    (void)flags;

    if (!filename || !archive) {
        return FALSE;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        return FALSE;
    }

    mpq = (MPQ_ARCHIVE *)calloc(1, sizeof(MPQ_ARCHIVE));
    if (!mpq) {
        fclose(fp);
        return FALSE;
    }

    mpq->fp = fp;
    strncpy(mpq->filename, filename, sizeof(mpq->filename) - 1);
    return SFileOpenArchiveSource(mpq, archive);
}

BOOL SFileOpenArchiveFromMemory(const void *data, DWORD size, DWORD flags, HANDLE *archive)
{
    MPQ_ARCHIVE *mpq;

    (void)flags;

    if (!data || size == 0 || !archive) {
        return FALSE;
    }

    mpq = (MPQ_ARCHIVE *)calloc(1, sizeof(MPQ_ARCHIVE));
    if (!mpq) {
        return FALSE;
    }

    mpq->memory = (const BYTE *)data;
    mpq->memory_size = size;
    strncpy(mpq->filename, "<memory>", sizeof(mpq->filename) - 1);
    return SFileOpenArchiveSource(mpq, archive);
}

BOOL SFileCreateArchive(LPCSTR filename, DWORD flags, DWORD maxFiles, HANDLE *archive)
{
    MPQ_ARCHIVE *mpq;
    MPQ_HEADER_V1 header;
    FILE *fp;

    (void)flags;

    if (!filename || !archive) {
        return FALSE;
    }

    fp = fopen(filename, "wb+");
    if (!fp) {
        return FALSE;
    }

    mpq = (MPQ_ARCHIVE *)calloc(1, sizeof(*mpq));
    if (!mpq) {
        fclose(fp);
        return FALSE;
    }

    memset(&header, 0, sizeof(header));
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        free(mpq);
        fclose(fp);
        return FALSE;
    }

    mpq->fp = fp;
    mpq->write_mode = TRUE;
    mpq->write_hash_table_size = NextPowerOfTwo(MAX(maxFiles * 2, 16));
    strncpy(mpq->filename, filename, sizeof(mpq->filename) - 1);
    *archive = (HANDLE)mpq;
    return TRUE;
}

BOOL SFileAddFile(HANDLE archive, LPCSTR sourceFile, LPCSTR archivedName)
{
    MPQ_ARCHIVE *mpq = (MPQ_ARCHIVE *)archive;
    FILE *fp;
    BYTE *buffer;
    long size_long;
    size_t size;
    const char *name = archivedName ? archivedName : BaseNamePtr(sourceFile);
    BOOL ok;

    if (!mpq || !mpq->write_mode || !sourceFile || !name) {
        return FALSE;
    }

    fp = fopen(sourceFile, "rb");
    if (!fp) {
        return FALSE;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return FALSE;
    }
    size_long = ftell(fp);
    if (size_long < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return FALSE;
    }

    size = (size_t)size_long;
    buffer = size ? (BYTE *)malloc(size) : NULL;
    if (size > 0 && !buffer) {
        fclose(fp);
        return FALSE;
    }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer);
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    ok = WriterAddData(mpq, name, buffer, (DWORD)size);
    free(buffer);
    return ok;
}

BOOL SFileAddFileFromBuffer(HANDLE archive, LPCSTR archivedName, const void *data, DWORD size)
{
    MPQ_ARCHIVE *mpq = (MPQ_ARCHIVE *)archive;

    if (!mpq || !mpq->write_mode || !archivedName || (size > 0 && !data)) {
        return FALSE;
    }

    return WriterAddData(mpq, archivedName, (const BYTE *)data, size);
}

BOOL SFileCloseArchive(HANDLE archive)
{
    MPQ_ARCHIVE *mpq = (MPQ_ARCHIVE *)archive;

    if (!mpq) {
        return FALSE;
    }

    if (mpq->write_mode) {
        BOOL ok = FinalizeCreatedArchive(mpq);
        FreeWriterEntries(mpq);
        if (mpq->fp) fclose(mpq->fp);
        free(mpq);
        return ok;
    }

    MpqFreeReadArchive(mpq);

    return TRUE;
}

static BOOL SFileOpenFileDirect(HANDLE archive, LPCSTR fileName, DWORD searchScope, HANDLE *file)
{
    MPQ_ARCHIVE *mpq = (MPQ_ARCHIVE *)archive;
    MPQ_FILE *mpqfile;
    DWORD hash1, hash2;
    DWORD block_index;

    if (!mpq || !fileName || !file || searchScope != SFILE_OPEN_FROM_MPQ) {
        return FALSE;
    }

    // Calculate file name hashes
    hash1 = HashString(fileName, MPQ_HASH_NAME_A);
    hash2 = HashString(fileName, MPQ_HASH_NAME_B);

    if (!LookupCachedBlock(mpq, fileName, &block_index)) {
        if (!FindBlockIndex(mpq, fileName, hash1, hash2, &block_index)) {
            return FALSE;
        }
    }

    if (block_index >= mpq->header.dwBlockTableSize) {
        return FALSE;
    }

    mpqfile = (MPQ_FILE *)malloc(sizeof(MPQ_FILE));
    if (!mpqfile) {
        return FALSE;
    }

    memset(mpqfile, 0, sizeof(MPQ_FILE));
    mpqfile->archive = mpq;
    mpqfile->block_index = block_index;
    mpqfile->file_size = mpq->blocktable[block_index].dwFileSize;
    mpqfile->compressed_size = mpq->blocktable[block_index].dwBlockSize;
    mpqfile->flags = mpq->blocktable[block_index].dwFlags;
    mpqfile->current_pos = 0;
    mpqfile->file_key = 0;
    mpqfile->sector_count = 0;
    mpqfile->sector_offsets = NULL;

    if (mpqfile->flags & MPQ_FILE_ENCRYPTED) {
        mpqfile->file_key = HashString(BaseNamePtr(fileName), MPQ_HASH_FILE_KEY);
        if (mpqfile->flags & MPQ_FILE_KEY_V2) {
            mpqfile->file_key = (mpqfile->file_key + mpq->blocktable[block_index].dwBlockOffset) ^ mpqfile->file_size;
        }
    }

    if ((mpqfile->flags & MPQ_FILE_COMPRESS) && !(mpqfile->flags & MPQ_FILE_SINGLE_UNIT)) {
        DWORD sector_count;
        DWORD offset_count;
        MPQ_BLOCK_ENTRY *block;
        BOOL offsets_valid = FALSE;

        block = &mpq->blocktable[block_index];
        sector_count = (mpqfile->file_size + mpq->sector_size - 1) / mpq->sector_size;
        offset_count = sector_count + 1;
        mpqfile->sector_offsets = (DWORD *)malloc(offset_count * sizeof(DWORD));
        if (!mpqfile->sector_offsets) {
            free(mpqfile);
            return FALSE;
        }

        MpqSeek(mpq, block->dwBlockOffset);
        if (MpqRead(mpq, mpqfile->sector_offsets, offset_count * sizeof(DWORD)) != offset_count * sizeof(DWORD)) {
            free(mpqfile->sector_offsets);
            free(mpqfile);
            return FALSE;
        }

        offsets_valid = SectorTableLooksValid(mpqfile->sector_offsets, sector_count, mpqfile->compressed_size);
        if (!offsets_valid && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock((LPBYTE)mpqfile->sector_offsets, offset_count * sizeof(DWORD), mpqfile->file_key - 1);
            offsets_valid = SectorTableLooksValid(mpqfile->sector_offsets, sector_count, mpqfile->compressed_size);
        }
        if (!offsets_valid) {
            free(mpqfile->sector_offsets);
            free(mpqfile);
            return FALSE;
        }

        mpqfile->sector_count = sector_count;
    }

    *file = (HANDLE)mpqfile;
    return TRUE;
}

static BOOL SFileOpenNestedFile(HANDLE archive, LPCSTR fileName, DWORD searchScope, HANDLE *file)
{
    char outerName[1024];
    LPCSTR innerName;

    if (!fileName) {
        return FALSE;
    }

    for (size_t i = 0; fileName[i]; i++) {
        HANDLE outerFile;
        DWORD outerSize;
        DWORD bytesRead = 0;
        BYTE *outerData;
        HANDLE nestedArchive;

        if (fileName[i] != '.' || !HasArchiveExtensionAt(fileName, i)) {
            continue;
        }
        if (i + 4 >= sizeof(outerName)) {
            continue;
        }

        memcpy(outerName, fileName, i + 4);
        outerName[i + 4] = '\0';
        innerName = fileName + i + 5;
        while (*innerName == '/' || *innerName == '\\') {
            innerName++;
        }
        if (!*innerName) {
            continue;
        }

        if (!SFileOpenFileDirect(archive, outerName, searchScope, &outerFile)) {
            continue;
        }

        outerSize = SFileGetFileSize(outerFile, NULL);
        outerData = (BYTE *)malloc(outerSize);
        if (!outerData) {
            SFileCloseFile(outerFile);
            return FALSE;
        }
        if (!SFileReadFile(outerFile, outerData, outerSize, &bytesRead, NULL) || bytesRead != outerSize) {
            free(outerData);
            SFileCloseFile(outerFile);
            continue;
        }
        SFileCloseFile(outerFile);

        if (!SFileOpenArchiveFromMemory(outerData, outerSize, 0, &nestedArchive)) {
            free(outerData);
            continue;
        }
        if (SFileOpenFileDirect(nestedArchive, innerName, searchScope, file)) {
            MPQ_FILE *mpqfile = (MPQ_FILE *)*file;
            mpqfile->owner_archive = nestedArchive;
            mpqfile->owner_memory = outerData;
            return TRUE;
        }

        SFileCloseArchive(nestedArchive);
        free(outerData);
    }

    return FALSE;
}

BOOL SFileOpenFileEx(HANDLE archive, LPCSTR fileName, DWORD searchScope, HANDLE *file)
{
    if (SFileOpenFileDirect(archive, fileName, searchScope, file)) {
        return TRUE;
    }
    return SFileOpenNestedFile(archive, fileName, searchScope, file);
}

BOOL SFileOpenFileFromArchiveMemory(BYTE *data, DWORD size, LPCSTR fileName, DWORD searchScope, HANDLE *file)
{
    HANDLE nestedArchive;

    if (!data || size == 0 || !fileName || !*fileName || !file ||
        searchScope != SFILE_OPEN_FROM_MPQ) {
        return FALSE;
    }
    if (!SFileOpenArchiveFromMemory(data, size, 0, &nestedArchive)) {
        return FALSE;
    }
    if (SFileOpenFileDirect(nestedArchive, fileName, searchScope, file)) {
        MPQ_FILE *mpqfile = (MPQ_FILE *)*file;

        mpqfile->owner_archive = nestedArchive;
        mpqfile->owner_memory = data;
        return TRUE;
    }
    SFileCloseArchive(nestedArchive);
    return FALSE;
}

BOOL SFileCloseFile(HANDLE file)
{
    if (file) {
        MPQ_FILE *mpqfile = (MPQ_FILE *)file;
        HANDLE owner_archive = mpqfile->owner_archive;
        BYTE *owner_memory = mpqfile->owner_memory;

        if (mpqfile->sector_offsets) {
            free(mpqfile->sector_offsets);
        }
        free(file);
        if (owner_archive) {
            SFileCloseArchive(owner_archive);
        }
        if (owner_memory) {
            free(owner_memory);
        }
    }
    return TRUE;
}

BOOL SFileReadFile(HANDLE file, void *buffer, DWORD toRead, LPDWORD bytesRead, LPOVERLAPPED overlapped)
{
    MPQ_FILE *mpqfile = (MPQ_FILE *)file;
    MPQ_ARCHIVE *mpq;
    MPQ_BLOCK_ENTRY *block;
    BYTE *dest = (BYTE *)buffer;
    DWORD read_so_far = 0;
    DWORD to_read_in_block;
    DWORD sector_start;
    DWORD sector_offset;
    DWORD bytes_in_sector;
    DWORD file_offset;

    (void)overlapped;

    if (!mpqfile || !buffer || toRead == 0) {
        if (bytesRead) *bytesRead = 0;
        return FALSE;
    }

    mpq = mpqfile->archive;
    block = &mpq->blocktable[mpqfile->block_index];

    if (mpqfile->current_pos >= mpqfile->file_size) {
        if (bytesRead) *bytesRead = 0;
        return TRUE;
    }

    // Limit read to file size
    if (mpqfile->current_pos + toRead > mpqfile->file_size) {
        toRead = mpqfile->file_size - mpqfile->current_pos;
    }

    file_offset = mpqfile->current_pos;

    // Determine if file is compressed
    if (!(block->dwFlags & MPQ_FILE_COMPRESS)) {
        // Uncompressed file
        MpqSeek(mpq, block->dwBlockOffset + file_offset);
        DWORD actually_read = (DWORD)MpqRead(mpq, dest, toRead);
        if (actually_read > 0 && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock(dest, actually_read, mpqfile->file_key);
        }
        mpqfile->current_pos += actually_read;
        if (bytesRead) *bytesRead = actually_read;
        return actually_read > 0;
    }

    if (block->dwFlags & MPQ_FILE_SINGLE_UNIT) {
        BYTE *compressed;
        BYTE *whole_file;
        DWORD bytes_in_file = 0;
        BOOL ok;

        compressed = (BYTE *)malloc(block->dwBlockSize ? block->dwBlockSize : 1);
        whole_file = (BYTE *)malloc(mpqfile->file_size ? mpqfile->file_size : 1);
        if (!compressed || !whole_file) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return FALSE;
        }

        MpqSeek(mpq, block->dwBlockOffset);
        if (MpqRead(mpq, compressed, block->dwBlockSize) != block->dwBlockSize) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return FALSE;
        }

        ok = TryInflateSector(compressed, block->dwBlockSize, mpqfile->file_size, whole_file, &bytes_in_file);
        if (!ok && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock(compressed, block->dwBlockSize, mpqfile->file_key);
            ok = TryInflateSector(compressed, block->dwBlockSize, mpqfile->file_size, whole_file, &bytes_in_file);
        }
        if (!ok && block->dwBlockSize >= mpqfile->file_size) {
            memcpy(whole_file, compressed, mpqfile->file_size);
            bytes_in_file = mpqfile->file_size;
            ok = TRUE;
        }

        if (!ok || file_offset >= bytes_in_file) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return FALSE;
        }

        if (toRead > bytes_in_file - file_offset) {
            toRead = bytes_in_file - file_offset;
        }
        memcpy(dest, whole_file + file_offset, toRead);
        mpqfile->current_pos += toRead;
        if (bytesRead) *bytesRead = toRead;

        free(compressed);
        free(whole_file);
        return TRUE;
    }

    // File is compressed - read through the sector offset table.
    if (!mpqfile->sector_offsets || mpqfile->sector_count == 0) {
        if (bytesRead) *bytesRead = 0;
        return FALSE;
    }

    sector_start = (file_offset / mpq->sector_size);
    sector_offset = (file_offset % mpq->sector_size);

    while (read_so_far < toRead && sector_start < mpqfile->sector_count) {
        DWORD sector_start_offset;
        DWORD sector_end_offset;
        DWORD sector_compressed_size;
        DWORD sector_uncompressed_size;
        BYTE *compressed;

        sector_start_offset = mpqfile->sector_offsets[sector_start];
        sector_end_offset = mpqfile->sector_offsets[sector_start + 1];

        if (sector_end_offset < sector_start_offset) {
            break;
        }

        sector_compressed_size = sector_end_offset - sector_start_offset;
        sector_uncompressed_size = mpq->sector_size;
        if (sector_start == mpqfile->sector_count - 1) {
            DWORD tail = mpqfile->file_size % mpq->sector_size;
            if (tail != 0) {
                sector_uncompressed_size = tail;
            }
        }

        compressed = (BYTE *)malloc(sector_compressed_size);
        if (!compressed) {
            break;
        }

        MpqSeek(mpq, block->dwBlockOffset + sector_start_offset);
        if (MpqRead(mpq, compressed, sector_compressed_size) != sector_compressed_size) {
            free(compressed);
            break;
        }

        if (!TryInflateSector(compressed, sector_compressed_size, sector_uncompressed_size, mpq->sector_buffer, &bytes_in_sector)) {
            if (mpqfile->flags & MPQ_FILE_ENCRYPTED) {
                DecryptBlock(compressed, sector_compressed_size, mpqfile->file_key + sector_start);
                if (!TryInflateSector(compressed, sector_compressed_size, sector_uncompressed_size, mpq->sector_buffer, &bytes_in_sector)) {
                    free(compressed);
                    break;
                }
            } else if (sector_compressed_size >= sector_uncompressed_size) {
                memcpy(mpq->sector_buffer, compressed, sector_uncompressed_size);
                bytes_in_sector = sector_uncompressed_size;
            } else {
                free(compressed);
                break;
            }
        }
        free(compressed);

        if (sector_offset >= bytes_in_sector) {
            break;
        }

        to_read_in_block = bytes_in_sector - sector_offset;
        if (to_read_in_block > (toRead - read_so_far)) {
            to_read_in_block = (toRead - read_so_far);
        }

        memcpy(dest, &mpq->sector_buffer[sector_offset], to_read_in_block);
        dest += to_read_in_block;
        read_so_far += to_read_in_block;
        sector_offset = 0;
        sector_start++;
    }

    mpqfile->current_pos += read_so_far;
    if (bytesRead) *bytesRead = read_so_far;

    return read_so_far > 0 || toRead == 0;
}

DWORD SFileGetFileSize(HANDLE file, LPDWORD highSize)
{
    MPQ_FILE *mpqfile = (MPQ_FILE *)file;

    if (!mpqfile) {
        return 0;
    }

    if (highSize) {
        *highSize = 0;
    }

    return mpqfile->file_size;
}

DWORD SFileSetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD moveMethod)
{
    MPQ_FILE *mpqfile = (MPQ_FILE *)file;
    DWORD new_pos;

    if (!mpqfile) {
        return SFILE_INVALID_POS;
    }

    (void)distanceHigh;

    switch (moveMethod) {
        case FILE_BEGIN:
            new_pos = distance;
            break;
        case FILE_CURRENT:
            new_pos = mpqfile->current_pos + distance;
            break;
        case FILE_END:
            new_pos = mpqfile->file_size + distance;
            break;
        default:
            return SFILE_INVALID_POS;
    }

    if (new_pos > mpqfile->file_size) {
        return SFILE_INVALID_POS;
    }

    mpqfile->current_pos = new_pos;
    return new_pos;
}

BOOL SFileExtractFile(HANDLE archive, LPCSTR toExtract, LPCSTR extracted, DWORD flags)
{
    HANDLE file;
    FILE *out;
    DWORD file_size;
    DWORD bytes_read;
    BYTE buffer[65536];
    BOOL success = FALSE;

    (void)flags;

    if (!SFileOpenFileEx(archive, toExtract, SFILE_OPEN_FROM_MPQ, &file)) {
        return FALSE;
    }

    file_size = SFileGetFileSize(file, NULL);
    out = fopen(extracted, "wb");
    if (!out) {
        SFileCloseFile(file);
        return FALSE;
    }

    while (file_size > 0) {
        DWORD to_read = (file_size > sizeof(buffer)) ? sizeof(buffer) : file_size;
        if (!SFileReadFile(file, buffer, to_read, &bytes_read, NULL) || bytes_read == 0) {
            break;
        }
        if (fwrite(buffer, 1, bytes_read, out) != bytes_read) {
            break;
        }
        file_size -= bytes_read;
    }

    if (file_size == 0) {
        success = TRUE;
    }

    fclose(out);
    SFileCloseFile(file);
    return success;
}

static DWORD PreloadBucketHash(DWORD hash1, DWORD hash2)
{
    return hash1 ^ (hash2 * 16777619u);
}

static void PreloadListfileCache(MPQ_ARCHIVE *mpq)
{
    HANDLE list_file;
    DWORD list_size;
    BYTE *buffer = NULL;
    DWORD bytes_read = 0;
    char *cursor;
    char *line;
    size_t entry_count = 0;
    size_t bucket_count = 0;
    MPQ_LISTFILE_ENTRY *entries = NULL;
    MPQ_LISTFILE_BUCKET_ENTRY **buckets = NULL;
    size_t i;
    BOOL trace = getenv("BZ_MPQ_TRACE") != NULL;

    if (!mpq || !mpq->hashtable || !mpq->blocktable) {
        return;
    }

    if (!SFileOpenFileEx((HANDLE)mpq, "(listfile)", SFILE_OPEN_FROM_MPQ, &list_file)) {
        return;
    }

    list_size = SFileGetFileSize(list_file, NULL);
    if (list_size == 0) {
        SFileCloseFile(list_file);
        return;
    }

    buffer = (BYTE *)malloc(list_size + 1);
    if (!buffer) {
        SFileCloseFile(list_file);
        return;
    }

    if (!SFileReadFile(list_file, buffer, list_size, &bytes_read, NULL) || bytes_read == 0) {
        free(buffer);
        SFileCloseFile(list_file);
        return;
    }
    buffer[bytes_read] = '\0';
    SFileCloseFile(list_file);

    cursor = (char *)buffer;
    while (*cursor) {
        while (*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
        if (!*cursor) {
            break;
        }
        line = cursor;
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
        }
        if (*line == '\0') {
            continue;
        }
        entry_count++;
    }

    if (entry_count == 0) {
        free(buffer);
        return;
    }

    entries = (MPQ_LISTFILE_ENTRY *)calloc(entry_count, sizeof(MPQ_LISTFILE_ENTRY));
    if (!entries) {
        free(buffer);
        return;
    }

    cursor = (char *)buffer;
    for (i = 0; i < entry_count; i++) {
        while (*cursor == '\r' || *cursor == '\n') {
            cursor++;
        }
        line = cursor;
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            cursor++;
        }
        if (*cursor) {
            *cursor++ = '\0';
        }

        entries[i].name = (char *)malloc(strlen(line) + 1);
        if (!entries[i].name) {
            break;
        }
        memcpy(entries[i].name, line, strlen(line) + 1);
        CanonicalizeMpqKey(entries[i].name, entries[i].name, strlen(entries[i].name) + 1);
        entries[i].hash1 = HashString(entries[i].name, MPQ_HASH_NAME_A);
        entries[i].hash2 = HashString(entries[i].name, MPQ_HASH_NAME_B);
    }

    if (i != entry_count) {
        for (i = 0; i < entry_count; i++) {
            free(entries[i].name);
        }
        free(entries);
        free(buffer);
        return;
    }

    bucket_count = 1;
    while (bucket_count < entry_count * 2) {
        bucket_count <<= 1;
    }

    buckets = (MPQ_LISTFILE_BUCKET_ENTRY **)calloc(bucket_count, sizeof(MPQ_LISTFILE_BUCKET_ENTRY *));
    if (!buckets) {
        for (i = 0; i < entry_count; i++) {
            free(entries[i].name);
        }
        free(entries);
        free(buffer);
        return;
    }

    for (i = 0; i < entry_count; i++) {
        MPQ_LISTFILE_BUCKET_ENTRY *node = (MPQ_LISTFILE_BUCKET_ENTRY *)malloc(sizeof(MPQ_LISTFILE_BUCKET_ENTRY));
        DWORD slot;

        if (!node) {
            continue;
        }
        slot = PreloadBucketHash(entries[i].hash1, entries[i].hash2) & (bucket_count - 1);
        node->entry = &entries[i];
        node->next = buckets[slot];
        buckets[slot] = node;
    }

    for (i = 0; i < mpq->header.dwHashTableSize; i++) {
        MPQ_HASH_ENTRY *hash_entry = &mpq->hashtable[i];
        DWORD slot;
        MPQ_LISTFILE_BUCKET_ENTRY *node;

        if (hash_entry->dwBlockIndex == MPQ_HASH_ENTRY_FREE ||
            hash_entry->dwBlockIndex == MPQ_HASH_ENTRY_DELETED) {
            continue;
        }

        slot = PreloadBucketHash(hash_entry->dwNameHash1, hash_entry->dwNameHash2) & (bucket_count - 1);
        node = buckets[slot];
        while (node) {
            if (node->entry->hash1 == hash_entry->dwNameHash1 &&
                node->entry->hash2 == hash_entry->dwNameHash2) {
                CacheBlockLookup(mpq, node->entry->name, hash_entry->dwBlockIndex & MPQ_BLOCK_INDEX_MASK);
                break;
            }
            node = node->next;
        }
    }

    if (trace) {
        fprintf(stderr, "MPQ preload: cached %zu listfile names\n", entry_count);
    }

    for (i = 0; i < bucket_count; i++) {
        MPQ_LISTFILE_BUCKET_ENTRY *node = buckets[i];
        while (node) {
            MPQ_LISTFILE_BUCKET_ENTRY *next = node->next;
            free(node);
            node = next;
        }
    }
    for (i = 0; i < entry_count; i++) {
        free(entries[i].name);
    }
    free(entries);
    free(buckets);
    free(buffer);
}

static BOOL FilenameMatches(const char *filename, const char *mask);
static void FreeFindList(MPQ_FIND *find);
static BOOL AppendFindListEntry(MPQ_FIND *find, const char *name);

HANDLE SFileFindFirstFile(HANDLE archive, LPCSTR mask, SFILE_FIND_DATA *findData, LPCSTR listFile)
{
    MPQ_ARCHIVE *mpq = (MPQ_ARCHIVE *)archive;
    MPQ_FIND *find;
    HANDLE list_file;
    DWORD list_size;
    BYTE *list_buffer;
    DWORD bytes_read;
    char *cursor;

    (void)listFile;

    if (!mpq || !mask || !findData) {
        return NULL;
    }

    find = (MPQ_FIND *)malloc(sizeof(MPQ_FIND));
    if (!find) {
        return NULL;
    }

    memset(find, 0, sizeof(MPQ_FIND));
    find->archive = mpq;
    find->current_index = 0;
    find->file_count = 0;
    find->files = NULL;
    strncpy(find->mask, mask, sizeof(find->mask) - 1);

    if (!SFileOpenFileEx(archive, "(listfile)", SFILE_OPEN_FROM_MPQ, &list_file)) {
        free(find);
        return NULL;
    }

    list_size = SFileGetFileSize(list_file, NULL);
    list_buffer = (BYTE *)malloc(list_size + 1);
    if (!list_buffer) {
        SFileCloseFile(list_file);
        free(find);
        return NULL;
    }

    if (!SFileReadFile(list_file, list_buffer, list_size, &bytes_read, NULL) || bytes_read != list_size) {
        free(list_buffer);
        SFileCloseFile(list_file);
        FreeFindList(find);
        free(find);
        return NULL;
    }
    list_buffer[list_size] = '\0';
    SFileCloseFile(list_file);

    cursor = (char *)list_buffer;
    while (*cursor) {
        char *line_end = cursor;

        while (*line_end && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }

        if (line_end > cursor) {
            char saved = *line_end;

            *line_end = '\0';
            NormalizeMpqPath(cursor);
            TrimEdgeSlashes(cursor);
            if (FilenameMatches(cursor, mask)) {
                if (!AppendFindListEntry(find, cursor)) {
                    free(list_buffer);
                    FreeFindList(find);
                    free(find);
                    return NULL;
                }
            }
            *line_end = saved;
        }

        while (*line_end == '\r' || *line_end == '\n') {
            line_end++;
        }
        cursor = line_end;
    }

    free(list_buffer);

    if (!SFileFindNextFile((HANDLE)find, findData)) {
        FreeFindList(find);
        free(find);
        return NULL;
    }

    return (HANDLE)find;
}

static BOOL FilenameMatches(const char *filename, const char *mask)
{
    // Simple wildcard matching - "*" matches everything
    if (!mask || !mask[0] || strcmp(mask, "*") == 0) {
        return TRUE;
    }

    // For now, simple prefix matching
    size_t mask_len = strlen(mask);
    if (mask[mask_len - 1] == '*') {
        return strncmp(filename, mask, mask_len - 1) == 0;
    }

    return strcmp(filename, mask) == 0;
}

static void FreeFindList(MPQ_FIND *find)
{
    DWORD i;

    if (!find) {
        return;
    }

    for (i = 0; i < find->file_count; i++) {
        free(find->files[i]);
    }
    free(find->files);
    find->files = NULL;
    find->file_count = 0;
}

static BOOL AppendFindListEntry(MPQ_FIND *find, const char *name)
{
    char **next;
    char *copy;

    next = (char **)realloc(find->files, (find->file_count + 1) * sizeof(*next));
    if (!next) {
        return FALSE;
    }

    copy = (char *)malloc(strlen(name) + 1);
    if (!copy) {
        return FALSE;
    }

    memcpy(copy, name, strlen(name) + 1);
    find->files = next;
    find->files[find->file_count++] = copy;
    return TRUE;
}

BOOL SFileFindNextFile(HANDLE find, SFILE_FIND_DATA *findData)
{
    MPQ_FIND *mpqfind = (MPQ_FIND *)find;
    const char *name;
    DWORD block_index;

    if (!mpqfind || !findData) {
        return FALSE;
    }

    if (mpqfind->current_index >= mpqfind->file_count) {
        return FALSE;
    }

    name = mpqfind->files[mpqfind->current_index++];
    memset(findData, 0, sizeof(*findData));
    strncpy(findData->cFileName, name, sizeof(findData->cFileName) - 1);
    findData->cFileName[sizeof(findData->cFileName) - 1] = '\0';
    findData->szPlainName = findData->cFileName;
    if (LookupCachedBlock(mpqfind->archive, name, &block_index) ||
        FindBlockIndex(mpqfind->archive,
                       name,
                       HashString(name, MPQ_HASH_NAME_A),
                       HashString(name, MPQ_HASH_NAME_B),
                       &block_index)) {
        MPQ_BLOCK_ENTRY *block = &mpqfind->archive->blocktable[block_index];

        findData->dwBlockIndex = block_index;
        findData->dwFileSize = block->dwFileSize;
        findData->dwCompSize = block->dwBlockSize;
        findData->dwFileFlags = block->dwFlags;
    }
    return TRUE;
}

BOOL SFileFindClose(HANDLE find)
{
    if (find) {
        FreeFindList((MPQ_FIND *)find);
        free(find);
    }
    return TRUE;
}
