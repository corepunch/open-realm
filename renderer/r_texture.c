#include "r_local.h"
#include <ctype.h>

/* Asset references may retain a .tga source name after conversion to BLP; actual TGA files take precedence. */
int R_ReadTextureFile(LPCSTR name, LPSTR path, void **buffer) {
    static LPCSTR const exact[] = { ".blp", ".dds", ".pcx" };
    LPCSTR ext = strrchr(name, '.');
    snprintf(path, sizeof(PATHSTR), "%s", name);
    int size = ri.FS_ReadFile(path, buffer);
    if (size >= 0 && *buffer) return size;
    FOR_LOOP(i, sizeof(exact) / sizeof(exact[0]))
        if (ext && !strcasecmp(ext, exact[i])) return size;
    /* Previously only extensionless names found BLPs, leaving authored .tga references unresolved. */
    int len = ext && !strcasecmp(ext, ".tga") ? (int)(ext - name) : (int)strlen(name);
    snprintf(path, sizeof(PATHSTR), "%.*s.blp", len, name);
    return ri.FS_ReadFile(path, buffer);
}

/* texid -> texture index for model texture resolution; the cache below owns the texture memory. */
static LPTEXTURE g_textures = NULL;
static GLenum r_bgra_internal;

/* Source bytes determine channel order; context capabilities only determine whether conversion is necessary. */
void R_InitTextureFormats(void) {
    if (strncmp((LPCSTR)glGetString(GL_VERSION), "OpenGL ES", 9))
        r_bgra_internal = GL_RGBA;
    else if (SDL_GL_ExtensionSupported("GL_EXT_texture_format_BGRA8888"))
        r_bgra_internal = BZ_GL_BGRA;
    else if (SDL_GL_ExtensionSupported("GL_APPLE_texture_format_BGRA8888"))
        r_bgra_internal = GL_RGBA;
    else
        r_bgra_internal = 0;
    fprintf(stderr, "OpenGL: texture uploads RGBA=direct BGRA=%s (internal=0x%x)\n", r_bgra_internal ? "direct" : "CPU conversion to RGBA", r_bgra_internal);
}

#define BZ_IMAGE_CACHE_BUCKETS 2048u // buckets; keeps thousands of resident world textures near O(1) lookup
#define BZ_IMAGE_HASH_INIT 2166136261u // FNV-1a seed; stable case-insensitive texture-path hashing
#define BZ_IMAGE_HASH_PRIME 16777619u // FNV-1a multiplier; distributes archive paths across cache buckets

typedef struct rImageCacheEntry_s {
    char *name;
    LPTEXTURE texture;
    BOOL owns_texture;
    BOOL streamed;              /* eligible for generation reclaim (streaming world texture) */
    BOOL pinned;               /* a non-streaming consumer depends on it; never reclaim */
    DWORD generation;          /* last streaming generation that referenced this texture */
    struct rImageCacheEntry_s *next;
    struct rImageCacheEntry_s *hash_next;
} rImageCacheEntry_t;

static rImageCacheEntry_t *r_image_cache;
static rImageCacheEntry_t *r_image_hash[BZ_IMAGE_CACHE_BUCKETS];

/* Quake 2 registration-sequence lifetime: streaming callers bump the generation
   when their working set changes, every touched texture is re-stamped, and
   textures left stale are reclaimed.  r_load_streamed gates whether a load marks
   its entry streamable; a texture also used by any non-streaming consumer is
   pinned so it is never reclaimed out from under that consumer. */
static BOOL r_load_streamed;
static DWORD r_stream_generation;

/* Aliases share one texture allocation, so lifetime state belongs to the cache entry that owns it. */
static rImageCacheEntry_t *R_TextureOwner(rImageCacheEntry_t *entry) {
    if (!entry || entry->owns_texture) return entry;
    for (rImageCacheEntry_t *owner = r_image_cache; owner; owner = owner->next)
        if (owner->owns_texture && owner->texture == entry->texture) return owner;
    return entry;
}

static void R_MarkEntryUse(rImageCacheEntry_t *entry) {
    entry = R_TextureOwner(entry);
    if (!entry || !entry->owns_texture) return;
    if (r_load_streamed) {
        if (!entry->pinned) { entry->streamed = true; entry->generation = r_stream_generation; }
    } else {
        /* A persistent consumer may outlive every streaming generation; the old code pinned only streamed-first entries. */
        entry->streamed = false; entry->pinned = true;
    }
}

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
        if (!strcasecmp(entry->name, name)) { R_MarkEntryUse(entry); return entry->texture; }
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
    entry->streamed = false;
    entry->pinned = false;
    entry->generation = 0;
    for (known = r_image_cache; known; known = known->next)
        if (known->texture == texture) { entry->owns_texture = false; break; }
    entry->next = r_image_cache;
    entry->hash_next = r_image_hash[hash];
    r_image_cache = entry;
    r_image_hash[hash] = entry;
    R_MarkEntryUse(entry);
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

/* Streaming world loads route through here so their cache entries are stamped
   with the current generation and become eligible for reclaim. */
LPTEXTURE R_LoadTextureStreamed(LPCSTR name) {
    LPTEXTURE texture;
    r_load_streamed = true;
    texture = R_LoadTexture(name);
    r_load_streamed = false;
    return texture;
}

/* Called when a streaming caller's working set changes (e.g. the WoW ADT window
   slides); every texture still needed is re-stamped during the reload that follows. */
void R_AdvanceTextureGeneration(void) {
    r_stream_generation++;
}

/* Remove a texture from the texid index list before its memory is freed. */
static void R_UnlinkTextureFromIndex(LPTEXTURE texture) {
    LPTEXTURE *pp = &g_textures;
    while (*pp) {
        if (*pp == texture) { *pp = texture->next; return; }
        pp = &(*pp)->next;
    }
}

/* Remove one path entry from both cache indexes without touching its shared texture allocation. */
static void R_FreeCacheEntry(rImageCacheEntry_t **link) {
    rImageCacheEntry_t *entry = *link;
    DWORD hash = R_TextureNameHash(entry->name) % BZ_IMAGE_CACHE_BUCKETS;
    rImageCacheEntry_t **hp = &r_image_hash[hash];

    while (*hp && *hp != entry) hp = &(*hp)->hash_next;
    if (*hp) *hp = entry->hash_next;
    *link = entry->next;
    ri.MemFree(entry->name);
    ri.MemFree(entry);
}

/* Free streamable textures not referenced within the last keep_recent generations.
   Non-streamed / pinned / built-in textures are never touched.  World geometry is
   rebuilt on every window slide, so any live reference re-stamps its texture the
   same generation — a stale stamp means nothing resident still uses it. */
void R_ReclaimStreamedTextures(DWORD keep_recent) {
    rImageCacheEntry_t **pp = &r_image_cache;
    DWORD freed = 0;
    while (*pp) {
        rImageCacheEntry_t *entry = *pp;
        if (entry->streamed && !entry->pinned && entry->owns_texture &&
            (r_stream_generation - entry->generation) > keep_recent) {
            LPTEXTURE texture = entry->texture;
            rImageCacheEntry_t **alias = &r_image_cache;

            /* The owner and every path alias become invalid together when the allocation is reclaimed. */
            while (*alias)
                if ((*alias)->texture == texture) R_FreeCacheEntry(alias);
                else alias = &(*alias)->next;
            R_UnlinkTextureFromIndex(texture);
            R_Call(glDeleteTextures, 1, &texture->texid);
            ri.MemFree(texture);
            freed++;
            pp = &r_image_cache;
        } else {
            pp = &(*pp)->next;
        }
    }
    if (freed) fprintf(stderr, "R_ReclaimStreamedTextures: freed %u stale streaming textures (gen %u)\n", freed, r_stream_generation);
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

/* The format describes the supplied bytes, never the host OS; unsupported BGRA preserves the caller's buffer. */
void R_LoadTextureMipLevel(LPCTEXTURE texture, LPCTEXMIP mip) {
    GLenum format = GL_RGBA, internal = GL_RGBA;
    BYTE *rgba = NULL;
    if (!mip->width || !mip->height)
        return;
    if (mip->format == PIXEL_BGRA) {
        if (r_bgra_internal) {
            format = BZ_GL_BGRA; internal = r_bgra_internal;
        } else if (mip->pixels) {
            /* Core GLES has no BGRA upload; this explicit, init-logged conversion keeps colors unchanged. */
            size_t bytes = (size_t)mip->width * mip->height * sizeof(COLOR32);
            rgba = ri.MemAlloc(bytes);
            memcpy(rgba, mip->pixels, bytes);
            R_SwapRedBlue(rgba, mip->width * mip->height, sizeof(COLOR32));
        }
    }
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
    R_Call(glTexImage2D, GL_TEXTURE_2D, mip->level, internal, mip->width, mip->height, 0, format, GL_UNSIGNED_BYTE, rgba ? rgba : mip->pixels);
    if (rgba) ri.MemFree(rgba);
    if (mip->level > 0) {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip->level);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        R_Call(glTexParameteri, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}
