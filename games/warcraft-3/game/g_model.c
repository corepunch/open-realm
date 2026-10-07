#include "g_local.h"
#include <ctype.h>
#include <stdlib.h>
#ifdef BZ_TESTS
#include "shared/test.h"
#endif

enum {
    ID_SEQS = MAKEFOURCC('S','E','Q','S'),
};

static uint32_t fnv1a32(cstring_t str) {
    uint32_t prime = 16777619;
    uint32_t hash  = 2166136261;
    while (*str) {
        hash = (hash ^ *str++) * prime;
    }
    return hash;
}

static void ConvertMDLXAnimationName(animation_t *seq) {
    char buffer[80];
    char *last_char = buffer;
    memset(buffer, 0, sizeof(buffer));
    strlcpy(buffer, seq->name, sizeof(buffer));
    for (char *ch = buffer; *ch; ch++) {
        if (isdigit(*ch) || *ch == '-') {
            while (*(++last_char)) {
                *last_char = '\0';
            }
            seq->syncpoint = fnv1a32(buffer);
            return;
        } else if (isalpha(*ch)) {
            *ch = tolower(*ch);
            last_char = ch;
        }
    }
    for (size_t len = strlen(buffer); len && isspace((unsigned char)buffer[len - 1]);)
        buffer[--len] = '\0';
    seq->syncpoint = fnv1a32(buffer);
}

/* ---- MD34 (StarCraft II / SC2 M3) ---- */

typedef struct {
    uint32_t nEntries;
    uint32_t offset;
    uint32_t flags;
} md34Reference_t;

struct md33Header {
    uint32_t ofsRefs;
    uint32_t nRefs;
    md34Reference_t MODL;
};

struct md34ReferenceEntry {
    uint32_t id;
    uint32_t offset;
    uint32_t nEntries;
    uint32_t version;
};

struct md34BoundingSphere {
    vec3_t min;
    vec3_t max;
    float radius;
};

struct md34NameRef {
    uint32_t nEntries;
    uint32_t ref;
    uint32_t flags;
};

struct md34Sequence {
    uint32_t unknown[2];
    struct md34NameRef name;
    uint32_t interval[2];
    float movementSpeed;
    uint32_t flags;
    uint32_t frequency;
    int32_t unk[3];
    int32_t unk2;
    struct md34BoundingSphere boundingSphere;
    int32_t d5[3];
};

static uint8_t const *ModelDataAt(uint8_t const *data, uint32_t data_size, uint32_t offset, uint32_t size) {
    if (!data || offset > data_size || size > data_size - offset)
        return NULL;
    return data + offset;
}

static int compare_animation_name(void const *a, void const *b) {
    return strcmp(((animation_t const *)a)->name, ((animation_t const *)b)->name);
}

static animation_t *LoadModelMD34(uint8_t const *data, uint32_t data_size, uint32_t *out_count) {
    struct md33Header const *hdr = (struct md33Header const *)ModelDataAt(data, data_size, 4, sizeof(*hdr));
    struct md34ReferenceEntry const *ent;

    animation_t *animations = NULL;
    uint32_t num = 0;

    if (!hdr) {
        *out_count = 0;
        return NULL;
    }
    ent = (struct md34ReferenceEntry const *)ModelDataAt(data, data_size, hdr->ofsRefs,
        sizeof(struct md34ReferenceEntry) * hdr->nRefs);
    if (!ent) {
        *out_count = 0;
        return NULL;
    }

    FOR_LOOP(i, hdr->nRefs) {
        struct md34ReferenceEntry const *re = ent + i;
        if (re->id != MAKEFOURCC('S','Q','E','S'))
            continue;
        struct md34Sequence const *seq = (struct md34Sequence const *)ModelDataAt(data, data_size, re->offset,
            re->nEntries * sizeof(struct md34Sequence));
        if (!seq)
            continue;
        animations = gi.MemAlloc(sizeof(animation_t) * re->nEntries);
        memset(animations, 0, sizeof(animation_t) * re->nEntries);
        num = re->nEntries;
        uint32_t startanim = 0;
        FOR_LOOP(j, re->nEntries) {
            struct md34Sequence const *src = seq + j;
            char const *name = src->name.ref < hdr->nRefs
                ? (char const *)ModelDataAt(data, data_size, ent[src->name.ref].offset, src->name.nEntries)
                : NULL;
            animation_t *dest = animations + j;
            if (name) {
                uint32_t name_len = MIN(src->name.nEntries, sizeof(dest->name) - 1);
                memcpy(dest->name, name, name_len);
            }
            dest->interval[0] = startanim + src->interval[0];
            dest->interval[1] = startanim + src->interval[1];
            startanim += src->interval[1];
        }
        qsort(animations, num, sizeof(animation_t), compare_animation_name);
        FOR_LOOP(j, num) {
            ConvertMDLXAnimationName(animations + j);
        }
        break;
    }
    *out_count = num;
    return animations;
}

/* ---- MDLX (Warcraft III) ---- */

static animation_t *LoadModelMDLXSequences(uint8_t const *data,uint32_t size,uint32_t *out_count) {
    enum { SEQ_RECORD_SIZE = 132 };
    uint32_t num=size/SEQ_RECORD_SIZE;
    animation_t *animations=gi.MemAlloc(sizeof(*animations)*num);
    memset(animations,0,sizeof(*animations)*num);
    FOR_LOOP(i,num) {
        memcpy(animations+i,data+i*SEQ_RECORD_SIZE,SEQ_RECORD_SIZE);
        ConvertMDLXAnimationName(animations+i);
    }
    *out_count=num;return animations;
}

static animation_t *LoadModelMDLX(uint8_t const *data, uint32_t data_size, uint32_t *out_count) {
    uint32_t payloadSize = data_size > 4 ? data_size - 4 : 0;
    animation_t *animations = NULL;
    uint32_t num = 0;
    uint8_t const *ptr = data + 4;
    uint8_t const *end = ptr + payloadSize;

    while (ptr && ptr + 8 <= end) {
        uint32_t header, size;
        memcpy(&header, ptr, sizeof(uint32_t));
        memcpy(&size,   ptr + 4, sizeof(uint32_t));
        ptr += 8;
        if (ptr + size > end) {
            size = (uint32_t)(end - ptr);
        }
        if (header == ID_SEQS) {
            gi.MemFree(animations);
            animations=LoadModelMDLXSequences(ptr,size,&num);
        }
        ptr += size;
    }
    *out_count = num;
    return animations;
}

#ifdef BZ_TESTS
TEST(wc3_model, empty_mdlx_sequence_name_does_not_break_animation_loading) {
    enum { SEQ_SIZE = 132, HEADER_SIZE = 12 };
    uint8_t data[HEADER_SIZE + 2 * SEQ_SIZE] = {0};
    uint32_t chunk = ID_SEQS, chunk_size = 2 * SEQ_SIZE, count = 0;
    animation_t *animations;

    memcpy(data, "MDLX", 4);
    memcpy(data + 4, &chunk, sizeof(chunk));
    memcpy(data + 8, &chunk_size, sizeof(chunk_size));
    memcpy(data + HEADER_SIZE + SEQ_SIZE, "Stand", 5);
    animations = LoadModelMDLX(data, sizeof(data), &count);
    T_NOT_NULL(animations);
    T_EQ(count, 2);
    if (animations && count == 2) {
        T_STREQ(animations[0].name, "");
        T_EQ(animations[0].syncpoint, fnv1a32(""));
        T_STREQ(animations[1].name, "Stand");
    }
    if (animations) gi.MemFree(animations);
}
#endif

/* ---- model cache ---- */

#define G_MAX_MODELS MAX_MODELS

typedef struct { uint32_t first, count; } animationVariantSpan_t;
typedef struct {
    animation_t *animations;
    animationVariantSpan_t *variants;
    uint32_t *variant_indices;
    uint32_t        num_animations;
    char         filename[MAX_PATHLEN];
    bool         loaded;   /* load attempted (success or failure) — avoids
                              re-reading/parsing the model from the MPQ every
                              frame for models that fail or have 0 animations. */
} g_cmodel_t;

static g_cmodel_t g_models[G_MAX_MODELS];
/* Logical text outlives model resource bindings. Units share immutable records;
 * changing a request or property set publishes a different record. */
struct unitAnimationText_s {
    struct unitAnimationText_s *next;
    uint32_t hash;
    size_t length;
    bool walk;
    char text[];
};
static unitAnimationText_t *animation_texts[1024];
typedef struct animationSelection_s {
    struct animationSelection_s *next;
    uint32_t model;
    unitAnimationText_t const *request, *properties;
    animation_t const *selected, *tagged;
} animationSelection_t;
static animationSelection_t *animation_selections[1024];
#ifdef BZ_TESTS
static uint32_t animation_selection_visits;
#endif

void G_NormalizeModelFilename(cstring_t authored, string_t out, size_t out_size) {
    cstring_t slash_back;
    cstring_t slash_forward;
    cstring_t slash;
    cstring_t dot;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!authored || !*authored) return;

    slash_back = strrchr(authored, '\\');
    slash_forward = strrchr(authored, '/');
    slash = slash_back;
    if (!slash || (slash_forward && slash_forward > slash)) slash = slash_forward;
    dot = strrchr(authored, '.');
    if (dot && (!slash || dot > slash))
        strlcpy(out, authored, out_size);
    else
        snprintf(out, out_size, "%s.mdx", authored);
}

int G_RegisterModel(cstring_t filename) {
    /* Units with a missile weapon but no authored Missileart have no model. */
    if (!filename || !filename[0]) return 0;
    int index = gi.ModelIndex(filename);
    if (index > 0 && index < G_MAX_MODELS && !g_models[index].filename[0])
        strlcpy(g_models[index].filename, filename, sizeof(g_models[index].filename));
    return index;
}

static uint8_t *ReadModelFile(cstring_t filename, uint32_t *out_size) {
    uint8_t *data;

    if (!filename || !*filename)
        return NULL;
    data = gi.ReadFile(filename, out_size);
    if (!data) {
        PATHSTR path;
        size_t len = strlen(filename);
        if (len == 0 || len >= sizeof(path))
            return NULL;
        memcpy(path, filename, len + 1);
        path[len - 1] = 'x';
        data = gi.ReadFile(path, out_size);
    }
    return data;
}

/* MDLX chunk offsets let simulation read sequence data without inflating
 * geometry, textures or animation tracks. Visit every header to retain the
 * full reader's last-SEQS and truncated-record behavior. */
static bool LoadModelMDLXArchive(handle_t file,cstring_t filename,uint32_t size,g_cmodel_t *model) {
    uint32_t offset=4, actual;
    while(offset<=size && size-offset>=8) {
        uint32_t chunk[2];
        if(SFileSetFilePointer(file,(int32_t)offset,NULL,FILE_BEGIN)!=offset ||
            !SFileReadFile(file,chunk,sizeof(chunk),&actual,NULL) || actual!=sizeof(chunk)) {
            fprintf(stderr,"LoadModel: incomplete chunk header in %s at %u\n",filename,offset);
            return false;
        }
        offset+=8;
        uint32_t payload=MIN(chunk[1],size-offset);
        if(chunk[0]==ID_SEQS) {
            uint8_t *data=gi.MemAlloc(payload ? payload : 1);
            if(payload && (!SFileReadFile(file,data,payload,&actual,NULL) || actual!=payload)) {
                fprintf(stderr,"LoadModel: incomplete sequences in %s at %u\n",filename,offset);
                gi.MemFree(data);return false;
            }
            gi.MemFree(model->animations);
            model->animations=LoadModelMDLXSequences(data,payload,&model->num_animations);
            gi.MemFree(data);
        }
        offset+=payload;
    }
    return true;
}

static g_cmodel_t *LoadModel(cstring_t filename) {
    uint32_t fileheader;
    uint32_t data_size = 0;
    handle_t file=gi.OpenFile ? gi.OpenFile(filename) : NULL;
    if(file) {
        uint32_t actual;
        data_size=SFileGetFileSize(file,NULL);
        bool header=data_size>=4 && SFileReadFile(file,&fileheader,4,&actual,NULL) && actual==4;
        if(header && fileheader==ID_MDLX) {
            g_cmodel_t *model=gi.MemAlloc(sizeof(*model));
            memset(model,0,sizeof(*model));
            bool loaded=LoadModelMDLXArchive(file,filename,data_size,model);
            gi.CloseFile(file);
            if(loaded)return model;
            gi.MemFree(model->animations);gi.MemFree(model);return NULL;
        }
        gi.CloseFile(file);
    }
    uint8_t *data = ReadModelFile(filename, &data_size);
    if (!data || data_size < sizeof(fileheader)) {
        if (data)
            gi.MemFree(data);
        return NULL;
    }

    g_cmodel_t *model = gi.MemAlloc(sizeof(g_cmodel_t));
    memset(model, 0, sizeof(*model));

    memcpy(&fileheader, data, sizeof(fileheader));
    switch (fileheader) {
        case ID_MDLX:
            model->animations = LoadModelMDLX(data, data_size, &model->num_animations);
            break;
        case ID_43DM:
            model->animations = LoadModelMD34(data, data_size, &model->num_animations);
            break;
        default:
            break;
    }
    gi.MemFree(data);
    return model;
}

static g_cmodel_t *GetModel(uint32_t modelindex) {
    if (modelindex == 0 || modelindex >= G_MAX_MODELS)
        return NULL;
    g_cmodel_t *entry = &g_models[modelindex];
    if (!entry->loaded && entry->filename[0]) {
        /* Attempt the load exactly once. Mark loaded up-front so a failed or
         * empty parse is not retried (re-reading the MPQ) on every frame. */
        entry->loaded = true;
        g_cmodel_t *m = LoadModel(entry->filename);
        if (m) {
            entry->animations     = m->animations;
            entry->num_animations = m->num_animations;
            gi.MemFree(m);
        }
    }
    return entry->animations ? entry : NULL;
}

#ifdef BZ_TESTS
#include <unistd.h>
static handle_t sequence_test_archive;
static uint32_t sequence_test_opens, sequence_test_full_bytes;
static handle_t sequence_test_open(cstring_t filename) {
    handle_t file = NULL;
    sequence_test_opens++;
    SFileOpenFileEx(sequence_test_archive,filename,0,&file);
    return file;
}
static void sequence_test_close(handle_t file) { SFileCloseFile(file); }
static handle_t sequence_test_read(cstring_t filename,uint32_t *size) {
    handle_t file;
    uint32_t actual;
    if(!SFileOpenFileEx(sequence_test_archive,filename,0,&file))return NULL;
    *size=SFileGetFileSize(file,NULL);
    void *buffer=gi.MemAlloc(*size);
    T_ASSERT(SFileReadFile(file,buffer,*size,&actual,NULL));
    T_EQ(actual,*size);sequence_test_full_bytes+=actual;
    SFileCloseFile(file);return buffer;
}
TEST(wc3_model, server_sequences_skip_archive_geometry_payloads) {
    char path[]="/tmp/openrealm-model-sequences-XXXXXX";
    uint32_t const junk_size=256*1024, size=4+8+junk_size+8+264;
    uint8_t *data=calloc(1,size);
    uint32_t id=MAKEFOURCC('J','U','N','K'), count=0, seq_size=264;
    memcpy(data,"MDLX",4);memcpy(data+4,&id,4);memcpy(data+8,&junk_size,4);
    id=ID_SEQS;memcpy(data+12+junk_size,&id,4);memcpy(data+16+junk_size,&seq_size,4);
    memcpy(data+20+junk_size,"Stand",6);memcpy(data+152+junk_size,"Death",6);
    animation_t *expected=LoadModelMDLX(data,size,&count);
    int fd=mkstemp(path);T_ASSERT(fd>=0);close(fd);
    T_ASSERT(SFileCreateArchive(path,0,16,&sequence_test_archive));
    T_ASSERT(SFileAddFileFromBuffer(sequence_test_archive,"stream-sequences.mdx",data,size));
    T_ASSERT(SFileCloseArchive(sequence_test_archive));
    T_ASSERT(SFileOpenArchive(path,0,0,&sequence_test_archive));
    struct game_import saved=gi;
    gi.OpenFile=sequence_test_open;gi.CloseFile=sequence_test_close;gi.ReadFile=sequence_test_read;
    g_cmodel_t saved_model=g_models[G_MAX_MODELS-1];
    g_models[G_MAX_MODELS-1]=(g_cmodel_t){.filename="stream-sequences.mdx"};
    sequence_test_opens=sequence_test_full_bytes=0;
    g_cmodel_t *model=GetModel(G_MAX_MODELS-1);
    T_NOT_NULL(model);
    if(model){T_EQ(model->num_animations,count);T_EQ(memcmp(model->animations,expected,count*sizeof(*expected)),0);}
    T_EQ(sequence_test_opens,1);T_EQ(sequence_test_full_bytes,0);
    gi.MemFree(g_models[G_MAX_MODELS-1].animations);g_models[G_MAX_MODELS-1]=saved_model;
    gi.MemFree(expected);gi=saved;
    SFileCloseArchive(sequence_test_archive);sequence_test_archive=NULL;unlink(path);free(data);
}
TEST(wc3_model, streamed_sequences_keep_last_chunk_and_truncated_records) {
    char path[]="/tmp/openrealm-model-tail-XXXXXX";
    uint8_t data[4+8+132+8+263]={0};
    uint32_t id=ID_SEQS,first_size=132,last_size=264,expected_count;
    memcpy(data,"MDLX",4);memcpy(data+4,&id,4);memcpy(data+8,&first_size,4);
    memcpy(data+12,"Stand",6);memcpy(data+144,&id,4);memcpy(data+148,&last_size,4);
    memcpy(data+152,"Death",6);
    animation_t *expected=LoadModelMDLX(data,sizeof(data),&expected_count);
    T_EQ(expected_count,1);
    int fd=mkstemp(path);T_ASSERT(fd>=0);close(fd);
    handle_t archive,file;
    T_ASSERT(SFileCreateArchive(path,0,16,&archive));
    T_ASSERT(SFileAddFileFromBuffer(archive,"tail.mdx",data,sizeof(data)));
    T_ASSERT(SFileCloseArchive(archive));T_ASSERT(SFileOpenArchive(path,0,0,&archive));
    T_ASSERT(SFileOpenFileEx(archive,"tail.mdx",0,&file));
    g_cmodel_t model={0};
    T_ASSERT(LoadModelMDLXArchive(file,"tail.mdx",sizeof(data),&model));
    T_EQ(model.num_animations,expected_count);
    T_EQ(memcmp(model.animations,expected,expected_count*sizeof(*expected)),0);
    gi.MemFree(model.animations);gi.MemFree(expected);
    SFileCloseFile(file);SFileCloseArchive(archive);unlink(path);
}
#endif

animation_t const *G_GetAnimation(uint32_t modelindex, cstring_t animname) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model)
        return NULL;
    uint32_t hash = fnv1a32(animname);
    FOR_LOOP(i, model->num_animations) {
        if (model->animations[i].syncpoint == hash)
            return &model->animations[i];
    }
    FOR_LOOP(i, model->num_animations) {
        if (!strcasecmp(model->animations[i].name, animname))
            return &model->animations[i];
    }
    return NULL;
}


#define WC3_ANIMATION_MAX_TAGS 24
#define WC3_ANIMATION_TAG_SIZE 32

typedef struct {
    char value[WC3_ANIMATION_MAX_TAGS][WC3_ANIMATION_TAG_SIZE];
    uint32_t count;
} animationTagSet_t;

static bool AnimationTokenIsNumeric(cstring_t token) {
    if (!token || !*token) return false;
    for (; *token; token++) if (!isdigit((unsigned char)*token)) return false;
    return true;
}

static bool AnimationTagSetContains(animationTagSet_t const *set, cstring_t token) {
    FOR_LOOP(i, set->count) if (!strcasecmp(set->value[i], token)) return true;
    return false;
}

static void AnimationTagSetAdd(animationTagSet_t *set, cstring_t token) {
    if (!token || !*token || AnimationTokenIsNumeric(token) ||
        AnimationTagSetContains(set, token) || set->count >= WC3_ANIMATION_MAX_TAGS)
        return;
    strlcpy(set->value[set->count++], token, WC3_ANIMATION_TAG_SIZE);
}

#ifdef BZ_TESTS
static uint32_t animation_words_read;
#endif
static bool AnimationReadWord(cstring_t *text, char token[WC3_ANIMATION_TAG_SIZE]) {
    uint32_t length = 0;

    if (!*text) return false;
    for (;;) {
        unsigned char ch = (unsigned char)*(*text)++;
        if (isalnum(ch) || ch == '_') {
            if (length + 1 < WC3_ANIMATION_TAG_SIZE) token[length++] = (char)tolower(ch);
            continue;
        }
        /* Leave the cursor at the terminator for the next read. */
        if (!ch) (*text)--;
        if (length) {
            token[length] = '\0';
#ifdef BZ_TESTS
            animation_words_read++;
#endif
            return true;
        }
        if (!ch) return false;
    }
}

static void AnimationParseWords(cstring_t text, animationTagSet_t *set) {
    char token[WC3_ANIMATION_TAG_SIZE];
    while (AnimationReadWord(&text, token)) AnimationTagSetAdd(set, token);
}

/* The family is the first nonnumeric word. Secondary tags are consumed by
 * the actual selector; deciding whether Walk needs a variant must not build
 * two complete tag sets on every stand transition. */
static void AnimationParsePrimary(cstring_t text, char primary[WC3_ANIMATION_TAG_SIZE]) {
    while (AnimationReadWord(&text, primary))
        if (!AnimationTokenIsNumeric(primary)) return;
    primary[0] = '\0';
}

static void AnimationParseRequest(cstring_t text, char primary[WC3_ANIMATION_TAG_SIZE],
                                  animationTagSet_t *secondary) {
    animationTagSet_t words = {0};

    primary[0] = '\0';
    AnimationParseWords(text, &words);
    if (!words.count) return;
    strlcpy(primary, words.value[0], WC3_ANIMATION_TAG_SIZE);
    for (uint32_t i = 1; i < words.count; i++) AnimationTagSetAdd(secondary, words.value[i]);
}

static cstring_t AnimationText(unitAnimationText_t const *value) {
    return value ? value->text : "";
}

static unitAnimationText_t const *AnimationInternText(cstring_t text, size_t limit) {
    if (!text || !*text || !limit) return NULL;
    size_t length = 0;
    uint32_t hash = 2166136261u;
    while (length < limit && text[length]) hash = (hash ^ (uint8_t)text[length++]) * 16777619u;
    unitAnimationText_t **bucket = animation_texts + (hash & 1023);
    for (unitAnimationText_t *value = *bucket; value; value = value->next)
        if (value->hash == hash && value->length == length && !memcmp(value->text, text, length)) return value;
    unitAnimationText_t *value = gi.MemAlloc(sizeof(*value) + length + 1);
    value->hash = hash; value->length = length;
    memcpy(value->text, text, length); value->text[length] = '\0';
    char primary[WC3_ANIMATION_TAG_SIZE];
    AnimationParsePrimary(value->text, primary);
    value->walk = !strcasecmp(primary, "walk");
    value->next = *bucket; *bucket = value;
    return value;
}

cstring_t G_UnitAnimationRequest(edict_t const *unit) { return AnimationText(unit->animation_request); }
cstring_t G_UnitAnimationProperties(edict_t const *unit) { return AnimationText(unit->animation_props); }

/* These text owners also serve save restoration and direct sequence selection.
 * The public setters retain their old, distinct parsing and selection stages. */
void G_StoreUnitAnimationRequest(edict_t *unit, cstring_t text) {
    unit->animation_request = AnimationInternText(text, WC3_ANIMATION_REQUEST_SIZE - 1);
}
void G_StoreUnitAnimationProperties(edict_t *unit, cstring_t text) {
    unit->animation_props = AnimationInternText(text, WC3_ANIMATION_PROPERTIES_SIZE - 1);
}

void G_ClearUnitAnimationText(void) {
    FOR_LOOP(i, sizeof(animation_texts) / sizeof(*animation_texts)) {
        while (animation_texts[i]) {
            unitAnimationText_t *value = animation_texts[i];
            animation_texts[i] = value->next;
            gi.MemFree(value);
        }
    }
}

static uint32_t AnimationTagSetMatchCount(animationTagSet_t const *required,
                                       animationTagSet_t const *candidate) {
    uint32_t matches = 0;
    FOR_LOOP(i, required->count) if (AnimationTagSetContains(candidate, required->value[i])) matches++;
    return matches;
}

static bool AnimationTagSetsEqual(animationTagSet_t const *a, animationTagSet_t const *b) {
    return a->count == b->count && AnimationTagSetMatchCount(a, b) == a->count;
}

typedef struct {
    uint32_t index, syncpoint;
    char primary[WC3_ANIMATION_TAG_SIZE];
    animationTagSet_t tags;
} animationVariantKey_t;

static int AnimationCompareTag(void const *a, void const *b) { return strcasecmp(a,b); }

static int AnimationCompareVariantFamily(animationVariantKey_t const *a, animationVariantKey_t const *b) {
    if(a->syncpoint!=b->syncpoint)return a->syncpoint<b->syncpoint?-1:1;
    int order=strcasecmp(a->primary,b->primary);
    if(order)return order;
    if(a->tags.count!=b->tags.count)return a->tags.count<b->tags.count?-1:1;
    FOR_LOOP(i,a->tags.count) {
        order=strcasecmp(a->tags.value[i],b->tags.value[i]);
        if(order)return order;
    }
    return 0;
}

static int AnimationCompareVariant(void const *left, void const *right) {
    animationVariantKey_t const *a=left,*b=right;
    int order=AnimationCompareVariantFamily(a,b);
    return order ? order : a->index<b->index ? -1 : a->index>b->index;
}

/* Immutable sequence families, stored once per model in O(sequence count)
 * space. Sorting groups equal tag sets, then restores authored sequence order
 * within each family so reservoir sampling consumes the same random draws. */
static void AnimationPrepareVariants(g_cmodel_t *model) {
    if(model->variants || !model->num_animations)return;
    uint32_t count=model->num_animations;
    animationVariantKey_t *keys=gi.MemAlloc((size_t)count*sizeof(*keys));
    model->variants=gi.MemAlloc((size_t)count*(sizeof(*model->variants)+sizeof(*model->variant_indices)));
    if(!keys || !model->variants)gi.error("Animation: cannot prepare %u sequence variants",count);
    model->variant_indices=(uint32_t *)(model->variants+count);
    memset(keys,0,(size_t)count*sizeof(*keys));
    FOR_LOOP(i,count) {
        keys[i].index=i;keys[i].syncpoint=model->animations[i].syncpoint;
        AnimationParseRequest(model->animations[i].name,keys[i].primary,&keys[i].tags);
        qsort(keys[i].tags.value,keys[i].tags.count,sizeof(*keys[i].tags.value),AnimationCompareTag);
    }
    qsort(keys,count,sizeof(*keys),AnimationCompareVariant);
    for(uint32_t first=0;first<count;) {
        uint32_t end=first+1;
        while(end<count && !AnimationCompareVariantFamily(keys+first,keys+end))end++;
        for(uint32_t i=first;i<end;i++) {
            model->variants[keys[i].index]=(animationVariantSpan_t){first,end-first};
            model->variant_indices[i]=keys[i].index;
        }
        first=end;
    }
    gi.MemFree(keys);
}

static bool AnimationTagSetContainsAll(animationTagSet_t const *candidate,
                                       animationTagSet_t const *required) {
    return AnimationTagSetMatchCount(required, candidate) == required->count;
}

static void AnimationTagSetReplace(animationTagSet_t const *source, cstring_t from, cstring_t to,
                                   animationTagSet_t *dest) {
    memset(dest, 0, sizeof(*dest));
    FOR_LOOP(i, source->count) {
        AnimationTagSetAdd(dest, !strcasecmp(source->value[i], from) ? to : source->value[i]);
    }
}

static animation_t const *AnimationFindContainingSet(animation_t const *animations, uint32_t count, cstring_t primary,
                                               animationTagSet_t const *required) {
    animation_t const *contains = NULL;
    uint32_t contains_extras = UINT32_MAX;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        uint32_t matches, extras;

        AnimationParseRequest(animations[i].name, sequence_primary, &sequence_tags);
        if (strcasecmp(primary, sequence_primary)) continue;
        matches = AnimationTagSetMatchCount(required, &sequence_tags);
        extras = sequence_tags.count > matches ? sequence_tags.count - matches : 0;
        if (!AnimationTagSetContainsAll(&sequence_tags, required)) continue;
        if (extras == 0) return animations + i;
        if (!contains || extras < contains_extras) {
            contains = animations + i;
            contains_extras = extras;
        }
    }
    return contains;
}

/* Select by Warcraft's primary animation family plus Required Animation Names.
 * Sequence number suffixes are intentionally ignored. Exact secondary-tag sets
 * win; if a model has no exact set, prefer a sequence containing all requested
 * tags with the fewest extras, then the best-overlap sequence. Warcraft's stock
 * object data also uses `alternateex` for some alternate-form units whose models
 * only expose `Alternate` sequences (notably Medivh raven form). Preserve a real
 * AlternateEx sequence when present, but retry Alternate before dropping to an
 * unrelated/untagged fallback. */
animation_t const *G_SelectAnimationForProperties(animation_t const *animations, uint32_t count,
                                            cstring_t animname, cstring_t properties) {
#ifdef BZ_TESTS
    animation_selection_visits++;
#endif
    animationTagSet_t required = {0};
    char primary[WC3_ANIMATION_TAG_SIZE];
    animation_t const *contains = NULL;
    animation_t const *overlap = NULL;
    animation_t const *primary_fallback = NULL;
    uint32_t contains_extras = UINT32_MAX;
    uint32_t overlap_matches = 0;
    uint32_t overlap_extras = UINT32_MAX;

    if (!animations || !count || !animname || !*animname) return NULL;
    AnimationParseRequest(animname, primary, &required);
    AnimationParseWords(properties, &required);
    if (!primary[0]) return NULL;

    FOR_LOOP(i, count) {
        animationTagSet_t sequence_tags = {0};
        char sequence_primary[WC3_ANIMATION_TAG_SIZE];
        uint32_t matches, extras;

        AnimationParseRequest(animations[i].name, sequence_primary, &sequence_tags);
        if (strcasecmp(primary, sequence_primary)) continue;
        if (!primary_fallback) primary_fallback = animations + i;
        matches = AnimationTagSetMatchCount(&required, &sequence_tags);
        extras = sequence_tags.count > matches ? sequence_tags.count - matches : 0;

        if (AnimationTagSetContainsAll(&sequence_tags, &required)) {
            if (extras == 0) return animations + i;
            if (!contains || extras < contains_extras) {
                contains = animations + i;
                contains_extras = extras;
            }
        }
        if (matches && (!overlap || matches > overlap_matches ||
                        (matches == overlap_matches && extras < overlap_extras))) {
            overlap = animations + i;
            overlap_matches = matches;
            overlap_extras = extras;
        }
    }

    if (contains) return contains;

    if (AnimationTagSetContains(&required, "alternateex") &&
        !AnimationTagSetContains(&required, "alternate")) {
        animationTagSet_t alternate_fallback = {0};
        animation_t const *alternate;

        AnimationTagSetReplace(&required, "alternateex", "alternate", &alternate_fallback);
        alternate = AnimationFindContainingSet(animations, count, primary, &alternate_fallback);
        if (alternate) return alternate;
    }

    if (overlap) return overlap;
    /* Warsmash/Warcraft fall back within the requested primary family when a
     * model lacks the requested secondary tag (for example a model with only
     * one generic Decay sequence serving both Decay Flesh and Decay Bone). */
    if (primary_fallback) return primary_fallback;
    return NULL;
}

static animation_t const *AnimationForPreparedProperties(uint32_t modelindex,
        unitAnimationText_t const *request, unitAnimationText_t const *properties, bool tagged) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model) return NULL;
    uint32_t hash = (request ? request->hash : 0) ^ (properties ? properties->hash : 0) ^ modelindex;
    animationSelection_t **bucket = animation_selections + (hash & 1023);
    for (animationSelection_t *entry = *bucket; entry; entry = entry->next)
        if (entry->model == modelindex && entry->request == request && entry->properties == properties)
            return tagged ? entry->tagged : entry->selected;
    cstring_t name = AnimationText(request), props = AnimationText(properties);
    animation_t const *selected = G_SelectAnimationForProperties(model->animations, model->num_animations, name, props);
    animationSelection_t *entry = gi.MemAlloc(sizeof(*entry));
    *entry = (animationSelection_t){ .next = *bucket, .model = modelindex, .request = request,
        .properties = properties, .tagged = selected, .selected = selected ? selected : G_GetAnimation(modelindex, name) };
    *bucket = entry;
    return tagged ? entry->tagged : entry->selected;
}

static animation_t const *AnimationForProperties(uint32_t modelindex, cstring_t animname,
                                                  cstring_t properties, bool tagged) {
    if (!animname) return NULL;
    return AnimationForPreparedProperties(modelindex, AnimationInternText(animname, SIZE_MAX),
                                            AnimationInternText(properties, SIZE_MAX), tagged);
}

animation_t const *G_GetAnimationForProperties(uint32_t modelindex, cstring_t animname, cstring_t properties) {
    return AnimationForProperties(modelindex, animname, properties, false);
}

/* Select numbered variants without crossing the selected sequence's tag set. */
animation_t const *G_SelectAnimationVariantForProperties(animation_t const *animations, uint32_t count,
                                                    cstring_t animname, cstring_t properties, bool randomize) {
    animation_t const *selected;
    animation_t const *choice = NULL;
    animationTagSet_t selected_tags = {0};
    char primary[WC3_ANIMATION_TAG_SIZE];
    char candidate_primary[WC3_ANIMATION_TAG_SIZE];
    uint32_t matches = 0;

    selected = G_SelectAnimationForProperties(animations, count, animname, properties);
    if (!selected || !randomize) return selected;
    AnimationParseRequest(selected->name, primary, &selected_tags);
    FOR_LOOP(i, count) {
        animation_t const *candidate = animations + i;
        animationTagSet_t candidate_tags = {0};
        AnimationParseRequest(candidate->name, candidate_primary, &candidate_tags);
        if (candidate->syncpoint != selected->syncpoint) continue;
        /* A shared sync point does not make Stand a variant of Walk. */
        if (strcasecmp(primary, candidate_primary)) continue;
        if (!AnimationTagSetsEqual(&selected_tags, &candidate_tags)) continue;
        matches++;
        if ((uint32_t)(rand() % matches) == 0) choice = candidate;
    }
    return choice ? choice : selected;
}

/* Select an authored animation variant while preserving the ordinary selector's fallback. */
static animation_t const *AnimationPreparedVariant(uint32_t modelindex, unitAnimationText_t const *request,
                                                    unitAnimationText_t const *properties, bool randomize) {
    g_cmodel_t *model = GetModel(modelindex);
    if (!model) return NULL;
    animation_t const *selected = AnimationForPreparedProperties(modelindex, request, properties, true);
    if (!selected || !randomize) return selected;
    AnimationPrepareVariants(model);
    animationVariantSpan_t span = model->variants[selected - model->animations];
    animation_t const *choice = selected;
    FOR_LOOP(i, span.count)
        if ((uint32_t)(rand() % (i + 1)) == 0) choice = model->animations + model->variant_indices[span.first + i];
    return choice;
}

static animation_t const *AnimationVariantForProperties(uint32_t modelindex, cstring_t animname,
                                                         cstring_t properties, bool randomize) {
    if (!animname) return NULL;
    return AnimationPreparedVariant(modelindex, AnimationInternText(animname, SIZE_MAX),
                                      AnimationInternText(properties, SIZE_MAX), randomize);
}

animation_t const *G_GetAnimationVariant(uint32_t modelindex, cstring_t animname, bool randomize) {
    return AnimationVariantForProperties(modelindex,animname,NULL,randomize);
}

bool G_AnimationHasPrimary(animation_t const *animation, cstring_t primary) {
    size_t len;
    unsigned char next;

    if (!animation || !primary || !*primary) return false;
    len = strlen(primary);
    if (strncasecmp(animation->name, primary, len)) return false;
    next = (unsigned char)animation->name[len];
    return next == '\0' || !isalnum(next);
}

static void AnimationTagSetWrite(animationTagSet_t const *set, string_t out, size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    FOR_LOOP(i, set->count) {
        if (i) strlcat(out, ",", out_size);
        strlcat(out, set->value[i], out_size);
    }
}

void G_ResetUnitAnimationPropertiesPrepared(edict_t *unit, unitAnimationDefaults_t *defaults) {
    UnitProfile_t const *row = unit->data.UnitProfile;
    cstring_t authored = row ? row->animProps : NULL;
    uint32_t metadata = G_UnitDataGeneration();
    if (!defaults->valid || defaults->row != row || defaults->authored != authored || defaults->metadata != metadata) {
        animationTagSet_t properties = {0};
        char text[WC3_ANIMATION_PROPERTIES_SIZE];
        AnimationParseWords(authored, &properties);
        AnimationTagSetWrite(&properties, text, sizeof(text));
        *defaults = (unitAnimationDefaults_t){ .row = row, .authored = authored, .metadata = metadata,
            .properties = AnimationInternText(text, WC3_ANIMATION_PROPERTIES_SIZE - 1), .valid = true };
    }
    unit->animation_props = defaults->properties;
    unit->animation_request = NULL;
}

void G_ResetUnitAnimationProperties(edict_t *unit) {
    if (unit) G_ResetUnitAnimationPropertiesPrepared(unit, &G_UnitRuntimeType(unit->class_id)->bindings.animation);
}

animation_t const *G_GetUnitAnimation(edict_t *unit, cstring_t animname) {
    if (!unit || !animname) return NULL;
    return AnimationForPreparedProperties(unit->s.model, AnimationInternText(animname, SIZE_MAX), unit->animation_props, false);
}

void G_SetUnitAnimation(edict_t *unit, cstring_t animname) {
    if (!unit || !animname) return;
    G_StoreUnitAnimationRequest(unit, animname);
    unitAnimationText_t const *request = unit->animation_request;
    unit->animation = request && request->walk ?
        AnimationPreparedVariant(unit->s.model, request, unit->animation_props, true) : NULL;
    if (!unit->animation)
        unit->animation = AnimationForPreparedProperties(unit->s.model, request, unit->animation_props, false);
}

void G_AddUnitAnimationProperties(edict_t *unit, cstring_t properties, bool add) {
    animationTagSet_t current = {0};
    animationTagSet_t changed = {0};
    animationTagSet_t result = {0};
    char request[WC3_ANIMATION_REQUEST_SIZE];
    bool mutated = false;

    if (!unit || !properties || !*properties) return;
    AnimationParseWords(G_UnitAnimationProperties(unit), &current);
    AnimationParseWords(properties, &changed);
    result = current;

    if (add) {
        FOR_LOOP(i, changed.count) {
            if (!AnimationTagSetContains(&result, changed.value[i])) {
                AnimationTagSetAdd(&result, changed.value[i]);
                mutated = true;
            }
        }
    } else {
        animationTagSet_t kept = {0};
        FOR_LOOP(i, result.count) {
            if (AnimationTagSetContains(&changed, result.value[i])) mutated = true;
            else AnimationTagSetAdd(&kept, result.value[i]);
        }
        result = kept;
    }
    if (!mutated) return;

    char text[WC3_ANIMATION_PROPERTIES_SIZE];
    AnimationTagSetWrite(&result, text, sizeof(text));
    G_StoreUnitAnimationProperties(unit, text);
    strlcpy(request, G_UnitAnimationRequest(unit), sizeof(request));
    if (!request[0] && unit->currentmove && unit->currentmove->animation)
        strlcpy(request, unit->currentmove->animation, sizeof(request));
    if (!request[0]) strlcpy(request, "stand", sizeof(request));
    G_SetUnitAnimation(unit, request);
}

void G_FreeModels(void) {
    FOR_LOOP(i, sizeof(animation_selections) / sizeof(*animation_selections)) {
        while (animation_selections[i]) {
            animationSelection_t *entry = animation_selections[i];
            animation_selections[i] = entry->next;
            gi.MemFree(entry);
        }
    }
    FOR_LOOP(i, G_MAX_MODELS) {
        if(g_models[i].variants)gi.MemFree(g_models[i].variants);
        if (g_models[i].animations) {
            gi.MemFree(g_models[i].animations);
        }
        memset(&g_models[i], 0, sizeof(g_models[i]));
    }
}

#ifdef BZ_TESTS
TEST(wc3_model, repeated_unit_stand_reads_only_the_primary_family) {
    uint32_t const index = G_MAX_MODELS - 1;
    animation_t values[] = { { .name = "Stand Foo Bar Alternate" }, { .name = "Stand" },
        { .name = "Walk Foo Alternate" }, { .name = "Walk Foo Alternate 2" } };
    G_FreeModels();
    g_models[index].animations = gi.MemAlloc(sizeof(values));
    memcpy(g_models[index].animations, values, sizeof(values));
    g_models[index].num_animations = sizeof(values) / sizeof(*values);
    g_models[index].loaded = true;
    edict_t unit = { .s.model = index };
    G_StoreUnitAnimationProperties(&unit, "alternate");
    cstring_t const request = "001 002 STAND,foo-bar";
    G_SetUnitAnimation(&unit, request);
    T_ASSERT(unit.animation == g_models[index].animations);
    animation_words_read = 0;
    FOR_LOOP(i, 128) {
        G_SetUnitAnimation(&unit, request);
        T_ASSERT(unit.animation == g_models[index].animations);
        T_STREQ(G_UnitAnimationRequest(&unit), request);
    }
    T_EQ(animation_words_read, 0);
    cstring_t const requests[] = { "000 !!! wAlK,Foo Alternate", "stand walk", "_walk", "123 456",
        "000-stand,foo", "00000000000000000000000000000000000000000000 walk foo", "", "walk foo", "walk2" };
    FOR_LOOP(i, sizeof(requests) / sizeof(*requests)) {
        char truncated[WC3_ANIMATION_REQUEST_SIZE], primary[WC3_ANIMATION_TAG_SIZE];
        animationTagSet_t tags = {0};
        strlcpy(truncated, requests[i], sizeof(truncated));
        AnimationParseRequest(truncated, primary, &tags);
        srand(471 + i);
        animation_t const *expected = !strcasecmp(primary, "walk") ?
            G_SelectAnimationVariantForProperties(g_models[index].animations,g_models[index].num_animations,
                                                   truncated,G_UnitAnimationProperties(&unit),true) : NULL;
        if (!expected) expected = G_GetUnitAnimation(&unit, truncated);
        int next = rand();
        srand(471 + i);
        G_SetUnitAnimation(&unit, requests[i]);
        T_ASSERT(unit.animation == expected);
        T_EQ(rand(), next);
        T_STREQ(G_UnitAnimationRequest(&unit), truncated);
    }
    G_FreeModels();
}
TEST(wc3_model, immutable_animation_text_and_selections_survive_large_working_sets) {
    uint32_t const index = G_MAX_MODELS - 1;
    animation_t values[] = {{.name = "Stand"}, {.name = "Stand Alternate"}, {.name = "Walk"}};
    G_FreeModels();
    g_models[index].animations = gi.MemAlloc(sizeof(values));
    memcpy(g_models[index].animations, values, sizeof(values));
    g_models[index].num_animations = sizeof(values) / sizeof(*values);
    g_models[index].loaded = true;
    UnitProfile_t profile = {.animProps = "Alternate alternate"};
    edict_t first = {.s.model = index, .data.UnitProfile = &profile}, second = first;
    unitAnimationDefaults_t defaults = {0};
    G_ResetUnitAnimationPropertiesPrepared(&first, &defaults);
    G_SetUnitAnimation(&first, "stand");
    animation_words_read = animation_selection_visits = 0;
    FOR_LOOP(i, 4096) {
        G_ResetUnitAnimationPropertiesPrepared(&second, &defaults);
        G_SetUnitAnimation(&second, "stand");
        T_ASSERT(first.animation_props == second.animation_props);
        T_ASSERT(first.animation_request == second.animation_request);
        T_ASSERT(first.animation == second.animation);
    }
    T_EQ(animation_words_read, 0); T_EQ(animation_selection_visits, 0);
    G_AddUnitAnimationProperties(&first, "alternate", false);
    T_STREQ(G_UnitAnimationProperties(&first), "");
    T_STREQ(G_UnitAnimationProperties(&second), "alternate");
    T_ASSERT(first.animation == g_models[index].animations);
    T_ASSERT(second.animation == g_models[index].animations + 1);
    profile.animProps = "work";
    G_ResetUnitAnimationPropertiesPrepared(&first, &defaults);
    T_STREQ(G_UnitAnimationProperties(&first), "work");
    T_STREQ(G_UnitAnimationProperties(&second), "alternate");
    /* More than the old direct-mapped capacity must retain all selections. */
    FOR_LOOP(i, 1025) {
        char text[80]; snprintf(text, sizeof(text), "stand case%u", i);
        G_SetUnitAnimation(&first, text);
    }
    animation_selection_visits = animation_words_read = 0;
    FOR_LOOP(i, 1025) {
        char text[80]; snprintf(text, sizeof(text), "stand case%u", i);
        G_SetUnitAnimation(&first, text);
    }
    T_EQ(animation_selection_visits, 0); T_EQ(animation_words_read, 0);
    unitAnimationText_t const *retained = second.animation_props;
    G_FreeModels();
    T_ASSERT(second.animation_props == retained);
    T_STREQ(G_UnitAnimationProperties(&second), "alternate");
    /* Re-registering the model resolves against the replacement resource. */
    g_models[index].animations = gi.MemAlloc(sizeof(values));
    memcpy(g_models[index].animations, values, sizeof(values));
    g_models[index].animations[1].interval[0] = 713;
    g_models[index].num_animations = sizeof(values) / sizeof(*values);
    g_models[index].loaded = true;
    G_SetUnitAnimation(&second, G_UnitAnimationRequest(&second));
    T_EQ(second.animation->interval[0], 713);
    G_FreeModels();
}

TEST(wc3_model, compiled_variant_families_preserve_rng_and_authored_order) {
    uint32_t const index=G_MAX_MODELS-1;
    animation_t values[]={
        {.name="Walk",.syncpoint=1}, {.name="Stand",.syncpoint=1},
        {.name="Walk Foo Alternate",.syncpoint=2}, {.name="Walk 2",.syncpoint=1},
        {.name="Walk Alternate Foo 3",.syncpoint=2}, {.name="Walk Foo Alternate 4",.syncpoint=3},
        {.name="Walk Foo",.syncpoint=2}, {.name="Walk AlternateEx",.syncpoint=1},
        {.name="Walk 3",.syncpoint=1}, {.name="Walk Foo Foo Alternate 5",.syncpoint=2}
    };
    cstring_t requests[]={"walk","WALK,foo","walk alternateex","missing","walk foo alternate"};
    cstring_t properties[]={"","alternate","foo,alternate","alternateex"};
    G_FreeModels();
    g_cmodel_t *model=g_models+index;
    model->animations=gi.MemAlloc(sizeof(values));memcpy(model->animations,values,sizeof(values));
    model->num_animations=sizeof(values)/sizeof(*values);model->loaded=true;
    FOR_LOOP(r,sizeof(requests)/sizeof(*requests)) FOR_LOOP(p,sizeof(properties)/sizeof(*properties))
        FOR_LOOP(seed,32) {
            srand(seed);
            animation_t const *expected=G_SelectAnimationVariantForProperties(model->animations,
                model->num_animations,requests[r],properties[p],true);
            int next=rand();srand(seed);
            T_ASSERT(AnimationVariantForProperties(index,requests[r],properties[p],true)==expected);
            T_EQ(rand(),next);
        }
    edict_t unit={.s.model=index};
    G_SetUnitAnimation(&unit,"walk");animation_words_read=0;
    FOR_LOOP(i,128)G_SetUnitAnimation(&unit,"walk");
    T_EQ(animation_words_read, 0); /* The immutable request already owns its primary family. */
    G_FreeModels();
    T_NULL(model->variants);T_NULL(model->variant_indices);
}

TEST(wc3_model, repeated_model_animation_selection_reuses_decoded_properties) {
    uint32_t const index = G_MAX_MODELS - 1;
    animation_t values[] = {{ .name="Stand" }, { .name="Stand Alternate" }, { .name="Death" }};
    char request[32]="stand", properties[32]="alternate";
    G_FreeModels();
    g_models[index].animations=gi.MemAlloc(sizeof(values));
    memcpy(g_models[index].animations,values,sizeof(values));
    g_models[index].num_animations=sizeof(values)/sizeof(*values);
    g_models[index].loaded=true;
    animation_selection_visits=0;
    animation_t const *alternate=G_GetAnimationForProperties(index,request,properties);
    T_ASSERT(alternate==g_models[index].animations+1);
    for(uint32_t i=0;i<1024;i++) T_ASSERT(G_GetAnimationForProperties(index,request,properties)==alternate);
    T_EQ(animation_selection_visits,1);
    properties[0]='\0';
    T_ASSERT(G_GetAnimationForProperties(index,request,properties)==g_models[index].animations);
    strlcpy(request,"unavailable",sizeof(request));
    T_ASSERT(!G_GetAnimationForProperties(index,request,properties));
    for(uint32_t i=0;i<1024;i++) T_ASSERT(!G_GetAnimationForProperties(index,request,properties));
    T_EQ(animation_selection_visits,3);
    G_FreeModels();
    g_models[index].animations=gi.MemAlloc(sizeof(values));
    memcpy(g_models[index].animations,values,sizeof(values));
    strlcpy(g_models[index].animations[0].name,"Birth",sizeof(values[0].name));
    g_models[index].num_animations=sizeof(values)/sizeof(*values);g_models[index].loaded=true;
    T_ASSERT(G_GetAnimationForProperties(index,"stand",NULL)==g_models[index].animations+1);
    T_EQ(animation_selection_visits,4);
    G_FreeModels();
}
#endif
