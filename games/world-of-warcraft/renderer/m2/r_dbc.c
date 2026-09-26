#include "renderer/r_local.h"
#include "r_dbc.h"
#include "r_m2_utils.h"
#include "common/stb_dbc.h"
#include <strings.h>

#define M2_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Renderer adapts ri.* onto the shared cache's I/O table. */
static void *M2_DbcRead(cstring_t filename, uint32_t *size) {
    void *data = NULL;
    int s = ri.FS_ReadFile(filename, &data);
    if (size) *size = (uint32_t)(s > 0 ? s : 0);
    return s > 0 ? data : NULL;
}
static void M2_DbcFree(void *p) { ri.FS_FreeFile(p); }
static void *M2_DbcAlloc(size_t n) { return ri.MemAlloc((long)n); }
static void M2_DbcDealloc(void *p) { ri.MemFree(p); }
static stbDbcIO_t const m2_dbc_io = { M2_DbcRead, M2_DbcFree, M2_DbcAlloc, M2_DbcDealloc };

enum {
    M2_SLOT_NONE, M2_SLOT_HEAD, M2_SLOT_SHOULDERS, M2_SLOT_CHEST, M2_SLOT_SHIRT, M2_SLOT_BELT,
    M2_SLOT_LEGS, M2_SLOT_BOOTS, M2_SLOT_GLOVES, M2_SLOT_TABARD, M2_SLOT_CAPE, M2_SLOT_COUNT
};

typedef struct { uint32_t display_ids[4]; } m2EquipmentItem_t;
typedef struct { uint32_t race_id, gender_id; m2EquipmentItem_t items[256]; } m2EquipmentSlotItems_t;

static stbDbcCache_t char_start_outfit_dbc;
static stbDbcCache_t item_display_info_dbc;
static stbDbcCache_t char_sections_dbc;
static stbDbcCache_t creature_display_info_dbc;
static stbDbcCache_t creature_display_info_extra_dbc;
static stbDbcCache_t helmet_geoset_vis_dbc;
static m2CharSectionsLayout_t char_sections_layout;

/* Column→field schemas (field numbers in docs/dbc-reference.md). The struct mirrors *the consumed subset of a DBC row; each entry maps a DBC column index to a struct
 * field, and Stb_DbcParseRows fills the array with no per-field decode code. */

/* CreatureDisplayInfo: 0 = id, 3 = extended display info id. */
typedef struct { uint32_t id, extra_id; } m2CreatureDisplayInfoRec_t;
static stbDbcField_t const creature_display_info_schema[] = {
    { 0, offsetof(m2CreatureDisplayInfoRec_t, id),       STB_DBC_U32 },
    { 3, offsetof(m2CreatureDisplayInfoRec_t, extra_id), STB_DBC_U32 },
};

/* CreatureDisplayInfoExtra: 3-7 = skin/face/hair, 8-18 = 11 item display ids. */
typedef struct { uint32_t id, skin, face, hair_style, hair_color, facial_hair, display_ids[11]; } m2CreatureDisplayInfoExtraRec_t;
static stbDbcField_t const creature_display_info_extra_schema[] = {
    {  0, offsetof(m2CreatureDisplayInfoExtraRec_t, id),              STB_DBC_U32 },
    {  3, offsetof(m2CreatureDisplayInfoExtraRec_t, skin),            STB_DBC_U32 },
    {  4, offsetof(m2CreatureDisplayInfoExtraRec_t, face),            STB_DBC_U32 },
    {  5, offsetof(m2CreatureDisplayInfoExtraRec_t, hair_style),      STB_DBC_U32 },
    {  6, offsetof(m2CreatureDisplayInfoExtraRec_t, hair_color),      STB_DBC_U32 },
    {  7, offsetof(m2CreatureDisplayInfoExtraRec_t, facial_hair),     STB_DBC_U32 },
    {  8, offsetof(m2CreatureDisplayInfoExtraRec_t, display_ids),     STB_DBC_U32, 11 },
};

/* ItemDisplayInfo: model stems 1-2, model textures 3-4, geoset groups 7-9, flags 10,
 * HelmetGeosetVis 12-13, component textures 14-21 (classic 23-field). The 25-field
 * Wrath layout shifts the vis/texture block one column right. */
typedef struct {
    uint32_t id;
    cstring_t model[2], model_texture[2];
    uint32_t geoset_group[3], flags, helm_vis[2];
    cstring_t component_texture[8];
} m2ItemDisplayInfoRec_t;

static stbDbcField_t const item_display_info_classic_schema[] = {
    {  0, offsetof(m2ItemDisplayInfoRec_t, id),                   STB_DBC_U32 },
    {  1, offsetof(m2ItemDisplayInfoRec_t, model),                STB_DBC_STR, 2 },
    {  3, offsetof(m2ItemDisplayInfoRec_t, model_texture),        STB_DBC_STR, 2 },
    {  7, offsetof(m2ItemDisplayInfoRec_t, geoset_group),         STB_DBC_U32, 3 },
    { 10, offsetof(m2ItemDisplayInfoRec_t, flags),                STB_DBC_U32 },
    { 12, offsetof(m2ItemDisplayInfoRec_t, helm_vis),             STB_DBC_U32, 2 },
    { 14, offsetof(m2ItemDisplayInfoRec_t, component_texture),    STB_DBC_STR, 8 },
};

static stbDbcField_t const item_display_info_wrath_schema[] = {
    {  0, offsetof(m2ItemDisplayInfoRec_t, id),                   STB_DBC_U32 },
    {  1, offsetof(m2ItemDisplayInfoRec_t, model),                STB_DBC_STR, 2 },
    {  3, offsetof(m2ItemDisplayInfoRec_t, model_texture),        STB_DBC_STR, 2 },
    {  7, offsetof(m2ItemDisplayInfoRec_t, geoset_group),         STB_DBC_U32, 3 },
    { 10, offsetof(m2ItemDisplayInfoRec_t, flags),                STB_DBC_U32 },
    { 13, offsetof(m2ItemDisplayInfoRec_t, helm_vis),             STB_DBC_U32, 2 },
    { 15, offsetof(m2ItemDisplayInfoRec_t, component_texture),    STB_DBC_STR, 8 },
};

/* Pre-23-field guard: the offsets the loader used before the classic layout was
 * pinned (geoset 0-2, flags 0, textures 0-7). Unreachable for real archives. */
static stbDbcField_t const item_display_info_legacy_schema[] = {
    { 0, offsetof(m2ItemDisplayInfoRec_t, id),                   STB_DBC_U32 },
    { 1, offsetof(m2ItemDisplayInfoRec_t, model),                STB_DBC_STR, 2 },
    { 3, offsetof(m2ItemDisplayInfoRec_t, model_texture),        STB_DBC_STR, 2 },
    { 0, offsetof(m2ItemDisplayInfoRec_t, geoset_group),         STB_DBC_U32, 3 },
    { 0, offsetof(m2ItemDisplayInfoRec_t, flags),                STB_DBC_U32 },
    { 0, offsetof(m2ItemDisplayInfoRec_t, component_texture),    STB_DBC_STR, 8 },
};

static stbDbcField_t const *item_display_info_schema(uint32_t fields, uint32_t *count) {
    switch (m2_item_display_texture_base(fields)) {
        case 15: *count = M2_COUNT(item_display_info_wrath_schema);   return item_display_info_wrath_schema;
        case 14: *count = M2_COUNT(item_display_info_classic_schema); return item_display_info_classic_schema;
        default: *count = M2_COUNT(item_display_info_legacy_schema);  return item_display_info_legacy_schema;
    }
}

/* CharSections: variation-first (classic) or texture-first (stock Wrath). */
typedef struct { uint32_t id, race_id, gender, section, variation, color; cstring_t texture[3]; } m2CharSectionsRec_t;
static stbDbcField_t const char_sections_variation_first_schema[] = {
    { 0, offsetof(m2CharSectionsRec_t, id),         STB_DBC_U32 },
    { 1, offsetof(m2CharSectionsRec_t, race_id),    STB_DBC_U32 },
    { 2, offsetof(m2CharSectionsRec_t, gender),     STB_DBC_U32 },
    { 3, offsetof(m2CharSectionsRec_t, section),    STB_DBC_U32 },
    { 4, offsetof(m2CharSectionsRec_t, variation),  STB_DBC_U32 },
    { 5, offsetof(m2CharSectionsRec_t, color),      STB_DBC_U32 },
    { 6, offsetof(m2CharSectionsRec_t, texture),    STB_DBC_STR, 3 },
};
static stbDbcField_t const char_sections_texture_first_schema[] = {
    { 0, offsetof(m2CharSectionsRec_t, id),         STB_DBC_U32 },
    { 1, offsetof(m2CharSectionsRec_t, race_id),    STB_DBC_U32 },
    { 2, offsetof(m2CharSectionsRec_t, gender),     STB_DBC_U32 },
    { 3, offsetof(m2CharSectionsRec_t, section),    STB_DBC_U32 },
    { 4, offsetof(m2CharSectionsRec_t, texture),    STB_DBC_STR, 3 },
    { 8, offsetof(m2CharSectionsRec_t, variation),  STB_DBC_U32 },
    { 9, offsetof(m2CharSectionsRec_t, color),      STB_DBC_U32 },
};

/* CharStartOutfit: field 1 = packed race|class<<8|gender<<16 key,
 * 14-25 = display ids, 26-37 = inventory types (12 each). */
typedef struct { uint32_t id, key, display_id[12], inventory_type[12]; } m2CharStartOutfitRec_t;
static stbDbcField_t const char_start_outfit_schema[] = {
    {  0, offsetof(m2CharStartOutfitRec_t, id),             STB_DBC_U32 },
    {  1, offsetof(m2CharStartOutfitRec_t, key),            STB_DBC_U32 },
    { 14, offsetof(m2CharStartOutfitRec_t, display_id),     STB_DBC_U32, 12 },
    { 26, offsetof(m2CharStartOutfitRec_t, inventory_type), STB_DBC_U32, 12 },
};

/* HelmetGeosetVisData: per-race hide bitmasks (hair/facial[0..2]/ears). */
typedef struct { uint32_t id, hair_flags, facial_flags[3], ears_flags; } m2HelmetGeosetVisRec_t;
static stbDbcField_t const helmet_geoset_vis_schema[] = {
    { 0, offsetof(m2HelmetGeosetVisRec_t, id),           STB_DBC_U32 },
    { 1, offsetof(m2HelmetGeosetVisRec_t, hair_flags),   STB_DBC_U32 },
    { 2, offsetof(m2HelmetGeosetVisRec_t, facial_flags), STB_DBC_U32, 3 },
    { 5, offsetof(m2HelmetGeosetVisRec_t, ears_flags),   STB_DBC_U32 },
};

/* DBCs stay as one resident file image plus a decoded struct array and an FNV-1a
 * integer index; both are built lazily on first lookup (see stb_dbc.h cache). */

/* ---- typed record finders (load + decode + index lookup) ---- */

static m2CreatureDisplayInfoRec_t const *M2_CreatureDisplayInfo(uint32_t id) {
    int idx;
    if (!Stb_DbcCacheLoad(&creature_display_info_dbc, "DBFilesClient\\CreatureDisplayInfo.dbc", &m2_dbc_io)) return NULL;
    Stb_DbcCacheDecode(&creature_display_info_dbc, creature_display_info_schema, M2_COUNT(creature_display_info_schema), sizeof(m2CreatureDisplayInfoRec_t), &m2_dbc_io);
    idx = Stb_DbcCacheFindID(&creature_display_info_dbc, id, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(creature_display_info_dbc, m2CreatureDisplayInfoRec_t, idx);
}

static m2CreatureDisplayInfoExtraRec_t const *M2_CreatureDisplayInfoExtra(uint32_t id) {
    int idx;
    if (!Stb_DbcCacheLoad(&creature_display_info_extra_dbc, "DBFilesClient\\CreatureDisplayInfoExtra.dbc", &m2_dbc_io)) return NULL;
    Stb_DbcCacheDecode(&creature_display_info_extra_dbc, creature_display_info_extra_schema, M2_COUNT(creature_display_info_extra_schema), sizeof(m2CreatureDisplayInfoExtraRec_t), &m2_dbc_io);
    idx = Stb_DbcCacheFindID(&creature_display_info_extra_dbc, id, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(creature_display_info_extra_dbc, m2CreatureDisplayInfoExtraRec_t, idx);
}

static m2ItemDisplayInfoRec_t const *M2_ItemDisplayInfo(uint32_t id) {
    uint32_t count;
    stbDbcField_t const *schema;
    int idx;
    if (!Stb_DbcCacheLoad(&item_display_info_dbc, "DBFilesClient\\ItemDisplayInfo.dbc", &m2_dbc_io)) return NULL;
    if (!item_display_info_dbc.rows) {
        schema = item_display_info_schema(item_display_info_dbc.fields, &count);
        Stb_DbcCacheDecode(&item_display_info_dbc, schema, count, sizeof(m2ItemDisplayInfoRec_t), &m2_dbc_io);
    }
    idx = Stb_DbcCacheFindID(&item_display_info_dbc, id, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(item_display_info_dbc, m2ItemDisplayInfoRec_t, idx);
}

static m2CharStartOutfitRec_t const *M2_CharStartOutfit(uint32_t key) {
    int idx;
    if (!Stb_DbcCacheLoad(&char_start_outfit_dbc, "DBFilesClient\\CharStartOutfit.dbc", &m2_dbc_io)) return NULL;
    Stb_DbcCacheDecode(&char_start_outfit_dbc, char_start_outfit_schema, M2_COUNT(char_start_outfit_schema), sizeof(m2CharStartOutfitRec_t), &m2_dbc_io);
    idx = Stb_DbcCacheFindKey(&char_start_outfit_dbc, 1, key, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(char_start_outfit_dbc, m2CharStartOutfitRec_t, idx);
}

static m2HelmetGeosetVisRec_t const *M2_HelmetGeosetVis(uint32_t id) {
    int idx;
    if (!Stb_DbcCacheLoad(&helmet_geoset_vis_dbc, "DBFilesClient\\HelmetGeosetVisData.dbc", &m2_dbc_io)) return NULL;
    Stb_DbcCacheDecode(&helmet_geoset_vis_dbc, helmet_geoset_vis_schema, M2_COUNT(helmet_geoset_vis_schema), sizeof(m2HelmetGeosetVisRec_t), &m2_dbc_io);
    idx = Stb_DbcCacheFindID(&helmet_geoset_vis_dbc, id, &m2_dbc_io);
    return idx < 0 ? NULL : STB_DBC_ROW(helmet_geoset_vis_dbc, m2HelmetGeosetVisRec_t, idx);
}

/* Detect the CharSections layout from the raw records, then decode and return the
 * row array; NULL when the mounted schema is unsupported. */
static m2CharSectionsRec_t const *M2_CharSections(void) {
    uint32_t count;
    stbDbcField_t const *schema;
    if (!Stb_DbcCacheLoad(&char_sections_dbc, "DBFilesClient\\CharSections.dbc", &m2_dbc_io)) return NULL;
    if (!char_sections_layout) {
        char_sections_layout = char_sections_dbc.fields < 10 ? M2_CHAR_SECTIONS_INVALID
            : m2_char_sections_layout(char_sections_dbc.records_base, char_sections_dbc.records, char_sections_dbc.record_size);
        if (char_sections_layout == M2_CHAR_SECTIONS_INVALID)
            fprintf(stderr, "M2 DBC: unsupported CharSections schema (%u fields, %u-byte records)\n",
                    char_sections_dbc.fields, char_sections_dbc.record_size);
    }
    if (char_sections_layout == M2_CHAR_SECTIONS_INVALID) return NULL;
    if (!char_sections_dbc.rows) {
        schema = char_sections_layout == M2_CHAR_SECTIONS_TEXTURE_FIRST
            ? (count = M2_COUNT(char_sections_texture_first_schema), char_sections_texture_first_schema)
            : (count = M2_COUNT(char_sections_variation_first_schema), char_sections_variation_first_schema);
        Stb_DbcCacheDecode(&char_sections_dbc, schema, count, sizeof(m2CharSectionsRec_t), &m2_dbc_io);
    }
    return char_sections_dbc.rows;
}

bool M2_DbcCharacterRaceGender(cstring_t model_path, uint32_t *race_id, uint32_t *gender_id) {
    cstring_t character, race, gender;
    char race_name[64], gender_name[64];
    size_t length;
    if (!model_path || !race_id || !gender_id) return false;
    character = strcasestr(model_path, "Character\\");
    if (!character) character = strcasestr(model_path, "Character/");
    if (!character) return false;
    race = character + strlen("Character\\"); gender = strpbrk(race, "\\/");
    if (!gender || !(length = (size_t)(gender - race)) || length >= sizeof(race_name)) return false;
    memcpy(race_name, race, length); race_name[length] = '\0';
    gender++; length = strcspn(gender, "\\/.");
    if (!length || length >= sizeof(gender_name)) return false;
    memcpy(gender_name, gender, length); gender_name[length] = '\0';
    if (!(*race_id = Wow_RaceNumber(race_name))) return false;
    if (!strcasecmp(gender_name, "Male")) *gender_id = 0;
    else if (!strcasecmp(gender_name, "Female")) *gender_id = 1;
    else return false;
    return true;
}

/* CreatureDisplayInfoExtra is decoded once into stable appearance and item display IDs. */
bool M2_DbcResolveCreatureAppearance(uint32_t display_id, m2CreatureAppearance_t *out) {
    m2CreatureDisplayInfoRec_t const *display;
    m2CreatureDisplayInfoExtraRec_t const *extra;
    if (!display_id || !out) return false;
    memset(out, 0, sizeof(*out));
    display = M2_CreatureDisplayInfo(display_id);
    if (!display) return false;
    extra = display->extra_id ? M2_CreatureDisplayInfoExtra(display->extra_id) : NULL;
    if (!extra) return false;
    out->appearance = Wow_PackAppearance((uint8_t)extra->skin, (uint8_t)extra->face, (uint8_t)extra->hair_style,
                                         (uint8_t)extra->hair_color, (uint8_t)extra->facial_hair, 1, 0);
    /* All 11 NPC item display ids (columns 8-18) map to the classic slots; the
     * previous bound read only 10, leaving the cape slot (index 10) empty. */
    FOR_LOOP(i, sizeof(out->display_ids) / sizeof(out->display_ids[0]))
        out->display_ids[i] = extra->display_ids[i];
    return true;
}

/* Per wowdev DB:ItemDisplayInfo — for each slot, the three geosetGroup DBC fields
 * (indices 0/1/2 at fields 7/8/9) map to these geoset groups:
 *   LEGS: geosetGroup[0] → group 13 (trouser mesh), geosetGroup[1] → group 9 (kneepads) */
static uint32_t const slot_geoset_group_map[M2_SLOT_COUNT][3] = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 8, 0, 13 }, { 8, 0, 0 }, { 0, 0, 0 },
    { 13, 9, 0 }, { 5, 0, 0 }, { 4, 0, 0 }, { 0, 0, 0 }, { 15, 0, 0 },
};

static void M2_DbcAddDisplayInfo(m2CharacterOutfit_t *outfit, uint32_t display_id, uint32_t slot) {
    m2ItemDisplayInfoRec_t const *record;
    if (!outfit || !display_id || display_id == 0xffffffffu || slot == M2_SLOT_NONE || slot >= M2_SLOT_COUNT)
        return;
    record = M2_ItemDisplayInfo(display_id);
    if (!record) return;
    FOR_LOOP(i, 3) {
        uint32_t group = slot_geoset_group_map[slot][i];
        uint32_t geoset = group ? record->geoset_group[i] : 0;
        if (geoset) outfit->geoset[group] = geoset;
    }
    outfit->flags |= record->flags;
    /* Head and shoulder items carry attachment model name stems (fields 1/2); these
     * render as separate M2s at the character's helm/shoulder bones. The head slot
     * also carries the per-race/gender HelmetGeosetVisData ids (fields 12/13). */
    if (slot == M2_SLOT_HEAD) {
        outfit->helm_model = record->model[0];
        outfit->helm_texture = record->model_texture[0];
        outfit->helm_vis_id[0] = record->helm_vis[0];
        outfit->helm_vis_id[1] = record->helm_vis[1];
    }
    if (slot == M2_SLOT_SHOULDERS) {
        outfit->shoulder_model[0] = record->model[0];
        outfit->shoulder_model[1] = record->model[1];
        outfit->shoulder_texture[0] = record->model_texture[0];
        outfit->shoulder_texture[1] = record->model_texture[1];
    }
    /* A worn tabard activates the hanging tabard mesh (geoset group 12, section 1202). */
    if (slot == M2_SLOT_TABARD) outfit->geoset[12] = 2;
    FOR_LOOP(i, M2_CHAR_TEX_COMPONENT_COUNT) {
        cstring_t texture = record->component_texture[i];
        signed char priority = Wow_CharacterTexturePriority(slot, i);
        if (texture && *texture && priority >= 0) outfit->texture[i][priority] = texture;
    }
    if (slot == M2_SLOT_CAPE)
        outfit->cape_texture = record->model_texture[0];
}

static m2EquipmentItem_t const *M2_DbcEquipmentItem(m2EquipmentSlotItems_t const *lists, uint32_t count,
                                                  uint32_t race_id, uint32_t gender_id, uint8_t item_index) {
    FOR_LOOP(i, count)
        if (lists[i].race_id == race_id && lists[i].gender_id == gender_id) return &lists[i].items[item_index];
    return NULL;
}

static void M2_DbcAddEquipmentItem(m2CharacterOutfit_t *outfit, m2EquipmentSlotItems_t const *lists,
                                   uint32_t count, uint32_t race_id, uint32_t gender_id, uint8_t item_index, uint32_t slot) {
    m2EquipmentItem_t const *item = M2_DbcEquipmentItem(lists, count, race_id, gender_id, item_index);
    if (!item) return;
    FOR_LOOP(i, 4) M2_DbcAddDisplayInfo(outfit, item->display_ids[i], slot);
}

static void M2_DbcApplyEquipment(m2CharacterOutfit_t *outfit, uint32_t race_id, uint32_t gender_id, uint32_t equipment) {
    static m2EquipmentSlotItems_t const upper[] = { { 2, 0, { [1] = { { 27274 } } } } };
    static m2EquipmentSlotItems_t const lower[] = { { 2, 0, { [1] = { { 27275 } } } } };
    static m2EquipmentSlotItems_t const hands[] = { { 2, 0, { [1] = { { 27271 } } } } };
    static m2EquipmentSlotItems_t const feet[] = { { 2, 0, { [1] = { { 27270 } } } } };
    wowEquipment_t items = Wow_UnpackEquipment(equipment);
    M2_DbcAddEquipmentItem(outfit, upper, 1, race_id, gender_id, items.upperBodyItem, M2_SLOT_CHEST);
    M2_DbcAddEquipmentItem(outfit, lower, 1, race_id, gender_id, items.lowerBodyItem, M2_SLOT_LEGS);
    M2_DbcAddEquipmentItem(outfit, hands, 1, race_id, gender_id, items.handItem, M2_SLOT_GLOVES);
    M2_DbcAddEquipmentItem(outfit, feet, 1, race_id, gender_id, items.footItem, M2_SLOT_BOOTS);
}

static bool M2_DbcStartOutfit(cstring_t model_path, uint32_t appearance, m2CharacterOutfit_t *outfit) {
    uint32_t race_id, gender_id, class_id, key;
    m2CharStartOutfitRec_t const *record;
    wowAppearance_t unpacked;
    if (!outfit || !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id)) return false;
    unpacked = Wow_UnpackAppearance(appearance); class_id = unpacked.classID ? unpacked.classID : 1;
    key = race_id | (class_id << 8) | (gender_id << 16);
    record = M2_CharStartOutfit(key);
    if (!record) return false;
    memset(outfit, 0, sizeof(*outfit));
    FOR_LOOP(i, 12) {
        uint32_t display_id = record->display_id[i];
        uint32_t slot = Wow_CharacterSlotForInventoryType(record->inventory_type[i]);
        M2_DbcAddDisplayInfo(outfit, display_id, slot);
    }
    return true;
}

/* Resolve a HelmetGeosetVisData record to the geoset-hide bitmask for a race.
 * Classic stores hairFlags, facialFlags[0..2], earsFlags as race bitmasks; a set
 * race bit hides that geoset group (hair 0, beard 1, sideburns 2, moustache 3,
 * ears 7 — wowdev "Character Customization" geoset numbering). */
static uint32_t M2_DbcHelmetHideMask(uint32_t vis_id, uint32_t race_id) {
    m2HelmetGeosetVisRec_t const *record;
    uint32_t mask = 0;
    if (!vis_id) return 0;
    record = M2_HelmetGeosetVis(vis_id);
    if (!record) return 0;
    if (record->hair_flags & (1u << race_id)) mask |= M2_HELM_HIDE_HAIR;
    if (record->facial_flags[0] & (1u << race_id)) mask |= M2_HELM_HIDE_BEARD;
    if (record->facial_flags[1] & (1u << race_id)) mask |= M2_HELM_HIDE_SIDEBURNS;
    if (record->facial_flags[2] & (1u << race_id)) mask |= M2_HELM_HIDE_MOUSTACHE;
    if (record->ears_flags & (1u << race_id)) mask |= M2_HELM_HIDE_EARS;
    return mask;
}

bool M2_DbcCharacterOutfit(cstring_t model_path, uint32_t appearance, uint32_t equipment,
                           m2CreatureAppearance_t const *creature, m2CharacterOutfit_t *outfit) {
    uint32_t race_id, gender_id;
    if (!outfit || !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id)) return false;
    if (creature) {
        memset(outfit, 0, sizeof(*outfit));
        FOR_LOOP(i, sizeof(creature->display_ids) / sizeof(creature->display_ids[0]))
            M2_DbcAddDisplayInfo(outfit, creature->display_ids[i], Wow_CharacterCreatureItemSlot(i));
        outfit->helm_hide = M2_DbcHelmetHideMask(outfit->helm_vis_id[gender_id], race_id);
        return true;
    }
    if (!M2_DbcStartOutfit(model_path, appearance, outfit)) return false;
    M2_DbcApplyEquipment(outfit, race_id, gender_id, equipment);
    outfit->helm_hide = M2_DbcHelmetHideMask(outfit->helm_vis_id[gender_id], race_id);
    return true;
}

bool M2_DbcCharacterVariationTexturePath(cstring_t model_path, uint32_t section_index, uint32_t variation_index,
                                         uint32_t color_index, uint32_t texture_index, string_t out, uint32_t out_size) {
    uint32_t race_id, gender_id;
    m2CharSectionsRec_t const *rows;
    if (!out || !out_size || texture_index >= 3 ||
        !M2_DbcCharacterRaceGender(model_path, &race_id, &gender_id)) return false;
    rows = M2_CharSections();
    if (!rows) return false;
    FOR_LOOP(i, char_sections_dbc.records) {
        m2CharSectionsRec_t const *record = &rows[i];
        cstring_t texture;
        if (record->race_id != race_id || record->gender != gender_id || record->section != section_index ||
            record->variation != variation_index || record->color != color_index) continue;
        texture = record->texture[texture_index];
        if (texture && *texture) { snprintf(out, out_size, "%s", texture); return true; }
        /* Classic male hair rows intentionally omit strings; the archive naming contract supplies the DBC-selected color. */
        if (section_index == 3 && gender_id == 0 && texture_index == 0) {
            PATHSTR derived;
            if (m2_classic_hair_texture_path(model_path, color_index, derived)) {
                snprintf(out, out_size, "%s", derived); return true;
            }
        }
    }
    return false;
}

bool M2_DbcCharacterTexturePathForType(cstring_t model_path, uint32_t appearance, uint32_t texture_type,
                                       string_t out, uint32_t out_size) {
    wowAppearance_t a = Wow_UnpackAppearance(appearance);
    switch (texture_type) {
        case 1: return M2_DbcCharacterVariationTexturePath(model_path, 0, 0, a.skinColorID, 0, out, out_size);
        case 2: return M2_DbcCharacterVariationTexturePath(model_path, 4, 0, a.skinColorID, 0, out, out_size);
        case 6:
            return M2_DbcCharacterVariationTexturePath(model_path, 3, a.hairStyleID, a.hairColorID, 0, out, out_size);
        case 8: return M2_DbcCharacterVariationTexturePath(model_path, 0, 0, a.skinColorID, 1, out, out_size);
        default: return false;
    }
}

void M2_DbcShutdown(void) {
    Stb_DbcCacheFree(&char_start_outfit_dbc, &m2_dbc_io);
    Stb_DbcCacheFree(&item_display_info_dbc, &m2_dbc_io);
    Stb_DbcCacheFree(&char_sections_dbc, &m2_dbc_io);
    Stb_DbcCacheFree(&creature_display_info_dbc, &m2_dbc_io);
    Stb_DbcCacheFree(&creature_display_info_extra_dbc, &m2_dbc_io);
    Stb_DbcCacheFree(&helmet_geoset_vis_dbc, &m2_dbc_io);
    char_sections_layout = M2_CHAR_SECTIONS_UNKNOWN;
}
