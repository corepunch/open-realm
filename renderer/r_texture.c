#include "r_local.h"
#include <ctype.h>

/* texid -> texture index for model texture resolution; the cache below owns the texture memory. */
static LPTEXTURE g_textures = NULL;

#define BZ_IMAGE_CACHE_BUCKETS 2048u // buckets; keeps thousands of resident world textures near O(1) lookup
#define BZ_IMAGE_HASH_INIT 2166136261u // FNV-1a seed; stable case-insensitive texture-path hashing
#define BZ_IMAGE_HASH_PRIME 16777619u // FNV-1a multiplier; distributes archive paths across cache buckets

typedef struct rImageCacheEntry_s {
    char *name;
    LPTEXTURE texture;
    BOOL owns_texture;
    struct rImageCacheEntry_s *next;
    struct rImageCacheEntry_s *hash_next;
} rImageCacheEntry_t;

static rImageCacheEntry_t *r_image_cache;
static rImageCacheEntry_t *r_image_hash[BZ_IMAGE_CACHE_BUCKETS];

/* Texture paths are case-insensitive inside MPQs, so the registry hash must use the same contract as lookup. */
static DWORD R_TextureNameHash(LPCSTR name) {
    DWORD hash = BZ_IMAGE_HASH_INIT;
    for (; name && *name; name++) hash = (hash ^ (BYTE)tolower((unsigned char)*name)) * BZ_IMAGE_HASH_PRIME;
    return hash;
}

LPTEXTURE R_FindLoadedTexture(LPCSTR name) {
    rImageCacheEntry_t *entry;
    DWORD hash;

    if (!name || !*name) return NULL;
    hash = R_TextureNameHash(name) % BZ_IMAGE_CACHE_BUCKETS;
    for (entry = r_image_hash[hash]; entry; entry = entry->hash_next)
        if (!strcasecmp(entry->name, name)) return entry->texture;
    return NULL;
}

void R_CacheLoadedTexture(LPCSTR name, LPTEXTURE texture) {
    rImageCacheEntry_t *entry, *known;
    DWORD hash;

    if (!name || !*name || !texture || R_FindLoadedTexture(name)) return;
    hash = R_TextureNameHash(name) % BZ_IMAGE_CACHE_BUCKETS;
    entry = ri.MemAlloc(sizeof(*entry));
    entry->name = ri.MemAlloc(strlen(name) + 1);
    strcpy(entry->name, name);
    entry->texture = texture;
    entry->owns_texture = texture != tr.texture[TEX_PLACEHOLDER];
    for (known = r_image_cache; known; known = known->next)
        if (known->texture == texture) { entry->owns_texture = false; break; }
    entry->next = r_image_cache;
    entry->hash_next = r_image_hash[hash];
    r_image_cache = entry;
    r_image_hash[hash] = entry;
}

void R_ShutdownTextureCache(void) {
    rImageCacheEntry_t *entry;

    while ((entry = r_image_cache) != NULL) {
        r_image_cache = entry->next;
        if (entry->owns_texture) {
            R_Call(glDeleteTextures, 1, &entry->texture->texid);
            ri.MemFree(entry->texture);
        }
        ri.MemFree(entry->name);
        ri.MemFree(entry);
    }
    memset(r_image_hash, 0, sizeof(r_image_hash));
    /* The cache owns every cached texture; g_textures only indexes them by texid, so it must not outlive the free. */
    g_textures = NULL;
}

int R_RegisterTextureFile(char const *textureFileName) {
    LPTEXTURE tex = (LPTEXTURE)R_LoadTexture(textureFileName);
    if (tex) {
        /* The cache can return an existing node; the old unbraced macro call always reassigned the head. */
        if (!R_FindTextureByID(tex->texid)) {
            ADD_TO_LIST(tex, g_textures);
        }
        return tex->texid;
    } else {
        return -1;
    }
}

struct texture const* R_FindTextureByID(DWORD textureID) {
    for (LPCTEXTURE tex = g_textures; tex; tex = tex->next) {
        if (tex->texid == textureID)
            return tex;
    }
    return NULL;
}

void R_BindTexture(LPCTEXTURE texture, DWORD unit) {
    R_Call(glActiveTexture, GL_TEXTURE0 + unit);
    R_Call(glBindTexture, GL_TEXTURE_2D, texture ? texture->texid : tr.texture[TEX_WHITE]->texid);
}

void R_SetTextureWrap(LPCTEXTURE texture, bool wrapS, bool wrapT) {
    if (!texture) {
        return;
    }
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

LPTEXTURE R_AllocateTexture(DWORD width, DWORD height) {
    LPTEXTURE texture = ri.MemAlloc(sizeof(TEXTURE));
    R_Call(glGenTextures, 1, &texture->texid);
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    texture->width = width;
    texture->height = height;
    return texture;
}

void R_ReleaseTexture(LPTEXTURE texture) {
    rImageCacheEntry_t *entry;

    if (!texture) {
        return;
    }
    for (entry = r_image_cache; entry; entry = entry->next)
        if (entry->texture == texture) return;
    FOR_LOOP(i, TEX_COUNT) {
        /* Missing assets share renderer-owned placeholders; cache eviction must not free a built-in
           used by other slots. */
        if (texture == tr.texture[i])
            return;
    }
    R_Call(glDeleteTextures, 1, &texture->texid);
    texture->texid = 0;
    ri.MemFree(texture);
}

void R_LoadTextureMipLevel(LPCTEXTURE pTexture, DWORD level, LPCCOLOR32 pPixels, DWORD width, DWORD height) {
    if (width == 0 || height == 0)
        return;
    R_Call(glBindTexture, GL_TEXTURE_2D, pTexture->texid);
#if __linux__
    R_Call(glTexImage2D, GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
#else
    R_Call(glTexImage2D, GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pPixels);
#endif
    if (level > 0) {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}
