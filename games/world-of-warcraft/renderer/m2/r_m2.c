#include "renderer/r_local.h"
#include "renderer/r_emit.h"
#include "r_m2_utils.h"
#include <stdlib.h>
#include <strings.h>

#define M2_MAX_BONES_PER_BATCH 128
#define M2_CHARACTER_TEXTURE_NONE 0xff

enum {
    M2_CHAR_TEX_UPPER_ARM,
    M2_CHAR_TEX_LOWER_ARM,
    M2_CHAR_TEX_HAND,
    M2_CHAR_TEX_UPPER_TORSO,
    M2_CHAR_TEX_LOWER_TORSO,
    M2_CHAR_TEX_UPPER_LEG,
    M2_CHAR_TEX_LOWER_LEG,
    M2_CHAR_TEX_FOOT,
    M2_CHAR_TEX_CAPE,       /* cape texture from ItemDisplayInfo.dbc field 3 */
    M2_CHAR_TEX_COUNT
};

typedef struct m2ModelBatch_s {
	LPBUFFER buffer;
	LPTEXTURE texture;
	LPTEXTURE character_texture;
	DWORD character_texture_key;
	DWORD num_vertices;
	DWORD texture_type;
	WORD bone_count;
	WORD bone_combo_index;
	WORD section_id;
	WORD geoset_index;
	BYTE alphamode;
	BYTE character_texture_slot;
	BOOL character_texture_loaded;
	struct m2ModelBatch_s *next;
} m2ModelBatch_t;

#define M2_NUM_GEOSET_GROUPS 16
#define M2_CHAR_FLAG_HELM 0x100u

typedef struct {
    LPCSTR texture[M2_CHAR_TEX_COUNT];
    DWORD geoset[M2_NUM_GEOSET_GROUPS];
    DWORD flags;
} m2CharacterOutfit_t;

enum { M2_COMPOSITE_CACHE_SIZE = 4 };
typedef struct {
    LPTEXTURE texture;
    DWORD key;
} m2CompositeCacheEntry_t;

/* Ribbon edge state — persisted across frames so trails grow/shrink properly.
   Each emitter produces a ring buffer of edges at the animated spine position.
   Pattern derived from WoWee's M2Instance::RibbonEdge + updateRibbons. */
#define MAX_RIBBON_EDGES 64
typedef struct {
    VECTOR3 world_pos;
    VECTOR3 color;
    float alpha, height_above, height_below, age;
} m2RibbonEdge_t;

typedef struct {
    m2RibbonEdge_t edges[MAX_RIBBON_EDGES];
    int head;       /* ring-buffer write index */
    int count;      /* active edges, capped at MAX_RIBBON_EDGES */
    float acc;      /* edge emission accumulator (rate * dt) */
} m2RibbonEmitter_t;

struct m2Model_s {
    m2ModelBatch_t *batches;
    DWORD num_batches;
    PATHSTR filename;
    BOX3 bounds;
    BOX3 geometry_bounds;
    BOOL has_geometry_bounds;
	BOOL character_model;
	BYTE *material_blend_modes;
	DWORD num_materials;
	m2CompositeCacheEntry_t composite_cache[M2_COMPOSITE_CACHE_SIZE];
    DWORD composite_cache_lru;
    m2CharacterOutfit_t character_outfit;
    DWORD character_outfit_appearance;
    DWORD character_outfit_equipment;
    BOOL character_outfit_resolved;
    BOOL character_outfit_valid;
    BYTE *data;
    DWORD data_size;
    m2Header_t *header;
    m2FormatDef_t const *format;
    BYTE *bones;
    BYTE *sequences;
    WORD *bone_lookup_table;
    m2Array_t global_loops;
    DWORD bone_count;
    DWORD sequence_count;
    DWORD bone_lookup_count;
    m2Array_t attachments;
    m2Array_t attachment_lookup;
    BYTE *cameras;
    DWORD camera_count;
    m2Array_t textures;
    m2Array_t texture_lookup_table;
    m2Array_t ribbons;
    m2Array_t particles;
    MATRIX4 *bone_matrices;
    FLOAT *emitter_accumulators;    /* per-particle-emitter accumulator (rate*dt), avoids time-anchoring */
    m2RibbonEmitter_t *ribbon_emitters; /* per-ribbon-emitter persistent edge state */
};

typedef struct {
    m2Array_t vertices;
    m2Array_t textures;
    m2Array_t texture_lookup_table;
    m2Box_t bounding_box;
} m2GeometryInfo_t;

static LPSHADER m2_shader;

static LPSHADER M2_Shader(void) {
    if (!m2_shader) {
        m2_shader = R_ModelShader();
    }
    return m2_shader ? m2_shader : tr.shader[SHADER_DEFAULT];
}

static void M2_LogFallback(LPCSTR modelFilename, LPCSTR reason) {
    static DWORD fallback_count;

    fallback_count++;
    if (fallback_count <= 64 || (fallback_count % 100) == 0) {
        fprintf(stderr,
                "M2 fallback: %s model=%s count=%u\n",
                reason ? reason : "unknown",
                modelFilename ? modelFilename : "<null>",
                fallback_count);
    }
}

static m2Model_t *M2_CreateFallbackModel(LPCSTR modelFilename, LPCSTR reason) {
    static VERTEX vertices[12];
    static BOOL initialized;
    m2Model_t *model;
    m2ModelBatch_t *batch;
    COLOR32 color = { 90, 230, 130, 255 };

    M2_LogFallback(modelFilename, reason);

    if (!initialized) {
        VECTOR3 base[4] = {
            { -14.0f, -14.0f, 0.0f },
            {  14.0f, -14.0f, 0.0f },
            {  14.0f,  14.0f, 0.0f },
            { -14.0f,  14.0f, 0.0f },
        };
        VECTOR3 top = { 0.0f, 0.0f, 42.0f };
        int tri[12] = { 0, 1, 2, 0, 2, 3, 0, 1, 4, 2, 3, 4 };
        VECTOR3 points[5];
        memcpy(points, base, sizeof(base));
        points[4] = top;
        FOR_LOOP(i, 12) {
            memset(&vertices[i], 0, sizeof(vertices[i]));
            vertices[i].position = points[tri[i]];
            vertices[i].normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
            vertices[i].texcoord = (VECTOR2){ 0.0f, 0.0f };
            vertices[i].color = color;
        }
        initialized = true;
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    model->bounds = (BOX3){
        .min = { -14.0f, -14.0f, 0.0f },
        .max = { 14.0f, 14.0f, 42.0f },
    };
    model->geometry_bounds = model->bounds;
    model->has_geometry_bounds = true;
    batch = ri.MemAlloc(sizeof(*batch));
    memset(batch, 0, sizeof(*batch));
    batch->buffer = R_MakeVertexArrayObject(vertices, 12);
    batch->texture = tr.texture[TEX_WHITE];
    batch->num_vertices = 12;
    model->batches = batch;
    model->num_batches = 1;
    return model;
}

typedef struct {
    LPBYTE data;
    DWORD size;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *records_base;
    BYTE const *strings_base;
    BOOL tried;
    BOOL valid;
} m2Dbc_t;

static m2Dbc_t m2_char_start_outfit_dbc;
static m2Dbc_t m2_item_display_info_dbc;
static m2Dbc_t m2_char_sections_dbc;

static DWORD M2_Read32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static BOOL M2_DbcLoad(m2Dbc_t *dbc, LPCSTR filename) {
    int size;

    if (!dbc || !filename) {
        return false;
    }
    if (dbc->tried) {
        return dbc->valid;
    }
    dbc->tried = true;

    size = ri.FS_ReadFile(filename, (void **)&dbc->data);
    if (size <= 20 || !dbc->data || memcmp(dbc->data, "WDBC", 4)) {
        SAFE_DELETE(dbc->data, ri.FS_FreeFile);
        return false;
    }

    dbc->size = (DWORD)size;
    dbc->records = M2_Read32(dbc->data + 4);
    dbc->fields = M2_Read32(dbc->data + 8);
    dbc->record_size = M2_Read32(dbc->data + 12);
    dbc->string_size = M2_Read32(dbc->data + 16);
    if (dbc->fields == 0 || dbc->record_size < sizeof(DWORD) ||
        20 + dbc->records * dbc->record_size + dbc->string_size > dbc->size) {
        SAFE_DELETE(dbc->data, ri.FS_FreeFile);
        memset(dbc, 0, sizeof(*dbc));
        dbc->tried = true;
        return false;
    }

    dbc->records_base = dbc->data + 20;
    dbc->strings_base = dbc->records_base + dbc->records * dbc->record_size;
    dbc->valid = true;
    return true;
}

static void M2_DbcShutdown(m2Dbc_t *dbc) {
    if (!dbc) {
        return;
    }
    SAFE_DELETE(dbc->data, ri.FS_FreeFile);
    memset(dbc, 0, sizeof(*dbc));
}

static DWORD M2_DbcField(m2Dbc_t const *dbc, BYTE const *record, DWORD field) {
    if (!dbc || !record || field >= dbc->fields || field * sizeof(DWORD) + sizeof(DWORD) > dbc->record_size) {
        return 0;
    }
    return M2_Read32(record + field * sizeof(DWORD));
}

static LPCSTR M2_DbcString(m2Dbc_t const *dbc, DWORD offset) {
    if (!dbc || !dbc->valid || offset == 0 || offset >= dbc->string_size) {
        return NULL;
    }
    return (LPCSTR)(dbc->strings_base + offset);
}

static BYTE const *M2_DbcFindID(m2Dbc_t *dbc, LPCSTR filename, DWORD wanted_id) {
    if (!M2_DbcLoad(dbc, filename)) {
        return NULL;
    }
    FOR_LOOP(i, dbc->records) {
        BYTE const *record = dbc->records_base + i * dbc->record_size;
        if (M2_DbcField(dbc, record, 0) == wanted_id) {
            return record;
        }
    }
    return NULL;
}

static BOOL M2_CharacterRaceGender(LPCSTR model_path, DWORD *race_id, DWORD *gender_id) {
    LPCSTR character;
    LPCSTR race;
    LPCSTR gender;
    char race_buf[64];
    char gender_buf[64];
    size_t len;

    if (!model_path || !race_id || !gender_id) {
        return false;
    }
    character = strcasestr(model_path, "Character\\");
    if (!character) {
        character = strcasestr(model_path, "Character/");
    }
    if (!character) {
        return false;
    }
    race = character + strlen("Character\\");
    gender = strpbrk(race, "\\/");
    if (!gender) {
        return false;
    }
    len = (size_t)(gender - race);
    if (len == 0 || len >= sizeof(race_buf)) {
        return false;
    }
    memcpy(race_buf, race, len);
    race_buf[len] = '\0';

    gender++;
    len = strcspn(gender, "\\/.");
    if (len == 0 || len >= sizeof(gender_buf)) {
        return false;
    }
    memcpy(gender_buf, gender, len);
    gender_buf[len] = '\0';

    if (!strcasecmp(race_buf, "Human")) *race_id = 1;
    else if (!strcasecmp(race_buf, "Orc")) *race_id = 2;
    else if (!strcasecmp(race_buf, "Dwarf")) *race_id = 3;
    else if (!strcasecmp(race_buf, "NightElf")) *race_id = 4;
    else if (!strcasecmp(race_buf, "Scourge") || !strcasecmp(race_buf, "Undead")) *race_id = 5;
    else if (!strcasecmp(race_buf, "Tauren")) *race_id = 6;
    else if (!strcasecmp(race_buf, "Gnome")) *race_id = 7;
    else if (!strcasecmp(race_buf, "Troll")) *race_id = 8;
    else if (!strcasecmp(race_buf, "BloodElf")) *race_id = 10;
    else if (!strcasecmp(race_buf, "Draenei")) *race_id = 11;
    else return false;

    if (!strcasecmp(gender_buf, "Male")) *gender_id = 0;
    else if (!strcasecmp(gender_buf, "Female")) *gender_id = 1;
    else return false;
    return true;
}

static DWORD M2_ItemDisplayInfoTextureBase(m2Dbc_t const *dbc) {
    if (!dbc) {
        return 0;
    }
    if (dbc->fields >= 25) {
        return 15;
    }
    if (dbc->fields >= 22) {
        return 14;
    }
    return 0;
}

static DWORD M2_ItemDisplayInfoGeosetBase(m2Dbc_t const *dbc) {
    if (!dbc) {
        return 0;
    }
    return dbc->fields >= 25 ? 7 : 6;
}

static DWORD M2_ItemDisplayInfoFlagsField(m2Dbc_t const *dbc) {
    if (!dbc) {
        return 0;
    }
    return dbc->fields >= 25 ? 10 : 9;
}

/* ItemDisplayInfo.dbc field 3 = LeftModelTexture (cape texture stem).
 * Classic (23-field) and TBC/Wrath (25-field) layouts both use field 3. */
static DWORD M2_ItemDisplayInfoCapeTextureField(m2Dbc_t const *dbc) {
    (void)dbc;
    return 3;
}

enum {
    M2_SLOT_NONE,
    M2_SLOT_HEAD,
    M2_SLOT_SHOULDERS,
    M2_SLOT_CHEST,
    M2_SLOT_SHIRT,
    M2_SLOT_BELT,
    M2_SLOT_LEGS,
    M2_SLOT_BOOTS,
    M2_SLOT_GLOVES,
    M2_SLOT_TABARD,
    M2_SLOT_CAPE,
    M2_SLOT_COUNT
};

// Maps (slot, geosetFieldIndex) → M2 section group number.
// geosetFieldIndex 0/1/2 correspond to geosetGroup[0]/[1]/[2] from ItemDisplayInfo.dbc.
// Group 0 means "no group mapping" for that field.
static DWORD const slot_geoset_group_map[M2_SLOT_COUNT][3] = {
    /* NONE */      { 0, 0, 0 },
    /* HEAD */      { 0, 0, 0 },
    /* SHOULDERS */ { 0, 0, 0 },
    /* CHEST */     { 8, 0, 12 },
    /* SHIRT */     { 8, 0, 0 },
    /* BELT */      { 0, 0, 0 },
    /* LEGS */      { 0, 9, 13 },
    /* BOOTS */     { 5, 0, 0 },
    /* GLOVES */    { 4, 0, 0 },
    /* TABARD */    { 0, 0, 0 },
    /* CAPE */      { 15, 0, 0 },
};

static void M2_AddDisplayInfoToOutfit(m2CharacterOutfit_t *outfit, DWORD display_id, DWORD slot) {
    BYTE const *record;
    DWORD texture_base;
    DWORD geoset_base;
    DWORD flags_field;

    if (!outfit || display_id == 0 || display_id == 0xffffffffu) {
        return;
    }
    record = M2_DbcFindID(&m2_item_display_info_dbc,
                          "DBFilesClient\\ItemDisplayInfo.dbc",
                          display_id);
    if (!record) {
        return;
    }

    texture_base = M2_ItemDisplayInfoTextureBase(&m2_item_display_info_dbc);
    geoset_base = M2_ItemDisplayInfoGeosetBase(&m2_item_display_info_dbc);
    flags_field = M2_ItemDisplayInfoFlagsField(&m2_item_display_info_dbc);

    FOR_LOOP(i, 3) {
        DWORD group = (slot < M2_SLOT_COUNT) ? slot_geoset_group_map[slot][i] : 0;
        if (group) {
            DWORD geoset_val = M2_DbcField(&m2_item_display_info_dbc, record, geoset_base + i);
            if (geoset_val) {
                outfit->geoset[group] = geoset_val;
            }
        }
    }
    {
        DWORD item_flags = M2_DbcField(&m2_item_display_info_dbc, record, flags_field);
        if (slot == M2_SLOT_HEAD && item_flags == 1) {
            outfit->flags |= M2_CHAR_FLAG_HELM;
        }
        outfit->flags |= item_flags;
    }
    FOR_LOOP(i, M2_CHAR_TEX_COUNT) {
        LPCSTR texture = M2_DbcString(&m2_item_display_info_dbc,
                                      M2_DbcField(&m2_item_display_info_dbc, record, texture_base + i));
        if (texture && *texture) {
            outfit->texture[i] = texture;
        }
    }
    /* Cape texture: ItemDisplayInfo.dbc field 3 = LeftModelTexture.
     * WoWee reads this field for slot 10 (cape) and resolves gender variants. */
    if (slot == M2_SLOT_CAPE) {
        DWORD cape_field = M2_ItemDisplayInfoCapeTextureField(&m2_item_display_info_dbc);
        LPCSTR cape_tex = M2_DbcString(&m2_item_display_info_dbc,
                                       M2_DbcField(&m2_item_display_info_dbc, record, cape_field));
        if (cape_tex && *cape_tex) {
            outfit->texture[M2_CHAR_TEX_CAPE] = cape_tex;
        }
    }
}

static void M2_AddDisplayInfoListToOutfit(m2CharacterOutfit_t *outfit,
                                           DWORD const *display_ids,
                                           DWORD display_count,
                                           DWORD slot) {
    FOR_LOOP(i, display_count) {
        M2_AddDisplayInfoToOutfit(outfit, display_ids[i], slot);
    }
}

typedef struct {
    DWORD display_ids[4];
} m2EquipmentItem_t;

typedef struct {
    DWORD race_id;
    DWORD gender_id;
    m2EquipmentItem_t items[256];
} m2EquipmentSlotItems_t;

static m2EquipmentItem_t const *M2_EquipmentSlotItem(m2EquipmentSlotItems_t const *lists,
                                                    DWORD list_count,
                                                    DWORD race_id,
                                                    DWORD gender_id,
                                                    BYTE item_index) {
    FOR_LOOP(i, list_count) {
        if (lists[i].race_id == race_id && lists[i].gender_id == gender_id) {
            return &lists[i].items[item_index];
        }
    }
    return NULL;
}

static void M2_AddEquipmentItemToOutfit(m2CharacterOutfit_t *outfit,
                                        m2EquipmentSlotItems_t const *lists,
                                        DWORD list_count,
                                        DWORD race_id,
                                        DWORD gender_id,
                                        BYTE item_index,
                                        DWORD slot) {
    m2EquipmentItem_t const *item = M2_EquipmentSlotItem(lists, list_count, race_id, gender_id, item_index);

    if (!outfit || !item) {
        return;
    }
    M2_AddDisplayInfoListToOutfit(outfit,
                                  item->display_ids,
                                  sizeof(item->display_ids) / sizeof(item->display_ids[0]),
                                  slot);
}

static void M2_ApplyEquipmentItems(m2CharacterOutfit_t *outfit,
                                   DWORD race_id,
                                   DWORD gender_id,
                                   DWORD equipment) {
    static m2EquipmentSlotItems_t const upper_body_items[] = {
        { 2, 0, { [1] = { { 27274, 0, 0, 0 } } } }
    };
    static m2EquipmentSlotItems_t const lower_body_items[] = {
        { 2, 0, { [1] = { { 27275, 0, 0, 0 } } } }
    };
    static m2EquipmentSlotItems_t const hand_items[] = {
        { 2, 0, { [1] = { { 27271, 0, 0, 0 } } } }
    };
    static m2EquipmentSlotItems_t const foot_items[] = {
        { 2, 0, { [1] = { { 27270, 0, 0, 0 } } } }
    };
    wowEquipment_t items = Wow_UnpackEquipment(equipment);

    M2_AddEquipmentItemToOutfit(outfit, upper_body_items,
                                sizeof(upper_body_items) / sizeof(upper_body_items[0]),
                                race_id, gender_id, items.upperBodyItem, M2_SLOT_CHEST);
    M2_AddEquipmentItemToOutfit(outfit, lower_body_items,
                                sizeof(lower_body_items) / sizeof(lower_body_items[0]),
                                race_id, gender_id, items.lowerBodyItem, M2_SLOT_LEGS);
    M2_AddEquipmentItemToOutfit(outfit, hand_items,
                                sizeof(hand_items) / sizeof(hand_items[0]),
                                race_id, gender_id, items.handItem, M2_SLOT_GLOVES);
    M2_AddEquipmentItemToOutfit(outfit, foot_items,
                                sizeof(foot_items) / sizeof(foot_items[0]),
                                race_id, gender_id, items.footItem, M2_SLOT_BOOTS);
}

static BOOL M2_CharacterStartOutfit(LPCSTR model_path,
                                    DWORD appearance,
                                    m2CharacterOutfit_t *outfit) {
    DWORD race_id;
    DWORD gender_id;
    DWORD class_id;
    wowAppearance_t unpacked;

    if (!outfit || !M2_CharacterRaceGender(model_path, &race_id, &gender_id) ||
        !M2_DbcLoad(&m2_char_start_outfit_dbc, "DBFilesClient\\CharStartOutfit.dbc")) {
        return false;
    }

    memset(outfit, 0, sizeof(*outfit));
    unpacked = Wow_UnpackAppearance(appearance);
    class_id = unpacked.classID ? unpacked.classID : 1;

    FOR_LOOP(record_index, m2_char_start_outfit_dbc.records) {
        BYTE const *record = m2_char_start_outfit_dbc.records_base + record_index * m2_char_start_outfit_dbc.record_size;
        DWORD race_class_gender = M2_DbcField(&m2_char_start_outfit_dbc, record, 1);
        DWORD record_race = race_class_gender & 0xff;
        DWORD record_class = (race_class_gender >> 8) & 0xff;
        DWORD record_gender = (race_class_gender >> 16) & 0xff;

        if (record_race != race_id || record_class != class_id || record_gender != gender_id) {
            continue;
        }
        // CharStartOutfit.dbc fields 14-25 map to equipment slots in order:
        // head, shoulders, chest, shirt, belt, legs, boots, gloves, tabard, cape, unused, unused
        static DWORD const start_outfit_slot_map[12] = {
            M2_SLOT_HEAD, M2_SLOT_SHOULDERS, M2_SLOT_CHEST, M2_SLOT_SHIRT,
            M2_SLOT_BELT, M2_SLOT_LEGS, M2_SLOT_BOOTS, M2_SLOT_GLOVES,
            M2_SLOT_TABARD, M2_SLOT_CAPE, M2_SLOT_NONE, M2_SLOT_NONE
        };
        FOR_LOOP(i, 12) {
            M2_AddDisplayInfoToOutfit(outfit, M2_DbcField(&m2_char_start_outfit_dbc, record, 14 + i),
                                      start_outfit_slot_map[i]);
        }
        return true;
    }
    return false;
}

static m2CharacterOutfit_t const *M2_CharacterOutfitForEntity(m2Model_t const *model,
                                                              renderEntity_t const *entity) {
    m2Model_t *mutable_model;
    DWORD race_id;
    DWORD gender_id;

    if (!model || !entity || !model->character_model) {
        return NULL;
    }
    if (!M2_CharacterRaceGender(model->filename, &race_id, &gender_id)) {
        return NULL;
    }

    mutable_model = (m2Model_t *)model;
    if (mutable_model->character_outfit_resolved &&
        mutable_model->character_outfit_appearance == entity->appearance &&
        mutable_model->character_outfit_equipment == entity->equipment) {
        return mutable_model->character_outfit_valid ? &mutable_model->character_outfit : NULL;
    }

    mutable_model->character_outfit_appearance = entity->appearance;
    mutable_model->character_outfit_equipment = entity->equipment;
    mutable_model->character_outfit_resolved = true;
    mutable_model->character_outfit_valid = false;
    if (!M2_CharacterStartOutfit(model->filename, entity->appearance, &mutable_model->character_outfit)) {
        return NULL;
    }
    M2_ApplyEquipmentItems(&mutable_model->character_outfit, race_id, gender_id, entity->equipment);
    mutable_model->character_outfit_valid = true;
    return &mutable_model->character_outfit;
}

static BYTE M2_CharacterTextureSlotForSection(WORD section_id) {
    switch (section_id) {
        case 401:
        case 402:
        case 403:
        case 404:
            return M2_CHAR_TEX_HAND;
        case 501:
        case 502:
        case 503:
        case 504:
            return M2_CHAR_TEX_FOOT;
        case 802:
        case 803:
            return M2_CHAR_TEX_LOWER_ARM;
        case 902:
        case 903:
            return M2_CHAR_TEX_LOWER_LEG;
        case 1002:
            return M2_CHAR_TEX_UPPER_TORSO;
        case 1102:
        case 1202:
            return M2_CHAR_TEX_LOWER_TORSO;
        case 1302:
            return M2_CHAR_TEX_UPPER_LEG;
        default:
            return M2_CHARACTER_TEXTURE_NONE;
    }
}

static BOOL M2_CharacterVariationTexturePath(LPCSTR model_path,
                                             DWORD section_index,
                                             DWORD variation_index,
                                             DWORD color_index,
                                             DWORD texture_index,
                                             LPSTR out,
                                             DWORD out_size) {
    /* Classic CharSections.dbc: 10 fields, textures not contiguous.
       0=ID 1=race 2=sex 3=section 4=tex[0] 5=variation
       6=tex[1] 7=tex[2] 8=tex[3] 9=color */
    DWORD race_id, gender_id;

    if (!out || out_size == 0 ||
        !M2_CharacterRaceGender(model_path, &race_id, &gender_id) ||
        !M2_DbcLoad(&m2_char_sections_dbc, "DBFilesClient\\CharSections.dbc")) {
        return false;
    }

    FOR_LOOP(i, m2_char_sections_dbc.records) {
        BYTE const *record = m2_char_sections_dbc.records_base + i * m2_char_sections_dbc.record_size;
        LPCSTR texture;

        if (M2_DbcField(&m2_char_sections_dbc, record, 1) != race_id ||
            M2_DbcField(&m2_char_sections_dbc, record, 2) != gender_id ||
            M2_DbcField(&m2_char_sections_dbc, record, 3) != section_index ||
            M2_DbcField(&m2_char_sections_dbc, record, 5) != variation_index ||
            M2_DbcField(&m2_char_sections_dbc, record, 9) != color_index) {
            continue;
        }
        if (texture_index >= 3)
            return false;
        texture = M2_DbcString(&m2_char_sections_dbc, M2_DbcField(&m2_char_sections_dbc, record, 6 + texture_index));
        if (!texture || !*texture)
            continue;
        snprintf(out, out_size, "%s", texture);
        return true;
    }
    return false;
}

static BOOL M2_CharacterTexturePathForType(LPCSTR model_path,
                                           DWORD appearance,
                                           DWORD texture_type,
                                           LPSTR out,
                                           DWORD out_size) {
    wowAppearance_t unpacked = Wow_UnpackAppearance(appearance);
    switch (texture_type) {
        case 1:
            return M2_CharacterVariationTexturePath(model_path, 0, 0, unpacked.skinColorID, 0, out, out_size);
        case 2:
            return M2_CharacterVariationTexturePath(model_path, 4, 0, unpacked.skinColorID, 0, out, out_size);
        case 6:
            return M2_CharacterVariationTexturePath(model_path, 3, unpacked.hairStyleID, unpacked.hairColorID, 0, out, out_size);
        case 8:
            return M2_CharacterVariationTexturePath(model_path, 0, 0, unpacked.skinColorID, 1, out, out_size);
        default:
            return false;
    }
}

static BOOL M2_TextureExists(LPCSTR path) {
    LPBYTE data = NULL;
    int size;

    if (!path || !*path) {
        return false;
    }
    size = ri.FS_ReadFile(path, (void **)&data);
    if (size <= 0 || !data) {
        if (data) {
            ri.FS_FreeFile(data);
        }
        return false;
    }
    ri.FS_FreeFile(data);
    return true;
}

static BOOL M2_CharacterComponentTexturePath(LPCSTR stem,
                                             BYTE slot,
                                             LPCSTR model_path,
                                             LPSTR out,
                                             DWORD out_size) {
    static LPCSTR const folders[M2_CHAR_TEX_COUNT] = {
        "ArmUpperTexture",
        "ArmLowerTexture",
        "HandTexture",
        "TorsoUpperTexture",
        "TorsoLowerTexture",
        "LegUpperTexture",
        "LegLowerTexture",
        "FootTexture"
    };
    DWORD race_id;
    DWORD gender_id;
    LPCSTR gender_suffix;
    PATHSTR candidate;

    if (!stem || !*stem || slot >= M2_CHAR_TEX_COUNT || !out || out_size == 0) {
        return false;
    }
    gender_suffix = M2_CharacterRaceGender(model_path, &race_id, &gender_id) && gender_id ? "F" : "M";

    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_%s.blp",
             folders[slot], stem, gender_suffix);
    if (M2_TextureExists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_U.blp",
             folders[slot], stem);
    if (M2_TextureExists(candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(out, out_size, "Item\\TextureComponents\\%s\\%s_%s.blp",
             folders[slot], stem, gender_suffix);
    return true;
}

static BOOL M2_TexturePixels(LPTEXTURE texture, LPCOLOR32 *pixels) {
    if (!texture || !texture->texid || !texture->width || !texture->height || !pixels) {
        return false;
    }
    *pixels = ri.MemAlloc(sizeof(COLOR32) * texture->width * texture->height);
    if (!*pixels) {
        return false;
    }
    R_Call(glBindTexture, GL_TEXTURE_2D, texture->texid);
#if __linux__
    R_Call(glGetTexImage, GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, *pixels);
#else
    R_Call(glGetTexImage, GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, *pixels);
#endif
    return true;
}

static void M2_BlendPixel(LPCOLOR32 dst, COLOR32 src) {
    DWORD inv;

    if (src.a == 0) {
        return;
    }
    if (src.a >= 250) {
        *dst = src;
        return;
    }
    inv = 255 - src.a;
    dst->b = (BYTE)((src.b * src.a + dst->b * inv) / 255);
    dst->g = (BYTE)((src.g * src.a + dst->g * inv) / 255);
    dst->r = (BYTE)((src.r * src.a + dst->r * inv) / 255);
    dst->a = (BYTE)MIN(255, src.a + (dst->a * inv) / 255);
}

static void M2_PasteComponent(LPCOLOR32 dst,
                              DWORD dst_width,
                              DWORD dst_height,
                              LPCOLOR32 src,
                              DWORD src_width,
                              DWORD src_height,
                              DWORD x,
                              DWORD y,
                              DWORD w,
                              DWORD h) {
    if (!dst || !src || !dst_width || !dst_height || !src_width || !src_height ||
        x >= dst_width || y >= dst_height) {
        return;
    }
    w = MIN(w, dst_width - x);
    h = MIN(h, dst_height - y);
    FOR_LOOP(row, h) {
        DWORD src_y = row * src_height / h;
        FOR_LOOP(col, w) {
            DWORD src_x = col * src_width / w;
            M2_BlendPixel(&dst[(y + row) * dst_width + x + col],
                          src[src_y * src_width + src_x]);
        }
    }
}

static void M2_PasteOutfitComponent(LPCOLOR32 pixels,
                                    DWORD width,
                                    DWORD height,
                                    LPCSTR model_path,
                                    m2CharacterOutfit_t const *outfit,
                                    BYTE slot) {
    enum { character_component_atlas_size = 512 };
    static DWORD const rects[M2_CHAR_TEX_COUNT][4] = {
        { 0,   0,   256, 128 },
        { 0,   128, 256, 128 },
        { 0,   256, 256, 64  },
        { 256, 0,   256, 128 },
        { 256, 128, 256, 64  },
        { 256, 192, 256, 128 },
        { 256, 320, 256, 128 },
        { 256, 448, 256, 64  }
    };
    PATHSTR texture_path;
    LPTEXTURE texture;
    LPCOLOR32 component_pixels = NULL;
    DWORD dst_x;
    DWORD dst_y;
    DWORD dst_w;
    DWORD dst_h;

    if (!outfit || slot >= M2_CHAR_TEX_COUNT ||
        !M2_CharacterComponentTexturePath(outfit->texture[slot], slot, model_path, texture_path, sizeof(texture_path))) {
        return;
    }
    texture = R_LoadTexture(texture_path);
    if (!texture || !M2_TexturePixels(texture, &component_pixels)) {
        SAFE_DELETE(texture, R_ReleaseTexture);
        return;
    }
    dst_x = rects[slot][0] * width / character_component_atlas_size;
    dst_y = rects[slot][1] * height / character_component_atlas_size;
    dst_w = MAX(1u, rects[slot][2] * width / character_component_atlas_size);
    dst_h = MAX(1u, rects[slot][3] * height / character_component_atlas_size);
    M2_PasteComponent(pixels,
                      width,
                      height,
                      component_pixels,
                      texture->width,
                      texture->height,
                      dst_x,
                      dst_y,
                      dst_w,
                      dst_h);
    ri.MemFree(component_pixels);
    R_ReleaseTexture(texture);
}

/* Paste a CharSections variation (e.g. face or facial hair) into the head
   regions of the composite.  section_id: 1=face, 2=facial_hair.
   HEAD_UPPER = (0,320,256,64), HEAD_LOWER = (0,384,256,128) in the 512 atlas. */
static void M2_PasteHeadVariation(LPCOLOR32 pixels,
                                  DWORD width,
                                  DWORD height,
                                  LPCSTR model_path,
                                  DWORD section_id,
                                  DWORD variation_index,
                                  DWORD color_index) {
    enum { atlas = 512 };
    static DWORD const head_rects[2][4] = {
        { 0, 320, 256, 64  }, /* HEAD_UPPER, texture_index 1 */
        { 0, 384, 256, 128 }, /* HEAD_LOWER, texture_index 0 */
    };
    /* upper first (tex 1), then lower (tex 0) */
    static DWORD const tex_indices[2] = { 1, 0 };

    for (int i = 0; i < 2; i++) {
        PATHSTR path;
        LPTEXTURE tex;
        LPCOLOR32 src = NULL;
        DWORD dx, dy, dw, dh;

        if (!M2_CharacterVariationTexturePath(model_path, section_id,
                                              variation_index, color_index,
                                              tex_indices[i],
                                              path, sizeof(path))) {
            continue;
        }
        tex = R_LoadTexture(path);
        if (!tex || !M2_TexturePixels(tex, &src)) {
            SAFE_DELETE(tex, R_ReleaseTexture);
            continue;
        }
        dx = head_rects[i][0] * width  / atlas;
        dy = head_rects[i][1] * height / atlas;
        dw = MAX(1u, head_rects[i][2] * width  / atlas);
        dh = MAX(1u, head_rects[i][3] * height / atlas);
        M2_PasteComponent(pixels, width, height, src, tex->width, tex->height,
                          dx, dy, dw, dh);
        ri.MemFree(src);
        R_ReleaseTexture(tex);
    }
}

static BOOL M2_DefaultCharacterTexturePath(LPCSTR model_path,
                                           DWORD texture_type,
                                           LPSTR out,
                                           DWORD out_size) {
    LPCSTR character;
    LPCSTR race;
    LPCSTR gender;
    LPCSTR model_name;
    char race_buf[64];
    char gender_buf[64];
    char model_buf[64];
    size_t len;

    if (!model_path || !out || out_size == 0) {
        return false;
    }
    character = strcasestr(model_path, "Character\\");
    if (!character) {
        character = strcasestr(model_path, "Character/");
    }
    if (!character) {
        return false;
    }
    race = character + strlen("Character\\");
    gender = strpbrk(race, "\\/");
    if (!gender) {
        return false;
    }
    len = (size_t)(gender - race);
    if (len == 0 || len >= sizeof(race_buf)) {
        return false;
    }
    memcpy(race_buf, race, len);
    race_buf[len] = '\0';

    gender++;
    model_name = strpbrk(gender, "\\/");
    if (!model_name) {
        model_name = strrchr(gender, '.');
        if (!model_name) {
            return false;
        }
        len = (size_t)(model_name - gender);
        if (len == 0 || len >= sizeof(gender_buf)) {
            return false;
        }
        memcpy(gender_buf, gender, len);
        gender_buf[len] = '\0';
        model_name = gender;
    } else {
        len = (size_t)(model_name - gender);
        if (len == 0 || len >= sizeof(gender_buf)) {
            return false;
        }
        memcpy(gender_buf, gender, len);
        gender_buf[len] = '\0';
        model_name++;
    }

    len = strcspn(model_name, ".");
    if (len == 0 || len >= sizeof(model_buf)) {
        return false;
    }
    memcpy(model_buf, model_name, len);
    model_buf[len] = '\0';

    switch (texture_type) {
        case 6:
            return M2_CharacterVariationTexturePath(model_path, 3, 0, 0, 0, out, out_size);
        default:
            return false;
    }
}

static BOOL M2_DefaultObjectComponentTexturePath(LPCSTR model_path,
                                                 DWORD texture_type,
                                                 LPSTR out,
                                                 DWORD out_size) {
    LPCSTR filename;
    size_t stem_len;

    if (!model_path || !out || out_size == 0 || texture_type != 2) {
        return false;
    }
    if (!strcasestr(model_path, "Item\\ObjectComponents\\Weapon\\") &&
        !strcasestr(model_path, "Item/ObjectComponents/Weapon/")) {
        return false;
    }

    filename = strrchr(model_path, '\\');
    if (!filename) {
        filename = strrchr(model_path, '/');
    }
    filename = filename ? filename + 1 : model_path;
    stem_len = strcspn(filename, ".");

    if (stem_len == strlen("Axe_1H_Horde_A_01") &&
        !strncasecmp(filename, "Axe_1H_Horde_A_01", stem_len)) {
        snprintf(out, out_size, "Item\\ObjectComponents\\Weapon\\Axe_1H_Horde_A_01Gray.blp");
        return true;
    }
    if (stem_len == 0 || stem_len + strlen("Item\\ObjectComponents\\Weapon\\.blp") + 1 > out_size) {
        return false;
    }

    snprintf(out, out_size, "Item\\ObjectComponents\\Weapon\\%.*s.blp", (int)stem_len, filename);
    return true;
}

static BOOL M2_DefaultCreatureTexturePath(LPCSTR model_path,
                                          DWORD texture_type,
                                          LPSTR out,
                                          DWORD out_size) {
    typedef struct {
        LPCSTR model;
        DWORD texture_type;
        LPCSTR texture;
    } defaultCreatureTexture_t;
    static defaultCreatureTexture_t const defaults[] = {
        { "Creature\\Wolf\\Wolf.mdx",     11, "Creature\\Wolf\\WolfSkinCoyote.blp" },
        { "Creature\\Wolf\\Wolf.m2",      11, "Creature\\Wolf\\WolfSkinCoyote.blp" },
        { "Creature\\Wolf\\Wolf.mdx",     12, "Creature\\Wolf\\WolfSkinCoyoteAlpha.blp" },
        { "Creature\\Wolf\\Wolf.m2",      12, "Creature\\Wolf\\WolfSkinCoyoteAlpha.blp" },
        { "Creature\\Boar\\Boar.mdx",     11, "Creature\\Boar\\BoarSkinIvory.blp" },
        { "Creature\\Boar\\Boar.m2",      11, "Creature\\Boar\\BoarSkinIvory.blp" },
        { "Creature\\Kobold\\Kobold.mdx", 11, "Creature\\Kobold\\koboldskinAlbino.blp" },
        { "Creature\\Kobold\\Kobold.m2",  11, "Creature\\Kobold\\koboldskinAlbino.blp" },
        { "Creature\\Murloc\\Murloc.mdx", 11, "Creature\\Murloc\\SahauginskinBlue.blp" },
        { "Creature\\Murloc\\Murloc.m2",  11, "Creature\\Murloc\\SahauginskinBlue.blp" },
    };

    if (!model_path || !out || out_size == 0) {
        return false;
    }

    FOR_LOOP(i, sizeof(defaults) / sizeof(defaults[0])) {
        if (texture_type == defaults[i].texture_type &&
            !strcasecmp(model_path, defaults[i].model)) {
            snprintf(out, out_size, "%s", defaults[i].texture);
            return true;
        }
    }
    return false;
}

static void *M2_ModelArrayPtr(m2Model_t const *model, m2Array_t array, DWORD elem_size) {
    if (!model || !model->data) {
        return NULL;
    }
    return m2_array_ptr(model->data, model->data_size, array, elem_size);
}

static DWORD M2_SequenceStart(m2Model_t const *model, DWORD sequence_index) {
    if (!model || !model->sequences || sequence_index >= model->sequence_count) {
        return 0;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2SequenceClassic_t const *sequence = (m2SequenceClassic_t const *)(model->sequences + sequence_index * model->format->sequence_stride);
        return sequence->start_timestamp;
    }
    return 0;
}

static DWORD M2_SequenceDuration(m2Model_t const *model, DWORD sequence_index) {
    if (!model || !model->sequences || sequence_index >= model->sequence_count) {
        return 0;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2SequenceClassic_t const *sequence = (m2SequenceClassic_t const *)(model->sequences + sequence_index * model->format->sequence_stride);
        return sequence->end_timestamp > sequence->start_timestamp
            ? sequence->end_timestamp - sequence->start_timestamp
            : 0;
    } else {
        m2SequenceModern_t const *sequence = (m2SequenceModern_t const *)(model->sequences + sequence_index * model->format->sequence_stride);
        return sequence->duration;
    }
}

static DWORD M2_SequenceFlags(m2Model_t const *model, DWORD sequence_index) {
    if (!model || !model->sequences || sequence_index >= model->sequence_count) {
        return 0;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2SequenceClassic_t const *sequence = (m2SequenceClassic_t const *)(model->sequences + sequence_index * model->format->sequence_stride);
        return sequence->flags;
    } else {
        m2SequenceModern_t const *sequence = (m2SequenceModern_t const *)(model->sequences + sequence_index * model->format->sequence_stride);
        return sequence->flags;
    }
}

static WORD M2_SequenceAnimId(m2Model_t const *model, DWORD sequence_index) {
    BYTE const *sequence;

    if (!model || !model->sequences || sequence_index >= model->sequence_count)
        return 0;
    sequence = model->sequences + sequence_index * model->format->sequence_stride;
    if (model->format->format == M2_FORMAT_CLASSIC)
        return ((m2SequenceClassic_t const *)sequence)->animation_id;
    return ((m2SequenceModern_t const *)sequence)->animation_id;
}

/* Lua SetSequence passes Blizzard animation IDs, not raw M2 sequence row indices. */
static BOOL M2_FindSequenceByAnimId(m2Model_t const *model, DWORD anim_id, LPDWORD sequence_index) {
    if (!model || !sequence_index)
        return false;
    FOR_LOOP(i, model->sequence_count) {
        if (M2_SequenceAnimId(model, i) == anim_id) {
            *sequence_index = i;
            return true;
        }
    }
    return false;
}

#define M2_FRAME_SEQUENCE_FLAG  0x80000000u
#define M2_FRAME_SEQUENCE_SHIFT 21
#define M2_FRAME_SEQUENCE_MASK  0x3ffu
#define M2_FRAME_TIME_MASK      0x1fffffu

BOOL M2_SetEntitySequenceFrame(m2Model_t const *model, LPCSTR anim, renderEntity_t *entity) {
    char *end = NULL;
    DWORD anim_id;
    DWORD sequence_index;

    if (!model || !entity)
        return false;
    anim_id = anim && *anim ? (DWORD)strtoul(anim, &end, 10) : 0;
    if (anim && *anim && (!end || *end))
        return false;
    if (!M2_FindSequenceByAnimId(model, anim_id, &sequence_index))
        sequence_index = anim_id;
    if (model->sequence_count > 0 && sequence_index >= model->sequence_count)
        return false;
    entity->frame = M2_FRAME_SEQUENCE_FLAG |
                    ((anim_id & M2_FRAME_SEQUENCE_MASK) << M2_FRAME_SEQUENCE_SHIFT) |
                    (entity->frame & M2_FRAME_TIME_MASK);
    entity->oldframe = M2_FRAME_SEQUENCE_FLAG |
                       ((anim_id & M2_FRAME_SEQUENCE_MASK) << M2_FRAME_SEQUENCE_SHIFT) |
                       (entity->oldframe & M2_FRAME_TIME_MASK);
    return true;
}

typedef struct {
    DWORD sequence_index;
    DWORD sequence_time;
} m2PoseTime_t;

static BOOL M2_FrameToPoseTime(m2Model_t const *model, DWORD frame, m2PoseTime_t *pose) {
    DWORD range_start = 0;

    if (pose) {
        pose->sequence_index = 0;
        pose->sequence_time = frame;
    }
    if (!model || !model->sequences || model->sequence_count == 0 || !pose) {
        return false;
    }

    if (frame & M2_FRAME_SEQUENCE_FLAG) {
        DWORD anim_id = (frame >> M2_FRAME_SEQUENCE_SHIFT) & M2_FRAME_SEQUENCE_MASK;
        DWORD sequence;
        DWORD local_time = frame & M2_FRAME_TIME_MASK;
        DWORD duration;

        if (!M2_FindSequenceByAnimId(model, anim_id, &sequence))
            sequence = anim_id;
        if (sequence >= model->sequence_count)
            sequence = 0;
        duration = M2_SequenceDuration(model, sequence);
        pose->sequence_index = sequence;
        pose->sequence_time = duration
            ? M2_SequenceStart(model, sequence) + (local_time % duration)
            : M2_SequenceStart(model, sequence);
        return true;
    }

    FOR_LOOP(i, model->sequence_count) {
        DWORD duration = M2_SequenceDuration(model, i);
        DWORD range_length = MAX(duration, 1);

        if (frame >= range_start && frame < range_start + range_length) {
            DWORD local_time = frame - range_start;

            pose->sequence_index = i;
            pose->sequence_time = duration
                ? M2_SequenceStart(model, i) + (local_time % duration)
                : M2_SequenceStart(model, i);
            return true;
        }
        range_start += range_length;
    }

    if (model->sequence_count > 0) {
        DWORD duration = M2_SequenceDuration(model, 0);

        pose->sequence_index = 0;
        pose->sequence_time = duration
            ? M2_SequenceStart(model, 0) + (frame % duration)
            : M2_SequenceStart(model, 0);
        return true;
    }
    return false;
}

static DWORD M2_AnimationTime(m2Model_t const *model, renderEntity_t const *entity, DWORD *sequence_index) {
    DWORD frame = entity ? entity->frame : tr.viewDef.time;
    m2PoseTime_t pose;
    m2PoseTime_t old_pose;

    if (sequence_index) {
        *sequence_index = 0;
    }
    if (!M2_FrameToPoseTime(model, frame, &pose)) {
        return frame;
    }

    if (entity &&
        M2_FrameToPoseTime(model, entity->oldframe, &old_pose) &&
        old_pose.sequence_index == pose.sequence_index) {
        DWORD duration = M2_SequenceDuration(model, pose.sequence_index);
        DWORD start_time = M2_SequenceStart(model, pose.sequence_index);
        DWORD old_time = old_pose.sequence_time - start_time;
        DWORD local_time = pose.sequence_time - start_time;
        FLOAT end_time = (FLOAT)local_time;
        FLOAT lerped;

        if (duration > 0 && old_time > local_time && !(M2_SequenceFlags(model, pose.sequence_index) & 0x1)) {
            end_time += (FLOAT)duration;
        }
        lerped = LerpNumber((FLOAT)old_time, end_time, tr.viewDef.lerpfrac);
        if (duration > 0 && lerped >= (FLOAT)duration) {
            lerped -= (FLOAT)duration * floorf(lerped / (FLOAT)duration);
        }
        pose.sequence_time = start_time + (DWORD)MAX(0.0f, lerped);
    }

    if (sequence_index) {
        *sequence_index = pose.sequence_index;
    }
    return pose.sequence_time;
}

static DWORD M2_TrackTime(m2Model_t const *model,
                          m2TrackView_t const *track,
                          DWORD sequence_index,
                          DWORD sequence_time) {
    DWORD const *loops;

    if (!model || !track || !model->header || track->loop_index == 0xFFFF) {
        return sequence_time;
    }

    loops = M2_ModelArrayPtr(model, model->global_loops, sizeof(DWORD));
    if (!loops || track->loop_index >= (WORD)model->global_loops.size || loops[track->loop_index] == 0) {
        return sequence_time;
    }

    (void)sequence_index;
    return tr.viewDef.time % loops[track->loop_index];
}

static BOOL M2_FindTrackKeys(m2Model_t const *model,
                             m2TrackView_t const *track,
                             DWORD sequence_index,
                             DWORD sequence_time,
                             DWORD elem_size,
                             void const **left,
                             void const **right,
                             float *ratio) {
    DWORD const *times;
    BYTE const *keys;
    DWORD count;

    if (!model || !track || !left || !right || !ratio) {
        return false;
    }

    if (track->classic) {
        m2Range_t const *ranges = M2_ModelArrayPtr(model, track->ranges, sizeof(*ranges));
        m2Range_t range;

        /* TBC-era classic particles store tracks without per-animation ranges (ranges.size==0).
           Fall back to reading times and keys as flat global arrays in that case. */
        if (!ranges || track->ranges.size == 0) {
            times = M2_ModelArrayPtr(model, track->sequence_times, sizeof(DWORD));
            keys = M2_ModelArrayPtr(model, track->sequence_keys, elem_size);
            if (!times || !keys) return false;
            count = MIN((DWORD)track->sequence_times.size, (DWORD)track->sequence_keys.size);
            if (count == 0) return false;
        } else {
        if (sequence_index >= (DWORD)track->ranges.size) {
            sequence_index = 0;
        }

        range = ranges[sequence_index];
        if (range.end < range.start) {
            return false;
        }

        times = M2_ModelArrayPtr(model, track->sequence_times, sizeof(DWORD));
        keys = M2_ModelArrayPtr(model, track->sequence_keys, elem_size);
        if (!times || !keys ||
            range.start >= (DWORD)track->sequence_times.size ||
            range.start >= (DWORD)track->sequence_keys.size) {
            return false;
        }

        count = range.end - range.start + 1;
        count = MIN(count, (DWORD)track->sequence_times.size - range.start);
        count = MIN(count, (DWORD)track->sequence_keys.size - range.start);
        times += range.start;
        keys += range.start * elem_size;
        if (count == 0) {
            return false;
        }
        }
    } else {
        m2SequenceTimes_t const *sequence_times = M2_ModelArrayPtr(model, track->sequence_times, sizeof(*sequence_times));
        m2SequenceKeys_t const *sequence_keys = M2_ModelArrayPtr(model, track->sequence_keys, sizeof(*sequence_keys));
        if (!sequence_times || !sequence_keys || track->sequence_times.size == 0 || track->sequence_keys.size == 0) {
            return false;
        }

        if (sequence_index >= (DWORD)track->sequence_times.size || sequence_index >= (DWORD)track->sequence_keys.size) {
            sequence_index = 0;
        }

        times = M2_ModelArrayPtr(model, sequence_times[sequence_index].times, sizeof(DWORD));
        keys = M2_ModelArrayPtr(model, sequence_keys[sequence_index].keys, elem_size);
        count = MIN((DWORD)sequence_times[sequence_index].times.size, (DWORD)sequence_keys[sequence_index].keys.size);
        if (!times || !keys || count == 0) {
            return false;
        }
    }

    if (count == 1 || track->track_type == TRACK_NO_INTERP || sequence_time <= times[0]) {
        *left = keys;
        *right = keys;
        *ratio = 0.0f;
        return true;
    }

    for (DWORD i = 1; i < count; i++) {
        if (sequence_time <= times[i]) {
            DWORD left_time = times[i - 1];
            DWORD right_time = times[i];
            *left = keys + ((i - 1) * elem_size);
            *right = keys + (i * elem_size);
            *ratio = right_time > left_time
                ? (float)(sequence_time - left_time) / (float)(right_time - left_time)
                : 0.0f;
            return true;
        }
    }

    *left = keys + ((count - 1) * elem_size);
    *right = *left;
    *ratio = 0.0f;
    return true;
}

static VECTOR3 M2_EvaluateVectorTrack(m2Model_t const *model,
                                      m2TrackView_t const *track,
                                      DWORD sequence_index,
                                      DWORD sequence_time,
                                      VECTOR3 default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, sequence_index, sequence_time);

    if (!M2_FindTrackKeys(model, track, sequence_index, track_time, sizeof(VECTOR3), &left, &right, &ratio)) {
        return default_value;
    }
    if (left == right) {
        return *(VECTOR3 const *)left;
    }
    return Vector3_lerp((LPCVECTOR3)left, (LPCVECTOR3)right, ratio);
}

static FLOAT M2_EvaluateFloatTrack(m2Model_t const *model,
                                   m2TrackView_t const *track,
                                   DWORD sequence_index,
                                   DWORD sequence_time,
                                   FLOAT default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, sequence_index, sequence_time);

    if (!M2_FindTrackKeys(model, track, sequence_index, track_time, sizeof(FLOAT), &left, &right, &ratio)) {
        return default_value;
    }
    if (left == right) {
        return *(FLOAT const *)left;
    }
    return LerpNumber(*(FLOAT const *)left, *(FLOAT const *)right, ratio);
}

BOOL M2_CameraView(m2Model_t const *model,
                   DWORD camera_index,
                   LPVECTOR3 eye,
                   LPVECTOR3 target,
                   LPFLOAT fov_degrees,
                   LPFLOAT znear,
                   LPFLOAT zfar) {
    m2PoseTime_t pose;
    m2TrackView_t position_track;
    m2TrackView_t target_track;
    VECTOR3 position_value;
    VECTOR3 target_value;
    VECTOR3 position_pivot;
    VECTOR3 target_pivot;
    FLOAT fov;
    FLOAT far_clip;
    FLOAT near_clip;

    if (!model || !model->cameras || camera_index >= model->camera_count || !eye || !target) {
        return false;
    }

    if (model->format->format == M2_FORMAT_CLASSIC) {
        BYTE const *record = model->cameras + camera_index * model->format->camera_stride;
        m2CameraClassic_t const *camera = (m2CameraClassic_t const *)record;

        position_track = m2_classic_track(&camera->position_track);
        target_track = m2_classic_track(&camera->target_track);
        position_pivot = camera->position_pivot;
        target_pivot = camera->target_pivot;
        fov = camera->fov; near_clip = camera->near_clip; far_clip = camera->far_clip;
    } else {
        BYTE const *record = model->cameras + camera_index * model->format->camera_stride;
        m2CameraModern_t const *camera = (m2CameraModern_t const *)record;

        position_track = m2_modern_track(&camera->position_track);
        target_track = m2_modern_track(&camera->target_track);
        position_pivot = camera->position_pivot;
        target_pivot = camera->target_pivot;
        fov = camera->fov; near_clip = camera->near_clip; far_clip = camera->far_clip;
    }
    M2_FrameToPoseTime(model, tr.viewDef.time, &pose);
    position_value = M2_EvaluateVectorTrack(model,
                                            &position_track,
                                            pose.sequence_index,
                                            pose.sequence_time,
                                            (VECTOR3){ 0.0f, 0.0f, 0.0f });
    target_value = M2_EvaluateVectorTrack(model,
                                          &target_track,
                                          pose.sequence_index,
                                          pose.sequence_time,
                                          (VECTOR3){ 0.0f, 0.0f, 0.0f });
    *eye = Vector3_add(&position_pivot, &position_value);
    *target = Vector3_add(&target_pivot, &target_value);
    if (fov_degrees)
        *fov_degrees = fov > 0.0f ? fov * 0.6f * 180.0f / (FLOAT)M_PI : 35.0f;
    if (znear)
        *znear = near_clip > 0.0f ? near_clip : 1.0f;
    if (zfar)
        *zfar = far_clip > near_clip ? far_clip : 4000.0f;
    return true;
}

static QUATERNION M2_DecodeCompQuat(m2CompQuat_t const *source) {
    return (QUATERNION) {
        .x = (float)(source->auCompQ[0] & 0xFFFF) * 0.000030518044f - 1.0f,
        .y = (float)(source->auCompQ[0] >> 16)    * 0.000030518044f - 1.0f,
        .z = (float)(source->auCompQ[1] & 0xFFFF) * 0.000030518044f - 1.0f,
        .w = (float)(source->auCompQ[1] >> 16)    * 0.000030518044f - 1.0f,
    };
}

static QUATERNION M2_QuaternionNlerp(LPCQUATERNION q1, LPCQUATERNION q2, float ratio) {
    QUATERNION out = {
        .x = (q2->x - q1->x) * ratio + q1->x,
        .y = (q2->y - q1->y) * ratio + q1->y,
        .z = (q2->z - q1->z) * ratio + q1->z,
        .w = (q2->w - q1->w) * ratio + q1->w,
    };
    float m = out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w;
    float v = ((m - 0.95906597f) * -0.532516f) + 1.021435f;

    if (m <= 0.91521198f) {
        v *= (((v * v * m) - 0.95906597f) * -0.532516f) + 1.021435f;
        if (m <= 0.6521197f) {
            v *= (((v * v * m) - 0.95906597f) * -0.532516f) + 1.021435f;
        }
    }

    out.x *= v;
    out.y *= v;
    out.z *= v;
    out.w *= v;
    return out;
}

static QUATERNION M2_EvaluateRotationTrack(m2Model_t const *model,
                                           m2TrackView_t const *track,
                                           DWORD sequence_index,
                                           DWORD sequence_time,
                                           QUATERNION default_value) {
    void const *left;
    void const *right;
    float ratio;
    DWORD track_time = M2_TrackTime(model, track, sequence_index, sequence_time);
    DWORD elem_size = track && track->classic ? sizeof(QUATERNION) : sizeof(m2CompQuat_t);

    if (!M2_FindTrackKeys(model, track, sequence_index, track_time, elem_size, &left, &right, &ratio)) {
        return default_value;
    }
    if (track->classic) {
        if (left == right) {
            return *(QUATERNION const *)left;
        }

        QUATERNION q1 = *(QUATERNION const *)left;
        QUATERNION q2 = *(QUATERNION const *)right;
        return M2_QuaternionNlerp(&q1, &q2, ratio);
    }
    if (left == right) {
        return M2_DecodeCompQuat((m2CompQuat_t const *)left);
    }

    QUATERNION q1 = M2_DecodeCompQuat((m2CompQuat_t const *)left);
    QUATERNION q2 = M2_DecodeCompQuat((m2CompQuat_t const *)right);
    return M2_QuaternionNlerp(&q1, &q2, ratio);
}

static BOOL M2_TrackHasKeys(m2TrackView_t const *track) {
    if (!track) {
        return false;
    }
    if (track->classic) {
        return track->ranges.size > 0 && track->sequence_times.size > 0 && track->sequence_keys.size > 0;
    }
    return track->sequence_times.size > 0 && track->sequence_keys.size > 0;
}

static void const *M2_BonePtr(m2Model_t const *model, DWORD bone_index) {
    if (!model || !model->bones || bone_index >= model->bone_count || model->format->bone_stride == 0) {
        return NULL;
    }
    return model->bones + (bone_index * model->format->bone_stride);
}

static DWORD M2_BoneFlags(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return 0;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->flags;
    }
    return ((m2CompBoneModern_t const *)bone)->flags;
}

static WORD M2_BoneParentIndex(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return 0xFFFF;
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->parent_index;
    }
    return ((m2CompBoneModern_t const *)bone)->parent_index;
}

static VECTOR3 M2_BonePivot(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return (VECTOR3){ 0.0f, 0.0f, 0.0f };
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return ((m2CompBoneClassic_t const *)bone)->pivot;
    }
    return ((m2CompBoneModern_t const *)bone)->pivot;
}

static m2TrackView_t M2_BoneTranslationTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->translation_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->translation_track);
}

static m2TrackView_t M2_BoneRotationTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->rotation_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->rotation_track);
}

static m2TrackView_t M2_BoneScaleTrack(m2Model_t const *model, DWORD bone_index) {
    void const *bone = M2_BonePtr(model, bone_index);

    if (!bone) {
        return m2_modern_track(NULL);
    }
    if (model->format->format == M2_FORMAT_CLASSIC) {
        return m2_classic_track(&((m2CompBoneClassic_t const *)bone)->scale_track);
    }
    return m2_modern_track(&((m2CompBoneModern_t const *)bone)->scale_track);
}

static float m2_fixed16_to_float(SHORT v) { return (float)v / 32767.0f; }

static BOOL m2_is_visible(m2Model_t const *model, m2TrackView_t *track,
                          DWORD seq_idx, DWORD seq_time) {
	if (!M2_TrackHasKeys(track)) return true;
	void const *left, *right; float ratio;
	DWORD t = M2_TrackTime(model, track, seq_idx, seq_time);
	if (!M2_FindTrackKeys(model, track, seq_idx, t, sizeof(BYTE), &left, &right, &ratio))
		return true;
	BYTE b = *(BYTE const *)left;
	if (left != right) b = (BYTE)LerpNumber((FLOAT)*(BYTE const *)left, (FLOAT)*(BYTE const *)right, ratio);
	return b != 0;
}

static void m2_sample_part_track(m2Model_t const *model, m2PartTrack_t const *track,
                                 float progress, DWORD elem_size, void *out) {
	SHORT const *times = M2_ModelArrayPtr(model, track->times, sizeof(SHORT));
	BYTE const *vals = M2_ModelArrayPtr(model, track->values, elem_size);
	DWORD count = (DWORD)track->times.size;
	if (!times || !vals || count == 0 || count != (DWORD)track->values.size) {
		memset(out, 0, elem_size); return;
	}
	progress = MAX(0.0f, MIN(1.0f, progress));
	if (count == 1 || progress <= m2_fixed16_to_float(times[0])) {
		memcpy(out, vals, elem_size); return;
	}
	FOR_LOOP(i, count) {
		if (i == 0) continue;
		float fi = m2_fixed16_to_float(times[i]), fi1 = m2_fixed16_to_float(times[i - 1]);
		if (progress <= fi || i == count - 1) {
			float ratio = fi > fi1 ? (progress - fi1) / (fi - fi1) : 0.0f;
			BYTE const *a = vals + (i - 1) * elem_size, *b = vals + i * elem_size;
			if (elem_size == sizeof(SHORT))
				*(SHORT *)out = (SHORT)LerpNumber((FLOAT)*(SHORT *)a, (FLOAT)*(SHORT *)b, ratio);
			else if (elem_size == sizeof(VECTOR2))
				*(VECTOR2 *)out = Vector2_lerp((LPCVECTOR2)a, (LPCVECTOR2)b, ratio);
			else if (elem_size == sizeof(VECTOR3))
				*(VECTOR3 *)out = Vector3_lerp((LPCVECTOR3)a, (LPCVECTOR3)b, ratio);
			else memcpy(out, a, elem_size);
			return;
		}
	}
	memcpy(out, vals + (count - 1) * elem_size, elem_size);
}

static LPTEXTURE m2_particle_texture(m2Model_t const *model, m2Particle_t const *p) {
	m2TextureDisk_t const *tex; LPCSTR path;
	if (!model || !p || !model->textures.size || p->texture_index >= (WORD)model->textures.size)
		return tr.texture[TEX_WHITE];
	tex = M2_ModelArrayPtr(model, model->textures, sizeof(*tex));
	if (!tex || p->texture_index >= (DWORD)model->textures.size) return tr.texture[TEX_WHITE];
	path = m2_string_ptr(model->data, model->data_size, tex[p->texture_index].filename);
	return path && *path ? R_LoadTexture(path) : tr.texture[TEX_WHITE];
}

static LPTEXTURE m2_ribbon_texture(m2Model_t const *model, m2Ribbon_t const *r, DWORD slot) {
	WORD const *indices; m2TextureDisk_t const *tex; DWORD idx; LPCSTR path;
	if (!model || !r || !model->textures.size) return tr.texture[TEX_WHITE];
	indices = M2_ModelArrayPtr(model, r->texture_indices, sizeof(WORD));
	tex = M2_ModelArrayPtr(model, model->textures, sizeof(*tex));
	if (!indices || !tex || slot >= (DWORD)r->texture_indices.size) return tr.texture[TEX_WHITE];
	idx = indices[slot];
	if (idx >= (DWORD)model->textures.size) return tr.texture[TEX_WHITE];
	path = m2_string_ptr(model->data, model->data_size, tex[idx].filename);
	return path && *path ? R_LoadTexture(path) : tr.texture[TEX_WHITE];
}

#define M2_C32(v,a) (COLOR32){ (BYTE)((v).x < 0 ? 0 : (v).x > 1 ? 255 : (BYTE)((v).x*255+.5f)), \
                               (BYTE)((v).y < 0 ? 0 : (v).y > 1 ? 255 : (BYTE)((v).y*255+.5f)), \
                               (BYTE)((v).z < 0 ? 0 : (v).z > 1 ? 255 : (BYTE)((v).z*255+.5f)), \
                               (BYTE)((a) < 0 ? 0 : (a) > 1 ? 255 : (BYTE)((a)*255+.5f)) }

typedef struct {
	FLOAT speed, varia, lat, lon, grav, life, life_var, zsource, midpoint;
	FLOAT alpha[3]; VECTOR2 scale[3]; VECTOR3 color[3];
	LPTEXTURE texture; WORD bone_index;
	m2Model_t const *model; m2Particle_t const *p; LPCMATRIX4 model_matrix;
} m2_pctx_t;

/* M2 emitter positions are local to their bone, not the model origin. */
static void M2_EmitterMatrix(m2_pctx_t const *ctx, LPMATRIX4 out) {
    if (ctx->model && ctx->bone_index < ctx->model->bone_count && ctx->model->bone_matrices) {
        Matrix4_multiply(ctx->model_matrix, &ctx->model->bone_matrices[ctx->bone_index], out);
        return;
    }
    *out = *ctx->model_matrix;
}

static void m2_spawn_particle(void *raw) {
	m2_pctx_t *ctx = (m2_pctx_t *)raw;
	cparticle_t *fx = R_SpawnParticle(); if (!fx) return;
	FLOAT r = (FLOAT)rand() / (FLOAT)RAND_MAX;
	MATRIX4 emitter_matrix;
	M2_EmitterMatrix(ctx, &emitter_matrix);
	VECTOR3 local_origin = ctx->p->position;
	local_origin.z += ctx->zsource;
	VECTOR3 org = Matrix4_multiply_vector3(&emitter_matrix, &local_origin);
	VECTOR3 dir = m2_particle_direction(ctx->lat, ctx->lon,
		(VECTOR2){ 2.0f * (FLOAT)rand() / (FLOAT)RAND_MAX - 1.0f,
		           2.0f * (FLOAT)rand() / (FLOAT)RAND_MAX - 1.0f });
	VECTOR3 w_dir = Matrix4_multiply_vector3(&emitter_matrix, &dir);
	VECTOR3 w_zero = Matrix4_multiply_vector3(&emitter_matrix, &(VECTOR3){ 0, 0, 0 });
	dir = Vector3_sub(&w_dir, &w_zero);
	Vector3_normalize(&dir);
	fx->texture = ctx->texture;
	fx->blend_mode = m2_particle_blend_mode(ctx->p->blend_mode);
	fx->org = org;
	fx->vel = Vector3_scale(&dir, MAX(0.0f, ctx->speed + (r - 0.5f) * ctx->varia));
	fx->accel = (VECTOR3){ 0, 0, -ctx->grav };
	fx->color[0] = M2_C32(ctx->color[0], ctx->alpha[0]);
	fx->color[1] = M2_C32(ctx->color[1], ctx->alpha[1]);
	fx->color[2] = M2_C32(ctx->color[2], ctx->alpha[2]);
	FLOAT s[3] = { MAX(ctx->scale[0].x, ctx->scale[0].y), MAX(ctx->scale[1].x, ctx->scale[1].y), MAX(ctx->scale[2].x, ctx->scale[2].y) };
	fx->time = 0.0f; fx->lifespan = MAX(0.05f, ctx->life + (r - 0.5f) * ctx->life_var);
	m2_particle_encode_curve(&(M2PARTICLECURVE){ { s[0], s[1], s[2] }, ctx->midpoint, fx->lifespan }, fx);
	fx->columns = MAX(1, ctx->p->cols); fx->rows = MAX(1, ctx->p->rows);
}


/* Vanilla/TBC stores three static BGRA lifecycle colors and three scalar scales. */
static void m2p_sample_classic_data(BYTE const *raw, m2_pctx_t *ctx) {
    m2ParticleClassic_t const *p = (m2ParticleClassic_t const *)raw;
    FLOAT midpoint = p->midpoint;
    BOOL all_alpha_zero = true;
    if (midpoint < 0.0f || midpoint > 1.0f) midpoint = 0.5f;
    FOR_LOOP(i, 3) {
        DWORD bgra = p->colors[i];
        ctx->color[i] = (VECTOR3){ ((bgra >> 16) & 0xff) / 255.0f, ((bgra >> 8) & 0xff) / 255.0f,
                                   (bgra & 0xff) / 255.0f };
        ctx->alpha[i] = ((bgra >> 24) & 0xff) / 255.0f;
        ctx->scale[i] = (VECTOR2){ p->scales[i], 0.0f };
        if (ctx->alpha[i] > 0.01f) all_alpha_zero = false;
    }
    if (all_alpha_zero) { ctx->alpha[0] = 1.0f; ctx->alpha[1] = 1.0f; ctx->alpha[2] = 0.0f; }
    ctx->midpoint = midpoint;
}

static void M2_DrawParticles(m2Model_t const *model, renderEntity_t const *entity, LPCMATRIX4 model_matrix) {
	if (!model || !entity || !model->particles.size) return;
	BYTE const *base = M2_ModelArrayPtr(model, model->particles, model->format->particle_stride);
	if (!base) return;
	DWORD seq_idx, seq_time = M2_AnimationTime(model, entity, &seq_idx);
	FOR_LOOP(i, (DWORD)model->particles.size) {
		BYTE const *raw = base + i * model->format->particle_stride;
		m2Particle_t const *p = (m2Particle_t const *)raw;
		m2TrackView_t vis = m2_particle_track(model->format, raw, M2_PARTICLE_VISIBILITY);
		if (!m2_is_visible(model, &vis, seq_idx, seq_time)) continue;
		m2TrackView_t rate_t = m2_particle_track(model->format, raw, M2_PARTICLE_EMISSION_RATE);
		FLOAT rate = M2_EvaluateFloatTrack(model, &rate_t, seq_idx, seq_time, 0.0f);
		if (rate <= 0.0f) continue;
		m2TrackView_t life_t = m2_particle_track(model->format, raw, M2_PARTICLE_LIFE);
		FLOAT life = MAX(0.05f, M2_EvaluateFloatTrack(model, &life_t, seq_idx, seq_time, 0.5f));
		/* WoWee §M2Renderer::emitParticles: a flame reads as a flame only when enough
		   particles are alive at once.  Authored rates vary wildly (candle 40/s over 0.5s,
		   chandelier 1/s over 6s).  Steady-state population is rate × lifespan, so floor
		   the rate against lifespan to hold every fixture at a comparable density. */
		if (rate > 0.0f && life > 0.0f) {
			const float kMinLiveParticles = 15.0f;
			rate = MAX(rate, kMinLiveParticles / MAX(life, 0.1f));
		}
		m2_pctx_t ctx = { .midpoint = 0.5f, .model = model, .p = p, .model_matrix = model_matrix };
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_SPEED);
		  ctx.speed = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_VARIATION);
		  ctx.varia = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_VERTICAL_RANGE);
		  ctx.lat = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_HORIZONTAL_RANGE);
		  ctx.lon = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_GRAVITY);
		  ctx.grav = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		ctx.life     = life;
		ctx.life_var = model->format->format == M2_FORMAT_CLASSIC ? 0.0f : p->life_variation;
		{ m2TrackView_t t = m2_particle_track(model->format, raw, M2_PARTICLE_ZSOURCE);
		  ctx.zsource = M2_EvaluateFloatTrack(model, &t, seq_idx, seq_time, 0.0f); }
		if (model->format->format == M2_FORMAT_CLASSIC) m2p_sample_classic_data(raw, &ctx);
		else {
			m2PartTrack_t const *alpha_pt = m2_particle_part_track(model->format, raw, 1);
			m2PartTrack_t const *scale_pt = m2_particle_part_track(model->format, raw, 2);
			m2PartTrack_t const *color_pt = m2_particle_part_track(model->format, raw, 0);
			{ SHORT raw2;
			  m2_sample_part_track(model, alpha_pt, 0.0f, sizeof(raw2), &raw2);
			  ctx.alpha[0] = m2_fixed16_to_float(raw2);
			  m2_sample_part_track(model, alpha_pt, 0.5f, sizeof(raw2), &raw2);
			  ctx.alpha[1] = m2_fixed16_to_float(raw2);
			  m2_sample_part_track(model, alpha_pt, 1.0f, sizeof(raw2), &raw2);
			  ctx.alpha[2] = m2_fixed16_to_float(raw2); }
			m2_sample_part_track(model, scale_pt, 0.0f, sizeof(VECTOR2), &ctx.scale[0]);
			m2_sample_part_track(model, scale_pt, 0.5f, sizeof(VECTOR2), &ctx.scale[1]);
			m2_sample_part_track(model, scale_pt, 1.0f, sizeof(VECTOR2), &ctx.scale[2]);
			m2_sample_part_track(model, color_pt, 0.0f, sizeof(VECTOR3), &ctx.color[0]);
			m2_sample_part_track(model, color_pt, 0.5f, sizeof(VECTOR3), &ctx.color[1]);
			m2_sample_part_track(model, color_pt, 1.0f, sizeof(VECTOR3), &ctx.color[2]);
		}
		ctx.texture = m2_particle_texture(model, p);
		ctx.bone_index = p->bone_index;
		R_EmitParticles(rate, &model->emitter_accumulators[i], tr.viewDef.deltaTime, m2_spawn_particle, &ctx);
	}
}


static void M2_DrawRibbons(m2Model_t const *model, renderEntity_t const *entity, LPCMATRIX4 model_matrix) {
	if (!model || !entity || !model->ribbons.size || !model->ribbon_emitters) return;
	BYTE const *base = M2_ModelArrayPtr(model, model->ribbons, model->format->ribbon_stride);
	if (!base) return;
	DWORD seq_idx, seq_time = M2_AnimationTime(model, entity, &seq_idx);
	FLOAT dt = (FLOAT)tr.viewDef.deltaTime / 1000.0f;
	FOR_LOOP(i, (DWORD)model->ribbons.size) {
		BYTE const *raw = base + i * model->format->ribbon_stride;
		m2Ribbon_t const *r = (m2Ribbon_t const *)raw;
		m2RibbonEmitter_t *res = &model->ribbon_emitters[i];
		m2TrackView_t vis = m2_ribbon_track(model->format, raw, M2_RIBBON_VISIBILITY);
		if (!m2_is_visible(model, &vis, seq_idx, seq_time)) continue;
		FLOAT eps = MAX(0.0f, m2_ribbon_edges_per_second(model->format, raw));
		if (eps <= 0.0f) continue;
		/* -- Age existing edges and drop expired ones -------------------- */
		FLOAT edge_life = m2_ribbon_edge_lifetime(model->format, raw);
		int write = res->head, alive = res->count;
		for (int e = 0; e < alive; e++) {
			int idx = (write - alive + e + MAX_RIBBON_EDGES) % MAX_RIBBON_EDGES;
			res->edges[idx].age += dt;
		}
		while (alive > 0) {
			int oldest = (write - alive + MAX_RIBBON_EDGES) % MAX_RIBBON_EDGES;
			if (res->edges[oldest].age < edge_life) break;
			alive--;
		}
		res->count = alive;
		/* -- Evaluate animated tracks ------------------------------------ */
		m2TrackView_t color_t = m2_ribbon_track(model->format, raw, M2_RIBBON_COLOR);
		VECTOR3 col = M2_EvaluateVectorTrack(model, &color_t, seq_idx, seq_time, (VECTOR3){ 1, 1, 1 });
		m2TrackView_t alpha_t = m2_ribbon_track(model->format, raw, M2_RIBBON_ALPHA);
		void const *la, *ra; float rta;
		DWORD tt = M2_TrackTime(model, &alpha_t, seq_idx, seq_time);
		FLOAT a = 1.0f;
		if (M2_FindTrackKeys(model, &alpha_t, seq_idx, tt, sizeof(SHORT), &la, &ra, &rta)) {
			FLOAT al = m2_fixed16_to_float(*(SHORT const *)la);
			FLOAT ar = la == ra ? al : m2_fixed16_to_float(*(SHORT const *)ra);
			a = LerpNumber(al, ar, rta);
		}
		m2TrackView_t above_t = m2_ribbon_track(model->format, raw, M2_RIBBON_HEIGHT_ABOVE);
		m2TrackView_t below_t = m2_ribbon_track(model->format, raw, M2_RIBBON_HEIGHT_BELOW);
		FLOAT h_above = MAX(0.0f, M2_EvaluateFloatTrack(model, &above_t, seq_idx, seq_time, 0.0f));
		FLOAT h_below = MAX(0.0f, M2_EvaluateFloatTrack(model, &below_t, seq_idx, seq_time, 0.0f));
		FLOAT w = MAX(1.0f, h_above + h_below) / 2.0f; /* half-width for billboard scale */
		m2TrackView_t slot_t = m2_ribbon_track(model->format, raw, M2_RIBBON_TEXTURE_SLOT);
		WORD slot = 0;
		void const *ls, *rs_; float rts;
		DWORD tts = M2_TrackTime(model, &slot_t, seq_idx, seq_time);
		if (M2_FindTrackKeys(model, &slot_t, seq_idx, tts, sizeof(WORD), &ls, &rs_, &rts))
			slot = (WORD)LerpNumber((FLOAT)*(WORD const *)ls, (FLOAT)*(WORD const *)rs_, rts);
		COLOR32 rgba = M2_C32(col, a);
		BYTE size_b = (BYTE)MIN(255, (int)(w + 0.5f));
		DWORD cols = MAX(1, m2_ribbon_cols(model->format, raw));
		DWORD rows = MAX(1, m2_ribbon_rows(model->format, raw));
		LPTEXTURE tex = m2_ribbon_texture(model, r, slot);
		FLOAT grav = m2_ribbon_gravity(model->format, raw);
		/* -- Emit new edges at the animated spine position ---------------- */
		MATRIX4 emitter_matrix = *model_matrix;
		if (r->bone_index < model->bone_count && model->bone_matrices)
			Matrix4_multiply(model_matrix, &model->bone_matrices[r->bone_index], &emitter_matrix);
		VECTOR3 spine = Matrix4_multiply_vector3(&emitter_matrix, &r->position);
		res->acc += eps * dt;
		while (res->acc >= 1.0f) {
			res->acc -= 1.0f;
			if (alive >= MAX_RIBBON_EDGES) { /* drop oldest */
				alive--;
			}
			m2RibbonEdge_t *e = &res->edges[write];
			e->world_pos = spine;
			e->color = col; e->alpha = a;
			e->height_above = h_above; e->height_below = h_below;
			e->age = 0.0f;
			write = (write + 1) % MAX_RIBBON_EDGES;
			alive++;
		}
		if (res->acc > 2.0f) res->acc = 0.0f;
		res->head = write; res->count = alive;
		/* -- Spawn one billboard particle per active edge ----------------- */
		for (int e = 0; e < alive; e++) {
			int idx = (write - alive + e + MAX_RIBBON_EDGES) % MAX_RIBBON_EDGES;
			m2RibbonEdge_t *re = &res->edges[idx];
			cparticle_t *fx = R_SpawnParticle(); if (!fx) break;
			/* Apply gravity downward drift (semi-implicit: ½ g t² at draw time) */
			re->world_pos.z -= MAX(0.0f, grav) * dt * dt * 0.5f;
			fx->texture = tex; fx->org = re->world_pos;
			fx->vel = (VECTOR3){ 0, 0, 0 };
			fx->accel = (VECTOR3){ 0, 0, -MAX(0.0f, grav) };
			FLOAT fade = 1.0f - MIN(1.0f, re->age / MAX(0.01f, edge_life));
			COLOR32 fc = rgba;
			fc.a = (BYTE)((FLOAT)fc.a * fade + 0.5f);
			fx->color[0] = fx->color[1] = fx->color[2] = fc;
			fx->size[0] = fx->size[1] = fx->size[2] = size_b;
			fx->midtime = 0x80; fx->columns = cols; fx->rows = rows;
			fx->time = 0.0f; fx->lifespan = MAX(0.05f, edge_life - re->age);
		}
	}
}

static void M2_CalculateBoneMatrices(m2Model_t const *model, renderEntity_t const *entity) {
    MATRIX4 identity;
    m2PoseTime_t current_pose;
    m2PoseTime_t old_pose;
    FLOAT pose_lerp = 1.0f;

    if (!model || !model->bone_matrices || !model->bones) {
        return;
    }

    Matrix4_identity(&identity);
    M2_FrameToPoseTime(model, entity ? entity->frame : tr.viewDef.time, &current_pose);
    old_pose = current_pose;
    if (entity &&
        entity->oldframe != entity->frame &&
        M2_FrameToPoseTime(model, entity->oldframe, &old_pose) &&
        old_pose.sequence_index != current_pose.sequence_index) {
        pose_lerp = MAX(0.0f, MIN(1.0f, tr.viewDef.lerpfrac));
    } else {
        current_pose.sequence_time = M2_AnimationTime(model, entity, &current_pose.sequence_index);
        old_pose = current_pose;
    }

    FOR_LOOP(i, model->bone_count) {
        WORD parent_index = M2_BoneParentIndex(model, i);
        DWORD flags = M2_BoneFlags(model, i);
        VECTOR3 pivot = M2_BonePivot(model, i);
        m2TrackView_t translation_track = M2_BoneTranslationTrack(model, i);
        m2TrackView_t rotation_track = M2_BoneRotationTrack(model, i);
        m2TrackView_t scale_track = M2_BoneScaleTrack(model, i);
        BOOL has_keys = M2_TrackHasKeys(&translation_track) ||
                        M2_TrackHasKeys(&rotation_track) ||
                        M2_TrackHasKeys(&scale_track);
        LPCMATRIX4 parent = &identity;

        if (parent_index != 0xFFFF && parent_index < i) {
            parent = &model->bone_matrices[parent_index];
        }

        if ((flags & (0x80 | 0x200)) || has_keys) {
            MATRIX4 local;
            VECTOR3 translation = M2_EvaluateVectorTrack(model,
                                                         &translation_track,
                                                         current_pose.sequence_index,
                                                         current_pose.sequence_time,
                                                         (VECTOR3){ 0.0f, 0.0f, 0.0f });
            QUATERNION rotation = M2_EvaluateRotationTrack(model,
                                                           &rotation_track,
                                                           current_pose.sequence_index,
                                                           current_pose.sequence_time,
                                                           (QUATERNION){ 0.0f, 0.0f, 0.0f, 1.0f });
            VECTOR3 scale = M2_EvaluateVectorTrack(model,
                                                   &scale_track,
                                                   current_pose.sequence_index,
                                                   current_pose.sequence_time,
                                                   (VECTOR3){ 1.0f, 1.0f, 1.0f });

            if (pose_lerp < 1.0f) {
                VECTOR3 old_translation = M2_EvaluateVectorTrack(model,
                                                                 &translation_track,
                                                                 old_pose.sequence_index,
                                                                 old_pose.sequence_time,
                                                                 (VECTOR3){ 0.0f, 0.0f, 0.0f });
                QUATERNION old_rotation = M2_EvaluateRotationTrack(model,
                                                                   &rotation_track,
                                                                   old_pose.sequence_index,
                                                                   old_pose.sequence_time,
                                                                   (QUATERNION){ 0.0f, 0.0f, 0.0f, 1.0f });
                VECTOR3 old_scale = M2_EvaluateVectorTrack(model,
                                                           &scale_track,
                                                           old_pose.sequence_index,
                                                           old_pose.sequence_time,
                                                           (VECTOR3){ 1.0f, 1.0f, 1.0f });

                translation = Vector3_lerp(&old_translation, &translation, pose_lerp);
                rotation = Quaternion_slerp(&old_rotation, &rotation, pose_lerp);
                scale = Vector3_lerp(&old_scale, &scale, pose_lerp);
            }

            Matrix4_from_rotation_translation_scale_origin(&local,
                                                           &rotation,
                                                           &translation,
                                                           &scale,
                                                           &pivot);
            Matrix4_multiply(parent, &local, &model->bone_matrices[i]);
        } else {
            model->bone_matrices[i] = *parent;
        }
    }
}

static void M2_UploadBatchBones(m2Model_t const *model, m2ModelBatch_t const *batch, LPSHADER shader) {
    MATRIX4 palette[M2_MAX_BONES_PER_BATCH];

    FOR_LOOP(i, M2_MAX_BONES_PER_BATCH) {
        Matrix4_identity(&palette[i]);
    }

    if (model && batch && model->bone_matrices && model->bone_lookup_table) {
        DWORD count = MIN((DWORD)batch->bone_count, (DWORD)M2_MAX_BONES_PER_BATCH);
        FOR_LOOP(i, count) {
            DWORD lookup = (DWORD)batch->bone_combo_index + i;
            if (lookup < model->bone_lookup_count) {
                WORD bone_index = model->bone_lookup_table[lookup];
                if (bone_index < model->bone_count) {
                    palette[i] = model->bone_matrices[bone_index];
                }
            }
        }
    }

    R_Call(glUniformMatrix4fv, shader->uBones, M2_MAX_BONES_PER_BATCH, GL_FALSE, palette[0].v);
}

static LPTEXTURE M2_TextureForBatch(BYTE const *m2_data,
                                    DWORD m2_size,
                                    m2GeometryInfo_t const *geom,
                                    m2Batch_t const *batch,
                                    LPCSTR modelFilename,
                                    BOOL use_texture_lookup,
                                    DWORD *texture_type_out) {
    SHORT const *texture_lookup;
    m2TextureDisk_t const *texture;
    SHORT texture_index;
    LPCSTR texture_path;
    PATHSTR replacement_path;

    if (!geom || !batch || batch->texture_count == 0) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }

    if (use_texture_lookup) {
        texture_lookup = m2_array_ptr(m2_data, m2_size, geom->texture_lookup_table, sizeof(SHORT));
        if (!texture_lookup || batch->texture_combo_index >= (WORD)geom->texture_lookup_table.size) {
            return tr.texture[TEX_WHITE];
        }
        texture_index = texture_lookup[batch->texture_combo_index];
    } else {
        texture_index = (SHORT)batch->texture_combo_index;
    }
    if (texture_index < 0 || texture_index >= geom->textures.size) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }

    texture = m2_array_ptr(m2_data, m2_size, geom->textures, sizeof(*texture));
    if (!texture) {
        if (texture_type_out) {
            *texture_type_out = 0;
        }
        return tr.texture[TEX_WHITE];
    }
    if (texture_type_out) {
        *texture_type_out = texture[texture_index].type;
    }

    texture_path = m2_string_ptr(m2_data, m2_size, texture[texture_index].filename);
    if (!texture_path || !*texture_path) {
        if (M2_DefaultCharacterTexturePath(modelFilename, texture[texture_index].type, replacement_path, sizeof(replacement_path))) {
            return R_LoadTexture(replacement_path);
        }
        if (M2_DefaultObjectComponentTexturePath(modelFilename, texture[texture_index].type, replacement_path, sizeof(replacement_path))) {
            return R_LoadTexture(replacement_path);
        }
        if (M2_DefaultCreatureTexturePath(modelFilename, texture[texture_index].type, replacement_path, sizeof(replacement_path))) {
            return R_LoadTexture(replacement_path);
        }
        return tr.texture[TEX_WHITE];
    }
    return R_LoadTexture(texture_path);
}

static BOOL M2_SkinPath(LPCSTR model_path, LPSTR out, DWORD out_size) {
    if (!m2_copy_with_extension(model_path, "00.skin", out, out_size)) {
        return false;
    }
    return true;
}

static VERTEX M2_MakeVertex(m2VertexDisk_t const *src) {
    VERTEX out;
    memset(&out, 0, sizeof(out));
    out.position = src->pos;
    out.normal = src->normal;
    out.texcoord = src->tex_coords[0];
    out.color = COLOR32_WHITE;
    memcpy(out.skin, src->bone_indices, sizeof(src->bone_indices));
    memcpy(out.boneWeight, src->bone_weights, sizeof(src->bone_weights));
    return out;
}

static BOOL M2_CalculateGeometryBounds(m2VertexDisk_t const *vertices, DWORD num_vertices, BOX3 *bounds) {
    if (!vertices || !num_vertices || !bounds) {
        return false;
    }

    bounds->min = vertices[0].pos;
    bounds->max = vertices[0].pos;
    for (DWORD i = 1; i < num_vertices; i++) {
        VECTOR3 p = vertices[i].pos;
        bounds->min.x = MIN(bounds->min.x, p.x);
        bounds->min.y = MIN(bounds->min.y, p.y);
        bounds->min.z = MIN(bounds->min.z, p.z);
        bounds->max.x = MAX(bounds->max.x, p.x);
        bounds->max.y = MAX(bounds->max.y, p.y);
        bounds->max.z = MAX(bounds->max.z, p.z);
    }
    return true;
}

static void M2_AddBatch(m2Model_t *model,
                        BYTE const *m2_data,
                        DWORD m2_size,
                        m2GeometryInfo_t const *geom,
                        m2VertexDisk_t const *m2_vertices,
                        WORD const *skin_vertices,
                        DWORD skin_vertex_count,
                        WORD const *skin_indices,
                        DWORD skin_index_count,
                        m2Ubyte4_t const *skin_bones,
                        DWORD index_start,
                        DWORD index_count,
                        WORD bone_count,
                        WORD bone_combo_index,
                        m2Batch_t const *batch,
                        WORD section_id,
                        LPCSTR modelFilename,
                        BOOL use_texture_lookup) {
    VERTEX *vertices;
    m2ModelBatch_t *render_batch;

    if (!geom || !batch || index_count == 0) {
        return;
    }

    vertices = ri.MemAlloc(sizeof(*vertices) * index_count);
    if (!vertices) {
        return;
    }

    FOR_LOOP(i, index_count) {
        DWORD skin_index = index_start + i;
        DWORD vertex_lookup;
        DWORD vertex_index;

        if (skin_index >= skin_index_count) {
            memset(&vertices[i], 0, sizeof(vertices[i]));
            vertices[i].color = COLOR32_WHITE;
            continue;
        }
        vertex_lookup = skin_indices[skin_index];
        if (vertex_lookup >= skin_vertex_count) {
            memset(&vertices[i], 0, sizeof(vertices[i]));
            vertices[i].color = COLOR32_WHITE;
            continue;
        }
        vertex_index = skin_vertices[vertex_lookup];
        if (vertex_index >= (DWORD)geom->vertices.size) {
            memset(&vertices[i], 0, sizeof(vertices[i]));
            vertices[i].color = COLOR32_WHITE;
            continue;
        }
        vertices[i] = M2_MakeVertex(&m2_vertices[vertex_index]);
        if (skin_bones) {
            memcpy(vertices[i].skin, skin_bones[vertex_lookup].v, sizeof(skin_bones[vertex_lookup].v));
        }
    }

    render_batch = ri.MemAlloc(sizeof(*render_batch));
    memset(render_batch, 0, sizeof(*render_batch));
    render_batch->buffer = R_MakeVertexArrayObject(vertices, index_count);
    render_batch->texture = M2_TextureForBatch(m2_data,
                                               m2_size,
                                               geom,
                                               batch,
                                               modelFilename,
                                               use_texture_lookup,
                                               &render_batch->texture_type);
    render_batch->num_vertices = index_count;
    render_batch->bone_count = bone_count;
    render_batch->bone_combo_index = bone_combo_index;
    render_batch->section_id = section_id;
	render_batch->geoset_index = batch->geoset_index;
	render_batch->character_texture_slot = M2_CharacterTextureSlotForSection(section_id);
	render_batch->alphamode = (model->material_blend_modes && batch->material_index < model->num_materials) ?
		model->material_blend_modes[batch->material_index] : 0;
	ADD_TO_LIST(render_batch, model->batches);
    model->num_batches++;
    ri.MemFree(vertices);
}

static BOOL M2_LoadSkinData(LPCSTR modelFilename,
                            LPBYTE *skin_data,
                            DWORD *skin_size,
                            PATHSTR skin_path) {
    int read_size;

    if (!skin_data || !skin_size || !M2_SkinPath(modelFilename, skin_path, sizeof(PATHSTR))) {
        return false;
    }

    read_size = ri.FS_ReadFile(skin_path, (void **)skin_data);
    if (read_size <= 0 && m2_path_has_extension(modelFilename, ".mdx")) {
        PATHSTR m2_path;
        if (m2_copy_with_extension(modelFilename, ".m2", m2_path, sizeof(m2_path)) &&
            M2_SkinPath(m2_path, skin_path, sizeof(PATHSTR))) {
            read_size = ri.FS_ReadFile(skin_path, (void **)skin_data);
        }
    }
    if (read_size <= 0 || !*skin_data) {
        *skin_size = 0;
        return false;
    }
    *skin_size = (DWORD)read_size;
    return true;
}

static BOOL M2_InitLegacyGeometry(BYTE const *m2_base,
                                  DWORD m2_size,
                                  m2GeometryInfo_t *geom,
                                  m2LegacyView_t const **view) {
    m2HeaderLegacy_t const *legacy;

    if (!m2_base || m2_size < sizeof(*legacy) || !geom || !view) {
        return false;
    }

    legacy = (m2HeaderLegacy_t const *)m2_base;
    *geom = (m2GeometryInfo_t){
        .vertices = legacy->vertices,
        .textures = legacy->textures,
        .texture_lookup_table = legacy->texture_lookup_table,
        .bounding_box = legacy->bounding_box,
    };
    *view = m2_array_ptr(m2_base, m2_size, legacy->views, sizeof(**view));
    return *view != NULL;
}

static BOOL M2_InitModernGeometry(BYTE const *m2_base,
                                  DWORD m2_size,
                                  m2GeometryInfo_t *geom,
                                  m2Header_t const **header) {
    m2Header_t const *modern;

    if (!m2_base || m2_size < sizeof(*modern) || !geom || !header) {
        return false;
    }

    modern = (m2Header_t const *)m2_base;
    *geom = (m2GeometryInfo_t){
        .vertices = modern->vertices,
        .textures = modern->textures,
        .texture_lookup_table = modern->texture_lookup_table,
        .bounding_box = modern->bounding_box,
    };
    *header = modern;
    return true;
}

static BOOL M2_GetSectionRange(void const *sections,
                               BOOL legacy_sections,
                               DWORD section_count,
                               WORD section_index,
                               WORD *section_id,
                               DWORD *index_start,
                               DWORD *index_count,
                               WORD *bone_count,
                               WORD *bone_combo_index) {
    if (!sections || !index_start || !index_count || !bone_count || !bone_combo_index || section_index >= section_count) {
        return false;
    }
    if (legacy_sections) {
        m2SkinSectionLegacy_t const *section = &((m2SkinSectionLegacy_t const *)sections)[section_index];
        if (section_id) {
            *section_id = section->skin_section_id;
        }
        *index_start = section->index_start;
        *index_count = section->index_count;
        *bone_count = section->bone_count;
        *bone_combo_index = section->bone_combo_index;
    } else {
        m2SkinSection_t const *section = &((m2SkinSection_t const *)sections)[section_index];
        if (section_id) {
            *section_id = section->skin_section_id;
        }
        *index_start = section->index_start;
        *index_count = section->index_count;
        *bone_count = section->bone_count;
        *bone_combo_index = section->bone_combo_index;
    }
    return true;
}

static BOOL M2_IsCharacterModelPath(LPCSTR model_path) {
    LPCSTR character;
    LPCSTR race_end;
    LPCSTR gender;
    size_t gender_len;

    if (!model_path) {
        return false;
    }

    character = strcasestr(model_path, "Character\\");
    if (!character) {
        character = strcasestr(model_path, "Character/");
    }
    if (!character) {
        return false;
    }

    race_end = strpbrk(character + strlen("Character\\"), "\\/");
    if (!race_end || !race_end[1]) {
        return false;
    }

    gender = race_end + 1;
    gender_len = strcspn(gender, "\\/.");
    return (gender_len == 4 && !strncasecmp(gender, "Male", 4)) ||
           (gender_len == 6 && !strncasecmp(gender, "Female", 6));
}

static BOOL M2_CharacterGeosetVisible(m2Model_t const *model,
                                       m2CharacterOutfit_t const *outfit,
                                       WORD section_id) {
    if (!model || !model->character_model) {
        return true;
    }
    if (section_id < 400) {
        /* Group 1 (100–199): hair geosets — helmet replaces hair with bald scalp (section 1). */
        if (section_id >= 100 && outfit && (outfit->flags & M2_CHAR_FLAG_HELM)) {
            return false;
        }
        return true;
    }
    if (!outfit) {
        /* Bare defaults: forearms (401), ears hidden (702), no cape (1501). */
        return section_id == 401 || section_id == 702 || section_id == 1501;
    }

    DWORD group = section_id / 100;
    DWORD geoset = (group < M2_NUM_GEOSET_GROUPS) ? outfit->geoset[group] : 0;
    WORD expected;

    /* Geoset group → section ID mapping (from WoWee/AzerothCore conventions):
     *   Group  4 (gloves):    base 401 = kGeosetBareForearms
     *   Group  5 (boots):     base 501 = kGeosetBareShins
     *   Group  7 (ears):      base 701, default 702 (helmet hides ears)
     *   Group  8 (sleeves):   base 801 = kGeosetBareSleeves
     *   Group  9 (kneepads):  base 902 = kGeosetDefaultKneepads
     *   Group 10 (eyes):      base 1001
     *   Group 11 (eyebrows):  base 1101
     *   Group 12 (hair):      base 1201
     *   Group 13 (pants):     base 1301 = kGeosetBarePants
     *   Group 15 (cloak):     base 1501 = kGeosetNoCape, 1502 = kGeosetWithCape */
    switch (group) {
        case 4:  expected = 401 + geoset; break;
        case 5:  expected = 501 + geoset; break;
        case 7:  expected = 700 + geoset; break;
        case 8:  expected = 801 + geoset; break;
        case 9:  expected = 902 + geoset; break;
        case 10: expected = 1001 + geoset; break;
        case 11: expected = 1101 + geoset; break;
        case 12: expected = 1201 + geoset; break;
        case 13:
            if (outfit->flags & 0x4) {
                return false;
            }
            expected = 1301 + geoset;
            break;
        case 15: expected = 1501 + geoset; break;
        default: return false;
    }
    return section_id == expected;
}

static void M2_FreeModelData(m2Model_t *model) {
	if (!model) {
		return;
	}
	if (model->bone_matrices) {
		ri.MemFree(model->bone_matrices);
		model->bone_matrices = NULL;
	}
	if (model->emitter_accumulators) {
		ri.MemFree(model->emitter_accumulators);
		model->emitter_accumulators = NULL;
	}
	if (model->ribbon_emitters) {
		ri.MemFree(model->ribbon_emitters);
		model->ribbon_emitters = NULL;
	}
	if (model->material_blend_modes) {
		ri.MemFree(model->material_blend_modes);
		model->material_blend_modes = NULL;
	}
	if (model->data) {
		ri.MemFree(model->data);
		model->data = NULL;
	}
}

static BOOL M2_CopyModelData(m2Model_t *model, BYTE const *m2_base, DWORD m2_size, BOOL legacy_header) {
    m2Array_t bones;
    m2Array_t sequences;
    m2Array_t bone_lookup_table;
    m2Array_t cameras;

    if (!model || !m2_base || m2_size < sizeof(m2Header_t)) {
        return false;
    }

    model->data = ri.MemAlloc(m2_size);
    if (!model->data) {
        return false;
    }
    memcpy(model->data, m2_base, m2_size);
    model->data_size = m2_size;
    model->header = (m2Header_t *)model->data;
    model->format = m2_format_def(model->header->version);

    if (legacy_header) {
        m2HeaderLegacy_t *legacy = (m2HeaderLegacy_t *)model->data;
        model->global_loops = legacy->global_loops;
        bones = legacy->bones;
        sequences = legacy->sequences;
        bone_lookup_table = legacy->bone_lookup_table;
        model->attachments = legacy->attachments;
        model->attachment_lookup = legacy->attachment_lookup;
        model->textures = legacy->textures;
        model->texture_lookup_table = legacy->texture_lookup_table;
        model->ribbons = legacy->ribbons;
        model->particles = legacy->particles;
        cameras = legacy->cameras;
    } else {
        model->global_loops = model->header->global_loops;
        bones = model->header->bones;
        sequences = model->header->sequences;
        bone_lookup_table = model->header->bone_lookup_table;
        model->attachments = model->header->attachments;
        model->attachment_lookup = model->header->attachment_lookup;
        model->textures = model->header->textures;
        model->texture_lookup_table = model->header->texture_lookup_table;
        model->ribbons = model->header->ribbons;
        model->particles = model->header->particles;
        cameras = model->header->cameras;
    }

    model->bones = M2_ModelArrayPtr(model, bones, model->format->bone_stride);
    model->sequences = M2_ModelArrayPtr(model, sequences, model->format->sequence_stride);
    model->bone_lookup_table = M2_ModelArrayPtr(model, bone_lookup_table, sizeof(*model->bone_lookup_table));
    model->cameras = M2_ModelArrayPtr(model, cameras, model->format->camera_stride);
    model->bone_count = model->bones ? (DWORD)bones.size : 0;
    model->sequence_count = model->sequences ? (DWORD)sequences.size : 0;
    model->bone_lookup_count = model->bone_lookup_table ? (DWORD)bone_lookup_table.size : 0;
    model->camera_count = model->cameras ? (DWORD)cameras.size : 0;

    if (model->bone_count) {
        model->bone_matrices = ri.MemAlloc(sizeof(*model->bone_matrices) * model->bone_count);
        if (!model->bone_matrices) {
            M2_FreeModelData(model);
            return false;
        }
        FOR_LOOP(i, model->bone_count) {
            Matrix4_identity(&model->bone_matrices[i]);
        }
    }

    if (model->particles.size) {
        model->emitter_accumulators = ri.MemAlloc(sizeof(*model->emitter_accumulators) * (DWORD)model->particles.size);
        if (!model->emitter_accumulators) { M2_FreeModelData(model); return false; }
        memset(model->emitter_accumulators, 0, sizeof(*model->emitter_accumulators) * (DWORD)model->particles.size);
    }
    if (model->ribbons.size) {
        model->ribbon_emitters = ri.MemAlloc(sizeof(*model->ribbon_emitters) * (DWORD)model->ribbons.size);
        if (!model->ribbon_emitters) { M2_FreeModelData(model); return false; }
        memset(model->ribbon_emitters, 0, sizeof(*model->ribbon_emitters) * (DWORD)model->ribbons.size);
    }

    return true;
}

m2Model_t *R_LoadModelM2(LPCSTR modelFilename, void *buffer, DWORD size) {
    m2Header_t const *modern_header = NULL;
    m2GeometryInfo_t geom;
    BYTE const *m2_base = buffer;
    DWORD m2_size = size;
    m2VertexDisk_t const *m2_vertices;
    LPBYTE skin_data = NULL;
    DWORD skin_size = 0;
    PATHSTR skin_path;
    m2SkinHeader_t const *skin;
    m2LegacyView_t const *legacy_view = NULL;
    WORD const *skin_vertices;
    WORD const *skin_indices;
    void const *sections;
    m2Batch_t const *batches;
    m2Ubyte4_t const *skin_bones;
    m2Model_t *model;
    DWORD batch_count;
    DWORD section_count;
    DWORD skin_vertex_count;
    DWORD skin_index_count;
    BOOL using_legacy_view = false;
    BOOL character_model;

    if (!buffer || size < sizeof(DWORD)) {
        return M2_CreateFallbackModel(modelFilename, "missing model data");
    }

    if (*(DWORD *)buffer == ID_MD21) {
        m2_base = m2_find_chunk(buffer, size, "MD21", &m2_size);
        if (!m2_base) {
            m2_base = buffer;
            m2_size = size;
        }
    } else if (*(DWORD *)buffer == ID_12DM) {
        m2_base = m2_find_chunk(buffer, size, "12DM", &m2_size);
    }
    if (!m2_base || m2_size < sizeof(DWORD) * 2) {
        return M2_CreateFallbackModel(modelFilename, "truncated header");
    }

    if (*(DWORD *)m2_base != ID_MD20) {
        return M2_CreateFallbackModel(modelFilename, "bad MD20 header");
    }

    if (!M2_InitModernGeometry(m2_base, m2_size, &geom, &modern_header)) {
        return M2_CreateFallbackModel(modelFilename, "invalid modern header");
    }

    if (M2_LoadSkinData(modelFilename, &skin_data, &skin_size, skin_path) &&
        skin_size >= sizeof(*skin)) {
        skin = (m2SkinHeader_t const *)skin_data;
        if (skin->magic == MAKEFOURCC('S', 'K', 'I', 'N')) {
            skin_vertices = m2_array_ptr(skin_data, skin_size, skin->vertices, sizeof(*skin_vertices));
            skin_indices = m2_array_ptr(skin_data, skin_size, skin->indices, sizeof(*skin_indices));
            skin_bones = m2_array_ptr(skin_data, skin_size, skin->bones, sizeof(*skin_bones));
            sections = m2_array_ptr(skin_data, skin_size, skin->sections, sizeof(m2SkinSection_t));
            batches = m2_array_ptr(skin_data, skin_size, skin->batches, sizeof(*batches));
            batch_count = (DWORD)skin->batches.size;
            section_count = (DWORD)skin->sections.size;
            skin_vertex_count = (DWORD)skin->vertices.size;
            skin_index_count = (DWORD)skin->indices.size;
        } else {
            M2_LogFallback(modelFilename, "bad skin magic; trying legacy embedded view");
            ri.FS_FreeFile(skin_data);
            skin_data = NULL;
            skin_vertices = NULL;
            skin_indices = NULL;
            skin_bones = NULL;
            sections = NULL;
            batches = NULL;
            batch_count = section_count = skin_vertex_count = skin_index_count = 0;
        }
    } else {
        if (skin_data) {
            ri.FS_FreeFile(skin_data);
            skin_data = NULL;
        }
        skin_vertices = NULL;
        skin_indices = NULL;
        skin_bones = NULL;
        sections = NULL;
        batches = NULL;
        batch_count = section_count = skin_vertex_count = skin_index_count = 0;
    }

    if (!skin_vertices || !skin_indices || !sections || !batches) {
        DWORD version = modern_header ? modern_header->version : 0;

        if (version <= 260 && M2_InitLegacyGeometry(m2_base, m2_size, &geom, &legacy_view)) {
            skin_vertices = m2_array_ptr(m2_base, m2_size, legacy_view->vertices, sizeof(*skin_vertices));
            skin_indices = m2_array_ptr(m2_base, m2_size, legacy_view->indices, sizeof(*skin_indices));
            skin_bones = m2_array_ptr(m2_base, m2_size, legacy_view->bones, sizeof(*skin_bones));
            sections = m2_array_ptr(m2_base, m2_size, legacy_view->sections, sizeof(m2SkinSectionLegacy_t));
            batches = m2_array_ptr(m2_base, m2_size, legacy_view->batches, sizeof(*batches));
            batch_count = (DWORD)legacy_view->batches.size;
            section_count = (DWORD)legacy_view->sections.size;
            skin_vertex_count = (DWORD)legacy_view->vertices.size;
            skin_index_count = (DWORD)legacy_view->indices.size;
            using_legacy_view = true;
        }
    }

    m2_vertices = m2_array_ptr(m2_base, m2_size, geom.vertices, sizeof(*m2_vertices));
    if (!m2_vertices || !skin_vertices || !skin_indices || !sections || !batches) {
        if (skin_data) {
            ri.FS_FreeFile(skin_data);
        }
        return M2_CreateFallbackModel(modelFilename,
                                      using_legacy_view ? "invalid legacy embedded view" : "missing skin profile");
    }

    model = ri.MemAlloc(sizeof(*model));
    memset(model, 0, sizeof(*model));
    snprintf(model->filename, sizeof(model->filename), "%s", modelFilename ? modelFilename : "");
    model->bounds = (BOX3){ geom.bounding_box.min, geom.bounding_box.max };
    model->has_geometry_bounds = M2_CalculateGeometryBounds(m2_vertices,
                                                            (DWORD)geom.vertices.size,
                                                            &model->geometry_bounds);
    character_model = M2_IsCharacterModelPath(modelFilename);
    model->character_model = character_model;
    if (!M2_CopyModelData(model, m2_base, m2_size, using_legacy_view)) {
        if (skin_data) {
            ri.FS_FreeFile(skin_data);
        }
        M2_FreeModelData(model);
        ri.MemFree(model);
        return M2_CreateFallbackModel(modelFilename, "failed to copy animation data");
    }

	/* Vanilla stores the same material records in render_flags, at a different header offset. */
	{
		m2HeaderLegacy_t const *legacy = (m2HeaderLegacy_t const *)model->data;
		m2Array_t material_array = m2_material_array(model->header->materials, legacy->render_flags,
			using_legacy_view);

		if (material_array.size > 0) {
			BYTE const *materials_data = m2_array_ptr(model->data, model->data_size, material_array, 4);
			if (materials_data) {
				model->num_materials = (DWORD)material_array.size;
				model->material_blend_modes = ri.MemAlloc(model->num_materials);
				if (model->material_blend_modes) {
					FOR_LOOP(i, model->num_materials) {
						WORD wow_blend = ((WORD const *)materials_data)[i * 2 + 1];
						model->material_blend_modes[i] = m2_blend_mode(wow_blend);
					}
				}
			}
		}
	}

	FOR_LOOP(i, batch_count) {
		m2Batch_t const *batch = &batches[i];
        DWORD index_start;
        DWORD index_count;
        WORD bone_count;
        WORD bone_combo_index;
        WORD section_id = 0;
        if (!M2_GetSectionRange(sections,
                                using_legacy_view,
                                section_count,
                                batch->skin_section_index,
                                &section_id,
                                &index_start,
                                &index_count,
                                &bone_count,
                                &bone_combo_index)) {
            continue;
        }
        M2_AddBatch(model,
                    m2_base,
                    m2_size,
                    &geom,
                    m2_vertices,
                    skin_vertices,
                    skin_vertex_count,
                    skin_indices,
                    skin_index_count,
                    skin_bones,
                    index_start,
                    index_count,
                    bone_count,
                    bone_combo_index,
                    batch,
                    section_id,
                    modelFilename,
                    true);
    }

    if (skin_data) {
        ri.FS_FreeFile(skin_data);
    }
    if (!model->batches) {
        M2_FreeModelData(model);
        ri.MemFree(model);
        return M2_CreateFallbackModel(modelFilename, using_legacy_view ? "legacy view produced no batches" : "skin produced no batches");
    }
    return model;
}

static LPTEXTURE M2_CharacterTextureForBatch(m2Model_t const *model,
                                             renderEntity_t const *entity,
                                             m2ModelBatch_t *batch,
                                             m2CharacterOutfit_t const *outfit) {
    m2Model_t *mutable_model;
    DWORD key;
    DWORD victim;
    PATHSTR texture_path;
    BOOL has_base_path;
    LPCOLOR32 pixels = NULL;
    LPTEXTURE base_texture = NULL;

    if (!model || !entity || !batch || !model->character_model) {
        return batch ? batch->texture : tr.texture[TEX_WHITE];
    }
    has_base_path = M2_CharacterTexturePathForType(model->filename,
                                                  entity->appearance,
                                                  batch->texture_type,
                                                  texture_path,
                                                  sizeof(texture_path));
    if (batch->texture_type != 1) {
        if (has_base_path) {
            key = entity->appearance ^ (entity->equipment * 16777619u);
            if (batch->character_texture && batch->character_texture_key == key) {
                return batch->character_texture;
            }
            if (batch->character_texture) {
                R_ReleaseTexture(batch->character_texture);
            }
            batch->character_texture = R_LoadTexture(texture_path);
            batch->character_texture_key = key;
            return batch->character_texture;
        }
        return batch->texture;
    }

    mutable_model = (m2Model_t *)model;
    key = entity->appearance ^ (entity->equipment * 16777619u);

    /* Look up composite cache — find existing or LRU victim */
    {
        DWORD lru = mutable_model->composite_cache_lru;
        victim = 0;
        for (DWORD i = 0; i < M2_COMPOSITE_CACHE_SIZE; i++) {
            if (mutable_model->composite_cache[i].texture &&
                mutable_model->composite_cache[i].key == key) {
                mutable_model->composite_cache_lru = lru | (1u << i);
                return mutable_model->composite_cache[i].texture;
            }
            if (!mutable_model->composite_cache[i].texture) {
                victim = i;
                break;
            }
            if (!(lru & (1u << i))) {
                victim = i;
            }
        }
        /* Evict victim if occupied */
        if (mutable_model->composite_cache[victim].texture) {
            R_ReleaseTexture(mutable_model->composite_cache[victim].texture);
        }
        mutable_model->composite_cache[victim].texture = NULL;
        mutable_model->composite_cache[victim].key = key;
        mutable_model->composite_cache_lru = lru | (1u << victim);
    }

    if (!outfit) {
        return batch->texture;
    }

    if (has_base_path) {
        base_texture = R_LoadTexture(texture_path);
    } else {
        for (m2ModelBatch_t *it = mutable_model->batches; it; it = it->next) {
            if (it->texture_type == 1 && it->texture) {
                base_texture = it->texture;
                break;
            }
        }
    }
    if (!base_texture || !M2_TexturePixels(base_texture, &pixels)) {
        return batch->texture;
    }

    FOR_LOOP(slot, M2_CHAR_TEX_COUNT) {
        M2_PasteOutfitComponent(pixels,
                                base_texture->width,
                                base_texture->height,
                                model->filename,
                                outfit,
                                (BYTE)slot);
    }

    {
        wowAppearance_t unpacked = Wow_UnpackAppearance(entity->appearance);
        M2_PasteHeadVariation(pixels, base_texture->width, base_texture->height,
                              model->filename, 1,
                              unpacked.faceID, unpacked.skinColorID);
        M2_PasteHeadVariation(pixels, base_texture->width, base_texture->height,
                              model->filename, 2,
                              unpacked.facialHairStyleID, unpacked.hairColorID);
    }

    {
        LPTEXTURE comp = R_AllocateTexture(base_texture->width, base_texture->height);
        R_LoadTextureMipLevel(comp, 0, pixels, base_texture->width, base_texture->height);
        ri.MemFree(pixels);
        mutable_model->composite_cache[victim].texture = comp;
        return comp ? comp : batch->texture;
    }
}

void M2_RenderModel(renderEntity_t const *entity, m2Model_t const *model, LPCMATRIX4 transform) {
    MATRIX3 normal_matrix;
    m2CharacterOutfit_t const *outfit = NULL;
    m2ModelBatch_t *batch;
    LPSHADER shader;

    if (!entity || !model || !transform) {
        return;
    }
    if (!Frustum_ContainsBox(&tr.viewDef.frustum, &model->bounds, transform)) return;

    shader = M2_Shader();
    M2_CalculateBoneMatrices(model, entity);
    M2_DrawParticles(model, entity, transform);
    M2_DrawRibbons(model, entity, transform);
    outfit = M2_CharacterOutfitForEntity(model, entity);
    Matrix3_normal(&normal_matrix, transform);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniform1i, shader->uLightCount, 0);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uTextureMatrix, 1, GL_FALSE, tr.viewDef.textureMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, transform->v);
    R_Call(glUniformMatrix4fv, shader->uLightMatrix, 1, GL_FALSE, tr.viewDef.lightMatrix.v);
    R_Call(glUniformMatrix3fv, shader->uNormalMatrix, 1, GL_TRUE, normal_matrix.v);
    {
        VECTOR3 lightDir = {
            -tr.viewDef.lightMatrix.v[2],
            -tr.viewDef.lightMatrix.v[6],
            -tr.viewDef.lightMatrix.v[10],
        };
        /* Match the old M2 shader which mixed mix(0.5, 1.0, v_light), i.e.
           0.5 ambient + 0.5 directional.  The unified shader uses
           uLightAmbient + uLightColor * dot(normal, uLightDir). */
        R_Call(glUniform3f, shader->uLightDir, lightDir.x * 1.2f, lightDir.y * 1.2f, lightDir.z * 1.2f);
        R_Call(glUniform3f, shader->uLightColor, 0.5f, 0.5f, 0.5f);
        R_Call(glUniform3f, shader->uLightAmbient, 0.5f, 0.5f, 0.5f);
    }
    /* The unified model shader transforms UVs through quat_transform using
     * uUvRot (default (0,0) collapses all UVs to 0.5).  Set identity defaults
     * for all UV/color/layer uniforms that M2 does not animate. */
    R_Call(glUniform4f, shader->uGeosetColor, 1.0f, 1.0f, 1.0f, 1.0f);
    R_Call(glUniform1f, shader->uLayerAlpha, 1.0f);
    R_Call(glUniform2f, shader->uUvTrans, 0.0f, 0.0f);
    R_Call(glUniform2f, shader->uUvRot, 0.0f, 1.0f);  /* identity quaternion */
    R_Call(glUniform2f, shader->uUvScale, 1.0f, 1.0f);
    R_Call(glUniform1i, shader->uUseDiscard, 0);
    R_Call(glUniform1i, shader->uUnshaded, 0);
    R_Call(glUniform1f, shader->uFogEnable, 0);
    R_Call(glUniform1f, shader->uFirstBoneLookupIndex, 0.0f);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glDisable, GL_BLEND);

	for (batch = model->batches; batch; batch = batch->next) {
		LPTEXTURE texture;

		if (!M2_CharacterGeosetVisible(model, outfit, batch->section_id)) {
			continue;
		}
		/* Alpha-key batches use the shared shader's discard path; opaque and
		 * blended batches must explicitly disable it after the previous batch. */
		R_Call(glUniform1i, shader->uUseDiscard, batch->alphamode == BLEND_MODE_ALPHAKEY);
		if (batch->alphamode == BLEND_MODE_NONE) {
			R_Call(glDisable, GL_BLEND);
			R_Call(glDepthMask, GL_TRUE);
		} else {
			R_Call(glEnable, GL_BLEND);
			R_Call(glDepthMask, GL_FALSE);
			switch (batch->alphamode) {
			case BLEND_MODE_ADD:
				R_Call(glBlendFunc, GL_ONE, GL_ONE);
				break;
			case BLEND_MODE_ADDALPHA:
				R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
				break;
			case BLEND_MODE_BLEND:
				R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				break;
			case BLEND_MODE_ALPHAKEY:
				R_Call(glDisable, GL_BLEND);
				R_Call(glDepthMask, GL_TRUE);
				break;
			default:
				R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				break;
			}
		}
		texture = M2_CharacterTextureForBatch(model, entity, batch, outfit);
		M2_UploadBatchBones(model, batch, shader);
		R_BindTexture(texture ? texture : tr.texture[TEX_WHITE], 0);
#ifdef USE_SHADOWMAPS
		R_BindTexture(tr.texture[TEX_SHADOWMAP], 1);
#endif
		R_BindTexture(tr.texture[TEX_WHITE], 2);
		R_DrawBuffer(batch->buffer, batch->num_vertices);
	}
}

BOOL M2_AttachmentMatrix(m2Model_t const *model,
                         DWORD attachment_id,
                         LPCMATRIX4 model_matrix,
                         LPMATRIX4 out) {
    BYTE const *attachments;
    WORD const *lookup;
    DWORD attachment_index = 0xFFFFu;
    WORD bone_index;
    VECTOR3 position;
    MATRIX4 local;

    if (!model || !model_matrix || !out || !model->bone_matrices || !model->bones) {
        return false;
    }

    attachments = M2_ModelArrayPtr(model, model->attachments, model->format->attachment_stride);
    if (!attachments || model->attachments.size <= 0) {
        return false;
    }

    lookup = M2_ModelArrayPtr(model, model->attachment_lookup, sizeof(*lookup));
    if (lookup && attachment_id < (DWORD)model->attachment_lookup.size) {
        attachment_index = lookup[attachment_id];
    }
    if (attachment_index >= (DWORD)model->attachments.size) {
        FOR_LOOP(i, (DWORD)model->attachments.size) {
            DWORD id = model->format->format == M2_FORMAT_CLASSIC
                ? ((m2AttachmentClassic_t const *)(attachments + i * model->format->attachment_stride))->attachment_id
                : ((m2AttachmentModern_t const *)(attachments + i * model->format->attachment_stride))->attachment_id;
            if (id == attachment_id) {
                attachment_index = i;
                break;
            }
        }
    }
    if (attachment_index >= (DWORD)model->attachments.size) {
        return false;
    }

    if (model->format->format == M2_FORMAT_CLASSIC) {
        m2AttachmentClassic_t const *attachment = (m2AttachmentClassic_t const *)(attachments + attachment_index * model->format->attachment_stride);
        bone_index = attachment->bone_index;
        position = attachment->position;
    } else {
        m2AttachmentModern_t const *attachment = (m2AttachmentModern_t const *)(attachments + attachment_index * model->format->attachment_stride);
        bone_index = attachment->bone_index;
        position = attachment->position;
    }

    if (bone_index >= model->bone_count) {
        return false;
    }

    local = model->bone_matrices[bone_index];
    Matrix4_translate(&local, &position);
    Matrix4_multiply(model_matrix, &local, out);
    return true;
}

FLOAT M2_GroundOffset(m2Model_t const *model) {
    if (!model || !model->has_geometry_bounds || model->geometry_bounds.min.z >= 0.0f) {
        return 0.0f;
    }
    return -model->geometry_bounds.min.z;
}

BOOL M2_IsCharacterModel(m2Model_t const *model) { return model && model->character_model; }

void M2_Release(m2Model_t *model) {
    m2ModelBatch_t *batch;

    if (!model) {
        return;
    }
    batch = model->batches;
    while (batch) {
        m2ModelBatch_t *next = batch->next;
        R_ReleaseVertexArrayObject(batch->buffer);
        if (batch->character_texture) {
            R_ReleaseTexture(batch->character_texture);
        }
        ri.MemFree(batch);
        batch = next;
    }
    for (DWORD i = 0; i < M2_COMPOSITE_CACHE_SIZE; i++) {
        if (model->composite_cache[i].texture) {
            R_ReleaseTexture(model->composite_cache[i].texture);
        }
    }
    M2_FreeModelData(model);
    ri.MemFree(model);
}

void M2_Shutdown(void) {
    M2_DbcShutdown(&m2_char_start_outfit_dbc);
    M2_DbcShutdown(&m2_item_display_info_dbc);
    M2_DbcShutdown(&m2_char_sections_dbc);
}
