/*
 * mpq.c — Pure C MPQ archive reader (War3.mpq compatible)
 *
 * Implements reading of Warcraft III MPQ archives without external dependencies.
 * Supports:
 *  - MPQ v1/v2 format
 *  - File lookup by name
 *  - ZLIB, PKWARE DCL, adaptive Huffman, and Blizzard ADPCM decompression
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
#include "vendor/blast/blast.h"

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#define MPQ_HASH_NAME_A 1
#define MPQ_HASH_NAME_B 2
#define MPQ_HASH_FILE_KEY 3
#define MPQ_KEY_HASH_TABLE 0xC3AF3770
#define MPQ_KEY_BLOCK_TABLE 0xEC83B3A3
#define MPQ_HASH_ENTRY_DELETED 0xFFFFFFFE
#define MPQ_HASH_ENTRY_FREE 0xFFFFFFFF
#define MPQ_BLOCK_INDEX_MASK 0x0FFFFFFF
#define MPQ_SECTOR_SIZE_SHIFT_MAX 15 // Storm max; 1<<(15+9) = 16 MiB sectors (protected WC3 maps)
#define MPQ_FILE_COMPRESS 0x00000200
#define MPQ_FILE_IMPLODE 0x00000100
#define MPQ_FILE_ENCRYPTED 0x00010000
#define MPQ_FILE_KEY_V2 0x00020000
#define MPQ_FILE_SINGLE_UNIT 0x01000000
#define MPQ_COMPRESSION_ZLIB 0x02
#define MPQ_FILE_EXISTS 0x80000000u

#define MPQ_COMP_HUFF         0x01 // mask bit; adaptive Huffman compression
#define MPQ_COMP_PKWARE       0x08 // mask bit; PKWARE DCL compression
#define MPQ_COMP_ADPCM_MONO   0x40 // mask bit; one-channel Blizzard ADPCM
#define MPQ_COMP_ADPCM_STEREO 0x80 // mask bit; two-channel Blizzard ADPCM
#define MPQ_HUFF_SYMBOLS      258 // symbols; 256 bytes plus end and escape markers
#define MPQ_HUFF_NODES        515 // nodes; full adaptive tree capacity
#define MPQ_ADPCM_INIT_STEP   44 // table index; Blizzard ADPCM initial quantizer step

typedef struct mpqHuffNode_s {
    struct mpqHuffNode_s *next, *prev, *parent, *low;
    uint32_t value, weight;
} mpqHuffNode_t;

typedef struct {
    mpqHuffNode_t head, nodes[MPQ_HUFF_NODES];
    mpqHuffNode_t *symbols[MPQ_HUFF_SYMBOLS];
    uint32_t used;
} mpqHuffTree_t;

typedef struct {
    uint8_t const *cur, *end;
    uint32_t bits, count;
} mpqBitReader_t;

typedef struct {
    uint8_t const *src;
    uint32_t src_size;
    uint8_t *dst;
    uint32_t dst_size;
    uint32_t out_size;
} mpqDecompress_t;

/* WC3 voice sectors use profile 7: a stereo-oriented distribution also used
 * for Blizzard's ordinary 5-bit ADPCM stream before channel reconstruction. */
static uint8_t const mpq_huff_profile7[256] = {
    0xc3,0xd9,0xef,0x3d,0xf9,0x7c,0xe9,0x1e,0xfd,0xab,0xf1,0x2c,0xfc,0x5b,0xfe,0x17,
    [64]=0xbd,0xd9,0xec,0x3d,0xf5,0x7d,0xe8,0x1d,0xfb,0xae,0xf0,0x2c,0xfb,0x5c,0xff,0x18,
    [128]=0x70,0x6c
};

static int const mpq_adpcm_next[32] = {
    -1,0,-1,4,-1,2,-1,6,-1,1,-1,5,-1,3,-1,7,
    -1,1,-1,5,-1,3,-1,7,-1,2,-1,4,-1,6,-1,8
};

static int const mpq_adpcm_step[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
    130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,
    1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,
    6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,
    29794,32767
};

static void Mpq_HuffLink(mpqHuffNode_t *at, mpqHuffNode_t *node) {
    node->next = at->next; node->prev = at; at->next->prev = node; at->next = node;
}

static void Mpq_HuffRemove(mpqHuffNode_t *node) {
    if (!node->next) return;
    node->prev->next = node->next; node->next->prev = node->prev; node->next = node->prev = NULL;
}

static mpqHuffNode_t *Mpq_HuffNew(mpqHuffTree_t *tree, uint32_t value, uint32_t weight, mpqHuffNode_t *at) {
    if (tree->used >= MPQ_HUFF_NODES) return NULL;
    mpqHuffNode_t *node = &tree->nodes[tree->used++];
    memset(node, 0, sizeof(*node)); node->value = value; node->weight = weight; Mpq_HuffLink(at, node);
    return node;
}

static mpqHuffNode_t *Mpq_HuffHigher(mpqHuffTree_t *tree, mpqHuffNode_t *node, uint32_t weight) {
    for (; node != &tree->head; node = node->prev) if (node->weight >= weight) return node;
    return &tree->head;
}

static uint32_t Mpq_HuffFix(mpqHuffTree_t *tree, mpqHuffNode_t *node, uint32_t max_weight) {
    if (node->weight >= max_weight) return node->weight;
    mpqHuffNode_t *higher = Mpq_HuffHigher(tree, tree->head.prev, node->weight);
    Mpq_HuffRemove(node); Mpq_HuffLink(higher, node);
    return max_weight;
}

/* Build the exact initial FGK tree selected by the stream's profile byte. */
static bool Mpq_HuffBuild(mpqHuffTree_t *tree, uint32_t profile) {
    if (profile != 7) return false;
    memset(tree, 0, sizeof(*tree)); tree->head.next = tree->head.prev = &tree->head;
    uint32_t max_weight = 0;
    FOR_LOOP(i, 256) {
        if (!mpq_huff_profile7[i]) continue;
        mpqHuffNode_t *node = Mpq_HuffNew(tree, i, mpq_huff_profile7[i], &tree->head);
        if (!node) return false;
        tree->symbols[i] = node; max_weight = Mpq_HuffFix(tree, node, max_weight);
    }
    tree->symbols[256] = Mpq_HuffNew(tree, 256, 1, tree->head.prev);
    tree->symbols[257] = Mpq_HuffNew(tree, 257, 1, tree->head.prev);
    for (mpqHuffNode_t *low = tree->head.prev; low != &tree->head;) {
        mpqHuffNode_t *high = low->prev;
        if (high == &tree->head) break;
        mpqHuffNode_t *parent = Mpq_HuffNew(tree, 0, high->weight + low->weight, &tree->head);
        if (!parent) return false;
        low->parent = high->parent = parent; parent->low = low;
        max_weight = Mpq_HuffFix(tree, parent, max_weight); low = high->prev;
    }
    return true;
}

static void Mpq_HuffRebalance(mpqHuffTree_t *tree, mpqHuffNode_t *node) {
    for (; node; node = node->parent) {
        node->weight++;
        mpqHuffNode_t *higher = Mpq_HuffHigher(tree, node->prev, node->weight);
        mpqHuffNode_t *swap = higher->next;
        if (swap == node) continue;
        Mpq_HuffRemove(swap); Mpq_HuffLink(node, swap);
        Mpq_HuffRemove(node); Mpq_HuffLink(higher, node);
        mpqHuffNode_t *low = swap->parent->low, *parent = node->parent;
        if (parent->low == node) parent->low = swap;
        if (low == swap) swap->parent->low = node;
        node->parent = swap->parent; swap->parent = parent;
    }
}

static bool Mpq_HuffInsert(mpqHuffTree_t *tree, uint32_t value) {
    mpqHuffNode_t *escape = tree->head.prev;
    mpqHuffNode_t *high = Mpq_HuffNew(tree, escape->value, escape->weight, tree->head.prev);
    mpqHuffNode_t *low = Mpq_HuffNew(tree, value, 0, tree->head.prev);
    if (!high || !low) return false;
    high->parent = low->parent = escape; escape->low = low;
    tree->symbols[value] = low; Mpq_HuffRebalance(tree, low);
    return true;
}

static bool Mpq_ReadBits(mpqBitReader_t *r, uint32_t count, uint32_t *value) {
    while (r->count < count) {
        if (r->cur >= r->end) return false;
        r->bits |= (uint32_t)*r->cur++ << r->count; r->count += 8;
    }
    *value = r->bits & ((1u << count) - 1); r->bits >>= count; r->count -= count;
    return true;
}

static bool Mpq_HuffDecode(uint8_t const *src, uint32_t src_size, uint8_t *dst, uint32_t cap, uint32_t *out_size) {
    mpqBitReader_t bits = { src, src + src_size, 0, 0 };
    mpqHuffTree_t tree;
    uint32_t profile, used = 0;
    if (!Mpq_ReadBits(&bits, 8, &profile) || !Mpq_HuffBuild(&tree, profile)) return false;
    while (used < cap) {
        mpqHuffNode_t *node = tree.head.next;
        while (node->low) {
            uint32_t bit;
            if (!Mpq_ReadBits(&bits, 1, &bit)) return false;
            node = bit ? node->low->prev : node->low;
        }
        uint32_t value = node->value;
        if (value == 256) { *out_size = used; return true; }
        if (value == 257) {
            if (!Mpq_ReadBits(&bits, 8, &value) || !Mpq_HuffInsert(&tree, value)) return false;
            node = tree.symbols[value]; Mpq_HuffRebalance(&tree, node);
        }
        dst[used++] = (uint8_t)value;
    }
    *out_size = used;
    return true;
}

static void Mpq_WriteShort(uint8_t **dst, int value) {
    (*dst)[0] = (uint8_t)value; (*dst)[1] = (uint8_t)(value >> 8); *dst += 2;
}

/* Expand Blizzard's byte-coded differential samples into interleaved PCM16. */
static bool Mpq_AdpcmDecode(uint8_t const *src, uint32_t src_size, uint8_t *dst, uint32_t cap, uint32_t channels, uint32_t *out_size) {
    if (channels < 1 || channels > 2 || src_size < 2 + channels * 2 || cap < channels * 2) return false;
    uint8_t const *cur = src + 2, *end = src + src_size;
    uint8_t *out = dst, *out_end = dst + cap;
    int shift = src[1], channel = (int)channels - 1;
    int predicted[2] = {0}, step_index[2] = { MPQ_ADPCM_INIT_STEP, MPQ_ADPCM_INIT_STEP };
    FOR_LOOP(i, channels) {
        predicted[i] = (short)(cur[0] | (cur[1] << 8)); cur += 2; Mpq_WriteShort(&out, predicted[i]);
    }
    while (cur < end) {
        int code = *cur++;
        channel = (channel + 1) % (int)channels;
        if (code == 0x80) {
            if (step_index[channel]) step_index[channel]--;
        } else if (code == 0x81) {
            step_index[channel] += 8;
            if (step_index[channel] > 88) step_index[channel] = 88;
            channel = (channel + 1) % (int)channels;
            continue;
        } else {
            int step = mpq_adpcm_step[step_index[channel]], diff = step >> shift;
            FOR_LOOP(bit, 6) if (code & (1 << bit)) diff += step >> bit;
            predicted[channel] += (code & 0x40) ? -diff : diff;
            if (predicted[channel] < -32768) predicted[channel] = -32768;
            if (predicted[channel] > 32767) predicted[channel] = 32767;
            step_index[channel] += mpq_adpcm_next[code & 0x1f];
            if (step_index[channel] < 0) step_index[channel] = 0;
            if (step_index[channel] > 88) step_index[channel] = 88;
        }
        if (out_end - out < 2) break;
        Mpq_WriteShort(&out, predicted[channel]);
    }
    *out_size = (uint32_t)(out - dst);
    return *out_size != 0;
}

static unsigned Mpq_BlastInput(void *how, unsigned char **buf) { return 0; }

static int Mpq_BlastOutput(void *how, unsigned char *buf, unsigned len) {
    mpqDecompress_t *p = how;
    if (len > p->dst_size - p->out_size) return 1;
    memcpy(p->dst + p->out_size, buf, len); p->out_size += len;
    return 0;
}

/* Decode the MPQ method byte in Blizzard's prescribed method order. */
static bool Mpq_DecompressSector(mpqDecompress_t *p) {
    if (!p || !p->src || !p->dst || !p->src_size || !p->dst_size) return false;
    if (p->src_size == p->dst_size) { memcpy(p->dst, p->src, p->src_size); p->out_size = p->src_size; return true; }
    uint8_t mask = p->src[0];
    if (mask == MPQ_COMP_PKWARE) {
        unsigned left = p->src_size - 1;
        unsigned char *in = (unsigned char *)p->src + 1;
        p->out_size = 0;
        return blast(Mpq_BlastInput, NULL, Mpq_BlastOutput, p, &left, &in) == 0;
    }
    if (mask == MPQ_COMPRESSION_ZLIB) {
        uLongf size = p->dst_size;
        if (uncompress(p->dst, &size, p->src + 1, p->src_size - 1) != Z_OK) return false;
        p->out_size = (uint32_t)size; return true;
    }
    if (mask == MPQ_COMP_ADPCM_MONO || mask == MPQ_COMP_ADPCM_STEREO)
        return Mpq_AdpcmDecode(p->src + 1, p->src_size - 1, p->dst, p->dst_size, mask == MPQ_COMP_ADPCM_STEREO ? 2 : 1, &p->out_size);
    if (mask == (MPQ_COMP_HUFF | MPQ_COMP_ADPCM_MONO) || mask == (MPQ_COMP_HUFF | MPQ_COMP_ADPCM_STEREO)) {
        uint8_t *tmp = malloc(p->dst_size);
        uint32_t tmp_size = 0, channels = mask & MPQ_COMP_ADPCM_STEREO ? 2 : 1;
        if (!tmp) return false;
        bool huff_ok = Mpq_HuffDecode(p->src + 1, p->src_size - 1, tmp, p->dst_size, &tmp_size);
        bool ok = huff_ok && Mpq_AdpcmDecode(tmp, tmp_size, p->dst, p->dst_size, channels, &p->out_size);
        free(tmp); return ok;
    }
    fprintf(stderr, "MPQ: unsupported sector compression mask 0x%02x\n", mask);
    return false;
}

#ifdef MPQ_TEST_API
bool Mpq_TestDecompressSector(uint8_t const *src, uint32_t src_size, uint8_t *dst, uint32_t dst_size, uint32_t *out_size) {
    mpqDecompress_t p = { src, src_size, dst, dst_size, 0 };
    bool ok = Mpq_DecompressSector(&p);
    if (out_size) *out_size = p.out_size;
    return ok;
}
#endif

typedef struct {
    uint32_t dwID;                 // "MPQ\x1a"
    uint32_t dwHeaderSize;         // Size of MPQ header
    uint32_t dwArchiveSize;        // Size of entire archive
    uint16_t wFormatVersion;      // 0 for v1, 1 for v2, etc.
    uint16_t wSectorSizeShift;    // Power of 2 for sector size (usually 12 = 4096)
    uint32_t dwHashTablePos;       // Offset to hash table from archive start
    uint32_t dwBlockTablePos;      // Offset to block table from archive start
    uint32_t dwHashTableSize;      // Number of hash table entries
    uint32_t dwBlockTableSize;     // Number of block table entries
} mpqHeaderV1_t;

typedef struct {
    uint32_t dwNameHash1;          // First name hash
    uint32_t dwNameHash2;          // Second name hash
    uint16_t wLocale;             // Locale
    uint8_t bPlatform;             // Platform
    uint8_t bFlags;                // Flags
    uint32_t dwBlockIndex;         // Block table index (or 0xFFFFFFFF for free/deleted)
} mpqHashEntry_t;

typedef struct {
    uint32_t dwBlockOffset;        // Offset from MPQ header
    uint32_t dwBlockSize;          // Compressed size
    uint32_t dwFileSize;           // Uncompressed size
    uint32_t dwFlags;              // File flags
} mpqBlockEntry_t;

struct mpq_cache_entry;

typedef struct {
    FILE *fp;
    uint8_t const *memory;
    uint32_t memory_size;
    uint32_t memory_pos;
    char filename[256];
    uint32_t base_offset;
    struct mpq_cache_entry **lookup_cache;
    uint32_t lookup_cache_size;
    mpqHeaderV1_t header;
    mpqHashEntry_t *hashtable;
    mpqBlockEntry_t *blocktable;
    uint32_t sector_size;
    uint8_t *sector_buffer;
    bool write_mode;
    uint32_t write_hash_table_size;
    struct mpq_write_entry *write_entries;
    uint32_t write_count;
    uint32_t write_capacity;
} mpqArchive_t;

typedef struct {
    mpqArchive_t *archive;
    handle_t owner_archive;
    uint8_t *owner_memory;
    uint32_t block_index;
    uint32_t file_size;
    uint32_t current_pos;
    uint32_t compressed_size;
    uint32_t flags;
    uint32_t file_key;
    uint32_t sector_count;
    uint32_t *sector_offsets;
} mpqFile_t;

typedef struct {
    mpqArchive_t *archive;
    uint32_t current_index;
    uint32_t file_count;
    char **files;
    char mask[MAX_PATH];
} mpqFind_t;

typedef struct mpq_cache_entry {
    char *name;
    uint32_t block_index;
    struct mpq_cache_entry *next;
} mpqCacheEntry_t;

typedef struct mpq_write_entry {
    char *name;
    uint32_t offset;
    uint32_t block_size;
    uint32_t file_size;
    uint32_t flags;
} mpqWriteEntry_t;

typedef struct {
    char *name;
    uint32_t hash1;
    uint32_t hash2;
} mpqListfileEntry_t;

typedef struct mpq_listfile_bucket_entry {
    mpqListfileEntry_t *entry;
    struct mpq_listfile_bucket_entry *next;
} mpqListfileBucketEntry_t;

#define STORM_BUFFER_SIZE 0x500

static uint32_t storm_buffer[STORM_BUFFER_SIZE];
static bool storm_buffer_ready = false;

static void InitStormBuffer(void)
{
    uint32_t seed = 0x00100001;
    uint32_t index1, index2, i;

    if (storm_buffer_ready) {
        return;
    }

    for (index1 = 0; index1 < 0x100; index1++) {
        for (index2 = index1, i = 0; i < 5; i++, index2 += 0x100) {
            uint32_t temp1, temp2;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp1 = (seed & 0xFFFF) << 0x10;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp2 = (seed & 0xFFFF);

            storm_buffer[index2] = (temp1 | temp2);
        }
    }

    storm_buffer_ready = true;
}

static uint8_t AsciiToUpper(uint8_t ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return ch - 0x20;
    }
    if (ch == '/') {
        return '\\';
    }
    return ch;
}

static uint32_t HashString(char const *str, uint32_t hash_type)
{
    uint32_t seed1 = 0x7FED7FED;
    uint32_t seed2 = 0xEEEEEEEE;
    uint8_t ch;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    while (*str) {
        ch = AsciiToUpper((uint8_t)*str++);
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

static uint32_t CacheKeyHash(char const *str)
{
    uint32_t hash = 2166136261u;

    while (*str) {
        uint8_t ch = (uint8_t)*str++;
        if (ch == '/') {
            ch = '\\';
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = (uint8_t)(ch + 0x20);
        }
        hash ^= ch;
        hash *= 16777619u;
    }

    return hash;
}

static void CanonicalizeMpqKey(char const *src, char *dst, size_t dst_size)
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

static bool LookupCachedBlock(mpqArchive_t *mpq, char const *fileName, uint32_t *block_index)
{
    char key[1024];
    uint32_t hash;
    mpqCacheEntry_t *entry;

    if (!mpq->lookup_cache || mpq->lookup_cache_size == 0) {
        return false;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    hash = CacheKeyHash(key);
    entry = mpq->lookup_cache[hash & (mpq->lookup_cache_size - 1)];
    while (entry) {
        if (!strcmp(entry->name, key)) {
            *block_index = entry->block_index;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

static void CacheBlockLookup(mpqArchive_t *mpq, char const *fileName, uint32_t block_index)
{
    char key[1024];
    uint32_t hash;
    mpqCacheEntry_t *entry;

    if (!mpq->lookup_cache || mpq->lookup_cache_size == 0) {
        return;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    hash = CacheKeyHash(key);
    entry = (mpqCacheEntry_t *)malloc(sizeof(mpqCacheEntry_t));
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

static bool PathCharEquals(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a + 0x20);
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b + 0x20);
    }
    return a == b;
}

static bool HasArchiveExtensionAt(cstring_t path, size_t dot)
{
    static cstring_t const exts[] = { ".mpq", ".w3m", ".w3x", NULL };

    for (uint32_t i = 0; exts[i]; i++) {
        bool match = true;
        for (uint32_t j = 0; exts[i][j]; j++) {
            if (!path[dot + j] || !PathCharEquals(path[dot + j], exts[i][j])) {
                match = false;
                break;
            }
        }
        if (match) {
            char next = path[dot + strlen(exts[i])];
            return next == '/' || next == '\\';
        }
    }
    return false;
}

static bool FindBlockIndex(mpqArchive_t *mpq, char const *fileName, uint32_t hash1, uint32_t hash2, uint32_t *block_index)
{
    uint32_t hash_pos;
    uint32_t index;
    bool trace = getenv("BZ_MPQ_TRACE") != NULL;

    hash_pos = (hash1 & (mpq->header.dwHashTableSize - 1));
    if (trace) {
        fprintf(stderr, "MPQ lookup: %s hash1=%08x hash2=%08x table=%u start=%u\n",
                fileName, hash1, hash2, mpq->header.dwHashTableSize, hash_pos);
    }
    for (index = 0; index < mpq->header.dwHashTableSize; index++) {
        mpqHashEntry_t *entry = &mpq->hashtable[(hash_pos + index) & (mpq->header.dwHashTableSize - 1)];

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
            return true;
        }
    }

    if (trace) {
        fprintf(stderr, "MPQ lookup: falling back to full scan\n");
    }
    for (index = 0; index < mpq->header.dwHashTableSize; index++) {
        mpqHashEntry_t *entry = &mpq->hashtable[index];

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
            return true;
        }
    }

    if (trace) {
        fprintf(stderr, "MPQ lookup: not found after full scan\n");
    }

    return false;
}

static char const *BaseNamePtr(char const *path)
{
    char const *slash1;
    char const *slash2;

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

static bool DecryptBlock(uint8_t *data, uint32_t size, uint32_t seed1)
{
    uint32_t *pdw = (uint32_t *)data;
    uint32_t nblocks = size >> 2; // uint32_t-aligned only; leftover 1-3 bytes stay ciphertext/plaintext as stored
    uint32_t seed2 = 0xEEEEEEEE;
    uint32_t index;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    for (index = 0; index < nblocks; index++) {
        uint32_t value;
        uint32_t seed2_base;

        seed2_base = seed2 + storm_buffer[MPQ_HASH_KEY2_MIX + (seed1 & 0xFF)];
        value = pdw[index] ^ (seed1 + seed2_base);
        pdw[index] = value;
        seed1 = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
        seed2 = value + seed2_base + (seed2_base << 5) + 3;
    }

    return true;
}

static bool EncryptBlock(uint8_t *data, uint32_t size, uint32_t seed1)
{
    uint32_t *pdw = (uint32_t *)data;
    uint32_t nblocks = size >> 2; // uint32_t-aligned only; leftover 1-3 bytes stay plaintext
    uint32_t seed2 = 0xEEEEEEEE;
    uint32_t index;

    if (!storm_buffer_ready) {
        InitStormBuffer();
    }

    for (index = 0; index < nblocks; index++) {
        uint32_t value = pdw[index];
        uint32_t seed2_base = seed2 + storm_buffer[MPQ_HASH_KEY2_MIX + (seed1 & 0xFF)];

        pdw[index] = value ^ (seed1 + seed2_base);
        seed1 = ((~seed1 << 0x15) + 0x11111111) | (seed1 >> 0x0B);
        seed2 = value + seed2_base + (seed2_base << 5) + 3;
    }

    return true;
}

#ifdef MPQ_TEST_API
uint32_t Mpq_TestHashString(char const *str, uint32_t hash_type) { return HashString(str, hash_type); }
bool Mpq_TestEncryptBlock(uint8_t *data, uint32_t size, uint32_t seed) { return EncryptBlock(data, size, seed); }
#endif

static uint32_t NextPowerOfTwo(uint32_t value)
{
    uint32_t result = 1;

    while (result < value) {
        result <<= 1;
    }

    return result;
}

static void FreeWriterEntries(mpqArchive_t *mpq)
{
    uint32_t i;

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

static bool WriterHasEntry(mpqArchive_t *mpq, char const *fileName)
{
    uint32_t i;
    char key[1024];

    if (!mpq || !fileName) {
        return false;
    }

    CanonicalizeMpqKey(fileName, key, sizeof(key));
    for (i = 0; i < mpq->write_count; i++) {
        char existing[1024];
        CanonicalizeMpqKey(mpq->write_entries[i].name, existing, sizeof(existing));
        if (!strcmp(existing, key)) {
            return true;
        }
    }

    return false;
}

static bool WriterCompressData(uint8_t const *data, uint32_t size, uint8_t **out_data, uint32_t *out_size, uint32_t *out_flags)
{
    uint8_t *compressed;
    uLongf compressed_bound;

    if (!out_data || !out_size || !out_flags) {
        return false;
    }

    *out_data = NULL;
    *out_size = size;
    *out_flags = MPQ_FILE_EXISTS;

    if (!data || size == 0) {
        return true;
    }

    compressed_bound = compressBound(size);
    if (compressed_bound + 1 > 0xFFFFFFFFu) {
        return true;
    }

    compressed = (uint8_t *)malloc((size_t)compressed_bound + 1);
    if (!compressed) {
        return false;
    }
    compressed[0] = MPQ_COMPRESSION_ZLIB;

    if (compress2(compressed + 1, &compressed_bound, data, size, Z_DEFAULT_COMPRESSION) != Z_OK ||
        compressed_bound + 1 >= size) {
        free(compressed);
        return true;
    }

    *out_data = compressed;
    *out_size = (uint32_t)(compressed_bound + 1);
    *out_flags = MPQ_FILE_EXISTS | MPQ_FILE_COMPRESS | MPQ_FILE_SINGLE_UNIT;
    return true;
}

static bool WriterAddData(mpqArchive_t *mpq, char const *archivedName, uint8_t const *data, uint32_t size)
{
    mpqWriteEntry_t *entry;
    uint8_t *compressed = NULL;
    uint8_t const *to_write = data;
    uint32_t write_size = size;
    uint32_t flags = MPQ_FILE_EXISTS;
    long pos;
    char path[1024];

    if (!mpq || !mpq->write_mode || !archivedName || !*archivedName) {
        return false;
    }

    strncpy(path, archivedName, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    NormalizeMpqPath(path);
    TrimEdgeSlashes(path);
    if (*path == '\0' || WriterHasEntry(mpq, path)) {
        return false;
    }

    if (!WriterCompressData(data, size, &compressed, &write_size, &flags)) {
        return false;
    }
    if (compressed) {
        to_write = compressed;
    }

    if (fseek(mpq->fp, 0, SEEK_END) != 0) {
        free(compressed);
        return false;
    }
    pos = ftell(mpq->fp);
    if (pos < 0 || (unsigned long)pos > 0xFFFFFFFFu) {
        free(compressed);
        return false;
    }
    if (write_size > 0 && (!to_write || fwrite(to_write, 1, write_size, mpq->fp) != write_size)) {
        free(compressed);
        return false;
    }
    free(compressed);

    if (mpq->write_count == mpq->write_capacity) {
        uint32_t next = mpq->write_capacity ? mpq->write_capacity * 2 : 16;
        mpqWriteEntry_t *tmp = (mpqWriteEntry_t *)realloc(mpq->write_entries, next * sizeof(*tmp));
        if (!tmp) {
            return false;
        }
        mpq->write_entries = tmp;
        mpq->write_capacity = next;
    }

    entry = &mpq->write_entries[mpq->write_count++];
    memset(entry, 0, sizeof(*entry));
    entry->name = (char *)malloc(strlen(path) + 1);
    if (!entry->name) {
        mpq->write_count--;
        return false;
    }
    memcpy(entry->name, path, strlen(path) + 1);
    entry->offset = (uint32_t)pos;
    entry->block_size = write_size;
    entry->file_size = size;
    entry->flags = flags;
    return true;
}

static bool FinalizeCreatedArchive(mpqArchive_t *mpq)
{
    mpqHeaderV1_t header;
    mpqHashEntry_t *hash_table = NULL;
    mpqBlockEntry_t *block_table = NULL;
    uint32_t total_entries;
    uint32_t hash_size;
    uint32_t block_size = 0;
    uint32_t offset;
    long table_pos;
    uint32_t i;
    bool ok = false;

    if (!mpq || !mpq->write_mode || !mpq->fp) {
        return false;
    }

    for (i = 0; i < mpq->write_count; i++) {
        block_size += (uint32_t)strlen(mpq->write_entries[i].name) + 1;
    }
    block_size += (uint32_t)strlen("(listfile)") + 1;

    {
        uint8_t *listfile_data = (uint8_t *)malloc(block_size ? block_size : 1);
        if (!listfile_data) {
            goto done;
        }

        offset = 0;
        for (i = 0; i < mpq->write_count; i++) {
            size_t len = strlen(mpq->write_entries[i].name);
            memcpy(listfile_data + offset, mpq->write_entries[i].name, len);
            offset += (uint32_t)len;
            listfile_data[offset++] = '\n';
        }
        memcpy(listfile_data + offset, "(listfile)", strlen("(listfile)"));
        offset += (uint32_t)strlen("(listfile)");
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

    hash_table = (mpqHashEntry_t *)malloc(hash_size * sizeof(*hash_table));
    block_table = (mpqBlockEntry_t *)malloc(total_entries * sizeof(*block_table));
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

    offset = (uint32_t)table_pos;
    for (i = 0; i < total_entries; i++) {
        mpqWriteEntry_t *entry = &mpq->write_entries[i];
        uint32_t slot = HashString(entry->name, MPQ_HASH_NAME_A) & (hash_size - 1);

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

    EncryptBlock((uint8_t *)hash_table, hash_size * sizeof(*hash_table), MPQ_KEY_HASH_TABLE);
    EncryptBlock((uint8_t *)block_table, total_entries * sizeof(*block_table), MPQ_KEY_BLOCK_TABLE);

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

    ok = true;

done:
    free(hash_table);
    free(block_table);
    return ok;
}

static bool SectorTableLooksValid(uint32_t const *offsets, uint32_t sector_count, uint32_t compressed_size)
{
    uint32_t i;

    if (!offsets || sector_count == 0) {
        return false;
    }

    if (offsets[0] == 0 || offsets[0] > compressed_size) {
        return false;
    }

    for (i = 0; i < sector_count; i++) {
        if (offsets[i] > offsets[i + 1]) {
            return false;
        }
        if (offsets[i + 1] > compressed_size) {
            return false;
        }
    }

    return true;
}

static void PreloadListfileCache(mpqArchive_t *mpq);

static bool TryInflateSector(uint8_t const *compressed, uint32_t compressed_size, uint32_t uncompressed_size, uint8_t *out, uint32_t *out_size)
{
    z_stream zs;
    int zlib_ret;
    uint8_t const *payload = compressed;
    uint32_t payload_size = compressed_size;

    if (!compressed || !out || !out_size || compressed_size == 0) {
        return false;
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
            return false;
        }
    }

    zlib_ret = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zlib_ret != Z_STREAM_END) {
        return false;
    }

    *out_size = zs.total_out;
    return true;
}

static bool MpqSeek(mpqArchive_t *mpq, uint32_t offset)
{
    uint32_t pos;

    if (!mpq) {
        return false;
    }
    pos = mpq->base_offset + offset;
    if (mpq->memory) {
        if (pos > mpq->memory_size) {
            return false;
        }
        mpq->memory_pos = pos;
        return true;
    }
    return mpq->fp && fseek(mpq->fp, pos, SEEK_SET) == 0;
}

static size_t MpqRead(mpqArchive_t *mpq, void *buffer, size_t size)
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
        mpq->memory_pos += (uint32_t)size;
        return size;
    }
    return mpq->fp ? fread(buffer, 1, size, mpq->fp) : 0;
}

static void MpqFreeReadArchive(mpqArchive_t *mpq)
{
    if (!mpq) {
        return;
    }
    if (mpq->lookup_cache) {
        uint32_t i;
        for (i = 0; i < mpq->lookup_cache_size; i++) {
            mpqCacheEntry_t *entry = mpq->lookup_cache[i];
            while (entry) {
                mpqCacheEntry_t *next = entry->next;
                free(entry->name);
                free(entry);
                entry = next;
            }
        }
        free(mpq->lookup_cache);
    }
    SAFE_DELETE(mpq->hashtable, free);
    SAFE_DELETE(mpq->blocktable, free);
    SAFE_DELETE(mpq->sector_buffer, free);
    SAFE_DELETE(mpq->fp, fclose);
    free(mpq);
}

static bool SFileOpenArchiveSource(mpqArchive_t *mpq, handle_t *archive)
{
    unsigned char probe[1024];
    size_t read;
    long archive_offset = -1;
    size_t i;

    if (!mpq || !archive) {
        return false;
    }

    mpq->lookup_cache_size = 4096;
    mpq->lookup_cache = (mpqCacheEntry_t **)calloc(mpq->lookup_cache_size, sizeof(mpqCacheEntry_t *));
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
    mpq->base_offset = (uint32_t)archive_offset;

    if (!MpqSeek(mpq, 0)) {
        goto fail;
    }

    // Read MPQ header
    if (MpqRead(mpq, &mpq->header, sizeof(mpqHeaderV1_t)) != sizeof(mpqHeaderV1_t)) {
        goto fail;
    }

    // Verify MPQ signature
    if (mpq->header.dwID != 0x1A51504D) {  // "MPQ\x1a"
        goto fail;
    }

    /* Sector size is 512 << wSectorSizeShift. Storm accepts up to shift 15 (16 MiB).
     * Protected maps such as DotA v6.83d use that max; never clamp it down to 4096. */
    if (mpq->header.wSectorSizeShift > MPQ_SECTOR_SIZE_SHIFT_MAX) {
        fprintf(stderr, "MPQ: unsupported wSectorSizeShift %u (max %u)\n",
                mpq->header.wSectorSizeShift, MPQ_SECTOR_SIZE_SHIFT_MAX);
        goto fail;
    }
    mpq->sector_size = 1u << (mpq->header.wSectorSizeShift + 9);

    // Allocate sector buffer (one scratch sector; 16 MiB once at open is acceptable)
    mpq->sector_buffer = (uint8_t *)malloc(mpq->sector_size);
    if (!mpq->sector_buffer) {
        fprintf(stderr, "MPQ: failed to allocate %u-byte sector buffer\n", mpq->sector_size);
        goto fail;
    }

    // Read hash table
    mpq->hashtable = (mpqHashEntry_t *)malloc(mpq->header.dwHashTableSize * sizeof(mpqHashEntry_t));
    if (!mpq->hashtable) {
        goto fail;
    }

    if (!MpqSeek(mpq, mpq->header.dwHashTablePos)) {
        goto fail;
    }
    read = MpqRead(mpq, mpq->hashtable, mpq->header.dwHashTableSize * sizeof(mpqHashEntry_t));
    if (read != mpq->header.dwHashTableSize * sizeof(mpqHashEntry_t)) {
        goto fail;
    }

    // Decrypt hash table
    DecryptBlock((uint8_t *)mpq->hashtable, mpq->header.dwHashTableSize * sizeof(mpqHashEntry_t),
                 MPQ_KEY_HASH_TABLE);

    // Read block table
    mpq->blocktable = (mpqBlockEntry_t *)malloc(mpq->header.dwBlockTableSize * sizeof(mpqBlockEntry_t));
    if (!mpq->blocktable) {
        goto fail;
    }

    if (!MpqSeek(mpq, mpq->header.dwBlockTablePos)) {
        goto fail;
    }
    read = MpqRead(mpq, mpq->blocktable, mpq->header.dwBlockTableSize * sizeof(mpqBlockEntry_t));
    if (read != mpq->header.dwBlockTableSize * sizeof(mpqBlockEntry_t)) {
        goto fail;
    }

    // Decrypt block table
    DecryptBlock((uint8_t *)mpq->blocktable, mpq->header.dwBlockTableSize * sizeof(mpqBlockEntry_t),
                 MPQ_KEY_BLOCK_TABLE);

    PreloadListfileCache(mpq);

    *archive = (handle_t)mpq;
    return true;

fail:
    MpqFreeReadArchive(mpq);
    return false;
}

bool SFileOpenArchive(cstring_t filename, uint32_t priority, uint32_t flags, handle_t *archive)
{
    mpqArchive_t *mpq;
    FILE *fp;

    (void)priority;
    (void)flags;

    if (!filename || !archive) {
        return false;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }

    mpq = (mpqArchive_t *)calloc(1, sizeof(mpqArchive_t));
    if (!mpq) {
        fclose(fp);
        return false;
    }

    mpq->fp = fp;
    strncpy(mpq->filename, filename, sizeof(mpq->filename) - 1);
    return SFileOpenArchiveSource(mpq, archive);
}

bool SFileOpenArchiveFromMemory(void const *data, uint32_t size, uint32_t flags, handle_t *archive)
{
    mpqArchive_t *mpq;

    (void)flags;

    if (!data || size == 0 || !archive) {
        return false;
    }

    mpq = (mpqArchive_t *)calloc(1, sizeof(mpqArchive_t));
    if (!mpq) {
        return false;
    }

    mpq->memory = (uint8_t const *)data;
    mpq->memory_size = size;
    strncpy(mpq->filename, "<memory>", sizeof(mpq->filename) - 1);
    return SFileOpenArchiveSource(mpq, archive);
}

bool SFileCreateArchive(cstring_t filename, uint32_t flags, uint32_t maxFiles, handle_t *archive)
{
    mpqArchive_t *mpq;
    mpqHeaderV1_t header;
    FILE *fp;

    (void)flags;

    if (!filename || !archive) {
        return false;
    }

    fp = fopen(filename, "wb+");
    if (!fp) {
        return false;
    }

    mpq = (mpqArchive_t *)calloc(1, sizeof(*mpq));
    if (!mpq) {
        fclose(fp);
        return false;
    }

    memset(&header, 0, sizeof(header));
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        free(mpq);
        fclose(fp);
        return false;
    }

    mpq->fp = fp;
    mpq->write_mode = true;
    mpq->write_hash_table_size = NextPowerOfTwo(MAX(maxFiles * 2, 16));
    strncpy(mpq->filename, filename, sizeof(mpq->filename) - 1);
    *archive = (handle_t)mpq;
    return true;
}

bool SFileAddFile(handle_t archive, cstring_t sourceFile, cstring_t archivedName)
{
    mpqArchive_t *mpq = (mpqArchive_t *)archive;
    FILE *fp;
    uint8_t *buffer;
    long size_long;
    size_t size;
    char const *name = archivedName ? archivedName : BaseNamePtr(sourceFile);
    bool ok;

    if (!mpq || !mpq->write_mode || !sourceFile || !name) {
        return false;
    }

    fp = fopen(sourceFile, "rb");
    if (!fp) {
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    size_long = ftell(fp);
    if (size_long < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    size = (size_t)size_long;
    buffer = size ? (uint8_t *)malloc(size) : NULL;
    if (size > 0 && !buffer) {
        fclose(fp);
        return false;
    }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer);
        fclose(fp);
        return false;
    }
    fclose(fp);

    ok = WriterAddData(mpq, name, buffer, (uint32_t)size);
    free(buffer);
    return ok;
}

bool SFileAddFileFromBuffer(handle_t archive, cstring_t archivedName, void const *data, uint32_t size)
{
    mpqArchive_t *mpq = (mpqArchive_t *)archive;

    if (!mpq || !mpq->write_mode || !archivedName || (size > 0 && !data)) {
        return false;
    }

    return WriterAddData(mpq, archivedName, (uint8_t const *)data, size);
}

bool SFileCloseArchive(handle_t archive)
{
    mpqArchive_t *mpq = (mpqArchive_t *)archive;

    if (!mpq) {
        return false;
    }

    if (mpq->write_mode) {
        bool ok = FinalizeCreatedArchive(mpq);
        FreeWriterEntries(mpq);
        SAFE_DELETE(mpq->fp, fclose);
        free(mpq);
        return ok;
    }

    MpqFreeReadArchive(mpq);

    return true;
}

static bool SFileOpenFileDirect(handle_t archive, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    mpqArchive_t *mpq = (mpqArchive_t *)archive;
    mpqFile_t *mpqfile;
    char canonical_name[1024];
    uint32_t hash1, hash2;
    uint32_t block_index;

    if (!mpq || !fileName || !file || searchScope != SFILE_OPEN_FROM_MPQ) {
        return false;
    }

    /* MPQ names are case-insensitive and use backslashes; normalize every lookup before hashing. */
    CanonicalizeMpqKey(fileName, canonical_name, sizeof(canonical_name));
    if (!canonical_name[0]) {
        return false;
    }

    // Calculate file name hashes
    hash1 = HashString(canonical_name, MPQ_HASH_NAME_A);
    hash2 = HashString(canonical_name, MPQ_HASH_NAME_B);

    if (!LookupCachedBlock(mpq, canonical_name, &block_index)) {
        if (!FindBlockIndex(mpq, canonical_name, hash1, hash2, &block_index)) {
            return false;
        }
    }

    if (block_index >= mpq->header.dwBlockTableSize) {
        return false;
    }

    mpqfile = (mpqFile_t *)malloc(sizeof(mpqFile_t));
    if (!mpqfile) {
        return false;
    }

    memset(mpqfile, 0, sizeof(mpqFile_t));
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
        mpqfile->file_key = HashString(BaseNamePtr(canonical_name), MPQ_HASH_FILE_KEY);
        if (mpqfile->flags & MPQ_FILE_KEY_V2) {
            mpqfile->file_key = (mpqfile->file_key + mpq->blocktable[block_index].dwBlockOffset) ^ mpqfile->file_size;
        }
    }

    if ((mpqfile->flags & MPQ_FILE_COMPRESS) && !(mpqfile->flags & MPQ_FILE_SINGLE_UNIT)) {
        uint32_t sector_count;
        uint32_t offset_count;
        mpqBlockEntry_t *block;
        bool offsets_valid = false;

        block = &mpq->blocktable[block_index];
        sector_count = (mpqfile->file_size + mpq->sector_size - 1) / mpq->sector_size;
        offset_count = sector_count + 1;
        mpqfile->sector_offsets = (uint32_t *)malloc(offset_count * sizeof(uint32_t));
        if (!mpqfile->sector_offsets) {
            free(mpqfile);
            return false;
        }

        MpqSeek(mpq, block->dwBlockOffset);
        if (MpqRead(mpq, mpqfile->sector_offsets, offset_count * sizeof(uint32_t)) != offset_count * sizeof(uint32_t)) {
            free(mpqfile->sector_offsets);
            free(mpqfile);
            return false;
        }

        offsets_valid = SectorTableLooksValid(mpqfile->sector_offsets, sector_count, mpqfile->compressed_size);
        if (!offsets_valid && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock((uint8_t *)mpqfile->sector_offsets, offset_count * sizeof(uint32_t), mpqfile->file_key - 1);
            offsets_valid = SectorTableLooksValid(mpqfile->sector_offsets, sector_count, mpqfile->compressed_size);
        }
        if (!offsets_valid) {
            free(mpqfile->sector_offsets);
            free(mpqfile);
            return false;
        }

        mpqfile->sector_count = sector_count;
    }

    *file = (handle_t)mpqfile;
    return true;
}

static bool SFileOpenNestedFile(handle_t archive, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    char outerName[1024];
    cstring_t innerName;

    if (!fileName) {
        return false;
    }

    for (size_t i = 0; fileName[i]; i++) {
        handle_t outerFile;
        uint32_t outerSize;
        uint32_t bytesRead = 0;
        uint8_t *outerData;
        handle_t nestedArchive;

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
        outerData = (uint8_t *)malloc(outerSize);
        if (!outerData) {
            SFileCloseFile(outerFile);
            return false;
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
            mpqFile_t *mpqfile = (mpqFile_t *)*file;
            mpqfile->owner_archive = nestedArchive;
            mpqfile->owner_memory = outerData;
            return true;
        }

        SFileCloseArchive(nestedArchive);
        free(outerData);
    }

    return false;
}

bool SFileOpenFileEx(handle_t archive, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    if (SFileOpenFileDirect(archive, fileName, searchScope, file)) {
        return true;
    }
    return SFileOpenNestedFile(archive, fileName, searchScope, file);
}

bool SFileOpenFileFromArchiveMemory(uint8_t *data, uint32_t size, cstring_t fileName, uint32_t searchScope, handle_t *file)
{
    handle_t nestedArchive;

    if (!data || size == 0 || !fileName || !*fileName || !file ||
        searchScope != SFILE_OPEN_FROM_MPQ) {
        return false;
    }
    if (!SFileOpenArchiveFromMemory(data, size, 0, &nestedArchive)) {
        return false;
    }
    if (SFileOpenFileDirect(nestedArchive, fileName, searchScope, file)) {
        mpqFile_t *mpqfile = (mpqFile_t *)*file;

        mpqfile->owner_archive = nestedArchive;
        mpqfile->owner_memory = data;
        return true;
    }
    SFileCloseArchive(nestedArchive);
    return false;
}

bool SFileCloseFile(handle_t file)
{
    if (file) {
        mpqFile_t *mpqfile = (mpqFile_t *)file;
        handle_t owner_archive = mpqfile->owner_archive;
        uint8_t *owner_memory = mpqfile->owner_memory;

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
    return true;
}

bool SFileReadFile(handle_t file, void *buffer, uint32_t toRead, uint32_t *bytesRead, void *overlapped)
{
    mpqFile_t *mpqfile = (mpqFile_t *)file;
    mpqArchive_t *mpq;
    mpqBlockEntry_t *block;
    uint8_t *dest = (uint8_t *)buffer;
    uint32_t read_so_far = 0;
    uint32_t to_read_in_block;
    uint32_t sector_start;
    uint32_t sector_offset;
    uint32_t bytes_in_sector;
    uint32_t file_offset;

    (void)overlapped;

    if (!mpqfile || !buffer || toRead == 0) {
        if (bytesRead) *bytesRead = 0;
        return false;
    }

    mpq = mpqfile->archive;
    block = &mpq->blocktable[mpqfile->block_index];

    if (mpqfile->current_pos >= mpqfile->file_size) {
        if (bytesRead) *bytesRead = 0;
        return true;
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
        uint32_t actually_read = (uint32_t)MpqRead(mpq, dest, toRead);
        if (actually_read > 0 && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock(dest, actually_read, mpqfile->file_key);
        }
        mpqfile->current_pos += actually_read;
        if (bytesRead) *bytesRead = actually_read;
        return actually_read > 0;
    }

    if (block->dwFlags & MPQ_FILE_SINGLE_UNIT) {
        uint8_t *compressed;
        uint8_t *whole_file;
        uint32_t bytes_in_file = 0;
        bool ok;

        compressed = (uint8_t *)malloc(block->dwBlockSize ? block->dwBlockSize : 1);
        whole_file = (uint8_t *)malloc(mpqfile->file_size ? mpqfile->file_size : 1);
        if (!compressed || !whole_file) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return false;
        }

        MpqSeek(mpq, block->dwBlockOffset);
        if (MpqRead(mpq, compressed, block->dwBlockSize) != block->dwBlockSize) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return false;
        }

        ok = TryInflateSector(compressed, block->dwBlockSize, mpqfile->file_size, whole_file, &bytes_in_file);
        if (!ok && (mpqfile->flags & MPQ_FILE_ENCRYPTED)) {
            DecryptBlock(compressed, block->dwBlockSize, mpqfile->file_key);
            ok = TryInflateSector(compressed, block->dwBlockSize, mpqfile->file_size, whole_file, &bytes_in_file);
        }
        if (!ok && block->dwBlockSize >= mpqfile->file_size) {
            memcpy(whole_file, compressed, mpqfile->file_size);
            bytes_in_file = mpqfile->file_size;
            ok = true;
        }

        if (!ok || file_offset >= bytes_in_file) {
            free(compressed);
            free(whole_file);
            if (bytesRead) *bytesRead = 0;
            return false;
        }

        if (toRead > bytes_in_file - file_offset) {
            toRead = bytes_in_file - file_offset;
        }
        memcpy(dest, whole_file + file_offset, toRead);
        mpqfile->current_pos += toRead;
        if (bytesRead) *bytesRead = toRead;

        free(compressed);
        free(whole_file);
        return true;
    }

    // File is compressed - read through the sector offset table.
    if (!mpqfile->sector_offsets || mpqfile->sector_count == 0) {
        if (bytesRead) *bytesRead = 0;
        return false;
    }

    sector_start = (file_offset / mpq->sector_size);
    sector_offset = (file_offset % mpq->sector_size);

    while (read_so_far < toRead && sector_start < mpqfile->sector_count) {
        uint32_t sector_start_offset;
        uint32_t sector_end_offset;
        uint32_t sector_compressed_size;
        uint32_t sector_uncompressed_size;
        uint8_t *compressed;

        sector_start_offset = mpqfile->sector_offsets[sector_start];
        sector_end_offset = mpqfile->sector_offsets[sector_start + 1];

        if (sector_end_offset < sector_start_offset) {
            break;
        }

        sector_compressed_size = sector_end_offset - sector_start_offset;
        sector_uncompressed_size = mpq->sector_size;
        if (sector_start == mpqfile->sector_count - 1) {
            uint32_t tail = mpqfile->file_size % mpq->sector_size;
            if (tail != 0) {
                sector_uncompressed_size = tail;
            }
        }

        compressed = (uint8_t *)malloc(sector_compressed_size);
        if (!compressed) {
            break;
        }

        MpqSeek(mpq, block->dwBlockOffset + sector_start_offset);
        if (MpqRead(mpq, compressed, sector_compressed_size) != sector_compressed_size) {
            free(compressed);
            break;
        }

        if (mpqfile->flags & MPQ_FILE_ENCRYPTED)
            DecryptBlock(compressed, sector_compressed_size, mpqfile->file_key + sector_start);
        if (!TryInflateSector(compressed, sector_compressed_size, sector_uncompressed_size, mpq->sector_buffer, &bytes_in_sector)) {
            mpqDecompress_t dec = { compressed, sector_compressed_size, mpq->sector_buffer,
                                    sector_uncompressed_size, 0 };
            if (Mpq_DecompressSector(&dec)) {
                bytes_in_sector = dec.out_size;
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

uint32_t SFileGetFileSize(handle_t file, uint32_t *highSize)
{
    mpqFile_t *mpqfile = (mpqFile_t *)file;

    if (!mpqfile) {
        return 0;
    }

    if (highSize) {
        *highSize = 0;
    }

    return mpqfile->file_size;
}

uint32_t SFileSetFilePointer(handle_t file, int32_t distance, int32_t *distanceHigh, uint32_t moveMethod)
{
    mpqFile_t *mpqfile = (mpqFile_t *)file;
    uint32_t new_pos;

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

bool SFileExtractFile(handle_t archive, cstring_t toExtract, cstring_t extracted, uint32_t flags)
{
    handle_t file;
    FILE *out;
    uint32_t file_size;
    uint32_t bytes_read;
    uint8_t buffer[65536];
    bool success = false;

    (void)flags;

    if (!SFileOpenFileEx(archive, toExtract, SFILE_OPEN_FROM_MPQ, &file)) {
        return false;
    }

    file_size = SFileGetFileSize(file, NULL);
    out = fopen(extracted, "wb");
    if (!out) {
        SFileCloseFile(file);
        return false;
    }

    while (file_size > 0) {
        uint32_t to_read = (file_size > sizeof(buffer)) ? sizeof(buffer) : file_size;
        if (!SFileReadFile(file, buffer, to_read, &bytes_read, NULL) || bytes_read == 0) {
            break;
        }
        if (fwrite(buffer, 1, bytes_read, out) != bytes_read) {
            break;
        }
        file_size -= bytes_read;
    }

    if (file_size == 0) {
        success = true;
    }

    fclose(out);
    SFileCloseFile(file);
    return success;
}

static uint32_t PreloadBucketHash(uint32_t hash1, uint32_t hash2)
{
    return hash1 ^ (hash2 * 16777619u);
}

static void PreloadListfileCache(mpqArchive_t *mpq)
{
    handle_t list_file;
    uint32_t list_size;
    uint8_t *buffer = NULL;
    uint32_t bytes_read = 0;
    char *cursor;
    char *line;
    size_t entry_count = 0;
    size_t bucket_count = 0;
    mpqListfileEntry_t *entries = NULL;
    mpqListfileBucketEntry_t **buckets = NULL;
    size_t i;
    bool trace = getenv("BZ_MPQ_TRACE") != NULL;

    if (!mpq || !mpq->hashtable || !mpq->blocktable) {
        return;
    }

    if (!SFileOpenFileEx((handle_t)mpq, "(listfile)", SFILE_OPEN_FROM_MPQ, &list_file)) {
        return;
    }

    list_size = SFileGetFileSize(list_file, NULL);
    if (list_size == 0) {
        SFileCloseFile(list_file);
        return;
    }

    buffer = (uint8_t *)malloc(list_size + 1);
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

    entries = (mpqListfileEntry_t *)calloc(entry_count, sizeof(mpqListfileEntry_t));
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

    buckets = (mpqListfileBucketEntry_t **)calloc(bucket_count, sizeof(mpqListfileBucketEntry_t *));
    if (!buckets) {
        for (i = 0; i < entry_count; i++) {
            free(entries[i].name);
        }
        free(entries);
        free(buffer);
        return;
    }

    for (i = 0; i < entry_count; i++) {
        mpqListfileBucketEntry_t *node = (mpqListfileBucketEntry_t *)malloc(sizeof(mpqListfileBucketEntry_t));
        uint32_t slot;

        if (!node) {
            continue;
        }
        slot = PreloadBucketHash(entries[i].hash1, entries[i].hash2) & (bucket_count - 1);
        node->entry = &entries[i];
        node->next = buckets[slot];
        buckets[slot] = node;
    }

    for (i = 0; i < mpq->header.dwHashTableSize; i++) {
        mpqHashEntry_t *hash_entry = &mpq->hashtable[i];
        uint32_t slot;
        mpqListfileBucketEntry_t *node;

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
        mpqListfileBucketEntry_t *node = buckets[i];
        while (node) {
            mpqListfileBucketEntry_t *next = node->next;
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

static bool FilenameMatches(char const *filename, char const *mask);
static void FreeFindList(mpqFind_t *find);
static bool AppendFindListEntry(mpqFind_t *find, char const *name);

handle_t SFileFindFirstFile(handle_t archive, cstring_t mask, sfileFindData_t *findData, cstring_t listFile)
{
    mpqArchive_t *mpq = (mpqArchive_t *)archive;
    mpqFind_t *find;
    handle_t list_file;
    uint32_t list_size;
    uint8_t *list_buffer;
    uint32_t bytes_read;
    char *cursor;

    (void)listFile;

    if (!mpq || !mask || !findData) {
        return NULL;
    }

    find = (mpqFind_t *)malloc(sizeof(mpqFind_t));
    if (!find) {
        return NULL;
    }

    memset(find, 0, sizeof(mpqFind_t));
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
    list_buffer = (uint8_t *)malloc(list_size + 1);
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

    if (!SFileFindNextFile((handle_t)find, findData)) {
        FreeFindList(find);
        free(find);
        return NULL;
    }

    return (handle_t)find;
}

static bool FilenameMatches(char const *filename, char const *mask)
{
    // Simple wildcard matching - "*" matches everything
    if (!mask || !mask[0] || strcmp(mask, "*") == 0) {
        return true;
    }

    // For now, simple prefix matching
    size_t mask_len = strlen(mask);
    if (mask[mask_len - 1] == '*') {
        return strncmp(filename, mask, mask_len - 1) == 0;
    }

    return strcmp(filename, mask) == 0;
}

static void FreeFindList(mpqFind_t *find)
{
    uint32_t i;

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

static bool AppendFindListEntry(mpqFind_t *find, char const *name)
{
    char **next;
    char *copy;

    next = (char **)realloc(find->files, (find->file_count + 1) * sizeof(*next));
    if (!next) {
        return false;
    }

    copy = (char *)malloc(strlen(name) + 1);
    if (!copy) {
        return false;
    }

    memcpy(copy, name, strlen(name) + 1);
    find->files = next;
    find->files[find->file_count++] = copy;
    return true;
}

bool SFileFindNextFile(handle_t find, sfileFindData_t *findData)
{
    mpqFind_t *mpqfind = (mpqFind_t *)find;
    char const *name;
    uint32_t block_index;

    if (!mpqfind || !findData) {
        return false;
    }

    if (mpqfind->current_index >= mpqfind->file_count) {
        return false;
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
        mpqBlockEntry_t *block = &mpqfind->archive->blocktable[block_index];

        findData->dwBlockIndex = block_index;
        findData->dwFileSize = block->dwFileSize;
        findData->dwCompSize = block->dwBlockSize;
        findData->dwFileFlags = block->dwFlags;
    }
    return true;
}

bool SFileFindClose(handle_t find)
{
    if (find) {
        FreeFindList((mpqFind_t *)find);
        free(find);
    }
    return true;
}
